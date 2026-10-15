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
#include <fmt/ranges.h>

namespace EQT {
namespace Graphics {

bool EntityRenderer::loadEquipmentModels() {
    if (!equipmentModelLoader_) {
        return false;
    }

    // B824: the item->model mapping is willeq.db-only
    // (WilleqDatabase::getItemModel, via getModelIdForItem()); the JSON map
    // that was parsed here on every startup was written and never read.
    // data/item_models.json is still a startup checkFile (application.cpp)
    // and an input to willeq.db -- it is just no longer parsed at runtime.

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

    // B1065: the point bones, resolved once per (skeleton, secondary item) --
    // bone_attachment.h resolveHeldItemBones() tries the skeleton's own model
    // code before the race row's, so a fallback-model race finds its bones.
    // T498 (board B755/B823): equipment[8] is a MATERIAL / IT model number for
    // every appearance, so the shield question is asked of it directly.
    if (visual.attachBoneSkeleton != skeleton.get() || visual.attachBoneSecondaryId != visual.currentSecondaryId) {
        std::string raceCode = RaceModelLoader::getRaceCode(visual.raceId);
        std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                      [](unsigned char c) { return std::toupper(c); });
        const bool isShield = EquipmentModelLoader::isShield(static_cast<int>(visual.currentSecondaryId));
        const HeldItemBones bones = resolveHeldItemBones(skeleton, raceCode, isShield);
        visual.attachBoneSkeleton = skeleton.get();
        visual.attachBoneSecondaryId = visual.currentSecondaryId;
        visual.primaryAttachBone = bones.primary;
        visual.secondaryAttachBone = bones.secondary;
        LOG_DEBUG(MOD_ENTITY, "held items: spawn {} ({}) skeleton '{}' race code '{}' primary IT{} -> {} secondary IT{}{} -> {}",
                  visual.spawnId, visual.name, skeleton->modelCode, raceCode, visual.currentPrimaryId,
                  bones.primary >= 0 ? skeleton->bones[bones.primary].name : std::string("(none)"),
                  visual.currentSecondaryId, isShield ? " (shield)" : "",
                  bones.secondary >= 0 ? skeleton->bones[bones.secondary].name : std::string("(none)"));
    }

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

    const int boneCount = static_cast<int>(boneStates.size());
    if (visual.primaryEquipNode && visual.primaryAttachBone >= 0 && visual.primaryAttachBone < boneCount) {
        placeAttachment(visual.primaryEquipNode, visual.primaryAttachBone);
    }
    if (visual.secondaryEquipNode && visual.secondaryAttachBone >= 0 && visual.secondaryAttachBone < boneCount) {
        placeAttachment(visual.secondaryEquipNode, visual.secondaryAttachBone);
    }
}


} // namespace Graphics
} // namespace EQT
