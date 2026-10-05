#include "client/graphics/constrained_renderer_config.h"
#include "common/logging.h"
#include "client/data_paths.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

#include <fstream>
#include <json/json.h>

namespace EQT {
namespace Graphics {

static bool isPowerOf2(int v) {
    return v > 0 && (v & (v - 1)) == 0;
}

bool ConstrainedRendererConfig::validate(std::string& errors) const {
    errors.clear();

    // Framebuffer
    if (colorDepthBits != 16 && colorDepthBits != 32)
        errors += fmt::format("  - colorDepthBits: {} (must be 16 or 32)\n", colorDepthBits);
    if (framebufferMemoryBytes == 0)
        errors += "  - framebufferMemoryBytes: must be > 0\n";

    // Texture memory
    if (textureMemoryBytes == 0)
        errors += "  - textureMemoryBytes: must be > 0\n";
    if (!isPowerOf2(maxTextureDimension) || maxTextureDimension < 64 || maxTextureDimension > 2048)
        errors += fmt::format("  - maxTextureDimension: {} (must be power of 2 in [64, 2048])\n", maxTextureDimension);

    // Rendering
    if (clipDistance <= 0)
        errors += fmt::format("  - clipDistance: {} (must be > 0)\n", clipDistance);
    if (entityRenderDistance <= 0)
        errors += fmt::format("  - entityRenderDistance: {} (must be > 0)\n", entityRenderDistance);
    if (entityLoadRadius <= 0)
        errors += fmt::format("  - entityLoadRadius: {} (must be > 0)\n", entityLoadRadius);
    if (entityLoadRadius < entityRenderDistance)
        errors += fmt::format("  - entityLoadRadius: {} must be >= entityRenderDistance: {} "
                               "(you cannot draw what you did not load)\n",
                               entityLoadRadius, entityRenderDistance);
    if (maxVisibleEntities <= 0)
        errors += fmt::format("  - maxVisibleEntities: {} (must be > 0)\n", maxVisibleEntities);
    if (maxPolygonsPerFrame <= 0)
        errors += fmt::format("  - maxPolygonsPerFrame: {} (must be > 0)\n", maxPolygonsPerFrame);
    if (fogStartRatio < 0.0f || fogStartRatio > 1.0f)
        errors += fmt::format("  - fogStartRatio: {} (must be in [0, 1])\n", fogStartRatio);
    if (fogEndRatio < 0.0f || fogEndRatio > 1.0f)
        errors += fmt::format("  - fogEndRatio: {} (must be in [0, 1])\n", fogEndRatio);
    if (fogEndRatio <= fogStartRatio)
        errors += fmt::format("  - fogEndRatio: {} must be > fogStartRatio: {}\n", fogEndRatio, fogStartRatio);
    if (targetFps <= 0)
        errors += fmt::format("  - targetFps: {} (must be > 0)\n", targetFps);
    if (backgroundThreadCount < 1)
        errors += fmt::format("  - backgroundThreadCount: {} (must be >= 1)\n", backgroundThreadCount);

    // PVS/occlusion
    if (entityPrepMaxPvsDepth < 0 || entityPrepMaxPvsDepth > 255)
        errors += fmt::format("  - entityPrepMaxPvsDepth: {} (must be in [0, 255])\n", entityPrepMaxPvsDepth);
    if (terrainPrepMaxPvsDepth < 0 || terrainPrepMaxPvsDepth > 255)
        errors += fmt::format("  - terrainPrepMaxPvsDepth: {} (must be in [0, 255])\n", terrainPrepMaxPvsDepth);
    if (objectPrepMaxPvsDepth < 0 || objectPrepMaxPvsDepth > 255)
        errors += fmt::format("  - objectPrepMaxPvsDepth: {} (must be in [0, 255])\n", objectPrepMaxPvsDepth);
    if (pvsNeighborhoodHops < 0)
        errors += fmt::format("  - pvsNeighborhoodHops: {} (must be >= 0)\n", pvsNeighborhoodHops);
    if (antiAliasLevel != 0 && !isPowerOf2(antiAliasLevel))
        errors += fmt::format("  - antiAliasLevel: {} (must be 0 or power of 2)\n", antiAliasLevel);
    if (anisotropicFilterLevel != 0 && !isPowerOf2(anisotropicFilterLevel))
        errors += fmt::format("  - anisotropicFilterLevel: {} (must be 0 or power of 2)\n", anisotropicFilterLevel);

    // Derived limits
    if (totalMemoryBudgetBytes > 0) {
        if (totalMemoryBudgetBytes < textureMemoryBytes + framebufferMemoryBytes)
            errors += fmt::format("  - totalMemoryBudgetBytes: {} < textureMemoryBytes ({}) + framebufferMemoryBytes ({})\n",
                                  totalMemoryBudgetBytes, textureMemoryBytes, framebufferMemoryBytes);
        // meshMemoryBytes validation removed T67 (field removed with ConstrainedMeshCache).
    }

    return errors.empty();
}

bool ConstrainedRendererConfig::validateResolution(int width, int height, std::string& error) const {
    size_t usage = calculateFramebufferUsage(width, height);
    if (usage > framebufferMemoryBytes) {
        int colorBytes = colorDepthBits / 8;
        int depthStencilBytes = enableStencilBuffer ? 4 : 2;
        int bytesPerPixel = (2 * colorBytes) + depthStencilBytes;
        error = fmt::format(
            "Resolution {}x{} requires {} bytes ({}x{}x{} bpp) but framebufferMemoryBytes is {} bytes. "
            "Reduce resolution or increase framebuffer budget.",
            width, height, usage, width, height, bytesPerPixel, framebufferMemoryBytes);
        return false;
    }
    return true;
}

void ConstrainedRendererConfig::calculateMemoryLimits() {
    if (totalMemoryBudgetBytes > 0) {
        lazyPfsLoading = true;
        releaseTextureDataAfterUpload = true;

        // Sound buffer cache: min(8MB, totalBudget/16)
        size_t eightMB = 8 * 1024 * 1024;
        soundBufferCacheBytes = std::min(eightMB, totalMemoryBudgetBytes / 16);

        // Mesh cache derived-limit computation removed T67 (field removed
        // with ConstrainedMeshCache).

        // Default: keep at most 4 other-zone _chr.s3d caches (overridable via JSON)
        if (chrCacheMaxEntries == 0) {
            chrCacheMaxEntries = 4;
        }
    }
}

void ConstrainedRendererConfig::calculateMaxResolution() {
    // Calculate bytes per pixel for framebuffer
    // Front buffer + back buffer + depth-stencil
    int colorBytes = colorDepthBits / 8;
    int depthStencilBytes = enableStencilBuffer ? 4 : 2;  // D24S8 or D16
    int bytesPerPixel = (2 * colorBytes) + depthStencilBytes;  // front + back + depth-stencil

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
    // Depth-stencil: D24S8 (4 bytes) when stencil enabled, D16 (2 bytes) otherwise
    int depthStencilBytes = enableStencilBuffer ? 4 : 2;
    int bytesPerPixel = (2 * colorBytes) + depthStencilBytes;  // front + back + depth-stencil
    return static_cast<size_t>(width) * static_cast<size_t>(height) * bytesPerPixel;
}

std::string ConstrainedRendererConfig::presetName(ConstrainedRenderingPreset preset) {
    switch (preset) {
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
            // P32: presets are renderer-independent (budgets/features only) —
            // no renderingBackend assignment here; GLES2 is the only backend
            // (docs/decisions.md 2026-07-28 addendum).
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
            config.backgroundThreadCount = 1;
            config.targetFps = 30.0f;
            break;

        case ConstrainedRenderingPreset::Voodoo2:
            // 3dfx Voodoo 2: 4MB FBI + 8MB TMU (with SLI could be more)
            // Use 128x128 textures to fit ~128 textures in 8MB
            // P32: renderer-independent preset (see Voodoo1 comment above).
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
            config.backgroundThreadCount = 1;
            config.targetFps = 30.0f;
            break;

        case ConstrainedRenderingPreset::TNT:
            // NVIDIA RIVA TNT: unified memory, typically 16MB total
            // P32: renderer-independent preset (see Voodoo1 comment above).
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
            config.enableShaders = true;
            // System RAM budget
            config.totalMemoryBudgetBytes = 128 * 1024 * 1024;  // 128MB
            config.backgroundThreadCount = 2;
            config.targetFps = 60.0f;
            break;

        case ConstrainedRenderingPreset::OrangePi:
            // Orange Pi One: Allwinner H3, Mali 400 (Lima/Mesa GL 2.1), 512MB shared RAM
            // 10MB framebuffer: 1280x720 @ 16-bit + D24S8 = 7.03MB + headroom for FBO RTTs
            // (MSAA is free on tile-based Mali 400 — resolved in on-chip tile SRAM)
            //
            // T293 verified this against the stated 1280x720 maximum, since the
            // margin is not large and depends entirely on colorDepthBits:
            //   colorDepthBits=16 (this preset): (2*2 + 4) = 8 B/px
            //                                    * 921,600 px =  7,372,800 B = 7.03 MiB   FITS (67% of 10 MiB)
            //   colorDepthBits=32:               (2*4 + 4) = 12 B/px
            //                                    * 921,600 px = 11,059,200 B = 10.55 MiB  DOES NOT FIT
            //                                    (over 10,485,760 B by 573,440 B)
            // So 10 MiB holds the stated maximum resolution as shipped, but only
            // because this preset is 16-bit; at 32 bits calculateMaxResolution()
            // yields 1072x808 and clampResolution() would silently narrow a
            // 1280x720 request to 1072x720.
            // The budget is NOT raised to cover the 32-bit case -- 16-bit is the
            // shipped configuration and Mali 400 has no reason to want 32.
            // textureMemoryBytes is the texture cache's real EVICTION
            // ceiling (ConstrainedTextureCache) and also the gate's
            // textureCacheBytesUsed budget (textureCacheBytesLimit).
            // B943 (2026-09-30): 64 -> 51 MiB, sized on the gate's S3D-path
            // qeynos2 run because a 20 MiB ceiling then segfaulted (B944).
            // B978 (2026-10-01, Will: "3) bless"): 51 -> 21 MiB, now that
            // eviction keeps the object and restores on bind (B944/B947). By
            // the in-tree rule on the SHIPPED EQE1 path: the 117-zone census
            // worst is gpuTextureBytes 16,797,104 B (qeynos; its
            // textureCacheBytesUsed is 16,250,840) x 1.25 = 20,996,380 B ->
            // 21 MiB (refactor/B978/README.md). The S3D-path gate run (qeynos2
            // holds 42 MB) now evicts under it -- intended.
            // P32: renderer-independent preset (see Voodoo1 comment above) —
            // GLES2 is the only backend, no #ifdef needed.
            config.useDRM = true;
            config.framebufferMemoryBytes = 10 * 1024 * 1024;  // 10MB
            config.textureMemoryBytes = 21 * 1024 * 1024;      // 21MB (B978)
            config.colorDepthBits = 16;
            config.maxTextureDimension = 512;
            // Render distance and geometry budgets
            config.clipDistance = 300.0f;
            config.entityRenderDistance = 300.0f;
            config.maxVisibleEntities = 40;
            config.maxPolygonsPerFrame = 80000;
            // Software occlusion culling — runs on SimulationWorker thread (not render thread)
            // so CPU cost doesn't impact frame delivery. Culls regions behind walls/terrain
            // that PVS marks as visible but are actually occluded from the camera viewpoint.
            // Benchmarked: 26% FPS improvement (64→81 avg), 45% fewer draw calls at worst case.
            config.portalOcclusion = true;     // Auto-enable stencil portal vis in indoor zones
            config.occlusionBufferWidth = 64;
            config.occlusionBufferHeight = 32;
            // P14 (2026-07-27, blessed by Will): 256 closes befallen's p95 KPI gap
            // under the merge+composite stack (35.32→32.31ms; uncapped ties 256).
            // GPU-sync −9.5% and submission −23.1% at the dense bf472 pose for
            // +0.44ms of CPU occlusion-test cost. 16 predates the stack era.
            // P39: this C++ default is also now mirrored explicitly in
            // config/constrained_presets.json's "orangepi" block (JSON
            // overrides this on top, per loadJsonOverrides() below) — the
            // JSON preset load previously had no parse for this key at all,
            // so it was silently un-tunable from the config file and
            // invisible to anyone reading the JSON as the source of truth.
            config.occlusionMaxOccluderRegions = 256;
            // GPU feature flags (Mali 400 via Lima supports these)
            config.enableMipmaps = true;
            config.enableNPOT = true;
            // B971 (Will, 2026-10-01: "orangepi uses a 16-bit depth buffer
            // (no stencil user there)"): the shipped surface has no stencil.
            // D24S8 is 3,686,400 B (3.52 MiB) of Lima GEM at 1280x720, the
            // single largest non-texture [memgpu] row after B962; 16-bit depth
            // with no stencil is 1,843,200 B. The surface is selected end to
            // end by enableStencilBuffer (params.Stencilbuffer in
            // renderer_init.cpp -> eglChooseConfig in
            // docker/irrlicht-drm/CIrrDeviceFB.cpp), so this is what a 16-bit-
            // depth surface ships as.
            //
            // Note the interplay with the portalOcclusion=true above: that is
            // the pre-B971 "portal vis auto-enables in indoor zones"
            // AVAILABILITY default, and the shipped config/constrained_presets.json
            // orangepi block sets portalOcclusion:false ON TOP of it (loadJsonOverrides
            // wins), so in the shipped config the two agree -- no stencil user,
            // no stencil buffer. This C++ baseline is what the x86 benchmark
            // (zone_bench_loader.cpp, fromPreset(OrangePi), no JSON) mirrors,
            // hence enableStencilBuffer=false here: the benchmark must render
            // on the D16/no-stencil surface the client ships. (The benchmark
            // does not consume portalOcclusion, so the baseline's true there
            // is inert.) Re-enabling portal occlusion in a local/custom config
            // re-requests the stencil automatically -- see loadJsonOverrides'
            // portalOcclusion/enableStencilBuffer invariant below.
            config.enableStencilBuffer = false;
            config.enableAlphaToCoverage = true;
            config.enableShaders = true;
            config.enableCompressedTextures = false;  // Mali 400 via Lima software-decodes S3TC; no GPU savings, extra CPU cost

            config.antiAliasLevel = 4;
            config.anisotropicFilterLevel = 4;
            // System RAM budget
            config.totalMemoryBudgetBytes = 128 * 1024 * 1024;  // 128MB
            // T293: per-subsystem memory budgets, derived from the assets on
            // disk, a 1280x720 maximum resolution and this board's measured
            // envelope (T289: peak RSS 209-230 MB, VmPeak 571-575 MB,
            // MemAvailable floor 55-66 MB; T195: ~483 MB usable RAM, ~3 GB
            // 32-bit address-space ceiling). Full derivation table in
            // docs/decisions.md and constrained_renderer_config.h's block
            // comment on these fields. Discharges the block T69 left open.
            // B943: 64 -> 38 MiB. Worst gpuTextureBytes 31,735,808 B (the gate's
            // qeynos2 golden invocation, S3D path; the 118-zone EQE1-path
            // census tops out at 16,797,104 qeynos) x 1.25 = 37.83 MiB -> 38.
            config.gpuTextureMemoryBytes         = 38 * 1024 * 1024;   // 38MB
            config.zoneGeometryMemoryBytes       = 16 * 1024 * 1024;   // 16MB
            config.placeableObjectsMemoryBytes   = 24 * 1024 * 1024;   // 24MB
            // B927 (2026-09-29, Will: "ratchet the zone_parse budget down"):
            // 32 -> 14 MiB, pinned equal to the ledger's zone_parse budget
            // below (board B803); the derivation is on that line.
            config.s3dZoneSourceMemoryBytes      = 14 * 1024 * 1024;   // 14MB
            config.vboEboMemoryBytes             = 40 * 1024 * 1024;   // 40MB
            // B943: 40 -> 2 MiB. Worst characterModelsBytes 1,544,392 B (oot,
            // 118-zone census; the T303 five-race coverage sample, so it
            // inherits that sample's limit) x 1.25 = 1.84 -> 2 MiB. The blessed
            // coverage BASELINES in tests/goldens/manifest.json are unchanged.
            config.characterModelsMemoryBytes    = 2 * 1024 * 1024;    // 2MB
            // B964 (Will, 2026-09-30: "Your recommended impostor budget
            // sounds fine"): the tex.impostor row, 128^2 impostors. B952's
            // 200 unique x 128^2 Pi-measured 15.79 MiB x 1.25 = 19.74 -> 20 MiB.
            config.impostorTextureMemoryBytes    = 20 * 1024 * 1024;   // 20MB
            config.impostorTierDistance          = 80.0f;
            // Address space, not RAM -- see the field's comment.
            config.eqe1MappedAddressSpaceBytes   = 256 * 1024 * 1024;  // 256MB
            // T491 (2026-09-14, Will's 100%-accounting ruling): per-category
            // budgets for the ALLOCATOR-level ledger (client/core/memory_ledger.h),
            // asserted by scripts/ci/visual_gate.py on every gate run. Derived by
            // the T293 headroom rule -- worst LIVE bytes measured across the three
            // golden zones plus the memory-coverage run, x 1.25, rounded up -- on
            // x86/llvmpipe at 800x600. Unlike the estimates above these bound
            // EVERY byte the category allocates, transients included.
            // 0 = no budget configured; the gate skips the category and says so.
            // DELIBERATELY 0, with the reason:
            //  - startup / textures_driver / irrlicht_scene: driver-coupled, and
            //    T496 Leg A MEASURED the Pi rather than assuming it. On Mali
            //    they are 26.82 / <=0.67 / <=1.45 MiB; the same binary on
            //    llvmpipe reports 40.5-49.9 / 2.1-30.5 / 24.0-27.1 MiB. So the
            //    x86 figures were not merely untransferable, they are 40-50x
            //    too LARGE. They still carry no budget, for a different reason
            //    than before: there is only ONE budget table and the benchmark
            //    the visual gate measures hard-codes this preset
            //    (zone_bench_loader.cpp, fromPreset(OrangePi)) while running on
            //    llvmpipe, so a Mali-derived budget fails the gate by 3,280% on
            //    textures_driver alone. Per-backend budgets are board B831.
            //  - doors / equipment / entity_instances / ui / network /
            //    game_state: no benchmark constructs them, so a budget would be
            //    decoration -- the same rule T69/T293 applied to their estimates.
            //  - audio: NO LONGER 0. T496 Leg D budgets it from the Pi; see
            //    below. It is the one category here the benchmark never
            //    constructs that is nevertheless worth budgeting, because it is
            //    the client's largest steady bucket and does not vary by zone.
            // THE ATTRIBUTION RATCHET, not a capacity budget: bytes
            // charged with NO MemoryScope on the calling thread (static
            // initialisers, a thread's first allocations). Measured 87,456 B
            // on every T491 run; 1 MiB is an order of magnitude of headroom so
            // a NEW unscoped subsystem is red. Ratchet DOWN, never up.
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::Unattributed)] =
                1 * 1024 * 1024;
            // B943 (2026-09-30, Will: "lower the seven budgets, hold audio"):
            // 4 -> 3 MiB. Measured worst 2,163,296 B (x86 client, timorous,
            // refactor/B909; Pi client 2,010,568 eastwastes; benchmark
            // 1,409,088 eastwastes) x 1.25 = 2.58 MiB -> 3 MiB.
            // (T491: 1.24 MiB coverage run x 1.25 = 1.6 -> 4.)
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::Archives)] =
                3 * 1024 * 1024;
            // B927 (2026-09-29, Will: "ratchet the zone_parse budget down"):
            // 32 -> 14 MiB. Measured worst at the point the gate asserts
            // (after the end-of-load source release, B914) over all 118 era
            // zones (refactor/B927/zone_parse_census.tsv, x86 benchmark:
            // iceclad 11,329,968 B), the x86 CLIENT (refactor/B909: iceclad
            // steady 11,714,736 B) and the Orange Pi client (refactor/B854:
            // iceclad 9.8 MiB at ready). Worst = x86 client, iceclad,
            // 11,714,736 B = 11.17 MiB x 1.25 = 13.97 -> 14 MiB.
            // NOT a mid-load bound: during the load iceclad holds 31.7 MB live
            // and peaks at 50.1 MB (refactor/B927/README.md, a finding).
            // Pinned to s3dZoneSourceMemoryBytes (board B803) because
            // zone_parse holds exactly that data.
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::ZoneParse)] =
                14 * 1024 * 1024;
            // measured worst 20.26 MiB (befallen) x 1.25 = 25.3 -> 28
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::ZoneMesh)] =
                28 * 1024 * 1024;
            // measured worst 2.68 MiB (gfaydark) x 1.25 = 3.4 -> 4.
            // ⚠ iceclad is 37,609 BSP regions against gfaydark's; this budget is
            // derived from zones that do not exercise the term T490 is chasing
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::Spatial)] =
                4 * 1024 * 1024;
            // B943: 20 -> 2 MiB. Measured worst 1,056,288 B (x86 client,
            // timorous; Pi 899,208 chardok; benchmark 316,432 thurgadina) x
            // 1.25 = 1.26 MiB -> 2 MiB. (T491: 13.04 MiB coverage x 1.25 -> 20.)
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::TexturesDecoded)] =
                2 * 1024 * 1024;
            // B943: 84 -> 38 MiB. Measured worst 31,563,888 B (Pi client,
            // charasis_retry at zone_load_complete, refactor/B943 census over
            // the B854 details.json ledgers; x86 client 17,491,440 dreadlands;
            // benchmark 5,083,088 nektulos) x 1.25 = 37.6 MiB -> 38 MiB.
            // This is a bound on the asserted point, not on the transient
            // high-water mark (nektulos peaks at 36.9 MB). (T491: 64.56 MiB
            // coverage, 5 races x 1.25 -> 84.)
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::EntityModels)] =
                38 * 1024 * 1024;
            // measured worst 28.91 MiB (coverage) x 1.25 = 36.1 -> 40
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::Objects)] =
                40 * 1024 * 1024;
            // T496 Leg D: the first `audio` budget, and the first budget in
            // this table derived from a measurement taken ON THE PI rather than
            // on x86. Measured steady state after B817 and B830, Orange Pi,
            // qeynos2, every lifecycle point: 25,725,824 B = 24.53 MiB
            // (review/T496/pi/table_categories.tsv). x1.25 = 30.66 -> 32.
            // This is two SoundFonts' float samples and essentially nothing
            // else; it does not vary with the zone.
            config.memoryLedgerBudgetBytes[static_cast<size_t>(eqt::memory::Category::Audio)] =
                32 * 1024 * 1024;
            // Deferred asset loading (progressive mesh building during gameplay)
            config.deferredAssetLoading = true;
            config.backgroundThreadCount = 1;
            config.targetFps = 30.0f;
            // Adaptive per-pixel lighting: nearest N regions/objects get per-pixel
            // player light, rest use lightweight per-vertex. Benchmarked on qeynos2:
            // zone budget 20 + object budget 10 maintains visual quality near camera
            // while saving ~4ms/frame on distant geometry.
            config.adaptiveLightingZoneBudget = 20;
            config.adaptiveLightingObjectBudget = 10;
            // Entity prep: queue entities up to 2 portal hops away
            config.entityPrepMaxPvsDepth = 2;
            config.terrainPrepMaxPvsDepth = 5;
            config.objectPrepMaxPvsDepth = 3;
            config.pvsNeighborhoodHops = 3;
            // Use lower-poly WLD dome mesh instead of procedural hemisphere
            config.skyDomeMode = SkyDomeMode::Original;
            break;

        case ConstrainedRenderingPreset::Custom:
        default:
            // Custom: use default values, caller should override
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
        LOG_WARN(MOD_GRAPHICS, "Preset '{}' is deprecated, mapping to OrangePi", name);
        return ConstrainedRenderingPreset::OrangePi;
    }

    // Unrecognized preset name — default to OrangePi
    if (!lower.empty()) {
        LOG_WARN(MOD_GRAPHICS, "Unknown preset '{}', defaulting to OrangePi", name);
    }
    return ConstrainedRenderingPreset::OrangePi;
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
    // T399 (board B235): shipped data resolves through the one path rule, and
    // a miss is no longer SILENT -- config/constrained_presets.json is
    // shipped, and without it the renderer runs on the built-in preset
    // numbers rather than the tuned ones, with nothing said.
    const std::string resolved = eqt::ResolveDataPath(jsonPath);
    std::ifstream file(resolved);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS,
                  "ConstrainedRendererConfig: cannot open '{}' (resolved to '{}') -- preset "
                  "'{}' keeps its built-in values; the shipped JSON overrides are NOT applied",
                  jsonPath, resolved, presetName);
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
    if (preset.isMember("entityLoadRadius"))
        entityLoadRadius = preset["entityLoadRadius"].asFloat();
    if (preset.isMember("framebufferMemoryBytes"))
        framebufferMemoryBytes = static_cast<size_t>(preset["framebufferMemoryBytes"].asUInt64());
    if (preset.isMember("textureMemoryBytes"))
        textureMemoryBytes = static_cast<size_t>(preset["textureMemoryBytes"].asUInt64());
    // T293: per-subsystem memory budgets. Parsed here so they are tunable from
    // config/constrained_presets.json exactly like the two budgets above --
    // P39/P42 both found preset keys that shipped in the JSON with no parse
    // behind them (silently un-tunable, and misleading to anyone reading the
    // JSON as the source of truth). Absent key => keep the preset's C++ value;
    // 0 => no budget configured, the CI gate skips that assertion.
    if (preset.isMember("gpuTextureMemoryBytes"))
        gpuTextureMemoryBytes = static_cast<size_t>(preset["gpuTextureMemoryBytes"].asUInt64());
    if (preset.isMember("zoneGeometryMemoryBytes"))
        zoneGeometryMemoryBytes = static_cast<size_t>(preset["zoneGeometryMemoryBytes"].asUInt64());
    if (preset.isMember("placeableObjectsMemoryBytes"))
        placeableObjectsMemoryBytes = static_cast<size_t>(preset["placeableObjectsMemoryBytes"].asUInt64());
    if (preset.isMember("s3dZoneSourceMemoryBytes"))
        s3dZoneSourceMemoryBytes = static_cast<size_t>(preset["s3dZoneSourceMemoryBytes"].asUInt64());
    if (preset.isMember("vboEboMemoryBytes"))
        vboEboMemoryBytes = static_cast<size_t>(preset["vboEboMemoryBytes"].asUInt64());
    if (preset.isMember("characterModelsMemoryBytes"))
        characterModelsMemoryBytes = static_cast<size_t>(preset["characterModelsMemoryBytes"].asUInt64());
    if (preset.isMember("impostorTextureMemoryBytes"))
        impostorTextureMemoryBytes = static_cast<size_t>(preset["impostorTextureMemoryBytes"].asUInt64());
    if (preset.isMember("impostorTierDistance"))
        impostorTierDistance = preset["impostorTierDistance"].asFloat();
    if (preset.isMember("eqe1MappedAddressSpaceBytes"))
        eqe1MappedAddressSpaceBytes = static_cast<size_t>(preset["eqe1MappedAddressSpaceBytes"].asUInt64());
    // T491: ledger budgets, keyed by eqt::memory::categoryName(). An unknown
    // key is a config error worth hearing about, not a silent skip.
    if (preset.isMember("memoryLedgerBudgets") && preset["memoryLedgerBudgets"].isObject()) {
        const auto& budgets = preset["memoryLedgerBudgets"];
        for (const auto& key : budgets.getMemberNames()) {
            eqt::memory::Category cat;
            if (!eqt::memory::categoryFromName(key, cat)) {
                LOG_WARN(MOD_GRAPHICS, "constrained preset '{}': unknown memoryLedgerBudgets key '{}' ignored",
                         presetName, key);
                continue;
            }
            memoryLedgerBudgetBytes[static_cast<size_t>(cat)] =
                static_cast<size_t>(budgets[key].asUInt64());
        }
    }
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
    if (preset.isMember("eqe1Path"))
        eqe1Path = preset["eqe1Path"].asString();
    if (preset.isMember("enableCompositeEntities"))
        enableCompositeEntities = preset["enableCompositeEntities"].asBool();
    if (preset.isMember("compositeEntityPath"))
        compositeEntityPath = preset["compositeEntityPath"].asString();
    if (preset.isMember("antiAliasLevel"))
        antiAliasLevel = preset["antiAliasLevel"].asInt();
    if (preset.isMember("anisotropicFilterLevel"))
        anisotropicFilterLevel = preset["anisotropicFilterLevel"].asInt();
    if (preset.isMember("adaptiveLightingZoneBudget"))
        adaptiveLightingZoneBudget = preset["adaptiveLightingZoneBudget"].asInt();
    if (preset.isMember("adaptiveLightingObjectBudget"))
        adaptiveLightingObjectBudget = preset["adaptiveLightingObjectBudget"].asInt();
    if (preset.isMember("adaptiveLightingBudgetMode")) {
        std::string mode = preset["adaptiveLightingBudgetMode"].asString();
        adaptiveLightingBudgetMode = (mode == "area")
            ? AdaptiveLightingBudgetMode::Area
            : (mode == "radius")
                  ? AdaptiveLightingBudgetMode::Radius
                  : AdaptiveLightingBudgetMode::Count;
    }
    if (preset.isMember("adaptiveLightingAreaBudgetFactor"))
        adaptiveLightingAreaBudgetFactor = preset["adaptiveLightingAreaBudgetFactor"].asFloat();
    if (preset.isMember("adaptiveLightingAreaPPFloor"))
        adaptiveLightingAreaPPFloor = preset["adaptiveLightingAreaPPFloor"].asInt();
    if (preset.isMember("adaptiveLightingRadiusMargin"))
        adaptiveLightingRadiusMargin = preset["adaptiveLightingRadiusMargin"].asFloat();
    // "mesh_memory_mb"/"meshMemoryBytes" preset keys removed T67 (field
    // removed with ConstrainedMeshCache). No shipped config/ preset used
    // either key (verified); a local custom JSON setting one is now
    // silently ignored, consistent with how any other unrecognized preset
    // key is already treated here (no allowlist/reject-unknown-keys
    // validation exists in this loader).
    if (preset.isMember("targetFps"))
        targetFps = preset["targetFps"].asFloat();
    if (preset.isMember("entityPrepMaxPvsDepth"))
        entityPrepMaxPvsDepth = preset["entityPrepMaxPvsDepth"].asInt();
    if (preset.isMember("terrainPrepMaxPvsDepth"))
        terrainPrepMaxPvsDepth = preset["terrainPrepMaxPvsDepth"].asInt();
    if (preset.isMember("objectPrepMaxPvsDepth"))
        objectPrepMaxPvsDepth = preset["objectPrepMaxPvsDepth"].asInt();
    if (preset.isMember("pvsNeighborhoodHops"))
        pvsNeighborhoodHops = preset["pvsNeighborhoodHops"].asInt();
    if (preset.isMember("deferredAssetLoading")) {
        std::string mode = preset["deferredAssetLoading"].asString();
        std::string lowerMode = mode;
        std::transform(lowerMode.begin(), lowerMode.end(), lowerMode.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        deferredAssetLoading = (lowerMode == "automatic" || lowerMode == "auto" || lowerMode == "true" || lowerMode == "1");
    }
    if (preset.isMember("backgroundThreadCount"))
        backgroundThreadCount = preset["backgroundThreadCount"].asInt();

    // Rendering backend — P32: GLES2 is the only supported backend
    // (docs/decisions.md 2026-07-28 addendum). "renderingBackend"/"useGLES2"
    // JSON keys are accepted but no longer change anything; kept only so
    // old preset files with these keys don't fail to load.
    if (preset.isMember("renderingBackend")) {
        std::string backend = preset["renderingBackend"].asString();
        std::string lowerBackend = backend;
        std::transform(lowerBackend.begin(), lowerBackend.end(), lowerBackend.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (lowerBackend != "gles2" && lowerBackend != "opengles2") {
            LOG_WARN(MOD_GRAPHICS, "Preset '{}' requests removed renderingBackend '{}' — "
                     "only gles2 is supported (docs/decisions.md 2026-07-28); ignoring",
                     presetName, backend);
        }
    }

    // Display mode
    if (preset.isMember("useDRM"))
        useDRM = preset["useDRM"].asBool();

    // Rendering feature toggles
    if (preset.isMember("fog"))
        fog = preset["fog"].asBool();
    if (preset.isMember("wireframe"))
        wireframe = preset["wireframe"].asBool();
    if (preset.isMember("portalOcclusion"))
        portalOcclusion = preset["portalOcclusion"].asBool();
    // B971 (Will, 2026-10-01: "orangepi uses a 16-bit depth buffer (no
    // stencil user there)"): /portal occlusion is the ONLY stencil consumer in
    // the tree, so a config that turns it on MUST have the stencil buffer on
    // the surface -- otherwise it builds a portal system that can never render
    // (the pre-B971 defect: the shipped JSON had portalOcclusion off but
    // enableStencilBuffer on, paying for D24S8 for a feature that was off; the
    // reverse footgun is portal on with no stencil). Make the implication
    // one-way (portal on => stencil on; stencil on does not force portal on,
    // so a stencil-less config stays stencil-less) and let it log, so a
    // local/custom config that re-enables portal occlusion gets D24S8 back
    // automatically instead of silently mis-rendering. The shipped orangepi
    // block sets portalOcclusion:false, so this is a no-op for it.
    if (portalOcclusion && !enableStencilBuffer) {
        LOG_WARN(MOD_GRAPHICS,
                 "Preset '{}': portalOcclusion is on but enableStencilBuffer is off; "
                 "turning the stencil buffer back on (D24S8 surface) -- portal occlusion "
                 "is the only stencil user, so it cannot render without it (B971)",
                 presetName);
        enableStencilBuffer = true;
    }
    if (preset.isMember("playerLight"))
        playerLight = preset["playerLight"].asBool();
    if (preset.isMember("objectLights"))
        objectLights = preset["objectLights"].asBool();
    if (preset.isMember("directionalLight"))
        directionalLight = preset["directionalLight"].asBool();
    if (preset.isMember("fireEffects"))
        fireEffects = preset["fireEffects"].asBool();
    if (preset.isMember("skyRendering"))
        skyRendering = preset["skyRendering"].asBool();
    if (preset.isMember("skyTextures")) {
        std::string val = preset["skyTextures"].asString();
        if (val == "original") skyDomeMode = SkyDomeMode::Original;
        else if (val == "procedural") skyDomeMode = SkyDomeMode::Procedural;
    }
    if (preset.isMember("nameTagsEnabled"))
        nameTagsEnabled = preset["nameTagsEnabled"].asBool();
    if (preset.isMember("frameTimingEnabled"))
        frameTimingEnabled = preset["frameTimingEnabled"].asBool();
    if (preset.isMember("chrCacheMaxEntries"))
        chrCacheMaxEntries = static_cast<size_t>(preset["chrCacheMaxEntries"].asUInt());
    if (preset.isMember("mergeZoneDraws"))
        mergeZoneDraws = preset["mergeZoneDraws"].asBool();

    // P39: occlusionMaxOccluderRegions previously had NO isMember() parse
    // here, even though fromPreset()'s OrangePi case sets it to the P14-
    // blessed 256 (constrained_renderer_config.cpp's OrangePi block).
    // Precedence, now coherent end to end: fromPreset() establishes the
    // tier baseline (256 for OrangePi, 16 struct-default otherwise); this
    // JSON key, when present, overrides that baseline (last-writer-wins,
    // same as every other field in this function). The gap this closes:
    // any caller that skips fromPreset() and builds a config via
    // default-construct + loadJsonOverrides() only (the test-fixture
    // pattern in test_constrained_renderer.cpp, and the general shape of
    // "load from JSON" call sites) previously fell through to the struct
    // default of 16 with zero way to recover 256 short of hardcoding it —
    // the JSON preset file could not express this budget at all. It now
    // can, and the shipped orangepi preset sets it explicitly (see
    // config/constrained_presets.json) so the blessed value is
    // version-controlled and visible in the JSON, not just buried in a
    // C++ switch-case default.
    if (preset.isMember("occlusionMaxOccluderRegions"))
        occlusionMaxOccluderRegions = preset["occlusionMaxOccluderRegions"].asInt();
    LOG_INFO(MOD_GRAPHICS, "Preset '{}': occlusionMaxOccluderRegions={} ({})",
             presetName, occlusionMaxOccluderRegions,
             preset.isMember("occlusionMaxOccluderRegions") ? "from JSON" : "C++ preset default (JSON silent)");

    // P42 (P39 same-pattern audit): totalMemoryBudgetBytes, occlusionBufferWidth,
    // and occlusionBufferHeight shared the exact loss pattern P39 fixed for
    // occlusionMaxOccluderRegions above — a non-default value set in
    // fromPreset()'s OrangePi case with no isMember() parse here, so the JSON
    // preset file could not express or override them. Same precedence: the
    // fromPreset() tier baseline applies first, this JSON key (when present)
    // overrides it (last-writer-wins, same as every other field in this
    // function). Shipped config/constrained_presets.json's "orangepi" block
    // now sets all three explicitly (see fromPreset()'s OrangePi case for the
    // C++ defaults these mirror).
    if (preset.isMember("totalMemoryBudgetBytes"))
        totalMemoryBudgetBytes = static_cast<size_t>(preset["totalMemoryBudgetBytes"].asUInt64());
    if (preset.isMember("occlusionBufferWidth"))
        occlusionBufferWidth = preset["occlusionBufferWidth"].asInt();
    if (preset.isMember("occlusionBufferHeight"))
        occlusionBufferHeight = preset["occlusionBufferHeight"].asInt();

    return true;
}

bool ConstrainedRendererConfig::loadDebugOverrides(const std::string& jsonPath) {
    // T399 (board B235): resolved, and a miss is logged -- config/debug.json
    // is shipped, and its skip* flags silently reverting to their built-in
    // values is exactly the class of silence B235 is about.
    const std::string resolved = eqt::ResolveDataPath(jsonPath);
    std::ifstream file(resolved);
    if (!file.is_open()) {
        LOG_ERROR(MOD_GRAPHICS,
                  "ConstrainedRendererConfig: cannot open '{}' (resolved to '{}') -- the "
                  "shipped debug skip-flags are NOT applied",
                  jsonPath, resolved);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_WARN(MOD_GRAPHICS, "Failed to parse debug config {}: {}", jsonPath, errors);
        return false;
    }

    bool anySet = false;

    auto applyBool = [&](const char* key, bool& field) {
        if (root.isMember(key)) {
            field = root[key].asBool();
            anySet = true;
        }
    };
    applyBool("skipManualZoneDraw", skipManualZoneDraw);
    applyBool("skipVBOUpload", skipVBOUpload);
    applyBool("skipEntityTextureUpload", skipEntityTextureUpload);
    applyBool("skipEntityBuild", skipEntityBuild);
    applyBool("skipObjectBuild", skipObjectBuild);
    applyBool("skipConstrainedTextureUpload", skipConstrainedTextureUpload);
    applyBool("skipSkyTextureUpload", skipSkyTextureUpload);
    applyBool("mergeZoneDraws", mergeZoneDraws);

    if (anySet) {
        LOG_WARN(MOD_GRAPHICS, "Debug overrides active from {}: ManualZone={} VBO={} EntityTex={} EntityBuild={} ObjBuild={} TexCache={} SkyTex={} MergeZoneDraws={}",
                 jsonPath,
                 skipManualZoneDraw ? "SKIP" : "ok",
                 skipVBOUpload ? "SKIP" : "ok",
                 skipEntityTextureUpload ? "SKIP" : "ok",
                 skipEntityBuild ? "SKIP" : "ok",
                 skipObjectBuild ? "SKIP" : "ok",
                 skipConstrainedTextureUpload ? "SKIP" : "ok",
                 skipSkyTextureUpload ? "SKIP" : "ok",
                 mergeZoneDraws ? "ON" : "off");
    }

    return true;
}

} // namespace Graphics
} // namespace EQT
