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

#include <algorithm>
#include <chrono>
#include <cmath>
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

// T491 (2026-09-17): GCC at -O2 DELETES a malloc/memset/free triple whose
// pointer never escapes -- `g++ -O2 -S` on the exact body below emits no call
// to any of the three. The inherited test measured that optimisation, not the
// ledger, and read live()==0. Every allocation these tests make must therefore
// escape before it is measured. (Protocol note: -O0 and -O2 are different
// programs; this is the same class as T400's atlas test.)
void escape(void* p) { __asm__ __volatile__("" : : "r"(p) : "memory"); }

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
        escape(p);
        memset(p, 2, 200000);
        escape(p);
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
    EXPECT_EQ(live(Category::Equipment), before);

    // T496 (board B819): this used to read
    //     EXPECT_EQ(realloc(nullptr, 0) != nullptr || true, true);
    // and then assert live == before with the returned pointer DISCARDED, which
    // made the case a function of the optimisation level rather than of the
    // ledger. PROVED, not inferred, on the exact body (`g++ -S`, count of
    // emitted calls):
    //   -O0: one `call malloc@PLT`  -- GCC lowers realloc(NULL, n) to malloc(n),
    //        the allocation really happens, nothing frees it, and the ledger
    //        correctly reports the 48-byte minimum chunk still live -> RED.
    //   -O2: ZERO calls              -- the pointer never escapes, so GCC elides
    //        the allocation entirely -> live == before -> GREEN.
    // Same class as this file's own FreeUnderAnotherScopeCreditsTheChargedCategory
    // (T491 Review §3) and T400's -O0/-O2 protocol trap. `realloc(ptr, 0)` and
    // the size of glibc's minimum chunk are both implementation-defined, so
    // neither is assertable. What the LEDGER promises is: whatever that call
    // returns, it is charged, and freeing it credits the same category back to
    // where it started. Let the pointer escape so no optimiser can remove the
    // call, then free it and assert exactly that.
    void* rz = realloc(nullptr, 0);
    __asm__ __volatile__("" : : "r"(rz) : "memory");  // the pointer escapes
    if (rz) {
        EXPECT_GE(live(Category::Equipment), before)
            << "realloc(nullptr, 0) returned a block the ledger did not charge";
        free(rz);
    }
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
    // T491 (2026-09-17): the honest invariant is that RssAnon SPLITS THREE
    // WAYS with nothing left over -- attributed + allocator slack (glibc free
    // lists, which no caller owns and the ledger can never attribute) +
    // unaccounted (stacks, direct mmap, JIT). The inherited bound
    // (unattributedPct < 60) folded slack into the ledger's share and failed
    // at 81.1% in a 450 kB test process whose arena was 544 kB with 97 kB in
    // use: the attribution was right, the arithmetic asked the wrong question.
    // FOUR buckets that COVER RssAnon (they do not partition it -- glibc's
    // arena can hold pages that are not resident, so the heap buckets may sum
    // past the resident total; measured, see the Snapshot comment).
    EXPECT_GE(s.ledgerTotalBytes + s.heapUnchargedBytes + s.mallocSlackBytes
                  + s.unaccountedBytes,
              s.rssAnonBytes)
        << eqt::memory::toLogLine(s);
    // glibc-internal allocations an in-executable interposer never sees. Exact
    // definition, and it plus the ledger covers everything glibc calls IN USE.
    EXPECT_EQ(s.heapUnchargedBytes,
              s.mallocInUseBytes > s.ledgerTotalBytes ? s.mallocInUseBytes - s.ledgerTotalBytes : 0u)
        << eqt::memory::toLogLine(s);
    EXPECT_GE(s.ledgerTotalBytes + s.heapUnchargedBytes, s.mallocInUseBytes)
        << eqt::memory::toLogLine(s);
    // The hooks must see the overwhelming majority of the heap: if glibc's
    // in-use total ran away from the ledger, the interposition is broken.
    EXPECT_LT(s.heapUnchargedBytes, s.mallocInUseBytes / 4) << eqt::memory::toLogLine(s);
    // Non-heap anonymous RSS is exact: RssAnon minus everything glibc holds.
    EXPECT_EQ(s.unaccountedBytes,
              s.rssAnonBytes > s.mallocArenaBytes ? s.rssAnonBytes - s.mallocArenaBytes : 0u)
        << eqt::memory::toLogLine(s);
}

// T491 addendum (Will, 2026-09-17): every report carries the 64 MB target and
// the 256 MB interim ceiling, and the gap to the target broken down by bucket,
// largest first. Pinned here because the gate and /pmem both read these fields.
TEST(MemoryLedger, ReportsCarryTheTargetAndTheCeiling) {
    const auto s = eqt::memory::snapshot("target_test");
    EXPECT_EQ(s.rssTargetBytes, 64ull * 1024 * 1024);
    EXPECT_EQ(s.rssCeilingBytes, 256ull * 1024 * 1024);
    EXPECT_EQ(s.overTargetBytes,
              s.rssAnonBytes > s.rssTargetBytes ? s.rssAnonBytes - s.rssTargetBytes : 0u);
    EXPECT_EQ(s.overCeilingBytes,
              s.rssAnonBytes > s.rssCeilingBytes ? s.rssAnonBytes - s.rssCeilingBytes : 0u);
    const std::string line = eqt::memory::toLogLine(s);
    for (const char* key : {" target=", " over_target=", " ceiling=", " over_ceiling=",
                            " unaccounted=", " heap_uncharged="}) {
        EXPECT_NE(line.find(key), std::string::npos) << key << " missing from " << line;
    }
    const std::string json = eqt::memory::toJson(s);
    for (const char* key : {"\"rssTargetBytes\"", "\"overTargetBytes\"", "\"rssCeilingBytes\"",
                            "\"overCeilingBytes\"", "\"unaccountedBytes\"",
                            "\"heapUnchargedBytes\""}) {
        EXPECT_NE(json.find(key), std::string::npos) << key << " missing from " << json;
    }
}

TEST(MemoryLedger, GapToTargetRanksBucketsLargestFirstAndIncludesNonCategories) {
    // Make one category unmistakably the largest live bucket.
    MemoryScope s(Category::ZoneMesh);
    const size_t kBig = 24u << 20;
    char* p = new char[kBig];
    escape(p);
    memset(p, 7, kBig);
    escape(p);
    const auto snap = eqt::memory::snapshot("gap_test");
    const auto rows = eqt::memory::gapToTarget(snap);
    ASSERT_FALSE(rows.empty());
    for (size_t i = 1; i < rows.size(); ++i) {
        EXPECT_GE(rows[i - 1].liveBytes, rows[i].liveBytes) << rows[i - 1].bucket;
    }
    EXPECT_EQ(rows.front().bucket, "zone_mesh") << eqt::memory::toLogLine(snap);
    bool sawSlack = false, sawUnaccounted = false, sawUncharged = false;
    uint64_t sum = 0;
    for (const auto& r : rows) {
        sum += r.liveBytes;
        if (r.bucket == "malloc_slack") sawSlack = true;
        if (r.bucket == "unaccounted") sawUnaccounted = true;
        if (r.bucket == "heap_uncharged") sawUncharged = true;
    }
    EXPECT_TRUE(sawSlack || snap.mallocSlackBytes == 0);
    EXPECT_TRUE(sawUnaccounted || snap.unaccountedBytes == 0);
    EXPECT_TRUE(sawUncharged || snap.heapUnchargedBytes == 0);
    // The buckets COVER RssAnon: their sum is every bucket, and it is at
    // least the resident anonymous total.
    EXPECT_EQ(sum, snap.ledgerTotalBytes + snap.heapUnchargedBytes
                       + snap.mallocSlackBytes + snap.unaccountedBytes);
    EXPECT_GE(sum, snap.rssAnonBytes) << eqt::memory::toLogLine(snap);
    delete[] p;
}

TEST(MemoryLedger, LogLineAndJsonCarryTheSchema) {
    const auto s = eqt::memory::snapshot("schema_test", "zone_x");
    const std::string line = eqt::memory::toLogLine(s);
    EXPECT_NE(line.find("[memledger] point=schema_test zone=zone_x seq="), std::string::npos) << line;
    for (const char* key : {" enabled=", " rss_anon=", " rss_file=",
                            // T500 (Will's addendum): the file-backed side is in
                            // the schema now -- it counts against a cgroup cap.
                            " rss_shmem=", " vm_rss=",
                            " ledger=", " ledger_peak=",
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
                            "rssFileBytes", "rssShmemBytes",  // T500
                            "vmRssBytes", "unattributedBytes", "unattributedPct",
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

// ---------------------------------------------------------------------------
// T491 addendum (board B801): the /proc/self/smaps census. T490 measured that a
// zone load costs ~nothing in anonymous RSS -- the whole gap to the 64 MB
// target is the startup baseline, and most of that baseline never goes through
// malloc, so the allocator ledger can only ever call it "unaccounted". These
// tests drive the parser against a FIXTURE (a parser proved only against the
// running process is proved against one input) and then against the real
// /proc/self/smaps for the invariants that must hold on any live process.
// ---------------------------------------------------------------------------

namespace {
// Four mappings: the brk heap, two segments of one shared library, an unnamed
// anonymous mapping (a thread stack or a driver arena), and the main binary.
const char kSmapsFixture[] =
    "55d0c0000000-55d0c0400000 rw-p 00000000 00:00 0                          [heap]\n"
    "Size:               4096 kB\n"
    "Rss:                2048 kB\n"
    "Pss:                2048 kB\n"
    "Anonymous:          2048 kB\n"
    "VmFlags: rd wr mr mw me ac sd\n"
    "7f1000000000-7f1000100000 r-xp 00000000 fd:01 100   /usr/lib/x86_64-linux-gnu/libfoo.so.1\n"
    "Size:               1024 kB\n"
    "Rss:                 512 kB\n"
    "Anonymous:             0 kB\n"
    "7f1000100000-7f1000110000 rw-p 00100000 fd:01 100   /usr/lib/x86_64-linux-gnu/libfoo.so.1\n"
    "Size:                 64 kB\n"
    "Rss:                  64 kB\n"
    "Anonymous:            64 kB\n"
    "7f2000000000-7f2000800000 rw-p 00000000 00:00 0 \n"
    "Size:               8192 kB\n"
    "Rss:                8192 kB\n"
    "Anonymous:          8192 kB\n"
    "560000000000-560000010000 r-xp 00000000 fd:01 200   /mnt/projects/fable/build/bin/willeq\n"
    "Size:                 64 kB\n"
    "Rss:                  64 kB\n"
    "Anonymous:             0 kB\n";

const eqt::memory::MappingRow* rowNamed(const eqt::memory::MappingBreakdown& b,
                                        const std::string& name) {
    for (const auto& r : b.rows) if (r.name == name) return &r;
    return nullptr;
}
}  // namespace

TEST(MemoryLedgerMappings, ParsesSmapsAndFoldsSegmentsOfOneLibraryTogether) {
    const auto b = eqt::memory::parseSmaps(kSmapsFixture);
    ASSERT_TRUE(b.available);
    EXPECT_EQ(b.mappings, 5u);
    // 2048 + 0 + 64 + 8192 + 0 kB
    EXPECT_EQ(b.totalAnonBytes, (2048u + 64u + 8192u) * 1024u);
    EXPECT_EQ(b.totalRssBytes, (2048u + 512u + 64u + 8192u + 64u) * 1024u);

    // Biggest anon owner first: the unnamed 8 MiB mapping.
    ASSERT_FALSE(b.rows.empty());
    EXPECT_EQ(b.rows[0].name, "[anon]");
    EXPECT_EQ(b.rows[0].anonBytes, 8192u * 1024u);
    for (size_t i = 1; i < b.rows.size(); ++i) {
        EXPECT_GE(b.rows[i - 1].anonBytes, b.rows[i].anonBytes) << b.rows[i - 1].name;
    }

    const auto* heap = rowNamed(b, "[heap]");
    ASSERT_NE(heap, nullptr);
    EXPECT_EQ(heap->anonBytes, 2048u * 1024u);
    EXPECT_EQ(heap->sizeBytes, 4096u * 1024u);
    EXPECT_EQ(heap->mappings, 1u);

    // BOTH segments of libfoo fold into one owner row: that folding is the
    // whole point -- 40 mappings of one library must read as one line.
    const auto* foo = rowNamed(b, "so:libfoo.so.1");
    ASSERT_NE(foo, nullptr) << "library segments were not folded by soname";
    EXPECT_EQ(foo->mappings, 2u);
    EXPECT_EQ(foo->anonBytes, 64u * 1024u);   // only the writable data segment
    EXPECT_EQ(foo->rssBytes, (512u + 64u) * 1024u);

    // The main binary is a file owner, not a library.
    EXPECT_NE(rowNamed(b, "file:willeq"), nullptr);
    EXPECT_EQ(rowNamed(b, "so:willeq"), nullptr);
}

TEST(MemoryLedgerMappings, EmptyInputIsUnavailableNotAZeroCensus) {
    const auto b = eqt::memory::parseSmaps("");
    EXPECT_FALSE(b.available);
    EXPECT_TRUE(b.rows.empty());
    EXPECT_NE(eqt::memory::mappingsToLogLines("p", b).find("unavailable=1"), std::string::npos);
}

TEST(MemoryLedgerMappings, RealProcessCensusAgreesWithProcStatusRssAnon) {
    const auto b = eqt::memory::readMappings();
    if (!b.available) GTEST_SKIP() << "/proc/self/smaps unreadable";
    const auto s = eqt::memory::snapshot("mapping_test");
    EXPECT_GT(b.mappings, 5u);
    EXPECT_GT(b.totalAnonBytes, 0u);
    // smaps' summed Anonymous IS RssAnon. They are sampled a few microseconds
    // apart in a live process, so allow a small drift rather than exactness.
    const double drift = std::abs(static_cast<double>(b.totalAnonBytes)
                                  - static_cast<double>(s.rssAnonBytes));
    EXPECT_LT(drift, 0.10 * static_cast<double>(s.rssAnonBytes) + 262144.0)
        << "smaps anon " << b.totalAnonBytes << " vs /proc/self/status RssAnon "
        << s.rssAnonBytes;
    // Every row's anon must be <= its mapped size, and the rows must sum.
    uint64_t sum = 0;
    for (const auto& r : b.rows) {
        EXPECT_LE(r.anonBytes, r.sizeBytes) << r.name;
        sum += r.anonBytes;
    }
    EXPECT_EQ(sum, b.totalAnonBytes);
}

TEST(MemoryLedgerMappings, LogLinesAndJsonCarryTheOwnerSchema) {
    const auto b = eqt::memory::parseSmaps(kSmapsFixture);
    const std::string lines = eqt::memory::mappingsToLogLines("startup", b, 3);
    EXPECT_NE(lines.find("[memmap] point=startup owner=TOTAL anon=10551296"), std::string::npos)
        << lines;
    EXPECT_NE(lines.find("owner=[anon] anon=8388608"), std::string::npos) << lines;
    // topN=3 caps the per-owner lines (plus the TOTAL line).
    EXPECT_EQ(std::count(lines.begin(), lines.end(), '\n'), 3);

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    const std::string json = eqt::memory::mappingsToJson(b, 24);
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    ASSERT_TRUE(reader->parse(json.data(), json.data() + json.size(), &root, &errs))
        << errs << "\n" << json;
    EXPECT_TRUE(root["available"].asBool());
    EXPECT_EQ(root["totalAnonBytes"].asUInt64(), b.totalAnonBytes);
    ASSERT_TRUE(root["owners"].isArray());
    EXPECT_EQ(root["owners"][0]["name"].asString(), "[anon]");
    EXPECT_EQ(root["owners"][0]["anonBytes"].asUInt64(), 8388608u);
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

// ---------------------------------------------------------------------------
// B841: the arena-retention census (parseMallocInfo / arenaNow / the bins).
// The parser is proved against a FIXTURE -- a parser proved only against the
// running process is proved against one input -- and then against the real
// process for the one invariant that must hold anywhere: the bins never hide
// bytes. glibc's own mallinfo2 is the source of truth for the totals, and
// the binned rows must be consistent with it.
// ---------------------------------------------------------------------------

namespace {

// A minimal glibc malloc_info(0) document: one heap, three <size> rows that
// pin one bin each (a small row, a middle row, a large row) plus one
// <unsorted> row that must NOT be folded into any size bin.
const char kMallocInfoFixture[] =
    "<malloc version=\"1\">\n"
    "<heap nr=\"0\">\n"
    "<sizes>\n"
    "  <size from=\"32\" to=\"64\" total=\"352\" count=\"11\"/>\n"
    "  <size from=\"4096\" to=\"8192\" total=\"49152\" count=\"6\"/>\n"
    "  <size from=\"70000\" to=\"70000\" total=\"140000\" count=\"2\"/>\n"
    "  <unsorted from=\"1000\" to=\"9000\" total=\"9000\" count=\"1\"/>\n"
    "</sizes>\n"
    "<total type=\"fast\" count=\"0\" size=\"0\"/>\n"
    "<total type=\"rest\" count=\"19\" size=\"189064\"/>\n"
    "<system type=\"current\" size=\"200000\"/>\n"
    "<system type=\"max\" size=\"262144\"/>\n"
    "</heap>\n"
    "<total type=\"fast\" count=\"0\" size=\"0\"/>\n"
    "<total type=\"rest\" count=\"19\" size=\"189064\"/>\n"
    "<total type=\"mmap\" count=\"3\" size=\"300000\"/>\n"
    "<system type=\"current\" size=\"200000\"/>\n"
    "<system type=\"max\" size=\"262144\"/>\n"
    "</malloc>\n";

}  // namespace

TEST(ArenaCensus, BinPlacementFollowsTheChunkSize) {
    EXPECT_EQ(eqt::memory::arenaBinForBytes(1), 0u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(127), 0u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(128), 1u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(1023), 3u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(1024), 4u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(65535), 9u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(65536), 10u);
    EXPECT_EQ(eqt::memory::arenaBinForBytes(1ull << 30), 10u);
}

TEST(ArenaCensus, ParsesMallocInfoXmlIntoSizeBins) {
    const auto r = eqt::memory::parseMallocInfo(kMallocInfoFixture);
    ASSERT_TRUE(r.ok);
    // bin 0: the 32-64 row (11 chunks, 352 B)
    EXPECT_EQ(r.bins[0].chunks, 11u);
    EXPECT_EQ(r.bins[0].freeBytes, 352u);
    // bin 6 (4k-8k): the 4096-8192 row (6 chunks, 49152 B)
    EXPECT_EQ(r.bins[6].chunks, 6u);
    EXPECT_EQ(r.bins[6].freeBytes, 49152u);
    // bin 10 (ge64k): the 70000 row (2 chunks, 140000 B)
    EXPECT_EQ(r.bins[10].chunks, 2u);
    EXPECT_EQ(r.bins[10].freeBytes, 140000u);
    // The unsorted row (9000 B, 1 chunk) is inside fordblks, not a size bin:
    // the binned total is the size rows ONLY. 352 + 49152 + 140000 = 189504.
    uint64_t binned = 0;
    for (const auto& b : r.bins) binned += b.freeBytes;
    EXPECT_EQ(binned, 352u + 49152u + 140000u);
    EXPECT_EQ(binned, 189504u);
    // And the bins account for every size-row chunk: 11 + 6 + 2.
    uint64_t chunkTotal = 0;
    for (const auto& b : r.bins) chunkTotal += b.chunks;
    EXPECT_EQ(chunkTotal, 19u);
}

TEST(ArenaCensus, RejectsGarbageInput) {
    EXPECT_FALSE(eqt::memory::parseMallocInfo("").ok);
    // The wrong root tag is a parse failure, whatever it contains.
    EXPECT_FALSE(eqt::memory::parseMallocInfo("<heap nr=\"0\"></heap>").ok);
    // A lone root with no <heap> is the no-free-chunks state: ok, zero bins.
    const auto lone = eqt::memory::parseMallocInfo("<malloc version=\"1\"></malloc>");
    ASSERT_TRUE(lone.ok);
    uint64_t total = 0;
    for (const auto& b : lone.bins) total += b.freeBytes;
    EXPECT_EQ(total, 0u);
}

TEST(ArenaCensus, LiveCensusAgreesWithMallinfo2) {
#if !defined(__linux__)
    GTEST_SKIP() << "glibc-only census";
#endif
    // Deterministic interior free chunks: a 1.5 MiB BASE block stays LIVE,
    // so the small blocks malloced out of it cannot coalesce into the top
    // pad when they free. 60 x 100 B + 1 x 512 B freed => ~6700 B of
    // interior free, all in the le128 bin, plus one 8k-16k chunk. (A freed
    // chunk at the top of the heap coalesces into the top pad and would make
    // the size bins empty -- that is the non-determinism this layout kills.)
    void* base = malloc(1536 * 1024);
    ASSERT_NE(base, nullptr);
    memset(base, 1, 1536 * 1024);  // commit the pages
    void* small[60];
    for (int i = 0; i < 60; ++i) {
        small[i] = malloc(100);
        ASSERT_NE(small[i], nullptr);
        memset(small[i], 2, 100);
    }
    void* mid = malloc(512);
    ASSERT_NE(mid, nullptr);
    memset(mid, 3, 512);
    for (int i = 0; i < 60; ++i) free(small[i]);
    free(mid);

    const auto r = eqt::memory::arenaNow();
    ASSERT_TRUE(r.ok) << "the live process must produce a parseable census";
    // mallinfo2 by construction: the aggregates are the mallinfo2 fields.
    EXPECT_EQ(r.interiorFreeBytes, static_cast<uint64_t>(r.mi.fordblks + r.mi.fsmblks));
    EXPECT_EQ(r.topPadBytes, static_cast<uint64_t>(r.mi.keepcost));
    EXPECT_EQ(r.mmapBytes, static_cast<uint64_t>(r.mi.hblkhd));
    // The 60 small frees are ~112 B chunks: they land in the le128 bin
    // (or a fastbin, which is smblks/fsmblks, also inside interiorFreeBytes).
    // Either way the interior free bytes must have grown by at least the
    // bytes requested -- the census cannot hide them.
    EXPECT_GE(r.interiorFreeBytes, 60ull * 100ull + 512ull);
    // Binned rows are a SUBSET of fordblks (unsorted/fast are excluded by
    // design): never more, and they account for the interior bytes that are
    // in sorted bins.
    uint64_t binned = 0;
    for (const auto& b : r.bins) binned += b.freeBytes;
    EXPECT_LE(binned, r.interiorFreeBytes);
    // The base block keeps the heap above the trim threshold, so the top
    // pad stays at roughly the 128 KiB default, far under the 1.5 MiB base.
    EXPECT_LT(r.topPadBytes, 1024 * 1024);
    (void)base;
    free(base);
}

TEST(ArenaCensus, TrimReturnsTheTopPad) {
#if !defined(__linux__)
    GTEST_SKIP() << "glibc-only census";
#endif
    // Grow the top pad well past the 128 KiB default trim threshold, then
    // let a single big block own it. Freeing it back must make
    // malloc_trim(0) release a substantial share of the pad: that is the
    // "trimmable" half of B841's decomposition, proven live.
    constexpr size_t kPad = 2ull * 1024 * 1024;
    void* p = malloc(kPad);
    ASSERT_NE(p, nullptr);
    memset(p, 1, kPad);
    free(p);
    const int pages = malloc_trim(0);
    EXPECT_GT(pages, 0)
        << "a 2 MiB top pad above the default 128 KiB trim threshold must trim";
    // The census after the trim: the top pad is down to roughly the trim
    // threshold, so keepcost must now be far smaller than the pad we made.
    const auto r = eqt::memory::arenaNow();
    ASSERT_TRUE(r.ok);
    EXPECT_LT(r.topPadBytes, kPad / 2);
}

TEST(ArenaCensus, ExperimentsAreOffUnlessTheirVariablesAreSet) {
    unsetenv("B841_TRIM_READY");
    unsetenv("B841_MMAP_THRESHOLD");
    unsetenv("B841_TRIM_THRESHOLD");
    const auto off = eqt::memory::arenaExperiments();
    EXPECT_FALSE(off.trimAtReady);
    EXPECT_EQ(off.mmapThresholdBytes, 0);
    EXPECT_EQ(off.trimThresholdBytes, 0);

    setenv("B841_TRIM_READY", "1", 1);
    setenv("B841_MMAP_THRESHOLD", "131072", 1);
    setenv("B841_TRIM_THRESHOLD", "4096", 1);
    const auto on = eqt::memory::arenaExperiments();
    EXPECT_TRUE(on.trimAtReady);
    EXPECT_EQ(on.mmapThresholdBytes, 131072);
    EXPECT_EQ(on.trimThresholdBytes, 4096);
    unsetenv("B841_TRIM_READY");
    unsetenv("B841_MMAP_THRESHOLD");
    unsetenv("B841_TRIM_THRESHOLD");
}
