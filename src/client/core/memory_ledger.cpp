// T491: the memory ledger -- see include/client/core/memory_ledger.h for the
// design. This TU is the ONLY place that replaces the allocator entry points;
// it must stay free of any dependency that could allocate from inside a hook
// (the hooks below touch atomics, a thread_local byte and glibc's __libc_*
// backends, nothing else).

#include "client/core/memory_ledger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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
    if (s.rssAnonBytes > 0) {
        s.unattributedPct = 100.0 * static_cast<double>(s.unattributedBytes)
                            / static_cast<double>(s.rssAnonBytes);
    }
    return s;
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
    out += "\"mallocSlackBytes\":" + std::to_string(s.mallocSlackBytes);
    out += "}";
    return out;
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
