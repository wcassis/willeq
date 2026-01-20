#ifndef EQT_GRAPHICS_DETAIL_TYPES_H
#define EQT_GRAPHICS_DETAIL_TYPES_H

#include <irrlicht.h>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace EQT {
namespace Graphics {
namespace Detail {

// Detail categories for independent toggling
enum class DetailCategory : uint32_t {
    None        = 0,
    Grass       = 1 << 0,
    Plants      = 1 << 1,
    Rocks       = 1 << 2,
    Debris      = 1 << 3,
    Mushrooms   = 1 << 4,
    All         = 0xFFFFFFFF
};

inline DetailCategory operator|(DetailCategory a, DetailCategory b) {
    return static_cast<DetailCategory>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline uint32_t operator&(DetailCategory a, DetailCategory b) {
    return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

inline uint32_t operator&(uint32_t a, DetailCategory b) {
    return a & static_cast<uint32_t>(b);
}

// Sprite orientation modes
enum class DetailOrientation {
    CrossedQuads,    // Two quads in X pattern (grass, small plants)
    FlatGround,      // Single quad lying flat (debris, leaves)
    SingleQuad       // Single vertical quad (rarely needed)
};

// Season types matching zone environments
enum class Season {
    Default,         // Standard green vegetation
    Snow,            // Everfrost, Permafrost, etc.
    Autumn,          // Optional
    Desert,          // Ro deserts - reduced vegetation
    Swamp            // Innothule - different colors
};

// Surface types for detail placement filtering
enum class SurfaceType : uint32_t {
    Unknown     = 0,
    Grass       = 1 << 0,   // Natural grass/vegetation areas
    Dirt        = 1 << 1,   // Dirt paths, unpaved ground
    Stone       = 1 << 2,   // Stone floors, cobblestone, rock
    Brick       = 1 << 3,   // Brick surfaces, walls
    Wood        = 1 << 4,   // Wooden floors, planks
    Sand        = 1 << 5,   // Desert sand, beaches
    Snow        = 1 << 6,   // Snow-covered ground
    Water       = 1 << 7,   // Water surfaces (excluded)
    Lava        = 1 << 8,   // Lava surfaces (excluded)

    // Composite masks for convenience
    Natural     = Grass | Dirt | Sand | Snow,  // Natural outdoor surfaces
    HardSurface = Stone | Brick | Wood,        // Man-made hard surfaces
    All         = 0xFFFFFFFF
};

inline SurfaceType operator|(SurfaceType a, SurfaceType b) {
    return static_cast<SurfaceType>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline uint32_t operator&(SurfaceType a, SurfaceType b) {
    return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

inline uint32_t operator&(uint32_t a, SurfaceType b) {
    return a & static_cast<uint32_t>(b);
}

// Single detail type definition
struct DetailType {
    std::string name;
    DetailCategory category = DetailCategory::Grass;
    DetailOrientation orientation = DetailOrientation::CrossedQuads;

    // UV coordinates in atlas (pixel coords, converted to normalized at load)
    int atlasX = 0;
    int atlasY = 0;
    int atlasWidth = 64;
    int atlasHeight = 64;

    // Cached normalized UVs (set by loader)
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

    // Size range (randomized per instance)
    float minSize = 0.5f;
    float maxSize = 1.5f;

    // Placement rules
    float minSlope = 0.0f;       // Radians (0 = flat)
    float maxSlope = 0.5f;       // ~28 degrees
    float baseDensity = 1.0f;    // Relative to global density

    // Allowed surface types (bitmask of SurfaceType)
    // Default: Natural surfaces only (no stone/brick/wood)
    uint32_t allowedSurfaces = static_cast<uint32_t>(SurfaceType::Natural);

    // Wind response (0.0 = no wind, 1.0 = full response)
    float windResponse = 1.0f;

    // Height at which wind effect is strongest (normalized 0-1)
    float windHeightBias = 0.8f;

    // Color for testing (used when no atlas is loaded)
    irr::video::SColor testColor = irr::video::SColor(255, 100, 200, 100);
};

// Per-instance placement data
struct DetailPlacement {
    irr::core::vector3df position;
    float rotation = 0.0f;       // Y-axis rotation in radians
    float scale = 1.0f;
    uint16_t typeIndex = 0;      // Index into DetailType array
    uint8_t seed = 0;            // For deterministic randomness
};

// Chunk grid key
struct ChunkKey {
    int32_t x = 0;
    int32_t z = 0;

    bool operator==(const ChunkKey& other) const {
        return x == other.x && z == other.z;
    }

    bool operator<(const ChunkKey& other) const {
        if (x != other.x) return x < other.x;
        return z < other.z;
    }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return std::hash<int64_t>()(
            (static_cast<int64_t>(k.x) << 32) |
            static_cast<uint32_t>(k.z));
    }
};

// Zone detail configuration
struct ZoneDetailConfig {
    std::string zoneName;
    bool isOutdoor = true;
    Season season = Season::Default;

    std::vector<DetailType> detailTypes;

    float densityMultiplier = 1.0f;
    float viewDistance = 150.0f;
    float chunkSize = 50.0f;

    // Wind settings for this zone
    float windStrength = 1.0f;
    float windFrequency = 0.5f;   // Oscillations per second

    // Seasonal color tint (applied to vertex colors)
    irr::video::SColor seasonTint{255, 255, 255, 255};

    // Exclusion regions (zone lines, water)
    std::vector<irr::core::aabbox3df> exclusionBoxes;
};

// Callback types for ground queries
// Returns: true if ground found, false otherwise
// outY: ground height
// outNormal: surface normal
// outSurfaceType: detected surface type based on texture
using GroundQueryFunc = std::function<bool(float x, float z, float& outY,
                                           irr::core::vector3df& outNormal,
                                           SurfaceType& outSurfaceType)>;
using ExclusionCheckFunc = std::function<bool(const irr::core::vector3df& pos)>;

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_DETAIL_TYPES_H
