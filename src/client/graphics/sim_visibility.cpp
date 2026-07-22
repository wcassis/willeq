#include "client/graphics/simulation_worker.h"
#include "client/graphics/portal_system.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/hc_map.h"
#include "common/logging.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace EQT {
namespace Graphics {

// ============================================================================
// Region Calculation (prep layer — runs during loading too)
// ============================================================================

void SimulationWorker::calculateRegions(const SimulationInput& input, SimulationOutput& output) {
    if (!zoneData_.bspTree || zoneData_.regionBounds.empty()) return;

    const auto& bsp = *zoneData_.bspTree;

    // BSP lookup — find which region the camera is in (EQ Z-up coords)
    size_t cameraRegion = bsp.findRegionIndexForPoint(input.camEqX, input.camEqY, input.camEqZ);
    output.currentPvsRegion = cameraRegion;

    // Check if camera is in an indoor region (for per-region portal occlusion)
    output.cameraInIndoorRegion = !zoneData_.indoorRegions.empty() &&
                                   cameraRegion != SIZE_MAX &&
                                   zoneData_.indoorRegions.count(cameraRegion) > 0;

    // Clear prep-layer outputs
    output.meshLoadQueue.clear();
    output.protectedRegions.clear();

    float renderDistSq = input.renderDistance * input.renderDistance;

    // Select candidate list: pre-computed PVS-visible indices (O(PVS)) or all indices (O(N))
    const std::vector<size_t>* candidates = &zoneData_.allBoundsIndices;
    if (zoneData_.usePvsCulling && cameraRegion != SIZE_MAX) {
        auto it = zoneData_.pvsVisibleBoundsIndices.find(cameraRegion);
        if (it != zoneData_.pvsVisibleBoundsIndices.end()) {
            candidates = &it->second;
        }
    }

    for (size_t boundsIdx : *candidates) {
        const auto& rb = zoneData_.regionBounds[boundsIdx];

        // Distance culling: nearest point on AABB to camera (EQ coords)
        float nearX = std::max(rb.minX, std::min(input.camEqX, rb.maxX));
        float nearY = std::max(rb.minY, std::min(input.camEqY, rb.maxY));
        float nearZ = std::max(rb.minZ, std::min(input.camEqZ, rb.maxZ));
        float dx = nearX - input.camEqX;
        float dy = nearY - input.camEqY;
        float dz = nearZ - input.camEqZ;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) continue;

        // Queue for lazy mesh loading (PVS + distance only — orientation-independent)
        output.meshLoadQueue.push_back({rb.regionIdx, distSq});

        // Track for mesh cache protection (all PVS+distance regions)
        output.protectedRegions.push_back(rb.regionIdx);
    }

    // Sort mesh load queue nearest-first for priority preloading
    std::sort(output.meshLoadQueue.begin(), output.meshLoadQueue.end(),
              [](const auto& a, const auto& b) { return a.distanceSq < b.distanceSq; });

    // Compute PVS depth map (portal adjacency BFS)
    computeRegionDepthMap(input, output);

    // Fallback for outdoor zones / regions not reachable via portals:
    // Assign depth based on Euclidean distance buckets (100 units per depth level)
    for (const auto& sr : output.meshLoadQueue) {
        if (output.regionPvsDepth.count(sr.regionIdx) == 0) {
            float dist = std::sqrt(sr.distanceSq);
            uint8_t depth = static_cast<uint8_t>(std::min(254.0f, dist / 100.0f));
            output.regionPvsDepth[sr.regionIdx] = depth;
        }
    }
}

// ============================================================================
// PVS Visibility Computation (render layer — skipped during loading)
// ============================================================================

void SimulationWorker::computeVisibility(const SimulationInput& input, SimulationOutput& output) {
    if (!zoneData_.bspTree || zoneData_.regionBounds.empty()) return;

    size_t regionCount = zoneData_.regionBounds.size();

    // Ensure output is sized correctly and zeroed
    if (output.regionVisible.size() != regionCount) {
        output.regionVisible.resize(regionCount, 0);
    } else {
        std::fill(output.regionVisible.begin(), output.regionVisible.end(), 0);
    }

    // currentPvsRegion already set by calculateRegions()
    output.sortedRegions.clear();

    float renderDistSq = input.renderDistance * input.renderDistance;

    // Select candidate list: pre-computed PVS-visible indices or all indices
    const std::vector<size_t>* candidates = &zoneData_.allBoundsIndices;
    if (zoneData_.usePvsCulling && output.currentPvsRegion != SIZE_MAX) {
        auto it = zoneData_.pvsVisibleBoundsIndices.find(output.currentPvsRegion);
        if (it != zoneData_.pvsVisibleBoundsIndices.end()) {
            candidates = &it->second;
        }
    }

    for (size_t boundsIdx : *candidates) {
        const auto& rb = zoneData_.regionBounds[boundsIdx];

        // Distance culling
        float nearX = std::max(rb.minX, std::min(input.camEqX, rb.maxX));
        float nearY = std::max(rb.minY, std::min(input.camEqY, rb.maxY));
        float nearZ = std::max(rb.minZ, std::min(input.camEqZ, rb.maxZ));
        float dx = nearX - input.camEqX;
        float dy = nearY - input.camEqY;
        float dz = nearZ - input.camEqZ;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) continue;

        // Frustum culling (EQ Z-up coordinates)
        if (input.frustumValid) {
            if (!testFrustumAABB(input.frustumPlanes,
                                  rb.minX, rb.minY, rb.minZ,
                                  rb.maxX, rb.maxY, rb.maxZ)) {
                continue;
            }
        }

        output.regionVisible[boundsIdx] = 1;

        // Add to sorted draw list (frustum-visible only)
        output.sortedRegions.push_back({rb.regionIdx, distSq});
    }

    // Sort front-to-back by distance
    std::sort(output.sortedRegions.begin(), output.sortedRegions.end(),
              [](const auto& a, const auto& b) { return a.distanceSq < b.distanceSq; });

    LOG_DEBUG(MOD_GRAPHICS, "RegionVis: {}/{} visible (candidates={}, frustumValid={})",
              output.sortedRegions.size(), regionCount, candidates->size(), input.frustumValid);
}

// ============================================================================
// Portal Visibility Computation (BFS walk from camera room through portals)
// ============================================================================

void SimulationWorker::computePortalVisibility(const SimulationInput& input, SimulationOutput& output) {
    output.portalVisibleRegions.clear();

    // BFS walk is the CPU fallback for portal visibility when stencil masking is
    // not available. When stencil IS available, the GPU does recursive stencil
    // masking at render time (drawZoneGeometryWithPortals) — no BFS needed.
    if (!zoneData_.enablePortalVis || zoneData_.enableStencilBuffer) {
        return;
    }

    const auto* portalSystem = zoneData_.portalSystem;
    if (!portalSystem || !portalSystem->hasPortals() || output.currentPvsRegion == SIZE_MAX) {
        return;
    }

    // Camera's room is always visible
    size_t cameraRegion = output.currentPvsRegion;
    output.portalVisibleRegions.insert(cameraRegion);

    // BFS stack: (regionIdx, depth)
    struct Entry { size_t region; int depth; };
    std::vector<Entry> stack;
    stack.push_back({cameraRegion, 0});

    constexpr int MAX_DEPTH = 1;
    constexpr size_t MAX_VISIBLE_REGIONS = 16;

    float camX = input.camEqX, camY = input.camEqY, camZ = input.camEqZ;

    while (!stack.empty()) {
        auto [fromRegion, depth] = stack.back();
        stack.pop_back();
        if (depth >= MAX_DEPTH) continue;

        const auto& portals = portalSystem->getPortalsForRegion(fromRegion);
        for (size_t portalIdx : portals) {
            size_t toRegion = portalSystem->getOtherRegion(portalIdx, fromRegion);
            if (toRegion == SIZE_MAX) continue;
            if (output.portalVisibleRegions.count(toRegion)) continue;

            const Portal& portal = portalSystem->getData().portals[portalIdx];

            // Skip vertical portals — floor/ceiling AABB overlaps (|normalZ| > 0.7)
            float absNZ = portal.normalZ < 0 ? -portal.normalZ : portal.normalZ;
            if (absNZ > 0.7f) continue;

            // Facing check: is the portal opening facing toward the camera?
            float toPX = portal.centerX - camX;
            float toPY = portal.centerY - camY;
            float toPZ = portal.centerZ - camZ;
            float normalSign = (fromRegion == portal.regionA) ? 1.0f : -1.0f;
            float facingDot = normalSign * (toPX * portal.normalX + toPY * portal.normalY + toPZ * portal.normalZ);
            if (facingDot < 0.0f) continue;

            // Frustum check: is the portal opening visible?
            if (input.frustumValid) {
                size_t nv = portal.vertexCount();
                if (nv == 0) continue;
                float minX = portal.vx(0), maxX = minX;
                float minY = portal.vy(0), maxY = minY;
                float minZ = portal.vz(0), maxZ = minZ;
                for (size_t v = 1; v < nv; ++v) {
                    if (portal.vx(v) < minX) minX = portal.vx(v);
                    if (portal.vx(v) > maxX) maxX = portal.vx(v);
                    if (portal.vy(v) < minY) minY = portal.vy(v);
                    if (portal.vy(v) > maxY) maxY = portal.vy(v);
                    if (portal.vz(v) < minZ) minZ = portal.vz(v);
                    if (portal.vz(v) > maxZ) maxZ = portal.vz(v);
                }
                if (!testFrustumAABB(input.frustumPlanes, minX, minY, minZ, maxX, maxY, maxZ)) {
                    continue;
                }
            }

            output.portalVisibleRegions.insert(toRegion);

            // Bail if too many regions — open area, portal culling won't help
            if (output.portalVisibleRegions.size() > MAX_VISIBLE_REGIONS) {
                output.portalVisibleRegions.clear();
                return;
            }

            stack.push_back({toRegion, depth + 1});
        }
    }
}

// ============================================================================
// PVS Depth Map (pure adjacency BFS, no frustum/facing checks)
// ============================================================================

void SimulationWorker::computeRegionDepthMap(const SimulationInput& input, SimulationOutput& output) {
    output.regionPvsDepth.clear();
    const auto* portalSystem = zoneData_.portalSystem;
    if (!portalSystem || !portalSystem->hasPortals() || output.currentPvsRegion == SIZE_MAX)
        return;

    // Cache: skip BFS if region unchanged from previous frame
    if (output.currentPvsRegion == cachedDepthMapRegion_ && !cachedDepthMap_.empty()) {
        output.regionPvsDepth = cachedDepthMap_;
        return;
    }

    size_t cameraRegion = output.currentPvsRegion;
    output.regionPvsDepth[cameraRegion] = 0;

    struct Entry { size_t region; uint8_t depth; };
    std::vector<Entry> queue;
    queue.push_back({cameraRegion, 0});
    size_t head = 0;

    constexpr uint8_t MAX_DEPTH = 8;

    while (head < queue.size()) {
        auto [fromRegion, depth] = queue[head++];
        if (depth >= MAX_DEPTH) continue;

        const auto& portals = portalSystem->getPortalsForRegion(fromRegion);
        for (size_t portalIdx : portals) {
            size_t toRegion = portalSystem->getOtherRegion(portalIdx, fromRegion);
            if (toRegion == SIZE_MAX) continue;
            if (output.regionPvsDepth.count(toRegion)) continue;

            uint8_t newDepth = depth + 1;
            output.regionPvsDepth[toRegion] = newDepth;
            queue.push_back({toRegion, newDepth});
        }
    }

    // Cache for next frame
    cachedDepthMapRegion_ = cameraRegion;
    cachedDepthMap_ = output.regionPvsDepth;
}

// ============================================================================
// Object Visibility Computation
// ============================================================================

void SimulationWorker::computeObjectVisibility(const SimulationInput& input, SimulationOutput& output) {
    size_t objectCount = zoneData_.objects.size();
    if (objectCount == 0) return;

    if (output.objectVisible.size() != objectCount) {
        output.objectVisible.resize(objectCount, 0);
    }

    float renderDistSq = input.renderDistance * input.renderDistance;
    int objVisible = 0, objDistCulled = 0, objPvsCulled = 0, objFrustumCulled = 0, objNoNode = 0;

    // Camera position in Irrlicht Y-up for AABB distance checks
    const auto& camPos = input.cameraPos;

    for (size_t i = 0; i < objectCount; ++i) {
        const auto& obj = zoneData_.objects[i];
        if (!obj.hasNode) {
            output.objectVisible[i] = 0;
            objNoNode++;
            continue;
        }

        // Distance culling: nearest point on AABB to camera (Irrlicht Y-up)
        const auto& bb = obj.boundingBox;
        float nearX = std::max(bb.MinEdge.X, std::min(camPos.X, bb.MaxEdge.X));
        float nearY = std::max(bb.MinEdge.Y, std::min(camPos.Y, bb.MaxEdge.Y));
        float nearZ = std::max(bb.MinEdge.Z, std::min(camPos.Z, bb.MaxEdge.Z));
        float dx = nearX - camPos.X;
        float dy = nearY - camPos.Y;
        float dz = nearZ - camPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.objectVisible[i] = 0;
            objDistCulled++;
            continue;
        }

        // PVS check: if object has a known BSP region, check PVS visibility
        if (zoneData_.usePvsCulling && zoneData_.bspTree &&
            obj.bspRegion != SIZE_MAX && output.currentPvsRegion != SIZE_MAX) {
            const auto& regions = zoneData_.bspTree->regions;
            if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                if (!pvs.empty() && obj.bspRegion < pvs.size() && !pvs[obj.bspRegion]) {
                    output.objectVisible[i] = 0;
                    objPvsCulled++;
                    continue;
                }
            }
        }

        // Frustum culling (convert AABB to EQ Z-up for frustum test)
        // Irrlicht (X,Y,Z) → EQ (X,Z,Y): swap Y and Z
        if (input.frustumValid) {
            if (!testFrustumAABB(input.frustumPlanes,
                                  bb.MinEdge.X, bb.MinEdge.Z, bb.MinEdge.Y,
                                  bb.MaxEdge.X, bb.MaxEdge.Z, bb.MaxEdge.Y)) {
                output.objectVisible[i] = 0;
                objFrustumCulled++;
                continue;
            }
        }

        output.objectVisible[i] = 1;
        objVisible++;
    }

    LOG_DEBUG(MOD_GRAPHICS, "ObjectVis: {}/{} visible (noNode={}, dist={}, pvs={}, frustum={})",
              objVisible, objectCount, objNoNode, objDistCulled, objPvsCulled, objFrustumCulled);
}

// ============================================================================
// Zone Light Visibility Computation
// ============================================================================

void SimulationWorker::computeLightVisibility(const SimulationInput& input, SimulationOutput& output) {
    size_t lightCount = zoneData_.zoneLights.size();
    if (lightCount == 0) return;

    if (output.lightVisible.size() != lightCount) {
        output.lightVisible.resize(lightCount, 0);
    }

    float renderDistSq = input.renderDistance * input.renderDistance;
    const auto& camPos = input.cameraPos;
    int ltVisible = 0, ltDistCulled = 0, ltPvsCulled = 0, ltFrustumCulled = 0;

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = zoneData_.zoneLights[i];

        // Distance culling (Irrlicht Y-up)
        float dx = light.position.X - camPos.X;
        float dy = light.position.Y - camPos.Y;
        float dz = light.position.Z - camPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.lightVisible[i] = 0;
            ltDistCulled++;
            continue;
        }

        // PVS check
        if (zoneData_.usePvsCulling && zoneData_.bspTree &&
            light.bspRegion != SIZE_MAX && output.currentPvsRegion != SIZE_MAX) {
            const auto& regions = zoneData_.bspTree->regions;
            if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                if (!pvs.empty() && light.bspRegion < pvs.size() && !pvs[light.bspRegion]) {
                    output.lightVisible[i] = 0;
                    ltPvsCulled++;
                    continue;
                }
            }
        }

        // Frustum culling: convert Irrlicht Y-up position to EQ Z-up
        if (input.frustumValid) {
            // Treat light as a small sphere (radius ~30 units) for frustum test
            float eqX = light.position.X;
            float eqY = light.position.Z;  // Irrlicht Z → EQ Y
            float eqZ = light.position.Y;  // Irrlicht Y → EQ Z
            float testRadius = 30.0f;
            if (!testFrustumAABB(input.frustumPlanes,
                                  eqX - testRadius, eqY - testRadius, eqZ - testRadius,
                                  eqX + testRadius, eqY + testRadius, eqZ + testRadius)) {
                output.lightVisible[i] = 0;
                ltFrustumCulled++;
                continue;
            }
        }

        output.lightVisible[i] = 1;
        ltVisible++;
    }

    LOG_DEBUG(MOD_GRAPHICS, "ZoneLightVis: {}/{} visible (dist={}, pvs={}, frustum={})",
              ltVisible, lightCount, ltDistCulled, ltPvsCulled, ltFrustumCulled);
}

// ============================================================================
// Object Light Visibility Computation
// ============================================================================

void SimulationWorker::computeObjectLightVisibility(const SimulationInput& input, SimulationOutput& output) {
    size_t lightCount = zoneData_.objectLights.size();
    if (lightCount == 0) return;

    if (output.objectLightVisible.size() != lightCount) {
        output.objectLightVisible.resize(lightCount, 0);
    }

    float renderDistSq = input.renderDistance * input.renderDistance;
    const auto& camPos = input.cameraPos;
    int olVisible = 0, olDistCulled = 0, olPvsCulled = 0, olFrustumCulled = 0;

    for (size_t i = 0; i < lightCount; ++i) {
        const auto& light = zoneData_.objectLights[i];

        // Distance culling (Irrlicht Y-up)
        float dx = light.position.X - camPos.X;
        float dy = light.position.Y - camPos.Y;
        float dz = light.position.Z - camPos.Z;
        float distSq = dx*dx + dy*dy + dz*dz;

        if (distSq > renderDistSq) {
            output.objectLightVisible[i] = 0;
            olDistCulled++;
            continue;
        }

        // PVS check
        if (zoneData_.usePvsCulling && zoneData_.bspTree &&
            light.bspRegion != SIZE_MAX && output.currentPvsRegion != SIZE_MAX) {
            const auto& regions = zoneData_.bspTree->regions;
            if (output.currentPvsRegion < regions.size() && regions[output.currentPvsRegion]) {
                const auto& pvs = regions[output.currentPvsRegion]->visibleRegions;
                if (!pvs.empty() && light.bspRegion < pvs.size() && !pvs[light.bspRegion]) {
                    output.objectLightVisible[i] = 0;
                    olPvsCulled++;
                    continue;
                }
            }
        }

        // Frustum culling: convert Irrlicht Y-up position to EQ Z-up
        if (input.frustumValid) {
            float eqX = light.position.X;
            float eqY = light.position.Z;  // Irrlicht Z → EQ Y
            float eqZ = light.position.Y;  // Irrlicht Y → EQ Z
            float testRadius = 30.0f;
            if (!testFrustumAABB(input.frustumPlanes,
                                  eqX - testRadius, eqY - testRadius, eqZ - testRadius,
                                  eqX + testRadius, eqY + testRadius, eqZ + testRadius)) {
                output.objectLightVisible[i] = 0;
                olFrustumCulled++;
                continue;
            }
        }

        output.objectLightVisible[i] = 1;
        olVisible++;
    }

    LOG_DEBUG(MOD_GRAPHICS, "ObjLightVis: {}/{} visible (dist={}, pvs={}, frustum={})",
              olVisible, lightCount, olDistCulled, olPvsCulled, olFrustumCulled);
}

// ============================================================================
// Light Selection (Top 8)
// ============================================================================

void SimulationWorker::computeLightSelection(const SimulationInput& input, SimulationOutput& output) {
    // Reset selection
    output.activeLightCount = 0;
    for (auto& sl : output.selectedLights) {
        sl.valid = false;
    }

    struct LightCandidate {
        float distance;
        size_t index;
        bool isZoneLight;    // true = zone light, false = object light
        bool isPlayerLight;
    };
    std::vector<LightCandidate> candidates;

    // Player position in Irrlicht Y-up for distance calculations
    irr::core::vector3df playerPosIrr(input.playerX, input.playerZ, input.playerY);

    // Player light always gets slot 0 if active
    if (input.playerLightLevel > 0) {
        candidates.push_back({0.0f, 0, false, true});
    }

    // Zone lights — use horizontal distance (ignore Y)
    for (size_t i = 0; i < zoneData_.zoneLightNodes.size(); ++i) {
        // Skip lights that aren't visible
        if (i < output.lightVisible.size() && !output.lightVisible[i]) continue;

        const auto& zl = zoneData_.zoneLightNodes[i];
        float dx = zl.position.X - playerPosIrr.X;
        float dz = zl.position.Z - playerPosIrr.Z;
        float distHoriz = std::sqrt(dx*dx + dz*dz);

        // Zone light range threshold: skip if too far
        if (distHoriz > input.renderDistance) continue;

        candidates.push_back({distHoriz, i, true, false});
    }

    // Object lights
    for (size_t i = 0; i < zoneData_.objectLights.size(); ++i) {
        // Skip lights that aren't PVS-visible
        if (i < output.objectLightVisible.size() && !output.objectLightVisible[i]) continue;

        const auto& ol = zoneData_.objectLights[i];
        float dx = ol.position.X - playerPosIrr.X;
        float dy = ol.position.Y - playerPosIrr.Y;
        float dz = ol.position.Z - playerPosIrr.Z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        if (dist > input.renderDistance) continue;

        candidates.push_back({dist, i, false, false});
    }

    // Sort by distance (player light always first at distance 0)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.distance < b.distance; });

    // Take top 8
    int count = 0;
    for (const auto& c : candidates) {
        if (count >= 8) break;

        auto& sl = output.selectedLights[count];

        if (c.isPlayerLight) {
            // Player light at current player position (elevated slightly)
            sl.position = irr::core::vector3df(input.playerX, input.playerZ + 3.0f, input.playerY);
            sl.diffuseColor = input.playerLightColor;
            sl.originalColor = input.playerLightColor;
            sl.radius = input.playerLightRadius;
            sl.attConstant = input.playerLightAttConstant;
            sl.attLinear = input.playerLightAttLinear;
            sl.attQuadratic = input.playerLightAttQuadratic;
            sl.isPlayerLight = true;
            sl.isZoneLight = false;
            sl.valid = true;
            sl.sourceIndex = SIZE_MAX;
        } else if (c.isZoneLight) {
            const auto& zl = zoneData_.zoneLightNodes[c.index];
            sl.position = zl.position;
            sl.diffuseColor = zl.diffuseColor;
            sl.originalColor = zl.diffuseColor;
            sl.radius = zl.radius;
            sl.attConstant = zl.attConstant;
            sl.attLinear = zl.attLinear;
            sl.attQuadratic = zl.attQuadratic;
            sl.isZoneLight = true;
            sl.isPlayerLight = false;
            sl.valid = true;
            sl.sourceIndex = c.index;
        } else {
            const auto& ol = zoneData_.objectLights[c.index];
            sl.position = ol.position;
            sl.originalColor = ol.originalColor;
            sl.diffuseColor = ol.originalColor;  // Will be updated by fire flicker
            sl.radius = ol.radius;
            sl.attConstant = ol.attConstant;
            sl.attLinear = ol.attLinear;
            sl.attQuadratic = ol.attQuadratic;
            sl.isZoneLight = false;
            sl.isPlayerLight = false;
            sl.valid = true;
            sl.sourceIndex = c.index;
        }

        count++;
    }
    output.activeLightCount = count;
}

// ============================================================================
// Frustum Test Helper
// ============================================================================

bool SimulationWorker::testFrustumAABB(const float planes[6][4],
                                        float minX, float minY, float minZ,
                                        float maxX, float maxY, float maxZ) const {
    // For each plane, find the positive vertex (most aligned with plane normal)
    // If positive vertex is behind the plane, the AABB is entirely outside
    for (int i = 0; i < 6; ++i) {
        float nx = planes[i][0], ny = planes[i][1], nz = planes[i][2], d = planes[i][3];

        // Pick the corner of the AABB most in the direction of the plane normal
        float px = (nx >= 0) ? maxX : minX;
        float py = (ny >= 0) ? maxY : minY;
        float pz = (nz >= 0) ? maxZ : minZ;

        if (nx * px + ny * py + nz * pz + d < 0) {
            return false;  // Entirely outside this plane
        }
    }
    return true;  // At least partially inside all planes
}

// ============================================================================
// Software Occlusion Culling
// ============================================================================

void SimulationWorker::computeSoftwareOcclusion(const SimulationInput& input, SimulationOutput& output) {
    output.occlusionCulledRegions.clear();

    if (!workerOcclusionCuller_ || !input.occlusionCamera.enabled) return;
    if (output.sortedRegions.empty()) return;

    const auto& cam = input.occlusionCamera;
    workerOcclusionCuller_->setCamera(
        input.camEqX, input.camEqY, input.camEqZ,
        cam.fwdX, cam.fwdY, cam.fwdZ,
        cam.rightX, cam.rightY, cam.rightZ,  // Note: rightZ is always 0 for 2D but we pass full vector
        cam.upX, cam.upY, cam.upZ,
        cam.fovRadV, cam.aspect);
    workerOcclusionCuller_->clear();
    workerOcclusionCuller_->resetStats();

    // Rasterize occluders from N closest PVS-visible regions
    int maxOccluderRegions = workerOcclusionCuller_->getConfig().maxOccluderRegions;
    int rasterizedRegions = 0;

    for (const auto& sr : output.sortedRegions) {
        if (rasterizedRegions >= maxOccluderRegions) break;
        const auto& occluders = workerOcclusionCuller_->getRegionOccluders(sr.regionIdx);
        if (occluders.empty()) continue;
        for (const auto& tri : occluders) {
            workerOcclusionCuller_->rasterizeTriangle(tri.v0, tri.v1, tri.v2);
        }
        rasterizedRegions++;
    }

    if (rasterizedRegions == 0) return;

    // Test PVS-visible region AABBs for occlusion
    for (size_t i = 0; i < zoneData_.regionBounds.size(); ++i) {
        if (output.regionVisible[i] == 0) continue;  // Already culled by PVS/frustum
        const auto& rb = zoneData_.regionBounds[i];
        if (workerOcclusionCuller_->testAABB(rb.minX, rb.minY, rb.minZ,
                                              rb.maxX, rb.maxY, rb.maxZ)) {
            output.occlusionCulledRegions.insert(rb.regionIdx);
            output.regionVisible[i] = 0;
        }
    }

    // Refine object visibility — mark objects in occluded regions as hidden
    for (size_t i = 0; i < zoneData_.objects.size(); ++i) {
        if (output.objectVisible[i] == 0) continue;
        const auto& obj = zoneData_.objects[i];
        if (obj.bspRegion != SIZE_MAX &&
            output.occlusionCulledRegions.count(obj.bspRegion)) {
            output.objectVisible[i] = 0;
        }
    }

    // Refine light visibility — mark lights in occluded regions as hidden
    for (size_t i = 0; i < zoneData_.zoneLights.size(); ++i) {
        if (output.lightVisible[i] == 0) continue;
        const auto& zl = zoneData_.zoneLights[i];
        if (zl.bspRegion != SIZE_MAX &&
            output.occlusionCulledRegions.count(zl.bspRegion)) {
            output.lightVisible[i] = 0;
        }
    }
}

} // namespace Graphics
} // namespace EQT
