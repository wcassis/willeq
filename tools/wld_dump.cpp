// WLD File Dump Tool
// Parses and dumps the contents of an EverQuest WLD file

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>

using namespace EQT::Graphics;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <file.wld|archive.s3d> [wld_name]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Dumps the contents of a WLD file." << std::endl;
    std::cerr << "Can read directly from a .wld file or extract from an .s3d archive." << std::endl;
    std::cerr << "If reading from .s3d, optionally specify which .wld to dump." << std::endl;
}

std::string getFragmentTypeName(uint32_t type) {
    switch (type) {
        case 0x03: return "BitmapName";
        case 0x04: return "BitmapInfo";
        case 0x05: return "BitmapInfoRef";
        case 0x10: return "SkeletonHierarchy";
        case 0x11: return "SkeletonHierarchyRef";
        case 0x12: return "TrackDef";
        case 0x13: return "TrackDefRef";
        case 0x14: return "Actor";
        case 0x15: return "ActorInstance";
        case 0x21: return "BspTree";
        case 0x22: return "BspRegion";
        case 0x29: return "Region";
        case 0x2a: return "AmbientLight";
        case 0x2c: return "LegacyMesh";
        case 0x2d: return "MeshReference";
        case 0x30: return "Material";
        case 0x31: return "MaterialList";
        case 0x32: return "VertexColors";
        case 0x33: return "VertexColorsRef";
        case 0x36: return "Mesh";
        default: return "Unknown";
    }
}

void dumpHex(const char* data, size_t len, size_t maxBytes = 64) {
    size_t toDump = std::min(len, maxBytes);
    for (size_t i = 0; i < toDump; i++) {
        if (i > 0 && i % 16 == 0) std::cout << std::endl << "        ";
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (static_cast<unsigned int>(data[i]) & 0xFF) << " ";
    }
    if (len > maxBytes) {
        std::cout << "... (" << std::dec << (len - maxBytes) << " more bytes)";
    }
    std::cout << std::dec << std::endl;
}

void dumpWld(const std::vector<char>& data, const std::string& name) {
    if (data.size() < 28) {
        LOG_ERROR(MOD_MAIN, "WLD file too small ({} bytes, need at least 28)", data.size());
        return;
    }

    const char* ptr = data.data();

    // Header
    uint32_t magic = *reinterpret_cast<const uint32_t*>(ptr);

    // Check magic number first
    if (magic != 0x54503D02) {
        LOG_ERROR(MOD_MAIN, "Invalid WLD magic number: 0x{:08x} (expected: 0x54503D02)", magic);
        return;
    }

    uint32_t version = *reinterpret_cast<const uint32_t*>(ptr + 4);
    uint32_t fragmentCount = *reinterpret_cast<const uint32_t*>(ptr + 8);
    uint32_t bspRegionCount = *reinterpret_cast<const uint32_t*>(ptr + 12);
    uint32_t unk1 = *reinterpret_cast<const uint32_t*>(ptr + 16);
    uint32_t stringHashSize = *reinterpret_cast<const uint32_t*>(ptr + 20);
    uint32_t unk2 = *reinterpret_cast<const uint32_t*>(ptr + 24);

    // Validate string hash size
    if (stringHashSize > data.size() - 28) {
        LOG_ERROR(MOD_MAIN, "Invalid string hash size: {} (file size: {})", stringHashSize, data.size());
        return;
    }

    std::cout << "# WLD File: " << name << std::endl;
    std::cout << std::endl;
    std::cout << "## Header" << std::endl;
    std::cout << std::endl;
    std::cout << "| Field | Value |" << std::endl;
    std::cout << "|-------|-------|" << std::endl;
    std::cout << "| Magic | 0x" << std::hex << magic << std::dec;
    if (magic == 0x54503D02) {
        std::cout << " (valid WLD)";
    }
    std::cout << " |" << std::endl;
    std::cout << "| Version | 0x" << std::hex << version << std::dec;
    if (version == 0x00015500) {
        std::cout << " (old format)";
    } else if (version == 0x1000C800) {
        std::cout << " (new format)";
    }
    std::cout << " |" << std::endl;
    std::cout << "| Fragment Count | " << fragmentCount << " |" << std::endl;
    std::cout << "| BSP Region Count | " << bspRegionCount << " |" << std::endl;
    std::cout << "| Unknown1 | " << unk1 << " |" << std::endl;
    std::cout << "| String Hash Size | " << stringHashSize << " bytes |" << std::endl;
    std::cout << "| Unknown2 | " << unk2 << " |" << std::endl;
    std::cout << std::endl;

    // String hash
    size_t headerSize = 28;
    const char* stringHash = ptr + headerSize;

    // Decode string hash using 8-byte XOR key (use function from wld_loader.h)
    std::vector<char> decodedStrings(stringHashSize);
    std::memcpy(decodedStrings.data(), stringHash, stringHashSize);
    EQT::Graphics::decodeStringHash(decodedStrings.data(), stringHashSize);

    std::cout << "## String Table" << std::endl;
    std::cout << std::endl;

    // Count and list strings
    // In WLD files, strings are referenced by negative offset into the string hash
    std::vector<std::pair<int, std::string>> strings;
    size_t pos = 0;
    while (pos < stringHashSize) {
        size_t start = pos;
        // Find end of string (null terminator)
        while (pos < stringHashSize && decodedStrings[pos] != '\0') {
            pos++;
        }
        if (pos > start) {
            std::string s(&decodedStrings[start], pos - start);
            // Filter to only printable ASCII strings
            bool isPrintable = true;
            for (char c : s) {
                if (c < 32 || c > 126) {
                    isPrintable = false;
                    break;
                }
            }
            if (isPrintable && s.length() >= 2) {
                // Store with negative offset (how WLD references strings)
                strings.push_back({-static_cast<int>(start), s});
            }
        }
        pos++; // skip null
    }

    std::cout << "Total strings: " << strings.size() << std::endl;
    std::cout << std::endl;

    // Show all strings
    std::cout << "| Index | Name |" << std::endl;
    std::cout << "|-------|------|" << std::endl;
    for (size_t i = 0; i < strings.size(); i++) {
        std::cout << "| " << strings[i].first << " | " << strings[i].second << " |" << std::endl;
    }
    std::cout << std::endl;

    // Fragments
    const char* fragPtr = ptr + headerSize + stringHashSize;
    size_t remaining = data.size() - headerSize - stringHashSize;

    std::cout << "## Fragments" << std::endl;
    std::cout << std::endl;

    // Count fragment types
    std::map<uint32_t, int> typeCounts;
    std::vector<std::tuple<uint32_t, uint32_t, int32_t, size_t>> fragments; // type, size, nameRef, offset

    size_t offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);
        int32_t nameRef = 0;
        if (offset + 12 <= remaining) {
            nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
        }

        typeCounts[fragType]++;
        fragments.push_back({fragType, fragSize, nameRef, offset});

        offset += fragSize + 8; // size doesn't include size/type fields
    }

    std::cout << "### Fragment Type Summary" << std::endl;
    std::cout << std::endl;
    std::cout << "| Type | Name | Count |" << std::endl;
    std::cout << "|------|------|-------|" << std::endl;
    for (const auto& [type, count] : typeCounts) {
        std::cout << "| 0x" << std::hex << std::setw(2) << std::setfill('0') << type
                  << std::dec << " | " << getFragmentTypeName(type)
                  << " | " << count << " |" << std::endl;
    }
    std::cout << std::endl;

    // Detailed fragment listing (all)
    std::cout << "### Fragment Details" << std::endl;
    std::cout << std::endl;
    std::cout << "| # | Type | Name | Size | NameRef | Name |" << std::endl;
    std::cout << "|---|------|------|------|---------|------|" << std::endl;

    for (size_t i = 0; i < fragments.size(); i++) {
        auto& [type, size, nameRef, off] = fragments[i];
        std::string typeName = getFragmentTypeName(type);

        // Look up name from string table
        std::string name = "";
        if (nameRef < 0) {
            int idx = -nameRef;
            for (const auto& [sIdx, sName] : strings) {
                if (sIdx == -idx) {
                    name = sName;
                    break;
                }
            }
        }

        std::cout << "| " << (i + 1)
                  << " | 0x" << std::hex << std::setw(2) << std::setfill('0') << type << std::dec
                  << " | " << typeName
                  << " | " << size
                  << " | " << nameRef
                  << " | " << name
                  << " |" << std::endl;
    }
    std::cout << std::endl;

    // Dump specific fragment types in detail
    std::cout << "## Skeleton Hierarchies (0x10)" << std::endl;
    std::cout << std::endl;

    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x10) {
            int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);

            std::string name = "";
            if (nameRef < 0) {
                int idx = -nameRef;
                for (const auto& [sIdx, sName] : strings) {
                    if (sIdx == -idx) {
                        name = sName;
                        break;
                    }
                }
            }

            std::cout << "### " << name << std::endl;
            std::cout << std::endl;

            // Parse skeleton structure
            const char* skelData = fragPtr + offset + 8;
            size_t skelSize = fragSize;

            if (skelSize >= 12) {
                uint32_t flags = *reinterpret_cast<const uint32_t*>(skelData + 4);
                uint32_t boneCount = *reinterpret_cast<const uint32_t*>(skelData + 8);

                std::cout << "- Flags: 0x" << std::hex << flags << std::dec << std::endl;
                std::cout << "- Bone Count: " << boneCount << std::endl;
                std::cout << std::endl;
            }
        }

        offset += fragSize + 8;
    }

    // Dump meshes (0x36)
    std::cout << "## Meshes (0x36)" << std::endl;
    std::cout << std::endl;

    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x36) {
            int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);

            std::string name = "";
            if (nameRef < 0) {
                int idx = -nameRef;
                for (const auto& [sIdx, sName] : strings) {
                    if (sIdx == -idx) {
                        name = sName;
                        break;
                    }
                }
            }

            std::cout << "### " << name << std::endl;
            std::cout << std::endl;

            // Parse mesh header
            const char* meshData = fragPtr + offset + 8;
            size_t meshSize = fragSize;

            if (meshSize >= 40) {
                uint32_t flags = *reinterpret_cast<const uint32_t*>(meshData + 4);
                int32_t matListRef = *reinterpret_cast<const int32_t*>(meshData + 8);
                int32_t animVertRef = *reinterpret_cast<const int32_t*>(meshData + 12);
                // Skip center (3 floats) and params (3 values)
                uint16_t vertexCount = *reinterpret_cast<const uint16_t*>(meshData + 36);
                uint16_t texCoordCount = *reinterpret_cast<const uint16_t*>(meshData + 38);
                uint16_t normalCount = *reinterpret_cast<const uint16_t*>(meshData + 40);
                uint16_t colorCount = *reinterpret_cast<const uint16_t*>(meshData + 42);
                uint16_t polyCount = *reinterpret_cast<const uint16_t*>(meshData + 44);
                uint16_t vertexPieceCount = *reinterpret_cast<const uint16_t*>(meshData + 46);
                uint16_t polyTexCount = *reinterpret_cast<const uint16_t*>(meshData + 48);
                uint16_t vertexTexCount = *reinterpret_cast<const uint16_t*>(meshData + 50);

                std::cout << "- Flags: 0x" << std::hex << flags << std::dec << std::endl;
                std::cout << "- Material List Ref: " << matListRef << std::endl;
                std::cout << "- Vertices: " << vertexCount << std::endl;
                std::cout << "- TexCoords: " << texCoordCount << std::endl;
                std::cout << "- Normals: " << normalCount << std::endl;
                std::cout << "- Colors: " << colorCount << std::endl;
                std::cout << "- Polygons: " << polyCount << std::endl;
                std::cout << "- Vertex Pieces: " << vertexPieceCount << std::endl;
                std::cout << "- Poly Tex Entries: " << polyTexCount << std::endl;
                std::cout << "- Vertex Tex Entries: " << vertexTexCount << std::endl;
                std::cout << std::endl;
            }
        }

        offset += fragSize + 8;
    }
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
    }

    return 0;
}
