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

bool EntityRenderer::loadEquipmentModels() {
    if (!equipmentModelLoader_) {
        return false;
    }

    // Load item ID to model ID mapping (file validated at startup by S03)
    int mappingCount = equipmentModelLoader_->loadItemModelMapping("data/item_models.json");
    if (mappingCount < 0) {
        LOG_FATAL(MOD_ENTITY, "EntityRenderer: Failed to load data/item_models.json");
    } else {
        LOG_INFO(MOD_ENTITY, "EntityRenderer: Loaded {} item-to-model mappings", mappingCount);
    }

    // Load equipment archives
    bool loaded = equipmentModelLoader_->loadEquipmentArchives();
    if (loaded) {
        LOG_INFO(MOD_ENTITY, "EntityRenderer: Indexed {} equipment models (lazy loading)", equipmentModelLoader_->getLoadedModelCount());
    }
    return loaded;
}

void EntityRenderer::attachEquipment(EntityVisual& visual) {
    if (!equipmentModelLoader_) {
        LOG_DEBUG(MOD_ENTITY, "attachEquipment: Equipment loader not available");
        return;
    }

    // Get primary and secondary equipment IDs from appearance
    uint32_t primaryId = visual.appearance.equipment[7];    // Primary slot
    uint32_t secondaryId = visual.appearance.equipment[8];  // Secondary slot

    // Skip if no equipment
    if (primaryId == 0 && secondaryId == 0) {
        return;
    }

    LOG_DEBUG(MOD_ENTITY, "attachEquipment: Entity {} ({}) primary={} secondary={}", visual.spawnId, visual.name, primaryId, secondaryId);

    // Get the entity's scene node (parent for equipment)
    irr::scene::ISceneNode* parentNode = visual.sceneNode;
    if (!parentNode) {
        return;
    }

    // Attach primary weapon (right hand)
    if (primaryId != 0) {
        irr::scene::IMesh* primaryMesh = equipmentModelLoader_->getEquipmentMesh(primaryId);
        if (primaryMesh) {
            visual.primaryEquipNode = smgr_->addMeshSceneNode(primaryMesh, parentNode);
            if (visual.primaryEquipNode) {
                visual.currentPrimaryId = primaryId;
                // Initial position will be updated by updateEquipmentTransforms
                visual.primaryEquipNode->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
                for (irr::u32 i = 0; i < visual.primaryEquipNode->getMaterialCount(); ++i) {
                    auto& mat = visual.primaryEquipNode->getMaterial(i);
                    mat.BackfaceCulling = false;
                    if (shaderMaterialSolid_ >= 0) {
                        if (mat.MaterialType == irr::video::EMT_SOLID) {
                            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                        } else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
                            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                        }
                        mat.Lighting = false;
                    } else {
                        mat.Lighting = lightingEnabled_;
                    }
                }
                LOG_DEBUG(MOD_ENTITY, "Attached primary equipment {} to entity {}", primaryId, visual.spawnId);
                // Track reference for cache eviction
                int modelId = equipmentModelLoader_->getModelIdForItem(primaryId);
                if (modelId < 0) modelId = static_cast<int>(primaryId);
                equipmentModelLoader_->addMeshRef(modelId);
            }
        }
    }

    // Attach secondary weapon/shield (left hand or shield point)
    if (secondaryId != 0) {
        irr::scene::IMesh* secondaryMesh = equipmentModelLoader_->getEquipmentMesh(secondaryId);
        if (secondaryMesh) {
            visual.secondaryEquipNode = smgr_->addMeshSceneNode(secondaryMesh, parentNode);
            if (visual.secondaryEquipNode) {
                visual.currentSecondaryId = secondaryId;
                // Initial position will be updated by updateEquipmentTransforms
                visual.secondaryEquipNode->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));
                for (irr::u32 i = 0; i < visual.secondaryEquipNode->getMaterialCount(); ++i) {
                    auto& mat = visual.secondaryEquipNode->getMaterial(i);
                    mat.BackfaceCulling = false;
                    if (shaderMaterialSolid_ >= 0) {
                        if (mat.MaterialType == irr::video::EMT_SOLID) {
                            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialSolid_);
                        } else if (mat.MaterialType == irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF) {
                            mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(shaderMaterialAlphaTest_);
                        }
                        mat.Lighting = false;
                    } else {
                        mat.Lighting = lightingEnabled_;
                    }
                }
                LOG_DEBUG(MOD_ENTITY, "Attached secondary equipment {} to entity {}", secondaryId, visual.spawnId);
                // Track reference for cache eviction
                int modelId = equipmentModelLoader_->getModelIdForItem(secondaryId);
                if (modelId < 0) modelId = static_cast<int>(secondaryId);
                equipmentModelLoader_->addMeshRef(modelId);
            }
        }
    }
}

// Helper: Extract rotation from bone matrix (EQ to Irrlicht coordinate conversion)
static irr::core::vector3df extractBoneRotation(const BoneMat4& m) {
    // Convert EQ bone matrix to Irrlicht rotation
    // We need to swap Y and Z axes
    irr::core::matrix4 irrMat;
    irrMat[0] = m.m[0];   irrMat[1] = m.m[2];   irrMat[2] = m.m[1];   irrMat[3] = 0;
    irrMat[4] = m.m[8];   irrMat[5] = m.m[10];  irrMat[6] = m.m[9];   irrMat[7] = 0;
    irrMat[8] = m.m[4];   irrMat[9] = m.m[6];   irrMat[10] = m.m[5];  irrMat[11] = 0;
    irrMat[12] = 0;       irrMat[13] = 0;       irrMat[14] = 0;       irrMat[15] = 1;

    return irrMat.getRotationDegrees();
}

// Helper: Find bone index trying multiple naming conventions
static int findBoneIndex(const std::shared_ptr<CharacterSkeleton>& skeleton, const std::string& raceCode,
                         const std::vector<std::string>& suffixes) {
    if (!skeleton) return -1;

    for (const auto& suffix : suffixes) {
        // Try uppercase race code
        std::string upperName = raceCode + suffix;
        int idx = skeleton->getBoneIndex(upperName);
        if (idx >= 0) return idx;

        // Try lowercase race code
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        std::string lowerName = lowerRace + suffix;
        idx = skeleton->getBoneIndex(lowerName);
        if (idx >= 0) return idx;

        // Try without underscore (e.g., "qcmr_point" instead of "qcm_r_point")
        if (suffix.length() > 0 && suffix[0] != '_') {
            // Already no leading underscore
        } else if (suffix.length() > 1) {
            std::string noUnderscoreSuffix = suffix.substr(1);
            idx = skeleton->getBoneIndex(lowerRace + noUnderscoreSuffix);
            if (idx >= 0) return idx;
        }

        // Try with _DAG suffix removed if present
        if (suffix.length() > 4 && suffix.substr(suffix.length() - 4) == "_DAG") {
            std::string noDAG = suffix.substr(0, suffix.length() - 4);
            idx = skeleton->getBoneIndex(raceCode + noDAG);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(lowerRace + noDAG);
            if (idx >= 0) return idx;
        }
    }
    return -1;
}

void EntityRenderer::updateEquipmentTransforms(EntityVisual& visual) {
    // Only update for animated entities with equipment
    if (!visual.isAnimated || !visual.animatedNode) {
        return;
    }

    if (!visual.primaryEquipNode && !visual.secondaryEquipNode) {
        return;
    }

    // Get animator and skeleton
    SkeletalAnimator& animator = visual.animatedNode->getAnimator();
    auto skeleton = animator.getSkeleton();
    if (!skeleton) {
        return;
    }

    const auto& boneStates = animator.getBoneStates();
    if (boneStates.empty()) {
        return;
    }

    // Get race code for bone name construction
    std::string raceCode = RaceModelLoader::getRaceCode(visual.raceId);
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    // Update primary weapon position and rotation (right hand - R_POINT)
    if (visual.primaryEquipNode) {
        std::vector<std::string> suffixes = {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"};
        int boneIndex = findBoneIndex(skeleton, raceCode, suffixes);

        if (boneIndex >= 0 && boneIndex < static_cast<int>(boneStates.size())) {
            const BoneMat4& worldTransform = boneStates[boneIndex].worldTransform;

            // Extract position from matrix (column 3)
            float px = worldTransform.m[12];
            float py = worldTransform.m[13];
            float pz = worldTransform.m[14];

            // Extract and apply bone rotation, then add weapon's base rotation (180° around X)
            irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
            irr::core::vector3df weaponRot = boneRot + irr::core::vector3df(180.0f, 0, 0);
            visual.primaryEquipNode->setRotation(weaponRot);

            // Calculate weapon offset based on mesh bounding box
            // The grip point should be near the hilt (end of weapon) for 1H weapons,
            // or at midpoint for 2H weapons (staffs, etc.)
            float weaponOffset = 0.5f;  // Default fallback
            irr::scene::IMesh* mesh = visual.primaryEquipNode->getMesh();
            if (mesh) {
                irr::core::aabbox3df bbox = mesh->getBoundingBox();
                float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;  // Length along X axis

                // Check if this is a two-handed weapon (staffs, 2H swords, etc.)
                // Two-handed weapons are typically longer (> 3 units) or specific model IDs
                bool isTwoHanded = (weaponLength > 3.0f);

                if (isTwoHanded) {
                    // Two-handed: grip at midpoint (half the length from center)
                    weaponOffset = weaponLength * 0.25f;
                } else {
                    // One-handed: grip near the hilt (small offset from end)
                    // The hilt is at MinEdge.X, so offset is distance from center to near-hilt
                    weaponOffset = -bbox.MinEdge.X * 0.8f;  // 80% toward hilt end
                }
            }

            // Apply offset along the weapon's local X axis (blade direction)
            irr::core::matrix4 rotMat;
            rotMat.setRotationDegrees(weaponRot);
            irr::core::vector3df localOffset(weaponOffset, 0, 0);
            rotMat.rotateVect(localOffset);

            // Convert EQ coords to Irrlicht coords: EQ(x,y,z) -> Irr(x,z,y) and apply offset
            visual.primaryEquipNode->setPosition(irr::core::vector3df(
                px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
        }
    }

    // Update secondary weapon/shield position and rotation (left hand or shield point)
    if (visual.secondaryEquipNode) {
        // Determine if this is a shield based on the direct model ID
        // For NPC equipment, the ID is already the model ID, not a database item ID
        bool isShield = EquipmentModelLoader::isShield(static_cast<int>(visual.currentSecondaryId));

        std::vector<std::string> suffixes = isShield
            ? std::vector<std::string>{"SHIELD_POINT_DAG", "SHIELD_POINT", "shield_point_dag", "shield_point",
                                       "L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"}
            : std::vector<std::string>{"L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"};
        int boneIndex = findBoneIndex(skeleton, raceCode, suffixes);

        if (boneIndex >= 0 && boneIndex < static_cast<int>(boneStates.size())) {
            const BoneMat4& worldTransform = boneStates[boneIndex].worldTransform;

            // Extract position from matrix (column 3)
            float px = worldTransform.m[12];
            float py = worldTransform.m[13];
            float pz = worldTransform.m[14];

            // Extract and apply bone rotation, then add base rotation
            // Shields need 180° around both X and Z axes
            irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
            irr::core::vector3df itemRot = boneRot + irr::core::vector3df(180.0f, 0, isShield ? 180.0f : 0);
            visual.secondaryEquipNode->setRotation(itemRot);

            // Calculate offset based on item type and size
            float itemOffset = 0.5f;  // Default fallback
            irr::scene::IMesh* mesh = visual.secondaryEquipNode->getMesh();
            if (mesh && !isShield) {
                // For weapons (not shields), calculate offset from bounding box
                irr::core::aabbox3df bbox = mesh->getBoundingBox();
                float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;

                bool isTwoHanded = (weaponLength > 3.0f);
                if (isTwoHanded) {
                    itemOffset = weaponLength * 0.25f;
                } else {
                    itemOffset = -bbox.MinEdge.X * 0.8f;
                }
            } else if (isShield) {
                // Shields: small offset to position on arm
                itemOffset = 0.3f;
            }

            // Apply offset along local X axis
            irr::core::matrix4 rotMat;
            rotMat.setRotationDegrees(itemRot);
            irr::core::vector3df localOffset(itemOffset, 0, 0);
            rotMat.rotateVect(localOffset);

            // Convert EQ coords to Irrlicht coords and apply offset
            visual.secondaryEquipNode->setPosition(irr::core::vector3df(
                px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
        }
    }
}


} // namespace Graphics
} // namespace EQT
