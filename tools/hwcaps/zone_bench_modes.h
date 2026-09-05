#pragma once
//
// zone_bench_modes.h — gles2_zone_benchmark's four early-return diagnostic
// modes.
//
// T387: main() was 1,713 lines of tools/hwcaps/gles2_zone_benchmark.cpp (3,060
// lines — over the 1,500 ceiling M00 enforces tree-wide; board B167). Four of
// its blocks are self-contained modes that render, print and exit without ever
// reaching the measured benchmark loop, so each becomes one function here and
// main() forwards to it. Bodies are verbatim, only dedented; each keeps its
// own `return 0` / `return 1`, which is now the function's result and main's.
//
// `data` is taken as the owning unique_ptr rather than a reference to the
// object so that every `data->` in the moved bodies stays character-identical.

#include <memory>

#include "zone_bench_loader.h"
#include "zone_bench_metrics.h"
#include "zone_bench_options.h"

int runTreeTest(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts);
int runCompositeEntityTest(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts);
int runPoseSearch(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts);
int runStaticPoseCapture(std::unique_ptr<ZoneBench::ZoneBenchData>& data, const Options& opts,
                         ZoneBench::MetricsCollector& metrics);
