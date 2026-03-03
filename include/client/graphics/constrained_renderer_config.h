#ifndef EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H
#define EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace EQT {
namespace Graphics {

// Rendering backend selection
enum class RenderingBackend {
    Software,   // Irrlicht Burnings software renderer (no GPU)
    OpenGL,     // Desktop OpenGL 2.1
    GLES2       // OpenGL ES 2.0 (COpenGLES2Driver)
};

// Get backend name as string
inline std::string backendName(RenderingBackend backend) {
    switch (backend) {
        case RenderingBackend::Software: return "Software";
        case RenderingBackend::OpenGL:   return "OpenGL";
        case RenderingBackend::GLES2:    return "GLES2";
        default:                         return "Unknown";
    }
}

// Preset configurations for different hardware classes
enum class ConstrainedRenderingPreset {
    Voodoo1,    // 2MB FBI, 2MB TMU, 256x256 max, 16-bit, 640x480 max
    Voodoo2,    // 4MB FBI, 8MB TMU, 256x256 max, 16-bit, 800x600 max
    TNT,        // 8MB FBI, 16MB TMU, 512x512 max, 16-bit, 1024x768 max
    OrangePi,   // 10MB FB, 64MB tex, 512x512 max, 16-bit, Mali 400 Lima GLES2, 512MB shared
    Custom      // User-defined limits
};

// Configuration for resource-constrained rendering
// Enforces hard memory limits for both framebuffer and texture memory
struct ConstrainedRendererConfig {
    // Rendering backend and display mode
    RenderingBackend renderingBackend = RenderingBackend::Software;
    bool useDRM = false;           // Use DRM/KMS framebuffer device (no X11)

    // Rendering feature toggles (startup defaults, may be overridden at runtime)
    bool fog = true;
    bool wireframe = false;
    bool frontToBackSorting = false;
    bool portalOcclusion = false;
    bool playerLight = true;
    bool objectLights = true;
    bool directionalLight = true;
    bool fireEffects = true;
    bool skyRendering = true;
    bool nameTagsEnabled = true;
    bool frameTimingEnabled = false;
    bool enableItemIcons = true;  // Load item/spell icon textures (disable to save RAM)

    // Framebuffer memory (determines max resolution)
    // Includes: front buffer + back buffer + depth-stencil buffer
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
    int entityPrepMaxPvsDepth = 0;         // Max PVS portal depth for entity prep queueing (0 = same region only)

    // Software occlusion culling
    int occlusionBufferWidth = 0;              // Depth buffer width (0 = disabled)
    int occlusionBufferHeight = 0;             // Depth buffer height (0 = disabled)
    int occlusionMaxOccluderRegions = 16;      // Max nearby regions to rasterize

    // Helper methods for fog distances
    float fogStart() const { return clipDistance * fogStartRatio; }
    float fogEnd() const { return clipDistance * fogEndRatio; }

    // GPU feature flags (queried/enabled per preset)
    bool enableMipmaps = false;              // Generate mipmaps for textures
    bool enableCompressedTextures = false;   // Upload DXT compressed via glCompressedTexImage2D
    bool enableNPOT = false;                 // Allow non-power-of-two textures
    bool enableStencilBuffer = false;        // Request stencil buffer from driver
    bool enableAlphaToCoverage = false;      // Use MSAA alpha-to-coverage for vegetation
    bool enableShaders = false;              // Use GLSL shaders for fog/lighting/tint
    bool enableTextureAtlas = false;         // Use pre-built ETC1 atlas files for zone textures
    bool skipManualZoneDraw = false;         // Debug: skip drawZoneGeometrySorted (isolate endScene)
    bool skipVBOUpload = false;              // Debug: skip glBufferData for zone meshes (use client arrays)
    bool skipEntityTextureUpload = false;    // Debug: skip entity texture uploads (glTexImage2D)
    bool skipEntityBuild = false;            // Debug: skip entity scene node creation entirely
    bool skipObjectBuild = false;            // Skip placeable object loading entirely (geometry + textures)
    bool skipConstrainedTextureUpload = false; // Debug: skip constrained cache glTexImage2D uploads
    bool skipSkyTextureUpload = false;         // Debug: skip sky texture GPU uploads (untextured sky dome)
    std::string atlasPath;                   // Directory containing .atlas files
    int antiAliasLevel = 0;                  // MSAA sample count (0=off, 4=4x, etc.)
    int anisotropicFilterLevel = 0;          // Anisotropic filtering (0=off, 4=4x, etc.)

    // System RAM budget (0 = no constraint)
    size_t totalMemoryBudgetBytes = 0;

    // Derived limits (computed by calculateMemoryLimits())
    size_t meshMemoryBytes = 0;            // Max region mesh cache (0 = no constraint / no lazy loading)
    size_t soundBufferCacheBytes = 0;       // Max decoded sound buffer cache
    size_t chrCacheMaxEntries = 0;          // Max otherChrCaches_ entries in RaceModelLoader
    bool lazyPfsLoading = false;            // Don't keep PFS archives decompressed in memory
    bool releaseTextureDataAfterUpload = false;  // Free raw pixel data post-GPU upload
    bool deferredAssetLoading = false;  // Defer mesh building to per-frame budget

    // Background thread pool size (shared by all BackgroundWorkQueues)
    int backgroundThreadCount = 1;

    // Frame budget governor target FPS (controls loading throttle)
    float targetFps = 60.0f;  // Default 60 for Max/TNT, overridden per preset

    // Compute memory-related derived limits from totalMemoryBudgetBytes
    void calculateMemoryLimits();

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
    // Formula: width * height * (2 * colorBytes + depthStencilBytes)
    // Where: front + back = 2 * colorBytes, depth-stencil = 4 bytes (D24S8) or 2 bytes (D16)
    size_t calculateFramebufferUsage(int width, int height) const;

    // Get preset name as string (for logging/debug)
    static std::string presetName(ConstrainedRenderingPreset preset);

    // Create config from preset
    static ConstrainedRendererConfig fromPreset(ConstrainedRenderingPreset preset);

    // Parse preset from string (case-insensitive)
    // Returns None if string is not recognized
    static ConstrainedRenderingPreset parsePreset(const std::string& name);

    // Load JSON overrides from a preset file
    // Looks up presets[presetName] and overrides matching fields
    // Returns true if overrides were applied, false if file not found or no matching preset
    bool loadJsonOverrides(const std::string& presetName, const std::string& jsonPath);

    // Parse "NxNxN" format: totalMB x textureCacheMB x framebufferMB
    // Returns true and fills outConfig if string matches NxNxN pattern
    static bool parseMemorySpec(const std::string& spec, ConstrainedRendererConfig& outConfig);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H
