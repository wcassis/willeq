/**
 * query_surface_map - Query a surface map file at specific coordinates
 *
 * Usage: query_surface_map <map_file> <x> <y>
 * Example: query_surface_map qeynos2_surface.map -100 50
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

// Surface map file format header (must match generate_surface_map.cpp)
struct SurfaceMapHeader {
    char magic[4];
    uint32_t version;
    float cellSize;
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    uint32_t gridWidth;
    uint32_t gridHeight;
    uint32_t cellCount;
};

const char* surfaceTypeName(uint8_t type) {
    switch (type) {
        case 0: return "Unknown";
        case 1: return "Grass";
        case 2: return "Dirt";
        case 3: return "Stone";
        case 4: return "Brick";
        case 5: return "Wood";
        case 6: return "Sand";
        case 7: return "Snow";
        case 8: return "Water";
        case 9: return "Lava";
        default: return "Invalid";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <map_file> <x> <y> [radius]\n";
        std::cerr << "Example: " << argv[0] << " qeynos2_surface.map -100 50\n";
        std::cerr << "         " << argv[0] << " qeynos2_surface.map -100 50 10\n";
        std::cerr << "\nOptions:\n";
        std::cerr << "  radius: Show all cells within this radius (default: 0)\n";
        return 1;
    }

    std::string mapFile = argv[1];
    float queryX = std::stof(argv[2]);
    float queryY = std::stof(argv[3]);
    float radius = (argc > 4) ? std::stof(argv[4]) : 0.0f;

    // Load map file
    std::ifstream file(mapFile, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open " << mapFile << "\n";
        return 1;
    }

    // Read header
    SurfaceMapHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        std::cerr << "Error: Failed to read header\n";
        return 1;
    }

    // Validate magic
    if (header.magic[0] != 'S' || header.magic[1] != 'M' ||
        header.magic[2] != 'A' || header.magic[3] != 'P') {
        std::cerr << "Error: Invalid magic number\n";
        return 1;
    }

    std::cout << "=== Surface Map Info ===\n";
    std::cout << "Cell size: " << header.cellSize << " units\n";
    std::cout << "Grid: " << header.gridWidth << " x " << header.gridHeight << "\n";
    std::cout << "Bounds: X[" << header.minX << ", " << header.maxX << "] Y["
              << header.minY << ", " << header.maxY << "]\n\n";

    // Read surface grid
    std::vector<uint8_t> surfaceGrid(header.cellCount);
    file.read(reinterpret_cast<char*>(surfaceGrid.data()), header.cellCount);
    if (!file) {
        std::cerr << "Error: Failed to read surface grid\n";
        return 1;
    }

    // Read height grid
    std::vector<float> heightGrid(header.cellCount);
    file.read(reinterpret_cast<char*>(heightGrid.data()), header.cellCount * sizeof(float));

    // Query single point or radius
    if (radius <= 0) {
        // Single point query
        if (queryX < header.minX || queryX >= header.maxX ||
            queryY < header.minY || queryY >= header.maxY) {
            std::cerr << "Error: Coordinates out of bounds\n";
            return 1;
        }

        uint32_t cellX = static_cast<uint32_t>((queryX - header.minX) / header.cellSize);
        uint32_t cellY = static_cast<uint32_t>((queryY - header.minY) / header.cellSize);
        size_t idx = cellY * header.gridWidth + cellX;

        float cellCenterX = header.minX + (cellX + 0.5f) * header.cellSize;
        float cellCenterY = header.minY + (cellY + 0.5f) * header.cellSize;

        std::cout << "Query: (" << queryX << ", " << queryY << ")\n";
        std::cout << "Cell: (" << cellX << ", " << cellY << ") index=" << idx << "\n";
        std::cout << "Cell center: (" << cellCenterX << ", " << cellCenterY << ")\n";
        std::cout << "Surface type: " << (int)surfaceGrid[idx] << " (" << surfaceTypeName(surfaceGrid[idx]) << ")\n";
        std::cout << "Height: " << heightGrid[idx] << "\n";
    } else {
        // Radius query - show all cells within radius
        std::cout << "Query: (" << queryX << ", " << queryY << ") radius=" << radius << "\n\n";

        int cellRadius = static_cast<int>(std::ceil(radius / header.cellSize)) + 1;
        uint32_t centerCellX = static_cast<uint32_t>((queryX - header.minX) / header.cellSize);
        uint32_t centerCellY = static_cast<uint32_t>((queryY - header.minY) / header.cellSize);

        // Count surface types
        int counts[10] = {0};

        std::cout << "Cells within radius:\n";
        for (int dy = -cellRadius; dy <= cellRadius; ++dy) {
            for (int dx = -cellRadius; dx <= cellRadius; ++dx) {
                int cellX = static_cast<int>(centerCellX) + dx;
                int cellY = static_cast<int>(centerCellY) + dy;

                if (cellX < 0 || cellX >= static_cast<int>(header.gridWidth) ||
                    cellY < 0 || cellY >= static_cast<int>(header.gridHeight)) {
                    continue;
                }

                float worldX = header.minX + (cellX + 0.5f) * header.cellSize;
                float worldY = header.minY + (cellY + 0.5f) * header.cellSize;

                float dist = std::sqrt((worldX - queryX) * (worldX - queryX) +
                                       (worldY - queryY) * (worldY - queryY));
                if (dist > radius) {
                    continue;
                }

                size_t idx = cellY * header.gridWidth + cellX;
                uint8_t surfType = surfaceGrid[idx];
                if (surfType < 10) counts[surfType]++;

                std::cout << "  (" << worldX << ", " << worldY << ") -> "
                          << surfaceTypeName(surfType) << " (h=" << heightGrid[idx] << ")\n";
            }
        }

        std::cout << "\nSummary within radius:\n";
        for (int i = 0; i < 10; ++i) {
            if (counts[i] > 0) {
                std::cout << "  " << surfaceTypeName(i) << ": " << counts[i] << "\n";
            }
        }
    }

    return 0;
}
