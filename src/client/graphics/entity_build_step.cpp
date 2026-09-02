#include "client/graphics/entity_renderer.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/entity_prep_worker.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/light_source.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/dds_decoder.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/animation_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>
#include <chrono>
#include <fstream>
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

bool EntityRenderer::processOneEntityBuildStep(size_t pvsRegion) {
    if (!smgr_ || !raceModelLoader_) return false;

    // Find nearest unbuilt entity that's ready for work
    float playerX = 0, playerY = 0;
    if (playerSpawnId_ != 0) {
        auto playerIt = entities_.find(playerSpawnId_);
        if (playerIt != entities_.end()) {
            playerX = playerIt->second.lastX;
            playerY = playerIt->second.lastY;
        }
    }

    // Collect entities in active phases first (in-progress), then Placeholder
    uint16_t bestId = 0;
    float bestDistSq = std::numeric_limits<float>::max();
    EntityBuildPhase bestPhase = EntityBuildPhase::Built;

    for (auto& [spawnId, vis] : entities_) {
        // Use buildPhase instead of meshBuilt — entity may have mesh built but still
        // need equipment texture upload + attach in later phases
        if (vis.buildPhase == EntityBuildPhase::Built) continue;

        // In-progress = any phase past Placeholder
        bool inProgress = (vis.buildPhase != EntityBuildPhase::Placeholder);

        // Only build entities that are currently visible (in scene graph).
        // Culled entities (distance/frustum/portal/occlusion) stay as placeholders.
        // Exception: entities already in-progress — finish them to avoid wasted partial work.
        if (!inProgress && !vis.inSceneGraph) continue;

        // PVS region gate: only build entities near the player's region.
        // Before PVS is active (pvsRegion == SIZE_MAX), skip all non-in-progress entities
        // — entity loading must wait until BSP/PVS culling has run.
        // Entities with entityPrepComplete (already PVS-depth-gated by prep system)
        // are eligible regardless of exact BSP region. Without prep, require same-region.
        if (!inProgress && spawnId != playerSpawnId_) {
            if (pvsRegion == SIZE_MAX) continue;
            if (!vis.entityPrepComplete && vis.cachedBspRegion != pvsRegion) continue;
        }

        float dx = vis.lastX - playerX;
        float dy = vis.lastY - playerY;
        float distSq = dx * dx + dy * dy;

        // Prefer in-progress over placeholder, then closer over farther
        if (inProgress && bestPhase == EntityBuildPhase::Placeholder) {
            bestId = spawnId;
            bestDistSq = distSq;
            bestPhase = vis.buildPhase;
        } else if (inProgress == (bestPhase != EntityBuildPhase::Placeholder)) {
            if (distSq < bestDistSq) {
                bestId = spawnId;
                bestDistSq = distSq;
                bestPhase = vis.buildPhase;
            }
        } else if (!inProgress && bestPhase == EntityBuildPhase::Built) {
            bestId = spawnId;
            bestDistSq = distSq;
            bestPhase = vis.buildPhase;
        }
    }

    // Always prioritize the player entity over distance-based selection
    if (playerSpawnId_ != 0) {
        auto playerIt = entities_.find(playerSpawnId_);
        if (playerIt != entities_.end() &&
            playerIt->second.buildPhase != EntityBuildPhase::Built) {
            bestId = playerSpawnId_;
        }
    }

    if (bestId == 0) return false;

    auto& vis = entities_[bestId];
    auto entryPhase = vis.buildPhase;
    auto stepStart = std::chrono::steady_clock::now();

    static auto phaseName = [](EntityBuildPhase p) -> const char* {
        switch (p) {
            case EntityBuildPhase::Placeholder: return "Placeholder";
            case EntityBuildPhase::TextureUploading: return "TextureUpload";
            case EntityBuildPhase::VariantTextureUploading: return "VariantTexUpload";
            case EntityBuildPhase::SceneNodeCreation: return "SceneNodeCreate";
            case EntityBuildPhase::NodeSetup: return "NodeSetup";
            case EntityBuildPhase::MeshFinalize: return "MeshFinalize";
            case EntityBuildPhase::EquipTextureUploading: return "EquipTexUpload";
            case EntityBuildPhase::EquipmentAttach: return "EquipAttach";
            case EntityBuildPhase::Built: return "Built";
        }
        return "Unknown";
    };

    // RAII logger — fires when processOneEntityBuildStep returns
    struct PhaseTimer {
        uint16_t id; const char* name; EntityBuildPhase entry; EntityBuildPhase* current;
        std::chrono::steady_clock::time_point start;
        ~PhaseTimer() {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            LOG_INFO(MOD_GRAPHICS, "EntityBuild: {} ({}) {} -> {} in {}us",
                     id, name, phaseName(entry), phaseName(*current), us);
        }
    } phaseTimer{bestId, vis.name.c_str(), entryPhase, &vis.buildPhase, stepStart};

    for (;;) { // Loop to fall through trivial phase transitions without wasting frames
    switch (vis.buildPhase) {
        case EntityBuildPhase::Placeholder: {
            // Wait for background prep to complete (both race model AND per-entity data)
            if (!vis.entityPrepComplete) {
                // Check if at least the race model data is cached (for entities that
                // were registered before the extended prep system was active)
                uint32_t cacheKey = (static_cast<uint32_t>(vis.raceId) << 8) | vis.gender;
                if (!raceModelLoader_->isModelDataCached(cacheKey)) {
                    return false;  // Background thread still working
                }
                // Race model is cached but entityPrepComplete not set — this entity
                // was queued with old-style prep. Mark it complete so we can proceed.
                vis.entityPrepComplete = true;
            }

            // Promote prepared data to main cache
            raceModelLoader_->promotePreparedModels();

            auto modelData = raceModelLoader_->getRaceModelData(vis.raceId, vis.gender);
            if (!modelData) {
                // No model data — fallback to synchronous placeholder build
                buildEntityMesh(bestId);
                return true;
            }

            // T13: Textures already in constrained cache via getOrLoad() (T05/T06/T07).
            // Skip texture decode/upload phases — go straight to scene node creation.
            vis.buildPhase = EntityBuildPhase::SceneNodeCreation;
            continue;
        }

        case EntityBuildPhase::TextureUploading: {
            // T13: Phase skipped — textures in constrained cache.
            vis.buildPhase = EntityBuildPhase::SceneNodeCreation;
            continue;
        }

        case EntityBuildPhase::VariantTextureUploading: {
            // T11: Variant textures already in constrained cache via getOrLoad() (T06).
            // Skip directly to scene node creation.
            vis.buildPhase = EntityBuildPhase::SceneNodeCreation;
            continue;
        }

        case EntityBuildPhase::SceneNodeCreation: {
            // Frame 1/3: Create node + set position/rotation/scale.
            // This is the heaviest frame — mesh creation (~10-15ms).
            // Materials and name tags are deferred to NodeSetup and MeshFinalize.

            // T11: Upload any decoded textures to GPU before mesh building.
            // During gameplay, only this entity's textures should be pending.
            if (constrainedTextureCache_) {
                constrainedTextureCache_->uploadAllPending();
            }

            // Promote any background-preloaded model data
            if (entityPrepWorker_ && raceModelLoader_) {
                raceModelLoader_->promotePreparedModels();
            }

            // Remove con-colored placeholder cube
            if (vis.usesPlaceholder && vis.sceneNode) {
                vis.sceneNode->remove();
                vis.sceneNode->drop();
                vis.sceneNode = nullptr;
                vis.meshNode = nullptr;
                vis.usesPlaceholder = false;
            }

            uint16_t raceId = vis.raceId;
            uint8_t gender = vis.gender;

            float scale = getScaleForRace(raceId);
            if (scale <= 0.0f) {
                vis.meshBuilt = true;
                vis.buildPhase = EntityBuildPhase::Built;
                return true;
            }

            scale = entityScaleForServerSize(scale, vis.serverSize);

            // Temporarily clear weapon equipment IDs so createAnimatedNodeWithEquipment
            // doesn't bake weapon geometry — weapons are attached in EquipmentAttach phase
            uint32_t savedPrimary = vis.appearance.equipment[7];
            uint32_t savedSecondary = vis.appearance.equipment[8];
            vis.appearance.equipment[7] = 0;
            vis.appearance.equipment[8] = 0;

            // Try animated mesh first
            EQAnimatedMeshSceneNode* animNode = nullptr;
            if (raceModelLoader_) {
                bool hasEquipment = false;
                for (int i = 0; i < 9; i++) {
                    if (vis.appearance.equipment[i] != 0) {
                        hasEquipment = true;
                        break;
                    }
                }
                if (hasEquipment || vis.appearance.face != 0 || vis.appearance.texture != 0) {
                    animNode = raceModelLoader_->createAnimatedNodeWithEquipment(
                        raceId, gender, vis.appearance, smgr_->getRootSceneNode());
                } else {
                    animNode = raceModelLoader_->createAnimatedNode(raceId, gender, smgr_->getRootSceneNode());
                }
            }

            // Restore weapon equipment IDs for EquipmentAttach phase
            vis.appearance.equipment[7] = savedPrimary;
            vis.appearance.equipment[8] = savedSecondary;

            if (animNode) {
                vis.animatedNode = animNode;
                vis.sceneNode = animNode;
                vis.sceneNode->grab();
                vis.inSceneGraph = true;
                vis.isAnimated = true;
                vis.usesPlaceholder = false;

                if (vis.isPlayer) {
                    animNode->setIsPlayerNode(true);
                }
                animNode->forceAnimationUpdate();

                // Collision offset: origin-to-feet, not half the bbox height
                vis.modelYOffset = 0;
                vis.collisionZOffset =
                    collisionZOffsetFromBounds(animNode->getBoundingBox(), scale);

                // Position, rotation, scale
                animNode->setPosition(irr::core::vector3df(vis.lastX, vis.lastZ + vis.modelYOffset, vis.lastY));
                float visualHeading = -vis.lastHeading;
                animNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
                animNode->setScale(irr::core::vector3df(scale, scale, scale));
            } else {
                // Fall back to static mesh
                irr::scene::IMesh* mesh = getMeshForRace(raceId, gender, vis.appearance);
                if (!mesh) {
                    // No model at all — fall back to synchronous buildEntityMesh
                    buildEntityMesh(bestId);
                    return true;
                }

                bool usesPlaceholder = (placeholderMeshCache_.find(raceId) != placeholderMeshCache_.end() &&
                    placeholderMeshCache_[raceId] == mesh);
                vis.usesPlaceholder = usesPlaceholder;
                vis.isAnimated = false;

                vis.meshNode = smgr_->addMeshSceneNode(mesh);
                if (!vis.meshNode) {
                    return false;
                }
                vis.sceneNode = vis.meshNode;
                vis.sceneNode->grab();
                vis.inSceneGraph = true;

                // Collision offset: origin-to-feet, not half the bbox height
                vis.modelYOffset = 0;
                vis.collisionZOffset =
                    collisionZOffsetFromBounds(mesh->getBoundingBox(), scale);

                // Position, rotation, scale
                vis.meshNode->setPosition(irr::core::vector3df(vis.lastX, vis.lastZ + vis.modelYOffset, vis.lastY));
                float visualHeading = -vis.lastHeading;
                vis.meshNode->setRotation(irr::core::vector3df(0, visualHeading, 0));
                vis.meshNode->setScale(irr::core::vector3df(scale, scale, scale));
            }

            vis.buildPhase = EntityBuildPhase::NodeSetup;
            return true;
        }

        case EntityBuildPhase::NodeSetup: {
            // Frame 2/3: Set materials, animation speed, player highlight.
            // Node was created in SceneNodeCreation — now configure rendering properties.

            irr::scene::ISceneNode* node = vis.sceneNode;
            if (!node) {
                // Shouldn't happen — node was created in Frame 1
                vis.buildPhase = EntityBuildPhase::MeshFinalize;
                continue;  // trivial transition — fall through
            }

            // Set material properties
            for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                auto& mat = node->getMaterial(i);
                mat.BackfaceCulling = false;
                if (shaderMaterialSolid_ >= 0) {
                    if (mat.MaterialType == irr::video::EMT_SOLID) {
                        mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                    } else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
                        applyAlphaTestMaterialType(mat);
                    }
                    mat.Lighting = false;
                } else {
                    mat.Lighting = lightingEnabled_;
                    if (lightingEnabled_) {
                        mat.NormalizeNormals = true;
                        mat.AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        mat.DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                }
            }

            // Apply animation speed for animated nodes
            if (vis.animatedNode) {
                vis.animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed_);
            }

            // Player highlight
            if (vis.isPlayer) {
                for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
                    node->getMaterial(i).EmissiveColor = irr::video::SColor(255, 50, 50, 100);
                }
            }

            vis.buildPhase = EntityBuildPhase::MeshFinalize;
            return true;
        }

        case EntityBuildPhase::MeshFinalize: {
            // Frame 3/3: Name tag, corpse animation, light level, race ref tracking,
            // and decide whether equipment phases are needed.

            // Read scale back from the node (set in SceneNodeCreation)
            float scale = vis.sceneNode ? vis.sceneNode->getScale().X : 1.0f;

            // Create name tag
            if (nameTagsVisible_ && !vis.name.empty() && vis.sceneNode) {
                std::wstring wname = EQT::toDisplayNameW(vis.name);
                float nameHeight;
                if (vis.isAnimated) {
                    nameHeight = 5.5f * scale;
                } else {
                    nameHeight = vis.usesPlaceholder ? 10.5f : 5.5f;
                    nameHeight *= scale;
                }
                irr::video::SColor nameColor = vis.isPlayer
                    ? irr::video::SColor(255, 100, 200, 255)
                    : irr::video::SColor(255, 255, 255, 255);
                vis.nameNode = smgr_->addTextSceneNode(
                    smgr_->getGUIEnvironment()->getBuiltInFont(),
                    wname.c_str(), nameColor, vis.sceneNode,
                    irr::core::vector3df(0, nameHeight, 0));
            }

            // Boat collision (static mesh only, race 72=Ship, 73=Launch)
            if (!vis.isAnimated && vis.meshNode && (vis.raceId == 72 || vis.raceId == 73)) {
                vis.hasCollision = true;
                irr::core::aabbox3df collBbox = vis.meshNode->getMesh()->getBoundingBox();
                float bboxWidth = std::max(collBbox.MaxEdge.X - collBbox.MinEdge.X,
                                           collBbox.MaxEdge.Z - collBbox.MinEdge.Z);
                vis.collisionRadius = (bboxWidth / 2.0f) * scale;
                vis.collisionHeight = (collBbox.MaxEdge.Y - collBbox.MinEdge.Y) * scale;
                vis.deckZ = vis.lastZ + (vis.collisionHeight / 2.0f);
                registerBoat(bestId);
            }

            // Track race model reference for cache eviction
            if (raceModelLoader_) {
                raceModelLoader_->addMeshRef(vis.raceId, vis.gender);
            }

            // Corpse death animation (animated only)
            if (vis.isCorpse && vis.animatedNode) {
                bool hasD05 = vis.animatedNode->hasAnimation("d05");
                bool hasD01 = vis.animatedNode->hasAnimation("d01");
                bool hasD02 = vis.animatedNode->hasAnimation("d02");

                std::string deathAnim;
                if (hasD05) deathAnim = "d05";
                else if (hasD02) deathAnim = "d02";
                else if (hasD01) deathAnim = "d01";

                if (!deathAnim.empty()) {
                    vis.animatedNode->playAnimation(deathAnim, false, false);
                    SkeletalAnimator& animator = vis.animatedNode->getAnimator();
                    animator.setToLastFrame();
                    vis.animatedNode->forceAnimationUpdate();
                    vis.corpsePositionAdjusted = true;
                    vis.corpseYOffset = 0.0f;
                    vis.currentAnimation = deathAnim;
                }
            }

            // Apply queued light level
            if (vis.lightLevel != 0) {
                setEntityLight(bestId, vis.lightLevel);
            }

            vis.meshBuilt = true;

            // T13: equipmentTextures removed. Equipment textures in constrained cache.
            // Decide next phase based on equipment staging data.
            if (!vis.equipmentStaging.empty()) {
                vis.buildPhase = EntityBuildPhase::EquipmentAttach;
            } else if (vis.appearance.equipment[7] != 0 || vis.appearance.equipment[8] != 0) {
                vis.buildPhase = EntityBuildPhase::EquipmentAttach;
            } else {
                vis.buildPhase = EntityBuildPhase::Built;
            }
            return true;
        }

        case EntityBuildPhase::EquipTextureUploading: {
            // T11: Equipment textures already in constrained cache via getOrLoad() (T07).
            // Skip directly to equipment attach.
            vis.buildPhase = EntityBuildPhase::EquipmentAttach;
            continue;
        }

        case EntityBuildPhase::EquipmentAttach: {
            // Cache pre-extracted equipment model data so getEquipmentMeshByModelId finds it
            if (equipmentModelLoader_) {
                for (const auto& staging : vis.equipmentStaging) {
                    if (staging.geometry) {
                        auto equipData = std::make_shared<EquipmentModelData>();
                        equipData->modelId = staging.modelId;
                        equipData->modelName = "IT" + std::to_string(staging.modelId);
                        equipData->geometry = staging.geometry;
                        equipData->textures = staging.rawTextures;
                        for (const auto& texName : staging.geometry->textureNames()) {
                            equipData->textureNames.push_back(texName);
                        }
                        equipmentModelLoader_->cacheEquipmentModelData(staging.modelId, equipData);
                    }
                }
            }

            // Now attach equipment using the normal path (which will find cached data)
            attachEquipment(vis);

            // Apply corpse death animation to equipment nodes if needed
            if (vis.isCorpse && vis.animatedNode) {
                // Equipment nodes are children and inherit parent transform
            }

            // Apply light level if set
            if (vis.lightLevel != 0) {
                setEntityLight(bestId, vis.lightLevel);
            }

            // Clean up staging data (free memory)
            vis.equipmentStaging.clear();
            vis.equipmentStaging.shrink_to_fit();

            vis.buildPhase = EntityBuildPhase::Built;
            LOG_DEBUG(MOD_GRAPHICS, "Entity {} ({}) equipment attached, build complete", bestId, vis.name);
            return true;
        }

        case EntityBuildPhase::Built:
            return false;
    }

    return false;
    } // end for(;;) fall-through loop
}

} // namespace Graphics
} // namespace EQT
