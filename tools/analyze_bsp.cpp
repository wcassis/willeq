#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <map>
#include <set>
#include "common/logging.h"

// Simple S3D/WLD parser to analyze BSP tree

struct BspNode {
    float normalX, normalY, normalZ;
    float splitDistance;
    int32_t regionId;
    int32_t left, right;
};

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Decode WLD string
std::string decodeString(const char* data, size_t len) {
    static const uint8_t key[] = {0x95, 0x3A, 0xC5, 0x2A, 0x95, 0x7A, 0x95, 0x6A};
    std::string result(len, 0);
    for (size_t i = 0; i < len; i++) {
        result[i] = data[i] ^ key[i % 8];
    }
    return result;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <wld_file>" << std::endl;
        return 1;
    }

    auto data = readFile(argv[1]);
    if (data.empty()) {
        LOG_ERROR(MOD_MAIN, "Failed to read file");
        return 1;
    }

    // Parse WLD header
    uint32_t magic = *reinterpret_cast<uint32_t*>(&data[0]);
    if (magic != 0x54503D02) {
        LOG_ERROR(MOD_MAIN, "Invalid WLD magic");
        return 1;
    }

    uint32_t version = *reinterpret_cast<uint32_t*>(&data[4]);
    uint32_t fragmentCount = *reinterpret_cast<uint32_t*>(&data[8]);
    uint32_t hashLength = *reinterpret_cast<uint32_t*>(&data[20]);

    std::cout << "WLD: " << fragmentCount << " fragments, hash=" << hashLength << std::endl;

    size_t idx = 24;

    // Read string hash
    std::string stringHash = decodeString(reinterpret_cast<char*>(&data[idx]), hashLength);
    idx += hashLength;

    std::vector<BspNode> bspNodes;
    std::map<int, std::set<int>> regionToNodes;  // regionId -> node indices
    std::map<int, std::string> regionTypes;  // region index -> type string

    // Parse fragments
    for (uint32_t f = 0; f < fragmentCount && idx + 8 <= data.size(); f++) {
        uint32_t fragSize = *reinterpret_cast<uint32_t*>(&data[idx]);
        uint32_t fragType = *reinterpret_cast<uint32_t*>(&data[idx + 4]);
        idx += 8;

        if (idx + fragSize > data.size()) break;

        const uint8_t* fragData = &data[idx];

        if (fragType == 0x21) {
            // BSP Tree
            uint32_t nodeCount = *reinterpret_cast<const uint32_t*>(fragData);
            std::cout << "BSP Tree: " << nodeCount << " nodes" << std::endl;

            const uint8_t* ptr = fragData + 4;
            for (uint32_t i = 0; i < nodeCount; i++) {
                BspNode node;
                node.normalX = *reinterpret_cast<const float*>(ptr); ptr += 4;
                node.normalY = *reinterpret_cast<const float*>(ptr); ptr += 4;
                node.normalZ = *reinterpret_cast<const float*>(ptr); ptr += 4;
                node.splitDistance = *reinterpret_cast<const float*>(ptr); ptr += 4;
                node.regionId = *reinterpret_cast<const int32_t*>(ptr); ptr += 4;
                node.left = *reinterpret_cast<const int32_t*>(ptr) - 1; ptr += 4;
                node.right = *reinterpret_cast<const int32_t*>(ptr) - 1; ptr += 4;

                bspNodes.push_back(node);

                if (node.regionId > 0) {
                    regionToNodes[node.regionId].insert(i);
                }
            }
        }
        else if (fragType == 0x29) {
            // BSP Region Type
            int32_t nameRef = *reinterpret_cast<const int32_t*>(fragData);
            int32_t flags = *reinterpret_cast<const int32_t*>(fragData + 4);
            int32_t regionCount = *reinterpret_cast<const int32_t*>(fragData + 8);

            const uint8_t* ptr = fragData + 12;
            std::vector<int32_t> regions;
            for (int i = 0; i < regionCount; i++) {
                int32_t regionIdx = *reinterpret_cast<const int32_t*>(ptr);
                regions.push_back(regionIdx);
                ptr += 4;
            }

            int32_t strSize = *reinterpret_cast<const int32_t*>(ptr);
            ptr += 4;

            std::string typeStr;
            if (strSize > 0) {
                typeStr = decodeString(reinterpret_cast<const char*>(ptr), strSize);
            } else if (nameRef < 0) {
                size_t offset = -nameRef;
                if (offset < stringHash.size()) {
                    typeStr = &stringHash[offset];
                }
            }

            // Check if zone line
            if (typeStr.find("drntp") != std::string::npos ||
                typeStr.find("wtntp") != std::string::npos) {
                std::cout << "Zone line type: " << typeStr << " -> regions: ";
                for (int r : regions) {
                    std::cout << r << " ";
                    regionTypes[r] = typeStr;
                }
                std::cout << std::endl;
            }
        }

        idx += fragSize;
    }

    // Find BSP nodes that have zone line regions
    std::cout << "\n=== Zone Line Region Nodes ===" << std::endl;
    for (const auto& [regionIdx, typeStr] : regionTypes) {
        int regionId = regionIdx + 1;  // regionId is 1-based
        if (regionToNodes.count(regionId)) {
            for (int nodeIdx : regionToNodes[regionId]) {
                const auto& node = bspNodes[nodeIdx];
                std::cout << "Region " << regionIdx << " (" << typeStr.substr(0, 30) << "...) at node " << nodeIdx
                          << " normal=(" << node.normalX << "," << node.normalY << "," << node.normalZ << ")"
                          << " dist=" << node.splitDistance << std::endl;
            }
        }
    }

    // Trace path to a zone line region (zone 4 = qeynos hills)
    std::cout << "\n=== Tracing path to zone 4 (qeynos hills) regions ===" << std::endl;
    for (const auto& [regionIdx, typeStr] : regionTypes) {
        if (typeStr.find("drntp00004") != std::string::npos) {
            int regionId = regionIdx + 1;
            if (regionToNodes.count(regionId)) {
                int nodeIdx = *regionToNodes[regionId].begin();
                std::cout << "\nRegion " << regionIdx << " is at node " << nodeIdx << std::endl;

                // Find a sample point that reaches this region by brute-force testing coordinates
                bool found = false;
                for (float x = -2000; x <= 2000 && !found; x += 50) {
                    for (float y = -2000; y <= 2000 && !found; y += 50) {
                        for (float z = -100; z <= 200 && !found; z += 50) {
                            int curNode = 0;
                            int depth = 0;
                            while (curNode >= 0 && curNode < (int)bspNodes.size() && depth < 50) {
                                const auto& n = bspNodes[curNode];
                                if (n.regionId > 0) {
                                    if (n.regionId == regionId) {
                                        std::cout << "  Coords (" << x << ", " << y << ", " << z << ") reach region " << regionIdx << std::endl;
                                        found = true;
                                    }
                                    break;
                                }
                                float dot = x * n.normalX + y * n.normalY + z * n.normalZ + n.splitDistance;
                                curNode = (dot <= 0) ? n.left : n.right;
                                depth++;
                            }
                        }
                    }
                }
                if (found) break;  // Just find one example
            }
        }
    }

    // Test with player's known coordinates
    std::cout << "\n=== Testing player coordinates (server: 91.42, 1592.17, 3.0) ===" << std::endl;
    float playerX = 91.42f, playerY = 1592.17f, playerZ = 3.0f;

    struct TestCoord {
        float x, y, z;
        const char* name;
    };
    TestCoord tests[] = {
        {playerX, playerY, playerZ, "server (x,y,z)"},
        {playerY, playerX, playerZ, "server (y,x,z)"},
        {-playerX, -playerY, playerZ, "server (-x,-y,z)"},
        {-playerY, -playerX, playerZ, "server (-y,-x,z)"},
        {playerX, -playerY, playerZ, "server (x,-y,z)"},
        {-playerX, playerY, playerZ, "server (-x,y,z)"},
        {playerY, -playerX, playerZ, "server (y,-x,z)"},
        {-playerY, playerX, playerZ, "server (-y,x,z)"},
    };

    for (const auto& test : tests) {
        int curNode = 0;
        int depth = 0;
        while (curNode >= 0 && curNode < (int)bspNodes.size() && depth < 50) {
            const auto& n = bspNodes[curNode];
            if (n.regionId > 0) {
                std::string regionType = regionTypes.count(n.regionId - 1) ? regionTypes[n.regionId - 1] : "normal";
                std::cout << test.name << " -> region " << (n.regionId - 1) << " (" << regionType.substr(0, 25) << ")" << std::endl;
                break;
            }
            float dot = test.x * n.normalX + test.y * n.normalY + test.z * n.normalZ + n.splitDistance;
            curNode = (dot <= 0) ? n.left : n.right;
            depth++;
        }
        if (curNode < 0 || curNode >= (int)bspNodes.size()) {
            std::cout << test.name << " -> NO REGION (dead end at depth " << depth << ")" << std::endl;
        }
    }

    return 0;
}
