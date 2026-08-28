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
#include "client/graphics/eq/bone_attachment.h"
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

    // T300: no first-person special case here any more. T296 hid freshly
    // attached equipment when the player was already in first person,
    // because the viewmodel pass drew a second copy of it and the world copy
    // showed up as a large object at the camera. There is no second copy now
    // -- first person draws the ordinary world model and its ordinary
    // bone-attached equipment, and the near clip plane hides both.
}

// Bone lookup, rotation extraction, and weapon/shield grip-offset math are
// the shared implementation in bone_attachment.h (T60, findBoneIndex /
// extractBoneRotation / computeWeaponGripOffset / computeShieldGripOffset,
// already visible here via the EQT::Graphics namespace) — see that
// header's comment for why. Do not reintroduce local copies here.

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

    // T301 Leg C: the PLAYER's body is drawn in a different frame from the one
    // its equipment children inherit -- applyAnimation() bakes (nodeRot.Y + 90)
    // into the player's vertices and render() then uses a TRANSLATION-ONLY
    // world transform, dropping the node's rotation AND scale, while an
    // equipment child still inherits both. Left uncompensated the weapon sits
    // at a yaw of (-2*nodeRot.Y - 90) degrees away from the hand that holds it,
    // which is why first person -- a 62.8-degree frustum centred on the body's
    // forward -- never showed it. See bone_attachment.h's
    // playerEquipmentYawCompensationDegrees() for the derivation, and
    // tests/test_first_person_weapon_attachment.cpp for the pin.
    // NPCs bake nothing and draw through their full AbsoluteTransformation, so
    // they take none of this: playerBake is false and every value below is the
    // pre-T301 one, bit for bit.
    const bool playerBake = visual.animatedNode->isPlayerNode();
    irr::core::matrix4 playerYawFix;   // identity unless playerBake
    float playerScale = 1.0f;
    if (playerBake) {
        playerYawFix.setRotationDegrees(irr::core::vector3df(
            0.0f, playerEquipmentYawCompensationDegrees(visual.animatedNode->getRotation().Y), 0.0f));
        const float s = visual.animatedNode->getScale().X;
        if (s > 0.0001f) playerScale = s;
    }
    // Places one bone-derived attachment, in the parent node's local frame, so
    // that it coincides with the geometry the node actually DRAWS for that bone.
    auto placeAttachment = [&](irr::scene::ISceneNode* node,
                               float px, float py, float pz,
                               const irr::core::vector3df& itemRot,
                               const irr::core::vector3df& worldOffset) {
        if (!playerBake) {
            node->setRotation(itemRot);
            node->setPosition(irr::core::vector3df(
                px + worldOffset.X, pz + worldOffset.Y, py + worldOffset.Z));
            return;
        }
        // Bone position: EQ(x,y,z) -> Irr(x,z,y), un-scaled because the baked
        // body is drawn without the node's scale; the grip offset stays in the
        // child's own (scaled) units, with the mesh it is derived from.
        irr::core::vector3df local(px / playerScale + worldOffset.X,
                                   pz / playerScale + worldOffset.Y,
                                   py / playerScale + worldOffset.Z);
        playerYawFix.rotateVect(local);
        irr::core::matrix4 itemMat;
        itemMat.setRotationDegrees(itemRot);
        // m3 = m1 * m2 applies m2 first, then m1 (Irrlicht matrix4::operator*).
        node->setRotation((playerYawFix * itemMat).getRotationDegrees());
        node->setPosition(local);
    };

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

            // Calculate weapon offset based on mesh bounding box
            // The grip point should be near the hilt (end of weapon) for 1H weapons,
            // or at midpoint for 2H weapons (staffs, etc.)
            float weaponOffset = 0.5f;  // Default fallback
            irr::scene::IMesh* mesh = visual.primaryEquipNode->getMesh();
            if (mesh) {
                weaponOffset = computeWeaponGripOffset(mesh->getBoundingBox());
            }

            // Apply offset along the weapon's local X axis (blade direction)
            irr::core::matrix4 rotMat;
            rotMat.setRotationDegrees(weaponRot);
            irr::core::vector3df localOffset(weaponOffset, 0, 0);
            rotMat.rotateVect(localOffset);

            // Convert EQ coords to Irrlicht coords: EQ(x,y,z) -> Irr(x,z,y) and apply offset
            placeAttachment(visual.primaryEquipNode, px, py, pz, weaponRot, localOffset);
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

            // Extract and apply bone rotation, then add base rotation. T65:
            // shields and plain secondary weapons use the same base
            // rotation (180° around X and Z) -- see
            // bone_attachment.h's computeSecondaryItemBaseRotation() for
            // why the previous isShield-conditional Z rotation was wrong
            // (it made plain secondary weapons render invisibly, hidden
            // inside the body mesh).
            irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
            irr::core::vector3df itemRot = boneRot + computeSecondaryItemBaseRotation();

            // Calculate offset based on item type and size
            float itemOffset = 0.5f;  // Default fallback
            irr::scene::IMesh* mesh = visual.secondaryEquipNode->getMesh();
            if (mesh && !isShield) {
                // For weapons (not shields), calculate offset from bounding box
                itemOffset = computeWeaponGripOffset(mesh->getBoundingBox());
            } else if (isShield) {
                // Shields: small offset to position on arm
                itemOffset = computeShieldGripOffset();
            }

            // Apply offset along local X axis
            irr::core::matrix4 rotMat;
            rotMat.setRotationDegrees(itemRot);
            irr::core::vector3df localOffset(itemOffset, 0, 0);
            rotMat.rotateVect(localOffset);

            // Convert EQ coords to Irrlicht coords and apply offset
            placeAttachment(visual.secondaryEquipNode, px, py, pz, itemRot, localOffset);
        }
    }
}


} // namespace Graphics
} // namespace EQT
