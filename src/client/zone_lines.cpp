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

    bool loadedAny = false;

    // Load proximity zone points from zone_points.json (primary method)
    // These use source coordinates that are verified to match actual player positions
    std::vector<std::string> zonePointsPaths = {
        "data/zone_points.json",                      // Relative to CWD (project root)
        "../data/zone_points.json",                   // Relative to CWD (build directory)
        eqClientPath + "/../data/zone_points.json"   // Relative to EQ client path
    };
    for (const auto& jsonPath : zonePointsPaths) {
        if (loadZonePointsFromJson(jsonPath)) {
            LOG_INFO(MOD_MAP, "Loaded {} proximity zone points for {} from {}",
                     proximityZonePoints_.size(), zoneName, jsonPath);
            loadedAny = true;
            break;
        }
    }

    // Also try to load from pre-extracted zone_lines.json (for trigger box visualization)
    std::vector<std::string> zoneLinesJsonPaths = {
        "data/zone_lines.json",                      // Relative to CWD (project root)
        "../data/zone_lines.json",                   // Relative to CWD (build directory)
        eqClientPath + "/../data/zone_lines.json"   // Relative to EQ client path
    };
    for (const auto& jsonPath : zoneLinesJsonPaths) {
        if (loadFromExtractedJson(zoneName, jsonPath)) {
            LOG_INFO(MOD_MAP, "Loaded {} pre-extracted zone lines for {} from {}",
                     extractedZoneLines_.size(), zoneName, jsonPath);
            loadedAny = true;
            break;
        }
    }

    // Also load BSP tree and geometry bounds from WLD
    Graphics::WldLoader loader;
    std::string archivePath = eqClientPath + "/" + zoneName + ".s3d";
    std::string wldName = zoneName + ".wld";
    if (loader.parseFromArchive(archivePath, wldName)) {
        bspTree_ = loader.getBspTree();
        if (bspTree_) {
            LOG_DEBUG(MOD_MAP, "Loaded BSP tree: {} nodes, {} regions (for debugging)",
                     bspTree_->nodes.size(), bspTree_->regions.size());
        }

        // Get geometry bounds from WLD if we don't have them from zone_lines.json
        if (!hasZoneBounds_) {
            auto geometry = loader.getCombinedGeometry();
            if (geometry) {
                zoneMinX_ = geometry->minX;
                zoneMinY_ = geometry->minY;
                zoneMinZ_ = geometry->minZ;
                zoneMaxX_ = geometry->maxX;
                zoneMaxY_ = geometry->maxY;
                zoneMaxZ_ = geometry->maxZ;
                hasZoneBounds_ = true;
                LOG_DEBUG(MOD_MAP, "Got geometry bounds from WLD: X[{:.1f},{:.1f}] Y[{:.1f},{:.1f}] Z[{:.1f},{:.1f}]",
                    zoneMinX_, zoneMaxX_, zoneMinY_, zoneMaxY_, zoneMinZ_, zoneMaxZ_);
            }
        }
    }

    if (!loadedAny) {
        LOG_WARN(MOD_MAP, "No zone line data found for '{}' - checked zone_points.json and zone_lines.json", zoneName);
    }

    return loadedAny;
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

        // Check for "any position" markers in source coordinates
        // These create extended zone lines that span the entire axis
        zp.extendX = (std::abs(zp.sourceX) >= SAME_COORD_MARKER - 1.0f);
        zp.extendY = (std::abs(zp.sourceY) >= SAME_COORD_MARKER - 1.0f);

        proximityZonePoints_.push_back(zp);
        matchCount++;

        if (zp.extendX || zp.extendY) {
            LOG_TRACE(MOD_MAP, "Loaded extended zone point {}: source=({}, {}, {}) extendX={} extendY={} -> zone {}",
                zp.number, zp.sourceX, zp.sourceY, zp.sourceZ, zp.extendX, zp.extendY, zp.targetZoneId);
        } else {
            LOG_TRACE(MOD_MAP, "Loaded proximity zone point {}: source=({}, {}, {}) -> zone {} at ({}, {}, {})",
                zp.number, zp.sourceX, zp.sourceY, zp.sourceZ, zp.targetZoneId, zp.targetX, zp.targetY, zp.targetZ);
        }
    }

    LOG_TRACE(MOD_MAP, "Loaded {} proximity zone points for {} (from {} total entries)",
        matchCount, currentZoneName_, totalCount);

    return matchCount > 0;
}

bool ZoneLines::loadFromExtractedJson(const std::string& zoneName, const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_TRACE(MOD_MAP, "Could not open extracted zone lines JSON: {}", jsonPath);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_MAP, "Failed to parse extracted zone lines JSON: {}", errors);
        return false;
    }

    if (!root.isObject()) {
        LOG_ERROR(MOD_MAP, "Extracted zone lines JSON is not an object");
        return false;
    }

    // Check if this zone exists in the JSON
    if (!root.isMember(zoneName)) {
        LOG_TRACE(MOD_MAP, "Zone {} not found in extracted zone lines JSON", zoneName);
        return false;
    }

    const Json::Value& zoneData = root[zoneName];
    if (!zoneData.isMember("zone_lines") || !zoneData["zone_lines"].isArray()) {
        LOG_TRACE(MOD_MAP, "Zone {} has no zone_lines array", zoneName);
        return false;
    }

    extractedZoneLines_.clear();
    const Json::Value& zoneLines = zoneData["zone_lines"];

    for (const auto& zl : zoneLines) {
        ExtractedZoneLine ezl;
        ezl.zonePointIndex = zl["zone_point_index"].asUInt();
        ezl.destinationZone = zl["destination_zone"].asString();
        ezl.destinationZoneId = static_cast<uint16_t>(zl["destination_zone_id"].asUInt());

        if (zl.isMember("trigger_box")) {
            const Json::Value& box = zl["trigger_box"];
            ezl.minX = box["min_x"].asFloat();
            ezl.minY = box["min_y"].asFloat();
            ezl.minZ = box["min_z"].asFloat();
            ezl.maxX = box["max_x"].asFloat();
            ezl.maxY = box["max_y"].asFloat();
            ezl.maxZ = box["max_z"].asFloat();
        }

        if (zl.isMember("destination")) {
            const Json::Value& dest = zl["destination"];
            ezl.destX = dest["x"].asFloat();
            ezl.destY = dest["y"].asFloat();
            ezl.destZ = dest["z"].asFloat();
            ezl.destHeading = dest["heading"].asFloat();
        }

        extractedZoneLines_.push_back(ezl);

        LOG_TRACE(MOD_MAP, "Loaded extracted zone line: #{} -> {} (id {}) box ({},{},{}) to ({},{},{})",
            ezl.zonePointIndex, ezl.destinationZone, ezl.destinationZoneId,
            ezl.minX, ezl.minY, ezl.minZ, ezl.maxX, ezl.maxY, ezl.maxZ);
    }

    // Also load geometry bounds if available
    if (zoneData.isMember("geometry_bounds")) {
        const Json::Value& bounds = zoneData["geometry_bounds"];
        zoneMinX_ = bounds["min_x"].asFloat();
        zoneMinY_ = bounds["min_y"].asFloat();
        zoneMinZ_ = bounds["min_z"].asFloat();
        zoneMaxX_ = bounds["max_x"].asFloat();
        zoneMaxY_ = bounds["max_y"].asFloat();
        zoneMaxZ_ = bounds["max_z"].asFloat();
        hasZoneBounds_ = true;
    }

    return !extractedZoneLines_.empty();
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
    extractedZoneLines_.clear();
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

    // The input coordinates are client display coords (m_x, m_y, m_z)
    // where m_x = server_y, m_y = server_x (swapped)
    // So server coords are: server_x = y (input), server_y = x (input)
    float server_x = y;
    float server_y = x;

    // FIRST: Check proximity-based zone points (including extended zone lines)
    // This must happen before BSP check because extended zone lines span the entire
    // axis and BSP may only detect them at specific spots
    if (!proximityZonePoints_.empty()) {
        // Use server coords (which match zone_points.json format)
        // IMPORTANT: Pass server coords for "current" position as well, since zone_points.json
        // target coordinates (including 999999 "same as current" markers) are in server format
        result = checkProximityZonePoints(server_x, server_y, z, server_x, server_y, z);
        if (result.isZoneLine) {
            LOG_INFO(MOD_MAP, "Proximity zone point matched at server coords ({:.1f},{:.1f},{:.1f})", server_x, server_y, z);
            return result;
        }
    }

    // SECOND: BSP tree check - try multiple coordinate mappings
    if (bspTree_) {
        // Define all coordinate mappings to test
        struct CoordMapping {
            float bsp_x, bsp_y, bsp_z;
            const char* name;
        };

        // Based on analysis: BSP might use local coordinates relative to mesh center
        // or have different axis conventions. Test all possibilities.
        CoordMapping mappings[] = {
            // Original client display coords
            {x, y, z, "client(x,y,z)"},
            // Server coords (swapped from client)
            {server_x, server_y, z, "server(y,x,z)"},
            // Negated variants
            {-x, y, z, "client(-x,y,z)"},
            {x, -y, z, "client(x,-y,z)"},
            {-x, -y, z, "client(-x,-y,z)"},
            {-server_x, server_y, z, "server(-y,x,z)"},
            {server_x, -server_y, z, "server(y,-x,z)"},
            {-server_x, -server_y, z, "server(-y,-x,z)"},
        };

        for (const auto& m : mappings) {
            auto zoneLineInfo = bspTree_->checkZoneLine(m.bsp_x, m.bsp_y, m.bsp_z);
            if (zoneLineInfo) {
                LOG_INFO(MOD_MAP, "BSP FOUND zone line using mapping {}! coords=({:.1f},{:.1f},{:.1f}) -> type={} zoneId={} zpIdx={}",
                    m.name, m.bsp_x, m.bsp_y, m.bsp_z,
                    (zoneLineInfo->type == Graphics::ZoneLineType::Absolute ? "Absolute" : "Reference"),
                    zoneLineInfo->zoneId, zoneLineInfo->zonePointIndex);

                // Pass server coordinates for 999999 resolution
                // zone_points.json targets are in server format, so we need server coords here
                return resolveZoneLine(*zoneLineInfo, server_x, server_y, z);
            }
        }

        // Log periodically if no mapping works
        if (callCount % 200 == 1) {
            LOG_DEBUG(MOD_MAP, "BSP check: no zone line found. client=({:.1f},{:.1f},{:.1f}) server=({:.1f},{:.1f},{:.1f})",
                x, y, z, server_x, server_y, z);

            // Try to find what coordinates DO reach zone lines by sampling
            static bool didSampleSearch = false;
            if (!didSampleSearch) {
                didSampleSearch = true;
                LOG_DEBUG(MOD_MAP, "Searching for BSP coordinates that reach zone lines...");
                for (float tx = -500; tx <= 500; tx += 50) {
                    for (float ty = 0; ty <= 500; ty += 50) {
                        auto info = bspTree_->checkZoneLine(tx, ty, z);
                        if (info && info->zoneId == 4) {  // qeytoqrg = zone 4
                            LOG_INFO(MOD_MAP, "Found qeytoqrg zone line at BSP coords ({:.1f},{:.1f},{:.1f})", tx, ty, z);
                            // Calculate the offset from player position
                            LOG_INFO(MOD_MAP, "Player client=({:.1f},{:.1f}) server=({:.1f},{:.1f}) -> BSP=({:.1f},{:.1f})",
                                x, y, server_x, server_y, tx, ty);
                            LOG_INFO(MOD_MAP, "Offset from client: dx={:.1f} dy={:.1f}", tx - x, ty - y);
                            LOG_INFO(MOD_MAP, "Offset from server: dx={:.1f} dy={:.1f}", tx - server_x, ty - server_y);
                            break;
                        }
                    }
                }
            }
        }
    }

    // Fallback: Check pre-extracted zone lines (trigger boxes may have coordinate issues)
    if (!extractedZoneLines_.empty()) {
        // Try server coords (which match zone_points.json format)
        result = checkExtractedZoneLines(server_x, server_y, z, currentX, currentY, currentZ);
        if (result.isZoneLine) {
            LOG_INFO(MOD_MAP, "Extracted zone line matched with SERVER coords ({:.1f},{:.1f},{:.1f})", server_x, server_y, z);
            return result;
        }

        // Try client coords as fallback
        result = checkExtractedZoneLines(x, y, z, currentX, currentY, currentZ);
        if (result.isZoneLine) {
            LOG_INFO(MOD_MAP, "Extracted zone line matched with CLIENT coords ({:.1f},{:.1f},{:.1f})", x, y, z);
            return result;
        }
    }

    if (callCount % 100 == 0 && extractedZoneLines_.empty() && proximityZonePoints_.empty()) {
        LOG_WARN(MOD_MAP, "checkPosition called but no zone line data for this zone!");
    }

    return result;
}

ZoneLineResult ZoneLines::checkExtractedZoneLines(float x, float y, float z,
                                                   float currentX, float currentY, float currentZ) const {
    ZoneLineResult result;
    result.isZoneLine = false;

    // Debug: log position and trigger boxes every 100 calls
    static int debugCount = 0;
    debugCount++;
    if (debugCount % 100 == 1) {
        LOG_DEBUG(MOD_MAP, "checkExtractedZoneLines: pos=({:.1f}, {:.1f}, {:.1f}) checking {} zone lines",
            x, y, z, extractedZoneLines_.size());
        for (const auto& ezl : extractedZoneLines_) {
            LOG_DEBUG(MOD_MAP, "  -> {} (#{}) box: ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f})",
                ezl.destinationZone, ezl.zonePointIndex,
                ezl.minX, ezl.minY, ezl.minZ, ezl.maxX, ezl.maxY, ezl.maxZ);
        }
    }

    for (const auto& ezl : extractedZoneLines_) {
        // Check if position is inside the trigger box
        if (x >= ezl.minX && x <= ezl.maxX &&
            y >= ezl.minY && y <= ezl.maxY &&
            z >= ezl.minZ && z <= ezl.maxZ) {

            result.isZoneLine = true;
            result.targetZoneId = ezl.destinationZoneId;
            result.zonePointIndex = ezl.zonePointIndex;

            // Handle "same as current" marker (999999)
            result.targetX = (std::abs(ezl.destX) >= SAME_COORD_MARKER - 1.0f) ? currentX : ezl.destX;
            result.targetY = (std::abs(ezl.destY) >= SAME_COORD_MARKER - 1.0f) ? currentY : ezl.destY;
            result.targetZ = (std::abs(ezl.destZ) >= SAME_COORD_MARKER - 1.0f) ? currentZ : ezl.destZ;
            result.heading = ezl.destHeading;
            result.needsServerLookup = false;

            LOG_INFO(MOD_MAP, "Zone line triggered! Point #{} at ({},{},{}) -> {} (zone {}) at ({},{},{})",
                ezl.zonePointIndex, x, y, z, ezl.destinationZone, result.targetZoneId,
                result.targetX, result.targetY, result.targetZ);

            return result;
        }
    }

    return result;
}

ZoneLineResult ZoneLines::checkProximityZonePoints(float x, float y, float z,
                                                    float currentX, float currentY, float currentZ) const {
    ZoneLineResult result;
    result.isZoneLine = false;

    for (const auto& zp : proximityZonePoints_) {
        bool inZoneLine = false;

        if (zp.extendX || zp.extendY) {
            // Extended zone point - check if player is within the extended bounds
            // For extended axis: use zone geometry bounds
            // For non-extended axis: use proximity radius around source coordinate
            bool xInRange = false;
            bool yInRange = false;

            if (zp.extendX && hasZoneBounds_) {
                // X axis extends across entire zone
                xInRange = (x >= zoneMinX_ && x <= zoneMaxX_);
            } else {
                // X axis uses proximity radius
                float dx = x - zp.sourceX;
                xInRange = (std::abs(dx) <= DEFAULT_PROXIMITY_RADIUS);
            }

            if (zp.extendY && hasZoneBounds_) {
                // Y axis extends across entire zone
                yInRange = (y >= zoneMinY_ && y <= zoneMaxY_);
            } else {
                // Y axis uses proximity radius
                float dy = y - zp.sourceY;
                yInRange = (std::abs(dy) <= DEFAULT_PROXIMITY_RADIUS);
            }

            // Check Z height
            float dz = z - zp.sourceZ;
            bool zInRange = (dz >= -10.0f && dz <= DEFAULT_PROXIMITY_HEIGHT);

            inZoneLine = xInRange && yInRange && zInRange;

            if (inZoneLine) {
                LOG_INFO(MOD_MAP, "Extended zone line detected! Point {} (extendX={} extendY={}) pos=({:.1f},{:.1f},{:.1f}) -> zone {}",
                    zp.number, zp.extendX, zp.extendY, x, y, z, zp.targetZoneId);
            }
        } else {
            // Regular proximity-based zone point
            // Calculate horizontal distance (X/Y plane)
            float dx = x - zp.sourceX;
            float dy = y - zp.sourceY;
            float horizontalDist = std::sqrt(dx * dx + dy * dy);

            // Check if within proximity radius
            if (horizontalDist <= DEFAULT_PROXIMITY_RADIUS) {
                // Check Z height (more lenient)
                float dz = z - zp.sourceZ;
                if (dz >= -10.0f && dz <= DEFAULT_PROXIMITY_HEIGHT) {
                    inZoneLine = true;
                    LOG_INFO(MOD_MAP, "Proximity zone line detected! Point {} at distance {} (radius={}) -> zone {}",
                        zp.number, horizontalDist, DEFAULT_PROXIMITY_RADIUS, zp.targetZoneId);
                }
            }
        }

        if (inZoneLine) {
            result.isZoneLine = true;
            result.targetZoneId = zp.targetZoneId;

            // Handle "same as current" marker (999999)
            result.targetX = (std::abs(zp.targetX) >= SAME_COORD_MARKER - 1.0f) ? currentX : zp.targetX;
            result.targetY = (std::abs(zp.targetY) >= SAME_COORD_MARKER - 1.0f) ? currentY : zp.targetY;
            result.targetZ = (std::abs(zp.targetZ) >= SAME_COORD_MARKER - 1.0f) ? currentZ : zp.targetZ;
            result.heading = zp.heading;
            result.needsServerLookup = false;

            LOG_INFO(MOD_MAP, "Zone point {} triggered -> zone {} at ({}, {}, {})",
                zp.number, result.targetZoneId, result.targetX, result.targetY, result.targetZ);

            return result;
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
    // Prefer extracted zone lines count
    if (!extractedZoneLines_.empty()) {
        return extractedZoneLines_.size();
    }

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

std::vector<ZoneLineBoundingBox> ZoneLines::getZoneLineBoundingBoxes() const {
    std::vector<ZoneLineBoundingBox> boxes;

    // First priority: proximity zone points (verified correct coordinates from zone_points.json)
    // zone_points.json uses server coordinates: (server_x, server_y, server_z)
    // Renderer uses client display coordinates: (m_x, m_y, m_z) where m_x=server_y, m_y=server_x
    // So we swap X and Y for visualization
    for (const auto& zp : proximityZonePoints_) {
        ZoneLineBoundingBox box;
        box.isProximityBased = true;
        box.targetZoneId = zp.targetZoneId;
        box.zonePointIndex = zp.number;

        // Create a box around the source point
        // Swap coordinates for client display format: client_x = server_y, client_y = server_x
        float clientZ = zp.sourceZ;  // Z stays the same

        // For extended zone lines (999999 source coords), use zone geometry bounds
        // Zone bounds are also in server format, so we swap them too
        if (zp.extendY && hasZoneBounds_) {
            // Extended in server Y direction -> extended in client X direction
            // Server Y bounds -> Client X bounds (swap)
            box.minX = zoneMinY_;  // client minX = server minY
            box.maxX = zoneMaxY_;  // client maxX = server maxY
        } else {
            float clientX = zp.sourceY;  // m_x = server_y
            box.minX = clientX - DEFAULT_PROXIMITY_RADIUS;
            box.maxX = clientX + DEFAULT_PROXIMITY_RADIUS;
        }

        if (zp.extendX && hasZoneBounds_) {
            // Extended in server X direction -> extended in client Y direction
            // Server X bounds -> Client Y bounds (swap)
            box.minY = zoneMinX_;  // client minY = server minX
            box.maxY = zoneMaxX_;  // client maxY = server maxX
        } else {
            float clientY = zp.sourceX;  // m_y = server_x
            box.minY = clientY - DEFAULT_PROXIMITY_RADIUS;
            box.maxY = clientY + DEFAULT_PROXIMITY_RADIUS;
        }

        box.minZ = clientZ - 10.0f;
        box.maxZ = clientZ + DEFAULT_PROXIMITY_HEIGHT;

        boxes.push_back(box);

        if (zp.extendX || zp.extendY) {
            LOG_DEBUG(MOD_MAP, "Zone line box {}: EXTENDED #{} -> zone {} extendX={} extendY={} bounds X[{:.1f},{:.1f}] Y[{:.1f},{:.1f}]",
                boxes.size() - 1, zp.number, box.targetZoneId, zp.extendX, zp.extendY,
                box.minX, box.maxX, box.minY, box.maxY);
        } else {
            LOG_TRACE(MOD_MAP, "Zone line box {}: proximity #{} -> zone {} server({},{},{}) radius={}",
                boxes.size() - 1, zp.number, box.targetZoneId,
                zp.sourceX, zp.sourceY, zp.sourceZ, DEFAULT_PROXIMITY_RADIUS);
        }
    }

    // Second priority: pre-extracted zone lines (may have coordinate issues with BSP bounds)
    // Only use if no proximity zone points available
    if (boxes.empty()) {
        for (const auto& ezl : extractedZoneLines_) {
            ZoneLineBoundingBox box;
            box.isProximityBased = false;
            box.targetZoneId = ezl.destinationZoneId;
            box.zonePointIndex = ezl.zonePointIndex;
            box.minX = ezl.minX;
            box.minY = ezl.minY;
            box.minZ = ezl.minZ;
            box.maxX = ezl.maxX;
            box.maxY = ezl.maxY;
            box.maxZ = ezl.maxZ;

            boxes.push_back(box);
            LOG_TRACE(MOD_MAP, "Zone line box {}: extracted #{} -> zone {} bounds ({},{},{}) to ({},{},{})",
                boxes.size() - 1, ezl.zonePointIndex, box.targetZoneId,
                box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ);
        }
    }

    // Third priority: BSP-based computation (has coordinate system issues, for debugging only)
    if (boxes.empty() && bspTree_) {
        // Create initial bounds from zone geometry
        Graphics::BspBounds initialBounds(
            zoneMinX_, zoneMinY_, zoneMinZ_,
            zoneMaxX_, zoneMaxY_, zoneMaxZ_);

        for (size_t i = 0; i < bspTree_->regions.size(); ++i) {
            const auto& region = bspTree_->regions[i];
            bool isZoneLine = false;
            for (auto type : region->regionTypes) {
                if (type == Graphics::RegionType::Zoneline) {
                    isZoneLine = true;
                    break;
                }
            }

            if (!isZoneLine) continue;

            // Compute bounds for this region using BSP tree traversal
            Graphics::BspBounds regionBounds = bspTree_->computeRegionBounds(i, initialBounds);

            if (regionBounds.valid) {
                ZoneLineBoundingBox box;
                box.isProximityBased = false;
                box.minX = regionBounds.minX;
                box.minY = regionBounds.minY;
                box.minZ = regionBounds.minZ;
                box.maxX = regionBounds.maxX;
                box.maxY = regionBounds.maxY;
                box.maxZ = regionBounds.maxZ;

                if (region->zoneLineInfo) {
                    box.targetZoneId = region->zoneLineInfo->zoneId;
                    box.zonePointIndex = region->zoneLineInfo->zonePointIndex;
                }

                boxes.push_back(box);
                LOG_TRACE(MOD_MAP, "Zone line box {}: BSP region {} -> zone {} bounds ({},{},{}) to ({},{},{})",
                    boxes.size() - 1, i, box.targetZoneId,
                    box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ);
            } else {
                LOG_TRACE(MOD_MAP, "Zone line region {} has no valid bounds", i);
            }
        }
    }

    LOG_INFO(MOD_MAP, "Generated {} zone line bounding boxes", boxes.size());
    return boxes;
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
            // Handle "same as current" marker (999999) in zone point target coordinates
            result.targetX = (std::abs(zonePoint->targetX) >= SAME_COORD_MARKER - 1.0f) ? currentX : zonePoint->targetX;
            result.targetY = (std::abs(zonePoint->targetY) >= SAME_COORD_MARKER - 1.0f) ? currentY : zonePoint->targetY;
            result.targetZ = (std::abs(zonePoint->targetZ) >= SAME_COORD_MARKER - 1.0f) ? currentZ : zonePoint->targetZ;
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
