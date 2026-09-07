// wld_dump_animation.cpp — dumpWld()'s animation and actor phase: bone tracks
// and their references (0x12/0x13), actor definitions (0x14), object
// locations (0x15), polygon animation (0x17/0x18), mesh animated vertices
// (0x37/0x2F), mesh references (0x2D), and the structured-hex fallback for
// every fragment type with no dedicated dumper. T447; bodies moved verbatim
// from tools/wld_dump.cpp, where they were a contiguous statement range
// inside dumpWld().

#include "wld_dump/wld_dump.h"

#include "client/graphics/eq/wld_loader.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace EQT::Graphics;

void dumpAnimationFragments(const WldDumpFragments& frags) {
    // Re-bind the fragment index to the local names dumpWld() used, so the
    // moved bodies below are unchanged.
    const char* fragPtr = frags.fragPtr;
    const size_t remaining = frags.remaining;
    const uint32_t fragmentCount = frags.fragmentCount;
    const std::vector<std::pair<int, std::string>>& strings = *frags.strings;
    // A COPY, not a reference: the moved bodies read it as typeCounts[0x12],
    // std::map's non-const operator[], and the map is ~30 entries.
    std::map<uint32_t, int> typeCounts = *frags.typeCounts;

    size_t offset = 0;


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

