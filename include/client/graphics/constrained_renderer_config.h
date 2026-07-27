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

    // Sky dome mesh source
    enum class SkyDomeMode {
        Procedural,   // Current procedural hemisphere (693 verts, smooth color gradients)
        Original      // WLD dome mesh from sky.s3d (145 verts, lower poly count)
    };
    SkyDomeMode skyDomeMode = SkyDomeMode::Procedural;  // Default: current behavior

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
    int terrainPrepMaxPvsDepth = 255;      // Max PVS depth for region mesh building (255 = no limit)
    int objectPrepMaxPvsDepth = 255;       // Max PVS depth for object + door building (255 = no limit)
    int pvsNeighborhoodHops = 3;           // Portal hops for PVS neighborhood (suppresses re-prioritization within neighborhood)

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
    // enableTextureAtlas removed (E10) — EQE1 flat packs replace zone atlas system
    bool skipManualZoneDraw = false;         // Debug: skip drawZoneGeometrySorted (isolate endScene)
    bool skipVBOUpload = false;              // Debug: skip glBufferData for zone meshes (use client arrays)
    bool skipEntityTextureUpload = false;    // Debug: skip entity texture uploads (glTexImage2D)
    bool skipEntityBuild = false;            // Debug: skip entity scene node creation entirely
    bool skipObjectBuild = false;            // Skip placeable object loading entirely (geometry + textures)
    bool skipConstrainedTextureUpload = false; // Debug: skip constrained cache glTexImage2D uploads
    bool skipSkyTextureUpload = false;         // Debug: skip sky texture GPU uploads (untextured sky dome)
    std::string eqe1Path;                    // Directory containing .eqe1 flat pack files (ETC1 textures)
    bool enableCompositeEntities = false;    // Use composite texture sheets for entity rendering (1 draw call)
    std::string compositeEntityPath;         // Directory containing composite BMP/EQE1 + JSON from entity_composite_builder
    int antiAliasLevel = 0;                  // MSAA sample count (0=off, 4=4x, etc.)
    int anisotropicFilterLevel = 0;          // Anisotropic filtering (0=off, 4=4x, etc.)

    // P10: cross-region same-material zone draw merge (production landing of the
    // P09/P09b/P09c-validated design). Default false everywhere -- flag OFF is the
    // byte-untouched existing per-region draw path. When true, drawZoneGeometrySorted()
    // may coalesce consecutive same-SMaterial runs of RESIDENT zone-region geometry
    // into single client-side-index glDrawElements calls (see renderer_zone_merge.h).
    // Reduced-landing scope (see refactor/P10_merge_production_landing.md Review):
    // merge only activates for zones where ConstrainedMeshCache never evicts (fully
    // resident); falls back to the unmerged path for the rest of that zone visit the
    // instant an eviction occurs. Zone objects/placeables always stay unmerged (out
    // of scope, matches the P09 prototype). Runtime-toggleable via /mergedraws.
    bool mergeZoneDraws = false;

    // Adaptive per-pixel lighting: nearest N regions/objects get per-pixel player light,
    // the rest use lightweight per-vertex shaders. Requires /plight enabled.
    // 0 = disabled (all per-pixel when /plight on, all per-vertex when off).
    int adaptiveLightingZoneBudget = 0;      // Max zone regions with per-pixel lighting (0=disabled)
    int adaptiveLightingObjectBudget = 0;    // Max objects with per-pixel lighting (0=disabled)

    // P06: how the zone budget above is spent. Count (default, unchanged behavior)
    // treats adaptiveLightingZoneBudget as a flat region COUNT in front-to-back draw
    // order. Area instead accumulates each region's estimated projected screen-space
    // footprint (nearest-first) and switches to lightweight once the running total
    // exceeds adaptiveLightingAreaBudgetFactor * screen area -- fixes the outskirts
    // anomaly (see refactor/P06_area_aware_budget.md) where 1-6 huge regions never
    // exhaust a region-count budget and pay full per-pixel fill cost on most of the
    // screen. Object budget (adaptiveLightingObjectBudget) is unaffected -- still
    // count-based regardless of this mode (see P06 review).
    enum class AdaptiveLightingBudgetMode { Count, Area, Radius };
    AdaptiveLightingBudgetMode adaptiveLightingBudgetMode = AdaptiveLightingBudgetMode::Count;
    // k in "cumulative estimated area > k * screen area -> lightweight" (area mode
    // only). Default 0.75 -- see refactor/P06_area_aware_budget.md Review for the
    // measured basis: k must be < 1.0 for a single near-full-screen outskirts
    // region to flip on its own (the whole point of area mode), and 0.6-0.95
    // measured equivalently at both the outskirts and city reference poses on the
    // Pi (city's cumulative area saturates within the first 1-3 regions regardless
    // of k in that range), so 0.75 is a mid-range value with no measured
    // downside relative to the extremes tested.
    float adaptiveLightingAreaBudgetFactor = 0.75f;

    // P07: hybrid floor for area mode. The first N regions (nearest-first
    // order) always get per-pixel lighting regardless of estimated area --
    // area mode alone demotes even the nearest region to lightweight for the
    // outskirts single-huge-region case (near-plane fallback pins its
    // estimate at the full screen area, so it flips at k<1.0 same as any
    // other region), which unlights the geometry the camera is closest to.
    // Floor regions' estimated area still accumulates into the running
    // total, so regions after the floor demote exactly as before -- this
    // only protects the nearest N from ever flipping themselves. Ignored in
    // Count mode. See refactor/P07_pp_hybrid_floor.md.
    int adaptiveLightingAreaPPFloor = 1;

    // P07d: radius-based demotion -- visually lossless by construction, since
    // the per-pixel and lightweight zone shaders differ ONLY in the player
    // light (light[0], the sole per-pixel term; the other 7 point lights are
    // per-vertex in both paths). A region whose world AABB lies entirely
    // beyond the player light's effective attenuation range renders
    // (near-)identically under either shader, so demoting exactly those
    // regions costs nothing visually. Demotion test: the CLOSEST POINT of the
    // region's world AABB to the player light position is beyond range R
    // (closest-point, NOT region-center -- a huge nearby region whose center
    // is far away must not be wrongly demoted). R is computed once per frame
    // from the SAME light[0] position + attenuation (constant/linear/quadratic)
    // the zone shader receives this frame, as the distance where the maximum
    // possible contribution (attenuation alone, i.e. NdotL/wrap-lighting
    // assumed saturated at 1.0 -- conservative, since wrap lighting's
    // (NdotL+0.5)/1.5 floor of 1/3 can only make the true contribution
    // smaller) falls below ~1/255. adaptiveLightingRadiusMargin multiplies
    // the computed R for headroom/tuning (default 1.0 = no margin). Unlike
    // Area mode, adaptiveLightingZoneBudget's numeric value is UNUSED in
    // Radius mode -- only its >0/==0 on/off gate applies (radius mode's
    // demotion decision comes entirely from R, not from a region count or
    // area budget). Count and Area modes are untouched by this field. See
    // refactor/P07d_radius_demotion.md.
    float adaptiveLightingRadiusMargin = 1.0f;

    // System RAM budget (0 = no constraint)
    size_t totalMemoryBudgetBytes = 0;

    // Derived limits (computed by calculateMemoryLimits())
    size_t meshMemoryBytes = 0;            // Max region mesh cache (0 = no constraint / no lazy loading)
    size_t soundBufferCacheBytes = 0;       // Max decoded sound buffer cache
    size_t chrCacheMaxEntries = 0;          // Max otherChrCaches_ entries in RaceModelLoader
    bool lazyPfsLoading = false;            // Don't keep PFS archives decompressed in memory
    bool releaseTextureDataAfterUpload = false;  // Free raw pixel data post-GPU upload
    bool deferredAssetLoading = false;  // Defer mesh building to per-frame budget
    bool loadMemAudit = false;  // P04: log per-step RSS + known-pool bytes during loadZoneSequential

    // P13: when non-empty, ConstrainedTextureCache records every GPU texture
    // upload + eviction during the zone load to an in-memory ledger and
    // dumps it to this file path at zone-in-complete. Off by default (empty
    // string = ledger disabled, zero behavior/perf change) — see
    // refactor/P13_texture_upload_audit.md and texture_upload_ledger.h.
    std::string textureUploadLedgerPath;

    // Background thread pool size (shared by all BackgroundWorkQueues)
    int backgroundThreadCount = 1;

    // Frame budget governor target FPS (controls loading throttle)
    float targetFps = 60.0f;  // Default 60 for Max/TNT, overridden per preset

    // Validate all config fields. Returns true if valid.
    // On failure, populates 'errors' with all validation failures.
    bool validate(std::string& errors) const;

    // Validate that the given resolution fits within framebuffer budget.
    // Returns true if valid. On failure, populates 'error' with details.
    bool validateResolution(int width, int height, std::string& error) const;

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

    // Load debug flags from config/debug.json
    // Overrides skip* flags used for isolating rendering subsystems
    // Returns true if file was loaded, false if not found or parse error
    bool loadDebugOverrides(const std::string& jsonPath);

    // Parse "NxNxN" format: totalMB x textureCacheMB x framebufferMB
    // Returns true and fills outConfig if string matches NxNxN pattern
    static bool parseMemorySpec(const std::string& spec, ConstrainedRendererConfig& outConfig);
};

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H
