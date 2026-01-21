#ifndef EQT_GRAPHICS_SURFACE_MAP_H
#define EQT_GRAPHICS_SURFACE_MAP_H

#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include "client/graphics/detail/detail_types.h"

namespace EQT {
namespace Graphics {
namespace Detail {

// Surface map file format header (must match generate_surface_map.cpp)
struct SurfaceMapHeader {
    char magic[4];           // "SMAP"
    uint32_t version;        // 1
    float cellSize;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    uint32_t gridWidth;
    uint32_t gridHeight;
    uint32_t cellCount;
};

// Raw surface type values in the map file
// NOTE: These values are stored in binary map files - do not reorder existing values!
// New types must be added at the end to maintain backwards compatibility.
enum class RawSurfaceType : uint8_t {
    Unknown = 0,
    Grass   = 1,
    Dirt    = 2,
    Stone   = 3,
    Brick   = 4,
    Wood    = 5,
    Sand    = 6,
    Snow    = 7,
    Water   = 8,
    Lava    = 9,
    Jungle  = 10,  // Kunark tropical vegetation
    Swamp   = 11,  // Wetlands, marshes
    Rock    = 12   // Natural rocky terrain (not man-made)
};

/**
 * SurfaceMap - Pre-computed surface type map for a zone
 *
 * This provides fast O(1) lookup of surface types at any world coordinate,
 * replacing the expensive on-the-fly texture classification.
 */
class SurfaceMap {
public:
    SurfaceMap() = default;
    ~SurfaceMap() = default;

    // Load from file
    bool load(const std::string& filePath);

    // Check if map is loaded
    bool isLoaded() const { return loaded_; }

    // Get surface type at world coordinates (EQ coordinates: X, Y horizontal, Z up)
    // Returns SurfaceType::Unknown if out of bounds
    SurfaceType getSurfaceType(float x, float y) const;

    // Get ground height at world coordinates
    // Returns -10000 if out of bounds or no ground data
    float getHeight(float x, float y) const;

    // Get grid cell at world coordinates
    bool getCell(float x, float y, uint32_t& outCellX, uint32_t& outCellY) const;

    // Accessors
    float getCellSize() const { return header_.cellSize; }
    float getMinX() const { return header_.minX; }
    float getMaxX() const { return header_.maxX; }
    float getMinY() const { return header_.minY; }
    float getMaxY() const { return header_.maxY; }
    uint32_t getGridWidth() const { return header_.gridWidth; }
    uint32_t getGridHeight() const { return header_.gridHeight; }

private:
    // Convert raw surface type to detail system SurfaceType
    static SurfaceType convertRawType(RawSurfaceType raw);

    bool loaded_ = false;
    SurfaceMapHeader header_{};
    std::vector<RawSurfaceType> surfaceGrid_;
    std::vector<float> heightGrid_;
};

// Inline implementations

inline bool SurfaceMap::load(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        return false;
    }

    // Read header
    file.read(reinterpret_cast<char*>(&header_), sizeof(header_));
    if (!file) {
        return false;
    }

    // Validate magic
    if (header_.magic[0] != 'S' || header_.magic[1] != 'M' ||
        header_.magic[2] != 'A' || header_.magic[3] != 'P') {
        return false;
    }

    // Validate version
    if (header_.version != 1) {
        return false;
    }

    // Read surface grid
    surfaceGrid_.resize(header_.cellCount);
    file.read(reinterpret_cast<char*>(surfaceGrid_.data()), header_.cellCount);
    if (!file) {
        surfaceGrid_.clear();
        return false;
    }

    // Read height grid
    heightGrid_.resize(header_.cellCount);
    file.read(reinterpret_cast<char*>(heightGrid_.data()), header_.cellCount * sizeof(float));
    if (!file) {
        surfaceGrid_.clear();
        heightGrid_.clear();
        return false;
    }

    loaded_ = true;
    return true;
}

inline bool SurfaceMap::getCell(float x, float y, uint32_t& outCellX, uint32_t& outCellY) const {
    if (!loaded_) return false;
    if (x < header_.minX || x >= header_.maxX) return false;
    if (y < header_.minY || y >= header_.maxY) return false;

    outCellX = static_cast<uint32_t>((x - header_.minX) / header_.cellSize);
    outCellY = static_cast<uint32_t>((y - header_.minY) / header_.cellSize);

    if (outCellX >= header_.gridWidth || outCellY >= header_.gridHeight) return false;
    return true;
}

inline SurfaceType SurfaceMap::getSurfaceType(float x, float y) const {
    uint32_t cellX, cellY;
    if (!getCell(x, y, cellX, cellY)) {
        return SurfaceType::Unknown;
    }

    size_t idx = cellY * header_.gridWidth + cellX;
    if (idx >= surfaceGrid_.size()) {
        return SurfaceType::Unknown;
    }

    return convertRawType(surfaceGrid_[idx]);
}

inline float SurfaceMap::getHeight(float x, float y) const {
    uint32_t cellX, cellY;
    if (!getCell(x, y, cellX, cellY)) {
        return -10000.0f;
    }

    size_t idx = cellY * header_.gridWidth + cellX;
    if (idx >= heightGrid_.size()) {
        return -10000.0f;
    }

    return heightGrid_[idx];
}

inline SurfaceType SurfaceMap::convertRawType(RawSurfaceType raw) {
    switch (raw) {
        case RawSurfaceType::Unknown: return SurfaceType::Unknown;
        case RawSurfaceType::Grass:   return SurfaceType::Grass;
        case RawSurfaceType::Dirt:    return SurfaceType::Dirt;
        case RawSurfaceType::Stone:   return SurfaceType::Stone;
        case RawSurfaceType::Brick:   return SurfaceType::Brick;
        case RawSurfaceType::Wood:    return SurfaceType::Wood;
        case RawSurfaceType::Sand:    return SurfaceType::Sand;
        case RawSurfaceType::Snow:    return SurfaceType::Snow;
        case RawSurfaceType::Water:   return SurfaceType::Water;
        case RawSurfaceType::Lava:    return SurfaceType::Lava;
        case RawSurfaceType::Jungle:  return SurfaceType::Jungle;
        case RawSurfaceType::Swamp:   return SurfaceType::Swamp;
        case RawSurfaceType::Rock:    return SurfaceType::Rock;
        default: return SurfaceType::Unknown;
    }
}

} // namespace Detail
} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_SURFACE_MAP_H
