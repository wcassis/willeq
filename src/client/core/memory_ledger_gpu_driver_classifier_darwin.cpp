// B1062 (#382): the Darwin arm of the ledger's gpu_driver caller classifier.
// See the header for the mechanism, the hook-safety argument and the cost
// A/B. All the Darwin table state lives here, so the host
// (memory_ledger.cpp) keeps only a one-line `#elif defined(__APPLE__)` arm
// -- its Linux preprocessed output is unchanged and it stays under the
// 1,500-line ceiling. Compiled and linked on Darwin only.
#include "memory_ledger_gpu_driver_classifier_darwin.h"

#if defined(__APPLE__)

#include <mach-o/dyld.h>   // _dyld_image_count / _dyld_get_image_header / _dyld_get_image_vmaddr_slide / _dyld_get_image_name
#include <mach-o/loader.h> // mach_header(64), segment_command_64, LC_SEGMENT_64
#include <dlfcn.h>         // dladdr (cost A/B only -- never on the hot path)
#include <execinfo.h>      // backtrace (cost A/B only)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace eqt {
namespace memory {
namespace classify_detail {

namespace {

constexpr size_t kMaxCodeRanges = 1024;  // the Darwin walker records the
                                        // __TEXT segment of every image:
                                        // one range per image (379 in the
                                        // mini's full dyld set), with
                                        // headroom for later dlopen'd ones
struct CodeRanges {
    CodeRange r[kMaxCodeRanges];
    size_t n;
};
CodeRanges g_rangeTables[2];                       // zero-initialised
std::atomic<int> g_activeRangeTable{-1};           // -1: never built
std::atomic_flag g_rangeTableBusy = ATOMIC_FLAG_INIT;
std::atomic<uint32_t> g_unknownAfterRebuild{0};
std::atomic<uint32_t> g_unknownHits{0};
thread_local bool t_inClassifier = false;          // rebuild re-entrancy guard

void rebuildCodeRanges();  // defined below, below the image walker

// The table is populated by the first unknown-address rebuild (the hot path
// below), which on a real run fires within the first few allocations: dyld
// has already loaded the initial image set (the executable, libSystem and
// the dylibs the binary links against -- ANGLE's dylibs are exactly such
// link-time images of this binary), so that first rebuild already contains
// the whole GPU stack. Later rebuilds (rate-limited below) only matter for
// images dlopen'd after that.

inline const char* imageBaseName(const char* name) {
    if (!name) return "";
    const char* slash = strrchr(name, '/');
    return slash ? slash + 1 : name;
}

void rebuildCodeRanges() {
    if (g_rangeTableBusy.test_and_set(std::memory_order_acquire)) return;
    const int cur = g_activeRangeTable.load(std::memory_order_relaxed);
    const int next = cur == 0 ? 1 : 0;
    CodeRanges& t = g_rangeTables[next];
    t.n = 0;
    const uint32_t images = _dyld_image_count();
    for (uint32_t i = 0; i < images; ++i) {
        const bool gpu = (i == 0) ? false : gpuStackModuleDarwin(_dyld_get_image_name(i));
        const mach_header* h = _dyld_get_image_header(i);
        const uintptr_t slide = _dyld_get_image_vmaddr_slide(i);
        const bool is64 = (h->magic == MH_MAGIC_64);
        const uint32_t ncmds = is64 ? (reinterpret_cast<const mach_header_64*>(h))->ncmds
                                    : h->ncmds;
        const char* p = reinterpret_cast<const char*>(h)
                        + (is64 ? sizeof(mach_header_64) : sizeof(mach_header));
        for (uint32_t c = 0; c < ncmds; ++c) {
            const auto* cmd = reinterpret_cast<const struct load_command*>(p);
            p += cmd->cmdsize;
            if (cmd->cmd != LC_SEGMENT_64) continue;
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            // One range PER IMAGE: its __TEXT segment (the code + read-only
            // data, and the one every caller address lives in). Recording
            // every non-__PAGEZERO segment (as an earlier revision did)
            // overflows the table on a full dyld image set -- the mini's
            // benchmark loads 379 images, and their ~2,400 data segments
            // (libswift*, libobjc, libSystem, ...) pushed the last images'
            // __TEXT past the end, which is exactly where the GPU stack's
            // call sites land. __PAGEZERO stays excluded: a virtual guard
            // range (0 .. 47-bit) with no code.
            if (std::strcmp(seg->segname, "__TEXT") != 0) continue;
            if (t.n < kMaxCodeRanges) {
                t.r[t.n++] = CodeRange{
                    slide + static_cast<uintptr_t>(seg->vmaddr),
                    slide + static_cast<uintptr_t>(seg->vmaddr + seg->vmsize),
                    gpu};
            }
        }
    }
    std::sort(t.r, t.r + t.n, [](const CodeRange& a, const CodeRange& b) { return a.lo < b.lo; });
    g_activeRangeTable.store(next, std::memory_order_release);
    g_rangeTableBusy.clear(std::memory_order_release);
}

// 1 = GPU image, 0 = a known non-GPU image, -1 = no known range contains pc.
int lookupCaller(uintptr_t pc) {
    const int a = g_activeRangeTable.load(std::memory_order_acquire);
    if (a < 0) return -1;
    const CodeRanges& t = g_rangeTables[a];
    size_t lo = 0, hi = t.n;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (pc < t.r[mid].lo) hi = mid;
        else if (pc >= t.r[mid].hi) lo = mid + 1;
        else return t.r[mid].gpu ? 1 : 0;
    }
    return -1;
}

}  // namespace

bool gpuStackModuleDarwin(const char* name) {
    if (!name || !*name) return false;  // image 0: the main executable
    const char* base = imageBaseName(name);
    static const char* const kNeedles[] = {
        "libEGL",      // ANGLE: libEGL.dylib
        "libGLESv2",   // ANGLE: libGLESv2.dylib
        "Metal",       // Apple Metal framework (ANGLE's backend)
        "IOSurface",   // Apple: IOSurface backing store
        "CoreDisplay", // Apple: display command stream
    };
    for (const char* needle : kNeedles) {
        if (strstr(base, needle)) return true;
    }
    return false;
}

int classifyCallerAddress(const void* caller) {
    if (t_inClassifier || !caller) return -1;
    const uintptr_t pc = reinterpret_cast<uintptr_t>(caller);
    int r = lookupCaller(pc);
    if (r >= 0) return r;
    // Unknown address: an image dyld loaded after the last scan (ANGLE and
    // the frameworks load at startup, so in practice this is the very first
    // allocation after process start) -- rebuild at once so the GPU stack's
    // first allocations are not missed. Once a rebuild has NOT found the
    // address a few times, it is JIT / non-image code and further rebuilds
    // are rate-limited to every 1024th unknown hit, so a JIT-heavy frame
    // does not walk the image list per allocation (the Linux arm's rule).
    const uint32_t hits = g_unknownHits.fetch_add(1, std::memory_order_relaxed);
    if (g_unknownAfterRebuild.load(std::memory_order_relaxed) > 8 && (hits & 1023) != 0) return -1;
    t_inClassifier = true;
    rebuildCodeRanges();
    t_inClassifier = false;
    r = lookupCaller(pc);
    if (r < 0) g_unknownAfterRebuild.fetch_add(1, std::memory_order_relaxed);
    return r;
}

double measureClassifierCostNs(const void* pc, int count) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(pc);
    const auto t0 = std::chrono::steady_clock::now();
    int dladdrHits = 0;
    for (int i = 0; i < count; ++i) {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(addr), &info)) ++dladdrHits;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double dladdrNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / static_cast<double>(count);

    const auto b0 = std::chrono::steady_clock::now();
    int framesResolved = 0;
    constexpr int kDepth = 8;  // the T504 trace's default depth, as the reference
    for (int i = 0; i < count; ++i) {
        void* frames[kDepth] = {};
        const int n = backtrace(frames, kDepth);
        for (int f = 0; f < n; ++f) {
            Dl_info info{};
            if (dladdr(frames[f], &info)) ++framesResolved;
        }
    }
    const auto b1 = std::chrono::steady_clock::now();
    const double btNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(b1 - b0).count() / static_cast<double>(count);

    // The classifier's own hot path, measured for the same line: one table
    // load + binary search over the dyld image table (no dladdr, no lock).
    const auto h0 = std::chrono::steady_clock::now();
    int tableHits = 0;
    for (int i = 0; i < count; ++i) {
        if (lookupCaller(addr) >= 0) ++tableHits;
    }
    const auto h1 = std::chrono::steady_clock::now();
    const double tableNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(h1 - h0).count() / static_cast<double>(count);

    std::fprintf(stderr,
                 "[classifier-cost] B1062 (macOS, %d calls each): dladdr(pc) %.1f ns/call (%d/%d resolved), "
                 "backtrace(8)+dladdr-per-frame %.1f ns/call (%d frames resolved), "
                 "the classifier's table binary search %.2f ns/call (%d/%d in a known image) -- the "
                 "search is the hot path; dladdr and backtrace are the alternatives it does not take\n",
                 count, dladdrNs, dladdrHits, count, btNs, framesResolved, tableNs, tableHits, count);
    return dladdrNs;
}

}  // namespace classify_detail
}  // namespace memory
}  // namespace eqt

#endif  // __APPLE__
