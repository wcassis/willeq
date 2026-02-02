#include "client/zone_lines.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <cmath>
#include <json/json.h>

namespace EQT {

// Special marker value in EQ meaning "use source coordinate"
static constexpr float SAME_COORD_MARKER = 999999.0f;

// Zone short name to zone ID mapping (EQ zone IDs are stable)
static const std::map<std::string, uint16_t> ZONE_NAME_TO_ID = {
    // Classic
    {"qeynos", 1}, {"qeynos2", 2}, {"qrg", 3}, {"qeytoqrg", 4}, {"highpass", 5},
    {"highkeep", 6}, {"freportn", 8}, {"freportw", 9}, {"freporte", 10},
    {"qey2hh1", 12}, {"northkarana", 13}, {"southkarana", 14}, {"eastkarana", 15},
    {"beholder", 16}, {"blackburrow", 17}, {"paw", 18}, {"rivervale", 19},
    {"kithicor", 20}, {"commons", 21}, {"ecommons", 22}, {"erudnint", 23},
    {"erudnext", 24}, {"nektulos", 25}, {"lavastorm", 27}, {"halas", 29},
    {"everfrost", 30}, {"soldunga", 31}, {"soldungb", 32}, {"misty", 33},
    {"nro", 34}, {"sro", 35}, {"befallen", 36}, {"oasis", 37}, {"tox", 38},
    {"hole", 39}, {"neriaka", 40}, {"neriakb", 41}, {"neriakc", 42},
    {"najena", 44}, {"qcat", 45}, {"innothule", 46}, {"feerrott", 47},
    {"cazicthule", 48}, {"oggok", 49}, {"rathemtn", 50}, {"lakerathe", 51},
    {"grobb", 52}, {"gfaydark", 54}, {"akanon", 55}, {"steamfont", 56},
    {"lfaydark", 57}, {"crushbone", 58}, {"mistmoore", 59}, {"kaladima", 60},
    {"kaladimb", 61}, {"felwithea", 62}, {"felwitheb", 63}, {"unrest", 64},
    {"kedge", 65}, {"guktop", 66}, {"gukbottom", 67}, {"butcher", 68},
    {"oot", 69}, {"cauldron", 70}, {"airplane", 71}, {"fearplane", 72},
    {"permafrost", 73}, {"kerraridge", 74}, {"paineel", 75}, {"hateplane", 76},
    {"arena", 77}, {"erudsxing", 98}, {"soltemple", 80}, {"runnyeye", 11},
    // Kunark
    {"fieldofbone", 78}, {"warslikswood", 79}, {"droga", 81}, {"cabwest", 82},
    {"cabeast", 83}, {"swampofnohope", 85}, {"firiona", 84}, {"lakeofillomen", 86},
    {"dreadlands", 87}, {"burningwood", 88}, {"kaesora", 89}, {"sebilis", 90},
    {"citymist", 91}, {"skyfire", 92}, {"frontiermtns", 93}, {"overthere", 94},
    {"emeraldjungle", 95}, {"trakanon", 96}, {"timorous", 97}, {"kurn", 99},
    {"charasis", 100}, {"chardok", 101}, {"dalnir", 102}, {"veeshan", 103},
    // Velious
    {"eastwastes", 104}, {"cobaltscar", 105}, {"wakening", 106}, {"necropolis", 107},
    {"velketor", 108}, {"sirens", 109}, {"iceclad", 110}, {"growthplane", 111},
    {"sleeper", 112}, {"westwastes", 113}, {"crystal", 114}, {"frozenshadow", 117},
    {"thurgadina", 115}, {"thurgadinb", 116}, {"skyshrine", 118}, {"templeveeshan", 119},
    {"kael", 120}, {"greatdivide", 121}, {"mischiefplane", 126},
    // PoP and beyond
    {"poknowledge", 202}, {"potranquility", 203}, {"ponightmare", 204},
    {"podisease", 205}, {"poinnovation", 206}, {"povalor", 208}, {"pojustice", 209},
    {"postorms", 210}, {"hohonora", 211}, {"hohonorb", 212}, {"potorment", 213},
    {"pofireA", 215}, {"potimea", 217}, {"poairA", 218}, {"powater", 219},
    {"poearthA", 220}, {"potactics", 221}, {"codecay", 200},
};

// Get zone ID from short name (returns 0 if not found)
static uint16_t getZoneIdFromName(const std::string& zoneName) {
    auto it = ZONE_NAME_TO_ID.find(zoneName);
    if (it != ZONE_NAME_TO_ID.end()) {
        return it->second;
    }
    LOG_WARN(MOD_MAP, "Unknown zone name '{}', cannot map to zone ID", zoneName);
    return 0;
}

bool ZoneLines::loadFromZone(const std::string& zoneName, const std::string& eqClientPath) {
    clear();

    bool loadedAny = false;

    // First, try to load from zone-specific file (preferred - manually curated data)
    std::vector<std::string> zoneSpecificPaths = {
        "data/zone_lines/" + zoneName + ".json",                      // Relative to CWD (project root)
        "../data/zone_lines/" + zoneName + ".json",                   // Relative to CWD (build directory)
        eqClientPath + "/../data/zone_lines/" + zoneName + ".json"   // Relative to EQ client path
    };
    for (const auto& jsonPath : zoneSpecificPaths) {
        if (loadFromZoneSpecificJson(zoneName, jsonPath)) {
            LOG_INFO(MOD_MAP, "Loaded {} zone lines for {} from zone-specific file {}",
                     extractedZoneLines_.size(), zoneName, jsonPath);
            loadedAny = true;
            break;
        }
    }

    // Fall back to old zone_lines.json format if zone-specific file not found
    if (!loadedAny) {
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
    }

    // TEMPORARILY DISABLED: BSP tree loading
    // TODO: Re-enable when coordinate issues are resolved
#if 0
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
#endif

    if (!loadedAny) {
        LOG_WARN(MOD_MAP, "No zone line data found for '{}' - checked zone_lines.json", zoneName);
    }

    return loadedAny;
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

    // Load coordinate transform if available
    // BSP coords -> world coords: world_x = BSP_y + offset_x, world_y = BSP_x + offset_y
    float offsetX = 0.0f, offsetY = 0.0f, offsetZ = 0.0f;
    bool hasTransform = false;
    if (zoneData.isMember("coordinate_transform")) {
        const Json::Value& transform = zoneData["coordinate_transform"];
        offsetX = transform["offset_x"].asFloat();
        offsetY = transform["offset_y"].asFloat();
        offsetZ = transform.get("offset_z", 0.0f).asFloat();
        hasTransform = true;
        LOG_DEBUG(MOD_MAP, "Zone {} coordinate transform: offset=({}, {}, {})", zoneName, offsetX, offsetY, offsetZ);
    }

    for (const auto& zl : zoneLines) {
        ExtractedZoneLine ezl;
        ezl.zonePointIndex = zl["zone_point_index"].asUInt();
        ezl.destinationZone = zl["destination_zone"].asString();
        ezl.destinationZoneId = static_cast<uint16_t>(zl["destination_zone_id"].asUInt());

        if (zl.isMember("trigger_box")) {
            const Json::Value& box = zl["trigger_box"];
            float bspMinX = box["min_x"].asFloat();
            float bspMinY = box["min_y"].asFloat();
            float bspMinZ = box["min_z"].asFloat();
            float bspMaxX = box["max_x"].asFloat();
            float bspMaxY = box["max_y"].asFloat();
            float bspMaxZ = box["max_z"].asFloat();

            if (hasTransform) {
                // Transform BSP coords to world coords:
                // world_x = BSP_y + offset_x (axes swapped)
                // world_y = BSP_x + offset_y (axes swapped)
                // world_z = BSP_z + offset_z
                ezl.minX = bspMinY + offsetX;
                ezl.maxX = bspMaxY + offsetX;
                ezl.minY = bspMinX + offsetY;
                ezl.maxY = bspMaxX + offsetY;
                ezl.minZ = bspMinZ + offsetZ;
                ezl.maxZ = bspMaxZ + offsetZ;
            } else {
                // No transform - use raw coords
                ezl.minX = bspMinX;
                ezl.minY = bspMinY;
                ezl.minZ = bspMinZ;
                ezl.maxX = bspMaxX;
                ezl.maxY = bspMaxY;
                ezl.maxZ = bspMaxZ;
            }
        }

        if (zl.isMember("destination")) {
            const Json::Value& dest = zl["destination"];
            ezl.destX = dest["x"].asFloat();
            ezl.destY = dest["y"].asFloat();
            ezl.destZ = dest["z"].asFloat();
            ezl.destHeading = dest["heading"].asFloat();
        }

        extractedZoneLines_.push_back(ezl);

        LOG_DEBUG(MOD_MAP, "Loaded zone line: #{} -> {} (id {}) world box ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f})",
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

bool ZoneLines::loadFromZoneSpecificJson(const std::string& zoneName, const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOG_TRACE(MOD_MAP, "Could not open zone-specific JSON: {}", jsonPath);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;

    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_MAP, "Failed to parse zone-specific JSON: {}", errors);
        return false;
    }

    if (!root.isObject()) {
        LOG_ERROR(MOD_MAP, "Zone-specific JSON is not an object");
        return false;
    }

    // Verify this is the correct zone
    std::string fileZone = root.get("zone", "").asString();
    if (!fileZone.empty() && fileZone != zoneName) {
        LOG_TRACE(MOD_MAP, "Zone mismatch: file is for '{}', wanted '{}'", fileZone, zoneName);
        return false;
    }

    if (!root.isMember("zone_lines") || !root["zone_lines"].isArray()) {
        LOG_TRACE(MOD_MAP, "Zone-specific file has no zone_lines array");
        return false;
    }

    extractedZoneLines_.clear();
    const Json::Value& zoneLines = root["zone_lines"];

    for (const auto& zl : zoneLines) {
        // Skip entries without bounds (not yet mapped in editor)
        if (!zl.isMember("bounds")) {
            LOG_TRACE(MOD_MAP, "Skipping zone line to '{}' - no bounds defined",
                zl.get("target_zone", "unknown").asString());
            continue;
        }

        ExtractedZoneLine ezl;
        ezl.destinationZone = zl.get("target_zone", "").asString();
        ezl.destinationZoneId = getZoneIdFromName(ezl.destinationZone);

        if (ezl.destinationZoneId == 0) {
            LOG_WARN(MOD_MAP, "Unknown destination zone '{}', skipping", ezl.destinationZone);
            continue;
        }

        // Load bounds directly - JSON is already in client coordinate format
        const Json::Value& bounds = zl["bounds"];
        ezl.minX = bounds["min_x"].asFloat();
        ezl.maxX = bounds["max_x"].asFloat();
        ezl.minY = bounds["min_y"].asFloat();
        ezl.maxY = bounds["max_y"].asFloat();
        ezl.minZ = bounds["min_z"].asFloat();
        ezl.maxZ = bounds["max_z"].asFloat();

        // Parse landing type to determine destination coordinates
        std::string landing = zl.get("landing", "fixed").asString();

        // For now, we use SAME_COORD_MARKER for preserved coordinates
        // The actual destination will be resolved by the server or at zone time
        if (landing == "preserve_x") {
            ezl.destX = SAME_COORD_MARKER;
            ezl.destY = 0.0f;
            ezl.destZ = 0.0f;
        } else if (landing == "preserve_y") {
            ezl.destX = 0.0f;
            ezl.destY = SAME_COORD_MARKER;
            ezl.destZ = 0.0f;
        } else if (landing == "preserve_z") {
            ezl.destX = 0.0f;
            ezl.destY = 0.0f;
            ezl.destZ = SAME_COORD_MARKER;
        } else {
            // "fixed" landing - use trigger center as placeholder
            // Server will provide actual destination
            ezl.destX = 0.0f;
            ezl.destY = 0.0f;
            ezl.destZ = 0.0f;
        }
        ezl.destHeading = 0.0f;

        // Zone point index (not always available in this format)
        ezl.zonePointIndex = 0;

        extractedZoneLines_.push_back(ezl);

        LOG_DEBUG(MOD_MAP, "Loaded zone line: {} (id {}) client box ({:.1f},{:.1f},{:.1f}) to ({:.1f},{:.1f},{:.1f})",
            ezl.destinationZone, ezl.destinationZoneId,
            ezl.minX, ezl.minY, ezl.minZ, ezl.maxX, ezl.maxY, ezl.maxZ);
    }

    LOG_INFO(MOD_MAP, "Loaded {} zone lines from zone-specific file", extractedZoneLines_.size());
    return !extractedZoneLines_.empty();
}

void ZoneLines::debugTestCoordinateMappings(float serverX, float serverY, float serverZ) const {
    // BSP-based coordinate mapping test is disabled - only bounding boxes from zone_lines.json are used
    (void)serverX;
    (void)serverY;
    (void)serverZ;
}

void ZoneLines::clear() {
    extractedZoneLines_.clear();
    bspTree_.reset();
    serverZonePoints_.clear();
    wldZonePoints_.clear();
}

void ZoneLines::expandZoneLinesToGeometry(HCMap* collisionMap) {
    if (!collisionMap || !collisionMap->IsLoaded()) {
        LOG_INFO(MOD_MAP, "No collision map available for zone line expansion");
        return;
    }

    LOG_INFO(MOD_MAP, "Expanding {} zone lines to fill passages...", extractedZoneLines_.size());

    const float MAX_EXPANSION = 200.0f;  // Maximum expansion distance
    const float RAY_STEP = 2.0f;         // Step size for expansion
    const float WALL_MARGIN = 1.0f;      // Margin from wall

    for (auto& ezl : extractedZoneLines_) {
        // Calculate center of current box
        float centerX = (ezl.minX + ezl.maxX) / 2.0f;
        float centerY = (ezl.minY + ezl.maxY) / 2.0f;
        float centerZ = (ezl.minZ + ezl.maxZ) / 2.0f;

        // Determine which axis is the "depth" axis (thin dimension = direction of travel)
        float widthX = ezl.maxX - ezl.minX;
        float widthY = ezl.maxY - ezl.minY;

        // If both dimensions are small (seed box), expand both
        // If one dimension is large (continuous), only expand the small one
        bool expandX = widthX < 50.0f;
        bool expandY = widthY < 50.0f;

        // Convert display coords to server coords for HCMap
        // HCMap uses server coords: server_x = display_y, server_y = display_x
        float serverCenterX = centerY;  // display m_y -> server x
        float serverCenterY = centerX;  // display m_x -> server y
        float serverCenterZ = centerZ;

        LOG_DEBUG(MOD_MAP, "Zone line #{} center: display({:.1f},{:.1f},{:.1f}) server({:.1f},{:.1f},{:.1f}) expandX={} expandY={}",
            ezl.zonePointIndex, centerX, centerY, centerZ, serverCenterX, serverCenterY, serverCenterZ, expandX, expandY);

        if (expandX) {
            // Expand in +X direction (display m_x)
            // display m_x = server_y, so expanding +X means checking +server_y
            float newMaxX = ezl.maxX;
            for (float dist = RAY_STEP; dist <= MAX_EXPANSION; dist += RAY_STEP) {
                float testDisplayX = centerX + dist;
                // Cast ray from center to test position (horizontal)
                glm::vec3 start(serverCenterX, serverCenterY, serverCenterZ);
                glm::vec3 end(serverCenterX, serverCenterY + dist, serverCenterZ);  // +server_y = +display_x

                if (!collisionMap->CheckLOS(start, end)) {
                    // Hit a wall, stop here
                    newMaxX = centerX + dist - WALL_MARGIN;
                    LOG_TRACE(MOD_MAP, "  +X hit wall at dist={:.1f}, newMaxX={:.1f}", dist, newMaxX);
                    break;
                }
                newMaxX = testDisplayX;
            }
            if (newMaxX > ezl.maxX) {
                ezl.maxX = newMaxX;
            }

            // Expand in -X direction (display m_x)
            float newMinX = ezl.minX;
            for (float dist = RAY_STEP; dist <= MAX_EXPANSION; dist += RAY_STEP) {
                float testDisplayX = centerX - dist;
                glm::vec3 start(serverCenterX, serverCenterY, serverCenterZ);
                glm::vec3 end(serverCenterX, serverCenterY - dist, serverCenterZ);  // -server_y = -display_x

                if (!collisionMap->CheckLOS(start, end)) {
                    newMinX = centerX - dist + WALL_MARGIN;
                    LOG_TRACE(MOD_MAP, "  -X hit wall at dist={:.1f}, newMinX={:.1f}", dist, newMinX);
                    break;
                }
                newMinX = testDisplayX;
            }
            if (newMinX < ezl.minX) {
                ezl.minX = newMinX;
            }
        }

        if (expandY) {
            // Expand in +Y direction (display m_y)
            // display m_y = server_x, so expanding +Y means checking +server_x
            float newMaxY = ezl.maxY;
            for (float dist = RAY_STEP; dist <= MAX_EXPANSION; dist += RAY_STEP) {
                float testDisplayY = centerY + dist;
                glm::vec3 start(serverCenterX, serverCenterY, serverCenterZ);
                glm::vec3 end(serverCenterX + dist, serverCenterY, serverCenterZ);  // +server_x = +display_y

                if (!collisionMap->CheckLOS(start, end)) {
                    newMaxY = centerY + dist - WALL_MARGIN;
                    LOG_TRACE(MOD_MAP, "  +Y hit wall at dist={:.1f}, newMaxY={:.1f}", dist, newMaxY);
                    break;
                }
                newMaxY = testDisplayY;
            }
            if (newMaxY > ezl.maxY) {
                ezl.maxY = newMaxY;
            }

            // Expand in -Y direction (display m_y)
            float newMinY = ezl.minY;
            for (float dist = RAY_STEP; dist <= MAX_EXPANSION; dist += RAY_STEP) {
                float testDisplayY = centerY - dist;
                glm::vec3 start(serverCenterX, serverCenterY, serverCenterZ);
                glm::vec3 end(serverCenterX - dist, serverCenterY, serverCenterZ);  // -server_x = -display_y

                if (!collisionMap->CheckLOS(start, end)) {
                    newMinY = centerY - dist + WALL_MARGIN;
                    LOG_TRACE(MOD_MAP, "  -Y hit wall at dist={:.1f}, newMinY={:.1f}", dist, newMinY);
                    break;
                }
                newMinY = testDisplayY;
            }
            if (newMinY < ezl.minY) {
                ezl.minY = newMinY;
            }
        }

        LOG_DEBUG(MOD_MAP, "Expanded zone line #{} -> {} to box ({:.1f},{:.1f}) - ({:.1f},{:.1f})",
            ezl.zonePointIndex, ezl.destinationZone,
            ezl.minX, ezl.minY, ezl.maxX, ezl.maxY);
    }

    LOG_INFO(MOD_MAP, "Zone line expansion complete");
}

bool ZoneLines::hasBspZoneLines() const {
    // BSP-based zone line detection is disabled - only bounding boxes from zone_lines.json are used
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

    // TEMPORARILY DISABLED: BSP tree check
    // TODO: Re-enable when coordinate issues are resolved
#if 0
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
#endif

    // Check pre-extracted zone lines from zone_lines.json
    // zone_lines.json uses display coordinates (m_x, m_y, m_z) format
    if (!extractedZoneLines_.empty()) {
        result = checkExtractedZoneLines(x, y, z, currentX, currentY, currentZ);
        if (result.isZoneLine) {
            LOG_INFO(MOD_MAP, "Extracted zone line matched at display coords ({:.1f},{:.1f},{:.1f})", x, y, z);
            return result;
        }
    }

    if (callCount % 100 == 0 && extractedZoneLines_.empty()) {
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

    return std::nullopt;
}

size_t ZoneLines::getZoneLineCount() const {
    // Only count extracted zone lines from zone_lines.json (BSP detection disabled)
    return extractedZoneLines_.size();
}

std::vector<ZoneLineBoundingBox> ZoneLines::getZoneLineBoundingBoxes() const {
    std::vector<ZoneLineBoundingBox> boxes;

    // Use pre-extracted zone lines from zone_lines.json
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

    // TEMPORARILY DISABLED: BSP-based computation
    // TODO: Re-enable when coordinate issues are resolved
#if 0
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
#endif

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
