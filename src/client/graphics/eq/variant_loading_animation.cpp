// RaceModelLoader variant loading - the animation-aware variant loader
//
// Split out of variant_loading.cpp by
// refactor/J35_variant_loading_split.md (move-only, verbatim relocation).
//
// T340 Leg B (B052) de-duplicated it. It used to hold exactly one method,
// loadModelFromGlobalChrWithVariantsForAnimation, 815 lines long -- and the
// length was DUPLICATION, not phases (refactor/G07_variant_loader.md measured
// it: the animation-merge body written out four times, byte-identical once
// normalised, and the whole match-select-combine-register character body
// written out twice, 0.906 similar). It is now that method's candidate loop
// plus the four functions the copies collapsed into:
//
//   buildCodesToTry     - the candidate list and the animation source code
//   selectVariantParts  - this variant's meshes out of one character
//   mergeAnimationsFrom - the four identical animation-merge copies, once
//   tryBuildVariantFrom - the character body, shared by the zone and global paths
//
// The loop keeps its shape and its exits. Its "continue"s mean TRY THE NEXT
// CANDIDATE, not "fail": returning false from this method makes the caller
// (variant_loading.cpp) write a PERMANENT negative cache entry, so a race whose
// primary code misses but whose fallback_code hits must still LOAD -- Will's
// EQE1 all-or-nothing rule, docs/decisions.md. tests/test_variant_loader_candidates.cpp
// is the behaviour net that pins exactly that (T340 Leg A, six cases, seven of
// the eleven semantic exits driven).
//
// Siblings: variant_loading.cpp (race and appearance mesh resolution) and
// variant_loading_preload.cpp (the variant cache and preloader).

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

namespace {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// The first part of `parts` whose geometry is named `meshName` (uppercased),
// or nullptr. The four mesh searches this method used to spell out inline.
const CharacterPart* findPartNamed(const std::vector<CharacterPart>& parts,
                                   const std::string& meshName) {
    for (const auto& part : parts) {
        if (!part.geometry) continue;
        if (toUpper(part.geometry->name) == meshName) {
            return &part;
        }
    }
    return nullptr;
}

}  // namespace

// The candidate race codes, in order. The ONLY exit of the loader that really
// is "abandon" (G07 exit 69) is the empty list this reports with false.
bool RaceModelLoader::buildCodesToTry(uint16_t raceId, uint8_t gender,
                                      std::vector<std::string>& codesToTry,
                                      std::string& animationSourceCode) {
    // 1. The row's own code, gendered (e.g. HUM -> HUF for female)
    std::string baseRaceCode = getRaceCode(raceId);
    if (!baseRaceCode.empty()) {
        std::string raceCode = toUpper(getGenderedRaceCode(baseRaceCode, gender));
        if (std::find(codesToTry.begin(), codesToTry.end(), raceCode) == codesToTry.end()) {
            codesToTry.push_back(raceCode);
        }
    }

    // 2. The row's fallback_code, if it names one (e.g. race 71 QCM -> HUM)
    std::string fallbackCode = toUpper(getFallbackRaceCode(raceId, gender));
    if (!fallbackCode.empty()) {
        if (std::find(codesToTry.begin(), codesToTry.end(), fallbackCode) == codesToTry.end()) {
            codesToTry.push_back(fallbackCode);
        }
    }

    // Animation source (EQSage's approach - ELM/ELF have 40+ animations)
    std::string baseCode = codesToTry.empty() ? "" : codesToTry[0];
    animationSourceCode = toUpper(getAnimationSourceCode(baseCode));

    return !codesToTry.empty();
}

// One character, this variant's meshes. False means "this character carries
// nothing usable" -- the caller must try the NEXT character, never abandon.
bool RaceModelLoader::selectVariantParts(const CharacterModel& character, const VariantRequest& req,
                                         std::vector<CharacterPart>& selectedSkinnedParts,
                                         std::vector<CharacterPart>& selectedRawParts) {
    // Fallback mesh names for variants that might not exist. Variant 0 has no
    // fallback: it IS the fallback.
    std::string headMeshFallback;
    std::string bodyMeshFallback;
    if (req.headVariant > 0) {
        char fallbackBuf[32];
        snprintf(fallbackBuf, sizeof(fallbackBuf), "%sHE00_DMSPRITEDEF", req.codeToTry.c_str());
        headMeshFallback = fallbackBuf;
    }
    if (req.bodyVariant > 0) {
        // Body variant 0 is just "{RACE}_DMSPRITEDEF"
        bodyMeshFallback = req.codeToTry + "_DMSPRITEDEF";
    }

    // The skinned parts (partsWithTransforms) and the raw parts (rawParts, for
    // animation) are selected the same way; only the log lines on the skinned
    // pass are the loader's own, and they are kept where they were.
    struct Pass {
        const std::vector<CharacterPart>& parts;
        std::vector<CharacterPart>& selected;
        bool logFallbacks;
    } passes[] = {
        {character.partsWithTransforms, selectedSkinnedParts, true},
        {character.rawParts, selectedRawParts, false},
    };

    for (auto& pass : passes) {
        bool foundRequestedHead = false;
        bool foundRequestedBody = false;
        for (const auto& part : pass.parts) {
            if (!part.geometry) continue;
            std::string partName = toUpper(part.geometry->name);
            if (partName == req.bodyMeshName) {
                pass.selected.push_back(part);
                foundRequestedBody = true;
            } else if (partName == req.headMeshName) {
                pass.selected.push_back(part);
                foundRequestedHead = true;
            }
        }

        // If the requested head variant wasn't found, fall back to head variant 0
        if (!foundRequestedHead && !headMeshFallback.empty()) {
            if (const CharacterPart* part = findPartNamed(pass.parts, headMeshFallback)) {
                if (pass.logFallbacks) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Head variant {} not found, using fallback head variant 0 ({})", (int)req.headVariant, headMeshFallback);
                }
                pass.selected.push_back(*part);
            }
        }

        // If the requested body variant wasn't found (e.g. a robe mesh), fall
        // back to body variant 0
        if (!foundRequestedBody && !bodyMeshFallback.empty()) {
            if (const CharacterPart* part = findPartNamed(pass.parts, bodyMeshFallback)) {
                if (pass.logFallbacks) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Body variant {} not found, using fallback body variant 0 ({})", (int)req.bodyVariant, bodyMeshFallback);
                }
                pass.selected.push_back(*part);
            }
        }
    }

    return !selectedSkinnedParts.empty() && !selectedRawParts.empty();
}

// The animation merge, written out four times before T340 and byte-identical in
// all four copies once comments and indentation are normalised. Model-specific
// animations are NEVER overwritten - only missing ones are added.
void RaceModelLoader::mergeAnimationsFrom(CharacterSkeleton& ourSkel, const CharacterSkeleton& sourceSkel,
                                          const std::string& codeToTry, const std::string& animationSourceCode,
                                          int& addedAnimations, int& mappedBones) {
    const std::string lowerCode = toLower(codeToTry);
    const std::string lowerSource = toLower(animationSourceCode);

    // Add missing animations (never overwrite existing)
    addedAnimations = 0;
    for (const auto& [animCode, sourceAnim] : sourceSkel.animations) {
        if (ourSkel.animations.find(animCode) == ourSkel.animations.end()) {
            ourSkel.animations[animCode] = sourceAnim;
            addedAnimations++;
        }
    }

    // Merge animation tracks for each bone (only add missing track entries).
    // e.g. QCM bones (qcmpe, qcmch, ...) <- ELM animations (elmpe, elmch, ...)
    mappedBones = 0;
    for (size_t i = 0; i < ourSkel.bones.size(); ++i) {
        std::string mappedName = ourSkel.bones[i].name;
        size_t pos = mappedName.find(lowerCode);
        if (pos != std::string::npos) {
            mappedName.replace(pos, lowerCode.length(), lowerSource);
        }

        int sourceIdx = sourceSkel.getBoneIndex(mappedName);
        if (sourceIdx >= 0 && sourceIdx < static_cast<int>(sourceSkel.bones.size())) {
            for (const auto& [trackCode, trackDef] : sourceSkel.bones[sourceIdx].animationTracks) {
                if (ourSkel.bones[i].animationTracks.find(trackCode) == ourSkel.bones[i].animationTracks.end()) {
                    ourSkel.bones[i].animationTracks[trackCode] = trackDef;
                }
            }
            mappedBones++;
        }
    }
}

// One character source, searched for this candidate. True = loaded, registered
// and cached (the loader's two success exits, G07 505 and 840). False = nothing
// HERE matched; the caller tries the next source, then the global source, then
// the next candidate. fromGlobal picks the global path's log wording and the
// trace's resolvedSource -- it changes no control flow.
bool RaceModelLoader::tryBuildVariantFrom(const std::vector<std::shared_ptr<CharacterModel>>& characters,
                                          bool fromGlobal, const VariantRequest& req) {
    for (const auto& character : characters) {
        // G07 exits 155 / 525 -- skip a null or partless character and keep
        // scanning this source. Unreachable with shipped data (T340 Leg A
        // enumerated every character model in all 378 *_chr.s3d archives; not
        // one has zero parts), so nothing behavioural pins it.
        if (!character || character->parts.empty()) {
            continue;
        }

        // Look for the race code in the character name
        if (toUpper(character->name).find(req.codeToTry) == std::string::npos) {
            continue;
        }

        // Found a matching character - now find the specific body and head parts
        std::vector<CharacterPart> selectedSkinnedParts;
        std::vector<CharacterPart> selectedRawParts;
        if (!selectVariantParts(*character, req, selectedSkinnedParts, selectedRawParts)) {
            // G07 exits 280 / 640 -- matched by name but carries nothing
            // usable, so try the NEXT character.
            if (fromGlobal) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Found {} but missing variant parts (skinned={}, raw={})",
                          toUpper(character->name), selectedSkinnedParts.size(), selectedRawParts.size());
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Could not find variant meshes '{}' or '{}' for animation in {}", req.bodyMeshName, req.headMeshName, req.codeToTry);
            }
            {
                std::lock_guard<std::mutex> traceLock(variantModelsMutex_);
                // T340 (B052): the read-only observation seam, see race_model_loader.h
                ++(fromGlobal ? variantLoadTrace_.globalSkips : variantLoadTrace_.zoneSkips);
            }
            continue;
        }

        auto combinedGeom = combineCharacterPartsWithTransforms(selectedSkinnedParts);
        auto rawGeom = combineCharacterPartsRaw(selectedRawParts);
        if (!combinedGeom || !rawGeom) {
            // G07 exits 287 / 646 -- next character.
            continue;
        }

        auto modelData = std::make_shared<RaceModelData>();
        modelData->combinedGeometry = combinedGeom;
        modelData->rawGeometry = rawGeom;
        // Use merged textures from all sources (global + numbered globals + zone)
        modelData->textures = getMergedTextures();
        modelData->raceName = character->name;
        modelData->raceId = req.raceId;
        modelData->gender = req.gender;
        modelData->scale = getRaceScale(req.raceId);

        // Copy animation data from the character's skeleton and merge missing
        // animations from the animation source. Key behavior from EQSage:
        // model-specific animations are NEVER overwritten.
        if (character->animatedSkeleton) {
            modelData->skeleton = character->animatedSkeleton;
            auto& ourSkel = *character->animatedSkeleton;
            size_t existingAnimCount = ourSkel.animations.size();

            if (fromGlobal) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - skeleton from {} with {} animations, animSource={}",
                          req.codeToTry, existingAnimCount, req.animationSourceCode);
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Using skeleton from {} with {} existing animations, animSource={}",
                          req.codeToTry, existingAnimCount, req.animationSourceCode);
            }

            if (!req.animationSourceCode.empty() && req.codeToTry != req.animationSourceCode) {
                if (fromGlobal) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - merging animations from {} to {}", req.animationSourceCode, req.codeToTry);
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Attempting animation merge from {} to {}", req.animationSourceCode, req.codeToTry);
                }

                // Check whether the animation source is in a zone-specific S3D file
                std::string animSourceS3dFile = getAnimationSourceS3DFile(req.raceId);
                bool foundSource = false;
                int addedAnimations = 0;
                int mappedBones = 0;

                // First, try the zone-specific animation source S3D if configured
                if (!animSourceS3dFile.empty() && !clientPath_.empty()) {
                    std::string lowerFilename = toLower(animSourceS3dFile);

                    // Protected by mutex for worker thread safety
                    std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
                    auto cacheIt = otherChrCaches_.find(lowerFilename);
                    if (cacheIt == otherChrCaches_.end()) {
                        // Load the animation source S3D file
                        std::string fullPath = clientPath_ + animSourceS3dFile;
                        S3DLoader loader;
                        if (loader.loadZone(fullPath)) {
                            auto zone = loader.getZone();
                            if (zone && !zone->characters.empty()) {
                                // T91: was a raw otherChrCaches_[key] = ...
                                // insert with no LRU registration -- see
                                // storeOtherChrCacheLocked's doc comment.
                                storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);
                                cacheIt = otherChrCaches_.find(lowerFilename);
                                size_t loaded = cacheIt != otherChrCaches_.end() ? cacheIt->second.characters.size() : 0;
                                if (fromGlobal) {
                                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - loaded animation source S3D {} ({} characters)",
                                              animSourceS3dFile, loaded);
                                } else {
                                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded animation source S3D {} ({} characters)",
                                              animSourceS3dFile, loaded);
                                }
                            }
                        }
                    }

                    // Search for the animation source in the loaded cache
                    if (cacheIt != otherChrCaches_.end()) {
                        for (const auto& sourceChar : cacheIt->second.characters) {
                            if (!sourceChar) continue;
                            if (toUpper(sourceChar->name).find(req.animationSourceCode) == std::string::npos) continue;
                            if (!sourceChar->animatedSkeleton || sourceChar->animatedSkeleton->animations.empty()) continue;

                            mergeAnimationsFrom(ourSkel, *sourceChar->animatedSkeleton,
                                                req.codeToTry, req.animationSourceCode,
                                                addedAnimations, mappedBones);
                            if (fromGlobal) {
                                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - merged from {} ({}) - added {} animations (had {}, now {}), mapped {}/{} bones",
                                          req.animationSourceCode, animSourceS3dFile, addedAnimations, existingAnimCount, ourSkel.animations.size(), mappedBones, ourSkel.bones.size());
                            } else {
                                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} ({}) to {} - added {} animations (had {}, now {}), mapped {}/{} bones",
                                          req.animationSourceCode, animSourceS3dFile, req.codeToTry, addedAnimations, existingAnimCount, ourSkel.animations.size(), mappedBones, ourSkel.bones.size());
                            }
                            foundSource = true;
                            break;
                        }
                    }

                    if (!foundSource && !fromGlobal) {
                        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Animation source {} not found in {} for race {}",
                                 req.animationSourceCode, animSourceS3dFile, req.raceId);
                    }
                }

                // Fall back to global characters if not found in the zone-specific S3D
                if (!foundSource) {
                    // Ensure global models are loaded so we can get animations
                    // from the source. On the global path this is already true
                    // (the caller only reaches it through loadGlobalModels), so
                    // the guard is a no-op there rather than a behaviour change.
                    if (!globalModelsLoaded_) {
                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loading global models for animation source {}", req.animationSourceCode);
                        loadGlobalModels();
                    }
                    for (const auto& sourceChar : globalCharacters_) {
                        if (!sourceChar) continue;
                        if (toUpper(sourceChar->name).find(req.animationSourceCode) == std::string::npos) continue;
                        if (!sourceChar->animatedSkeleton || sourceChar->animatedSkeleton->animations.empty()) continue;

                        mergeAnimationsFrom(ourSkel, *sourceChar->animatedSkeleton,
                                            req.codeToTry, req.animationSourceCode,
                                            addedAnimations, mappedBones);
                        if (fromGlobal) {
                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - merged {} animations (had {}, now {}), mapped {}/{} bones",
                                      addedAnimations, existingAnimCount, ourSkel.animations.size(), mappedBones, ourSkel.bones.size());
                        } else {
                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} to {} - added {} animations (had {}, now {}), mapped {}/{} bones",
                                      req.animationSourceCode, req.codeToTry, addedAnimations, existingAnimCount, ourSkel.animations.size(), mappedBones, ourSkel.bones.size());
                        }
                        foundSource = true;
                        break;
                    }
                }

                if (!foundSource) {
                    if (fromGlobal) {
                        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Global fallback - animation source {} not found for race {}",
                                 req.animationSourceCode, req.raceId);
                    } else {
                        LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Animation source {} not found for race {} (code {})",
                                 req.animationSourceCode, req.raceId, req.codeToTry);
                    }
                }
            } else if (!fromGlobal) {
                if (req.animationSourceCode.empty()) {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No animation source for {} - keeping {} animations",
                              req.codeToTry, ourSkel.animations.size());
                } else {
                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Animation source {} same as {} - skipping merge",
                              req.animationSourceCode, req.codeToTry);
                }
            }

            if (fromGlobal) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Global fallback - {} final animation count: {}",
                          req.codeToTry, modelData->skeleton->animations.size());
            } else {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Variant {} final animation count: {}",
                          req.codeToTry, modelData->skeleton->animations.size());
            }
        }

        if (!rawGeom->vertexPieces.empty()) {
            modelData->vertexPieces = rawGeom->vertexPieces;
        }

        uint64_t key = makeVariantCacheKey(req.raceId, req.gender, req.headVariant, req.bodyVariant);
        {
            std::lock_guard<std::mutex> lock(variantModelsMutex_);
            variantModels_[key] = modelData;
            // T340 (B052): which candidate and which character won.
            variantLoadTrace_.succeeded = true;
            variantLoadTrace_.resolvedCode = req.codeToTry;
            variantLoadTrace_.resolvedCharacter = character->name;
            variantLoadTrace_.resolvedSource = fromGlobal ? "global" : "zone";
        }

        if (fromGlobal) {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) variant from global ({} parts)",
                      req.raceId, req.codeToTry, selectedSkinnedParts.size());
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded race {} ({}) variant (head={}, body={}) for animation ({} parts, {} vertices)",
                      req.raceId, req.codeToTry, (int)req.headVariant, (int)req.bodyVariant, selectedSkinnedParts.size(), combinedGeom->vertices.size());
        }
        return true;
    }

    return false;
}

bool RaceModelLoader::loadModelFromGlobalChrWithVariantsForAnimation(uint16_t raceId, uint8_t gender,
                                                                      uint8_t headVariant, uint8_t bodyVariant) {
    // T340 (B052): read-only observation seam -- see VariantLoadTrace in
    // race_model_loader.h. Records that this loader RAN (the caller caches a
    // null on false, so a second call never reaches here) and clears the
    // previous run's verdict. No control flow reads any of it.
    {
        std::lock_guard<std::mutex> traceLock(variantModelsMutex_);
        ++variantLoadTrace_.attempts;
        variantLoadTrace_.succeeded = false;
        variantLoadTrace_.resolvedCode.clear();
        variantLoadTrace_.resolvedCharacter.clear();
        variantLoadTrace_.resolvedSource.clear();
        variantLoadTrace_.zoneSkips = 0;
        variantLoadTrace_.globalSkips = 0;
    }

    // NOTE: We defer loading global models until we confirm zone models don't have what we need.
    // This prevents the expensive loading of ALL race skeletons when only a zone-specific race is needed.

    std::vector<std::string> codesToTry;
    std::string animationSourceCode;
    if (!buildCodesToTry(raceId, gender, codesToTry, animationSourceCode)) {
        return false;
    }

    // Try each race code in order. A "continue" here means TRY THE NEXT
    // CANDIDATE -- see this file's header comment.
    for (const std::string& codeToTry : codesToTry) {
        VariantRequest req;
        req.raceId = raceId;
        req.gender = gender;
        req.headVariant = headVariant;
        req.bodyVariant = bodyVariant;
        req.codeToTry = codeToTry;
        req.animationSourceCode = animationSourceCode;

        // Build the expected mesh names for body and head
        if (bodyVariant == 0) {
            req.bodyMeshName = codeToTry + "_DMSPRITEDEF";
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%s%02d_DMSPRITEDEF", codeToTry.c_str(), bodyVariant);
            req.bodyMeshName = buf;
        }
        char headBuf[32];
        snprintf(headBuf, sizeof(headBuf), "%sHE%02d_DMSPRITEDEF", codeToTry.c_str(), headVariant);
        req.headMeshName = headBuf;

        // Build the list of character sources to search: JSON-specified first,
        // then the current zone. globalCharacters_ is added later, and only if
        // this search fails.
        std::vector<const std::vector<std::shared_ptr<CharacterModel>>*> characterSources;

        // First, check whether this race has a JSON-specified s3d_file
        std::string jsonS3dFile = getRaceS3DFile(raceId);
        if (!jsonS3dFile.empty() && !clientPath_.empty()) {
            // Extract the zone name from the s3d filename ("freporte_chr.s3d" -> "freporte")
            auto chrPos = jsonS3dFile.find("_chr.s3d");
            if (chrPos != std::string::npos) {
                std::string jsonZoneName = jsonS3dFile.substr(0, chrPos);
                // Only if this is a different zone, and not a global archive
                if (jsonZoneName != currentZoneName_ && jsonZoneName.find("global") != 0) {
                    std::string lowerFilename = toLower(jsonS3dFile);

                    // Check if already in cache (protected by mutex for worker thread safety)
                    {
                        std::lock_guard<std::mutex> chrLock(otherChrCacheMutex_);
                        auto cacheIt = otherChrCaches_.find(lowerFilename);
                        if (cacheIt == otherChrCaches_.end()) {
                            // Load the chr file
                            std::string fullPath = clientPath_ + jsonS3dFile;
                            S3DLoader loader;
                            if (loader.loadZone(fullPath)) {
                                auto zone = loader.getZone();
                                if (zone && !zone->characters.empty()) {
                                    // T91: was a raw otherChrCaches_[key] = ...
                                    // insert with no LRU registration -- see
                                    // storeOtherChrCacheLocked's doc comment.
                                    storeOtherChrCacheLocked(lowerFilename, zone->characters, zone->characterTextures);
                                    cacheIt = otherChrCaches_.find(lowerFilename);
                                    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loaded {} characters from JSON-specified {} for race {}",
                                              cacheIt != otherChrCaches_.end() ? cacheIt->second.characters.size() : 0,
                                              jsonS3dFile, raceId);
                                }
                            }
                        }

                        // Add JSON-specified chr file to search sources
                        if (cacheIt != otherChrCaches_.end() && !cacheIt->second.characters.empty()) {
                            characterSources.push_back(&cacheIt->second.characters);
                            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching JSON-specified {} ({} models) for {}",
                                      jsonS3dFile, cacheIt->second.characters.size(), codeToTry);
                        }
                    }
                }
            }
        }

        // Then add current zone characters
        if (zoneModelsLoaded_ && !zoneCharacters_.empty()) {
            characterSources.push_back(&zoneCharacters_);
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching zone characters ({} models) for {}", zoneCharacters_.size(), codeToTry);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No zone characters loaded (zoneModelsLoaded_={})", zoneModelsLoaded_);
        }
        // Note: globalCharacters_ is searched later, only if this search fails.

        for (const auto* characters : characterSources) {
            if (tryBuildVariantFrom(*characters, /*fromGlobal=*/false, req)) {
                return true;
            }
        }

        // Zone search didn't find it - try global models
        if (!globalModelsLoaded_) {
            LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Zone search failed for {}, loading global models...", codeToTry);
            if (!loadGlobalModels()) {
                continue;  // Failed to load globals, try next code
            }
        }

        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Searching global characters ({} models) for {} variant (body={}, head={})",
                  globalCharacters_.size(), codeToTry, req.bodyMeshName, req.headMeshName);

        if (tryBuildVariantFrom(globalCharacters_, /*fromGlobal=*/true, req)) {
            return true;
        }
    }

    return false;
}

} // namespace Graphics
} // namespace EQT
