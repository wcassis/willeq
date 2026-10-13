#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/zone_lifecycle/zone_lifecycle_component.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/renderer_zone_merge.h"
#include "client/graphics/draw_census.h"  // B997: per-site 3D-draw census
#include "client/graphics/work_priority.h"
#include "common/logging.h"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#ifdef EQT_HAS_DRM
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#else
#include <GL/gl.h>
#endif
#include <EGL/egl.h>
#endif

// Bridge functions for GLES2 static VBO management.
// Defined in COpenGLES2Driver.cpp (compiled into Irrlicht library), declared here
// to avoid including the full COpenGLES2Driver.h (which depends on Irrlicht-internal headers).
#ifdef EQT_HAS_GLES2
extern bool gles2CreateStaticHWBuffer(void* driver, const void* meshBuffer);
extern void gles2DeleteStaticHWBuffer(void* driver, const void* meshBuffer);
#endif

namespace EQT {
namespace Graphics {

namespace {

// P06: estimate a region's projected screen-space coverage, cheaply and
// conservatively -- project the 8 world-space (Irrlicht-coords) AABB corners
// with the current view-projection matrix and take the clamped screen-space
// bounding-rect area. If any corner is at/behind the near plane (w <= 0),
// the region is treated as covering the full screen -- conservative (never
// under-counts), and avoids near-plane clipping math for a decision that
// only needs to be directionally right, not exact (see
// refactor/P06_area_aware_budget.md "Projection details").
float estimateRegionScreenArea(const irr::core::aabbox3df& worldBox,
                                const irr::core::matrix4& viewProj,
                                float screenW, float screenH) {
    const float fullScreenArea = screenW * screenH;
    const irr::core::vector3df& mn = worldBox.MinEdge;
    const irr::core::vector3df& mx = worldBox.MaxEdge;
    const irr::core::vector3df corners[8] = {
        {mn.X, mn.Y, mn.Z}, {mx.X, mn.Y, mn.Z}, {mn.X, mx.Y, mn.Z}, {mx.X, mx.Y, mn.Z},
        {mn.X, mn.Y, mx.Z}, {mx.X, mn.Y, mx.Z}, {mn.X, mx.Y, mx.Z}, {mx.X, mx.Y, mx.Z}
    };

    float minSx = std::numeric_limits<float>::max();
    float maxSx = -std::numeric_limits<float>::max();
    float minSy = std::numeric_limits<float>::max();
    float maxSy = -std::numeric_limits<float>::max();
    for (int i = 0; i < 8; ++i) {
        float clip[4];
        viewProj.transformVect(clip, corners[i]);
        if (clip[3] <= 0.0001f) {
            return fullScreenArea;  // At/behind near plane -- conservative.
        }
        const float invW = 1.0f / clip[3];
        const float sx = (clip[0] * invW * 0.5f + 0.5f) * screenW;
        const float sy = (1.0f - (clip[1] * invW * 0.5f + 0.5f)) * screenH;
        minSx = std::min(minSx, sx);
        maxSx = std::max(maxSx, sx);
        minSy = std::min(minSy, sy);
        maxSy = std::max(maxSy, sy);
    }

    // Clamp to viewport.
    minSx = std::max(0.0f, minSx);
    maxSx = std::min(screenW, maxSx);
    minSy = std::max(0.0f, minSy);
    maxSy = std::min(screenH, maxSy);

    if (maxSx <= minSx || maxSy <= minSy) return 0.0f;
    return (maxSx - minSx) * (maxSy - minSy);
}

// P07d: compute the player light's effective range R -- the distance beyond
// which the maximum possible per-pixel contribution (attenuation alone,
// saturating NdotL/wrap-lighting at 1.0 -- conservative, see
// refactor/P07d_radius_demotion.md) falls below ~1/255. Attenuation is
// 1/(constant + linear*d + quadratic*d^2) (matches zone_shader.cpp's fillAttenLUT
// and the FS's attenuation LUT lookup); solve for the largest d where
// constant + linear*d + quadratic*d^2 <= 255. Returns FLT_MAX if attenuation
// never decays enough (e.g. quadratic==linear==0, or the solve is otherwise
// degenerate) -- the caller must treat that as "radius mode demotes nothing"
// and the plan requires flagging this for orchestrator review rather than
// tuning unilaterally.
float computeLightEffectiveRadius(const float atten[3], float margin) {
    const float c = atten[0];
    const float l = atten[1];
    const float q = atten[2];
    const float target = 255.0f - c;  // constant + l*d + q*d^2 <= 255
    float r;
    if (q > 0.0f) {
        const float disc = l * l + 4.0f * q * target;
        if (disc < 0.0f || target <= 0.0f) {
            // Attenuation already below threshold at d=0 (or no real root) --
            // degenerate; treat conservatively as "never in range" so no
            // region ever gets a false pass (R=0 demotes everything except
            // regions overlapping the light position itself).
            r = 0.0f;
        } else {
            r = (-l + std::sqrt(disc)) / (2.0f * q);
        }
    } else if (l > 0.0f) {
        r = (target > 0.0f) ? (target / l) : 0.0f;
    } else {
        // No linear or quadratic falloff -- attenuation is constant and never
        // drops below 1/255 (assuming constant <= 255, always true here since
        // constant is O(1)). No finite range exists.
        r = std::numeric_limits<float>::max();
    }
    if (r >= std::numeric_limits<float>::max() / margin) {
        return std::numeric_limits<float>::max();
    }
    return r * margin;
}

// H02b/Q02: address-independent 16-bit texture grouping ID for materialKey.
// The old key was `(uintptr_t)texture & 0xFFFF` -- the low bits of a heap
// POINTER. That made the material-sorted draw order depend on allocation
// addresses, so ASLR (or merely a different environment-block size shifting
// the layout) reordered equal-material draws between otherwise identical
// runs -> z-fighting pixel flips on coplanar geometry and a wobbling
// stateChanges count. This mirrors the fix already applied to the benchmark
// loader (tools/hwcaps/zone_bench_loader.cpp, textureGroupId(), H02b) -- keep
// the two copies in sync. An FNV-1a hash of the texture NAME is just as good
// a grouping key and is stable across runs.
static uint32_t textureGroupId(const irr::video::SMaterial& mat) {
    if (!mat.getTexture(0)) return 0;
    const irr::io::path& tname = mat.getTexture(0)->getName().getPath();
    uint32_t h = 2166136261u;
    for (irr::u32 ci = 0; ci < tname.size(); ++ci) {
        h = (h ^ static_cast<uint8_t>(tname[ci])) * 16777619u;
    }
    return h & 0xFFFF;
}

} // namespace

ZoneMergeBuffers& ZoneLifecycleComponent::ensureZoneMergeBuffers() {
    if (!zoneMergeBuffers_) zoneMergeBuffers_ = std::make_unique<ZoneMergeBuffers>();
    return *zoneMergeBuffers_;
}

bool ZoneLifecycleComponent::zonePoolsEnabled() const {
    // WILLEQ_ZONE_POOLS=0 (or "u", unpooled) forces the legacy per-region-node
    // path with the same binary, so the two can be compared frame for frame.
    const char* poolsEnv = std::getenv("WILLEQ_ZONE_POOLS");
    return ctx_.config.constrainedConfig.mergeZoneDraws &&
           !(poolsEnv && (poolsEnv[0] == '0' || poolsEnv[0] == 'u'));
}

void ZoneLifecycleComponent::createZoneMesh() {
    if (!ctx_.currentZone || !ctx_.currentZone->geometry) {
        return;
    }

    if (ctx_.zoneMeshNode) {
        ctx_.zoneMeshNode->remove();
        ctx_.zoneMeshNode = nullptr;
    }

    ZoneMeshBuilder builder(ctx_.smgr, ctx_.driver, ctx_.device->getFileSystem());

    // Pass constrained texture cache if in constrained mode
    if (ctx_.constrainedTextureCache) {
        builder.setConstrainedTextureCache(ctx_.constrainedTextureCache);
    }
    // Pass GLSL shader material types if available
    if (ctx_.zoneShader && ctx_.zoneShader->isAvailable()) {
        builder.setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                       ctx_.zoneShader->getActiveAlphaTest(),
                                       ctx_.zoneShader->getActiveAlphaTestRGBA());
    }

    irr::scene::IMesh* mesh = nullptr;

    if (!ctx_.currentZone->textures.empty() && !ctx_.currentZone->geometry->textureNames().empty()) {
        mesh = builder.buildTexturedMesh(*ctx_.currentZone->geometry, ctx_.currentZone->textures);
    } else {
        mesh = builder.buildColoredMesh(*ctx_.currentZone->geometry);
    }

    if (mesh) {
        // Use octree scene node for automatic frustum culling of zone geometry
        // minimalPolysPerNode=256 controls octree subdivision granularity
        // This helps with OpenGL/llvmpipe but not with Irrlicht's software renderer
        ctx_.zoneMeshNode = ctx_.smgr->addOctreeSceneNode(mesh, nullptr, -1, 256);
        if (ctx_.zoneMeshNode) {
            LOG_INFO(MOD_GRAPHICS, "Zone mesh created as octree node (polys per node: 256)");
            for (irr::u32 i = 0; i < ctx_.zoneMeshNode->getMaterialCount(); ++i) {
                ctx_.zoneMeshNode->getMaterial(i).Lighting = ctx_.lightingEnabled;
                ctx_.zoneMeshNode->getMaterial(i).BackfaceCulling = false;
                ctx_.zoneMeshNode->getMaterial(i).GouraudShading = true;
                ctx_.zoneMeshNode->getMaterial(i).FogEnable = ctx_.fogEnabled;
                ctx_.zoneMeshNode->getMaterial(i).Wireframe = ctx_.wireframeMode;
                ctx_.zoneMeshNode->getMaterial(i).NormalizeNormals = true;
                // Set material colors for proper lighting response
                ctx_.zoneMeshNode->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
                ctx_.zoneMeshNode->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
            }

            // NOTE: collision is NOT set up here, and NOT progressively. It is built
            // once, by setupMinimalZoneCollision() at load step 9
            // (renderer_zone_load_objects.cpp), which walks ctx_.regionMeshNodes in a
            // single pass; that same step then adds the eagerly-built objects inline.
            // T354 (B047) deleted the incremental registration API this comment used to
            // name -- it had never had a caller. Anything streamed in after step 9 still
            // gets no collision; that gap is on the board, not fixed here.

            // Initialize animated texture manager for zone textures
            ctx_.animatedTextureManager = std::make_unique<AnimatedTextureManager>(ctx_.driver, ctx_.device->getFileSystem());
            if (ctx_.constrainedTextureCache) {
                ctx_.animatedTextureManager->setConstrainedTextureCache(ctx_.constrainedTextureCache);
            }
            int animCount = ctx_.animatedTextureManager->initialize(
                *ctx_.currentZone->geometry, ctx_.currentZone->textures, mesh);

            // Auto-detect water texture animations by naming pattern (water01, water02, etc.)
            int waterAnimCount = ctx_.animatedTextureManager->detectWaterAnimations(
                ctx_.currentZone->textures, mesh);
            animCount += waterAnimCount;

            if (animCount > 0) {
                LOG_DEBUG(MOD_GRAPHICS, "Initialized {} animated zone textures ({} water auto-detected)",
                          animCount, waterAnimCount);
                // Register the zone scene node for animated texture updates
                ctx_.animatedTextureManager->addSceneNode(ctx_.zoneMeshNode);
            }
        }
        mesh->drop();
    }
}

// ======== OnRenderPassPreRender (ILightManager hook) ========
// Fires after CAMERA pass sets up view/projection, before SOLID pass renders nodes.
// We intercept ESNRP_SOLID to draw zone geometry manually in sorted order.
void IrrlichtRenderer::RenderPassTimer::OnRenderPassPreRender(irr::scene::E_SCENE_NODE_RENDER_PASS renderPass) {
    passStart_ = std::chrono::steady_clock::now();
    if (!firstPassSeen_) {
        firstPassStart_ = passStart_;
        firstPassSeen_ = true;
    }

    // Capture 3D camera transforms during the SOLID pass — this is the only
    // reliable point where ETS_VIEW/ETS_PROJECTION contain the 3D perspective
    // camera matrices. By the time drawAll() returns, they may be overwritten
    // by 2D rendering (billboards, GUI nodes, overlays).
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_) {
        renderer_->captured3DView_ = renderer_->driver_->getTransform(irr::video::ETS_VIEW);
        renderer_->captured3DProj_ = renderer_->driver_->getTransform(irr::video::ETS_PROJECTION);
        renderer_->Frame().have3DTransforms() = true;
    }

    // B997 (board #317): the per-material client-array batch of the PVS doors,
    // drawn at this SOLID-pre point (the CAMERA pass has already set the 3D P/V
    // matrices -- see the capture block above). The batch bakes each door's
    // world matrix into its vertices and draws with identity ETS_WORLD, one
    // gathered client-array draw per material group (the zone-merge's primitive,
    // COpenGLES2Driver.cpp:1484). The doors it drew were detached from the
    // scene graph in gatherSceneGraphBatch(), so drawAll()'s SOLID list below
    // never re-draws them; onSolidPassPost() re-attaches them. It sits OUTSIDE
    // the manualZoneDrawEnabled_ block on purpose: the gather runs
    // unconditionally in phase 1, so the render must too, or a door would be
    // detached but never drawn. No-op when the gate is off or nothing batched.
    // See scene_graph_batch.h for the pixel-identity argument.
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_ && renderer_->Context().doorManager)
        renderer_->Context().doorManager->renderSceneGraphBatch();

    // Hook: draw zone geometry manually before Irrlicht's SOLID pass
    if (renderPass == irr::scene::ESNRP_SOLID && renderer_ && renderer_->manualZoneDrawEnabled_) {
        auto drawStart = std::chrono::steady_clock::now();
        // P52: GPU-sync characterization. Zone geometry (manual draw, sorted
        // front-to-back) and entity/door geometry (Irrlicht's own SOLID pass,
        // which follows immediately below — zone nodes are invisible so it
        // only draws entities/doors) are cleanly separable exactly here,
        // which is why this is the injection point rather than wrapping the
        // whole drawAll() call. No-op when the probe is disabled or the
        // driver has no GL_EXT_disjoint_timer_query support (see
        // docker/irrlicht-drm/gpu_sync_probe.h).
        eqtGPUPhaseBegin(0 /* GPUSyncPhase::ZonePass */);
        if (renderer_->portalOcclusionEnabled_ && renderer_->portalOcclusionEligible_) {
            renderer_->ZoneLifecycle().drawZoneGeometryWithPortals();
        } else {
            renderer_->ZoneLifecycle().drawZoneGeometrySorted();
        }
        eqtGPUPhaseEnd(0 /* GPUSyncPhase::ZonePass */);
        renderer_->Frame().timings().manualZoneDraw = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - drawStart).count();

        // Open the entity-pass query now; Irrlicht's own SOLID pass (entities,
        // doors) runs right after this function returns and is closed in
        // OnRenderPassPostRender(ESNRP_SOLID) below.
        eqtGPUPhaseBegin(1 /* GPUSyncPhase::EntityPass */);
        entityGPUQueryActive_ = true;
    }
}

// ======== onSolidPassPost (B997) ========
// Re-attach the PVS doors the per-material client-array batch drew at the
// SOLID-pre point. The batch detached them in gatherSceneGraphBatch() so
// drawAll()'s SOLID list skipped them; this runs right after the SOLID pass
// (before the next frame's gather) and puts them back in the scene graph.
// RenderPassTimer only forward-declares IrrlichtRenderer, so this body lives
// here (same split as OnRenderPassPreRender). No-op when nothing batched.
void IrrlichtRenderer::RenderPassTimer::onSolidPassPost() {
    if (renderer_ && renderer_->Context().doorManager)
        renderer_->Context().doorManager->restoreSceneGraphBatch();
}

// ======== uploadMeshHardwareBuffers ========
// Uploads VBOs/EBOs for all mesh buffers in a mesh node (GLES2 only).
void ZoneLifecycleComponent::uploadMeshHardwareBuffers(irr::scene::IMeshSceneNode* node) {
#ifdef EQT_HAS_GLES2
    if (!node || !ctx_.driver || ctx_.driver->getDriverType() != irr::video::EDT_OGLES2)
        return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf && buf->getVertexCount() > 0)
            gles2CreateStaticHWBuffer(ctx_.driver, buf);
    }
#endif
}

// ======== deleteMeshHardwareBuffers ========
// Deletes VBOs/EBOs for all mesh buffers in a mesh node (GLES2 only).
void ZoneLifecycleComponent::deleteMeshHardwareBuffers(irr::scene::IMeshSceneNode* node) {
#ifdef EQT_HAS_GLES2
    if (!node || !ctx_.driver || ctx_.driver->getDriverType() != irr::video::EDT_OGLES2)
        return;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (buf)
            gles2DeleteStaticHWBuffer(ctx_.driver, buf);
    }
#endif
}

// ======== drawZoneGeometrySorted ========
// Draws zone geometry sorted by material (primary) and distance (secondary).
// Material sorting minimizes expensive setMaterial() state changes (glUseProgram,
// glBindTexture, glUniform*). Distance sorting within each material group
// preserves front-to-back order for early-Z rejection on tile-based GPUs.
void ZoneLifecycleComponent::drawZoneGeometrySorted() {
    if (ctx_.sortedZoneDrawList.empty()) return;

    // Collect all visible mesh buffers into a flat draw list
    sortedDrawEntries_.clear();

    // Adaptive lighting: if /plight is on and budget > 0, nearest N regions
    // get per-pixel lighting; the rest get lightweight per-vertex shaders.
    const bool adaptiveLight = ctx_.zoneShader && ctx_.zoneShader->isPerPixelPlayerLight() &&
                                ctx_.config.constrainedConfig.adaptiveLightingZoneBudget > 0;
    const int zonePPBudget = ctx_.config.constrainedConfig.adaptiveLightingZoneBudget;
    const int objectPPBudget = ctx_.config.constrainedConfig.adaptiveLightingObjectBudget;
    const irr::s32 ppSolid = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeSolid() : -1;
    const irr::s32 ppAlpha = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeAlphaTest() : -1;
    const irr::s32 lwSolid = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeLWSolid() : -1;
    const irr::s32 lwAlpha = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeLWAlphaTest() : -1;

    // Compute lightweight material override for a buffer beyond the per-pixel budget.
    // Returns -1 if no override needed (keep original material).
    const irr::s32 ppWind = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeWindAlphaTest() : -1;
    const irr::s32 lwWind = ctx_.zoneShader ? ctx_.zoneShader->getMaterialTypeLWWindAlphaTest() : -1;

    static bool adaptiveLightLogOnce = false;
    if (!adaptiveLightLogOnce) {
        LOG_INFO(MOD_GRAPHICS, "[ADAPTIVE] adaptiveLight={} ppSolid={} ppAlpha={} ppWind={} lwSolid={} lwAlpha={} lwWind={} zoneBudget={} objBudget={} zoneBudgetMode={} areaFactor={}",
                 adaptiveLight, ppSolid, ppAlpha, ppWind, lwSolid, lwAlpha, lwWind, zonePPBudget, objectPPBudget,
                 (ctx_.config.constrainedConfig.adaptiveLightingBudgetMode ==
                      ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Area) ? "area" :
                 (ctx_.config.constrainedConfig.adaptiveLightingBudgetMode ==
                      ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Radius) ? "radius" : "count",
                 ctx_.config.constrainedConfig.adaptiveLightingAreaBudgetFactor);
        adaptiveLightLogOnce = true;
    }

    auto getLightweightOverride = [&](irr::s32 origType) -> irr::s32 {
        if (!adaptiveLight || lwSolid < 0) return -1;
        if (origType == ppSolid) return lwSolid;
        if (origType == ppAlpha) return lwAlpha;
        if (origType == ppWind && lwWind >= 0) return lwWind;
        return -1;
    };

    auto collectMeshBuffers = [&](irr::scene::IMeshSceneNode* node, float distSq,
                                    bool useLightweight = false) {
        irr::scene::IMesh* mesh = node->getMesh();
        if (!mesh) return;

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(node->getPosition());

        for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();

            // Compute per-buffer material override for adaptive lighting
            irr::s32 matOverride = useLightweight ? getLightweightOverride(mat.MaterialType) : -1;

            // Material key: (materialType << 16) | textureID
            // Groups all same-shader, same-texture draws together
            uint32_t texId = textureGroupId(mat);
            irr::s32 effectiveType = (matOverride >= 0) ? matOverride : mat.MaterialType;
            uint32_t matKey = (static_cast<uint32_t>(effectiveType) << 16) | texId;

            sortedDrawEntries_.push_back({matKey, distSq, buf, worldMat, matOverride});
        }
    };

    // T504 (zone draw rework): POOLED mode has no region nodes. A visible region
    // contributes its ZoneSlices; the material comes from the pools' table.
    const bool pooledDraw = zoneMergeBuffers_ && zoneMergeBuffers_->isPooled();
    const irr::core::matrix4 pooledIdentity;
    auto collectRegionSlices = [&](const ZoneSlice* slices, size_t count, float distSq, bool useLightweight) {
        for (size_t k = 0; k < count; ++k) {
            const irr::video::SMaterial& mat = zoneMergeBuffers_->material(slices[k].materialId);
            irr::s32 matOverride = useLightweight ? getLightweightOverride(mat.MaterialType) : -1;
            uint32_t texId = textureGroupId(mat);
            irr::s32 effectiveType = (matOverride >= 0) ? matOverride : mat.MaterialType;
            uint32_t matKey = (static_cast<uint32_t>(effectiveType) << 16) | texId;
            ZoneDrawEntry de{matKey, distSq, nullptr, pooledIdentity, matOverride};
            de.slice = &slices[k];
            sortedDrawEntries_.push_back(de);
        }
    };
    // World-space box of a region: the node's in legacy mode, the one step 2/4
    // recorded in regionBoundingBoxes in pooled mode.
    auto regionWorldBox = [&](size_t regionIdx, irr::scene::IMeshSceneNode* node, irr::core::aabbox3df& out) -> bool {
        if (!pooledDraw) {
            irr::scene::IMesh* m = node ? node->getMesh() : nullptr;
            if (!m) return false;
            out = m->getBoundingBox();
            out.MinEdge += node->getPosition();
            out.MaxEdge += node->getPosition();
            return true;
        }
        auto bit = ctx_.regionBoundingBoxes.find(regionIdx);
        if (bit == ctx_.regionBoundingBoxes.end()) return false;
        out = bit->second;
        return true;
    };

    // When portal vis is enabled via BFS (no stencil), filter to portal-visible regions
    const bool useBfsPortalCulling = owner_.portalVisibleRegions_ && !owner_.portalVisibleRegions_->empty() &&
                                     ctx_.config.constrainedConfig.portalOcclusion &&
                                     !ctx_.config.constrainedConfig.enableStencilBuffer;

    // Occlusion-culled regions set (from SimWorker's software occlusion pass)
    const std::unordered_set<size_t>* occlusionCulled = nullptr;
    if (owner_.currentFrameSimResults_ && !owner_.currentFrameSimResults_->occlusionCulledRegions.empty()) {
        occlusionCulled = &owner_.currentFrameSimResults_->occlusionCulledRegions;
    }

    // P06: area-mode budget setup. sortedZoneDrawList_ is already nearest-first
    // (frustum-visible regions sorted by distanceSq in sim_visibility.cpp), so
    // the existing draw loop below can accumulate cumulative screen area in
    // order without a separate distance pre-pass.
    const bool areaBudgetMode = adaptiveLight &&
        ctx_.config.constrainedConfig.adaptiveLightingBudgetMode ==
            ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Area;
    irr::core::matrix4 zoneViewProj;
    float screenAreaW = 0.0f, screenAreaH = 0.0f, areaBudgetLimit = 0.0f;
    float cumulativeRegionArea = 0.0f;
    const int areaPPFloor = ctx_.config.constrainedConfig.adaptiveLightingAreaPPFloor;
    if (areaBudgetMode) {
        zoneViewProj = ctx_.captured3DProj * ctx_.captured3DView;
        irr::core::dimension2du screenSize = ctx_.driver->getScreenSize();
        screenAreaW = static_cast<float>(screenSize.Width);
        screenAreaH = static_cast<float>(screenSize.Height);
        areaBudgetLimit = ctx_.config.constrainedConfig.adaptiveLightingAreaBudgetFactor *
                          screenAreaW * screenAreaH;
    }

    // P07d: radius-mode setup. R is computed once per frame from the SAME
    // light[0] position + attenuation the zone shader will use this frame
    // (zoneShader_->playerLightPos()/playerLightAtten(), already in Irrlicht
    // world coords -- see zone_shader.h). adaptiveLightingZoneBudget's numeric
    // value is unused here (only its >0 gate, folded into `adaptiveLight`,
    // matters). regionsDemotedRadius/regionsConsideredRadius are informational
    // only (see refactor/P07d_radius_demotion.md Review for how they're
    // surfaced via the benchmark tool's per-frame CSV).
    const bool radiusBudgetMode = adaptiveLight && ctx_.zoneShader &&
        ctx_.config.constrainedConfig.adaptiveLightingBudgetMode ==
            ConstrainedRendererConfig::AdaptiveLightingBudgetMode::Radius;
    irr::core::vector3df lightPos;
    float lightRangeSq = 0.0f;
    float lightRange = 0.0f;
    int regionsDemotedRadius = 0;
    int regionsConsideredRadius = 0;
    if (radiusBudgetMode) {
        const float* pos = ctx_.zoneShader->playerLightPos();
        lightPos = irr::core::vector3df(pos[0], pos[1], pos[2]);
        lightRange = computeLightEffectiveRadius(
            ctx_.zoneShader->playerLightAtten(),
            ctx_.config.constrainedConfig.adaptiveLightingRadiusMargin);
        lightRangeSq = (lightRange >= std::numeric_limits<float>::max())
                           ? std::numeric_limits<float>::max()
                           : lightRange * lightRange;
    }

    static bool radiusLogOnce = false;
    if (radiusBudgetMode && !radiusLogOnce) {
        LOG_INFO(MOD_GRAPHICS, "[ADAPTIVE][P07d] radius mode: lightPos=({},{},{}) R={} margin={}",
                 lightPos.X, lightPos.Y, lightPos.Z, lightRange,
                 ctx_.config.constrainedConfig.adaptiveLightingRadiusMargin);
        radiusLogOnce = true;
    }

    int regionsDrawn = 0;
    int portalCulled = 0;
    int occlusionCulledCount = 0;
    for (const auto& entry : ctx_.sortedZoneDrawList) {
        if (!pooledDraw && !entry.node) continue;

        // BFS portal culling: skip regions not reachable through portal walk
        if (useBfsPortalCulling && owner_.portalVisibleRegions_->count(entry.regionIdx) == 0) {
            portalCulled++;
            continue;
        }

        // Software occlusion culling: skip regions behind walls/terrain
        if (occlusionCulled && occlusionCulled->count(entry.regionIdx)) {
            occlusionCulledCount++;
            continue;
        }

        auto it = ctx_.regionMeshNodes.find(entry.regionIdx);
        size_t regionSliceCount = 0;
        const ZoneSlice* regionSlices =
            pooledDraw ? zoneMergeBuffers_->slicesForRegion(entry.regionIdx, regionSliceCount) : nullptr;
        if (pooledDraw) {
            if (!regionSlices) continue;
        } else if (it == ctx_.regionMeshNodes.end() || !it->second) {
            continue;
        }
        irr::scene::IMeshSceneNode* regionNode = pooledDraw ? nullptr : it->second;

        bool regionUseLW;
        if (!adaptiveLight) {
            regionUseLW = false;
        } else if (areaBudgetMode) {
            // Add this region's own estimated area to the running total FIRST,
            // then decide. This must include the current region -- the whole
            // point of area mode is that even the FIRST (nearest) region can
            // flip to lightweight if its own screen footprint alone exceeds
            // the budget (the P05 outskirts case: a single huge region covers
            // nearly the whole screen). Deciding on the pre-region total would
            // always give the nearest region a free pass, exactly reproducing
            // the count-mode bug for the single/few-huge-region case.
            irr::core::aabbox3df worldBox;
            const bool regionMesh = regionWorldBox(entry.regionIdx, regionNode, worldBox);
            if (regionMesh) {
                cumulativeRegionArea += estimateRegionScreenArea(
                    worldBox, zoneViewProj, screenAreaW, screenAreaH);
            }
            // P07: hybrid floor -- the first adaptiveLightingAreaPPFloor regions
            // (nearest-first, regionsDrawn is 0 here for the very first region)
            // always keep per-pixel regardless of estimated area. Their area
            // still accumulated above, so later regions demote exactly as
            // before. Count mode is untouched (areaBudgetMode false there).
            regionUseLW = (regionsDrawn >= areaPPFloor) && cumulativeRegionArea > areaBudgetLimit;
        } else if (radiusBudgetMode) {
            // P07d: demote iff the CLOSEST POINT of the region's world AABB
            // to the player light position is beyond the light's effective
            // range R -- closest-point (clamp-to-box), not center distance,
            // so a huge nearby region (outskirts case) whose center is far
            // away is not wrongly demoted. Same clamp-to-box formula
            // sim_visibility.cpp uses for entry.distanceSq (camera-to-region),
            // but computed against the LIGHT position, not the camera --
            // they differ slightly (light sits at player pos + vertical
            // offset, see renderer_entities.cpp), so entry.distanceSq is not
            // reused directly.
            irr::core::aabbox3df worldBox;
            const bool regionMesh = regionWorldBox(entry.regionIdx, regionNode, worldBox);
            bool inRange = true;  // conservative default if mesh missing
            if (regionMesh) {
                const float nearX = std::max(worldBox.MinEdge.X, std::min(lightPos.X, worldBox.MaxEdge.X));
                const float nearY = std::max(worldBox.MinEdge.Y, std::min(lightPos.Y, worldBox.MaxEdge.Y));
                const float nearZ = std::max(worldBox.MinEdge.Z, std::min(lightPos.Z, worldBox.MaxEdge.Z));
                const float dx = nearX - lightPos.X;
                const float dy = nearY - lightPos.Y;
                const float dz = nearZ - lightPos.Z;
                const float distSq = dx * dx + dy * dy + dz * dz;
                inRange = distSq <= lightRangeSq;
                regionsConsideredRadius++;
                if (!inRange) regionsDemotedRadius++;
            }
            regionUseLW = !inRange;
        } else {
            regionUseLW = regionsDrawn >= zonePPBudget;
        }
        if (pooledDraw) {
            collectRegionSlices(regionSlices, regionSliceCount, entry.distanceSq, regionUseLW);
        } else {
            collectMeshBuffers(regionNode, entry.distanceSq, regionUseLW);
        }
        regionsDrawn++;
    }

    // P07d: per-frame informational trace (module TRACE, filtered by default) --
    // demoted-region fraction for the current pose, for measurement/review use.
    if (radiusBudgetMode && regionsConsideredRadius > 0) {
        LOG_TRACE(MOD_GRAPHICS, "[ADAPTIVE][P07d] R={} regionsConsidered={} regionsDemoted={} fraction={}",
                  lightRange, regionsConsideredRadius, regionsDemotedRadius,
                  static_cast<float>(regionsDemotedRadius) / static_cast<float>(regionsConsideredRadius));
    }

    // Add visible object mesh buffers (sorted alongside zone geometry)
    if (!ctx_.objectRecords.empty() && owner_.currentFrameSimResults_) {
        const auto* results = owner_.currentFrameSimResults_;

        // Get camera position for distance sorting
        irr::core::vector3df camPos(0, 0, 0);
        if (ctx_.smgr && ctx_.smgr->getActiveCamera()) {
            camPos = ctx_.smgr->getActiveCamera()->getAbsolutePosition();
        }

        int objectsDrawn = 0;
        // B938 (#256): read the compact record, not the node. The record's
        // world/mesh/buffer-materials are byte-identical to what the old
        // node->getRelativeTransformation()/node->getMesh() provided, so the
        // drawn pixels do not move. (In PVS zones the records carry no node.)
        for (size_t i = 0; i < ctx_.objectRecords.size() && i < results->objectVisible.size(); ++i) {
            if (!results->objectVisible[i]) continue;
            const auto& rec = ctx_.objectRecords[i];
            irr::scene::IMesh* mesh = rec.mesh;
            if (!mesh) continue;

            // Distance from the record's position (Irrlicht coords)
            float distSq = 0.0f;
            {
                irr::core::vector3df diff = rec.position - camPos;
                distSq = diff.getLengthSQ();
            }

            // Adaptive lighting: beyond object budget, use lightweight
            bool objUseLW = adaptiveLight && objectPPBudget > 0 && objectsDrawn >= objectPPBudget;

            // World transform from the record (= the old node's relative transform)
            irr::core::matrix4 worldMat = rec.world;

            for (irr::u32 bi = 0; bi < mesh->getMeshBufferCount(); ++bi) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(bi);
                if (!buf || buf->getVertexCount() == 0) continue;

                const irr::video::SMaterial& mat = buf->getMaterial();
                irr::s32 matOverride = objUseLW ? getLightweightOverride(mat.MaterialType) : -1;

                if (rec.name == "TREE6" || rec.name == "TREE1") {
                    LOG_DEBUG(MOD_GRAPHICS, "[ADAPTIVE] {} obj[{}] buf[{}] matType={} useLW={} override={}",
                              rec.name, i, bi, (int)mat.MaterialType, objUseLW, matOverride);
                }

                uint32_t texId = textureGroupId(mat);
                irr::s32 effectiveType = (matOverride >= 0) ? matOverride : mat.MaterialType;
                uint32_t matKey = (static_cast<uint32_t>(effectiveType) << 16) | texId;

                sortedDrawEntries_.push_back({matKey, distSq, buf, worldMat, matOverride});
            }
            objectsDrawn++;
        }
    }

    // Sort by material key (primary), distance (secondary)
    std::sort(sortedDrawEntries_.begin(), sortedDrawEntries_.end(),
        [](const ZoneDrawEntry& a, const ZoneDrawEntry& b) {
            if (a.materialKey != b.materialKey)
                return a.materialKey < b.materialKey;
            return a.distanceSq < b.distanceSq;
        });

    // P10: cross-region zone draw merge (flag-gated, default off; see
    // renderer_zone_merge.h). Decide whether the merge path is usable THIS
    // frame before drawing. Reduced-landing contract: only ever build the
    // merge buffers once the zone's region set is fully resident with zero
    // evictions so far; destroy + permanently disable for the rest of this
    // zone visit the instant an eviction occurs (the merge buffers are a
    // derived snapshot, never a source of truth, so this is always safe --
    // worst case is losing the merge benefit for a zone that doesn't fit
    // budget, never drawing stale geometry).
    //
    // P10b: the eviction count that gates rebuild/teardown originally also
    // had to include constrainedTextureCache_'s eviction count alongside
    // ConstrainedMeshCache's, because the qeynos2 SIGSEGV this defends
    // against came from the texture cache evicting LRU zone-region textures
    // once entity/equipment texture load pushed total usage over budget --
    // a live, active mechanism a mesh-cache-only gate would have missed.
    // T67 (2026-08-11) removed ConstrainedMeshCache entirely (it was never
    // constructed in any shipped build -- ConstrainedMeshCache::
    // getEvictionCount() had always contributed exactly 0 here; see
    // src/client/graphics/CLAUDE.md for the reference pointer). Only the
    // texture cache's real, live eviction count remains.
    bool mergeActive = false;
    if (ctx_.mergeZoneDrawsEnabled && !mergeZoneDrawsDisabledForZone_) {
        if (!zoneMergeBuffers_) zoneMergeBuffers_ = std::make_unique<ZoneMergeBuffers>();

        const size_t evictionCount = ctx_.constrainedTextureCache ? ctx_.constrainedTextureCache->getEvictionCount() : 0;

        if (zoneMergeBuffers_->isPooled()) {
            // T504: the pools ARE the zone geometry -- there is nothing to fall
            // back to, so an eviction does not tear them down. The eviction guard
            // swaps the evicted texture for the placeholder in the pools'
            // material table, which is what RegionMeshTextureGuard does for
            // region buffers.
        } else if (zoneMergeBuffers_->isBuilt() && evictionCount > mergeZoneDrawsBuildEvictionCount_) {
            // A region's texture evicted after we built the merge -- the
            // merge buffers may reference textures that are no longer
            // resident/valid. Bail permanently for this zone visit rather
            // than rebuild every eviction (rebuild-on-every-eviction would
            // thrash for any zone near its budget, defeating the point of
            // merging).
            LOG_WARN(MOD_GRAPHICS, "[mergedraws] texture cache eviction after merge build ({} evictions) "
                     "-- disabling merge for the rest of this zone visit (falls back to "
                     "the unmerged path)", evictionCount);
            zoneMergeBuffers_->destroy();
            mergeZoneDrawsDisabledForZone_ = true;
        } else if (!zoneMergeBuffers_->isBuilt() && !owner_.isLoading() && !ctx_.regionMeshNodes.empty() &&
                   evictionCount == 0) {
            // "Fully resident": regions are always built eagerly at zone
            // load (loadZoneStep4_Regions()) -- there is no streaming
            // concept to reconcile with, so once loading is done every
            // region that will ever exist already has a node.
            //
            // P10b: pass the texture-liveness check (see
            // renderer_zone_merge.h's build() doc comment) -- defense in
            // depth so a stale/dangling texture pointer, however it got
            // there, is skipped rather than dereferenced.
            auto isTextureLive = ctx_.constrainedTextureCache
                ? std::function<bool(irr::video::ITexture*)>(
                      [this](irr::video::ITexture* tex) { return ctx_.constrainedTextureCache->isLiveTexture(tex); })
                : std::function<bool(irr::video::ITexture*)>();
            zoneMergeBuffers_->build(ctx_.regionMeshNodes, isTextureLive);
            mergeZoneDrawsBuildEvictionCount_ = evictionCount;

            // T504 (load-step audit): what the per-region objects weigh, by part.
            // zone_mesh was 40-55 MiB in dreadlands/iceclad while ALL the zone's
            // vertices fit in 7-9 MB of merged buffers.
            {
                uint64_t nodes = 0, buffers = 0, vbytes = 0, ibytes = 0, nodeMaterials = 0;
                for (const auto& [idx, node] : ctx_.regionMeshNodes) {
                    if (!node || !node->getMesh()) continue;
                    ++nodes;
                    nodeMaterials += node->getMaterialCount();
                    irr::scene::IMesh* m = node->getMesh();
                    for (irr::u32 b = 0; b < m->getMeshBufferCount(); ++b) {
                        irr::scene::IMeshBuffer* mb = m->getMeshBuffer(b);
                        ++buffers;
                        vbytes += static_cast<uint64_t>(mb->getVertexCount()) * sizeof(irr::video::S3DVertex);
                        ibytes += static_cast<uint64_t>(mb->getIndexCount()) * sizeof(irr::u16);
                    }
                }
                LOG_INFO(MOD_GRAPHICS, "[regionmesh] nodes={} mesh_buffers={} vertex_bytes={} index_bytes={} "
                         "node_materials={} sizeof(SMaterial)={} sizeof(SMeshBuffer)={} "
                         "=> buffer_objects={} B, node_material_copies={} B",
                         nodes, buffers, vbytes, ibytes, nodeMaterials,
                         sizeof(irr::video::SMaterial), sizeof(irr::scene::SMeshBuffer),
                         buffers * sizeof(irr::scene::SMeshBuffer),
                         nodeMaterials * sizeof(irr::video::SMaterial));
            }
        }

        mergeActive = zoneMergeBuffers_->isBuilt();
    }
    // T504: pooled geometry can only be drawn by renderMerged() -- whatever the
    // /mergedraws flag says, there are no region buffers to draw unmerged.
    if (pooledDraw && zoneMergeBuffers_->isBuilt()) mergeActive = true;

    int drawCalls = 0;
    if (mergeActive) {
        drawCalls = zoneMergeBuffers_->renderMerged(ctx_.driver, sortedDrawEntries_);
    } else {
        // Draw — setMaterial() is called every time because custom shader callbacks
        // need the current world matrix. However, sorting by material ensures the
        // driver's state tracking (SOGLES2State) skips redundant GL calls for
        // blend/depth/cull/texture/program when consecutive draws share the same material.
        for (const auto& de : sortedDrawEntries_) {
            if (!de.buffer) continue;  // T504: a pooled slice has no mesh buffer
            ctx_.driver->setTransform(irr::video::ETS_WORLD, de.worldMat);
            if (de.materialOverride >= 0) {
                // Adaptive lighting: override material type (per-pixel → lightweight)
                irr::video::SMaterial mat = de.buffer->getMaterial();
                mat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(de.materialOverride);
                ctx_.driver->setMaterial(mat);
            } else {
                ctx_.driver->setMaterial(de.buffer->getMaterial());
            }
            ctx_.driver->drawMeshBuffer(de.buffer);
        }
        drawCalls = static_cast<int>(sortedDrawEntries_.size());
    }
    // B997: count this zone pass's 3D client draws (merged or unmerged) so the
    // [drawsite] census splits the d3client pool by site.
    drawCensusAddZone(drawCalls);

    // P08d-style adoption-line: unmerged-would-be draws vs actual (merged or
    // not) draws, sampled every 120 frames (~2s at 60fps) so live sessions
    // show the reduction without an instrumentation build. sortedDrawEntries_.size()
    // is always the unmerged-would-be draw count, regardless of mergeActive.
    static int mergeSampleCounter = 0;
    if (++mergeSampleCounter >= 120) {
        mergeSampleCounter = 0;
        const int unmergedWouldBe = static_cast<int>(sortedDrawEntries_.size());
        const float reductionPct = unmergedWouldBe > 0
            ? 100.0f * (1.0f - static_cast<float>(drawCalls) / static_cast<float>(unmergedWouldBe))
            : 0.0f;
        LOG_INFO(MOD_GRAPHICS, "[mergedraws] zone={} merge={} unmerged_would_be_draws={} actual_draws={} reduction={:.1f}%",
                 ctx_.currentZoneName, mergeActive ? "ON" : "off", unmergedWouldBe, drawCalls, reductionPct);
    }

    ctx_.Frame().timings().manualZoneRegionsDrawn = regionsDrawn;
    ctx_.Frame().timings().manualZoneDrawCalls = drawCalls;
}

// ======== drawRegionMesh ========
// Draws a single region's mesh buffers with proper world transform.
void ZoneLifecycleComponent::drawRegionMesh(size_t regionIdx) {
    auto it = ctx_.regionMeshNodes.find(regionIdx);
    if (it == ctx_.regionMeshNodes.end() || !it->second) return;
    irr::scene::IMeshSceneNode* node = it->second;

    irr::scene::IMesh* mesh = node->getMesh();
    if (!mesh) return;

    // Set world transform — region nodes only have translation (position), no rotation/scale.
    // Use getPosition() (relative) not getAbsolutePosition() because Irrlicht's OnAnimate()
    // skips invisible nodes, so AbsoluteTransformation is never computed for hidden nodes.
    // Zone mesh nodes are direct children of the root scene node, so relative == absolute.
    irr::core::matrix4 worldMat;
    worldMat.setTranslation(node->getPosition());
    ctx_.driver->setTransform(irr::video::ETS_WORLD, worldMat);

    // Draw each mesh buffer
    for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
        irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
        if (!buf || buf->getVertexCount() == 0) continue;
        ctx_.driver->setMaterial(buf->getMaterial());
        ctx_.driver->drawMeshBuffer(buf);
    }
}

// ======== drawPortalQuad ========
// Draws a portal polygon as a triangle fan (for stencil write/clear).
// Portal vertices are in EQ Z-up coords, converted to Irrlicht Y-up: (x,y,z) -> (x,z,y)
void ZoneLifecycleComponent::drawPortalQuad(const Portal& portal) {
    size_t n = portal.vertexCount();
    if (n < 3) return;

    std::vector<irr::video::S3DVertex> verts(n);
    for (size_t i = 0; i < n; ++i) {
        verts[i].Pos.X = portal.vx(i);
        verts[i].Pos.Y = portal.vz(i);  // EQ Z -> Irrlicht Y
        verts[i].Pos.Z = portal.vy(i);  // EQ Y -> Irrlicht Z
        verts[i].Color = irr::video::SColor(255, 255, 255, 255);
        verts[i].TCoords.X = 0.0f;
        verts[i].TCoords.Y = 0.0f;
        verts[i].Normal.X = portal.normalX;
        verts[i].Normal.Y = portal.normalZ;
        verts[i].Normal.Z = portal.normalY;
    }

    // Fan triangulation: (0,1,2), (0,2,3), (0,3,4), ...
    std::vector<irr::u16> indices;
    indices.reserve((n - 2) * 3);
    for (size_t i = 1; i + 1 < n; ++i) {
        indices.push_back(0);
        indices.push_back(static_cast<irr::u16>(i));
        indices.push_back(static_cast<irr::u16>(i + 1));
    }

    // Identity world transform
    irr::core::matrix4 identity;
    ctx_.driver->setTransform(irr::video::ETS_WORLD, identity);

    // Material: no texture, no lighting, depth test but no depth write
    irr::video::SMaterial mat;
    mat.Lighting = false;
    mat.ZBuffer = irr::video::ECFN_LESSEQUAL;
    mat.ZWriteEnable = false;
    mat.BackfaceCulling = false;
    mat.MaterialType = irr::video::EMT_SOLID;
    ctx_.driver->setMaterial(mat);

    ctx_.driver->drawVertexPrimitiveList(verts.data(), static_cast<irr::u32>(n),
        indices.data(), static_cast<irr::u32>(n - 2),
        irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
}

// ======== computePortalVisibleRegions ========
// Walk the portal graph from the camera's BSP region via BFS.
// Only regions reachable through portal openings that face the camera
// and intersect the view frustum are added.  Depth limit 3.
void ZoneLifecycleComponent::buildRegionNeighborMap() {
    ctx_.regionNeighbors.clear();
    if (!ctx_.portalSystem || !ctx_.portalSystem->hasPortals()) return;

    const auto& portalData = ctx_.portalSystem->getData();
    for (const auto& [regionIdx, portalIndices] : portalData.regionPortals) {
        auto& neighbors = ctx_.regionNeighbors[regionIdx];
        for (size_t pi : portalIndices) {
            size_t other = ctx_.portalSystem->getOtherRegion(pi, regionIdx);
            if (other != SIZE_MAX) neighbors.push_back(other);
        }
    }
    LOG_DEBUG(MOD_GRAPHICS, "Built region neighbor map: {} regions with portal neighbors",
              ctx_.regionNeighbors.size());
}

// ======== drawZoneGeometryWithPortals ========
// Uses stencil-based portal occlusion to render only visible rooms.
void ZoneLifecycleComponent::drawZoneGeometryWithPortals() {
#ifdef EQT_HAS_GLES2
    if (!ctx_.portalSystem || ctx_.currentPvsRegion == SIZE_MAX) {
        // Fallback: just do front-to-back sorted draw
        drawZoneGeometrySorted();
        return;
    }

    // Draw current room with no stencil masking
    std::unordered_set<size_t> drawnRegions;
    drawRegionMesh(ctx_.currentPvsRegion);
    drawnRegions.insert(ctx_.currentPvsRegion);

    // Recursively draw visible rooms through portals using stencil
    drawPortalRecursive(ctx_.currentPvsRegion, 0, 3, drawnRegions);

    // Disable stencil, restore color mask and depth write to defaults
    // (matches SOGLES2State::reset() defaults so driver cache stays in sync)
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
#else
    drawZoneGeometrySorted();
#endif
}

// ======== drawPortalRecursive ========
// Recursively draws rooms visible through portals using nested stencil masking.
void ZoneLifecycleComponent::drawPortalRecursive(size_t fromRegion, int stencilLevel,
                                            int maxDepth,
                                            std::unordered_set<size_t>& drawn) {
#ifdef EQT_HAS_GLES2
    if (!ctx_.portalSystem || maxDepth <= 0) return;

    const auto& portalIndices = ctx_.portalSystem->getPortalsForRegion(fromRegion);

    for (size_t portalIdx : portalIndices) {
        size_t otherRegion = ctx_.portalSystem->getOtherRegion(portalIdx, fromRegion);
        if (otherRegion == SIZE_MAX) continue;
        if (drawn.count(otherRegion)) continue;

        // Check PVS: is otherRegion visible from current region?
        if (ctx_.currentPvsRegion != SIZE_MAX && ctx_.currentPvsRegion < ctx_.zoneBspTree->regions.size()) {
            const auto& currentRegionData = ctx_.zoneBspTree->regions[ctx_.currentPvsRegion];
            if (currentRegionData && otherRegion < currentRegionData->pvsRegionCount()) {
                if (!currentRegionData->pvs()[otherRegion]) continue;
            }
        }

        // Check if the other region has a mesh
        auto nodeIt = ctx_.regionMeshNodes.find(otherRegion);
        if (nodeIt == ctx_.regionMeshNodes.end() || !nodeIt->second) continue;

        // Frustum cull the portal center (quick reject)
        if (ctx_.frustumCuller && ctx_.frustumCuller->isEnabled()) {
            const Portal& portal = ctx_.portalSystem->getData().portals[portalIdx];
            auto bboxIt = ctx_.regionBoundingBoxes.find(otherRegion);
            if (bboxIt != ctx_.regionBoundingBoxes.end()) {
                const auto& bbox = bboxIt->second;
                if (!ctx_.frustumCuller->testAABB(
                        bbox.MinEdge.X, bbox.MinEdge.Y, bbox.MinEdge.Z,
                        bbox.MaxEdge.X, bbox.MaxEdge.Y, bbox.MaxEdge.Z)) {
                    continue;
                }
            }
        }

        const Portal& portal = ctx_.portalSystem->getData().portals[portalIdx];

        // Step 1: Stencil write — draw portal quad, increment stencil where depth passes
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_EQUAL, stencilLevel, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        drawPortalQuad(portal);

        // Step 2: Draw room mesh where stencil == stencilLevel+1
        glStencilFunc(GL_EQUAL, stencilLevel + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        drawRegionMesh(otherRegion);
        drawn.insert(otherRegion);

        // Step 3: Recurse into deeper rooms
        drawPortalRecursive(otherRegion, stencilLevel + 1, maxDepth - 1, drawn);

        // Step 4: Stencil pop — decrement stencil back to previous level
        glStencilFunc(GL_EQUAL, stencilLevel + 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        drawPortalQuad(portal);
    }

    // Restore color/depth after portal loop
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
#endif
}

bool ZoneLifecycleComponent::rebuildRegionMesh(size_t regionIdx) {
    auto rebuildStart = std::chrono::steady_clock::now();
    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: BEGIN region {} | ctx_.currentZone={} wldLoader={} "
             "ctx_.regionMeshNodes.count={}",
             regionIdx,
             ctx_.currentZone != nullptr,
             (ctx_.currentZone && ctx_.currentZone->wldLoader) ? true : false,
             ctx_.regionMeshNodes.count(regionIdx));

    if (!ctx_.currentZone || !ctx_.currentZone->wldLoader) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: ABORT region {} — ctx_.currentZone={} wldLoader={}",
                 regionIdx, ctx_.currentZone != nullptr,
                 (ctx_.currentZone ? (ctx_.currentZone->wldLoader != nullptr) : false));
        return false;
    }

    // Clean up existing node if rebuilding (prevents scene node leak)
    auto existingIt = ctx_.regionMeshNodes.find(regionIdx);
    if (existingIt != ctx_.regionMeshNodes.end() && existingIt->second) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: region {} already has node (parent={}), cleaning up",
                 regionIdx, existingIt->second->getParent() != nullptr);
        auto* oldNode = existingIt->second;
        deleteMeshHardwareBuffers(oldNode);
        if (ctx_.animatedTextureManager)
            ctx_.animatedTextureManager->removeSceneNode(oldNode);
        if (oldNode->getParent()) oldNode->remove(); else oldNode->drop();
        existingIt->second = nullptr;
    }

    auto geom = ctx_.currentZone->wldLoader->getGeometryForRegion(regionIdx);
    if (!geom || geom->vertices.empty()) {
        LOG_WARN(MOD_GRAPHICS, "rebuildRegionMesh: ABORT region {} — getGeometryForRegion returned {} "
                 "(vertices={})",
                 regionIdx, geom ? "geom" : "nullptr",
                 geom ? geom->vertices.size() : 0);
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} geom OK — verts={} tris={} texNames={} "
             "center=({:.1f},{:.1f},{:.1f})",
             regionIdx, geom->vertices.size(), geom->triangles.size(),
             geom->textureNames().size(),
             geom->centerX, geom->centerY, geom->centerZ);

    ZoneMeshBuilder builder(ctx_.smgr, ctx_.driver, ctx_.device->getFileSystem());
    // Use the constrained cache at runtime only, not during sequential loading.
    // (The historical rationale — the cache's async background decode submitting
    // to a thread pool that may not be initialized yet — is stale: T475 deleted
    // the async texture-arrival path and B338 deleted the unused pool handle.
    // The guard itself remains a live runtime condition.)
    if (ctx_.constrainedTextureCache && !owner_.isLoading())
        builder.setConstrainedTextureCache(ctx_.constrainedTextureCache);

    bool shaderAvail = ctx_.zoneShader && ctx_.zoneShader->isAvailable();
    if (shaderAvail)
        builder.setShaderMaterialTypes(ctx_.zoneShader->getActiveSolid(),
                                       ctx_.zoneShader->getActiveAlphaTest(),
                                       ctx_.zoneShader->getActiveAlphaTestRGBA());

    bool hasTextures = !ctx_.currentZone->textures.empty();
    bool hasTexNames = !geom->textureNames().empty();

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} build config — shader={} "
             "hasTextures={} (count={}) hasTexNames={} activeSolid={} activeAlpha={}",
             regionIdx, shaderAvail,
             hasTextures, ctx_.currentZone->textures.size(),
             hasTexNames,
             shaderAvail ? ctx_.zoneShader->getActiveSolid() : -1,
             shaderAvail ? ctx_.zoneShader->getActiveAlphaTest() : -1);

    irr::scene::IMesh* mesh = nullptr;
    const char* buildPath = "none";
    auto meshBuildStart = std::chrono::steady_clock::now();

    if (hasTextures && hasTexNames) {
        buildPath = "buildTexturedMesh";
        mesh = builder.buildTexturedMesh(*geom, ctx_.currentZone->textures);
    } else {
        buildPath = "buildColoredMesh";
        mesh = builder.buildColoredMesh(*geom);
    }

    auto meshBuildEnd = std::chrono::steady_clock::now();
    auto meshBuildMs = std::chrono::duration_cast<std::chrono::microseconds>(
        meshBuildEnd - meshBuildStart).count() / 1000.0f;

    if (!mesh) {
        LOG_ERROR(MOD_GRAPHICS, "rebuildRegionMesh: FAILED region {} — {} returned null "
                  "(took {:.1f}ms) | textures.size={} texNames.size={}",
                  regionIdx, buildPath, meshBuildMs,
                  ctx_.currentZone->textures.size(), geom->textureNames().size());
        return false;
    }

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} mesh built via {} in {:.1f}ms — "
             "buffers={}", regionIdx, buildPath, meshBuildMs, mesh->getMeshBufferCount());

    // Textures that were null during build. The cache is synchronous (T04), so
    // nothing re-textures this region later: this line is B671's evidence, and
    // T475 deleted the pendingTextureRegions_ map it used to feed.
    const auto& missing = builder.getMissingTextures();
    if (!missing.empty()) {
        LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: region {} has {} missing textures "
                 "(not re-textured later, board B671)", regionIdx, missing.size());
    }

    auto* node = ctx_.smgr->addMeshSceneNode(mesh);
    if (!node) {
        LOG_ERROR(MOD_GRAPHICS, "rebuildRegionMesh: FAILED region {} — addMeshSceneNode returned null",
                  regionIdx);
        mesh->drop();
        return false;
    }

    // Apply standard zone region materials
    node->setPosition(irr::core::vector3df(geom->centerX, geom->centerZ, geom->centerY));
    for (irr::u32 i = 0; i < node->getMaterialCount(); ++i) {
        node->getMaterial(i).Lighting = ctx_.lightingEnabled;
        node->getMaterial(i).BackfaceCulling = false;
        node->getMaterial(i).GouraudShading = true;
        node->getMaterial(i).FogEnable = ctx_.fogEnabled;
        node->getMaterial(i).Wireframe = ctx_.wireframeMode;
        node->getMaterial(i).NormalizeNormals = true;
        node->getMaterial(i).AmbientColor = irr::video::SColor(255, 255, 255, 255);
        node->getMaterial(i).DiffuseColor = irr::video::SColor(255, 255, 255, 255);
    }
    mesh->drop();

    // Force AbsoluteTransformation update after setPosition — Irrlicht only updates
    // this in OnAnimate() for visible nodes. Since we immediately set the node invisible
    // (below), OnAnimate() will skip it and the AbsoluteTransformation would remain at
    // identity (from the constructor). extractWorldTriangles(), which
    // setupMinimalZoneCollision() runs over these nodes, uses getAbsoluteTransformation()
    // to transform collision triangles, so a stale identity transform would place them at
    // center-relative positions instead of world-space.
    node->updateAbsolutePosition();

    // Start invisible — PVS will make it visible on the next Tier2 frame if appropriate.
    // Without this, newly-built nodes are visible by default in Irrlicht, and if PVS
    // doesn't run before the next render, they add to the polygon count unchecked.
    node->setVisible(false);

    // When manual draw is active, remove from scene graph entirely — the manual draw
    // path accesses nodes directly via regionMeshNodes_ map using getPosition()/getMesh().
    if (ctx_.manualZoneDrawEnabled) {
        node->grab();
        node->remove();
    }

    // Update renderer state
    ctx_.regionMeshNodes[regionIdx] = node;

    // Upload static VBOs for zone geometry (GLES2 only)
    if (!ctx_.config.constrainedConfig.skipVBOUpload) {
#ifdef EQT_HAS_GLES2
        if (ctx_.gpuUploadThread && gpuUploadEnabled_ && ctx_.gpuUploadThread->isAvailable() &&
            node->getMesh() && node->getMesh()->getMeshBufferCount() > 0) {
            // Submit VBO uploads asynchronously — mesh renders via software vertex
            // path (client-side arrays) until each buffer's upload completes.
            irr::scene::IMesh* mesh = node->getMesh();
            for (irr::u32 i = 0; i < mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0 || buf->getIndexCount() == 0)
                    continue;

                UploadRequest req;
                req.type = UploadRequestType::VertexBuffer;
                req.vertexCount = buf->getVertexCount();
                req.indexCount = buf->getIndexCount();

                switch (buf->getVertexType()) {
                    case irr::video::EVT_STANDARD:  req.vertexStride = 36; break;
                    case irr::video::EVT_2TCOORDS:  req.vertexStride = 44; break;
                    case irr::video::EVT_TANGENTS:  req.vertexStride = 60; break;
                    default: req.vertexStride = 36; break;
                }

                size_t vboSize = req.vertexCount * req.vertexStride;
                req.vertexData.resize(vboSize);
                std::memcpy(req.vertexData.data(), buf->getVertices(), vboSize);

                req.indexData.resize(req.indexCount);
                std::memcpy(req.indexData.data(), buf->getIndices(),
                            req.indexCount * sizeof(uint16_t));

                // Encode regionIdx (low 48 bits) + buffer index (high 16 bits)
                req.callbackKey = (static_cast<uint64_t>(i) << 48) |
                                  (static_cast<uint64_t>(regionIdx) & 0xFFFFFFFFFFFFULL);
                req.priority = WorkPriorityKey::make(ctx_.ZoneStream().getRegionPvsDepth(regionIdx), AssetType::ZoneMesh).value;

                ctx_.gpuUploadThread->submit(std::move(req));
            }
            owner_.pendingVBOUploads_.insert(regionIdx);
        } else {
            uploadMeshHardwareBuffers(node);
        }
#else
        uploadMeshHardwareBuffers(node);
#endif
    }

    // Register with animated texture manager (buffer materials only — manual draw path)
    if (ctx_.animatedTextureManager) {
        auto geom = ctx_.currentZone->wldLoader->getGeometryForRegion(regionIdx);
        if (geom) {
            ctx_.animatedTextureManager->addMesh(*geom, ctx_.currentZone->textures, node->getMesh());
            LOG_TRACE(MOD_GRAPHICS, "rebuildRegionMesh: region {} addMesh registered", regionIdx);
        }
    }

    // Report the built mesh's size. T67 (2026-08-11) removed the cache
    // eviction/registration this used to feed (evictUntilAvailable()/
    // onLoaded() on the never-constructed ConstrainedMeshCache -- this call
    // site dereferenced constrainedMeshCache_ WITHOUT a null check, a
    // latent crash if this lazy-rebuild path was ever reached with a live
    // node build while the cache was absent; see this unit's Review for
    // why that appears to never actually happen in practice). The size
    // estimate itself is preserved verbatim as a free function — see
    // src/client/graphics/CLAUDE.md for the reference pointer.
    size_t meshSize = estimateMeshSizeBytes(node);

    auto rebuildEnd = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::microseconds>(
        rebuildEnd - rebuildStart).count() / 1000.0f;

    LOG_INFO(MOD_GRAPHICS, "rebuildRegionMesh: SUCCESS region {} — {} bytes, {:.1f}ms total "
             "(mesh build {:.1f}ms)",
             regionIdx, meshSize, totalMs, meshBuildMs);
    return true;
}

} // namespace Graphics
} // namespace EQT
