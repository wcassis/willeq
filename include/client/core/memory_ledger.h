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
//
// Diagnostic environment switches (all off by default; T504 audit rig):
//   WILLEQ_LEDGER_OWNERS=<category>       re-point the [memledger-startup]
//                                         owner rows at another category
//   WILLEQ_LEDGER_TRACE=<category>[:<pt>] backtrace every allocation of that
//                                         category from lifecycle point <pt>
//                                         (default "ready"); [memledger-trace]
//                                         rows per point; _DEPTH=4..14 frames
//                                         hashed, _SORT=peak orders by each
//                                         site's high-water mark
//   WILLEQ_LEDGER_IDLE_EVERY=<s>          after steady_60s keep recording
//     [WILLEQ_LEDGER_IDLE_COUNT=<n>]      points idle_<seconds since ready>
//   B841_TRIM_READY / B841_MMAP_THRESHOLD / B841_TRIM_THRESHOLD: arena
//                                         experiments (arenaExperiments())
// WILLEQ_BASELINE=1 (baseline_mode.h) is the original-client feature-parity
// switch the audit measures under.

#include <array>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__linux__)
#include "client/core/malloc_compat.h"  // <malloc.h> + the mallinfo2 shim for glibc < 2.33 (B1056)
#endif

#include "client/core/gpu_buffer_census.h"  // B955: the gpu_buffers row

#ifndef EQT_MEMORY_LEDGER
#define EQT_MEMORY_LEDGER 0
#endif

// B1039 (#359): the allocator hooks wrap glibc's __libc_* entry points, which
// macOS libSystem does not have. On Darwin the ledger is ALWAYS its OFF shape
// (no symbol replaced, empty MemoryScope, every category 0, enabled() false)
// whatever the build asked for; CMakeLists.txt passes 0 there too, so no TU
// can disagree about the layout. A malloc_zone backend is open (B1039 README).
#if defined(__APPLE__) && EQT_MEMORY_LEDGER
#undef EQT_MEMORY_LEDGER
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
    GpuDriver,          // T504: a block whose malloc/new CALLER is inside the GL
                        // stack's own code (Mesa/gallium, LLVM, EGL/GLES, DRM) --
                        // pseudo-VRAM. On llvmpipe this is where the "GPU's"
                        // textures, vertex buffers and shader JIT live, inside
                        // our process; on Lima it is the driver's CPU-side state
                        // (texture storage is in GEM buffer objects). It is
                        // classified by return address, whatever MemoryScope is
                        // open, so no other category is contaminated by the
                        // platform's rasteriser. Reported on its own line and
                        // NOT counted against the 64 MB client budget (Will,
                        // 2026-09-20).
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
    "gpu_driver",
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
// B839: the current startup SUB-owner index (see the B839 block below).
// Default 0 = none: until a StartupScope opens, everything folds into the
// catch-all row.
inline thread_local uint8_t t_sublabel = 0;
}

// B839 sub-owner index values. 0 is "no label open" (every block it charges
// folds into the catch-all row); 1 is the catch-all row itself. Named
// sub-owners get 2..kStartupSublabelSlots-1 in first-open order.
constexpr uint8_t kStartupSublabelNone = 0;   // no sub-owner open
constexpr uint8_t kStartupSublabelMisc = 1;   // the catch-all sub-owner
// B909: 32 -> 64. The table is shared by EVERY owner category (T504 re-pointed
// it with WILLEQ_LEDGER_OWNERS) and filled in first-open order: by the time
// a zone loads, the startup, gfx.* and ent.* labels had taken the 30 named
// slots, so a label opened at Step 9 got kStartupSublabelNone and its 8.1 MB
// landed in "misc" (measured, gfaydark spatial, 2026-09-29). The header
// carries the index in 8 bits, so any value up to 255 is representable.
constexpr uint8_t kStartupSublabelSlots = 64; // total rows incl. the two above

// RAII scope: every allocation on this thread until the scope ends is charged
// to `c`. Nests; the previous category is restored on exit. A block freed
// under another scope is credited to the category it was CHARGED to (the
// header remembers), so mismatched scopes never drive a category negative.
class MemoryScope {
public:
#if EQT_MEMORY_LEDGER
    explicit MemoryScope(Category c) : prev_(detail::t_category), prevSub_(detail::t_sublabel) {
        detail::t_category = static_cast<uint8_t>(c);
        // B839: a category scope is not a startup sub-owner. If this thread
        // opens one while a StartupScope is still open (the client's startup
        // steps open real scopes inside their labelled step), the label must
        // go: what a ZoneParse scope charges is zone_parse, never startup.*.
        // The LIFO restore below still hands the label back to whoever owns
        // it, because prevSub_ was captured BEFORE this write.
        detail::t_sublabel = kStartupSublabelNone;
    }
    ~MemoryScope() {
        detail::t_category = prev_;
        detail::t_sublabel = prevSub_;
    }
#else
    explicit MemoryScope(Category) {}
    ~MemoryScope() = default;
#endif
    MemoryScope(const MemoryScope&) = delete;
    MemoryScope& operator=(const MemoryScope&) = delete;
private:
#if EQT_MEMORY_LEDGER
    uint8_t prev_;
    uint8_t prevSub_;
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

// ---------------------------------------------------------------------------
// B839 (2026-09-18): the `startup` sub-owners. The startup category is set
// once at main() (src/client/main.cpp) and never re-scoped per step, so the
// ledger charged every byte of process init to one bucket with no owners --
// 28,134,104 B live at `ready` on the Pi (measured T496 Leg D / T500). This
// adds a SECOND, NARROWER label that applies only while the thread's category
// is Startup: named sub-owners (startup.client, startup.graphics, ...) whose
// live bytes always sum to the category's live bytes, the catch-all
// ("misc") included. The label is per-THREAD and per-BLOCK: a block is
// credited to the sub-owner it was CHARGED to, exactly like the category, so
// mismatched scopes cannot drive a sub-owner negative. It exists so the
// [memledger-startup] lines printed at `startup` and `ready` (recordPoint)
// name every byte of the bucket on both platforms without a debug build.
//
// Cost when OFF: zero (the thread_local is a single byte, the RAII object is
// empty, the hooks are not compiled in). Cost when ON: one relaxed atomic
// increment per startup-category allocation -- no lock, no map lookup; the
// label index is resolved once per scope open against a fixed-size table.
// (The t_sublabel variable and the index constants live at the top of the
// scope helpers, because MemoryScope restores the label in LIFO order.)
//
// RAII sub-owner: every allocation this thread makes while the scope is open
// AND its category is Startup is charged to `name`. Nesting and interleaving
// with MemoryScope are safe: the label is restored in LIFO order on exit, and
// a MemoryScope opened for another category clears it (a UI allocation after
// a startup scope must not be reported as startup.*).
class StartupScope {
public:
    explicit StartupScope(const char* name);
    ~StartupScope();
    StartupScope(const StartupScope&) = delete;
    StartupScope& operator=(const StartupScope&) = delete;
private:
    uint8_t prev_;
};

// Resolve a sub-owner name to its index, creating the row on first use. When
// the slot table is full (kStartupSublabelSlots) it returns
// kStartupSublabelNone so the caller's bytes still land -- in the catch-all,
// exactly as an unlabelled allocation would. The client opens far fewer
// scopes than slots, so this overflow path is a safety net, not a routine.
uint8_t startupSublabelIndex(const char* name);

// B919 (#238): the owner table is a diagnostic (WILLEQ_LEDGER_OWNERS=<cat>
// selects the category whose blocks the rows partition). Callers that open a
// StartupScope on a HOT path (the bridge's per-event pushEvent) gate on this
// so an ordinary run -- no table requested -- pays none of the per-block
// label lookup (a mutex round-trip against the name table). It reads the
// environment ONCE per process: WILLEQ_LEDGER_OWNERS is set before main
// (every capture script exports it with the binary), so the answer never
// changes; the same variable initOwnerCategoryFromEnv() reads for g_ownerCat.
// Header-inline (getenv + the header-inline kCategoryNames): the definition
// sits here because memory_ledger.cpp is at the 1,500-line ceiling (the same
// reason B924's devices live in memory_ledger_b924.cpp).
inline bool ownerRowsEnabled() {
    static const bool on = [] {
        if (!EQT_MEMORY_LEDGER) return false;  // the ledger is off (enabled())
        const char* want = std::getenv("WILLEQ_LEDGER_OWNERS");
        if (!want || !*want) return false;
        for (size_t i = 0; i < kCategoryCount; ++i) {
            if (std::strcmp(want, kCategoryNames[i]) == 0) return true;
        }
        return false;
    }();
    return on;
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
    uint64_t liveBytes = 0;   // charged - credited (the whole glibc chunk)
    uint64_t peakBytes = 0;   // high-water mark of liveBytes since start/resetPeaks()
    uint64_t allocs = 0;      // charges since start (monotonic)
    // T500 (Will's addendum, 2026-09-17). liveBytes is the CHUNK: the caller's
    // bytes plus glibc's size word, its 16-byte rounding and the ledger's own
    // 16-byte LedgerHeader. A subsystem that tallies its own logical bytes
    // (RaceModelLoader::getMemoryStats(), say) can only ever reconcile against
    // liveRequestedBytes; the difference is per-BLOCK overhead and it is not a
    // rounding error -- T500 measured 13.07 MB of it over 824,868
    // entity_models blocks, 15.84 B each, which is 22 % of that category.
    // liveBytes stays the number checked against RssAnon, because the overhead
    // is real resident memory; this pair is what gives it a NAME.
    uint64_t liveRequestedBytes = 0;  // sum of the sizes the callers asked for
    uint64_t liveBlocks = 0;          // blocks currently live in this category
    uint64_t blockOverheadBytes = 0;  // liveBytes - liveRequestedBytes
};

// B839: one sub-owner of the startup category. The rows (misc included) are a
// PARTITION of the category's live blocks: their liveBytes always sum to
// CategoryStats(Category::Startup).liveBytes, and their liveBlocks to its
// liveBlocks. The sum holds by construction (every charge/credit touches
// exactly one row) -- the tests pin it, they do not re-derive it.
struct StartupSublabelRow {
    std::string name;   // "misc" or the scope's name, e.g. "client"
    uint64_t liveBytes = 0;
    uint64_t liveBlocks = 0;
};
// The live breakdown of the startup category, misc last (it is what a shrink
// unit has to chase), named sub-owners before it, live bytes descending.
// Empty when the ledger is OFF or before the first recordPoint() arms the
// rows (recordPoint's comment: the "startup" point is the first).
std::vector<StartupSublabelRow> startupBreakdown();

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
    uint64_t renderedFrames = 0;             // B924: processFrame() calls since start
    uint64_t rssAnonBytes = 0;               // /proc/self/status RssAnon
    uint64_t rssFileBytes = 0;               // /proc/self/status RssFile
    // T500 (Will's addendum, 2026-09-17): file-backed and shared pages have
    // never been in any number this project quotes, and they count against a
    // cgroup cap exactly as anon does. RssFile was read but reported only on
    // the log line; RssShmem was read nowhere at all.
    uint64_t rssShmemBytes = 0;              // /proc/self/status RssShmem
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
    // B933 (Will, 2026-09-29: "64 MB excludes driver memory") + B978 (Will,
    // 2026-10-01, "1) yes": GPU buffers count toward the 64 MB target): the
    // target is judged on willeq-OWNED memory =
    //   RssAnon - the ledger's gpu_driver category (clamped at 0)
    //   + gpu_buffers when they live OUTSIDE RssAnon (Lima GEM on the Pi).
    // On a software rasteriser (llvmpipe, x86) the GPU buffers are heap inside
    // gpu_driver already, so nothing is added -- no double count. The driver
    // .so files' own copy-on-write pages stay on the OWNED side (they are in
    // RssAnon and not in gpu_driver); B932 measures them per library. The
    // 256 MB ceiling stays on whole RssAnon.
    uint64_t gpuDriverBytes = 0;             // categories[GpuDriver].liveBytes
    uint64_t ownedRssAnonBytes = 0;          // max(0, rssAnon - gpuDriverBytes): the RssAnon component
    uint64_t ownedBytes = 0;                 // ownedRssAnon + gpuBuffers (only if outside RssAnon)
    uint64_t overTargetBytes = 0;            // max(0, ownedBytes - target)
    uint64_t overTargetRssAnonBytes = 0;     // max(0, rssAnon - target): the old figure, driver included
    // B955: GPU buffer memory (textures, VBOs, render targets) held OUTSIDE
    // RssAnon -- Lima GEM/shmem on the Pi -- or, on a software rasteriser,
    // the renderer's tally that already sits inside gpu_driver
    // (gpu_buffer_census.h has the sources and the decision). Since B978 it is
    // the second component of ownedBytes when it is outside RssAnon.
    GpuBufferCensus gpuBuffers;
};

// T491 addendum: the categories that would have to shrink to reach the 64 MB
// target, largest live bytes first, with the three non-category buckets
// (heap_uncharged, malloc_slack, unaccounted) included -- they are part of
// RssAnon and shrinking only the categories cannot close the gap on its own.
// The rows COVER RssAnon (see the Snapshot comment). B978: when the GPU buffers
// live outside RssAnon (the Pi) they are part of owned and so of the gap, and
// they are listed as their own `gpu_buffers` row on top of the RssAnon buckets
// (pctOfRssAnon is still relative to RssAnon). Returns every bucket with
// nonzero live bytes, sorted descending.
struct GapRow {
    std::string bucket;   // category name, or one of the three non-category buckets
    uint64_t liveBytes = 0;
    double pctOfRssAnon = 0.0;
    // B933: gpu_driver is listed (every driver byte stays reported) but is NOT
    // part of the gap to the target, so it is flagged and callers must not sum
    // it into "bytes to shrink".
    bool excludedFromGap = false;
};
std::vector<GapRow> gapToTarget(const Snapshot& s);

// B933/B978: derive ownedRssAnonBytes / ownedBytes / overTargetBytes /
// overTargetRssAnonBytes from a snapshot's rssAnonBytes, gpuDriverBytes,
// gpuBuffers and rssTargetBytes. snapshot() calls it; a test that fills those
// fields by hand calls it to pin the arithmetic.
void finishTargetGap(Snapshot& s);

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
// B839: `[memledger-startup] point=<point> owner=startup.<name> live=<bytes>
// blocks=<n> ...` lines, one per sub-owner (misc last, it is what a shrink
// unit has to chase), preceded by a total line whose live equals the
// category's live. Emitted as a sibling of the [memledger] line by
// recordPoint() at every lifecycle point, so the Pi run (and the x86 runs)
// get the same table without a second tool. Same line contract as
// toLogLine: one owner per line, `key=value` tokens.
std::string startupToLogLines(const std::string& point);

// T504 stack-trace instrument (diagnostic, off by default). traceConfigure()
// is the programmatic form of WILLEQ_LEDGER_TRACE=<category>[:<arm point>]:
// from the first recordPoint(armPoint) on, every allocation charged to `cat`
// captures a backtrace and is filed under a call-site; traceReport(point) is
// the [memledger-trace] block recordPoint() prints (empty before arming).
void traceConfigure(Category cat, const std::string& armPoint);
std::string traceReport(const std::string& point);
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
// Returns true on the call that records "steady_60s" (or an idle point), so
// the caller can log its own steady-state tables at the same instant.
bool pollSteadyState();

// Reset every category's peak (and the total peak) to its current live value.
void resetPeaks();

// B924 (#243): called once per rendered frame from the render thread. Bumps
// the frame counter the [memledger] line prints as `frames=`, so an idle
// point can be correlated with frames rendered, and -- when the
// WILLEQ_FPS_CAP=<fps> environment is set -- paces the render loop (a sleep
// to the target interval) so a hold run can be compared capped vs uncapped.
// The cap is a measurement device for B924's attribution, off by default and
// never used by any gate. Defined in src/client/core/memory_ledger_b924.cpp
// (memory_ledger.cpp is at its line ceiling).
void noteRenderedFrame();

// B924 (#243): the rendered-frame count so far (processFrame() calls).
uint64_t renderedFrames();

// B924 (#243): when the WILLEQ_IDLE_MEMMAP=1 environment is set,
// pollSteadyState() also prints the full [memmap] smaps census at every
// idle_<s> point (not just at startup/ready), so a hold run shows which
// mapping owner grows over minutes of idle rendering. A measurement switch
// off by default; it never changes what a gate reads.
bool idleMemmapEnabled();

// ---------------------------------------------------------------------------
// B841 (board, 2026-09-18): the arena-retention census. T496/T500 measured
// that between `ready` and `steady_60s` the ledger frees 17.4 MB while
// RssAnon falls 0.09 MB, because malloc_slack grows 31.58 -> 47.67 MB: a byte
// freed late is not a byte returned. mallinfo2's slack is ONE number, though,
// and it does not say where the bytes sit -- top-of-heap (trimmable), interior
// free chunks (fragmentation, not trimmable), or mmap'd (already returned to
// the OS on free). This census is that answer: glibc's own per-bin free-chunk
// accounting (malloc_info XML) folded into size bins, plus the mallinfo2
// totals, so the slack decomposes into named bytes.
//
// recordArena() is a LIFECYCLE-POINT call like recordPoint(): one malloc_info
// (a ~few-hundred-kB XML write to an in-memory stream, milliseconds) and one
// mallinfo2 per call, never per frame.
// ---------------------------------------------------------------------------

// One free-chunk size bin. Bins are half-open [lo, hi) in BYTES of the free
// chunk, EXCEPT the last bin, which is [lo, +infinity). The last break is
// 64 KiB because glibc's dynamic M_MMAP_THRESHOLD starts at 128 KiB
// (2 x 65536) and only grows: a freed chunk at or above it is returned to the
// OS via munmap instead of joining the free lists, so the bins above it are
// where retained slack would actually show up.
struct ArenaBin {
    uint64_t chunks = 0;
    uint64_t freeBytes = 0;   // sum of `total` over this bin's rows
};

constexpr size_t kArenaBinCount = 11;
inline const char* const kArenaBinNames[kArenaBinCount] = {
    "le128",   // < 128 B
    "128-256",
    "256-512",
    "512-1k",
    "1k-2k",
    "2k-4k",
    "4k-8k",
    "8k-16k",
    "16k-32k",
    "32k-64k",
    "ge64k",   // >= 64 KiB
};

// Which bin a free chunk of this size lands in (index into kArenaBinNames).
// Pure so it is testable against a fixture without an allocator.
size_t arenaBinForBytes(size_t freeChunkBytes);

// Parse a glibc malloc_info(0) XML document into the bin census. Split out
// from the in-memory write for the same reason parseSmaps is: a parser
// proved only against the running process is proved against one input.
// Unparseable input (empty, wrong root tag) yields an empty report with
// `ok` false.
struct ArenaReport {
    bool ok = false;
#if defined(__linux__)
    struct mallinfo2 mi{};          // the mallinfo2() of the same instant
#endif
    std::array<ArenaBin, kArenaBinCount> bins{};
    // Aggregates the report itself names, so a log line never needs the
    // reader to subtract: interior (fragmentation, untrimmable) vs the rest.
    uint64_t interiorFreeBytes = 0;    // fordblks + fsmblks
    uint64_t topPadBytes = 0;          // keepcost
    uint64_t mmapBytes = 0;            // hblkhd (returned to OS on free)
};

// The XML parse, fixture-testable without touching the process's allocator.
ArenaReport parseMallocInfo(const std::string& xml);

// Take the census now and LOG_INFO it as
//   [memarena] point=... zone=... ok=1 ordblks=.. smblks=.. fordblks=..
//   fsmblks=.. keepcost=.. hblks=.. hblkhd=.. arena=.. uordblks=..
//   mmap_thr=.. <bin>=<chunks>/<bytes> ...
// `trim` runs malloc_trim(0) FIRST and reports the pages it actually gave
// back (0 if the top pad was below the trim threshold). The shipped call
// sites never trim: this is the B841 experiment knob (a one-shot run flag),
// and default behaviour must stay byte-for-byte what the client has always
// done.
ArenaReport recordArena(const std::string& point, bool trim = false);

// Raw census without logging, for tests and for callers that want to compute
// their own deltas. Same cost as the log line minus the format.
ArenaReport arenaNow();

// B841 experiment knobs -- one-shot measurement runs, NEVER shipped defaults.
// The orchestrator decides after the Pi confirms (board B841), so every
// knob is off unless its variable is set, and a run without the variables
// behaves byte-for-byte as before.
//   B841_TRIM_READY=1         -> one malloc_trim(0) before the `ready` census
//   B841_MMAP_THRESHOLD=<B>   -> mallopt(M_MMAP_THRESHOLD, B) at startup, so
//                                frees at or above B unmap instead of joining
//                                the free lists (dynamic-threshold experiment)
//   B841_TRIM_THRESHOLD=<B>   -> mallopt(M_TRIM_THRESHOLD, B) at startup, so
//                                the top pad trims back to B instead of the
//                                128 KiB default
struct ArenaExperiments {
    bool trimAtReady = false;
    int mmapThresholdBytes = 0;   // 0 = leave glibc's default alone
    int trimThresholdBytes = 0;   // 0 = leave glibc's default alone
};
// Reads the environment fresh on every call (getenv is thread-safe; the
// call sites are startup and `ready`, so there is nothing to cache).
ArenaExperiments arenaExperiments();

// Raw counters for tests: live/peak/allocs of one category.
CategoryStats stats(Category c);

} // namespace memory
} // namespace eqt
