#ifndef EQT_GRAPHICS_WLD_LOADER_H
#define EQT_GRAPHICS_WLD_LOADER_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include "placeable.h"

namespace EQT {
namespace Graphics {

// Region types for BSP regions
enum class RegionType : uint8_t {
    Normal = 0,
    Water = 1,
    Lava = 2,
    Pvp = 3,
    Zoneline = 4,
    WaterBlockLOS = 5,
    FreezingWater = 6,
    Slippery = 7,
    Unknown = 8
};

// Zone line types
enum class ZoneLineType : uint8_t {
    Reference = 0,  // References a zone_point from the DB
    Absolute = 1    // Direct zone coordinates embedded in the name
};

// Zone line destination info
struct ZoneLineInfo {
    ZoneLineType type = ZoneLineType::Reference;
    uint16_t zoneId = 0;           // Target zone ID (for Absolute type)
    uint32_t zonePointIndex = 0;   // Zone point index (for Reference type)
    float x = 0.0f, y = 0.0f, z = 0.0f;  // Destination coordinates
    float heading = 0.0f;          // Destination heading (rotation)
};

// BSP tree node
struct BspNode {
    float normalX = 0.0f, normalY = 0.0f, normalZ = 0.0f;
    float splitDistance = 0.0f;
    int32_t regionId = 0;   // 1-indexed, 0 = no region
    int32_t left = -1;      // Left child index (-1 = no child)
    int32_t right = -1;     // Right child index (-1 = no child)
};

// BSP region (fragment 0x22)
struct BspRegion {
    bool containsPolygons = false;
    int32_t meshReference = -1;
    std::vector<RegionType> regionTypes;
    std::optional<ZoneLineInfo> zoneLineInfo;
    // PVS (Potentially Visible Set) data - which regions are visible from this region
    // Indexed by region ID (0-based), true = that region is visible from this one
    std::vector<bool> visibleRegions;
};

// Axis-aligned bounding box for BSP region bounds calculation
struct BspBounds {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    bool valid = false;

    BspBounds() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0), valid(false) {}
    BspBounds(float x1, float y1, float z1, float x2, float y2, float z2)
        : minX(x1), minY(y1), minZ(z1), maxX(x2), maxY(y2), maxZ(z2), valid(true) {}

    // Merge with another bounds (union)
    void merge(const BspBounds& other) {
        if (!other.valid) return;
        if (!valid) {
            *this = other;
            return;
        }
        minX = std::min(minX, other.minX);
        minY = std::min(minY, other.minY);
        minZ = std::min(minZ, other.minZ);
        maxX = std::max(maxX, other.maxX);
        maxY = std::max(maxY, other.maxY);
        maxZ = std::max(maxZ, other.maxZ);
    }
};

// BSP tree structure for zone
struct BspTree {
    std::vector<BspNode> nodes;
    std::vector<std::shared_ptr<BspRegion>> regions;

    // Find which region a point is in by traversing the BSP tree
    // Returns nullptr if not in any region
    std::shared_ptr<BspRegion> findRegionForPoint(float x, float y, float z) const;

    // Check if a point is in a zone line region
    // Returns the zone line info if in a zone line, nullopt otherwise
    std::optional<ZoneLineInfo> checkZoneLine(float x, float y, float z) const;

    // Compute bounding box for a specific region by traversing the BSP tree
    // regionIndex is 0-based index into the regions vector
    // initialBounds provides the starting search area (typically zone geometry bounds)
    BspBounds computeRegionBounds(size_t regionIndex, const BspBounds& initialBounds) const;

private:
    // Recursive helper for computeRegionBounds
    // Returns bounds for the target region found in this subtree
    BspBounds computeRegionBoundsRecursive(int nodeIdx, size_t targetRegionIndex,
                                            const BspBounds& currentBounds) const;

    // Clip bounds by a plane, returning the portion on the specified side
    // frontSide=true: return portion where dot >= 0
    // frontSide=false: return portion where dot < 0
    static BspBounds clipBoundsByPlane(const BspBounds& bounds,
                                        float nx, float ny, float nz, float dist,
                                        bool frontSide);
};

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

struct ZoneGeometry {
    std::vector<Vertex3D> vertices;
    std::vector<Triangle> triangles;
    std::string name;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    // Mesh center point (vertices are stored relative to this)
    float centerX = 0, centerY = 0, centerZ = 0;
    std::vector<std::string> textureNames;
    std::vector<bool> textureInvisible;
    // Texture animation info (indexed by textureIndex)
    std::vector<TextureAnimationInfo> textureAnimations;
    // For character models - vertex to bone mapping
    std::vector<VertexPiece> vertexPieces;
    // Vertex animation data (for flags, banners, etc.)
    std::shared_ptr<MeshAnimatedVertices> animatedVertices;
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
    std::string currentHash_;
    std::map<uint32_t, WldTexture> textures_;
    std::map<uint32_t, WldTextureBrush> brushes_;
    std::map<uint32_t, uint32_t> textureRefs_;
    std::map<uint32_t, WldTextureBrush> materials_;
    std::map<uint32_t, WldTextureBrushSet> brushSets_;
    std::vector<std::string> textureNames_;
    std::vector<std::shared_ptr<Placeable>> placeables_;
    std::map<std::string, WldObjectDef> objectDefs_;
    std::map<uint32_t, WldModelRef> modelRefs_;
    std::map<uint32_t, std::shared_ptr<SkeletonTrack>> skeletonTracks_;
    std::map<uint32_t, uint32_t> skeletonRefs_;
    std::map<uint32_t, std::shared_ptr<BoneOrientation>> boneOrientations_;
    std::map<uint32_t, uint32_t> boneOrientationRefs_;
    std::map<uint32_t, std::shared_ptr<ZoneLight>> lightDefs_;
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
