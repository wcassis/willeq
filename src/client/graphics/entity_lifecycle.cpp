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
#include "client/graphics/impostor/impostor_cache.h"
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

void EntityRenderer::removeEntity(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    // Remove from active entities tracking
    activeEntities_.erase(spawnId);

    // Remove from boat tracking
    if (it->second.hasCollision) {
        unregisterBoat(spawnId);
    }

    // Remove from spatial grid
    removeEntityFromGrid(spawnId);

    // B964: free its impostor cell (or keep it, dirty, across an appearance
    // rebuild -- see updateEntityAppearance()).
    impostorOnEntityRemoved(spawnId);

    EntityVisual& visual = it->second;

    // Release mesh ref counts for equipment
    if (equipmentModelLoader_) {
        // T498 (board B755/B823): a MATERIAL / IT model number, matching the
        // addMeshRef() in entity_equipment.cpp -- no item-id lookup.
        if (visual.currentPrimaryId != 0) {
            equipmentModelLoader_->removeMeshRef(static_cast<int>(visual.currentPrimaryId));
        }
        if (visual.currentSecondaryId != 0) {
            equipmentModelLoader_->removeMeshRef(static_cast<int>(visual.currentSecondaryId));
        }
    }

    // Release mesh ref count for race model
    if (raceModelLoader_) {
        raceModelLoader_->removeMeshRef(visual.raceId, visual.gender);
    }

    // Remove equipment nodes first (they're children of sceneNode)
    if (visual.primaryEquipNode) {
        visual.primaryEquipNode->remove();
        visual.primaryEquipNode = nullptr;
    }
    if (visual.secondaryEquipNode) {
        visual.secondaryEquipNode->remove();
        visual.secondaryEquipNode = nullptr;
    }

    // Remove casting bar billboard
    if (visual.castBarBillboard) {
        visual.castBarBillboard->remove();
        visual.castBarBillboard = nullptr;
    }

    // Remove light source. T391 (B202): the lightNode is grabbed at creation
    // (setEntityLight, entity_renderer.cpp) because the worker's cull path
    // detaches it with remove(); detach it here and then release OUR
    // reference. Before T391 this line ran remove() on a node the cull had
    // already freed, which is where ASAN caught B202 (see clearEntities()
    // below and refactor/T391_two_client_segv.md).
    if (visual.lightNode) {
        visual.lightNode->remove();
        visual.lightNode->drop();
        visual.lightNode = nullptr;
    }

    if (visual.nameNode) {
        visual.nameNode->remove();
    }
    if (visual.sceneNode) {
        // If not in scene graph, we still hold the reference from grab()
        if (visual.inSceneGraph) {
            visual.sceneNode->remove();
        }
        visual.sceneNode->drop();  // Release our reference
    }

    entities_.erase(it);
}

void EntityRenderer::startCorpseDecay(uint16_t spawnId) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        return;
    }

    EntityVisual& visual = it->second;

    // Only start decay for corpses, and only if not already fading
    if (!visual.isCorpse || visual.isFading) {
        // Not a corpse or already fading - just remove immediately
        if (!visual.isCorpse) {
            removeEntity(spawnId);
        }
        return;
    }

    LOG_DEBUG(MOD_ENTITY, "Starting corpse decay for {} ({})", spawnId, visual.name);

    // Start the fade-out animation
    visual.isFading = true;
    visual.fadeTimer = 0.0f;
    visual.fadeAlpha = 1.0f;

    // Add to active entities - fading entities need per-frame updates
    activeEntities_.insert(spawnId);
}

void EntityRenderer::updateEntityAppearance(uint16_t spawnId, uint16_t raceId, uint8_t gender,
                                             const EntityAppearance& appearance) {
    auto it = entities_.find(spawnId);
    if (it == entities_.end()) {
        LOG_DEBUG(MOD_GRAPHICS, "updateEntityAppearance: entity {} not found", spawnId);
        return;
    }

    // Save current entity state
    EntityVisual& oldVisual = it->second;
    std::string name = oldVisual.name;
    float x = oldVisual.lastX;
    float y = oldVisual.lastY;
    float z = oldVisual.lastZ;
    float heading = oldVisual.lastHeading;
    bool isPlayer = oldVisual.isPlayer;
    bool isNPC = oldVisual.isNPC;
    bool isCorpse = oldVisual.isCorpse;
    std::string currentAnimation = oldVisual.currentAnimation;

    LOG_INFO(MOD_GRAPHICS, "Updating appearance for entity {} ({}) to race {} gender {}",
             spawnId, name, raceId, gender);

    // Remove the old entity (this cleans up scene nodes). B964: an impostored
    // entity keeps its cell across the rebuild, marked for re-capture -- the
    // EntityAppearanceChanged / OP_WearChange refresh trigger.
    impostorRetainOnRemove_ = true;
    removeEntity(spawnId);
    impostorRetainOnRemove_ = false;

    // Recreate with new appearance
    bool result = createEntity(spawnId, raceId, name, x, y, z, heading,
                               isPlayer, gender, appearance, isNPC, isCorpse);

    if (result) {
        // Restore animation state if entity had one.
        //
        // T454 (board B473): NOT for a corpse. A corpse's currentAnimation is
        // the death animation ("d05", or the d02/d01 fallback), and the
        // rebuild above has already put the body in that animation's LAST
        // frame and stopped it there -- which is what "lying down" is. Playing
        // it again as a LOOPING animation restarts it at frame 0, the STANDING
        // pose, and then loops the character falling over forever. Measured
        // before the fix on a Human male corpse: state Stopped / frame 12
        // became state Playing / frame 0. The oracle lays a corpse flat and
        // leaves it there (T346's OP_SpawnAppearance AT_ANIMATION param 115,
        // ANIM_LYING; T431's frames), so there is nothing to restore here.
        // Pinned by CorpsePoseTest.AnAppearanceChangeDoesNotStandTheCorpseBackUp.
        if (!isCorpse && !currentAnimation.empty()) {
            setEntityAnimation(spawnId, currentAnimation, true, false);
        }

        // Re-mark as player if needed (createEntity may not preserve this)
        if (isPlayer) {
            setPlayerSpawnId(spawnId);
        }

        LOG_DEBUG(MOD_GRAPHICS, "Successfully updated appearance for entity {}", spawnId);
    } else {
        LOG_WARN(MOD_GRAPHICS, "Failed to recreate entity {} with new appearance", spawnId);
    }
}

void EntityRenderer::clearEntities() {
    // P08d: emit the per-zone-load composite-entity adoption summary before tearing
    // down this zone's entities (plan B6). Only fires when the tally is non-empty (i.e.
    // enableCompositeEntities was on and at least one entity was built).
    if (compositeTotalCount_ > 0) {
        size_t sheetHits = raceModelLoader_ ? raceModelLoader_->getCompositeSheetHits() : 0;
        size_t sheetMisses = raceModelLoader_ ? raceModelLoader_->getCompositeSheetMisses() : 0;
        LOG_INFO(MOD_GRAPHICS,
                 "Composite entity adoption: {}/{} entities eligible, {} sheet hits, {} sheet misses",
                 compositeEligibleCount_, compositeTotalCount_, sheetHits, sheetMisses);
        compositeEligibleCount_ = 0;
        compositeTotalCount_ = 0;
        if (raceModelLoader_) {
            raceModelLoader_->resetCompositeAdoptionStats();
        }
    }

    for (auto& [spawnId, visual] : entities_) {
        // Remove equipment nodes first
        if (visual.primaryEquipNode) {
            visual.primaryEquipNode->remove();
        }
        if (visual.secondaryEquipNode) {
            visual.secondaryEquipNode->remove();
        }
        // Remove casting bar billboard
        if (visual.castBarBillboard) {
            visual.castBarBillboard->remove();
        }
        // Remove light node. T391 (B202): same grab/drop ownership as
        // removeEntity() above -- this is the exact line ASAN faulted on
        // (entity_lifecycle.cpp:210, heap-buffer-overflow read of a node the
        // worker's cull had already deleted).
        if (visual.lightNode) {
            visual.lightNode->remove();
            visual.lightNode->drop();
            visual.lightNode = nullptr;
        }
        if (visual.nameNode) {
            visual.nameNode->remove();
        }
        if (visual.sceneNode) {
            // If not in scene graph, we still hold the reference from grab()
            if (visual.inSceneGraph) {
                visual.sceneNode->remove();
            }
            visual.sceneNode->drop();  // Release our reference
        }
    }
    entities_.clear();
    boatSpawnIds_.clear();
    // B964: a zone change drops every impostor cell and page.
    impostorSpawns_.clear();
    impostorPolicy_.reset();
    if (impostors_) impostors_->clear();
}

bool EntityRenderer::hasEntity(uint16_t spawnId) const {
    return entities_.find(spawnId) != entities_.end();
}

void EntityRenderer::setAllEntitiesVisible(bool visible) {
    for (auto& [spawnId, visual] : entities_) {
        if (visual.sceneNode) {
            if (visible) {
                // Add back to scene graph if not already there
                if (!visual.inSceneGraph) {
                    smgr_->getRootSceneNode()->addChild(visual.sceneNode);
                    visual.inSceneGraph = true;
                }
                visual.sceneNode->setVisible(true);
            } else {
                visual.sceneNode->setVisible(false);
            }
        }
    }
}

size_t EntityRenderer::getModeledEntityCount() const {
    size_t count = 0;
    for (const auto& [spawnId, visual] : entities_) {
        if (!visual.usesPlaceholder) {
            count++;
        }
    }
    return count;
}

size_t EntityRenderer::getPerInstanceMemoryBytes() const {
    size_t total = 0;
    for (const auto& [spawnId, visual] : entities_) {
        total += sizeof(EntityVisual);
        if (visual.animatedNode) {
            auto* instanceMesh = visual.animatedNode->getInstanceMesh();
            if (instanceMesh) {
                for (irr::u32 i = 0; i < instanceMesh->getMeshBufferCount(); ++i) {
                    auto* buf = instanceMesh->getMeshBuffer(i);
                    if (buf) {
                        total += buf->getVertexCount() * sizeof(irr::video::S3DVertex);
                        total += buf->getIndexCount() * sizeof(irr::u16);
                    }
                }
            }
        }
    }
    return total;
}

void EntityRenderer::setNameTagsVisible(bool visible) {
    nameTagsVisible_ = visible;

    for (auto& [spawnId, visual] : entities_) {
        if (visual.nameNode) {
            visual.nameNode->setVisible(visible);
        }
    }
}

void EntityRenderer::swapShaderMaterials(irr::s32 oldSolid, irr::s32 newSolid,
                                         irr::s32 oldAlpha, irr::s32 newAlpha,
                                         irr::s32 oldAlphaRGBA, irr::s32 newAlphaRGBA) {
    if (oldSolid == newSolid && oldAlpha == newAlpha && oldAlphaRGBA == newAlphaRGBA) return;

    auto swapMat = [&](irr::video::SMaterial& mat) {
        if (mat.MaterialType == static_cast<irr::video::E_MATERIAL_TYPE>(oldSolid))
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(newSolid);
        else if (mat.MaterialType == static_cast<irr::video::E_MATERIAL_TYPE>(oldAlpha))
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(newAlpha);
        else if (oldAlphaRGBA >= 0 && mat.MaterialType == static_cast<irr::video::E_MATERIAL_TYPE>(oldAlphaRGBA))
            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(newAlphaRGBA);
    };

    auto swapSceneNode = [&](irr::scene::ISceneNode* node) {
        if (!node) return;
        for (irr::u32 i = 0; i < node->getMaterialCount(); ++i)
            swapMat(node->getMaterial(i));
    };

    int swapped = 0;
    for (auto& [spawnId, visual] : entities_) {
        swapSceneNode(visual.meshNode);
        swapSceneNode(visual.animatedNode);
        swapSceneNode(visual.primaryEquipNode);
        swapSceneNode(visual.secondaryEquipNode);
        ++swapped;
    }
    LOG_INFO(MOD_GRAPHICS, "EntityRenderer::swapShaderMaterials: {} entities swapped (old={}/{}/{} new={}/{}/{})",
             swapped, oldSolid, oldAlpha, oldAlphaRGBA, newSolid, newAlpha, newAlphaRGBA);
}

void EntityRenderer::setLightingEnabled(bool enabled) {
    lightingEnabled_ = enabled;

    // When GLSL shaders are active, Lighting must stay false (shader handles it).
    // Only change Lighting flag for fixed-function materials.
    bool useFixedFunction = (shaderMaterialSolid_ < 0);

    // Update all entity materials
    for (auto& [spawnId, visual] : entities_) {
        if (visual.animatedNode) {
            for (irr::u32 i = 0; i < visual.animatedNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.animatedNode->getMaterial(i).Lighting = enabled;
                    if (enabled) {
                        visual.animatedNode->getMaterial(i).NormalizeNormals = true;
                        visual.animatedNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        visual.animatedNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                }
            }
        }
        if (visual.meshNode) {
            for (irr::u32 i = 0; i < visual.meshNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.meshNode->getMaterial(i).Lighting = enabled;
                    if (enabled) {
                        visual.meshNode->getMaterial(i).NormalizeNormals = true;
                        visual.meshNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                        visual.meshNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
                    }
                }
            }
        }
        if (visual.primaryEquipNode) {
            for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.primaryEquipNode->getMaterial(i).Lighting = enabled;
                }
            }
        }
        if (visual.secondaryEquipNode) {
            for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                if (useFixedFunction) {
                    visual.secondaryEquipNode->getMaterial(i).Lighting = enabled;
                }
            }
        }
    }

    LOG_DEBUG(MOD_GRAPHICS, "Entity lighting: {}", enabled ? "ON" : "OFF");
}

void EntityRenderer::setPlayerEntityVisible(bool visible) {
    bool foundPlayer = false;
    for (auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            foundPlayer = true;
            // Show/hide the mesh node (animated or static)
            if (visual.animatedNode) {
                visual.animatedNode->setVisible(visible);
                LOG_DEBUG(MOD_GRAPHICS, "setPlayerEntityVisible: visible={} spawnId={} animatedNode={} IsVisible={}",
                          visible, spawnId, (void*)visual.animatedNode, visual.animatedNode->isVisible());
            } else if (visual.meshNode) {
                visual.meshNode->setVisible(visible);
            }
            // Show/hide the name tag
            if (visual.nameNode) {
                visual.nameNode->setVisible(visible);
            }
            // Show/hide equipment
            if (visual.primaryEquipNode) {
                visual.primaryEquipNode->setVisible(visible);
            }
            if (visual.secondaryEquipNode) {
                visual.secondaryEquipNode->setVisible(visible);
            }
            break; // Only one player entity
        }
    }
    if (!foundPlayer) {
        LOG_DEBUG(MOD_GRAPHICS, "setPlayerEntityVisible: No player entity found!");
    }
}

void EntityRenderer::setDebugTargetId(uint16_t spawnId) {
    // Disable verbose logging on previous target
    if (debugTargetId_ != 0 && debugTargetId_ != spawnId) {
        auto it = entities_.find(debugTargetId_);
        if (it != entities_.end() && it->second.isAnimated && it->second.animatedNode) {
            it->second.animatedNode->getAnimator().setVerboseLogging(false);
        }
    }

    debugTargetId_ = spawnId;

    // Enable verbose logging on new target
    if (spawnId != 0) {
        auto it = entities_.find(spawnId);
        if (it != entities_.end() && it->second.isAnimated && it->second.animatedNode) {
            it->second.animatedNode->getAnimator().setVerboseLogging(true);
        }
    }
}

void EntityRenderer::debugLogPlayerVisibility() const {
    static int frameCounter = 0;
    if (++frameCounter % 60 != 0) return;  // Log once per second at 60fps

    for (const auto& [spawnId, visual] : entities_) {
        if (visual.isPlayer) {
            bool nodeVisible = false;
            bool parentVisible = true;
            irr::scene::ISceneNode* parent = nullptr;

            if (visual.animatedNode) {
                nodeVisible = visual.animatedNode->isVisible();
                parent = visual.animatedNode->getParent();
                if (parent) {
                    parentVisible = parent->isVisible();
                }
            }

            LOG_DEBUG(MOD_GRAPHICS, "debugLogPlayerVisibility: spawnId={} animatedNode={} nodeVisible={} parent={} parentVisible={} isFirstPersonMode={}",
                      spawnId, (void*)visual.animatedNode, nodeVisible, (void*)parent, parentVisible, visual.isFirstPersonMode);
            return;
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "debugLogPlayerVisibility: No player entity found!");
}

} // namespace Graphics
} // namespace EQT
