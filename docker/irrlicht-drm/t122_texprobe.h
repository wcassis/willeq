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
//  - Entries are POD (pointer, 3x uint32_t, 1 byte kind) — no strings, no
//    containers, no formatting.
//  - No logging, no file I/O, no syscalls on the record path. pthread_self()
//    is a plain TLS read on glibc/NPTL, not a syscall.
//  - Nothing is read back at runtime. The ring is written-only from the
//    process's point of view; it is recovered entirely offline from a core
//    dump by scripts/t122_extract_ring.py, which locates it by ELF symbol
//    and parses the raw bytes — no gdb pretty-printing of the C++ type is
//    required, so this file does not need to ship with -g debug info to be
//    extractable (though the probe build turns -g on anyway, for the crash
//    backtrace itself).
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

// 24 bytes on x86_64 (8-byte pointer), 20 on armhf — POD, no padding surprises
// beyond natural alignment (pointer first keeps 8-byte alignment on 64-bit
// without an explicit alignas).
struct Entry {
    const void* ptr;   // COGLES2Texture* (CREATE/DESTROY) or tex (TEXPARAMS)
    uint32_t seq;       // this entry's pre-mask fetch_add value (monotonic per-process)
    uint32_t tid;       // (uint32_t)(uintptr_t)pthread_self() — thread that recorded this
    uint32_t value;     // CREATE/DESTROY: GL texture name; TEXPARAMS: magFilter (e.g. 0x2601 = GL_LINEAR)
    uint8_t kind;        // EventKind
    uint8_t pad[3];
};

constexpr uint32_t kRingCapacity = 1u << 18;  // 262144 entries (~6.3MB on x86_64), power of two
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
    e.seq = seq;
    e.tid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
    e.value = value;
    e.kind = static_cast<uint8_t>(kind);
}

}  // namespace t122
}  // namespace video
}  // namespace irr

#define T122_RECORD_CREATE(ptr, value) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::CREATE, (ptr), (value))
#define T122_RECORD_DESTROY(ptr, value) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::DESTROY, (ptr), (value))
#define T122_RECORD_TEXPARAMS(ptr, value) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::TEXPARAMS, (ptr), (value))
#define T122_RECORD_MATERIAL(matPtr) \
    ::irr::video::t122::t122Record(::irr::video::t122::EventKind::MATERIAL, (matPtr), 0)

#else  // !WILLEQ_T122_TEXPROBE

#define T122_RECORD_CREATE(ptr, value) ((void)0)
#define T122_RECORD_DESTROY(ptr, value) ((void)0)
#define T122_RECORD_TEXPARAMS(ptr, value) ((void)0)
#define T122_RECORD_MATERIAL(matPtr) ((void)0)

#endif  // WILLEQ_T122_TEXPROBE
