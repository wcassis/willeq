#pragma once

// P08: draw-composition histogram report (--draw-histogram, one-shot
// --dump-style diagnostic gated to designated frames, NOT per-frame -- see
// refactor/P08_draw_call_investigation.md design point 3). Kept in its own
// TU (rather than added to gles2_zone_benchmark.cpp, already WARN-listed by
// scripts/refactor/check_file_sizes.sh) so the report/formatting code does
// not push that file toward the FAIL threshold. Also houses
// computeOcclusionHeadroomUncapped() (design point 6) -- moved here from
// zone_bench_loader.cpp when that TU crossed the 3000-line FAIL threshold;
// it only needs ZoneBenchData's public members (declared in
// zone_bench_loader.h, included below), no access to that TU's internals.

#include <cstdio>
#include <string>
#include <vector>

#include "zone_bench_loader.h"

namespace ZoneBench {

// Draws-per-region and polys-per-draw distributions for one measured frame,
// captured from collectDrawEntries()'s optional outDrawsPerRegion/
// outPolysPerDraw params (region-visitation order, i.e. before the
// material-key sort reorders draw entries).
struct DrawHistogram {
    int frame = 0;
    int regionsVisible = 0;
    int regionsDrawn = 0;
    int regionsOccluded = 0;
    int stateChanges = 0;
    std::vector<int> drawsPerRegion;  // one entry per region that contributed >=1 draw
    std::vector<int> polysPerDraw;    // one entry per draw entry (this frame's zone draws)
};

// Appends one summary row (CSV) to `out` -- draws-per-region min/mean/p50/
// p95/max, polys-per-draw min/mean/p50/p95/max, a coarse poly-count bucket
// histogram, and the tiny-draw (<32 polys) count/fraction. Writes a header
// line first if `writeHeader` is true (caller tracks first-call).
void writeDrawHistogramCSV(FILE* out, const DrawHistogram& h, bool writeHeader);

// Human-readable version of the same data for stdout (--dump companion).
void printDrawHistogram(const DrawHistogram& h);

// P08 design point 6: occlusion headroom ground-truth diagnostic (one-shot,
// call at 2-3 pinned poses only -- NOT per-frame, unbounded occluder count
// makes this more expensive than the production culler). Re-runs PVS +
// frustum culling, then occlusion-tests every visible region against ALL
// nearer regions' occluder triangles (no maxOccluderRegions cap, unlike
// production's collectDrawEntries()/the real occlusion pass) to estimate how
// many additional regions a bigger occluder budget could catch beyond what
// the production-capped pass already finds. Returns counts only -- does not
// touch any DrawEntry vector.
struct OcclusionHeadroom {
    int regionsVisiblePVS = 0;       // after PVS + frustum cull, before any occlusion test
    int regionsOccludedCapped = 0;   // what collectDrawEntries()'s production pass found
                                      // (maxOccluderRegions-capped occluder set)
    int regionsOccludedUncapped = 0; // this function's uncapped-occluder-budget result
};
OcclusionHeadroom computeOcclusionHeadroomUncapped(
    ZoneBenchData& data,
    const CameraState& cam,
    bool pvsCull,
    bool frustumCull,
    int regionsOccludedCapped);

} // namespace ZoneBench
