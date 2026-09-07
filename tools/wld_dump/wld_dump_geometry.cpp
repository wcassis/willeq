// wld_dump_geometry.cpp — dumpWld()'s geometry and lighting phase: skeleton
// hierarchies (0x10), meshes (0x36), light definitions/references/instances
// (0x1B/0x1C/0x28), ambient light regions (0x2A) and the global ambient
// light (0x35). T447; bodies moved verbatim from tools/wld_dump.cpp, where
// they were a contiguous statement range inside dumpWld().

#include "wld_dump/wld_dump.h"

#include "client/graphics/eq/wld_loader.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace EQT::Graphics;

void dumpGeometryFragments(const WldDumpFragments& frags) {
    // Re-bind the fragment index to the local names dumpWld() used, so the
    // moved bodies below are unchanged.
    const char* fragPtr = frags.fragPtr;
    const size_t remaining = frags.remaining;
    const uint32_t fragmentCount = frags.fragmentCount;
    const std::vector<std::pair<int, std::string>>& strings = *frags.strings;

    size_t offset = 0;


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
}
