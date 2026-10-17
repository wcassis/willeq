// T272 -- the spellbook popup's click gesture memorizes a spell.
//
// Before this unit the click handler in static_ui_input.cpp was a LOG stub
// whose comment named MemorizeSpellIntent as the missing piece (T264 kept
// the intent for exactly this), so the whole route was dead in BOTH
// directions:
//
//   * PRODUCER: nothing anywhere pushed MemorizeSpellIntent -- it sat on
//     test_reachability_sweep's kBlockedIntents list.
//   * CONSUMER: eq_bridge.cpp's arm called
//     SpellManager::memorizeSpell(uint32_t spell_id, uint8_t gem_slot)
//     as memorizeSpell(i.gemSlot, i.spellId) -- ARGUMENTS SWAPPED. It asked
//     to memorize spell <gem number> into gem <spell id truncated to 8
//     bits>. Nothing produced the intent, so the line never ran and the bug
//     was invisible. ConsumerTest below is red against that code.
//
// Three legs, in the order a click travels:
//   1. resolveMemorizeGem()          -- destination-gem policy, no device
//   2. StaticUIInput::processInput() -- the real producer, real intent queue
//   3. EverQuest::ProcessBridgeIntents() -- the real consumer, real
//      SpellManager, asserted on the gem the spell actually lands in.
//
// Leg 2 drives the REAL StaticUIInput against the REAL SpellGemPanelState
// the gem panel renders from, so the click and the on-screen destination
// marker can never disagree. Spellbook rows are populated directly on
// SpellbookPopupState (its production writer, IrrlichtBridge, needs a live
// IrrlichtRenderer); that hop is NOT covered here -- same honest limit
// T156's skills-popup test documented.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

using namespace EQT::Graphics;

namespace {

// ===================================================================
// Leg 1 -- resolveMemorizeGem() destination policy
// ===================================================================

TEST(ResolveMemorizeGem, EmptyPanelPicksFirstGem) {
    SpellGemPanelState gems;
    EXPECT_EQ(resolveMemorizeGem(gems), 0);
}

TEST(ResolveMemorizeGem, PicksFirstEmptyGemNotFirstGem) {
    SpellGemPanelState gems;
    gems.gems[0].spellId = 111;
    gems.gems[1].spellId = 222;
    EXPECT_EQ(resolveMemorizeGem(gems), 2);
}

TEST(ResolveMemorizeGem, SpellUnknownSentinelCountsAsEmpty) {
    // SpellGemState documents both 0 and SPELL_UNKNOWN (0xFFFFFFFF) as
    // "empty"; a panel fed from SpellManager carries the sentinel.
    SpellGemPanelState gems;
    for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) gems.gems[i].spellId = 0xFFFFFFFFu;
    gems.gems[0].spellId = 111;
    EXPECT_EQ(resolveMemorizeGem(gems), 1);
}

TEST(ResolveMemorizeGem, ExplicitTargetWinsOverFirstEmpty) {
    SpellGemPanelState gems;
    gems.memorizeTargetGem = 5;
    EXPECT_EQ(resolveMemorizeGem(gems), 5);
}

TEST(ResolveMemorizeGem, ExplicitTargetWinsEvenWhenOccupied) {
    // The whole point of picking a gem by hand: overwriting a full one.
    SpellGemPanelState gems;
    for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) gems.gems[i].spellId = 100 + i;
    gems.memorizeTargetGem = 3;
    EXPECT_EQ(resolveMemorizeGem(gems), 3);
}

TEST(ResolveMemorizeGem, AllGemsFullWithNoExplicitTargetReturnsMinusOne) {
    // Refuse rather than silently clobber gem 1.
    SpellGemPanelState gems;
    for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) gems.gems[i].spellId = 100 + i;
    EXPECT_EQ(resolveMemorizeGem(gems), -1);
}

TEST(ResolveMemorizeGem, OutOfRangeExplicitTargetIsIgnored) {
    SpellGemPanelState gems;
    gems.memorizeTargetGem = 99;
    EXPECT_EQ(resolveMemorizeGem(gems), 0);
    gems.memorizeTargetGem = -7;
    EXPECT_EQ(resolveMemorizeGem(gems), 0);
}

// ===================================================================
// Leg 2 -- the click producer
// ===================================================================

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// 800x600 geometry, same source of truth as test_static_ui_panels.cpp:
//   centerPopup: {264, 65, 664, 415}, spellGems: {748, 192, 796, 482}
//   (B1075: the spellbook is the layout table's CentreSlot; the gem window
//   is right-aligned and ends MARGIN above the 1x10 hotbar's top, y=486;
//   B1077: the oracle's 48x290 window, so it is wider than the old gem bar)
// Spellbook rows: startY = 65 + 24 = 89, LINE_H 28 + MARGIN 4 = 32 pitch.
//   row 0 -> y in [89, 117)    -> probe y=100
//   row 1 -> y in [121, 149)   -> probe y=130
// Gems (B1077): the oracle's 36x28 cells at (3, 10+30i) in the window,
// screen x in [751, 787) (probe x=770), top y = 192 + 10 + 30i = 202 + 30i.
//   gem 0 -> y in [202, 230) -> probe y=216
//                                   gem 2 -> y in [262, 290) -> probe y=286
class SpellbookClickTest : public ::testing::Test {
protected:
    UILayout layout;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;
    StaticUIInput input;
    TestBridge bridge;

    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        spellbook.isOpen = true;
        for (int i = 0; i < 3; ++i) {
            SpellbookDisplayEntry e;
            e.slot = static_cast<uint16_t>(i);
            e.spellId = 3000u + i;
            e.name = "Spell " + std::to_string(i);
            e.level = 1;
            spellbook.spells.push_back(e);
        }
    }

    void click(int x, int y) {
        input.processInput(x, y, /*leftClicked=*/true, layout, hotbar, gems,
                            inventory, spellbook, skills, buffs, loot);
    }
};

TEST_F(SpellbookClickTest, ClickOnFirstRowPushesMemorizeIntentForThatSpell) {
    click(300, 100);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::MemorizeSpellIntent>(intents[0]));
    const auto& m = std::get<eqt::events::MemorizeSpellIntent>(intents[0]);
    EXPECT_EQ(m.spellId, 3000u);
    EXPECT_EQ(m.gemSlot, 0);
}

TEST_F(SpellbookClickTest, ClickOnSecondRowCarriesThatRowsSpellId) {
    click(300, 130);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto& m = std::get<eqt::events::MemorizeSpellIntent>(intents[0]);
    EXPECT_EQ(m.spellId, 3001u);
}

TEST_F(SpellbookClickTest, PageOffsetIsAppliedToTheRowIndex) {
    // Nine spells -> two pages; page 1 row 0 is the ninth spell.
    for (int i = 3; i < 9; ++i) {
        SpellbookDisplayEntry e;
        e.spellId = 3000u + i;
        e.name = "Spell " + std::to_string(i);
        spellbook.spells.push_back(e);
    }
    spellbook.currentPage = 1;
    click(300, 100);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<eqt::events::MemorizeSpellIntent>(intents[0]).spellId, 3008u);
}

TEST_F(SpellbookClickTest, ClickOnAnEmptyRowPushesNothing) {
    // Three spells populated, row 5 is past the end of the page.
    click(300, 89 + 5 * 32 + 4);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(SpellbookClickTest, DestinationIsTheFirstEmptyGem) {
    gems.gems[0].spellId = 500;
    gems.gems[1].spellId = 501;
    click(300, 100);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<eqt::events::MemorizeSpellIntent>(intents[0]).gemSlot, 2);
}

TEST_F(SpellbookClickTest, GemClickWhileSpellbookOpenSelectsTargetInsteadOfCasting) {
    click(770, 286);  // gem 2

    EXPECT_EQ(gems.memorizeTargetGem, 2);
    EXPECT_TRUE(bridge.drainIntents().empty()) << "a gem click must not cast while the book is open";
}

TEST_F(SpellbookClickTest, ClickingTheSelectedGemAgainClearsTheSelection) {
    click(770, 286);
    ASSERT_EQ(gems.memorizeTargetGem, 2);
    click(770, 286);
    EXPECT_EQ(gems.memorizeTargetGem, -1);
}

TEST_F(SpellbookClickTest, SelectedGemBecomesTheMemorizeDestinationAndIsThenConsumed) {
    click(770, 286);            // pick gem 2 as the destination
    click(300, 100);            // memorize row 0 there

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<eqt::events::MemorizeSpellIntent>(intents[0]).gemSlot, 2);
    // The explicit pick applies to the click it targeted; the next one falls
    // back to "first empty gem".
    EXPECT_EQ(gems.memorizeTargetGem, -1);
}

TEST_F(SpellbookClickTest, AllGemsFullAndNoPickPushesNothing) {
    for (int i = 0; i < static_cast<int>(EQT::VISIBLE_SPELL_GEM_COUNT); ++i) gems.gems[i].spellId = 100 + i;
    click(300, 100);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(SpellbookClickTest, GemClickWithSpellbookClosedStillCasts) {
    // The cast gesture is only diverted while the book is open -- pin that
    // the ordinary case is untouched.
    spellbook.isOpen = false;
    click(770, 216);  // gem 0 (B1077: the oracle's cell at the window's (3,10))

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::CastSpellIntent>(intents[0]));
    EXPECT_EQ(gems.memorizeTargetGem, -1);
}

TEST_F(SpellbookClickTest, ClosedSpellbookRowClickPushesNothing) {
    spellbook.isOpen = false;
    click(300, 100);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ===================================================================
// Leg 3 -- the consumer arm in eq_bridge.cpp
// ===================================================================

// The real SQLite spell DB is opened once for the whole binary and
// published as the global instance in main() below -- EverQuest::
// InitializeSpellSystem() reads it from there (test_spell_manager.cpp's
// precedent). DB-independent legs run regardless.
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) return env_path;
    return WILLEQ_DB_PATH;
}

bool hasDb() { return g_db_available; }

uint32_t FindSpellId(EQ::SpellDatabase& db, uint32_t lo, uint32_t hi,
                      const std::function<bool(const EQ::SpellData&)>& pred) {
    for (uint32_t id = lo; id <= hi; ++id) {
        const EQ::SpellData* s = db.getSpell(id);
        if (s && pred(*s)) return id;
    }
    return 0;
}

TEST(SpellbookMemorizeConsumer, IntentMemorizesTheNamedSpellIntoTheNamedGem) {
    // RED before T272: the arm's arguments were swapped, so this asked for
    // "memorize spell 6 into gem <id & 0xFF>" -- gem_slot >= EQT::VISIBLE_SPELL_GEM_COUNT,
    // an immediate false, nothing memorized in gem 6 or anywhere else.
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const EQ::SpellData& s) {
        return s.canClassUse(EQ::PlayerClass::Cleric, 60);
    });
    if (id == 0) GTEST_SKIP() << "no Cleric-usable spell found in scanned range";
    ASSERT_GT(id, 8u) << "spell id must exceed EQT::VISIBLE_SPELL_GEM_COUNT for the swap to be detectable";

    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    ASSERT_TRUE(mgr->scribeSpell(id, 0));
    eq.GetGameState().player().setClass(static_cast<uint32_t>(EQ::PlayerClass::Cleric));
    eq.GetGameState().player().setLevel(60);

    constexpr uint8_t kGem = 6;
    bridge.pushIntent(eqt::events::MemorizeSpellIntent{kGem, id});
    eq.Bridge().ProcessBridgeIntents();

    EXPECT_TRUE(mgr->isMemorizing());
    EXPECT_EQ(mgr->m_memorize_slot, kGem);
    EXPECT_EQ(mgr->m_memorize_spell_id, id);
    EXPECT_EQ(mgr->getGemState(kGem), EQ::GemState::MemorizeProgress);
}

TEST(SpellbookMemorizeConsumer, IntentForAnUnscribedSpellMemorizesNothing) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    uint32_t id = FindSpellId(mgr->getDatabase(), 1, 4000, [](const EQ::SpellData&) { return true; });
    if (id == 0) GTEST_SKIP() << "no spell found in scanned range";

    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    // Deliberately NOT scribed.
    bridge.pushIntent(eqt::events::MemorizeSpellIntent{0, id});
    eq.Bridge().ProcessBridgeIntents();

    EXPECT_FALSE(mgr->isMemorizing());
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = getWilleqDbPath();
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
