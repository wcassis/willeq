#include "client/graphics/ui/character_model_view.h"
#include "client/graphics/ui/inventory_manager.h"
#include "client/graphics/ui/inventory_constants.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/equipment_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>

namespace eqt {
namespace ui {

CharacterModelView::CharacterModelView() = default;

CharacterModelView::~CharacterModelView() {
    // Clean up our scene manager (this also removes all nodes in it)
    if (modelSmgr_) {
        modelSmgr_->drop();
        modelSmgr_ = nullptr;
    }

    // Note: renderTarget_ is owned by the driver and will be cleaned up
    // when the driver is destroyed or explicitly removed
}

void CharacterModelView::init(irr::scene::ISceneManager* smgr,
                               irr::video::IVideoDriver* driver,
                               int width, int height) {
    if (initialized_) {
        LOG_WARN(MOD_UI, "CharacterModelView::init called when already initialized");
        return;
    }

    if (!smgr || !driver) {
        LOG_ERROR(MOD_UI, "CharacterModelView::init: null scene manager or driver");
        return;
    }

    parentSmgr_ = smgr;
    driver_ = driver;
    width_ = width;
    height_ = height;

    // Create render target texture
    irr::core::dimension2d<irr::u32> rtSize(width, height);
    renderTarget_ = driver_->addRenderTargetTexture(rtSize, "CharModelViewRT",
                                                     irr::video::ECF_A8R8G8B8);
    if (!renderTarget_) {
        LOG_ERROR(MOD_UI, "CharacterModelView: Failed to create render target texture {}x{}",
                  width, height);
        // Continue anyway - we may be on software renderer with limited RTT support
    }

    // Create our own scene manager for isolated rendering
    modelSmgr_ = smgr->createNewSceneManager(false);  // false = don't copy root scene node
    if (!modelSmgr_) {
        LOG_ERROR(MOD_UI, "CharacterModelView: Failed to create scene manager");
        return;
    }

    setupScene();
    setupCamera();
    setupLighting();

    initialized_ = true;
    LOG_INFO(MOD_UI, "CharacterModelView initialized: {}x{} RTT={}",
             width, height, (renderTarget_ ? "yes" : "no"));
}

void CharacterModelView::setRaceModelLoader(EQT::Graphics::RaceModelLoader* loader) {
    raceModelLoader_ = loader;

    // If we already have appearance set, rebuild the model with the new loader
    if (hasAppearance_ && loader) {
        updateCharacterModel();
    }
}

void CharacterModelView::setEquipmentModelLoader(EQT::Graphics::EquipmentModelLoader* loader) {
    equipmentModelLoader_ = loader;

    // If we already have a character, attach weapons
    if (characterNode_ && loader) {
        attachWeapons();
    }
}

void CharacterModelView::setInventoryManager(inventory::InventoryManager* invManager) {
    inventoryManager_ = invManager;

    // If we already have a character, rebuild with inventory materials
    if (hasAppearance_ && invManager) {
        updateCharacterModel();
    }
}

void CharacterModelView::setCharacter(uint16_t raceId, uint8_t gender,
                                       const EQT::Graphics::EntityAppearance& appearance) {
    LOG_INFO(MOD_UI, "CharacterModelView::setCharacter race={} gender={} texture={} helm={} primary={} secondary={}",
             raceId, gender, (int)appearance.texture, (int)appearance.helm,
             appearance.equipment[7], appearance.equipment[8]);

    // Store the appearance data locally
    currentRaceId_ = raceId;
    currentGender_ = gender;
    hasAppearance_ = true;

    // Copy appearance fields to our storage
    storedFace_ = appearance.face;
    storedHaircolor_ = appearance.haircolor;
    storedHairstyle_ = appearance.hairstyle;
    storedBeardcolor_ = appearance.beardcolor;
    storedBeard_ = appearance.beard;
    storedTexture_ = appearance.texture;
    storedHelm_ = appearance.helm;
    for (int i = 0; i < 9; i++) {
        storedEquipment_[i] = appearance.equipment[i];
        storedEquipmentTint_[i] = appearance.equipment_tint[i];
    }

    updateCharacterModel();
}

void CharacterModelView::updateCharacterModel() {
    if (!modelSmgr_ || !raceModelLoader_) {
        return;
    }

    // Remove existing weapon nodes first (they're children of character node)
    if (primaryWeaponNode_) {
        primaryWeaponNode_->remove();
        primaryWeaponNode_ = nullptr;
        currentPrimaryId_ = 0;
    }
    if (secondaryWeaponNode_) {
        secondaryWeaponNode_->remove();
        secondaryWeaponNode_ = nullptr;
        currentSecondaryId_ = 0;
    }

    // Remove existing character node
    if (characterNode_) {
        characterNode_->remove();
        characterNode_ = nullptr;
    }

    if (!hasAppearance_) {
        return;
    }

    // Reconstruct appearance from stored fields
    EQT::Graphics::EntityAppearance appearance;
    appearance.face = storedFace_;
    appearance.haircolor = storedHaircolor_;
    appearance.hairstyle = storedHairstyle_;
    appearance.beardcolor = storedBeardcolor_;
    appearance.beard = storedBeard_;
    appearance.texture = storedTexture_;
    appearance.helm = storedHelm_;
    for (int i = 0; i < 9; i++) {
        appearance.equipment[i] = storedEquipment_[i];
        appearance.equipment_tint[i] = storedEquipmentTint_[i];
    }

    // For player character: override equipment materials from inventory items
    // This gives correct per-slot textures based on actual equipped items
    if (inventoryManager_) {
        // Map inventory slots to TextureProfile indices (EquipSlot)
        // Inventory SLOT -> EquipSlot index
        struct SlotMapping {
            int16_t inventorySlot;
            int equipSlotIndex;
        };
        static const SlotMapping mappings[] = {
            { inventory::SLOT_HEAD, 0 },      // Head
            { inventory::SLOT_CHEST, 1 },     // Chest
            { inventory::SLOT_ARMS, 2 },      // Arms
            { inventory::SLOT_WRIST1, 3 },    // Wrist (use first wrist slot)
            { inventory::SLOT_HANDS, 4 },     // Hands
            { inventory::SLOT_LEGS, 5 },      // Legs
            { inventory::SLOT_FEET, 6 },      // Feet
            { inventory::SLOT_PRIMARY, 7 },   // Primary
            { inventory::SLOT_SECONDARY, 8 }, // Secondary
        };

        LOG_DEBUG(MOD_UI, "Building appearance from inventory items:");
        for (const auto& mapping : mappings) {
            const inventory::ItemInstance* item = inventoryManager_->getItem(mapping.inventorySlot);
            if (item) {
                // Use item's material for body texture, or item ID for weapons
                if (mapping.equipSlotIndex < 7) {
                    // Body armor: use material for texture variant and color for tint
                    appearance.equipment[mapping.equipSlotIndex] = item->material;
                    appearance.equipment_tint[mapping.equipSlotIndex] = item->color;
                    LOG_DEBUG(MOD_UI, "  Slot {} ({}) -> material {} color=0x{:08X} (item: {})",
                              mapping.inventorySlot, inventory::getSlotName(mapping.inventorySlot),
                              (int)item->material, item->color, item->name);
                } else {
                    // Weapons: use item ID for model lookup
                    appearance.equipment[mapping.equipSlotIndex] = item->itemId;
                    LOG_DEBUG(MOD_UI, "  Slot {} ({}) -> itemId {} ({})",
                              mapping.inventorySlot, inventory::getSlotName(mapping.inventorySlot),
                              item->itemId, item->name);
                }
            }
        }
    }

    // Create animated node with equipment
    // The parent is the root scene node of our isolated scene manager
    characterNode_ = raceModelLoader_->createAnimatedNodeWithEquipment(
        currentRaceId_, currentGender_, appearance,
        modelSmgr_->getRootSceneNode());

    if (!characterNode_) {
        // Try without equipment
        characterNode_ = raceModelLoader_->createAnimatedNode(
            currentRaceId_, currentGender_,
            modelSmgr_->getRootSceneNode());
    }

    if (characterNode_) {
        // Apply race scale
        float scale = raceModelLoader_->getRaceScale(currentRaceId_);
        characterNode_->setScale(irr::core::vector3df(scale, scale, scale));

        // Position at origin
        characterNode_->setPosition(irr::core::vector3df(0, 0, 0));

        // Apply current rotation
        characterNode_->setRotation(irr::core::vector3df(0, rotationY_, 0));

        // Start idle animation (o01)
        if (characterNode_->hasAnimation("o01")) {
            characterNode_->playAnimation("o01", true);  // Loop
            LOG_DEBUG(MOD_UI, "CharacterModelView: Playing o01 animation for race {}",
                      currentRaceId_);
        } else {
            LOG_WARN(MOD_UI, "CharacterModelView: No o01 animation for race {}",
                     currentRaceId_);
        }

        // Force animation update to recalculate bounding box with correct geometry
        characterNode_->forceAnimationUpdate();

        // Attach weapon models
        attachWeapons();

        // Adjust camera to frame the model
        adjustCameraForModel();

        LOG_DEBUG(MOD_UI, "CharacterModelView: Created model for race={} gender={} scale={}",
                  currentRaceId_, currentGender_, scale);
    } else {
        LOG_WARN(MOD_UI, "CharacterModelView: Failed to create model for race={} gender={}",
                 currentRaceId_, currentGender_);
    }
}

void CharacterModelView::setupScene() {
    if (!modelSmgr_) return;

    // Set ambient light for the scene
    modelSmgr_->setAmbientLight(irr::video::SColorf(0.5f, 0.5f, 0.5f, 1.0f));
}

void CharacterModelView::setupCamera() {
    if (!modelSmgr_) return;

    // Create camera positioned to view a humanoid character
    // Position: in front of character, looking at center mass
    // Using Y-up coordinate system (Irrlicht default)
    camera_ = modelSmgr_->addCameraSceneNode(
        nullptr,
        irr::core::vector3df(0, 8, -25),   // Position: front, slightly above center
        irr::core::vector3df(0, 5, 0));    // Look at: character center

    if (camera_) {
        // Set perspective with moderate FOV
        camera_->setFOV(irr::core::PI / 5.0f);  // ~36 degrees
        camera_->setAspectRatio(static_cast<float>(width_) / static_cast<float>(height_));
        camera_->setNearValue(1.0f);
        camera_->setFarValue(500.0f);
    }
}

void CharacterModelView::setupLighting() {
    if (!modelSmgr_) return;

    // Key light - main illumination from front-right
    irr::scene::ILightSceneNode* keyLight = modelSmgr_->addLightSceneNode(
        nullptr,
        irr::core::vector3df(15, 20, -20),
        irr::video::SColorf(0.9f, 0.9f, 0.9f),
        100.0f);
    if (keyLight) {
        keyLight->getLightData().Type = irr::video::ELT_POINT;
    }

    // Fill light - softer from front-left
    irr::scene::ILightSceneNode* fillLight = modelSmgr_->addLightSceneNode(
        nullptr,
        irr::core::vector3df(-15, 15, -15),
        irr::video::SColorf(0.4f, 0.4f, 0.5f),
        80.0f);
    if (fillLight) {
        fillLight->getLightData().Type = irr::video::ELT_POINT;
    }

    // Rim light - from behind to add depth
    irr::scene::ILightSceneNode* rimLight = modelSmgr_->addLightSceneNode(
        nullptr,
        irr::core::vector3df(0, 15, 20),
        irr::video::SColorf(0.3f, 0.3f, 0.4f),
        60.0f);
    if (rimLight) {
        rimLight->getLightData().Type = irr::video::ELT_POINT;
    }
}

void CharacterModelView::adjustCameraForModel() {
    if (!camera_ || !characterNode_) return;

    // Get model bounding box to adjust camera distance
    irr::core::aabbox3df bbox = characterNode_->getBoundingBox();

    // Apply scale to bounding box
    irr::core::vector3df scale = characterNode_->getScale();
    irr::core::vector3df extent = bbox.getExtent();
    extent.X *= scale.X;
    extent.Y *= scale.Y;
    extent.Z *= scale.Z;

    // Calculate model center (in Y)
    float modelCenter = (bbox.MinEdge.Y + bbox.MaxEdge.Y) * 0.5f * scale.Y;

    // Use the largest dimension as the "bounding cube" size
    float maxExtent = std::max({extent.X, extent.Y, extent.Z});

    // Calculate distance to fit the model in view
    // Using trigonometry: distance = (size/2) / tan(fov/2)
    // Add a small margin (0.9 = 90% fill)
    float fov = camera_->getFOV();
    float aspectRatio = static_cast<float>(width_) / static_cast<float>(height_);

    // For vertical FOV, we need to fit height; for horizontal we need to consider aspect
    // The vertical extent determines distance for tall models, horizontal for wide ones
    float verticalSize = extent.Y;
    float horizontalSize = std::max(extent.X, extent.Z);

    // Calculate distance needed to fit each dimension
    float distanceForHeight = (verticalSize * 0.5f) / std::tan(fov * 0.5f);
    float horizontalFov = 2.0f * std::atan(std::tan(fov * 0.5f) * aspectRatio);
    float distanceForWidth = (horizontalSize * 0.5f) / std::tan(horizontalFov * 0.5f);

    // Use the larger distance to ensure model fits, with margin for padding
    float distance = std::max(distanceForHeight, distanceForWidth) * 1.1f;

    // Clamp to reasonable range
    if (distance < minCameraDistance_) distance = minCameraDistance_;
    if (distance > maxCameraDistance_) distance = maxCameraDistance_;

    // Store for zoom functionality
    cameraDistance_ = distance;
    modelCenterY_ = modelCenter;

    // Position camera slightly above center, looking at model center
    float cameraHeight = modelCenter + extent.Y * 0.1f;
    camera_->setPosition(irr::core::vector3df(0, cameraHeight, -distance));
    camera_->setTarget(irr::core::vector3df(0, modelCenter, 0));

    LOG_DEBUG(MOD_UI, "CharacterModelView: Adjusted camera - extents=({:.1f},{:.1f},{:.1f}) center={:.1f} dist={:.1f}",
              extent.X, extent.Y, extent.Z, modelCenter, distance);
}

void CharacterModelView::onMouseDown(int x, int y) {
    isDragging_ = true;
    dragStartX_ = x;
    dragStartY_ = y;
    dragStartRotation_ = rotationY_;
    dragStartDistance_ = cameraDistance_;
}

void CharacterModelView::onMouseMove(int x, int y) {
    if (!isDragging_) return;

    // Calculate rotation delta (horizontal movement)
    // Positive X movement = rotate right (positive Y rotation)
    float deltaX = static_cast<float>(x - dragStartX_);
    rotationY_ = dragStartRotation_ + deltaX;

    // Normalize to 0-360 range
    while (rotationY_ >= 360.0f) rotationY_ -= 360.0f;
    while (rotationY_ < 0.0f) rotationY_ += 360.0f;

    // Calculate zoom delta (vertical movement)
    // Dragging up (negative Y) = zoom in (decrease distance)
    // Dragging down (positive Y) = zoom out (increase distance)
    float deltaY = static_cast<float>(y - dragStartY_);
    float zoomSensitivity = 0.125f;  // Pixels per unit distance (reduced for finer control)
    cameraDistance_ = dragStartDistance_ + deltaY * zoomSensitivity;

    // Clamp to limits
    if (cameraDistance_ < minCameraDistance_) cameraDistance_ = minCameraDistance_;
    if (cameraDistance_ > maxCameraDistance_) cameraDistance_ = maxCameraDistance_;

    // Apply rotation to model
    if (characterNode_) {
        characterNode_->setRotation(irr::core::vector3df(0, rotationY_, 0));
    }

    // Update camera position
    if (camera_) {
        irr::core::vector3df pos = camera_->getPosition();
        pos.Z = -cameraDistance_;
        camera_->setPosition(pos);
    }
}

void CharacterModelView::onMouseUp() {
    isDragging_ = false;
}

void CharacterModelView::setRotationY(float angle) {
    rotationY_ = angle;

    // Normalize
    while (rotationY_ >= 360.0f) rotationY_ -= 360.0f;
    while (rotationY_ < 0.0f) rotationY_ += 360.0f;

    // Apply to model
    if (characterNode_) {
        characterNode_->setRotation(irr::core::vector3df(0, rotationY_, 0));
    }
}

void CharacterModelView::update(float deltaTimeMs) {
    if (!modelSmgr_ || !characterNode_) return;

    // The animated mesh scene node updates automatically via OnAnimate
    // which is called during drawAll(). We just need to ensure the scene
    // manager's timer is updated, which happens in renderToTexture().
}

// Helper to extract rotation from BoneMat4 (copied from model_viewer.cpp)
// Swaps Y and Z for EQ to Irrlicht coordinate conversion
static irr::core::vector3df extractBoneRotation(const EQT::Graphics::BoneMat4& m) {
    // Build an Irrlicht matrix from the bone transform, swapping Y/Z for coordinate conversion
    // EQ matrix is column-major: m[0-3] = col0, m[4-7] = col1, etc.
    // We need to swap Y and Z axes
    irr::core::matrix4 irrMat;
    irrMat[0] = m.m[0];   irrMat[1] = m.m[2];   irrMat[2] = m.m[1];   irrMat[3] = 0;
    irrMat[4] = m.m[8];   irrMat[5] = m.m[10];  irrMat[6] = m.m[9];   irrMat[7] = 0;
    irrMat[8] = m.m[4];   irrMat[9] = m.m[6];   irrMat[10] = m.m[5];  irrMat[11] = 0;
    irrMat[12] = m.m[12]; irrMat[13] = m.m[14]; irrMat[14] = m.m[13]; irrMat[15] = 1;
    return irrMat.getRotationDegrees();
}

// Helper to find bone index trying multiple naming conventions
// Matches entity_renderer.cpp implementation for comprehensive bone lookup
static int findBoneIndex(const std::shared_ptr<EQT::Graphics::CharacterSkeleton>& skeleton,
                         const std::string& raceCode,
                         const std::vector<std::string>& suffixes) {
    if (!skeleton) return -1;

    for (const auto& suffix : suffixes) {
        // Try uppercase race code (e.g., "HUMr_point")
        std::string upperName = raceCode + suffix;
        int idx = skeleton->getBoneIndex(upperName);
        if (idx >= 0) return idx;

        // Try with underscore between race and suffix (e.g., "HUM_r_point")
        idx = skeleton->getBoneIndex(raceCode + "_" + suffix);
        if (idx >= 0) return idx;

        // Try lowercase race code (e.g., "humr_point")
        std::string lowerRace = raceCode;
        std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        std::string lowerName = lowerRace + suffix;
        idx = skeleton->getBoneIndex(lowerName);
        if (idx >= 0) return idx;

        // Try lowercase with underscore (e.g., "hum_r_point")
        idx = skeleton->getBoneIndex(lowerRace + "_" + suffix);
        if (idx >= 0) return idx;

        // Try without leading underscore in suffix (e.g., "qcmr_point" instead of "qcm_r_point")
        if (suffix.length() > 1 && suffix[0] == '_') {
            std::string noUnderscoreSuffix = suffix.substr(1);
            idx = skeleton->getBoneIndex(lowerRace + noUnderscoreSuffix);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(raceCode + noUnderscoreSuffix);
            if (idx >= 0) return idx;
        }

        // Try with _DAG suffix removed if present (e.g., "HUM_R_POINT" instead of "HUM_R_POINT_DAG")
        if (suffix.length() > 4 && suffix.substr(suffix.length() - 4) == "_DAG") {
            std::string noDAG = suffix.substr(0, suffix.length() - 4);
            idx = skeleton->getBoneIndex(raceCode + noDAG);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(raceCode + "_" + noDAG);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(lowerRace + noDAG);
            if (idx >= 0) return idx;
            idx = skeleton->getBoneIndex(lowerRace + "_" + noDAG);
            if (idx >= 0) return idx;
        }
    }
    return -1;
}

void CharacterModelView::attachWeapons() {
    if (!characterNode_ || !equipmentModelLoader_) {
        return;
    }

    // Get primary and secondary equipment IDs
    uint32_t primaryId = storedEquipment_[7];    // Primary slot (index 7)
    uint32_t secondaryId = storedEquipment_[8];  // Secondary slot (index 8)

    LOG_DEBUG(MOD_UI, "CharacterModelView::attachWeapons primary={} secondary={}", primaryId, secondaryId);

    // Get skeleton and bone states for bone lookup
    EQT::Graphics::SkeletalAnimator& animator = characterNode_->getAnimator();
    auto skeleton = animator.getSkeleton();
    const auto& boneStates = animator.getBoneStates();

    // Get race code for bone name lookup (must account for gender)
    // E.g., Dark Elf Female needs "DAF" not "DAM"
    std::string baseRaceCode = EQT::Graphics::RaceModelLoader::getRaceCode(currentRaceId_);
    std::string raceCode = EQT::Graphics::getGenderedRaceCode(baseRaceCode, currentGender_);
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });

    // Attach primary weapon (right hand)
    if (primaryId != 0 && primaryId != currentPrimaryId_) {
        // Remove old weapon if exists
        if (primaryWeaponNode_) {
            primaryWeaponNode_->remove();
            primaryWeaponNode_ = nullptr;
        }
        primaryBoneIndex_ = -1;
        primaryWeaponOffset_ = 0.0f;

        irr::scene::IMesh* primaryMesh = equipmentModelLoader_->getEquipmentMesh(primaryId);
        if (primaryMesh) {
            primaryWeaponNode_ = modelSmgr_->addMeshSceneNode(primaryMesh, characterNode_);
            if (primaryWeaponNode_) {
                currentPrimaryId_ = primaryId;
                primaryWeaponNode_->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));

                // Find right hand bone (skeleton lookup only needs skeleton, not bone states)
                if (skeleton) {
                    LOG_DEBUG(MOD_UI, "CharacterModelView: Searching for R_POINT bone (race={}). Available bones ({}):",
                              raceCode, skeleton->bones.size());
                    for (size_t i = 0; i < skeleton->bones.size() && i < 20; ++i) {
                        LOG_DEBUG(MOD_UI, "  [{}] {}", i, skeleton->bones[i].name);
                    }
                    if (skeleton->bones.size() > 20) {
                        LOG_DEBUG(MOD_UI, "  ... and {} more bones", skeleton->bones.size() - 20);
                    }

                    std::vector<std::string> suffixes = {
                        "r_point", "R_POINT", "R_POINT_DAG",
                        "BO_R_DAG", "TO_R_DAG",
                        "r_point_dag"
                    };
                    primaryBoneIndex_ = findBoneIndex(skeleton, raceCode, suffixes);
                    LOG_DEBUG(MOD_UI, "CharacterModelView: R_POINT bone search result: index={}", primaryBoneIndex_);
                }

                // If bone wasn't found, set fallback position (approximate right hand area)
                if (primaryBoneIndex_ < 0) {
                    LOG_WARN(MOD_UI, "CharacterModelView: Could not find R_POINT bone for race {}", raceCode);
                    primaryWeaponNode_->setPosition(irr::core::vector3df(2.0f, 5.0f, 0.0f));
                }

                // Calculate weapon offset based on weapon length
                // Long weapons (2H) should be gripped ~1/3 from bottom, short weapons near hilt
                irr::core::aabbox3df bbox = primaryMesh->getBoundingBox();
                float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;

                // Two-handed weapons are typically longer (> 3 units)
                bool isTwoHanded = (weaponLength > 3.0f);
                if (isTwoHanded) {
                    // Two-handed: grip about 1/3 from bottom (25% from center)
                    primaryWeaponOffset_ = weaponLength * 0.25f;
                } else {
                    // One-handed: grip near the hilt (80% toward hilt end)
                    primaryWeaponOffset_ = -bbox.MinEdge.X * 0.8f;
                }

                // Set initial position and rotation
                irr::core::vector3df rot(180, 0, 0);
                primaryWeaponNode_->setRotation(rot);

                for (irr::u32 i = 0; i < primaryWeaponNode_->getMaterialCount(); ++i) {
                    primaryWeaponNode_->getMaterial(i).Lighting = false;
                    primaryWeaponNode_->getMaterial(i).BackfaceCulling = false;
                }
                LOG_DEBUG(MOD_UI, "CharacterModelView: Attached primary weapon {} (bone={} offset={:.2f})",
                          primaryId, primaryBoneIndex_, primaryWeaponOffset_);
            }
        }
    }

    // Attach secondary weapon/shield (left hand)
    if (secondaryId != 0 && secondaryId != currentSecondaryId_) {
        // Remove old weapon if exists
        if (secondaryWeaponNode_) {
            secondaryWeaponNode_->remove();
            secondaryWeaponNode_ = nullptr;
        }
        secondaryBoneIndex_ = -1;
        secondaryWeaponOffset_ = 0.0f;

        irr::scene::IMesh* secondaryMesh = equipmentModelLoader_->getEquipmentMesh(secondaryId);
        if (secondaryMesh) {
            secondaryWeaponNode_ = modelSmgr_->addMeshSceneNode(secondaryMesh, characterNode_);
            if (secondaryWeaponNode_) {
                currentSecondaryId_ = secondaryId;
                secondaryIsShield_ = EQT::Graphics::EquipmentModelLoader::isShield(static_cast<int>(secondaryId));
                secondaryWeaponNode_->setScale(irr::core::vector3df(1.0f, 1.0f, 1.0f));

                // Find left hand or shield bone (skeleton lookup only needs skeleton, not bone states)
                if (skeleton) {
                    LOG_DEBUG(MOD_UI, "CharacterModelView: Searching for {} bone (race={}).",
                              secondaryIsShield_ ? "SHIELD_POINT/L_POINT" : "L_POINT", raceCode);

                    std::vector<std::string> suffixes = secondaryIsShield_
                        ? std::vector<std::string>{
                            "shield_point", "SHIELD_POINT", "SHIELD_POINT_DAG",
                            "shield_point_dag",
                            "l_point", "L_POINT", "L_POINT_DAG",
                            "BO_L_DAG", "TO_L_DAG", "l_point_dag"
                          }
                        : std::vector<std::string>{
                            "l_point", "L_POINT", "L_POINT_DAG",
                            "BO_L_DAG", "TO_L_DAG", "l_point_dag"
                          };
                    secondaryBoneIndex_ = findBoneIndex(skeleton, raceCode, suffixes);
                    LOG_DEBUG(MOD_UI, "CharacterModelView: {} bone search result: index={}",
                              secondaryIsShield_ ? "SHIELD_POINT/L_POINT" : "L_POINT", secondaryBoneIndex_);
                }

                // If bone wasn't found, set fallback position (approximate left hand area)
                if (secondaryBoneIndex_ < 0) {
                    LOG_WARN(MOD_UI, "CharacterModelView: Could not find L_POINT/SHIELD_POINT bone for race {}", raceCode);
                    secondaryWeaponNode_->setPosition(irr::core::vector3df(-2.0f, 5.0f, 0.0f));
                }

                // Calculate offset based on item type and size
                irr::core::aabbox3df bbox = secondaryMesh->getBoundingBox();
                if (secondaryIsShield_) {
                    float shieldHeight = bbox.MaxEdge.X - bbox.MinEdge.X;
                    secondaryWeaponOffset_ = shieldHeight * 0.175f;
                } else {
                    // Weapon: check length for grip position
                    float weaponLength = bbox.MaxEdge.X - bbox.MinEdge.X;
                    bool isTwoHanded = (weaponLength > 3.0f);
                    if (isTwoHanded) {
                        secondaryWeaponOffset_ = weaponLength * 0.25f;
                    } else {
                        secondaryWeaponOffset_ = -bbox.MinEdge.X * 0.8f;
                    }
                }

                // Set initial position and rotation
                // Shields: (180, 0, 180) - rotates so outside faces outward
                // Weapons: (180, 0, 0)
                irr::core::vector3df rot = secondaryIsShield_
                    ? irr::core::vector3df(180, 0, 180)
                    : irr::core::vector3df(180, 0, 0);
                secondaryWeaponNode_->setRotation(rot);

                for (irr::u32 i = 0; i < secondaryWeaponNode_->getMaterialCount(); ++i) {
                    secondaryWeaponNode_->getMaterial(i).Lighting = false;
                    secondaryWeaponNode_->getMaterial(i).BackfaceCulling = false;
                }
                LOG_DEBUG(MOD_UI, "CharacterModelView: Attached secondary {} {} (bone={} offset={:.2f})",
                          secondaryIsShield_ ? "shield" : "weapon", secondaryId,
                          secondaryBoneIndex_, secondaryWeaponOffset_);
            }
        }
    }

    // Update weapon positions based on current bone transforms
    updateWeaponTransforms();
}

void CharacterModelView::updateWeaponTransforms() {
    if (!characterNode_) return;
    if (!primaryWeaponNode_ && !secondaryWeaponNode_) return;

    // Get animator and skeleton
    EQT::Graphics::SkeletalAnimator& animator = characterNode_->getAnimator();
    const auto& boneStates = animator.getBoneStates();
    if (boneStates.empty()) return;

    // Update primary weapon (right hand - R_POINT)
    if (primaryWeaponNode_ && primaryBoneIndex_ >= 0 &&
        primaryBoneIndex_ < static_cast<int>(boneStates.size())) {
        const EQT::Graphics::BoneMat4& worldTransform = boneStates[primaryBoneIndex_].worldTransform;

        // Extract position from matrix (column 3)
        float px = worldTransform.m[12];
        float py = worldTransform.m[13];
        float pz = worldTransform.m[14];

        // Extract rotation from matrix and add weapon's base rotation
        irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
        irr::core::vector3df weaponRot = boneRot + irr::core::vector3df(180, 0, 0);
        primaryWeaponNode_->setRotation(weaponRot);

        // Apply offset along weapon's local X axis (blade direction)
        irr::core::matrix4 rotMat;
        rotMat.setRotationDegrees(weaponRot);
        irr::core::vector3df localOffset(primaryWeaponOffset_, 0, 0);
        rotMat.rotateVect(localOffset);

        // Convert EQ(x,y,z) to Irrlicht(x,z,y) and apply rotated offset
        primaryWeaponNode_->setPosition(irr::core::vector3df(
            px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
    }

    // Update secondary weapon/shield (left hand - L_POINT or SHIELD_POINT)
    if (secondaryWeaponNode_ && secondaryBoneIndex_ >= 0 &&
        secondaryBoneIndex_ < static_cast<int>(boneStates.size())) {
        const EQT::Graphics::BoneMat4& worldTransform = boneStates[secondaryBoneIndex_].worldTransform;

        float px = worldTransform.m[12];
        float py = worldTransform.m[13];
        float pz = worldTransform.m[14];

        // Extract rotation from matrix and add item's base rotation
        // Shields: (180, 0, 180) - rotates so outside faces outward
        // Weapons: (180, 0, 0)
        irr::core::vector3df boneRot = extractBoneRotation(worldTransform);
        irr::core::vector3df itemRot = secondaryIsShield_
            ? boneRot + irr::core::vector3df(180, 0, 180)
            : boneRot + irr::core::vector3df(180, 0, 0);
        secondaryWeaponNode_->setRotation(itemRot);

        // Apply offset along local X axis
        irr::core::matrix4 rotMat;
        rotMat.setRotationDegrees(itemRot);
        irr::core::vector3df localOffset(secondaryWeaponOffset_, 0, 0);
        rotMat.rotateVect(localOffset);

        // Convert EQ(x,y,z) to Irrlicht(x,z,y) and apply rotated offset
        secondaryWeaponNode_->setPosition(irr::core::vector3df(
            px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
    }
}

void CharacterModelView::renderToTexture() {
    if (!driver_ || !modelSmgr_ || !renderTarget_) {
        return;
    }

    // Update weapon transforms before rendering
    updateWeaponTransforms();

    // Set our render target
    bool success = driver_->setRenderTarget(renderTarget_, true, true, backgroundColor_);
    if (!success) {
        // RTT may not be supported (software renderer)
        return;
    }

    // Set our camera as active
    if (camera_) {
        modelSmgr_->setActiveCamera(camera_);
    }

    // Draw the model scene
    modelSmgr_->drawAll();

    // Reset render target to default (screen)
    driver_->setRenderTarget(nullptr, false, false);
}

} // namespace ui
} // namespace eqt
