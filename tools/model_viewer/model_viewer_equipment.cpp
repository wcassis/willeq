// tools/model_viewer/model_viewer_equipment.cpp -- weapon/shield bone
// attachment and per-frame repositioning.
// Bodies moved verbatim from tools/model_viewer.cpp by T385
// (board B167 -- that TU was 3,246 lines, over the 3,000-line hard cap).

#include "model_viewer_state.h"

// Bone lookup and rotation extraction converge (T60) on the shared
// implementation in bone_attachment.h — see that header's comment.
// entity_equipment.cpp's fallback chain (EQT::Graphics::findBoneIndex) is
// used here rather than this tool's previous, narrower chain (no
// underscore-joined "RACE_suffix" variant, no leading-underscore-stripped
// variant beyond one case); this widens what the viewer can find, which is
// fine here — the STOP condition on unchanged behaviour applies only to
// the in-world/production path (entity_equipment.cpp), not this tool.
using EQT::Graphics::findBoneIndex;
using EQT::Graphics::extractBoneRotation;
using EQT::Graphics::computeWeaponGripOffset;
using EQT::Graphics::computeShieldGripOffset;

// Stored bone indices for equipment attachment (cached after first lookup)
int primaryBoneIndex = -1;
int secondaryBoneIndex = -1;
float primaryWeaponOffset = 0.0f;  // Offset to position handle at hand
float secondaryWeaponOffset = 0.0f;  // Offset for shield/secondary weapon

void updateEquipmentPositions() {
    if (!animatedNode || !useAnimatedMesh) return;

    auto skeleton = animatedNode->getEQMesh()->getSkeleton();
    if (!skeleton) return;

    const auto& boneStates = animatedNode->getAnimator().getBoneStates();
    if (boneStates.empty()) return;

    // Update primary weapon position and rotation
    if (primaryEquipNode && primaryBoneIndex >= 0 && primaryBoneIndex < static_cast<int>(boneStates.size())) {
        const BoneMat4& worldXform = boneStates[primaryBoneIndex].worldTransform;
        float px = worldXform.m[12];
        float py = worldXform.m[13];
        float pz = worldXform.m[14];

        // Extract and apply bone rotation, then add weapon's base rotation
        core::vector3df boneRot = extractBoneRotation(worldXform);
        core::vector3df weaponRot = boneRot + core::vector3df(180, 0, 0);
        primaryEquipNode->setRotation(weaponRot);

        // Apply offset along the weapon's local X axis (blade direction)
        // Transform offset by the weapon's rotation to get world-space offset
        core::matrix4 rotMat;
        rotMat.setRotationDegrees(weaponRot);
        core::vector3df localOffset(primaryWeaponOffset, 0, 0);  // Offset along blade
        rotMat.rotateVect(localOffset);

        // Convert EQ(x,y,z) to Irrlicht(x,z,y) and apply rotated offset
        primaryEquipNode->setPosition(core::vector3df(px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
    }

    // Update secondary weapon/shield position and rotation
    if (secondaryEquipNode && secondaryBoneIndex >= 0 && secondaryBoneIndex < static_cast<int>(boneStates.size())) {
        const BoneMat4& worldXform = boneStates[secondaryBoneIndex].worldTransform;
        float px = worldXform.m[12];
        float py = worldXform.m[13];
        float pz = worldXform.m[14];

        // Extract and apply bone rotation, then add the secondary-slot base
        // rotation (T65: shields and plain weapons use the same one -- see
        // bone_attachment.h's computeSecondaryItemBaseRotation()).
        core::vector3df boneRot = extractBoneRotation(worldXform);
        core::vector3df shieldRot = boneRot + computeSecondaryItemBaseRotation();
        secondaryEquipNode->setRotation(shieldRot);

        // Apply offset along local X axis
        core::matrix4 rotMat;
        rotMat.setRotationDegrees(shieldRot);
        core::vector3df localOffset(secondaryWeaponOffset, 0, 0);
        rotMat.rotateVect(localOffset);

        // Convert EQ(x,y,z) to Irrlicht(x,z,y) and apply rotated offset
        secondaryEquipNode->setPosition(core::vector3df(px + localOffset.X, pz + localOffset.Y, py + localOffset.Z));
    }
}

// Attach equipment models (weapons/shields) to the current entity
void attachEquipmentModels(uint32_t primaryItemId, uint32_t secondaryItemId) {
    std::cout << "attachEquipmentModels(" << primaryItemId << ", " << secondaryItemId << ")" << std::endl << std::flush;

    // Remove existing equipment nodes and reset bone indices/offsets
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    primaryBoneIndex = -1;
    primaryWeaponOffset = 0.0f;

    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }
    secondaryBoneIndex = -1;
    secondaryWeaponOffset = 0.0f;

    std::cout << "  Checking equipmentModelLoader..." << std::flush;
    if (!equipmentModelLoader || !equipmentModelLoader->isLoaded()) {
        std::cout << " not loaded, returning" << std::endl;
        return;
    }
    std::cout << " OK" << std::endl;

    // Get the parent node - use animatedNode if in animated mode, modelNode for static mode
    scene::ISceneNode* parentNode = nullptr;
    if (animatedNode && useAnimatedMesh) {
        parentNode = animatedNode;
    } else if (modelNode) {
        parentNode = modelNode;
    }

    std::cout << "Equipment parent: " << (parentNode ? (useAnimatedMesh ? "animatedNode" : "modelNode") : "none (root)") << std::endl << std::flush;

    // Get skeleton and bone states from animated node (only available in animated mode)
    std::shared_ptr<CharacterSkeleton> skeleton;
    const std::vector<AnimatedBoneState>* boneStates = nullptr;

    // Get race code for bone name lookup (uppercase)
    std::string raceCode = currentRaceCode;
    std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                  [](unsigned char c) { return std::toupper(c); });
    std::cout << "  Race code for bone lookup: " << raceCode << std::endl;

    if (animatedNode) {
        std::cout << "  Getting skeleton from animatedNode..." << std::flush;
        EQAnimatedMesh* eqMesh = animatedNode->getEQMesh();
        std::cout << " eqMesh=" << (eqMesh ? "OK" : "NULL") << std::flush;
        if (eqMesh) {
            skeleton = eqMesh->getSkeleton();
            std::cout << " skeleton=" << (skeleton ? "OK" : "NULL") << std::flush;
        }
        std::cout << std::endl;

        std::cout << "  Getting bone states..." << std::flush;
        const auto& states = animatedNode->getAnimator().getBoneStates();
        boneStates = &states;
        std::cout << " count=" << boneStates->size() << std::endl << std::flush;

        // Debug: list all bone names
        if (skeleton) {
            std::cout << "  Available bones: ";
            for (size_t i = 0; i < skeleton->bones.size() && i < 10; ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << skeleton->bones[i].name;
            }
            if (skeleton->bones.size() > 10) std::cout << "... (" << skeleton->bones.size() << " total)";
            std::cout << std::endl;
        }
    }

    // Character scale factor - equipment inherits parent scale, so use 1:1 when parented
    const float equipScale = parentNode ? 1.0f : 10.0f;

    // Primary weapon (right hand) - equipment slot 7
    if (primaryItemId > 0) {
        int modelId = static_cast<int>(primaryItemId);
        std::cout << "Looking for primary weapon model IT" << modelId << std::endl;
        scene::IMesh* equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
        std::cout << "  Direct lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;

        if (!equipMesh) {
            int mappedId = equipmentModelLoader->getModelIdForItem(primaryItemId);
            std::cout << "  Item lookup for " << primaryItemId << " -> " << mappedId << std::endl;
            if (mappedId >= 0) {
                modelId = mappedId;
                equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
                std::cout << "  Mapped lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;
            }
        }

        if (equipMesh) {
            const auto* modelData = equipmentModelLoader->getEquipmentModelData(modelId);
            if (modelData) {
                std::cout << "  Source: " << modelData->sourceArchive << " / " << modelData->sourceWld << std::endl;
                std::cout << "  Geometry: " << modelData->geometryName << std::endl;
                std::cout << "  Textures: ";
                for (size_t i = 0; i < modelData->textureNames.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modelData->textureNames[i];
                }
                std::cout << std::endl;
            }

            // Create equipment node parented to character
            primaryEquipNode = smgr->addMeshSceneNode(equipMesh, parentNode);
            if (primaryEquipNode) {
                // Default position/rotation - offset to right side of character
                core::vector3df pos(2.0f, 1.5f, 0.0f);  // Local offset in character space
                // Rotate 180° around X to point outward and face edge inward
                core::vector3df rot(180, 0, 0);

                // Try to find right hand bone (various naming conventions)
                if (skeleton && boneStates && !boneStates->empty()) {
                    // List all bones with relevant keywords to help debug
                    std::cout << "  Available bones with R/POINT/WRIST/HAND:" << std::endl;
                    for (size_t i = 0; i < skeleton->bones.size(); ++i) {
                        const std::string& name = skeleton->bones[i].name;
                        std::string upperName = name;
                        std::transform(upperName.begin(), upperName.end(), upperName.begin(),
                                      [](unsigned char c) { return std::toupper(c); });
                        if (upperName.find("R_") != std::string::npos ||
                            upperName.find("_R") != std::string::npos ||
                            upperName.find("POINT") != std::string::npos ||
                            upperName.find("WRIST") != std::string::npos ||
                            upperName.find("HAND") != std::string::npos) {
                            std::cout << "    [" << i << "] " << name << std::endl;
                        }
                    }
                    // Bone names: qcmr_point (lowercase, no underscore between race and "r")
                    primaryBoneIndex = findBoneIndex(skeleton, raceCode, {"r_point", "R_POINT", "R_POINT_DAG", "BO_R_DAG", "TO_R_DAG"});
                    if (primaryBoneIndex >= 0 && primaryBoneIndex < static_cast<int>(boneStates->size())) {
                        const BoneMat4& worldXform = (*boneStates)[primaryBoneIndex].worldTransform;
                        // Extract position from matrix (EQ coords)
                        float px = worldXform.m[12];
                        float py = worldXform.m[13];
                        float pz = worldXform.m[14];
                        // Convert EQ(x,y,z) to Irrlicht(x,z,y)
                        pos = core::vector3df(px, pz, py);
                        std::cout << "  Bone position: (" << px << ", " << py << ", " << pz << ") -> Irr(" << pos.X << ", " << pos.Y << ", " << pos.Z << ")" << std::endl;
                    }
                }

                // Offset along blade (local X axis) so handle grip is at hand.
                // T60: converged onto the same grip-offset formula
                // entity_equipment.cpp uses per-frame in production (was a
                // flat weaponLength*0.35 here before convergence — a
                // debug-tool-only change, not the in-world path).
                core::aabbox3df bbox = equipMesh->getBoundingBox();
                primaryWeaponOffset = computeWeaponGripOffset(bbox);

                // Apply rotated offset to initial position
                core::matrix4 rotMat;
                rotMat.setRotationDegrees(rot);
                core::vector3df localOffset(primaryWeaponOffset, 0, 0);
                rotMat.rotateVect(localOffset);
                pos += localOffset;

                primaryEquipNode->setPosition(pos);
                primaryEquipNode->setScale(core::vector3df(equipScale, equipScale, equipScale));
                primaryEquipNode->setRotation(rot);
                std::cout << "  Position: (" << pos.X << ", " << pos.Y << ", " << pos.Z << "), scale: " << equipScale << std::endl;

                for (u32 i = 0; i < primaryEquipNode->getMaterialCount(); ++i) {
                    primaryEquipNode->getMaterial(i).BackfaceCulling = false;
                    primaryEquipNode->getMaterial(i).Lighting = false;
                    primaryEquipNode->getMaterial(i).Wireframe = wireframe;
                }

                std::cout << "Attached primary weapon: IT" << modelId << std::endl;
            }
        } else {
            std::cout << "Primary weapon mesh not found: IT" << primaryItemId << std::endl;
        }
    }

    // Secondary weapon/shield (left hand) - equipment slot 8
    if (secondaryItemId > 0) {
        int modelId = static_cast<int>(secondaryItemId);
        std::cout << "Looking for secondary weapon/shield model IT" << modelId << std::endl;
        scene::IMesh* equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
        std::cout << "  Direct lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;

        if (!equipMesh) {
            int mappedId = equipmentModelLoader->getModelIdForItem(secondaryItemId);
            std::cout << "  Item lookup for " << secondaryItemId << " -> " << mappedId << std::endl;
            if (mappedId >= 0) {
                modelId = mappedId;
                equipMesh = equipmentModelLoader->getEquipmentMeshByModelId(modelId);
                std::cout << "  Mapped lookup: " << (equipMesh ? "FOUND" : "not found") << std::endl;
            }
        }

        if (equipMesh) {
            const auto* modelData = equipmentModelLoader->getEquipmentModelData(modelId);
            if (modelData) {
                std::cout << "  Source: " << modelData->sourceArchive << " / " << modelData->sourceWld << std::endl;
                std::cout << "  Geometry: " << modelData->geometryName << std::endl;
                std::cout << "  Textures: ";
                for (size_t i = 0; i < modelData->textureNames.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << modelData->textureNames[i];
                }
                std::cout << std::endl;
            }

            bool isShield = EquipmentModelLoader::isShield(modelId);

            // Create equipment node parented to character
            secondaryEquipNode = smgr->addMeshSceneNode(equipMesh, parentNode);
            if (secondaryEquipNode) {
                // Default position/rotation - offset to left side of character
                core::vector3df pos(-2.0f, 1.5f, 0.0f);  // Local offset in character space
                // T65: shields and plain weapons in this slot use the same
                // base rotation -- see bone_attachment.h's
                // computeSecondaryItemBaseRotation().
                core::vector3df rot = computeSecondaryItemBaseRotation();

                // Try to find left hand bone (various naming conventions)
                // Bone names: qcmshield_point (lowercase, no underscore between race and "shield")
                if (skeleton && boneStates && !boneStates->empty()) {
                    std::vector<std::string> suffixes = isShield
                        ? std::vector<std::string>{"shield_point", "SHIELD_POINT", "SHIELD_POINT_DAG", "l_point", "L_POINT", "L_POINT_DAG", "BO_L_DAG", "TO_L_DAG"}
                        : std::vector<std::string>{"l_point", "L_POINT", "L_POINT_DAG", "BO_L_DAG", "TO_L_DAG"};
                    secondaryBoneIndex = findBoneIndex(skeleton, raceCode, suffixes);
                    if (secondaryBoneIndex >= 0 && secondaryBoneIndex < static_cast<int>(boneStates->size())) {
                        const BoneMat4& worldXform = (*boneStates)[secondaryBoneIndex].worldTransform;
                        float px = worldXform.m[12];
                        float py = worldXform.m[13];
                        float pz = worldXform.m[14];
                        pos = core::vector3df(px, pz, py);
                        std::cout << "  Bone position: (" << px << ", " << py << ", " << pz << ") -> Irr(" << pos.X << ", " << pos.Y << ", " << pos.Z << ")" << std::endl;
                    }
                }

                // Offset shield down (along local X axis after rotation).
                // T60: converged onto entity_equipment.cpp's production
                // flat-constant shield offset (was shieldHeight*0.175 here
                // before convergence — debug-tool-only change).
                if (isShield) {
                    secondaryWeaponOffset = computeShieldGripOffset();

                    core::matrix4 rotMat;
                    rotMat.setRotationDegrees(rot);
                    core::vector3df localOffset(secondaryWeaponOffset, 0, 0);
                    rotMat.rotateVect(localOffset);
                    pos += localOffset;
                }

                secondaryEquipNode->setPosition(pos);
                secondaryEquipNode->setScale(core::vector3df(equipScale, equipScale, equipScale));
                secondaryEquipNode->setRotation(rot);
                std::cout << "  Position: (" << pos.X << ", " << pos.Y << ", " << pos.Z << "), scale: " << equipScale << std::endl;

                for (u32 i = 0; i < secondaryEquipNode->getMaterialCount(); ++i) {
                    secondaryEquipNode->getMaterial(i).BackfaceCulling = false;
                    secondaryEquipNode->getMaterial(i).Lighting = false;
                    secondaryEquipNode->getMaterial(i).Wireframe = wireframe;
                }

                std::cout << "Attached secondary " << (isShield ? "shield" : "weapon")
                          << ": IT" << modelId << std::endl;
            }
        } else {
            std::cout << "Secondary weapon mesh not found: IT" << secondaryItemId << std::endl;
        }
    }
}
