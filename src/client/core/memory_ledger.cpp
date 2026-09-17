// T491: the memory ledger -- see include/client/core/memory_ledger.h for the
// design. This TU is the ONLY place that replaces the allocator entry points;
// it must stay free of any dependency that could allocate from inside a hook
// (the hooks below touch atomics, a thread_local byte and glibc's __libc_*
// backends, nothing else).

#include "client/core/memory_ledger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <mutex>
#include <new>

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

inline void charge(uint8_t cat, uint64_t bytes) {
    const uint64_t v = g_counters.live[cat].fetch_add(bytes, std::memory_order_relaxed) + bytes;
    raiseMax(g_counters.peak[cat], v);
    g_counters.allocs[cat].fetch_add(1, std::memory_order_relaxed);
    const uint64_t t = g_counters.totalLive.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    raiseMax(g_counters.totalPeak, t);
}

inline void credit(uint8_t cat, uint64_t bytes) {
    g_counters.live[cat].fetch_sub(bytes, std::memory_order_relaxed);
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
    return st;
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
    Snapshot s = snapshot(point, zone);
    LOG_INFO(MOD_MAIN, "{}", toLogLine(s));
    return s;
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
    uint64_t charged;   // bytes charged to `category` (whole backend chunk)
    uint32_t category;
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
    h->charged = backendChunkBytes(base);
    h->category = eqt::memory::detail::t_category;
    h->offset = offset;
    eqt::memory::charge(static_cast<uint8_t>(h->category), h->charged);
    return user;
}

void ledgerFree(void* user) {
    if (!user) return;
    LedgerHeader* h = headerOf(user);
    eqt::memory::credit(static_cast<uint8_t>(h->category), h->charged);
    __libc_free(static_cast<char*>(user) - h->offset);
}

void* ledgerRealloc(void* user, size_t size) {
    if (!user) return ledgerAlloc(size, kHeader);
    if (size == 0) {
        ledgerFree(user);
        return nullptr;
    }
    LedgerHeader* h = headerOf(user);
    const uint32_t cat = h->category;
    if (h->offset == kHeader) {
        const uint64_t oldCharged = h->charged;
        void* base = static_cast<char*>(user) - kHeader;
        void* nbase = __libc_realloc(base, size + kHeader);
        if (!nbase) return nullptr;
        void* nuser = static_cast<char*>(nbase) + kHeader;
        LedgerHeader* nh = headerOf(nuser);
        nh->charged = backendChunkBytes(nbase);
        nh->category = cat;
        nh->offset = kHeader;
        // Ownership stays with the original category: a growing container
        // belongs to whoever created it, not to whoever happens to push.
        eqt::memory::credit(static_cast<uint8_t>(cat), oldCharged);
        eqt::memory::charge(static_cast<uint8_t>(cat), nh->charged);
        return nuser;
    }
    // Over-aligned block: realloc has no alignment contract beyond the
    // default, so allocate fresh and copy.
    const uint64_t oldUsable = h->charged > h->offset ? h->charged - h->offset : 0;
    const uint8_t saved = eqt::memory::detail::t_category;
    eqt::memory::detail::t_category = static_cast<uint8_t>(cat);
    void* nuser = ledgerAlloc(size, kHeader);
    eqt::memory::detail::t_category = saved;
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
    // own size word (SIZE_SZ) that the next chunk overlaps.
    const uint64_t tail = h->charged > h->offset + sizeof(size_t)
                              ? h->charged - h->offset - sizeof(size_t) : 0;
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
