#ifndef EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H
#define EQT_GRAPHICS_CONSTRAINED_RENDERER_CONFIG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "client/core/memory_ledger.h"

namespace EQT {
namespace Graphics {

// Rendering backend selection.
// P32 (Phase 4, docs/decisions.md 2026-07-28 addendum): Software (Irrlicht
// Burnings) and OpenGL (desktop GL 2.1) were removed — GLES2
// (COpenGLES2Driver) is the sole supported renderer. The enum/function are
// kept (rather than deleted outright) because callers still name the
// backend for logging/config purposes; there is exactly one value.
enum class RenderingBackend {
    GLES2       // OpenGL ES 2.0 (COpenGLES2Driver) — the only supported backend
};

// Get backend name as string
inline std::string backendName(RenderingBackend backend) {
    switch (backend) {
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
    RenderingBackend renderingBackend = RenderingBackend::GLES2;
    bool useDRM = false;           // Use DRM/KMS framebuffer device (no X11)
    bool useEglX11 = false;        // P25: X11-windowed EGL surface (x86 GLES2 dev
                                    // builds only; explicit opt-in, never DISPLAY-
                                    // detected — surfaceless stays the default)

    // Rendering feature toggles (startup defaults, may be overridden at runtime)
    bool fog = true;
    bool wireframe = false;
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

    // Framebuffer memory (determines max resolution)
    // Includes: front buffer + back buffer + depth-stencil buffer
    size_t framebufferMemoryBytes = 2 * 1024 * 1024;  // 2MB default (Voodoo1)
    int colorDepthBits = 16;  // 16 or 32

    // Texture memory (separate from framebuffer)
    size_t textureMemoryBytes = 2 * 1024 * 1024;  // 2MB default (Voodoo1)
    int maxTextureDimension = 256;

    // ---- T293: per-subsystem memory budgets (Will's ruling, 2026-08-26) ----
    //
    // Before T293 exactly two categories of the ~13 the memory accounting
    // tracks had a configured ceiling: framebufferMemoryBytes and
    // textureMemoryBytes above. T69 wired those two into the CI gate as real
    // "usage <= budget" assertions and could only baseline-guard the rest,
    // because no budget existed to assert against; T69 deliberately refused
    // to invent numbers ("Proposing numbers is Will's call"). Will ruled on
    // 2026-08-26: derive them from the assets on disk, a 1280x720 maximum
    // resolution, and the Orange Pi One's real envelope.
    //
    // The full derivation (envelope arithmetic, per-category asset-scale
    // factors, measured usage, headroom rule, and the residual gaps) is in
    // docs/decisions.md under "2026-08-26 - per-subsystem memory budgets"
    // and refactor/T293_memory_budgets.md. Summary of the rule used:
    //
    //   budget = ceil_8MiB( worst_measured_of_3_golden_zones
    //                       * asset_scale_factor_from_disk
    //                       * 1.25 slack )
    //
    // where asset_scale_factor is the ratio of the LARGEST on-disk asset of
    // that kind across all 310 zones to the largest among the three golden
    // zones the gate measures (2.34x zone geometry, 3.56x placeable objects,
    // 4.28x character models). Rows with no natural on-disk analogue
    // (GPU textures, VBO/EBO) are derived from the budgets that feed them
    // instead -- see the per-field comments.
    //
    // ZERO MEANS "NO BUDGET CONFIGURED": the CI gate skips the assertion
    // rather than asserting usage <= 0. Only the OrangePi preset (and
    // config/constrained_presets.json's "orangepi" block) sets these today;
    // the Voodoo/TNT presets predate the accounting and stay unbudgeted.
    //
    // These are per-subsystem CEILINGS, not reservations. Their sum (282 MiB)
    // deliberately exceeds the derived 256 MB client RSS ceiling, because no
    // single zone is the worst case for every category at once and because
    // gpuTextureMemoryBytes and textureMemoryBytes bound substantially the
    // same textures. The simultaneous total is bounded separately by
    // totalMemoryBudgetBytes below. This matches Will's standing ruling:
    // per-subsystem budgets, defined and enforced; no global cap, no
    // cross-asset coordinator.

    // GPU-side texture allocations reported by the driver. Superset of the
    // constrained texture cache on the EQE1 path (measured delta 264-526 KB
    // of non-cached driver textures: fonts, sky, UI), but NOT on the
    // uncompressed S3D path, where the cache's CPU-side accounting exceeds
    // the driver's allocation (qeynos2: cache 42.2 MB vs GPU 31.7 MB).
    // Neither strictly contains the other, so both are budgeted separately.
    // Derived: 2.0x the worst measured (31.7 MB, qeynos2/S3D), which also
    // lands exactly on textureMemoryBytes -- the cache is its dominant feeder.
    // B943 (2026-09-30, Will: "lower the seven budgets, hold audio"): the
    // orangepi preset is now 38 MiB (textureMemoryBytes 51 MiB). Worst
    // gpuTextureBytes 31,735,808 B x 1.25 = 37.83 MiB -> 38 MiB and
    // textureCacheBytesUsed 42,226,984 B x 1.25 = 50.34 MiB -> 51 MiB, both
    // the gate's qeynos2 golden invocation (S3D path; the EQE1-path census
    // tops out at 16.8 / 16.3 MB). Read refactor/B943/README.md; the
    // two-feeder argument above is the T293 history.
    size_t gpuTextureMemoryBytes = 0;

    // Zone region/single-mesh vertex+index bytes held CPU-side.
    // Derived: 4.76 MiB worst measured (gfaydark) x 2.34 (ponightmare.s3d
    // 9.72 MB / qeynos2.s3d 4.16 MB) x 1.25 = 13.95 MiB -> 16 MiB.
    size_t zoneGeometryMemoryBytes = 0;

    // Placeable-object mesh bytes held CPU-side.
    // Derived: 5.36 MiB worst measured (gfaydark) x 3.56 (dulak_obj.s3d
    // 10.60 MB / gfaydark_obj.s3d 2.98 MB) x 1.25 = 23.87 MiB -> 24 MiB.
    size_t placeableObjectsMemoryBytes = 0;

    // CPU-side S3D zone source retained for mesh rebuilds (geometry, BSP,
    // animation data).
    // T293 derivation: 7.52 MiB worst measured (gfaydark) x 2.34 x 1.25 =
    // 22.00 MiB -> 24 MiB.
    // T491 (2026-09-17, board B803): 24 MiB DOES NOT DESCRIBE THE WORST ZONE.
    // T490 measured iceclad's S3D zone source at 24.55 MiB -- over the budget
    // by itself -- because the T293 scaling factor was taken over the three
    // GOLDEN zones and iceclad is not one of them. Re-derived on the measured
    // worst case instead of an extrapolated one: 24.55 MiB x 1.25 headroom =
    // 30.7 MiB -> 32 MiB. This is a budget that now describes reality; it is
    // NOT a target. Under Will's 32-64 MB RSS goal (docs/decisions.md
    // 2026-09-17) a single category holding 32 MiB is itself the shrink work,
    // and the memory ledger's zone_parse/zone_mesh categories measure it at
    // the allocator rather than by this estimate.
    // B927 (2026-09-29, Will: "ratchet the zone_parse budget down"): 32 ->
    // 14 MiB on the orangepi preset. Since B914 the gate measures after the
    // end-of-load source release; the worst figure at that point over the 118
    // era zones, the x86 client and the Pi client is 11.17 MiB (x86 client,
    // iceclad), x 1.25 -> 14 MiB. The gate now runs iceclad (B813), so CI
    // covers the worst zone. Derivation: constrained_renderer_config.cpp.
    size_t s3dZoneSourceMemoryBytes = 0;

    // GPU hardware buffers (VBO/EBO). Measured to be exactly the GPU mirror
    // of zone geometry + placeable objects (gfaydark: 10.22 MiB VBO/EBO vs
    // 4.76 + 5.36 = 10.12 MiB CPU-side), so this is derived by identity from
    // the two budgets above rather than by scaling: 16 + 24 = 40 MiB.
    size_t vboEboMemoryBytes = 0;

    // RaceModelLoader total (model geometry, skeletons, animated meshes and
    // every character texture pool).
    // Derived from a measured race-count sweep on qeynos2 rather than a flat
    // multiple, because the category is mostly a fixed cost: 23.68 MiB with a
    // single race resident, plus 189 KB per additional resident race (n=1..12,
    // r^2 linear). Budget = 23.68 fixed + 40 races x 0.18 = 7.2 + 8.0 MiB for
    // the largest zone _chr on disk (griegsend_chr.s3d 10.45 MB vs
    // qeynos2_chr.s3d 2.44 MB) = 38.5 MiB -> 40 MiB. 40 resident races is
    // ~3x the 14 distinct models T289 measured live in befallen on the Pi.
    // B943 (2026-09-30, Will: "lower the seven budgets, hold audio"): the
    // orangepi preset is now 2 MiB. The T293 derivation above is history.
    // Worst characterModelsBytes over the 118 era zones 1,544,392 B (oot,
    // refactor/B927/category_census.tsv; a five-race coverage sample, so
    // races beyond it are unmeasured) x 1.25 = 1.84 -> 2 MiB.
    size_t characterModelsMemoryBytes = 0;

    // T491: per-category budgets for the allocator-level memory ledger
    // (client/core/memory_ledger.h), indexed by eqt::memory::Category and
    // keyed by categoryName() in config/constrained_presets.json
    // ("memoryLedgerBudgets"). 0 = no budget configured; the CI gate
    // (scripts/ci/visual_gate.py) skips that category. Derived by the T293
    // headroom rule from the three golden zones -- see docs/decisions.md
    // 2026-09-14 for the table. Unlike the T69 estimates above these are
    // measured at the allocator, so they bound EVERY byte the category
    // allocates, transient intermediates included.
    std::array<size_t, eqt::memory::kCategoryCount> memoryLedgerBudgetBytes{};

    // EQE1 flat-pack mmap pool. THIS ONE BOUNDS ADDRESS SPACE, NOT RAM --
    // mapping an archive costs virtual address space and only the touched
    // pages become resident (T105 measured EQE1 archives at 1.6-10.6%
    // resident), which is why eqe1MappedBytes is excluded from totalEstimate
    // (T108). The binding ceiling for it is the 32-bit ARM user address space
    // (~3 GB), not the 512 MB of RAM: T195's eager directory scan mapped
    // ~2,900 MB and threw std::bad_alloc with 260 MB of RAM still free.
    // Derived from the archive set the config can demand, not from a run:
    // the worst zone (powar) is 41.24 MiB of zone packs + 132.57 MiB of the
    // 33 global archives in config/eqe1_archives.json = 173.81 MiB; x1.25 for
    // composite-entity late-opens (T289 saw 7 on the Pi) = 217 MiB -> 256 MiB.
    // That is 8.3% of the address-space ceiling and 2.1x the 114.39 MiB the
    // gate's coverage run actually maps. The whole EverQuest_ETC1 directory
    // is 4,545 MiB across 1,506 packs -- the budget is emphatically not "fit
    // the assets in RAM", it is "bound the resident/mapped working set".
    size_t eqe1MappedAddressSpaceBytes = 0;

    // Render distance (clip plane) - geometry beyond this is not rendered
    float clipDistance = 500.0f;           // Max render distance (EQ units)
    float fogStartRatio = 0.6f;            // Fog starts at 60% of clip distance
    float fogEndRatio = 0.95f;             // Fog fully opaque at 95% of clip distance

    // Geometry budgets
    int maxVisibleEntities = 50;           // Max NPCs/players rendered at once
    int maxPolygonsPerFrame = 50000;       // Soft limit for zone geometry
    float entityRenderDistance = 200.0f;   // Max distance to render entities
    // T109: max distance (EQ units, from the player's landing position) at which
    // Step 8 (loadZoneStep8_Entities) runs the full inline prep+build chain for
    // an entity at zone load. Entities beyond this are deferred — left unbuilt
    // (meshBuilt=false) — intended to be picked up by the post-load background
    // streaming path (EntityPrepWorker + processFrameProgressiveLoad ->
    // queueEntityPrepRequests) once the zone is playable. Separate from
    // entityRenderDistance (the render cap, protects FPS): this protects load
    // time. Must be >= entityRenderDistance (validate()) — you cannot draw what
    // you did not load.
    //
    // SHIPS INERT (struct default effectively unbounded, no preset overrides
    // it — see fromPreset()/config/constrained_presets.json). T109 built and
    // live-verified the gate itself (measured savings: x86 befallen Step 8
    // -20%/zone-request-to-ready -8%, Pi Step 8 -12%/zone-request-to-ready -7%,
    // at a 400-unit radius = entityRenderDistance+100, the elbow T105/T109
    // measured across befallen/nektulos/qeynos2 where most of the distance-
    // gated race-model saving is already captured) — but ALSO live-verified
    // that its prerequisite is broken: entities deferred here are NOT reliably
    // picked back up. `EntityPrepWorker` had zero construction sites anywhere
    // in the codebase (fixed by this unit, independently valuable), but even
    // fixed, `checkProgressiveLoadingComplete()` (renderer_zone_stream.cpp)
    // declares progressive loading complete based only on the player's
    // CURRENT BSP region's unbuilt entities, and nothing re-arms
    // `progressiveLoadingActive_` on a PVS region change (only brand-new
    // entity registration does, in `registerEntity()`) — so a deferred entity
    // outside the landing region never gets queued again, even as the player
    // walks toward it. Live-confirmed in befallen: modeled-entity count froze
    // at 82-83/178 across ~40s idle and after two large `/move`s (-400, -700
    // units, crossing regions); zero `EntityBuild:`/`EntityPrepWorker: queued`
    // log lines fired after Step 8 in either test. Per this unit's plan
    // ("STOP and report if the deferred path does not reliably pick entities
    // up — a half-working gate that loses NPCs is worse than no gate"), the
    // gate ships present-but-inert. Reactivating it is a scoped follow-up:
    // fix the completion/re-arm logic first (see refactor/deferred.md), then
    // set this field (or the JSON presets) to entityRenderDistance+100.
    float entityLoadRadius = 1'000'000.0f;
    int entityPrepMaxPvsDepth = 0;         // Max PVS portal depth for entity prep queueing (0 = same region only)
    int terrainPrepMaxPvsDepth = 255;      // Max PVS depth for region mesh building (255 = no limit)
    int objectPrepMaxPvsDepth = 255;       // Max PVS depth for object + door building (255 = no limit)
    int pvsNeighborhoodHops = 3;           // Portal hops for PVS neighborhood (suppresses re-prioritization within neighborhood)

    // Software occlusion culling
    int occlusionBufferWidth = 0;              // Depth buffer width (0 = disabled)
    int occlusionBufferHeight = 0;             // Depth buffer height (0 = disabled)
    // Max nearby regions to rasterize. Struct default (16) is the pre-P14
    // baseline; fromPreset() overrides per tier (OrangePi -> 256, P14-
    // blessed); loadJsonOverrides()'s "occlusionMaxOccluderRegions" JSON
    // key (P39) overrides that again when present in the preset file —
    // same fromPreset()-then-JSON precedence as every other tunable field
    // here. See constrained_renderer_config.cpp's OrangePi case and
    // loadJsonOverrides() for the full rationale.
    int occlusionMaxOccluderRegions = 16;

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
    // merge only activates once a zone's region set is fully resident with
    // zero evictions so far; falls back to the unmerged path for the rest of
    // that zone visit the instant an eviction occurs. (Originally gated on
    // ConstrainedMeshCache's eviction count too; T67, 2026-08-11, removed
    // that cache — it was never constructed in any shipped build — so the
    // gate is now the live constrainedTextureCache_'s eviction count alone.
    // See src/client/graphics/CLAUDE.md for the reference pointer.) Zone
    // objects/placeables always stay unmerged (out of scope, matches the P09
    // prototype). Runtime-toggleable via /mergedraws.
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
    // meshMemoryBytes (region mesh cache budget) removed T67 — it fed
    // ConstrainedMeshCache, which was never constructed in any shipped
    // build; consumed by nothing. See src/client/graphics/CLAUDE.md for
    // the reference pointer.
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
