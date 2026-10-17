// B1025-H3 (GitLab #345): the hotbar assignment gesture's state machine.
//
// The oracle (H-O, refactor/B1025-H0/README.md "Measured facts"): a 0.117 s
// gem press released with NO clone and a cast fired; a long hold cloned by
// ~1.0-1.1 s (ability and occupied-slot ~1.0 s, inventory 0.72-1.02 s);
// clones SURVIVE release (gestures.tsv:3, "remained attached to cursor on
// release"); a world click dismisses a carried button (gestures.tsv "
// hotbar-worlddrop"); placing over an occupied slot replaces it and leaves
// the displaced button on the cursor (gestures.tsv "hotbar-swap"); a hold on
// an occupied slot clears its source (gestures.tsv "hotbar-pickup").
//
// These tests drive the PURE functions in hotbar_assignment.h -- the hold
// timer, the deferred-action holder, the displacement -- with no device and
// no layout, the same idiom as test_skill_reachability's hotbar leg (the
// bridge there is a trivial concrete subclass; pushIntent/drainIntents are
// concrete on the base).

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/hotbar_assignment.h"
#include "client/graphics/ui/hotbar_layout.h"
#include "client/graphics/ui/hotbutton_holder.h"
#include "client/graphics/ui/inventory_layout.h"  // B1025-H3: the item-hold test's slot rect
#include "client/graphics/ui/tradeskill_panel.h"  // B1025-H3: processPanelInput's other panel
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/hotbar/hotbar_slot_type.h"

using EQT::Graphics::CursorAttachmentState;
using EQT::Graphics::HotbarHoldSource;
using EQT::Graphics::HotbarPanelState;
using EQT::Graphics::HotbarSlotState;
using EQT::Graphics::HotButtonHolder;
using EQT::Graphics::kHotbarHoldThresholdMs;
using EQT::Graphics::PopupType;          // B1025-H3: the press-release/hold E2E geometry
using EQT::Graphics::UILayout;
namespace IL = EQT::Graphics::InventoryLayout;          // namespace, not a type
namespace CAL = EQT::Graphics::CombatAbilityLayout;     // namespace, not a type
using eqt::HotbarSlotType;

namespace {

class NoopBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

HotbarHoldSource GemSource(int gem, uint32_t spellId, const std::string& name) {
    HotbarHoldSource s;
    s.kind = HotbarHoldSource::Kind::Gem;
    s.id = static_cast<uint32_t>(gem);
    s.cloneType = HotbarSlotType::Spell;
    s.cloneId = spellId;
    s.name = name;
    return s;
}

HotbarHoldSource SlotSource(int page, int slot, HotbarSlotType type,
                            uint32_t id, const std::string& name) {
    HotbarHoldSource s;
    s.kind = HotbarHoldSource::Kind::Slot;
    s.id = static_cast<uint32_t>(slot);
    s.page = page;
    s.slot = slot;
    s.cloneType = type;
    s.cloneId = id;
    s.name = name;
    return s;
}

// ===================================================================
// tickHotbarHold -- the hold timer
// ===================================================================

class HotbarHoldTimerTest : public ::testing::Test {};

// A press that releases BEFORE the threshold: no clone, timer zeroed.
TEST_F(HotbarHoldTimerTest, ShortPressDoesNotClone) {
    NoopBridge bridge;
    CursorAttachmentState cursor;
    const auto src = GemSource(0, 278, "Frost Armor");

    // 0.117 s at 60 fps is seven frames of ~16.7 ms.
    for (int i = 0; i < 7; ++i) {
        EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, src, 0.0167f));
    }
    EXPECT_FALSE(cursor.hasHotbutton);
    // The release frame runs the timer down.
    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, false, src, 0.0167f));
    EXPECT_EQ(cursor.holdElapsedMs, 0.0f);
    EXPECT_FALSE(cursor.hasHotbutton);
    (void)bridge;
}

// A hold that CROSSES the threshold: the clone appears on the crossing frame,
// carries the source's kind/id, and the timer resets.
TEST_F(HotbarHoldTimerTest, HoldCrossingTheThresholdClones) {
    NoopBridge bridge;
    CursorAttachmentState cursor;
    const auto src = GemSource(3, 412, "Haste");

    // 200 ms of holding: under the 300 ms threshold, no clone yet.
    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, src, 0.100f));
    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, src, 0.100f));
    EXPECT_FALSE(cursor.hasHotbutton);
    // The crossing frame: 300 ms total.
    EXPECT_TRUE(EQT::Graphics::tickHotbarHold(cursor, true, src, 0.100f));
    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Spell);
    EXPECT_EQ(cursor.hotbutton.id, 412u);
    EXPECT_EQ(cursor.hotbutton.text, "Haste");
    EXPECT_EQ(cursor.holdElapsedMs, 0.0f);  // reset after the clone
    (void)bridge;
}

// A move OFF the source (with the button still down) restarts the timer on
// the new source; time on one source is not credited to another.
TEST_F(HotbarHoldTimerTest, MovingOffSourceResetsTheTimer) {
    NoopBridge bridge;
    CursorAttachmentState cursor;
    const auto gem0 = GemSource(0, 278, "Frost Armor");
    const auto gem1 = GemSource(1, 330, "Stone Skin");

    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, gem0, 0.200f));
    EXPECT_NEAR(cursor.holdElapsedMs, 200.0f, 1.0f);
    // The pointer is on a DIFFERENT gem: reset, remember, no accumulation.
    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, gem1, 0.100f));
    EXPECT_NEAR(cursor.holdElapsedMs, 0.0f, 1.0f);
    EXPECT_FALSE(cursor.hasHotbutton);
    // Back on gem0: it starts again from zero, even though gem0 had 200 ms.
    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, gem0, 0.100f));
    EXPECT_NEAR(cursor.holdElapsedMs, 0.0f, 1.0f);
    (void)bridge;
}

// No source under the pointer: the timer cannot run at all.
TEST_F(HotbarHoldTimerTest, NoSourceNeverClones) {
    NoopBridge bridge;
    CursorAttachmentState cursor;
    const HotbarHoldSource none;
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, none, 0.016f));
    }
    EXPECT_FALSE(cursor.hasHotbutton);
    (void)bridge;
}

// With a clone already on the cursor the timer is done: the press belongs to
// the button, not the source, and the accumulator runs back to zero.
TEST_F(HotbarHoldTimerTest, ExistingCloneStopsTheTimer) {
    NoopBridge bridge;
    CursorAttachmentState cursor;
    cursor.hasHotbutton = true;
    cursor.holdElapsedMs = 123.0f;
    const auto src = GemSource(0, 278, "Frost Armor");
    EXPECT_FALSE(EQT::Graphics::tickHotbarHold(cursor, true, src, 0.100f));
    EXPECT_EQ(cursor.holdElapsedMs, 0.0f);
    EXPECT_TRUE(cursor.hasHotbutton);  // the old clone is kept
    (void)bridge;
}

// A pickup clone DISPLACES the item the cursor was holding: the hot button
// and the item do not share a cursor (the oracle's pickup replaces the
// cursor's contents).
TEST_F(HotbarHoldTimerTest, CloneDisplacesAHeldItem) {
    NoopBridge bridge;
    CursorAttachmentState cursor;
    cursor.hasItem = true;
    cursor.itemId = 21779;
    cursor.itemName = "Rusty Dagger";
    cursor.iconId = 42;
    cursor.quantity = 1;
    const auto src = GemSource(2, 501, "Fireball");
    EXPECT_TRUE(EQT::Graphics::tickHotbarHold(cursor, true, src, 1.0f));
    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Spell);
    EXPECT_EQ(cursor.hotbutton.id, 501u);
    EXPECT_FALSE(cursor.hasItem);
    EXPECT_EQ(cursor.itemId, 0u);
    EXPECT_TRUE(cursor.itemName.empty());
    EXPECT_EQ(cursor.quantity, 0);
    (void)bridge;
}

// ===================================================================
// HotButtonHolder -- the deferred source action
// ===================================================================

class HotButtonHolderTest : public ::testing::Test {
protected:
    NoopBridge bridge;
    HotButtonHolder holder;
    const HotbarHoldSource src{
        HotbarHoldSource::Kind::Slot, 4, HotbarSlotType::Spell, 278,
        0, 4, "Frost Armor", 0};
};

TEST_F(HotButtonHolderTest, ReleaseOverSourceFiresTheAction) {
    bool pushed = false;
    holder.begin(src, [&pushed](eqt::bridge::GameStateBridge&) { pushed = true; });
    // Still holding over the source: pending.
    EXPECT_FALSE(holder.step(src, /*buttonDown=*/true, bridge));
    EXPECT_TRUE(holder.hasPending());
    // The release frame, still over the source: fires.
    EXPECT_TRUE(holder.step(src, /*buttonDown=*/false, bridge));
    EXPECT_TRUE(pushed);
    EXPECT_FALSE(holder.hasPending());
}

TEST_F(HotButtonHolderTest, MovingOffSourceCancels) {
    bool pushed = false;
    holder.begin(src, [&pushed](eqt::bridge::GameStateBridge&) { pushed = true; });
    const HotbarHoldSource other{
        HotbarHoldSource::Kind::Slot, 5, HotbarSlotType::Spell, 300,
        0, 5, "Other", 0};
    EXPECT_FALSE(holder.step(other, /*buttonDown=*/true, bridge));
    EXPECT_FALSE(holder.hasPending());
    EXPECT_FALSE(pushed);
}

TEST_F(HotButtonHolderTest, ReleasingElsewhereCancels) {
    // The pointer is over a different source when the button releases: that
    // is a move-off, not a release over the source.
    bool pushed = false;
    holder.begin(src, [&pushed](eqt::bridge::GameStateBridge&) { pushed = true; });
    const HotbarHoldSource other{
        HotbarHoldSource::Kind::Slot, 5, HotbarSlotType::Spell, 300,
        0, 5, "Other", 0};
    EXPECT_FALSE(holder.step(other, /*buttonDown=*/false, bridge));
    EXPECT_FALSE(pushed);
}

TEST_F(HotButtonHolderTest, CancelDropsWithoutFiring) {
    bool pushed = false;
    holder.begin(src, [&pushed](eqt::bridge::GameStateBridge&) { pushed = true; });
    holder.cancel();
    EXPECT_FALSE(holder.hasPending());
    EXPECT_FALSE(pushed);
    // A step after a cancel is a no-op.
    EXPECT_FALSE(holder.step(src, /*buttonDown=*/false, bridge));
    EXPECT_FALSE(pushed);
}

// ===================================================================
// stepHotbarGesture -- the clone/pickup/cancel half
// ===================================================================

class HotbarGestureStepTest : public ::testing::Test {
protected:
    NoopBridge bridge;
    CursorAttachmentState cursor;
    HotButtonHolder holder;
};

// THE PICKUP: a clone on the cursor cancels the press's pending action AND,
// when the press began on an occupied slot, clears that source (H-O
// "hotbar-pickup": "source INI key removed"). The clear is a
// HotbarClearIntent carrying the slot's own page/slot.
TEST_F(HotbarGestureStepTest, CloneOnPickupClearsTheSlotSource) {
    cursor.hasHotbutton = true;
    holder.begin(
        SlotSource(2, 7, HotbarSlotType::Emote, 0, "/say hi"),
        [](eqt::bridge::GameStateBridge&) {});

    EQT::Graphics::stepHotbarGesture(cursor, holder, HotbarHoldSource{},
                                     /*buttonDown=*/true, bridge);

    ASSERT_FALSE(holder.hasPending());
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* clear = std::get_if<eqt::events::HotbarClearIntent>(&intents[0]);
    ASSERT_NE(clear, nullptr);
    EXPECT_EQ(clear->page, 2);
    EXPECT_EQ(clear->slot, 7);
}

// A clone from a NON-slot source (a gem) cancels the pending action but
// clears nothing: there is no slot whose INI key is the clone's source.
TEST_F(HotbarGestureStepTest, CloneOnGemSourceCancelsWithoutClearing) {
    cursor.hasHotbutton = true;
    holder.begin(GemSource(1, 412, "Haste"),
                 [](eqt::bridge::GameStateBridge&) {});

    EQT::Graphics::stepHotbarGesture(cursor, holder, HotbarHoldSource{},
                                     /*buttonDown=*/true, bridge);

    EXPECT_FALSE(holder.hasPending());
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// No clone on the cursor: the holder gets its plain one-frame step. A
// release over the source fires (the short-press activate), not clears.
TEST_F(HotbarGestureStepTest, NoCloneReleasesToTheHolder) {
    bool pushed = false;
    holder.begin(SlotSource(0, 3, HotbarSlotType::Skill, 73, "Taunt"),
                 [&pushed](eqt::bridge::GameStateBridge&) { pushed = true; });
    const auto src = SlotSource(0, 3, HotbarSlotType::Skill, 73, "Taunt");

    EQT::Graphics::stepHotbarGesture(cursor, holder, src,
                                     /*buttonDown=*/false, bridge);

    EXPECT_TRUE(pushed);
    EXPECT_FALSE(holder.hasPending());
    // The SHORT press must not clear its own slot: the activation fired
    // instead.
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ===================================================================
// applyHotbarAssignPending -- the /hotbutton command's button
// ===================================================================

class HotbarAssignPendingTest : public ::testing::Test {};

TEST_F(HotbarAssignPendingTest, TheCommandButtonLandsOnTheCursor) {
    CursorAttachmentState cursor;
    eqt::state::HotbarAssignPendingData data;
    data.type = HotbarSlotType::Emote;
    data.id = 0;
    data.text = "/say hi";
    data.iconId = 0;

    EQT::Graphics::applyHotbarAssignPending(cursor, data);

    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Emote);
    EXPECT_EQ(cursor.hotbutton.text, "/say hi");
    EXPECT_EQ(cursor.hotbutton.id, 0u);
}

TEST_F(HotbarAssignPendingTest, TheCommandButtonReplacesACarriedClone) {
    CursorAttachmentState cursor;
    cursor.hasHotbutton = true;
    cursor.hotbutton = eqt::HotButtonCarried{HotbarSlotType::Spell, 278,
                                             "Frost Armor", 0};

    eqt::state::HotbarAssignPendingData data;
    data.type = HotbarSlotType::Emote;
    data.text = "/wave";
    EQT::Graphics::applyHotbarAssignPending(cursor, data);

    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Emote);
    EXPECT_EQ(cursor.hotbutton.text, "/wave");
}

// ===================================================================
// applyHotbarPlacementDisplacement -- the window-cache hop
// ===================================================================

class HotbarPlacementDisplacementTest : public ::testing::Test {
protected:
    HotbarPanelState hotbar;
    void occupy(int slot, HotbarSlotType type, uint32_t id,
                const std::string& name = "") {
        hotbar.slots[slot].type = type;
        hotbar.slots[slot].id = id;
        hotbar.slots[slot].name = name;
        hotbar.slots[slot].emoteText = name;
    }
};

// Placing on an EMPTY slot: the cache shows the new button; nothing
// displaced.
TEST_F(HotbarPlacementDisplacementTest, EmptySlotReceivesTheButton) {
    occupy(4, HotbarSlotType::Empty, 0);
    const HotbarSlotState oldSlot = hotbar.slots[4];
    const eqt::HotButtonCarried fresh{HotbarSlotType::Emote, 0, "/say hi", 0};

    EQT::Graphics::applyHotbarPlacementDisplacement(hotbar, 4, oldSlot, fresh);

    EXPECT_EQ(hotbar.slots[4].type, HotbarSlotType::Emote);
    EXPECT_EQ(hotbar.slots[4].emoteText, "/say hi");
    EXPECT_EQ(hotbar.slots[4].name, "/say hi");  // truncated label
}

// Placing over an OCCUPIED slot: the cache now shows the NEW button (the
// displaced one rides back on the cursor, which the caller handles).
TEST_F(HotbarPlacementDisplacementTest, OccupiedSlotShowsTheNewButton) {
    occupy(2, HotbarSlotType::Spell, 278, "Frost Armor");
    hotbar.slots[2].cooldownDurationMs = 30000;
    const HotbarSlotState oldSlot = hotbar.slots[2];
    const eqt::HotButtonCarried fresh{HotbarSlotType::Emote, 0, "/wave", 0};

    EQT::Graphics::applyHotbarPlacementDisplacement(hotbar, 2, oldSlot, fresh);

    EXPECT_EQ(hotbar.slots[2].type, HotbarSlotType::Emote);
    EXPECT_EQ(hotbar.slots[2].id, 0u);
    EXPECT_EQ(hotbar.slots[2].emoteText, "/wave");
    EXPECT_EQ(hotbar.slots[2].cooldownDurationMs, 0);
}

// Out-of-range slots are ignored (a corrupted index must not write).
TEST_F(HotbarPlacementDisplacementTest, OutOfRangeSlotIsIgnored) {
    occupy(0, HotbarSlotType::Emote, 0, "/say x");
    const HotbarSlotState before = hotbar.slots[0];
    const eqt::HotButtonCarried fresh{HotbarSlotType::Emote, 0, "/other", 0};

    EQT::Graphics::applyHotbarPlacementDisplacement(hotbar, -1, before, fresh);
    EQT::Graphics::applyHotbarPlacementDisplacement(
        hotbar, HotbarPanelState::SLOT_COUNT, before, fresh);

    EXPECT_EQ(hotbar.slots[0].emoteText, "/say x");
}

// ===================================================================
// The end-to-end press: StaticUIInput drives the whole loop
// ===================================================================

// A trivial concrete bridge (test_skill_reachability's idiom: everything but
// applyEvent is concrete on the base).
class GestureBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// The full loop on the REAL StaticUIInput: a press on an occupied slot
// defers its activate, the hold clones AND clears the source (the pickup),
// a release keeps the clone, and a placement click assigns the slot. The
// bridge carries every intent, so no value is copied by hand between the
// frames.
class HotbarGestureEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        // A button in slot 4 of the hotbar window's cache, the way the
        // HotbarSlotAssigned event would have put it.
        hotbar.slots[4].type = HotbarSlotType::Emote;
        hotbar.slots[4].id = 0;
        hotbar.slots[4].name = "/say hi";
        hotbar.slots[4].emoteText = "/say hi";
        hotbar.hoveredSlot = 4;
        input.setFrameClickState(false, eqt::inventory::ClickModifiers{},
                                  &cursor);
    }

    // The slot's own rect (hotbar_layout.h -- the hit test's geometry), so
    // the click lands where hitTestHotbar() says it does. The orientation is
    // the LAYOUT TABLE's: at 800x600 the bar is 1x10 (B1075, #395), so the
    // rect must use layout.hotbarColumns the same way the production hit
    // test does (hotbar_panel.cpp), not the oracle's stock 2-column default.
    int slotX(int slot) const {
        const auto r = EQT::Graphics::HotbarLayout::slotRect(
            layout.hotbar, slot, layout.hotbarColumns);
        return (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
    }
    int slotY(int slot) const {
        const auto r = EQT::Graphics::HotbarLayout::slotRect(
            layout.hotbar, slot, layout.hotbarColumns);
        return (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
    }

    // One frame of the production loop: the gesture tick, then (on a press
    // frame) the input pass. `x`,`y` the mouse position, `down` the button.
    // The panel pass (processPanelInput) is the ability window's input hop
    // in the same frame (renderer_frame_render.cpp) -- the B1025-H3 ability
    // press arm lives there.
    void frame(float dt, bool down, bool pressed, int x, int y) {
        input.processFrameGesture(dt, down, hotbar, gems, inventory, abilities);
        if (pressed) {
            input.processInput(x, y, /*leftClicked=*/true, layout, hotbar,
                               gems, inventory, spellbook, skills, buffs,
                               loot);
            input.processPanelInput(x, y, /*leftClicked=*/true, layout,
                                    tradeskill, abilities);
        }
    }

    GestureBridge bridge;
    EQT::Graphics::UILayout layout;
    EQT::Graphics::CursorAttachmentState cursor;
    EQT::Graphics::HotbarPanelState hotbar;
    EQT::Graphics::SpellGemPanelState gems;
    EQT::Graphics::InventoryPanelState inventory;
    EQT::Graphics::SpellbookPopupState spellbook;
    EQT::Graphics::SkillsPopupState skills;
    EQT::Graphics::BuffBarState buffs;
    EQT::Graphics::LootPanelState loot;
    EQT::Graphics::CombatAbilityPanelState abilities;
    EQT::Graphics::TradeskillPanelState tradeskill;  // B1025-H3: processPanelInput's other panel
    EQT::Graphics::StaticUIInput input;
};

TEST_F(HotbarGestureEndToEndTest, PressReleaseUnderThresholdActivates) {
    const int x = slotX(4);
    const int y = slotY(4);

    // Press frame: the slot press DEFERS its activate (the gesture is
    // active -- cursor bound) instead of pushing it.
    frame(0.016f, /*down=*/true, /*pressed=*/true, x, y);
    EXPECT_TRUE(bridge.drainIntents().empty());
    // A few frames under the threshold, still down on the slot.
    frame(0.050f, true, false, x, y);
    EXPECT_FALSE(cursor.hasHotbutton);
    // Release over the slot: the deferred activate fires; the slot keeps
    // its button (a short press does not clear it).
    frame(0.016f, /*down=*/false, /*pressed=*/false, x, y);
    EXPECT_FALSE(cursor.hasHotbutton);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* activate =
        std::get_if<eqt::events::HotbarActivateIntent>(&intents[0]);
    ASSERT_NE(activate, nullptr);
    EXPECT_EQ(activate->index, 4);
    EXPECT_EQ(activate->type, HotbarSlotType::Emote);
    EXPECT_EQ(hotbar.slots[4].type, HotbarSlotType::Emote);
}

TEST_F(HotbarGestureEndToEndTest, HoldClonesAndPickupClearsTheSource) {
    const int x = slotX(4);
    const int y = slotY(4);

    // Press on the occupied slot, then hold past the threshold.
    frame(0.016f, true, true, x, y);
    frame(0.100f, true, false, x, y);
    frame(0.100f, true, false, x, y);
    frame(0.100f, true, false, x, y);  // crosses 300 ms here
    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Emote);
    EXPECT_EQ(cursor.hotbutton.text, "/say hi");

    // The PICKUP: the clone's appearance cleared the source's button (the
    // HotbarClearIntent is what BridgeComponent turns into clearSlot).
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* clear = std::get_if<eqt::events::HotbarClearIntent>(&intents[0]);
    ASSERT_NE(clear, nullptr);
    EXPECT_EQ(clear->page, hotbar.currentPage);
    EXPECT_EQ(clear->slot, 4);

    // A RELEASE keeps the clone (H-O: "remained attached to cursor on
    // release").
    frame(0.016f, false, false, x, y);
    EXPECT_TRUE(cursor.hasHotbutton);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(HotbarGestureEndToEndTest, PlacementAssignsAndDisplaces) {
    // The cursor carries a button (the /hotbutton command's, landed by the
    // HotbarCommandAssigned event in production).
    input.processFrameGesture(0.016f, false, hotbar, gems, inventory, abilities);
    eqt::state::HotbarAssignPendingData data;
    data.type = HotbarSlotType::Emote;
    data.text = "/say hi";
    EQT::Graphics::applyHotbarAssignPending(cursor, data);
    ASSERT_TRUE(cursor.hasHotbutton);

    // The window shows a DIFFERENT button in slot 4 (what will be displaced).
    hotbar.slots[4].emoteText = "/wave";
    hotbar.slots[4].name = "/wave";
    const int x = slotX(4);
    const int y = slotY(4);
    hotbar.hoveredSlot = 4;
    frame(0.016f, /*down=*/true, /*pressed=*/true, x, y);

    // THE assignment: one HotbarAssignIntent (the new button, the slot's
    // page), and the slot's cache now shows it.
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* assign = std::get_if<eqt::events::HotbarAssignIntent>(&intents[0]);
    ASSERT_NE(assign, nullptr);
    EXPECT_EQ(assign->page, hotbar.currentPage);
    EXPECT_EQ(assign->slot, 4);
    EXPECT_EQ(assign->type, HotbarSlotType::Emote);
    EXPECT_EQ(assign->text, "/say hi");
    EXPECT_EQ(hotbar.slots[4].emoteText, "/say hi");
    // The DISPLACED button rides back on the cursor (H-O "hotbar-swap"):
    // the slot now shows "/say hi" and the cursor carries the old "/wave".
    EXPECT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.text, "/wave");
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Emote);
}

// ===================================================================
// The other H-O sources: gem, ability, item (gestures.tsv rows 2-3, 12, 9)
// ===================================================================

// H-O "spell-hold": a 0.117 s gem press released and CAST with no clone; a
// 3.074 s hold cloned by ~1.1 s. Through the REAL StaticUIInput: a short
// press-release fires the cast; a hold clones a Spell button and the cast
// never fires.
TEST_F(HotbarGestureEndToEndTest, GemPressReleaseCastsAndHoldClones) {
    gems.hoveredGem = -1;
    gems.gems[2].spellId = 501;
    gems.gems[2].spellName = "Fireball";
    // The gem's own rect (the hit test's geometry).
    const int sx = layout.spellGems.UpperLeftCorner.X;
    const int sy = layout.spellGems.UpperLeftCorner.Y;
    const int gx = sx + UILayout::SLOT_SIZE / 2;
    const int gy = sy + 2 * (UILayout::SLOT_SIZE + UILayout::MARGIN) +
                   UILayout::SLOT_SIZE / 2;

    // A SHORT press (under the threshold) over the gem: release fires the
    // cast, no clone.
    gems.hoveredGem = 2;
    frame(0.016f, /*down=*/true, /*pressed=*/true, gx, gy);
    frame(0.016f, /*down=*/false, /*pressed=*/false, gx, gy);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CastSpellIntent>(&intents[0]), nullptr);
    EXPECT_FALSE(cursor.hasHotbutton);

    // A HOLD over the gem: the clone appears and the cast is cancelled.
    frame(0.016f, true, true, gx, gy);
    frame(0.100f, true, false, gx, gy);
    frame(0.100f, true, false, gx, gy);
    frame(0.100f, true, false, gx, gy);  // crosses 300 ms
    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Spell);
    EXPECT_EQ(cursor.hotbutton.id, 501u);
    EXPECT_EQ(cursor.hotbutton.text, "Fireball");
    EXPECT_TRUE(bridge.drainIntents().empty())
        << "the hold cancels the pending cast";
}

// H-O "ability-hold": a 3.286 s hold on Actions-Melee Bash cloned by ~1.0 s
// with no activation. Through the REAL StaticUIInput: a short press-release
// over the row fires the activation; a hold clones a Combat button and the
// activation never fires.
TEST_F(HotbarGestureEndToEndTest, AbilityPressReleaseActivatesAndHoldClones) {
    abilities.isOpen = true;
    abilities.abilities = {{10, "Bash", 55}};
    const auto r = CAL::buttonRect(layout, 0);
    const int ax = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
    const int ay = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;

    // A SHORT press over the row: release fires the activation, no clone.
    frame(0.016f, /*down=*/true, /*pressed=*/true, ax, ay);
    frame(0.016f, /*down=*/false, /*pressed=*/false, ax, ay);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::SkillActivateIntent>(&intents[0]), nullptr);
    EXPECT_FALSE(cursor.hasHotbutton);

    // A HOLD over the row: the clone appears and the activation is cancelled.
    frame(0.016f, true, true, ax, ay);
    frame(0.100f, true, false, ax, ay);
    frame(0.100f, true, false, ax, ay);
    frame(0.100f, true, false, ax, ay);  // crosses 300 ms
    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Combat);
    EXPECT_EQ(cursor.hotbutton.id, 10u);
    EXPECT_EQ(cursor.hotbutton.text, "Bash");
    EXPECT_TRUE(bridge.drainIntents().empty())
        << "the hold cancels the pending activation";
}

// H-O "inventory-hold": a 2.974 s hold on inventory slot 22 cloned the item
// to the cursor AND the item REMAINED in inventory (the pickup never fired).
// Through the REAL StaticUIInput: a short press-release over the item still
// picks it up; a hold clones an Item button and the pickup never fires.
TEST_F(HotbarGestureEndToEndTest, ItemPressReleasePicksUpAndHoldClones) {
    inventory.activePopup = PopupType::Inventory;
    inventory.generalSlots[0].hasItem = true;  // Titanium slot 22
    inventory.generalSlots[0].itemId = 21779;
    inventory.generalSlots[0].itemName = "Rusty Dagger";
    inventory.generalSlots[0].quantity = 1;
    const auto r = IL::slotRect(layout, 22);
    const int ix = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
    const int iy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;

    // A SHORT press-release over the item: release fires the pickup.
    frame(0.016f, /*down=*/true, /*pressed=*/true, ix, iy);
    frame(0.016f, /*down=*/false, /*pressed=*/false, ix, iy);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* g = std::get_if<eqt::events::CursorGestureIntent>(&intents[0]);
    ASSERT_NE(g, nullptr) << "the short press is the click-to-cursor pickup";
    EXPECT_EQ(g->slot, 22);
    EXPECT_FALSE(cursor.hasHotbutton);

    // A HOLD over the item: the clone appears and the pickup never fires
    // -- the item REMAINS in inventory.
    frame(0.016f, true, true, ix, iy);
    frame(0.100f, true, false, ix, iy);
    frame(0.100f, true, false, ix, iy);
    frame(0.100f, true, false, ix, iy);  // crosses 300 ms
    ASSERT_TRUE(cursor.hasHotbutton);
    EXPECT_EQ(cursor.hotbutton.type, HotbarSlotType::Item);
    EXPECT_EQ(cursor.hotbutton.id, 21779u);
    EXPECT_EQ(cursor.hotbutton.text, "Rusty Dagger");
    EXPECT_TRUE(bridge.drainIntents().empty())
        << "the hold cancels the pending pickup; the item remains";
}

}  // namespace
