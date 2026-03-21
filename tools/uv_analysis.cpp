// UV Wrapping Analysis Tool
// Scans character model archives (_chr.s3d) and reports UV extents per texture.
// Used to determine how much textures need to be "unrolled" (pre-tiled) to
// eliminate GL_REPEAT dependency, enabling entity texture atlasing.
//
// Usage:
//   uv_analysis --eq-path /path/to/EQ --archive global_chr.s3d
//   uv_analysis --eq-path /path/to/EQ --all-chr
//   uv_analysis --eq-path /path/to/EQ --all-chr --csv > uv_report.csv

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <cstring>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/s3d_loader.h"

using namespace EQT::Graphics;
namespace fs = std::filesystem;

struct TextureUVStats {
    std::string textureName;
    std::string modelName;
    std::string archiveName;
    float minU = 1e9f, maxU = -1e9f;
    float minV = 1e9f, maxV = -1e9f;
    int triangleCount = 0;
    int vertexCount = 0;
    int texWidth = 0;   // from BMP header if available
    int texHeight = 0;

    float wrapU() const { return maxU - minU; }
    float wrapV() const { return maxV - minV; }

    // Tile count needed: ceil of UV range (e.g., range 3.7 → 4 tiles)
    int tilesU() const { return std::max(1, static_cast<int>(std::ceil(wrapU()))); }
    int tilesV() const { return std::max(1, static_cast<int>(std::ceil(wrapV()))); }
};

// Read texture dimensions from BMP header
static bool getTextureDimensions(PfsArchive& archive, const std::string& name,
                                  int& width, int& height) {
    std::vector<char> data;
    if (!archive.get(name, data) || data.size() < 54) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    if (d[0] != 'B' || d[1] != 'M') return false;
    width = *reinterpret_cast<const int32_t*>(d + 18);
    height = std::abs(*reinterpret_cast<const int32_t*>(d + 22));
    return width > 0 && height > 0;
}

static void analyzeArchive(const std::string& eqPath, const std::string& archiveName,
                            std::vector<TextureUVStats>& allStats, bool verbose) {
    std::string archivePath = eqPath + "/" + archiveName;
    S3DLoader loader;
    if (!loader.loadZone(archivePath)) {
        if (verbose) std::cerr << "  Could not load: " << archivePath << std::endl;
        return;
    }

    auto zone = loader.getZone();
    if (!zone) return;

    // Open archive directly for texture dimension queries
    PfsArchive archive;
    archive.open(archivePath);

    // Texture dimension cache
    std::map<std::string, std::pair<int,int>> texDims;
    auto getTexDims = [&](const std::string& name) -> std::pair<int,int> {
        auto it = texDims.find(name);
        if (it != texDims.end()) return it->second;
        int w = 0, h = 0;
        getTextureDimensions(archive, name, w, h);
        texDims[name] = {w, h};
        return {w, h};
    };

    for (const auto& character : zone->characters) {
        if (!character) continue;

        // Use parts (geometry without bone transforms — raw UVs)
        for (const auto& part : character->parts) {
            if (!part || part->vertices.empty() || part->triangles.empty()) continue;

            const auto& texNames = part->textureNames();

            // Group triangles by texture and compute UV extents
            std::map<uint32_t, TextureUVStats> perTexture;

            for (const auto& tri : part->triangles) {
                if (tri.textureIndex >= texNames.size()) continue;

                auto& stats = perTexture[tri.textureIndex];
                if (stats.textureName.empty()) {
                    stats.textureName = texNames[tri.textureIndex];
                    stats.modelName = character->name;
                    stats.archiveName = archiveName;
                    auto dims = getTexDims(stats.textureName);
                    stats.texWidth = dims.first;
                    stats.texHeight = dims.second;
                }

                // Update UV extents from triangle vertices
                for (uint32_t vi : {tri.v1, tri.v2, tri.v3}) {
                    if (vi >= part->vertices.size()) continue;
                    const auto& v = part->vertices[vi];
                    stats.minU = std::min(stats.minU, v.u);
                    stats.maxU = std::max(stats.maxU, v.u);
                    stats.minV = std::min(stats.minV, v.v);
                    stats.maxV = std::max(stats.maxV, v.v);
                    stats.vertexCount++;
                }
                stats.triangleCount++;
            }

            for (auto& [idx, stats] : perTexture) {
                if (!stats.textureName.empty()) {
                    allStats.push_back(std::move(stats));
                }
            }
        }
    }
}

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " --eq-path <path> --archive <name>\n"
              << "       " << prog << " --eq-path <path> --all-chr\n"
              << "\nOptions:\n"
              << "  --eq-path <path>     Path to EverQuest client directory\n"
              << "  --archive <name>     Analyze single archive (e.g., global_chr.s3d)\n"
              << "  --all-chr            Analyze all *_chr.s3d files\n"
              << "  --csv                Output CSV format (for spreadsheet analysis)\n"
              << "  --wrapping-only      Only show textures with UV wrapping (range > 1.0)\n"
              << "  --verbose            Print per-archive progress\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string eqPath;
    std::string archiveName;
    bool allChr = false;
    bool csv = false;
    bool wrappingOnly = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--eq-path" && i + 1 < argc) eqPath = argv[++i];
        else if (arg == "--archive" && i + 1 < argc) archiveName = argv[++i];
        else if (arg == "--all-chr") allChr = true;
        else if (arg == "--csv") csv = true;
        else if (arg == "--wrapping-only") wrappingOnly = true;
        else if (arg == "--verbose") verbose = true;
        else if (arg == "--help" || arg == "-h") { printUsage(argv[0]); return 0; }
    }

    if (eqPath.empty() || (!allChr && archiveName.empty())) {
        printUsage(argv[0]);
        return 1;
    }

    // Collect archives
    std::vector<std::string> archives;
    if (allChr) {
        for (const auto& entry : fs::directory_iterator(eqPath)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (lower.size() > 8 && lower.substr(lower.size() - 8) == "_chr.s3d") {
                archives.push_back(name);
            }
        }
        std::sort(archives.begin(), archives.end());
        if (verbose)
            std::cerr << "Analyzing " << archives.size() << " chr archives..." << std::endl;
    } else {
        archives.push_back(archiveName);
    }

    // Analyze all archives
    std::vector<TextureUVStats> allStats;
    for (size_t i = 0; i < archives.size(); ++i) {
        if (verbose)
            std::cerr << "[" << (i+1) << "/" << archives.size() << "] " << archives[i] << std::endl;
        analyzeArchive(eqPath, archives[i], allStats, verbose);
    }

    // Filter if requested
    if (wrappingOnly) {
        allStats.erase(std::remove_if(allStats.begin(), allStats.end(),
            [](const TextureUVStats& s) { return s.wrapU() <= 1.01f && s.wrapV() <= 1.01f; }),
            allStats.end());
    }

    // Sort by wrap magnitude (most wrapping first)
    std::sort(allStats.begin(), allStats.end(), [](const TextureUVStats& a, const TextureUVStats& b) {
        return (a.tilesU() * a.tilesV()) > (b.tilesU() * b.tilesV());
    });

    if (csv) {
        std::cout << "archive,model,texture,texW,texH,minU,maxU,minV,maxV,wrapU,wrapV,tilesU,tilesV,unrolledW,unrolledH,tris,verts\n";
        for (const auto& s : allStats) {
            int unrolledW = s.texWidth * s.tilesU();
            int unrolledH = s.texHeight * s.tilesV();
            std::cout << s.archiveName << "," << s.modelName << "," << s.textureName << ","
                      << s.texWidth << "," << s.texHeight << ","
                      << std::fixed << std::setprecision(2)
                      << s.minU << "," << s.maxU << "," << s.minV << "," << s.maxV << ","
                      << s.wrapU() << "," << s.wrapV() << ","
                      << s.tilesU() << "," << s.tilesV() << ","
                      << unrolledW << "," << unrolledH << ","
                      << s.triangleCount << "," << s.vertexCount << "\n";
        }
    } else {
        // Summary report
        int totalTextures = allStats.size();
        int wrapping = 0;
        int maxTilesU = 0, maxTilesV = 0;
        size_t totalOriginalBytes = 0;
        size_t totalUnrolledBytes = 0;

        for (const auto& s : allStats) {
            if (s.wrapU() > 1.01f || s.wrapV() > 1.01f) wrapping++;
            maxTilesU = std::max(maxTilesU, s.tilesU());
            maxTilesV = std::max(maxTilesV, s.tilesV());
            if (s.texWidth > 0 && s.texHeight > 0) {
                // ETC1: 4bpp = 0.5 bytes/pixel
                totalOriginalBytes += (s.texWidth * s.texHeight) / 2;
                totalUnrolledBytes += (s.texWidth * s.tilesU() * s.texHeight * s.tilesV()) / 2;
            }
        }

        std::cout << "=== UV Wrapping Analysis ===" << std::endl;
        std::cout << "Archives analyzed: " << archives.size() << std::endl;
        std::cout << "Total texture refs: " << totalTextures << std::endl;
        std::cout << "Textures with wrapping (UV > 1.0): " << wrapping
                  << " (" << (totalTextures > 0 ? wrapping * 100 / totalTextures : 0) << "%)" << std::endl;
        std::cout << "Max tile count: " << maxTilesU << "x" << maxTilesV << std::endl;
        std::cout << "ETC1 original: " << totalOriginalBytes / 1024 << " KB" << std::endl;
        std::cout << "ETC1 unrolled: " << totalUnrolledBytes / 1024 << " KB" << std::endl;
        std::cout << "Memory increase: " << (totalOriginalBytes > 0 ?
            (totalUnrolledBytes * 100 / totalOriginalBytes) : 0) << "%" << std::endl;

        // Top 20 worst wrappers
        std::cout << "\n--- Top 20 Most-Wrapped Textures ---" << std::endl;
        std::cout << std::left << std::setw(30) << "texture"
                  << std::setw(20) << "model"
                  << std::setw(8) << "size"
                  << std::setw(12) << "UV range"
                  << std::setw(8) << "tiles"
                  << std::setw(10) << "unrolled"
                  << std::setw(10) << "ETC1 KB"
                  << std::endl;
        std::cout << std::string(98, '-') << std::endl;

        int shown = 0;
        for (const auto& s : allStats) {
            if (shown >= 20) break;
            int unrolledW = s.texWidth * s.tilesU();
            int unrolledH = s.texHeight * s.tilesV();
            size_t etc1Bytes = (unrolledW * unrolledH) / 2;
            std::cout << std::left << std::setw(30) << s.textureName
                      << std::setw(20) << s.modelName
                      << s.texWidth << "x" << std::left << std::setw(5) << s.texHeight
                      << std::fixed << std::setprecision(1)
                      << "[" << s.minU << "," << s.maxU << "]x[" << s.minV << "," << s.maxV << "]"
                      << "  " << s.tilesU() << "x" << s.tilesV()
                      << "  " << unrolledW << "x" << unrolledH
                      << "  " << etc1Bytes / 1024 << " KB"
                      << std::endl;
            shown++;
        }

        // Distribution histogram
        std::cout << "\n--- Tile Count Distribution ---" << std::endl;
        std::map<int, int> tileDistribution;
        for (const auto& s : allStats) {
            int tiles = s.tilesU() * s.tilesV();
            tileDistribution[tiles]++;
        }
        for (const auto& [tiles, count] : tileDistribution) {
            std::cout << "  " << std::setw(4) << tiles << " tiles: " << count << " textures" << std::endl;
        }
    }

    return 0;
}
