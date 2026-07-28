// Tool to find what coordinates reach zone lines in a BSP tree
// This helps determine the correct coordinate mapping between world and BSP space

#include "client/graphics/eq/wld_loader.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <cmath>
#include <json/json.h>

using namespace EQT::Graphics;

// Zone ID to name mapping
std::map<uint16_t, std::string> zoneIdToName = {
    {1, "qeynos"}, {2, "qeynos2"}, {3, "qrg"}, {4, "qeytoqrg"}, {5, "highpass"},
    {17, "blackburrow"}, {45, "qcat"},
};

std::string getZoneName(uint16_t zoneId) {
    auto it = zoneIdToName.find(zoneId);
    if (it != zoneIdToName.end()) return it->second;
    return "zone" + std::to_string(zoneId);
}

// Zone point data from JSON
struct ZonePointData {
    std::string sourceZone;
    uint32_t number;
    uint16_t targetZoneId;
    std::string targetZoneName;
};

// Load zone points from JSON
std::map<std::string, std::map<uint32_t, ZonePointData>> loadZonePoints(const std::string& jsonPath) {
    std::map<std::string, std::map<uint32_t, ZonePointData>> result;
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "Warning: Could not open zone points JSON: " << jsonPath << std::endl;
        return result;
    }
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        std::cerr << "Warning: Failed to parse zone points JSON: " << errors << std::endl;
        return result;
    }
    if (!root.isArray()) return result;
    for (const auto& entry : root) {
        ZonePointData zp;
        zp.sourceZone = entry["zone"].asString();
        zp.number = entry["number"].asUInt();
        zp.targetZoneId = static_cast<uint16_t>(entry["target_zone_id"].asUInt());
        zp.targetZoneName = getZoneName(zp.targetZoneId);
        result[zp.sourceZone][zp.number] = zp;
    }
    return result;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <eq_client_path> <zone_points_json> <zone_name>" << std::endl;
        std::cerr << "Example: " << argv[0] << " /path/to/EQ data/zone_points.json qeynos2" << std::endl;
        return 1;
    }

    std::string eqClientPath = argv[1];
    std::string zonePointsPath = argv[2];
    std::string zoneName = argv[3];

    // Load zone points data
    std::cout << "Loading zone points from: " << zonePointsPath << std::endl;
    auto zonePointsData = loadZonePoints(zonePointsPath);
    std::cout << "Loaded zone points for " << zonePointsData.size() << " zones" << std::endl;

    // Get zone points for this specific zone
    std::map<uint32_t, ZonePointData>* zonePoints = nullptr;
    auto zpIt = zonePointsData.find(zoneName);
    if (zpIt != zonePointsData.end()) {
        zonePoints = &zpIt->second;
        std::cout << "Zone " << zoneName << " has " << zonePoints->size() << " zone points" << std::endl;
    }

    // Load zone WLD
    WldLoader loader;
    std::string archivePath = eqClientPath + "/" + zoneName + ".s3d";
    std::string wldName = zoneName + ".wld";

    std::cout << "Loading zone: " << zoneName << std::endl;
    if (!loader.parseFromArchive(archivePath, wldName)) {
        std::cerr << "Failed to load zone archive: " << archivePath << std::endl;
        return 1;
    }

    auto bspTree = loader.getBspTree();
    if (!bspTree) {
        std::cerr << "No BSP tree found in zone" << std::endl;
        return 1;
    }

    std::cout << "BSP tree: " << bspTree->nodes.size() << " nodes, "
              << bspTree->regions.size() << " regions" << std::endl;

    // Get geometry bounds
    auto geometry = loader.getCombinedGeometry();
    if (!geometry) {
        std::cerr << "No geometry found" << std::endl;
        return 1;
    }

    std::cout << "\nWorld geometry bounds:" << std::endl;
    std::cout << "  X: [" << geometry->minX << ", " << geometry->maxX << "]" << std::endl;
    std::cout << "  Y: [" << geometry->minY << ", " << geometry->maxY << "]" << std::endl;
    std::cout << "  Z: [" << geometry->minZ << ", " << geometry->maxZ << "]" << std::endl;

    // Find all zone line regions and their targets
    std::map<uint16_t, std::vector<size_t>> zoneLinesByTarget;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        const auto& region = bspTree->regions[i];
        for (auto type : region->regionTypes) {
            if (type == RegionType::Zoneline && region->zoneLineInfo) {
                zoneLinesByTarget[region->zoneLineInfo->zoneId].push_back(i);
                break;
            }
        }
    }

    std::cout << "\nZone line regions by destination:" << std::endl;
    for (const auto& [zoneId, regions] : zoneLinesByTarget) {
        std::cout << "  -> " << getZoneName(zoneId) << " (id " << zoneId << "): "
                  << regions.size() << " region(s)" << std::endl;
    }

    // Sample the BSP tree to find coordinates that reach zone lines
    std::cout << "\n=== SEARCHING FOR ZONE LINE COORDINATES ===" << std::endl;

    // Search across the full world bounds
    float searchStep = 25.0f;
    float zTest = 0.0f;  // Test at z=0 first, then at other Z levels

    // Map: zone_point_index -> list of coordinates
    std::map<uint32_t, std::vector<std::tuple<float, float, float>>> foundCoordsByZonePoint;
    std::map<uint32_t, uint16_t> zonePointToZoneId;  // For tracking zone IDs

    // Search at a specific Z level that's likely to be ground level (slightly above minZ)
    std::vector<float> zLevels = {0.0f, -10.0f, -20.0f, -30.0f, 10.0f};

    for (float testZ : zLevels) {
        for (float testX = geometry->minX; testX <= geometry->maxX; testX += searchStep) {
            for (float testY = geometry->minY; testY <= geometry->maxY; testY += searchStep) {
                auto info = bspTree->checkZoneLine(testX, testY, testZ);
                if (info) {
                    // Found a zone line - record by zone point index
                    uint32_t zpIdx = info->zonePointIndex;
                    auto& coords = foundCoordsByZonePoint[zpIdx];
                    zonePointToZoneId[zpIdx] = info->zoneId;
                    // Only record first few samples per zone point
                    if (coords.size() < 3) {
                        coords.push_back({testX, testY, testZ});
                    }
                }
            }
        }
    }

    std::cout << "\nCoordinates that reach zone lines by zone_point_index:" << std::endl;
    for (const auto& [zpIdx, coords] : foundCoordsByZonePoint) {
        uint16_t zoneId = zonePointToZoneId[zpIdx];
        std::string destZone = getZoneName(zoneId);

        // Resolve zone point index to destination using zone_points.json
        if (zonePoints && zoneId == 0) {
            auto zpDataIt = zonePoints->find(zpIdx);
            if (zpDataIt != zonePoints->end()) {
                zoneId = zpDataIt->second.targetZoneId;
                destZone = zpDataIt->second.targetZoneName.empty() ? getZoneName(zoneId) : zpDataIt->second.targetZoneName;
            }
        }

        std::cout << "\n-> Zone Point #" << zpIdx << " -> " << destZone << " (zoneId=" << zoneId << "):" << std::endl;
        for (const auto& [cx, cy, cz] : coords) {
            std::cout << "   BSP coords: (" << cx << ", " << cy << ", " << cz << ")" << std::endl;
        }

        // Calculate bounds of all found coordinates
        if (!coords.empty()) {
            float minX = std::get<0>(coords[0]), maxX = minX;
            float minY = std::get<1>(coords[0]), maxY = minY;
            for (const auto& [cx, cy, cz] : coords) {
                minX = std::min(minX, cx);
                maxX = std::max(maxX, cx);
                minY = std::min(minY, cy);
                maxY = std::max(maxY, cy);
            }
            std::cout << "   Approximate bounds: X=[" << minX << ", " << maxX << "] Y=[" << minY << ", " << maxY << "]" << std::endl;
        }
    }

    // Now let's try to understand the coordinate mapping
    std::cout << "\n=== COORDINATE MAPPING ANALYSIS ===" << std::endl;

    // Known player position at qeynos2->qeytoqrg zone line (from user testing)
    float playerClientX = 1593.0f;  // m_x
    float playerClientY = 88.0f;    // m_y
    float playerZ = 3.35f;

    float playerServerX = playerClientY;  // server_x = m_y
    float playerServerY = playerClientX;  // server_y = m_x

    std::cout << "\nKnown player position at qeynos2->qeytoqrg zone line:" << std::endl;
    std::cout << "  Client display: (" << playerClientX << ", " << playerClientY << ", " << playerZ << ")" << std::endl;
    std::cout << "  Server coords:  (" << playerServerX << ", " << playerServerY << ", " << playerZ << ")" << std::endl;

    // Zone point #9 is qeytoqrg according to zone_lines.json
    auto qeyIt = foundCoordsByZonePoint.find(9);
    if (qeyIt != foundCoordsByZonePoint.end() && !qeyIt->second.empty()) {
        float bspX = std::get<0>(qeyIt->second[0]);
        float bspY = std::get<1>(qeyIt->second[0]);
        float bspZ = std::get<2>(qeyIt->second[0]);

        std::cout << "\nFound zone point #9 (qeytoqrg) at BSP: (" << bspX << ", " << bspY << ", " << bspZ << ")" << std::endl;

        // Calculate possible transforms
        std::cout << "\nPossible transforms to go from player position to BSP coords:" << std::endl;

        // Direct offset from client coords
        std::cout << "  From client (x,y): BSP = client + (" << (bspX - playerClientX) << ", " << (bspY - playerClientY) << ")" << std::endl;

        // Direct offset from server coords
        std::cout << "  From server (x,y): BSP = server + (" << (bspX - playerServerX) << ", " << (bspY - playerServerY) << ")" << std::endl;

        // Swapped with offset
        std::cout << "  From client swap: BSP = (y,x) + (" << (bspX - playerClientY) << ", " << (bspY - playerClientX) << ")" << std::endl;

        // Check if geometry center is involved
        float worldCenterX = (geometry->minX + geometry->maxX) / 2.0f;
        float worldCenterY = (geometry->minY + geometry->maxY) / 2.0f;
        std::cout << "\n  World center: (" << worldCenterX << ", " << worldCenterY << ")" << std::endl;

        // Check various center-relative transforms
        std::cout << "\nCenter-relative analysis:" << std::endl;
        std::cout << "  BSP relative to center: (" << (bspX - worldCenterX) << ", " << (bspY - worldCenterY) << ")" << std::endl;
        std::cout << "  Player client relative to center: (" << (playerClientX - worldCenterX) << ", " << (playerClientY - worldCenterY) << ")" << std::endl;

        // Try mirror transforms
        float mirrorX = 2*worldCenterX - playerClientX;
        float mirrorY = 2*worldCenterY - playerClientY;
        std::cout << "\n  Mirror of client around center: (" << mirrorX << ", " << mirrorY << ")" << std::endl;
        std::cout << "  Difference from BSP: (" << (bspX - mirrorX) << ", " << (bspY - mirrorY) << ")" << std::endl;

        // Try negation
        std::cout << "\n  -client: (" << -playerClientX << ", " << -playerClientY << ")" << std::endl;
        std::cout << "  Difference from BSP: (" << (bspX - (-playerClientX)) << ", " << (bspY - (-playerClientY)) << ")" << std::endl;
    } else {
        std::cout << "\nZone point #9 (qeytoqrg) not found in sample search." << std::endl;

        // List all found zone point indices
        std::cout << "Found zone point indices: ";
        for (const auto& [zpIdx, _] : foundCoordsByZonePoint) {
            std::cout << zpIdx << " ";
        }
        std::cout << std::endl;

        // Debug: check if BSP tree finds ANY regions at various Z levels
        std::cout << "\nTesting BSP region coverage:" << std::endl;
        int regionsFound = 0;
        for (float testX = geometry->minX; testX <= geometry->maxX; testX += 100.0f) {
            for (float testY = geometry->minY; testY <= geometry->maxY; testY += 100.0f) {
                auto region = bspTree->findRegionForPoint(testX, testY, 0.0f);
                if (region) {
                    regionsFound++;
                }
            }
        }
        std::cout << "  Found " << regionsFound << " regions at z=0" << std::endl;
    }

    // Final summary
    std::cout << "\n=== JSON TRIGGER BOX COMPARISON ===" << std::endl;
    std::cout << "From zone_lines.json, qeytoqrg trigger box:" << std::endl;
    std::cout << "  X: [178.93, 181.93]" << std::endl;
    std::cout << "  Y: [356.86, 391.84]" << std::endl;
    std::cout << "  Z: [-22.99, -14.99]" << std::endl;

    // Test if BSP finds zone lines at those exact coordinates
    std::cout << "\nTesting BSP at JSON trigger box center (180, 374, -18)..." << std::endl;
    auto testResult = bspTree->checkZoneLine(180.0f, 374.0f, -18.0f);
    if (testResult) {
        std::cout << "  FOUND zone line! zpIdx=" << testResult->zonePointIndex << " zoneId=" << testResult->zoneId << std::endl;
    } else {
        std::cout << "  NO zone line found at JSON trigger box center" << std::endl;
    }

    // Test zone_points.json source coordinates for zone point #9
    std::cout << "\n=== ZONE_POINTS.JSON SOURCE COORDINATE TEST ===" << std::endl;
    std::cout << "Zone point #9 source from zone_points.json: (73, 1272, 2.5)" << std::endl;

    // Test different coordinate interpretations
    struct TestCoord {
        float x, y, z;
        const char* desc;
    };
    TestCoord tests[] = {
        {73, 1272, 2.5, "direct (x,y,z)"},
        {1272, 73, 2.5, "swapped (y,x,z)"},
        {73, 1272, -18, "direct with BSP Z"},
        {1272, 73, -18, "swapped with BSP Z"},
        // Try with Z at ground level
        {73, 1272, 0, "direct at z=0"},
        {1272, 73, 0, "swapped at z=0"},
    };

    for (const auto& t : tests) {
        auto result = bspTree->checkZoneLine(t.x, t.y, t.z);
        if (result) {
            std::cout << "  " << t.desc << " (" << t.x << "," << t.y << "," << t.z << ") -> FOUND zpIdx=" << result->zonePointIndex << std::endl;
        } else {
            std::cout << "  " << t.desc << " (" << t.x << "," << t.y << "," << t.z << ") -> not found" << std::endl;
        }
    }

    // Print all zone_points.json entries for this zone
    if (zonePoints) {
        std::cout << "\n=== ALL ZONE POINTS FROM JSON ===" << std::endl;
        for (const auto& [zpIdx, zpData] : *zonePoints) {
            std::cout << "  #" << zpIdx << " -> " << zpData.targetZoneName << " (zone " << zpData.targetZoneId << ")" << std::endl;
        }
    }

    return 0;
}
