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
};

// WLD file structures (packed)
#pragma pack(push, 1)

struct WldHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t fragmentCount;
    uint32_t unk1;
    uint32_t unk2;
    uint32_t hashLength;
    uint32_t unk3;
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

struct WldFragment30Header {
    uint32_t flags;
    uint32_t params1;
    uint32_t params2;
    float params3[2];
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
    uint8_t x;
    uint8_t y;
    uint8_t z;
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

// Fragment 0x15 - Placeable object instance
struct WldFragment15Header {
    uint32_t flags;
    int32_t refId;
    float x, y, z;
    float rotateZ, rotateY, rotateX;
    float unk;
    float scaleY, scaleX;
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
struct WldFragment1BHeader {
    uint32_t flags;
    uint32_t params1;
    float color[3];
};

// Fragment 0x28 - Light source instance
struct WldFragment28Header {
    uint32_t flags;
    float x, y, z;
    float radius;
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

// Light source data
struct ZoneLight {
    std::string name;
    float x, y, z;
    float r, g, b;
    float radius;
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

    // BSP tree accessor (for zone line detection)
    const std::shared_ptr<BspTree>& getBspTree() const { return bspTree_; }
    bool hasZoneLines() const { return bspTree_ && !bspTree_->regions.empty(); }

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
                         const char* hash, bool oldFormat);
    void parseFragment2C(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment2D(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment10(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat,
                         const std::vector<std::pair<size_t, const WldFragmentHeader*>>& fragments,
                         const std::vector<char>& buffer);
    void parseFragment11(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
    void parseFragment12(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);
    void parseFragment13(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash);
    void parseFragment1B(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                         int32_t nameRef, const char* hash, bool oldFormat);
    void parseFragment28(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex);
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
};

// Helper function to decode WLD string hash
void decodeStringHash(char* str, size_t len);

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_WLD_LOADER_H
