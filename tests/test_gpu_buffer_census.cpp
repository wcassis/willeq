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

// B961 (GitLab #279): the two byte classes on no ledger row. The captured
// fixture has a kernel= line, so memcg_kernel_seen=1; the scanout pair is the
// device's published tally (readScanoutTally() fills in.scanoutFb* in
// readGpuBufferCensus). The rows sit AFTER fdinfo_purgeable and BEFORE the
// B978 owned fields (the pre-B961 parsers read the owned pair last) and in
// the JSON.
TEST(GpuBufferCensus, B961RowsReportTheTwoUnrowedByteClasses) {
    GpuBufferInputs in;
    in.drmFds = 6;
    in.maps = parseDrmMaps(fixture("kmscube_maps"));
    in.memcg = parseMemcgStat(fixture("kmscube_memory.stat"));
    in.cgroupProcs = 1;
    in.softwareRasteriser = 0;
    // The Pi's steady_60s numbers from the issue: two 3,686,400 B buffers.
    in.scanoutFbBufs = 2;
    in.scanoutFbBytes = 2u * 3686400u;
    const GpuBufferCensus c = decideGpuBuffers(in);

    const B961Rows r = b961Rows(c);
    EXPECT_EQ(r.scanoutFbBufs, 2u);
    EXPECT_EQ(r.scanoutFbBytes, 2u * 3686400u);
    EXPECT_EQ(r.memcgKernelBytes, 151552u);
    EXPECT_TRUE(r.memcgKernelAvailable);

    const std::string line = gpuBuffersToLogLine("steady_60s", "qeynos2", c, 1000);
    const size_t purgeable = line.find("fdinfo_purgeable=");
    const size_t scanout = line.find(" scanout_fb=7372800 scanout_fb_bufs=2 memcg_kernel_seen=1 ");
    const size_t owned = line.find("owned_rss_anon=");
    ASSERT_NE(purgeable, std::string::npos) << line;
    ASSERT_NE(scanout, std::string::npos) << line;
    ASSERT_NE(owned, std::string::npos) << line;
    EXPECT_LT(purgeable, scanout) << "the B961 rows ride after fdinfo_purgeable";
    EXPECT_LT(scanout, owned) << "the B978 owned fields stay last";
    // The B961 rows are REPORT, not COUNT: owned is unchanged by them.
    EXPECT_NE(line.find(" owned_rss_anon=1000 owned=1807336 "), std::string::npos) << line;

    const std::string json = gpuBuffersToJson(c, 1000);
    EXPECT_NE(json.find("\"scanoutFbBytes\":7372800"), std::string::npos) << json;
    EXPECT_NE(json.find("\"scanoutFbBufs\":2"), std::string::npos) << json;
    EXPECT_NE(json.find("\"memcgKernelSeen\":true"), std::string::npos) << json;
}

// B961: a memory.stat without a kernel= line reports 0 bytes and
// kernelSeen=false (the row still exists, named as absent, not guessed).
TEST(GpuBufferCensus, B961KernelAbsentIsReportedAbsent) {
    const MemcgStat s = parseMemcgStat("shmem 1000\nanon 2000\n");
    ASSERT_TRUE(s.ok);
    EXPECT_FALSE(s.kernelSeen);
    EXPECT_EQ(s.kernelBytes, 0u);
    EXPECT_FALSE(b961Rows(decideGpuBuffers(GpuBufferInputs{})).memcgKernelAvailable);
}

// B961: the publish round-trip -- the device->core seam. readScanoutTally()
// is a pure read of the core-owned atomics: whatever publishScanoutTally()
// last stored is what comes back, and before any publish (this test process
// never linked the device) the tally is the {0, 0} empty default -- the true
// answer of "this process has no GBM buffers". The device's destructor
// publishes {0, 0} at teardown.
TEST(GpuBufferCensus, B961PublishRoundTripAndEmptyDefault) {
    // Order-independent: establish the empty default by publishing it first.
    publishScanoutTally(0, 0);
    const ScanoutTallyRead empty = readScanoutTally();
    EXPECT_EQ(empty.bufs, 0u);
    EXPECT_EQ(empty.bytes, 0u);
    publishScanoutTally(3, 1234u);
    const ScanoutTallyRead r = readScanoutTally();
    EXPECT_EQ(r.bufs, 3u);
    EXPECT_EQ(r.bytes, 1234u);
    publishScanoutTally(0, 0);   // the device's teardown
    const ScanoutTallyRead zero = readScanoutTally();
    EXPECT_EQ(zero.bufs, 0u);
    EXPECT_EQ(zero.bytes, 0u);
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

// B1035 (#354, GitLab issue 354): the ROCK 4D captures (run of build
// bfcf50ea on the published aarch64 build, qeynos2, own cgroup, the
// steady_60s window; raw files
// /home/user/kb-evidence/kb-b1035-lane3-fdinfo-gap/raw/raw030/):
//
// (a) the panfrost render node's fdinfo (rock4d_fdinfo_renderD128_steady):
//     drm-resident-memory 30088 KiB = 30,810,112 B,
//     drm-shared-memory 9288 KiB = 9,510,912 B (the three scanout
//     framebuffers, each 1024x768 XR24 -- debugfs framebuffer size
//     3,170,304 x 3; the device's stride-based tally reads 9,437,184);
// (b) the rockchip-drm CARD node's fdinfo (rock4d_fdinfo_card0): NO drm-*
//     memory key at all -- pos/flags/mnt_id/ino only;
// (c) the exclusive memcg's memory.stat (rock4d_memory.stat): shmem
//     39,841,792 B ~= (a)'s resident 30,810,112 + (a)'s shared 9,510,912
//     within the capture window (the render node's keys move a few hundred
//     KiB second to second; shmem tracks them with the same jitter);
// (d) smaps: the scanout framebuffers are shmem-backed objects, not
//     process mappings (smaps Shmem: 0).
//
// The gap the issue names (resident below the cgroup's shmem) is the
// scanout framebuffers: charged to the process memcg as shmem, published
// by the render node as SHARED, and present in no node's RESIDENT figure
// (the card node publishes none). The hypothesis's "shows under the
// rockchip-drm node's fdinfo" half does NOT hold -- that node has no
// memory keys -- so the ledger names the mechanism instead: when the
// cgroup is exclusive and its shmem exceeds the resident figure, the row
// takes the shmem (the higher measurement of the same bytes) and
// fdinfo_scanout_gap names the delta.
TEST(GpuBufferCensus, B1035CardNodePublishesNoMemoryKeys) {
    const DrmFdinfo f = parseDrmFdinfo(fixture("rock4d_fdinfo_card0"));
    EXPECT_FALSE(f.isDrm);            // no drm-driver/client-id/memory key
    EXPECT_FALSE(f.hasMemoryKeys);
    EXPECT_EQ(f.clientId, 0u);
    EXPECT_EQ(f.totalBytes, 0u);
    EXPECT_EQ(f.residentBytes, 0u);
    GpuBufferInputs in;
    DrmFdinfoSeen seen;
    foldDrmFdinfo(f, seen, in);   // skipped: no memory key
    EXPECT_EQ(seen.n, 0u);
    EXPECT_FALSE(in.fdinfoMemoryKeys);
}

TEST(GpuBufferCensus, B1035CaptureFoldsToTheResidentRow) {
    // Both nodes of the SAME client (a display card + its render node):
    // the card fd contributes nothing (no memory keys), the render fd the
    // resident figure. This is exactly the two-node fold
    // readGpuBufferCensus() performs.
    const DrmFdinfo card = parseDrmFdinfo(fixture("rock4d_fdinfo_card0"));
    const DrmFdinfo render = parseDrmFdinfo(fixture("rock4d_fdinfo_renderD128_steady"));
    GpuBufferInputs in;
    DrmFdinfoSeen seen;
    in.drmFds = 6;   // three fds per node (dup'd)
    foldDrmFdinfo(card, seen, in);
    foldDrmFdinfo(card, seen, in);
    foldDrmFdinfo(card, seen, in);
    foldDrmFdinfo(render, seen, in);
    foldDrmFdinfo(render, seen, in);
    foldDrmFdinfo(render, seen, in);   // dup'd render fd: one client, once
    EXPECT_EQ(seen.n, 1u);
    EXPECT_TRUE(in.fdinfoResidentKeys);
    EXPECT_EQ(in.fdinfoResidentBytes, 30088u * 1024);
    EXPECT_EQ(in.fdinfoTotalBytes, 168400u * 1024);
}

TEST(GpuBufferCensus, B1035ShmemHigherThanResidentWinsAndNamesTheGap) {
    // The capture's steady numbers (raw030): resident 30,810,112 (fixture
    // 30088 KiB) vs the exclusive memcg's shmem 39,841,792 (fixture). The
    // row is the shmem -- the higher measurement of the same bytes -- and
    // the delta is named; the scanout_fb row carries the device's own
    // tally of those bytes (the [memgpu] line of the run: 9,437,184 over
    // 3 buffers).
    GpuBufferInputs in;
    const DrmFdinfo card = parseDrmFdinfo(fixture("rock4d_fdinfo_card0"));
    const DrmFdinfo render = parseDrmFdinfo(fixture("rock4d_fdinfo_renderD128_steady"));
    DrmFdinfoSeen seen;
    in.drmFds = 6;
    foldDrmFdinfo(card, seen, in);
    foldDrmFdinfo(render, seen, in);
    in.memcg = parseMemcgStat(fixture("rock4d_memory.stat"));
    ASSERT_TRUE(in.memcg.ok);
    in.cgroupProcs = 1;
    in.softwareRasteriser = 0;
    in.scanoutFbBufs = 3;
    in.scanoutFbBytes = 9437184u;   // the device's tally (the run's [memgpu] line)
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::DrmFdinfo);   // a resident figure exists
    EXPECT_EQ(c.gpuBuffersBytes, 39841792u);           // the shmem, not 30810112
    EXPECT_EQ(c.fdinfoScanoutGapBytes, 39841792u - 30088u * 1024u);
    EXPECT_TRUE(c.outsideRssAnon);
    // The named delta brackets the device's own scanout tally (shmem and
    // the render node's shared figure carry the same jitter -- a few
    // hundred KiB second to second -- plus page rounding): it is NOT the
    // total (the tiler heap) and it is of the scanout's size.
    EXPECT_GT(c.fdinfoScanoutGapBytes, in.scanoutFbBytes / 2u);
    EXPECT_LT(c.fdinfoScanoutGapBytes, in.scanoutFbBytes * 2u);
    // The [memgpu] line names it, positioned after memcg_kernel_seen and
    // BEFORE the B978 owned fields (which stay last for the pre-B961
    // parsers); the owned arithmetic is unchanged in meaning: owned still
    // adds the row when it is outside RssAnon.
    const std::string line = gpuBuffersToLogLine("steady_60s", "qeynos2", c, 25208784u);
    const size_t kernelSeen = line.find(" memcg_kernel_seen=");
    const size_t gap = line.find(" fdinfo_scanout_gap=" + std::to_string(c.fdinfoScanoutGapBytes) + " ");
    const size_t owned = line.find(" owned_rss_anon=");
    ASSERT_NE(kernelSeen, std::string::npos) << line;
    ASSERT_NE(gap, std::string::npos) << line;
    ASSERT_NE(owned, std::string::npos) << line;
    EXPECT_LT(kernelSeen, gap) << "fdinfo_scanout_gap rides after memcg_kernel_seen";
    EXPECT_LT(gap, owned) << "the B978 owned fields stay last";
    EXPECT_NE(line.find(" scanout_fb=9437184 scanout_fb_bufs=3 "), std::string::npos) << line;
    EXPECT_EQ(ownedPlusGpuBuffers(25208784u, c), 25208784u + 39841792u);
    const std::string json = gpuBuffersToJson(c, 25208784u);
    EXPECT_NE(json.find("\"fdinfoScanoutGapBytes\":" + std::to_string(c.fdinfoScanoutGapBytes)), std::string::npos) << json;
}

// The rule must not move a lane whose fdinfo resident figure already
// covers the shmem (the Lima lanes: no fdinfo keys at all, the shmem IS
// the row; a Panfrost run where the resident figure leads): the row is
// the resident figure and the gap is named as 0.
TEST(GpuBufferCensus, B1035ResidentAtLeastShmemKeepsTheResidentRow) {
    GpuBufferInputs in;
    in.drmFds = 1;
    in.fdinfoMemoryKeys = true;
    in.fdinfoResidentKeys = true;
    in.fdinfoTotalBytes = 175411200;
    in.fdinfoResidentBytes = 39407616;   // the B1032 run: shmem 41.2 MB was
    in.softwareRasteriser = 0;           // within it
    in.memcg = parseMemcgStat("shmem 39000000\n");
    in.cgroupProcs = 1;
    const GpuBufferCensus c = decideGpuBuffers(in);
    EXPECT_EQ(c.source, GpuBufferSource::DrmFdinfo);
    EXPECT_EQ(c.gpuBuffersBytes, 39407616u);   // the resident figure, unchanged
    EXPECT_EQ(c.fdinfoScanoutGapBytes, 0u);
    // And a non-exclusive cgroup never reaches the shmem at all:
    in.cgroupProcs = 3;
    const GpuBufferCensus c2 = decideGpuBuffers(in);
    EXPECT_EQ(c2.gpuBuffersBytes, 39407616u);
    EXPECT_EQ(c2.fdinfoScanoutGapBytes, 0u);
    const std::string line = gpuBuffersToLogLine("steady_60s", "qeynos2", c, 1000);
    EXPECT_NE(line.find(" fdinfo_scanout_gap=0 "), std::string::npos) << line;
}
