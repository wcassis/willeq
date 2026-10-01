// zone_bench_lod.cpp — the LOD / impostor / adaptive-LOD entity render paths
//
// T387: extracted verbatim from tools/hwcaps/zone_bench_loader.cpp, which was
// 3,280 lines (the 1,500 ceiling M00 enforces tree-wide, and which
// check_file_sizes.sh did not yet scan under tools/ -- board B167). Function
// bodies are unchanged; only the include set and the file boundary are new.
// The public declarations for everything here stay in zone_bench_loader.h,
// which remains this module's single interface header.

#include "zone_bench_loader.h"
#include "zone_bench_impostor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <cstdlib>
#include <mutex>
#include <set>
#include <unordered_map>

#include "common/logging.h"

#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/eq/zone_geometry.h"
#include "client/graphics/zone_shader.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/frustum_culler.h"
#include "client/graphics/portal_system.h"
#include "client/zone_bsp.h"

#include "client/graphics/software_occlusion_culler.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_texture_index.h"
#include "client/graphics/load_mem_audit.h"

// The LOD/impostor paths call GLES2 entry points. In the pre-split TU this
// include sat mid-file, INSIDE namespace ZoneBench; at file scope here it
// declares the same extern "C" entry points.
#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>
#endif

using namespace EQT::Graphics;

namespace ZoneBench {

int renderLodEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    float billboardDist,
    bool frustumCull,
    int& outEntityPolys,
    int& outTier1Count,
    int& outTier2Count) {

    outEntityPolys = 0;
    outTier1Count = 0;
    outTier2Count = 0;
    float renderDistSq = renderDistance * renderDistance;
    float bbDistSq = billboardDist * billboardDist;

    // Camera basis vectors for billboard orientation (EQ Z-up)
    float fwdLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
    float fX = cam.dirX / fwdLen, fY = cam.dirY / fwdLen;
    // Right vector (EQ Z-up): forward × (0,0,1)
    float rX = fY, rY = -fX;
    float rLen = std::sqrt(rX * rX + rY * rY);
    if (rLen > 0.001f) { rX /= rLen; rY /= rLen; }

    // Tier 1: composite entities (near)
    std::vector<irr::video::S3DVertex> compositeVerts;
    std::vector<irr::u16> compositeIndices;
    irr::core::matrix4 identityMat;
    int drawCalls = 0;

    // Tier 2: billboard quads (far) — collected then drawn in one batch
    std::vector<irr::video::S3DVertex> bbVerts;
    std::vector<irr::u16> bbIndices;
    irr::video::SMaterial bbMaterial;
    bool hasBbMaterial = false;

    const float bbHalfW = 3.0f;   // Billboard half-width in EQ units
    const float bbHeight = 8.0f;  // Billboard height in EQ units

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        if (distSq < bbDistSq) {
            // Tier 1: composite entity (1 draw per entity)
            compositeVerts.clear();
            compositeIndices.clear();
            irr::video::SMaterial compositeMat;
            bool hasMat = false;

            for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0) continue;

                if (!hasMat) { compositeMat = buf->getMaterial(); hasMat = true; }

                irr::u32 baseVertex = static_cast<irr::u32>(compositeVerts.size());
                if (baseVertex + buf->getVertexCount() > 65535) break;

                const irr::video::S3DVertex* srcVerts =
                    static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                    irr::video::S3DVertex vert = srcVerts[v];
                    vert.Pos += ent.position;
                    compositeVerts.push_back(vert);
                }

                const irr::u16* srcIdx = buf->getIndices();
                for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                    compositeIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }

            if (!compositeIndices.empty() && hasMat) {
                data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
                data.driver->setMaterial(compositeMat);
                data.driver->drawVertexPrimitiveList(
                    compositeVerts.data(), compositeVerts.size(),
                    compositeIndices.data(), compositeIndices.size() / 3,
                    irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                    irr::video::EIT_16BIT);
                outEntityPolys += compositeIndices.size() / 3;
                drawCalls++;
                outTier1Count++;
            }
        } else {
            // Tier 2: billboard quad
            // Grab material from first buffer if we don't have one yet
            if (!hasBbMaterial && ent.mesh->getMeshBufferCount() > 0) {
                auto* buf = ent.mesh->getMeshBuffer(0);
                if (buf) { bbMaterial = buf->getMaterial(); hasBbMaterial = true; }
            }

            // Check 16-bit index overflow
            if (bbVerts.size() + 4 > 65535) continue;

            irr::u32 base = static_cast<irr::u32>(bbVerts.size());

            // Billboard corners in Irrlicht coords (Y-up)
            // Entity position is already in Irrlicht coords
            // Right vector: EQ (rX, rY, 0) -> Irrlicht (rX, 0, rY)
            float irrRX = rX * bbHalfW, irrRZ = rY * bbHalfW;

            irr::video::SColor white(255, 255, 255, 255);
            // Bottom-left
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X - irrRX, ent.position.Y,
                ent.position.Z - irrRZ, 0, 0, 1, white, 0.0f, 1.0f));
            // Bottom-right
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X + irrRX, ent.position.Y,
                ent.position.Z + irrRZ, 0, 0, 1, white, 1.0f, 1.0f));
            // Top-right
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X + irrRX, ent.position.Y + bbHeight,
                ent.position.Z + irrRZ, 0, 0, 1, white, 1.0f, 0.0f));
            // Top-left
            bbVerts.push_back(irr::video::S3DVertex(
                ent.position.X - irrRX, ent.position.Y + bbHeight,
                ent.position.Z - irrRZ, 0, 0, 1, white, 0.0f, 0.0f));

            bbIndices.push_back(base + 0);
            bbIndices.push_back(base + 1);
            bbIndices.push_back(base + 2);
            bbIndices.push_back(base + 0);
            bbIndices.push_back(base + 2);
            bbIndices.push_back(base + 3);

            outTier2Count++;
        }
    }

    // Draw all billboards in one batch
    if (!bbIndices.empty() && hasBbMaterial) {
        data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
        data.driver->setMaterial(bbMaterial);
        data.driver->drawVertexPrimitiveList(
            bbVerts.data(), bbVerts.size(),
            bbIndices.data(), bbIndices.size() / 3,
            irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
            irr::video::EIT_16BIT);
        outEntityPolys += bbIndices.size() / 3;
        drawCalls++;  // One draw for ALL billboards
    }

    return drawCalls;
}

// B952: the impostor textures moved to zone_bench_impostor.cpp (ImpostorCache).
// Before B952 getOrCreateImpostor() lived here: one addRenderTargetTexture()
// (RGBA8 + its own DEPTH16) per entity at a fixed 256^2, created lazily INSIDE
// the draw (a mid-frame FBO switch), captured from the model's +X side.

// B952: the tier decision, split out of renderAdaptiveLodEntities() so the
// impostor pre-pass (before beginScene) and the draw use the same plan.
void planAdaptiveLod(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    float lastFrameMs) {

    float renderDistSq = renderDistance * renderDistance;
    auto& cfg = data.adaptiveLodConfig;
    auto& state = data.adaptiveLodState;
    const float tierDistSq = cfg.tierDist * cfg.tierDist;

    // Tick hysteresis timers (decrement counters, remove expired)
    state.tickHysteresis();

    // ── Collect visible entities with priority scoring ──
    struct VisibleEntity {
        int entityIdx;
        float distSq;
        float priorityScore;  // Lower = higher priority (render as composite first)
    };
    std::vector<VisibleEntity> visible;

    for (int i = 0; i < static_cast<int>(data.entities.size()); ++i) {
        const auto& ent = data.entities[i];
        if (!ent.mesh) continue;

        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        // Priority score: lower = more important = rendered as composite first.
        // Priority tiers dominate, distance breaks ties within a tier.
        // Tier weight: Target=0, Group=1, ..., Idle=7. Multiply by large constant
        // so tier always dominates over distance.
        float tierWeight;
        switch (ent.priority) {
            case ZoneBenchData::EntityPriority::Target:          tierWeight = 0.0f; break;
            case ZoneBenchData::EntityPriority::Group:           tierWeight = 1.0f; break;
            case ZoneBenchData::EntityPriority::Raid:            tierWeight = 2.0f; break;
            case ZoneBenchData::EntityPriority::Guild:           tierWeight = 3.0f; break;
            case ZoneBenchData::EntityPriority::Friend:          tierWeight = 4.0f; break;
            case ZoneBenchData::EntityPriority::RecentInteract:  tierWeight = 5.0f; break;
            case ZoneBenchData::EntityPriority::Default:         tierWeight = 6.0f; break;
            case ZoneBenchData::EntityPriority::Idle:            tierWeight = 7.0f; break;
            default: tierWeight = 6.0f; break;
        }
        float dist = std::sqrt(distSq);
        // 10000 ensures tier always dominates over distance (max zone dist ~3000)
        float priorityScore = tierWeight * 10000.0f + dist;
        visible.push_back({i, distSq, priorityScore});
    }

    // Sort by priority score ascending — lowest = highest priority = composite first
    std::sort(visible.begin(), visible.end(),
              [](const VisibleEntity& a, const VisibleEntity& b) {
                  return a.priorityScore < b.priorityScore;
              });

    int visibleCount = static_cast<int>(visible.size());

    // ── Proactive prediction: how many composites can we afford? ──
    // Two signals:
    //   1) Frame time: predictedCost = baseZoneMs + N * perEntityMs < budget * safetyMargin
    //   2) Hard cap: never exceed maxEntitiesPerScene
    float effectiveBudgetMs = cfg.frameBudgetMs * cfg.safetyMargin;
    float headroomMs = effectiveBudgetMs - state.avgBaseZoneMs;
    int rawLimit = (state.avgPerEntityMs > 0.001f)
        ? static_cast<int>(headroomMs / state.avgPerEntityMs)
        : cfg.maxEntitiesPerScene;
    rawLimit = std::max(rawLimit, 1);
    rawLimit = std::min(rawLimit, cfg.maxEntitiesPerScene);

    // Rate-limit changes to prevent oscillation (max ±2 per frame)
    if (state.smoothedCompositeLimit < 0) {
        state.smoothedCompositeLimit = rawLimit;  // Initialize on first frame
    } else {
        int delta = rawLimit - state.smoothedCompositeLimit;
        int maxDelta = state.maxLimitChangePerFrame;
        if (delta > maxDelta) delta = maxDelta;
        if (delta < -maxDelta) delta = -maxDelta;
        state.smoothedCompositeLimit += delta;
    }
    int compositeLimit = state.smoothedCompositeLimit;

    // Debug: log prediction state periodically
    static int lodFrameCounter = 0;
    if (++lodFrameCounter % 60 == 1) {
        LOG_INFO(MOD_GRAPHICS, "AdaptiveLOD: visible={} limit={} (raw={} cap={}) "
                 "avgBase={:.1f}ms avgEntity={:.1f}ms headroom={:.1f}ms budget={:.1f}ms lastFrame={:.1f}ms",
                 visibleCount, compositeLimit, rawLimit, cfg.maxEntitiesPerScene,
                 state.avgBaseZoneMs, state.avgPerEntityMs, headroomMs, effectiveBudgetMs, lastFrameMs);
    }

    // ── Walk sorted list: composites up to limit, then billboards ──
    state.plan.clear();
    state.plan.reserve(visible.size());
    int compositeIdx = 0;
    for (int vi = 0; vi < visibleCount; ++vi) {
        const auto& ve = visible[vi];

        // B952: beyond the distance tier an entity is always an impostor and
        // takes no composite slot (tierDist 0 = off).
        const bool beyondTier = cfg.tierDist > 0.0f && ve.distSq > tierDistSq;

        // Determine if this entity should be a billboard:
        // 1) Over the composite limit (frame-time or hard-cap driven)
        // 2) At the boundary (within 3 slots of limit) AND has active hysteresis timer —
        //    prevents flickering. Entities well within the limit always recover.
        bool overLimit = (compositeIdx >= compositeLimit);
        bool nearBoundary = (compositeIdx >= compositeLimit - 3);
        bool forcedBillboard = nearBoundary && state.isForcedBillboard(ve.entityIdx);
        bool isBillboard = beyondTier || overLimit || forcedBillboard;

        if (isBillboard) {
            // Start or refresh hysteresis timer
            state.markBillboard(ve.entityIdx, cfg.hysteresisFrames);
        } else {
            compositeIdx++;
        }
        state.plan.push_back({ve.entityIdx, isBillboard});
    }
    state.planValid = true;
}

int renderAdaptiveLodEntities(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    float lastFrameMs,
    int& outEntityPolys,
    int& outTier1Count,
    int& outTier2Count,
    float& outEntityMs) {

    outEntityPolys = 0;
    outTier1Count = 0;
    outTier2Count = 0;
    outEntityMs = 0;
    auto& cfg = data.adaptiveLodConfig;
    auto& state = data.adaptiveLodState;

    if (!state.planValid)
        planAdaptiveLod(data, cam, renderDistance, frustumCull, lastFrameMs);

    ImpostorCache& impostors = impostorCache(data);

    std::vector<irr::video::S3DVertex> compositeVerts;
    std::vector<irr::u16> compositeIndices;
    irr::core::matrix4 identityMat;
    int drawCalls = 0;
    int compositeIdx = 0;

    float entityDrawMs = 0;  // Accumulate only actual draw time (not impostor creation)

    for (const auto& pe : state.plan) {
        const auto& ent = data.entities[pe.entityIdx];

        if (pe.billboard) {
            // B952: the pre-pass normally created this already; acquire()
            // creates it here only when no pre-pass ran (a mid-frame FBO
            // switch). Creation is NOT counted in entity draw time -- a
            // one-time cost that would otherwise poison the rolling average.
            ImpostorEntry* imp = impostors.acquire(data, pe.entityIdx, cam);
            if (!imp || !imp->tex) continue;

            drawImpostorBillboard(data, *imp, ent.position, cam);
            drawCalls++;

            outTier2Count++;
        } else {
            // Composite entity (1 draw call)
            compositeVerts.clear();
            compositeIndices.clear();
            irr::video::SMaterial compositeMat;
            bool hasMat = false;

            for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
                irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
                if (!buf || buf->getVertexCount() == 0) continue;
                if (!hasMat) { compositeMat = buf->getMaterial(); hasMat = true; }

                irr::u32 baseVertex = static_cast<irr::u32>(compositeVerts.size());
                if (baseVertex + buf->getVertexCount() > 65535) break;

                const irr::video::S3DVertex* srcVerts =
                    static_cast<const irr::video::S3DVertex*>(buf->getVertices());
                for (irr::u32 v = 0; v < buf->getVertexCount(); ++v) {
                    irr::video::S3DVertex vert = srcVerts[v];
                    vert.Pos += ent.position;
                    compositeVerts.push_back(vert);
                }

                const irr::u16* srcIdx = buf->getIndices();
                for (irr::u32 idx = 0; idx < buf->getIndexCount(); ++idx)
                    compositeIndices.push_back(static_cast<irr::u16>(srcIdx[idx] + baseVertex));
            }

            if (!compositeIndices.empty() && hasMat) {
                // Adaptive lighting: nearest composites get per-pixel shader
                if (data.adaptiveLighting && data.materialLWSolid >= 0) {
                    if (compositeIdx < data.perPixelEntityBudget) {
                        compositeMat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                            data.materialPerPixelSolid);
                    } else {
                        compositeMat.MaterialType = static_cast<irr::video::E_MATERIAL_TYPE>(
                            data.materialLWSolid);
                    }
                }
                compositeIdx++;

                auto drawStart = std::chrono::steady_clock::now();
                data.driver->setTransform(irr::video::ETS_WORLD, identityMat);
                data.driver->setMaterial(compositeMat);
                data.driver->drawVertexPrimitiveList(
                    compositeVerts.data(), compositeVerts.size(),
                    compositeIndices.data(), compositeIndices.size() / 3,
                    irr::video::EVT_STANDARD, irr::scene::EPT_TRIANGLES,
                    irr::video::EIT_16BIT);
                outEntityPolys += compositeIndices.size() / 3;
                drawCalls++;
                outTier1Count++;
                entityDrawMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - drawStart).count();
            }
        }
    }
    state.planValid = false;  // next frame plans again

    outEntityMs = entityDrawMs;

    // Update rolling averages for next frame's prediction.
    // Skip on first frame (lastFrameMs == 0) to avoid poisoning the average.
    if (lastFrameMs > 0.0f && outTier1Count > 0) {
        state.updateRollingAverage(lastFrameMs, outEntityMs, outTier1Count,
                                   cfg.rollingWindowSize);
    }

    return drawCalls;
}

} // namespace ZoneBench
