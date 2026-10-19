// B1076 (GitLab #396) fix leg 2, review t_d00ada41 findings 1 and 2:
//
// 1. EDGE-ARROW PAGE TURN. The book's edge arrows (static_ui_input.cpp)
//    advanced currentPage -- a SPREAD index, SPELLS_PER_PAGE = 16 spells per
//    spread -- by SPELLS_PER_PAGE SPREADS per click, while the pre-existing
//    /newbookprev / /newbooknext slash commands stepped by ONE spread. On a
//    17-spread book a single forward click jumped to spread 16 (all 16 slots
//    blank, page numbers 33/34) and a back click clamped straight to 0, so no
//    intermediate spread was ever reachable by the arrows. Both now call the
//    ONE spread-turn body in spellbook_page_turn.h, so they cannot drift
//    apart again.
//
// 2. SCRIBE GAUGE NEVER CLEARED. static_ui_input.cpp's scribe's second click
//    wrote scribeProgress = 1.0 and the comment claimed a completion event
//    would clear it -- but SpellScribeCompleted (irrlicht_bridge.cpp) only
//    appended the spell, and close did not reset it, so the orange gauge
//    rendered at 100 % forever after a single scribe. The gauge is now driven
//    by the scribe lifecycle through the one writer pair scribeGaugeStart()
//    (the second click) / scribeGaugeClear() (the completion event, a fresh
//    snapshot, and the book closing), so the render can never outlive its
//    attempt.
//
// 3. SPELL SWAP WAS UNMEASURED AND COMMENTED FALSE. The committing
//    right-click did a bare std::swap on the render-thread vector with a
//    comment claiming it was oracle-measured ("client-local") and citing a
//    /swap command that does not exist. plan5_swap.tsv (UIGAP-ORACLE 205)
//    measured the COMMITTING second right-click on the lane-1 official client
//    2026-10-18: it sends OP_SwapSpell (0x2126), opcode + two uint32
//    spellbook-slot indices (the capture read slots 0 and 1), server-acked
//    with the same opcode and payload, the book display visibly exchanging
//    (icon cross-correlation 0.999). The gesture now pushes
//    SwapSpellbookSlotsIntent carrying the two real book-slot values; the
//    consumer (onSwapSpellbookSlotsIntent) moves the model through
//    SpellManager::swapSpellbookSlots() and queues the packet, so the render
//    copy and the game copy can no longer drift, and the server's book (the
//    thing the next zone-in reads) is updated too.
//
// Leg A drives the REAL StaticUIInput against a 17-spread book (the real
// arrows, the real rects, the real input path -- the slash commands are the
// SAME body, pinned by the one-header assertion below, so there is no second
// code path to test); leg B drives the gauge's real writers, including the
// REAL bridge arm (IrrlichtBridge::applyEvent on SpellScribeCompleted /
// SpellbookSnapshot) and the REAL toggle body's close arm; leg C drives the
// swap's real input arm and the model's exchange.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/bridge/irrlicht_bridge.h"
#include "client/eq.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/spellbook_layout.h"
#include "client/graphics/ui/spellbook_page_turn.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/state/event_bus.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

using namespace EQT::Graphics;

namespace {

// The real SQLite spell DB, opened once for the model leg (same preamble
// as test_spellbook_memorize): InitializeSpellSystem() reads it from the
// global instance.
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

bool hasDb() { return g_db_available; }

class B1076Fix2Bridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class SpellbookArrowTest : public ::testing::Test {
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
    B1076Fix2Bridge bridge;

    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        spellbook.isOpen = true;
    }

    void makeBook(int spreads) {
        // 17 spells -> pageCount() == 2 (16 + 1). A spread is the 16-slot
        // open book; a 17-spread book needs 17 * 16 spells.
        const int n = spreads * SpellbookPopupState::SPELLS_PER_PAGE;
        for (int i = 0; i < n; ++i) {
            SpellbookDisplayEntry e;
            e.slot = static_cast<uint16_t>(i);
            e.spellId = 3000u + i;
            e.name = "S" + std::to_string(i);
            spellbook.spells.push_back(e);
        }
    }

    // A click at the screen position of a window-relative rect (the layout
    // table's bookPopup origin + the layout's own rect).
    void clickRect(const irr::core::rect<irr::s32>& r) {
        const auto& p = layout.bookPopup;
        input.processInput(
            p.UpperLeftCorner.X + (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
            p.UpperLeftCorner.Y + (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2,
            /*leftClicked=*/true, layout, hotbar, gems, inventory, spellbook,
            skills, buffs, loot);
    }

    // A RIGHT click at the screen position of a window-relative rect. The
    // frame's click state (setFrameClickState) is what feeds rightClicked_
    // -- the real renderer calls it once per frame; the test sets it
    // directly, with no cursor (the swap arm never reads one).
    void rightClickRect(const irr::core::rect<irr::s32>& r) {
        const auto& p = layout.bookPopup;
        eqt::inventory::ClickModifiers mods;
        input.setFrameClickState(/*rightClicked=*/true, mods, nullptr);
        input.processInput(
            p.UpperLeftCorner.X + (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
            p.UpperLeftCorner.Y + (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2,
            /*leftClicked=*/false, layout, hotbar, gems, inventory, spellbook,
            skills, buffs, loot);
    }

    void clickForward() { clickRect(SpellBookLayout::pageUpRect()); }
    void clickBack() { clickRect(SpellBookLayout::pageDownRect()); }
};

// --- Finding 1: the arrows step ONE spread, like the slash commands -------

TEST_F(SpellbookArrowTest, ForwardArrowStepsOneSpread) {
    // The review's exact case: a book of 17 spreads, one forward click ->
    // spread 1. At 8dde4a4b this landed on spread 16 (the
    // SPELLS_PER_PAGE-spreads-per-click step), all 16 slots blank and the
    // page numbers 33/34.
    makeBook(17);
    clickForward();
    EXPECT_EQ(spellbook.currentPage, 1);
}

TEST_F(SpellbookArrowTest, ForwardArrowWalksThroughIntermediateSpreads) {
    // The unreachable middle: two forward clicks from 0 land on spread 2,
    // not past it. (8dde4a4b: one click already skipped to the end.)
    makeBook(17);
    clickForward();
    clickForward();
    EXPECT_EQ(spellbook.currentPage, 2);
}

TEST_F(SpellbookArrowTest, ForwardArrowClampsAtTheLastSpread) {
    makeBook(17);
    for (int i = 0; i < 30; ++i) clickForward();
    EXPECT_EQ(spellbook.currentPage, 16);
}

TEST_F(SpellbookArrowTest, BackArrowStepsOneSpread) {
    // Back from spread 2 -> spread 1. (8dde4a4b: back clamped to 0 in ONE
    // click, so the step was never observable below the last spread.)
    makeBook(17);
    spellbook.currentPage = 2;
    clickBack();
    EXPECT_EQ(spellbook.currentPage, 1);
    clickBack();
    EXPECT_EQ(spellbook.currentPage, 0);
    clickBack();  // already at 0: the clamp, not an underflow.
    EXPECT_EQ(spellbook.currentPage, 0);
}

TEST_F(SpellbookArrowTest, ArrowsAreTheSlashCommandsOwnBody) {
    // The whole point of the fix is one code path: the header's functions are
    // what both the arrows (static_ui_input.cpp) and /newbookprev /
    // /newbooknext (renderer_slash_commands.cpp) call. Pin the step they
    // implement so a "simplification" back to per-caller arithmetic fails.
    SpellbookPopupState b;
    for (int i = 0; i < 2 * SpellbookPopupState::SPELLS_PER_PAGE; ++i) {
        SpellbookDisplayEntry e;
        e.spellId = 7000u + i;
        b.spells.push_back(e);
    }
    EXPECT_EQ(b.pageCount(), 2);
    EXPECT_TRUE(turnSpellbookSpreadForward(b));
    EXPECT_EQ(b.currentPage, 1);
    EXPECT_FALSE(turnSpellbookSpreadForward(b)) << "clamps at the last spread";
    EXPECT_TRUE(turnSpellbookSpreadBack(b));
    EXPECT_EQ(b.currentPage, 0);
    EXPECT_FALSE(turnSpellbookSpreadBack(b)) << "clamps at 0";
}

// --- Finding 2: the scribe gauge is driven by its lifecycle ---------------

TEST(ScribeGaugeLifecycle, StartSetsItAndTheCompletionEventClearsIt) {
    // The real bridge arm: SpellScribeCompleted appends the spell AND clears
    // the gauge. At 8dde4a4b the arm only appended -- the gauge sat at 1.0
    // forever.
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);
    auto& book = renderer.Frame().spellbookState_;

    scribeGaugeStart(book);
    EXPECT_FLOAT_EQ(book.scribeProgress, 1.0f) << "the scribe's second click starts the gauge full";

    eqt::state::SpellScribeCompletedData d;
    d.spellId = 42;
    d.slot = 3;
    d.spellName = "Fireball";
    d.iconId = 1;
    bridge.applyEvent(eqt::state::GameEvent(eqt::state::GameEventType::SpellScribeCompleted, d));
    EXPECT_EQ(book.spells.size(), 1u) << "the arm still appends the scribed spell";
    EXPECT_FLOAT_EQ(book.scribeProgress, 0.0f) << "the attempt ended; the gauge empties";
}

TEST(ScribeGaugeLifecycle, AFreshSnapshotClearsIt) {
    // A zone-in snapshot is a fresh book: an in-flight gauge belongs to the
    // old book and goes with it.
    IrrlichtRenderer renderer;
    eqt::bridge::IrrlichtBridge bridge;
    bridge.setRenderer(&renderer);
    auto& book = renderer.Frame().spellbookState_;

    scribeGaugeStart(book);
    EXPECT_FLOAT_EQ(book.scribeProgress, 1.0f);

    eqt::state::SpellbookSnapshotData d;  // empty book
    bridge.applyEvent(eqt::state::GameEvent(eqt::state::GameEventType::SpellbookSnapshot, d));
    EXPECT_EQ(book.spells.size(), 0u);
    EXPECT_FLOAT_EQ(book.scribeProgress, 0.0f);
}

TEST(ScribeGaugeLifecycle, ClosingTheBookClearsIt) {
    // The other arm of the lifecycle: the close path (the SAME toggle body
    // Ctrl+B / /book / the Done X use) empties the gauge with the book.
    IrrlichtRenderer renderer;
    auto& f = renderer.Frame();
    f.spellbookState_.isOpen = true;
    scribeGaugeStart(f.spellbookState_);
    EXPECT_FLOAT_EQ(f.spellbookState_.scribeProgress, 1.0f);

    f.toggleSpellbookWindow();  // close
    EXPECT_FALSE(f.spellbookState_.isOpen);
    EXPECT_FLOAT_EQ(f.spellbookState_.scribeProgress, 0.0f) << "a closed book has no in-flight scribe";

    f.toggleSpellbookWindow();  // re-open: still clear
    EXPECT_TRUE(f.spellbookState_.isOpen);
    EXPECT_FLOAT_EQ(f.spellbookState_.scribeProgress, 0.0f);
}

TEST(ScribeGaugeLifecycle, TheScribeClickItselfStartsTheGauge) {
    // The producer arm, through the REAL StaticUIInput: the scroll is armed
    // (the inventory click's latch), a LEFT click on an open book slot fires
    // ScribeSpellFromScrollIntent AND starts the gauge.
    UILayout layout;
    layout.computeLayout(800, 600);
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;
    B1076Fix2Bridge bridge;
    StaticUIInput input;
    input.init(&bridge);
    spellbook.isOpen = true;
    // One scribed spell fills slot 0; slot 5 is open.
    {
        SpellbookDisplayEntry e;
        e.slot = 0;
        e.spellId = 999u;
        e.name = "Known";
        spellbook.spells.push_back(e);
    }
    spellbook.scribScrollSlot = 3;
    spellbook.scribScrollSpell = 1234u;

    const auto& p = layout.bookPopup;
    const auto r = SpellBookLayout::slotRect(5);
    input.processInput(
        p.UpperLeftCorner.X + (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
        p.UpperLeftCorner.Y + (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2,
        /*leftClicked=*/true, layout, hotbar, gems, inventory, spellbook,
        skills, buffs, loot);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::ScribeSpellFromScrollIntent>(intents[0]));
    const auto& s = std::get<eqt::events::ScribeSpellFromScrollIntent>(intents[0]);
    EXPECT_EQ(s.spellId, 1234u);
    EXPECT_EQ(spellbook.scribScrollSlot, -1) << "the arm disarms";
    EXPECT_FLOAT_EQ(spellbook.scribeProgress, 1.0f) << "the request is in flight: gauge full";
}

// --- Finding 3: the committing right-click is the wire, not a local swap --

// A two-spell book: slots 0 and 1, both occupied (render order == book
// order, so the entry's .slot is the vector index here).
TEST_F(SpellbookArrowTest, SwapCommitExchangesTheDisplayAndPushesTheIntent) {
    makeBook(2);
    spellbook.spells[0].slot = 0;  spellbook.spells[0].spellId = 100u;
    spellbook.spells[1].slot = 1;  spellbook.spells[1].spellId = 200u;

    // First right-click on slot 0: the latch. No wire, no intent.
    rightClickRect(SpellBookLayout::slotRect(0));
    EXPECT_EQ(spellbook.swapFirstSlot, 0);
    EXPECT_TRUE(bridge.drainIntents().empty())
        << "the FIRST right-click latches locally; only the commit goes on the wire";
    // The display did not move on the latch.
    EXPECT_EQ(spellbook.spells[0].spellId, 100u);

    // Second right-click on slot 1: the COMMIT.
    rightClickRect(SpellBookLayout::slotRect(1));
    EXPECT_EQ(spellbook.swapFirstSlot, -1) << "the commit disarms the latch";
    // The display exchanged (the oracle's visible result).
    EXPECT_EQ(spellbook.spells[0].spellId, 200u);
    EXPECT_EQ(spellbook.spells[1].spellId, 100u);
    // AND the intent carries the two real book-slot values for the game
    // thread's packet + model move.
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::SwapSpellbookSlotsIntent>(intents[0]));
    const auto& sw = std::get<eqt::events::SwapSpellbookSlotsIntent>(intents[0]);
    const bool isPair01 = (sw.slotA == 0 && sw.slotB == 1) ||
                          (sw.slotA == 1 && sw.slotB == 0);
    EXPECT_TRUE(isPair01)
        << "the intent carries book slots 0 and 1 (got " << sw.slotA << "/" << sw.slotB << ")";
}

TEST_F(SpellbookArrowTest, SwapLatchOnTheSameSlotIsACancel) {
    // Latch slot 0, right-click IT again: cancel -- no wire, nothing moves.
    // (The empty-side of the latch is the occupied?-i:-1 arm; the render
    // vector holds exactly the occupied slots, so an open slot is a vector
    // index past size() and latches nothing by construction.)
    makeBook(2);
    spellbook.spells[0].slot = 0;  spellbook.spells[0].spellId = 100u;
    spellbook.spells[1].slot = 1;  spellbook.spells[1].spellId = 200u;

    rightClickRect(SpellBookLayout::slotRect(0));
    EXPECT_EQ(spellbook.swapFirstSlot, 0) << "the occupied slot latches";
    EXPECT_TRUE(bridge.drainIntents().empty());

    rightClickRect(SpellBookLayout::slotRect(0));
    EXPECT_EQ(spellbook.swapFirstSlot, -1) << "the same slot cancels";
    EXPECT_TRUE(bridge.drainIntents().empty())
        << "a cancel sends nothing on the wire";
    // Nothing moved.
    EXPECT_EQ(spellbook.spells[0].spellId, 100u);
    EXPECT_EQ(spellbook.spells[1].spellId, 200u);
}

// The model side: the consumer's SpellManager::swapSpellbookSlots() moves
// the game thread's book (the thing the next zone-in reads). Same db
// preamble as test_spellbook_memorize (scribeSpell needs the spell rows).
TEST(SpellbookSwapModel, SwapsTwoOccupiedSlotsAndRefusesOutOfRange) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    ASSERT_TRUE(mgr->scribeSpell(111u, 0)) << "spell 111 must exist in the db";
    ASSERT_TRUE(mgr->scribeSpell(222u, 3));
    ASSERT_EQ(mgr->getSpellbookSpell(0), 111u);
    ASSERT_EQ(mgr->getSpellbookSpell(3), 222u);

    EXPECT_TRUE(mgr->swapSpellbookSlots(0, 3));
    EXPECT_EQ(mgr->getSpellbookSpell(0), 222u);
    EXPECT_EQ(mgr->getSpellbookSpell(3), 111u);

    EXPECT_FALSE(mgr->swapSpellbookSlots(0, EQ::MAX_SPELLBOOK_SLOTS));
    EXPECT_FALSE(mgr->swapSpellbookSlots(EQ::MAX_SPELLBOOK_SLOTS, 1));

    // Two empties: a no-op the gesture still reports as handled.
    EXPECT_TRUE(mgr->swapSpellbookSlots(1, 2));
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = WILLEQ_DB_PATH;
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
