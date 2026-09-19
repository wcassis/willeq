// T491: the memory ledger -- see include/client/core/memory_ledger.h for the
// design. This TU is the ONLY place that replaces the allocator entry points;
// it must stay free of any dependency that could allocate from inside a hook
// (the hooks below touch atomics, a thread_local byte and glibc's __libc_*
// backends, nothing else).

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // B841: mallinfo2, malloc_info, malloc_trim, get_mallopt
#endif
#include "client/core/memory_ledger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <mutex>
#include <new>
#include <string>

#if defined(__linux__)
#include <malloc.h>
#include <unistd.h>
#endif

#include "common/logging.h"

namespace eqt {
namespace memory {

namespace {

struct Counters {
    std::atomic<uint64_t> live[kCategoryCount];
    std::atomic<uint64_t> peak[kCategoryCount];
    std::atomic<uint64_t> allocs[kCategoryCount];
    // T500 (Will's addendum, 2026-09-17): the bytes the CALLER asked for, as
    // against `live`, which is the whole glibc chunk -- caller bytes plus
    // glibc's size word, its 16-byte rounding, and the ledger's own 16-byte
    // LedgerHeader. The difference is per-BLOCK overhead, and it is not small:
    // T500 measured 13.07 MB of it across 824,868 entity_models allocations
    // (15.84 B each). It is real resident memory, so `live` stays the number
    // that is checked against RssAnon -- but a subsystem that tallies its own
    // logical bytes can only ever reconcile against THIS one, and 100%
    // accounting means the gap has a name instead of being a residual.
    std::atomic<uint64_t> liveRequested[kCategoryCount];
    std::atomic<uint64_t> liveBlocks[kCategoryCount];
    // B839: the startup category's sub-owners. A block charged while its
    // thread's category is Startup goes to the row its label names
    // (kStartupSublabelNone/Misc -> the catch-all row); a block charged in any
    // other category touches no row, which is why these arrays always sum to
    // the startup category's live bytes/blocks. Constant-sized, constant-
    // initialised: the hooks below may charge from the very first allocation.
    std::atomic<uint64_t> subLive[kStartupSublabelSlots];
    std::atomic<uint64_t> subBlocks[kStartupSublabelSlots];
    std::atomic<uint64_t> totalLive;
    std::atomic<uint64_t> totalPeak;
};
// Constant-initialised (all zero): usable from the very first allocation,
// before any static constructor has run.
Counters g_counters;

inline void raiseMax(std::atomic<uint64_t>& peak, uint64_t v) {
    uint64_t p = peak.load(std::memory_order_relaxed);
    while (v > p && !peak.compare_exchange_weak(p, v, std::memory_order_relaxed)) {
    }
}

// B839: the sub-owner name table. Slot 0 is unused, slot 1 is the catch-all
// (whose name, "misc", is SPOKEN by the readers, never stored); slots
// 2..kStartupSublabelSlots-1 are named sub-owners in first-open order. The
// table is plain data (fixed char buffers, no std::string, `used` a trivial
// integer initialiser, even the mutex left default) because the allocator
// hooks below can run BEFORE any dynamic initialiser has finished: a
// static-init allocation must be chargeable with the ledger at half-
// constructed state, so nothing the hook touches may allocate. The mutex
// protects the name writes, which happen once per StartupScope OPEN -- at
// most a handful of times per process -- while the charge/credit path reads
// the SLOTS lock-free.
struct Sublabels {
    std::mutex mutex;
    char names[kStartupSublabelSlots][24];  // 23 chars + NUL
    uint8_t used = kStartupSublabelMisc;    // highest slot allocated so far
};
Sublabels g_sublabels;

// Armed by recordPoint() (the first lifecycle point, "startup", is taken
// long after every static constructor has run). Until then
// startupBreakdown() reports no rows, so a pre-arming report shows the
// startup category without owner names -- the same NUMBER, the pre-B839
// view, rather than a partition of all-zero rows.
volatile bool g_sublabelsArmed = false;

inline void packSub(uint32_t& catWord, uint8_t sub) {
    catWord = static_cast<uint32_t>(catWord & 0x0000FFFFu) |
              (static_cast<uint32_t>(sub) << 16);
}
inline uint8_t subOf(uint32_t catWord) {
    return static_cast<uint8_t>(catWord >> 16);
}
inline uint8_t catOf(uint32_t catWord) {
    // The header word carries category | (sub << 16) (packSub); the
    // category enum needs only its low bits, and every array index below
    // takes the LOW half -- the packed word itself would be out of range.
    return static_cast<uint8_t>(catWord & 0x0000FFFFu);
}

inline uint8_t subSlotOf(uint8_t label) {
    // kStartupSublabelNone ("no scope open") and kStartupSublabelMisc both
    // fold into the catch-all row; anything else is already its own slot.
    return label == kStartupSublabelNone ? kStartupSublabelMisc : label;
}

// B839: the per-block label is carried in the header (see packSub above), so
// charge and credit take it as a parameter instead of re-reading the
// thread_local: a block credits the owner it was CHARGED to, whatever scope
// is open on the freeing thread.
inline void charge(uint8_t cat, uint64_t bytes, uint64_t requested, uint8_t sub) {
    const uint64_t v = g_counters.live[cat].fetch_add(bytes, std::memory_order_relaxed) + bytes;
    raiseMax(g_counters.peak[cat], v);
    g_counters.allocs[cat].fetch_add(1, std::memory_order_relaxed);
    g_counters.liveRequested[cat].fetch_add(requested, std::memory_order_relaxed);  // T500
    g_counters.liveBlocks[cat].fetch_add(1, std::memory_order_relaxed);             // T500
    // The startup category's bytes are split one level deeper, by the
    // block's own label. NO other category touches the rows -- even a named
    // label opened on a non-startup thread (a scope whose category is not
    // Startup) folds into nothing, which is what keeps the rows a partition
    // of exactly the startup category.
    if (cat == static_cast<uint8_t>(Category::Startup)) {
        const uint8_t slot = subSlotOf(sub);
        g_counters.subLive[slot].fetch_add(bytes, std::memory_order_relaxed);
        g_counters.subBlocks[slot].fetch_add(1, std::memory_order_relaxed);
    }
    const uint64_t t = g_counters.totalLive.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    raiseMax(g_counters.totalPeak, t);
}

inline void credit(uint8_t cat, uint64_t bytes, uint64_t requested, uint8_t sub) {
    g_counters.live[cat].fetch_sub(bytes, std::memory_order_relaxed);
    g_counters.liveRequested[cat].fetch_sub(requested, std::memory_order_relaxed);  // T500
    g_counters.liveBlocks[cat].fetch_sub(1, std::memory_order_relaxed);             // T500
    if (cat == static_cast<uint8_t>(Category::Startup)) {
        const uint8_t slot = subSlotOf(sub);
        g_counters.subLive[slot].fetch_sub(bytes, std::memory_order_relaxed);
        g_counters.subBlocks[slot].fetch_sub(1, std::memory_order_relaxed);
    }
    g_counters.totalLive.fetch_sub(bytes, std::memory_order_relaxed);
}

std::mutex g_zoneMutex;
std::string g_zone;
uint32_t g_seq = 0;

std::atomic<bool> g_steadyArmed{false};
std::atomic<bool> g_steadyDone{false};
std::chrono::steady_clock::time_point g_steadyDeadline;
std::mutex g_steadyMutex;

#if defined(__linux__)
void readProcStatus(Snapshot& s) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        unsigned long kb = 0;
        if (sscanf(line, "RssAnon: %lu kB", &kb) == 1) {
            s.rssAnonBytes = static_cast<uint64_t>(kb) * 1024;
        } else if (sscanf(line, "RssFile: %lu kB", &kb) == 1) {
            s.rssFileBytes = static_cast<uint64_t>(kb) * 1024;
        } else if (sscanf(line, "RssShmem: %lu kB", &kb) == 1) {
            s.rssShmemBytes = static_cast<uint64_t>(kb) * 1024;  // T500
        } else if (sscanf(line, "VmRSS: %lu kB", &kb) == 1) {
            s.vmRssBytes = static_cast<uint64_t>(kb) * 1024;
        }
    }
    fclose(f);
}
#endif

void appendKV(std::string& out, const char* key, uint64_t v) {
    out += ' ';
    out += key;
    out += '=';
    out += std::to_string(v);
}

} // namespace

CategoryStats stats(Category c) {
    CategoryStats st;
    const size_t i = static_cast<size_t>(c);
    if (i >= kCategoryCount) return st;
    st.liveBytes = g_counters.live[i].load(std::memory_order_relaxed);
    st.peakBytes = g_counters.peak[i].load(std::memory_order_relaxed);
    st.allocs = g_counters.allocs[i].load(std::memory_order_relaxed);
    // T500: the logical side of the same blocks.
    st.liveRequestedBytes = g_counters.liveRequested[i].load(std::memory_order_relaxed);
    st.liveBlocks = g_counters.liveBlocks[i].load(std::memory_order_relaxed);
    st.blockOverheadBytes = st.liveBytes > st.liveRequestedBytes
                                ? st.liveBytes - st.liveRequestedBytes : 0;
    return st;
}

// ---------------------------------------------------------------------------
// B839: the startup sub-owners.
// ---------------------------------------------------------------------------

uint8_t startupSublabelIndex(const char* name) {
    // A name already allocated gets its own slot back; a new one takes the
    // next free slot. Both paths hold the table mutex (one open per step,
    // never on a hot path).
    std::lock_guard<std::mutex> lock(g_sublabels.mutex);
    for (uint8_t s = kStartupSublabelMisc + 1; s <= g_sublabels.used; ++s) {
        if (strncmp(g_sublabels.names[s], name, 24) == 0) return s;
    }
    if (g_sublabels.used + 1 >= kStartupSublabelSlots) {
        return kStartupSublabelNone;  // table full: charge into the catch-all
    }
    ++g_sublabels.used;
    strncpy(g_sublabels.names[g_sublabels.used], name, 23);
    g_sublabels.names[g_sublabels.used][23] = '\0';
    return g_sublabels.used;
}

StartupScope::StartupScope(const char* name) : prev_(detail::t_sublabel) {
    detail::t_sublabel = startupSublabelIndex(name);
}

StartupScope::~StartupScope() { detail::t_sublabel = prev_; }

std::vector<StartupSublabelRow> startupBreakdown() {
    std::vector<StartupSublabelRow> rows;
    if (!enabled() || !g_sublabelsArmed) return rows;  // see the armed note above
    for (uint8_t s = kStartupSublabelMisc; s <= g_sublabels.used; ++s) {
        const uint64_t live = g_counters.subLive[s].load(std::memory_order_relaxed);
        const uint64_t blocks = g_counters.subBlocks[s].load(std::memory_order_relaxed);
        if (!live && !blocks) continue;
        // The catch-all's name is spoken here, not stored in the table
        // (see the Sublabels comment: no stored string, no constructor).
        const char* name = s == kStartupSublabelMisc ? "misc" : g_sublabels.names[s];
        rows.push_back(StartupSublabelRow{
            name, live, blocks});
    }
    // misc last: it is the catch-all a shrink unit has to chase, and the
    // named owners are what the table is for.
    std::stable_sort(rows.begin(), rows.end(),
                     [](const StartupSublabelRow& a, const StartupSublabelRow& b) {
                         if (a.name == "misc") return false;
                         if (b.name == "misc") return true;
                         return a.liveBytes > b.liveBytes;
                     });
    return rows;
}

std::string startupToLogLines(const std::string& point) {
    if (!enabled() || !g_sublabelsArmed) return std::string();
    const auto rows = startupBreakdown();
    // The TOTAL line is the SUM OF THE ROWS, not a second read of the
    // category counters: reading the category first and the rows after
    // would let this table's own string allocations (charged to startup.misc
    // on the client's main thread) land between the two reads and make the
    // total and the rows disagree inside one table. Taken from the rows,
    // the total equals their sum exactly by construction, and it equals the
    // category's live bytes to within this call's own churn -- which the
    // card's 1 % acceptance bound covers.
    uint64_t totalLive = 0, totalBlocks = 0;
    for (const auto& r : rows) { totalLive += r.liveBytes; totalBlocks += r.liveBlocks; }
    std::string out = "[memledger-startup] point=" + point + " owner=TOTAL";
    appendKV(out, "live", totalLive);
    appendKV(out, "blocks", totalBlocks);
    for (const auto& r : rows) {
        out += "\n[memledger-startup] point=";
        out += point;
        out += " owner=startup.";
        out += r.name;
        appendKV(out, "live", r.liveBytes);
        appendKV(out, "blocks", r.liveBlocks);
    }
    return out;
}

void resetPeaks() {
    for (size_t i = 0; i < kCategoryCount; ++i) {
        g_counters.peak[i].store(g_counters.live[i].load(std::memory_order_relaxed),
                                 std::memory_order_relaxed);
    }
    g_counters.totalPeak.store(g_counters.totalLive.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
}

void setCurrentZone(const std::string& zone) {
    std::lock_guard<std::mutex> lock(g_zoneMutex);
    g_zone = zone;
    ++g_seq;
}

std::string currentZone() {
    std::lock_guard<std::mutex> lock(g_zoneMutex);
    return g_zone;
}

Snapshot snapshot(const std::string& point, const std::string& zone) {
    Snapshot s;
    s.point = point;
    s.ledgerEnabled = enabled();
    {
        std::lock_guard<std::mutex> lock(g_zoneMutex);
        s.zone = zone.empty() ? g_zone : zone;
        s.seq = g_seq;
    }
    for (size_t i = 0; i < kCategoryCount; ++i) {
        s.categories[i] = stats(static_cast<Category>(i));
        s.ledgerTotalBytes += s.categories[i].liveBytes;
        s.allocCount += s.categories[i].allocs;
    }
    s.ledgerPeakBytes = g_counters.totalPeak.load(std::memory_order_relaxed);
#if defined(__linux__)
    readProcStatus(s);
#if defined(__GLIBC__)
    {
        struct mallinfo2 mi = mallinfo2();
        s.mallocArenaBytes = static_cast<uint64_t>(mi.arena) + static_cast<uint64_t>(mi.hblkhd);
        s.mallocInUseBytes = static_cast<uint64_t>(mi.uordblks) + static_cast<uint64_t>(mi.hblkhd);
        s.mallocSlackBytes = s.mallocArenaBytes > s.mallocInUseBytes
                                 ? s.mallocArenaBytes - s.mallocInUseBytes : 0;
    }
#endif
#endif
    if (s.rssAnonBytes > s.ledgerTotalBytes) {
        s.unattributedBytes = s.rssAnonBytes - s.ledgerTotalBytes;
    }
    // Cover RssAnon with four named buckets so nothing is hidden. See the
    // Snapshot comment for why this covers rather than partitions: glibc's
    // arena can hold non-resident free pages.
    if (s.mallocInUseBytes > s.ledgerTotalBytes) {
        s.heapUnchargedBytes = s.mallocInUseBytes - s.ledgerTotalBytes;
    }
    if (s.rssAnonBytes > s.mallocArenaBytes) {
        s.unaccountedBytes = s.rssAnonBytes - s.mallocArenaBytes;
    }
    if (s.rssAnonBytes > 0) {
        s.unattributedPct = 100.0 * static_cast<double>(s.unattributedBytes)
                            / static_cast<double>(s.rssAnonBytes);
        s.unaccountedPct = 100.0 * static_cast<double>(s.unaccountedBytes)
                           / static_cast<double>(s.rssAnonBytes);
    }
    // T491 addendum (2026-09-17): both lines, on every point.
    if (s.rssAnonBytes > s.rssCeilingBytes) s.overCeilingBytes = s.rssAnonBytes - s.rssCeilingBytes;
    if (s.rssAnonBytes > s.rssTargetBytes) s.overTargetBytes = s.rssAnonBytes - s.rssTargetBytes;
    return s;
}

std::vector<GapRow> gapToTarget(const Snapshot& s) {
    std::vector<GapRow> rows;
    rows.reserve(kCategoryCount + 2);
    auto push = [&](const char* name, uint64_t live) {
        if (!live) return;
        GapRow r;
        r.bucket = name;
        r.liveBytes = live;
        r.pctOfRssAnon = s.rssAnonBytes
                             ? 100.0 * static_cast<double>(live) / static_cast<double>(s.rssAnonBytes)
                             : 0.0;
        rows.push_back(r);
    };
    for (size_t i = 0; i < kCategoryCount; ++i) push(kCategoryNames[i], s.categories[i].liveBytes);
    push("heap_uncharged", s.heapUnchargedBytes);
    push("malloc_slack", s.mallocSlackBytes);
    push("unaccounted", s.unaccountedBytes);
    std::sort(rows.begin(), rows.end(),
              [](const GapRow& a, const GapRow& b) { return a.liveBytes > b.liveBytes; });
    return rows;
}

std::string toLogLine(const Snapshot& s) {
    std::string out = "[memledger] point=";
    out += s.point;
    out += " zone=";
    out += s.zone.empty() ? "-" : s.zone;
    appendKV(out, "seq", s.seq);
    out += " enabled=";
    out += s.ledgerEnabled ? "1" : "0";
    appendKV(out, "rss_anon", s.rssAnonBytes);
    appendKV(out, "rss_file", s.rssFileBytes);
    appendKV(out, "rss_shmem", s.rssShmemBytes);   // T500
    appendKV(out, "vm_rss", s.vmRssBytes);         // T500
    appendKV(out, "ledger", s.ledgerTotalBytes);
    appendKV(out, "ledger_peak", s.ledgerPeakBytes);
    appendKV(out, "unattributed", s.unattributedBytes);
    char pct[32];
    snprintf(pct, sizeof(pct), " unattributed_pct=%.1f", s.unattributedPct);
    out += pct;
    appendKV(out, "malloc_arena", s.mallocArenaBytes);
    appendKV(out, "malloc_inuse", s.mallocInUseBytes);
    appendKV(out, "malloc_slack", s.mallocSlackBytes);
    appendKV(out, "heap_uncharged", s.heapUnchargedBytes);
    appendKV(out, "unaccounted", s.unaccountedBytes);
    // T491 addendum: the 64 MB target and the 256 MB interim ceiling travel on
    // every line, so a cap-hunt log answers "how far past 64 MB" without a
    // second tool.
    appendKV(out, "target", s.rssTargetBytes);
    appendKV(out, "over_target", s.overTargetBytes);
    appendKV(out, "ceiling", s.rssCeilingBytes);
    appendKV(out, "over_ceiling", s.overCeilingBytes);
    appendKV(out, "allocs", s.allocCount);
    for (size_t i = 0; i < kCategoryCount; ++i) {
        out += ' ';
        out += kCategoryNames[i];
        out += '=';
        out += std::to_string(s.categories[i].liveBytes);
        out += '/';
        out += std::to_string(s.categories[i].peakBytes);
    }
    return out;
}

std::string toReportText(const Snapshot& s) {
    char buf[256];
    std::string out;
    snprintf(buf, sizeof(buf), "--- Memory Ledger [%s] zone=%s seq=%u (%s) ---\n",
             s.point.c_str(), s.zone.empty() ? "-" : s.zone.c_str(), s.seq,
             s.ledgerEnabled ? "allocator hooks ON" : "allocator hooks OFF, categories unavailable");
    out += buf;
    snprintf(buf, sizeof(buf), "  %-18s %12s %12s %10s\n", "category", "live", "peak", "allocs");
    out += buf;
    for (size_t i = 0; i < kCategoryCount; ++i) {
        const auto& c = s.categories[i];
        snprintf(buf, sizeof(buf), "  %-18s %12llu %12llu %10llu\n", kCategoryNames[i],
                 static_cast<unsigned long long>(c.liveBytes),
                 static_cast<unsigned long long>(c.peakBytes),
                 static_cast<unsigned long long>(c.allocs));
        out += buf;
    }
    snprintf(buf, sizeof(buf), "  ledger total %llu (peak %llu) | RssAnon %llu | unattributed %llu (%.1f%%)\n",
             static_cast<unsigned long long>(s.ledgerTotalBytes),
             static_cast<unsigned long long>(s.ledgerPeakBytes),
             static_cast<unsigned long long>(s.rssAnonBytes),
             static_cast<unsigned long long>(s.unattributedBytes), s.unattributedPct);
    out += buf;
    snprintf(buf, sizeof(buf), "  malloc arena %llu | in use %llu | slack %llu | RssFile %llu\n",
             static_cast<unsigned long long>(s.mallocArenaBytes),
             static_cast<unsigned long long>(s.mallocInUseBytes),
             static_cast<unsigned long long>(s.mallocSlackBytes),
             static_cast<unsigned long long>(s.rssFileBytes));
    out += buf;
    // T500 (Will's addendum): the file-backed side, which no report quoted.
    snprintf(buf, sizeof(buf), "  RssFile %llu | RssShmem %llu | VmRSS %llu\n",
             static_cast<unsigned long long>(s.rssFileBytes),
             static_cast<unsigned long long>(s.rssShmemBytes),
             static_cast<unsigned long long>(s.vmRssBytes));
    out += buf;
    snprintf(buf, sizeof(buf),
             "  RssAnon covered by: attributed %.1f MiB + glibc-internal %.1f MiB + "
             "allocator slack %.1f MiB + non-heap %.1f MiB (%.1f%%)\n",
             s.ledgerTotalBytes / 1048576.0, s.heapUnchargedBytes / 1048576.0,
             s.mallocSlackBytes / 1048576.0,
             s.unaccountedBytes / 1048576.0, s.unaccountedPct);
    out += buf;
    // T491 addendum (Will, 2026-09-17): both lines, and the gap to the target.
    snprintf(buf, sizeof(buf),
             "  RssAnon %.1f MiB vs TARGET %.0f MiB (%s%.1f MiB) vs ceiling %.0f MiB (%s%.1f MiB)\n",
             s.rssAnonBytes / 1048576.0, s.rssTargetBytes / 1048576.0,
             s.overTargetBytes ? "over by " : "under, headroom ",
             (s.overTargetBytes ? s.overTargetBytes : s.rssTargetBytes - s.rssAnonBytes) / 1048576.0,
             s.rssCeilingBytes / 1048576.0,
             s.overCeilingBytes ? "over by " : "under, headroom ",
             (s.overCeilingBytes ? s.overCeilingBytes : s.rssCeilingBytes - s.rssAnonBytes) / 1048576.0);
    out += buf;
    if (s.overTargetBytes) {
        snprintf(buf, sizeof(buf),
                 "  to reach the %.0f MiB target, shrink %.1f MiB; biggest buckets first:\n",
                 s.rssTargetBytes / 1048576.0, s.overTargetBytes / 1048576.0);
        out += buf;
        const auto rows = gapToTarget(s);
        for (size_t i = 0; i < rows.size() && i < 8; ++i) {
            snprintf(buf, sizeof(buf), "    %-18s %10.2f MiB  %5.1f%% of RssAnon\n",
                     rows[i].bucket.c_str(), rows[i].liveBytes / 1048576.0, rows[i].pctOfRssAnon);
            out += buf;
        }
    }
    return out;
}

std::string toJson(const Snapshot& s) {
    std::string out = "{";
    out += "\"point\":\"" + s.point + "\",";
    out += "\"zone\":\"" + s.zone + "\",";
    out += "\"seq\":" + std::to_string(s.seq) + ",";
    out += std::string("\"ledgerEnabled\":") + (s.ledgerEnabled ? "true" : "false") + ",";
    out += "\"categories\":{";
    for (size_t i = 0; i < kCategoryCount; ++i) {
        if (i) out += ',';
        out += '"';
        out += kCategoryNames[i];
        out += "\":{\"live\":" + std::to_string(s.categories[i].liveBytes)
             + ",\"peak\":" + std::to_string(s.categories[i].peakBytes)
             + ",\"allocs\":" + std::to_string(s.categories[i].allocs) + "}";
    }
    out += "},";
    out += "\"ledgerTotalBytes\":" + std::to_string(s.ledgerTotalBytes) + ",";
    out += "\"ledgerPeakBytes\":" + std::to_string(s.ledgerPeakBytes) + ",";
    out += "\"allocCount\":" + std::to_string(s.allocCount) + ",";
    out += "\"rssAnonBytes\":" + std::to_string(s.rssAnonBytes) + ",";
    out += "\"rssFileBytes\":" + std::to_string(s.rssFileBytes) + ",";
    out += "\"rssShmemBytes\":" + std::to_string(s.rssShmemBytes) + ",";  // T500
    out += "\"vmRssBytes\":" + std::to_string(s.vmRssBytes) + ",";
    out += "\"unattributedBytes\":" + std::to_string(s.unattributedBytes) + ",";
    char pct[32];
    snprintf(pct, sizeof(pct), "%.2f", s.unattributedPct);
    out += std::string("\"unattributedPct\":") + pct + ",";
    out += "\"mallocArenaBytes\":" + std::to_string(s.mallocArenaBytes) + ",";
    out += "\"mallocInUseBytes\":" + std::to_string(s.mallocInUseBytes) + ",";
    out += "\"mallocSlackBytes\":" + std::to_string(s.mallocSlackBytes) + ",";
    out += "\"heapUnchargedBytes\":" + std::to_string(s.heapUnchargedBytes) + ",";
    out += "\"unaccountedBytes\":" + std::to_string(s.unaccountedBytes) + ",";
    snprintf(pct, sizeof(pct), "%.2f", s.unaccountedPct);
    out += std::string("\"unaccountedPct\":") + pct + ",";
    // T491 addendum (2026-09-17): the gate prints the gap to 64 MB from these.
    out += "\"rssTargetBytes\":" + std::to_string(s.rssTargetBytes) + ",";
    out += "\"overTargetBytes\":" + std::to_string(s.overTargetBytes) + ",";
    out += "\"rssCeilingBytes\":" + std::to_string(s.rssCeilingBytes) + ",";
    out += "\"overCeilingBytes\":" + std::to_string(s.overCeilingBytes);
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// T491 addendum: /proc/self/smaps attribution of the baseline (board B801).
// ---------------------------------------------------------------------------

namespace {

// Fold a mapping's pathname into an OWNER name. The grouping is what makes the
// output readable: 40 mappings of the same .so are one row, and every unnamed
// anonymous mapping (thread stacks, driver arenas, JIT pools) is one row that
// can then be chased with the mapping count.
std::string ownerOf(const std::string& path) {
    if (path.empty()) return "[anon]";
    if (path[0] == '[') return path;                 // [heap] [stack] [vvar] ...
    const size_t slash = path.find_last_of('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    if (base.find(".so") != std::string::npos) return "so:" + base;
    return "file:" + base;
}

uint64_t kbField(const std::string& line, const char* key, size_t keyLen) {
    if (line.compare(0, keyLen, key, keyLen) != 0) return UINT64_MAX;
    const char* p = line.c_str() + keyLen;
    while (*p == ' ' || *p == '\t') ++p;
    return strtoull(p, nullptr, 10) * 1024;
}

}  // namespace

MappingBreakdown parseSmaps(const std::string& text) {
    MappingBreakdown b;
    b.available = !text.empty();
    // Insertion-ordered accumulation keyed by owner name.
    std::vector<MappingRow> rows;
    auto rowFor = [&rows](const std::string& name) -> MappingRow& {
        for (auto& r : rows) {
            if (r.name == name) return r;
        }
        rows.push_back(MappingRow{name, 0, 0, 0, 0});
        return rows.back();
    };
    std::string owner;
    size_t pos = 0;
    bool inMapping = false;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (line.empty()) {
            if (nl >= text.size()) break;
            continue;
        }
        // A header line starts with a hex address range; a field line starts
        // with a capital letter and contains ':'. Distinguish on the former.
        const size_t dash = line.find('-');
        const bool isHeader = dash != std::string::npos && dash > 0 && dash < 40 &&
                              line.find(' ') > dash &&
                              line.find_first_not_of("0123456789abcdefABCDEF") == dash;
        if (isHeader) {
            // fields: range perms offset dev inode [path]
            size_t p = 0;
            int spaces = 0;
            for (; p < line.size() && spaces < 5; ++p) {
                if (line[p] == ' ') {
                    ++spaces;
                    while (p + 1 < line.size() && line[p + 1] == ' ') ++p;
                }
            }
            const std::string path = p < line.size() ? line.substr(p) : std::string();
            owner = ownerOf(path);
            MappingRow& r = rowFor(owner);
            ++r.mappings;
            ++b.mappings;
            inMapping = true;
            continue;
        }
        if (!inMapping) continue;
        MappingRow& r = rowFor(owner);
        uint64_t v;
        if ((v = kbField(line, "Anonymous:", 10)) != UINT64_MAX) {
            r.anonBytes += v;
            b.totalAnonBytes += v;
        } else if ((v = kbField(line, "Rss:", 4)) != UINT64_MAX) {
            r.rssBytes += v;
            b.totalRssBytes += v;
        } else if ((v = kbField(line, "Size:", 5)) != UINT64_MAX) {
            r.sizeBytes += v;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const MappingRow& a, const MappingRow& c) {
        if (a.anonBytes != c.anonBytes) return a.anonBytes > c.anonBytes;
        return a.rssBytes > c.rssBytes;
    });
    b.rows = std::move(rows);
    return b;
}

MappingBreakdown readMappings() {
#if defined(__linux__)
    FILE* f = fopen("/proc/self/smaps", "r");
    if (!f) return MappingBreakdown{};
    std::string text;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    return parseSmaps(text);
#else
    return MappingBreakdown{};
#endif
}

std::string mappingsToLogLines(const std::string& point, const MappingBreakdown& b,
                               size_t topN) {
    std::string out;
    if (!b.available) {
        out = "[memmap] point=" + point + " unavailable=1";
        return out;
    }
    out = "[memmap] point=" + point + " owner=TOTAL";
    appendKV(out, "anon", b.totalAnonBytes);
    appendKV(out, "rss", b.totalRssBytes);
    appendKV(out, "maps", b.mappings);
    const size_t limit = topN ? (topN < b.rows.size() ? topN : b.rows.size()) : b.rows.size();
    for (size_t i = 0; i < limit; ++i) {
        const auto& r = b.rows[i];
        if (!r.anonBytes && !r.rssBytes) continue;
        out += "\n[memmap] point=";
        out += point;
        out += " owner=";
        out += r.name;
        appendKV(out, "anon", r.anonBytes);
        appendKV(out, "rss", r.rssBytes);
        appendKV(out, "size", r.sizeBytes);
        appendKV(out, "maps", r.mappings);
    }
    return out;
}

std::string mappingsToJson(const MappingBreakdown& b, size_t topN) {
    std::string out = "{\"available\":";
    out += b.available ? "true" : "false";
    out += ",\"totalAnonBytes\":" + std::to_string(b.totalAnonBytes);
    out += ",\"totalRssBytes\":" + std::to_string(b.totalRssBytes);
    out += ",\"mappings\":" + std::to_string(b.mappings);
    out += ",\"owners\":[";
    const size_t limit = topN ? (topN < b.rows.size() ? topN : b.rows.size()) : b.rows.size();
    for (size_t i = 0; i < limit; ++i) {
        const auto& r = b.rows[i];
        if (i) out += ',';
        out += "{\"name\":\"" + r.name + "\",\"anonBytes\":" + std::to_string(r.anonBytes)
             + ",\"rssBytes\":" + std::to_string(r.rssBytes)
             + ",\"sizeBytes\":" + std::to_string(r.sizeBytes)
             + ",\"mappings\":" + std::to_string(r.mappings) + "}";
    }
    out += "]}";
    return out;
}

MappingBreakdown recordMappings(const std::string& point, size_t topN) {
    MappingBreakdown b = readMappings();
    LOG_INFO(MOD_MAIN, "{}", mappingsToLogLines(point, b, topN));
    return b;
}

Snapshot recordPoint(const std::string& point, const std::string& zone) {
    // B839: arm the startup sub-owner rows on the FIRST lifecycle point. The
    // first one is "startup" (Application::run), taken long after every
    // static constructor, so from here on the sub rows partition the startup
    // category. Arming here (not in a static init) keeps the arming
    // constructor- and allocation-free by construction.
    g_sublabelsArmed = true;
    Snapshot s = snapshot(point, zone);
    LOG_INFO(MOD_MAIN, "{}", toLogLine(s));
    // The startup breakdown rides as a sibling of the ledger line at EVERY
    // point, so `startup`, `ready` and `steady_60s` all carry it and the
    // Pi run gets the same table the x86 run does -- the bucket that never
    // moved between them now names its owners at each of them.
    const std::string startup = startupToLogLines(point);
    if (!startup.empty()) {
        LOG_INFO(MOD_MAIN, "{}", startup);
    }
    return s;
}

// ---------------------------------------------------------------------------
// B841: the arena-retention census. The slack decomposition, named:
//   malloc_slack = arena + hblkhd - (uordblks + hblkhd)
//                = keepcost (top pad, trimmable)
//                + fordblks + fsmblks (interior free chunks, NOT trimmable)
//   plus, outside the brk arena:
//     hblkhd   = live mmapped regions (returned to the OS the instant they
//                free -- retention lives here ONLY as the gap between the
//                ledger and this number)
//     mmap_thr = the DYNAMIC mmap threshold; it only grows (double when a
//                large free fails to trim), and it decides which future frees
//                land in the free lists at all.
// The per-size-bin rows come from glibc's own malloc_info(0) XML, so they
// agree with mallinfo2 by construction (same counters, one format apart).
// ---------------------------------------------------------------------------

namespace {

struct XmlAttr {
    std::string name;
    std::string value;
};

// One <attr name="v"/> pair inside a start tag. Enough of an XML reader for
// malloc_info's flat two-level document; no namespaces, no nesting beyond
// <malloc><heap><sizes><size/>.
std::vector<XmlAttr> parseAttrs(const std::string& tag) {
    std::vector<XmlAttr> out;
    size_t pos = 1;  // skip the '<'
    // Skip the element name first: it is NOT an attribute, and folding it
    // into the first attr's name (the "size from" bug) is the one way this
    // reader can silently drop every row of a document.
    while (pos < tag.size() && !std::isspace(static_cast<unsigned char>(tag[pos]))) ++pos;
    while (pos < tag.size()) {
        while (pos < tag.size() && std::isspace(static_cast<unsigned char>(tag[pos]))) ++pos;
        if (pos >= tag.size() || tag[pos] == '/') break;
        size_t eq = tag.find('=', pos);
        if (eq == std::string::npos) break;
        std::string name = tag.substr(pos, eq - pos);
        // trim trailing spaces from the name
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty()) break;
        size_t vstart = eq + 1;
        while (vstart < tag.size() && tag[vstart] == ' ') ++vstart;
        if (vstart >= tag.size()) break;
        char quote = tag[vstart];
        if (quote != '"' && quote != '\'') break;
        size_t vend = tag.find(quote, vstart + 1);
        if (vend == std::string::npos) break;
        out.push_back({name, tag.substr(vstart + 1, vend - vstart - 1)});
        pos = vend + 1;
    }
    return out;
}

bool attr(const std::vector<XmlAttr>& a, const char* name, std::string& out) {
    for (const auto& x : a) if (x.name == name) { out = x.value; return true; }
    return false;
}

uint64_t attrU64(const std::vector<XmlAttr>& a, const char* name, uint64_t fallback = 0) {
    std::string v;
    return attr(a, name, v) ? std::stoull(v) : fallback;
}

}  // namespace

size_t arenaBinForBytes(size_t freeChunkBytes) {
    static const size_t breaks[kArenaBinCount] = {
        128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 0};
    for (size_t i = 0; i < kArenaBinCount; ++i) {
        if (breaks[i] == 0 || freeChunkBytes < breaks[i]) return i;
    }
    return kArenaBinCount - 1;
}

namespace {

// Walk one <heap> element. glibc emits the main heap (nr=0) first; the
// census is per-process either way, so every heap's size rows are summed.
void foldHeapSizes(ArenaReport& r, const std::string& xml, size_t from, size_t to) {
    size_t pos = from;
    while (pos < to) {
        size_t open = xml.find("<size", pos);
        size_t unsorted = xml.find("<unsorted", pos);
        // Closest element of either kind wins; a non-size element between
        // them would be </sizes> or </heap>, which ends the walk.
        const auto endSizes = xml.find("</sizes>", pos);
        size_t stop = std::min(open == std::string::npos ? to : open,
                               unsorted == std::string::npos ? to : unsorted);
        if (endSizes != std::string::npos && endSizes < stop) break;
        if (stop >= to) break;
        size_t tagEnd = xml.find('>', stop);
        if (tagEnd == std::string::npos || tagEnd >= to) break;
        const std::string tag = xml.substr(stop, tagEnd - stop + 1);
        // find("<size") matches the <sizes> CONTAINER as well as a <size> row
        // (both start "<size"). A real row is "<size" + space; a closing tag
        // ("</sizes>", "</heap>") ends the walk; the container is skipped.
        const bool isSizeRow = tag.compare(0, 5, "<size") == 0 && tag[5] == ' ';
        const bool isUnsortedRow = tag.compare(0, 8, "<unsorte") == 0;
        if (tag[1] == '/') break;
        if (!isSizeRow && !isUnsortedRow) { pos = tagEnd + 1; continue; }
        const auto attrs = parseAttrs(tag);
        const uint64_t total = attrU64(attrs, "total");
        const uint64_t count = attrU64(attrs, "count");
        if (isUnsortedRow) {
            // The unsorted bin: a free chunk glibc has not yet coalesced into
            // a size bin. malloc_info reports it as a RANGE row (from < to)
            // and it is already inside fordblks, so it is NOT added to any
            // size bin -- double-counting is the one thing the census must
            // not do. It shows up in the log line through fordblks.
        } else {
            const uint64_t fromB = attrU64(attrs, "from");
            // A <size> row is a RANGE: every chunk whose size is in [from,to].
            // Bin on the LOW end: a row straddling a bin edge over-reports the
            // low bin and under-reports the high one, never hides bytes --
            // `total` is the whole row and it is placed exactly once.
            const size_t bin = arenaBinForBytes(fromB);
            r.bins[bin].chunks += count;
            r.bins[bin].freeBytes += total;
        }
        pos = tagEnd + 1;
    }
}

}  // namespace

ArenaReport parseMallocInfo(const std::string& xml) {
    ArenaReport r;
    // An empty document is a failure, not a "malloc with zero free chunks".
    r.ok = xml.rfind("<malloc", 0) == 0;
    if (!r.ok) return r;
    size_t pos = 0;
    while (pos < xml.size()) {
        const size_t heap = xml.find("<heap", pos);
        if (heap == std::string::npos) break;
        // Distinguish the opening tag from the "</heap>" the next search would
        // otherwise treat as a new heap: only a non-'/' after "<heap" opens.
        if (heap + 5 < xml.size() && xml[heap + 5] == '/') break;
        size_t heapEnd = xml.find("</heap>", heap);
        if (heapEnd == std::string::npos) heapEnd = xml.size();
        foldHeapSizes(r, xml, heap, heapEnd);
        pos = heapEnd + 1;  // past the "<" of "</heap>"
    }
    return r;
}

ArenaReport arenaNow() {
    ArenaReport r;
#if defined(__linux__)
    r.mi = mallinfo2();
    r.interiorFreeBytes = r.mi.fordblks + r.mi.fsmblks;
    r.topPadBytes = r.mi.keepcost;
    r.mmapBytes = r.mi.hblkhd;
    // malloc_info(0, f) writes glibc's own per-bin free-chunk XML into any
    // stream. A per-pid temp file is the least-surprising FILE (fmemopen on a
    // growable std::string is not), and the whole census is a LIFECYCLE-POINT
    // call, so one write+read+unlink is nothing next to what recordPoint
    // already does.
    const std::string path = "/tmp/.memarena_" + std::to_string(::getpid());
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return r;
    std::string xml;
    if (malloc_info(0, f) == 0) {
        std::fclose(f);
        std::ifstream in(path);
        xml.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        const ArenaReport parsed = parseMallocInfo(xml);
        r.bins = parsed.bins;
        r.ok = parsed.ok;
    }
    std::remove(path.c_str());
#else
    (void)r;
#endif
    return r;
}

namespace {

std::string arenaToLogLine(const std::string& point, const ArenaReport& r,
                           uint64_t trimmedPages, int mmapThreshold) {
    std::string out = "[memarena] point=";
    out += point;
    out += " ok=";
    out += r.ok ? "1" : "0";
    if (r.ok) {
        appendKV(out, "ordblks", r.mi.ordblks);
        appendKV(out, "smblks", r.mi.smblks);
        appendKV(out, "fordblks", r.mi.fordblks);
        appendKV(out, "fsmblks", r.mi.fsmblks);
        appendKV(out, "keepcost", r.mi.keepcost);
        appendKV(out, "hblks", r.mi.hblks);
        appendKV(out, "hblkhd", r.mi.hblkhd);
        appendKV(out, "arena", r.mi.arena);
        appendKV(out, "uordblks", r.mi.uordblks);
        appendKV(out, "mmap_thr", static_cast<uint64_t>(mmapThreshold));
        appendKV(out, "trim_pages", trimmedPages);
        for (size_t i = 0; i < kArenaBinCount; ++i) {
            out += ' ';
            out += kArenaBinNames[i];
            out += "=";
            out += std::to_string(r.bins[i].chunks);
            out += '/';
            out += std::to_string(r.bins[i].freeBytes);
        }
    }
    return out;
}

}  // namespace

ArenaReport recordArena(const std::string& point, bool trim) {
    ArenaReport r;
#if defined(__linux__)
    uint64_t trimmedPages = 0;
    if (trim) {
        // malloc_trim(0) returns the number of 4 KiB pages it released from
        // the top of the heap (0 when the top pad is below the threshold).
        // Called BEFORE the census: the census then shows what the trim left.
        const int pages = malloc_trim(0);
        trimmedPages = pages > 0 ? static_cast<uint64_t>(pages) : 0;
    }
    // The DYNAMIC M_MMAP_THRESHOLD. glibc has NO way to read it back:
    // mallopt(opt, 0) SETS the option (it has no query form), mallinfo2 has
    // no field for it, and get_mallopt() is glibc >= 2.40 (this host is
    // 2.39). When the B841_MMAP_THRESHOLD experiment is on the value IS
    // known to the caller (it set it at startup), so the census carries it;
    // otherwise it is reported as 0 (unknown) and DERIVED in the analysis:
    // it starts at 128 KiB and doubles whenever a large free cannot trim the
    // top down to the trim threshold, so the census's ge64k-bin population
    // plus the keepcost growth between two points is the evidence.
    const int mmapThreshold =
        arenaExperiments().mmapThresholdBytes > 0 ? arenaExperiments().mmapThresholdBytes : 0;
    r = arenaNow();
    LOG_INFO(MOD_MAIN, "{}", arenaToLogLine(point, r, trimmedPages, mmapThreshold));
#endif
    return r;
}

ArenaExperiments arenaExperiments() {
    ArenaExperiments e;
    if (const char* v = std::getenv("B841_TRIM_READY")) e.trimAtReady = (v[0] == '1');
    if (const char* v = std::getenv("B841_MMAP_THRESHOLD")) e.mmapThresholdBytes = std::atoi(v);
    if (const char* v = std::getenv("B841_TRIM_THRESHOLD")) e.trimThresholdBytes = std::atoi(v);
    return e;
}

void noteReady(double seconds) {
    std::lock_guard<std::mutex> lock(g_steadyMutex);
    g_steadyDeadline = std::chrono::steady_clock::now()
                       + std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0));
    g_steadyDone.store(false, std::memory_order_relaxed);
    g_steadyArmed.store(true, std::memory_order_release);
}

void pollSteadyState() {
    if (!g_steadyArmed.load(std::memory_order_acquire)) return;
    if (g_steadyDone.load(std::memory_order_relaxed)) return;
    std::chrono::steady_clock::time_point deadline;
    {
        std::lock_guard<std::mutex> lock(g_steadyMutex);
        deadline = g_steadyDeadline;
    }
    if (std::chrono::steady_clock::now() < deadline) return;
    g_steadyDone.store(true, std::memory_order_relaxed);
    g_steadyArmed.store(false, std::memory_order_relaxed);
    recordPoint("steady_60s");
    // B841: the second half of the delta. T496/T500 measured 17.4 MB of
    // ledger frees between ready and here with a 0.09 MB RssAnon fall; this
    // census says whether those frees landed in the top pad (trimmable), an
    // interior free bin (fragmentation), or mmap (already returned).
    recordArena("steady_60s");
}

} // namespace memory
} // namespace eqt

// ============================================================================
// Allocator hooks. Only compiled with EQT_MEMORY_LEDGER=1.
// ============================================================================
#if EQT_MEMORY_LEDGER

#if !defined(__GLIBC__)
#error "EQT_MEMORY_LEDGER=1 needs glibc's __libc_malloc family; configure with -DEQT_MEMORY_LEDGER=OFF"
#endif

extern "C" {
void* __libc_malloc(size_t);
void __libc_free(void*);
void* __libc_realloc(void*, size_t);
void* __libc_memalign(size_t, size_t);
}

namespace {

// 16 bytes on every ABI (uint64 first): keeps the user pointer at the
// backend's own alignment (16 on x86_64, 8 on 32-bit ARM).
struct LedgerHeader {
    // T500: this field used to cache the CHUNK size. It now holds the bytes the
    // caller asked for, and the chunk is recomputed from glibc's own chunk word
    // at free/realloc time -- which is exact, costs one load, and needs no extra
    // header bytes (a 24-byte header would break max_align_t, and a 32-byte one
    // would double the very overhead this is measuring).
    uint64_t requested; // bytes the caller asked for
    uint32_t category;
    // B839: the category word's UPPER 16 bits carry the startup sub-owner
    // label (packSub/subOf). The category itself needs only its 5 bits, so
    // no new header bytes: the 16-byte size and the user-pointer alignment
    // are unchanged, and a block freed by another thread or under another
    // scope still credits the owner it was charged to.
    uint32_t offset;    // user pointer - backend pointer
};
static_assert(sizeof(LedgerHeader) == 16, "header must be 16 bytes");
constexpr size_t kHeader = sizeof(LedgerHeader);

// glibc chunk accounting, exactly what its own malloc_usable_size() does:
// the size word sits SIZE_SZ bytes before the returned block; low three bits
// are flags (bit 1 = IS_MMAPPED). Usable = chunksize - SIZE_SZ, or minus
// 2*SIZE_SZ for an mmapped chunk.
inline uint64_t backendChunkBytes(void* mem) {
    const size_t sz = reinterpret_cast<size_t*>(mem)[-1];
    const size_t chunk = sz & ~static_cast<size_t>(7);
    return chunk;  // the whole chunk, header included: what the arena really spent
}

inline LedgerHeader* headerOf(void* user) {
    return reinterpret_cast<LedgerHeader*>(static_cast<char*>(user) - kHeader);
}

// T500: the whole backend chunk, recomputed. Valid for as long as the block is
// allocated, which is every call site (free reads it before __libc_free).
inline uint64_t chargedOf(void* user) {
    const LedgerHeader* h = headerOf(user);
    return backendChunkBytes(static_cast<char*>(user) - h->offset);
}

void* ledgerAlloc(size_t size, size_t align) {
    void* base;
    uint32_t offset;
    if (align <= kHeader) {
        base = __libc_malloc(size + kHeader);
        offset = kHeader;
    } else {
        // Over-allocate one alignment unit so the user pointer can sit at
        // base + align with the header in the padding before it.
        if (align & (align - 1)) return nullptr;  // not a power of two
        base = __libc_memalign(align, size + align);
        offset = static_cast<uint32_t>(align);
    }
    if (!base) return nullptr;
    void* user = static_cast<char*>(base) + offset;
    LedgerHeader* h = headerOf(user);
    h->requested = size;
    h->category = eqt::memory::detail::t_category;
    eqt::memory::packSub(h->category, eqt::memory::detail::t_sublabel);  // B839: upper 16 bits
    h->offset = offset;
    // The category index is the LOW half of the packed word (packSub put the
    // sub-owner in the upper 16); catOf() makes that explicit rather than
    // relying on the implicit uint32->uint8 narrowing.
    eqt::memory::charge(eqt::memory::catOf(h->category), backendChunkBytes(base), size,
                        eqt::memory::detail::t_sublabel);
    return user;
}

void ledgerFree(void* user) {
    if (!user) return;
    LedgerHeader* h = headerOf(user);
    eqt::memory::credit(eqt::memory::catOf(h->category), chargedOf(user), h->requested,
                        eqt::memory::subOf(h->category));
    __libc_free(static_cast<char*>(user) - h->offset);
}

void* ledgerRealloc(void* user, size_t size) {
    if (!user) return ledgerAlloc(size, kHeader);
    if (size == 0) {
        ledgerFree(user);
        return nullptr;
    }
    LedgerHeader* h = headerOf(user);
    const uint32_t cat = h->category;   // B839: label included (upper 16 bits)
    if (h->offset == kHeader) {
        const uint64_t oldCharged = chargedOf(user);
        const uint64_t oldRequested = h->requested;
        void* base = static_cast<char*>(user) - kHeader;
        void* nbase = __libc_realloc(base, size + kHeader);
        if (!nbase) return nullptr;
        void* nuser = static_cast<char*>(nbase) + kHeader;
        LedgerHeader* nh = headerOf(nuser);
        nh->requested = size;
        nh->category = cat;
        nh->offset = kHeader;
        // Ownership stays with the original category AND sub-owner: a growing
        // container belongs to whoever created it, not to whoever happens to
        // push (or to whichever scope happens to be open on the resizing
        // thread).
        eqt::memory::credit(eqt::memory::catOf(cat), oldCharged, oldRequested, eqt::memory::subOf(cat));
        eqt::memory::charge(eqt::memory::catOf(cat), backendChunkBytes(nbase), size, eqt::memory::subOf(cat));
        return nuser;
    }
    // Over-aligned block: realloc has no alignment contract beyond the
    // default, so allocate fresh and copy.
    const uint64_t oldUsable = h->requested;
    const uint8_t savedCat = eqt::memory::detail::t_category;
    const uint8_t savedSub = eqt::memory::detail::t_sublabel;
    eqt::memory::detail::t_category = eqt::memory::catOf(cat);
    eqt::memory::detail::t_sublabel = eqt::memory::subOf(cat);
    void* nuser = ledgerAlloc(size, kHeader);
    eqt::memory::detail::t_category = savedCat;
    eqt::memory::detail::t_sublabel = savedSub;
    if (!nuser) return nullptr;
    memcpy(nuser, user, oldUsable < size ? static_cast<size_t>(oldUsable) : size);
    ledgerFree(user);
    return nuser;
}

void* newImpl(size_t size, size_t align) {
    if (size == 0) size = 1;
    for (;;) {
        void* p = ledgerAlloc(size, align);
        if (p) return p;
        std::new_handler h = std::get_new_handler();
        if (!h) throw std::bad_alloc();
        h();
    }
}

} // namespace

extern "C" {

void* malloc(size_t size) { return ledgerAlloc(size, kHeader); }
void free(void* p) { ledgerFree(p); }
void* calloc(size_t n, size_t m) {
    size_t total;
    if (__builtin_mul_overflow(n, m, &total)) return nullptr;
    void* p = ledgerAlloc(total, kHeader);
    if (p) memset(p, 0, total);
    return p;
}
void* realloc(void* p, size_t size) { return ledgerRealloc(p, size); }
void* memalign(size_t align, size_t size) { return ledgerAlloc(size, align); }
void* aligned_alloc(size_t align, size_t size) { return ledgerAlloc(size, align); }
int posix_memalign(void** out, size_t align, size_t size) {
    if (align < sizeof(void*) || (align & (align - 1))) return 22;  // EINVAL
    void* p = ledgerAlloc(size, align);
    if (!p) return 12;  // ENOMEM
    *out = p;
    return 0;
}
void* valloc(size_t size) {
    return ledgerAlloc(size, static_cast<size_t>(sysconf(_SC_PAGESIZE)));
}
void* pvalloc(size_t size) {
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return ledgerAlloc((size + page - 1) & ~(page - 1), page);
}
size_t malloc_usable_size(void* p) {
    if (!p) return 0;
    const LedgerHeader* h = headerOf(p);
    // Bytes from the user pointer to the end of the chunk, minus the chunk's
    // own size word (SIZE_SZ) that the next chunk overlaps. T500: the chunk is
    // recomputed rather than cached in the header -- same number, no extra
    // header bytes.
    const uint64_t charged = chargedOf(p);
    const uint64_t tail = charged > h->offset + sizeof(size_t)
                              ? charged - h->offset - sizeof(size_t) : 0;
    return static_cast<size_t>(tail);
}

} // extern "C"

void* operator new(size_t n) { return newImpl(n, kHeader); }
void* operator new[](size_t n) { return newImpl(n, kHeader); }
void* operator new(size_t n, const std::nothrow_t&) noexcept {
    try { return newImpl(n, kHeader); } catch (...) { return nullptr; }
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept {
    try { return newImpl(n, kHeader); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { ledgerFree(p); }
void operator delete[](void* p) noexcept { ledgerFree(p); }
void operator delete(void* p, size_t) noexcept { ledgerFree(p); }
void operator delete[](void* p, size_t) noexcept { ledgerFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { ledgerFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { ledgerFree(p); }

void* operator new(size_t n, std::align_val_t a) { return newImpl(n, static_cast<size_t>(a)); }
void* operator new[](size_t n, std::align_val_t a) { return newImpl(n, static_cast<size_t>(a)); }
void* operator new(size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return newImpl(n, static_cast<size_t>(a)); } catch (...) { return nullptr; }
}
void* operator new[](size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return newImpl(n, static_cast<size_t>(a)); } catch (...) { return nullptr; }
}
void operator delete(void* p, std::align_val_t) noexcept { ledgerFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { ledgerFree(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { ledgerFree(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { ledgerFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { ledgerFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { ledgerFree(p); }

#endif // EQT_MEMORY_LEDGER
