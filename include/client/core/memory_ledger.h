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
};

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
