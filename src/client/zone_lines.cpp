#include "client/zone_lines.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath>
#include <json/json.h>

namespace EQT {

// Special marker value in EQ meaning "use source coordinate"
static constexpr float SAME_COORD_MARKER = 999999.0f;

bool ZoneLines::loadFromZone(const std::string& zoneName, const std::string& eqClientPath) {
    clear();

    // Store zone name for proximity-based detection
    currentZoneName_ = zoneName;

    // Load the zone's main WLD file to get BSP regions
    Graphics::WldLoader loader;
    std::string archivePath = eqClientPath + "/" + zoneName + ".s3d";
    std::string wldName = zoneName + ".wld";

    LOG_INFO(MOD_MAP, "Loading zone lines from {} / {}", archivePath, wldName);

    if (!loader.parseFromArchive(archivePath, wldName)) {
        LOG_WARN(MOD_MAP, "Failed to parse zone archive/wld, falling back to JSON zone points");
        // Still try to load proximity-based zone points
        std::string jsonPath = eqClientPath + "/../data/zone_points.json";
        loadZonePointsFromJson(jsonPath);
        return hasZoneLines();
    }

    // Get the BSP tree with zone line regions
    bspTree_ = loader.getBspTree();

    if (!bspTree_) {
        LOG_TRACE(MOD_MAP, "No BSP tree found in WLD");
    } else {
        LOG_TRACE(MOD_MAP, "BSP tree loaded: {} nodes, {} regions", bspTree_->nodes.size(), bspTree_->regions.size());
    }

    // Count zone line regions
    size_t zoneLineCount = 0;
    if (bspTree_) {
        for (size_t i = 0; i < bspTree_->regions.size(); ++i) {
            const auto& region = bspTree_->regions[i];
            for (auto type : region->regionTypes) {
                if (type == Graphics::RegionType::Zoneline) {
                    zoneLineCount++;
                    if (region->zoneLineInfo) {
                        LOG_TRACE(MOD_MAP, "Region {} is a zone line -> type={} zoneId={} zonePointIdx={} coords=({}, {}, {})",
                            i, (region->zoneLineInfo->type == Graphics::ZoneLineType::Absolute ? "Absolute" : "Reference"),
                            region->zoneLineInfo->zoneId, region->zoneLineInfo->zonePointIndex,
                            region->zoneLineInfo->x, region->zoneLineInfo->y, region->zoneLineInfo->z);
                    } else {
                        LOG_TRACE(MOD_MAP, "Region {} is a zone line (no info)", i);
                    }
                    break;
                }
            }
        }
    }
    LOG_TRACE(MOD_MAP, "Found {} BSP zone line regions", zoneLineCount);

    // Always load zone points from JSON - needed for Reference type zone lines
    // to look up target zone info, and as fallback for proximity detection
    std::string jsonPath = eqClientPath + "/../data/zone_points.json";
    loadZonePointsFromJson(jsonPath);

    // Extract any absolute zone line coordinates as fallback zone points
    // This helps when the server doesn't send zone points
    if (bspTree_) {
        for (const auto& region : bspTree_->regions) {
            if (region->zoneLineInfo) {
                const auto& info = *region->zoneLineInfo;
                if (info.type == Graphics::ZoneLineType::Absolute) {
                    // Create a zone point from the absolute coordinates
                    // We use a synthetic zone point number based on the zone ID
                    // This won't conflict with server zone points since server uses different numbering
                    ZonePoint point;
                    point.number = 0;  // Not indexed, accessed directly
                    point.targetZoneId = info.zoneId;
                    point.targetX = info.x;
                    point.targetY = info.y;
                    point.targetZ = info.z;
                    point.heading = info.heading;
                    // Store in wldZonePoints with a synthetic key
                    // (not used for lookup, just for reference)
                }
            }
        }
    }

    return hasZoneLines();
}

bool ZoneLines::loadZonePointsFromJson(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_TRACE(MOD_MAP, "Could not open zone points JSON: {}", jsonPath);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_MAP, "Failed to parse zone points JSON: {}", errors);
        return false;
    }

    if (!root.isArray()) {
        LOG_ERROR(MOD_MAP, "Zone points JSON is not an array");
        return false;
    }

    proximityZonePoints_.clear();
    size_t totalCount = 0;
    size_t matchCount = 0;

    for (const auto& entry : root) {
        totalCount++;
        std::string zone = entry["zone"].asString();

        // Only load zone points for the current zone
        if (zone != currentZoneName_) {
            continue;
        }

        ZonePointWithSource zp;
        zp.zoneName = zone;
        zp.number = entry["number"].asUInt();
        zp.sourceX = entry["source_x"].asFloat();
        zp.sourceY = entry["source_y"].asFloat();
        zp.sourceZ = entry["source_z"].asFloat();
        zp.targetZoneId = static_cast<uint16_t>(entry["target_zone_id"].asUInt());
        zp.targetX = entry["target_x"].asFloat();
        zp.targetY = entry["target_y"].asFloat();
        zp.targetZ = entry["target_z"].asFloat();
        zp.heading = entry["target_heading"].asFloat();

        // Skip entries with magic "same as current" values for source coordinates
        // These can't be used for proximity detection
        if (std::abs(zp.sourceX) >= SAME_COORD_MARKER - 1.0f ||
            std::abs(zp.sourceY) >= SAME_COORD_MARKER - 1.0f) {
            LOG_TRACE(MOD_MAP, "Skipping zone point {} with magic source coords", zp.number);
            continue;
        }

        proximityZonePoints_.push_back(zp);
        matchCount++;

        LOG_TRACE(MOD_MAP, "Loaded proximity zone point {}: source=({}, {}, {}) -> zone {} at ({}, {}, {})",
            zp.number, zp.sourceX, zp.sourceY, zp.sourceZ, zp.targetZoneId, zp.targetX, zp.targetY, zp.targetZ);
    }

    LOG_TRACE(MOD_MAP, "Loaded {} proximity zone points for {} (from {} total entries)",
        matchCount, currentZoneName_, totalCount);

    return matchCount > 0;
}

void ZoneLines::debugTestCoordinateMappings(float serverX, float serverY, float serverZ) const {
    if (!bspTree_) {
        LOG_TRACE(MOD_MAP, "No BSP tree for coordinate test");
        return;
    }

    LOG_TRACE(MOD_MAP, "=== COORDINATE MAPPING TEST ===");
    LOG_TRACE(MOD_MAP, "Server position: ({}, {}, {})", serverX, serverY, serverZ);

    // First, find sample coordinates that DO reach zone line regions
    LOG_TRACE(MOD_MAP, "--- Finding coords that reach zone 4 (qeynos hills) regions ---");
    bool foundExample = false;
    for (float x = -500; x <= 500 && !foundExample; x += 25) {
        for (float y = -2000; y <= 2000 && !foundExample; y += 100) {
            for (float z = -50; z <= 100 && !foundExample; z += 50) {
                auto info = bspTree_->checkZoneLine(x, y, z);
                if (info && info->zoneId == 4) {  // Zone 4 = qeynos hills
                    LOG_TRACE(MOD_MAP, "Found zone 4 region at BSP coords: ({}, {}, {})", x, y, z);
                    foundExample = true;
                }
            }
        }
    }

    // Test all 8 coordinate mappings
    struct CoordMapping {
        float x, y, z;
        const char* name;
    };
    CoordMapping mappings[] = {
        {serverX, serverY, serverZ, "(x, y, z)"},
        {serverY, serverX, serverZ, "(y, x, z)"},
        {-serverX, serverY, serverZ, "(-x, y, z)"},
        {serverX, -serverY, serverZ, "(x, -y, z)"},
        {-serverX, -serverY, serverZ, "(-x, -y, z)"},
        {-serverY, serverX, serverZ, "(-y, x, z)"},
        {serverY, -serverX, serverZ, "(y, -x, z)"},
        {-serverY, -serverX, serverZ, "(-y, -x, z)"},
    };

    LOG_TRACE(MOD_MAP, "--- Testing coordinate mappings ---");
    for (const auto& m : mappings) {
        auto region = bspTree_->findRegionForPoint(m.x, m.y, m.z);
        if (region) {
            bool isZoneLine = false;
            for (auto type : region->regionTypes) {
                if (type == Graphics::RegionType::Zoneline) {
                    isZoneLine = true;
                    break;
                }
            }
            if (isZoneLine && region->zoneLineInfo) {
                LOG_TRACE(MOD_MAP, "{} = ({}, {}, {}) -> ZONE LINE! zoneId={}", m.name, m.x, m.y, m.z, region->zoneLineInfo->zoneId);
            } else {
                LOG_TRACE(MOD_MAP, "{} = ({}, {}, {}) -> region (not zone line)", m.name, m.x, m.y, m.z);
            }
        } else {
            LOG_TRACE(MOD_MAP, "{} = ({}, {}, {}) -> NO REGION", m.name, m.x, m.y, m.z);
        }
    }
    LOG_TRACE(MOD_MAP, "=== END COORDINATE TEST ===");
}

void ZoneLines::clear() {
    bspTree_.reset();
    serverZonePoints_.clear();
    wldZonePoints_.clear();
    proximityZonePoints_.clear();
    currentZoneName_.clear();
}

bool ZoneLines::hasBspZoneLines() const {
    if (!bspTree_) {
        return false;
    }

    for (const auto& region : bspTree_->regions) {
        for (auto type : region->regionTypes) {
            if (type == Graphics::RegionType::Zoneline) {
                return true;
            }
        }
    }
    return false;
}

void ZoneLines::setServerZonePoints(const std::vector<ZonePoint>& points) {
    serverZonePoints_.clear();
    for (const auto& point : points) {
        serverZonePoints_[point.number] = point;
    }
}

void ZoneLines::addZonePoint(const ZonePoint& point) {
    serverZonePoints_[point.number] = point;
}

ZoneLineResult ZoneLines::checkPosition(float x, float y, float z,
                                         float currentX, float currentY, float currentZ) const {
    ZoneLineResult result;
    result.isZoneLine = false;

    static int callCount = 0;
    callCount++;

    // First try BSP tree detection if available
    if (bspTree_) {
        if (callCount % 100 == 0) {
            LOG_TRACE(MOD_MAP, "checkPosition #{}: ({}, {}, {}) bspTree nodes={}", callCount, x, y, z, bspTree_->nodes.size());
        }

        // Check if position is in a zone line region using BSP tree
        auto zoneLineInfo = bspTree_->checkZoneLine(x, y, z);
        if (zoneLineInfo) {
            return resolveZoneLine(*zoneLineInfo, currentX, currentY, currentZ);
        }
    }

    // Fall back to proximity-based detection
    if (!proximityZonePoints_.empty()) {
        return checkProximityZonePoints(x, y, z, currentX, currentY, currentZ);
    }

    if (callCount % 100 == 0 && !bspTree_ && proximityZonePoints_.empty()) {
        LOG_WARN(MOD_MAP, "checkPosition called but no zone line data!");
    }

    return result;
}

ZoneLineResult ZoneLines::checkProximityZonePoints(float x, float y, float z,
                                                    float currentX, float currentY, float currentZ) const {
    ZoneLineResult result;
    result.isZoneLine = false;

    for (const auto& zp : proximityZonePoints_) {
        // Calculate horizontal distance (X/Y plane)
        float dx = x - zp.sourceX;
        float dy = y - zp.sourceY;
        float horizontalDist = std::sqrt(dx * dx + dy * dy);

        // Check if within proximity radius
        if (horizontalDist <= DEFAULT_PROXIMITY_RADIUS) {
            // Check Z height (more lenient)
            float dz = z - zp.sourceZ;
            if (dz >= -10.0f && dz <= DEFAULT_PROXIMITY_HEIGHT) {
                // Found a matching zone point!
                result.isZoneLine = true;
                result.targetZoneId = zp.targetZoneId;

                // Handle "same as current" marker (999999)
                result.targetX = (std::abs(zp.targetX) >= SAME_COORD_MARKER - 1.0f) ? currentX : zp.targetX;
                result.targetY = (std::abs(zp.targetY) >= SAME_COORD_MARKER - 1.0f) ? currentY : zp.targetY;
                result.targetZ = (std::abs(zp.targetZ) >= SAME_COORD_MARKER - 1.0f) ? currentZ : zp.targetZ;
                result.heading = zp.heading;
                result.needsServerLookup = false;

                LOG_INFO(MOD_MAP, "Proximity zone line detected! Point {} at distance {} (radius={}) -> zone {} at ({}, {}, {})",
                    zp.number, horizontalDist, DEFAULT_PROXIMITY_RADIUS, result.targetZoneId, result.targetX, result.targetY, result.targetZ);

                return result;
            }
        }
    }

    return result;
}

std::optional<ZoneLineResult> ZoneLines::checkMovement(float oldX, float oldY, float oldZ,
                                                        float newX, float newY, float newZ) const {
    // Simple approach: check if new position is in a zone line
    // More sophisticated approach would check line segment intersection with BSP regions
    auto result = checkPosition(newX, newY, newZ, oldX, oldY, oldZ);
    if (result.isZoneLine) {
        return result;
    }
    return std::nullopt;
}

std::optional<ZonePoint> ZoneLines::getZonePoint(uint32_t index) const {
    // First check server zone points (takes precedence)
    auto it = serverZonePoints_.find(index);
    if (it != serverZonePoints_.end()) {
        return it->second;
    }

    // Fall back to WLD zone points
    it = wldZonePoints_.find(index);
    if (it != wldZonePoints_.end()) {
        return it->second;
    }

    // Fall back to proximity zone points from JSON
    for (const auto& zp : proximityZonePoints_) {
        if (zp.number == index) {
            ZonePoint point;
            point.number = zp.number;
            point.targetZoneId = zp.targetZoneId;
            point.targetX = zp.targetX;
            point.targetY = zp.targetY;
            point.targetZ = zp.targetZ;
            point.heading = zp.heading;
            return point;
        }
    }

    return std::nullopt;
}

size_t ZoneLines::getZoneLineCount() const {
    size_t count = 0;

    // Count BSP zone lines
    if (bspTree_) {
        for (const auto& region : bspTree_->regions) {
            for (auto type : region->regionTypes) {
                if (type == Graphics::RegionType::Zoneline) {
                    count++;
                    break;
                }
            }
        }
    }

    // Add proximity zone points
    count += proximityZonePoints_.size();

    return count;
}

ZoneLineResult ZoneLines::resolveZoneLine(const Graphics::ZoneLineInfo& info,
                                           float currentX, float currentY, float currentZ) const {
    ZoneLineResult result;
    result.isZoneLine = true;

    if (info.type == Graphics::ZoneLineType::Absolute) {
        // Direct coordinates in the zone line
        result.targetZoneId = info.zoneId;

        // Handle "same as current" marker (999999)
        result.targetX = (info.x >= SAME_COORD_MARKER - 1.0f) ? currentX : info.x;
        result.targetY = (info.y >= SAME_COORD_MARKER - 1.0f) ? currentY : info.y;
        result.targetZ = (info.z >= SAME_COORD_MARKER - 1.0f) ? currentZ : info.z;
        result.heading = (info.heading >= 999.0f) ? 0.0f : info.heading;
        result.needsServerLookup = false;

    } else {
        // Reference type - need to look up zone point
        auto zonePoint = getZonePoint(info.zonePointIndex);
        if (zonePoint) {
            result.targetZoneId = zonePoint->targetZoneId;
            result.targetX = zonePoint->targetX;
            result.targetY = zonePoint->targetY;
            result.targetZ = zonePoint->targetZ;
            result.heading = zonePoint->heading;
            result.needsServerLookup = false;
        } else {
            // We don't have the zone point data - need server lookup
            result.needsServerLookup = true;
            result.zonePointIndex = info.zonePointIndex;
            // Set defaults that indicate unknown destination
            result.targetZoneId = 0;
            result.targetX = currentX;
            result.targetY = currentY;
            result.targetZ = currentZ;
            result.heading = 0.0f;
        }
    }

    return result;
}

} // namespace EQT
