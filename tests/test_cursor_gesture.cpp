// B442 (GitLab #151) -- the click-to-cursor gesture, offline.
//
// Four layers, each against the REAL code:
//   1. classifyClick(): the button map, positives and negatives.
//   2. StaticUIInput: synthetic clicks over each item window's slot rects
//      (inventory, purse, bank, loot, merchant, the QuantityWnd) -> the
//      intents the real producer pushes.
//   3. ApplyCursorGesture() over a real InventoryManager -> the MoveItemIntent
//      sequence the existing pickup/place bodies push (what goes on the wire).
//   4. The slash commands: ParseCursorCommand() (slot numbers through
//      parseUserSlot()), the CommandProcessor registration and dispatch, and
//      the end-to-end claim that /pickup and a click are ONE path.
//
// The rules asserted here are the OFFICIAL client's, measured by #151 Leg O:
// review/UIGAP-ORACLE/151/gestures.tsv. A test that pins a ledger row names
// it ("row N"). Where the ledger is silent (the loot window, the purse's
// SHIFT, the right-click display being held vs kept) the test says so.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/quantity_layout.h"
#include "client/graphics/ui/quantity_window.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/cursor_gesture.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "common/packets/merchant.h"

#include <functional>
#include <string>
#include <variant>
#include <vector>

using namespace EQT::Graphics;
using eqt::events::CursorGestureIntent;
using eqt::events::CursorGestureOp;
using eqt::events::MoveItemIntent;
using eqt::inventory::ClickButton;
using eqt::inventory::ClickModifiers;
using eqt::inventory::CursorSnapshot;
using eqt::inventory::GestureAction;
using eqt::inventory::InventoryManager;
using eqt::inventory::ItemInstance;
using eqt::inventory::SlotSnapshot;
using eqt::inventory::classifyClick;

namespace {

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

UILayout layout800() {
    UILayout l;
    l.computeLayout(800, 600);
    return l;
}

ItemInstance item(uint32_t id, const char* name) {
    ItemInstance it;
    it.itemId = id;
    it.name = name;
    it.slots = 0xFFFFFFFFu;
    return it;
}

ItemInstance stack(uint32_t id, const char* name, int32_t qty, int32_t size) {
    ItemInstance it = item(id, name);
    it.stackable = true;
    it.quantity = qty;
    it.stackSize = size;
    return it;
}

std::vector<MoveItemIntent> moves(CapturingBridge& b) {
    std::vector<MoveItemIntent> out;
    for (const auto& i : b.drainIntents()) {
        if (const auto* m = std::get_if<MoveItemIntent>(&i)) out.push_back(*m);
    }
    return out;
}

bool sameMove(const MoveItemIntent& m, int16_t from, int16_t to, uint32_t qty) {
    return m.fromSlot == from && m.toSlot == to && m.quantity == qty;
}

CursorGestureIntent gesture(CursorGestureOp op, int16_t slot, int32_t qty = 0) {
    CursorGestureIntent g;
    g.op = op;
    g.slot = slot;
    g.quantity = qty;
    return g;
}

// A point inside a hit-test function's answer, found by scanning the screen
// -- the tests never re-derive a window's row arithmetic.
bool findPoint(const std::function<bool(int, int)>& hit, int& x, int& y) {
    for (y = 0; y < 600; ++y)
        for (x = 0; x < 800; ++x)
            if (hit(x, y)) return true;
    return false;
}

// One frame of the inventory popup's input, as renderStaticUI drives it.
struct InventoryRig {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inv;
    SpellbookPopupState book;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;
    CursorAttachmentState cursor;

    InventoryRig() {
        input.init(&bridge);
        inv.activePopup = PopupType::Inventory;
    }
    void clickAt(int x, int y, bool left, bool right, ClickModifiers mods = {}) {
        input.setFrameClickState(right, mods, &cursor);
        input.processInput(x, y, left, l, hotbar, gems, inv, book, skills, buffs, loot,
                           mods.shift);
    }
    void clickSlot(int slot, bool left, bool right, ClickModifiers mods = {}) {
        const auto r = InventoryLayout::slotRect(l, slot);
        clickAt(r.UpperLeftCorner.X + 3, r.UpperLeftCorner.Y + 3, left, right, mods);
    }
};

}  // namespace

// ===========================================================================
// 1. The button map
// ===========================================================================

// Rows 1 and 15: a single item (or a stack of 1) is picked up at the click.
// Row 3: a plain click on a stack of 2+ is the QuantityWnd, NOT the stack.
TEST(CursorGestureMap, LeftPicksUpASingleItemAndOpensTheDialogOnAStack) {
    EXPECT_EQ(classifyClick(ClickButton::Left, {}, SlotSnapshot{true, 1}, CursorSnapshot{false}),
              GestureAction::CursorClick);
    EXPECT_EQ(classifyClick(ClickButton::Left, {}, SlotSnapshot{true, 0}, CursorSnapshot{false}),
              GestureAction::CursorClick);
    EXPECT_EQ(classifyClick(ClickButton::Left, {}, SlotSnapshot{true, 3}, CursorSnapshot{false}),
              GestureAction::OpenQuantity);
}

// Row 10: SHIFT = the whole stack, no dialog. Row 11: CTRL = exactly one.
// Row 15: CTRL on a stack of 1 is a plain pick-up.
TEST(CursorGestureMap, ShiftTakesTheWholeStackAndCtrlTakesOne) {
    ClickModifiers shift; shift.shift = true;
    ClickModifiers ctrl; ctrl.ctrl = true;
    EXPECT_EQ(classifyClick(ClickButton::Left, shift, SlotSnapshot{true, 20}, CursorSnapshot{false}),
              GestureAction::CursorClick);
    EXPECT_EQ(classifyClick(ClickButton::Left, ctrl, SlotSnapshot{true, 5}, CursorSnapshot{false}),
              GestureAction::PickUpOne);
    EXPECT_EQ(classifyClick(ClickButton::Left, ctrl, SlotSnapshot{true, 1}, CursorSnapshot{false}),
              GestureAction::CursorClick);
}

// Row 26: ALT+LEFT is the item display and never a pick-up.
TEST(CursorGestureMap, AltLeftIsTheItemDisplay) {
    ClickModifiers alt; alt.alt = true;
    EXPECT_EQ(classifyClick(ClickButton::Left, alt, SlotSnapshot{true, 1}, CursorSnapshot{false}),
              GestureAction::ItemDisplay);
}

TEST(CursorGestureMap, LeftWithAnItemHeldPlacesOnEmptyAndOccupiedSlotsAlike) {
    EXPECT_EQ(classifyClick(ClickButton::Left, {}, SlotSnapshot{false, 0}, CursorSnapshot{true}),
              GestureAction::CursorClick);
    EXPECT_EQ(classifyClick(ClickButton::Left, {}, SlotSnapshot{true, 1}, CursorSnapshot{true}),
              GestureAction::CursorClick);
    // Row 13: CTRL with an item held just places; so does SHIFT.
    ClickModifiers shift; shift.shift = true;
    ClickModifiers ctrl; ctrl.ctrl = true;
    EXPECT_EQ(classifyClick(ClickButton::Left, shift, SlotSnapshot{true, 20}, CursorSnapshot{true}),
              GestureAction::CursorClick);
    EXPECT_EQ(classifyClick(ClickButton::Left, ctrl, SlotSnapshot{false, 0}, CursorSnapshot{true}),
              GestureAction::CursorClick);
}

// Rows 24/25: a right click on an item never picks it up. (The display it
// asks for is shown only while the button is HELD -- B442b, pinned in
// test_cursor_gesture_completion.cpp's ItemDisplayLifetime.)
TEST(CursorGestureMap, RightIsTheDisplayAndNeverAMove) {
    EXPECT_EQ(classifyClick(ClickButton::Right, {}, SlotSnapshot{true, 1}, CursorSnapshot{false}),
              GestureAction::ItemDisplay);
    EXPECT_EQ(classifyClick(ClickButton::Right, {}, SlotSnapshot{true, 1}, CursorSnapshot{true}),
              GestureAction::ItemDisplay);
    EXPECT_EQ(classifyClick(ClickButton::Right, {}, SlotSnapshot{false, 0}, CursorSnapshot{true}),
              GestureAction::None) << "a right click never puts the held item down";
}

TEST(CursorGestureMap, AnEmptySlotWithAnEmptyCursorIsNothing) {
    EXPECT_EQ(classifyClick(ClickButton::Left, {}, SlotSnapshot{false, 0}, CursorSnapshot{false}),
              GestureAction::None);
    EXPECT_EQ(classifyClick(ClickButton::Right, {}, SlotSnapshot{false, 0}, CursorSnapshot{false}),
              GestureAction::None);
}

TEST(CursorGestureMap, TheOpenContainerHookTakesTheRightClickWhenItClaimsIt) {
    CapturingBridge bridge;
    eqt::inventory::CursorGesture g;
    g.setBridge(&bridge);
    std::vector<int16_t> asked;
    g.setOpenContainerHook([&](int16_t slot) { asked.push_back(slot); return slot == 23; });

    EXPECT_EQ(g.click(23, ClickButton::Right, {}, SlotSnapshot{true, 1}, CursorSnapshot{}),
              GestureAction::OpenContainer);
    EXPECT_TRUE(bridge.drainIntents().empty()) << "a claimed right click shows no display";

    EXPECT_EQ(g.click(24, ClickButton::Right, {}, SlotSnapshot{true, 1}, CursorSnapshot{}),
              GestureAction::ItemDisplay);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_NE(std::get_if<eqt::events::ItemDisplayRequestIntent>(&intents[0]), nullptr);
    EXPECT_EQ(asked, (std::vector<int16_t>{23, 24}));

    // ALT+LEFT on a bag is the display, never the bag window.
    ClickModifiers alt; alt.alt = true;
    EXPECT_EQ(g.click(23, ClickButton::Left, alt, SlotSnapshot{true, 1}, CursorSnapshot{}),
              GestureAction::ItemDisplay);
    EXPECT_EQ(asked.size(), 2u);
    EXPECT_EQ(bridge.drainIntents().size(), 1u);
}

TEST(CursorGestureMap, CtrlOnAStackPushesAPickUpOfOne) {
    CapturingBridge bridge;
    eqt::inventory::CursorGesture g;
    g.setBridge(&bridge);
    ClickModifiers ctrl; ctrl.ctrl = true;
    EXPECT_EQ(g.click(27, ClickButton::Left, ctrl, SlotSnapshot{true, 5}, CursorSnapshot{}),
              GestureAction::PickUpOne);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto& pick = std::get<CursorGestureIntent>(intents[0]);
    EXPECT_EQ(pick.op, CursorGestureOp::PickUp);
    EXPECT_EQ(pick.slot, 27);
    EXPECT_EQ(pick.quantity, 1);
}

// ===========================================================================
// 2. The real producer, window by window
// ===========================================================================

TEST(CursorGestureInventory, LeftClickOnAnOccupiedSlotPushesTheCursorClick) {
    InventoryRig rig;
    rig.inv.generalSlots[0].hasItem = true;   // Titanium slot 22
    rig.inv.generalSlots[0].quantity = 1;
    rig.clickSlot(22, /*left=*/true, /*right=*/false);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* g = std::get_if<CursorGestureIntent>(&intents[0]);
    ASSERT_NE(g, nullptr) << "left click no longer pushes the item display (T421's deviation)";
    EXPECT_EQ(g->op, CursorGestureOp::Click);
    EXPECT_EQ(g->slot, 22);
    EXPECT_FALSE(g->echo) << "a click is silent; only a slash command echoes";
}

TEST(CursorGestureInventory, RightClickOnAnOccupiedSlotIsTheItemDisplay) {
    InventoryRig rig;
    rig.inv.equipSlots[13].hasItem = true;
    rig.clickSlot(13, /*left=*/false, /*right=*/true);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* d = std::get_if<eqt::events::ItemDisplayRequestIntent>(&intents[0]);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->slot, 13);
}

TEST(CursorGestureInventory, WithAnItemHeldAClickOnAnEmptySlotPlacesIt) {
    InventoryRig rig;
    rig.cursor.hasItem = true;
    rig.clickSlot(25, /*left=*/true, /*right=*/false);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* g = std::get_if<CursorGestureIntent>(&intents[0]);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->slot, 25);
}

TEST(CursorGestureInventory, NegativesPushNothing) {
    InventoryRig rig;
    rig.clickSlot(25, true, false);           // empty slot, empty cursor
    rig.clickSlot(25, false, true);           // right on an empty slot
    rig.inv.generalSlots[0].hasItem = true;
    rig.clickSlot(22, false, false);          // hover only
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    // ...and a closed popup takes no click at all.
    rig.inv.activePopup = PopupType::None;
    rig.clickSlot(22, true, false);
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
}

// Rows 3-5: a plain click on a stack opens the dialog prefilled with the
// whole stack; Accept picks the chosen count up.
TEST(CursorGestureInventory, ClickOnAStackOpensTheQuantityWindowAndAcceptPicksUpThatMany) {
    InventoryRig rig;
    rig.inv.generalSlots[1].hasItem = true;   // slot 23
    rig.inv.generalSlots[1].itemName = "Arrow";
    rig.inv.generalSlots[1].quantity = 20;
    rig.clickSlot(23, true, false);
    EXPECT_TRUE(rig.bridge.drainIntents().empty()) << "the dialog chooses first";
    const auto& q = rig.input.quantityWindow();
    ASSERT_TRUE(q.isOpen);
    EXPECT_EQ(q.slotId, 23);
    EXPECT_EQ(q.maxValue, 20);
    EXPECT_EQ(q.value, 20) << "it opens on the whole stack (row 3)";

    // Row 18: while it is up, other slot clicks are ignored.
    rig.inv.generalSlots[0].hasItem = true;
    rig.clickSlot(22, true, false);
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    EXPECT_EQ(rig.input.quantityWindow().slotId, 23);

    // Click the slider's left end: 1. Then its middle.
    const auto s = QuantityLayout::sliderRect(rig.l);
    EXPECT_TRUE(rig.input.processQuantityInput(s.UpperLeftCorner.X, s.UpperLeftCorner.Y + 5,
                                               true, rig.l));
    EXPECT_EQ(rig.input.quantityWindow().value, 1);
    const int mid = (s.UpperLeftCorner.X + s.LowerRightCorner.X - 1) / 2;
    rig.input.processQuantityInput(mid, s.UpperLeftCorner.Y + 5, true, rig.l);
    const int chosen = rig.input.quantityWindow().value;
    EXPECT_GT(chosen, 1);
    EXPECT_LT(chosen, 20);

    const auto a = QuantityLayout::acceptRect(rig.l);
    EXPECT_TRUE(rig.input.processQuantityInput(a.UpperLeftCorner.X + 2, a.UpperLeftCorner.Y + 2,
                                               true, rig.l));
    EXPECT_FALSE(rig.input.quantityWindow().isOpen);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* g = std::get_if<CursorGestureIntent>(&intents[0]);
    ASSERT_NE(g, nullptr);
    EXPECT_EQ(g->op, CursorGestureOp::PickUp);
    EXPECT_EQ(g->slot, 23);
    EXPECT_EQ(g->quantity, chosen);
}

// Row 17: the X closes it, nothing picked up. Row 16: ESC does NOT close it.
TEST(CursorGestureInventory, TheQuantityCloseBoxClosesItAndEscDoesNot) {
    InventoryRig rig;
    rig.input.quantityWindow().openForStack(23, "Arrow", 20);
    const auto c = QuantityLayout::closeBoxRect(rig.l);
    EXPECT_TRUE(rig.input.processQuantityInput(c.UpperLeftCorner.X + 2, c.UpperLeftCorner.Y + 2,
                                               true, rig.l));
    EXPECT_FALSE(rig.input.quantityWindow().isOpen);

    rig.input.quantityWindow().openForStack(23, "Arrow", 20);
    TradeskillPanelState ts;
    rig.input.processKey(irr::KEY_ESCAPE, true, false, false, rig.inv, rig.book,
                         rig.skills, rig.loot, ts);
    EXPECT_TRUE(rig.input.quantityWindow().isOpen) << "row 16: ESC leaves the dialog up";
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    rig.input.quantityWindow().close();

    // Off the dialog: not consumed.
    rig.input.quantityWindow().openForStack(23, "Arrow", 20);
    EXPECT_FALSE(rig.input.processQuantityInput(5, 590, true, rig.l));
}

// Row 28: a purse row opens the QuantityWnd prefilled with all of it.
// Row 29: Accept moves that coin (the wire type) onto the cursor. Row 30: a
// purse click with coin held puts it back. SHIFT = all with no dialog is
// measured on the BANK's coin (row 43) and inferred for the purse.
TEST(CursorGestureInventory, APurseRowIsTheCoinDialogAndHeldCoinGoesBack) {
    InventoryRig rig;
    rig.inv.coins[0] = 10;  // platinum
    const auto r = InventoryLayout::coinRect(rig.l, 0);
    rig.clickAt(r.UpperLeftCorner.X + 30, r.UpperLeftCorner.Y + 4, true, false);
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    ASSERT_TRUE(rig.input.quantityWindow().isOpen);
    EXPECT_EQ(rig.input.quantityWindow().purpose, QuantityWindowState::Purpose::Coin);
    EXPECT_EQ(rig.input.quantityWindow().value, 10);

    const auto a = QuantityLayout::acceptRect(rig.l);
    rig.input.processQuantityInput(a.UpperLeftCorner.X + 2, a.UpperLeftCorner.Y + 2, true, rig.l);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    auto g = std::get<CursorGestureIntent>(intents[0]);
    EXPECT_EQ(g.op, CursorGestureOp::CoinClick);
    EXPECT_EQ(g.coinType, EQT::COINTYPE_PP);
    EXPECT_EQ(g.quantity, 10);

    // Coin held: the next purse click is the put-back (no dialog). B442b: the
    // render thread keeps no shadow -- "held" is what the game thread's
    // CursorCoinChanged put on the cursor attachment, simulated here.
    rig.cursor.coinType = EQT::COINTYPE_PP;
    rig.cursor.coinAmount = 10;
    rig.clickAt(r.UpperLeftCorner.X + 30, r.UpperLeftCorner.Y + 4, true, false);
    EXPECT_FALSE(rig.input.quantityWindow().isOpen);
    intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<CursorGestureIntent>(intents[0]).op, CursorGestureOp::CoinClick);

    // SHIFT on copper (the last row, the wire's 0): all of it, no dialog.
    rig.cursor.coinAmount = 0;   // the put-back's CursorCoinChanged
    rig.inv.coins[3] = 47;
    ClickModifiers shift; shift.shift = true;
    const auto cp = InventoryLayout::coinRect(rig.l, 3);
    rig.clickAt(cp.UpperLeftCorner.X + 30, cp.UpperLeftCorner.Y + 4, true, false, shift);
    EXPECT_FALSE(rig.input.quantityWindow().isOpen);
    intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    g = std::get<CursorGestureIntent>(intents[0]);
    EXPECT_EQ(g.coinType, EQT::COINTYPE_CP);
    EXPECT_EQ(g.quantity, 0);
}

TEST(CursorGestureBank, ABankSlotIsTheSameGestureOnSlot2000PlusN) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);
    MerchantPanelState merchant;
    BankPanelState bank;
    bank.isOpen = true;
    bank.slots[3].hasItem = true;
    bank.slots[3].quantity = 1;
    CursorAttachmentState cursor;

    auto click = [&](int slot, bool left, bool right) {
        const auto r = BankLayout::slotRect(l, slot);
        input.setFrameClickState(right, {}, &cursor);
        return input.processCommerceInput(r.UpperLeftCorner.X + 4, r.UpperLeftCorner.Y + 4,
                                          left, l, merchant, bank, cursor);
    };

    EXPECT_TRUE(click(3, true, false));
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<CursorGestureIntent>(intents[0]).slot, eqt::inventory::BANK_BEGIN + 3);

    EXPECT_TRUE(click(3, false, true));
    intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<eqt::events::ItemDisplayRequestIntent>(intents[0]).slot,
              eqt::inventory::BANK_BEGIN + 3);

    // An empty bank slot: nothing with an empty cursor, a put-down with one held.
    EXPECT_TRUE(click(5, true, false));
    EXPECT_TRUE(bridge.drainIntents().empty());
    cursor.hasItem = true;
    EXPECT_TRUE(click(5, true, false));
    intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_EQ(std::get<CursorGestureIntent>(intents[0]).slot, eqt::inventory::BANK_BEGIN + 5);
}

TEST(CursorGestureLoot, LeftLootsToTheCursorAndRightTakesNothing) {
    InventoryRig rig;
    rig.inv.activePopup = PopupType::None;
    rig.loot.isOpen = true;
    rig.loot.corpseId = 77;
    LootItemDisplay d;
    d.slot = 5;
    d.itemName = "Bone Chips";
    rig.loot.items.push_back(d);
    int x = 0, y = 0;
    ASSERT_TRUE(findPoint([&](int px, int py) {
        return hitTestLootRow(rig.l, rig.loot, px, py) == 0 &&
               hitTestLootButton(rig.l, rig.loot, px, py) == LootButton::None;
    }, x, y));

    rig.clickAt(x, y, /*left=*/true, /*right=*/false);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* li = std::get_if<eqt::events::LootItemIntent>(&intents[0]);
    ASSERT_NE(li, nullptr) << "LootItemIntent is the corpse's cursor pick-up (auto_loot 0)";
    EXPECT_EQ(li->corpseId, 77);
    EXPECT_EQ(li->slot, 5);

    rig.clickAt(x, y, /*left=*/false, /*right=*/true);
    EXPECT_TRUE(rig.bridge.drainIntents().empty())
        << "RIGHT on a corpse row is the oracle's inspect, which is owed -- never a loot";
}

// review/UIGAP-ORACLE/152: on Titanium selling NEVER uses the cursor, so a
// held item clicked onto the merchant list is not a sale -- the row click
// still only selects (the inventory-selection sell rule is #152's).
TEST(CursorGestureMerchant, TheMerchantListIsNotAPutDownTarget) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);
    MerchantPanelState merchant;
    merchant.isOpen = true;
    merchant.npcId = 42;
    MerchantItemRow row;
    row.vendorSlot = 9;
    row.itemName = "Bread";
    merchant.items.push_back(row);
    BankPanelState bank;
    CursorAttachmentState cursor;
    cursor.hasItem = true;
    cursor.quantity = 3;

    int x = 0, y = 0;
    ASSERT_TRUE(findPoint([&](int px, int py) {
        return hitTestMerchantRow(l, merchant, px, py) == 0;
    }, x, y));
    input.setFrameClickState(false, {}, &cursor);
    EXPECT_TRUE(input.processCommerceInput(x, y, true, l, merchant, bank, cursor));
    EXPECT_EQ(merchant.selectedRow, 0);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// ===========================================================================
// 3. The game-thread half: what goes on the wire
// ===========================================================================

TEST(CursorGestureApply, PickUpThenPlaceIsTwoWholeStackMoves) {
    InventoryManager inv;
    CapturingBridge bridge;
    inv.setBridge(&bridge);
    inv.setItem(22, item(1001, "Rusty Dagger"));

    auto out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 22));
    ASSERT_TRUE(out.success) << out.message;
    auto m = moves(bridge);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_TRUE(sameMove(m[0], 22, eqt::inventory::CURSOR_SLOT, 0));
    EXPECT_EQ(out.touched, (std::vector<int16_t>{22, eqt::inventory::CURSOR_SLOT}));
    ASSERT_TRUE(inv.hasCursorItem());

    out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 25));
    ASSERT_TRUE(out.success) << out.message;
    m = moves(bridge);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_TRUE(sameMove(m[0], eqt::inventory::CURSOR_SLOT, 25, 0));
    EXPECT_FALSE(inv.hasCursorItem());
    ASSERT_NE(inv.getItem(25), nullptr);
}

TEST(CursorGestureApply, PlacingOnAnOccupiedSlotSwapsTheDisplacedItemOntoTheCursor) {
    InventoryManager inv;
    CapturingBridge bridge;
    inv.setBridge(&bridge);
    inv.setItem(22, item(1001, "Rusty Dagger"));
    inv.setItem(23, item(1002, "Cracked Staff"));
    eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 22));
    moves(bridge);

    auto out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 23));
    ASSERT_TRUE(out.success);
    auto m = moves(bridge);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_TRUE(sameMove(m[0], eqt::inventory::CURSOR_SLOT, 23, 0));
    ASSERT_NE(inv.getCursorItem(), nullptr);
    EXPECT_EQ(inv.getCursorItem()->name, "Cracked Staff");
    EXPECT_EQ(inv.getItem(23)->name, "Rusty Dagger");
}

TEST(CursorGestureApply, AMatchingStackMergesWithItsCount) {
    InventoryManager inv;
    CapturingBridge bridge;
    inv.setBridge(&bridge);
    inv.setItem(22, stack(1003, "Bat Wing", 5, 20));
    inv.setItem(23, stack(1003, "Bat Wing", 8, 20));
    eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 22));
    moves(bridge);

    auto out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 23));
    ASSERT_TRUE(out.success);
    auto m = moves(bridge);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_TRUE(sameMove(m[0], eqt::inventory::CURSOR_SLOT, 23, 5));
    EXPECT_EQ(inv.getItem(23)->quantity, 13);
    EXPECT_FALSE(inv.hasCursorItem());
}

TEST(CursorGestureApply, APartialPickUpCarriesItsQuantity) {
    InventoryManager inv;
    CapturingBridge bridge;
    inv.setBridge(&bridge);
    inv.setItem(22, stack(1003, "Arrow", 20, 100));
    auto out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::PickUp, 22, 7));
    ASSERT_TRUE(out.success) << out.message;
    auto m = moves(bridge);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_TRUE(sameMove(m[0], 22, eqt::inventory::CURSOR_SLOT, 7));
    EXPECT_EQ(inv.getItem(22)->quantity, 13);
    EXPECT_EQ(inv.getCursorItem()->quantity, 7);

    // A quantity of the whole stack (or more) is a whole-stack pick-up.
    InventoryManager inv2;
    inv2.setBridge(&bridge);
    inv2.setItem(22, stack(1003, "Arrow", 20, 100));
    eqt::inventory::ApplyCursorGesture(inv2, gesture(CursorGestureOp::PickUp, 22, 20));
    m = moves(bridge);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_TRUE(sameMove(m[0], 22, eqt::inventory::CURSOR_SLOT, 0));
}

TEST(CursorGestureApply, RefusalsSendNothing) {
    InventoryManager inv;
    CapturingBridge bridge;
    inv.setBridge(&bridge);
    inv.setItem(22, item(1001, "Rusty Dagger"));

    // Nothing to pick up; PutDown with an empty cursor; the cursor itself.
    EXPECT_FALSE(eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 25)).success);
    EXPECT_FALSE(eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::PutDown, 25)).success);
    EXPECT_FALSE(eqt::inventory::ApplyCursorGesture(
        inv, gesture(CursorGestureOp::Click, eqt::inventory::CURSOR_SLOT)).success);
    EXPECT_TRUE(moves(bridge).empty());

    // PickUp while holding is refused, not turned into a swap.
    eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 22));
    moves(bridge);
    inv.setItem(23, item(1002, "Cracked Staff"));
    auto out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::PickUp, 23));
    EXPECT_FALSE(out.success);
    EXPECT_NE(out.message.find("/putdown"), std::string::npos) << out.message;
    EXPECT_TRUE(moves(bridge).empty());

    // An item that cannot be worn in a slot stays on the cursor.
    InventoryManager inv2;
    inv2.setBridge(&bridge);
    ItemInstance ring = item(1004, "Ring");
    ring.slots = 0;  // fits no worn slot
    inv2.setItem(22, ring);
    eqt::inventory::ApplyCursorGesture(inv2, gesture(CursorGestureOp::Click, 22));
    moves(bridge);
    out = eqt::inventory::ApplyCursorGesture(inv2, gesture(CursorGestureOp::Click, 13));
    EXPECT_FALSE(out.success);
    EXPECT_TRUE(moves(bridge).empty());
    EXPECT_TRUE(inv2.hasCursorItem());
}

TEST(CursorGestureApply, TheCoinPlanTakesCoinAndReturnsHeldCoin) {
    InventoryManager inv;
    auto plan = eqt::inventory::PlanCoinGesture(inv, EQT::COINTYPE_GP, 0, 12);
    ASSERT_TRUE(plan.send);
    EXPECT_TRUE(plan.toCursor);
    EXPECT_EQ(plan.coinType, EQT::COINTYPE_GP);
    EXPECT_EQ(plan.amount, 12);

    plan = eqt::inventory::PlanCoinGesture(inv, EQT::COINTYPE_GP, 5, 12);
    EXPECT_EQ(plan.amount, 5);

    EXPECT_FALSE(eqt::inventory::PlanCoinGesture(inv, EQT::COINTYPE_GP, 13, 12).send)
        << "refused, never clamped (the /bank precedent)";
    EXPECT_FALSE(eqt::inventory::PlanCoinGesture(inv, EQT::COINTYPE_SP, 0, 0).send);
    EXPECT_FALSE(eqt::inventory::PlanCoinGesture(inv, 9, 1, 12).send);

    inv.pickupMoney(InventoryManager::CursorMoneyType::Platinum, 3);
    plan = eqt::inventory::PlanCoinGesture(inv, EQT::COINTYPE_CP, 0, 50);
    ASSERT_TRUE(plan.send);
    EXPECT_FALSE(plan.toCursor) << "held coin goes back whatever row is clicked";
    EXPECT_EQ(plan.coinType, EQT::COINTYPE_PP);
    EXPECT_EQ(plan.amount, 3);

    InventoryManager holding;
    holding.setItem(22, item(1001, "Rusty Dagger"));
    holding.pickupItem(22);
    EXPECT_FALSE(eqt::inventory::PlanCoinGesture(holding, EQT::COINTYPE_PP, 0, 5).send);
}

// The ledger's wire rows that involve the bank and the bag sub-slots, run
// through the same game-thread body: row 38 (bank 2000 -> cursor, whole),
// row 39 (cursor -> bank 2004), row 11 (CTRL: one item, qty 1), rows 22/23
// (cursor -> bag sub-slot 251 and back; 251 = 251 + (22-22)*10 + 0).
TEST(CursorGestureApply, TheLedgersBankBagAndCtrlRowsPutTheSameMovesOnTheWire) {
    CapturingBridge bridge;
    {
        InventoryManager inv;
        inv.setBridge(&bridge);
        inv.setItem(2000, stack(2001, "Bottle", 5, 20));
        eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 2000));
        auto m = moves(bridge);
        ASSERT_EQ(m.size(), 1u);
        EXPECT_TRUE(sameMove(m[0], 2000, eqt::inventory::CURSOR_SLOT, 0)) << "row 38";
        eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 2004));
        m = moves(bridge);
        ASSERT_EQ(m.size(), 1u);
        EXPECT_TRUE(sameMove(m[0], eqt::inventory::CURSOR_SLOT, 2004, 0)) << "row 39";
    }
    {
        InventoryManager inv;
        inv.setBridge(&bridge);
        inv.setItem(27, stack(2001, "Bottle", 5, 20));
        eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::PickUp, 27, 1));
        auto m = moves(bridge);
        ASSERT_EQ(m.size(), 1u);
        EXPECT_TRUE(sameMove(m[0], 27, eqt::inventory::CURSOR_SLOT, 1)) << "row 11";
        // Row 12: the one bottle clicked back onto its stack merges with qty 1.
        eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 27));
        m = moves(bridge);
        ASSERT_EQ(m.size(), 1u);
        EXPECT_TRUE(sameMove(m[0], eqt::inventory::CURSOR_SLOT, 27, 1)) << "row 12";
        EXPECT_EQ(inv.getItem(27)->quantity, 5);
    }
    {
        InventoryManager inv;
        inv.setBridge(&bridge);
        ItemInstance bag = item(3001, "Large Bag");
        bag.bagSlots = 6;
        bag.bagSize = 4;
        inv.setItem(22, bag);
        inv.setItem(26, item(3002, "Skinning Knife"));
        eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 26));
        moves(bridge);
        auto out = eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 251));
        ASSERT_TRUE(out.success) << out.message;
        auto m = moves(bridge);
        ASSERT_EQ(m.size(), 1u);
        EXPECT_TRUE(sameMove(m[0], eqt::inventory::CURSOR_SLOT, 251, 0)) << "row 22";
        eqt::inventory::ApplyCursorGesture(inv, gesture(CursorGestureOp::Click, 251));
        m = moves(bridge);
        ASSERT_EQ(m.size(), 1u);
        EXPECT_TRUE(sameMove(m[0], 251, eqt::inventory::CURSOR_SLOT, 0)) << "row 23";
    }
}

// ===========================================================================
// 4. The slash commands
// ===========================================================================

TEST(CursorGestureCommands, SlotNumbersAreInventorySlotIdThroughParseUserSlot) {
    // inventory.slot_id 23 is general 1, Titanium 22 (T494's one convention).
    auto p = eqt::inventory::ParseCursorCommand("pickup", "23");
    ASSERT_TRUE(p.ok) << p.error;
    EXPECT_EQ(p.intent.op, CursorGestureOp::PickUp);
    EXPECT_EQ(p.intent.slot, 22);
    EXPECT_EQ(p.intent.quantity, 0);
    EXPECT_TRUE(p.intent.echo);

    p = eqt::inventory::ParseCursorCommand("pickup", "24 5");
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.intent.slot, 23);
    EXPECT_EQ(p.intent.quantity, 5);

    p = eqt::inventory::ParseCursorCommand("putdown", "13");
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.intent.op, CursorGestureOp::PutDown);
    EXPECT_EQ(p.intent.slot, 13);

    p = eqt::inventory::ParseCursorCommand("putdown", "2000");
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.intent.slot, eqt::inventory::BANK_BEGIN);
}

TEST(CursorGestureCommands, BadInputIsRefusedWithUsage) {
    for (const auto& [verb, args] : std::vector<std::pair<std::string, std::string>>{
             {"pickup", ""}, {"pickup", "abc"}, {"pickup", "23 0"}, {"pickup", "23 x"},
             {"pickup", "23 1 2"}, {"putdown", ""}, {"putdown", "23 5"},
             {"coin", ""}, {"coin", "pp"}, {"coin", "xx 5"}, {"coin", "pp 0"}, {"coin", "pp -3"}}) {
        auto p = eqt::inventory::ParseCursorCommand(verb, args);
        EXPECT_FALSE(p.ok) << "/" << verb << " " << args;
        EXPECT_FALSE(p.error.empty());
    }
    // The power source (21) is a real server slot a Titanium client never gets.
    auto p = eqt::inventory::ParseCursorCommand("pickup", "21");
    EXPECT_FALSE(p.ok);
    EXPECT_NE(p.error.find("not a slot this client can reach"), std::string::npos) << p.error;
    // The cursor (33) is not a slot to pick up from.
    EXPECT_FALSE(eqt::inventory::ParseCursorCommand("pickup", "33").ok);
    EXPECT_FALSE(eqt::inventory::ParseCursorCommand("putdown", "33").ok);
}

TEST(CursorGestureCommands, CoinParsesEachDenominationToTheWireType) {
    const std::pair<const char*, int> rows[] = {
        {"pp", EQT::COINTYPE_PP}, {"GP", EQT::COINTYPE_GP},
        {"sp", EQT::COINTYPE_SP}, {"cp", EQT::COINTYPE_CP}};
    for (const auto& [tok, type] : rows) {
        auto p = eqt::inventory::ParseCursorCommand("coin", std::string(tok) + " 4");
        ASSERT_TRUE(p.ok) << tok;
        EXPECT_EQ(p.intent.op, CursorGestureOp::CoinClick);
        EXPECT_EQ(p.intent.coinType, type);
        EXPECT_EQ(p.intent.quantity, 4);
    }
}

TEST(CursorGestureCommands, CursorDescribesItemAndCoin) {
    InventoryManager inv;
    EXPECT_EQ(eqt::inventory::DescribeCursor(inv), "Your cursor is empty.");
    inv.setItem(22, stack(1003, "Arrow", 20, 100));
    inv.pickupItem(22);
    EXPECT_EQ(eqt::inventory::DescribeCursor(inv), "Cursor: Arrow x20.");
    inv.pickupMoney(InventoryManager::CursorMoneyType::Gold, 4);
    EXPECT_EQ(eqt::inventory::DescribeCursor(inv), "Cursor: Arrow x20; 4 gold.");
}

// The ONE-PATH claim: a click and /pickup push the same intent type and, run
// through the same game-thread body, put the same move on the wire.
TEST(CursorGestureCommands, AClickAndPickupAreOnePathToTheSameMove) {
    InventoryRig rig;
    rig.inv.generalSlots[0].hasItem = true;
    rig.clickSlot(22, true, false);
    auto clicked = rig.bridge.drainIntents();
    ASSERT_EQ(clicked.size(), 1u);
    const auto clickIntent = std::get<CursorGestureIntent>(clicked[0]);
    const auto cmd = eqt::inventory::ParseCursorCommand("pickup", "23");
    ASSERT_TRUE(cmd.ok);
    EXPECT_EQ(clickIntent.slot, cmd.intent.slot);

    auto wire = [](const CursorGestureIntent& g) {
        InventoryManager inv;
        CapturingBridge b;
        inv.setBridge(&b);
        inv.setItem(22, item(1001, "Rusty Dagger"));
        eqt::inventory::ApplyCursorGesture(inv, g);
        return moves(b);
    };
    const auto a = wire(clickIntent);
    const auto b = wire(cmd.intent);
    ASSERT_EQ(a.size(), 1u);
    ASSERT_EQ(b.size(), 1u);
    EXPECT_TRUE(sameMove(a[0], b[0].fromSlot, b[0].toSlot, b[0].quantity));
}
