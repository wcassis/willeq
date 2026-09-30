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
std::atomic<bool> g_logicalPublished{false};
std::atomic<int> g_software{-1};

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

void publishGpuLogicalTally(uint64_t textureBytes, uint64_t bufferBytes) {
    g_logicalTextures.store(textureBytes, std::memory_order_relaxed);
    g_logicalBuffers.store(bufferBytes, std::memory_order_relaxed);
    g_logicalPublished.store(true, std::memory_order_release);
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
            f.residentBytes += parseScaled(val);
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
            f.totalBytes += parseScaled(line.c_str() + colon + 1);
        }
    }
    return f;
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
        else if (key == "kernel") m.kernelBytes = v;
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
    c.logicalBytes = in.logicalTextureBytes + in.logicalBufferBytes;
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
    } else if (in.fdinfoMemoryKeys) {
        c.source = GpuBufferSource::DrmFdinfo;
        c.gpuBuffersBytes = in.fdinfoTotalBytes;
        c.outsideRssAnon = true;
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
    in.softwareRasteriser = g_software.load(std::memory_order_relaxed);
#if defined(__linux__)
    // DRM fds: one client may hold several fds (dup'd, or card + render
    // node); dedupe the memory keys by drm-client-id.
    if (DIR* d = opendir("/proc/self/fd")) {
        uint64_t seen[16];
        size_t nSeen = 0;
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            char link[320], target[256];
            snprintf(link, sizeof(link), "/proc/self/fd/%s", e->d_name);
            const ssize_t n = readlink(link, target, sizeof(target) - 1);
            if (n <= 0) continue;
            target[n] = '\0';
            if (strncmp(target, "/dev/dri/", 9) != 0) continue;
            ++in.drmFds;
            char info[320];
            snprintf(info, sizeof(info), "/proc/self/fdinfo/%s", e->d_name);
            const DrmFdinfo f = parseDrmFdinfo(readSmallFile(info));
            if (!f.hasMemoryKeys) continue;
            bool dup = false;
            for (size_t i = 0; i < nSeen; ++i) dup = dup || seen[i] == f.clientId;
            if (dup) continue;
            if (nSeen < 16) seen[nSeen++] = f.clientId;
            in.fdinfoMemoryKeys = true;
            in.fdinfoTotalBytes += f.totalBytes;
            in.fdinfoResidentBytes += f.residentBytes;
        }
        closedir(d);
    }
    in.maps = readDrmMaps();
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
                                const GpuBufferCensus& c, uint64_t ownedBytes) {
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
    appendKV(out, "owned", ownedBytes);
    appendKV(out, "owned_plus_gpu_buffers", ownedPlusGpuBuffers(ownedBytes, c));
    return out;
}

std::string gpuBuffersToJson(const GpuBufferCensus& c, uint64_t ownedBytes) {
    std::string out = "{";
    out += "\"gpuBuffersBytes\":" + std::to_string(c.gpuBuffersBytes);
    out += ",\"source\":\"" + std::string(gpuBufferSourceName(c.source)) + "\"";
    out += std::string(",\"outsideRssAnon\":") + (c.outsideRssAnon ? "true" : "false");
    out += ",\"logicalBytes\":" + std::to_string(c.logicalBytes);
    out += ",\"logicalTextureBytes\":" + std::to_string(c.in.logicalTextureBytes);
    out += ",\"logicalBufferBytes\":" + std::to_string(c.in.logicalBufferBytes);
    out += ",\"residualBytes\":" + std::to_string(c.residualBytes);
    out += ",\"memcgShmemBytes\":" + std::to_string(c.in.memcg.shmemBytes);
    out += ",\"memcgKernelBytes\":" + std::to_string(c.in.memcg.kernelBytes);
    out += ",\"cgroupProcs\":" + std::to_string(c.in.cgroupProcs);
    out += ",\"drmMappedBytes\":" + std::to_string(c.in.maps.bytes);
    out += ",\"drmMappings\":" + std::to_string(c.in.maps.mappings);
    out += ",\"drmFds\":" + std::to_string(c.in.drmFds);
    out += ",\"fdinfoTotalBytes\":" + std::to_string(c.in.fdinfoTotalBytes);
    out += ",\"fdinfoResidentBytes\":" + std::to_string(c.in.fdinfoResidentBytes);
    out += ",\"ownedPlusGpuBuffersBytes\":" + std::to_string(ownedPlusGpuBuffers(ownedBytes, c));
    out += "}";
    return out;
}

} // namespace memory
} // namespace eqt
