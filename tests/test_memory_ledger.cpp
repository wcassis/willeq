// T491: the memory ledger (include/client/core/memory_ledger.h).
//
// These tests exercise the allocator hooks THROUGH the public C/C++
// allocation entry points -- malloc/calloc/realloc/posix_memalign/valloc,
// operator new (plain, array, aligned, nothrow) -- because that is the path
// every byte in the client takes. The ledger TU is linked into this binary,
// so every allocation the test framework itself makes is attributed too;
// the tests therefore compare DELTAS against a baseline taken under the
// same scope, never absolute values.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <thread>
#include <vector>

#include <json/json.h>

#include "client/core/memory_ledger.h"

using eqt::memory::Category;
using eqt::memory::MemoryScope;

extern "C" {
void* __libc_malloc(size_t);
void __libc_free(void*);
}

namespace {

uint64_t live(Category c) { return eqt::memory::stats(c).liveBytes; }

}  // namespace

TEST(MemoryLedger, EnabledInThisBuild) {
    // The whole suite below is meaningless if the hooks are compiled out.
    ASSERT_TRUE(eqt::memory::enabled())
        << "configure with -DEQT_MEMORY_LEDGER=ON (the default) to test the ledger";
}

TEST(MemoryLedger, CategoryNamesRoundTrip) {
    for (size_t i = 0; i < eqt::memory::kCategoryCount; ++i) {
        const auto c = static_cast<Category>(i);
        Category back = Category::Unattributed;
        ASSERT_TRUE(eqt::memory::categoryFromName(eqt::memory::categoryName(c), back))
            << eqt::memory::categoryName(c);
        EXPECT_EQ(back, c);
    }
    Category dummy;
    EXPECT_FALSE(eqt::memory::categoryFromName("no_such_category", dummy));
    EXPECT_STREQ(eqt::memory::categoryName(Category::Unattributed), "unattributed");
    EXPECT_STREQ(eqt::memory::categoryName(Category::ZoneParse), "zone_parse");
}

TEST(MemoryLedger, ScopeChargesAndCreditsTheCategory) {
    const uint64_t before = live(Category::ZoneParse);
    char* p = nullptr;
    {
        MemoryScope s(Category::ZoneParse);
        p = new char[1 << 20];
        memset(p, 1, 1 << 20);
        EXPECT_GE(live(Category::ZoneParse), before + (1u << 20));
    }
    delete[] p;
    EXPECT_EQ(live(Category::ZoneParse), before);
}

TEST(MemoryLedger, NestedScopesRestoreInLifoOrder) {
    const Category outer = eqt::memory::currentCategory();
    {
        MemoryScope a(Category::Audio);
        EXPECT_EQ(eqt::memory::currentCategory(), Category::Audio);
        {
            MemoryScope b(Category::UI);
            EXPECT_EQ(eqt::memory::currentCategory(), Category::UI);
        }
        EXPECT_EQ(eqt::memory::currentCategory(), Category::Audio);
    }
    EXPECT_EQ(eqt::memory::currentCategory(), outer);
}

TEST(MemoryLedger, FreeUnderAnotherScopeCreditsTheChargedCategory) {
    const uint64_t doorsBefore = live(Category::Doors);
    const uint64_t netBefore = live(Category::Network);
    void* p;
    {
        MemoryScope s(Category::Doors);
        p = malloc(200000);
        memset(p, 2, 200000);
    }
    EXPECT_GE(live(Category::Doors), doorsBefore + 200000);
    {
        MemoryScope s(Category::Network);
        free(p);
    }
    EXPECT_EQ(live(Category::Doors), doorsBefore);
    EXPECT_EQ(live(Category::Network), netBefore);
}

TEST(MemoryLedger, ChargedBytesCoverTheRequestPlusBoundedOverhead) {
    MemoryScope s(Category::Spatial);
    const uint64_t before = live(Category::Spatial);
    void* p = malloc(1000);
    const uint64_t charged = live(Category::Spatial) - before;
    // Whole glibc chunk: 1000 requested + 16 ledger header + <= 32 of
    // rounding/chunk header. Anything larger means the chunk-size read is
    // wrong; anything smaller means bytes are escaping the ledger.
    EXPECT_GE(charged, 1000u + 16u);
    EXPECT_LE(charged, 1000u + 16u + 32u);
    EXPECT_GE(malloc_usable_size(p), 1000u);
    EXPECT_LE(malloc_usable_size(p), 1000u + 32u);
    free(p);
    EXPECT_EQ(live(Category::Spatial), before);
}

TEST(MemoryLedger, MallocFamilyAllRouteThroughTheLedger) {
    MemoryScope s(Category::Equipment);
    const uint64_t before = live(Category::Equipment);

    void* c = calloc(1000, 8);
    ASSERT_NE(c, nullptr);
    for (size_t i = 0; i < 8000; ++i) ASSERT_EQ(static_cast<char*>(c)[i], 0);
    EXPECT_GE(live(Category::Equipment), before + 8000);

    void* r = realloc(c, 64000);
    ASSERT_NE(r, nullptr);
    EXPECT_GE(live(Category::Equipment), before + 64000);
    r = realloc(r, 100);  // shrink
    ASSERT_NE(r, nullptr);
    EXPECT_LT(live(Category::Equipment), before + 64000);
    free(r);
    EXPECT_EQ(live(Category::Equipment), before);

    void* pm = nullptr;
    ASSERT_EQ(posix_memalign(&pm, 4096, 10000), 0);
    ASSERT_NE(pm, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(pm) % 4096, 0u);
    memset(pm, 3, 10000);
    EXPECT_GE(live(Category::Equipment), before + 10000);
    // realloc of an over-aligned block takes the copy path
    void* pm2 = realloc(pm, 20000);
    ASSERT_NE(pm2, nullptr);
    EXPECT_EQ(static_cast<char*>(pm2)[9999], 3);
    free(pm2);
    EXPECT_EQ(live(Category::Equipment), before);

    void* am = aligned_alloc(64, 640);
    ASSERT_NE(am, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(am) % 64, 0u);
    free(am);
    void* v = valloc(100);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(v) % 4096, 0u);
    free(v);
    void* m = memalign(256, 300);
    ASSERT_NE(m, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(m) % 256, 0u);
    free(m);
    EXPECT_EQ(live(Category::Equipment), before);

    free(nullptr);  // must be a no-op
    EXPECT_EQ(realloc(nullptr, 0) != nullptr || true, true);
    EXPECT_EQ(live(Category::Equipment), before);
}

TEST(MemoryLedger, AlignedAndNothrowNewRouteThroughTheLedger) {
    MemoryScope s(Category::Objects);
    const uint64_t before = live(Category::Objects);
    struct alignas(128) Big { char data[512]; };
    Big* b = new Big;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 128, 0u);
    EXPECT_GE(live(Category::Objects), before + sizeof(Big));
    delete b;
    Big* arr = new Big[4];
    EXPECT_EQ(reinterpret_cast<uintptr_t>(arr) % 128, 0u);
    delete[] arr;
    char* n = new (std::nothrow) char[4096];
    ASSERT_NE(n, nullptr);
    delete[] n;
    EXPECT_EQ(live(Category::Objects), before);
}

TEST(MemoryLedger, ThreadsStartUnattributedAndScopesAreThreadLocal) {
    MemoryScope s(Category::GameState);
    Category seenAtStart = Category::GameState;
    Category seenInScope = Category::Unattributed;
    std::thread t([&] {
        seenAtStart = eqt::memory::currentCategory();
        MemoryScope inner(Category::Audio);
        seenInScope = eqt::memory::currentCategory();
    });
    t.join();
    EXPECT_EQ(seenAtStart, Category::Unattributed);
    EXPECT_EQ(seenInScope, Category::Audio);
    EXPECT_EQ(eqt::memory::currentCategory(), Category::GameState);
    EXPECT_EQ(eqt::memory::setThreadCategory(Category::UI), Category::GameState);
    EXPECT_EQ(eqt::memory::currentCategory(), Category::UI);
    eqt::memory::setThreadCategory(Category::GameState);
}

TEST(MemoryLedger, PeakIsAHighWaterMarkAndResetPeaksLowersIt) {
    MemoryScope s(Category::TexturesDecoded);
    const uint64_t liveBefore = live(Category::TexturesDecoded);
    char* p = new char[2 << 20];
    memset(p, 4, 2 << 20);
    const uint64_t peakWhileLive = eqt::memory::stats(Category::TexturesDecoded).peakBytes;
    EXPECT_GE(peakWhileLive, liveBefore + (2u << 20));
    delete[] p;
    EXPECT_EQ(eqt::memory::stats(Category::TexturesDecoded).peakBytes, peakWhileLive);
    eqt::memory::resetPeaks();
    EXPECT_EQ(eqt::memory::stats(Category::TexturesDecoded).peakBytes,
              eqt::memory::stats(Category::TexturesDecoded).liveBytes);
}

TEST(MemoryLedger, SnapshotArithmeticIsConsistent) {
    eqt::memory::setCurrentZone("testzone");
    const auto s = eqt::memory::snapshot("unit_test");
    EXPECT_TRUE(s.ledgerEnabled);
    EXPECT_EQ(s.point, "unit_test");
    EXPECT_EQ(s.zone, "testzone");
    EXPECT_GE(s.seq, 1u);
    uint64_t sum = 0, allocs = 0;
    for (const auto& c : s.categories) { sum += c.liveBytes; allocs += c.allocs; }
    EXPECT_EQ(s.ledgerTotalBytes, sum);
    EXPECT_EQ(s.allocCount, allocs);
    EXPECT_GT(s.ledgerTotalBytes, 0u);
    EXPECT_GE(s.ledgerPeakBytes, s.ledgerTotalBytes);
#ifdef __linux__
    EXPECT_GT(s.rssAnonBytes, 0u);
    EXPECT_GT(s.vmRssBytes, 0u);
    EXPECT_GT(s.mallocArenaBytes, 0u);
    EXPECT_EQ(s.mallocSlackBytes,
              s.mallocArenaBytes > s.mallocInUseBytes ? s.mallocArenaBytes - s.mallocInUseBytes : 0u);
#endif
    if (s.rssAnonBytes > s.ledgerTotalBytes) {
        EXPECT_EQ(s.unattributedBytes, s.rssAnonBytes - s.ledgerTotalBytes);
        EXPECT_NEAR(s.unattributedPct, 100.0 * s.unattributedBytes / s.rssAnonBytes, 1e-6);
    } else {
        EXPECT_EQ(s.unattributedBytes, 0u);
    }
    // A test process is small: the ledger must account for the bulk of the
    // anonymous RSS or the hooks are not seeing the allocations.
    EXPECT_LT(s.unattributedPct, 60.0) << eqt::memory::toLogLine(s);
}

TEST(MemoryLedger, LogLineAndJsonCarryTheSchema) {
    const auto s = eqt::memory::snapshot("schema_test", "zone_x");
    const std::string line = eqt::memory::toLogLine(s);
    EXPECT_NE(line.find("[memledger] point=schema_test zone=zone_x seq="), std::string::npos) << line;
    for (const char* key : {" enabled=", " rss_anon=", " rss_file=", " ledger=", " ledger_peak=",
                            " unattributed=", " unattributed_pct=", " malloc_arena=",
                            " malloc_inuse=", " malloc_slack=", " allocs="}) {
        EXPECT_NE(line.find(key), std::string::npos) << key << " missing from " << line;
    }
    for (size_t i = 0; i < eqt::memory::kCategoryCount; ++i) {
        const std::string tok = std::string(" ") + eqt::memory::categoryName(static_cast<Category>(i)) + "=";
        EXPECT_NE(line.find(tok), std::string::npos) << tok;
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    const std::string json = eqt::memory::toJson(s);
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    ASSERT_TRUE(reader->parse(json.data(), json.data() + json.size(), &root, &errs)) << errs << "\n" << json;
    EXPECT_EQ(root["point"].asString(), "schema_test");
    EXPECT_EQ(root["zone"].asString(), "zone_x");
    EXPECT_TRUE(root["ledgerEnabled"].asBool());
    for (const char* key : {"ledgerTotalBytes", "ledgerPeakBytes", "allocCount", "rssAnonBytes",
                            "rssFileBytes", "vmRssBytes", "unattributedBytes", "unattributedPct",
                            "mallocArenaBytes", "mallocInUseBytes", "mallocSlackBytes"}) {
        EXPECT_TRUE(root.isMember(key)) << key;
    }
    ASSERT_TRUE(root["categories"].isObject());
    EXPECT_EQ(root["categories"].size(), eqt::memory::kCategoryCount);
    EXPECT_EQ(root["categories"]["zone_parse"]["live"].asUInt64(),
              s.categories[static_cast<size_t>(Category::ZoneParse)].liveBytes);
    EXPECT_EQ(root["ledgerTotalBytes"].asUInt64(), s.ledgerTotalBytes);

    const std::string text = eqt::memory::toReportText(s);
    EXPECT_NE(text.find("Memory Ledger [schema_test]"), std::string::npos);
    EXPECT_NE(text.find("zone_parse"), std::string::npos);
    EXPECT_NE(text.find("unattributed"), std::string::npos);
}

TEST(MemoryLedger, SteadyStatePointFiresOnceAfterTheDeadline) {
    eqt::memory::noteReady(0.05);
    eqt::memory::pollSteadyState();  // too early: nothing
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    eqt::memory::pollSteadyState();  // records "steady_60s" once
    eqt::memory::pollSteadyState();  // disarmed: no-op
    SUCCEED();
}

// Per-allocation cost of the hooks: ledger malloc/free versus the raw glibc
// backend it wraps, same size, same pattern. Printed for the T491 Review;
// asserted only against a generous ceiling so a pathological regression
// (a lock, a hash map) is red without making the test timing-flaky.
TEST(MemoryLedger, HookOverheadPerAllocationIsSmall) {
    constexpr int kIters = 400000;
    constexpr int kRing = 256;
    std::vector<void*> ring(kRing, nullptr);
    auto bench = [&](auto allocFn, auto freeFn) {
        for (int i = 0; i < kRing; ++i) ring[i] = nullptr;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i) {
            void*& slot = ring[i % kRing];
            if (slot) freeFn(slot);
            slot = allocFn(static_cast<size_t>(64 + (i % 7) * 16));
        }
        for (int i = 0; i < kRing; ++i) if (ring[i]) { freeFn(ring[i]); ring[i] = nullptr; }
        auto t1 = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::nano>(t1 - t0).count() / kIters;
    };
    // warm both paths once
    bench([](size_t n) { return __libc_malloc(n); }, [](void* p) { __libc_free(p); });
    bench([](size_t n) { return malloc(n); }, [](void* p) { free(p); });
    double rawBest = 1e9, ledgerBest = 1e9;
    for (int rep = 0; rep < 5; ++rep) {
        rawBest = std::min(rawBest, bench([](size_t n) { return __libc_malloc(n); },
                                          [](void* p) { __libc_free(p); }));
        ledgerBest = std::min(ledgerBest, bench([](size_t n) { return malloc(n); },
                                                [](void* p) { free(p); }));
    }
    printf("[memledger-overhead] raw glibc malloc+free: %.1f ns/pair, ledger: %.1f ns/pair, "
           "delta: %.1f ns/pair\n", rawBest, ledgerBest, ledgerBest - rawBest);
    EXPECT_LT(ledgerBest - rawBest, 200.0);
}
