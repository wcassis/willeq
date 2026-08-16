// t122_texprobe.h — T122 lock-free texture-lifecycle probe.
//
// T123 extension: adds EventKind::MATERIAL, recording the SMaterial* the
// driver holds at each applyMaterialRenderState() call (irrespective of
// whether cacheTexParams() actually fires -- P42's texParamsMatch() dedup
// skips most calls). Recorded at the OUTERMOST non-copied reference
// (COpenGLES2Driver::applyMaterialState()'s own `material` parameter for the
// built-in path, and applyCustomShaderState()'s own `material` parameter --
// BEFORE it is copied into a local `baseMat` -- for the custom-shader path),
// specifically so the recorded address is never a stack-local copy. Because
// both the MATERIAL record and any resulting TEXPARAMS record happen
// synchronously on the same thread with nothing else in this translation
// unit recording between them, the MATERIAL entry immediately preceding a
// given TEXPARAMS entry in the same thread's per-thread sequence is that
// write's real material address -- resolvable offline against mesh-buffer/
// node-override materials to name the holder. See
// refactor/T123_texture_holder_audit.md.
//
// T126 extension: T124's post-mortem structural inference on that MATERIAL
// pointer proved unreliable -- the core is taken at *crash* time, potentially
// long after the offending write, so memory around a resolved material_ptr
// may already have been reused by something else. See
// refactor/T126_uaf_attribution.md and T124's Review for the two
// inconclusive gdb attempts this replaces as the primary technique.
//
// Instead, T126 records the answer AT WRITE TIME: every TEXPARAMS entry now
// carries an 8-deep return-address chain (deeper than the plan's suggested
// "0..3" -- see the kCallChainDepth comment below for why 4 wasn't enough)
// (__builtin_return_address(0..7), __builtin_extract_return_addr'd) captured
// in cacheTexParams() itself, via the T122_RECORD_TEXPARAMS macro expanding
// directly at that call site (so the builtins see cacheTexParams()'s own
// frame as "frame 0", not some wrapper's). This names the CALL PATH into
// cacheTexParams() -- which scene node/subsystem's draw call reached it --
// directly from evidence recorded at the moment of the write, with nothing
// to reconstruct from potentially-reused heap memory afterward. Symbolised
// offline with addr2line/llvm-symbolizer against the unstripped probe
// binary (scripts/t126_resolve_callchain.py). A few extra register reads and
// stack-frame walks, still no allocation, no locks, no syscalls -- compatible
// with every constraint below. The probe build now also uses
// -fno-omit-frame-pointer (in addition to the existing -g) specifically so
// __builtin_return_address(N>0) has real frame-pointer chains to walk;
// without it, frames beyond 0 are unreliable on x86_64 per GCC's own
// documentation for that builtin.
//
// Generic sanitizers are exhausted on this arc (T120: ASAN with a real
// quarantine only ever catches the corruption at *free* time, never at the
// write; T121: TSAN found no race on the zone-crossing/teardown path). What
// is left is "a write of a plausible value through a wrong pointer into a
// live allocation" — undetectable by either tool, and only diagnosable by
// recording the texture lifecycle (construction/destruction) and every
// cacheTexParams() write, then resolving offline against a core dump.
//
// T119 already produced a false negative on this exact arc: its diagnostic
// took ConstrainedTextureCache::mutex_ and linear-scanned the texture cache
// at the same call site this probe watches, on the renderer's hottest path,
// which turned a 10-of-10 crash into two clean runs (see the T120
// orchestrator-review entry in refactor/deferred.md). This probe is
// deliberately built to NOT repeat that:
//
//  - Fixed-size static array in BSS (kRingCapacity, power of two) — no
//    dynamic allocation, ever.
//  - A single std::atomic<uint32_t> fetch_add(relaxed) claims a slot; no
//    mutex, no CAS retry loop.
//  - Entries are POD (pointer, 4 return-address pointers, 3x uint32_t, 1
//    byte kind) — no strings, no containers, no formatting.
//  - No logging, no file I/O, no syscalls on the record path. pthread_self()
//    is a plain TLS read on glibc/NPTL, not a syscall; __builtin_return_address
//    is a handful of loads following the frame-pointer chain, also not a
//    syscall and not a call into libc.
//  - Nothing is read back at runtime. The ring is written-only from the
//    process's point of view; it is recovered entirely offline from a core
//    dump by scripts/t122_extract_ring.py, which locates it by ELF symbol
//    and parses the raw bytes — no gdb pretty-printing of the C++ type is
//    required, so this file does not need to ship with -g debug info to be
//    extractable (though the probe build turns -g on anyway, for the crash
//    backtrace itself and for T126's addr2line symbolisation).
//
// Compile-time gated behind WILLEQ_T122_TEXPROBE, OFF by default. When the
// macro is undefined, every T122_RECORD_* callsite compiles to nothing (the
// call sites use the macro form specifically so a normal build never even
// names irr::video::t122:: — this header does not need to be #ifdef-guarded
// at its #include sites).
#pragma once

#ifdef WILLEQ_T122_TEXPROBE

#include <atomic>
#include <cstdint>
#include <pthread.h>

namespace irr {
namespace video {
namespace t122 {

enum class EventKind : uint8_t {
    CREATE    = 1,  // COGLES2Texture constructed
    DESTROY   = 2,  // COGLES2Texture destructed
    TEXPARAMS = 3,  // cacheTexParams() entered (the candidate corrupting write)
    MATERIAL  = 4,  // T123: SMaterial* the driver holds at applyMaterialRenderState()
                    // entry, recorded at the outermost non-copied reference (see
                    // header comment above). `ptr` is the SMaterial address; `value`
                    // is unused (0).
};

// T126 deviation from the plan's "0..3" suggestion: 4 frames landed entirely
// inside COpenGLES2Driver.cpp itself (applyMaterialRenderState ->
// applyCustomShaderState -> applyMaterialState -> setMaterial) -- the driver's
// own internal call stack, one level short of the scene node/subsystem that
// actually called setMaterial(). Extended to 8 to reach past the driver
// boundary into the caller that names the holder. Still a handful of loads
// (frame-pointer chain walk), no allocation, no locks.
constexpr uint32_t kCallChainDepth = 8;

// 88 bytes on x86_64 (8-byte pointer + 8x8-byte retaddr + 12 bytes of
// uint32_t fields + 1 kind byte + 3 pad), 68 on armhf — POD,
// no padding surprises beyond natural alignment (pointer fields first keeps
// 8-byte alignment on 64-bit without an explicit alignas). retaddr[] is only
// populated for TEXPARAMS entries (T126); it is zero-filled for CREATE/
// DESTROY/MATERIAL, which don't need a call path.
struct Entry {
    const void* ptr;                     // COGLES2Texture* (CREATE/DESTROY) or tex (TEXPARAMS/MATERIAL's SMaterial*)
    const void* retaddr[kCallChainDepth]; // T126: TEXPARAMS only -- __builtin_return_address(0..7)
                                          // from the cacheTexParams() call site, outermost-first.
                                          // A null entry mid-chain means the frame walk ran out
                                          // (e.g. inlined into its caller, or genuinely fewer
                                          // frames deep) -- not an error, see
                                          // t126_resolve_callchain.py.
    uint32_t seq;       // this entry's pre-mask fetch_add value (monotonic per-process)
    uint32_t tid;       // (uint32_t)(uintptr_t)pthread_self() — thread that recorded this
    uint32_t value;     // CREATE/DESTROY: GL texture name; TEXPARAMS: magFilter (e.g. 0x2601 = GL_LINEAR)
    uint8_t kind;        // EventKind
    uint8_t pad[3];
};

constexpr uint32_t kRingCapacity = 1u << 18;  // 262144 entries (~23.1MB on x86_64 at 88B/entry), power of two
constexpr uint32_t kRingMask = kRingCapacity - 1;

// Zero-initialized in BSS — defined once in COGLES2Texture.cpp.
extern Entry g_t122Ring[kRingCapacity];
extern std::atomic<uint32_t> g_t122RingIndex;

inline void t122Record(EventKind kind, const void* ptr, uint32_t value) {
    // relaxed: entries only need to be individually well-formed and their
    // relative order recoverable from `seq`; no cross-thread happens-before
    // relationship is needed between the index claim and anything else.
    uint32_t seq = g_t122RingIndex.fetch_add(1, std::memory_order_relaxed);
    Entry& e = g_t122Ring[seq & kRingMask];
    e.ptr = ptr;
    for (uint32_t i = 0; i < kCallChainDepth; ++i) e.retaddr[i] = nullptr;
    e.seq = seq;
    e.tid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
    e.value = value;
    e.kind = static_cast<uint8_t>(kind);
}

// T126: TEXPARAMS-specific recorder taking a pre-captured 8-deep return
// address chain. The chain MUST be captured by the caller (via the
// T122_RECORD_TEXPARAMS macro, which expands __builtin_return_address(N)
// directly at the cacheTexParams() call site) rather than inside this
// function -- __builtin_return_address(N) is relative to whichever function
// textually contains the call, so wrapping it in a second function call
// would shift every frame by one and make "frame 0" this function instead
// of cacheTexParams()'s actual caller.
inline void t122RecordTexParamsWithCallChain(const void* ptr, uint32_t value,
                                              const void* ra0, const void* ra1,
                                              const void* ra2, const void* ra3,
                                              const void* ra4, const void* ra5,
                                              const void* ra6, const void* ra7) {
    uint32_t seq = g_t122RingIndex.fetch_add(1, std::memory_order_relaxed);
    Entry& e = g_t122Ring[seq & kRingMask];
    e.ptr = ptr;
    e.retaddr[0] = ra0;
    e.retaddr[1] = ra1;
    e.retaddr[2] = ra2;
    e.retaddr[3] = ra3;
    e.retaddr[4] = ra4;
    e.retaddr[5] = ra5;
    e.retaddr[6] = ra6;
    e.retaddr[7] = ra7;
    e.seq = seq;
    e.tid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
    e.value = value;
    e.kind = static_cast<uint8_t>(EventKind::TEXPARAMS);
}

}  // namespace t122
}  // namespace video
}  // namespace irr

#define T122_RECORD_CREATE(ptr, value) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::CREATE, (ptr), (value))
#define T122_RECORD_DESTROY(ptr, value) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::DESTROY, (ptr), (value))
// T126: expands the __builtin_return_address() calls directly at the
// caller's call site (see t122RecordTexParamsWithCallChain's comment above
// for why this must not be hidden behind a second function call).
// __builtin_extract_return_addr strips any ISA-mode bits some targets stash
// in the low bits of a return address (a no-op on x86_64, load-bearing on
// some 32-bit ARM ABIs) so the raw pointer is directly addr2line-able either
// way.
#define T122_RECORD_TEXPARAMS(ptr, value) \
    ::irr::video::t122::t122RecordTexParamsWithCallChain((ptr), (value), \
        __builtin_extract_return_addr(__builtin_return_address(0)), \
        __builtin_extract_return_addr(__builtin_return_address(1)), \
        __builtin_extract_return_addr(__builtin_return_address(2)), \
        __builtin_extract_return_addr(__builtin_return_address(3)), \
        __builtin_extract_return_addr(__builtin_return_address(4)), \
        __builtin_extract_return_addr(__builtin_return_address(5)), \
        __builtin_extract_return_addr(__builtin_return_address(6)), \
        __builtin_extract_return_addr(__builtin_return_address(7)))
#define T122_RECORD_MATERIAL(matPtr) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::MATERIAL, (matPtr), 0)

#else  // !WILLEQ_T122_TEXPROBE

#define T122_RECORD_CREATE(ptr, value) ((void)0)
#define T122_RECORD_DESTROY(ptr, value) ((void)0)
#define T122_RECORD_TEXPARAMS(ptr, value) ((void)0)
#define T122_RECORD_MATERIAL(matPtr) ((void)0)

#endif  // WILLEQ_T122_TEXPROBE
