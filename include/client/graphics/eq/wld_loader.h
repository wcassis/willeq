#ifndef EQT_GRAPHICS_WLD_LOADER_H
#define EQT_GRAPHICS_WLD_LOADER_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include "placeable.h"
#include "client/zone_bsp.h"  // D20f1: BSP structs shared with game state

namespace EQT {
namespace Graphics {

// WLD file structures (packed)
#pragma pack(push, 1)

struct WldHeader {
    uint32_t magic;           // 0x54503D02 for WLD files
    uint32_t version;         // 0x00015500 = old format, 0x1000C800 = new format
    uint32_t fragmentCount;   // Number of fragments in the file
    uint32_t bspRegionCount;  // Number of BSP regions (was unk1)
    uint32_t unk2;            // Unknown, skipped
    uint32_t hashLength;      // Size of encoded string hash table
    uint32_t unk3;            // Unknown, skipped
};

struct WldFragmentHeader {
    uint32_t size;
    uint32_t id;
    // NOTE: nameRef is NOT part of the header - it's the first field in the fragment data
};

struct WldFragment03Header {
    uint32_t textureCount;
};

struct WldFragment04Header {
    uint32_t flags;
    uint32_t textureCount;
};

// Fragment 0x30 - Material Definition
// Matches eqsage: sage/lib/s3d/materials/material.js
struct WldFragment30Header {
    uint32_t flags;           // Usually 0x02 in practice
    uint32_t parameters;      // Contains MaterialType (mask with ~0x80000000)
    uint8_t colorR, colorG, colorB, colorA;  // Color tint RGBA
    float brightness;
    float scaledAmbient;
    int32_t bitmapInfoRef;    // Reference to Fragment 0x05 (1-indexed, 0 = none)
};

struct WldFragment31Header {
    uint32_t unk;
    uint32_t count;
};

struct WldFragmentRef {
    int32_t id;
};

struct WldFragment36Header {
    uint32_t flags;
    uint32_t frag1;
    uint32_t frag2;
    uint32_t frag3;
    uint32_t frag4;
    float centerX;
    float centerY;
    float centerZ;
    uint32_t params2[3];
    float maxDist;
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
    uint16_t vertexCount;
    uint16_t texCoordCount;
    uint16_t normalCount;
    uint16_t colorCount;
    uint16_t polygonCount;
    uint16_t size6;
    uint16_t polygonTexCount;
    uint16_t vertexTexCount;
    uint16_t size9;
    int16_t scale;
};

struct WldVertex {
    int16_t x;
    int16_t y;
    int16_t z;
};

struct WldTexCoordOld {
    int16_t u;
    int16_t v;
};

struct WldTexCoordNew {
    float u;
    float v;
};

struct WldNormal {
    int8_t x;   // Signed: range -128 to 127, divide by 128.0 for [-1, 1]
    int8_t y;
    int8_t z;
};

struct WldPolygon {
    uint16_t flags;
    uint16_t index[3];
};

struct WldTexMap {
    uint16_t polyCount;
    uint16_t tex;
};

// Fragment 0x14 - Object definition (actor)
struct WldFragment14Header {
    uint32_t flags;
    int32_t ref;
    uint32_t entries;
    uint32_t entries2;
    int32_t ref2;
};

// Fragment 0x15 - Placeable object instance (ActorInstance)
// This fragment uses flag-based parsing - fields are only present when their flag is set
// The struct below is NOT used for direct casting - use flag-based parsing instead
namespace Fragment15Flags {
    constexpr uint32_t HasCurrentAction = 0x01;
    constexpr uint32_t HasLocation = 0x02;
    constexpr uint32_t HasBoundingRadius = 0x04;
    constexpr uint32_t HasScaleFactor = 0x08;
    constexpr uint32_t HasSound = 0x10;
    constexpr uint32_t Active = 0x20;
    constexpr uint32_t SpriteVolumeOnly = 0x80;
    constexpr uint32_t HasVertexColorReference = 0x100;
}

// NOTE: This struct is DEPRECATED - do not use for direct casting
// Fragment 0x15 is variable-length based on flags
struct WldFragment15Header {
    uint32_t flags;
    int32_t refId;
    float x, y, z;
    float rotateZ, rotateY, rotateX;
    float unk;  // Unknown - often 0
    float scaleY, scaleX;  // Only 2 scale values in format
};

// Fragment 0x2C - Legacy Mesh (uncompressed float storage)
// Found in older character models like global_chr.s3d
struct WldFragment2CHeader {
    uint32_t flags;
    uint32_t vertexCount;
    uint32_t texCoordCount;
    uint32_t normalCount;
    uint32_t colorCount;
    uint32_t polygonCount;
    uint16_t vertexPieceCount;
    uint16_t polygonTexCount;
    uint16_t vertexTexCount;
    uint16_t size9;
    float scale;  // Usually 1.0 for legacy meshes
    float centerX;
    float centerY;
    float centerZ;
    float params[3];  // Unknown
    float maxDist;
    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
};

// Fragment 0x2D - Model reference
struct WldFragment2DHeader {
    int32_t ref;
};

// Fragment 0x10 - Skeleton track
struct WldFragment10Header {
    uint32_t flags;
    uint32_t trackRefCount;
    uint32_t polygonAnimFrag;
};

struct WldFragment10BoneEntry {
    int32_t nameRef;
    uint32_t flags;
    int32_t orientationRef;
    int32_t modelRef;
    uint32_t childCount;
};

// Fragment 0x11 - Skeleton track reference
struct WldFragment11Header {
    int32_t ref;
};

// Fragment 0x12 - Bone orientation
struct WldFragment12Header {
    uint32_t flags;
    uint32_t size;
    int16_t rotDenom;
    int16_t rotXNum;
    int16_t rotYNum;
    int16_t rotZNum;
    int16_t shiftXNum;
    int16_t shiftYNum;
    int16_t shiftZNum;
    int16_t shiftDenom;
};

// Fragment 0x13 - Bone orientation reference
struct WldFragment13Header {
    int32_t ref;
    uint32_t flags;
};

// Fragment 0x1B - Light source definition
// Matches eqsage: sage/lib/s3d/lights/light.js LightSource
// Variable-length structure with conditional fields based on flags
struct WldFragment1BHeader {
    uint32_t flags;       // LightFlags: 0x01=HasCurrentFrame, 0x02=HasSleep, 0x04=HasLightLevels, 0x08=SkipFrames, 0x10=HasColor
    uint32_t frameCount;  // Number of animation frames
    // Followed by conditional fields:
    // [if flags & 0x01] uint32_t currentFrame
    // [if flags & 0x02] uint32_t sleep
    // [if flags & 0x04] float lightLevels[frameCount]
    // [if flags & 0x10] float colors[frameCount * 3] (RGB for each frame)
};

// Fragment 0x1B flag constants (matches eqsage LightFlags)
static constexpr uint32_t LIGHT_FLAG_HAS_CURRENT_FRAME = 0x01;
static constexpr uint32_t LIGHT_FLAG_HAS_SLEEP = 0x02;
static constexpr uint32_t LIGHT_FLAG_HAS_LIGHT_LEVELS = 0x04;
static constexpr uint32_t LIGHT_FLAG_SKIP_FRAMES = 0x08;
static constexpr uint32_t LIGHT_FLAG_HAS_COLOR = 0x10;

// Fragment 0x28 - Light source instance
struct WldFragment28Header {
    uint32_t flags;
    float x, y, z;
    float radius;
};

// Fragment 0x2A - Ambient Light Region
// Matches eqsage: sage/lib/s3d/lights/light.js AmbientLight
struct WldFragment2AHeader {
    uint32_t flags;
    uint32_t regionCount;
    // Followed by: int32_t regionRefs[regionCount]
};

// Fragment 0x35 - Global Ambient Light
// Matches eqsage: sage/lib/s3d/lights/light.js GlobalAmbientLight
struct WldFragment35Header {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
};

// Fragment 0x37 - Mesh Animated Vertices (DMTRACKDEF)
// Contains frames of vertex positions for vertex animation
struct WldFragment37Header {
    int32_t nameRef;
    uint32_t flags;
    uint16_t vertexCount;
    uint16_t frameCount;
    uint16_t delayMs;      // Milliseconds between frames
    uint16_t param2;       // Unknown
    int16_t scale;         // Stored as power of 2
};

// Fragment 0x2F - Mesh Animated Vertices Reference
struct WldFragment2FHeader {
    int32_t nameRef;
    int32_t meshAnimVertRef;  // Reference to 0x37 fragment
    uint32_t flags;
};

#pragma pack(pop)

// Texture information from WLD
struct WldTexture {
    std::vector<std::string> frames;
};

// Texture brush (Fragment 0x04)
struct WldTextureBrush {
    std::vector<uint32_t> textureRefs;
    uint32_t flags = 0;
    bool isAnimated = false;           // True if this is an animated texture
    int animationDelayMs = 0;          // Milliseconds between frames (if animated)
};

// Texture brush set
struct WldTextureBrushSet {
    std::vector<uint32_t> brushRefs;
};

// Object definition
struct WldObjectDef {
    std::string name;
    std::vector<uint32_t> meshRefs;
};

// Model reference
struct WldModelRef {
    uint32_t geometryFragRef;
};

// Single keyframe transform for a bone
struct BoneTransform {
    // Rotation as quaternion (x, y, z, w) - normalized
    float quatX, quatY, quatZ, quatW;
    // Translation (divided by 256 from raw values)
    float shiftX, shiftY, shiftZ;
    // Scale factor (divided by 256 from raw value)
    float scale;
};

// Bone orientation data (stored as quaternion rotation) - alias for single-frame compatibility
using BoneOrientation = BoneTransform;

// Animation track definition (Fragment 0x12) - contains keyframe data for one bone
struct TrackDef {
    std::string name;
    std::vector<BoneTransform> frames;
    uint32_t fragIndex;
};

// Animation track reference (Fragment 0x13) - metadata about a track
struct TrackRef {
    std::string name;
    uint32_t trackDefRef;      // Reference to TrackDef fragment
    int frameMs;               // Milliseconds per frame (0 = use animation default)
    bool isPoseAnimation;      // True if this is the default pose

    // Parsed from track name (e.g., "C01HUFLARM" -> animCode="c01", modelCode="huf", boneName="larm")
    std::string animCode;      // Animation code (e.g., "c01", "l01", "p01")
    std::string modelCode;     // Model code (e.g., "huf", "elf", "dwf")
    std::string boneName;      // Bone/piece name (e.g., "root", "head", "larm")
    bool isNameParsed;
};

// Complete animation with all bone tracks
struct Animation {
    std::string name;          // Animation code (e.g., "c01", "l01")
    std::string modelCode;     // Model this animation belongs to
    std::map<std::string, std::shared_ptr<TrackRef>> tracks;  // Tracks keyed by bone name
    int frameCount;            // Maximum frames across all tracks
    int animationTimeMs;       // Total animation duration
    bool isLooped;             // Whether animation should loop

    Animation() : frameCount(0), animationTimeMs(0), isLooped(false) {}
};

// Skeleton bone structure
struct SkeletonBone {
    std::string name;
    std::shared_ptr<BoneOrientation> orientation;
    uint32_t modelRef;
    std::vector<std::shared_ptr<SkeletonBone>> children;
};

// Skeleton track
struct SkeletonTrack {
    std::string name;
    std::vector<std::shared_ptr<SkeletonBone>> bones;  // Root bones only
    std::vector<std::shared_ptr<SkeletonBone>> allBones;  // All bones in original file order
    std::vector<int> parentIndices;  // Parent index for each bone (-1 for roots)
};

// Light source data (Fragment 0x1B definition + 0x28 placement)
struct ZoneLight {
    std::string name;
    float x, y, z;           // Position from Fragment 0x28
    float r, g, b;           // Color (first frame if animated)
    float radius;            // Radius from Fragment 0x28

    // Animation data from Fragment 0x1B (optional)
    uint32_t flags = 0;
    uint32_t frameCount = 1;
    uint32_t currentFrame = 0;
    uint32_t sleepMs = 0;
    std::vector<float> lightLevels;              // frameCount elements
    std::vector<std::tuple<float,float,float>> colors;  // frameCount RGB tuples

    bool isAnimated() const { return frameCount > 1; }
};

// Ambient light region (Fragment 0x2A)
struct AmbientLightRegion {
    std::string name;
    uint32_t flags = 0;
    std::vector<int32_t> regionRefs;  // References to BSP regions
};

// Global ambient light (Fragment 0x35)
struct GlobalAmbientLight {
    float r, g, b, a;  // RGBA normalized to 0.0-1.0
};

// Geometry data structures
struct Vertex3D {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

struct Triangle {
    uint32_t v1, v2, v3;
    uint32_t textureIndex;
    uint32_t flags;
};

// Vertex piece for skinned meshes - maps vertex range to bone
struct VertexPiece {
    uint16_t count;      // Number of vertices in this piece
    uint16_t boneIndex;  // Bone index for these vertices
};

// Texture animation info for a single texture slot
struct TextureAnimationInfo {
    bool isAnimated = false;           // True if this texture is animated
    int animationDelayMs = 0;          // Milliseconds between frames
    std::vector<std::string> frames;   // All frame texture names
};

// Single frame of vertex positions for vertex animation
struct VertexAnimFrame {
    std::vector<float> positions;  // x, y, z for each vertex (size = vertexCount * 3)
};

// Mesh animated vertices data (from Fragment 0x37)
struct MeshAnimatedVertices {
    std::string name;
    uint32_t fragIndex;
    int delayMs;                              // Milliseconds between frames
    std::vector<VertexAnimFrame> frames;      // All animation frames
};

// Resolved material list — shared between geometries that reference the same 0x31 fragment.
// In qeynos2, all 1915 mesh fragments share one material list, saving ~33 MB.
struct ResolvedMaterialList {
    std::vector<std::string> textureNames;
    std::vector<bool> textureInvisible;
    std::vector<TextureAnimationInfo> textureAnimations;

    size_t getMemoryUsage() const {
        size_t total = sizeof(ResolvedMaterialList);
        for (const auto& n : textureNames) total += n.capacity();
        total += textureNames.capacity() * sizeof(std::string);
        total += textureInvisible.capacity() / 8;  // vector<bool>
        for (const auto& a : textureAnimations) {
            total += sizeof(TextureAnimationInfo);
            for (const auto& f : a.frames) total += f.capacity();
            total += a.frames.capacity() * sizeof(std::string);
        }
        total += textureAnimations.capacity() * sizeof(TextureAnimationInfo);
        return total;
    }
};

struct ZoneGeometry {
    std::vector<Vertex3D> vertices;
    std::vector<Triangle> triangles;
    std::string name;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    // Mesh center point (vertices are stored relative to this)
    float centerX = 0, centerY = 0, centerZ = 0;

    // Texture mapping data — shared between geometries using same material list
    std::shared_ptr<ResolvedMaterialList> materialData;

    // Const accessors (return empty defaults if materialData is null)
    const std::vector<std::string>& textureNames() const {
        static const std::vector<std::string> empty;
        return materialData ? materialData->textureNames : empty;
    }
    const std::vector<bool>& textureInvisible() const {
        static const std::vector<bool> empty;
        return materialData ? materialData->textureInvisible : empty;
    }
    const std::vector<TextureAnimationInfo>& textureAnimations() const {
        static const std::vector<TextureAnimationInfo> empty;
        return materialData ? materialData->textureAnimations : empty;
    }

    // Ensure materialData exists for write access, returns reference
    ResolvedMaterialList& mutableMaterialData() {
        if (!materialData) materialData = std::make_shared<ResolvedMaterialList>();
        return *materialData;
    }

    // For character models - vertex to bone mapping
    std::vector<VertexPiece> vertexPieces;
    // Vertex animation data (for flags, banners, etc.)
    std::shared_ptr<MeshAnimatedVertices> animatedVertices;

    // Memory usage estimate (CPU-side source data, excluding shared materialData)
    size_t getMemoryUsage() const {
        size_t total = sizeof(ZoneGeometry);
        total += vertices.capacity() * sizeof(Vertex3D);
        total += triangles.capacity() * sizeof(Triangle);
        // materialData counted separately to handle sharing (see WldLoader::getMemoryUsage)
        total += sizeof(std::shared_ptr<ResolvedMaterialList>);
        total += vertexPieces.capacity() * sizeof(VertexPiece);
        if (animatedVertices) {
            total += sizeof(MeshAnimatedVertices);
            for (const auto& f : animatedVertices->frames)
                total += f.positions.capacity() * sizeof(float);
            total += animatedVertices->frames.capacity() * sizeof(VertexAnimFrame);
        }
        return total;
    }

    // Full memory usage including materialData (for standalone geometries not in WldLoader)
    size_t getFullMemoryUsage() const {
        size_t total = getMemoryUsage();
        if (materialData) total += materialData->getMemoryUsage();
        return total;
    }
};

// WLD Loader class
class WldLoader {
public:
    WldLoader() = default;
    ~WldLoader() = default;

    bool parseFromArchive(const std::string& archivePath, const std::string& wldName);

    const std::vector<std::shared_ptr<ZoneGeometry>>& getGeometries() const { return geometries_; }
    std::shared_ptr<ZoneGeometry> getCombinedGeometry() const;
    const std::vector<std::string>& getTextureNames() const { return textureNames_; }
    const std::vector<std::shared_ptr<Placeable>>& getPlaceables() const { return placeables_; }
    const std::map<std::string, WldObjectDef>& getObjectDefs() const { return objectDefs_; }
    const std::map<uint32_t, WldModelRef>& getModelRefs() const { return modelRefs_; }
    const std::map<uint32_t, std::shared_ptr<SkeletonTrack>>& getSkeletonTracks() const { return skeletonTracks_; }
    const std::map<uint32_t, std::shared_ptr<BoneOrientation>>& getBoneOrientations() const { return boneOrientations_; }
    bool hasCharacterData() const { return !skeletonTracks_.empty(); }
    const std::vector<std::shared_ptr<ZoneLight>>& getLights() const { return lights_; }
    const std::vector<std::shared_ptr<AmbientLightRegion>>& getAmbientLightRegions() const { return ambientLightRegions_; }
    const std::shared_ptr<GlobalAmbientLight>& getGlobalAmbientLight() const { return globalAmbientLight_; }
    bool hasGlobalAmbientLight() const { return globalAmbientLight_ != nullptr; }

    // BSP tree accessor (for zone line detection)
    const std::shared_ptr<BspTree>& getBspTree() const { return bspTree_; }
    bool hasZoneLines() const { return bspTree_ && !bspTree_->regions.empty(); }

    // Memory usage estimate (all CPU-side data in this loader)
    size_t getMemoryUsage() const;

    // Log per-component memory breakdown (called from /pmem)
    void logMemoryBreakdown() const;

    // Release data no longer needed after BSP install and region mesh initialization.
    // Preserves geometries_, geometryByFragIndex_, bspTree_, textures_, textureNames_
    // (needed by mesh cache for progressive region rebuilds).
    void releasePostLoadData();

    // PVS (Potentially Visible Set) accessors
    // Get the geometry associated with a BSP region (via meshReference)
    std::shared_ptr<ZoneGeometry> getGeometryForRegion(size_t regionIndex) const;
    // Check if zone has usable PVS data (at least one region with visibility info)
    bool hasPvsData() const;
    // Get total region count from WLD header
    uint32_t getTotalRegionCount() const { return totalRegionCount_; }

    // Animation data accessors
    const std::map<uint32_t, std::shared_ptr<TrackDef>>& getTrackDefs() const { return trackDefs_; }
    const std::map<uint32_t, std::shared_ptr<TrackRef>>& getTrackRefs() const { return trackRefs_; }

    // Get track definition by fragment index
    std::shared_ptr<TrackDef> getTrackDef(uint32_t fragIndex) const {
        auto it = trackDefs_.find(fragIndex);
        return (it != trackDefs_.end()) ? it->second : nullptr;
    }

    // Get track reference by fragment index
    std::shared_ptr<TrackRef> getTrackRef(uint32_t fragIndex) const {
        auto it = trackRefs_.find(fragIndex);
        return (it != trackRefs_.end()) ? it->second : nullptr;
    }

    // Get geometry by fragment index (for character bone model lookups)
    std::shared_ptr<ZoneGeometry> getGeometryByFragmentIndex(uint32_t fragIndex) const {
        auto it = geometryByFragIndex_.find(fragIndex);
        return (it != geometryByFragIndex_.end()) ? it->second : nullptr;
    }

private:
    bool parseWldBuffer(const std::vector<char>& buffer);

    void parseFragment03(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         const char* hash, bool oldFormat);
    void parseFragment04(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         const char* hash, bool oldFormat);
    void parseFragment05(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment30(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         const char* hash, bool oldFormat);
    void parseFragment31(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment36(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment14(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment15(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment2C(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment2D(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment10(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat,
                         const std::vector<std::pair<size_t, WldFragmentHeader>>& fragments,
                         const std::vector<char>& buffer);
    void parseFragment11(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment12(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);
    void parseFragment13(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);
    void parseFragment1B(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment1C(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment28(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment2A(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);
    void parseFragment35(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment2F(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment37(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);
    void parseFragment21(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment22(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment29(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);

    // Decode zone line info from region type string (drntp, wtntp, lantp patterns)
    static std::optional<ZoneLineInfo> decodeZoneLineString(const std::string& regionTypeString);

    std::string resolveTextureName(uint32_t texIndex) const;

    std::vector<std::shared_ptr<ZoneGeometry>> geometries_;
    std::map<uint32_t, WldTexture> textures_;
    std::map<uint32_t, WldTextureBrush> brushes_;
    std::map<uint32_t, uint32_t> textureRefs_;
    std::map<uint32_t, WldTextureBrush> materials_;
    std::map<uint32_t, WldTextureBrushSet> brushSets_;
    // Cache of resolved material lists keyed by 0x31 fragment index
    std::map<uint32_t, std::shared_ptr<ResolvedMaterialList>> resolvedMaterialListCache_;
    std::vector<std::string> textureNames_;
    std::vector<std::shared_ptr<Placeable>> placeables_;
    std::map<std::string, WldObjectDef> objectDefs_;
    std::map<uint32_t, WldModelRef> modelRefs_;
    std::map<uint32_t, std::shared_ptr<SkeletonTrack>> skeletonTracks_;
    std::map<uint32_t, uint32_t> skeletonRefs_;
    std::map<uint32_t, std::shared_ptr<BoneOrientation>> boneOrientations_;
    std::map<uint32_t, uint32_t> boneOrientationRefs_;
    std::map<uint32_t, std::shared_ptr<ZoneLight>> lightDefs_;
    std::map<uint32_t, uint32_t> lightDefRefs_;  // Fragment 0x1C: maps ref index -> 0x1B def index
    std::vector<std::shared_ptr<ZoneLight>> lights_;
    std::vector<std::shared_ptr<AmbientLightRegion>> ambientLightRegions_;
    std::shared_ptr<GlobalAmbientLight> globalAmbientLight_;

    // Map from fragment index to geometry (for precise bone model lookups)
    std::map<uint32_t, std::shared_ptr<ZoneGeometry>> geometryByFragIndex_;

    // Animation data
    std::map<uint32_t, std::shared_ptr<TrackDef>> trackDefs_;    // Fragment 0x12 - keyframe data
    std::map<uint32_t, std::shared_ptr<TrackRef>> trackRefs_;    // Fragment 0x13 - track references

    // Vertex animation data (for flags, banners, etc.)
    std::map<uint32_t, std::shared_ptr<MeshAnimatedVertices>> meshAnimatedVertices_;  // Fragment 0x37
    std::map<uint32_t, uint32_t> meshAnimatedVerticesRefs_;  // Fragment 0x2F -> 0x37 mapping

    // BSP tree for zone regions (zone lines, water, lava, etc.)
    std::shared_ptr<BspTree> bspTree_;
    uint32_t totalRegionCount_ = 0;  // From WLD header, used for PVS array sizing
};

// Helper function to decode WLD string hash
void decodeStringHash(char* str, size_t len);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_WLD_LOADER_H
