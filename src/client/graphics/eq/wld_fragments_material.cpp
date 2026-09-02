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

void WldLoader::parseFragment03(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    WldFragment03Header header = read_val<WldFragment03Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment03Header);

    uint32_t count = header.textureCount;
    if (count == 0) count = 1;

    WldTexture tex;
    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = read_val<uint16_t>(ptr);
        ptr += sizeof(uint16_t);

        std::vector<char> nameBuf(nameLen);
        std::memcpy(nameBuf.data(), ptr, nameLen);
        decodeStringHash(nameBuf.data(), nameLen);

        std::string texName(nameBuf.data());
        std::transform(texName.begin(), texName.end(), texName.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        tex.frames.push_back(texName);

        ptr += nameLen;
    }

    LOG_TRACE(MOD_GRAPHICS, "Frag03[{}]: BitmapName frames={} first='{}'",
              fragIndex, tex.frames.size(), tex.frames.empty() ? "" : tex.frames[0]);
    textures_[fragIndex] = tex;
}

void WldLoader::parseFragment04(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    WldFragment04Header header = read_val<WldFragment04Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment04Header);

    WldTextureBrush brush;
    brush.flags = header.flags;

    // Check for animated texture (bit 3 set)
    brush.isAnimated = (header.flags & (1 << 3)) != 0;

    // Skip unknown field if bit 2 is set
    if (header.flags & (1 << 2)) {
        ptr += sizeof(uint32_t);
    }

    // Read animation delay if bit 3 is set (animated texture)
    if (brush.isAnimated) {
        brush.animationDelayMs = read_val<int32_t>(ptr);
        ptr += sizeof(int32_t);
    }

    uint32_t count = header.textureCount;
    if (count == 0) count = 1;

    for (uint32_t i = 0; i < count; ++i) {
        WldFragmentRef ref = read_val<WldFragmentRef>(ptr);
        ptr += sizeof(WldFragmentRef);

        if (ref.id > 0) {
            brush.textureRefs.push_back(ref.id);
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "Frag04[{}]: BitmapInfo flags=0x{:x} isAnimated={} textureRefs={} animDelayMs={}",
              fragIndex, brush.flags, brush.isAnimated, brush.textureRefs.size(), brush.animationDelayMs);
    for (size_t ri = 0; ri < brush.textureRefs.size(); ++ri) {
        LOG_TRACE(MOD_GRAPHICS, "  Frag04[{}]: textureRef[{}]={}", fragIndex, ri, brush.textureRefs[ri]);
    }
    brushes_[fragIndex] = brush;
}

void WldLoader::parseFragment05(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    WldFragmentRef ref = read_val<WldFragmentRef>(fragBuffer);
    LOG_TRACE(MOD_GRAPHICS, "Frag05[{}]: BitmapInfoRef ref={}", fragIndex, ref.id);
    if (ref.id > 0) {
        textureRefs_[fragIndex] = ref.id;
    } else {
        LOG_TRACE(MOD_GRAPHICS, "Frag05[{}]: ref <= 0, NOT stored", fragIndex);
    }
}

void WldLoader::parseFragment30(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                const char* hash, bool oldFormat) {
    // Fragment 0x30 structure matches eqsage: sage/lib/s3d/materials/material.js
    WldFragment30Header header = read_val<WldFragment30Header>(fragBuffer);

    WldTextureBrush material;

    // Extract material type from parameters (matches eqsage's materialType masking)
    uint32_t materialType = header.parameters & ~0x80000000;
    material.materialType = materialType;

    LOG_TRACE(MOD_GRAPHICS, "Frag30[{}]: Material flags=0x{:x} params=0x{:x} materialType={} bitmapInfoRef={} color=({},{},{},{}) brightness={} scaledAmbient={}",
              fragIndex, header.flags, header.parameters, materialType, header.bitmapInfoRef,
              header.colorR, header.colorG, header.colorB, header.colorA, header.brightness, header.scaledAmbient);

    if (materialType == 0 || header.bitmapInfoRef == 0) {
        // Boundary or invisible material
        material.flags = 1;
        LOG_TRACE(MOD_GRAPHICS, "Frag30[{}]: invisible (materialType={} bitmapInfoRef={})", fragIndex, materialType, header.bitmapInfoRef);
    } else {
        // Resolve texture reference chain: 0x30 -> 0x05 -> 0x04
        int32_t frag05Ref = header.bitmapInfoRef;
        auto it05 = textureRefs_.find(frag05Ref);
        if (it05 != textureRefs_.end()) {
            material.textureRefs.push_back(it05->second);
            LOG_TRACE(MOD_GRAPHICS, "Frag30[{}]: resolved bitmapInfoRef={} -> frag05 -> brushIdx={}", fragIndex, frag05Ref, it05->second);
        } else {
            LOG_DEBUG(MOD_GRAPHICS, "Frag30[{}]: bitmapInfoRef={} NOT FOUND in textureRefs_ (size={})",
                      fragIndex, frag05Ref, textureRefs_.size());
        }
        material.flags = 0;
    }

    materials_[fragIndex] = material;
}

void WldLoader::parseFragment31(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    WldFragment31Header header = read_val<WldFragment31Header>(fragBuffer);
    const char* ptr = fragBuffer + sizeof(WldFragment31Header);

    LOG_TRACE(MOD_GRAPHICS, "Frag31[{}]: MaterialList unk={} count={}", fragIndex, header.unk, header.count);

    WldTextureBrushSet brushSet;
    for (uint32_t i = 0; i < header.count; ++i) {
        uint32_t refId = read_val<uint32_t>(ptr);
        ptr += sizeof(uint32_t);

        if (refId > 0) {
            brushSet.brushRefs.push_back(refId);
            LOG_TRACE(MOD_GRAPHICS, "  Frag31[{}]: brushRef[{}]={}", fragIndex, i, refId);
        } else {
            LOG_TRACE(MOD_GRAPHICS, "  Frag31[{}]: brushRef[{}]={} SKIPPED (<=0)", fragIndex, i, refId);
        }
    }

    LOG_TRACE(MOD_GRAPHICS, "Frag31[{}]: stored {} brushRefs (of {} entries)", fragIndex, brushSet.brushRefs.size(), header.count);
    brushSets_[fragIndex] = brushSet;
    lastBrushSetFragIndex_ = fragIndex;  // T211: see the header's comment
}

} // namespace Graphics
} // namespace EQT
