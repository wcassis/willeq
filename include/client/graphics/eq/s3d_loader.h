#ifndef EQT_GRAPHICS_S3D_LOADER_H
#define EQT_GRAPHICS_S3D_LOADER_H

#include "pfs.h"
#include "wld_loader.h"
#include "placeable.h"
#include "client/graphics/texture_origin.h"
#include <cctype>
#include <string>
#include <memory>
#include <map>
#include <set>

namespace EQT {
namespace Graphics {

// Object instance with resolved geometry
struct ObjectInstance {
    std::shared_ptr<Placeable> placeable;
    std::shared_ptr<ZoneGeometry> geometry;
};

// Single texture frame data
struct TextureFrame {
    std::string name;
    std::vector<char> data;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Texture information from S3D (supports animated textures)
struct TextureInfo {
    std::string name;               // Primary texture name (first frame for animated)
    std::vector<char> data;         // Primary texture data (first frame for animated)
    uint32_t width = 0;
    uint32_t height = 0;

    // B843: the eqt::memory::Category that ALLOCATED the pixel data -- the
    // MemoryScope active when loadCharacterTextures() (and getArmorTexture())
    // filled `data`. A TextureInfo is shared by shared_ptr across every
    // map that holds it (globalTextures_, zoneTextures_, otherChrCaches_,
    // per-model maps), so its pixels have EXACTLY ONE ledger owner, and a
    // tally that adds them to every claimant would over-count.
    // kOwnerUnknown means no pixels are allocated yet; 0xFF is deliberately
    // outside the Category range, so an allocated pixel always carries its
    // real allocating scope (s3d_loader_textures.cpp stamps it).
    uint8_t ownerCategory = kOwnerUnknown;
    static constexpr uint8_t kOwnerUnknown = 0xFF;

    // Animation support
    bool isAnimated = false;                     // True if this is an animated texture
    int animationDelayMs = 0;                    // Milliseconds between frames
    std::vector<TextureFrame> frames;            // All frames (including first)

    // Helper to get frame count
    size_t frameCount() const { return isAnimated ? frames.size() : 1; }

    // T504 (load-step audit): "this texture exists" is NOT "its file bytes are
    // in memory". Readers used `!data.empty()` for both, which made it
    // impossible to free the bytes before every consumer had run -- so on the
    // EQE1 presets, where the bytes are never decoded at all (the cache serves
    // the name from a flat pack, and a miss is fatal), whole archives' worth of
    // raw BMP/DDS sat in the heap through the load peak. releaseBytes() frees
    // them and keeps the texture present; ask present(), not data.empty().
    bool bytesReleased = false;
    bool present() const { return bytesReleased || !data.empty(); }
    size_t releaseBytes() {
        const size_t freed = rawDataBytes();
        std::vector<char>().swap(data);
        for (auto& f : frames) std::vector<char>().swap(f.data);
        bytesReleased = true;
        return freed;
    }

    // Get total raw data bytes (for memory reporting)
    size_t rawDataBytes() const {
        size_t total = data.capacity();
        for (const auto& f : frames) total += f.data.capacity();
        return total;
    }

    // B947 (GitLab #265): the archive these bytes were read from (shared with
    // the PfsArchive that read them -- one pointer per texture, no path copy),
    // and the member name when it is not simply lower-case `name` (an
    // equipment texture found under its .bmp/.dds sibling). Null when the
    // bytes did not come from an archive. origin() is what a caller hands
    // ConstrainedTextureCache::getOrLoad() so an evicted texture can be
    // re-decoded from the same bytes; see texture_origin.h.
    std::shared_ptr<const std::string> sourceArchive;
    std::shared_ptr<const std::string> sourceMember;
    TextureOrigin origin() const {
        if (!sourceArchive) return {};
        return TextureOrigin::archiveMember(sourceArchive, sourceMember ? *sourceMember : lowerCased(name));
    }

private:
    static std::string lowerCased(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

// Character model part with bone transform
struct CharacterPart {
    std::shared_ptr<ZoneGeometry> geometry;
    // Accumulated bone transform (world space)
    float shiftX = 0.0f, shiftY = 0.0f, shiftZ = 0.0f;
    float rotateX = 0.0f, rotateY = 0.0f, rotateZ = 0.0f;
};

// Skeleton bone with animation data
struct AnimatedBone {
    std::string name;
    int parentIndex;                                    // -1 for root bones
    std::vector<int> childIndices;
    std::shared_ptr<TrackDef> poseTrack;               // Default pose keyframes
    std::map<std::string, std::shared_ptr<TrackDef>> animationTracks;  // Animations keyed by anim code
};

// Character skeleton with animations
struct CharacterSkeleton {
    std::string modelCode;                             // e.g., "huf", "elf"
    std::vector<AnimatedBone> bones;                   // Bones in hierarchy order
    std::map<std::string, std::shared_ptr<Animation>> animations;  // Animations keyed by code (e.g., "l01", "c01")

    // Get bone index by name (-1 if not found)
    int getBoneIndex(const std::string& name) const {
        for (size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].name == name) return static_cast<int>(i);
        }
        return -1;
    }
};

// Character model geometry (flattened from skeleton)
struct CharacterModel {
    std::string name;
    std::vector<std::shared_ptr<ZoneGeometry>> parts;  // Geometry parts from bone meshes (legacy, no transforms)
    std::vector<CharacterPart> partsWithTransforms;    // Parts with bone transforms applied (skinned)
    std::vector<CharacterPart> rawParts;               // Parts WITHOUT transforms (for animation)
    std::shared_ptr<SkeletonTrack> skeleton;           // Original skeleton data
    std::shared_ptr<CharacterSkeleton> animatedSkeleton;  // Skeleton with animation data
};

// Complete S3D zone data
struct S3DZone {
    std::shared_ptr<ZoneGeometry> geometry;
    std::map<std::string, std::shared_ptr<TextureInfo>> textures;
    std::string zoneName;

    // WLD loader - provides access to BSP tree and per-region geometry for PVS culling
    std::shared_ptr<WldLoader> wldLoader;

    // Placeable objects
    std::vector<ObjectInstance> objects;

    // All object geometries from _obj.s3d (keyed by uppercase name, e.g., "DOOR1")
    // This includes objects not placed in the zone (like doors which are placed dynamically)
    // B734: skinned geometries (vertex pieces on a 0x10 skeleton, flag 0x200) are
    // stored at their REST POSE, keyed by the ACTOR name the placement uses
    // (e.g. "TDTORCH301"), with the MESH name ("STOR301") aliased to the same
    // shared_ptr — one copy, per the 100 %-memory rule.
    std::map<std::string, std::shared_ptr<ZoneGeometry>> objectGeometries;

    // Object textures loaded from _obj.s3d
    std::map<std::string, std::shared_ptr<TextureInfo>> objectTextures;

    // Character models loaded from _chr.s3d
    std::vector<std::shared_ptr<CharacterModel>> characters;
    std::vector<std::string> allCharacterNames;   // T504b Leg 3: every skeleton name, filtered or not

    // Character textures loaded from _chr.s3d
    std::map<std::string, std::shared_ptr<TextureInfo>> characterTextures;

    // Light sources loaded from lights.wld
    std::vector<std::shared_ptr<ZoneLight>> lights;

    // CPU-side memory usage of all zone source data
    size_t getMemoryUsage() const {
        size_t total = sizeof(S3DZone);

        // Combined zone geometry (standalone, not shared via WldLoader)
        if (geometry) total += geometry->getFullMemoryUsage();

        // Zone texture raw pixel data (may be released after GPU upload)
        for (const auto& [name, tex] : textures)
            if (tex) total += tex->rawDataBytes() + sizeof(TextureInfo);

        // WLD loader data (BSP tree, per-region geometries, placeables, tracks, etc.)
        if (wldLoader) total += wldLoader->getMemoryUsage();

        // Placeable objects (ObjectInstance = shared_ptr pair, geometry may be shared)
        total += objects.capacity() * sizeof(ObjectInstance);

        // Object geometries from _obj.s3d (standalone, not shared via WldLoader).
        // B734: a skinned geometry is stored under the ACTOR name with the mesh
        // name aliased to the SAME shared_ptr — account each distinct geometry
        // ONCE (the 100 %-accounting rule), not once per key.
        {
            std::set<const ZoneGeometry*> counted;
            for (const auto& [name, geom] : objectGeometries) {
                if (!geom) continue;
                if (counted.insert(geom.get()).second) total += geom->getFullMemoryUsage();
            }
        }

        // Object textures
        for (const auto& [name, tex] : objectTextures)
            if (tex) total += tex->rawDataBytes() + sizeof(TextureInfo);

        // Character models
        for (const auto& model : characters) {
            if (!model) continue;
            total += sizeof(CharacterModel);
            for (const auto& part : model->parts)
                if (part) total += part->getFullMemoryUsage();
            total += model->partsWithTransforms.capacity() * sizeof(CharacterPart);
            total += model->rawParts.capacity() * sizeof(CharacterPart);
            // Skeleton and animation data accounted via CharacterSkeleton below
            if (model->animatedSkeleton) {
                total += sizeof(CharacterSkeleton);
                for (const auto& bone : model->animatedSkeleton->bones) {
                    total += sizeof(AnimatedBone) + bone.name.capacity();
                    total += bone.childIndices.capacity() * sizeof(int);
                    if (bone.poseTrack)
                        total += bone.poseTrack->frames.capacity() * sizeof(BoneTransform);
                    for (const auto& [code, track] : bone.animationTracks)
                        if (track) total += track->frames.capacity() * sizeof(BoneTransform);
                }
                for (const auto& [code, anim] : model->animatedSkeleton->animations)
                    if (anim) total += sizeof(Animation) + anim->tracks.size() * 64;
            }
        }

        // Character textures
        for (const auto& [name, tex] : characterTextures)
            if (tex) total += tex->rawDataBytes() + sizeof(TextureInfo);

        // Lights
        for (const auto& light : lights) {
            if (!light) continue;
            total += sizeof(ZoneLight);
            total += light->lightLevels.capacity() * sizeof(float);
            total += light->colors.capacity() * sizeof(std::tuple<float,float,float>);
        }

        return total;
    }

    // Release character model data (geometry, skeletons, textures).
    // The renderer's RaceModelLoader independently loads _chr.s3d into its own cache,
    // so zone->characters/characterTextures are unused duplicates after zone loading.
    void clearCharacterData() {
        characters.clear();
        characters.shrink_to_fit();
        characterTextures.clear();
    }

    // Release raw pixel data from all texture infos (zone, object, character).
    // Call after all Irrlicht textures and animated texture frames have been uploaded.
    // Returns the number of bytes freed.
    size_t releaseTexturePixelData() {
        size_t freed = 0;
        auto releaseMap = [&freed](std::map<std::string, std::shared_ptr<TextureInfo>>& texMap) {
            for (auto& [name, texInfo] : texMap) {
                if (!texInfo) continue;
                freed += texInfo->data.capacity();
                texInfo->data.clear();
                texInfo->data.shrink_to_fit();
                for (auto& frame : texInfo->frames) {
                    freed += frame.data.capacity();
                    frame.data.clear();
                    frame.data.shrink_to_fit();
                }
            }
        };
        releaseMap(textures);
        releaseMap(objectTextures);
        releaseMap(characterTextures);
        return freed;
    }

    // B914: the zone-SOURCE release that ends a zone load -- the client's
    // Step 13 (ZoneLoadComponent::loadZoneStep13_Cleanup) and the gate's
    // benchmark (tools/hwcaps/zone_bench_loader.cpp) both call THIS, so the
    // gate measures the state the client actually holds after a load. In
    // order: raw texture pixels (only when the preset sets
    // releaseTextureDataAfterUpload), the zone's character models, the WLD
    // loader's post-load tables (releasePostLoadData) and its per-region
    // source geometry (releaseRegionGeometry, T504). Call it only once every
    // region mesh is built: getGeometryForRegion() returns null afterwards.
    struct PostLoadRelease {
        bool texturePixelsReleased = false;
        size_t texturePixelBytes = 0;     // releaseTexturePixelData()
        bool wldReleased = false;         // false when there is no wldLoader
        size_t regionGeometryBytes = 0;   // WldLoader::releaseRegionGeometry()
    };
    PostLoadRelease releaseSourceAfterLoad(bool releaseTexturePixels);
};

// Options for selective zone loading
struct S3DLoadOptions {
    bool loadObjects = true;
    bool loadCharacters = true;
    bool computeCombinedGeometry = true;
    // T504c Leg 2c: record character textures by NAME only (TextureInfo
    // present() but bytesReleased) instead of inflating every BMP/DDS of the
    // archive. Set by RaceModelLoader when an EQE1 pack is authoritative:
    // the bytes were inflated (global_chr: 6.7 MiB, 3,233 files) and then
    // released unread by releaseBytesIfEqe1Authoritative() on every load.
    bool textureNamesOnly = false;
    // T504b Leg 3: build a CharacterModel (three geometry copies + two
    // skeletons each) only for skeletons whose name contains one of these
    // UPPER-CASE codes; empty = every character. S3DZone::allCharacterNames
    // still lists every skeleton the archive holds, so the caller's archive
    // index knows what a later request could reload.
    std::vector<std::string> characterCodeFilter;
};

// High-level S3D zone loader
class S3DLoader {
public:
    S3DLoader() = default;
    ~S3DLoader() = default;

    // Load a zone from an S3D file
    // archivePath: path to the .s3d file
    // Returns true on success
    bool loadZone(const std::string& archivePath);
    bool loadZone(const std::string& archivePath, const S3DLoadOptions& options);

    // Load a zone using pre-opened archive and pre-parsed WLD (from game state layer)
    // The archive and WLD were already opened/parsed during ZoneProcessNewZone().
    // This avoids duplicate file I/O and WLD parsing.
    bool loadZoneFromPreParsed(std::shared_ptr<PfsArchive> archive,
                                std::shared_ptr<WldLoader> wldLoader,
                                const std::string& zoneName,
                                const std::string& archivePath,
                                const S3DLoadOptions& options);

    // Get the loaded zone data
    std::shared_ptr<S3DZone> getZone() const { return zone_; }

    // Get the zone geometry directly
    std::shared_ptr<ZoneGeometry> getGeometry() const {
        return zone_ ? zone_->geometry : nullptr;
    }

    // Get zone name extracted from filename
    const std::string& getZoneName() const { return zoneName_; }

    // Get error message if loading failed
    const std::string& getError() const { return error_; }

    // Get object instances
    const std::vector<ObjectInstance>& getObjects() const {
        return zone_ ? zone_->objects : emptyObjects_;
    }

    // Get object count
    size_t getObjectCount() const {
        return zone_ ? zone_->objects.size() : 0;
    }

    // Get character models
    const std::vector<std::shared_ptr<CharacterModel>>& getCharacters() const {
        return zone_ ? zone_->characters : emptyCharacters_;
    }

    // Get character count
    size_t getCharacterCount() const {
        return zone_ ? zone_->characters.size() : 0;
    }

    // Get light sources
    const std::vector<std::shared_ptr<ZoneLight>>& getLights() const {
        return zone_ ? zone_->lights : emptyLights_;
    }

    // Get light count
    size_t getLightCount() const {
        return zone_ ? zone_->lights.size() : 0;
    }

    // Set zone from an external source (for deferred object loading on a separate thread)
    void setZone(std::shared_ptr<S3DZone> zone) { zone_ = zone; zoneName_ = zone->zoneName; }

    // Load placeable objects from _obj.s3d (public for deferred loading)
    bool loadObjects(const std::string& archivePath);
    // T504: same, from the already-open zone archive (objects.wld lives there).
    bool loadObjectsFromArchive(PfsArchive& mainArchive, const std::string& archivePath);

    // T369 (B105): the character archive that pairs with a zone archive.
    // The `_chr` marker belongs to the FILENAME's stem, never to the path.
    // The old derivation searched the WHOLE path for the first "_chr" and
    // truncated there, so a client directory whose name contained `_chr`
    // collapsed every archive to one bogus path (measured T366:
    // /tmp/willeq_chrscan_<pid>/arena_chr.s3d -> /tmp/willeq_chr.s3d) and
    // every character load silently returned zero. Public and static so the
    // derivation is testable on its own.
    //
    // `<dir>/<stem>.s3d` -> `<dir>/<stem-without-trailing-_chr[N]>_chr.s3d`.
    // The trailing-digits allowance keeps the `<zone>_chr2.s3d` archives
    // (acrylia, bothunder, cazicthule, ...) resolving to `<zone>_chr.s3d`
    // exactly as before.
    static std::string deriveChrArchivePath(const std::string& archivePath);

private:
    bool loadTextures(PfsArchive& archive);
    bool loadObjectTextures(PfsArchive& archive);
    bool loadCharacterTextures(PfsArchive& archive);
    // T504c Leg 2b: `openArchive` is loadZone()'s already-open archive; it
    // is reused when the archive being loaded IS the character archive
    // (every <zone>_chr / global*_chr load), instead of a second open.
    bool loadCharacters(const std::string& archivePath, PfsArchive* openArchive = nullptr);
    S3DLoadOptions options_;   // the options of the load in progress (T504b Leg 3: characterCodeFilter)
    std::string mainWldName_;  // the WLD loadZone() parsed into zone_->wldLoader (T504c: loadCharacters reuses it)
    bool loadLights(const std::string& archivePath);
    bool loadLightsFromArchive(PfsArchive& archive);
    void flattenSkeleton(const std::shared_ptr<SkeletonBone>& bone,
                         std::shared_ptr<CharacterModel>& model,
                         WldLoader& wldLoader,
                         float parentShiftX = 0.0f, float parentShiftY = 0.0f, float parentShiftZ = 0.0f,
                         float parentRotX = 0.0f, float parentRotY = 0.0f, float parentRotZ = 0.0f);

    // Apply skeleton bone transforms to mesh vertices based on vertex pieces
    void applySkinning(std::shared_ptr<ZoneGeometry>& mesh,
                       const std::shared_ptr<SkeletonTrack>& skeleton);

    // Build animated skeleton from WLD track data
    std::shared_ptr<CharacterSkeleton> buildAnimatedSkeleton(
        const std::string& modelCode,
        const std::shared_ptr<SkeletonTrack>& skeleton,
        WldLoader& wldLoader);

    std::string extractZoneName(const std::string& path);

    static const std::vector<ObjectInstance> emptyObjects_;
    static const std::vector<std::shared_ptr<CharacterModel>> emptyCharacters_;
    static const std::vector<std::shared_ptr<ZoneLight>> emptyLights_;

    std::shared_ptr<S3DZone> zone_;
    std::string zoneName_;
    std::string error_;
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_S3D_LOADER_H
