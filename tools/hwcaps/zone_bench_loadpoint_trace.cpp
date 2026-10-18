// B928 (issue #246): the per-point trace instrument for the gate's benchmark.
// See zone_bench_loadpoint_trace.h for the contract.
//
// The T504 instrument (memory_ledger_trace.cpp) parses WILLEQ_LEDGER_TRACE at
// process start (initTraceFromEnv) but ARMS it only when recordPoint() reaches
// the env's arm point -- and the benchmark's [memledger] lines are printed
// through snapshot() (printLedgerPoint), never recordPoint(). So on the
// benchmark's path the instrument would stay disarmed forever: g_traceArmed
// is false and not one allocation would be traced. loadPointTraceConfigure()
// arms the SAME category NOW, before the benchmark's first ledger point,
// reusing the env's own category match so the off-state (variable unset)
// stays byte-for-byte unchanged; loadPointTracePrint() is traceReport() --
// the same [memledger-trace] lines the client's recordPoint() prints.

#include "zone_bench_loadpoint_trace.h"

#include "client/core/memory_ledger.h"
#include "client/core/memory_ledger_trace.h"  // T504 internal seam: traceArmAt

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ZoneBench {
namespace b928 {

namespace {
// WILLEQ_LEDGER_TRACE=<category>[:<point>] names the instrument's category;
// the env constructor (initTraceFromEnv) has already parsed it into the
// instrument. Same variable, same kCategoryNames match (through the public
// categoryFromName), so this is true exactly when the instrument's category
// is set -- and for nothing else.
bool traceCategoryFromEnv(eqt::memory::Category& cat) {
    const char* want = std::getenv("WILLEQ_LEDGER_TRACE");
    if (!want || !*want) return false;
    char name[64];
    std::strncpy(name, want, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    if (char* colon = std::strchr(name, ':')) *colon = 0;  // drop the arm point
    return eqt::memory::categoryFromName(name, cat);
}
}  // namespace

void loadPointTraceConfigure() {
    eqt::memory::Category cat;
    if (!traceCategoryFromEnv(cat)) return;  // off: no change to any gate run
    // Arm NOW, before the first ledger point. recordPoint() -- the only other
    // arming site -- is never called on the benchmark's path, so without this
    // the instrument's armed flag stays false for the whole run. traceConfigure
    // resets the armed flag AND the arm point; arming at that same point
    // immediately then means every allocation from here on is traced. The
    // env's own arm point (a client lifecycle name the benchmark never
    // records) is irrelevant: arming before "startup" satisfies its meaning --
    // trace from the start -- and the point's live/peak split in the report
    // names the transient owners the high-water mark alone cannot.
    eqt::memory::traceConfigure(cat, "b928");
    eqt::memory::trace_detail::traceArmAt("b928");
}

void loadPointTracePrint(const char* point) {
    const std::string lines = eqt::memory::traceReport(point);
    if (lines.empty()) return;
    std::printf("%s\n", lines.c_str());
    std::fflush(stdout);
}

}  // namespace b928
} // namespace ZoneBench
