// Tool to extract zone line bounding boxes from EQ zone files
// Usage: extract_zone_lines <eq_client_path> <zone_points_json> <zone_name> [zone_name2] ...
// Or:    extract_zone_lines <eq_client_path> <zone_points_json> --all

#include "client/graphics/eq/wld_loader.h"
#include <iostream>
#include <fstream>
#include <json/json.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>
#include <limits>

using namespace EQT::Graphics;

// Zone ID to short name mapping (Classic + Kunark + Velious)
std::map<uint16_t, std::string> zoneIdToName = {
    {1, "qeynos"}, {2, "qeynos2"}, {3, "qrg"}, {4, "qeytoqrg"}, {5, "highpass"},
    {6, "highkeep"}, {8, "freportn"}, {9, "freportw"}, {10, "freporte"},
    {11, "runnyeye"}, {12, "qey2hh1"}, {13, "northkarana"}, {14, "southkarana"},
    {15, "eastkarana"}, {16, "beholder"}, {17, "blackburrow"}, {18, "paw"},
    {19, "rivervale"}, {20, "kithicor"}, {21, "commons"}, {22, "ecommons"},
    {23, "erudnint"}, {24, "erudnext"}, {25, "nektulos"}, {26, "cshome"},
    {27, "lavastorm"}, {28, "nektropos"}, {29, "halas"}, {30, "everfrost"},
    {31, "soldunga"}, {32, "soldungb"}, {33, "misty"}, {34, "nro"},
    {35, "sro"}, {36, "befallen"}, {37, "oasis"}, {38, "tox"},
    {39, "hole"}, {40, "neriaka"}, {41, "neriakb"}, {42, "neriakc"},
    {43, "neriakd"}, {44, "najena"}, {45, "qcat"}, {46, "innothule"},
    {47, "feerrott"}, {48, "cazicthule"}, {49, "oggok"}, {50, "rathemtn"},
    {51, "lakerathe"}, {52, "grobb"}, {53, "aviak"}, {54, "gfaydark"},
    {55, "akanon"}, {56, "steamfont"}, {57, "lfaydark"}, {58, "crushbone"},
    {59, "mistmoore"}, {60, "kaladima"}, {61, "felwithea"}, {62, "felwitheb"},
    {63, "unrest"}, {64, "kedge"}, {65, "guktop"}, {66, "gukbottom"},
    {67, "kaladimb"}, {68, "butcher"}, {69, "oot"}, {70, "cauldron"},
    {71, "airplane"}, {72, "fearplane"}, {73, "permafrost"}, {74, "kerraridge"},
    {75, "paineel"}, {76, "hateplane"}, {77, "arena"}, {78, "fieldofbone"},
    {79, "warslikswood"}, {80, "soltemple"}, {81, "droga"}, {82, "cabwest"},
    {83, "swampofnohope"}, {84, "firiona"}, {85, "lakeofillomen"}, {86, "dreadlands"},
    {87, "burningwood"}, {88, "kaesora"}, {89, "sebilis"}, {90, "citymist"},
    {91, "skyfire"}, {92, "frontiermtns"}, {93, "overthere"}, {94, "emeraldjungle"},
    {95, "trakanon"}, {96, "timorous"}, {97, "kurn"}, {98, "erudsxing"},
    {100, "stonebrunt"}, {101, "warrens"}, {102, "karnor"}, {103, "chardok"},
    {104, "dalnir"}, {105, "charasis"}, {106, "cabeast"}, {107, "nurga"},
    {108, "veeshan"}, {109, "veksar"}, {110, "iceclad"}, {111, "frozenshadow"},
    {112, "velketor"}, {113, "kael"}, {114, "skyshrine"}, {115, "thurgadina"},
    {116, "eastwastes"}, {117, "cobaltscar"}, {118, "greatdivide"}, {119, "wakening"},
    {120, "westwastes"}, {121, "crystal"}, {123, "necropolis"}, {124, "templeveeshan"},
    {125, "sirens"}, {126, "mischiefplane"}, {127, "growthplane"}, {128, "sleeper"},
    {129, "thurgadinb"}, {130, "erudsxing2"},
};

std::string getZoneName(uint16_t zoneId) {
    auto it = zoneIdToName.find(zoneId);
    if (it != zoneIdToName.end()) {
        return it->second;
    }
    return "";
}

// Structure to hold zone point data from JSON (for resolving reference-type zone lines)
struct ZonePointData {
    std::string sourceZone;
    uint32_t number;
    uint16_t targetZoneId;
    float targetX, targetY, targetZ;
    float targetHeading;
};

// Load zone points from JSON file
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

    if (!root.isArray()) {
        std::cerr << "Warning: Zone points JSON is not an array" << std::endl;
        return result;
    }

    for (const auto& entry : root) {
        ZonePointData zp;
        zp.sourceZone = entry["zone"].asString();
        zp.number = entry["number"].asUInt();
        zp.targetZoneId = static_cast<uint16_t>(entry["target_zone_id"].asUInt());
        zp.targetX = entry["target_x"].asFloat();
        zp.targetY = entry["target_y"].asFloat();
        zp.targetZ = entry["target_z"].asFloat();
        zp.targetHeading = entry["target_heading"].asFloat();

        result[zp.sourceZone][zp.number] = zp;
    }

    return result;
}

// Structure for merged zone line data
struct MergedZoneLine {
    uint32_t zonePointIndex;
    std::string destinationZone;
    uint16_t destinationZoneId;
    float destX, destY, destZ, destHeading;
    bool isReferenceType = false;

    // Merged bounds from all BSP regions
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    bool hasBounds = false;

    int bspRegionCount = 0;

    void mergeBounds(float bMinX, float bMinY, float bMinZ, float bMaxX, float bMaxY, float bMaxZ) {
        if (!hasBounds) {
            minX = bMinX; minY = bMinY; minZ = bMinZ;
            maxX = bMaxX; maxY = bMaxY; maxZ = bMaxZ;
            hasBounds = true;
        } else {
            minX = std::min(minX, bMinX);
            minY = std::min(minY, bMinY);
            minZ = std::min(minZ, bMinZ);
            maxX = std::max(maxX, bMaxX);
            maxY = std::max(maxY, bMaxY);
            maxZ = std::max(maxZ, bMaxZ);
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <eq_client_path> <zone_points_json> <zone_name> [zone_name2] ..." << std::endl;
        std::cerr << "   Or: " << argv[0] << " <eq_client_path> <zone_points_json> --all" << std::endl;
        std::cerr << "Example: " << argv[0] << " /path/to/EQ data/zone_points.json qeynos2 qeytoqrg" << std::endl;
        return 1;
    }

    std::string eqClientPath = argv[1];
    std::string zonePointsPath = argv[2];
    std::vector<std::string> zoneNames;

    // Build set of valid Classic/Kunark/Velious zone names
    std::set<std::string> validZoneNames;
    for (const auto& [id, name] : zoneIdToName) {
        validZoneNames.insert(name);
    }

    // Check for --all flag
    if (std::string(argv[3]) == "--all") {
        // Find all .s3d files in the EQ client path that are Classic/Kunark/Velious zones
        for (const auto& entry : std::filesystem::directory_iterator(eqClientPath)) {
            if (entry.path().extension() == ".s3d") {
                std::string filename = entry.path().stem().string();
                // Skip character/object archives and other non-zone files
                if (filename.find("_chr") != std::string::npos ||
                    filename.find("_obj") != std::string::npos ||
                    filename.find("global") != std::string::npos ||
                    filename.find("chequip") != std::string::npos) {
                    continue;
                }
                // Only include Classic/Kunark/Velious zones
                if (validZoneNames.find(filename) != validZoneNames.end()) {
                    zoneNames.push_back(filename);
                }
            }
        }
        std::sort(zoneNames.begin(), zoneNames.end());
        std::cout << "Found " << zoneNames.size() << " Classic/Kunark/Velious zone files" << std::endl;
    } else {
        for (int i = 3; i < argc; ++i) {
            zoneNames.push_back(argv[i]);
        }
    }

    // Load zone points data for resolving reference-type zone lines
    std::cout << "Loading zone points from: " << zonePointsPath << std::endl;
    auto zonePointsData = loadZonePoints(zonePointsPath);
    std::cout << "Loaded zone points for " << zonePointsData.size() << " zones" << std::endl;

    Json::Value root(Json::objectValue);
    int zonesProcessed = 0;
    int zonesWithZoneLines = 0;

    for (const auto& zoneName : zoneNames) {
        WldLoader loader;
        std::string archivePath = eqClientPath + "/" + zoneName + ".s3d";
        std::string wldName = zoneName + ".wld";

        if (!loader.parseFromArchive(archivePath, wldName)) {
            // Silently skip files that aren't valid zone archives
            continue;
        }

        auto bspTree = loader.getBspTree();
        if (!bspTree) {
            continue;
        }

        zonesProcessed++;
        std::cout << "\n=== Processing zone: " << zoneName << " ===" << std::endl;
        std::cout << "  BSP tree: " << bspTree->nodes.size() << " nodes, "
                  << bspTree->regions.size() << " regions" << std::endl;

        // Get zone geometry bounds (world coordinates)
        auto geometry = loader.getCombinedGeometry();
        float geoMinX = -10000, geoMinY = -10000, geoMinZ = -1000;
        float geoMaxX = 10000, geoMaxY = 10000, geoMaxZ = 1000;
        if (geometry) {
            geoMinX = geometry->minX;
            geoMinY = geometry->minY;
            geoMinZ = geometry->minZ;
            geoMaxX = geometry->maxX;
            geoMaxY = geometry->maxY;
            geoMaxZ = geometry->maxZ;
        }

        // Compute the full BSP bounds by finding bounds of ALL regions
        // This helps us determine the coordinate transform between BSP and world space
        float bspFullMinX = std::numeric_limits<float>::max();
        float bspFullMinY = std::numeric_limits<float>::max();
        float bspFullMaxX = std::numeric_limits<float>::lowest();
        float bspFullMaxY = std::numeric_limits<float>::lowest();

        BspBounds initialBounds(geoMinX, geoMinY, geoMinZ, geoMaxX, geoMaxY, geoMaxZ);

        // First pass: compute bounds of all BSP regions to find coordinate transform
        for (size_t i = 0; i < bspTree->regions.size(); ++i) {
            BspBounds bounds = bspTree->computeRegionBounds(i, initialBounds);
            if (bounds.valid) {
                bspFullMinX = std::min(bspFullMinX, bounds.minX);
                bspFullMinY = std::min(bspFullMinY, bounds.minY);
                bspFullMaxX = std::max(bspFullMaxX, bounds.maxX);
                bspFullMaxY = std::max(bspFullMaxY, bounds.maxY);
            }
        }

        // Compute BSP center and world center
        float bspCenterX = (bspFullMinX + bspFullMaxX) / 2.0f;
        float bspCenterY = (bspFullMinY + bspFullMaxY) / 2.0f;
        float worldCenterX = (geoMinX + geoMaxX) / 2.0f;
        float worldCenterY = (geoMinY + geoMaxY) / 2.0f;

        std::cout << "  World bounds: x=[" << geoMinX << ", " << geoMaxX << "] y=[" << geoMinY << ", " << geoMaxY << "]" << std::endl;
        std::cout << "  BSP full bounds: x=[" << bspFullMinX << ", " << bspFullMaxX << "] y=[" << bspFullMinY << ", " << bspFullMaxY << "]" << std::endl;
        std::cout << "  World center: (" << worldCenterX << ", " << worldCenterY << ")" << std::endl;
        std::cout << "  BSP center: (" << bspCenterX << ", " << bspCenterY << ")" << std::endl;

        // The BSP tree uses a coordinate system that needs to be transformed to world coords
        // Based on analysis: world_x = BSP_y + offsetX, world_y = BSP_x + offsetY (swap + offset)
        // The offsets align the BSP center with the world center after the swap
        float transformOffsetX = worldCenterX - bspCenterY;  // For world_x = BSP_y + offset
        float transformOffsetY = worldCenterY - bspCenterX;  // For world_y = BSP_x + offset
        float transformOffsetZ = 0.0f;  // Z usually doesn't need transform

        std::cout << "  Transform offsets: (" << transformOffsetX << ", " << transformOffsetY << ", " << transformOffsetZ << ")" << std::endl;

        // Get zone points for this zone (for resolving reference types)
        std::map<uint32_t, ZonePointData>* zonePoints = nullptr;
        auto zpIt = zonePointsData.find(zoneName);
        if (zpIt != zonePointsData.end()) {
            zonePoints = &zpIt->second;
        }

        // Collect and merge zone lines by zone_point_index
        std::map<uint32_t, MergedZoneLine> mergedZoneLines;
        int totalRegions = 0;
        int regionsWithBounds = 0;

        for (size_t i = 0; i < bspTree->regions.size(); ++i) {
            const auto& region = bspTree->regions[i];
            bool isZoneLine = false;
            for (auto type : region->regionTypes) {
                if (type == RegionType::Zoneline) {
                    isZoneLine = true;
                    break;
                }
            }

            if (!isZoneLine) continue;
            totalRegions++;

            uint32_t zpIndex = 0;
            uint16_t targetZoneId = 0;
            float destX = 0, destY = 0, destZ = 0, destHeading = 0;
            bool isReferenceType = false;

            if (region->zoneLineInfo) {
                zpIndex = region->zoneLineInfo->zonePointIndex;
                targetZoneId = region->zoneLineInfo->zoneId;
                destX = region->zoneLineInfo->x;
                destY = region->zoneLineInfo->y;
                destZ = region->zoneLineInfo->z;
                destHeading = region->zoneLineInfo->heading;
                isReferenceType = (region->zoneLineInfo->type == ZoneLineType::Reference);
            }

            // Get or create merged entry
            auto& merged = mergedZoneLines[zpIndex];
            merged.zonePointIndex = zpIndex;
            merged.bspRegionCount++;
            merged.isReferenceType = isReferenceType;

            // Set destination info from BSP (for absolute types)
            if (targetZoneId != 0 && merged.destinationZoneId == 0) {
                merged.destinationZoneId = targetZoneId;
                merged.destinationZone = getZoneName(targetZoneId);
                merged.destX = destX;
                merged.destY = destY;
                merged.destZ = destZ;
                merged.destHeading = destHeading;
            }

            // Compute bounds
            BspBounds bounds = bspTree->computeRegionBounds(i, initialBounds);
            if (bounds.valid) {
                merged.mergeBounds(bounds.minX, bounds.minY, bounds.minZ,
                                   bounds.maxX, bounds.maxY, bounds.maxZ);
                regionsWithBounds++;
            }
        }

        // Resolve reference-type zone lines using zone_points.json
        if (zonePoints) {
            for (auto& [zpIndex, merged] : mergedZoneLines) {
                if (merged.isReferenceType && merged.destinationZoneId == 0) {
                    auto zpDataIt = zonePoints->find(zpIndex);
                    if (zpDataIt != zonePoints->end()) {
                        const auto& zp = zpDataIt->second;
                        merged.destinationZoneId = zp.targetZoneId;
                        merged.destinationZone = getZoneName(zp.targetZoneId);
                        merged.destX = zp.targetX;
                        merged.destY = zp.targetY;
                        merged.destZ = zp.targetZ;
                        merged.destHeading = zp.targetHeading;
                    }
                }
            }
        }

        std::cout << "  Total BSP zone line regions: " << totalRegions << std::endl;
        std::cout << "  Regions with valid bounds: " << regionsWithBounds << std::endl;
        std::cout << "  Unique zone_point_index values: " << mergedZoneLines.size() << std::endl;

        // Count resolved zone connections
        int resolvedConnections = 0;
        int unresolvedConnections = 0;
        std::map<std::string, std::vector<uint32_t>> byDestination;
        for (const auto& [zpIndex, merged] : mergedZoneLines) {
            if (merged.destinationZoneId != 0 || !merged.destinationZone.empty()) {
                resolvedConnections++;
                byDestination[merged.destinationZone].push_back(zpIndex);
            } else {
                unresolvedConnections++;
            }
        }

        std::cout << "  Resolved zone connections: " << resolvedConnections << std::endl;
        if (unresolvedConnections > 0) {
            std::cout << "  Unresolved (internal/unused): " << unresolvedConnections << std::endl;
        }

        if (!byDestination.empty()) {
            zonesWithZoneLines++;
            std::cout << "\n  Connections by destination:" << std::endl;
            for (const auto& [dest, indices] : byDestination) {
                std::string destName = dest.empty() ? "(unresolved)" : dest;
                std::cout << "    -> " << destName << ": " << indices.size() << " zone point(s) (";
                for (size_t i = 0; i < indices.size() && i < 5; ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << "#" << indices[i];
                }
                if (indices.size() > 5) std::cout << ", ...";
                std::cout << ")" << std::endl;
            }
        }

        // Build JSON output - only include zone lines with resolved destinations
        if (resolvedConnections > 0) {
            Json::Value zoneData(Json::objectValue);
            zoneData["zone_name"] = zoneName;

            if (geometry) {
                Json::Value boundsObj(Json::objectValue);
                boundsObj["min_x"] = geoMinX;
                boundsObj["min_y"] = geoMinY;
                boundsObj["min_z"] = geoMinZ;
                boundsObj["max_x"] = geoMaxX;
                boundsObj["max_y"] = geoMaxY;
                boundsObj["max_z"] = geoMaxZ;
                zoneData["geometry_bounds"] = boundsObj;
            }

            // Store transform parameters for debugging/verification
            Json::Value transformObj(Json::objectValue);
            transformObj["offset_x"] = transformOffsetX;
            transformObj["offset_y"] = transformOffsetY;
            transformObj["offset_z"] = transformOffsetZ;
            transformObj["bsp_center_x"] = bspCenterX;
            transformObj["bsp_center_y"] = bspCenterY;
            transformObj["world_center_x"] = worldCenterX;
            transformObj["world_center_y"] = worldCenterY;
            zoneData["coordinate_transform"] = transformObj;

            Json::Value zoneLines(Json::arrayValue);
            for (const auto& [zpIndex, merged] : mergedZoneLines) {
                // Only include zone lines with resolved destinations
                if (merged.destinationZoneId == 0 && merged.destinationZone.empty()) continue;
                if (!merged.hasBounds) continue;  // Skip if no trigger box

                Json::Value zlObj(Json::objectValue);
                zlObj["zone_point_index"] = merged.zonePointIndex;
                zlObj["destination_zone"] = merged.destinationZone;
                zlObj["destination_zone_id"] = merged.destinationZoneId;
                zlObj["bsp_region_count"] = merged.bspRegionCount;
                zlObj["type"] = merged.isReferenceType ? "reference" : "absolute";

                // Output raw BSP coordinates (no transform)
                // The runtime check will need to use matching coordinates
                Json::Value boundsObj(Json::objectValue);
                boundsObj["min_x"] = merged.minX;
                boundsObj["max_x"] = merged.maxX;
                boundsObj["min_y"] = merged.minY;
                boundsObj["max_y"] = merged.maxY;
                boundsObj["min_z"] = merged.minZ;
                boundsObj["max_z"] = merged.maxZ;
                zlObj["trigger_box"] = boundsObj;

                Json::Value destObj(Json::objectValue);
                destObj["x"] = merged.destX;
                destObj["y"] = merged.destY;
                destObj["z"] = merged.destZ;
                destObj["heading"] = merged.destHeading;
                zlObj["destination"] = destObj;

                zoneLines.append(zlObj);
            }

            zoneData["zone_lines"] = zoneLines;
            root[zoneName] = zoneData;
        }
    }

    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Zones processed: " << zonesProcessed << std::endl;
    std::cout << "Zones with zone lines: " << zonesWithZoneLines << std::endl;

    // Output JSON
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::string output = Json::writeString(builder, root);

    // Write to file
    std::ofstream outFile("zone_lines_extracted.json");
    if (outFile.is_open()) {
        outFile << output;
        outFile.close();
        std::cout << "\nWritten to zone_lines_extracted.json" << std::endl;
    }

    return 0;
}
