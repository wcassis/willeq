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
#include "client/data_paths.h"
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
    int mappingCount = equipmentModelLoader_->loadItemModelMapping(
        eqt::ResolveDataPath("data/item_models.json"));
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
                // Track reference for cache eviction. T498 (board B755/B823):
                // equipment[] is uniformly a MATERIAL / IT model number now --
                // the self path takes it from the wire's own idfile, exactly as
                // the server computes a peer's -- so the item-id lookup that
                // used to sit here (and could collide, see B824) is gone.
                equipmentModelLoader_->addMeshRef(static_cast<int>(primaryId));
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
                // Track reference for cache eviction (T498: a material, see above).
                equipmentModelLoader_->addMeshRef(static_cast<int>(secondaryId));
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

// Bone lookup and the held-item transform are the shared implementation in
// bone_attachment.h (T60 findBoneIndex, T472/T485 placeHeldItem) --
// see that header's comment for why. Do not reintroduce local copies here.

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
    // T472 (B588) / T485: every held item takes ONE transform and ONE
    // placement, bone_attachment.h's placeHeldItem() -- the point bone's frame
    // with the T485 correction (chosen by search against the official client)
    // and the item's origin (its grip, once the mesh centre is added) exactly
    // at the bone. The paperdoll and the model viewer call the same function.
    auto placeAttachment = [&](irr::scene::ISceneNode* node, int boneIndex) {
        placeHeldItem(node, boneStates[boneIndex].worldTransform, playerBake ? &playerYawFix : nullptr, playerScale);
    };

    // Primary (right hand - R_POINT)
    if (visual.primaryEquipNode) {
        std::vector<std::string> suffixes = {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"};
        int boneIndex = findBoneIndex(skeleton, raceCode, suffixes);
        if (boneIndex >= 0 && boneIndex < static_cast<int>(boneStates.size())) {
            placeAttachment(visual.primaryEquipNode, boneIndex);
        }
    }

    // Secondary (SHIELD_POINT for a shield, otherwise the left hand)
    if (visual.secondaryEquipNode) {
        // T498 (board B755/B823): every appearance -- player, peer and NPC --
        // carries a MATERIAL / IT model number here, so the shield question is
        // asked of it directly. It used to be a DATABASE item id for the local
        // player only, resolved through data/item_models.json by a rule the
        // server does not use.
        bool isShield = EquipmentModelLoader::isShield(static_cast<int>(visual.currentSecondaryId));

        std::vector<std::string> suffixes = isShield
            ? std::vector<std::string>{"SHIELD_POINT_DAG", "SHIELD_POINT", "shield_point_dag", "shield_point",
                                       "L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"}
            : std::vector<std::string>{"L_POINT_DAG", "L_POINT", "l_point_dag", "l_point"};
        int boneIndex = findBoneIndex(skeleton, raceCode, suffixes);
        if (boneIndex >= 0 && boneIndex < static_cast<int>(boneStates.size())) {
            placeAttachment(visual.secondaryEquipNode, boneIndex);
        }
    }
}


} // namespace Graphics
} // namespace EQT
