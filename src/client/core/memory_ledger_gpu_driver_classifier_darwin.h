// B1062 (#382): the Darwin arm of the ledger's gpu_driver caller classifier.
//
// T504's classifier (memory_ledger.cpp, `#if defined(__linux__)`) charges an
// allocation to `gpu_driver` when its RETURN ADDRESS lies inside a code
// range of a GL/driver module: the host keeps a double-buffered, sorted
// table of CodeRanges, filled with dl_iterate_phdr and rebuilt on a miss.
// Darwin has no dl_iterate_phdr; this TU is the same mechanism for that
// platform, filled from `_dyld_image_count()` / `_dyld_get_image_header()`
// (mach-o/loader.h): each image's __TEXT segment, slide applied
// (__PAGEZERO excluded, a virtual guard range, not code). Image 0 (the main
// executable) is never a GPU image,
// every other image is tested through gpuStackModuleDarwin() against the
// ANGLE dylibs (libEGL, libGLESv2) and Apple's GPU frameworks (Metal,
// IOSurface, CoreDisplay) -- the same `gpu_driver` category the Linux
// classifier uses for Mesa, so the row means the same thing on both
// platforms.
//
// The hot path is ONE table load and a binary search, exactly the Linux
// arm's shape: no dladdr, no backtrace, on the allocation path. The spec's
// cost A/B (dladdr on the return address vs backtrace + dladdr, per-call
// ns) is measured by measureClassifierCostNs() -- called once from the
// unit test, never from a hook -- and quoted in docs/macos.md ("What
// differs on Darwin"); the measured gap is what justifies the table.
//
// Hook-safe by the same argument as the Linux arm: no allocation and no
// lock on the hot path (an atomic load + a search; the rebuild under a
// try-lock flag is a read of dyld's image array, and
// _dyld_get_image_header takes no malloc-zone or loader lock -- dladdr
// locks, which is another reason it stays out of the hot path); re-entry
// is cut by the thread_local below, so a malloc made from inside the
// rebuild classifies by scope, as before.
//
// All the Darwin table state lives HERE so memory_ledger.cpp keeps only a
// one-line `#elif defined(__APPLE__)` arm (plus a same-line include): its
// Linux preprocessed output is unchanged and it stays under the 1,500-line
// ceiling. Compiled and linked on Darwin only.
#pragma once
#include <cstdint>

namespace eqt {
namespace memory {
namespace classify_detail {

// One dyld image's __TEXT LC_SEGMENT_64, slide applied: an
// address in [lo, hi) came from this image. The host's
// callerIsGlStack() never sees this type -- the table is TU-private.
struct CodeRange {
    uintptr_t lo;
    uintptr_t hi;
    bool gpu;
};

// The needle test, exposed for the unit test: an image whose file name
// carries one of the built-in ANGLE / Apple-GPU needles is a GPU image;
// the empty name (image 0, the main executable) and everything else is
// not.
bool gpuStackModuleDarwin(const char* name);

// The whole classification, one entry point for the host's
// callerIsGlStack() and for the unit test. `caller` is the malloc/new
// return address (null classifies false, by the host's check). First
// contact with an address in no known range triggers the dyld rebuild
// (bounded: after a few still-unknown rebuilds, only every 1024th unknown
// hit rebuilds again, so a JIT-heavy caller does not walk the image list
// per allocation -- the same rate limit the Linux arm applies).
// Returns 1 = inside a GPU image, 0 = inside a known non-GPU image,
// -1 = no known image contains the address (the caller charges by scope).
int classifyCallerAddress(const void* caller);

// The bool form the host's `#elif` arm wants.
inline bool callerIsGlStackDarwin(const void* caller) {
    return classifyCallerAddress(caller) == 1;
}

// The spec's cost A/B, timed on the machine that runs it (the mini) and
// printed once as a [classifier-cost] line: `count` calls of dladdr(pc),
// `count` calls of backtrace(pc, 8) + dladdr on every frame, and
// `count` calls of the classifier's own table binary search for contrast.
// Returns the dladdr figure in ns/call; the other two are in the printed
// line. NEVER called from a hook -- the unit test calls it once.
double measureClassifierCostNs(const void* pc, int count);

}  // namespace classify_detail
}  // namespace memory
}  // namespace eqt
