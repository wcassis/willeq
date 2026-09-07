// wld_dump_core.cpp — fragment-type names, the hex dumper, and dumpWld()'s
// first phase: file header, string table and the fragment index/details
// table (T447; bodies moved verbatim from tools/wld_dump.cpp).
//
// dumpWld() ends by handing the fragment index to the two phase functions in
// the sibling TUs, in the order the single function used to run them.

#include "wld_dump/wld_dump.h"

#include "client/graphics/eq/wld_loader.h"
#include "common/logging.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace EQT::Graphics;

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

void dumpHex(const char* data, size_t len, size_t maxBytes) {
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

    WldDumpFragments frags;
    frags.fragPtr = fragPtr;
    frags.remaining = remaining;
    frags.fragmentCount = fragmentCount;
    frags.strings = &strings;
    frags.typeCounts = &typeCounts;

    dumpGeometryFragments(frags);
    dumpAnimationFragments(frags);
}
