// T47 -- Bridge pointers are never cleared at teardown (refactor/deferred.md
// "Bridge pointers are never cleared at teardown -- five dangling pointers",
// refactor/T47_bridge_teardown.md).
//
// Five pointers reference the bridge: EverQuest::m_bridge plus the four
// managers' (InventoryManager, SpellManager, SkillManager, HotbarModel).
// T46 wired EverQuest::setBridge()/wireBridgeToSubsystems() to propagate
// whatever pointer it's given -- including nullptr -- to all five at once,
// but nothing called it with nullptr at teardown. T47 adds exactly that
// call to Application::shutdown() (application.cpp, right after
// m_running.store(false), unconditional -- not gated by EQT_HAS_GRAPHICS --
// so it runs in both graphics and true-headless builds).
//
// Two things are proven here, deliberately kept in separate tests because
// they support different-strength claims (see the plan's "match the claim
// to the artifact" standard):
//
// 1. BridgeTeardownInvariant.* -- ALWAYS RUNS (both configures, normal
//    ctest, no sanitizer needed). Deterministic, no UAF risk: the bridge
//    object stays alive throughout, only the pointer to it is nulled. Pins
//    the invariant "EverQuest::setBridge(nullptr) -- the exact call
//    Application::shutdown() now issues -- stops HotbarModel from
//    forwarding events." This is the "acceptable fallback" evidence tier:
//    it demonstrates the mechanism the fix relies on, not the hazard itself.
//
// 2. BridgeTeardownAsanRepro.* -- SKIPPED BY DEFAULT (deliberately, via an
//    opt-in env var). These deliberately construct a genuine
//    heap-use-after-free: a HotbarModel wired to a heap-allocated bridge,
//    with the bridge destroyed WITHOUT clearing the model's pointer first
//    (the pre-T47 shape of the defect -- nothing called setBridge(nullptr)
//    before the bridge was destroyed). This is undefined behavior under a
//    plain build (may or may not crash) and would abort the whole ctest
//    binary under ASAN, so it must never run as part of the normal green
//    suite -- it is gated behind WILLEQ_T47_UAF_REPRO=1 and is meant to be
//    invoked directly, filtered to just these two tests, against an
//    ASAN-instrumented build. See refactor/T47_bridge_teardown.md's Review
//    for the captured ASAN output from doing exactly that.
//
// Framing note (matches the plan's "latent, not live" framing): this test
// constructs the underlying mechanism (a manager whose bridge pointer
// outlives the bridge object) directly, rather than driving
// Application::shutdown() end to end. That's not possible for this
// specific hazard: Application::shutdown() destroys EverQuest (and every
// manager with it) via a single m_eqClient.reset() at the end of the
// function, so by the time shutdown() returns, both the bridge and every
// manager that pointed at it are gone together -- there is no window
// reachable from outside Application to catch a manager alive with a
// freed bridge pointer post-shutdown. The real window this fix closes is
// INSIDE shutdown(), between the graphics path's bridge teardown
// (ShutdownGraphics()/m_stateSyncBridge.reset()) and the final
// m_eqClient.reset() a few lines later, during which (pre-fix)
// SpellManager/SkillManager/HotbarModel held dangling bridge pointers
// while still fully alive. This test reproduces that exact shape --
// manager alive, bridge freed, pointer not cleared -- at the smallest
// scale that demonstrates it.

#include <gtest/gtest.h>

#include "client/hotbar/hotbar_model.h"
#include "client/hotbar/hotbar_persistence.h"
#include "common/util/json_config.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace zonetest;

namespace {

TEST(HotbarPersistence, TwoCharactersHaveIndependentFiles) {
    const auto dir = std::filesystem::temp_directory_path() /
        ("willeq_h1_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const char* previous = std::getenv("WILLEQ_CACHE_DIR");
    const std::string saved = previous ? previous : "";
    ::setenv("WILLEQ_CACHE_DIR", dir.c_str(), 1);

    eqt::HotbarModel alice;
    eqt::HotbarModel bob;
    alice.setSpellAtGem([](uint8_t gem) { return gem == 0 ? 777u : 0u; });
    alice.assignSlot({2, 4}, eqt::HotbarSlotType::Spell, 777);
    bob.assignSlot({9, 4}, eqt::HotbarSlotType::Item, 888);
    EXPECT_TRUE(eqt::SaveCharacterHotbar("Alice", "server", alice));
    EXPECT_TRUE(eqt::SaveCharacterHotbar("Bob", "server", bob));
    eqt::HotbarModel aliceRead;
    eqt::HotbarModel bobRead;
    aliceRead.setSpellAtGem([](uint8_t gem) { return gem == 0 ? 777u : 0u; });
    EXPECT_EQ(eqt::LoadCharacterHotbar("Alice", "server", aliceRead), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(eqt::LoadCharacterHotbar("Bob", "server", bobRead), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(aliceRead.getSlot({2, 4}).id, 777u);
    EXPECT_EQ(aliceRead.getSlot({9, 4}).type, eqt::HotbarSlotType::Empty);
    EXPECT_EQ(bobRead.getSlot({9, 4}).id, 888u);
    EXPECT_EQ(bobRead.getSlot({2, 4}).type, eqt::HotbarSlotType::Empty);
    aliceRead.reset();  // BridgeComponent does this before an identity load.
    EXPECT_EQ(eqt::LoadCharacterHotbar("Bob", "server", aliceRead), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(aliceRead.getSlot({2, 4}).type, eqt::HotbarSlotType::Empty);
    EXPECT_EQ(aliceRead.getSlot({9, 4}).id, 888u);
    EXPECT_EQ(eqt::HotbarCacheName("O'Brian", "my server"),
              "ui/O_Brian_my_server.ini");

    if (saved.empty()) ::unsetenv("WILLEQ_CACHE_DIR");
    else ::setenv("WILLEQ_CACHE_DIR", saved.c_str(), 1);
    std::filesystem::remove_all(dir);
}

TEST(HotbarPersistence, LegacyConfigImportsOnceAndThenSavesPerCharacter) {
    const auto dir = std::filesystem::temp_directory_path() /
        ("willeq_h1_import_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    const char* previous = std::getenv("WILLEQ_CACHE_DIR");
    const std::string saved = previous ? previous : "";
    ::setenv("WILLEQ_CACHE_DIR", dir.c_str(), 1);
    const auto configPath = dir / "client.json";
    {
        std::ofstream config(configPath);
        config << R"({"clients":[{"hotbar":{"buttons":[{"type":2,"id":77}]}}]})";
    }

    EverQuest alice = MakeEq();
    alice.Bridge().SetConfigPath(configPath.string());
    EXPECT_EQ(alice.Bridge().LoadHotbarConfig(), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(alice.GetHotbarModel()->getSlot({0, 0}).id, 77u);
    EXPECT_TRUE(std::filesystem::exists(dir / eqt::HotbarCacheName("Testchar", "test")));
    auto config = EQ::JsonConfigFile::Load(configPath.string());
    EXPECT_FALSE(config.RawHandle()["clients"][0].isMember("hotbar"));

    TestBridge bridge;
    alice.setBridge(&bridge);
    alice.GetHotbarModel()->assignSlot({4, 3}, eqt::HotbarSlotType::Item, 999);
    eqt::HotbarModel savedAlice;
    EXPECT_EQ(eqt::LoadCharacterHotbar("Testchar", "test", savedAlice), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(savedAlice.getSlot({4, 3}).id, 999u);

    EverQuest bob("127.0.0.1", 0, "test", "test", "test", "Bob");
    bob.Bridge().SetConfigPath(configPath.string());
    EXPECT_EQ(bob.Bridge().LoadHotbarConfig(), eqt::HotbarLoadResult::Missing);
    EXPECT_EQ(bob.GetHotbarModel()->getSlot({0, 0}).type, eqt::HotbarSlotType::Empty);
    EXPECT_FALSE(std::filesystem::exists(dir / eqt::HotbarCacheName("Bob", "test")));

    alice.setBridge(nullptr);
    if (saved.empty()) ::unsetenv("WILLEQ_CACHE_DIR");
    else ::setenv("WILLEQ_CACHE_DIR", saved.c_str(), 1);
    std::filesystem::remove_all(dir);
}

TEST(HotbarPersistence, ExistingInvalidCharacterFileIsNeverReplacedByLegacyImport) {
    const auto dir = std::filesystem::temp_directory_path() /
        ("willeq_h1_invalid_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir / "ui");
    const char* previous = std::getenv("WILLEQ_CACHE_DIR");
    const std::string saved = previous ? previous : "";
    ::setenv("WILLEQ_CACHE_DIR", dir.c_str(), 1);
    const auto characterPath = dir / eqt::HotbarCacheName("Testchar", "test");
    const auto configPath = dir / "client.json";
    {
        std::ofstream character(characterPath);
        character << "[HotButtons]\nPage1Button1=bad!\n";
        std::ofstream config(configPath);
        config << R"({"clients":[{"hotbar":{"buttons":[{"type":2,"id":77}]}}]})";
    }
    EverQuest eq = MakeEq();
    eq.Bridge().SetConfigPath(configPath.string());
    EXPECT_EQ(eq.Bridge().LoadHotbarConfig(), eqt::HotbarLoadResult::Error);
    std::ifstream character(characterPath);
    const std::string contents((std::istreambuf_iterator<char>(character)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "[HotButtons]\nPage1Button1=bad!\n");
    EXPECT_EQ(eq.GetHotbarModel()->getSlot({0, 0}).type, eqt::HotbarSlotType::Empty);
    auto config = EQ::JsonConfigFile::Load(configPath.string());
    EXPECT_TRUE(config.RawHandle()["clients"][0].isMember("hotbar"));
    {
        std::ofstream malformed(characterPath, std::ios::trunc);
        malformed << "invalid JSON to preserve";
    }
    EXPECT_EQ(eq.Bridge().LoadHotbarConfig(), eqt::HotbarLoadResult::Error);
    std::ifstream stillMalformed(characterPath);
    const std::string unchanged((std::istreambuf_iterator<char>(stillMalformed)),
                                std::istreambuf_iterator<char>());
    EXPECT_EQ(unchanged, "invalid JSON to preserve");
    EXPECT_EQ(eq.GetHotbarModel()->getSlot({0, 0}).type, eqt::HotbarSlotType::Empty);
    if (saved.empty()) ::unsetenv("WILLEQ_CACHE_DIR");
    else ::setenv("WILLEQ_CACHE_DIR", saved.c_str(), 1);
    std::filesystem::remove_all(dir);
}

TEST(HotbarPersistence, RealCapturedCharacterIniRoundTrips) {
    // Copied byte-for-byte from the H-O official-client evidence.
    // B1090: the repo root, not __FILE__ -- a ccache hit from another
    // worktree would carry THAT worktree's __FILE__ into this binary.
    const auto captured = std::filesystem::path(WILLEQ_REPO_ROOT) /
        "tests/fixtures/hotbar/testchar-after-skill.ini";
    const auto dir = std::filesystem::temp_directory_path() /
        ("willeq_h1_capture_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir / "ui");
    const char* previous = std::getenv("WILLEQ_CACHE_DIR");
    const std::string saved = previous ? previous : "";
    ::setenv("WILLEQ_CACHE_DIR", dir.c_str(), 1);
    const auto path = dir / eqt::HotbarCacheName("Testchar", "test");
    ASSERT_TRUE(std::filesystem::copy_file(captured, path));
    eqt::HotbarModel model;
    ASSERT_EQ(eqt::LoadCharacterHotbar("Testchar", "test", model), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(model.getSlot({0, 0}).type, eqt::HotbarSlotType::Combat);
    EXPECT_EQ(model.getSlot({0, 0}).sourceCode, "J10");
    EXPECT_EQ(model.getSlot({0, 1}).type, eqt::HotbarSlotType::Skill);
    EXPECT_EQ(model.getSlot({0, 1}).sourceCode, "J30");
    ASSERT_TRUE(eqt::SaveCharacterHotbar("Testchar", "test", model));
    eqt::HotbarModel reread;
    ASSERT_EQ(eqt::LoadCharacterHotbar("Testchar", "test", reread), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(reread.getSlot({0, 0}).sourceCode, "J10");
    EXPECT_EQ(reread.getSlot({0, 1}).sourceCode, "J30");
    std::ifstream file(path);
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("[Friends]"), std::string::npos);
    EXPECT_NE(contents.find("Page1Button1=J10"), std::string::npos);
    EXPECT_NE(contents.find("Page1Button2=J30"), std::string::npos);
    if (saved.empty()) ::unsetenv("WILLEQ_CACHE_DIR");
    else ::setenv("WILLEQ_CACHE_DIR", saved.c_str(), 1);
    std::filesystem::remove_all(dir);
}

void ExpectObservedCode(const char* code, eqt::HotbarSlotType type, uint32_t id) {
    const auto dir = std::filesystem::temp_directory_path() /
        ("willeq_h1_codes_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir / "ui");
    const char* previous = std::getenv("WILLEQ_CACHE_DIR");
    const std::string saved = previous ? previous : "";
    ::setenv("WILLEQ_CACHE_DIR", dir.c_str(), 1);
    const auto path = dir / eqt::HotbarCacheName("Testchar", "test");
    {
        std::ofstream file(path);
        file << "[Other]\nKeep=1\n[HotButtons]\nPage1Button1=" << code << "\n";
    }
    eqt::HotbarModel model;
    model.setSpellAtGem([](uint8_t gem) { return gem == 0 ? 700u : gem == 2 ? 702u : 0u; });
    ASSERT_EQ(eqt::LoadCharacterHotbar("Testchar", "test", model), eqt::HotbarLoadResult::Loaded);
    EXPECT_EQ(model.getSlot({0, 0}).type, type);
    EXPECT_EQ(model.getSlot({0, 0}).id, id);
    EXPECT_EQ(model.getSlot({0, 0}).sourceCode, code);
    ASSERT_TRUE(eqt::SaveCharacterHotbar("Testchar", "test", model));
    std::ifstream file(path);
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, std::string("[Other]\nKeep=1\n[HotButtons]\nPage1Button1=") + code + "\n");
    if (saved.empty()) ::unsetenv("WILLEQ_CACHE_DIR");
    else ::setenv("WILLEQ_CACHE_DIR", saved.c_str(), 1);
    std::filesystem::remove_all(dir);
}

TEST(HotbarPersistence, ObservedE12) { ExpectObservedCode("E12", eqt::HotbarSlotType::Emote, 12); }
TEST(HotbarPersistence, ObservedJ10) { ExpectObservedCode("J10", eqt::HotbarSlotType::Combat, 10); }
TEST(HotbarPersistence, ObservedJ30) { ExpectObservedCode("J30", eqt::HotbarSlotType::Skill, 30); }
TEST(HotbarPersistence, ObservedH0) { ExpectObservedCode("H0", eqt::HotbarSlotType::Spell, 700); }
TEST(HotbarPersistence, ObservedH2) { ExpectObservedCode("H2", eqt::HotbarSlotType::Spell, 702); }
TEST(HotbarPersistence, ObservedF22) { ExpectObservedCode("F22", eqt::HotbarSlotType::Item, 22); }

TEST(BridgeTeardownInvariant, EverQuestSetBridgeNullStopsHotbarModelForwarding) {
    EverQuest eq = MakeEq();
    TestBridge bridge;

    eq.setBridge(&bridge);
    auto* hotbar = eq.GetHotbarModel();
    ASSERT_NE(hotbar, nullptr);

    // Wired: publishAllSlots() reaches the bridge for every slot.
    hotbar->publishAllSlots();
    auto wiredEvents = bridge.drainEvents();
    EXPECT_EQ(wiredEvents.size(), static_cast<size_t>(eqt::HotbarModel::SLOT_COUNT))
        << "expected one HotbarSlotAssigned event per slot while wired";
    for (const auto& event : wiredEvents) {
        EXPECT_EQ(event.type, eqt::state::GameEventType::HotbarSlotAssigned);
    }

    // This is the exact call T47 added to Application::shutdown() (both
    // configures, before the bridge is destroyed): EverQuest::setBridge(nullptr)
    // -> wireBridgeToSubsystems(nullptr) -> propagates to all four managers.
    eq.setBridge(nullptr);

    // Cleared: publishAllSlots() must not reach the bridge anymore.
    // publishSlot()'s `if (!bridge_) return;` guard makes this safe -- the
    // bridge object itself is still alive here (never freed in this test),
    // so this assertion carries no UAF risk either way.
    hotbar->publishAllSlots();
    auto clearedEvents = bridge.drainEvents();
    EXPECT_TRUE(clearedEvents.empty())
        << "HotbarModel forwarded " << clearedEvents.size()
        << " event(s) to the bridge after setBridge(nullptr) -- propagation broke";
}

// Companion sanity check: setBridge(nullptr) must not itself crash when
// there was never a live bridge to begin with (double-clear / clear before
// wire), matching the T46 Review's "idempotent, safe to call regardless"
// claim about wireBridgeToSubsystems().
TEST(BridgeTeardownInvariant, SetBridgeNullOnFreshEverQuestIsSafe) {
    EverQuest eq = MakeEq();
    eq.setBridge(nullptr);
    auto* hotbar = eq.GetHotbarModel();
    ASSERT_NE(hotbar, nullptr);
    hotbar->publishAllSlots();  // no bridge ever set -- must be a no-op, not a crash
    SUCCEED();
}

// ---------------------------------------------------------------------
// ASAN repro pair -- skipped by default. See file header for why these
// cannot be part of the normal green suite. To run:
//   WILLEQ_T47_UAF_REPRO=1 ./test_bridge_teardown \
//     --gtest_filter=BridgeTeardownAsanRepro.*
// against an ASAN-instrumented build (see refactor/T47_bridge_teardown.md
// Review for the exact tree/flags used and the captured output).
// ---------------------------------------------------------------------

bool AsanReproEnabled() {
    return std::getenv("WILLEQ_T47_UAF_REPRO") != nullptr;
}

// Pre-T47 shape of the defect: bridge destroyed while the manager's
// pointer to it is never cleared. publishAllSlots() -> publishSlot() ->
// bridge_->pushEvent() locks a std::mutex and push_back()s into a
// std::vector that both belong to the freed TestBridge -- a genuine
// heap-use-after-free ASAN will name.
TEST(BridgeTeardownAsanRepro, UnclearedPointerAfterBridgeDestructionIsUseAfterFree) {
    if (!AsanReproEnabled()) {
        GTEST_SKIP() << "Deliberately triggers a heap-use-after-free (T47 evidence). "
                         "Undefined behavior without a sanitizer, aborts the whole "
                         "binary under ASAN -- skipped unless WILLEQ_T47_UAF_REPRO=1. "
                         "See refactor/T47_bridge_teardown.md.";
    }

    eqt::HotbarModel model;
    auto* bridge = new TestBridge();
    model.setBridge(bridge);
    model.publishAllSlots();  // sanity: succeeds while the bridge is alive

    delete bridge;  // bridge destroyed -- model's pointer is NOT cleared first

    model.publishAllSlots();  // UAF: touches the freed bridge's mutex + vector
    SUCCEED();  // unreachable under ASAN; kept so a non-ASAN run reports a
                // recognizable pass/fail instead of just relying on the crash
}

// Post-T47 shape: the fix's mechanism (setBridge(nullptr) before the
// bridge is destroyed) applied directly at this scale. Same object graph
// as the test above, opposite ordering -- clean no-op, no sanitizer
// finding, run to provide the "after" half of the before/after pair.
TEST(BridgeTeardownAsanRepro, ClearedPointerAfterBridgeDestructionIsCleanNoOp) {
    if (!AsanReproEnabled()) {
        GTEST_SKIP() << "Paired with UnclearedPointerAfterBridgeDestructionIsUseAfterFree "
                         "-- skipped unless WILLEQ_T47_UAF_REPRO=1. "
                         "See refactor/T47_bridge_teardown.md.";
    }

    eqt::HotbarModel model;
    auto* bridge = new TestBridge();
    model.setBridge(bridge);
    model.publishAllSlots();

    model.setBridge(nullptr);  // T47's fix, applied before the bridge dies
    delete bridge;

    model.publishAllSlots();  // clean no-op: publishSlot()'s guard short-circuits
    SUCCEED();
}

}  // namespace
