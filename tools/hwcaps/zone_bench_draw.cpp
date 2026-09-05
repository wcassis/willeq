// zone_bench_draw.cpp — zone/object/entity draw-entry COLLECTION
//
// T387: extracted verbatim from tools/hwcaps/zone_bench_loader.cpp, which was
// 3,280 lines (the 1,500 ceiling M00 enforces tree-wide, and which
// check_file_sizes.sh did not yet scan under tools/ -- board B167). Function
// bodies are unchanged; only the include set and the file boundary are new.
// The public declarations for everything here stay in zone_bench_loader.h,
// which remains this module's single interface header.

#include "zone_bench_loader.h"
#include "zone_bench_draw_util.h"
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

using namespace EQT::Graphics;

namespace ZoneBench {

// textureGroupId() moved to zone_bench_draw_util.h (T387): it is the one
// helper this split left with callers on both sides of the seam (here and
// zone_bench_entities.cpp's renderBatchedEntities).

static irr::s32 debugMaterialOverride(const ZoneBench::ZoneBenchData& data, irr::s32 currentMaterialType) {
    if (data.shaderDebugMaterial < 0 || !data.zoneShader) return -1;
    if (currentMaterialType >= data.zoneShader->getMaterialTypeSolid()) return data.shaderDebugMaterial;
    return -1;
}

// P06: mirrors IrrlichtRenderer's (anonymous-namespace) estimateRegionScreenArea
// in src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp -- same method (project 8
// world-space AABB corners with the current view-projection, clamped
// screen-space bounding-rect area; any corner at/behind the near plane ->
// conservative full-screen area) so benchmark area-mode numbers predict
// production area-mode behavior.
static float estimateRegionScreenAreaBench(const irr::core::aabbox3df& worldBox,
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

    minSx = std::max(0.0f, minSx);
    maxSx = std::min(screenW, maxSx);
    minSy = std::max(0.0f, minSy);
    maxSy = std::min(screenH, maxSy);

    if (maxSx <= minSx || maxSy <= minSy) return 0.0f;
    return (maxSx - minSx) * (maxSy - minSy);
}

// P07d: mirrors IrrlichtRenderer's (anonymous-namespace) computeLightEffectiveRadius
// in src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp -- same attenuation solve
// (1/(c + l*d + q*d^2) < 1/255, saturating NdotL/wrap-lighting at 1.0,
// conservative) so benchmark radius-mode numbers predict production behavior.
static float computeLightEffectiveRadiusBench(const float atten[3], float margin) {
    const float c = atten[0];
    const float l = atten[1];
    const float q = atten[2];
    const float target = 255.0f - c;
    float r;
    if (q > 0.0f) {
        const float disc = l * l + 4.0f * q * target;
        if (disc < 0.0f || target <= 0.0f) {
            r = 0.0f;
        } else {
            r = (-l + std::sqrt(disc)) / (2.0f * q);
        }
    } else if (l > 0.0f) {
        r = (target > 0.0f) ? (target / l) : 0.0f;
    } else {
        r = std::numeric_limits<float>::max();
    }
    if (r >= std::numeric_limits<float>::max() / margin) {
        return std::numeric_limits<float>::max();
    }
    return r * margin;
}

void collectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    bool materialSort,
    bool pvsCull,
    bool frustumCull,
    bool occlusionCull,
    bool frontToBack,
    int maxDraws,
    int maxPolys,
    std::vector<DrawEntry>& outEntries,
    int& outRegionsVisible,
    int& outRegionsDrawn,
    int& outRegionsOccluded,
    float* outLightRadius,
    int* outRegionsDemotedRadius,
    float* outCollectCullMs,
    float* outSortMs,
    std::vector<int>* outDrawsPerRegion,
    std::vector<int>* outPolysPerDraw)
{
    // P08: collect/cull phase starts here; the sort block below is timed separately.
    const auto collectCullStart = std::chrono::steady_clock::now();

    outEntries.clear();
    outRegionsVisible = 0;
    outRegionsDrawn = 0;
    outRegionsOccluded = 0;

    float camEqX = cam.eqX, camEqY = cam.eqY, camEqZ = cam.eqZ;

    // Find camera's BSP region for PVS
    size_t camRegion = data.bspTree->findRegionIndexForPoint(camEqX, camEqY, camEqZ);

    int polyBudget = (maxPolys > 0) ? maxPolys : std::numeric_limits<int>::max();
    int drawBudget = (maxDraws > 0) ? maxDraws : std::numeric_limits<int>::max();
    int polysCollected = 0;

    // Collect visible regions and their draw entries
    struct RegionEntry {
        size_t idx;
        float distSq;
    };
    std::vector<RegionEntry> visibleRegions;

    for (auto& [regionIdx, rd] : data.regions) {
        // PVS culling
        if (pvsCull && camRegion != SIZE_MAX && data.bspTree) {
            auto& camReg = data.bspTree->regions[camRegion];
            if (camReg && regionIdx < camReg->visibleRegions.size() &&
                !camReg->visibleRegions[regionIdx]) {
                continue;
            }
        }

        // Frustum culling
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testAABB(
                    rd.eqMinX, rd.eqMinY, rd.eqMinZ,
                    rd.eqMaxX, rd.eqMaxY, rd.eqMaxZ)) {
                continue;
            }
        }

        // Distance (squared) from camera to nearest AABB edge
        float dx = std::max(0.0f, std::max(rd.eqMinX - camEqX, camEqX - rd.eqMaxX));
        float dy = std::max(0.0f, std::max(rd.eqMinY - camEqY, camEqY - rd.eqMaxY));
        float dz = std::max(0.0f, std::max(rd.eqMinZ - camEqZ, camEqZ - rd.eqMaxZ));
        float distSq = dx * dx + dy * dy + dz * dz;

        visibleRegions.push_back({regionIdx, distSq});
        outRegionsVisible++;
    }

    // Sort regions front-to-back for draw entry collection and occlusion rasterization
    if (frontToBack || occlusionCull) {
        std::sort(visibleRegions.begin(), visibleRegions.end(),
                  [](const RegionEntry& a, const RegionEntry& b) {
                      return a.distSq < b.distSq;
                  });
    }

    // Software occlusion culling: rasterize nearby occluders, then test AABBs
    if (occlusionCull && data.occlusionCuller && data.occlusionCuller->hasOccluders()) {
        // Compute camera right/up vectors from forward direction (EQ Z-up)
        float fwdLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
        float fX = cam.dirX / fwdLen, fY = cam.dirY / fwdLen, fZ = cam.dirZ / fwdLen;
        // Right = forward × Z-up (then normalize)
        float rX = fY * 0.0f - fZ * 0.0f;  // fwd × (0,0,1) simplified
        float rY = fZ * 0.0f - fX * 0.0f;
        // Actually: right = normalize(forward × (0,0,1))
        rX = fY;   // cross product with Z-up: (fY*1 - fZ*0, fZ*0 - fX*1, fX*0 - fY*0)
        rY = -fX;
        float rZ = 0.0f;
        float rLen = std::sqrt(rX * rX + rY * rY);
        if (rLen > 0.001f) { rX /= rLen; rY /= rLen; }
        // Up = right × forward
        float uX = rY * fZ - rZ * fY;
        float uY = rZ * fX - rX * fZ;
        float uZ = rX * fY - rY * fX;

        data.occlusionCuller->setCamera(camEqX, camEqY, camEqZ,
                                         fX, fY, fZ, rX, rY, rZ, uX, uY, uZ,
                                         cam.fovRadV, cam.aspect);
        data.occlusionCuller->clear();
        data.occlusionCuller->resetStats();

        // Rasterize occluders from nearest regions
        int maxOccluderRegions = data.occlusionCuller->getConfig().maxOccluderRegions;
        int rasterized = 0;
        for (const auto& re : visibleRegions) {
            if (rasterized >= maxOccluderRegions) break;
            const auto& occluders = data.occlusionCuller->getRegionOccluders(re.idx);
            if (occluders.empty()) continue;
            for (const auto& tri : occluders) {
                data.occlusionCuller->rasterizeTriangle(tri.v0, tri.v1, tri.v2);
            }
            rasterized++;
        }

        // Test AABBs and remove occluded regions
        if (rasterized > 0) {
            auto it = visibleRegions.begin();
            while (it != visibleRegions.end()) {
                auto& rd = data.regions[it->idx];
                if (data.occlusionCuller->testAABB(rd.eqMinX, rd.eqMinY, rd.eqMinZ,
                                                    rd.eqMaxX, rd.eqMaxY, rd.eqMaxZ)) {
                    outRegionsOccluded++;
                    outRegionsVisible--;
                    it = visibleRegions.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // P06: area-mode budget decision. Computed as a separate pre-pass sorted
    // strictly by camera distance, independent of visibleRegions' actual
    // iteration/draw order below (which only follows distance when
    // frontToBack||occlusionCull requested a sort) -- the area accumulation's
    // nearest-first invariant must hold regardless of --no-front-to-back.
    // Mirrors IrrlichtRenderer::drawZoneGeometrySorted's area-mode branch.
    std::unordered_map<size_t, bool> areaUseLW;
    if (data.adaptiveLighting && data.zonePPAreaMode) {
        std::vector<const RegionEntry*> distOrder;
        distOrder.reserve(visibleRegions.size());
        for (const auto& re : visibleRegions) distOrder.push_back(&re);
        std::sort(distOrder.begin(), distOrder.end(),
                  [](const RegionEntry* a, const RegionEntry* b) { return a->distSq < b->distSq; });

        irr::core::matrix4 viewProj = data.driver->getTransform(irr::video::ETS_PROJECTION);
        viewProj *= data.driver->getTransform(irr::video::ETS_VIEW);
        irr::core::dimension2du screenSize = data.driver->getScreenSize();
        float screenW = static_cast<float>(screenSize.Width);
        float screenH = static_cast<float>(screenSize.Height);
        float areaBudgetLimit = data.zonePPAreaBudgetFactor * screenW * screenH;
        float cumulativeArea = 0.0f;
        int regionsDrawn = 0;
        for (const auto* rePtr : distOrder) {
            auto& rd = data.regions[rePtr->idx];
            // Add this region's own area FIRST, then decide -- even the
            // nearest region can flip to lightweight if its own footprint
            // alone exceeds the budget (see the matching comment in
            // IrrlichtRenderer::drawZoneGeometrySorted).
            if (rd.mesh) {
                irr::core::aabbox3df worldBox = rd.mesh->getBoundingBox();
                worldBox.MinEdge += rd.position;
                worldBox.MaxEdge += rd.position;
                cumulativeArea += estimateRegionScreenAreaBench(worldBox, viewProj, screenW, screenH);
            }
            // P07: hybrid floor -- first zonePPAreaFloor regions (nearest-first)
            // always keep per-pixel regardless of estimated area; their area
            // still accumulated above so later regions demote as before.
            areaUseLW[rePtr->idx] = (regionsDrawn >= data.zonePPAreaFloor) &&
                                    cumulativeArea > areaBudgetLimit;
            regionsDrawn++;
        }
    }

    // P07d: radius-mode budget decision. Computed as its own pre-pass (order
    // doesn't matter here -- unlike area mode, radius mode's per-region
    // decision is independent across regions, no running total) keyed by
    // region index. Mirrors IrrlichtRenderer::drawZoneGeometrySorted's
    // radius-mode branch: demote iff the closest point of the region's world
    // AABB to the player light position is beyond R.
    std::unordered_map<size_t, bool> radiusUseLW;
    int regionsDemotedRadiusLocal = 0;
    int regionsConsideredRadiusLocal = 0;
    float lightRadiusLocal = 0.0f;
    if (data.adaptiveLighting && data.zonePPRadiusMode && data.zoneShader) {
        const float* pos = data.zoneShader->playerLightPos();
        irr::core::vector3df lightPos(pos[0], pos[1], pos[2]);
        lightRadiusLocal = computeLightEffectiveRadiusBench(
            data.zoneShader->playerLightAtten(), data.zonePPRadiusMargin);
        const float lightRangeSq = (lightRadiusLocal >= std::numeric_limits<float>::max())
                                        ? std::numeric_limits<float>::max()
                                        : lightRadiusLocal * lightRadiusLocal;
        for (const auto& re : visibleRegions) {
            auto& rd = data.regions[re.idx];
            if (!rd.mesh) continue;
            irr::core::aabbox3df worldBox = rd.mesh->getBoundingBox();
            worldBox.MinEdge += rd.position;
            worldBox.MaxEdge += rd.position;
            const float nearX = std::max(worldBox.MinEdge.X, std::min(lightPos.X, worldBox.MaxEdge.X));
            const float nearY = std::max(worldBox.MinEdge.Y, std::min(lightPos.Y, worldBox.MaxEdge.Y));
            const float nearZ = std::max(worldBox.MinEdge.Z, std::min(lightPos.Z, worldBox.MaxEdge.Z));
            const float dx = nearX - lightPos.X;
            const float dy = nearY - lightPos.Y;
            const float dz = nearZ - lightPos.Z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            const bool demote = distSq > lightRangeSq;
            radiusUseLW[re.idx] = demote;
            regionsConsideredRadiusLocal++;
            if (demote) regionsDemotedRadiusLocal++;
        }
    }
    if (outLightRadius) *outLightRadius = lightRadiusLocal;
    if (outRegionsDemotedRadius) *outRegionsDemotedRadius = regionsDemotedRadiusLocal;
    (void)regionsConsideredRadiusLocal;

    // Collect mesh buffers from visible regions
    // visibleRegions is sorted front-to-back (nearest first) when
    // frontToBack||occlusionCull; the area-mode decision above does not rely
    // on this loop's order (see pre-pass above).
    int regionIdx = 0;
    for (const auto& re : visibleRegions) {
        auto& rd = data.regions[re.idx];
        if (!rd.mesh) continue;

        // Adaptive zone lighting: nearest N regions get per-pixel, rest get lightweight
        bool usePerPixel;
        if (!data.adaptiveLighting) {
            usePerPixel = true;
        } else if (data.zonePPAreaMode) {
            auto itLW = areaUseLW.find(re.idx);
            usePerPixel = (itLW == areaUseLW.end()) || !itLW->second;
        } else if (data.zonePPRadiusMode) {
            auto itLW = radiusUseLW.find(re.idx);
            usePerPixel = (itLW == radiusUseLW.end()) || !itLW->second;
        } else {
            usePerPixel = regionIdx < data.perPixelZoneBudget;
        }

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(rd.position);

        bool budgetExceeded = false;
        int drawsThisRegion = 0;
        for (irr::u32 i = 0; i < rd.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = rd.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            int bufPolys = buf->getIndexCount() / 3;
            if (polysCollected + bufPolys > polyBudget) { budgetExceeded = true; break; }
            if (static_cast<int>(outEntries.size()) >= drawBudget) { budgetExceeded = true; break; }

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)

            // H02d: --shader-debug takes priority over adaptive lighting swaps.
            irr::s32 matOverride = debugMaterialOverride(data, mat.MaterialType);

            // Compute material override for adaptive lighting
            if (matOverride < 0 && data.adaptiveLighting && !usePerPixel && data.materialLWSolid >= 0) {
                irr::s32 origType = mat.MaterialType;
                // Map per-pixel material types to their lightweight equivalents
                if (origType == data.materialPerPixelSolid)
                    matOverride = data.materialLWSolid;
                else if (origType == data.materialPerPixelAlpha)
                    matOverride = data.materialLWAlpha;
            }

            uint32_t effectiveMatType = (matOverride >= 0) ?
                static_cast<uint32_t>(matOverride) : static_cast<uint32_t>(mat.MaterialType);
            uint32_t matKey = (effectiveMatType << 16) | texId;

            outEntries.push_back({matKey, re.distSq, buf, worldMat, matOverride});
            polysCollected += bufPolys;
            drawsThisRegion++;
            if (outPolysPerDraw) outPolysPerDraw->push_back(bufPolys);
        }
        if (outDrawsPerRegion && drawsThisRegion > 0) outDrawsPerRegion->push_back(drawsThisRegion);

        regionIdx++;
        outRegionsDrawn++;
        if (budgetExceeded) break;
    }

    if (outCollectCullMs) {
        *outCollectCullMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - collectCullStart).count();
    }

    // Material sort (primary key: material, secondary: distance)
    const auto sortStart = std::chrono::steady_clock::now();
    if (materialSort) {
        std::sort(outEntries.begin(), outEntries.end(),
                  [](const DrawEntry& a, const DrawEntry& b) {
                      if (a.materialKey != b.materialKey)
                          return a.materialKey < b.materialKey;
                      return a.distanceSq < b.distanceSq;
                  });
    }
    if (outSortMs) {
        *outSortMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - sortStart).count();
    }
}

void collectObjectDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool pvsCull,
    bool frustumCull,
    bool occlusionCull,
    std::vector<DrawEntry>& outEntries,
    int& outObjectDrawCalls,
    int& outObjectPolys) {

    outObjectDrawCalls = 0;
    outObjectPolys = 0;
    float renderDistSq = renderDistance * renderDistance;

    // Find camera's BSP region for PVS
    size_t camRegion = SIZE_MAX;
    if (pvsCull && data.bspTree) {
        camRegion = data.bspTree->findRegionIndexForPoint(cam.eqX, cam.eqY, cam.eqZ);
    }

    for (const auto& obj : data.objects) {
        if (!obj.mesh) continue;

        // Distance cull (nearest AABB edge to camera)
        float dx = std::max(0.0f, std::max(obj.eqMinX - cam.eqX, cam.eqX - obj.eqMaxX));
        float dy = std::max(0.0f, std::max(obj.eqMinY - cam.eqY, cam.eqY - obj.eqMaxY));
        float dz = std::max(0.0f, std::max(obj.eqMinZ - cam.eqZ, cam.eqZ - obj.eqMaxZ));
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        // PVS culling: object visible if ANY of its regions are in camera's PVS
        if (pvsCull && camRegion != SIZE_MAX && data.bspTree &&
            !obj.bspRegions.empty()) {
            auto& camReg = data.bspTree->regions[camRegion];
            bool pvsVisible = false;
            if (camReg) {
                for (size_t regionIdx : obj.bspRegions) {
                    if (regionIdx < camReg->visibleRegions.size() &&
                        camReg->visibleRegions[regionIdx]) {
                        pvsVisible = true;
                        break;
                    }
                }
            }
            if (!pvsVisible) continue;
        }

        // Frustum cull (use object AABB)
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testAABB(
                    obj.eqMinX, obj.eqMinY, obj.eqMinZ,
                    obj.eqMaxX, obj.eqMaxY, obj.eqMaxZ)) {
                continue;
            }
        }

        // Software occlusion cull (test AABB against occlusion buffer from zone pass)
        if (occlusionCull && data.occlusionCuller && data.occlusionCuller->hasOccluders()) {
            if (data.occlusionCuller->testAABB(
                    obj.eqMinX, obj.eqMinY, obj.eqMinZ,
                    obj.eqMaxX, obj.eqMaxY, obj.eqMaxZ)) {
                continue;
            }
        }

        for (irr::u32 i = 0; i < obj.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = obj.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;
            irr::s32 matOverride = debugMaterialOverride(data, mat.MaterialType);  // H02d

            outEntries.push_back({matKey, distSq, buf, obj.worldTransform, matOverride});
            outObjectDrawCalls++;
            outObjectPolys += buf->getIndexCount() / 3;
        }
    }
}

void collectEntityDrawEntries(
    ZoneBenchData& data,
    const CameraState& cam,
    float renderDistance,
    bool frustumCull,
    bool occlusionCull,
    std::vector<DrawEntry>& outEntries,
    int& outEntityDrawCalls,
    int& outEntityPolys,
    int& outEntitiesVisible) {

    outEntityDrawCalls = 0; outEntityPolys = 0; outEntitiesVisible = 0;
    float renderDistSq = renderDistance * renderDistance;

    for (const auto& ent : data.entities) {
        if (!ent.mesh) continue;

        // Distance cull
        float dx = ent.eqX - cam.eqX;
        float dy = ent.eqY - cam.eqY;
        float dz = ent.eqZ - cam.eqZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > renderDistSq) continue;

        // Frustum cull (use entity position as a sphere test with radius ~5)
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testSphere(ent.eqX, ent.eqY, ent.eqZ, 5.0f))
                continue;
        }

        // Software occlusion cull (approximate entity as small AABB around position)
        if (occlusionCull && data.occlusionCuller && data.occlusionCuller->hasOccluders()) {
            float r = 3.0f;  // approximate entity half-size
            if (data.occlusionCuller->testAABB(
                    ent.eqX - r, ent.eqY - r, ent.eqZ,
                    ent.eqX + r, ent.eqY + r, ent.eqZ + 8.0f)) {
                continue;
            }
        }

        irr::core::matrix4 worldMat;
        worldMat.setTranslation(ent.position);

        bool contributedBuffer = false;
        for (irr::u32 i = 0; i < ent.mesh->getMeshBufferCount(); ++i) {
            irr::scene::IMeshBuffer* buf = ent.mesh->getMeshBuffer(i);
            if (!buf || buf->getVertexCount() == 0) continue;

            const irr::video::SMaterial& mat = buf->getMaterial();
            uint32_t texId = textureGroupId(mat);  // H02b: address-independent (see textureGroupId)
            uint32_t matKey = (static_cast<uint32_t>(mat.MaterialType) << 16) | texId;

            outEntries.push_back({matKey, distSq, buf, worldMat});
            outEntityDrawCalls++;
            outEntityPolys += buf->getIndexCount() / 3;
            contributedBuffer = true;
        }
        if (contributedBuffer) outEntitiesVisible++;  // P08b: count once per entity
    }
}

} // namespace ZoneBench
