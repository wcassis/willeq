#ifndef EQT_GRAPHICS_RACE_MODEL_LOADER_H
#define EQT_GRAPHICS_RACE_MODEL_LOADER_H

#include "s3d_loader.h"
#include "zone_geometry.h"
#include "composite_texture_data.h"
#include "animated_mesh_scene_node.h"
#include "race_codes.h"
#include "equipment_textures.h"
#include <irrlicht.h>
#include <atomic>
#include <map>
#include <memory>
#include <chrono>
#include <mutex>
#include <functional>
#include <thread>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <list>

// Forward declarations
namespace EQT { namespace Graphics { struct EntityAppearance; class GraphicsArchiveIndex; class ConstrainedTextureCache; } }

namespace EQT {
namespace Graphics {

// Pre-decoded texture data (CPU-side ARGB pixels, ready for GPU upload)
struct DecodedTexture {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> argbPixels;  // Pre-decoded ARGB for GPU upload
    bool hasAlpha = false;
};

// Race model data - combined geometry for a race/gender combo
// T504 (load-step audit): RaceModelData::textures used to be a std::map BY VALUE
// and a dozen sites assigned it `getMergedTextures()`, which returns the merged
// map by value -- so every base model, every appearance variant and every _chr
// model carried its own full copy of a ~1,000-node map with string keys.
// MEASURED qeynos2: 10.5 MiB in 47,535 allocations for the variant models alone
// (~260 KB and ~1,100 blocks each), filed under entity_instances.
//
// This is that map shared by pointer. Reads are unchanged (find/begin/end/size,
// and it converts to `const Map&`). The only writers call set(), which detaches
// a private copy first (copy-on-write). There is deliberately NO operator[]: a
// read through it would silently detach, so a missed site fails to compile.
class SharedTextureMap {
public:
    using Map = std::map<std::string, std::shared_ptr<TextureInfo>>;
    SharedTextureMap() = default;
    SharedTextureMap& operator=(const Map& m) { map_ = std::make_shared<Map>(m); return *this; }
    SharedTextureMap& operator=(Map&& m) { map_ = std::make_shared<Map>(std::move(m)); return *this; }
    SharedTextureMap& operator=(std::shared_ptr<const Map> shared) {
        map_ = std::const_pointer_cast<Map>(std::move(shared));   // never written through: set() detaches
        return *this;
    }
    const Map& get() const { return map_ ? *map_ : emptyMap(); }
    operator const Map&() const { return get(); }
    Map::const_iterator find(const std::string& k) const { return get().find(k); }
    Map::const_iterator begin() const { return get().begin(); }
    Map::const_iterator end() const { return get().end(); }
    size_t count(const std::string& k) const { return get().count(k); }
    size_t size() const { return get().size(); }
    bool empty() const { return get().empty(); }
    void set(const std::string& k, std::shared_ptr<TextureInfo> v) {
        if (!map_) map_ = std::make_shared<Map>();
        else if (map_.use_count() > 1) map_ = std::make_shared<Map>(*map_);
        (*map_)[k] = std::move(v);
    }
    // identity of the underlying storage, for tallies that must count a shared map once
    const void* storageId() const { return map_.get(); }
private:
    static const Map& emptyMap() { static const Map e; return e; }
    std::shared_ptr<Map> map_;
};

struct RaceModelData {
    std::shared_ptr<ZoneGeometry> combinedGeometry;      // Skinned geometry (bone transforms applied)
    std::shared_ptr<ZoneGeometry> rawGeometry;           // Raw geometry (no bone transforms, for animation)
    SharedTextureMap textures;   // T504: a shared snapshot, not a private copy (see the class)
    std::string raceName;
    uint16_t raceId = 0;
    uint8_t gender = 0;
    float scale = 1.0f;

    // Animation data
    std::shared_ptr<CharacterSkeleton> skeleton;     // Skeleton with animation tracks
    std::vector<VertexPiece> vertexPieces;           // Vertex-to-bone mapping for skinning

    // Composite texture data (if available — enables 1-draw-call entity rendering)
    std::shared_ptr<CompositeTextureData> compositeData;  // UV remap metadata
    std::string compositeBmpPath;  // EQE1 entry name for composite texture (e.g., "hum_hs_def_0_0")
};

// T452 (board B468): the S3D archives to probe for an armor texture, most
// likely first, derived from the name's {variant}{page} digits. Empty when the
// name is not a 17-23 armor texture, which is the answer "open nothing".
// Defined in model_loading.cpp, where the measurement behind it is written out.
std::vector<std::string> ArmorArchivesForTextureName(const std::string& lowerName);

// Loads and caches character models by race ID
class RaceModelLoader {
public:
    RaceModelLoader(irr::scene::ISceneManager* smgr, irr::video::IVideoDriver* driver,
                    irr::io::IFileSystem* fileSystem);
    ~RaceModelLoader();

    // Set the base path for EQ client files
    void setClientPath(const std::string& path);

    // Set graphics archive index for on-demand model loading (deferred mode)
    void setGraphicsArchiveIndex(GraphicsArchiveIndex* index) { graphicsArchiveIndex_ = index; }

    // Load all global character models from global_chr.s3d
    bool loadGlobalModels();

    // Load additional numbered global#_chr.s3d files (global2-global7).
    //
    // T500 Leg B: this loads their TEXTURES, and no longer their
    // CharacterModels. Every caller but one wanted the textures --
    // getMergedTextures() calls it for exactly that -- and retaining the
    // characters cost 15,486,203 B (2,856,632 of CharacterModel plus
    // 12,629,571 of animated skeletons, MEASURED x86, twelve global_chr races)
    // for archives the default old-models path never takes a model from.
    // Nothing is dropped: loadModelFromNumberedGlobal() calls
    // ensureNumberedGlobalCharacters() for the one archive it is about to
    // search, so a race that really does live in a numbered global gets its
    // model exactly as before, at the price of a parse. Load time is
    // secondary (docs/decisions.md); a missing model would be a pixel change
    // and is out of scope.
    bool loadNumberedGlobalModels();

    // T500 Leg B: parse global<num>_chr.s3d's characters into
    // numberedGlobalCharacters_[num] if they are not there yet. Idempotent;
    // a failed or empty archive is remembered so it is parsed once.
    bool ensureNumberedGlobalCharacters(int num);

    // Load zone-specific character models from zone_chr.s3d
    bool loadZoneModels(const std::string& zoneName);

    // T452 (board B468): load one armor texture on demand from whichever of
    // global17-23_amr.s3d holds it. Returns nullptr, and remembers that it
    // did, when no armor archive has the name. PUBLIC because this is the
    // exact call mesh_building.cpp's texture fallback makes and the exact
    // call tests/test_armor_texture_archive.cpp drives -- it was private, and
    // the six months it spent querying a table that did not exist went
    // unnoticed partly because nothing could reach it to check.
    std::shared_ptr<TextureInfo> getArmorTexture(const std::string& lowerName);

    // Set the current zone name (for zone-specific model loading)
    void setCurrentZone(const std::string& zoneName);

    // Check if a race model is available
    bool hasRaceModel(uint16_t raceId, uint8_t gender = 0) const;

    // Get an Irrlicht mesh for a specific race/gender
    // Returns nullptr if not found (caller should use placeholder)
    irr::scene::IMesh* getMeshForRace(uint16_t raceId, uint8_t gender = 0);

    // Get an Irrlicht mesh for a specific race/gender with appearance variants
    // headVariant: which head mesh to use (0 = default, maps to HUMHE00, HUMHE01, etc.)
    // bodyVariant: which body mesh to use (0 = default, maps to HUM, HUM01, etc.)
    irr::scene::IMesh* getMeshForRaceWithAppearance(uint16_t raceId, uint8_t gender,
                                                     uint8_t headVariant, uint8_t bodyVariant);

    // Get an animated mesh for a specific race/gender
    // Returns nullptr if not found or no animation data available
    // compositeAllowed (P08d): per-entity eligibility gate for the composite-texture-sheet
    // mesh path. Defaults to true so every EXISTING caller (model_viewer, the composite
    // audit tool, the P08c benchmark tools) keeps its prior (dormant-unless-config-enabled)
    // behavior unchanged. The production entity path (entity_renderer.cpp) is the only
    // caller that explicitly passes false for ineligible entities (players, NPCs with
    // per-slot equipment overrides or tints) — see EntityRenderer::buildEntityMesh.
    // Composite-allowed and composite-disallowed builds for the SAME race/gender are
    // cached in SEPARATE maps (compositeAnimatedMeshCache_ vs animatedMeshCache_) so an
    // eligible NPC and an ineligible player of the same race never share a cached mesh.
    EQAnimatedMesh* getAnimatedMeshForRace(uint16_t raceId, uint8_t gender = 0, bool compositeAllowed = true);

    // Get an animated mesh with appearance-based variant selection
    // headVariant: which head mesh to use (0 = default)
    // bodyVariant: which body mesh to use (0 = default)
    // textureVariant: equipment texture (0=naked, 1=leather, 2=chain, 3=plate, 10+=robes)
    // compositeAllowed: see getAnimatedMeshForRace. Only consulted when headVariant==0 &&
    // bodyVariant==0 (entity_composite_builder only bakes the default body mesh geometry;
    // head/body mesh variants are structurally ineligible for a composite sheet).
    EQAnimatedMesh* getAnimatedMeshWithAppearance(uint16_t raceId, uint8_t gender,
                                                   uint8_t headVariant, uint8_t bodyVariant,
                                                   uint8_t textureVariant = 0,
                                                   bool compositeAllowed = true);

    // Create an animated mesh scene node for a race
    // The caller is responsible for adding the node to the scene
    EQAnimatedMeshSceneNode* createAnimatedNode(uint16_t raceId, uint8_t gender,
                                                  irr::scene::ISceneNode* parent = nullptr,
                                                  irr::s32 id = -1,
                                                  bool compositeAllowed = true);

    // Create an animated mesh scene node with appearance-based variants
    EQAnimatedMeshSceneNode* createAnimatedNodeWithAppearance(uint16_t raceId, uint8_t gender,
                                                               uint8_t headVariant, uint8_t bodyVariant,
                                                               irr::scene::ISceneNode* parent = nullptr,
                                                               irr::s32 id = -1,
                                                               bool compositeAllowed = true);

    // Create an animated mesh scene node with full equipment appearance
    // compositeAllowed (P08d): the production per-entity eligibility gate. Callers other
    // than entity_renderer.cpp should leave this at its default (true).
    EQAnimatedMeshSceneNode* createAnimatedNodeWithEquipment(uint16_t raceId, uint8_t gender,
                                                              const EntityAppearance& appearance,
                                                              irr::scene::ISceneNode* parent = nullptr,
                                                              irr::s32 id = -1,
                                                              bool compositeAllowed = true);

    // Get race model data
    std::shared_ptr<RaceModelData> getRaceModelData(uint16_t raceId, uint8_t gender = 0);

    // P13b: ensures loadedModels_[raceId,gender] is populated (running the same
    // S3D-archive search/load fallback chain getMeshForRace() used to run inline),
    // WITHOUT building a textured static mesh (buildTexturedMesh()'s eager per-part
    // texture decode/upload) as a side effect. getMeshForRace() calls this then
    // builds+caches its static mesh as before; getAnimatedMeshForRace() (which
    // previously called getMeshForRace() purely for this side effect, discarding
    // the returned mesh) now calls this directly so an eligible entity whose race
    // got a composite-sheet hit doesn't have its "deferred" per-part textures
    // eagerly decoded anyway by the static-mesh path. Returns nullptr if the race
    // model cannot be found by any search path (caller should use placeholder).
    std::shared_ptr<RaceModelData> ensureModelDataLoaded(uint16_t raceId, uint8_t gender = 0);

    // Get mesh builder (for registering pre-uploaded textures in multi-frame pipeline)
    ZoneMeshBuilder* getMeshBuilder() { return meshBuilder_.get(); }

    // Set constrained texture cache for direct texture loading via getOrLoad()
    void setConstrainedTextureCache(EQT::Graphics::ConstrainedTextureCache* cache) { constrainedTextureCache_ = cache; }

    // Set composite entity texture path (directory containing composite BMP + JSON from entity_composite_builder)
    void setCompositeEntityPath(const std::string& path) { compositeEntityPath_ = path; }

    // Background-safe: loads S3D model data + merges animations into staging cache.
    // Does NOT create textures, Irrlicht meshes, or scene nodes (no GL calls).
    // Reads from immutable archives (globalCharacters_, etc.) and writes to
    // preparedModelData_ (thread-safe staging map). Call promotePreparedModels()
    // on the main thread to move data into loadedModels_ before getMeshForRace().
    bool preloadModelData(uint16_t raceId, uint8_t gender, int textureVariant = 0);

    // Background-safe: loads variant model data (S3D load + character search +
    // part combining + animation merge) for zone-specific variants (e.g., QCM).
    // Stores result in variantModels_ (protected by variantModelsMutex_).
    // Does NOT create textures, Irrlicht meshes, or scene nodes (no GL calls).
    bool preloadVariantModel(uint16_t raceId, uint8_t gender,
                             uint8_t headVariant, uint8_t bodyVariant);

    // Get variant model data from variantModels_ cache (thread-safe).
    // Returns nullptr if not cached.
    std::shared_ptr<RaceModelData> getVariantModelData(uint16_t raceId, uint8_t gender,
                                                        uint8_t headVariant, uint8_t bodyVariant);

    // T340 (B052): the read-only observation seam for
    // loadModelFromGlobalChrWithVariantsForAnimation. That method searches an
    // ORDERED candidate list -- the race row's own gendered code, then the
    // row's fallback_code -- across an ordered source list (the row's
    // s3d_file, the current zone, then global_chr.s3d), and its six
    // "continue"s mean "try the next candidate", not "fail". Nothing it does
    // is observable from outside except the model it leaves in
    // variantModels_, and that cannot distinguish WHICH candidate won, nor a
    // run that returned false from a run that never happened (the caller
    // caches a null on false -- variant_loading.cpp:273-277 -- so the second
    // call short-circuits). This struct records both. It is written ONLY by
    // that loader and read ONLY by tests (tests/test_variant_loader_candidates.cpp);
    // no production code consults it and no control flow depends on it.
    // Guarded by variantModelsMutex_ -- the mutex the loader already takes at
    // both of its success points -- because the loader also runs on
    // EntityPrepWorker's background thread.
    struct VariantLoadTrace {
        size_t attempts = 0;            // times the loader actually ran
        bool succeeded = false;         // outcome of the most recent run
        std::string resolvedCode;       // the codeToTry that won ("" if none)
        std::string resolvedCharacter;  // CharacterModel::name that won ("" if none)
        std::string resolvedSource;     // "zone" (row s3d_file / current zone) or "global"
        // T340 Leg B: characters that matched the race code by name but carried
        // no usable variant meshes, so the character loop skipped them (G07's
        // exits 280 and 640). Counted per path, because 640 is the global-path
        // twin that Leg A left untested -- merging the zone and global blocks
        // with only the zone one under test would have been under-netted.
        size_t zoneSkips = 0;
        size_t globalSkips = 0;
    };
    VariantLoadTrace variantLoadTrace() const {
        std::lock_guard<std::mutex> lock(variantModelsMutex_);
        return variantLoadTrace_;
    }

    // Check if model data is available in either the main cache (loadedModels_)
    // or the staging cache (preparedModelData_). Thread-safe.
    // cacheKey = (raceId << 8) | gender
    bool isModelDataCached(uint32_t cacheKey) const;

    // T94: Returns true if a prior preloadModelData() call for this
    // race/gender already walked the FULL search chain (zoneCharacters_,
    // otherChrCaches_, JSON-specified S3D, global_chr.s3d, global2-7_chr.s3d)
    // and found nothing. A permanent negative memo — the S3D archives are
    // static for the process lifetime, so a race with no model at startup
    // has none at shutdown; there is no invalidation path to build. Callers
    // (loadZoneStep8a_BaseModelPrep, EntityPrepWorker::processRequest) use
    // this to skip calling preloadModelData() entirely for every subsequent
    // entity of an already-proven-absent race/gender, instead of rediscovering
    // "no model" from scratch per entity (T94's measured cost: 130/452
    // entities in Nektulos, 93.8% of Step 8's wall time, before this cache).
    // cacheKey = (raceId << 8) | gender (variant/texture index do not
    // participate — every preloadModelData() caller passes textureVariant=0,
    // and the parameter only affects a post-success composite lookup, never
    // whether the base model search finds a match). Thread-safe (same
    // preparedDataMutex_ preloadModelData already holds for its other
    // staging-cache checks).
    bool isRaceModelKnownMissing(uint32_t cacheKey) const;

    // Move prepared model data from staging cache to main cache.
    // Call this on the main thread before getMeshForRace() to make
    // background-preloaded data available for mesh building.
    void promotePreparedModels();

    // Get race scale factor (some races are larger/smaller)
    // Delegates to the free function in race_codes.h
    float getRaceScale(uint16_t raceId) const { return EQT::Graphics::getRaceScale(raceId); }

    // Get the S3D filename for a race (for loading from zone archives)
    // Delegates to the free function in race_codes.h
    static std::string getRaceModelFilename(uint16_t raceId, uint8_t gender) {
        return EQT::Graphics::getRaceModelFilename(raceId, gender);
    }

    // Get a 3-letter race code (HUM, ELF, DWF, etc.)
    // Delegates to the free function in race_codes.h
    static std::string getRaceCode(uint16_t raceId) {
        return EQT::Graphics::getRaceCode(raceId);
    }

    // Get number of loaded race models
    // T114: loadedModels_ is written by EntityPrepWorker's background thread
    // (via promotePreparedModels()) as well as the main/loading thread --
    // every other accessor in this class takes loadedModelsMutex_ (T113);
    // this one, called from renderer_diagnostics.cpp's /pmem path, did not.
    size_t getLoadedModelCount() const {
        std::lock_guard<std::mutex> lock(loadedModelsMutex_);
        return loadedModels_.size();
    }

    // Release raw texture data after GPU upload (frees CPU-side pixel data)
    // Armor texture cache is also released (reconstructible from index)
    // Returns the number of bytes freed
    // T504: the merged texture map as ONE shared immutable snapshot (rebuilt when
    // the merged cache is). Models keep the snapshot they were built against,
    // exactly as they kept their private copy before.
    std::shared_ptr<const SharedTextureMap::Map> getMergedTexturesShared();
    size_t releaseRawTextureData();

    // T504 (load-step audit): release one texture map's raw file bytes AS SOON
    // AS IT IS STORED, when an EQE1 flat pack is authoritative. On those presets
    // ConstrainedTextureCache::getOrLoad() serves every name from a pack and a
    // miss is a fatal exit, so the BMP/DDS bytes parsed out of the S3D archives
    // are never decoded -- yet they were held until releaseRawTextureData() ran
    // "after progressive load", ~140 ms AFTER the ready point, i.e. through the
    // whole load peak (MEASURED qeynos2: 17.8 MiB freed too late to lower it).
    // The textures stay present() (names, sizes, animation info), which is what
    // every reader needs. No-op (returns 0) without EQE1 loaders.
    size_t releaseBytesIfEqe1Authoritative(std::map<std::string, std::shared_ptr<TextureInfo>>& texMap);

    // Memory stats for /pmem reporting
    struct MemoryStats {
        size_t globalTextureBytes = 0;     // Raw bytes in globalTextures_
        size_t numberedTextureBytes = 0;   // Raw bytes in numberedGlobalTextures_
        size_t armorTextureBytes = 0;      // Raw bytes in armorTextureCache_
        size_t zoneTextureBytes = 0;       // Raw bytes in zoneTextures_
        size_t otherChrTextureBytes = 0;   // Raw bytes in otherChrCaches_ textures
        size_t loadedModelCount = 0;       // Number of loaded race models
        size_t meshCacheCount = 0;         // meshCache_ entries
        size_t variantMeshCacheCount = 0;  // variantMeshCache_ entries
        size_t animatedMeshCacheCount = 0; // animatedMeshCache_ + variantAnimatedMeshCache_
        size_t armorTextureCount = 0;      // Number of armor textures

        // Geometry and skeleton tracking (previously untracked)
        size_t modelGeometryBytes = 0;     // ZoneGeometry data in loadedModels_ + variantModels_
        size_t modelSkeletonBytes = 0;     // CharacterSkeleton + animation tracks in loadedModels_
        size_t characterModelBytes = 0;    // CharacterModel data (global + numbered + zone + other)
        size_t animatedMeshBytes = 0;      // EQAnimatedMesh working data (originalVertices_ etc.)
        size_t irrlichtMeshBytes = 0;      // Irrlicht IMesh vertex/index buffers in mesh caches

        // --- T500 Leg A: the entity_models byte split (ADDITIVE; no lifetime
        // moved, no existing field touched). Every field below is either
        // DISJOINT from the ten above or a stated SUBDIVISION of one of them,
        // so modelMemTotal() can sum them without double counting.

        // (a) RaceModelData::textures is a full std::map COPY of the merged
        // character-texture map, per model. The TextureInfo PIXELS are shared
        // (shared_ptr) and are already counted in globalTextureBytes /
        // numberedTextureBytes / armorTextureBytes / zoneTextureBytes /
        // otherChrTextureBytes; the MAP is not counted anywhere -- one
        // red-black node plus one std::string key per entry, per model.
        // DISJOINT from every field above.
        size_t mergedTextureMapBytes = 0;   // nodes + out-of-SSO keys, over
                                            // loadedModels_ + variantModels_ + preparedModelData_
        size_t mergedTextureMapCount = 0;   // how many such maps exist
        size_t mergedTextureEntryCount = 0; // total entries across them
        size_t distinctTextureMapCount = 0; // maps whose value-pointer set differs
                                            // from cachedMergedTextures_ (the rest are
                                            // byte-for-byte duplicates of it)

        // (b) the retained source CharacterModels, by owner. These four SUM to
        // characterModelBytes exactly -- a SUBDIVISION, not an addition.
        size_t sourceCharacterGlobalBytes = 0;    // globalCharacters_
        size_t sourceCharacterNumberedBytes = 0;  // numberedGlobalCharacters_
        size_t sourceCharacterZoneBytes = 0;      // zoneCharacters_
        size_t sourceCharacterOtherBytes = 0;     // otherChrCaches_[*].characters

        // (c) CharacterModel::rawParts holds a DEEP COPY of each part's
        // geometry (s3d_loader_characters.cpp: "Deep copy geometry for rawParts
        // BEFORE skinning is applied"), and characterModelBytes charges those
        // vectors only sizeof(CharacterPart) each -- the copied vertices and
        // triangles are counted NOWHERE. partsWithTransforms aliases `parts`
        // (same shared_ptr) and is correctly not double counted; this term
        // counts only geometry pointers that are in neither `parts` nor
        // `partsWithTransforms`. DISJOINT from every field above.
        size_t duplicateGeometryBytes = 0;

        // (d) T500 Leg A, second pass. The first pass closed to only 52 % of
        // the ledger; these are the four terms the residual turned out to be
        // made of, and each is DISJOINT from everything above.
        //
        // The big one: modelSkeletonBytes charges the skeletons of the twelve
        // models in loadedModels_ and NOTHING else, but every CharacterModel
        // retained in globalCharacters_ / numberedGlobalCharacters_ /
        // zoneCharacters_ / otherChrCaches_ holds its OWN animatedSkeleton
        // (bones, pose tracks, one BoneTransform array per animation per bone)
        // and its own SkeletonTrack, for every race in the archive including
        // the ones no spawn will ever use. Deduplicated by pointer against the
        // skeletons modelSkeletonBytes already charged.
        size_t sourceSkeletonGlobalBytes = 0;
        size_t sourceSkeletonNumberedBytes = 0;
        size_t sourceSkeletonZoneBytes = 0;
        size_t sourceSkeletonOtherBytes = 0;

        // ResolvedMaterialList is SHARED between geometries (that is the point
        // of the 0x31 fragment), so ZoneGeometry::getMemoryUsage() deliberately
        // excludes it and the caller is supposed to charge it once. Nothing
        // ever did. Deduplicated by pointer over every retained geometry.
        size_t materialListBytes = 0;

        // TextureInfo::rawDataBytes() is the PIXELS only; the object, its name
        // and its frames vector are not in it. Deduplicated by pointer.
        // B843: counts only objects whose ownerCategory IS entity_models;
        // foreign-owned objects are in sharedTextureBytes[].storageBytes.
        size_t textureObjectBytes = 0;

        // The loader's OWN texture maps (globalTextures_, numberedGlobalTextures_,
        // armorTextureCache_, zoneTextures_, otherChrCaches_[*].textures and
        // cachedMergedTextures_): nodes and keys, same arithmetic as
        // mergedTextureMapBytes, which covers only the per-MODEL copies.
        // B843: counts only entries whose ownerCategory IS entity_models;
        // foreign-owned entries' nodes + keys are in
        // sharedTextureBytes[].storageBytes.
        size_t loaderTextureMapBytes = 0;

        // (e) T500 Leg A, third pass. calcSkeletonBytes() charges the KEYFRAME
        // ARRAYS and little else: not the std::map nodes that hold the tracks,
        // not the TrackDef/TrackRef objects themselves, not the shared_ptr
        // control blocks, and it estimates an Animation's track map at a flat
        // 64 B per entry. With the source skeletons now in the tally (term d)
        // those omissions are the largest thing left. Deduplicated by pointer,
        // and DISJOINT from modelSkeletonBytes / sourceSkeleton*Bytes -- this
        // term is only what those two do not charge.
        size_t skeletonContainerBytes = 0;
        // Vector capacity the per-element sums above do not reach
        // (CharacterModel::parts, and excess capacity in the bone vectors).
        size_t containerCapacityBytes = 0;

        // (f) B843 (Will's ruling, docs/decisions.md 2026-09-17 item 3): a
        // TextureInfo is shared by shared_ptr, and the block it names -- its
        // PIXELS plus the map node + key that hold it and the TextureInfo
        // object itself -- was allocated under whatever MemoryScope the load
        // path that first parsed its archive was running in. On the Pi that is
        // ZoneParse for zoneTextures_ (the render thread's loadZoneModels
        // call, inside loadZoneStep1_S3D) and the entity-prep thread's scope
        // for the zone _chr.s3d caches, not EntityModels. The five tex_* terms
        // above therefore count only bytes whose ownerCategory IS
        // entity_models; the rest are reported ONCE here, deduplicated by
        // pointer, never added into this category's sum. The shared_ptr
        // control block, like the object it guards, lives in the allocator's
        // block and is intentionally outside this tally (the T500 (d) baseline
        // never charged it), so it is NOT part of storageBytes.
        // One entry per distinct shared TextureInfo held by a loader map.
        struct SharedTextureBytes {
            uint64_t bytes = 0;                 // rawDataBytes(), counted once
            uint64_t storageBytes = 0;          // map node+key (per occurrence) + object (once)
            const char* owner = "unknown";      // ledger category that allocated the block
            std::string claimants;              // comma-separated map names holding it
        };
        std::vector<SharedTextureBytes> sharedTextureBytes;

        // (g) B843: the RAW held totals the tex_* terms are a SUBDIVISION of
        // (owned + shared, per map). Exposed so a test can assert the split
        // against the bytes actually held rather than re-summing the maps.
        uint64_t zoneCharacterTexturesHeld = 0;      // sum of zoneTextures_ rawDataBytes
        uint64_t numberedTextureBytesHeld = 0;       // sum of numberedGlobalTextures_
    };
    MemoryStats getMemoryStats() const;

    // T500 Leg A: the sum of the DISJOINT terms in MemoryStats -- i.e. what
    // RaceModelLoader claims to hold. The bound for Leg B's red, and the left
    // side of the split-closes check against
    // eqt::memory::stats(Category::EntityModels).liveBytes.
    // B843: the five tex_* terms count ONLY pixels whose owning ledger
    // category is EntityModels (TextureInfo::ownerCategory). Pixels the
    // loader holds but that were allocated under another category (the Pi's
    // zoneTextures_ under ZoneParse, the zone _chr.s3d caches under the
    // entity-prep scope) are excluded here and reported ONCE, with every
    // claimant named, in MemoryStats::sharedTextureBytes -- Will's ruling,
    // docs/decisions.md 2026-09-17 item 3.
    static size_t modelMemTotal(const MemoryStats& s);

    // T500 Leg A: the `[modelmem] ...` line. Emitted once per zone-in at
    // zone_load_complete (LOG_INFO, MOD_GRAPHICS) and used by the offline
    // split test. Carries the ledger's EntityModels live/peak read at the SAME
    // instant, so total-vs-ledger is one line, not two greps.
    std::string modelMemLogLine(const std::string& point) const;
    // T504b Leg 0: the source-character census. One [srcchar] line per
    // retained archive: characters parsed, how many a built model was made
    // from (skeleton identity), how many only DONATE animations to a built
    // model (Animation pointer identity), how many nothing references, and
    // the bytes of each class (geometry once per distinct part + skeleton +
    // tracks). "unused" bytes are what Leg 2 may drop; the leg is held to
    // this number.
    std::string srcCharLogLines(const std::string& point) const;

    // B843: the `[modelmem-shared] ...` lines, in Will's ruling form
    // (`X: owner_category, [claimant, claimant, ...]` per distinct block,
    // docs/decisions.md 2026-09-17 item 3). One line per distinct TextureInfo
    // whose pixels this loader holds but whose ALLOCATING ledger category is
    // not entity_models -- reported once, never added into the entity_models
    // total. Empty string when there is nothing shared, so the call site logs
    // nothing.
    std::string modelMemSharedLines(const std::string& point) const;

    // P08d: composite sheet hit/miss counters, tracked across all composite-ALLOWED mesh
    // build attempts (getAnimatedMeshForRace / getAnimatedMeshWithAppearance) since the
    // last reset. "Hit" = a pre-baked sheet was found and successfully built into a
    // composite mesh; "miss" = compositeAllowed was true but no usable sheet existed
    // (missing JSON, texture not yet uploaded, or build failure) and the call fell back
    // to the multi-buffer path. Used for the per-zone-load adoption summary log.
    size_t getCompositeSheetHits() const { return compositeSheetHits_; }
    size_t getCompositeSheetMisses() const { return compositeSheetMisses_; }
    void resetCompositeAdoptionStats() { compositeSheetHits_ = 0; compositeSheetMisses_ = 0; }

    // Reference counting for mesh cache eviction
    // Call addMeshRef when creating an entity with this race/gender
    // Call removeMeshRef when removing that entity
    void addMeshRef(uint16_t raceId, uint8_t gender);
    void removeMeshRef(uint16_t raceId, uint8_t gender);

    // T504b Leg 2b (Will, rev 1: residency is a working set in both
    // directions). A model whose last entity despawned is not evicted at
    // once: it becomes EVICTABLE and is dropped by sweepEvictable() after a
    // hysteresis window (WILLEQ_MODEL_EVICT_SECONDS, default 60 -- a roamer
    // crossing the visibility edge must not thrash a 0.75 s archive parse),
    // or at once, oldest first, while the entity_models ledger row is over
    // its budget. addMeshRef() cancels a pending eviction. After an eviction
    // the source characters are pruned too (pruneSourceCharacters), so both
    // the built model and its retained source leave. Render thread only,
    // like the refcounts themselves.
    void sweepEvictable();
    void setEntityModelBudgetBytes(size_t bytes) { entityModelBudgetBytes_ = bytes; }
    size_t evictableCountForTest() const { return evictable_.size(); }
    void sweepEvictableForTest(double ageSeconds);   // treats every evictable entry as ageSeconds old

    // Set maximum cached _chr.s3d entries (0 = unlimited)
    void setMaxChrCacheEntries(size_t max) { maxChrCacheEntries_ = max; }

    // T364 (B065): the chr-cache LRU accounting has no other observable
    // surface -- otherChrCaches_, chrCacheLruOrder_ and maxChrCacheEntries_
    // are all private and getMemoryStats() reports only texture bytes. The
    // invariant a bypassed insert breaks (T91, and B065 at the one surviving
    // site) is `untracked == 0`: an entry that never entered
    // chrCacheLruOrder_ counts against maxChrCacheEntries_ forever and can
    // never be evicted, so `entries` climbs past `maxEntries` and the
    // legitimately tracked entries thrash instead.
    struct ChrCacheAccounting {
        size_t entries = 0;    // otherChrCaches_.size()
        size_t lruOrder = 0;   // chrCacheLruOrder_.size()
        size_t untracked = 0;  // cache keys absent from chrCacheLruOrder_
        size_t maxEntries = 0; // maxChrCacheEntries_ (0 = unlimited)
    };
    ChrCacheAccounting getChrCacheAccountingForTest() const;

    // Old models mode - when true, only load from global_chr.s3d (classic models)
    // When false, prefer race-specific S3D files (Luclin+ models)
    void setUseOldModels(bool useOld);
    bool isUsingOldModels() const { return useOldModels_; }

    // Clear cached meshes (call after toggling old/new models)
    void clearCache();

    // Clear mesh caches for zone transition (keeps model data, forces fresh mesh/texture rebuild)
    void clearMeshCaches();

    // Build a composite mesh from geometry + pre-uploaded atlas texture + UV metadata.
    // Public interface for dynamic atlas pipeline (audit tool, runtime atlas builder).
    irr::scene::IMesh* buildCompositeEntityMesh(
        const std::shared_ptr<ZoneGeometry>& geometry,
        irr::video::ITexture* compositeTexture,
        const CompositeTextureData& compositeData) {
        return buildCompositeMeshFromGeometry(geometry, compositeTexture, compositeData);
    }

private:
    // Load model from a specific S3D file
    bool loadModelFromS3D(const std::string& s3dPath, uint16_t raceId, uint8_t gender);

    // Load model from global_chr.s3d by searching for race code
    bool loadModelFromGlobalChr(uint16_t raceId, uint8_t gender);

    // Load model from a numbered global#_chr.s3d file
    bool loadModelFromNumberedGlobal(int globalNum, uint16_t raceId, uint8_t gender);

    // Load model from zone-specific _chr.s3d file (clears cache, for current game zone)
    bool loadModelFromZoneChr(const std::string& zoneName, uint16_t raceId, uint8_t gender);

    // Load model from a chr file using otherChrCaches_ (preserves cache, for JSON-specified files)
    bool loadModelFromCachedChr(const std::string& chrFilename, uint16_t raceId, uint8_t gender);

    // Load model using GraphicsArchiveIndex for targeted per-archive loading
    bool loadModelFromArchiveIndex(uint16_t raceId, uint8_t gender);

    // Search all loaded global character archives for a model
    bool searchAllGlobalsForModel(uint16_t raceId, uint8_t gender);

    // Search all zone _chr.s3d files in the client directory for a model
    bool searchZoneChrFilesForModel(uint16_t raceId, uint8_t gender);

    // Build an Irrlicht mesh from geometry data
    // bodyTextureVariant: 0=naked, 1=leather, 2=chain, 3=plate, 10+=robes
    // raceCode: 3-letter code like "QCF", "HUM" for texture name transformation
    irr::scene::IMesh* buildMeshFromGeometry(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint8_t bodyTextureVariant = 0,
        const std::string& raceCode = "");

    // Build a single-buffer mesh using composite texture sheet (1 draw call per entity).
    // Remaps UVs to composite sheet coordinates and populates vertexMappingForAnimation_.
    // compositeTexture: pre-uploaded composite sheet ITexture*
    // compositeData: UV remap metadata from entity_composite_builder JSON
    irr::scene::IMesh* buildCompositeMeshFromGeometry(
        const std::shared_ptr<ZoneGeometry>& geometry,
        irr::video::ITexture* compositeTexture,
        const CompositeTextureData& compositeData);

    // Build an Irrlicht mesh with equipment texture overrides
    irr::scene::IMesh* buildMeshWithEquipment(
        const std::shared_ptr<ZoneGeometry>& geometry,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        const std::string& raceCode,
        const uint32_t* equipment);  // Array of 9 material IDs

    // Create cache key for race/gender combo
    static uint32_t makeCacheKey(uint16_t raceId, uint8_t gender) {
        return (static_cast<uint32_t>(raceId) << 8) | gender;
    }

    // Create cache key for race/gender/variant combo (includes head, body, and texture variant)
    static uint64_t makeVariantCacheKey(uint16_t raceId, uint8_t gender, uint8_t headVariant, uint8_t bodyVariant, uint8_t textureVariant = 0) {
        return (static_cast<uint64_t>(raceId) << 32) |
               (static_cast<uint64_t>(gender) << 24) |
               (static_cast<uint64_t>(headVariant) << 16) |
               (static_cast<uint64_t>(bodyVariant) << 8) |
               textureVariant;
    }

    // Load model from global_chr.s3d with specific variants
    bool loadModelFromGlobalChrWithVariants(uint16_t raceId, uint8_t gender,
                                            uint8_t headVariant, uint8_t bodyVariant);

    // Load model from global_chr.s3d with specific variants including raw geometry for animation
    bool loadModelFromGlobalChrWithVariantsForAnimation(uint16_t raceId, uint8_t gender,
                                                         uint8_t headVariant, uint8_t bodyVariant);

    // ---- T340 Leg B (B052): the four pieces that method was built out of ----
    // It was 815 lines because it was DUPLICATED, not because it had phases:
    // one animation-merge body written out four times (byte-identical once
    // normalised) and one match-select-combine-register character body written
    // out twice, 0.906 similar. See refactor/T340_variant_loader_behaviour_test.md
    // and refactor/G07_variant_loader.md. The candidate loop keeps its shape:
    // "continue" there still means TRY THE NEXT CANDIDATE, never "abandon", and
    // returning false from the whole method still makes the caller cache a
    // permanent null (Will's EQE1 all-or-nothing rule, docs/decisions.md).

    // One candidate: the race code being tried plus everything derived from it
    // that every helper below needs. Built once per iteration of the loop.
    struct VariantRequest {
        uint16_t raceId = 0;
        uint8_t gender = 0;
        uint8_t headVariant = 0;
        uint8_t bodyVariant = 0;
        std::string codeToTry;            // the candidate race code, uppercase
        std::string animationSourceCode;  // ELM/ELF/... , uppercase, may be empty
        std::string bodyMeshName;         // "{CODE}_DMSPRITEDEF" or "{CODE}{NN}_DMSPRITEDEF"
        std::string headMeshName;         // "{CODE}HE{NN}_DMSPRITEDEF"
    };

    // The candidate list, in order: the row's own gendered code, then the row's
    // fallback_code. Also resolves the animation source code. False = there is
    // no candidate at all, the one exit of this method that really is "abandon"
    // (G07 exit 69).
    bool buildCodesToTry(uint16_t raceId, uint8_t gender,
                         std::vector<std::string>& codesToTry,
                         std::string& animationSourceCode);

    // Pick this variant's body and head meshes out of ONE character, skinned and
    // raw, falling back to variant 0 when the requested variant is absent.
    // False = this character carries nothing usable, so the caller must try the
    // NEXT character (G07 exits 280 and 640). NOT "abandon the load".
    bool selectVariantParts(const CharacterModel& character, const VariantRequest& req,
                            std::vector<CharacterPart>& selectedSkinnedParts,
                            std::vector<CharacterPart>& selectedRawParts);

    // Copy into ourSkel every animation and per-bone animation track it is
    // MISSING from sourceSkel, mapping bone names by race-code substitution
    // (qcmpe -> elmpe). Model-specific animations are NEVER overwritten -- the
    // EQSage rule. This body appeared four times; the copies were identical, so
    // nothing here is parameterised except the two counts each call site logs.
    void mergeAnimationsFrom(CharacterSkeleton& ourSkel, const CharacterSkeleton& sourceSkel,
                             const std::string& codeToTry, const std::string& animationSourceCode,
                             int& addedAnimations, int& mappedBones);

    // Search ONE character source for a character carrying this candidate's
    // variant meshes; on the first that does, build, register and cache the
    // RaceModelData and return true (the loader's two success exits). False =
    // nothing in THIS source matched, so the caller tries the next source, then
    // the global source, then the next candidate. fromGlobal selects the
    // global-path log wording and the trace's resolvedSource; it changes no
    // control flow.
    bool tryBuildVariantFrom(const std::vector<std::shared_ptr<CharacterModel>>& characters,
                             bool fromGlobal, const VariantRequest& req);

    // Build a merged texture map from all sources (global + numbered globals + zone)
    // Order: global_chr.s3d -> global2-7_chr.s3d (new only) -> zone_chr.s3d (overrides)
    std::map<std::string, std::shared_ptr<TextureInfo>> getMergedTextures();

    irr::scene::ISceneManager* smgr_;
    irr::video::IVideoDriver* driver_;
    irr::io::IFileSystem* fileSystem_;
    std::unique_ptr<ZoneMeshBuilder> meshBuilder_;

    std::string clientPath_;

    // Graphics archive index for on-demand loading (deferred mode, non-owning)
    GraphicsArchiveIndex* graphicsArchiveIndex_ = nullptr;

    // Constrained texture cache for direct texture loading (non-owning)
    EQT::Graphics::ConstrainedTextureCache* constrainedTextureCache_ = nullptr;
    std::shared_ptr<const SharedTextureMap::Map> mergedTexturesShared_;  // T504
    uint64_t mergedTexturesGeneration_ = 0;        // bumped on every merged-map rebuild
    uint64_t mergedTexturesSharedGeneration_ = 0;  // generation mergedTexturesShared_ was built from

    // Composite entity texture directory (from entity_composite_builder output)
    std::string compositeEntityPath_;

    // Staging cache for background-preloaded model data.
    // Written by EntityPrepWorker thread (preloadModelData).
    // Promoted to loadedModels_ on main thread via promotePreparedModels().
    mutable std::mutex preparedDataMutex_;
    std::map<uint32_t, std::shared_ptr<RaceModelData>> preparedModelData_;

    // T94: negative-result memo for preloadModelData()'s full search chain.
    // Key present => that race/gender has no model anywhere; value is the
    // number of entities that have hit the negative cache so far (for the
    // zone-load summary, so the once-per-race log line can report how many
    // entities were affected without re-searching). Guarded by
    // preparedDataMutex_, same as preparedModelData_/loadedModels_ staging
    // reads elsewhere in this class.
    std::unordered_map<uint32_t, size_t> knownMissingRaceGenders_;

    // P13b: per-part texture names DEFERRED (decode/upload skipped) because
    // preloadModelData() got a composite-sheet HIT for that (race, gender) —
    // see race_model_loader.cpp's T05 loop guard. Populated under
    // preparedDataMutex_ (same lock preloadModelData already holds when
    // touching preparedModelData_). Never cleared: an entry stays "deferred"
    // for the RaceModelLoader's whole lifetime (races persist across zone
    // transitions in loadedModels_/preparedModelData_, and a still-ineligible
    // entity of this race could appear in ANY future zone). Consulted by
    // releaseRawTextureData() (model_loading.cpp) to keep these specific raw
    // TextureInfo bytes alive across the releaseTextureDataAfterUpload
    // memory-reclaim pass, so the on-demand load (buildMeshFromGeometry /
    // createAnimatedNodeWithEquipment's texture-swap, mesh_building.cpp /
    // animated_mesh_creation.cpp) has real bytes to decode from whenever an
    // ineligible entity of this race first actually needs one.
    std::set<std::string> deferredPerPartTextureNames_;

    // Search character archive for race model, build RaceModelData without modifying caches.
    // Pure computation helper for preloadModelData() (thread-safe reads only).
    std::shared_ptr<RaceModelData> buildModelDataFromCharacters(
        const std::vector<std::shared_ptr<CharacterModel>>& characters,
        const std::map<std::string, std::shared_ptr<TextureInfo>>& textures,
        uint16_t raceId, uint8_t gender) const;

    // Cache of loaded race model data. Was documented "main thread only — no
    // mutex needed" until T113: EntityPrepWorker's background thread reads
    // this directly (getRaceModelData() from prepVariantTextures(),
    // isModelDataCached() from processRequest()) while the main/loading
    // thread inserts/erases/clears it from many sites in model_loading.cpp
    // and race_model_loader.cpp. That invariant broke silently when the
    // worker started doing sustained concurrent work (T111) — unprotected
    // concurrent std::map mutation corrupts the map's internal tree, which
    // then either SIGSEGVs on a later find() walk (mid-play) or SIGABRTs
    // with "double free or corruption"/"corrupted size vs. prev_size" when
    // a destructor walks the same corrupted tree at shutdown (T110). See
    // refactor/T113_pi_midtour_segv.md Review. Now protected by
    // loadedModelsMutex_ at every access site (same pattern as
    // variantModelsMutex_/preparedDataMutex_/otherChrCacheMutex_ below).
    mutable std::mutex loadedModelsMutex_;
    std::map<uint32_t, std::shared_ptr<RaceModelData>> loadedModels_;

    // Cache of Irrlicht meshes (separate from model data for memory management)
    std::map<uint32_t, irr::scene::IMesh*> meshCache_;

    // Cache of animated meshes (non-composite / multi-buffer builds; also the fallback
    // destination when a composite-allowed build has no sheet available)
    std::map<uint32_t, EQAnimatedMesh*> animatedMeshCache_;

    // Cache of variant animated meshes (key includes head/body variant)
    std::map<uint64_t, EQAnimatedMesh*> variantAnimatedMeshCache_;

    // P08d: composite-mesh caches, kept SEPARATE from animatedMeshCache_/
    // variantAnimatedMeshCache_ so the same race/gender/variant can have both a composite
    // build (for eligible entities) and a multi-buffer build (for ineligible entities)
    // cached side by side without one clobbering the other. Same key schemes as their
    // non-composite counterparts (makeCacheKey / makeVariantCacheKey).
    std::map<uint32_t, EQAnimatedMesh*> compositeAnimatedMeshCache_;
    std::map<uint64_t, EQAnimatedMesh*> compositeVariantAnimatedMeshCache_;

    // P08d: composite sheet adoption counters (see getCompositeSheetHits/Misses above)
    size_t compositeSheetHits_ = 0;
    size_t compositeSheetMisses_ = 0;

    // Cache for variant-specific meshes (key includes head/body variant)
    // variantModels_ is written by worker thread (preloadVariantModel) and read by
    // render thread (getAnimatedMeshWithAppearance) — protected by variantModelsMutex_
    mutable std::mutex variantModelsMutex_;
    std::map<uint64_t, std::shared_ptr<RaceModelData>> variantModels_;

    // T340 (B052): see variantLoadTrace() above. Guarded by variantModelsMutex_.
    VariantLoadTrace variantLoadTrace_;
    std::map<uint64_t, irr::scene::IMesh*> variantMeshCache_;

    // Global character data loaded from global_chr.s3d
    std::vector<std::shared_ptr<CharacterModel>> globalCharacters_;
    std::map<std::string, std::shared_ptr<TextureInfo>> globalTextures_;
    // T504b Leg 1: atomic so the worker-side adopt (below) may read the flag
    // and, once it is set, the write-once globalCharacters_ vector.
    std::atomic<bool> globalModelsLoaded_{false};

    // Numbered global character data (global2-7_chr.s3d)
    std::map<int, std::vector<std::shared_ptr<CharacterModel>>> numberedGlobalCharacters_;
    std::map<int, std::map<std::string, std::shared_ptr<TextureInfo>>> numberedGlobalTextures_;
    // T500 Leg B: "the numbered globals' TEXTURES are loaded". The characters
    // are loaded per archive, on demand, and numberedGlobalCharacterArchives_
    // records which have been tried (present in the map = has characters;
    // in the set but not the map = parsed and had none).
    bool numberedGlobalsLoaded_ = false;
    std::set<int> numberedGlobalCharacterArchives_;

    // T452: on-demand armor textures from global17-23_amr.s3d. A NULL mapped
    // value is a cached MISS -- every reader of this map must skip it.
    std::map<std::string, std::shared_ptr<TextureInfo>> armorTextureCache_;

    // Zone-specific character data
    std::string currentZoneName_;
    std::vector<std::shared_ptr<CharacterModel>> zoneCharacters_;
    std::map<std::string, std::shared_ptr<TextureInfo>> zoneTextures_;
    bool zoneModelsLoaded_ = false;

    // Cached merged textures (performance optimization)
    mutable std::map<std::string, std::shared_ptr<TextureInfo>> cachedMergedTextures_;
    mutable bool mergedTexturesCacheValid_ = false;

    // Old models mode (classic models from global_chr.s3d only)
    bool useOldModels_ = true;  // Default to old models

    // Cache for other _chr.s3d files loaded during searchZoneChrFilesForModel
    // Key is lowercase filename (e.g., "crushbone_chr.s3d")
    // Written by worker thread (preloadVariantModel) and render thread
    // (loadModelFromGlobalChrWithVariantsForAnimation) — protected by otherChrCacheMutex_
    struct OtherChrCache {
        std::vector<std::shared_ptr<CharacterModel>> characters;
        std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    };
    mutable std::mutex otherChrCacheMutex_;
    std::map<std::string, OtherChrCache> otherChrCaches_;
    size_t maxChrCacheEntries_ = 0;          // 0 = unlimited
    std::list<std::string> chrCacheLruOrder_; // Front = most recently used

    // T91: the ONLY correct way to insert into otherChrCaches_. Stores the
    // entry, registers it in chrCacheLruOrder_, and evicts the
    // least-recently-loaded entry if over maxChrCacheEntries_ -- exactly
    // what tryLoadFromArchive's lambda (preloadModelData, race_model_loader.cpp)
    // already did inline. Before T91, five insertion sites in
    // variant_loading.cpp (preloadVariantModel's JSON-specified-S3D and
    // animation-source-S3D loads, both in the primary and global-fallback
    // branches) wrote `otherChrCaches_[key] = ...` directly, bypassing this
    // accounting entirely: those entries grew otherChrCaches_.size() without
    // ever entering chrCacheLruOrder_, so they could never be evicted, while
    // silently pushing otherChrCaches_.size() over maxChrCacheEntries_ and
    // forcing the *tracked* entries (the global_chr.s3d/global2-7_chr.s3d
    // base-model fallback chain preloadModelData legitimately needs) to be
    // evicted and reloaded from disk far more often than the configured cap
    // should allow -- see refactor/T91_entity_prepare_gate.md Review for the
    // measured thrash this caused. Caller must already hold
    // otherChrCacheMutex_ (every existing call site already does).
    //
    // The bypass outlived T91 at five more sites, closed since: T364 (B065)
    // took the four in loadModelFromCachedChr, T366 (B095) the one in
    // loadModelFromZoneChr's animation-source branch and the three in
    // searchZoneChrFilesForModel (two negative-cache inserts plus one that
    // open-coded the push/evict below). There is now exactly one
    // `otherChrCaches_[key]` in the tree and it is the line below.
    // T504b Leg 1: global_chr.s3d is parsed ONCE per process. Whichever side
    // parses it first publishes the same CharacterModel objects to the other
    // store: loadGlobalModels() adopts otherChrCaches_["global_chr.s3d"] if
    // the worker got there first and publishes its own parse there
    // otherwise; the worker-side sites call this (otherChrCacheMutex_ held)
    // before parsing and get true when the entry now exists. Measured before:
    // src_char_global == src_char_other byte-identical (two full parses).
    bool adoptGlobalChrIntoOtherCacheLocked();

    // T504b Leg 2: zoneCharacters_/globalCharacters_/numberedGlobalCharacters_
    // are read by the prep worker AND the render thread and, from Leg 2 on,
    // pruned (a writer). Every reader takes a pointer-vector snapshot under
    // sourceStoresMutex_ (a few dozen shared_ptr copies); writers assign under
    // it. Lock order: otherChrCacheMutex_ -> sourceStoresMutex_, never the
    // reverse (the snapshot helpers hold only sourceStoresMutex_).
    using CharacterList = std::vector<std::shared_ptr<CharacterModel>>;
    CharacterList snapshotZoneCharacters() const;
    CharacterList snapshotGlobalCharacters() const;
    CharacterList snapshotNumberedGlobalCharacters(int num) const;
    mutable std::mutex sourceStoresMutex_;

    // T504b Leg 2: source pruning (refactor/T504b_entity_source_pruning.md).
    // After a build, every store keeps the source CharacterModel of the USED
    // set only (skeleton identity of a built model, plus the animation
    // donors those skeletons share Animation objects with); the rest of the
    // archive is dropped. archiveIndex_ remembers every character NAME an
    // archive yielded, so a later request for a dropped character reloads
    // the archive, takes the wanted characters and drops the rest again
    // (ensureSourceForRace). Opt-in: WILLEQ_PRUNE_SOURCES=1 (see pruningEnabled()).
    struct ArchiveIndexEntry {
        std::vector<std::string> names;   // upper-case character names the archive yielded
        std::string path;                 // the .s3d to reload from
        bool pruned = false;
        size_t reloads = 0;
    };
    std::map<std::string, ArchiveIndexEntry> archiveIndex_;  // "zone" | "global" | "global<N>" | "other:<file>"; under sourceStoresMutex_
    // A (race, gender) is reloaded at most ONCE per process: the build that
    // follows a reload either uses the characters (then they are kept) or
    // fails (then no later request can succeed either). Without this a race
    // with no model thrashed: reload, fail, prune, reload... (measured 1,398
    // reloads for 380 requests in the fingerprint tool).
    // Keyed by (race, gender, SITE): the base build, the variant build and
    // the synchronous fallback need different sources (measured: the variant
    // merges donor animations the base build never touched, so a memo keyed
    // by race alone starved it -- STOP 1 hit, fingerprint anims shrank).
    std::set<std::string> reloadedRaceGenders_;   // under sourceStoresMutex_
    // The memo means "reloaded, not yet built". When a built model is
    // EVICTED (removeMeshRef at refcount 0, zone change) its sources may be
    // pruned again, so the next request must be allowed to reload: measured
    // without this, the player's appearance update evicted race 1, the
    // re-preload was memo-blocked, the negative cache took it, and the
    // entity looped on deferral (1,206 times in 60 s).
    void forgetReloadMemo(uint32_t key);      // one (race, gender), every site
    void forgetAllReloadMemos();
    std::thread::id renderThread_;                 // the constructing thread (IrrlichtRenderer::init, main)
    void evictModel(uint32_t key);                 // the built model + every cache keyed by it
    std::map<uint32_t, std::chrono::steady_clock::time_point> evictable_;   // key -> when its refcount hit 0
    size_t entityModelBudgetBytes_ = 0;            // 0 = no budget pressure
    double evictHysteresisSeconds_ = 60.0;
    std::function<void(uint16_t, uint8_t)> workerPreloadRequest_;
    void recordArchiveNamesLocked(const std::string& key, const std::string& path, const CharacterList& chars);
    void computeUsedSet(std::set<const CharacterSkeleton*>& usedSkeletons,
                        std::set<const Animation*>& usedAnimations) const;
    static bool pruningEnabled();
public:
    // T504b (Will, 2026-09-22): the render thread NEVER parses an archive.
    // ensureModelDataLoaded() on the render thread with the model neither
    // loaded nor prepared hands the (race, gender) to the prep worker through
    // this callback and returns null; lastLookupDeferred() tells the caller
    // not to cache that null (the model arrives when the worker's result is
    // promoted, and the entity's build step finishes it -- as a late spawn).
    void setWorkerPreloadRequest(std::function<void(uint16_t, uint8_t)> fn) { workerPreloadRequest_ = std::move(fn); }
    bool lastLookupDeferred() const;
    // True when a model is loaded (after promoting any prepared one): the
    // render-thread build may proceed without touching an archive.
    bool isModelReady(uint16_t raceId, uint8_t gender);
    // Drop every retained source character no built model uses (see above).
    // Safe from any thread; takes the store locks itself. `why` is logged.
    void pruneSourceCharacters(const char* why);
    // Reload-on-miss for one race: if a wanted code (race, fallback,
    // animation donor) names a character an archive is known to hold but a
    // pruned store no longer has, re-parse that archive for it.
    void ensureSourceForRace(uint16_t raceId, uint8_t gender, const char* site = "?");
private:
    void storeOtherChrCacheLocked(const std::string& lowerFilename,
                                   std::vector<std::shared_ptr<CharacterModel>> characters,
                                   std::map<std::string, std::shared_ptr<TextureInfo>> textures);

    // Reference counts for cached meshes (cache key -> ref count)
    std::map<uint32_t, int> meshRefCounts_;

    // Temporary storage for vertex data during animated mesh building
    // (populated by buildMeshFromGeometry/buildCompositeMeshFromGeometry,
    // consumed immediately after by getAnimatedMeshForRace() on the
    // main/render thread AND preloadVariantModel() on EntityPrepWorker's
    // background thread -- protected by animScratchMutex_, held across the
    // whole build-then-consume sequence at both call sites. T114: found via
    // a Pi core dump ("double free or corruption" SIGABRT inside
    // RaceModelLoader::~RaceModelLoader()'s vertexMappingForAnimation_
    // destruction) -- these two vectors had no synchronization at all
    // before this fix.
    std::vector<irr::video::S3DVertex> originalVerticesForAnimation_;
    std::vector<VertexMappingList> vertexMappingForAnimation_;  // one-to-many, see P08D-RETAKE note
    mutable std::mutex animScratchMutex_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_RACE_MODEL_LOADER_H
