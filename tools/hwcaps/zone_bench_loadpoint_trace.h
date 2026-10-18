// B928 (issue #246): the per-point trace instrument for the gate's benchmark.
//
// The benchmark already prints a [memledger] line at every load point
// (zone_bench_loader.cpp's ledgerPoint), but a category's `peak` field on
// those lines is a running high-water mark: it names a number, not its
// owner. WILLEQ_LEDGER_TRACE (memory_ledger_trace.cpp) does name the owners,
// but it is a manual env switch and the benchmark never prints
// traceReport() at its points, so the two could not be joined.
//
// This is the join. With WILLEQ_LEDGER_TRACE=<category> set the way T504's
// instrument already reads it, loadPointTraceConfigure() turns the
// category+arm point from an env-parse into an explicit configure call at
// process start (arm point "startup", the benchmark's first ledger point,
// so every load-point allocation is seen) and prints the category's
// [memledger-trace] block at every ledger point alongside the existing
// [memledger] line. OFF by default: without the variable it configures no
// category (the instrument's 0xFF "off" state) and prints nothing, so a
// gate run's stdout is byte-for-byte what the gate reads. The gate does not
// set the variable (grep scripts/ci/), so this changes no gate input.

#pragma once
#include <string>

namespace ZoneBench {
namespace b928 {

// At process start, before the first ledger point: arm WILLEQ_LEDGER_TRACE's
// category (if set) at "startup". No-op when the variable is unset.
void loadPointTraceConfigure();

// At each ledger point, after printLedgerPoint(): the category's
// [memledger-trace] block (live and per-site high-water, sorted as
// WILLEQ_LEDGER_TRACE_SORT says), or nothing when the instrument is off.
void loadPointTracePrint(const char* point);

} // namespace b928
} // namespace ZoneBench
