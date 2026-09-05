// tools/model_viewer/model_viewer_entity.cpp -- the entity JSON reader
// (a hand-rolled parser, kept as-is) and entity model loading, cycling
// and texture reload.
// Bodies moved verbatim from tools/model_viewer.cpp by T385
// (board B167 -- that TU was 3,246 lines, over the 3,000-line hard cap).

#include "model_viewer_state.h"

// Valid texture values: 0=naked, 1=leather, 2=chain, 3=plate, 4=monk, 10-16=robes, 17-23=velious
const std::vector<uint8_t> validTextureValues = {0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};

std::string getTextureName(uint8_t texVal) {
    return ""; // these texture names only apply to players and not NPCs, so they're irrelevant when looking at zone entities since they're all npcs!

    switch (texVal) {
        case 0: return "Naked";
        case 1: return "Leather";
        case 2: return "Chain";
        case 3: return "Plate";
        case 4: return "Monk";
        case 10: return "Robe1";
        case 11: return "Robe2";
        case 12: return "Robe3";
        case 13: return "Robe4";
        case 14: return "Robe5";
        case 15: return "Robe6";
        case 16: return "Robe7";
        case 17: return "VelLeather";
        case 18: return "VelChain";
        case 19: return "VelPlate";
        case 20: return "VelCloth";
        case 21: return "VelRing";
        case 22: return "VelScale";
        case 23: return "VelMonk";
        default: return "Unknown";
    }
}

// Forward declaration for texture cycling
void reloadEntityWithTextures();

// Helper to extract a string value from JSON
std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos = json.find("\"", pos + searchKey.length());
    if (pos == std::string::npos) return "";
    size_t endPos = json.find("\"", pos + 1);
    if (endPos == std::string::npos) return "";
    return json.substr(pos + 1, endPos - pos - 1);
}

// Helper to extract an integer value from JSON
int extractJsonInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return 0;
    pos += searchKey.length();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    int value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        value = value * 10 + (json[pos] - '0');
        pos++;
    }
    return value;
}

// Helper to extract an array of integers from JSON
std::vector<uint32_t> extractJsonIntArray(const std::string& json, const std::string& key) {
    std::vector<uint32_t> result;
    std::string searchKey = "\"" + key + "\":";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return result;
    pos = json.find("[", pos);
    if (pos == std::string::npos) return result;
    size_t endPos = json.find("]", pos);
    if (endPos == std::string::npos) return result;

    std::string arrayStr = json.substr(pos + 1, endPos - pos - 1);
    uint32_t value = 0;
    bool inNumber = false;
    for (char c : arrayStr) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            inNumber = true;
        } else if (inNumber) {
            result.push_back(value);
            value = 0;
            inNumber = false;
        }
    }
    if (inNumber) result.push_back(value);
    return result;
}

// Simple JSON parser for entity data (just enough to parse our format)
// Also extracts zone name and stores it in currentZoneName global
std::vector<EntityData> parseEntityFile(const std::string& filename) {
    std::vector<EntityData> entities;
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_ERROR(MOD_MAIN, "Failed to open entity file: {}", filename);
        return entities;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Extract zone name from top-level JSON
    currentZoneName = extractJsonString(content, "zone");
    if (!currentZoneName.empty()) {
        std::cout << "Zone: " << currentZoneName << std::endl;
    }

    // Find each entity object
    size_t pos = 0;
    while ((pos = content.find("\"spawn_id\":", pos)) != std::string::npos) {
        // Find the start of this entity object
        size_t objStart = content.rfind("{", pos);
        if (objStart == std::string::npos) { pos++; continue; }

        // Find the end of this entity object
        size_t objEnd = content.find("}", pos);
        if (objEnd == std::string::npos) break;

        std::string entityJson = content.substr(objStart, objEnd - objStart + 1);

        EntityData entity;
        entity.name = extractJsonString(entityJson, "name");
        entity.race_id = extractJsonInt(entityJson, "race_id");
        entity.gender = extractJsonInt(entityJson, "gender");
        entity.face = extractJsonInt(entityJson, "face");
        entity.haircolor = extractJsonInt(entityJson, "haircolor");
        entity.hairstyle = extractJsonInt(entityJson, "hairstyle");
        entity.beardcolor = extractJsonInt(entityJson, "beardcolor");
        entity.beard = extractJsonInt(entityJson, "beard");
        entity.texture = extractJsonInt(entityJson, "texture");

        auto equipArray = extractJsonIntArray(entityJson, "equipment");
        for (size_t i = 0; i < equipArray.size() && i < 9; i++) {
            entity.equipment[i] = equipArray[i];
        }

        auto tintArray = extractJsonIntArray(entityJson, "equipment_tint");
        for (size_t i = 0; i < tintArray.size() && i < 9; i++) {
            entity.equipment_tint[i] = tintArray[i];
        }

        entities.push_back(entity);
        pos = objEnd + 1;
    }

    std::cout << "Loaded " << entities.size() << " entities from " << filename << std::endl;
    return entities;
}

// Find entity by name (case-insensitive partial match)
int findEntityByName(const std::string& name) {
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (size_t i = 0; i < loadedEntities.size(); ++i) {
        std::string entityName = loadedEntities[i].name;
        std::transform(entityName.begin(), entityName.end(), entityName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (entityName.find(lowerName) != std::string::npos) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Load entity model with equipment textures
void loadEntityModel(int entityIndex) {
    if (entityIndex < 0 || entityIndex >= static_cast<int>(loadedEntities.size())) {
        std::cout << "Invalid entity index: " << entityIndex << std::endl;
        return;
    }

    const EntityData& entity = loadedEntities[entityIndex];
    currentEntityIndex = entityIndex;

    std::cout << "\n=== Loading Entity ===" << std::endl;
    std::cout << "Name: " << entity.name << std::endl;
    std::cout << "Race ID: " << entity.race_id << std::endl;
    std::cout << "Gender: " << (int)entity.gender << std::endl;
    std::cout << "Face: " << (int)entity.face << std::endl;
    std::cout << "Texture: " << (int)entity.texture << std::endl;
    std::cout << "Equipment: [";
    for (int i = 0; i < 9; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << entity.equipment[i];
    }
    std::cout << "]" << std::endl;
    std::cout << "Equipment Tint: [";
    for (int i = 0; i < 9; i++) {
        if (i > 0) std::cout << ", ";
        std::cout << entity.equipment_tint[i];
    }
    std::cout << "]" << std::endl;

    // Remove equipment nodes first (they may be children of model/animated nodes)
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }

    // Remove existing nodes
    if (animatedNode) {
        animatedNode->remove();
        animatedNode = nullptr;
    }
    if (modelNode) {
        modelNode->remove();
        modelNode = nullptr;
    }

    // Update current race info
    currentRaceId = entity.race_id;
    currentGender = entity.gender;

    // Get base race code and apply gender suffix for playable races
    // E.g., HUM -> HUF for female human, BAM -> BAF for female barbarian
    std::string baseRaceCode = RaceModelLoader::getRaceCode(entity.race_id);
    if (!baseRaceCode.empty() && baseRaceCode.length() == 3 && baseRaceCode.back() == 'M' && entity.gender == 1) {
        currentRaceCode = baseRaceCode;
        currentRaceCode.back() = 'F';  // Convert male to female code
    } else {
        currentRaceCode = baseRaceCode;
    }
    std::cout << "Race Code: " << currentRaceCode << " (gender=" << (int)entity.gender << ")" << std::endl;

    // Try to load body/head variants from zone-specific S3D first, then fall back to global
    bool variantsLoaded = false;

    // 1. Try zone-specific _chr.s3d (e.g., qeynos2_chr.s3d for QCM)
    if (!currentZoneName.empty()) {
        std::string zoneChrPath = clientPath + currentZoneName + "_chr.s3d";
        if (IsDebugEnabled()) {
            std::cout << "Trying to load " << currentRaceCode << " variants from " << zoneChrPath << std::endl;
        }
        variantsLoaded = loadVariantsFromS3D(zoneChrPath, currentRaceCode);
    }

    // 2. If not found, try global_chr.s3d with race code
    if (!variantsLoaded) {
        std::string globalChrPath = clientPath + "global_chr.s3d";
        if (IsDebugEnabled()) {
            std::cout << "Trying to load " << currentRaceCode << " variants from " << globalChrPath << std::endl;
        }
        variantsLoaded = loadVariantsFromS3D(globalChrPath, currentRaceCode);
    }

    // 3. If still not found, try fallback race code (e.g., QCM -> HUM/HUF)
    if (!variantsLoaded) {
        // Get fallback code for citizen races
        std::string fallbackCode;
        switch (entity.race_id) {
            case 71:  // Qeynos Citizen -> Human
            case 77:  // Freeport Citizen -> Human
            case 15:  // Freeport Guard -> Human
            case 80:  // Karana Citizen -> Human
                fallbackCode = (entity.gender == 1) ? "HUF" : "HUM";
                break;
            case 61:  // Neriak Citizen -> Dark Elf
                fallbackCode = (entity.gender == 1) ? "DAF" : "DAM";
                break;
            default:
                break;
        }

        if (!fallbackCode.empty()) {
            std::string globalChrPath = clientPath + "global_chr.s3d";
            std::cout << "Falling back to " << fallbackCode << " variants from " << globalChrPath << std::endl;
            variantsLoaded = loadVariantsFromS3D(globalChrPath, fallbackCode);
            if (variantsLoaded) {
                currentRaceCode = fallbackCode;  // Update race code to the fallback
            }
        }
    }

    if (!variantsLoaded) {
        std::cout << "Warning: Could not load variants for " << currentRaceCode << std::endl;
    }

    // Build EntityAppearance from entity data
    EntityAppearance appearance;
    appearance.face = entity.face;
    appearance.haircolor = entity.haircolor;
    appearance.hairstyle = entity.hairstyle;
    appearance.beardcolor = entity.beardcolor;
    appearance.beard = entity.beard;
    appearance.texture = entity.texture;
    for (int i = 0; i < 9; i++) {
        appearance.equipment[i] = entity.equipment[i];
        appearance.equipment_tint[i] = entity.equipment_tint[i];
    }

    // Initialize current textures from entity data
    // For NPCs, the texture value determines:
    // 1. Body variant (mesh) - texture value selects which body mesh (0=default, 1=variant 01, etc.)
    // 2. Head variant (mesh) - same as body variant for NPCs
    // 3. Equipment texture - the skin/armor texture to apply
    currentBodyTexture = entity.texture;
    currentHelmTexture = entity.texture;  // NPCs use same texture for helm as body

    // Set body and head variants based on texture value
    // Texture 0 = variant 0 (default mesh), texture 1 = variant 1, etc.
    if (!bodyVariants.empty()) {
        currentBodyVariant = std::min(static_cast<size_t>(entity.texture), bodyVariants.size() - 1);
    } else {
        currentBodyVariant = 0;
    }

    if (!headVariants.empty()) {
        currentHeadVariant = std::min(static_cast<size_t>(entity.texture), headVariants.size() - 1);
    } else {
        currentHeadVariant = 0;
    }

    std::cout << "Body variant: " << (currentBodyVariant + 1) << "/" << bodyVariants.size();
    if (!bodyVariants.empty() && currentBodyVariant < bodyVariants.size()) {
        std::cout << " (" << bodyVariants[currentBodyVariant]->name << ")";
    }
    std::cout << std::endl;

    std::cout << "Head variant: " << (currentHeadVariant + 1) << "/" << headVariants.size();
    if (!headVariants.empty() && currentHeadVariant < headVariants.size()) {
        std::cout << " (" << headVariants[currentHeadVariant]->name << ")";
    }
    std::cout << std::endl;

    if (useEquipmentTextures) {
        std::cout << "Equipment textures: ON (texture=" << (int)currentBodyTexture
                  << " " << getTextureName(currentBodyTexture) << ")" << std::endl;
    }

    // Start in static mode - build static mesh from current variants
    useAnimatedMesh = false;
    rebuildCurrentVariant();

    // Attach equipment models (weapons/shields) if present
    // Equipment slots: 7 = Primary (right hand), 8 = Secondary (left hand/shield)
    if (entity.equipment[7] > 0 || entity.equipment[8] > 0) {
        attachEquipmentModels(entity.equipment[7], entity.equipment[8]);
    }

    std::cout << "Loaded entity in static mode. Press 'S' to switch to animated mode." << std::endl;
}

// Cycle through loaded entities
void cycleEntity(int direction) {
    if (loadedEntities.empty()) {
        std::cout << "No entities loaded. Use --entities <file.json> to load entity data." << std::endl;
        return;
    }

    currentEntityIndex += direction;
    if (currentEntityIndex < 0) {
        currentEntityIndex = static_cast<int>(loadedEntities.size()) - 1;
    } else if (currentEntityIndex >= static_cast<int>(loadedEntities.size())) {
        currentEntityIndex = 0;
    }

    // Update current textures from entity data
    if (currentEntityIndex >= 0 && currentEntityIndex < static_cast<int>(loadedEntities.size())) {
        currentBodyTexture = loadedEntities[currentEntityIndex].texture;
        currentHelmTexture = loadedEntities[currentEntityIndex].equipment[0];  // Head slot
    }

    loadEntityModel(currentEntityIndex);
}

// Helper to get equipment variant texture name (same logic as static mode buildMesh)
std::string getEquipmentVariantTextureName(const std::string& baseTexName, uint8_t textureVariant) {
    if (textureVariant == 0) {
        return baseTexName;  // No override for variant 0
    }

    std::string lowerTexName = baseTexName;
    std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    // Pattern: {race}{part}01{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
    // Variant 1 textures have "01" in the name, variant 2 has "02", etc.
    // e.g., qcmhe0101.bmp is already variant 1, qcmch0001.bmp base -> qcmch0101.bmp for variant 1

    // First try to find "00" (base texture) and replace with variant
    size_t pos = lowerTexName.find("00");
    if (pos != std::string::npos && pos >= 3 && lowerTexName.length() > pos + 4) {
        char variantStr[8];
        snprintf(variantStr, sizeof(variantStr), "%02d", textureVariant);
        return lowerTexName.substr(0, pos) + variantStr + lowerTexName.substr(pos + 2);
    }

    return baseTexName;  // Return original if pattern doesn't match
}

// Reload current model with updated body/helm textures
void reloadEntityWithTextures() {
    if (!raceModelLoader) {
        std::cout << "Race model loader not initialized" << std::endl;
        return;
    }

    std::cout << "\n=== Reloading with textures ===" << std::endl;
    std::cout << "Equipment: " << (useEquipmentTextures ? "ON" : "OFF");
    if (useEquipmentTextures) {
        std::cout << " Body: " << (int)currentBodyTexture << " (" << getTextureName(currentBodyTexture) << ")";
    }
    std::cout << std::endl;

    // Remove equipment nodes first (they may be children of model/animated nodes)
    if (primaryEquipNode) {
        primaryEquipNode->remove();
        primaryEquipNode = nullptr;
    }
    if (secondaryEquipNode) {
        secondaryEquipNode->remove();
        secondaryEquipNode = nullptr;
    }

    // Remove existing nodes
    if (animatedNode) {
        animatedNode->remove();
        animatedNode = nullptr;
    }
    if (modelNode) {
        modelNode->remove();
        modelNode = nullptr;
    }

    // Create animated node with current body/head variants
    animatedNode = raceModelLoader->createAnimatedNodeWithAppearance(
        currentRaceId, currentGender,
        static_cast<uint8_t>(currentHeadVariant),
        static_cast<uint8_t>(currentBodyVariant),
        nullptr, -1);

    if (animatedNode) {
        animatedNode->setScale(core::vector3df(10, 10, 10));
        animatedNode->setPosition(core::vector3df(0, 0, 0));
        animatedNode->setRotation(core::vector3df(0, 90, 0));  // Rotate to face camera

        // Apply equipment textures using same logic as static mode
        if (useEquipmentTextures) {
            if (IsDebugEnabled()) {
                std::cout << "  Applying equipment texture overrides (animated mode):" << std::endl;
                std::cout << "    Body texture: " << (int)currentBodyTexture << ", Helm texture: " << (int)currentHelmTexture << std::endl;
            }
            // Get the mesh to read current texture names
            irr::scene::IAnimatedMesh* animMesh = animatedNode->getMesh();
            if (animMesh) {
                irr::scene::IMesh* mesh = animMesh->getMesh(0);
                if (mesh) {
                    for (u32 i = 0; i < mesh->getMeshBufferCount() && i < animatedNode->getMaterialCount(); ++i) {
                        irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(i);
                        if (!buffer) continue;

                        irr::video::ITexture* currentTex = buffer->getMaterial().getTexture(0);
                        if (!currentTex) continue;

                        std::string currentTexName = currentTex->getName().getPath().c_str();
                        // Extract just filename
                        size_t lastSlash = currentTexName.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            currentTexName = currentTexName.substr(lastSlash + 1);
                        }
                        // Strip "eqt_tex_" or "model_viewer_" prefix if present
                        if (currentTexName.find("eqt_tex_") == 0) {
                            currentTexName = currentTexName.substr(8);
                        } else if (currentTexName.find("model_viewer_") == 0) {
                            currentTexName = currentTexName.substr(13);
                        }

                        std::string lowerTexName = currentTexName;
                        std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                                      [](unsigned char c) { return std::tolower(c); });

                        // Check if this is a head texture (contains "he" after race code, e.g., humhe0001.bmp)
                        bool isHeadTexture = false;
                        /*
			size_t hePos = lowerTexName.find("he");
                        if (hePos != std::string::npos && hePos >= 3 && hePos <= 4) {
                            // Check if followed by digits (head texture pattern)
                            if (hePos + 2 < lowerTexName.size() && std::isdigit(lowerTexName[hePos + 2])) {
                                isHeadTexture = true;
                            }
                        }
			*/

                        // Determine which texture variant to use
                        uint8_t textureVariant = isHeadTexture ? currentHelmTexture : currentBodyTexture;

                        if (textureVariant > 0) {
                            // Get equipment variant texture name
                            std::string equipTexName = getEquipmentVariantTextureName(currentTexName, textureVariant);

                            if (equipTexName != currentTexName) {
                                // Look up the equipment texture in characterTextures
                                std::string lowerEquipTex = equipTexName;
                                std::transform(lowerEquipTex.begin(), lowerEquipTex.end(), lowerEquipTex.begin(),
                                              [](unsigned char c) { return std::tolower(c); });

                                std::string lowerBaseTex = currentTexName;
                                std::transform(lowerBaseTex.begin(), lowerBaseTex.end(), lowerBaseTex.begin(),
                                              [](unsigned char c) { return std::tolower(c); });

                                auto texIt = characterTextures.find(lowerEquipTex);
                                if (texIt != characterTextures.end() && texIt->second && !texIt->second->data.empty()) {
                                    irr::video::ITexture* equipTex = loadTexture(equipTexName, texIt->second->data);
                                    if (equipTex) {
                                        animatedNode->getMaterial(i).setTexture(0, equipTex);
                                        if (IsDebugEnabled()) {
                                            std::cout << "    Texture override (" << (isHeadTexture ? "head" : "body") << "): "
                                                      << currentTexName << " -> " << equipTexName << std::endl;
                                        }
                                    }
                                } else if (IsDebugEnabled()) {
                                    std::cout << "    Texture override failed: " << equipTexName << " not found in cache" << std::endl;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Get available animations
        availableAnimations = animatedNode->getAnimationList();

        // Restore animation if one was playing, or start with 'o01' (idle) if available
        if (currentAnimIndex >= 0 && currentAnimIndex < static_cast<int>(availableAnimations.size())) {
            animatedNode->playAnimation(availableAnimations[currentAnimIndex], true);
            animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
        } else if (!availableAnimations.empty()) {
            // Prefer 'o01' (idle/stand) animation if it exists
            currentAnimIndex = 0;
            auto it = std::find(availableAnimations.begin(), availableAnimations.end(), "o01");
            if (it != availableAnimations.end()) {
                currentAnimIndex = static_cast<int>(std::distance(availableAnimations.begin(), it));
            }
            animatedNode->playAnimation(availableAnimations[currentAnimIndex], true);
            animatedNode->setAnimationSpeed(10.0f * globalAnimationSpeed);
        }

        for (u32 i = 0; i < animatedNode->getMaterialCount(); ++i) {
            animatedNode->getMaterial(i).Wireframe = wireframe;
            animatedNode->getMaterial(i).BackfaceCulling = false;
            animatedNode->getMaterial(i).Lighting = false;
        }

        std::cout << "Reloaded with " << animatedNode->getMaterialCount() << " materials" << std::endl;

        // Update hand bone indices for spell particle emission
        if (spellTestMode) {
            casterRightHandBoneIdx = animatedNode->findRightHandBoneIndex();
            casterLeftHandBoneIdx = animatedNode->findLeftHandBoneIndex();
            std::cout << "  Hand bones: R=" << casterRightHandBoneIdx
                      << " L=" << casterLeftHandBoneIdx << std::endl;
        }

        // Attach equipment models if we have entity data
        if (currentEntityIndex >= 0 && currentEntityIndex < static_cast<int>(loadedEntities.size())) {
            const EntityData& entity = loadedEntities[currentEntityIndex];
            if (entity.equipment[7] > 0 || entity.equipment[8] > 0) {
                attachEquipmentModels(entity.equipment[7], entity.equipment[8]);
            }
        }
    } else {
        std::cout << "Failed to create animated mesh" << std::endl;
    }
}
