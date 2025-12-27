// Tool to analyze BSP tree and find coordinates that reach zone line regions
// Uses the existing WldLoader

#include <iostream>
#include <iomanip>
#include <map>
#include <algorithm>
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"

using namespace EQT::Graphics;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <s3d_file> <wld_name>" << std::endl;
        std::cerr << "Example: " << argv[0] << " qeynos2.s3d qeynos2.wld" << std::endl;
        return 1;
    }

    std::string archivePath = argv[1];
    std::string wldName = argv[2];

    WldLoader loader;
    if (!loader.parseFromArchive(archivePath, wldName)) {
        LOG_ERROR(MOD_MAIN, "Failed to parse WLD");
        return 1;
    }

    auto bspTree = loader.getBspTree();
    if (!bspTree) {
        LOG_ERROR(MOD_MAIN, "No BSP tree found");
        return 1;
    }

    std::cout << "=== BSP Tree Analysis ===" << std::endl;
    std::cout << "Nodes: " << bspTree->nodes.size() << std::endl;
    std::cout << "Regions: " << bspTree->regions.size() << std::endl;

    // Find zone line regions (specifically zone 4 = qeynos hills)
    std::cout << "\n=== Zone Line Regions ===" << std::endl;
    std::vector<size_t> zone4Regions;
    for (size_t i = 0; i < bspTree->regions.size(); ++i) {
        const auto& region = bspTree->regions[i];
        for (auto type : region->regionTypes) {
            if (type == RegionType::Zoneline && region->zoneLineInfo) {
                std::cout << "Region " << i << ": zoneId=" << region->zoneLineInfo->zoneId
                          << " coords=(" << region->zoneLineInfo->x << ", "
                          << region->zoneLineInfo->y << ", " << region->zoneLineInfo->z << ")" << std::endl;
                if (region->zoneLineInfo->zoneId == 4) {
                    zone4Regions.push_back(i);
                }
                break;
            }
        }
    }

    // Find BSP nodes that have these regions
    std::cout << "\n=== BSP Nodes with Zone 4 Regions ===" << std::endl;
    std::vector<size_t> zone4Nodes;
    for (size_t i = 0; i < bspTree->nodes.size(); ++i) {
        const auto& node = bspTree->nodes[i];
        // regionId is 1-based
        for (size_t regionIdx : zone4Regions) {
            if (node.regionId == static_cast<int32_t>(regionIdx + 1)) {
                std::cout << "Node " << i << ": regionId=" << node.regionId
                          << " normal=(" << node.normalX << ", " << node.normalY << ", " << node.normalZ << ")"
                          << " dist=" << node.splitDistance
                          << " left=" << node.left << " right=" << node.right << std::endl;
                zone4Nodes.push_back(i);
            }
        }
    }

    // Find parent nodes that point to zone 4 nodes
    std::cout << "\n=== Parent nodes pointing to Zone 4 leaf nodes ===" << std::endl;
    for (size_t z4node : zone4Nodes) {
        for (size_t i = 0; i < bspTree->nodes.size(); ++i) {
            const auto& node = bspTree->nodes[i];
            if (node.left == static_cast<int32_t>(z4node) || node.right == static_cast<int32_t>(z4node)) {
                std::cout << "Node " << i << " points to zone4 node " << z4node
                          << " via " << (node.left == static_cast<int32_t>(z4node) ? "LEFT" : "RIGHT")
                          << " normal=(" << node.normalX << ", " << node.normalY << ", " << node.normalZ << ")"
                          << " dist=" << node.splitDistance << std::endl;
            }
        }
    }

    // Check node 0 (root) structure
    std::cout << "\n=== Root node (node 0) ===" << std::endl;
    if (!bspTree->nodes.empty()) {
        const auto& root = bspTree->nodes[0];
        std::cout << "Root: normal=(" << root.normalX << ", " << root.normalY << ", " << root.normalZ << ")"
                  << " dist=" << root.splitDistance
                  << " regionId=" << root.regionId
                  << " left=" << root.left << " right=" << root.right << std::endl;
    }

    // Trace path from root to a high-numbered zone 4 node (like 6350)
    std::cout << "\n=== Tracing path from root to zone 4 node ===" << std::endl;
    if (!zone4Nodes.empty()) {
        // Find a high-numbered zone 4 node (>6000)
        size_t targetNode = zone4Nodes[0];
        for (size_t n : zone4Nodes) {
            if (n >= 6350) {
                targetNode = n;
                break;
            }
        }
        std::cout << "Target: node " << targetNode << std::endl;

        // Build parent map
        std::map<int32_t, int32_t> parent;
        std::map<int32_t, bool> isLeftChild;
        for (size_t i = 0; i < bspTree->nodes.size(); ++i) {
            const auto& node = bspTree->nodes[i];
            if (node.left >= 0) {
                parent[node.left] = i;
                isLeftChild[node.left] = true;
            }
            if (node.right >= 0) {
                parent[node.right] = i;
                isLeftChild[node.right] = false;
            }
        }

        // Trace back to root
        std::vector<int32_t> path;
        int32_t current = static_cast<int32_t>(targetNode);
        while (parent.count(current)) {
            path.push_back(current);
            current = parent[current];
        }
        path.push_back(current);  // Add root
        std::reverse(path.begin(), path.end());

        std::cout << "Path from root to target (" << path.size() << " nodes):" << std::endl;
        std::cout << "\nConstraints for reaching zone 4 node:" << std::endl;
        for (size_t i = 0; i < path.size() - 1; ++i) {
            int32_t nodeIdx = path[i];
            const auto& node = bspTree->nodes[nodeIdx];
            bool goLeft = (isLeftChild.count(path[i+1]) && isLeftChild[path[i+1]]);

            // Constraint: normal · point + dist <= 0 for LEFT, > 0 for RIGHT
            std::cout << "  Node " << nodeIdx << ": ";
            if (std::abs(node.normalX) > 0.5f) {
                float threshold = -node.splitDistance / node.normalX;
                std::cout << "x " << (goLeft ? (node.normalX > 0 ? "<=" : ">=") : (node.normalX > 0 ? ">" : "<"))
                          << " " << threshold;
            } else if (std::abs(node.normalY) > 0.5f) {
                float threshold = -node.splitDistance / node.normalY;
                std::cout << "y " << (goLeft ? (node.normalY > 0 ? "<=" : ">=") : (node.normalY > 0 ? ">" : "<"))
                          << " " << threshold;
            } else if (std::abs(node.normalZ) > 0.5f) {
                float threshold = -node.splitDistance / node.normalZ;
                std::cout << "z " << (goLeft ? (node.normalZ > 0 ? "<=" : ">=") : (node.normalZ > 0 ? ">" : "<"))
                          << " " << threshold;
            } else {
                std::cout << "complex: " << node.normalX << "*x + " << node.normalY << "*y + "
                          << node.normalZ << "*z + " << node.splitDistance
                          << (goLeft ? " <= 0" : " > 0");
            }
            std::cout << " (go " << (goLeft ? "LEFT" : "RIGHT") << ")" << std::endl;
        }
    }

    // Build parent map to trace paths
    std::map<int32_t, int32_t> parent;
    std::map<int32_t, bool> isLeftChild;
    for (size_t i = 0; i < bspTree->nodes.size(); ++i) {
        const auto& node = bspTree->nodes[i];
        if (node.left >= 0) {
            parent[node.left] = i;
            isLeftChild[node.left] = true;
        }
        if (node.right >= 0) {
            parent[node.right] = i;
            isLeftChild[node.right] = false;
        }
    }

    // For each zone 4 region, trace constraints from root to leaf
    std::cout << "\n=== Zone 4 Region Bounding Constraints ===" << std::endl;

    for (size_t nodeIdx = 0; nodeIdx < bspTree->nodes.size(); ++nodeIdx) {
        const auto& node = bspTree->nodes[nodeIdx];
        if (node.regionId <= 0) continue;

        size_t regionIdx = node.regionId - 1;
        if (regionIdx >= bspTree->regions.size()) continue;

        const auto& region = bspTree->regions[regionIdx];
        bool isZone4 = false;
        for (auto type : region->regionTypes) {
            if (type == RegionType::Zoneline && region->zoneLineInfo && region->zoneLineInfo->zoneId == 4) {
                isZone4 = true;
                break;
            }
        }
        if (!isZone4) continue;

        // Trace path from this node back to root
        std::vector<int32_t> path;
        int32_t current = static_cast<int32_t>(nodeIdx);
        while (parent.count(current)) {
            path.push_back(current);
            current = parent[current];
        }
        path.push_back(current);  // Add root
        std::reverse(path.begin(), path.end());

        // Collect constraints
        float minX = -1e9, maxX = 1e9;
        float minY = -1e9, maxY = 1e9;
        float minZ = -1e9, maxZ = 1e9;

        for (size_t i = 0; i < path.size() - 1; ++i) {
            int32_t pathNodeIdx = path[i];
            const auto& pathNode = bspTree->nodes[pathNodeIdx];
            bool goLeft = isLeftChild.count(path[i+1]) && isLeftChild[path[i+1]];

            // For LEFT (front): dot >= 0, meaning normal·point + dist >= 0
            // For RIGHT (back): dot < 0, meaning normal·point + dist < 0
            float nx = pathNode.normalX, ny = pathNode.normalY, nz = pathNode.normalZ;
            float d = pathNode.splitDistance;

            // Simplify for axis-aligned planes
            // LEFT (front): dot >= 0, meaning n·p + d >= 0
            // RIGHT (back): dot < 0, meaning n·p + d < 0
            if (std::abs(nx) > 0.9f && std::abs(ny) < 0.1f && std::abs(nz) < 0.1f) {
                float threshold = -d / nx;
                if (goLeft) {
                    // nx*x + d >= 0: if nx>0 then x >= threshold, if nx<0 then x <= threshold
                    if (nx > 0) minX = std::max(minX, threshold);
                    else maxX = std::min(maxX, threshold);
                } else {
                    // nx*x + d < 0: if nx>0 then x < threshold, if nx<0 then x > threshold
                    if (nx > 0) maxX = std::min(maxX, threshold);
                    else minX = std::max(minX, threshold);
                }
            } else if (std::abs(ny) > 0.9f && std::abs(nx) < 0.1f && std::abs(nz) < 0.1f) {
                float threshold = -d / ny;
                if (goLeft) {
                    if (ny > 0) minY = std::max(minY, threshold);
                    else maxY = std::min(maxY, threshold);
                } else {
                    if (ny > 0) maxY = std::min(maxY, threshold);
                    else minY = std::max(minY, threshold);
                }
            } else if (std::abs(nz) > 0.9f && std::abs(nx) < 0.1f && std::abs(ny) < 0.1f) {
                float threshold = -d / nz;
                if (goLeft) {
                    if (nz > 0) minZ = std::max(minZ, threshold);
                    else maxZ = std::min(maxZ, threshold);
                } else {
                    if (nz > 0) maxZ = std::min(maxZ, threshold);
                    else minZ = std::max(minZ, threshold);
                }
            }
        }

        // Print bounds for this zone 4 region
        std::cout << "Region " << regionIdx << " (node " << nodeIdx << "): ";
        std::cout << "X=[";
        if (minX > -1e8) std::cout << minX; else std::cout << "-inf";
        std::cout << ", ";
        if (maxX < 1e8) std::cout << maxX; else std::cout << "inf";
        std::cout << "] Y=[";
        if (minY > -1e8) std::cout << minY; else std::cout << "-inf";
        std::cout << ", ";
        if (maxY < 1e8) std::cout << maxY; else std::cout << "inf";
        std::cout << "] Z=[";
        if (minZ > -1e8) std::cout << minZ; else std::cout << "-inf";
        std::cout << ", ";
        if (maxZ < 1e8) std::cout << maxZ; else std::cout << "inf";
        std::cout << "]" << std::endl;
    }

    return 0;
}
