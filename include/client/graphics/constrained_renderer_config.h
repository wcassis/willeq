#ifndef EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H
#define EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace EQT {
namespace Graphics {

// Preset configurations for different hardware classes
enum class ConstrainedRenderingPreset {
    None,       // No constraints (modern hardware)
    Voodoo1,    // 2MB FBI, 2MB TMU, 256x256 max, 16-bit, 640x480 max
    Voodoo2,    // 4MB FBI, 8MB TMU, 256x256 max, 16-bit, 800x600 max
    TNT,        // 8MB FBI, 16MB TMU, 512x512 max, 16-bit, 1024x768 max
    OrangePi,   // 4MB FB, 8MB tex, 128x128 max, 16-bit, 800x600 (Mali 400, 512MB shared)
    Custom      // User-defined limits
};

// Configuration for resource-constrained rendering
// Enforces hard memory limits for both framebuffer and texture memory
struct ConstrainedRendererConfig {
    bool enabled = false;

    // Framebuffer memory (determines max resolution)
    // Includes: front buffer + back buffer + Z-buffer
    size_t framebufferMemoryBytes = 2 * 1024 * 1024;  // 2MB default (Voodoo1)
    int colorDepthBits = 16;  // 16 or 32

    // Texture memory (separate from framebuffer)
    size_t textureMemoryBytes = 2 * 1024 * 1024;  // 2MB default (Voodoo1)
    int maxTextureDimension = 256;

    // Render distance (clip plane) - geometry beyond this is not rendered
    float clipDistance = 500.0f;           // Max render distance (EQ units)
    float fogStartRatio = 0.6f;            // Fog starts at 60% of clip distance
    float fogEndRatio = 0.95f;             // Fog fully opaque at 95% of clip distance

    // Geometry budgets
    int maxVisibleEntities = 50;           // Max NPCs/players rendered at once
    int maxPolygonsPerFrame = 50000;       // Soft limit for zone geometry
    float entityRenderDistance = 200.0f;   // Max distance to render entities

    // Helper methods for fog distances
    float fogStart() const { return clipDistance * fogStartRatio; }
    float fogEnd() const { return clipDistance * fogEndRatio; }

    // Derived limits (calculated at startup via calculateMaxResolution())
    int maxResolutionWidth = 640;
    int maxResolutionHeight = 480;

    // Calculate max resolution from framebuffer budget
    // Call this after setting framebufferMemoryBytes and colorDepthBits
    void calculateMaxResolution();

    // Validate and clamp requested resolution to max allowed
    // Returns true if resolution was clamped (original exceeded max)
    bool clampResolution(int& width, int& height) const;

    // Calculate framebuffer memory usage for a given resolution
    // Formula: width * height * (2 * colorBytes + 2)
    // Where: front buffer + back buffer = 2 * colorBytes, Z-buffer = 2 bytes (16-bit)
    size_t calculateFramebufferUsage(int width, int height) const;

    // Get preset name as string (for logging/debug)
    static std::string presetName(ConstrainedRenderingPreset preset);

    // Create config from preset
    static ConstrainedRendererConfig fromPreset(ConstrainedRenderingPreset preset);

    // Parse preset from string (case-insensitive)
    // Returns None if string is not recognized
    static ConstrainedRenderingPreset parsePreset(const std::string& name);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H
