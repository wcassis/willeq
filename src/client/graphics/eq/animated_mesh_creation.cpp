// RaceModelLoader animated mesh creation methods - split from race_model_loader.cpp
// These methods remain part of the RaceModelLoader class

#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/race_codes.h"
#include "client/graphics/eq/animation_mapping.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/entity_renderer.h"  // For EntityAppearance
#include "client/graphics/constrained_texture_cache.h"  // P17: isLiveTexture() defensive check
#include "common/logging.h"
#include "common/performance_metrics.h"
#include <algorithm>
#include <iostream>
#include <chrono>

namespace EQT {
namespace Graphics {

EQAnimatedMesh* RaceModelLoader::getAnimatedMeshForRace(uint16_t raceId, uint8_t gender, bool compositeAllowed) {
    uint32_t key = makeCacheKey(raceId, gender);

    // P08d: composite-allowed and composite-disallowed builds of the same race/gender
    // are cached in separate maps (see race_model_loader.h) so an eligible NPC and an
    // ineligible player never share a cached mesh.
    auto& cache = compositeAllowed ? compositeAnimatedMeshCache_ : animatedMeshCache_;

    // Check the appropriate animated mesh cache first
    auto cacheIt = cache.find(key);
    if (cacheIt != cache.end()) {
        return cacheIt->second;
    }

    // Ensure the model is loaded (this populates modelData with raw geometry).
    // P13b: was getMeshForRace(raceId, gender) — its returned IMesh* was only ever
    // null-checked and then discarded, but building it ALSO ran
    // buildTexturedMesh()'s eager, unconditional per-part texture decode/upload as
    // a side effect (every call, for every entity, composite-eligible or not) —
    // silently defeating preloadModelData's T05 composite-hit deferral (a
    // composite-hit race's per-part textures would get decoded+uploaded here
    // anyway, just under a different ledger category, before this function even
    // reached the composite-vs-multi-buffer branch below). ensureModelDataLoaded()
    // does the same "find or load the race's model data" work WITHOUT the
    // wasted static-mesh build. See race_model_loader.h's comment on that method
    // and the plan Review for how this was caught (the ledger acceptance run).
    auto modelData = ensureModelDataLoaded(raceId, gender);
    if (!modelData) {
        cache[key] = nullptr;
        return nullptr;
    }

    // Check if we have animation data AND raw geometry
    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::getAnimatedMeshForRace race={} skeleton={} animations={} vertexPieces={} rawGeometry={}",
              raceId, (modelData->skeleton ? "yes" : "no"), (modelData->skeleton ? modelData->skeleton->animations.size() : 0),
              modelData->vertexPieces.size(), (modelData->rawGeometry ? "yes" : "no"));

    // If skeleton exists, try to merge missing animations from animation source (e.g., HUF -> ELF)
    // Key behavior from EQSage: model-specific animations are NEVER overwritten - only missing animations are added
    if (modelData->skeleton) {
        std::string raceCode = getRaceCode(raceId);
        raceCode = getGenderedRaceCode(raceCode, gender);

        // Use the animation source map (based on EQSage's approach)
        // ELM/ELF have 40+ animations vs HUM's 3 - we ADD the missing ones
        std::string animSourceCode = getAnimationSourceCode(raceCode);

        if (!animSourceCode.empty() && animSourceCode != raceCode) {
            // Ensure global models are loaded so we can get animations from the source
            if (!globalModelsLoaded_) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Loading global models for animation source {}", animSourceCode);
                loadGlobalModels();
            }
            // Search for animation source skeleton in global characters
            for (const auto& sourceChar : globalCharacters_) {
                if (!sourceChar) continue;
                std::string sourceName = sourceChar->name;
                std::transform(sourceName.begin(), sourceName.end(), sourceName.begin(),
                               [](unsigned char c) { return std::toupper(c); });

                if (sourceName.find(animSourceCode) != std::string::npos) {
                    if (sourceChar->animatedSkeleton && !sourceChar->animatedSkeleton->animations.empty()) {
                        auto& ourSkel = modelData->skeleton;
                        auto& sourceSkel = sourceChar->animatedSkeleton;

                        std::string lowerCode = raceCode;
                        std::transform(lowerCode.begin(), lowerCode.end(), lowerCode.begin(),
                                       [](unsigned char c) { return std::tolower(c); });
                        std::string lowerSource = animSourceCode;
                        std::transform(lowerSource.begin(), lowerSource.end(), lowerSource.begin(),
                                       [](unsigned char c) { return std::tolower(c); });

                        // Count existing animations before merging
                        size_t existingAnimCount = ourSkel->animations.size();
                        int addedAnimations = 0;

                        // For each animation in the source, add it ONLY if we don't already have it
                        // This preserves model-specific animations while filling in missing ones
                        for (const auto& [animCode, sourceAnim] : sourceSkel->animations) {
                            if (ourSkel->animations.find(animCode) == ourSkel->animations.end()) {
                                // Animation doesn't exist in our skeleton - add it
                                ourSkel->animations[animCode] = sourceAnim;
                                addedAnimations++;
                            }
                            // If animation already exists, keep the model-specific one (don't overwrite)
                        }

                        // For each bone, merge animation tracks (only add missing track entries)
                        int mappedBones = 0;
                        int unmappedBones = 0;
                        for (size_t i = 0; i < ourSkel->bones.size(); ++i) {
                            std::string ourBoneName = ourSkel->bones[i].name;
                            std::string mappedName = ourBoneName;
                            size_t pos = mappedName.find(lowerCode);
                            if (pos != std::string::npos) {
                                mappedName.replace(pos, lowerCode.length(), lowerSource);
                            }

                            int sourceIdx = sourceSkel->getBoneIndex(mappedName);
                            if (sourceIdx >= 0 && sourceIdx < static_cast<int>(sourceSkel->bones.size())) {
                                // Merge animation tracks - only add missing ones
                                int addedTracks = 0;
                                for (const auto& [trackCode, trackDef] : sourceSkel->bones[sourceIdx].animationTracks) {
                                    if (ourSkel->bones[i].animationTracks.find(trackCode) == ourSkel->bones[i].animationTracks.end()) {
                                        ourSkel->bones[i].animationTracks[trackCode] = trackDef;
                                        addedTracks++;
                                    }
                                }
                                mappedBones++;
                                if (i < 3) {
                                    LOG_TRACE(MOD_GRAPHICS, "  Bone[{}] '{}' -> '{}' matched source[{}], added {} tracks",
                                              i, ourBoneName, mappedName, sourceIdx, addedTracks);
                                }
                            } else {
                                unmappedBones++;
                                if (unmappedBones <= 3) {
                                    LOG_TRACE(MOD_GRAPHICS, "  Bone[{}] '{}' -> '{}' NOT FOUND in source skeleton",
                                              i, ourBoneName, mappedName);
                                }
                            }
                        }

                        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Merged animations from {} to {} - added {} animations (had {}, now {}), mapped {}/{} bones",
                                  animSourceCode, raceCode, addedAnimations, existingAnimCount, ourSkel->animations.size(),
                                  mappedBones, ourSkel->bones.size());
                        break;
                    }
                }
            }
        }
    }

    if (!modelData->skeleton || modelData->skeleton->animations.empty()) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No animation data for race {} - using static mesh", raceId);
        cache[key] = nullptr;
        return nullptr;
    }

    if (!modelData->rawGeometry) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: No raw geometry for race {} - using static mesh", raceId);
        cache[key] = nullptr;
        return nullptr;
    }

    // Build mesh from RAW (unskinned) geometry for animation
    // This is critical - animated meshes need the original vertex positions
    //
    // T114: originalVerticesForAnimation_/vertexMappingForAnimation_ are
    // RaceModelLoader-scratch member vectors -- buildCompositeMeshFromGeometry()/
    // buildMeshFromGeometry() (mesh_building.cpp) write them as a side effect,
    // and setOriginalVertices()/setVertexMapping() below read them immediately
    // after. This same pair is ALSO written+read by
    // preloadVariantModel() (variant_loading.cpp), which runs on
    // EntityPrepWorker's background thread -- so this function (reachable from
    // the main/render thread) and the worker thread can race on these two
    // vectors with zero prior synchronization. Found via a Pi core dump: SIGABRT
    // inside RaceModelLoader::~RaceModelLoader()'s vertexMappingForAnimation_
    // destruction, "double free or corruption (!prev)" -- glibc detecting
    // corruption caused earlier by exactly this race, the same
    // detected-later-than-caused pattern T113 diagnosed for the RaceModelLoader
    // map races. animScratchMutex_ is held across the whole
    // build-then-consume sequence (not just each read/write) because a
    // concurrent write from the other thread landing between the build and the
    // consume would still corrupt the read.
    std::lock_guard<std::mutex> animScratchLock(animScratchMutex_);
    irr::scene::IMesh* rawMesh = nullptr;

    // If composite data is available AND this call is composite-eligible (P08d gate),
    // build single-buffer mesh with remapped UVs. compositeAllowed=false forces the
    // multi-buffer path below unconditionally, regardless of whether a sheet exists.
    // P08d: downgraded to LOG_DEBUG (was LOG_INFO) — this fires once per entity mesh
    // build, which would spam production logs continuously once composite entities are
    // enabled. The per-zone-load adoption summary (EntityRenderer::clearEntities) is the
    // intended INFO-level signal (plan B6).
    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::getAnimatedMeshForRace race={}: compositeAllowed={} compositeData={} compositeBmpPath='{}' meshBuilder_={}",
             raceId, compositeAllowed, (bool)modelData->compositeData, modelData->compositeBmpPath, (bool)meshBuilder_);
    if (compositeAllowed && modelData->compositeData && !modelData->compositeBmpPath.empty() && meshBuilder_) {
        // compositeBmpPath holds the EQE1 entry name (e.g., "hum_hs_def_0_0")
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::getAnimatedMeshForRace race={}: calling getOrLoadTexture('{}')",
                 raceId, modelData->compositeBmpPath);
        irr::video::ITexture* compositeTexture = meshBuilder_->getOrLoadTexture(modelData->compositeBmpPath);
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::getAnimatedMeshForRace race={}: compositeTexture={} ({})",
                 raceId, (bool)compositeTexture,
                 compositeTexture ? compositeTexture->getName().getPath().c_str() : "null");
        if (compositeTexture) {
            rawMesh = buildCompositeMeshFromGeometry(
                modelData->rawGeometry, compositeTexture, *modelData->compositeData);
            if (rawMesh) {
                LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Built composite animated mesh for race {} ({} buffers)",
                          raceId, rawMesh->getMeshBufferCount());
                ++compositeSheetHits_;  // P08d adoption tracking
            } else {
                LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: buildCompositeMeshFromGeometry returned null for race {}", raceId);
                ++compositeSheetMisses_;  // P08d adoption tracking
            }
        } else {
            LOG_WARN(MOD_GRAPHICS, "RaceModelLoader: Composite texture not yet uploaded for race {} — falling back to multi-buffer", raceId);
            ++compositeSheetMisses_;  // P08d adoption tracking
        }
    } else {
        if (compositeAllowed) {
            // Composite-eligible but no sheet available for this race/gender — a miss.
            ++compositeSheetMisses_;  // P08d adoption tracking
        }
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader::getAnimatedMeshForRace race={}: composite path NOT taken (compositeAllowed={} compositeData={} bmpPath='{}' meshBuilder={})",
                 raceId, compositeAllowed, (bool)modelData->compositeData, modelData->compositeBmpPath, (bool)meshBuilder_);
    }

    // Fall back to standard multi-buffer mesh
    if (!rawMesh) {
        LOG_INFO(MOD_GRAPHICS, "RaceModelLoader::getAnimatedMeshForRace race={}: falling back to buildMeshFromGeometry (multi-buffer)",
                 raceId);
        rawMesh = buildMeshFromGeometry(modelData->rawGeometry, modelData->textures);
    }
    if (!rawMesh) {
        LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Failed to build raw mesh for race {}", raceId);
        cache[key] = nullptr;
        return nullptr;
    }

    // Create animated mesh using the RAW (unskinned) mesh
    EQAnimatedMesh* animMesh = new EQAnimatedMesh();
    animMesh->setBaseMesh(rawMesh);
    animMesh->setSkeleton(modelData->skeleton);
    animMesh->setVertexPieces(modelData->vertexPieces);

    // Set vertex mapping data for multi-buffer animation support
    // (populated by buildMeshFromGeometry above)
    animMesh->setOriginalVertices(originalVerticesForAnimation_);
    animMesh->setVertexMapping(vertexMappingForAnimation_);

    // Apply initial pose (this will transform vertices using bone data)
    animMesh->applyAnimation();

    cache[key] = animMesh;

    LOG_DEBUG(MOD_GRAPHICS, "RaceModelLoader: Created animated mesh for race {} with {} animations, {} vertex pieces, {} texture buffers",
              raceId, modelData->skeleton->animations.size(), modelData->vertexPieces.size(), rawMesh->getMeshBufferCount());

    return animMesh;
}

EQAnimatedMeshSceneNode* RaceModelLoader::createAnimatedNode(uint16_t raceId, uint8_t gender,
                                                              irr::scene::ISceneNode* parent,
                                                              irr::s32 id,
                                                              bool compositeAllowed) {
    EQAnimatedMesh* animMesh = getAnimatedMeshForRace(raceId, gender, compositeAllowed);
    if (!animMesh) {
        return nullptr;
    }

    // Use parent's scene manager if provided, otherwise use our default smgr_
    // This is critical for callers that create nodes in an isolated scene
    // manager of their own (e.g. a standalone model-viewer tool) rather
    // than the shared zone scene manager. (T54: this comment used to name
    // CharacterModelView, the inventory paperdoll UI, as the example —
    // that class was deleted in 0f25cb57 along with WindowManager; no
    // replacement isolated-scene-manager caller exists today, but the
    // parent-vs-default-smgr_ branch below still needs to hold for any
    // future one.)
    irr::scene::ISceneNode* parentNode = parent ? parent : smgr_->getRootSceneNode();
    irr::scene::ISceneManager* targetSmgr = parent ? parent->getSceneManager() : smgr_;

    // Create the scene node
    EQAnimatedMeshSceneNode* node = new EQAnimatedMeshSceneNode(
        animMesh,
        parentNode,
        targetSmgr,
        id
    );
    // T127: ISceneNode's constructor already registered `node` with
    // parentNode->addChild(), which grab()s it -- that grab is meant to be
    // the sole owning reference (stock Irrlicht convention, see any
    // addXSceneNode() factory: construct, configure, drop() before
    // returning). Without this the node sits at refcount 2 forever: nothing
    // in this codebase ever holds a second owning pointer to balance it, so
    // EntityRenderer's later remove()+drop() teardown can only bring it back
    // to 1, never 0 -- ~EQAnimatedMeshSceneNode() (and the instanceMesh_/
    // eqMesh_ it owns) never runs. Confirmed as a true LSAN-unreachable leak
    // over a repeated zone-crossing tour, not a live/bounded cache question.
    node->drop();

    // Set scale
    float scale = getRaceScale(raceId);
    node->setScale(irr::core::vector3df(scale, scale, scale));

    // Start with idle animation if available
    if (node->hasAnimation("o01")) {
        node->playAnimation("o01", true);  // Stand idle
    } else if (node->hasAnimation("l01")) {
        node->playAnimation("l01", true);  // Walk as fallback
    }

    return node;
}

EQAnimatedMeshSceneNode* RaceModelLoader::createAnimatedNodeWithAppearance(uint16_t raceId, uint8_t gender,
                                                                            uint8_t headVariant, uint8_t bodyVariant,
                                                                            irr::scene::ISceneNode* parent,
                                                                            irr::s32 id,
                                                                            bool compositeAllowed) {
    EQAnimatedMesh* animMesh = getAnimatedMeshWithAppearance(raceId, gender, headVariant, bodyVariant, 0, compositeAllowed);
    if (!animMesh) {
        // Fall back to default appearance
        return createAnimatedNode(raceId, gender, parent, id, compositeAllowed);
    }

    // Use parent's scene manager if provided, otherwise use our default smgr_
    irr::scene::ISceneNode* parentNode = parent ? parent : smgr_->getRootSceneNode();
    irr::scene::ISceneManager* targetSmgr = parent ? parent->getSceneManager() : smgr_;

    // Create the scene node
    EQAnimatedMeshSceneNode* node = new EQAnimatedMeshSceneNode(
        animMesh,
        parentNode,
        targetSmgr,
        id
    );
    // T127: see the identical drop() in createAnimatedNode() above -- same
    // missing balancing drop() after parentNode->addChild()'s grab().
    node->drop();

    // Set scale
    float scale = getRaceScale(raceId);
    node->setScale(irr::core::vector3df(scale, scale, scale));

    // Start with idle animation if available
    if (node->hasAnimation("o01")) {
        node->playAnimation("o01", true);  // Stand idle
    } else if (node->hasAnimation("l01")) {
        node->playAnimation("l01", true);  // Walk as fallback
    }

    return node;
}

EQAnimatedMeshSceneNode* RaceModelLoader::createAnimatedNodeWithEquipment(uint16_t raceId, uint8_t gender,
                                                                           const EntityAppearance& appearance,
                                                                           irr::scene::ISceneNode* parent,
                                                                           irr::s32 id,
                                                                           bool compositeAllowed) {
    // Appearance fields:
    // - helm: Head mesh variant for NPCs (selects HE01, HE02, etc.)
    // - texture: Body armor texture for NPCs (0=naked, 1-4=armor, 11-16=robes)
    // - equipment[Chest]: Chest equipment material for PCs (11-16=robes)
    //
    // For robes (texture 11-16), we need to load the robe body mesh variant (01).
    // The robe mesh is named {RACE}01_DMSPRITEDEF (e.g., HUM01_DMSPRITEDEF).
    // Races with robe meshes: DAF, DAM, ERF, ERM, GNF, GNM, HIF, HIM, HUF, HUM

    uint8_t headVariant = appearance.helm;     // Head mesh variant (from helm field)
    uint8_t bodyVariant = 0;
    uint8_t textureVariant = appearance.texture;  // Equipment texture variant

    // Check if wearing a robe - can be in appearance.texture (NPCs) OR equipment[Chest] (PCs)
    uint8_t chestMaterial = static_cast<uint8_t>(appearance.equipment[static_cast<uint8_t>(EquipSlot::Chest)] & 0xFF);
    bool hasRobeFromTexture = isRobeTexture(appearance.texture);
    bool hasRobeFromEquipment = isRobeTexture(chestMaterial);

    if (hasRobeFromTexture || hasRobeFromEquipment) {
        // EQ only has ONE robe body mesh per race (XX01_DMSPRITEDEF)
        // Different robe textures (11-16) apply different CLK textures to this same mesh
        bodyVariant = 1;  // Always use XX01 mesh for robes

        // Get the robe texture ID to apply correct CLK textures
        uint8_t robeTexture = hasRobeFromTexture ? appearance.texture : chestMaterial;
        textureVariant = robeTexture;  // Apply robe texture (11, 12, 13, etc.)

        LOG_DEBUG(MOD_GRAPHICS, "createAnimatedNodeWithEquipment: Robe detected (texture={}, chest={}), using body variant 01, textureVariant={}",
                  (int)appearance.texture, (int)chestMaterial, (int)textureVariant);
    }

    // First try to get the base animated mesh with head/body/texture variants
    // The texture variant is passed to apply equipment texture overrides during mesh building
    EQAnimatedMesh* animMesh = getAnimatedMeshWithAppearance(raceId, gender, headVariant, bodyVariant, textureVariant, compositeAllowed);

    // If robe body variant (01) not found, fall back to default body mesh with robe textures
    if (!animMesh && bodyVariant == 1) {
        LOG_DEBUG(MOD_GRAPHICS, "createAnimatedNodeWithEquipment: Robe body variant 01 not found for race {}, using default body with robe textures", raceId);
        animMesh = getAnimatedMeshWithAppearance(raceId, gender, headVariant, 0, textureVariant, compositeAllowed);
    }

    // If head variant not found (e.g., SKEHE01 doesn't exist), fall back to head variant 0
    if (!animMesh && headVariant > 0) {
        LOG_DEBUG(MOD_GRAPHICS, "createAnimatedNodeWithEquipment: Head variant {} not found for race {}, falling back to head variant 0", (int)headVariant, raceId);
        animMesh = getAnimatedMeshWithAppearance(raceId, gender, 0, bodyVariant, textureVariant, compositeAllowed);
    }

    // Final fallback to default mesh (no robe textures)
    if (!animMesh) {
        animMesh = getAnimatedMeshForRace(raceId, gender, compositeAllowed);
    }

    if (!animMesh) {
        return nullptr;
    }

    // Get the model data to access textures
    // Try variant cache first (for models loaded with head/body variants)
    // Note: Model data is keyed without texture variant since geometry is the same
    std::shared_ptr<RaceModelData> modelData;
    uint64_t modelKey = makeVariantCacheKey(raceId, gender, headVariant, bodyVariant, 0);
    {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        auto variantIt = variantModels_.find(modelKey);
        if (variantIt != variantModels_.end()) {
            modelData = variantIt->second;
        }
    }
    if (!modelData) {
        // Fall back to base model data
        modelData = getRaceModelData(raceId, gender);
    }
    std::string raceCode = getRaceCode(raceId);

    // Adjust race code for gender (e.g., HUM -> HUF for female)
    if (gender == 1 && raceCode.length() == 3 && raceCode.back() == 'M') {
        raceCode[raceCode.length() - 1] = 'F';
    }

    // Use parent's scene manager if provided, otherwise use our default smgr_
    // This is critical for callers that create nodes in an isolated scene
    // manager of their own (e.g. a standalone model-viewer tool) rather
    // than the shared zone scene manager. (T54: this comment used to name
    // CharacterModelView, the inventory paperdoll UI, as the example —
    // that class was deleted in 0f25cb57 along with WindowManager; no
    // replacement isolated-scene-manager caller exists today, but the
    // parent-vs-default-smgr_ branch below still needs to hold for any
    // future one.)
    irr::scene::ISceneNode* parentNode = parent ? parent : smgr_->getRootSceneNode();
    irr::scene::ISceneManager* targetSmgr = parent ? parent->getSceneManager() : smgr_;

    // Create the scene node
    EQAnimatedMeshSceneNode* node = new EQAnimatedMeshSceneNode(
        animMesh,
        parentNode,
        targetSmgr,
        id
    );
    // T127: see the identical drop() in createAnimatedNode() above -- same
    // missing balancing drop() after parentNode->addChild()'s grab(). Safe to
    // drop here, before the extensive node->getMaterial()/node->getMesh()
    // configuration below: the parent's own reference (from addChild) keeps
    // the node alive for the rest of this function and for the caller, this
    // factory just stops holding a second, never-released owning reference.
    node->drop();

    // Apply equipment textures to the scene node's materials (NOT the cached mesh!)
    // We modify the node's materials, not the mesh buffer's materials, to avoid corrupting the cache
    //
    // IMPORTANT: If textureVariant > 0, the mesh was already built with equipment textures
    // baked in via buildMeshFromGeometry. Skip the override to avoid corrupting the correct textures.
    // Only apply textures here for textureVariant == 0 (when mesh has base "00" textures).
    LOG_TRACE(MOD_GRAPHICS, "createAnimatedNodeWithEquipment: modelData={} node materials={} textureVariant={} skipOverrides={}",
              (modelData ? "yes" : "no"), node->getMaterialCount(), (int)textureVariant, (textureVariant > 0));
    if (textureVariant == 0 && modelData && node->getMaterialCount() > 0) {
        irr::scene::IMesh* mesh = node->getMesh() ? node->getMesh()->getMesh(0) : nullptr;
        if (mesh) {
            // Map body part textures to equipment textures
            for (irr::u32 b = 0; b < mesh->getMeshBufferCount() && b < node->getMaterialCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                if (!buffer) continue;

                // Check if this buffer uses a body part texture that should be replaced
                // Read from mesh buffer to identify texture, but write to node material
                irr::video::ITexture* currentTex = buffer->getMaterial().getTexture(0);
                LOG_DEBUG(MOD_GRAPHICS, "  buffer[{}] currentTex={}", b, (currentTex ? "yes" : "no"));
                if (!currentTex) continue;

                // P17 defensive check (refactor/deferred.md, "found by P15,
                // NOT chased"): `buffer` is a mesh buffer on this race's
                // SHARED, RaceModelLoader-cached base mesh (node->getMesh()
                // ->getMesh(0)) -- not per-instance state EntityMeshTextureGuard
                // covers (that guard protects EQAnimatedMeshSceneNode's OWN
                // materials_/instanceMesh_, a different set of objects). A
                // texture referenced by this shared cached mesh's material
                // can go dangling either from ordinary LRU eviction or from
                // the P17 zone-unload full clear() (ConstrainedTextureCache::
                // clear() calls driver_->removeTexture() for every entry) if
                // this race's base mesh survives clearMeshCaches() -- reused
                // for the same race across zone visits -- with the OLD
                // zone's now-freed texture pointer still baked into its
                // material. Reproduced live: befallen -> qeynos2 -> befallen
                // (2nd visit) SIGSEGV'd here on currentTex->getName() with 0
                // evictions logged (confirming this is the full-clear path,
                // not eviction pressure -- matches deferred.md's own
                // "confirmed independent of eviction volume" note). Mirrors
                // ZoneMergeBuffers::build()'s isTextureLive defensive check
                // (P10b) at the equivalent dereference point: same pattern,
                // same isLiveTexture() reverse-pointer lookup, applied here
                // instead of trusting the raw pointer.
                if (constrainedTextureCache_ && !constrainedTextureCache_->isLiveTexture(currentTex)) {
                    LOG_DEBUG(MOD_GRAPHICS, "  buffer[{}] currentTex is stale (evicted/cleared) -- "
                              "skipping body-part-texture remap for this buffer", b);
                    continue;
                }

                // P50: ground-truth masked status for this slot, read off the
                // shared cached mesh buffer's material (set at build time by
                // buildMeshFromGeometry's isMaskedMaterial -> MaterialType
                // assignment, mesh_building.cpp — P37). EntityRenderer::
                // applyAlphaTestMaterialType() (P21), which would otherwise
                // convert this into a custom shader material id, only runs
                // AFTER createAnimatedNodeWithEquipment() returns (see
                // entity_renderer.cpp call sites), so this check is reliable
                // here. Closes P46d Part B gap #2: the per-slot equipment
                // texture-variant swap below previously called
                // loadTextureFromBMP() with no mask flag, so a masked base
                // body texture lost its mask on any equipped (materialId != 0)
                // slot.
                bool isMaskedMaterial = buffer->getMaterial().MaterialType ==
                                         irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;

                std::string currentTexName = currentTex->getName().getPath().c_str();
                LOG_DEBUG(MOD_GRAPHICS, "    texName: {}", currentTexName);
                std::string lowerTexName = currentTexName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                // Extract just the filename
                size_t lastSlash = lowerTexName.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    lowerTexName = lowerTexName.substr(lastSlash + 1);
                }

                // Check which body part this texture is for
                EquipSlot slot = EquipSlot::Chest;  // Default
                bool isBodyPartTexture = false;

                std::string lowerRace = raceCode;
                std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                if (lowerTexName.find(lowerRace + "ch") != std::string::npos) {
                    slot = EquipSlot::Chest;
                    isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "lg") != std::string::npos) {
                    slot = EquipSlot::Legs;
                    isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "ft") != std::string::npos) {
                    slot = EquipSlot::Feet;
                    isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "ua") != std::string::npos) {
                    slot = EquipSlot::Arms;
                    isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "fa") != std::string::npos) {
                    slot = EquipSlot::Wrist;
                    isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "hn") != std::string::npos) {
                    slot = EquipSlot::Hands;
                    isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "he") != std::string::npos) {
                    slot = EquipSlot::Head;
                    isBodyPartTexture = true;
                }

                if (isBodyPartTexture) {
                    // PC texture mapping:
                    // - Each body part uses its own equipment material from appearance.equipment[slot]
                    // - For NPCs without per-slot data, fall back to appearance.texture for body parts
                    // - Primary/Secondary are weapon models (not body textures)
                    uint32_t materialId = appearance.equipment[static_cast<uint8_t>(slot)];

                    // Fall back to appearance.texture if slot has no material but texture is set
                    // (This handles NPCs that use a uniform body texture)
                    if (materialId == 0 && slot != EquipSlot::Head && appearance.texture != 0) {
                        materialId = appearance.texture;
                    }

                    LOG_DEBUG(MOD_GRAPHICS, "  Body part texture: {} -> slot {}, materialId={} (body_tex={}, helm_tex={})",
                              lowerTexName, static_cast<int>(slot), materialId, (int)appearance.texture, appearance.equipment[0]);
                    if (materialId != 0) {
                        // Transform the original texture name to the variant texture
                        // This preserves the page number (e.g., humch0002.bmp -> humch0102.bmp)
                        // Pattern: {race}{part}00{page}.bmp -> {race}{part}{variant:02d}{page}.bmp
                        std::string equipTexName = getVariantTextureName(lowerTexName, static_cast<uint8_t>(materialId));

                        LOG_DEBUG(MOD_GRAPHICS, "    -> variant texture: {}", equipTexName);

                        // Try the variant texture first
                        auto texIt = modelData->textures.find(equipTexName);
                        if (texIt != modelData->textures.end() && texIt->second && meshBuilder_) {
                            irr::video::ITexture* equipTex = meshBuilder_->loadTextureFromBMP(
                                equipTexName, texIt->second->data, isMaskedMaterial);
                            if (equipTex) {
                                node->getMaterial(b).setTexture(0, equipTex);
                                LOG_DEBUG(MOD_GRAPHICS, "    -> REPLACED with {}", equipTexName);
                            }
                        } else {
                            // Variant texture not found - try the legacy equipment texture lookup
                            std::string legacyTexName = getEquipmentTextureName(raceCode, slot, materialId);
                            LOG_DEBUG(MOD_GRAPHICS, "    -> variant not found, trying legacy: {}", legacyTexName);
                            if (!legacyTexName.empty()) {
                                std::string lowerLegacyTex = legacyTexName;
                                std::transform(lowerLegacyTex.begin(), lowerLegacyTex.end(), lowerLegacyTex.begin(),
                                              [](unsigned char c) { return std::tolower(c); });

                                auto legacyIt = modelData->textures.find(lowerLegacyTex);
                                if (legacyIt != modelData->textures.end() && legacyIt->second && meshBuilder_) {
                                    irr::video::ITexture* equipTex = meshBuilder_->loadTextureFromBMP(
                                        legacyTexName, legacyIt->second->data, isMaskedMaterial);
                                    if (equipTex) {
                                        node->getMaterial(b).setTexture(0, equipTex);
                                        LOG_DEBUG(MOD_GRAPHICS, "    -> REPLACED with legacy {}", legacyTexName);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Apply equipment tints to body part materials
    // Tints are applied regardless of textureVariant since they're per-entity colors
    if (node->getMaterialCount() > 0) {
        irr::scene::IMesh* mesh = node->getMesh() ? node->getMesh()->getMesh(0) : nullptr;
        if (mesh) {
            // Debug: log all tint values for this entity
            bool hasAnyTint = false;
            for (int i = 0; i < 9; i++) {
                if (appearance.equipment_tint[i] != 0) hasAnyTint = true;
            }
            if (hasAnyTint) {
                LOG_DEBUG(MOD_GRAPHICS, "Entity tints: [{:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}, {:08X}]",
                          appearance.equipment_tint[0], appearance.equipment_tint[1], appearance.equipment_tint[2],
                          appearance.equipment_tint[3], appearance.equipment_tint[4], appearance.equipment_tint[5],
                          appearance.equipment_tint[6], appearance.equipment_tint[7], appearance.equipment_tint[8]);
            }

            std::string lowerRace = raceCode;
            std::transform(lowerRace.begin(), lowerRace.end(), lowerRace.begin(),
                          [](unsigned char c) { return std::tolower(c); });

            for (irr::u32 b = 0; b < mesh->getMeshBufferCount() && b < node->getMaterialCount(); ++b) {
                irr::scene::IMeshBuffer* buffer = mesh->getMeshBuffer(b);
                if (!buffer) continue;

                irr::video::ITexture* currentTex = buffer->getMaterial().getTexture(0);
                if (!currentTex) continue;

                std::string currentTexName = currentTex->getName().getPath().c_str();
                std::string lowerTexName = currentTexName;
                std::transform(lowerTexName.begin(), lowerTexName.end(), lowerTexName.begin(),
                              [](unsigned char c) { return std::tolower(c); });

                size_t lastSlash = lowerTexName.find_last_of("/\\");
                if (lastSlash != std::string::npos) {
                    lowerTexName = lowerTexName.substr(lastSlash + 1);
                }

                // Determine which equipment slot this texture belongs to
                EquipSlot slot = EquipSlot::Chest;
                bool isBodyPartTexture = false;

                if (lowerTexName.find(lowerRace + "ch") != std::string::npos) {
                    slot = EquipSlot::Chest; isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "lg") != std::string::npos) {
                    slot = EquipSlot::Legs; isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "ft") != std::string::npos) {
                    slot = EquipSlot::Feet; isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "ua") != std::string::npos) {
                    slot = EquipSlot::Arms; isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "fa") != std::string::npos) {
                    slot = EquipSlot::Wrist; isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "hn") != std::string::npos) {
                    slot = EquipSlot::Hands; isBodyPartTexture = true;
                } else if (lowerTexName.find(lowerRace + "he") != std::string::npos) {
                    slot = EquipSlot::Head; isBodyPartTexture = true;
                }
                // Also check for CLK (robe) textures - apply chest tint
                // Texture names may have prefix like "eqt_tex_clk0601.bmp"
                else if (lowerTexName.find("clk") != std::string::npos) {
                    slot = EquipSlot::Chest; isBodyPartTexture = true;
                }

                if (isBodyPartTexture) {
                    uint32_t tint = appearance.equipment_tint[static_cast<uint8_t>(slot)];
                    if (tint != 0) {
                        // Tint is in BGRA format: B at bits 0-7, G at 8-15, R at 16-23
                        uint8_t tintR = (tint >> 16) & 0xFF;
                        uint8_t tintG = (tint >> 8) & 0xFF;
                        uint8_t tintB = (tint >> 0) & 0xFF;

                        // Apply tint as diffuse color - this multiplies with the texture
                        // Enable lighting so material colors are used in rendering
                        node->getMaterial(b).Lighting = true;
                        node->getMaterial(b).ColorMaterial = irr::video::ECM_NONE;
                        node->getMaterial(b).DiffuseColor = irr::video::SColor(255, tintR, tintG, tintB);
                        node->getMaterial(b).AmbientColor = irr::video::SColor(255, tintR, tintG, tintB);

                        LOG_DEBUG(MOD_GRAPHICS, "Applied tint to buffer {}: slot={} tint=0x{:08X} RGB=({},{},{})",
                                  b, static_cast<int>(slot), tint, tintR, tintG, tintB);
                    }
                }
            }
        }
    }

    // Set scale
    float scale = getRaceScale(raceId);
    node->setScale(irr::core::vector3df(scale, scale, scale));

    // Start with idle animation if available
    if (node->hasAnimation("o01")) {
        node->playAnimation("o01", true);
    } else if (node->hasAnimation("l01")) {
        node->playAnimation("l01", true);
    }

    return node;
}

} // namespace Graphics
} // namespace EQT
