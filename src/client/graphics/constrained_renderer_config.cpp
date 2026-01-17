#include "client/graphics/constrained_renderer_config.h"
#include <algorithm>
#include <cmath>
#include <cctype>

namespace EQT {
namespace Graphics {

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
        case ConstrainedRenderingPreset::None:    return "None";
        case ConstrainedRenderingPreset::Voodoo1: return "Voodoo1";
        case ConstrainedRenderingPreset::Voodoo2: return "Voodoo2";
        case ConstrainedRenderingPreset::TNT:     return "TNT";
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
            break;

        case ConstrainedRenderingPreset::Custom:
            // Custom: enabled but use default values, caller should override
            config.enabled = true;
            break;

        case ConstrainedRenderingPreset::None:
        default:
            // No constraints
            config.enabled = false;
            break;
    }

    // Calculate derived max resolution
    if (config.enabled) {
        config.calculateMaxResolution();
    }

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
    if (lower == "custom") {
        return ConstrainedRenderingPreset::Custom;
    }
    if (lower == "none" || lower == "off" || lower == "disabled") {
        return ConstrainedRenderingPreset::None;
    }

    // Unrecognized preset name
    return ConstrainedRenderingPreset::None;
}

} // namespace Graphics
} // namespace EQT
