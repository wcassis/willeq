// WLD File Dump Tool
// Parses and dumps the contents of an EverQuest WLD file

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

std::string getFragmentTypeName(uint32_t type) {
    switch (type) {
        case 0x03: return "TextureBitmapName";
        case 0x04: return "TextureBitmapInfo";
        case 0x05: return "TextureBitmapInfoRef";
        case 0x06: return "Sprite2DDef";
        case 0x07: return "Sprite2DDefRef";
        case 0x08: return "Camera";
        case 0x09: return "CameraRef";
        case 0x10: return "TrackSet";
        case 0x11: return "TrackSetRef";
        case 0x12: return "MobTrack";
        case 0x13: return "MobTrackRef";
        case 0x14: return "PlaceableRef";
        case 0x15: return "ObjectLocation";
        case 0x16: return "ZoneUnknown";
        case 0x17: return "PolygonAnimation";
        case 0x18: return "PolygonAnimationRef";
        case 0x1B: return "LightSource";
        case 0x1C: return "LightSourceRef";
        case 0x21: return "BspTree";
        case 0x22: return "BspRegion";
        case 0x28: return "LightInfo";
        case 0x29: return "RegionFlag";
        case 0x26: return "ParticleSpriteDef";
        case 0x27: return "ParticleSpriteDefRef";
        case 0x2A: return "AmbientLight";
        case 0x2C: return "LegacyMesh";
        case 0x2D: return "MeshReference";
        case 0x2F: return "MeshAnimVertRef";
        case 0x30: return "Texture";
        case 0x31: return "TextureList";
        case 0x32: return "VertexColor";
        case 0x33: return "VertexColorRef";
        case 0x34: return "ParticleCloudDef";
        case 0x35: return "FirstFragment";
        case 0x36: return "Mesh";
        case 0x37: return "MeshAnimVert";
        default: {
            char buf[32];
            snprintf(buf, sizeof(buf), "Unknown(0x%02x)", type);
            return buf;
        }
    }
}

void dumpHex(const char* data, size_t len, size_t maxBytes = 0) {
    size_t toDump = (maxBytes == 0) ? len : std::min(len, maxBytes);
    for (size_t i = 0; i < toDump; i++) {
        if (i > 0 && i % 16 == 0) std::cout << std::endl << "        ";
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (static_cast<unsigned int>(data[i]) & 0xFF) << " ";
    }
    if (maxBytes > 0 && len > maxBytes) {
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

            // Parse mesh header using packed struct (matches main parser)
            const char* meshData = fragPtr + offset + 8;
            size_t meshSize = fragSize;

            if (meshSize >= 4 + sizeof(WldFragment36Header)) {
                WldFragment36Header header;
                std::memcpy(&header, meshData + 4, sizeof(header));  // +4 to skip nameRef

                std::cout << "- Flags: 0x" << std::hex << header.flags << std::dec << std::endl;
                std::cout << "- Material List Ref: " << header.frag1 << std::endl;
                std::cout << "- Center: (" << header.centerX << ", " << header.centerY << ", " << header.centerZ << ")" << std::endl;
                std::cout << "- Bounds: (" << header.minX << "," << header.minY << "," << header.minZ
                          << ") - (" << header.maxX << "," << header.maxY << "," << header.maxZ << ")" << std::endl;
                std::cout << "- Vertices: " << header.vertexCount << std::endl;
                std::cout << "- TexCoords: " << header.texCoordCount << std::endl;
                std::cout << "- Normals: " << header.normalCount << std::endl;
                std::cout << "- Colors: " << header.colorCount << std::endl;
                std::cout << "- Polygons: " << header.polygonCount << std::endl;
                std::cout << "- Vertex Pieces: " << header.size6 << std::endl;
                std::cout << "- Poly Tex Entries: " << header.polygonTexCount << std::endl;
                std::cout << "- Vertex Tex Entries: " << header.vertexTexCount << std::endl;
                std::cout << "- Scale: " << header.scale << std::endl;
                std::cout << std::endl;

                // Dump vertex positions and normals for tree meshes
                if (name.find("TREE") != std::string::npos) {
                    float scale = 1.0f / static_cast<float>(1 << header.scale);
                    float recip128 = 1.0f / 128.0f;
                    const char* vptr = meshData + 4 + sizeof(WldFragment36Header);

                    std::cout << "#### Vertices (EQ coords, Z-up)" << std::endl;
                    for (uint16_t vi = 0; vi < header.vertexCount; ++vi) {
                        int16_t vx = *reinterpret_cast<const int16_t*>(vptr);
                        int16_t vy = *reinterpret_cast<const int16_t*>(vptr + 2);
                        int16_t vz = *reinterpret_cast<const int16_t*>(vptr + 4);
                        vptr += 6;
                        std::cout << "  v[" << vi << "]: (" << vx * scale << ", " << vy * scale << ", " << vz * scale << ")" << std::endl;
                    }

                    // Skip tex coords (old format: 2x int16, new format: 2x float)
                    bool oldFmt = (*reinterpret_cast<const uint32_t*>(fragPtr + offset + 8 + 4) & 0xFF) == 0x15;
                    // Actually just use the version flag from the file header
                    // Old format uses int16 tex coords (4 bytes each), new uses float (8 bytes each)
                    // Since we know Scale=8 for trees in old format zones, skip accordingly
                    if (header.texCoordCount > 0) {
                        // Old format: 4 bytes per texcoord
                        vptr += header.texCoordCount * 4;
                    }

                    std::cout << "#### Normals (EQ coords, Z-up, int8 / 128)" << std::endl;
                    for (uint16_t ni = 0; ni < header.normalCount; ++ni) {
                        int8_t nx = *reinterpret_cast<const int8_t*>(vptr);
                        int8_t ny = *reinterpret_cast<const int8_t*>(vptr + 1);
                        int8_t nz = *reinterpret_cast<const int8_t*>(vptr + 2);
                        vptr += 3;
                        float fnx = nx * recip128;
                        float fny = ny * recip128;
                        float fnz = nz * recip128;
                        float len = std::sqrt(fnx*fnx + fny*fny + fnz*fnz);
                        if (len > 0.0001f) { fnx /= len; fny /= len; fnz /= len; }
                        std::cout << "  n[" << ni << "]: raw(" << (int)nx << "," << (int)ny << "," << (int)nz
                                  << ") -> normalized(" << fnx << ", " << fny << ", " << fnz << ")" << std::endl;
                    }
                    std::cout << std::endl;
                }
            }
        }

        offset += fragSize + 8;
    }

    // Dump light definitions (0x1B)
    std::cout << "## Light Definitions (0x1B)" << std::endl;
    std::cout << std::endl;

    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x1B) {
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

            std::cout << "### Fragment #" << (i + 1) << ": " << name << " (size=" << fragSize << ")" << std::endl;
            std::cout << std::endl;

            // Raw hex dump of fragment body (after nameRef)
            const char* lightData = fragPtr + offset + 8 + 4; // skip nameRef
            size_t lightSize = fragSize - 4;
            std::cout << "- Raw: ";
            dumpHex(lightData, lightSize);

            if (lightSize >= 8) {
                uint32_t flags = *reinterpret_cast<const uint32_t*>(lightData);
                uint32_t frameCount = *reinterpret_cast<const uint32_t*>(lightData + 4);
                const char* p = lightData + 8;

                std::cout << "- Flags: 0x" << std::hex << flags << std::dec;
                if (flags & 0x01) std::cout << " [HasCurrentFrame]";
                if (flags & 0x02) std::cout << " [HasSleep]";
                if (flags & 0x04) std::cout << " [HasLightLevels]";
                if (flags & 0x08) std::cout << " [SkipFrames]";
                if (flags & 0x10) std::cout << " [HasColor]";
                std::cout << std::endl;
                std::cout << "- Frame Count: " << frameCount;
                if (frameCount > 1) std::cout << " **(ANIMATED)**";
                std::cout << std::endl;

                if (flags & 0x01) {
                    uint32_t currentFrame = *reinterpret_cast<const uint32_t*>(p);
                    std::cout << "- Current Frame: " << currentFrame << std::endl;
                    p += 4;
                }

                if (flags & 0x02) {
                    uint32_t sleepMs = *reinterpret_cast<const uint32_t*>(p);
                    std::cout << "- Sleep: " << sleepMs << " ms" << std::endl;
                    p += 4;
                }

                if (flags & 0x04) {
                    std::cout << "- Light Levels: [";
                    for (uint32_t f = 0; f < frameCount && f < 16; f++) {
                        if (f > 0) std::cout << ", ";
                        float level = *reinterpret_cast<const float*>(p);
                        std::cout << std::fixed << std::setprecision(3) << level;
                        p += 4;
                    }
                    if (frameCount > 16) {
                        std::cout << ", ...(" << (frameCount - 16) << " more)";
                        p += (frameCount - 16) * 4;
                    }
                    std::cout << "]" << std::defaultfloat << std::endl;
                }

                if (flags & 0x10) {
                    std::cout << "- Colors:" << std::endl;
                    for (uint32_t f = 0; f < frameCount && f < 8; f++) {
                        float r = *reinterpret_cast<const float*>(p);
                        float g = *reinterpret_cast<const float*>(p + 4);
                        float b = *reinterpret_cast<const float*>(p + 8);
                        std::cout << "    [" << f << "] RGB("
                                  << std::fixed << std::setprecision(3)
                                  << r << ", " << g << ", " << b << ")" << std::defaultfloat << std::endl;
                        p += 12;
                    }
                    if (frameCount > 8) {
                        std::cout << "    ...(" << (frameCount - 8) << " more frames)" << std::endl;
                    }
                }
            }
            std::cout << std::endl;
        }

        offset += fragSize + 8;
    }

    // Dump light references (0x1C)
    std::cout << "## Light References (0x1C)" << std::endl;
    std::cout << std::endl;

    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x1C) {
            const char* refData = fragPtr + offset + 8 + 4; // skip nameRef
            if (fragSize >= 8) {
                int32_t ref = *reinterpret_cast<const int32_t*>(refData);
                std::cout << "- Fragment #" << (i + 1) << " -> LightDef #" << ref << std::endl;
            }
        }

        offset += fragSize + 8;
    }
    std::cout << std::endl;

    // Dump light instances (0x28)
    std::cout << "## Light Instances (0x28)" << std::endl;
    std::cout << std::endl;

    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x28) {
            const char* instData = fragPtr + offset + 8;
            // No nameRef for 0x28 - starts directly with ref
            if (fragSize >= 20) {
                int32_t ref = *reinterpret_cast<const int32_t*>(instData);
                uint32_t instFlags = *reinterpret_cast<const uint32_t*>(instData + 4);
                float x = *reinterpret_cast<const float*>(instData + 8);
                float y = *reinterpret_cast<const float*>(instData + 12);
                float z = *reinterpret_cast<const float*>(instData + 16);
                float radius = *reinterpret_cast<const float*>(instData + 20);

                std::cout << "- Fragment #" << (i + 1)
                          << " -> Ref #" << ref
                          << "  pos=(" << std::fixed << std::setprecision(1)
                          << x << ", " << y << ", " << z << ")"
                          << "  radius=" << radius
                          << "  flags=0x" << std::hex << instFlags << std::dec
                          << std::defaultfloat << std::endl;
            }
        }

        offset += fragSize + 8;
    }
    std::cout << std::endl;

    // Dump ambient light regions (0x2A)
    std::cout << "## Ambient Light Regions (0x2A)" << std::endl;
    std::cout << std::endl;

    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x2A) {
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

            const char* ambData = fragPtr + offset + 8 + 4; // skip nameRef
            size_t ambSize = fragSize - 4;

            if (ambSize >= 8) {
                uint32_t ambFlags = *reinterpret_cast<const uint32_t*>(ambData);
                uint32_t regionCount = *reinterpret_cast<const uint32_t*>(ambData + 4);

                std::cout << "- Fragment #" << (i + 1) << ": " << name
                          << "  flags=0x" << std::hex << ambFlags << std::dec
                          << "  regions=" << regionCount;

                if (regionCount > 0 && ambSize >= 8 + regionCount * 4) {
                    std::cout << " [";
                    for (uint32_t r = 0; r < regionCount && r < 10; r++) {
                        if (r > 0) std::cout << ", ";
                        int32_t regionRef = *reinterpret_cast<const int32_t*>(ambData + 8 + r * 4);
                        std::cout << regionRef;
                    }
                    if (regionCount > 10) std::cout << ", ...(" << (regionCount - 10) << " more)";
                    std::cout << "]";
                }
                std::cout << std::endl;
            }
        }

        offset += fragSize + 8;
    }
    std::cout << std::endl;

    // Dump global ambient light (0x35)
    offset = 0;
    for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
        uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
        uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

        if (fragType == 0x35) {
            std::cout << "## Global Ambient Light (0x35)" << std::endl;
            std::cout << std::endl;

            const char* globData = fragPtr + offset + 8;
            // 0x35 has no nameRef, starts with 4 bytes (unknown/flags) then RGBA color
            if (fragSize >= 20) {
                // Skip first 4 bytes (flags/unknown), then read 4 floats
                float r = *reinterpret_cast<const float*>(globData + 4);
                float g = *reinterpret_cast<const float*>(globData + 8);
                float b = *reinterpret_cast<const float*>(globData + 12);
                float a = *reinterpret_cast<const float*>(globData + 16);
                std::cout << "- RGBA(" << std::fixed << std::setprecision(3)
                          << r << ", " << g << ", " << b << ", " << a << ")"
                          << std::defaultfloat << std::endl;
            }
            std::cout << std::endl;
        }

        offset += fragSize + 8;
    }

    // Helper lambda to look up a name from the string table by nameRef
    auto lookupName = [&strings](int32_t nameRef) -> std::string {
        if (nameRef < 0) {
            int idx = -nameRef;
            for (const auto& [sIdx, sName] : strings) {
                if (sIdx == -idx) return sName;
            }
        }
        return "";
    };

    // Dump bone tracks (0x12) with frame counts
    if (typeCounts.count(0x12)) {
        std::cout << "## Bone Tracks (0x12) — " << typeCounts[0x12] << " fragments" << std::endl;
        std::cout << std::endl;

        // Summary table first
        std::cout << "| # | Name | Flags | Frames | Size | Bytes/Frame |" << std::endl;
        std::cout << "|---|------|-------|--------|------|-------------|" << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x12) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                if (bodySize >= 8) {
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(body);
                    uint32_t frameCount = *reinterpret_cast<const uint32_t*>(body + 4);
                    size_t dataBytes = bodySize - 8;
                    size_t bytesPerFrame = frameCount > 0 ? dataBytes / frameCount : 0;

                    std::cout << "| " << (i + 1)
                              << " | " << name
                              << " | 0x" << std::hex << flags << std::dec
                              << " | " << frameCount
                              << " | " << fragSize
                              << " | " << bytesPerFrame
                              << " |" << std::endl;
                }
            }
            offset += fragSize + 8;
        }
        std::cout << std::endl;

        // Show first few frames of a multi-frame track as example
        offset = 0;
        bool shownExample = false;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining && !shownExample; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x12) {
                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                if (bodySize >= 8) {
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(body);
                    uint32_t fc = *reinterpret_cast<const uint32_t*>(body + 4);
                    if (fc > 1 && bodySize >= 8 + fc * 16) {
                        int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                        std::string name = lookupName(nameRef);

                        std::cout << "### Example: " << name << " (" << fc << " frames)" << std::endl;
                        std::cout << std::endl;
                        std::cout << "| Frame | RotW | RotX | RotY | RotZ | ShiftX | ShiftY | ShiftZ | Scale |" << std::endl;
                        std::cout << "|-------|------|------|------|------|--------|--------|--------|-------|" << std::endl;

                        const char* fp = body + 8;
                        uint32_t showFrames = std::min(fc, 10u);
                        for (uint32_t f = 0; f < showFrames; f++) {
                            int16_t rw = *reinterpret_cast<const int16_t*>(fp);
                            int16_t rx = *reinterpret_cast<const int16_t*>(fp + 2);
                            int16_t ry = *reinterpret_cast<const int16_t*>(fp + 4);
                            int16_t rz = *reinterpret_cast<const int16_t*>(fp + 6);
                            int16_t sx = *reinterpret_cast<const int16_t*>(fp + 8);
                            int16_t sy = *reinterpret_cast<const int16_t*>(fp + 10);
                            int16_t sz = *reinterpret_cast<const int16_t*>(fp + 12);
                            int16_t sd = *reinterpret_cast<const int16_t*>(fp + 14);
                            fp += 16;

                            std::cout << "| " << f
                                      << " | " << rw << " | " << rx << " | " << ry << " | " << rz
                                      << " | " << sx << " | " << sy << " | " << sz
                                      << " | " << sd << " |" << std::endl;
                        }
                        if (fc > showFrames) {
                            std::cout << "| ... | (" << (fc - showFrames) << " more frames) |" << std::endl;
                        }
                        std::cout << std::endl;
                        shownExample = true;
                    }
                }
            }
            offset += fragSize + 8;
        }
    }

    // Dump bone track references (0x13)
    if (typeCounts.count(0x13)) {
        std::cout << "## Bone Track References (0x13) — " << typeCounts[0x13] << " fragments" << std::endl;
        std::cout << std::endl;
        std::cout << "| # | Name | Ref | Flags | FrameMs |" << std::endl;
        std::cout << "|---|------|-----|-------|---------|" << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x13) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                if (bodySize >= 8) {
                    int32_t ref = *reinterpret_cast<const int32_t*>(body);
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(body + 4);
                    int32_t frameMs = 0;
                    if ((flags & 1) && bodySize >= 12) {
                        frameMs = *reinterpret_cast<const int32_t*>(body + 8);
                    }

                    std::cout << "| " << (i + 1)
                              << " | " << name
                              << " | " << ref
                              << " | 0x" << std::hex << flags << std::dec
                              << " | " << frameMs
                              << " |" << std::endl;
                }
            }
            offset += fragSize + 8;
        }
        std::cout << std::endl;
    }

    // Dump actor definitions (0x14)
    if (typeCounts.count(0x14)) {
        std::cout << "## Actor Definitions (0x14) — " << typeCounts[0x14] << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x14) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                std::cout << "### " << name << " (frag #" << (i + 1) << ", size=" << fragSize << ")" << std::endl;

                if (bodySize >= sizeof(WldFragment14Header)) {
                    WldFragment14Header hdr;
                    std::memcpy(&hdr, body, sizeof(hdr));

                    std::string refName = "";
                    if (hdr.ref < 0) {
                        refName = lookupName(hdr.ref);
                    }

                    std::cout << "- Flags: 0x" << std::hex << hdr.flags << std::dec << std::endl;
                    std::cout << "- Ref: " << hdr.ref;
                    if (!refName.empty()) std::cout << " (" << refName << ")";
                    std::cout << std::endl;
                    std::cout << "- Entries: " << hdr.entries << std::endl;
                    std::cout << "- Entries2 (mesh refs): " << hdr.entries2 << std::endl;
                    std::cout << "- Ref2: " << hdr.ref2 << std::endl;

                    // Skip variable-length entry data, read mesh refs
                    const char* p = body + sizeof(WldFragment14Header);
                    size_t pRemaining = bodySize - sizeof(WldFragment14Header);

                    // Skip flags-conditional fields
                    if (hdr.flags & 1) { p += 4; pRemaining -= 4; }
                    if (hdr.flags & 2) { p += 4; pRemaining -= 4; }

                    // Skip entries (each has a uint32 count + count*(int32+float) pairs)
                    for (uint32_t e = 0; e < hdr.entries && pRemaining >= 4; e++) {
                        uint32_t sz = *reinterpret_cast<const uint32_t*>(p);
                        p += 4; pRemaining -= 4;
                        size_t skip = sz * 8;
                        if (skip <= pRemaining) { p += skip; pRemaining -= skip; }
                    }

                    // Read entries2 mesh refs
                    if (hdr.entries2 > 0) {
                        std::cout << "- Mesh Refs: [";
                        for (uint32_t e = 0; e < hdr.entries2 && pRemaining >= 4; e++) {
                            uint32_t mref = *reinterpret_cast<const uint32_t*>(p);
                            p += 4; pRemaining -= 4;
                            if (e > 0) std::cout << ", ";
                            std::cout << mref;
                        }
                        std::cout << "]" << std::endl;
                    }
                }
                std::cout << std::endl;
            }
            offset += fragSize + 8;
        }
    }

    // Dump object locations (0x15)
    if (typeCounts.count(0x15)) {
        std::cout << "## Object Locations (0x15) — " << typeCounts[0x15] << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x15) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                // First field is an object ref (string hash ref, not fragment ref)
                if (bodySize >= 4) {
                    int32_t objRef = *reinterpret_cast<const int32_t*>(body);
                    std::string objName = "";
                    if (objRef < 0) {
                        for (const auto& [sIdx, sName] : strings) {
                            if (sIdx == objRef) { objName = sName; break; }
                        }
                    }

                    std::cout << "### " << name << " (frag #" << (i + 1) << ")" << std::endl;
                    std::cout << "- Object Ref: " << objRef;
                    if (!objName.empty()) std::cout << " (" << objName << ")";
                    std::cout << std::endl;

                    if (bodySize >= 4 + sizeof(WldFragment15Header)) {
                        WldFragment15Header hdr;
                        std::memcpy(&hdr, body + 4, sizeof(hdr));

                        std::cout << "- Flags: 0x" << std::hex << hdr.flags << std::dec << std::endl;
                        std::cout << "- Position: (" << std::fixed << std::setprecision(2)
                                  << hdr.x << ", " << hdr.y << ", " << hdr.z << ")" << std::defaultfloat << std::endl;
                        std::cout << "- Rotation: (" << hdr.rotateX << ", " << hdr.rotateY << ", " << hdr.rotateZ << ")" << std::endl;
                        std::cout << "- Scale: (" << hdr.scaleX << ", " << hdr.scaleY << ")" << std::endl;
                    }
                    std::cout << std::endl;
                }
            }
            offset += fragSize + 8;
        }
    }

    // Dump polygon animation (0x17) — UNKNOWN FORMAT, structured hex analysis
    if (typeCounts.count(0x17)) {
        std::cout << "## Polygon Animation (0x17) — " << typeCounts[0x17] << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x17) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                std::cout << "### " << name << " (frag #" << (i + 1) << ", size=" << fragSize << ")" << std::endl;

                // Try to interpret structure: first fields are likely flags and counts
                if (bodySize >= 4) {
                    uint32_t field0 = *reinterpret_cast<const uint32_t*>(body);
                    std::cout << "- Field[0] (uint32): " << field0 << " (0x" << std::hex << field0 << std::dec << ")" << std::endl;
                }
                if (bodySize >= 8) {
                    uint32_t field1 = *reinterpret_cast<const uint32_t*>(body + 4);
                    std::cout << "- Field[1] (uint32): " << field1 << " (0x" << std::hex << field1 << std::dec << ")" << std::endl;
                }
                if (bodySize >= 12) {
                    uint32_t field2 = *reinterpret_cast<const uint32_t*>(body + 8);
                    std::cout << "- Field[2] (uint32): " << field2 << " (0x" << std::hex << field2 << std::dec << ")" << std::endl;
                }
                if (bodySize >= 16) {
                    // Also try reading as int16 pairs in case it's packed like 0x12
                    std::cout << "- Field[3] (uint32): " << *reinterpret_cast<const uint32_t*>(body + 12)
                              << " (as int16 pair: " << *reinterpret_cast<const int16_t*>(body + 12)
                              << ", " << *reinterpret_cast<const int16_t*>(body + 14) << ")" << std::endl;
                }

                // Structured hex: show as uint32 words
                std::cout << "- Words (" << bodySize / 4 << "):" << std::endl;
                for (size_t w = 0; w < bodySize / 4; w++) {
                    uint32_t val = *reinterpret_cast<const uint32_t*>(body + w * 4);
                    float fval;
                    std::memcpy(&fval, &val, 4);
                    int16_t lo = static_cast<int16_t>(val & 0xFFFF);
                    int16_t hi = static_cast<int16_t>((val >> 16) & 0xFFFF);
                    std::cout << "    [" << std::setw(2) << w << "] 0x" << std::hex << std::setw(8) << std::setfill('0') << val << std::dec << std::setfill(' ')
                              << "  u32=" << std::setw(10) << val
                              << "  i16=(" << std::setw(6) << lo << "," << std::setw(6) << hi << ")"
                              << "  f32=" << std::fixed << std::setprecision(4) << fval << std::defaultfloat
                              << std::endl;
                }
                // Raw hex for cross-reference
                std::cout << "- Raw hex:" << std::endl << "        ";
                dumpHex(body, bodySize);
                std::cout << std::endl;
            }
            offset += fragSize + 8;
        }
    }

    // Dump polygon animation reference (0x18)
    if (typeCounts.count(0x18)) {
        std::cout << "## Polygon Animation Reference (0x18) — " << typeCounts[0x18] << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x18) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                std::cout << "### " << name << " (frag #" << (i + 1) << ", size=" << fragSize << ")" << std::endl;

                if (bodySize >= 4) {
                    int32_t ref = *reinterpret_cast<const int32_t*>(body);
                    std::cout << "- Ref: " << ref << " -> frag #" << ref << std::endl;
                }
                if (bodySize >= 8) {
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(body + 4);
                    std::cout << "- Flags: 0x" << std::hex << flags << std::dec << std::endl;
                }
                // Dump any remaining data
                if (bodySize > 8) {
                    std::cout << "- Extra (" << (bodySize - 8) << " bytes):" << std::endl << "        ";
                    dumpHex(body + 8, bodySize - 8, 64);
                }
                std::cout << std::endl;
            }
            offset += fragSize + 8;
        }
    }

    // Dump mesh animated vertices (0x37)
    if (typeCounts.count(0x37)) {
        std::cout << "## Mesh Animated Vertices (0x37) — " << typeCounts[0x37] << " fragments" << std::endl;
        std::cout << std::endl;
        std::cout << "| # | Name | Flags | Vertices | Frames | Delay(ms) | Scale | Data Size |" << std::endl;
        std::cout << "|---|------|-------|----------|--------|-----------|-------|-----------|" << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x37) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                if (bodySize >= 10) {
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(body);
                    uint16_t vertCount = *reinterpret_cast<const uint16_t*>(body + 4);
                    uint16_t frameCount = *reinterpret_cast<const uint16_t*>(body + 6);
                    uint16_t delayMs = *reinterpret_cast<const uint16_t*>(body + 8);
                    uint16_t param2 = (bodySize >= 12) ? *reinterpret_cast<const uint16_t*>(body + 10) : 0;
                    int16_t scale = (bodySize >= 14) ? *reinterpret_cast<const int16_t*>(body + 12) : 0;
                    size_t expectedData = static_cast<size_t>(vertCount) * frameCount * 6;

                    std::cout << "| " << (i + 1)
                              << " | " << name
                              << " | 0x" << std::hex << flags << std::dec
                              << " | " << vertCount
                              << " | " << frameCount
                              << " | " << delayMs
                              << " | " << scale
                              << " | " << expectedData << " (expected " << vertCount * frameCount * 6 << ")"
                              << " |" << std::endl;
                }
            }
            offset += fragSize + 8;
        }
        std::cout << std::endl;
    }

    // Dump mesh animated vertices references (0x2F)
    if (typeCounts.count(0x2F)) {
        std::cout << "## Mesh Animated Vertices References (0x2F) — " << typeCounts[0x2F] << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x2F) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                std::cout << "- Fragment #" << (i + 1) << ": " << name;
                if (bodySize >= 4) {
                    int32_t ref = *reinterpret_cast<const int32_t*>(body);
                    std::cout << " -> ref #" << ref;
                }
                if (bodySize >= 8) {
                    uint32_t flags = *reinterpret_cast<const uint32_t*>(body + 4);
                    std::cout << "  flags=0x" << std::hex << flags << std::dec;
                }
                std::cout << std::endl;
            }
            offset += fragSize + 8;
        }
        std::cout << std::endl;
    }

    // Dump mesh references (0x2D)
    if (typeCounts.count(0x2D)) {
        std::cout << "## Mesh References (0x2D) — " << typeCounts[0x2D] << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == 0x2D) {
                int32_t nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                std::string name = lookupName(nameRef);

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                std::cout << "- Fragment #" << (i + 1) << ": " << name;
                if (bodySize >= 4) {
                    int32_t ref = *reinterpret_cast<const int32_t*>(body);
                    std::cout << " -> mesh frag #" << ref;
                }
                std::cout << std::endl;
            }
            offset += fragSize + 8;
        }
        std::cout << std::endl;
    }

    // Dump any remaining fragment types with hex analysis
    std::set<uint32_t> detailedTypes = {
        0x03, 0x04, 0x05, 0x10, 0x12, 0x13, 0x14, 0x15, 0x17, 0x18,
        0x1B, 0x1C, 0x21, 0x22, 0x28, 0x29, 0x2A, 0x2C, 0x2D, 0x2F,
        0x30, 0x31, 0x35, 0x36, 0x37
    };
    for (const auto& [type, count] : typeCounts) {
        if (detailedTypes.count(type)) continue;

        std::cout << "## " << getFragmentTypeName(type) << " (0x"
                  << std::hex << std::setw(2) << std::setfill('0') << type << std::dec
                  << ") — " << count << " fragments" << std::endl;
        std::cout << std::endl;

        offset = 0;
        for (uint32_t i = 0; i < fragmentCount && offset + 8 <= remaining; i++) {
            uint32_t fragSize = *reinterpret_cast<const uint32_t*>(fragPtr + offset);
            uint32_t fragType = *reinterpret_cast<const uint32_t*>(fragPtr + offset + 4);

            if (fragType == type) {
                int32_t nameRef = 0;
                if (offset + 12 <= remaining) {
                    nameRef = *reinterpret_cast<const int32_t*>(fragPtr + offset + 8);
                }
                std::string name = lookupName(nameRef);

                std::cout << "### Fragment #" << (i + 1) << ": " << name
                          << " (size=" << fragSize << ", nameRef=" << nameRef << ")" << std::endl;

                const char* body = fragPtr + offset + 8 + 4;
                size_t bodySize = (fragSize > 4) ? fragSize - 4 : 0;

                // Show as uint32 words + raw interpretations
                if (bodySize > 0) {
                    std::cout << "  Words (" << bodySize / 4 << "):" << std::endl;
                    for (size_t w = 0; w < bodySize / 4; w++) {
                        uint32_t val = *reinterpret_cast<const uint32_t*>(body + w * 4);
                        float fval;
                        std::memcpy(&fval, &val, 4);
                        int16_t lo = static_cast<int16_t>(val & 0xFFFF);
                        int16_t hi = static_cast<int16_t>((val >> 16) & 0xFFFF);
                        std::cout << "    [" << std::setw(2) << w << "] 0x" << std::hex << std::setw(8) << std::setfill('0') << val << std::dec << std::setfill(' ')
                                  << "  u32=" << std::setw(10) << val
                                  << "  i16=(" << std::setw(6) << lo << "," << std::setw(6) << hi << ")"
                                  << "  f32=" << std::fixed << std::setprecision(4) << fval << std::defaultfloat
                                  << std::endl;
                    }
                    std::cout << "  Raw hex:" << std::endl << "        ";
                    dumpHex(body, bodySize);
                }
                std::cout << std::endl;
            }
            offset += fragSize + 8;
        }
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
