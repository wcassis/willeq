#pragma once

// B955 (GitLab #273, 2026-09-30): the `gpu_buffers` ledger row -- the GPU
// buffer memory (textures, vertex/index buffers, render targets) the process
// holds OUTSIDE its RssAnon.
//
// Why it exists. On the Orange Pi the GPU is a Mali-400 driven by Lima, and
// every texture/VBO/render target is a Lima GEM object: shmem-backed pages the
// kernel allocates on our behalf. They are NOT in RssAnon, NOT in RssShmem
// (the process maps only some of them, and those mappings are VM_PFNMAP, so
// they are not counted as resident) and NOT in the ledger's gpu_driver
// category (that is the driver's CPU-side heap). B952 measured 200 impostor
// textures move only the SYSTEM-wide Shmem line. Will's rule: 100 % of the
// memory, on the Pi, driver/DRM mappings included -- a byte the ledger cannot
// see is a defect. This row is where those bytes are seen.
//
// Sources, measured on the Pi (kernel 6.12.58-current-sunxi, Mesa 25.2.8
// LLVM-less prefix) -- refactor/B955/README.md has the table:
//   1. DRM fdinfo (`drm-total-*` / `drm-resident-*`): the kernel's own
//      per-client figure. Lima on 6.12 publishes NO drm-* key at all, so this
//      source is read but never used on the Pi today; it is first in line so
//      a kernel that adds it wins automatically.
//   2. The process's own memory cgroup, when the process is ALONE in it
//      (`systemd-run --user --scope`, the Pi rigs): memory.stat `shmem`. Lima
//      pins a BO's pages from its GEM_CREATE ioctl, in process context, so
//      they are charged to our cgroup. Measured equal to the system Shmem
//      delta within 0.5 % (kmscube 1,810,432 B vs 1,772 kB; the qeynos2
//      benchmark 45,182,976 B vs 44,328 kB). It also counts any tmpfs file
//      the process itself writes (a frame dump into /tmp) -- the rigs name
//      those bytes beside it.
//   3. The DRM mappings in /proc/self/maps: the BOs the driver has mapped for
//      CPU access (texture/VBO uploads). A LOWER BOUND: GPU-only buffers
//      (render targets, impostor textures, depth) are never mapped.
//   4. On a software rasteriser (llvmpipe on x86) there is no GPU: the
//      "buffers" are ordinary heap inside the process and the ledger already
//      charges them to gpu_driver. The row then reports the renderer's own
//      logical tally with source `logical_in_gpu_driver`, and is NOT added to
//      owned -- adding it would count the same bytes twice.
//
// The renderer's logical tally (the driver's gpuTextureBytes + vboEboBytes,
// i.e. what fable believes it uploaded) is printed beside the row so the two
// reconcile and the residual has a name (page rounding per BO, mip storage,
// render targets and the driver's own internal BOs).
//
// Cost: one small read of /proc/self/fdinfo per DRM fd, /proc/self/cgroup,
// one memory.stat, and a streamed read of /proc/self/maps (one line per
// mapping -- ~7,600 on the Pi in qeynos2) through a stack buffer, so nothing
// scales with the mapping count. Lifecycle-point cost, never per frame.

#include <cstddef>
#include <cstdint>
#include <string>

namespace eqt {
namespace memory {

// ---- The renderer's logical tally (published, read at every point) --------
// The driver's own counters iterate its texture/buffer containers, so they
// are read on the thread that owns the GL context (the loading thread at its
// points, the render thread about once a second) and PUBLISHED here; the
// ledger points (some of them on the game thread) read the last published
// value. Relaxed atomics; a point reads a value at most ~1 s old.
void publishGpuLogicalTally(uint64_t textureBytes, uint64_t bufferBytes);
// Whether the GL renderer is a software rasteriser (GL_RENDERER names
// llvmpipe/softpipe/swrast), published once by the renderer after context
// creation. Unpublished: inferred (no DRM fd open = software).
void publishGpuSoftwareRasteriser(bool software);

// ---- Parsed sources (pure parsers are fixture-testable) --------------------

struct DrmFdinfo {
    bool isDrm = false;           // a drm-driver: or drm-client-id: key was present
    bool hasMemoryKeys = false;   // any drm-total-/drm-resident-/drm-memory- key
    uint64_t clientId = 0;        // drm-client-id (dedupe: several fds, one client)
    uint64_t totalBytes = 0;      // sum over regions of drm-total-* (else drm-memory-*)
    uint64_t residentBytes = 0;   // sum over regions of drm-resident-*
};
// Parse one /proc/<pid>/fdinfo/<fd> text. Units: a bare number is bytes, a
// "KiB"/"MiB" suffix scales (Documentation/gpu/drm-usage-stats.rst).
DrmFdinfo parseDrmFdinfo(const std::string& text);

struct DrmMaps {
    uint32_t mappings = 0;        // mappings whose path is /dev/dri/*
    uint64_t bytes = 0;           // sum of their sizes (end - start)
};
// Fold one /proc/<pid>/maps line into `acc`. Lines of other paths are ignored.
// The live read streams /proc/self/maps through it one line at a time.
void accumulateDrmMapsLine(const char* line, size_t len, DrmMaps& acc);

struct MemcgStat {
    bool ok = false;
    uint64_t shmemBytes = 0;      // memory.stat shmem
    uint64_t kernelBytes = 0;     // memory.stat kernel (slab, page tables, ...)
    uint64_t anonBytes = 0;       // memory.stat anon
};
MemcgStat parseMemcgStat(const std::string& text);
// "0::/user.slice/.../x.scope" -> "/user.slice/.../x.scope" (cgroup v2 line);
// empty if the text has no v2 line.
std::string parseCgroupV2Path(const std::string& text);

// ---- The census and the row -------------------------------------------------

// Which source the row was taken from. Stable tokens: the log lines, the JSON
// and the rigs key on them.
enum class GpuBufferSource : uint8_t {
    None = 0,
    DrmFdinfo,            // kernel per-client DRM memory stats
    MemcgShmem,           // exclusive memory cgroup's shmem
    DrmMapsLowerBound,    // CPU-mapped DRM BOs only (GPU-only buffers unseen)
    LogicalInGpuDriver,   // software rasteriser: the tally, already in gpu_driver
    Logical,              // the tally alone (no kernel source readable)
};
const char* gpuBufferSourceName(GpuBufferSource s);

struct GpuBufferInputs {
    // fdinfo, already deduplicated by client id
    uint32_t drmFds = 0;
    bool fdinfoMemoryKeys = false;
    uint64_t fdinfoTotalBytes = 0;
    uint64_t fdinfoResidentBytes = 0;
    DrmMaps maps;
    MemcgStat memcg;
    uint32_t cgroupProcs = 0;     // processes in our cgroup (1 = exclusive)
    uint64_t logicalTextureBytes = 0;
    uint64_t logicalBufferBytes = 0;
    bool logicalPublished = false;
    int softwareRasteriser = -1;  // -1 unpublished, 0 no, 1 yes
};

struct GpuBufferCensus {
    GpuBufferInputs in;
    GpuBufferSource source = GpuBufferSource::None;
    uint64_t gpuBuffersBytes = 0;   // THE row
    bool outsideRssAnon = false;    // true: add to owned; false: already in gpu_driver
    bool software = false;          // the decision's software-rasteriser verdict
    uint64_t logicalBytes = 0;      // textures + buffers
    int64_t residualBytes = 0;      // gpuBuffersBytes - logicalBytes (the named gap)
    bool cgroupExclusive = false;
};

// The decision, pure: fdinfo memory keys > exclusive memcg shmem > DRM maps
// (lower bound) > the logical tally (in gpu_driver on a software rasteriser).
GpuBufferCensus decideGpuBuffers(const GpuBufferInputs& in);

// Read every source for THIS process now and decide. Linux only; elsewhere
// the logical tally alone.
GpuBufferCensus readGpuBufferCensus();

// `[memgpu] point=<p> zone=<z> gpu_buffers=<B> source=<tok> outside_rss_anon=<0|1>
//  logical=<B> logical_textures=<B> logical_buffers=<B> residual=<B>
//  memcg_shmem=<B> memcg_kernel=<B> cgroup_procs=<n> drm_mapped=<B>
//  drm_maps=<n> drm_fds=<n> fdinfo_total=<B> fdinfo_resident=<B> owned=<B>
//  owned_plus_gpu_buffers=<B>`
std::string gpuBuffersToLogLine(const std::string& point, const std::string& zone,
                                const GpuBufferCensus& c, uint64_t ownedBytes);
// JSON object (no trailing newline), the same fields.
std::string gpuBuffersToJson(const GpuBufferCensus& c, uint64_t ownedBytes);

// owned + the row when it lives outside RssAnon; owned when it does not.
inline uint64_t ownedPlusGpuBuffers(uint64_t ownedBytes, const GpuBufferCensus& c) {
    return ownedBytes + (c.outsideRssAnon ? c.gpuBuffersBytes : 0);
}

} // namespace memory
} // namespace eqt
