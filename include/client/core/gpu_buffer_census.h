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
//
// B962: textureBytes is the textures' STORAGE (COGLES2Texture::getStorageBytes()
// -- what the upload really sent, not the budget figure the gate reads as
// gpuTextureBytes), and depthStencilBytes the window surface's depth/stencil
// buffer (width x height x its bytes per pixel), a buffer the client asked for
// at device creation that no texture or VBO tally contains.
void publishGpuLogicalTally(uint64_t textureBytes, uint64_t bufferBytes,
                            uint64_t depthStencilBytes = 0);
// B962: the window surface's depth/stencil buffer, from the default
// framebuffer's GL_DEPTH_BITS/GL_STENCIL_BITS: a 24/8 (or 24/0, 32) buffer is 4
// bytes a pixel, a 16-bit one 2 (Lima stores D24S8 as one 32-bit word, Z16 as
// 16). The tile/page rounding the driver adds stays in the residual.
inline uint64_t depthStencilBytesFor(uint32_t width, uint32_t height, int depthBits, int stencilBits) {
    const int bits = (depthBits > 0 ? depthBits : 0) + (stencilBits > 0 ? stencilBits : 0);
    const uint64_t bpp = bits == 0 ? 0 : (bits <= 16 ? 2 : 4);
    return static_cast<uint64_t>(width) * height * bpp;
}

// B961 (GitLab #279): the scanout framebuffers' bytes, the client-side
// source. The GBM colour buffers live on the DISPLAY device (CMA on the
// Orange Pi; imported into Lima by PRIME) -- not shmem, not in RssAnon,
// not in the process's memcg -- so the gpu_buffers row and every
// per-process kernel source are blind to them (the rigs read them as root
// from /sys/kernel/debug/dri/display-engine/framebuffer). The DRM device
// knows every gbm_bo it has locked (stride x height each, see
// docker/irrlicht-drm/scanout_buffer_tally.h) and publishes the tally;
// these read it.
//
// Resolution: the B955 PUBLISH IDIOM (publishGpuLogicalTally /
// publishGpuSoftwareRasteriser) -- a STRONG device->core reference, the only
// direction that binds across the two static archives (libwilleq_core.a is
// linked before libIrrlicht.a). Both device forks
// (docker/irrlicht-drm/CIrrDeviceFB.cpp, docker/irrlicht-drm-x86/
// CIrrDeviceFB.cpp) call publishScanoutTally() from their scanout
// register/unregister callbacks (as the surface's buffers are locked and
// freed) and with {0, 0} from the destructor. The core's read is a pure
// atomic load of the pair the device last published -- never a call into
// the device. (First draft: a weak DEFINITION of the getter -- the linker
// resolved it to the core's own {0,0} body before it ever looked in the
// later libIrrlicht.a; second draft: a WEAKREF, which also did not bind
// across the archives. Both are dead ends with static archives in this
// order; the publish direction works.)
struct ScanoutTallyRead {
    uint32_t bufs = 0;
    uint64_t bytes = 0;
};
void publishScanoutTally(uint32_t bufs, uint64_t bytes);
// Reads the published tally. Unpublished (no device, or after its
// destructor): {0, 0} -- the true answer of "no GBM buffers in this
// process" (tests and narrow tools never link the device at all).
//
// WHY NOT A WEAK SYMBOL (both forms were measured in the willeq binary):
// a weak DEFINITION of a reader in this TU won -- the linker bound the
// reference to the local {0,0} body while extracting the core archive and
// never reconsidered it against the device's strong definition in the later
// libIrrlicht.a; a weak REFERENCE (declaration only) stayed weak-undefined
// (final binary: `w`, null at runtime) for the same archive-order reason.
// Only a strong reference the DEVICE makes into the CORE resolves.
ScanoutTallyRead readScanoutTally();

// Whether the GL renderer is a software rasteriser (GL_RENDERER names
// llvmpipe/softpipe/swrast), published once by the renderer after context
// creation. Unpublished: inferred (no DRM fd open = software).
void publishGpuSoftwareRasteriser(bool software);

// ---- Parsed sources (pure parsers are fixture-testable) --------------------

struct DrmFdinfo {
    bool isDrm = false;           // a drm-driver: or drm-client-id: key was present
    bool hasMemoryKeys = false;   // any drm-total-/drm-resident-/drm-memory- key
    bool hasResidentKeys = false; // a drm-resident-* key was present (B1032)
    bool totalIsLegacyResident = false;  // totalBytes came from the legacy
                                  // drm-memory-* keys (no drm-total-* key), which ARE
                                  // the resident size; false = drm-total-*, which is not
    uint64_t clientId = 0;        // drm-client-id (dedupe: several fds, one client)
    uint64_t totalBytes = 0;      // sum over regions of drm-total-* (else drm-memory-*)
    uint64_t residentBytes = 0;   // sum over regions of drm-resident-*
    uint64_t purgeableBytes = 0;  // sum over regions of drm-purgeable-* (reported only)
};
// Which fdinfo figure is the row (B1032, #352): drm-resident-* -- the buffers
// whose backing store is instantiated. drm-total-* counts every created
// buffer whether or not it has pages (Documentation/gpu/drm-usage-stats.rst),
// and on Panfrost that is the unfaulted part of Mesa's 128 MiB growable tiler
// heap (pages attached only on GPU fault): 175 MB total against 39 MB
// resident on the ROCK 4D. It is NOT the Mesa BO cache -- drm-purgeable-* was
// 64-512 KiB there (review/B1032/README.md). The legacy drm-memory-<region>
// key IS the resident figure (the same document), so a kernel that publishes
// only that one still yields the right row (totalIsLegacyResident). A modern
// kernel that prints drm-total-* with NO drm-resident-* key (it omits the
// resident key when no object reports a resident status -- lane 3's kernel
// 7.1 prints `drm-total-memory: 0` that way) gives no resident figure at all,
// and its total is never used as one.
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
    bool ok = false;             // a shmem line was present
    uint64_t shmemBytes = 0;     // memory.stat shmem
    bool kernelSeen = false;     // B961: a kernel line was present
    uint64_t kernelBytes = 0;    // memory.stat kernel (slab, page tables, ...)
    uint64_t anonBytes = 0;      // memory.stat anon
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
    bool fdinfoResidentKeys = false;   // some fd published drm-resident-* (B1032)
    bool fdinfoLegacyKeys = false;     // some fd published only legacy drm-memory-*
    uint64_t fdinfoTotalBytes = 0;
    uint64_t fdinfoResidentBytes = 0;
    uint64_t fdinfoLegacyResidentBytes = 0;  // those fds' drm-memory-* sum (resident)
    uint64_t fdinfoPurgeableBytes = 0;
    DrmMaps maps;
    MemcgStat memcg;
    uint32_t cgroupProcs = 0;     // processes in our cgroup (1 = exclusive)
    uint64_t logicalTextureBytes = 0;
    uint64_t logicalBufferBytes = 0;
    uint64_t logicalDepthStencilBytes = 0;  // B962
    bool logicalPublished = false;
    int softwareRasteriser = -1;  // -1 unpublished, 0 no, 1 yes
    // B961: the display device's GBM scanout buffers (the device's own
    // tally, readScanoutTally()) -- a separate byte class from gpu_buffers
    // (CMA on the display device, not shmem) and from memcg.kernel.
    uint32_t scanoutFbBufs = 0;
    uint64_t scanoutFbBytes = 0;
};

// Fold one fd's parsed fdinfo into `in` the way readGpuBufferCensus() does for
// every /dev/dri fd: an fd with no memory key is skipped, a repeated
// drm-client-id (dup'd fd, card + render node of one client) is counted once,
// the key flags are OR'ed over fds and the byte figures summed. `drmFds` is
// the caller's (it counts every DRM fd, memory keys or not).
struct DrmFdinfoSeen {
    uint64_t clientIds[16] = {};
    size_t n = 0;
};
void foldDrmFdinfo(const DrmFdinfo& f, DrmFdinfoSeen& seen, GpuBufferInputs& in);

struct GpuBufferCensus {
    GpuBufferInputs in;
    GpuBufferSource source = GpuBufferSource::None;
    uint64_t gpuBuffersBytes = 0;   // THE row
    bool outsideRssAnon = false;    // true: add to owned; false: already in gpu_driver
    bool software = false;          // the decision's software-rasteriser verdict
    uint64_t logicalBytes = 0;      // textures + buffers + depth/stencil (B962)
    int64_t residualBytes = 0;      // gpuBuffersBytes - logicalBytes (the named gap)
    bool cgroupExclusive = false;
};

// The decision, pure: fdinfo RESIDENT figure (drm-resident-* or the legacy
// drm-memory-*) > exclusive memcg shmem > DRM maps (lower bound) > the logical
// tally (in gpu_driver on a software rasteriser). An fdinfo with only
// drm-total-* keys has no resident figure, so it falls through to the next
// source rather than reporting 0 or its total (B1032 review).
GpuBufferCensus decideGpuBuffers(const GpuBufferInputs& in);

// Read every source for THIS process now and decide. Linux only; elsewhere
// the logical tally alone.
GpuBufferCensus readGpuBufferCensus();

// B1056: whether /proc/<pid>/fdinfo of a DRM fd on `devPath` (/dev/dri/<node>)
// may be read. False for the IMG PowerVR module (pvrsrvkm: pvr_show_fdinfo
// panics the kernel, lane 4), false when the driver cannot be resolved through
// /sys/class/drm/<node>/device/driver (fails closed), and false whenever
// WILLEQ_GPU_CENSUS_NO_FDINFO is set (read per call).
// scripts/pi/proc_mem_snapshot.py mirrors it for out-of-process readers.
bool drmFdinfoIsSafe(const char* devPath);

// `[memgpu] point=<p> zone=<z> gpu_buffers=<B> source=<tok> outside_rss_anon=<0|1>
//  logical=<B> logical_textures=<B> logical_buffers=<B> residual=<B>
//  ... (B962, appended last so every existing parse holds:)
//  logical_depth_stencil=<B>
//  memcg_shmem=<B> memcg_kernel=<B> cgroup_procs=<n> drm_mapped=<B>
//  drm_maps=<n> drm_fds=<n> fdinfo_total=<B> fdinfo_resident=<B>
//  fdinfo_purgeable=<B>
//  ... (B961, inserted after fdinfo_purgeable= and before the B978 owned
//  fields, which must stay last: a pre-B961 parser of the line still finds
//  every field it knew:)
//  scanout_fb=<B> scanout_fb_bufs=<n> memcg_kernel_seen=<0|1>
//  owned_rss_anon=<B> owned=<B>`
// B961: the two byte classes on no ledger row (refactor/B961/README.md): the
// display device's GBM scanout buffers (CMA; the rigs' root read is the
// cross-check) and the process cgroup's memory.stat kernel. Neither is added
// to owned -- the counting question is Will's ruling.
// B978 (Will, 2026-10-01: GPU buffers count toward the 64 MB target):
// owned_rss_anon is RssAnon - gpu_driver and owned is the TARGET figure,
// owned_rss_anon + gpu_buffers when the row is outside RssAnon. Before B978
// the line carried `owned=<RssAnon-gpu_driver> owned_plus_gpu_buffers=<B>`;
// the Pi summarizers read both shapes (the presence of owned_rss_anon= tells
// them apart).
std::string gpuBuffersToLogLine(const std::string& point, const std::string& zone,
                                const GpuBufferCensus& c, uint64_t ownedRssAnonBytes);
// JSON object (no trailing newline), the same fields.
std::string gpuBuffersToJson(const GpuBufferCensus& c, uint64_t ownedRssAnonBytes);

// B978: owned = (RssAnon - gpu_driver) + the row when it lives outside RssAnon;
// on a software rasteriser the row is already inside gpu_driver, nothing added.
inline uint64_t ownedPlusGpuBuffers(uint64_t ownedRssAnonBytes, const GpuBufferCensus& c) {
    return ownedRssAnonBytes + (c.outsideRssAnon ? c.gpuBuffersBytes : 0);
}

// B961 (GitLab #279): the two byte classes on no ledger row. Neither counts
// toward any figure this header defines -- they are REPORTED, and whether they
// enter the owned target is Will's ruling (refactor/B961/README.md, "left open
// for Will"). Both ride on the [memgpu] line and the ledger JSON as their own
// rows beside gpu_buffers:
//   scanout_fb_bytes  -- the device's GBM colour buffers: CMA on the DISPLAY
//                       device (the rigs' root read
//                       /sys/kernel/debug/dri/display-engine/framebuffer is the
//                       cross-check; 2 x 3,686,400 B = 7.03 MiB at 1280x720).
//                       Not shmem, not RssAnon, not in the process memcg.
//   memcg_kernel_bytes -- the process's own cgroup memory.stat kernel (slab,
//                       page tables, kernel stacks; 1.11-1.93 MiB at
//                       steady_60s in the issue's runs). Charged to the
//                       process cgroup, but it is not in RssAnon (kernel
//                       memory is not a process's kernel) and never was in any
//                       owned figure; this row names it.
// Both are 0 when their source does not exist in the process (no GBM device
// or no cgroup v2 memory controller).
struct B961Rows {
    uint32_t scanoutFbBufs = 0;
    uint64_t scanoutFbBytes = 0;
    uint64_t memcgKernelBytes = 0;
    bool memcgKernelAvailable = false;  // memory.stat had a kernel= line
};
// Derive the rows from a census (its inputs carry both sources). Pure, so the
// log line, the JSON and the tests all name one place.
inline B961Rows b961Rows(const GpuBufferCensus& c) {
    B961Rows r;
    r.scanoutFbBufs = c.in.scanoutFbBufs;
    r.scanoutFbBytes = c.in.scanoutFbBytes;
    r.memcgKernelBytes = c.in.memcg.kernelBytes;
    r.memcgKernelAvailable = c.in.memcg.kernelSeen;
    return r;
}

} // namespace memory
} // namespace eqt
