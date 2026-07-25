#pragma once

// P08: draw-composition histogram report (--draw-histogram, one-shot
// --dump-style diagnostic gated to designated frames, NOT per-frame -- see
// refactor/P08_draw_call_investigation.md design point 3). Kept in its own
// TU (rather than added to gles2_zone_benchmark.cpp, already WARN-listed by
// scripts/refactor/check_file_sizes.sh) so the report/formatting code does
// not push that file toward the FAIL threshold.

#include <cstdio>
#include <string>
#include <vector>

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

} // namespace ZoneBench
