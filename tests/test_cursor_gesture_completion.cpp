// B442b (GitLab #151 remainder, #152) -- the click-to-cursor gesture set,
// finished, and the merchant's select-to-sell model. Offline.
//
// THE ORACLE LEDGERS ARE THE SPEC. Every case that pins a measured rule names
// its row: "row N" is review/UIGAP-ORACLE/151/gestures.tsv, "merchant row N"
// is review/UIGAP-ORACLE/152/merchant.tsv. A case with no row is fable's own
// assumption and says so.
//
// Two layers, each against the REAL code:
//   A. the render thread -- StaticUIInput, the panel appliers and the new
//      widgets (confirmation dialog, typed Quantity box, held item display),
//      no device;
//   B. the game thread -- a REAL EverQuest whose intents are drained by
//      BridgeComponent::ProcessBridgeIntents(), its outbound stream observed
//      on a DaybreakConnection over FakeUdpTransport (the T234 idiom), the
//      prompts composed from the SHIPPED eqstr table.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "eqstr_test_db.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/confirmation_dialog.h"
#include "client/graphics/ui/confirmation_layout.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/cursor_attachment_layout.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/merchant_layout.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/quantity_layout.h"
#include "client/graphics/ui/quantity_window.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/cursor_gesture.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/merchant/merchant_component.h"
#include "client/state/event_bus.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "common/packets/merchant.h"

#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace EQT::Graphics;
using eqt::events::CursorGestureIntent;
using eqt::events::CursorGestureOp;
using eqt::inventory::ClickModifiers;

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

int cx(const irr::core::rect<irr::s32>& r) { return (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2; }
int cy(const irr::core::rect<irr::s32>& r) { return (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2; }

template <typename T>
std::vector<T> only(const std::vector<eqt::events::RendererIntent>& in) {
    std::vector<T> out;
    for (const auto& i : in) {
        if (const auto* t = std::get_if<T>(&i)) out.push_back(*t);
    }
    return out;
}

// One frame of the static UI, as renderStaticUI drives it: the dialogs
// first, then the inventory popup, then the commerce windows.
struct Rig {
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
    CombatAbilityPanelState abilities;  // B1025-H3: processFrameGesture's source
    CursorAttachmentState cursor;
    MerchantPanelState merchant;
    BankPanelState bank;

    Rig() {
        input.init(&bridge);
        input.bindCommerceSelection(&merchant, &inv);
        inv.activePopup = PopupType::Inventory;
    }
    void frame(int x, int y, bool left, bool right, ClickModifiers mods = {}) {
        input.setFrameClickState(right, mods, &cursor);
        if (input.processDialogInput(x, y, left, l)) {
            left = false;
            input.setFrameClickState(false, mods, &cursor);
        }
        const bool took =
            input.processInput(x, y, left, l, hotbar, gems, inv, book, skills, buffs, loot, mods.shift);
        // The rig never lets one click land on two windows (the inventory
        // popup and the merchant share centerPopup; a test opens one at a time).
        input.processCommerceInput(x, y, left && !took, l, merchant, bank, cursor);
        input.setFrameClickState(false, {}, &cursor);
    }
    void clickSlot(int slot, bool left, bool right, ClickModifiers mods = {}) {
        const auto r = InventoryLayout::slotRect(l, slot);
        frame(r.UpperLeftCorner.X + 3, r.UpperLeftCorner.Y + 3, left, right, mods);
    }
    // B1025-H3 (GitLab #345): a left press on a holdable single item DEFERS
    // its pickup to the holder (a hold may clone the item away instead,
    // H-O "inventory-hold"); the release frame over the source fires it.
    // The production click is this two-frame form, so the tests that assert
    // on the final state run the press, the gesture tick, the release, the
    // tick that fires the deferred action.
    void pressReleaseSlot(int slot) {
        const auto r = InventoryLayout::slotRect(l, slot);
        const int x = r.UpperLeftCorner.X + 3;
        const int y = r.UpperLeftCorner.Y + 3;
        frame(x, y, /*left=*/true, /*right=*/false);
        input.processFrameGesture(0.001f, /*buttonDown=*/true, hotbar, gems,
                                  inv, abilities);
        frame(x, y, /*left=*/false, /*right=*/false);
        input.processFrameGesture(0.001f, /*buttonDown=*/false, hotbar, gems,
                                  inv, abilities);
    }
    void clickRect(const irr::core::rect<irr::s32>& r, ClickModifiers mods = {}) {
        frame(cx(r), cy(r), true, false, mods);
    }
    void general(int slot, const char* name, int32_t qty, uint8_t bagSlots = 0, uint32_t icon = 600) {
        auto& s = inv.generalSlots[slot - 22];
        s.hasItem = true;
        s.itemName = name;
        s.quantity = qty;
        s.iconId = icon;
        s.bagSlots = bagSlots;
    }
};

ClickModifiers shiftMod() { ClickModifiers m; m.shift = true; return m; }
ClickModifiers ctrlMod() { ClickModifiers m; m.ctrl = true; return m; }
ClickModifiers altMod() { ClickModifiers m; m.alt = true; return m; }

}  // namespace

// ===========================================================================
// A1. Rows 19-21: RIGHT on a carried bag toggles its window
// ===========================================================================

// Row 19: right-click the Large Bag in general slot 22 -> its window. The
// render thread pushes ToggleBag (the /bag body), never the item display.
TEST(BagToggleRender, RightOnACarriedBagPushesToggleBagNotTheDisplay) {
    Rig rig;
    rig.general(22, "Large Bag", 1, /*bagSlots=*/6);
    rig.clickSlot(22, false, true);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* g = std::get_if<CursorGestureIntent>(&intents[0]);
    ASSERT_NE(g, nullptr) << "row 19: a bag's right click is the bag window";
    EXPECT_EQ(g->op, CursorGestureOp::ToggleBag);
    EXPECT_EQ(g->slot, 22);
    EXPECT_EQ(rig.input.takeDisplayRequest(), StaticUIInput::DisplayRequest::None);
    // Row 21: the same click again is the same intent -- the game thread
    // toggles (it is the one truth for which bags are open).
    rig.clickSlot(22, false, true);
    EXPECT_EQ(only<CursorGestureIntent>(rig.bridge.drainIntents()).at(0).op,
              CursorGestureOp::ToggleBag);
}

TEST(BagToggleRender, NegativesAreTheDisplayOrTheGesture) {
    Rig rig;
    // A plain item: right is the item display (row 24/25).
    rig.general(23, "Rusty Long Sword", 1);
    rig.clickSlot(23, false, true);
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::ItemDisplayRequestIntent>(&intents[0]), nullptr);
    // A bag LEFT-clicked is picked up like any item (row 1), not toggled.
    rig.general(22, "Large Bag", 1, 6);
    rig.clickSlot(22, true, false);
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::Click);
    // ALT+LEFT on a bag is its display (row 26), never the window.
    rig.clickSlot(22, true, false, altMod());
    intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::ItemDisplayRequestIntent>(&intents[0]), nullptr);
    // A container that is NOT in a general slot (worn) is not a carried bag:
    // the right click is its display (fable's rule; worn bags unmeasured).
    rig.inv.equipSlots[13].hasItem = true;
    rig.inv.equipSlots[13].bagSlots = 4;
    rig.clickSlot(13, false, true);
    intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::ItemDisplayRequestIntent>(&intents[0]), nullptr);
}

// ===========================================================================
// A2. Rows 24-27: the item display's two lifetimes
// ===========================================================================

eqt::state::ItemDisplayShownData shown(const char* name) {
    eqt::state::ItemDisplayShownData d;
    d.itemId = 1;
    d.name = name;
    return d;
}

// Row 25: a RIGHT press shows the display WHILE held and it is gone on release.
TEST(ItemDisplayLifetime, ARightPressShowsItOnlyWhileHeld) {
    ItemDisplayPanelState s;
    noteItemDisplayRequest(s, /*held=*/true);
    updateHeldItemDisplay(s, /*rightDown=*/true);
    applyItemDisplayShown(s, shown("Rusty Long Sword"));
    EXPECT_TRUE(s.isOpen);
    updateHeldItemDisplay(s, true);
    EXPECT_TRUE(s.isOpen) << "still held";
    updateHeldItemDisplay(s, false);
    EXPECT_FALSE(s.isOpen) << "row 25: gone on release";
}

// Row 24: press+release before the contents come back -> nothing persists.
TEST(ItemDisplayLifetime, AReleaseBeforeTheContentsArriveOpensNothing) {
    ItemDisplayPanelState s;
    noteItemDisplayRequest(s, true);
    updateHeldItemDisplay(s, /*rightDown=*/false);
    applyItemDisplayShown(s, shown("Rusty Long Sword"));
    EXPECT_FALSE(s.isOpen) << "row 24: nothing persists after release";
    // ...and the NEXT display (an ALT+LEFT) is not swallowed by it.
    noteItemDisplayRequest(s, false);
    applyItemDisplayShown(s, shown("Rusty Long Sword"));
    EXPECT_TRUE(s.isOpen);
}

// Row 26: ALT+LEFT keeps it; the right button's state does not close it.
// Row 27 (Escape closes it) is FrameComponent's existing ESC arm, which
// clears isOpen -- nothing here keeps a closed window alive.
TEST(ItemDisplayLifetime, AltLeftKeepsItThroughTheRightButtonsState) {
    ItemDisplayPanelState s;
    noteItemDisplayRequest(s, /*held=*/false);
    applyItemDisplayShown(s, shown("Rusty Long Sword"));
    for (int i = 0; i < 5; ++i) updateHeldItemDisplay(s, false);
    EXPECT_TRUE(s.isOpen) << "row 26: it stays";
    s.isOpen = false;   // ESC (row 27)
    updateHeldItemDisplay(s, false);
    EXPECT_FALSE(s.isOpen);
}

TEST(ItemDisplayLifetime, AStaleReleasedRequestIsForgotten) {
    ItemDisplayPanelState s;
    noteItemDisplayRequest(s, true);
    updateHeldItemDisplay(s, false);   // released, the request was refused
    for (int i = 0; i <= kHeldDisplayForgetFrames; ++i) updateHeldItemDisplay(s, false);
    applyItemDisplayShown(s, shown("Spell"));   // e.g. a shift+click spell display
    EXPECT_TRUE(s.isOpen);
    EXPECT_FALSE(s.heldOnly);
}

TEST(ItemDisplayLifetime, TheClickSaysWhichKindItAskedFor) {
    Rig rig;
    rig.general(23, "Rusty Long Sword", 1);
    rig.clickSlot(23, false, true);
    EXPECT_EQ(rig.input.takeDisplayRequest(), StaticUIInput::DisplayRequest::Held);
    EXPECT_EQ(rig.input.takeDisplayRequest(), StaticUIInput::DisplayRequest::None) << "read once";
    rig.clickSlot(23, true, false, altMod());
    EXPECT_EQ(rig.input.takeDisplayRequest(), StaticUIInput::DisplayRequest::Kept);
    rig.clickSlot(23, true, false);   // a plain left is a pick-up, no display
    EXPECT_EQ(rig.input.takeDisplayRequest(), StaticUIInput::DisplayRequest::None);
}

// ===========================================================================
// A3. Rows 28-30, 43-44: coin, on the cursor and between the stores
// ===========================================================================

// Row 29: the cursor draws the coin (dragitem 644 for platinum, MEASURED by
// template match against run1.G33a) with its count in a strip measured at
// (0,47) 50x12 below the box's corner.
TEST(CoinOnCursor, TheFeedTheIconAndTheMeasuredCountStrip) {
    CursorAttachmentState c;
    eqt::state::CursorCoinChangedData d;
    d.coinType = EQT::COINTYPE_PP;
    d.amount = 10;
    applyCursorCoinChanged(c, d);
    EXPECT_TRUE(c.holdsCoin());
    EXPECT_EQ(c.coinAmount, 10u);
    EXPECT_EQ(cursorCoinIcon(EQT::COINTYPE_PP), 644u) << "measured";
    EXPECT_EQ(cursorCoinIcon(EQT::COINTYPE_GP), 645u) << "inferred from dragitem5's column";
    EXPECT_EQ(cursorCoinIcon(EQT::COINTYPE_SP), 646u);
    EXPECT_EQ(cursorCoinIcon(EQT::COINTYPE_CP), 647u);
    const UILayout l = layout800();
    const auto strip = CursorAttachmentLayout::coinCountRect(l, 100, 100);
    EXPECT_EQ(strip, (irr::core::rect<irr::s32>(100, 147, 150, 159)));
    d.amount = 0;
    applyCursorCoinChanged(c, d);
    EXPECT_FALSE(c.holdsCoin());
}

// The shadow is gone: held coin is whatever the GAME THREAD published, so a
// /coin typed behind the UI's back makes the next purse click a put-back
// (row 30) with no dialog -- and with no coin published, the click is the
// dialog (row 28).
TEST(CoinOnCursor, APurseClickReadsTheCursorFeedNotAShadow) {
    Rig rig;
    rig.inv.coins[0] = 10;
    const auto pp = InventoryLayout::coinRect(rig.l, 0);
    rig.cursor.coinType = EQT::COINTYPE_GP;   // /coin gp 4, behind the UI
    rig.cursor.coinAmount = 4;
    rig.clickRect(pp);
    EXPECT_FALSE(rig.input.quantityWindow().isOpen) << "no extra dialog";
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::CoinClick);
    EXPECT_EQ(g[0].coinSource, EQT::COINSLOT_INVENTORY);

    rig.cursor.coinAmount = 0;   // the game thread said: nothing held
    rig.clickRect(pp);
    EXPECT_TRUE(rig.input.quantityWindow().isOpen) << "row 28";
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
}

// Row 43: SHIFT on the bank's platinum takes it all with no dialog, from the
// BANK (OP_MoveCoin from_slot 2).
TEST(CoinOnCursor, TheBanksCoinRowsAreTheSameGestureOnSlotTwo) {
    Rig rig;
    rig.inv.activePopup = PopupType::None;
    rig.bank.isOpen = true;
    rig.bank.coins[0] = 5;
    const auto pp = BankLayout::coinRect(rig.l, 0);
    rig.clickRect(pp, shiftMod());
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::CoinClick);
    EXPECT_EQ(g[0].coinType, EQT::COINTYPE_PP);
    EXPECT_EQ(g[0].quantity, 0);
    EXPECT_EQ(g[0].coinSource, EQT::COINSLOT_BANK) << "row 43: from_slot=2";

    // A plain click: the dialog, and its Accept keeps the bank as the store
    // (the bank's plain click is inferred from the purse's row 28).
    rig.clickRect(pp);
    ASSERT_TRUE(rig.input.quantityWindow().isOpen);
    rig.clickRect(QuantityLayout::acceptRect(rig.l));
    g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].coinSource, EQT::COINSLOT_BANK);
    EXPECT_EQ(g[0].quantity, 5);

    // Held coin clicked onto a bank row goes back to the BANK.
    rig.cursor.coinType = EQT::COINTYPE_PP;
    rig.cursor.coinAmount = 5;
    rig.clickRect(pp);
    g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].coinSource, EQT::COINSLOT_BANK);
}

// ===========================================================================
// A4. Rows 31-35: the confirmation dialog and IW_Destroy
// ===========================================================================

TEST(ConfirmationDialog, TheGeometryIsTheXmlsAnchors) {
    const UILayout l = layout800();
    const auto w = ConfirmationLayout::windowRect(l);
    EXPECT_EQ(w.getWidth(), 274);
    EXPECT_EQ(w.getHeight(), 200);
    const auto c = ConfirmationLayout::clientRect(l);
    const auto yes = ConfirmationLayout::yesRect(l);
    const auto no = ConfirmationLayout::noRect(l);
    // CD_Yes_Button: left 2 .. 66 from the LEFT; CD_No_Button: 66 .. 2 from
    // the RIGHT; both 22 .. 2 up from the BOTTOM.
    EXPECT_EQ(yes.UpperLeftCorner.X - c.UpperLeftCorner.X, 2);
    EXPECT_EQ(yes.LowerRightCorner.X - c.UpperLeftCorner.X, 66);
    EXPECT_EQ(c.LowerRightCorner.X - no.UpperLeftCorner.X, 66);
    EXPECT_EQ(c.LowerRightCorner.X - no.LowerRightCorner.X, 2);
    EXPECT_EQ(c.LowerRightCorner.Y - yes.UpperLeftCorner.Y, 22);
    EXPECT_EQ(c.LowerRightCorner.Y - yes.LowerRightCorner.Y, 2);
    const auto t = ConfirmationLayout::textRect(l);
    EXPECT_EQ(c.LowerRightCorner.Y - t.LowerRightCorner.Y, 24) << "CD_TextOutput bottom 24";
    EXPECT_TRUE(w.isRectCollided(t));
}

TEST(ConfirmationDialog, YesNoOkAndTheClicksAroundThem) {
    const UILayout l = layout800();
    ConfirmationDialogState s;
    EXPECT_EQ(processConfirmationClick(l, s, cx(ConfirmationLayout::yesRect(l)),
                                       cy(ConfirmationLayout::yesRect(l)), true),
              ConfirmationClick::None) << "a closed dialog eats nothing";
    eqt::state::ConfirmationRequestedData req;
    req.purpose = 1;
    req.text = "ARE YOU SURE you wish to drop your Rusty Long Sword on the ground?";
    applyConfirmationRequested(s, req);
    ASSERT_TRUE(s.isOpen);
    EXPECT_EQ(processConfirmationClick(l, s, cx(ConfirmationLayout::yesRect(l)),
                                       cy(ConfirmationLayout::yesRect(l)), true),
              ConfirmationClick::Yes);
    EXPECT_EQ(processConfirmationClick(l, s, cx(ConfirmationLayout::noRect(l)),
                                       cy(ConfirmationLayout::noRect(l)), true),
              ConfirmationClick::No);
    EXPECT_EQ(processConfirmationClick(l, s, cx(ConfirmationLayout::textRect(l)),
                                       cy(ConfirmationLayout::textRect(l)), true),
              ConfirmationClick::Consumed);
    EXPECT_EQ(processConfirmationClick(l, s, 2, 2, true), ConfirmationClick::None);
    // The Ok form (the reusable shape B899's 8504 refusal could use).
    s.open(9, "It will not fit.", ConfirmationDialogState::Buttons::Ok);
    EXPECT_EQ(processConfirmationClick(l, s, cx(ConfirmationLayout::okRect(l)),
                                       cy(ConfirmationLayout::okRect(l)), true),
              ConfirmationClick::Ok);
    EXPECT_EQ(processConfirmationClick(l, s, cx(ConfirmationLayout::yesRect(l)),
                                       cy(ConfirmationLayout::yesRect(l)), true),
              ConfirmationClick::Consumed) << "no Yes in the Ok form";
}

TEST(ConfirmationDialog, TheTextIsWrappedToTheBox) {
    const auto lines = wrapConfirmationText(
        "ARE YOU SURE you wish to drop your Rusty Long Sword on the ground?", 180, nullptr);
    ASSERT_GE(lines.size(), 2u);
    for (const auto& l : lines) EXPECT_LE(static_cast<int>(l.size()) * 6, 180) << l;
    EXPECT_EQ(lines.front().substr(0, 12), "ARE YOU SURE");
}

// Rows 32-33: No closes and sends nothing; Yes sends the confirm for the
// dialog's purpose. The dialog sits above the QuantityWnd and every window.
TEST(ConfirmationDialog, YesPushesTheConfirmForItsPurposeAndNoPushesNothing) {
    Rig rig;
    eqt::state::ConfirmationRequestedData req;
    req.purpose = static_cast<uint8_t>(eqt::inventory::ConfirmPurpose::DropItem);
    req.text = "drop?";
    applyConfirmationRequested(rig.input.confirmationDialog(), req);
    rig.clickRect(ConfirmationLayout::noRect(rig.l));
    EXPECT_FALSE(rig.input.confirmationDialog().isOpen) << "row 32";
    EXPECT_TRUE(rig.bridge.drainIntents().empty());

    applyConfirmationRequested(rig.input.confirmationDialog(), req);
    rig.clickRect(ConfirmationLayout::yesRect(rig.l));
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::ConfirmDrop) << "row 33";

    req.purpose = static_cast<uint8_t>(eqt::inventory::ConfirmPurpose::DestroyItem);
    applyConfirmationRequested(rig.input.confirmationDialog(), req);
    rig.clickRect(ConfirmationLayout::yesRect(rig.l));
    g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::ConfirmDestroy) << "row 35";

    // While it is up, a slot click under it is withheld from the gesture
    // (fable's rule, the QuantityWnd's row 18 applied to this dialog).
    applyConfirmationRequested(rig.input.confirmationDialog(), req);
    rig.general(22, "Rusty Long Sword", 1);
    rig.clickSlot(22, true, false);
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
}

// Row 34: IW_Destroy with an item held asks for the prompt; with nothing
// held it does nothing. The button is disjoint from every slot.
TEST(DestroyButton, AsksOnlyWithAnItemHeldAndOverlapsNoSlot) {
    Rig rig;
    const auto d = InventoryLayout::destroyButtonRect(rig.l);
    EXPECT_TRUE(rig.l.centerPopup.isRectCollided(d));
    for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
        EXPECT_FALSE(InventoryLayout::slotRect(rig.l, i).isRectCollided(d)) << "slot " << i;
    }
    for (int c = 0; c < InventoryLayout::COIN_COUNT; ++c) {
        EXPECT_FALSE(InventoryLayout::coinRect(rig.l, c).isRectCollided(d));
    }
    EXPECT_EQ(d.getWidth(), 50 * rig.l.centerPopup.getWidth() / 428) << "IW_Destroy CX=50, scaled";

    rig.clickRect(d);
    EXPECT_TRUE(rig.bridge.drainIntents().empty()) << "nothing held: nothing asked";
    rig.cursor.hasItem = true;
    rig.clickRect(d);
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::RequestDestroy);
}

// ===========================================================================
// A5. Row 5: typing into the QuantityWnd's box
// ===========================================================================

// Row 5: "Accept after typing 2 (of 3)" -> qty=2.
TEST(QuantityTyping, TypedDigitsSetTheValueAndReturnAccepts) {
    Rig rig;
    rig.general(26, "Low Quality Wolf Skin", 3);
    rig.clickSlot(26, true, false);
    ASSERT_TRUE(rig.input.quantityWindow().isOpen);
    EXPECT_FALSE(rig.input.processQuantityKey(irr::KEY_KEY_2, L'2')) << "the box is not focused yet";
    rig.clickRect(QuantityLayout::inputRect(rig.l));
    ASSERT_TRUE(rig.input.quantityWindow().input.inputActive);
    EXPECT_TRUE(rig.input.processQuantityKey(irr::KEY_KEY_2, L'2'));
    EXPECT_EQ(rig.input.quantityWindow().value, 2);
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    EXPECT_TRUE(rig.input.processQuantityKey(irr::KEY_RETURN, 0));
    EXPECT_FALSE(rig.input.quantityWindow().isOpen);
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].op, CursorGestureOp::PickUp);
    EXPECT_EQ(g[0].slot, 26);
    EXPECT_EQ(g[0].quantity, 2) << "row 5: partial => qty = picked count";
}

TEST(QuantityTyping, NonDigitsClampingEscapeAndZero) {
    Rig rig;
    rig.input.quantityWindow().openForStack(26, "Wolf Skin", 3);
    rig.clickRect(QuantityLayout::inputRect(rig.l));
    rig.input.processQuantityKey(irr::KEY_KEY_A, L'a');
    EXPECT_EQ(rig.input.quantityWindow().input.inputText, "") << "a letter is swallowed";
    rig.input.processQuantityKey(irr::KEY_KEY_9, L'9');
    EXPECT_EQ(rig.input.quantityWindow().value, 3) << "clamped to the stack";
    rig.input.processQuantityKey(irr::KEY_BACK, 0);
    EXPECT_EQ(rig.input.quantityWindow().value, 0);
    rig.input.processQuantityKey(irr::KEY_RETURN, 0);
    EXPECT_TRUE(rig.input.quantityWindow().isOpen) << "0 is not accepted";
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    rig.input.processQuantityKey(irr::KEY_ESCAPE, 0);
    EXPECT_TRUE(rig.input.quantityWindow().isOpen) << "row 16: Escape never closes it";
    EXPECT_FALSE(rig.input.quantityWindow().input.inputActive) << "it lets go of the box";
}

// ===========================================================================
// A6. #152: the merchant's select-to-sell model
// ===========================================================================

struct MerchantRig : Rig {
    MerchantRig() {
        inv.activePopup = PopupType::Inventory;
        merchant.isOpen = true;
        merchant.npcId = 117;
        MerchantItemRow row;
        row.vendorSlot = 2;
        row.itemName = "Bottle";
        row.price = 4;
        merchant.items.push_back(row);
    }
};

// Merchant row 3: a left click on an inventory item SELECTS it -- no
// pick-up, no intent -- and the shared button reads Sell.
TEST(MerchantSelectToSell, AnInventoryClickSelectsAndSendsNothing) {
    MerchantRig rig;
    EXPECT_FALSE(rig.merchant.sellSelected()) << "merchant row 1: Buy when the window opens";
    rig.general(27, "Bottle", 5, 0, 750);
    rig.clickSlot(27, true, false);
    EXPECT_TRUE(rig.bridge.drainIntents().empty()) << "selected, NOT picked up";
    EXPECT_TRUE(rig.merchant.sellSelected());
    EXPECT_EQ(rig.merchant.playerSlot, 27);
    EXPECT_EQ(rig.merchant.playerQuantity, 5);
    EXPECT_EQ(rig.merchant.playerIconId, 750u);
    EXPECT_EQ(rig.inv.selectedSlot, 27) << "the yellow frame";
    EXPECT_FALSE(rig.input.quantityWindow().isOpen) << "no stack dialog either";
}

// Merchant rows 4 and 9: a merchant row after that -> Buy (the LAST selection).
TEST(MerchantSelectToSell, TheButtonFollowsTheLastSelectedSide) {
    MerchantRig rig;
    rig.general(27, "Bottle", 5);
    rig.clickSlot(27, true, false);
    ASSERT_TRUE(rig.merchant.sellSelected());
    int x = 0, y = 0;
    bool found = false;
    for (y = 0; y < 600 && !found; ++y)
        for (x = 0; x < 800 && !found; ++x)
            found = hitTestMerchantRow(rig.l, rig.merchant, x, y) == 0;
    ASSERT_TRUE(found);
    rig.inv.activePopup = PopupType::None;   // the row is under the popup region
    rig.frame(x - 1, y - 1, true, false);
    EXPECT_FALSE(rig.merchant.sellSelected()) << "merchant row 4: Buy";
    EXPECT_EQ(rig.merchant.lastSelected, MerchantPanelState::Side::Merchant);
    // Buy now buys the row, one (merchant row 10 measured ctrl = 1).
    rig.clickRect(MerchantLayout::tradeButtonRect(rig.l), ctrlMod());
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* b = std::get_if<eqt::events::VendorBuyIntent>(&intents[0]);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->npcId, 117);
    EXPECT_EQ(b->slot, 2);
    EXPECT_EQ(b->quantity, 1);
}

// Merchant rows 5-6: Sell on a stack opens the QuantityWnd with the whole
// stack; typing 2 and Accept sells 2 STRAIGHT FROM THE SLOT (itemslot=27).
TEST(MerchantSelectToSell, SellOnAStackIsTheQuantityWndThenSellsFromTheSlot) {
    MerchantRig rig;
    rig.general(27, "Bottle", 5);
    rig.clickSlot(27, true, false);
    rig.inv.activePopup = PopupType::None;
    rig.clickRect(MerchantLayout::tradeButtonRect(rig.l));
    const auto& q = rig.input.quantityWindow();
    ASSERT_TRUE(q.isOpen);
    EXPECT_EQ(q.purpose, QuantityWindowState::Purpose::Sell);
    EXPECT_EQ(q.value, 5) << "merchant row 5: prefilled with the whole stack";
    EXPECT_TRUE(rig.bridge.drainIntents().empty());
    rig.clickRect(QuantityLayout::inputRect(rig.l));
    rig.input.processQuantityKey(irr::KEY_KEY_2, L'2');
    rig.clickRect(QuantityLayout::acceptRect(rig.l));
    auto intents = rig.bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* s = std::get_if<eqt::events::VendorSellIntent>(&intents[0]);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->npcId, 117);
    EXPECT_EQ(s->slot, 27) << "merchant row 6: from the inventory slot, never the cursor";
    EXPECT_EQ(s->quantity, 2);
}

TEST(MerchantSelectToSell, ModifiersAndASingleItemSellWithoutTheDialog) {
    MerchantRig rig;
    rig.general(27, "Bottle", 5);
    rig.general(28, "Rusty Long Sword", 1);
    rig.clickSlot(27, true, false);
    rig.inv.activePopup = PopupType::None;
    rig.clickRect(MerchantLayout::tradeButtonRect(rig.l), ctrlMod());
    auto s = only<eqt::events::VendorSellIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0].quantity, 1) << "CTRL: one (Merchant Tip)";
    rig.clickRect(MerchantLayout::tradeButtonRect(rig.l), shiftMod());
    s = only<eqt::events::VendorSellIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0].quantity, 5) << "SHIFT: the stack (Merchant Tip)";
    rig.inv.activePopup = PopupType::Inventory;
    rig.clickSlot(28, true, false);
    rig.inv.activePopup = PopupType::None;
    rig.clickRect(MerchantLayout::tradeButtonRect(rig.l));
    s = only<eqt::events::VendorSellIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0].slot, 28);
    EXPECT_EQ(s[0].quantity, 1);
    EXPECT_FALSE(rig.input.quantityWindow().isOpen);
}

// T436's default, inverted: a HELD item does not make the button Sell and is
// never what is sold. With something held, a slot click is the ordinary
// gesture (merchant rows 7-8 are unresolved ghost rows; fable invents none).
TEST(MerchantSelectToSell, TheCursorIsNeverSoldAndAHeldItemStillGoesDown) {
    MerchantRig rig;
    rig.cursor.hasItem = true;
    rig.cursor.itemName = "Bread Cake";
    rig.inv.activePopup = PopupType::None;
    rig.clickRect(MerchantLayout::tradeButtonRect(rig.l));
    EXPECT_TRUE(rig.bridge.drainIntents().empty()) << "nothing selected: no Sell of the cursor";
    rig.inv.activePopup = PopupType::Inventory;
    rig.clickSlot(25, true, false);
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u) << "a held item is put down, not 'selected'";
    EXPECT_FALSE(rig.merchant.sellSelected());
}

TEST(MerchantSelectToSell, WithNoMerchantTheClickIsTheGesture) {
    Rig rig;
    rig.general(27, "Bottle", 5);
    rig.clickSlot(27, true, false, shiftMod());
    auto g = only<CursorGestureIntent>(rig.bridge.drainIntents());
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(rig.inv.selectedSlot, -1);
}

TEST(MerchantSelectToSell, TheSelectionFollowsItsSlotAndEndsWithTheSession) {
    MerchantPanelState m;
    InventoryPanelState inv;
    m.isOpen = true;
    m.selectPlayerItem(27, "Bottle", 750, 5);
    inv.selectedSlot = 27;
    eqt::state::InventorySlotChangedData d{};
    d.slotId = 27;
    d.hasItem = true;
    d.itemName = "Bottle";
    d.quantity = 3;
    applyMerchantSelectionSlotChanged(m, inv, d);
    EXPECT_EQ(m.playerQuantity, 3) << "a partial sale";
    d.slotId = 26;
    d.hasItem = false;
    applyMerchantSelectionSlotChanged(m, inv, d);
    EXPECT_TRUE(m.sellSelected()) << "another slot changes nothing";
    d.slotId = 27;
    applyMerchantSelectionSlotChanged(m, inv, d);
    EXPECT_FALSE(m.sellSelected()) << "sold out";
    EXPECT_EQ(inv.selectedSlot, -1);

    m.selectPlayerItem(27, "Bottle", 750, 5);
    applyVendorWindowClosed(m, eqt::state::WindowClosedData{});
    EXPECT_FALSE(m.sellSelected());
    EXPECT_EQ(m.lastSelected, MerchantPanelState::Side::None);
}

// ===========================================================================
// B. The game thread: a REAL EverQuest, its wire, its eqstr
// ===========================================================================

namespace {

struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

class GameHarness {
public:
    GameHarness() : eq(zonetest::MakeEq()) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection([this](std::shared_ptr<EQ::Net::DaybreakConnection> c) {
            conn = c;
        });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);
        eq.setBridge(&bridge);
        eq.Net().Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;
        conn->m_stats.sent_packets = 1;   // T378: SafeQueueZone's "appears invalid" guard
        eq.GetGameState().world().setZoneConnected(true);
    }
    ~GameHarness() {
        eq.m_zone_connection.reset();
        eq.setBridge(nullptr);
    }

    eqt::inventory::InventoryManager& inv() { return *eq.GetInventoryManager(); }

    void seed(int16_t slot, const char* name, int32_t qty = 1, uint8_t bagSlots = 0) {
        eqt::inventory::ItemInstance it;
        it.itemId = 5000u + static_cast<uint32_t>(slot);
        it.name = name;
        it.slots = 0xFFFFFFFFu;
        it.quantity = qty;
        it.stackable = qty > 1;
        it.stackSize = qty > 1 ? 20 : 1;
        it.bagSlots = bagSlots;
        inv().setItem(slot, it);
    }

    void run(CursorGestureIntent i) {
        bridge.pushIntent(i);
        eq.Bridge().ProcessBridgeIntents();
        eq.Bridge().ProcessBridgeIntents();   // intents the first pass pushed
    }

    std::vector<SentAppPacket> sent() {
        std::vector<SentAppPacket> out;
        for (auto& [seq, s] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(s.packet.Data());
            size_t n = s.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (off < n && d[off] == 0) ++off;
            if (off + 2 > n) continue;
            SentAppPacket p;
            p.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            p.body.assign(d + off + 2, d + n);
            out.push_back(std::move(p));
        }
        return out;
    }
    std::vector<SentAppPacket> sentOp(uint16_t op) {
        std::vector<SentAppPacket> out;
        for (auto& p : sent()) if (p.opcode == op) out.push_back(p);
        return out;
    }
    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    template <typename T>
    std::vector<T> events(eqt::state::GameEventType type) {
        std::vector<T> out;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == type) out.push_back(std::get<T>(e.data));
        }
        return out;
    }

    EQ::Test::FakeChannel c2s;
    EQ::Test::FakeChannel s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    CapturingBridge bridge;
    EverQuest eq;
};

CursorGestureIntent op(CursorGestureOp o, int16_t slot = -1) {
    CursorGestureIntent i;
    i.op = o;
    i.slot = slot;
    return i;
}

}  // namespace

// Rows 19-21: ToggleBag runs the /bag body -- open, then close.
TEST(CursorGestureGame, ToggleBagOpensAndClosesTheCarriedContainer) {
    GameHarness h;
    h.seed(22, "Large Bag", 1, 6);
    h.bridge.drainEvents();
    h.run(op(CursorGestureOp::ToggleBag, 22));
    auto opened = h.events<eqt::state::TradeskillContainerOpenedEvent>(
        eqt::state::GameEventType::TradeskillContainerOpened);
    ASSERT_EQ(opened.size(), 1u) << "row 19";
    EXPECT_FALSE(opened[0].isWorldObject);
    EXPECT_EQ(opened[0].inventorySlot, 22);
    EXPECT_EQ(opened[0].slotCount, 6) << "row 19: slots = bag size";
    h.run(op(CursorGestureOp::ToggleBag, 22));
    auto closed = h.events<eqt::state::TradeskillContainerClosedEvent>(
        eqt::state::GameEventType::TradeskillContainerClosed);
    ASSERT_EQ(closed.size(), 1u) << "row 21: right-click toggles";
    EXPECT_TRUE(h.sent().empty()) << "client-side: no packet (rows 19-21)";
}

TEST(CursorGestureGame, ToggleBagOnANonContainerOpensNothing) {
    GameHarness h;
    h.seed(23, "Rusty Long Sword");
    h.bridge.drainEvents();
    h.run(op(CursorGestureOp::ToggleBag, 23));
    EXPECT_TRUE(h.events<eqt::state::TradeskillContainerOpenedEvent>(
                     eqt::state::GameEventType::TradeskillContainerOpened).empty());
}

// The bag size crosses the bridge on InventorySlotChanged, so the render
// thread can tell a bag from an item.
TEST(CursorGestureGame, TheSlotEventCarriesTheBagSize) {
    GameHarness h;
    h.seed(22, "Large Bag", 1, 6);
    h.bridge.drainEvents();
    h.run(op(CursorGestureOp::Click, 22));   // pick it up
    bool sawCursorBag = false;
    for (auto& d : h.events<eqt::state::InventorySlotChangedData>(
             eqt::state::GameEventType::InventorySlotChanged)) {
        if (d.slotId == eqt::inventory::CURSOR_SLOT && d.hasItem) {
            EXPECT_EQ(d.bagSlots, 6);
            sawCursorBag = true;
        }
    }
    EXPECT_TRUE(sawCursorBag);
}

// Rows 31 and 34: the prompts are the official eqstr lines, composed from the
// shipped table with the HELD item's name.
TEST(CursorGestureGame, TheTwoPromptsAreTheOfficialEqstrLines) {
    ASSERT_TRUE(eqt_test::ShippedStrings().isLoaded()) << "data/willeq.db";
    GameHarness h;
    ASSERT_TRUE(h.eq.Strings().LoadStringFiles(""));
    h.seed(22, "Rusty Long Sword");
    ASSERT_TRUE(h.inv().pickupItem(22));
    h.bridge.drainIntents();   // the seed pick-up's own MoveItemIntent is not under test
    h.bridge.drainEvents();
    h.run(op(CursorGestureOp::RequestDrop));
    auto asks = h.events<eqt::state::ConfirmationRequestedData>(
        eqt::state::GameEventType::ConfirmationRequested);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].text, "ARE YOU SURE you wish to drop your Rusty Long Sword on the ground?")
        << "row 31";
    EXPECT_EQ(asks[0].purpose, static_cast<uint8_t>(eqt::inventory::ConfirmPurpose::DropItem));
    EXPECT_FLOAT_EQ(asks[0].timeoutSeconds, 20.0f) << "UIGAP-live2 run3/run5: Drop 20 s";

    h.run(op(CursorGestureOp::RequestDestroy));
    asks = h.events<eqt::state::ConfirmationRequestedData>(
        eqt::state::GameEventType::ConfirmationRequested);
    ASSERT_EQ(asks.size(), 1u);
    EXPECT_EQ(asks[0].text, "Are you absolutely sure you want to destroy your Rusty Long Sword?")
        << "row 34";
    EXPECT_EQ(asks[0].purpose, static_cast<uint8_t>(eqt::inventory::ConfirmPurpose::DestroyItem));
    EXPECT_FLOAT_EQ(asks[0].timeoutSeconds, 10.0f) << "UIGAP-live2 run3/run5: Destroy 10 s";
    EXPECT_TRUE(h.sent().empty()) << "rows 31/34: nothing at the click";
}

// MEASURED by UIGAP-live2 (review/UIGAP-ORACLE/151/README.md, run3 + run5,
// two runs each): left unanswered, the Drop prompt closes itself at 20 s and
// the Destroy prompt at 10 s, sending nothing; the item stays on the cursor.
// The game side states the timeout on the request...
TEST(CursorGestureGame, ThePromptsCarryTheMeasuredTimeouts) {
    GameHarness h;
    h.eq.Strings().LoadStringFiles("");
    h.seed(22, "Rusty Dagger");
    ASSERT_TRUE(h.inv().pickupItem(22));
    const auto drop = eqt::inventory::PlanConfirmPrompt(h.inv(), CursorGestureOp::RequestDrop);
    ASSERT_TRUE(drop.prompt);
    EXPECT_FLOAT_EQ(drop.timeoutSeconds, 20.0f) << "run3/run5: drop dialog up 20.1 s";
    const auto destroy = eqt::inventory::PlanConfirmPrompt(h.inv(), CursorGestureOp::RequestDestroy);
    ASSERT_TRUE(destroy.prompt);
    EXPECT_FLOAT_EQ(destroy.timeoutSeconds, 10.0f) << "run3/run5: destroy dialog up 10.1 s";
}

// ...and the dialog's one clock closes it as a No: no intent, nothing sent.
TEST(ConfirmationDialog, AnUnansweredPromptClosesItselfAtItsTimeoutAndSendsNothing) {
    for (const auto& [purpose, timeout] :
         {std::pair{eqt::inventory::ConfirmPurpose::DropItem, 20.0f},
          std::pair{eqt::inventory::ConfirmPurpose::DestroyItem, 10.0f}}) {
        Rig rig;
        auto& dlg = rig.input.confirmationDialog();
        eqt::state::ConfirmationRequestedData req;
        req.purpose = static_cast<uint8_t>(purpose);
        req.text = "prompt";
        req.timeoutSeconds = timeout;
        applyConfirmationRequested(dlg, req);
        ASSERT_TRUE(dlg.isOpen);
        // 60 Hz frames up to just short of the timeout: still up.
        const int frames = static_cast<int>(timeout * 60.0f) - 2;
        for (int i = 0; i < frames; ++i) {
            ASSERT_FALSE(tickConfirmationDialog(dlg, 1.0f / 60.0f)) << "frame " << i;
        }
        EXPECT_TRUE(dlg.isOpen) << timeout << " s prompt still up just before its timeout";
        bool expired = false;
        for (int i = 0; i < 4 && !expired; ++i) expired = tickConfirmationDialog(dlg, 1.0f / 60.0f);
        EXPECT_TRUE(expired) << timeout << " s prompt expires at its timeout";
        dlg.close();   // the owner's No (FrameComponent::processFrameInput)
        EXPECT_TRUE(rig.bridge.drainIntents().empty()) << "a timeout sends nothing";
    }
    // No timeout stated (0): the dialog stays up until answered.
    ConfirmationDialogState s;
    s.open(1, "stays");
    EXPECT_FALSE(tickConfirmationDialog(s, 1000.0f));
    EXPECT_TRUE(s.isOpen);
    // A closed dialog never expires.
    ConfirmationDialogState closed;
    EXPECT_FALSE(tickConfirmationDialog(closed, 1000.0f));
}

TEST(CursorGestureGame, NothingHeldNothingAsked) {
    GameHarness h;
    h.eq.Strings().LoadStringFiles("");
    h.bridge.drainEvents();
    h.run(op(CursorGestureOp::RequestDrop));
    h.run(op(CursorGestureOp::RequestDestroy));
    EXPECT_TRUE(h.events<eqt::state::ConfirmationRequestedData>(
                     eqt::state::GameEventType::ConfirmationRequested).empty());
}

// Row 33: Yes on the drop prompt -> OP_GroundSpawn with an EMPTY body; the
// item leaves the cursor and slot 30 is republished empty.
TEST(CursorGestureGame, ConfirmDropSendsAnEmptyGroundSpawn) {
    GameHarness h;
    h.seed(22, "Rusty Long Sword");
    ASSERT_TRUE(h.inv().pickupItem(22));
    h.bridge.drainIntents();   // the seed pick-up's own MoveItemIntent is not under test
    h.bridge.drainEvents();
    h.clearSent();
    h.run(op(CursorGestureOp::ConfirmDrop));
    auto gs = h.sentOp(HC_OP_GroundSpawn);
    ASSERT_EQ(gs.size(), 1u) << "row 33";
    EXPECT_TRUE(gs[0].body.empty()) << "row 33: OP_GroundSpawn (0x0f47, empty body)";
    EXPECT_TRUE(h.sentOp(HC_OP_MoveItem).empty());
    EXPECT_FALSE(h.inv().hasCursorItem());
    bool cursorEmptied = false;
    for (auto& d : h.events<eqt::state::InventorySlotChangedData>(
             eqt::state::GameEventType::InventorySlotChanged)) {
        if (d.slotId == eqt::inventory::CURSOR_SLOT && !d.hasItem) cursorEmptied = true;
    }
    EXPECT_TRUE(cursorEmptied);
}

// Row 35: Yes on the destroy prompt -> OP_MoveItem from 30 to -1, qty 0 --
// the official wire, NOT OP_DeleteItem.
TEST(CursorGestureGame, ConfirmDestroySendsMoveItemCursorToMinusOne) {
    GameHarness h;
    h.seed(22, "Low Quality Wolf Skin", 3);
    ASSERT_TRUE(h.inv().pickupItem(22));
    h.bridge.drainIntents();   // the seed pick-up's own MoveItemIntent is not under test
    h.bridge.drainEvents();
    h.clearSent();
    h.run(op(CursorGestureOp::ConfirmDestroy));
    auto mv = h.sentOp(HC_OP_MoveItem);
    ASSERT_EQ(mv.size(), 1u);
    ASSERT_EQ(mv[0].body.size(), 12u);
    uint32_t from = 0, to = 0, qty = 99;
    std::memcpy(&from, mv[0].body.data(), 4);
    std::memcpy(&to, mv[0].body.data() + 4, 4);
    std::memcpy(&qty, mv[0].body.data() + 8, 4);
    EXPECT_EQ(from, 30u);
    EXPECT_EQ(to, 0xFFFFFFFFu) << "row 35: to=-1";
    EXPECT_EQ(qty, 0u);
    EXPECT_TRUE(h.sentOp(HC_OP_DeleteItem).empty());
    EXPECT_TRUE(h.sentOp(HC_OP_GroundSpawn).empty());
    EXPECT_FALSE(h.inv().hasCursorItem());
}

TEST(CursorGestureGame, AConfirmWithNothingHeldSendsNothing) {
    GameHarness h;
    h.clearSent();
    h.run(op(CursorGestureOp::ConfirmDrop));
    h.run(op(CursorGestureOp::ConfirmDestroy));
    EXPECT_TRUE(h.sentOp(HC_OP_GroundSpawn).empty());
    EXPECT_TRUE(h.sentOp(HC_OP_MoveItem).empty());
}

namespace {
std::vector<EQT::MoveCoin_Struct> moveCoins(GameHarness& h) {
    std::vector<EQT::MoveCoin_Struct> out;
    for (auto& p : h.sentOp(HC_OP_MoveCoin)) {
        if (p.body.size() < sizeof(EQT::MoveCoin_Struct)) continue;
        EQT::MoveCoin_Struct mc;
        std::memcpy(&mc, p.body.data(), sizeof(mc));
        out.push_back(mc);
    }
    return out;
}
}  // namespace

// Rows 29-30: 10pp purse -> cursor (1 -> 0), then back (0 -> 1); each move
// publishes the cursor's coin so the attachment draws it (row 29) and the
// render thread needs no shadow.
TEST(CursorGestureGame, PurseCoinRoundTripPublishesTheCursorCoin) {
    GameHarness h;
    h.eq.GetGameState().player().setCurrency(10, 0, 0, 0);
    h.bridge.drainEvents();
    h.clearSent();
    CursorGestureIntent c = op(CursorGestureOp::CoinClick);
    c.coinType = EQT::COINTYPE_PP;
    c.quantity = 10;
    h.run(c);
    auto mc = moveCoins(h);
    ASSERT_EQ(mc.size(), 1u);
    EXPECT_EQ(mc[0].from_slot, EQT::COINSLOT_INVENTORY) << "row 29";
    EXPECT_EQ(mc[0].to_slot, EQT::COINSLOT_CURSOR);
    EXPECT_EQ(mc[0].cointype1, static_cast<uint32_t>(EQT::COINTYPE_PP));
    EXPECT_EQ(mc[0].amount, 10);
    auto coin = h.events<eqt::state::CursorCoinChangedData>(eqt::state::GameEventType::CursorCoinChanged);
    ASSERT_EQ(coin.size(), 1u);
    EXPECT_EQ(coin[0].coinType, EQT::COINTYPE_PP);
    EXPECT_EQ(coin[0].amount, 10u);

    h.clearSent();
    c.quantity = 0;
    h.run(c);
    mc = moveCoins(h);
    ASSERT_EQ(mc.size(), 1u);
    EXPECT_EQ(mc[0].from_slot, EQT::COINSLOT_CURSOR) << "row 30";
    EXPECT_EQ(mc[0].to_slot, EQT::COINSLOT_INVENTORY);
    EXPECT_EQ(mc[0].amount, 10);
    coin = h.events<eqt::state::CursorCoinChangedData>(eqt::state::GameEventType::CursorCoinChanged);
    ASSERT_EQ(coin.size(), 1u);
    EXPECT_EQ(coin[0].amount, 0u) << "nothing held";
    EXPECT_EQ(h.eq.GetGameState().player().platinum(), 10u);
}

// Rows 43-44: 5pp bank -> cursor (2 -> 0), then onto the PURSE (0 -> 1).
TEST(CursorGestureGame, BankCoinToCursorThenPurse) {
    GameHarness h;
    h.eq.GetGameState().player().setCurrency(0, 0, 0, 0);
    h.eq.GetGameState().player().setBankCurrency(5, 0, 0, 0);
    h.eq.Merchant().OpenBankWindow(1);   // B996: the coin rows exist only in the open bank
    h.clearSent();
    CursorGestureIntent c = op(CursorGestureOp::CoinClick);
    c.coinType = EQT::COINTYPE_PP;
    c.coinSource = EQT::COINSLOT_BANK;
    h.run(c);
    auto mc = moveCoins(h);
    ASSERT_EQ(mc.size(), 1u);
    EXPECT_EQ(mc[0].from_slot, EQT::COINSLOT_BANK) << "row 43: from_slot=2";
    EXPECT_EQ(mc[0].to_slot, EQT::COINSLOT_CURSOR);
    EXPECT_EQ(mc[0].amount, 5);
    EXPECT_EQ(h.eq.GetGameState().player().bankPlatinum(), 0u);

    h.clearSent();
    c.coinSource = EQT::COINSLOT_INVENTORY;
    h.run(c);
    mc = moveCoins(h);
    ASSERT_EQ(mc.size(), 1u);
    EXPECT_EQ(mc[0].from_slot, EQT::COINSLOT_CURSOR) << "row 44";
    EXPECT_EQ(mc[0].to_slot, EQT::COINSLOT_INVENTORY);
    EXPECT_EQ(mc[0].amount, 5);
    EXPECT_EQ(h.eq.GetGameState().player().platinum(), 5u);
    EXPECT_FALSE(h.inv().hasCursorMoney());
}

// B996 (#316), MEASURED (review/UIGAP-ORACLE/316/README.md): with the bank
// shut the official client has no bank slot or coin row to click, so it never
// moves anything to or from 2000+. The gesture (click, /pickup, /putdown, a
// bank coin row) refuses with the window shut and works with it open.
TEST(CursorGestureGame, BankSlotsAndCoinRowsNeedTheOpenBankWindow) {
    GameHarness h;
    h.seed(22, "Rusty Dagger");
    ASSERT_TRUE(h.inv().pickupItem(22));
    h.bridge.drainIntents();
    h.eq.GetGameState().player().setBankCurrency(5, 0, 0, 0);
    h.clearSent();

    h.run(op(CursorGestureOp::Click, eqt::inventory::BANK_BEGIN));
    EXPECT_TRUE(h.sentOp(HC_OP_MoveItem).empty()) << "bank shut: no move";
    EXPECT_NE(h.inv().getCursorItem(), nullptr);
    EXPECT_EQ(h.inv().getItem(eqt::inventory::BANK_BEGIN), nullptr);
    CursorGestureIntent coin = op(CursorGestureOp::CoinClick);
    coin.coinType = EQT::COINTYPE_PP;
    coin.coinSource = EQT::COINSLOT_BANK;
    h.run(coin);
    EXPECT_TRUE(moveCoins(h).empty()) << "bank shut: no bank coin move";

    h.eq.Merchant().OpenBankWindow(1);
    h.run(op(CursorGestureOp::Click, eqt::inventory::BANK_BEGIN));
    EXPECT_EQ(h.sentOp(HC_OP_MoveItem).size(), 1u) << "bank open: 30 -> 2000";
    EXPECT_NE(h.inv().getItem(eqt::inventory::BANK_BEGIN), nullptr);
}

TEST(CursorGestureGame, BankCoinRefusalsSendNothing) {
    GameHarness h;
    h.eq.GetGameState().player().setBankCurrency(0, 0, 0, 0);
    h.clearSent();
    CursorGestureIntent c = op(CursorGestureOp::CoinClick);
    c.coinType = EQT::COINTYPE_PP;
    c.coinSource = EQT::COINSLOT_BANK;
    h.run(c);
    EXPECT_TRUE(moveCoins(h).empty());
}
