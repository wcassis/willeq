// B955 (GitLab #273): the gpu_buffers row -- see
// include/client/core/gpu_buffer_census.h for the sources and the decision.
// This TU must not be reached from inside an allocator hook: it allocates
// (small std::strings) and reads /proc. It is called at lifecycle points only.

#include "client/core/gpu_buffer_census.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace eqt {
namespace memory {

namespace {

std::atomic<uint64_t> g_logicalTextures{0};
std::atomic<uint64_t> g_logicalBuffers{0};
std::atomic<uint64_t> g_logicalDepthStencil{0};
std::atomic<bool> g_logicalPublished{false};
std::atomic<int> g_software{-1};

// B961 (GitLab #279): the device's scanout-tally pair, published by the
// device (both forks) on every change (buffer locked / freed) and reset to
// {0, 0} at teardown.
std::atomic<uint32_t> g_scanoutBufs{0};
std::atomic<uint64_t> g_scanoutBytes{0};

// Parse "<number> [KiB|MiB|GiB|kB]" after a key's colon.
uint64_t parseScaled(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    char* end = nullptr;
    const unsigned long long v = strtoull(p, &end, 10);
    if (end == p) return 0;
    while (*end == ' ' || *end == '\t') ++end;
    uint64_t scale = 1;
    if (!strncmp(end, "KiB", 3) || !strncmp(end, "kB", 2)) scale = 1024ull;
    else if (!strncmp(end, "MiB", 3)) scale = 1024ull * 1024;
    else if (!strncmp(end, "GiB", 3)) scale = 1024ull * 1024 * 1024;
    return static_cast<uint64_t>(v) * scale;
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, strlen(prefix), prefix) == 0;
}

void appendKV(std::string& out, const char* key, uint64_t v) {
    out += ' ';
    out += key;
    out += '=';
    out += std::to_string(v);
}

void appendKVs(std::string& out, const char* key, int64_t v) {
    out += ' ';
    out += key;
    out += '=';
    out += std::to_string(v);
}

#if defined(__linux__)
// Read a small /proc or /sys file into a string (fdinfo, memory.stat, cgroup).
std::string readSmallFile(const char* path) {
    std::string out;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) out.append(buf, static_cast<size_t>(n));
    close(fd);
    return out;
}

// Stream /proc/self/maps through a stack buffer, one line at a time.
DrmMaps readDrmMaps() {
    DrmMaps acc;
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return acc;
    char buf[8192];
    size_t have = 0;
    for (;;) {
        const ssize_t n = read(fd, buf + have, sizeof(buf) - have);
        if (n <= 0) break;
        have += static_cast<size_t>(n);
        size_t start = 0;
        for (size_t i = 0; i < have; ++i) {
            if (buf[i] == '\n') {
                accumulateDrmMapsLine(buf + start, i - start, acc);
                start = i + 1;
            }
        }
        // keep the partial tail; a line longer than the buffer is dropped
        if (start == 0 && have == sizeof(buf)) {
            have = 0;
            continue;
        }
        memmove(buf, buf + start, have - start);
        have -= start;
    }
    if (have) accumulateDrmMapsLine(buf, have, acc);
    close(fd);
    return acc;
}

uint32_t countLines(const std::string& text) {
    uint32_t n = 0;
    for (char ch : text) n += (ch == '\n');
    if (!text.empty() && text.back() != '\n') ++n;
    return n;
}
#endif

} // namespace

// B1056: the DRM fdinfo read is a kernel call into the driver's show_fdinfo.
// The IMG PowerVR DDK module (pvrsrvkm 24.2, lane 4 / Radxa A7S) dereferences
// NULL in pvr_show_fdinfo on that read and panics the kernel (trace in
// review/B1056/lane4_attempts/bench60_local4.dmesg), so that driver is never
// asked. The guard FAILS CLOSED: an fd whose driver cannot be resolved through
// sysfs is not read either (a panic is the cost of a wrong "yes").
// WILLEQ_GPU_CENSUS_NO_FDINFO=1 skips the read for every driver; it is read
// per call so a rig (or a test) can set it after startup.
bool drmFdinfoIsSafe(const char* devPath) {
    const char* v = std::getenv("WILLEQ_GPU_CENSUS_NO_FDINFO");
    if (v && *v && *v != '0') return false;
    if (!devPath) return false;
    const char* node = strrchr(devPath, '/');
    if (!node || !*(node + 1)) return false;
    char link[320], target[320];
    snprintf(link, sizeof(link), "/sys/class/drm/%s/device/driver", node + 1);
    const ssize_t n = readlink(link, target, sizeof(target) - 1);
    if (n <= 0) return false;  // unknown driver: not worth a kernel panic
    target[n] = '\0';
    const char* drv = strrchr(target, '/');
    drv = drv ? drv + 1 : target;
    return *drv != '\0' && strcmp(drv, "pvrsrvkm") != 0;
}

void publishGpuLogicalTally(uint64_t textureBytes, uint64_t bufferBytes, uint64_t depthStencilBytes) {
    g_logicalTextures.store(textureBytes, std::memory_order_relaxed);
    g_logicalBuffers.store(bufferBytes, std::memory_order_relaxed);
    g_logicalDepthStencil.store(depthStencilBytes, std::memory_order_relaxed);
    g_logicalPublished.store(true, std::memory_order_release);
}

// B961 (GitLab #279): the publish side of the scanout tally. The device
// (both forks) calls this STRONG core function on every tally change (a
// buffer's first lock, its free) and with {0, 0} at teardown. A strong
// device->core reference is the only symbol direction that binds across the
// two static archives (libwilleq_core.a ... libIrrlicht.a): a weak DEFINITION
// of a reader in this TU was measured to win over the device's strong one in
// the willeq binary, and a weak REFERENCE stayed weak-undefined
// (include/client/core/gpu_buffer_census.h, "WHY NOT A WEAK SYMBOL"). Same
// shape as publishGpuLogicalTally / publishGpuSoftwareRasteriser.
void publishScanoutTally(uint32_t bufs, uint64_t bytes) {
    g_scanoutBufs.store(bufs, std::memory_order_relaxed);
    g_scanoutBytes.store(bytes, std::memory_order_relaxed);
}

ScanoutTallyRead readScanoutTally() {
    ScanoutTallyRead r;
    r.bufs = g_scanoutBufs.load(std::memory_order_relaxed);
    r.bytes = g_scanoutBytes.load(std::memory_order_relaxed);
    return r;
}

void publishGpuSoftwareRasteriser(bool software) {
    g_software.store(software ? 1 : 0, std::memory_order_relaxed);
}

const char* gpuBufferSourceName(GpuBufferSource s) {
    switch (s) {
        case GpuBufferSource::DrmFdinfo: return "drm_fdinfo";
        case GpuBufferSource::MemcgShmem: return "memcg_shmem";
        case GpuBufferSource::DrmMapsLowerBound: return "drm_maps_lower_bound";
        case GpuBufferSource::LogicalInGpuDriver: return "logical_in_gpu_driver";
        case GpuBufferSource::Logical: return "logical";
        case GpuBufferSource::None: break;
    }
    return "none";
}

DrmFdinfo parseDrmFdinfo(const std::string& text) {
    DrmFdinfo f;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (!startsWith(line, "drm-")) continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const char* val = line.c_str() + colon + 1;
        if (startsWith(line, "drm-driver:") || startsWith(line, "drm-pdev:")) {
            f.isDrm = true;
        } else if (startsWith(line, "drm-client-id:")) {
            f.isDrm = true;
            f.clientId = strtoull(val, nullptr, 10);
        } else if (startsWith(line, "drm-total-")) {
            f.hasMemoryKeys = true;
            f.totalBytes += parseScaled(val);
        } else if (startsWith(line, "drm-resident-")) {
            f.hasMemoryKeys = true;
            f.hasResidentKeys = true;
            f.residentBytes += parseScaled(val);
        } else if (startsWith(line, "drm-purgeable-")) {
            f.purgeableBytes += parseScaled(val);
        }
    }
    // The pre-6.5 spelling (drm-memory-<region>) carries the resident size
    // only; use it as the total when no drm-total-* key exists.
    if (!f.totalBytes) {
        pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) nl = text.size();
            const std::string line = text.substr(pos, nl - pos);
            pos = nl + 1;
            if (!startsWith(line, "drm-memory-")) continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            f.hasMemoryKeys = true;
            f.totalIsLegacyResident = true;
            f.totalBytes += parseScaled(line.c_str() + colon + 1);
        }
    }
    return f;
}

void foldDrmFdinfo(const DrmFdinfo& f, DrmFdinfoSeen& seen, GpuBufferInputs& in) {
    if (!f.hasMemoryKeys) return;
    for (size_t i = 0; i < seen.n; ++i)
        if (seen.clientIds[i] == f.clientId) return;
    if (seen.n < sizeof(seen.clientIds) / sizeof(seen.clientIds[0])) seen.clientIds[seen.n++] = f.clientId;
    in.fdinfoMemoryKeys = true;
    in.fdinfoResidentKeys = in.fdinfoResidentKeys || f.hasResidentKeys;
    in.fdinfoTotalBytes += f.totalBytes;
    in.fdinfoResidentBytes += f.residentBytes;
    in.fdinfoPurgeableBytes += f.purgeableBytes;
    if (f.totalIsLegacyResident) {
        in.fdinfoLegacyKeys = true;
        in.fdinfoLegacyResidentBytes += f.totalBytes;
    }
}

void accumulateDrmMapsLine(const char* line, size_t len, DrmMaps& acc) {
    // "start-end perms offset dev inode   path"
    static const char kDri[] = "/dev/dri/";
    const size_t kDriLen = sizeof(kDri) - 1;
    size_t p = 0;
    while (p < len && line[p] != '/') ++p;   // the path is the first '/'
    if (p + kDriLen > len || memcmp(line + p, kDri, kDriLen) != 0) return;
    char* end = nullptr;
    const unsigned long long lo = strtoull(line, &end, 16);
    if (!end || *end != '-') return;
    const unsigned long long hi = strtoull(end + 1, nullptr, 16);
    if (hi <= lo) return;
    acc.bytes += static_cast<uint64_t>(hi - lo);
    ++acc.mappings;
}

MemcgStat parseMemcgStat(const std::string& text) {
    MemcgStat m;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        const std::string key = line.substr(0, sp);
        const uint64_t v = strtoull(line.c_str() + sp + 1, nullptr, 10);
        if (key == "shmem") { m.shmemBytes = v; m.ok = true; }
        else if (key == "kernel") { m.kernelBytes = v; m.kernelSeen = true; }
        else if (key == "anon") m.anonBytes = v;
    }
    return m;
}

std::string parseCgroupV2Path(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        if (startsWith(line, "0::")) return line.substr(3);
    }
    return std::string();
}

GpuBufferCensus decideGpuBuffers(const GpuBufferInputs& in) {
    GpuBufferCensus c;
    c.in = in;
    c.logicalBytes = in.logicalTextureBytes + in.logicalBufferBytes + in.logicalDepthStencilBytes;
    c.cgroupExclusive = in.memcg.ok && in.cgroupProcs == 1;
    // A software rasteriser has no GPU: published by the renderer, else
    // inferred from the absence of any DRM fd.
    c.software = in.softwareRasteriser >= 0 ? in.softwareRasteriser == 1 : in.drmFds == 0;
    if (c.software) {
        // llvmpipe: the "GPU" buffers are heap in this process, charged to
        // gpu_driver by the ledger's return-address classifier. Never a
        // kernel source: an exclusive cgroup's shmem would be something else.
        c.source = in.logicalPublished ? GpuBufferSource::LogicalInGpuDriver : GpuBufferSource::None;
        c.gpuBuffersBytes = in.logicalPublished ? c.logicalBytes : 0;
        c.outsideRssAnon = false;
    } else if (in.fdinfoResidentKeys || in.fdinfoLegacyKeys) {
        // B1032: the row is the RESIDENT figure. drm-total-* counts buffers
        // with no backing store (Panfrost: the unfaulted part of Mesa's 128 MiB
        // growable tiler heap; NOT the BO cache, purgeable was <= 512 KiB) --
        // 4.5x the resident bytes on the ROCK 4D. The legacy
        // drm-memory-<region> key is the resident size by definition, so a fd
        // that published only that one adds its figure here. A fd that
        // published drm-total-* with no resident key has NO resident figure:
        // its total is never used, and when no fd has one this branch is not
        // taken -- the census falls through to the exclusive cgroup / the
        // mapped lower bound, keeping the documented order a ranking of
        // RESIDENT sources (an fdinfo row of 0 would claim a measurement the
        // kernel did not make).
        const uint64_t resident =
            in.fdinfoResidentBytes + in.fdinfoLegacyResidentBytes;
        c.source = GpuBufferSource::DrmFdinfo;
        c.gpuBuffersBytes = resident;
        c.outsideRssAnon = true;
        // B1035 (#354): the resident figure is a PER-NODE tally. The
        // process's scanout framebuffers are charged to its cgroup's
        // shmem and published by the panfrost render node as
        // drm-shared-memory, yet count as 0 in EVERY node's resident
        // figure -- the rockchip card node publishes no memory key at all
        // (tests/data/b955/rock4d_fdinfo_card0). When the cgroup is ours
        // alone and its shmem exceeds the resident figure, the shmem is
        // the higher measurement of the same bytes (the exclusive memcg is
        // the second-best source in this very ranking); take it, and name
        // the delta the bytes the fdinfo could not see. On the ROCK 4D
        // the delta is the scanout framebuffers within capture noise
        // (shmem 39,841,792 - resident 30,810,112 = 9,031,680 vs
        // scanout_fb 9,437,184: the framebuffers' page rounding and the
        // render node's own shmem-backed objects); on the Lima lanes the
        // shmem never exceeds the resident figure (Lima publishes no
        // key and the cgroup shmem IS the row there), so nothing changes.
        if (c.cgroupExclusive && in.memcg.shmemBytes > resident) {
            c.gpuBuffersBytes = in.memcg.shmemBytes;
            c.fdinfoScanoutGapBytes = in.memcg.shmemBytes - resident;
        }
    } else if (c.cgroupExclusive) {
        c.source = GpuBufferSource::MemcgShmem;
        c.gpuBuffersBytes = in.memcg.shmemBytes;
        c.outsideRssAnon = true;
    } else if (in.maps.mappings) {
        c.source = GpuBufferSource::DrmMapsLowerBound;
        c.gpuBuffersBytes = in.maps.bytes;
        c.outsideRssAnon = true;
    } else if (in.logicalPublished) {
        c.source = GpuBufferSource::Logical;
        c.gpuBuffersBytes = c.logicalBytes;
        c.outsideRssAnon = true;
    }
    c.residualBytes = static_cast<int64_t>(c.gpuBuffersBytes) - static_cast<int64_t>(c.logicalBytes);
    return c;
}

GpuBufferCensus readGpuBufferCensus() {
    GpuBufferInputs in;
    in.logicalPublished = g_logicalPublished.load(std::memory_order_acquire);
    in.logicalTextureBytes = g_logicalTextures.load(std::memory_order_relaxed);
    in.logicalBufferBytes = g_logicalBuffers.load(std::memory_order_relaxed);
    in.logicalDepthStencilBytes = g_logicalDepthStencil.load(std::memory_order_relaxed);
    in.softwareRasteriser = g_software.load(std::memory_order_relaxed);
#if defined(__linux__)
    // DRM fds: one client may hold several fds (dup'd, or card + render
    // node); dedupe the memory keys by drm-client-id.
    if (DIR* d = opendir("/proc/self/fd")) {
        DrmFdinfoSeen seen;
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            char link[320], target[256];
            snprintf(link, sizeof(link), "/proc/self/fd/%s", e->d_name);
            const ssize_t n = readlink(link, target, sizeof(target) - 1);
            if (n <= 0) continue;
            target[n] = '\0';
            if (strncmp(target, "/dev/dri/", 9) != 0) continue;
            ++in.drmFds;
            if (!drmFdinfoIsSafe(target)) continue;
            char info[320];
            snprintf(info, sizeof(info), "/proc/self/fdinfo/%s", e->d_name);
            foldDrmFdinfo(parseDrmFdinfo(readSmallFile(info)), seen, in);
        }
        closedir(d);
    }
    in.maps = readDrmMaps();
    // B961: the display device's GBM scanout tally (the device's own
    // published pair; zero where the process has no GBM device).
    const ScanoutTallyRead scanout = readScanoutTally();
    in.scanoutFbBufs = scanout.bufs;
    in.scanoutFbBytes = scanout.bytes;
    const std::string cg = parseCgroupV2Path(readSmallFile("/proc/self/cgroup"));
    if (!cg.empty()) {
        const std::string base = "/sys/fs/cgroup" + cg;
        in.memcg = parseMemcgStat(readSmallFile((base + "/memory.stat").c_str()));
        if (in.memcg.ok) in.cgroupProcs = countLines(readSmallFile((base + "/cgroup.procs").c_str()));
    }
#endif
    return decideGpuBuffers(in);
}

std::string gpuBuffersToLogLine(const std::string& point, const std::string& zone,
                                const GpuBufferCensus& c, uint64_t ownedRssAnonBytes) {
    std::string out = "[memgpu] point=" + point + " zone=" + (zone.empty() ? "-" : zone);
    appendKV(out, "gpu_buffers", c.gpuBuffersBytes);
    out += " source=";
    out += gpuBufferSourceName(c.source);
    appendKV(out, "outside_rss_anon", c.outsideRssAnon ? 1 : 0);
    appendKV(out, "logical", c.logicalBytes);
    appendKV(out, "logical_textures", c.in.logicalTextureBytes);
    appendKV(out, "logical_buffers", c.in.logicalBufferBytes);
    appendKVs(out, "residual", c.residualBytes);
    appendKV(out, "memcg_shmem", c.in.memcg.shmemBytes);
    appendKV(out, "memcg_kernel", c.in.memcg.kernelBytes);
    appendKV(out, "cgroup_procs", c.in.cgroupProcs);
    appendKV(out, "drm_mapped", c.in.maps.bytes);
    appendKV(out, "drm_maps", c.in.maps.mappings);
    appendKV(out, "drm_fds", c.in.drmFds);
    appendKV(out, "fdinfo_total", c.in.fdinfoTotalBytes);
    appendKV(out, "fdinfo_resident", c.in.fdinfoResidentBytes);
    appendKV(out, "fdinfo_purgeable", c.in.fdinfoPurgeableBytes);
    // B961: the two byte classes on no ledger row, named as their own rows
    // (b961Rows() derives both from the census in one place). Neither is
    // added to owned -- the counting question is Will's
    // (refactor/B961/README.md). Inserted after fdinfo_purgeable, BEFORE the
    // B978 owned fields, which must stay last for the pre-B961 parsers
    // (owned_from_memgpu, rendered_zone_summarize.py).
    const B961Rows b961 = b961Rows(c);
    appendKV(out, "scanout_fb", b961.scanoutFbBytes);
    appendKV(out, "scanout_fb_bufs", b961.scanoutFbBufs);
    appendKV(out, "memcg_kernel_seen", b961.memcgKernelAvailable ? 1 : 0);
    // B1035 (#354): the bytes the fdinfo resident figure could not see
    // (the row took the exclusive cgroup's shmem over it); 0 otherwise.
    // Named before the B978 owned fields, which stay last for the
    // pre-B961 parsers (owned_from_memgpu, rendered_zone_summarize.py).
    appendKV(out, "fdinfo_scanout_gap", c.fdinfoScanoutGapBytes);
    // B978: owned = RssAnon - gpu_driver + gpu_buffers (outside RssAnon only).
    appendKV(out, "owned_rss_anon", ownedRssAnonBytes);
    appendKV(out, "owned", ownedPlusGpuBuffers(ownedRssAnonBytes, c));
    appendKV(out, "logical_depth_stencil", c.in.logicalDepthStencilBytes);  // B962
    return out;
}

std::string gpuBuffersToJson(const GpuBufferCensus& c, uint64_t ownedRssAnonBytes) {
    std::string out = "{";
    out += "\"gpuBuffersBytes\":" + std::to_string(c.gpuBuffersBytes);
    out += ",\"source\":\"" + std::string(gpuBufferSourceName(c.source)) + "\"";
    out += std::string(",\"outsideRssAnon\":") + (c.outsideRssAnon ? "true" : "false");
    out += ",\"logicalBytes\":" + std::to_string(c.logicalBytes);
    out += ",\"logicalTextureBytes\":" + std::to_string(c.in.logicalTextureBytes);
    out += ",\"logicalBufferBytes\":" + std::to_string(c.in.logicalBufferBytes);
    out += ",\"logicalDepthStencilBytes\":" + std::to_string(c.in.logicalDepthStencilBytes);
    out += ",\"residualBytes\":" + std::to_string(c.residualBytes);
    out += ",\"memcgShmemBytes\":" + std::to_string(c.in.memcg.shmemBytes);
    out += ",\"memcgKernelBytes\":" + std::to_string(c.in.memcg.kernelBytes);
    out += ",\"cgroupProcs\":" + std::to_string(c.in.cgroupProcs);
    out += ",\"drmMappedBytes\":" + std::to_string(c.in.maps.bytes);
    out += ",\"drmMappings\":" + std::to_string(c.in.maps.mappings);
    out += ",\"drmFds\":" + std::to_string(c.in.drmFds);
    out += ",\"fdinfoTotalBytes\":" + std::to_string(c.in.fdinfoTotalBytes);
    out += ",\"fdinfoResidentBytes\":" + std::to_string(c.in.fdinfoResidentBytes);
    out += ",\"fdinfoPurgeableBytes\":" + std::to_string(c.in.fdinfoPurgeableBytes);
    // B961: the two unrowed byte classes, through the same b961Rows() the
    // [memgpu] log line uses (one derivation site for both emitters).
    const B961Rows b961 = b961Rows(c);
    out += ",\"scanoutFbBytes\":" + std::to_string(b961.scanoutFbBytes);
    out += ",\"scanoutFbBufs\":" + std::to_string(b961.scanoutFbBufs);
    out += ",\"memcgKernelSeen\":" + std::string(b961.memcgKernelAvailable ? "true" : "false");
    // B1035 (#354): the bytes the fdinfo resident figure could not see
    // (the row took the exclusive cgroup's shmem over it); 0 otherwise.
    out += ",\"fdinfoScanoutGapBytes\":" + std::to_string(c.fdinfoScanoutGapBytes);
    out += ",\"ownedRssAnonBytes\":" + std::to_string(ownedRssAnonBytes);
    out += ",\"ownedBytes\":" + std::to_string(ownedPlusGpuBuffers(ownedRssAnonBytes, c));
    out += "}";
    return out;
}

} // namespace memory
} // namespace eqt
