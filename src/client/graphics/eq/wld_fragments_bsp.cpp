#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/pfs.h"
#include "common/logging.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <set>
#include <vector>
#include <iostream>
#include <iomanip>
#include "client/graphics/eq/wld_internal.h"

namespace EQT {
namespace Graphics {

void WldLoader::parseFragment37(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                 int32_t nameRef, const char* hash) {
    // Fragment 0x37 - Mesh Animated Vertices (DMTRACKDEF)
    // Contains frames of vertex positions for vertex animation (flags, banners, etc.)
    if (fragLength < 18) {  // Minimum header size
        return;
    }

    // Read fields - fragment 0x37 format appears to be:
    // bytes 0-3: flags/unknown (4 bytes)
    // bytes 4-5: vertexCount (2 bytes)
    // bytes 6-7: frameCount (2 bytes)
    // bytes 8-9: delay (2 bytes)
    // bytes 10-11: param2 (2 bytes)
    // bytes 12-13: scale (2 bytes)
    // ... possibly more header bytes
    // Then vertex data: frameCount * vertexCount * 6 bytes

    const char* ptr = fragBuffer;
    uint32_t flags = read_val<uint32_t>(ptr); ptr += 4;
    uint16_t vertexCount = read_val<uint16_t>(ptr); ptr += 2;
    uint16_t frameCount = read_val<uint16_t>(ptr); ptr += 2;
    uint16_t delayMs = read_val<uint16_t>(ptr); ptr += 2;
    uint16_t param2 = read_val<uint16_t>(ptr); ptr += 2;
    int16_t scaleVal = read_val<int16_t>(ptr); ptr += 2;

    // Header is 14 bytes: flags(4) + vertexCount(2) + frameCount(2) + delay(2) + param2(2) + scale(2)
    // ptr is already at byte 14 after reading header fields
    // (Don't use calculated headerSize as it may include padding)

    LOG_DEBUG(MOD_GRAPHICS, "Fragment 0x37: flags=0x{:x} vertexCount={} frameCount={} delayMs={} scale={} fragLen={}",
        flags, vertexCount, frameCount, delayMs, scaleVal, fragLength);

    auto animVerts = std::make_shared<MeshAnimatedVertices>();
    animVerts->fragIndex = fragIndex;

    // Name comes from fragment header's nameRef
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            animVerts->name = &hash[nameOffset];
        }
    }

    // Animation delay in milliseconds
    animVerts->delayMs = delayMs;
    if (animVerts->delayMs <= 0) {
        animVerts->delayMs = 100;  // Default to 100ms if not specified
    }

    // Calculate scale factor
    float scale = 1.0f / (1 << scaleVal);

    // ptr is already positioned after the header fields (18 bytes)

    // Read all frames
    animVerts->frames.resize(frameCount);
    for (uint16_t f = 0; f < frameCount; ++f) {
        animVerts->frames[f].positions.resize(vertexCount * 3);

        for (uint16_t v = 0; v < vertexCount; ++v) {
            int16_t vd0 = read_val<int16_t>(ptr);
            int16_t vd1 = read_val<int16_t>(ptr + 2);
            int16_t vd2 = read_val<int16_t>(ptr + 4);
            ptr += 6;  // 3 x int16_t

            // Store raw EQ coordinates (transform applied at render time if needed)
            animVerts->frames[f].positions[v * 3 + 0] = vd0 * scale;
            animVerts->frames[f].positions[v * 3 + 1] = vd1 * scale;
            animVerts->frames[f].positions[v * 3 + 2] = vd2 * scale;
        }
    }

    meshAnimatedVertices_[fragIndex] = animVerts;

    LOG_DEBUG(MOD_GRAPHICS, "WldLoader: Parsed fragment 0x37 '{}' with {} frames, {} vertices, {}ms delay",
        animVerts->name, frameCount, vertexCount, animVerts->delayMs);
}

// Decode RLE-encoded PVS (Potentially Visible Set) data from Fragment 0x22
// RLE format from libeq documentation:
// - 0x00..0x3E: skip forward by this many region IDs (not visible)
// - 0x3F, WORD: skip forward by the amount in the following 16-bit WORD
// - 0x40..0x7F: skip forward based on bits 3..5, then include IDs based on bits 0..2
// - 0x80..0xBF: include IDs based on bits 3..5, then skip forward based on bits 0..2
// - 0xC0..0xFE: (byte - 0xC0) region IDs are nearby (visible)
// - 0xFF, WORD: the number of region IDs given by the following WORD are nearby

void WldLoader::parseFragment21(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x21 - BSP Tree
    // Contains the BSP tree nodes for zone region detection
    if (fragLength < 4) {
        return;
    }

    const char* ptr = fragBuffer;
    uint32_t nodeCount = read_val<uint32_t>(ptr);
    ptr += sizeof(uint32_t);

    LOG_TRACE(MOD_MAP, "[BSP] Fragment 0x21: fragLength={} Loading {} BSP tree nodes", fragLength, nodeCount);
    LOG_TRACE(MOD_MAP, "[BSP] Expected size: {} bytes", 4 + nodeCount * 28);
    if (fragLength != 4 + nodeCount * 28) {
        LOG_WARN(MOD_MAP, "[BSP] WARNING: Size mismatch! Off by {} bytes", (int)(fragLength - (4 + nodeCount * 28)));
    }

    // Debug: count nodes with regions
    int nodesWithRegions = 0;

    // Create BSP tree if not exists
    if (!bspTree_) {
        bspTree_ = std::make_shared<BspTree>();
    }

    bspTree_->nodes.reserve(nodeCount);

    // Each node is 28 bytes: normalX(4) + normalY(4) + normalZ(4) + splitDist(4) + regionId(4) + left(4) + right(4)
    for (uint32_t i = 0; i < nodeCount; ++i) {
        if (ptr + 28 > fragBuffer + fragLength) break;

        BspNode node;
        node.normalX = read_val<float>(ptr); ptr += 4;
        node.normalY = read_val<float>(ptr); ptr += 4;
        node.normalZ = read_val<float>(ptr); ptr += 4;
        node.splitDistance = read_val<float>(ptr); ptr += 4;
        node.regionId = read_val<int32_t>(ptr); ptr += 4;
        // Left/right are 1-indexed in file, convert to 0-indexed (-1 for no child)
        int32_t leftRaw = read_val<int32_t>(ptr); ptr += 4;
        int32_t rightRaw = read_val<int32_t>(ptr); ptr += 4;
        node.left = leftRaw - 1;
        node.right = rightRaw - 1;

        if (node.regionId > 0) {
            nodesWithRegions++;
            // Debug: print nodes with zone 4 regions (regionId 1902-1966 for regions 1901-1965)
            // Zone 4 regions are 1901, 1903, 1905, etc. up to ~1965
            if (node.regionId >= 1902 && node.regionId <= 1966) {
                LOG_TRACE(MOD_MAP, "[BSP] ZONE4 Node {} has regionId={} (region {}) normal=({},{},{}) dist={}",
                    i, node.regionId, node.regionId-1, node.normalX, node.normalY, node.normalZ, node.splitDistance);
            }
        }

        bspTree_->nodes.push_back(node);
    }

    LOG_TRACE(MOD_MAP, "[BSP] Fragment 0x21: {} nodes have regionId > 0", nodesWithRegions);
}

// T504c Leg 2g: the RLE PVS byte count a 0x22 body carries, read the same
// way parseFragment22() walks to it, so parseWldBuffer() can reserve the
// tree's arena EXACTLY before the first region appends (the sum of the 0x22
// bodies over-reserved iceclad's 4.6 MiB arena to 8.1 and the shrink then
// copied it). 0 when the body is malformed.
size_t WldLoader::fragment22PvsBytes(const char* fragBuffer, uint32_t fragLength) {
    const char* end = fragBuffer + fragLength;
    const char* ptr = fragBuffer;
    if (ptr + 40 > end) return 0;
    ptr += 4;                                              // flags
    ptr += 4;                                              // unknown1
    int32_t data1Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data2Size = read_val<int32_t>(ptr); ptr += 4;
    ptr += 4;                                              // unknown2
    int32_t data3Size = read_val<int32_t>(ptr); ptr += 4;
    ptr += 4;                                              // data4Size
    ptr += 4;                                              // unknown3
    int32_t data5Size = read_val<int32_t>(ptr); ptr += 4;
    ptr += 4;                                              // data6Size
    if (data1Size < 0 || data2Size < 0 || data3Size < 0 || data5Size < 0) return 0;
    ptr += 12 * static_cast<size_t>(data1Size) + 12 * static_cast<size_t>(data2Size);
    for (int32_t i = 0; i < data3Size; ++i) {
        if (ptr + 8 > end) return 0;
        ptr += 4;
        int32_t data3Size2 = read_val<int32_t>(ptr); ptr += 4;
        if (data3Size2 < 0) return 0;
        ptr += static_cast<size_t>(data3Size2) * 4;
    }
    ptr += 7 * 4 * static_cast<size_t>(data5Size);
    if (ptr + 2 > end) return 0;
    const int16_t pvsSize = read_val<int16_t>(ptr);
    if (pvsSize <= 0 || ptr + 2 + pvsSize > end) return 0;
    return static_cast<size_t>(pvsSize);
}

void WldLoader::parseFragment22(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                BspRegionPool* regionPool) {
    // Fragment 0x22 - BSP Region
    // Defines a region in the BSP tree (water, lava, zone line, etc.)
    if (fragLength < 40) {
        return;
    }

    // Create BSP tree if not exists
    if (!bspTree_) {
        bspTree_ = std::make_shared<BspTree>();
    }

    // B903: every region comes from the parse's one-block pool, and the
    // handle array is sized to the pool's exact count up front.
    if (regionPool && bspTree_->regions.empty()) bspTree_->regions.reserve(regionPool->size());
    auto region = regionPool ? regionPool->next() : std::make_shared<BspRegion>();
    size_t regionIndex = bspTree_->regions.size();
    // T504c Leg 2g: the region joins the tree FIRST so its PVS bytes append
    // straight into the tree's arena. Measured (iceclad, zone_parse trace):
    // setPvsRle() used to fill a private per-region store and addRegion()
    // copied it into the tree arena as it grew -- 13 MiB of transient for a
    // 4.6 MiB arena. The arena is reserved from the 0x22 fragment sizes
    // (parseWldBuffer) so it grows once.
    if (pvsArenaReserveHint_ > 0) { bspTree_->reservePvsArena(pvsArenaReserveHint_); pvsArenaReserveHint_ = 0; }
    bspTree_->addRegion(region);

    const char* ptr = fragBuffer;

    int32_t flags = read_val<int32_t>(ptr); ptr += 4;
    region->containsPolygons = (flags == 0x181);

    // Skip unknown1 (always 0)
    ptr += 4;

    int32_t data1Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data2Size = read_val<int32_t>(ptr); ptr += 4;

    // Skip unknown2 (always 0)
    ptr += 4;

    int32_t data3Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data4Size = read_val<int32_t>(ptr); ptr += 4;

    // Skip unknown3 (always 0)
    ptr += 4;

    int32_t data5Size = read_val<int32_t>(ptr); ptr += 4;
    int32_t data6Size = read_val<int32_t>(ptr); ptr += 4;

    // Skip data1 and data2 (12 bytes each entry)
    ptr += 12 * data1Size + 12 * data2Size;

    // Skip data3 (variable size per entry)
    for (int32_t i = 0; i < data3Size; ++i) {
        if (ptr + 8 > fragBuffer + fragLength) break;
        int32_t data3Flags = read_val<int32_t>(ptr); ptr += 4;
        int32_t data3Size2 = read_val<int32_t>(ptr); ptr += 4;
        ptr += data3Size2 * 4;
    }

    // Skip data4 (no data per entry in known files)

    // Skip data5 (28 bytes each entry)
    ptr += 7 * 4 * data5Size;

    // Decode PVS (Potentially Visible Set) data
    // This RLE-encoded bitfield indicates which other regions are visible from this region
    if (ptr + 2 <= fragBuffer + fragLength) {
        int16_t pvsSize = read_val<int16_t>(ptr); ptr += 2;
        if (pvsSize > 0 && totalRegionCount_ > 0 && ptr + pvsSize <= fragBuffer + fragLength) {
            // T490: keep the RLE bytes; the bitset is decoded on demand
            // (zone_bsp.cpp). Decoding every region here was regions^2/8 bytes.
            region->setPvsRle(reinterpret_cast<const uint8_t*>(ptr), static_cast<size_t>(pvsSize),
                              totalRegionCount_);
            if (GetDebugLevel() >= 3) {
                LOG_TRACE(MOD_MAP, "[BSP] Region {} PVS: {} RLE bytes, {}/{} regions visible",
                    regionIndex, pvsSize, region->pvsVisibleCount(), totalRegionCount_);
            }
        }
        ptr += pvsSize;
    }

    // Skip final unknown data
    if (ptr + 4 <= fragBuffer + fragLength) {
        ptr += 4;  // bytes
        ptr += 16; // unknown padding
    }

    // Read mesh reference if region contains polygons
    if (region->containsPolygons && ptr + 4 <= fragBuffer + fragLength) {
        region->meshReference = read_val<int32_t>(ptr);
    }
}

void WldLoader::parseFragment29(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                 int32_t nameRef, const char* hash) {
    // Fragment 0x29 - Region Type
    // Links region type info (water, lava, zone line, etc.) to BSP regions
    if (fragLength < 12 || !bspTree_) {
        return;
    }

    const char* ptr = fragBuffer;

    int32_t flags = read_val<int32_t>(ptr); ptr += 4;
    int32_t regionCount = read_val<int32_t>(ptr); ptr += 4;

    // Read region indices and link this type to them
    std::vector<int32_t> regionIndices;
    for (int32_t i = 0; i < regionCount; ++i) {
        if (ptr + 4 > fragBuffer + fragLength) break;
        int32_t regionIdx = read_val<int32_t>(ptr); ptr += 4;
        regionIndices.push_back(regionIdx);
    }

    // Get the region type string from the fragment NAME (not body)
    // nameRef is a negative offset into the string hash table
    std::string regionTypeString;
    if (nameRef < 0 && hash) {
        regionTypeString = std::string(hash + (-nameRef));
    }

    // Convert to lowercase for pattern matching
    std::transform(regionTypeString.begin(), regionTypeString.end(), regionTypeString.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Determine region types from the string
    std::vector<RegionType> types;
    std::optional<ZoneLineInfo> zoneLineInfo;

    if (regionTypeString.substr(0, 4) == "wtn_" || regionTypeString.substr(0, 3) == "wt_") {
        types.push_back(RegionType::Water);
    } else if (regionTypeString.substr(0, 5) == "wtntp") {
        types.push_back(RegionType::Water);
        types.push_back(RegionType::Zoneline);
        zoneLineInfo = decodeZoneLineString(regionTypeString);
    } else if (regionTypeString.substr(0, 4) == "lan_" || regionTypeString.substr(0, 3) == "la_") {
        types.push_back(RegionType::Lava);
    } else if (regionTypeString.substr(0, 5) == "lantp") {
        types.push_back(RegionType::Lava);
        types.push_back(RegionType::Zoneline);
        zoneLineInfo = decodeZoneLineString(regionTypeString);
    } else if (regionTypeString.substr(0, 5) == "drntp") {
        types.push_back(RegionType::Zoneline);
        zoneLineInfo = decodeZoneLineString(regionTypeString);
    } else if (regionTypeString.length() >= 6 && regionTypeString[0] == 'z' &&
               regionTypeString.find("_zone") != std::string::npos) {
        // Handle z####_zone format (e.g., z0000_zone, z0001_zone)
        // The number after 'z' is the zone point index
        types.push_back(RegionType::Zoneline);
        ZoneLineInfo info;
        info.type = ZoneLineType::Reference;
        info.zonePointIndex = 0;
        // Extract zone point index from string (e.g., "z0009_zone" -> 9)
        size_t numStart = 1;  // Skip 'z'
        size_t numEnd = regionTypeString.find('_');
        if (numEnd != std::string::npos && numEnd > numStart) {
            try {
                info.zonePointIndex = std::stoul(regionTypeString.substr(numStart, numEnd - numStart));
            } catch (...) {
                // Keep default 0
            }
        }
        zoneLineInfo = info;
    } else if (regionTypeString.substr(0, 4) == "drp_") {
        types.push_back(RegionType::Pvp);
    } else if (regionTypeString.substr(0, 4) == "drn_") {
        if (regionTypeString.find("_s_") != std::string::npos) {
            types.push_back(RegionType::Slippery);
        } else {
            types.push_back(RegionType::Unknown);
        }
    } else if (regionTypeString.substr(0, 4) == "sln_") {
        types.push_back(RegionType::WaterBlockLOS);
    } else if (regionTypeString.substr(0, 4) == "vwn_") {
        types.push_back(RegionType::FreezingWater);
    } else {
        types.push_back(RegionType::Normal);
    }

    // Apply types to all referenced regions
    for (int32_t regionIdx : regionIndices) {
        if (regionIdx >= 0 && static_cast<size_t>(regionIdx) < bspTree_->regions.size()) {
            auto& region = bspTree_->regions[regionIdx];
            region->regionTypes = types;
            region->zoneLineInfo = zoneLineInfo;

            // Debug output for zone lines
            for (auto type : types) {
                if (type == RegionType::Zoneline) {
                    LOG_TRACE(MOD_MAP, "[BSP] Fragment 0x29: Region {} marked as zone line (regionTypeString='{}')",
                        regionIdx, regionTypeString);
                    if (zoneLineInfo) {
                        LOG_TRACE(MOD_MAP, "[BSP]   -> type={} zoneId={} zonePointIdx={} coords=({}, {}, {}) heading={}",
                            (zoneLineInfo->type == ZoneLineType::Absolute ? "Absolute" : "Reference"),
                            zoneLineInfo->zoneId, zoneLineInfo->zonePointIndex,
                            zoneLineInfo->x, zoneLineInfo->y, zoneLineInfo->z, zoneLineInfo->heading);
                    }
                    break;
                }
            }
        }
    }
}

std::optional<ZoneLineInfo> WldLoader::decodeZoneLineString(const std::string& regionTypeString) {
    // Decode zone line destination from region type string
    // Formats:
    //   drntp_zone -> Reference type, index 0
    //   drntp00255{index}... -> Reference type, index = slice(10,16)
    //   drntp{zoneId}{x}{y}{z}{rot} -> Absolute type
    //     zoneId: 5 digits (slice 5-10)
    //     x: 6 digits (slice 10-16)
    //     y: 6 digits (slice 16-22)
    //     z: 6 digits (slice 22-28)
    //     rot: 3 digits (slice 28-31)
    // Same patterns for wtntp and lantp

    ZoneLineInfo info;

    // Find the prefix (drntp, wtntp, or lantp)
    std::string prefix;
    if (regionTypeString.substr(0, 5) == "drntp" ||
        regionTypeString.substr(0, 5) == "wtntp" ||
        regionTypeString.substr(0, 5) == "lantp") {
        prefix = regionTypeString.substr(0, 5);
    } else {
        return std::nullopt;
    }

    // Special case: drntp_zone
    if (regionTypeString == "drntp_zone") {
        info.type = ZoneLineType::Reference;
        info.zonePointIndex = 0;
        return info;
    }

    // Need at least 10 characters for zone ID
    if (regionTypeString.length() < 10) {
        return std::nullopt;
    }

    // Parse zone ID (5 digits after prefix)
    int zoneId = 0;
    try {
        zoneId = std::stoi(regionTypeString.substr(5, 5));
    } catch (...) {
        return std::nullopt;
    }

    // Zone ID 255 means reference type
    if (zoneId == 255) {
        if (regionTypeString.length() >= 16) {
            info.type = ZoneLineType::Reference;
            try {
                info.zonePointIndex = std::stoi(regionTypeString.substr(10, 6));
            } catch (...) {
                info.zonePointIndex = 0;
            }
        }
        return info;
    }

    // Absolute type - parse coordinates
    info.type = ZoneLineType::Absolute;
    info.zoneId = static_cast<uint16_t>(zoneId);

    if (regionTypeString.length() >= 28) {
        try {
            info.x = static_cast<float>(std::stoi(regionTypeString.substr(10, 6)));
            info.y = static_cast<float>(std::stoi(regionTypeString.substr(16, 6)));
            info.z = static_cast<float>(std::stoi(regionTypeString.substr(22, 6)));
        } catch (...) {
            // Keep default 0 values
        }
    }

    if (regionTypeString.length() >= 31) {
        try {
            info.heading = static_cast<float>(std::stoi(regionTypeString.substr(28, 3)));
        } catch (...) {
            info.heading = 0.0f;
        }
    }

    return info;
}
} // namespace Graphics
} // namespace EQT
