// WLD File Dump Tool
// Parses and dumps the contents of an EverQuest WLD file
//
// T447 split the tool four ways; the seam is tools/wld_dump/wld_dump.h and
// the dump phases live in tools/wld_dump/. What stayed here is the archive
// I/O: usage, .s3d extraction, WLD selection and the WldLoader analysis.

#include "wld_dump/wld_dump.h"

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <set>

using namespace EQT::Graphics;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <file.wld|archive.s3d> [wld_name]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Dumps the contents of a WLD file." << std::endl;
    std::cerr << "Can read directly from a .wld file or extract from an .s3d archive." << std::endl;
    std::cerr << "If reading from .s3d, optionally specify which .wld to dump." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string inputPath = argv[1];
    std::string wldName = argc >= 3 ? argv[2] : "";

    // Check if it's a .wld or .s3d file
    bool isS3d = inputPath.size() >= 4 &&
                 (inputPath.substr(inputPath.size() - 4) == ".s3d" ||
                  inputPath.substr(inputPath.size() - 4) == ".S3D");

    std::vector<char> wldData;
    std::string wldFilename;

    if (isS3d) {
        // Extract from S3D
        PfsArchive archive;
        if (!archive.open(inputPath)) {
            LOG_ERROR(MOD_MAIN, "Failed to open S3D archive: {}", inputPath);
            return 1;
        }

        const auto& files = archive.getFiles();

        // Find WLD files
        std::vector<std::string> wldFiles;
        for (const auto& [name, data] : files) {
            if (name.size() >= 4 &&
                (name.substr(name.size() - 4) == ".wld" ||
                 name.substr(name.size() - 4) == ".WLD")) {
                wldFiles.push_back(name);
            }
        }

        if (wldFiles.empty()) {
            LOG_ERROR(MOD_MAIN, "No WLD files found in archive");
            return 1;
        }

        // Select which WLD to dump
        if (wldName.empty()) {
            if (wldFiles.size() == 1) {
                wldFilename = wldFiles[0];
            } else {
                std::cout << "Multiple WLD files found. Please specify one:" << std::endl;
                for (const auto& name : wldFiles) {
                    std::cout << "  " << name << std::endl;
                }
                return 1;
            }
        } else {
            // Find matching WLD
            for (const auto& name : wldFiles) {
                if (name == wldName || name.find(wldName) != std::string::npos) {
                    wldFilename = name;
                    break;
                }
            }
            if (wldFilename.empty()) {
                LOG_ERROR(MOD_MAIN, "WLD file not found: {}", wldName);
                return 1;
            }
        }

        // Get WLD data
        if (!archive.get(wldFilename, wldData)) {
            LOG_ERROR(MOD_MAIN, "Failed to extract WLD: {}", wldFilename);
            return 1;
        }
    } else {
        // Read directly from file
        std::ifstream file(inputPath, std::ios::binary);
        if (!file) {
            LOG_ERROR(MOD_MAIN, "Failed to open file: {}", inputPath);
            return 1;
        }

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        wldData.resize(size);
        file.read(wldData.data(), size);

        wldFilename = inputPath;
    }

    dumpWld(wldData, wldFilename);

    // Also parse using WldLoader to get BSP tree analysis
    std::cout << std::endl;
    std::cout << "## WldLoader Analysis" << std::endl;
    std::cout << std::endl;

    WldLoader loader;
    bool parsed = false;

    if (isS3d) {
        parsed = loader.parseFromArchive(inputPath, wldFilename);
    } else {
        std::cout << "Note: Full BSP analysis requires S3D archive." << std::endl;
    }

    if (!parsed) {
        std::cout << "WldLoader failed to parse the file." << std::endl;
    } else {
        auto bspTree = loader.getBspTree();
        if (bspTree) {
            std::cout << "### BSP Tree" << std::endl;
            std::cout << std::endl;
            std::cout << "- Nodes: " << bspTree->nodes.size() << std::endl;
            std::cout << "- Regions: " << bspTree->regions.size() << std::endl;
            std::cout << std::endl;

            // Count region types
            size_t zoneLineCount = 0;
            size_t waterCount = 0;
            size_t lavaCount = 0;
            size_t normalCount = 0;
            size_t otherCount = 0;

            for (size_t i = 0; i < bspTree->regions.size(); ++i) {
                const auto& region = bspTree->regions[i];
                bool hasZoneLine = false;
                bool hasWater = false;
                bool hasLava = false;

                for (auto type : region->regionTypes) {
                    switch (type) {
                        case RegionType::Zoneline: hasZoneLine = true; break;
                        case RegionType::Water: hasWater = true; break;
                        case RegionType::Lava: hasLava = true; break;
                        default: break;
                    }
                }

                if (hasZoneLine) {
                    zoneLineCount++;
                    std::cout << "Zone Line Region " << i << ":";
                    if (region->zoneLineInfo) {
                        std::cout << " type=" << (region->zoneLineInfo->type == ZoneLineType::Absolute ? "Absolute" : "Reference")
                                  << " zoneId=" << region->zoneLineInfo->zoneId
                                  << " zonePointIdx=" << region->zoneLineInfo->zonePointIndex
                                  << " coords=(" << region->zoneLineInfo->x << ", " << region->zoneLineInfo->y << ", " << region->zoneLineInfo->z << ")"
                                  << " heading=" << region->zoneLineInfo->heading;
                    }
                    std::cout << std::endl;
                } else if (hasWater) {
                    waterCount++;
                } else if (hasLava) {
                    lavaCount++;
                } else if (region->regionTypes.empty()) {
                    normalCount++;
                } else {
                    otherCount++;
                }
            }

            std::cout << std::endl;
            std::cout << "### Region Type Summary" << std::endl;
            std::cout << std::endl;
            std::cout << "| Type | Count |" << std::endl;
            std::cout << "|------|-------|" << std::endl;
            std::cout << "| Zone Line | " << zoneLineCount << " |" << std::endl;
            std::cout << "| Water | " << waterCount << " |" << std::endl;
            std::cout << "| Lava | " << lavaCount << " |" << std::endl;
            std::cout << "| Normal/Empty | " << normalCount << " |" << std::endl;
            std::cout << "| Other | " << otherCount << " |" << std::endl;
        } else {
            std::cout << "No BSP tree found." << std::endl;
        }

        // Dump resolved lights (after 0x1C indirection)
        const auto& lights = loader.getLights();
        if (!lights.empty()) {
            size_t animatedCount = 0;
            for (const auto& light : lights) {
                if (light->isAnimated()) animatedCount++;
            }

            std::cout << std::endl;
            std::cout << "### Resolved Lights" << std::endl;
            std::cout << std::endl;
            std::cout << "Total: " << lights.size()
                      << " (" << animatedCount << " animated, "
                      << (lights.size() - animatedCount) << " static)" << std::endl;
            std::cout << std::endl;

            std::cout << "| # | Name | Position | Radius | Color | Frames | Sleep |" << std::endl;
            std::cout << "|---|------|----------|--------|-------|--------|-------|" << std::endl;

            for (size_t i = 0; i < lights.size(); i++) {
                const auto& light = lights[i];
                std::cout << "| " << i
                          << " | " << (light->name.empty() ? "(unnamed)" : light->name)
                          << " | (" << std::fixed << std::setprecision(1)
                          << light->x << ", " << light->y << ", " << light->z << ")"
                          << " | " << light->radius
                          << " | (" << std::setprecision(3)
                          << light->r << ", " << light->g << ", " << light->b << ")"
                          << " | " << std::defaultfloat << light->frameCount;
                if (light->isAnimated()) std::cout << " **ANIM**";
                std::cout << " | " << light->sleepMs << "ms"
                          << " |" << std::endl;
            }
            std::cout << std::endl;

            // Detail for animated lights
            if (animatedCount > 0) {
                std::cout << "### Animated Light Details" << std::endl;
                std::cout << std::endl;
                for (size_t i = 0; i < lights.size(); i++) {
                    const auto& light = lights[i];
                    if (!light->isAnimated()) continue;

                    std::cout << "**" << (light->name.empty() ? "(unnamed)" : light->name)
                              << "** (light #" << i << "): "
                              << light->frameCount << " frames, "
                              << light->sleepMs << "ms sleep, flags=0x"
                              << std::hex << light->flags << std::dec << std::endl;

                    if (!light->colors.empty()) {
                        std::cout << "  Colors:" << std::endl;
                        for (size_t f = 0; f < light->colors.size() && f < 16; f++) {
                            auto& [r, g, b] = light->colors[f];
                            std::cout << "    [" << f << "] RGB("
                                      << std::fixed << std::setprecision(3)
                                      << r << ", " << g << ", " << b << ")"
                                      << std::defaultfloat << std::endl;
                        }
                        if (light->colors.size() > 16) {
                            std::cout << "    ...(" << (light->colors.size() - 16) << " more)" << std::endl;
                        }
                    }

                    if (!light->lightLevels.empty()) {
                        std::cout << "  Light Levels: [";
                        for (size_t f = 0; f < light->lightLevels.size() && f < 16; f++) {
                            if (f > 0) std::cout << ", ";
                            std::cout << std::fixed << std::setprecision(3) << light->lightLevels[f];
                        }
                        if (light->lightLevels.size() > 16) {
                            std::cout << ", ...(" << (light->lightLevels.size() - 16) << " more)";
                        }
                        std::cout << "]" << std::defaultfloat << std::endl;
                    }
                    std::cout << std::endl;
                }
            }
        }

        // Dump ambient light info
        const auto& ambientRegions = loader.getAmbientLightRegions();
        if (!ambientRegions.empty()) {
            std::cout << "### Ambient Light Regions (resolved)" << std::endl;
            std::cout << std::endl;
            for (size_t i = 0; i < ambientRegions.size(); i++) {
                const auto& region = ambientRegions[i];
                std::cout << "- " << region->name
                          << "  flags=0x" << std::hex << region->flags << std::dec
                          << "  regionRefs=" << region->regionRefs.size() << std::endl;
            }
            std::cout << std::endl;
        }

        const auto& globalAmbient = loader.getGlobalAmbientLight();
        if (globalAmbient) {
            std::cout << "### Global Ambient Light (resolved)" << std::endl;
            std::cout << std::endl;
            std::cout << "- RGBA(" << std::fixed << std::setprecision(3)
                      << globalAmbient->r << ", " << globalAmbient->g << ", "
                      << globalAmbient->b << ", " << globalAmbient->a << ")"
                      << std::defaultfloat << std::endl;
            std::cout << std::endl;
        }
    }

    return 0;
}
