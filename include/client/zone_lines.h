#ifndef EQT_ZONE_LINES_H
#define EQT_ZONE_LINES_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include "client/graphics/eq/wld_loader.h"

namespace EQT {

// Zone point data received from server (or stored for fallback)
struct ZonePoint {
    uint32_t number = 0;           // Zone point ID
    float targetX = 0.0f;          // Destination X
    float targetY = 0.0f;          // Destination Y
    float targetZ = 0.0f;          // Destination Z
    float heading = 0.0f;          // Destination heading
    uint16_t targetZoneId = 0;     // Destination zone ID
};

// Zone point with source coordinates for proximity-based detection
// Used when BSP zone line regions aren't available
struct ZonePointWithSource {
    std::string zoneName;          // Source zone short name
    uint32_t number = 0;           // Zone point ID
    float sourceX = 0.0f;          // Source/trigger X
    float sourceY = 0.0f;          // Source/trigger Y
    float sourceZ = 0.0f;          // Source/trigger Z
    float targetX = 0.0f;          // Destination X
    float targetY = 0.0f;          // Destination Y
    float targetZ = 0.0f;          // Destination Z
    float heading = 0.0f;          // Destination heading
    uint16_t targetZoneId = 0;     // Destination zone ID
    bool extendX = false;          // True if sourceX was 999999 (extend box to zone bounds)
    bool extendY = false;          // True if sourceY was 999999 (extend box to zone bounds)
};

// Result of checking if a position is in a zone line
struct ZoneLineResult {
    bool isZoneLine = false;
    uint16_t targetZoneId = 0;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;
    float heading = 0.0f;
    bool needsServerLookup = false;  // True if we have a reference but no zone point data
    uint32_t zonePointIndex = 0;     // Zone point index if needsServerLookup is true
};

// Bounding box for zone line visualization
struct ZoneLineBoundingBox {
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    uint16_t targetZoneId = 0;
    uint32_t zonePointIndex = 0;     // For reference types
    bool isProximityBased = false;   // True if derived from proximity zone point
};

// Pre-extracted zone line with trigger box and destination
struct ExtractedZoneLine {
    uint32_t zonePointIndex = 0;
    std::string destinationZone;
    uint16_t destinationZoneId = 0;
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
    float destX = 0.0f, destY = 0.0f, destZ = 0.0f;
    float destHeading = 0.0f;
};

// Container for zone line data
class ZoneLines {
public:
    ZoneLines() = default;
    ~ZoneLines() = default;

    // Load zone lines from the zone's S3D/WLD file
    // Returns true if any zone lines were found
    bool loadFromZone(const std::string& zoneName, const std::string& eqClientPath);

    // Load pre-extracted zone lines from zone_lines.json
    // This is the preferred method - uses pre-computed trigger boxes
    bool loadFromExtractedJson(const std::string& zoneName, const std::string& jsonPath);

    // Load zone points with source coordinates from JSON file
    // Used for proximity-based detection when BSP zone lines aren't available
    bool loadZonePointsFromJson(const std::string& jsonPath);

    // Clear all loaded zone line data
    void clear();

    // Set zone points received from server (takes precedence over WLD data)
    void setServerZonePoints(const std::vector<ZonePoint>& points);

    // Add a single zone point
    void addZonePoint(const ZonePoint& point);

    // Check if a position is inside a zone line
    // If current position is provided, uses it to resolve "same as current" coordinates (999999)
    ZoneLineResult checkPosition(float x, float y, float z,
                                  float currentX = 0.0f, float currentY = 0.0f, float currentZ = 0.0f) const;

    // Check if movement from old_pos to new_pos crosses a zone line
    // Returns the zone line info if crossing detected
    std::optional<ZoneLineResult> checkMovement(float oldX, float oldY, float oldZ,
                                                 float newX, float newY, float newZ) const;

    // Get a zone point by index (for resolving reference-type zone lines)
    std::optional<ZonePoint> getZonePoint(uint32_t index) const;

    // Check if we have zone line data loaded (extracted, BSP, or proximity-based)
    bool hasZoneLines() const {
        return !extractedZoneLines_.empty() ||
               (bspTree_ != nullptr && !bspTree_->regions.empty()) ||
               !proximityZonePoints_.empty();
    }

    // Check if we have pre-extracted zone lines loaded
    bool hasExtractedZoneLines() const {
        return !extractedZoneLines_.empty();
    }

    // Check if we have BSP-based zone lines
    bool hasBspZoneLines() const;

    // Get the number of zone lines
    size_t getZoneLineCount() const;

    // Get bounding boxes for zone line visualization
    // Calculates approximate bounds for BSP zone lines and exact bounds for proximity zone points
    std::vector<ZoneLineBoundingBox> getZoneLineBoundingBoxes() const;

    // Debug: test all coordinate mappings to find which reaches zone line regions
    void debugTestCoordinateMappings(float serverX, float serverY, float serverZ) const;

    // Default proximity radius for zone point detection (in game units)
    static constexpr float DEFAULT_PROXIMITY_RADIUS = 50.0f;
    static constexpr float DEFAULT_PROXIMITY_HEIGHT = 30.0f;

private:
    // Resolve a zone line info to a concrete result
    ZoneLineResult resolveZoneLine(const Graphics::ZoneLineInfo& info,
                                    float currentX, float currentY, float currentZ) const;

    // Check extracted zone lines (trigger box detection)
    ZoneLineResult checkExtractedZoneLines(float x, float y, float z,
                                            float currentX, float currentY, float currentZ) const;

    // Check proximity-based zone points (fallback when no BSP zone lines)
    ZoneLineResult checkProximityZonePoints(float x, float y, float z,
                                             float currentX, float currentY, float currentZ) const;

    // Pre-extracted zone lines from zone_lines.json (preferred)
    std::vector<ExtractedZoneLine> extractedZoneLines_;

    // BSP tree from WLD file (fallback for zone line detection)
    std::shared_ptr<Graphics::BspTree> bspTree_;

    // Zone geometry bounds (used for BSP region bounds computation)
    float zoneMinX_ = -10000.0f, zoneMinY_ = -10000.0f, zoneMinZ_ = -1000.0f;
    float zoneMaxX_ = 10000.0f, zoneMaxY_ = 10000.0f, zoneMaxZ_ = 1000.0f;
    bool hasZoneBounds_ = false;

    // Zone points from server (keyed by zone point number)
    std::map<uint32_t, ZonePoint> serverZonePoints_;

    // Zone points parsed from WLD (fallback if server doesn't send them)
    std::map<uint32_t, ZonePoint> wldZonePoints_;

    // Zone points with source coordinates for proximity-based detection
    // Loaded from zone_points.json, filtered by current zone
    std::vector<ZonePointWithSource> proximityZonePoints_;

    // Current zone name (for filtering proximity zone points)
    std::string currentZoneName_;
};

} // namespace EQT

#endif // EQT_ZONE_LINES_H
