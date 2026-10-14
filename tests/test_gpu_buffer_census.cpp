// B955 (GitLab #273): the gpu_buffers ledger row -- parsers, the source
// decision, and the row on the [memledger] line / JSON.
//
// The fixtures under tests/data/b955/ were CAPTURED on the Orange Pi
// (kernel 6.12.58-current-sunxi, Lima, Mesa 25.2.8 LLVM-less prefix) from a
// kmscube run inside its own `systemd-run --user --scope`: its
// /proc/<pid>/maps, the render node's fdinfo (no drm-* key: Lima on 6.12
// publishes none), its cgroup's memory.stat and cgroup.procs. The i915-style
// fdinfo below is the documented key set (Documentation/gpu/
// drm-usage-stats.rst), for the branch Lima does not take today.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>

#include "client/core/gpu_buffer_census.h"
#include "client/core/memory_ledger.h"

using namespace eqt::memory;

namespace {

std::string fixture(const char* name) {
    std::ifstream f(std::string(B955_FIXTURE_DIR) + "/" + name);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

const char* kI915Fdinfo =
    "pos:\t0\n"
    "flags:\t02100002\n"
    "mnt_id:\t26\n"
    "ino:\t685\n"
    "drm-driver:\ti915\n"
    "drm-client-id:\t7\n"
    "drm-pdev:\t0000:00:02.0\n"
    "drm-total-system0:\t6148 KiB\n"
    "drm-shared-system0:\t0\n"
    "drm-active-system0:\t0\n"
    "drm-resident-system0:\t6148 KiB\n"
    "drm-purgeable-system0:\t0\n"
    "drm-total-stolen-system0:\t1 MiB\n";

// Fold a whole maps text through the production line parser (the client
// streams /proc/self/maps through the same function one line at a time).
DrmMaps parseDrmMaps(const std::string& text) {
    DrmMaps acc;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) accumulateDrmMapsLine(line.data(), line.size(), acc);
    return acc;
}

} // namespace

TEST(GpuBufferCensus, LimaFdinfoHasNoMemoryKeys) {
    const DrmFdinfo f = parseDrmFdinfo(fixture("kmscube_fdinfo_renderD128"));
    EXPECT_FALSE(f.isDrm);
    EXPECT_FALSE(f.hasMemoryKeys);
    EXPECT_EQ(f.totalBytes, 0u);
}

TEST(GpuBufferCensus, DocumentedFdinfoKeysSumOverRegions) {
    const DrmFdinfo f = parseDrmFdinfo(kI915Fdinfo);
    EXPECT_TRUE(f.isDrm);
    EXPECT_TRUE(f.hasMemoryKeys);
    EXPECT_EQ(f.clientId, 7u);
    EXPECT_EQ(f.totalBytes, 6148u * 1024 + 1024u * 1024);
    EXPECT_EQ(f.residentBytes, 6148u * 1024);
    EXPECT_FALSE(f.totalIsLegacyResident);   // drm-total-*: NOT a resident figure
}

TEST(GpuBufferCensus, LegacyDrmMemoryKeyIsTheTotal) {
    const DrmFdinfo f = parseDrmFdinfo("drm-driver:\tpanfrost\ndrm-memory-vram:\t300 KiB\n");
    EXPECT_TRUE(f.hasMemoryKeys);
    EXPECT_TRUE(f.totalIsLegacyResident);
    EXPECT_EQ(f.totalBytes, 300u * 1024);
}

TEST(GpuBufferCensus, CapturedMapsSumOnlyTheDrmMappings) {
    const DrmMaps m = parseDrmMaps(fixture("kmscube_maps"));
    // The eight /dev/dri/renderD128 mappings of the capture (the libdrm and
    // dri_gbm.so file mappings do not count): 0xf000 + 0xf000 + 0x100000 +
    // 4 x 0x1000 + 0x1000 -- ignoring the paths that merely CONTAIN "dri".
    EXPECT_EQ(m.mappings, 8u);
    EXPECT_EQ(m.bytes, 0xf000u + 0xf000u + 0x100000u + 4u * 0x1000u + 0x1000u);
}

TEST(GpuBufferCensus, MapsLineParserIgnoresOtherPaths) {
    DrmMaps m;
    const std::string a = "b6728000-b6730000 r-xp 00000000 b3:01 259320     /home/user/mesa-nollvm/lib/gbm/dri_gbm.so";
    const std::string b = "b6f9c000-b6f9d000 rw-s 10000000 00:06 454        /dev/dri/renderD128";
    const std::string c = "b6f9c000-b6f9d000 rw-p 00000000 00:00 0 ";
    accumulateDrmMapsLine(a.data(), a.size(), m);
    accumulateDrmMapsLine(b.data(), b.size(), m);
    accumulateDrmMapsLine(c.data(), c.size(), m);
    EXPECT_EQ(m.mappings, 1u);
    EXPECT_EQ(m.bytes, 0x1000u);
}

TEST(GpuBufferCensus, CapturedMemcgStatAndCgroupPath) {
    const MemcgStat s = parseMemcgStat(fixture("kmscube_memory.stat"));
    ASSERT_TRUE(s.ok);
    EXPECT_EQ(s.shmemBytes, 1806336u);
    EXPECT_EQ(s.kernelBytes, 151552u);
    EXPECT_EQ(s.anonBytes, 7507968u);
    EXPECT_EQ(parseCgroupV2Path(fixture("kmscube_cgroup")),
              "/user.slice/user-1000.slice/user@1000.service/app.slice/b955kmsfx.scope");
}

// The captured Pi process, end to end: no fdinfo keys, an exclusive cgroup,
// DRM mappings -> the row is the cgroup's shmem, outside RssAnon, and the
// mapped bytes are reported beside it as the lower bound they are.
TEST(GpuBufferCensus, CapturedPiProcessTakesTheExclusiveMemcg) {
    GpuBufferInputs in;
    in.drmFds = 6;
    in.maps = parseDrmMaps(fixture("kmscube_maps"));
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;
    in.logicalPublished = true;
    in.logicalTextureBytes = 1000000;
    in.logicalBufferBytes = 200000;
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::MemcgShmem);
    EXPECT_STREQ(gpuBufferSourceName(c.source), "memcg_shmem");
    EXPECT_EQ(c.gpuBuffersBytes, 1806336u);
    EXPECT_TRUE(c.outsideRssAnon);
    EXPECT_EQ(c.logicalBytes, 1200000u);
    EXPECT_EQ(c.residualBytes, 1806336 - 1200000);
    EXPECT_EQ(ownedPlusGpuBuffers(40u << 20, c), (40u << 20) + 1806336u);
}

// B978 (Will, 2026-10-01: GPU buffers count toward the 64 MB target): the
// captured Pi process's row flows into the snapshot's owned figure --
// owned = RssAnon - gpu_driver + gpu_buffers -- and into the gap.
TEST(GpuBufferCensus, CapturedPiRowCountsTowardTheTarget) {
    GpuBufferInputs in;
    in.drmFds = 6;
    in.maps = parseDrmMaps(fixture("kmscube_maps"));
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;
    in.softwareRasteriser = 0;
    Snapshot s;
    s.rssAnonBytes = 70u << 20;
    s.gpuDriverBytes = 7u << 20;
    s.gpuBuffers = decideGpuBuffers(in);
    finishTargetGap(s);
    EXPECT_EQ(s.ownedRssAnonBytes, 63u << 20);
    EXPECT_EQ(s.ownedBytes, (63u << 20) + 1806336u);
    EXPECT_EQ(s.overTargetBytes, s.ownedBytes - (64u << 20));   // over only because of the buffers
    EXPECT_GT(s.overTargetBytes, 0u);
}

TEST(GpuBufferCensus, SharedCgroupFallsBackToTheMappedLowerBound) {
    GpuBufferInputs in;
    in.drmFds = 6;
    in.maps = parseDrmMaps(fixture("kmscube_maps"));
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 3;   // a login session scope: the shmem is not ours alone
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::DrmMapsLowerBound);
    EXPECT_EQ(c.gpuBuffersBytes, in.maps.bytes);
    EXPECT_TRUE(c.outsideRssAnon);
}

TEST(GpuBufferCensus, FdinfoKeysWinOverEverything) {
    GpuBufferInputs in;
    in.drmFds = 1;
    in.fdinfoMemoryKeys = true;
    in.fdinfoTotalBytes = 12345678;   // legacy drm-memory-* only: the total IS the resident size
    in.fdinfoLegacyKeys = true;       // (B1032 review: the fold records that it was legacy)
    in.fdinfoLegacyResidentBytes = 12345678;
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::DrmFdinfo);
    EXPECT_EQ(c.gpuBuffersBytes, 12345678u);
}

// B1032 (#352): the row is the RESIDENT figure, never the total. Numbers from
// the ROCK 4D (Panfrost, kernel 7.1, Mesa 26.0) at qeynos2 steady: the total
// counts the unfaulted part of Mesa's 128 MiB growable tiler heap (NOT the BO
// cache: purgeable was 64-512 KiB, review/B1032/README.md), 4.5x the bytes
// with pages behind them; the resident figure agrees with the cgroup's
// shmem (41.2 MB) and the mapped lower bound (36.7 MB).
TEST(GpuBufferCensus, FdinfoResidentIsTheRowNotTheTotal) {
    GpuBufferInputs in;
    in.drmFds = 6;
    in.fdinfoMemoryKeys = true;
    in.fdinfoResidentKeys = true;
    in.fdinfoTotalBytes = 175411200;
    in.fdinfoResidentBytes = 39407616;
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::DrmFdinfo);
    EXPECT_EQ(c.gpuBuffersBytes, 39407616u);
    EXPECT_TRUE(c.outsideRssAnon);
    EXPECT_EQ(ownedPlusGpuBuffers(25208784u, c), 25208784u + 39407616u);
    const std::string line = gpuBuffersToLogLine("steady", "qeynos2", c, 25208784u);
    EXPECT_NE(line.find(" gpu_buffers=39407616 "), std::string::npos) << line;
    EXPECT_NE(line.find(" fdinfo_total=175411200 "), std::string::npos) << line;
    EXPECT_NE(line.find(" fdinfo_resident=39407616 "), std::string::npos) << line;
    EXPECT_NE(line.find(" fdinfo_purgeable="), std::string::npos) << line;
}

// B1032 review: a MODERN kernel that prints drm-total-* with no resident key.
// drm_show_memory_stats() prints drm-resident-* only when some object reports
// a resident status (lane 3's kernel 7.1 prints `drm-total-memory: 0` and no
// resident key when nothing is resident), so a Panfrost-shaped fdinfo holding
// only the total -- here the unfaulted 128 MiB tiler heap -- carries NO
// resident figure. Taking its total would be the very overstatement B1032
// removes; the fdinfo source has nothing to say, so the census falls through
// to the next source (the exclusive cgroup's shmem). The fd is folded exactly
// as readGpuBufferCensus() folds it.
TEST(GpuBufferCensus, TotalOnlyFdinfoIsNotTheRow) {
    const DrmFdinfo f = parseDrmFdinfo(
        "drm-driver:\tpanfrost\n"
        "drm-client-id:\t12\n"
        "drm-total-memory:\t131072 KiB\n"
        "drm-shared-memory:\t0\n");
    EXPECT_TRUE(f.hasMemoryKeys);
    EXPECT_FALSE(f.hasResidentKeys);
    EXPECT_EQ(f.totalBytes, 134217728u);
    GpuBufferInputs in;
    in.drmFds = 1;
    in.fdinfoMemoryKeys = f.hasMemoryKeys;
    in.fdinfoResidentKeys = f.hasResidentKeys;
    in.fdinfoTotalBytes = f.totalBytes;
    in.fdinfoResidentBytes = f.residentBytes;
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_NE(c.gpuBuffersBytes, 134217728u);
    EXPECT_EQ(c.source, GpuBufferSource::MemcgShmem);
    EXPECT_EQ(c.gpuBuffersBytes, 1806336u);
}

// The render node's fdinfo CAPTURED on the ROCK 4D during that run.
TEST(GpuBufferCensus, PanfrostFdinfoPublishesResidentAndPurgeable) {
    const DrmFdinfo f = parseDrmFdinfo(fixture("rock4d_fdinfo_renderD128"));
    EXPECT_TRUE(f.isDrm);
    EXPECT_TRUE(f.hasMemoryKeys);
    EXPECT_TRUE(f.hasResidentKeys);
    EXPECT_GT(f.totalBytes, f.residentBytes * 3);
    EXPECT_GT(f.residentBytes, 0u);
    // drm-usage-stats.rst: purgeable buffers are a subset of the RESIDENT ones.
    EXPECT_LE(f.purgeableBytes, f.residentBytes);
}

// readGpuBufferCensus()'s per-fd fold (B1032 review): an fd with no memory key
// is skipped, a repeated drm-client-id counts once, the key flags are OR'ed
// over fds and the byte figures summed -- so a resident key on ANY fd makes the
// fdinfo the row, a total-only fd adds nothing to it, and a legacy drm-memory-*
// fd adds its figure as resident.
TEST(GpuBufferCensus, FdinfoFoldOrsKeysAndDedupesClients) {
    const DrmFdinfo lima = parseDrmFdinfo(fixture("kmscube_fdinfo_renderD128"));
    const DrmFdinfo panfrost = parseDrmFdinfo(fixture("rock4d_fdinfo_renderD128"));
    const DrmFdinfo totalOnly = parseDrmFdinfo(
        "drm-driver:\trockchip\ndrm-client-id:\t5\ndrm-total-memory:\t0\ndrm-shared-memory:\t0\n");
    const DrmFdinfo legacy = parseDrmFdinfo(
        "drm-driver:\told\ndrm-client-id:\t9\ndrm-memory-vram:\t300 KiB\n");
    GpuBufferInputs in;
    DrmFdinfoSeen seen;
    foldDrmFdinfo(totalOnly, seen, in);   // first: no resident key yet
    EXPECT_TRUE(in.fdinfoMemoryKeys);
    EXPECT_FALSE(in.fdinfoResidentKeys);
    foldDrmFdinfo(lima, seen, in);        // no memory key: skipped
    foldDrmFdinfo(panfrost, seen, in);
    foldDrmFdinfo(panfrost, seen, in);    // the same client (a dup'd fd): once
    foldDrmFdinfo(legacy, seen, in);
    EXPECT_EQ(seen.n, 3u);
    EXPECT_TRUE(in.fdinfoResidentKeys);   // OR'ed: true once any fd had one
    EXPECT_TRUE(in.fdinfoLegacyKeys);
    EXPECT_EQ(in.fdinfoTotalBytes, panfrost.totalBytes + 300u * 1024);
    EXPECT_EQ(in.fdinfoResidentBytes, 38484u * 1024);
    EXPECT_EQ(in.fdinfoLegacyResidentBytes, 300u * 1024);
    EXPECT_EQ(in.fdinfoPurgeableBytes, 512u * 1024);
    in.drmFds = 5;
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::DrmFdinfo);
    EXPECT_EQ(c.gpuBuffersBytes, 38484u * 1024 + 300u * 1024);
}

// llvmpipe: the buffers are heap already charged to gpu_driver. The row
// reports the tally but must NOT be added to owned (double counting).
TEST(GpuBufferCensus, SoftwareRasteriserReportsTheTallyInsideGpuDriver) {
    GpuBufferInputs in;
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;   // even an exclusive cgroup: its shmem is not GPU memory here
    in.logicalPublished = true;
    in.logicalTextureBytes = 30u << 20;
    in.logicalBufferBytes = 5u << 20;
    in.softwareRasteriser = 1;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::LogicalInGpuDriver);
    EXPECT_EQ(c.gpuBuffersBytes, 35u << 20);
    EXPECT_FALSE(c.outsideRssAnon);
    EXPECT_EQ(ownedPlusGpuBuffers(40u << 20, c), 40u << 20);
}

TEST(GpuBufferCensus, UnpublishedRendererIsInferredFromDrmFds) {
    GpuBufferInputs in;
    in.logicalPublished = true;
    in.logicalTextureBytes = 100;
    EXPECT_TRUE(decideGpuBuffers(in).software);   // no DRM fd: software
    in.drmFds = 2;
    in.maps.mappings = 1;
    in.maps.bytes = 4096;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_FALSE(c.software);
    EXPECT_EQ(c.source, GpuBufferSource::DrmMapsLowerBound);
}

TEST(GpuBufferCensus, LogLineAndJsonCarryTheRow) {
    GpuBufferInputs in;
    in.drmFds = 6;
    in.maps = parseDrmMaps(fixture("kmscube_maps"));
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;
    in.softwareRasteriser = 0;
    const GpuBufferCensus c = decideGpuBuffers(in);
    const std::string line = gpuBuffersToLogLine("ready", "qeynos2", c, 1000);
    EXPECT_EQ(line.rfind("[memgpu] point=ready zone=qeynos2 gpu_buffers=1806336 source=memcg_shmem "
                         "outside_rss_anon=1 ", 0), 0u) << line;
    // B978: owned = RssAnon-gpu_driver (1000) + gpu_buffers outside RssAnon.
    EXPECT_NE(line.find(" owned_rss_anon=1000 owned=1807336"), std::string::npos) << line;
    EXPECT_EQ(line.find("owned_plus_gpu_buffers"), std::string::npos) << line;
    const std::string json = gpuBuffersToJson(c, 1000);
    EXPECT_NE(json.find("\"gpuBuffersBytes\":1806336"), std::string::npos) << json;
    EXPECT_NE(json.find("\"source\":\"memcg_shmem\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"ownedRssAnonBytes\":1000,\"ownedBytes\":1807336"), std::string::npos) << json;
}

// The row rides on the [memledger] line BEFORE `allocs=` (every rig parses
// the categories as the <name>=<live>/<peak> pairs after it) and in the JSON.
TEST(GpuBufferCensus, LedgerSnapshotCarriesTheRow) {
    publishGpuSoftwareRasteriser(true);
    publishGpuLogicalTally(3u << 20, 1u << 20);
    const Snapshot s = snapshot("b955_test");
    // This test binary opens no DRM device, so the published verdict holds.
    EXPECT_EQ(s.gpuBuffers.source, GpuBufferSource::LogicalInGpuDriver);
    EXPECT_EQ(s.gpuBuffers.gpuBuffersBytes, 4u << 20);
    // B978: inside gpu_driver on a software rasteriser -- owned does not add it again.
    EXPECT_EQ(s.ownedBytes, s.ownedRssAnonBytes);
    const std::string line = toLogLine(s);
    const size_t allocs = line.find(" allocs=");
    ASSERT_NE(allocs, std::string::npos);
    const size_t row = line.find(" gpu_buffers=4194304 gpu_buffers_src=logical_in_gpu_driver"
                                 " gpu_buffers_outside_rss_anon=0 owned=");
    ASSERT_NE(row, std::string::npos) << line;
    EXPECT_LT(row, allocs);
    const std::string json = toJson(s);
    EXPECT_NE(json.find("\"gpuBuffersBytes\":4194304"), std::string::npos) << json;
    EXPECT_NE(json.find("\"gpuBuffers\":{"), std::string::npos) << json;
}

// B1056: the fdinfo guard fails closed -- an fd whose driver has no sysfs row
// is NOT read (a wrong "yes" is a kernel panic on pvrsrvkm) -- and the kill
// switch, read per call, wins over everything.
TEST(GpuBufferCensus, FdinfoGuardFailsClosedAndHonoursKillSwitch) {
    unsetenv("WILLEQ_GPU_CENSUS_NO_FDINFO");
    EXPECT_FALSE(eqt::memory::drmFdinfoIsSafe("/dev/dri/no-such-node-b1056"));
    EXPECT_FALSE(eqt::memory::drmFdinfoIsSafe("/dev/dri/"));
    EXPECT_FALSE(eqt::memory::drmFdinfoIsSafe(nullptr));
    // A real node on the build host (any in-tree driver) is readable...
    bool anyReal = false;
    for (const char* node : {"/dev/dri/card0", "/dev/dri/card1", "/dev/dri/renderD128"}) {
        char link[128];
        snprintf(link, sizeof(link), "/sys/class/drm/%s/device/driver", node + 9);
        char buf[256];
        if (readlink(link, buf, sizeof(buf) - 1) <= 0) continue;
        anyReal = true;
        EXPECT_TRUE(eqt::memory::drmFdinfoIsSafe(node)) << node;
        // ...unless the kill switch is set, which is read on every call.
        setenv("WILLEQ_GPU_CENSUS_NO_FDINFO", "1", 1);
        EXPECT_FALSE(eqt::memory::drmFdinfoIsSafe(node)) << node;
        unsetenv("WILLEQ_GPU_CENSUS_NO_FDINFO");
        EXPECT_TRUE(eqt::memory::drmFdinfoIsSafe(node)) << node;
        break;
    }
    if (!anyReal) GTEST_SKIP() << "no DRM node with a sysfs driver row on this host";
}
