#include "client/graphics/constrained_renderer_config.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include <fstream>
#include <json/json.h>

namespace EQT {
namespace Graphics {

void ConstrainedRendererConfig::calculateMemoryLimits() {
    if (totalMemoryBudgetBytes > 0) {
        lazyPfsLoading = true;
        releaseTextureDataAfterUpload = true;

        // Sound buffer cache: min(8MB, totalBudget/16)
        size_t eightMB = 8 * 1024 * 1024;
        soundBufferCacheBytes = std::min(eightMB, totalMemoryBudgetBytes / 16);

        // Mesh cache: derive from total if not explicitly set by preset
        if (meshMemoryBytes == 0) {
            meshMemoryBytes = totalMemoryBudgetBytes / 5;
        }

        // Keep at most 4 other-zone _chr.s3d caches
        chrCacheMaxEntries = 4;
    }
}

void ConstrainedRendererConfig::calculateMaxResolution() {
    // Calculate bytes per pixel for framebuffer
    // Front buffer + back buffer + Z-buffer (always 16-bit = 2 bytes)
    int colorBytes = colorDepthBits / 8;
    int bytesPerPixel = (2 * colorBytes) + 2;  // front + back + z

    // Calculate max pixels that fit in framebuffer memory
    size_t maxPixels = framebufferMemoryBytes / bytesPerPixel;

    // Calculate max resolution assuming 4:3 aspect ratio
    // width * height = maxPixels
    // width / height = 4/3
    // width = height * 4/3
    // height * 4/3 * height = maxPixels
    // height^2 = maxPixels * 3/4
    // height = sqrt(maxPixels * 3/4)
    double height = std::sqrt(static_cast<double>(maxPixels) * 3.0 / 4.0);
    double width = height * 4.0 / 3.0;

    // Round down to nearest multiple of 8 (common for graphics hardware)
    maxResolutionWidth = (static_cast<int>(width) / 8) * 8;
    maxResolutionHeight = (static_cast<int>(height) / 8) * 8;

    // Ensure we don't exceed memory with the rounded values
    while (calculateFramebufferUsage(maxResolutionWidth, maxResolutionHeight) > framebufferMemoryBytes) {
        maxResolutionWidth -= 8;
        maxResolutionHeight = (maxResolutionWidth * 3) / 4;
    }
}

bool ConstrainedRendererConfig::clampResolution(int& width, int& height) const {
    bool clamped = false;

    // Check if requested resolution exceeds max
    if (width > maxResolutionWidth) {
        width = maxResolutionWidth;
        clamped = true;
    }
    if (height > maxResolutionHeight) {
        height = maxResolutionHeight;
        clamped = true;
    }

    // Also check total pixel count (for non-4:3 aspect ratios)
    size_t requestedUsage = calculateFramebufferUsage(width, height);
    if (requestedUsage > framebufferMemoryBytes) {
        // Scale down proportionally
        double scale = std::sqrt(static_cast<double>(framebufferMemoryBytes) / requestedUsage);
        width = (static_cast<int>(width * scale) / 8) * 8;
        height = (static_cast<int>(height * scale) / 8) * 8;
        clamped = true;
    }

    return clamped;
}

size_t ConstrainedRendererConfig::calculateFramebufferUsage(int width, int height) const {
    int colorBytes = colorDepthBits / 8;
    int bytesPerPixel = (2 * colorBytes) + 2;  // front + back + z
    return static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerPixel;
}

std::string ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset preset) {
    switch (preset) {
        case ConstrainedRenderingPreset::Max:     return "Max";
        case ConstrainedRenderingPreset::Voodoo1: return "Voodoo1";
        case ConstrainedRenderingPreset::Voodoo2: return "Voodoo2";
        case ConstrainedRenderingPreset::TNT:      return "TNT";
        case ConstrainedRenderingPreset::OrangePi: return "OrangePi";
        case ConstrainedRenderingPreset::Custom:  return "Custom";
        default:                                   return "Unknown";
    }
}

ConstrainedRendererConfig ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset preset) {
    ConstrainedRendererConfig config;

    switch (preset) {
        case ConstrainedRenderingPreset::Voodoo1:
            // 3dfx Voodoo 1: 2MB FBI + 2MB TMU
            // Very constrained - use 64x64 textures to fit ~128 textures in 2MB
            config.enabled = true;
            config.framebufferMemoryBytes = 2 * 1024 * 1024;  // 2MB
            config.textureMemoryBytes = 2 * 1024 * 1024;      // 2MB
            config.colorDepthBits = 16;
            config.maxTextureDimension = 64;  // 16KB per texture = 128 max
            // Render distance and geometry budgets
            config.clipDistance = 300.0f;
            config.entityRenderDistance = 150.0f;
            config.maxVisibleEntities = 30;
            config.maxPolygonsPerFrame = 30000;
            // System RAM budget
            config.totalMemoryBudgetBytes = 32 * 1024 * 1024;  // 32MB
            config.meshMemoryBytes = 4 * 1024 * 1024;  // 4MB mesh cache
            break;

        case ConstrainedRenderingPreset::Voodoo2:
            // 3dfx Voodoo 2: 4MB FBI + 8MB TMU (with SLI could be more)
            // Use 128x128 textures to fit ~128 textures in 8MB
            config.enabled = true;
            config.framebufferMemoryBytes = 4 * 1024 * 1024;  // 4MB
            config.textureMemoryBytes = 8 * 1024 * 1024;      // 8MB
            config.colorDepthBits = 16;
            config.maxTextureDimension = 128;  // 64KB per texture = 128 max
            // Render distance and geometry budgets
            config.clipDistance = 500.0f;
            config.entityRenderDistance = 250.0f;
            config.maxVisibleEntities = 50;
            config.maxPolygonsPerFrame = 50000;
            // System RAM budget
            config.totalMemoryBudgetBytes = 64 * 1024 * 1024;  // 64MB
            config.meshMemoryBytes = 8 * 1024 * 1024;  // 8MB mesh cache
            break;

        case ConstrainedRenderingPreset::TNT:
            // NVIDIA RIVA TNT: unified memory, typically 16MB total
            config.enabled = true;
            config.framebufferMemoryBytes = 8 * 1024 * 1024;   // 8MB for framebuffer
            config.textureMemoryBytes = 16 * 1024 * 1024;      // 16MB for textures
            config.colorDepthBits = 16;
            config.maxTextureDimension = 512;
            // Render distance and geometry budgets
            config.clipDistance = 800.0f;
            config.entityRenderDistance = 400.0f;
            config.maxVisibleEntities = 75;
            config.maxPolygonsPerFrame = 100000;
            // GPU feature flags
            config.enableMipmaps = true;
            // System RAM budget
            config.totalMemoryBudgetBytes = 128 * 1024 * 1024;  // 128MB
            config.meshMemoryBytes = 16 * 1024 * 1024;  // 16MB mesh cache
            break;

        case ConstrainedRenderingPreset::OrangePi:
            // Orange Pi One: Allwinner H3, Mali 400 (Lima/Mesa GL 2.1), 512MB shared RAM
            // 24MB framebuffer supports 1280x720 @ 16-bit + 4x MSAA
            // 64MB texture budget: 512x512 textures with mipmaps = ~349KB each, ~187 textures
            config.enabled = true;
            config.framebufferMemoryBytes = 24 * 1024 * 1024;  // 24MB
            config.textureMemoryBytes = 64 * 1024 * 1024;      // 64MB
            config.colorDepthBits = 16;
            config.maxTextureDimension = 512;
            // Render distance and geometry budgets
            config.clipDistance = 300.0f;
            config.entityRenderDistance = 300.0f;
            config.maxVisibleEntities = 40;
            config.maxPolygonsPerFrame = 80000;
            // Software occlusion culling (128x64 depth buffer = 32KB)
            config.occlusionBufferWidth = 128;
            config.occlusionBufferHeight = 64;
            config.occlusionMaxOccluderRegions = 48;
            // GPU feature flags (Mali 400 via Lima supports these)
            config.enableMipmaps = true;
            config.enableNPOT = true;
            config.enableStencilBuffer = true;
            config.enableAlphaToCoverage = true;
            config.enableShaders = true;
            config.enableCompressedTextures = false;  // Mali 400 via Lima software-decodes S3TC; no GPU savings, extra CPU cost
            config.antiAliasLevel = 4;
            config.anisotropicFilterLevel = 4;
            // System RAM budget
            config.totalMemoryBudgetBytes = 128 * 1024 * 1024;  // 128MB
            config.meshMemoryBytes = 24 * 1024 * 1024;  // 24MB mesh cache
            // Deferred asset loading (progressive mesh building during gameplay)
            config.deferredAssetLoading = true;
            break;

        case ConstrainedRenderingPreset::Custom:
            // Custom: enabled but use default values, caller should override
            config.enabled = true;
            break;

        case ConstrainedRenderingPreset::Max:
        default:
            // Max: no practical limits (modern hardware)
            config.enabled = true;
            config.framebufferMemoryBytes = 256 * 1024 * 1024;  // 256MB
            config.textureMemoryBytes = 256 * 1024 * 1024;      // 256MB
            config.colorDepthBits = 32;
            config.maxTextureDimension = 4096;
            config.clipDistance = 99999.0f;
            config.entityRenderDistance = 99999.0f;
            config.maxVisibleEntities = 10000;
            config.maxPolygonsPerFrame = 10000000;
            config.occlusionBufferWidth = 256;
            config.occlusionBufferHeight = 128;
            config.occlusionMaxOccluderRegions = 64;
            // GPU feature flags
            config.enableMipmaps = true;
            config.enableNPOT = true;
            config.enableStencilBuffer = true;
            config.enableShaders = true;
            config.totalMemoryBudgetBytes = 0;  // No RAM constraint
            break;
    }

    // Calculate derived limits
    config.calculateMaxResolution();
    config.calculateMemoryLimits();

    return config;
}

ConstrainedRenderingPreset ConstrainedRendererConfig::parsePreset(const std::string& name) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "voodoo1" || lower == "voodoo" || lower == "v1") {
        return ConstrainedRenderingPreset::Voodoo1;
    }
    if (lower == "voodoo2" || lower == "v2") {
        return ConstrainedRenderingPreset::Voodoo2;
    }
    if (lower == "tnt" || lower == "riva" || lower == "rivatnt") {
        return ConstrainedRenderingPreset::TNT;
    }
    if (lower == "orangepi" || lower == "opi") {
        return ConstrainedRenderingPreset::OrangePi;
    }
    if (lower == "custom") {
        return ConstrainedRenderingPreset::Custom;
    }
    if (lower == "max" || lower == "none" || lower == "off" || lower == "disabled") {
        return ConstrainedRenderingPreset::Max;
    }

    // Unrecognized preset name
    return ConstrainedRenderingPreset::Max;
}

bool ConstrainedRendererConfig::parseMemorySpec(const std::string& spec, ConstrainedRendererConfig& outConfig) {
    int totalMB = 0, texMB = 0, fbMB = 0;

    // Parse NxNxN pattern (case-insensitive 'x' or 'X')
    if (std::sscanf(spec.c_str(), "%dx%dx%d", &totalMB, &texMB, &fbMB) != 3 &&
        std::sscanf(spec.c_str(), "%dX%dX%d", &totalMB, &texMB, &fbMB) != 3) {
        return false;
    }

    if (totalMB <= 0 || texMB <= 0 || fbMB <= 0) {
        return false;
    }

    outConfig.enabled = true;
    outConfig.totalMemoryBudgetBytes = static_cast<size_t>(totalMB) * 1024 * 1024;
    outConfig.textureMemoryBytes = static_cast<size_t>(texMB) * 1024 * 1024;
    outConfig.framebufferMemoryBytes = static_cast<size_t>(fbMB) * 1024 * 1024;

    // Set sensible defaults based on total memory budget
    if (totalMB <= 64) {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 64;
        outConfig.clipDistance = 300.0f;
        outConfig.entityRenderDistance = 150.0f;
        outConfig.maxVisibleEntities = 30;
        outConfig.maxPolygonsPerFrame = 30000;
    } else if (totalMB <= 128) {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 128;
        outConfig.clipDistance = 400.0f;
        outConfig.entityRenderDistance = 200.0f;
        outConfig.maxVisibleEntities = 40;
        outConfig.maxPolygonsPerFrame = 40000;
    } else if (totalMB <= 256) {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 256;
        outConfig.clipDistance = 600.0f;
        outConfig.entityRenderDistance = 300.0f;
        outConfig.maxVisibleEntities = 60;
        outConfig.maxPolygonsPerFrame = 75000;
    } else {
        outConfig.colorDepthBits = 16;
        outConfig.maxTextureDimension = 512;
        outConfig.clipDistance = 800.0f;
        outConfig.entityRenderDistance = 400.0f;
        outConfig.maxVisibleEntities = 75;
        outConfig.maxPolygonsPerFrame = 100000;
    }

    outConfig.calculateMaxResolution();
    outConfig.calculateMemoryLimits();
    return true;
}

bool ConstrainedRendererConfig::loadJsonOverrides(const std::string& presetName,
                                                   const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        return false;
    }

    if (!root.isMember("presets") || !root["presets"].isObject()) {
        return false;
    }

    // Case-insensitive preset lookup
    std::string lowerName = presetName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    const Json::Value& presets = root["presets"];
    Json::Value preset;
    for (const auto& key : presets.getMemberNames()) {
        std::string lowerKey = key;
        std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerKey == lowerName) {
            preset = presets[key];
            break;
        }
    }

    if (preset.isNull()) {
        return false;
    }

    // Override fields that are present in the JSON
    if (preset.isMember("maxTextureDimension"))
        maxTextureDimension = preset["maxTextureDimension"].asInt();
    if (preset.isMember("maxPolygonsPerFrame"))
        maxPolygonsPerFrame = preset["maxPolygonsPerFrame"].asInt();
    if (preset.isMember("clipDistance"))
        clipDistance = preset["clipDistance"].asFloat();
    if (preset.isMember("entityRenderDistance"))
        entityRenderDistance = preset["entityRenderDistance"].asFloat();
    if (preset.isMember("framebufferMemoryBytes"))
        framebufferMemoryBytes = static_cast<size_t>(preset["framebufferMemoryBytes"].asUInt64());
    if (preset.isMember("textureMemoryBytes"))
        textureMemoryBytes = static_cast<size_t>(preset["textureMemoryBytes"].asUInt64());
    if (preset.isMember("colorDepthBits"))
        colorDepthBits = preset["colorDepthBits"].asInt();
    if (preset.isMember("maxVisibleEntities"))
        maxVisibleEntities = preset["maxVisibleEntities"].asInt();
    if (preset.isMember("fogStartRatio"))
        fogStartRatio = preset["fogStartRatio"].asFloat();
    if (preset.isMember("fogEndRatio"))
        fogEndRatio = preset["fogEndRatio"].asFloat();
    if (preset.isMember("enableMipmaps"))
        enableMipmaps = preset["enableMipmaps"].asBool();
    if (preset.isMember("enableCompressedTextures"))
        enableCompressedTextures = preset["enableCompressedTextures"].asBool();
    if (preset.isMember("enableNPOT"))
        enableNPOT = preset["enableNPOT"].asBool();
    if (preset.isMember("enableStencilBuffer"))
        enableStencilBuffer = preset["enableStencilBuffer"].asBool();
    if (preset.isMember("enableAlphaToCoverage"))
        enableAlphaToCoverage = preset["enableAlphaToCoverage"].asBool();
    if (preset.isMember("enableShaders"))
        enableShaders = preset["enableShaders"].asBool();
    if (preset.isMember("antiAliasLevel"))
        antiAliasLevel = preset["antiAliasLevel"].asInt();
    if (preset.isMember("anisotropicFilterLevel"))
        anisotropicFilterLevel = preset["anisotropicFilterLevel"].asInt();
    if (preset.isMember("mesh_memory_mb"))
        meshMemoryBytes = static_cast<size_t>(preset["mesh_memory_mb"].asUInt64()) * 1024 * 1024;
    if (preset.isMember("meshMemoryBytes"))
        meshMemoryBytes = static_cast<size_t>(preset["meshMemoryBytes"].asUInt64());

    return true;
}

} // namespace Graphics
} // namespace EQT
