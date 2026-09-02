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

void WldLoader::parseFragment1B(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash, bool oldFormat) {
    // Fragment 0x1B - Light Source Definition
    // Matches eqsage: sage/lib/s3d/lights/light.js LightSource
    // Variable-length structure with conditional fields based on flags

    if (fragLength < 8) {
        return;  // Need at least flags + frameCount
    }

    const char* ptr = fragBuffer;

    auto light = std::make_shared<ZoneLight>();

    // Get name from hash table
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            light->name = std::string(&hash[nameOffset]);
        }
    }

    // Initialize position (set by Fragment 0x28)
    light->x = 0.0f;
    light->y = 0.0f;
    light->z = 0.0f;
    light->radius = 0.0f;

    // Read flags and frameCount
    light->flags = read_val<uint32_t>(ptr);
    ptr += 4;
    light->frameCount = read_val<uint32_t>(ptr);
    ptr += 4;

    // Read optional currentFrame (if flags & 0x01)
    if (light->flags & LIGHT_FLAG_HAS_CURRENT_FRAME) {
        light->currentFrame = read_val<uint32_t>(ptr);
        ptr += 4;
    }

    // Read optional sleep (if flags & 0x02)
    if (light->flags & LIGHT_FLAG_HAS_SLEEP) {
        light->sleepMs = read_val<uint32_t>(ptr);
        ptr += 4;
    }

    // Read optional light levels (if flags & 0x04)
    if (light->flags & LIGHT_FLAG_HAS_LIGHT_LEVELS) {
        light->lightLevels.reserve(light->frameCount);
        for (uint32_t i = 0; i < light->frameCount; ++i) {
            float level = read_val<float>(ptr);
            light->lightLevels.push_back(level);
            ptr += 4;
        }
    }

    // Read optional colors (if flags & 0x10)
    // This is the CORRECT flag (0x10 = HasColor), NOT 0x08 (SkipFrames)
    if (light->flags & LIGHT_FLAG_HAS_COLOR) {
        light->colors.reserve(light->frameCount);
        for (uint32_t i = 0; i < light->frameCount; ++i) {
            float r = read_val<float>(ptr);
            ptr += 4;
            float g = read_val<float>(ptr);
            ptr += 4;
            float b = read_val<float>(ptr);
            ptr += 4;
            light->colors.push_back({r, g, b});
        }

        // Set primary color from first frame
        if (!light->colors.empty()) {
            auto& [r, g, b] = light->colors[0];
            light->r = r;
            light->g = g;
            light->b = b;
        } else {
            light->r = 1.0f;
            light->g = 1.0f;
            light->b = 1.0f;
        }
    } else {
        // No color data - default to white
        light->r = 1.0f;
        light->g = 1.0f;
        light->b = 1.0f;
    }

    lightDefs_[fragIndex] = light;
}

void WldLoader::parseFragment1C(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x1C - Light Source Reference
    // Simple indirection: 0x28 (instance) -> 0x1C (ref) -> 0x1B (definition)
    if (fragLength < 4) return;
    WldFragmentRef ref = read_val<WldFragmentRef>(fragBuffer);
    if (ref.id > 0) {
        lightDefRefs_[fragIndex] = static_cast<uint32_t>(ref.id);
    }
}

void WldLoader::parseFragment28(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x28 - Light Instance
    // Matches eqsage: sage/lib/s3d/lights/light.js LightInstance
    // Places a light source at a position with a radius

    WldFragmentRef ref = read_val<WldFragmentRef>(fragBuffer);
    WldFragment28Header header = read_val<WldFragment28Header>(
        fragBuffer + sizeof(WldFragmentRef));

    uint32_t lightDefIndex = ref.id;
    if (lightDefIndex == 0) {
        return;
    }

    // Follow through Fragment 0x1C reference if needed
    // 0x28 may reference a 0x1C (which references a 0x1B), or directly reference a 0x1B
    auto refIt = lightDefRefs_.find(lightDefIndex);
    if (refIt != lightDefRefs_.end()) {
        lightDefIndex = refIt->second;
    }

    auto it = lightDefs_.find(lightDefIndex);
    if (it == lightDefs_.end()) {
        // No light definition found - create basic white light
        auto light = std::make_shared<ZoneLight>();
        light->r = 1.0f;
        light->g = 1.0f;
        light->b = 1.0f;
        light->x = header.x;
        light->y = header.y;
        light->z = header.z;
        light->radius = header.radius;
        lights_.push_back(light);
        return;
    }

    // Copy light definition and add position/radius
    auto light = std::make_shared<ZoneLight>();
    light->name = it->second->name;
    light->r = it->second->r;
    light->g = it->second->g;
    light->b = it->second->b;
    light->x = header.x;
    light->y = header.y;
    light->z = header.z;
    light->radius = header.radius;

    // Copy animation data
    light->flags = it->second->flags;
    light->frameCount = it->second->frameCount;
    light->currentFrame = it->second->currentFrame;
    light->sleepMs = it->second->sleepMs;
    light->lightLevels = it->second->lightLevels;
    light->colors = it->second->colors;

    lights_.push_back(light);
}

void WldLoader::parseFragment2A(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex,
                                int32_t nameRef, const char* hash) {
    // Fragment 0x2A - Ambient Light Region
    // Matches eqsage: sage/lib/s3d/lights/light.js AmbientLight
    // Defines regions where ambient light applies

    if (fragLength < 8) {
        return;  // Need at least flags + regionCount
    }

    auto ambient = std::make_shared<AmbientLightRegion>();

    // Get name from hash table
    if (nameRef != 0) {
        int32_t nameOffset = -nameRef;
        if (nameOffset > 0) {
            ambient->name = std::string(&hash[nameOffset]);
        }
    }

    const char* ptr = fragBuffer;

    // Read flags and regionCount
    ambient->flags = read_val<uint32_t>(ptr);
    ptr += 4;
    uint32_t regionCount = read_val<uint32_t>(ptr);
    ptr += 4;

    // Read region references
    ambient->regionRefs.reserve(regionCount);
    for (uint32_t i = 0; i < regionCount; ++i) {
        int32_t regionRef = read_val<int32_t>(ptr);
        ambient->regionRefs.push_back(regionRef);
        ptr += 4;
    }

    ambientLightRegions_.push_back(ambient);
}

void WldLoader::parseFragment35(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x35 - Global Ambient Light
    // Matches eqsage: sage/lib/s3d/lights/light.js GlobalAmbientLight
    // Defines zone-wide ambient color

    if (fragLength < 4) {
        return;  // Need 4 bytes (BGRA)
    }

    auto ambient = std::make_shared<GlobalAmbientLight>();

    // eqsage reads in BGRA order
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(fragBuffer);
    uint8_t blue = bytes[0];
    uint8_t green = bytes[1];
    uint8_t red = bytes[2];
    uint8_t alpha = bytes[3];

    // Normalize to 0.0-1.0 range
    ambient->r = static_cast<float>(red) / 255.0f;
    ambient->g = static_cast<float>(green) / 255.0f;
    ambient->b = static_cast<float>(blue) / 255.0f;
    ambient->a = static_cast<float>(alpha) / 255.0f;

    globalAmbientLight_ = ambient;
}

void WldLoader::parseFragment2F(const char* fragBuffer, uint32_t fragLength, uint32_t fragIndex) {
    // Fragment 0x2F - Mesh Animated Vertices Reference
    // References a 0x37 fragment containing animated vertex frames
    if (fragLength < 8) {  // Minimum: ref + flags
        return;
    }

    // Read the fragment reference (to 0x37)
    const char* ptr = fragBuffer;
    int32_t meshAnimVertRef = read_val<int32_t>(ptr);

    LOG_DEBUG(MOD_GRAPHICS, "Fragment 0x2F: fragIndex={} meshAnimVertRef={}", fragIndex, meshAnimVertRef);

    // Store the mapping from this fragment to the 0x37 fragment it references
    if (meshAnimVertRef > 0) {
        meshAnimatedVerticesRefs_[fragIndex] = static_cast<uint32_t>(meshAnimVertRef);
    }
}

} // namespace Graphics
} // namespace EQT
