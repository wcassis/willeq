#pragma once

// T491 (2026-09-14): the memory ledger -- 100% attribution of anonymous
// memory, at the allocator level, to a lifecycle/owner category.
//
// Will's ruling (docs/decisions.md 2026-09-14): "I want 100% accounting of
// memory use and I want it controlled." The T69 per-subsystem *estimates*
// (getMemoryReportBreakdown) tallied what each subsystem believed it held and
// left 76-90% of RSS untracked. This ledger charges every byte the allocator
// hands out to whichever category the CALLING THREAD is scoped to, so the
// residual (RssAnon - ledger) is allocator slack plus non-heap anonymous
// mappings, not a guess.
//
// Mechanism (memory_ledger.cpp): with EQT_MEMORY_LEDGER=1 the TU replaces
// malloc/free/calloc/realloc/memalign/posix_memalign/aligned_alloc/valloc/
// pvalloc/malloc_usable_size and every operator new/delete with thin
// wrappers over glibc's __libc_* entry points. Each block carries a 16-byte
// header (charged bytes, category, offset to the backend pointer); charge and
// credit are relaxed atomics per category. A `thread_local` category is set
// by MemoryScope (RAII, nests, restores the previous on exit). Irrlicht's own
// containers allocate through operator new (irrAllocator::internal_new) and
// resolve to these same symbols at link time, so driver/scene allocations
// are attributed through the scope that called into Irrlicht -- no
// third_party patch is needed.
//
// Cost when OFF (EQT_MEMORY_LEDGER=0): zero -- no symbol is replaced,
// MemoryScope is an empty struct, snapshot() still reads /proc and
// mallinfo2 but every category reads 0 and `enabled` is false.
//
// Thread-safety: charge/credit are lock-free; snapshot() is a consistent
// enough read for a diagnostic (relaxed loads of monotonic counters).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifndef EQT_MEMORY_LEDGER
#define EQT_MEMORY_LEDGER 0
#endif

namespace eqt {
namespace memory {

// The lifecycle/owner set. Order is the report order; names via categoryName().
enum class Category : uint8_t {
    Unattributed = 0,   // no scope on the allocating thread (llvmpipe/Mali worker
                        // threads, glibc internals, static initialisers)
    Startup,            // process init: device, driver, fonts, DB, config
    Archives,           // PFS/S3D/EQE1 archive readers and name indexes
    ZoneParse,          // WLD/S3D parse: fragments, source geometry, S3DZone
    ZoneMesh,           // built region/zone meshes (ZoneMeshBuilder output)
    Spatial,            // BSP tree, PVS, region neighbours, HCMap/collision
    TexturesDecoded,    // CPU-side pixel data: decode, ETC1 buffers, cache entries
    TexturesDriver,     // driver-side textures (addTexture / COGLES2Texture)
    EntityModels,       // RaceModelLoader: per-race model data, skeletons, meshes
    EntityInstances,    // per-spawn render data: scene nodes, animators, name tags
    Doors,
    Objects,            // placeables (zone _obj + server objects)
    Equipment,          // EquipmentModelLoader: held/worn item models
    UI,                 // atlas, icon sheets, fonts, panels, chat
    Audio,
    Network,            // connections, packet buffers, session recording
    GameState,          // entities/inventory/spells on the game thread
    IrrlichtScene,      // scene manager, environment, sky, lights, particles,
                        // simulation worker, per-frame driver work
    Count
};

constexpr size_t kCategoryCount = static_cast<size_t>(Category::Count);

// Names: the config keys (config/constrained_presets.json "memoryLedgerBudgets"),
// the JSON keys and the log-line tokens. Header-inline so a TU that only
// needs a name (the preset parser, a tool) does not pull the allocator hooks.
inline const char* const kCategoryNames[kCategoryCount] = {
    "unattributed",
    "startup",
    "archives",
    "zone_parse",
    "zone_mesh",
    "spatial",
    "textures_decoded",
    "textures_driver",
    "entity_models",
    "entity_instances",
    "doors",
    "objects",
    "equipment",
    "ui",
    "audio",
    "network",
    "game_state",
    "irrlicht_scene",
};

inline const char* categoryName(Category c) {
    const size_t i = static_cast<size_t>(c);
    return i < kCategoryCount ? kCategoryNames[i] : "?";
}

// Reverse lookup for config/JSON keys; returns false if unknown.
inline bool categoryFromName(const std::string& name, Category& out) {
    for (size_t i = 0; i < kCategoryCount; ++i) {
        if (name == kCategoryNames[i]) {
            out = static_cast<Category>(i);
            return true;
        }
    }
    return false;
}

namespace detail {
// The calling thread's current category. An inline thread_local (C++17) so
// the scope helpers below are header-only: every TU that opens a scope
// shares this one variable with the hooks in memory_ledger.cpp, and a TU
// that opens scopes but is linked into a binary WITHOUT the hooks (a tool,
// a narrow test) still links -- the scope then simply records nothing.
inline thread_local uint8_t t_category = 0;
}

// RAII scope: every allocation on this thread until the scope ends is charged
// to `c`. Nests; the previous category is restored on exit. A block freed
// under another scope is credited to the category it was CHARGED to (the
// header remembers), so mismatched scopes never drive a category negative.
class MemoryScope {
public:
#if EQT_MEMORY_LEDGER
    explicit MemoryScope(Category c) : prev_(detail::t_category) {
        detail::t_category = static_cast<uint8_t>(c);
    }
    ~MemoryScope() { detail::t_category = prev_; }
#else
    explicit MemoryScope(Category) {}
    ~MemoryScope() = default;
#endif
    MemoryScope(const MemoryScope&) = delete;
    MemoryScope& operator=(const MemoryScope&) = delete;
private:
#if EQT_MEMORY_LEDGER
    uint8_t prev_;
#endif
};

// The category the calling thread is currently scoped to.
inline Category currentCategory() {
    return static_cast<Category>(detail::t_category);
}
// Set the thread's default category with no scope object (thread entry
// points). Returns the previous one.
inline Category setThreadCategory(Category c) {
    const Category prev = static_cast<Category>(detail::t_category);
    detail::t_category = static_cast<uint8_t>(c);
    return prev;
}

constexpr bool enabled() { return EQT_MEMORY_LEDGER != 0; }

// T491 addendum -- Will's standing decision, 2026-09-17: "the goal is willeq's
// RSS footprint between 32 and 64 MB. The 256 MB ceiling is now an interim
// bound, not the target." Both lines travel with every report so a lifecycle
// point is read against the target, not only against the ceiling that is being
// retired. kRssCeilingBytes is the FAILING bound (docs/decisions.md, T293
// envelope); kRssTargetBytes is printed as a gap, never asserted -- asserting
// it today would make every run red and tell nobody anything new.
constexpr uint64_t kRssCeilingBytes = 256ull * 1024 * 1024;
constexpr uint64_t kRssTargetBytes = 64ull * 1024 * 1024;

struct CategoryStats {
    uint64_t liveBytes = 0;   // charged - credited
    uint64_t peakBytes = 0;   // high-water mark of liveBytes since start/resetPeaks()
    uint64_t allocs = 0;      // charges since start (monotonic)
};

// One lifecycle-point report. Same schema for /pmem, the log line, the
// benchmark JSON and the gate.
struct Snapshot {
    std::string point;                       // lifecycle label
    std::string zone;                        // zone name if known
    uint32_t seq = 0;                        // zone-in counter (0 before any)
    bool ledgerEnabled = false;
    std::array<CategoryStats, kCategoryCount> categories{};
    uint64_t ledgerTotalBytes = 0;           // sum of live over every category
    uint64_t ledgerPeakBytes = 0;            // high-water mark of the total
    uint64_t allocCount = 0;                 // total charges since start
    uint64_t rssAnonBytes = 0;               // /proc/self/status RssAnon
    uint64_t rssFileBytes = 0;               // /proc/self/status RssFile
    uint64_t vmRssBytes = 0;                 // /proc/self/status VmRSS
    uint64_t unattributedBytes = 0;          // max(0, rssAnon - ledgerTotal)
    double unattributedPct = 0.0;            // 100 * unattributed / rssAnon
    uint64_t mallocArenaBytes = 0;           // mallinfo2: arena + hblkhd
    uint64_t mallocInUseBytes = 0;           // mallinfo2: uordblks + hblkhd
    uint64_t mallocSlackBytes = 0;           // arena - inuse (free lists, fragmentation)
    // How RssAnon is covered. NOT a strict partition, and the reason is
    // measured, not assumed: mallinfo2's `arena` is what glibc has taken from
    // the OS, and its free lists may hold pages that are NOT resident, so the
    // heap buckets can sum past the resident total. The buckets therefore
    // COVER RssAnon (sum >= RssAnon); only `unaccounted` below is exact.
    //   attributed      = ledgerTotalBytes           (a category owns it)
    //   heap_uncharged  = mallocInUse - ledgerTotal  (glibc's own internals)
    //   malloc_slack    = arena - inUse              (free lists, fragmentation)
    //   unaccounted     = rssAnon - arena            (NOT heap at all)
    // unattributedBytes above is rssAnon - ledger, kept because the plan and
    // the gate's first bound were written in those terms.
    // Heap bytes glibc counts as IN USE that the ledger did not charge:
    // allocations glibc makes through its OWN internal malloc, which an
    // in-executable interposer does not see (early stdio/locale/dl work).
    // Small and bounded, but it is real RSS, so it gets a named bucket rather
    // than silently widening "unaccounted" -- that is what 100% means.
    uint64_t heapUnchargedBytes = 0;         // max(0, mallocInUse - ledgerTotal)
    // Anonymous RSS that is NOT on glibc's heap at all: thread stacks, direct
    // mmap (Irrlicht/driver), llvmpipe's JIT code pages. Exact, and the only
    // residual no amount of allocator attribution can ever reduce.
    uint64_t unaccountedBytes = 0;           // max(0, rssAnon - mallocArena)
    double unaccountedPct = 0.0;             // 100 * unaccounted / rssAnon
    // T491 addendum (2026-09-17): the two lines every point is read against.
    uint64_t rssCeilingBytes = kRssCeilingBytes;
    uint64_t rssTargetBytes = kRssTargetBytes;
    uint64_t overCeilingBytes = 0;           // max(0, rssAnon - ceiling)
    uint64_t overTargetBytes = 0;            // max(0, rssAnon - target)
};

// T491 addendum: the categories that would have to shrink to reach the 64 MB
// target, largest live bytes first, with the three non-category buckets
// (heap_uncharged, malloc_slack, unaccounted) included -- they are part of
// RssAnon and shrinking only the categories cannot close the gap on its own.
// The rows are a PARTITION of RssAnon: they sum to it exactly. Returns every
// bucket with nonzero live bytes, sorted descending.
struct GapRow {
    std::string bucket;   // category name, or one of the three non-category buckets
    uint64_t liveBytes = 0;
    double pctOfRssAnon = 0.0;
};
std::vector<GapRow> gapToTarget(const Snapshot& s);

// ---------------------------------------------------------------------------
// T491 addendum (2026-09-17, from T490's measurement, board B801): the gap to
// the 64 MB target is ENTIRELY the pre-zone baseline -- on the Pi nro goes
// 192,284 kB -> 190,548 kB across the whole load ladder, so zone loading costs
// ~nothing in anon and ~180 MB is already resident before any zone exists.
// The allocator ledger cannot see that memory: driver/EGL/Mali allocations,
// thread stacks and every shared library's writable data are mmap'd or brk'd
// behind malloc's back and land in the `unaccounted` bucket by construction.
//
// /proc/self/smaps CAN see it: every mapping reports its own `Anonymous:`
// (private dirty anonymous pages -- exactly RssAnon, summed) alongside the
// pathname that says WHOSE it is. This is the tool that answers "what is the
// untracked 180 MB", and it is the only one that works for allocations that
// never went through malloc.
// ---------------------------------------------------------------------------

struct MappingRow {
    std::string name;        // "[heap]", "[stack]", "[anon]", "so:libfoo.so.1", "file:willeq"
    uint64_t anonBytes = 0;  // sum of `Anonymous:` -- the RssAnon this owner holds
    uint64_t rssBytes = 0;   // sum of `Rss:` (anon + file-backed resident)
    uint64_t sizeBytes = 0;  // sum of `Size:` -- address space, resident or not
    uint32_t mappings = 0;   // how many mappings folded into this row
};

struct MappingBreakdown {
    bool available = false;      // false if /proc/self/smaps could not be read
    std::vector<MappingRow> rows;  // descending by anonBytes
    uint64_t totalAnonBytes = 0;
    uint64_t totalRssBytes = 0;
    uint32_t mappings = 0;
};

// Parse an smaps-format buffer. Split out from the /proc read so it is
// testable offline against a fixture -- a parser proved only against the
// running process is proved against one input.
MappingBreakdown parseSmaps(const std::string& text);
// Read and parse /proc/self/smaps. Costs one ~100 kB read and a parse
// (milliseconds), so this is a LIFECYCLE-POINT call, never per frame.
MappingBreakdown readMappings();
// `[memmap] point=<point> owner=<name> anon=<bytes> rss=<bytes> size=<bytes>
// maps=<n>` lines, one per row, biggest first, capped at `topN` (0 = all),
// preceded by a total line. Same parse contract as the [memledger] lines.
std::string mappingsToLogLines(const std::string& point, const MappingBreakdown& b,
                               size_t topN = 12);
// JSON array of the rows, for the benchmark's memory JSON and the gate.
std::string mappingsToJson(const MappingBreakdown& b, size_t topN = 24);
// Snapshot + LOG_INFO of the [memmap] lines. Called at the baseline-defining
// lifecycle points (startup, ready) -- not at every point.
MappingBreakdown recordMappings(const std::string& point, size_t topN = 12);

// Take a snapshot now. `point` is the lifecycle label; `zone` optional.
Snapshot snapshot(const std::string& point, const std::string& zone = std::string());

// Single-line form: `[memledger] point=... zone=... seq=... rss_anon=...
// ledger=... unattributed=... unattributed_pct=... malloc_arena=...
// malloc_inuse=... malloc_slack=... allocs=... <cat>=<live>/<peak> ...`.
// This is what the cap-hunt script greps.
std::string toLogLine(const Snapshot& s);
// Multi-line human form for /pmem (header, one row per category, totals).
std::string toReportText(const Snapshot& s);
// JSON object (no trailing newline). Keys: point, zone, seq, ledgerEnabled,
// categories{name:{live,peak,allocs}}, ledgerTotalBytes, ledgerPeakBytes,
// allocCount, rssAnonBytes, rssFileBytes, vmRssBytes, unattributedBytes,
// unattributedPct, mallocArenaBytes, mallocInUseBytes, mallocSlackBytes.
std::string toJson(const Snapshot& s);

// Snapshot + LOG_INFO(MOD_MAIN) of the log line. The lifecycle-point call.
Snapshot recordPoint(const std::string& point, const std::string& zone = std::string());

// Zone-in bookkeeping carried on every line: the renderer calls
// setCurrentZone() at zone entry and it bumps `seq`.
void setCurrentZone(const std::string& zone);
std::string currentZone();

// Steady-state point: noteReady() arms a timer (default 60 s); pollSteadyState()
// (once per frame) records "steady_60s" exactly once after it elapses.
void noteReady(double seconds = 60.0);
void pollSteadyState();

// Reset every category's peak (and the total peak) to its current live value.
void resetPeaks();

// Raw counters for tests: live/peak/allocs of one category.
CategoryStats stats(Category c);

} // namespace memory
} // namespace eqt
