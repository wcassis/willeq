#include "zone_bench_histogram.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "client/graphics/frustum_culler.h"
#include "client/graphics/software_occlusion_culler.h"
#include "client/zone_bsp.h"

namespace ZoneBench {

namespace {

constexpr int kTinyDrawPolyThreshold = 32;

struct DistStats {
    int mn = 0, mx = 0;
    double mean = 0;
    int p50 = 0, p95 = 0;
};

DistStats computeStats(std::vector<int> values) {
    DistStats s;
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    s.mn = values.front();
    s.mx = values.back();
    s.mean = std::accumulate(values.begin(), values.end(), 0.0) /
             static_cast<double>(values.size());
    size_t p50idx = values.size() / 2;
    size_t p95idx = static_cast<size_t>(values.size() * 0.95);
    if (p95idx >= values.size()) p95idx = values.size() - 1;
    s.p50 = values[p50idx];
    s.p95 = values[p95idx];
    return s;
}

// Coarse poly-count buckets: <8, 8-15, 16-31, 32-63, 64-127, 128+.
struct PolyBuckets {
    int b0_7 = 0, b8_15 = 0, b16_31 = 0, b32_63 = 0, b64_127 = 0, b128plus = 0;
};

PolyBuckets bucketPolys(const std::vector<int>& polysPerDraw) {
    PolyBuckets b;
    for (int p : polysPerDraw) {
        if (p < 8) b.b0_7++;
        else if (p < 16) b.b8_15++;
        else if (p < 32) b.b16_31++;
        else if (p < 64) b.b32_63++;
        else if (p < 128) b.b64_127++;
        else b.b128plus++;
    }
    return b;
}

int countTinyDraws(const std::vector<int>& polysPerDraw) {
    int n = 0;
    for (int p : polysPerDraw) {
        if (p < kTinyDrawPolyThreshold) n++;
    }
    return n;
}

} // namespace

void writeDrawHistogramCSV(FILE* out, const DrawHistogram& h, bool writeHeader) {
    if (!out) return;

    if (writeHeader) {
        fprintf(out,
            "frame,regions_vis,regions_drawn,regions_occluded,state_changes,"
            "draws_total,draws_per_region_min,draws_per_region_mean,"
            "draws_per_region_p50,draws_per_region_p95,draws_per_region_max,"
            "polys_per_draw_min,polys_per_draw_mean,polys_per_draw_p50,"
            "polys_per_draw_p95,polys_per_draw_max,"
            "bucket_0_7,bucket_8_15,bucket_16_31,bucket_32_63,bucket_64_127,"
            "bucket_128plus,tiny_draws_lt32,tiny_draw_frac\n");
    }

    DistStats dpr = computeStats(h.drawsPerRegion);
    DistStats ppd = computeStats(h.polysPerDraw);
    PolyBuckets buckets = bucketPolys(h.polysPerDraw);
    int tinyDraws = countTinyDraws(h.polysPerDraw);
    int totalDraws = static_cast<int>(h.polysPerDraw.size());
    double tinyFrac = totalDraws > 0 ?
        static_cast<double>(tinyDraws) / totalDraws : 0.0;

    fprintf(out,
        "%d,%d,%d,%d,%d,%d,%d,%.2f,%d,%d,%d,%d,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.4f\n",
        h.frame, h.regionsVisible, h.regionsDrawn, h.regionsOccluded, h.stateChanges,
        totalDraws, dpr.mn, dpr.mean, dpr.p50, dpr.p95, dpr.mx,
        ppd.mn, ppd.mean, ppd.p50, ppd.p95, ppd.mx,
        buckets.b0_7, buckets.b8_15, buckets.b16_31, buckets.b32_63,
        buckets.b64_127, buckets.b128plus, tinyDraws, tinyFrac);
}

void printDrawHistogram(const DrawHistogram& h) {
    DistStats dpr = computeStats(h.drawsPerRegion);
    DistStats ppd = computeStats(h.polysPerDraw);
    PolyBuckets buckets = bucketPolys(h.polysPerDraw);
    int tinyDraws = countTinyDraws(h.polysPerDraw);
    int totalDraws = static_cast<int>(h.polysPerDraw.size());
    double tinyFrac = totalDraws > 0 ?
        static_cast<double>(tinyDraws) / totalDraws : 0.0;

    printf("\n[draw-histogram] frame=%d regions vis=%d drawn=%d occ=%d state_changes=%d\n",
           h.frame, h.regionsVisible, h.regionsDrawn, h.regionsOccluded, h.stateChanges);
    printf("  draws/region:  min=%-4d mean=%-6.2f p50=%-4d p95=%-4d max=%-4d (n=%zu regions)\n",
           dpr.mn, dpr.mean, dpr.p50, dpr.p95, dpr.mx, h.drawsPerRegion.size());
    printf("  polys/draw:    min=%-4d mean=%-6.2f p50=%-4d p95=%-4d max=%-4d (n=%d draws)\n",
           ppd.mn, ppd.mean, ppd.p50, ppd.p95, ppd.mx, totalDraws);
    printf("  poly buckets:  [0-7]=%d [8-15]=%d [16-31]=%d [32-63]=%d [64-127]=%d [128+]=%d\n",
           buckets.b0_7, buckets.b8_15, buckets.b16_31, buckets.b32_63,
           buckets.b64_127, buckets.b128plus);
    printf("  tiny draws (<%d polys): %d / %d (%.1f%%)\n",
           kTinyDrawPolyThreshold, tinyDraws, totalDraws, tinyFrac * 100.0);
}

OcclusionHeadroom computeOcclusionHeadroomUncapped(
    ZoneBenchData& data,
    const CameraState& cam,
    bool pvsCull,
    bool frustumCull,
    int regionsOccludedCapped)
{
    OcclusionHeadroom result;
    result.regionsOccludedCapped = regionsOccludedCapped;

    if (!data.occlusionCuller || !data.occlusionCuller->hasOccluders()) {
        return result;
    }

    float camEqX = cam.eqX, camEqY = cam.eqY, camEqZ = cam.eqZ;
    size_t camRegion = data.bspTree->findRegionIndexForPoint(camEqX, camEqY, camEqZ);

    struct RegionEntry { size_t idx; float distSq; };
    std::vector<RegionEntry> visibleRegions;

    for (auto& [regionIdx, rd] : data.regions) {
        if (pvsCull && camRegion != SIZE_MAX && data.bspTree) {
            auto& camReg = data.bspTree->regions[camRegion];
            if (camReg && regionIdx < camReg->pvsRegionCount() &&
                !camReg->pvs()[regionIdx]) {
                continue;
            }
        }
        if (frustumCull && data.frustumCuller && data.frustumCuller->isEnabled()) {
            if (!data.frustumCuller->testAABB(
                    rd.eqMinX, rd.eqMinY, rd.eqMinZ,
                    rd.eqMaxX, rd.eqMaxY, rd.eqMaxZ)) {
                continue;
            }
        }
        float dx = std::max(0.0f, std::max(rd.eqMinX - camEqX, camEqX - rd.eqMaxX));
        float dy = std::max(0.0f, std::max(rd.eqMinY - camEqY, camEqY - rd.eqMaxY));
        float dz = std::max(0.0f, std::max(rd.eqMinZ - camEqZ, camEqZ - rd.eqMaxZ));
        float distSq = dx * dx + dy * dy + dz * dz;
        visibleRegions.push_back({regionIdx, distSq});
    }
    result.regionsVisiblePVS = static_cast<int>(visibleRegions.size());

    std::sort(visibleRegions.begin(), visibleRegions.end(),
              [](const RegionEntry& a, const RegionEntry& b) {
                  return a.distSq < b.distSq;
              });

    // Compute camera basis (same math as collectDrawEntries' occlusion block).
    float fwdLen = std::sqrt(cam.dirX * cam.dirX + cam.dirY * cam.dirY + cam.dirZ * cam.dirZ);
    float fX = cam.dirX / fwdLen, fY = cam.dirY / fwdLen, fZ = cam.dirZ / fwdLen;
    float rX = fY, rY = -fX, rZ = 0.0f;
    float rLen = std::sqrt(rX * rX + rY * rY);
    if (rLen > 0.001f) { rX /= rLen; rY /= rLen; }
    float uX = rY * fZ - rZ * fY;
    float uY = rZ * fX - rX * fZ;
    float uZ = rX * fY - rY * fX;

    data.occlusionCuller->setCamera(camEqX, camEqY, camEqZ,
                                     fX, fY, fZ, rX, rY, rZ, uX, uY, uZ,
                                     cam.fovRadV, cam.aspect);
    data.occlusionCuller->clear();
    data.occlusionCuller->resetStats();

    // Uncapped: every region is tested against the depth buffer accumulated
    // from ALL nearer regions' occluders (no maxOccluderRegions ceiling),
    // then (if not occluded) rasterizes its OWN occluders for farther
    // regions -- same nearest-first incremental semantics as production,
    // just without the region-count cap on the occluder set.
    for (const auto& re : visibleRegions) {
        auto& rd = data.regions[re.idx];
        bool occluded = data.occlusionCuller->testAABB(
            rd.eqMinX, rd.eqMinY, rd.eqMinZ, rd.eqMaxX, rd.eqMaxY, rd.eqMaxZ);
        if (occluded) {
            result.regionsOccludedUncapped++;
            continue;  // fully occluded regions don't contribute new occluders either
        }
        const auto& occluders = data.occlusionCuller->getRegionOccluders(re.idx);
        for (const auto& tri : occluders) {
            data.occlusionCuller->rasterizeTriangle(tri.v0, tri.v1, tri.v2);
        }
    }

    return result;
}

} // namespace ZoneBench
