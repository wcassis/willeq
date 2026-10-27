#include "client/graphics/entity_renderer.h"
#include "client/graphics/corpse_death_pose.h"  // T456 (B477): the ONE death-pose ladder
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

// T412 (board B291): the single seam through which the entity build path
// promotes its decoded textures to the GPU, and the only reader of
// skipTextureUpload_. The knob arrives from config/debug.json's
// "skipEntityTextureUpload" via ConstrainedRendererConfig and
// IrrlichtRenderer::initDeferredGraphics; before this it was stored and never
// read, so setting it in the shipped debug config changed nothing.
//
// Skipping leaves the decoded entries in the cache untouched (they are NOT
// dropped), so a run that clears the flag later uploads them normally. The
// entities themselves still build -- they draw untextured, which is exactly
// what a "skip glTexImage2D to isolate upload cost" debug flag is for.
int EntityRenderer::uploadPendingEntityTextures() {
    if (!constrainedTextureCache_) return 0;
    if (skipTextureUpload_) {
        LOG_DEBUG(MOD_GRAPHICS,
                  "skipEntityTextureUpload set -- entity texture upload skipped");
        return 0;
    }
    return constrainedTextureCache_->uploadAllPending();
}

namespace {
// B973: the scheduler predicts a unit's cost by what the unit does.
EntityBuildUnitKind unitKindForPhase(EntityBuildPhase phase) {
    switch (phase) {
        case EntityBuildPhase::MeshFinalize: return EntityBuildUnitKind::Finalize;
        case EntityBuildPhase::EquipTextureUploading:
        case EntityBuildPhase::EquipmentAttach: return EntityBuildUnitKind::Equip;
        default: return EntityBuildUnitKind::Create;
    }
}
}  // namespace

bool EntityRenderer::hasPendingTextureUploads() const {
    return constrainedTextureCache_ && !skipTextureUpload_ &&
           constrainedTextureCache_->pendingUploadCount() > 0;
}

bool EntityRenderer::processOnePendingTextureUpload(EntityBuildScheduler* scheduler) {
    if (!hasPendingTextureUploads()) return false;
    if (scheduler && !scheduler->mayStartUnit(EntityBuildUnitKind::Upload)) return false;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    // The cache lock is shared with the prep worker's getOrLoad(); this first
    // locked call measures how long the render thread waited for it.
    const size_t pendingBefore = constrainedTextureCache_->pendingUploadCount();
    const auto t1 = Clock::now();
    const int uploaded = constrainedTextureCache_->uploadPending(1);
    const auto t2 = Clock::now();
    const float lockMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
    const float ms = std::chrono::duration<float, std::milli>(t2 - t0).count();
    if (scheduler) scheduler->recordUnit(EntityBuildUnitKind::Upload, ms);
    if (ms >= 20.0f) {
        LOG_INFO(MOD_GRAPHICS, "EntityBuild: upload unit {} tex in {:.2f} ms (lock_wait={:.2f} ms upload={:.2f} ms, "
                 "{} pending before)", uploaded, ms, lockMs, ms - lockMs, pendingBefore);
    } else {
        LOG_DEBUG(MOD_GRAPHICS, "EntityBuild: upload unit {} tex in {:.2f} ms ({} pending before)", uploaded, ms,
                  pendingBefore);
    }
    return uploaded > 0;
}

EntityBuildBacklog EntityRenderer::countBuildBacklog() const {
    EntityBuildBacklog b;
    if (constrainedTextureCache_)
        b.pendingUploads = static_cast<uint32_t>(constrainedTextureCache_->pendingUploadCount());
    for (const auto& [spawnId, vis] : entities_) {
        (void)spawnId;
        if (vis.buildPhase == EntityBuildPhase::Built) {
            b.built++;
            continue;
        }
        b.unbuiltTotal++;
        const bool inProgress = vis.buildPhase != EntityBuildPhase::Placeholder;
        if (inProgress) b.inProgress++;
        if (!inProgress && !vis.inSceneGraph) continue;
        b.unbuiltVisible += inProgress ? 0 : 1;
        if (!inProgress && !vis.entityPrepComplete) b.awaitingPrep++;
        const bool weapons = vis.appearance.equipment[7] != 0 || vis.appearance.equipment[8] != 0 ||
                             !vis.equipmentStaging.empty();
        switch (vis.buildPhase) {
            case EntityBuildPhase::MeshFinalize: b.pendingUnits += weapons ? 2 : 1; break;
            case EntityBuildPhase::EquipTextureUploading:
            case EntityBuildPhase::EquipmentAttach: b.pendingUnits += 1; break;
            default: b.pendingUnits += weapons ? 3 : 2; break;   // create (+setup), finalize
        }
    }
    return b;
}

bool EntityRenderer::processOneEntityBuildStep(size_t pvsRegion, uint16_t priorityTargetId,
                                               EntityBuildScheduler* scheduler) {
    if (!smgr_ || !raceModelLoader_) return false;
    // B973: the legacy A/B keeps the pre-B973 selection and the separate
    // NodeSetup frame too, so the "before" column is the old code path.
    const bool legacy = scheduler && scheduler->legacy();

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
    bool targetEligible = false;

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

        // B973: a Placeholder whose model is not ready yet cannot advance
        // (the Placeholder case below returns without work). Choosing it
        // anyway made the nearest not-ready entity block every ready one
        // behind it for as long as its prep took.
        if (!legacy && !inProgress && !vis.entityPrepComplete) {
            const uint32_t key = (static_cast<uint32_t>(vis.raceId) << 8) | vis.gender;
            if (!raceModelLoader_->isModelDataCached(key)) continue;
        }
        if (!legacy && priorityTargetId != 0 && spawnId == priorityTargetId && !inProgress)
            targetEligible = true;

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

    // B973: the player's target next -- after anything already in progress
    // (finishing a started entity is cheaper than starting another).
    if (targetEligible && bestPhase == EntityBuildPhase::Placeholder) {
        bestId = priorityTargetId;
    }

    // Always prioritize the player entity over distance-based selection
    if (playerSpawnId_ != 0) {
        auto playerIt = entities_.find(playerSpawnId_);
        if (playerIt != entities_.end() &&
            playerIt->second.buildPhase != EntityBuildPhase::Built) {
            const auto& pv = playerIt->second;
            const uint32_t key = (static_cast<uint32_t>(pv.raceId) << 8) | pv.gender;
            // B973: a player whose model is not ready cannot advance; do not
            // let it hold every other entity back while its prep runs.
            const bool ready = pv.buildPhase != EntityBuildPhase::Placeholder ||
                               pv.entityPrepComplete || raceModelLoader_->isModelDataCached(key);
            if (legacy || ready) bestId = playerSpawnId_;
        }
    }

    if (bestId == 0) return false;

    auto& vis = entities_[bestId];
    auto entryPhase = vis.buildPhase;
    const EntityBuildUnitKind unitKind = unitKindForPhase(entryPhase);
    if (scheduler && !scheduler->mayStartUnit(unitKind)) return false;
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

    // B973: the create unit's own decomposition (texture uploads, node/mesh
    // creation, texture-cache misses = textures loaded inside the creation).
    int64_t uploadUs = 0, createUs = 0;
    int uploadedTextures = 0;
    size_t cacheMissesBefore = constrainedTextureCache_ ? constrainedTextureCache_->getCacheMisses() : 0;
    size_t cacheMisses = 0;
    using SteadyClock = std::chrono::steady_clock;
    auto usSince = [](SteadyClock::time_point t) {
        return std::chrono::duration_cast<std::chrono::microseconds>(SteadyClock::now() - t).count();
    };

    const bool did = [&]() -> bool {
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
            // T412 (board B291): routed through uploadPendingEntityTextures()
            // so the skipEntityTextureUpload debug knob is actually honoured.
            {
                const auto t0 = SteadyClock::now();
                uploadedTextures = uploadPendingEntityTextures();
                uploadUs = usSince(t0);
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

            scale = clientActorScale(raceId, gender, vis.serverSize);  // B1172: actor_scale.h

            // Temporarily clear weapon equipment IDs so createAnimatedNodeWithEquipment
            // doesn't bake weapon geometry — weapons are attached in EquipmentAttach phase
            uint32_t savedPrimary = vis.appearance.equipment[7];
            uint32_t savedSecondary = vis.appearance.equipment[8];
            vis.appearance.equipment[7] = 0;
            vis.appearance.equipment[8] = 0;

            // Try animated mesh first
            EQAnimatedMeshSceneNode* animNode = nullptr;
            const auto createStart = SteadyClock::now();
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

            createUs = usSince(createStart);

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
            // B966 (GitLab #284): the node joined the scene graph above with
            // the mesh's raw materials; drawn before NodeSetup ran, it went
            // through the driver's built-in Solid3D/AlphaTest3D programs for
            // one frame. NodeSetup costs ~10 us (measured, Pi), so it runs in
            // this same unit and the node's first draw is already promoted.
            if (!legacy) continue;
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
                vis.nameNode = createNameTagNode(  // B975: drawn by the name-tag batch
                    vis.sceneNode, wname, nameColor, irr::core::vector3df(0, nameHeight, 0));
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

            // Corpse death animation (animated only). T456 (board B477): this
            // was a textual duplicate of the synchronous path's ladder in
            // entity_mesh_build.cpp and nothing asserted it. Same function now.
            if (vis.isCorpse) {
                ApplyCorpseDeathPose(vis, vis.animatedNode);
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
    }();

    const int64_t us = usSince(stepStart);
    if (constrainedTextureCache_) cacheMisses = constrainedTextureCache_->getCacheMisses() - cacheMissesBefore;
    if (createUs > 0 || uploadUs > 0) {
        LOG_INFO(MOD_GRAPHICS, "EntityBuild: {} ({}) {} -> {} in {}us (upload={}us/{}tex create={}us tex_loads={} did={})",
                 bestId, vis.name, phaseName(entryPhase), phaseName(vis.buildPhase), us,
                 uploadUs, uploadedTextures, createUs, cacheMisses, did ? 1 : 0);
    } else {
        LOG_INFO(MOD_GRAPHICS, "EntityBuild: {} ({}) {} -> {} in {}us",
                 bestId, vis.name, phaseName(entryPhase), phaseName(vis.buildPhase), us);
    }
    if (scheduler && did) scheduler->recordUnit(unitKind, static_cast<float>(us) / 1000.0f);
    return did;
}

} // namespace Graphics
} // namespace EQT
