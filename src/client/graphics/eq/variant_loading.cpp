// RaceModelLoader variant loading - race and appearance to mesh resolution
// These methods remain part of the RaceModelLoader class
//
// Originally split from race_model_loader.cpp; split three ways by
// refactor/J35_variant_loading_split.md (move-only, verbatim relocation).
// This TU holds the three entry points that resolve a race plus an
// appearance to a mesh: getMeshForRaceWithAppearance,
// loadModelFromGlobalChrWithVariants and getAnimatedMeshWithAppearance.
// Siblings: variant_loading_animation.cpp (the animation-aware variant
// loader) and variant_loading_preload.cpp (the variant cache and preloader).

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/geometry_combiner.h"
#include "client/graphics/eq/s3d_loader.h"
#include "common/logging.h"
#include <algorithm>
#include <cstdio>
#include <iostream>

namespace EQT {
namespace Graphics {

irr::scene::IMesh* RaceModelLoader::getMeshForRaceWithAppearance(uint16_t raceId, uint8_t gender,
                                                                  uint8_t headVariant, uint8_t bodyVariant) {
    // For now, only old models support variant selection
    if (!useOldModels_) {
        // New models don't have the same variant system - just use default
        return getMeshForRace(raceId, gender);
    }

    uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant);

    // Check variant mesh cache first
    auto meshIt = variantMeshCache_.find(key);
    if (meshIt != variantMeshCache_.end()) {
        return meshIt->second;
    }

    // Check if variant model data is loaded
    std::shared_ptr<RaceModelData> modelData;
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        auto modelIt = variantModels_.find(key);
        if (modelIt != variantModels_.end()) {
            modelData = modelIt->second;
        }
    }

    if (!modelData) {
        // Try to load the model with specific variants
        if (loadModelFromGlobalChrWithVariants(raceId, gender, headVariant, bodyVariant)) {
            std::lock_guard<std::mutex> lock(variantModelsMutex_);
            auto modelIt = variantModels_.find(key);
            if (modelIt != variantModels_.end()) {
                modelData = modelIt->second;
            }
        }

        if (!modelData) {
            // Fall back to default model
            variantMeshCache_[key] = getMeshForRace(raceId, gender);
            return variantMeshCache_[key];
        }
    }

    // Build mesh from model data
    if (!modelData || !modelData->combinedGeometry) {
        variantMeshCache_[key] = nullptr;
        return nullptr;
    }

    irr::scene::IMesh* mesh = nullptr;
    if (!modelData->textures.empty() && !modelData->combinedGeometry->textureNames().empty()) {
        mesh = meshBuilder_->buildTexturedMesh(*modelData->combinedGeometry, modelData->textures, true);
    } else {
        mesh = meshBuilder_->buildColoredMesh(*modelData->combinedGeometry);
    }

    variantMeshCache_[key] = mesh;
    return mesh;
}

bool RaceModelLoader::loadModelFromGlobalChrWithVariants(uint16_t raceId, uint8_t gender,
                                                          uint8_t headVariant, uint8_t bodyVariant) {
    if (!globalModelsLoaded_) {
        if (!loadGlobalModels()) {
            return false;
        }
    }

    // Build list of race codes to try (primary first, then fallback)
    std::vector<std::string> codesToTry;

    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        // Get gender-specific code (e.g., HUM -> HUF for female)
        std::string raceCode = getGenderedRaceCode(baseRaceCode, gender);
        std::transform(raceCode.begin(), raceCode.end(), raceCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        codesToTry.push_back(raceCode);
    }

    // Add fallback code for citizen races (e.g., QCM -> HUM)
    std::string fallbackCode = getFallbackRaceCode(raceId, gender);
    if (!fallbackCode.empty()) {
        std::transform(fallbackCode.begin(), fallbackCode.end(), fallbackCode.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        if (std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
            codesToTry.push_back(fallbackCode);
        }
    }

    if (codesToTry.empty()) {
        return false;
    }

    // Try each race code in order
    for (const std::string& codeToTry : codesToTry) {
        // Build expected mesh names for body and head
        // Body: "{RACE}_DMSPRITEDEF" for variant 0, "{RACE}01_DMSPRITEDEF" for variant 1, etc.
        // Head: "{RACE}HE00_DMSPRITEDEF" for variant 0, "{RACE}HE01_DMSPRITEDEF" for variant 1, etc.
        std::string bodyMeshName;
        if (bodyVariant == 0) {
            bodyMeshName = codeToTry + "_DMSPRITEDEF";
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s%02d_DMSPRITEDEF", codeToTry.c_str(), bodyVariant);
            bodyMeshName = buf;
        }

        char headBuf[32];
        snprintf(headBuf, sizeof(headBuf), "%sHE%02d_DMSPRITEDEF", codeToTry.c_str(), headVariant);
        std::string headMeshName = headBuf;

        // Search for a character model that matches this race code
        for (const auto& character : globalCharacters_) {
            if (!character || character->parts.empty()) {
                continue;
            }

            std::string charName = character->name;
            std::transform(charName.begin(), charName.end(), charName.begin(),
                           [](unsigned char c) { return std::toupper(c); });

            // Look for race code in character name
            if (charName.find(codeToTry) != std::string::npos) {
                // Found a matching character - now find specific body and head parts
                std::vector<CharacterPart> selectedParts;

                for (const auto& part : character->partsWithTransforms) {
                    if (!part.geometry) continue;

                    std::string partName = part.geometry->name;
                    std::transform(partName.begin(), partName.end(), partName.begin(),
                                   [](unsigned char c) { return std::toupper(c); });

                    // Check if this is the body or head we want
                    if (partName == bodyMeshName || partName == headMeshName) {
                        selectedParts.push_back(part);
                    }
                }

                // If we didn't find variant parts, continue to next code to try
                if (selectedParts.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find variant meshes '{}' or '{}' for {}", bodyMeshName, headMeshName, codeToTry);
                    continue;
                }

                // For body variant > 0 (robes), we MUST find the body mesh.
                // If we only found the head, that's not a valid robe model - return false
                // so the fallback logic can use the default body with robe textures.
                if (bodyVariant > 0) {
                    bool foundBody = false;
                    for (const auto& part : selectedParts) {
                        if (part.geometry) {
                            std::string partName = part.geometry->name;
                            std::transform(partName.begin(), partName.end(), partName.begin(),
                                           [](unsigned char c) { return std::toupper(c); });
                            if (partName == bodyMeshName) {
                                foundBody = true;
                                break;
                            }
                        }
                    }
                    if (!foundBody) {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Robe body mesh '{}' not found for {}, will fall back to default body",
                                  bodyMeshName, codeToTry);
                        continue;  // Skip to next race code or fall back entirely
                    }
                }

                auto combinedGeom = combineCharacterPartsWithTransforms(selectedParts);
                if (!combinedGeom) {
                    continue;
                }

                auto modelData = std::make_shared<RaceModelData>();
                modelData->combinedGeometry = combinedGeom;
                // Use merged textures from all sources (global + numbered globals + zone)
                modelData->textures = getMergedTextures();
                modelData->raceName = character->name;
                modelData->raceId = raceId;
                modelData->gender = gender;
                modelData->scale = getRaceScale(raceId);

                uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant);
                {
                    std::lock_guard<std::mutex> lock(variantModelsMutex_);
                    variantModels_[key] = modelData;
                }

                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) with variants (head={}, body={}) from global_chr.s3d ({} parts, {} vertices)",
                          raceId, codeToTry, (int)headVariant, (int)bodyVariant, selectedParts.size(), combinedGeom->vertices.size());
                return true;
            }
        }
    }

    return false;
}

EQAnimatedMesh* RaceModelLoader::getAnimatedMeshWithAppearance(uint16_t raceId, uint8_t gender,
                                                                uint8_t headVariant, uint8_t bodyVariant,
                                                                uint8_t textureVariant,
                                                                bool compositeAllowed) {
    // For non-default variants, use variant cache (includes texture variant now)
    if (headVariant == 0 && bodyVariant == 0 && textureVariant == 0) {
        return getAnimatedMeshForRace(raceId, gender, compositeAllowed);
    }

    // P08d: composite sheets only exist for the default body/head geometry (headVariant==0,
    // bodyVariant==0) — entity_composite_builder bakes texture variants of the base mesh
    // only, never a distinct mesh (robe body, alternate head). A uniform-texture-variant
    // NPC (e.g. appearance.texture=2, chain armor, no per-slot overrides) reaches this
    // function with headVariant==0, bodyVariant==0, textureVariant!=0 — eligible for a
    // composite lookup keyed by (race, gender, textureVariant), which the pre-P08d code
    // never attempted (it only ever checked compositeData for textureVariant==0, via the
    // getAnimatedMeshForRace delegation above). Any other combination (headVariant!=0 or
    // bodyVariant!=0) falls straight through to the existing multi-buffer path, unchanged.
    bool tryVariantComposite = compositeAllowed && headVariant == 0 && bodyVariant == 0 &&
                                textureVariant != 0 && !compositeEntityPath_.empty() && meshBuilder_;

    // Include texture variant in cache key
    uint64_t key = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, textureVariant);

    // Composite-eligible and non-eligible builds of the same variant key are cached
    // separately (mirrors getAnimatedMeshForRace's split — see race_model_loader.h).
    auto& variantCache = tryVariantComposite ? compositeVariantAnimatedMeshCache_ : variantAnimatedMeshCache_;

    // Check the appropriate variant animated mesh cache first
    auto cacheIt = variantCache.find(key);
    if (cacheIt != variantCache.end()) {
        return cacheIt->second;
    }

    // For model loading, we need the base variant key (without texture variant)
    // because model geometry is the same regardless of texture
    uint64_t modelKey = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, 0);

    // Try to load the model with specific variants for animation
    std::shared_ptr<RaceModelData> modelData;
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        auto modelIt = variantModels_.find(modelKey);
        if (modelIt != variantModels_.end()) {
            modelData = modelIt->second;
        }
    }

    if (!modelData) {
        if (!loadModelFromGlobalChrWithVariantsForAnimation(raceId, gender, headVariant, bodyVariant)) {
            variantCache[key] = nullptr;
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        auto modelIt = variantModels_.find(modelKey);
        if (modelIt == variantModels_.end()) {
            variantCache[key] = nullptr;
            return nullptr;
        }
        modelData = modelIt->second;
    }
    if (!modelData) {
        variantCache[key] = nullptr;
        return nullptr;
    }

    // Check if we have animation data AND raw geometry
    if (!modelData->skeleton || modelData->skeleton->animations.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No animation data for race {} variant (head={}, body={}) - falling back to default",
                  raceId, (int)headVariant, (int)bodyVariant);
        variantCache[key] = nullptr;
        return nullptr;
    }

    if (!modelData->rawGeometry) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No raw geometry for race {} variant - falling back to default", raceId);
        variantCache[key] = nullptr;
        return nullptr;
    }

    // Build mesh from RAW (unskinned) geometry for animation.
    // P08d: composite-eligible uniform-texture-variant NPCs (tryVariantComposite) try the
    // pre-baked composite sheet for this (race, gender, textureVariant) FIRST. Falls back
    // to the existing per-slot-capable multi-buffer build (byte-identical to pre-P08d) if
    // no sheet exists or the texture isn't uploaded yet — same fallback discipline as
    // getAnimatedMeshForRace's default-variant composite path.
    //
    // T114: animScratchMutex_ guards originalVerticesForAnimation_/
    // vertexMappingForAnimation_ across the whole build-then-consume sequence
    // (through setOriginalVertices()/setVertexMapping() below) -- this
    // function runs on EntityPrepWorker's background thread, and
    // getAnimatedMeshForRace() (animated_mesh_creation.cpp) writes/reads the
    // exact same two RaceModelLoader-scratch vectors from the main/render
    // thread. See that call site's comment for the crash this closes (a
    // Pi-reproduced "double free or corruption" SIGABRT inside
    // RaceModelLoader::~RaceModelLoader()'s vertexMappingForAnimation_
    // destruction).
    std::lock_guard<std::mutex> animScratchLock(animScratchMutex_);
    irr::scene::IMesh* rawMesh = nullptr;
    if (tryVariantComposite) {
        std::string modelName = modelData->raceName;
        std::transform(modelName.begin(), modelName.end(), modelName.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        // entity_composite_builder output naming: {modelname}_0_{variant}.json/.eqe1
        // (gender is always folded into modelName, so the middle field is always "0" —
        // same convention as preloadModelData's default-variant composite lookup).
        std::string compositeBaseName = modelName + "_0_" + std::to_string(textureVariant);
        std::string jsonPath = compositeEntityPath_ + "/" + compositeBaseName + ".json";
        CompositeTextureData compositeData;
        if (compositeData.loadFromJson(jsonPath)) {
            irr::video::ITexture* compositeTexture = meshBuilder_->getOrLoadTexture(compositeBaseName);
            if (compositeTexture) {
                rawMesh = buildCompositeMeshFromGeometry(modelData->rawGeometry, compositeTexture, compositeData);
                if (rawMesh) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Built composite variant animated mesh for race {} (head={}, body={}, texture={}, {} buffers)",
                             raceId, (int)headVariant, (int)bodyVariant, (int)textureVariant, rawMesh->getMeshBufferCount());
                    ++compositeSheetHits_;  // P08d adoption tracking
                } else {
                    LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: buildCompositeMeshFromGeometry returned null for race {} variant texture {}", raceId, (int)textureVariant);
                    ++compositeSheetMisses_;  // P08d adoption tracking
                }
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: composite texture '{}' not yet uploaded — falling back to multi-buffer for race {} variant texture {}",
                          compositeBaseName, raceId, (int)textureVariant);
                ++compositeSheetMisses_;  // P08d adoption tracking
            }
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: no composite sheet at {} — falling back to multi-buffer for race {} variant texture {}",
                      jsonPath, raceId, (int)textureVariant);
            ++compositeSheetMisses_;  // P08d adoption tracking
        }
    }

    // Fall back to standard multi-buffer mesh (pass texture variant and race code for
    // equipment texture override — unchanged from pre-P08d)
    if (!rawMesh) {
        rawMesh = buildMeshFromGeometry(modelData->rawGeometry, modelData->textures,
                                         textureVariant, modelData->raceName);
    }
    if (!rawMesh) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Failed to build raw mesh for race {} variant", raceId);
        variantCache[key] = nullptr;
        return nullptr;
    }

    // Create animated mesh using the RAW (unskinned) mesh
    EQAnimatedMesh* animMesh = new EQAnimatedMesh();
    animMesh->setBaseMesh(rawMesh);
    animMesh->setSkeleton(modelData->skeleton);
    animMesh->setVertexPieces(modelData->vertexPieces);

    // Set vertex mapping data for multi-buffer animation support
    animMesh->setOriginalVertices(originalVerticesForAnimation_);
    animMesh->setVertexMapping(vertexMappingForAnimation_);

    // Apply initial pose
    animMesh->applyAnimation();

    variantCache[key] = animMesh;

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Created variant animated mesh for race {} (head={}, body={}, texture={}) with {} animations",
              raceId, (int)headVariant, (int)bodyVariant, (int)textureVariant, modelData->skeleton->animations.size());

    return animMesh;
}

} // namespace Graphics
} // namespace EQT
