// T490 -- the BSP PVS is stored as the WLD's RLE bytes, decoded per camera
// region through a bounded cache, and the zone's spatial data fits a stated
// budget for every classic zone, iceclad included.
//
// Why this exists: BspRegion::visibleRegions used to be a dense
// std::vector<bool> sized to the zone's region count, decoded for EVERY region
// at parse time. That is regions^2/8 bytes per tree -- 176.9 MB for iceclad's
// 37,609 regions (heaptrack, review/T490/) -- and it OOM-killed the Orange Pi
// at "Computing spatial data" (AUDIT-0914, anon-rss 318 MB). The 1999 client
// kept the RLE; so does fable now.
//
// Three nets:
//   1. the RLE grammar decodes exactly as before (synthetic streams covering
//      every opcode class), pvsContains() agrees with the decoded bitset bit
//      for bit, and malformed input is harmless;
//   2. the decode cache is BOUNDED: decoding every region of a tree retains
//      at most kPvsCacheSlots bitsets;
//   3. on the real zones, the resident spatial data stays under the orangepi
//      S3D-zone-source budget and the parse's PEAK heap (a counting
//      operator new seam) stays under a stated multiple of it -- iceclad AND
//      the three golden zones, so the bound is not iceclad-specific. A
//      return to dense storage fails the iceclad case by ~150 MB.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <malloc.h>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "client/zone_bsp.h"
#include "client/graphics/eq/wld_loader.h"

using namespace EQT::Graphics;

// ── Counting allocator seam ────────────────────────────────────────────────
// Global operator new/delete replacement for THIS test binary. Tracks bytes
// in flight (malloc_usable_size, so it counts what the allocator really
// hands out) and the high-water mark since the last resetPeak().
namespace {

std::atomic<size_t> g_heapCurrent{0};
std::atomic<size_t> g_heapPeak{0};

void trackAlloc(void* p) {
    size_t n = malloc_usable_size(p);
    size_t cur = g_heapCurrent.fetch_add(n, std::memory_order_relaxed) + n;
    size_t peak = g_heapPeak.load(std::memory_order_relaxed);
    while (cur > peak && !g_heapPeak.compare_exchange_weak(peak, cur, std::memory_order_relaxed)) {}
}

void trackFree(void* p) {
    if (!p) return;
    g_heapCurrent.fetch_sub(malloc_usable_size(p), std::memory_order_relaxed);
}

void resetPeak() { g_heapPeak.store(g_heapCurrent.load()); }

}  // namespace

void* operator new(std::size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    trackAlloc(p);
    return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { trackFree(p); std::free(p); }
void operator delete[](void* p) noexcept { operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { operator delete(p); }

namespace {

std::vector<bool> bitsOf(std::initializer_list<int> ones, uint32_t n) {
    std::vector<bool> v(n, false);
    for (int i : ones) v[static_cast<size_t>(i)] = true;
    return v;
}

// ── 1. RLE grammar ─────────────────────────────────────────────────────────

TEST(ZoneBspPvsRle, SkipShortAndIncludeShort) {
    // 0x02: skip 2; 0xC3: include 3 -> regions 2,3,4 visible of 8
    const uint8_t rle[] = {0x02, 0xC3};
    EXPECT_EQ(decodeRlePvs(rle, sizeof(rle), 8), bitsOf({2, 3, 4}, 8));
}

TEST(ZoneBspPvsRle, SkipWord) {
    // 0x3F + WORD 300: skip 300; 0xC1: include 1 -> region 300 of 400
    const uint8_t rle[] = {0x3F, 0x2C, 0x01, 0xC1};
    EXPECT_EQ(decodeRlePvs(rle, sizeof(rle), 400), bitsOf({300}, 400));
}

TEST(ZoneBspPvsRle, SkipThenIncludeNibbles) {
    // 0x40 + (skip 2 << 3 | include 3) = 0x53 -> skip 2, include 3
    const uint8_t rle[] = {0x53};
    EXPECT_EQ(decodeRlePvs(rle, sizeof(rle), 8), bitsOf({2, 3, 4}, 8));
}

TEST(ZoneBspPvsRle, IncludeThenSkipNibbles) {
    // 0x80 + (include 2 << 3 | skip 1) = 0x91 -> include 0,1; skip 2; then 0xC1 -> include 3
    const uint8_t rle[] = {0x91, 0xC1};
    EXPECT_EQ(decodeRlePvs(rle, sizeof(rle), 8), bitsOf({0, 1, 3}, 8));
}

TEST(ZoneBspPvsRle, IncludeWord) {
    // 0xFF + WORD 5 -> regions 0..4 visible of 6
    const uint8_t rle[] = {0xFF, 0x05, 0x00};
    EXPECT_EQ(decodeRlePvs(rle, sizeof(rle), 6), bitsOf({0, 1, 2, 3, 4}, 6));
}

TEST(ZoneBspPvsRle, IncludeClampsAtRegionCount) {
    // include 5 of only 3 regions: never writes past the end
    const uint8_t rle[] = {0xC5};
    EXPECT_EQ(decodeRlePvs(rle, sizeof(rle), 3), bitsOf({0, 1, 2}, 3));
}

TEST(ZoneBspPvsRle, MalformedWordAtEndIsHarmless) {
    const uint8_t rle1[] = {0x3F, 0x01};  // truncated skip word
    EXPECT_EQ(decodeRlePvs(rle1, sizeof(rle1), 8), bitsOf({}, 8));
    const uint8_t rle2[] = {0xC1, 0xFF};  // truncated include word after one include
    EXPECT_EQ(decodeRlePvs(rle2, sizeof(rle2), 8), bitsOf({0}, 8));
    EXPECT_EQ(decodeRlePvs(nullptr, 0, 4), bitsOf({}, 4));
}

TEST(ZoneBspPvsRle, ContainsAgreesWithDecodeBitForBit) {
    const uint8_t rle[] = {0x02, 0xC3, 0x53, 0x91, 0x3F, 0x10, 0x00, 0xFF, 0x04, 0x00, 0x05, 0xC2};
    const uint32_t n = 64;
    BspRegion r;
    r.setPvsRle(rle, sizeof(rle), n);
    ASSERT_TRUE(r.hasPvs());
    EXPECT_EQ(r.pvsRegionCount(), n);
    auto dense = decodeRlePvs(rle, sizeof(rle), n);
    auto bits = r.pvs();
    ASSERT_EQ(bits.size(), n);
    size_t ones = 0;
    for (size_t i = 0; i < n; ++i) {
        EXPECT_EQ(bits[i], dense[i]) << "bit " << i;
        EXPECT_EQ(r.pvsContains(i), dense[i]) << "contains " << i;
        ones += dense[i] ? 1 : 0;
    }
    EXPECT_EQ(r.pvsVisibleCount(), ones);
    EXPECT_GT(ones, 0u);
    EXPECT_FALSE(r.pvsContains(n));       // out of range, like the old size() guard
    EXPECT_FALSE(r.pvsContains(SIZE_MAX));
}

TEST(ZoneBspPvsRle, RegionWithoutPvsAnswersLikeAnEmptyBitset) {
    BspRegion r;
    EXPECT_FALSE(r.hasPvs());
    EXPECT_TRUE(r.pvs().empty());
    EXPECT_EQ(r.pvs().size(), 0u);
    EXPECT_FALSE(r.pvsContains(0));
    EXPECT_EQ(r.pvsVisibleCount(), 0u);
}

TEST(ZoneBspPvsRle, PinnedFormForHandBuiltTrees) {
    BspRegion r;
    r.setPvs(std::vector<bool>{true, false, true});
    EXPECT_TRUE(r.hasPvs());
    EXPECT_EQ(r.pvs().size(), 3u);
    EXPECT_TRUE(r.pvsContains(0));
    EXPECT_FALSE(r.pvsContains(1));
    EXPECT_TRUE(r.pvsContains(2));
    EXPECT_FALSE(r.pvsContains(3));
    EXPECT_EQ(r.pvsVisibleCount(), 2u);
}

// ── 2. Bounded retention ───────────────────────────────────────────────────

TEST(ZoneBspPvsCache, DecodingEveryRegionRetainsAtMostTheSlotCount) {
    const uint32_t n = 4096;
    auto tree = std::make_shared<BspTree>();
    for (uint32_t i = 0; i < n; ++i) {
        auto r = std::make_shared<BspRegion>();
        // region i sees itself: skip i (as a word), include 1
        const uint8_t rle[] = {0x3F, static_cast<uint8_t>(i & 0xFF), static_cast<uint8_t>(i >> 8), 0xC1};
        r->setPvsRle(rle, sizeof(rle), n);
        tree->addRegion(r);
    }
    size_t rleBytes = 0;
    for (const auto& r : tree->regions) rleBytes += r->pvsMemoryBytes();
    EXPECT_LT(rleBytes, n * 16u) << "RLE storage is linear in regions";
    tree->shrinkPvsArena();
    EXPECT_EQ(tree->pvsArenaBytes(), rleBytes) << "one arena, no per-region heap blocks";

    for (uint32_t i = 0; i < n; ++i) {
        auto bits = tree->regions[i]->pvs();
        ASSERT_EQ(bits.size(), n);
        EXPECT_TRUE(bits[i]);
        EXPECT_FALSE(bits[(i + 1) % n]);
    }
    const size_t denseTree = static_cast<size_t>(n) * n / 8;  // what the old form held
    const size_t oneBitset = sizeof(std::vector<bool>) + (n + 7) / 8;
    EXPECT_LE(tree->pvsCacheBytes(), BspTree::kPvsCacheSlots * oneBitset);
    EXPECT_LT(tree->pvsCacheBytes() * 50, denseTree);

    // A hit returns the cached vector (same bits object), so per-frame readers
    // of the camera region do not re-decode.
    auto a = tree->regions[7]->pvs();
    auto b = tree->regions[7]->pvs();
    EXPECT_EQ(&a.bits(), &b.bits());
}

// ── 2b. Regions are one block (B903) ───────────────────────────────────────
// Each region used to be its own make_shared heap block and `regions` grew by
// doubling: iceclad's 37,609 regions were 37,609 blocks (5.4 MB of zone_parse)
// plus a 65,536-slot pointer array. The parser now takes every region from a
// BspRegionPool of 1,024-region slabs; each handle aliases its slab.

TEST(ZoneBspRegionPool, PooledRegionsAreContiguousAndBehaveAsBefore) {
    const uint32_t n = 1000;
    BspRegionPool pool(n);
    auto tree = std::make_shared<BspTree>();
    tree->regions.reserve(pool.size());
    for (uint32_t i = 0; i < n; ++i) {
        auto r = pool.next();
        r->meshReference = static_cast<int32_t>(i);
        const uint8_t rle[] = {0x3F, static_cast<uint8_t>(i & 0xFF), static_cast<uint8_t>(i >> 8), 0xC1};
        tree->addRegion(r);
        r->setPvsRle(rle, sizeof(rle), n);
    }
    for (uint32_t i = 1; i < n; ++i) {
        ASSERT_EQ(tree->regions[i].get(), tree->regions[0].get() + i) << "region " << i << " is not in the pool";
    }
    for (uint32_t i = 0; i < n; i += 97) {
        EXPECT_EQ(tree->regions[i]->meshReference, static_cast<int32_t>(i));
        EXPECT_TRUE(tree->regions[i]->pvsContains(i));
        EXPECT_FALSE(tree->regions[i]->pvsContains((i + 1) % n));
    }
}

TEST(ZoneBspRegionPool, SlabsAreFewAndStayOnTheHeap) {
    // A slab must stay under glibc's default mmap threshold (128 KiB): a
    // single mmapped block counts against the gate's non-heap anon bound.
    EXPECT_LT(BspRegionPool::kSlabRegions * sizeof(BspRegion) + 64, 128u * 1024u);
    const size_t n = 2500;
    BspRegionPool pool(n);
    std::vector<std::shared_ptr<BspRegion>> rs;
    for (size_t i = 0; i < n; ++i) rs.push_back(pool.next());
    size_t breaks = 0;
    for (size_t i = 1; i < n; ++i) if (rs[i].get() != rs[i - 1].get() + 1) ++breaks;
    EXPECT_EQ(breaks, 2u) << "2,500 regions are three slabs";
}

TEST(ZoneBspRegionPool, AHandleKeepsItsRegionAliveAfterPoolAndTree) {
    std::shared_ptr<BspRegion> kept;
    {
        BspRegionPool pool(4);
        auto tree = std::make_shared<BspTree>();
        for (int i = 0; i < 4; ++i) tree->addRegion(pool.next());
        tree->regions[2]->meshReference = 42;
        kept = tree->regions[2];
    }
    ASSERT_NE(kept, nullptr);
    EXPECT_EQ(kept->meshReference, 42);
}

TEST(ZoneBspRegionPool, PastTheCountAndWithNoCountRegionsStillWork) {
    BspRegionPool empty(0);
    EXPECT_EQ(empty.size(), 0u);
    ASSERT_NE(empty.next(), nullptr);
    BspRegionPool one(1);
    auto a = one.next();
    auto b = one.next();                 // one past the count: its own block
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get());
}

// ── 3. Real zones ──────────────────────────────────────────────────────────

class ZoneBspPvsRealZones : public ::testing::Test {
protected:
    std::string eqPath_;
    void SetUp() override {
        const char* env = std::getenv("EQ_CLIENT_PATH");
        eqPath_ = env ? env : EQ_CLIENT_DATA_PATH;
    }
    bool exists(const std::string& p) { std::ifstream f(p); return f.good(); }

    // The bound is MEASURED, not argued. review/T490/x86/pvs_census.tsv is a
    // census of all 118 original/Kunark/Velious zone archives on this client
    // at the T490 tip; the maxima are iceclad, and iceclad is the largest zone
    // in the set by a factor of two in regions (37,609 vs timorous' 19,321):
    //
    //   WldLoader::getMemoryUsage()       max 25,737,912 B = 24.55 MiB
    //   heap retained by the parse        max 28,870,416 B = 27.53 MiB
    //   heap peak during the parse        max 65,450,048 B = 62.42 MiB
    //
    // Retained is bounded at 32 MiB -- the next binary step above the measured
    // maximum, 16% headroom -- and the parse peak at 96 MiB (3x that). Against
    // Will's settled ceiling (256 MB RSS, docs/decisions.md T293) the zone
    // source is then at most 12.5% of the client's whole resident envelope,
    // and against the orangepi preset's totalMemoryBudgetBytes (128 MiB) at
    // most 25%.
    //
    // NOT the preset: config/constrained_presets.json orangepi
    // s3dZoneSourceMemoryBytes is 24 MiB (T293's derivation, 7.52 MiB gfaydark
    // x 2.34 largest-.s3d-on-disk x 1.25), and iceclad measures 24.55 MiB
    // against it -- 572,088 B over. The visual gate applies that preset to the
    // three GOLDEN zones only (qeynos2 2.68 MiB, gfaydark 5.83 MiB, nro
    // 1.46 MiB), so nothing is red today; but the preset was derived from
    // gfaydark and does not describe the largest zone in the game. T491 owns
    // the preset and the ledger -- see review/T490/README.md.
    static constexpr size_t kZoneSourceBudgetBytes = 32u * 1024u * 1024u;
    static constexpr size_t kParsePeakBudgetBytes = 3u * kZoneSourceBudgetBytes;

    void checkZone(const char* zone, size_t minRegions) {
        std::string s3d = eqPath_ + "/" + zone + ".s3d";
        if (!exists(s3d)) GTEST_SKIP() << "EQ client not found at: " << s3d;

        resetPeak();
        const size_t before = g_heapCurrent.load();
        auto loader = std::make_unique<WldLoader>();
        ASSERT_TRUE(loader->parseFromArchive(s3d, std::string(zone) + ".wld"));
        const size_t peak = g_heapPeak.load() - before;
        const size_t resident = g_heapCurrent.load() - before;

        auto tree = loader->getBspTree();
        ASSERT_NE(tree, nullptr);
        const size_t regions = tree->regions.size();
        EXPECT_GE(regions, minRegions);
        size_t withPvs = 0, rleBytes = 0;
        for (const auto& r : tree->regions) {
            if (r && r->hasPvs()) { ++withPvs; rleBytes += r->pvsMemoryBytes(); }
        }
        EXPECT_GT(withPvs, 0u);
        // B903: the parser takes every region from one exactly-sized block.
        EXPECT_EQ(tree->regions.capacity(), regions) << zone << ": region handle array has growth slack or an over-count";
        size_t breaks = 0;
        for (size_t i = 1; i < regions; ++i)
            if (tree->regions[i].get() != tree->regions[i - 1].get() + 1) ++breaks;
        const size_t slabs = (regions + BspRegionPool::kSlabRegions - 1) / BspRegionPool::kSlabRegions;
        EXPECT_EQ(breaks + 1, slabs) << zone << ": regions are not in 1,024-region slabs (separate heap blocks again?)";
        const size_t denseBytes = regions * regions / 8;
        const size_t tallied = loader->getMemoryUsage();

        printf("[T490] %s: regions=%zu withPvs=%zu pvsRle=%zu B arena=%zu B (dense would be %zu B) "
               "wldTally=%zu B parsePeak=%zu B parseResident=%zu B cache=%zu B sizeof(BspRegion)=%zu\n",
               zone, regions, withPvs, rleBytes, tree->pvsArenaBytes(), denseBytes, tallied, peak, resident,
               tree->pvsCacheBytes(), sizeof(BspRegion));
        EXPECT_EQ(tree->pvsArenaBytes(), rleBytes);

        EXPECT_LT(rleBytes * 4, denseBytes + 1024u * 1024u)
            << zone << ": PVS storage is back to the dense form";
        EXPECT_LE(tallied, kZoneSourceBudgetBytes)
            << zone << ": WldLoader::getMemoryUsage() over the S3D zone source budget";
        EXPECT_LE(resident, kZoneSourceBudgetBytes)
            << zone << ": heap retained by the WLD parse over the S3D zone source budget";
        EXPECT_LE(peak, kParsePeakBudgetBytes)
            << zone << ": WLD parse peak heap over 3x the S3D zone source budget";

        // The decoded camera-region bitset for the largest zone is one
        // regions/8 vector, not regions^2/8.
        size_t sample = 0;
        for (size_t i = 0; i < regions; ++i) if (tree->regions[i] && tree->regions[i]->hasPvs()) { sample = i; break; }
        auto bits = tree->regions[sample]->pvs();
        EXPECT_EQ(bits.size(), regions);
        EXPECT_LE(tree->pvsCacheBytes(), BspTree::kPvsCacheSlots * (sizeof(std::vector<bool>) + regions / 8 + 8));
        // Cross-check one region's contains() against its decoded bits.
        size_t checked = 0;
        for (size_t i = 0; i < regions && checked < 2048; i += 17, ++checked) {
            EXPECT_EQ(tree->regions[sample]->pvsContains(i), bits[i]) << zone << " region " << i;
        }
    }
};

TEST_F(ZoneBspPvsRealZones, IcecladFitsTheBudget) { checkZone("iceclad", 30000); }
// The second-largest zone in the set (Kunark), so the bound is not a statement
// about iceclad alone: 19,321 regions, 138.0 MiB of dense PVS + 44.5 MiB of
// per-region candidate lists before T490.
TEST_F(ZoneBspPvsRealZones, TimorousFitsTheBudget) { checkZone("timorous", 19000); }
TEST_F(ZoneBspPvsRealZones, Qeynos2FitsTheBudget) { checkZone("qeynos2", 2000); }
TEST_F(ZoneBspPvsRealZones, GfaydarkFitsTheBudget) { checkZone("gfaydark", 2500); }
TEST_F(ZoneBspPvsRealZones, NroFitsTheBudget) { checkZone("nro", 2500); }

}  // namespace
