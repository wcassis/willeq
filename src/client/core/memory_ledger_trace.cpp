// T504: the memory ledger's stack-trace instrument (WILLEQ_LEDGER_TRACE).
// Split out of memory_ledger.cpp (file-size ceiling); the hooks and
// recordPoint() reach it through memory_ledger_trace.h.
#include "client/core/memory_ledger.h"
#include "memory_ledger_trace.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <execinfo.h>
#include <dlfcn.h>

#include "common/logging.h"

namespace eqt {
namespace memory {
namespace {

// ---------------------------------------------------------------------------
// T504 stack-trace instrument. WILLEQ_LEDGER_TRACE=<category>[:<arm point>]
// (arm point defaults to "ready") makes every allocation charged to that
// category AFTER the arm point capture a short backtrace; the block header's
// top byte carries the call-site id so a free credits the same site. Each
// lifecycle point then prints the sites by live bytes as
// [memledger-trace] lines with module-relative frame addresses
// (module+0xoff, resolvable offline with addr2line -e <module> -f -C). It
// exists to name what a catch-all row such as irrlicht_scene (the render
// thread's default category) is made of; it is a diagnostic, off by default,
// and costs one backtrace() per traced allocation when on.
// ---------------------------------------------------------------------------
constexpr int kTraceFrames = 14;
constexpr int kTraceSites = 254;          // ids 1..254 distinct; 0 = untraced
constexpr int kTraceOverflow = 255;       // every site the table had no room for
int g_traceDepth = kTraceFrames;          // WILLEQ_LEDGER_TRACE_DEPTH: frames hashed
struct TraceSite {
    std::atomic<uint64_t> hash{0};
    void* frames[kTraceFrames] = {};
    int n = 0;
    std::atomic<int64_t> live{0};
    std::atomic<int64_t> peak{0};         // high-water mark of live (transient owners)
    std::atomic<int64_t> liveBlocks{0};
    std::atomic<uint64_t> allocs{0};
};
TraceSite g_traceSites[kTraceOverflow + 1];
std::atomic<int> g_traceUsed{0};
std::atomic<bool> g_traceArmed{false};
std::atomic<uint64_t> g_traceDropped{0};   // armed but table full / reentrant
std::atomic<uint64_t> g_traceRecycled{0};  // dead sites recycled for new stacks
uint8_t g_traceCat = 0xFF;
char g_traceArmPoint[32] = "ready";
thread_local bool t_inTrace = false;
std::mutex g_traceMutex;

__attribute__((constructor)) void initTraceFromEnv() {
    const char* want = std::getenv("WILLEQ_LEDGER_TRACE");
    if (!want || !*want) return;
    char cat[64];
    std::strncpy(cat, want, sizeof(cat) - 1);
    cat[sizeof(cat) - 1] = 0;
    if (char* colon = std::strchr(cat, ':')) {
        *colon = 0;
        std::strncpy(g_traceArmPoint, colon + 1, sizeof(g_traceArmPoint) - 1);
        g_traceArmPoint[sizeof(g_traceArmPoint) - 1] = 0;
    }
    if (const char* d = std::getenv("WILLEQ_LEDGER_TRACE_DEPTH")) {
        const int v = std::atoi(d);
        if (v >= 4 && v <= kTraceFrames) g_traceDepth = v;
    }
    for (size_t i = 0; i < kCategoryCount; ++i) {
        if (std::strcmp(cat, kCategoryNames[i]) == 0) {
            g_traceCat = static_cast<uint8_t>(i);
            return;
        }
    }
}


// Called from the alloc hook. Returns the site id (0 = not traced).
std::string traceFrame(void* pc) {
    Dl_info info;
    char buf[160];
    if (dladdr(pc, &info) && info.dli_fname) {
        const char* base = std::strrchr(info.dli_fname, '/');
        base = base ? base + 1 : info.dli_fname;
        std::snprintf(buf, sizeof(buf), "%s+0x%lx", base,
                      static_cast<unsigned long>(reinterpret_cast<uintptr_t>(pc)
                                                 - reinterpret_cast<uintptr_t>(info.dli_fbase)));
    } else {
        std::snprintf(buf, sizeof(buf), "?+0x%lx", static_cast<unsigned long>(reinterpret_cast<uintptr_t>(pc)));
    }
    return buf;
}


}  // namespace

namespace trace_detail {

uint8_t traceSiteFor(uint8_t cat, uint64_t bytes) {
    if (cat != g_traceCat || !g_traceArmed.load(std::memory_order_relaxed)) return 0;
    if (t_inTrace) { g_traceDropped.fetch_add(1, std::memory_order_relaxed); return 0; }
    t_inTrace = true;
    void* frames[kTraceFrames];
    const int n = backtrace(frames, g_traceDepth);
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; ++i) {
        h ^= reinterpret_cast<uintptr_t>(frames[i]);
        h *= 1099511628211ull;
    }
    if (h == 0) h = 1;
    // Linear probe over the used prefix; insert under the mutex.
    int used = g_traceUsed.load(std::memory_order_acquire);
    int id = 0;
    for (int i = 1; i <= used; ++i) {
        if (g_traceSites[i].hash.load(std::memory_order_acquire) == h) { id = i; break; }
    }
    if (id == 0) {
        std::lock_guard<std::mutex> lock(g_traceMutex);
        used = g_traceUsed.load(std::memory_order_acquire);
        for (int i = 1; i <= used; ++i) {
            if (g_traceSites[i].hash.load(std::memory_order_acquire) == h) { id = i; break; }
        }
        if (id == 0 && used < kTraceSites) {
            id = used + 1;
            TraceSite& t = g_traceSites[id];
            std::memcpy(t.frames, frames, sizeof(void*) * static_cast<size_t>(n));
            t.n = n;
            t.hash.store(h, std::memory_order_release);
            g_traceUsed.store(id, std::memory_order_release);
        } else if (id == 0) {
            // Table full: recycle a DEAD site (no live blocks, so no header
            // still carries its id) with the smallest peak. Transient churn
            // sites (regex temporaries, per-frame strings) then stop
            // starving the table of the long-lived allocations it exists to
            // name; the recycled site's history is lost, which is what the
            // OVERFLOW bucket would have shown for the newcomer anyway.
            int victim = 0;
            int64_t victimPeak = 0;
            for (int i = 1; i <= used; ++i) {
                TraceSite& c = g_traceSites[i];
                if (c.liveBlocks.load(std::memory_order_relaxed) != 0) continue;
                const int64_t pk = c.peak.load(std::memory_order_relaxed);
                if (victim == 0 || pk < victimPeak) { victim = i; victimPeak = pk; }
            }
            if (victim != 0) {
                id = victim;
                TraceSite& t = g_traceSites[id];
                std::memcpy(t.frames, frames, sizeof(void*) * static_cast<size_t>(n));
                t.n = n;
                t.live.store(0, std::memory_order_relaxed);
                t.peak.store(0, std::memory_order_relaxed);
                t.allocs.store(0, std::memory_order_relaxed);
                t.hash.store(h, std::memory_order_release);
                g_traceRecycled.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    t_inTrace = false;
    if (id == 0) { g_traceDropped.fetch_add(1, std::memory_order_relaxed); id = kTraceOverflow; }
    TraceSite& t = g_traceSites[id];
    const int64_t nowLive = t.live.fetch_add(static_cast<int64_t>(bytes), std::memory_order_relaxed)
                            + static_cast<int64_t>(bytes);
    int64_t pk = t.peak.load(std::memory_order_relaxed);
    while (nowLive > pk && !t.peak.compare_exchange_weak(pk, nowLive, std::memory_order_relaxed)) {}
    t.liveBlocks.fetch_add(1, std::memory_order_relaxed);
    t.allocs.fetch_add(1, std::memory_order_relaxed);
    return static_cast<uint8_t>(id);
}

void traceCredit(uint32_t catWord, uint64_t bytes) {
    const uint8_t id = traceIdOf(catWord);
    if (!id) return;
    g_traceSites[id].live.fetch_sub(static_cast<int64_t>(bytes), std::memory_order_relaxed);
    g_traceSites[id].liveBlocks.fetch_sub(1, std::memory_order_relaxed);
}
void traceRecharge(uint32_t catWord, uint64_t bytes) {
    const uint8_t id = traceIdOf(catWord);
    if (!id) return;
    TraceSite& t = g_traceSites[id];
    const int64_t nowLive = t.live.fetch_add(static_cast<int64_t>(bytes), std::memory_order_relaxed)
                            + static_cast<int64_t>(bytes);
    int64_t pk = t.peak.load(std::memory_order_relaxed);
    while (nowLive > pk && !t.peak.compare_exchange_weak(pk, nowLive, std::memory_order_relaxed)) {}
    t.liveBlocks.fetch_add(1, std::memory_order_relaxed);
}

void traceArmAt(const std::string& point) {
    if (g_traceCat == 0xFF || g_traceArmed.load(std::memory_order_relaxed)) return;
    if (point == g_traceArmPoint) g_traceArmed.store(true, std::memory_order_release);
}

std::string traceToLogLines(const std::string& point) {
    if (g_traceCat == 0xFF || !g_traceArmed.load(std::memory_order_relaxed)) return std::string();
    const int used = g_traceUsed.load(std::memory_order_acquire);
    std::vector<int> ids;
    for (int i = 1; i <= used; ++i) ids.push_back(i);
    ids.push_back(kTraceOverflow);
    // WILLEQ_LEDGER_TRACE_SORT=peak orders by each site's high-water mark
    // (names the transient owners); default is live bytes.
    static const bool byPeak = [] {
        const char* v = std::getenv("WILLEQ_LEDGER_TRACE_SORT");
        return v && std::strcmp(v, "peak") == 0;
    }();
    std::sort(ids.begin(), ids.end(), [](int a, int b) {
        return byPeak ? g_traceSites[a].peak.load() > g_traceSites[b].peak.load()
                      : g_traceSites[a].live.load() > g_traceSites[b].live.load();
    });
    int64_t total = 0;
    for (int i = 1; i <= used; ++i) total += g_traceSites[i].live.load();
    total += g_traceSites[kTraceOverflow].live.load();
    std::string out = "[memledger-trace] point=" + point + " category=" + kCategoryNames[g_traceCat]
                    + " sites=" + std::to_string(used) + " live=" + std::to_string(total)
                    + " dropped=" + std::to_string(g_traceDropped.load())
                    + " recycled=" + std::to_string(g_traceRecycled.load());
    int shown = 0;
    for (int id : ids) {
        const TraceSite& t = g_traceSites[id];
        const int64_t live = t.live.load();
        const int64_t peak = t.peak.load();
        if ((byPeak ? peak : live) < 16384 && shown >= 8) break;   // small sites: only the first few
        if (shown >= 40) break;
        ++shown;
        out += "\n[memledger-trace] point=" + point + " site=" + std::to_string(id)
             + " live=" + std::to_string(live) + " peak=" + std::to_string(peak)
             + " blocks=" + std::to_string(t.liveBlocks.load())
             + " allocs=" + std::to_string(t.allocs.load()) + " stack=";
        if (id == kTraceOverflow) { out += "OVERFLOW(table full)"; continue; }
        // frames 0..2 are backtrace()/traceSiteFor/ledgerAlloc; 3 is the
        // malloc/new entry. Start at 3 so the entry point is visible.
        for (int f = 3; f < t.n; ++f) {
            if (f > 3) out += ' ';
            out += traceFrame(t.frames[f]);
        }
    }
    return out;
}


}  // namespace trace_detail

void traceConfigure(Category cat, const std::string& armPoint) {
    g_traceCat = static_cast<uint8_t>(cat);
    std::strncpy(g_traceArmPoint, armPoint.c_str(), sizeof(g_traceArmPoint) - 1);
    g_traceArmPoint[sizeof(g_traceArmPoint) - 1] = 0;
    g_traceArmed.store(false, std::memory_order_release);
}

std::string traceReport(const std::string& point) {
    return trace_detail::traceToLogLines(point);
}


}  // namespace memory
}  // namespace eqt
