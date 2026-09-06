// T415 Leg A (board B321): the tradeskill container window.
//
// Both panels are pure state + geometry: no Irrlicht device is needed to
// assert either one, exactly like test_static_ui_panels.cpp's layout cases.
// What each test pins:
//
//   Leg A (B321) -- TradeskillContainerOpened's four fields
//   (containerName / isWorldObject / slotCount, and Closed's wasWorldObject)
//   reached a LOG_TRACE and died there. The applier below is the hop that
//   replaced it, and every rect is the ORACLE'S, quoted in
//   include/client/graphics/ui/tradeskill_layout.h from
//   /mnt/projects/EverQuestP1999/uifiles/default/EQUI_TradeskillWnd.xml.
//

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

UILayout layout800() {
    UILayout l;
    l.computeLayout(800, 600);
    return l;
}

std::string rectStr(const irr::core::rect<irr::s32>& r) {
    return "{" + std::to_string(r.UpperLeftCorner.X) + "," +
           std::to_string(r.UpperLeftCorner.Y) + "," +
           std::to_string(r.LowerRightCorner.X) + "," +
           std::to_string(r.LowerRightCorner.Y) + "}";
}

eqt::state::TradeskillContainerOpenedEvent forgeOpened() {
    eqt::state::TradeskillContainerOpenedEvent d;
    d.isWorldObject = true;
    d.objectId = 4242;
    d.inventorySlot = -1;
    d.containerName = "Forge";
    d.containerType = 15;
    d.slotCount = eqt::inventory::WORLD_COUNT;
    return d;
}

eqt::state::InventorySlotChangedData worldSlot(int16_t slot, const char* name,
                                               uint32_t itemId, uint32_t qty) {
    eqt::state::InventorySlotChangedData d;
    d.slotId = slot;
    d.hasItem = true;
    d.itemId = itemId;
    d.itemName = name;
    d.iconId = 7;
    d.quantity = qty;
    return d;
}

} // namespace

// ===========================================================================
// Leg A -- B321, the tradeskill container window
// ===========================================================================

// The open event is what makes the window exist, and all four of the fields
// B321 named have to land somewhere a panel reads.
TEST(TradeskillWindowT415, OpenEventPopulatesTheWindow) {
    TradeskillPanelState state;
    EXPECT_FALSE(state.isOpen);

    applyTradeskillContainerOpened(state, forgeOpened());

    EXPECT_TRUE(state.isOpen);
    EXPECT_EQ(state.containerName, "Forge");     // -> the title
    EXPECT_TRUE(state.isWorldObject);            // -> close behaviour
    EXPECT_EQ(state.slotCount, 10);              // -> the grid size
    EXPECT_EQ(state.objectId, 4242u);
}

// A container smaller than the oracle's ten icons draws only its own slots.
TEST(TradeskillWindowT415, SlotCountBoundsTheDrawnGrid) {
    TradeskillPanelState state;
    auto d = forgeOpened();
    d.containerName = "Sewing Kit";
    d.slotCount = 4;
    applyTradeskillContainerOpened(state, d);

    EXPECT_EQ(state.slotCount, 4);
    EXPECT_EQ(tradeskillVisibleSlots(state), 4);

    // and never more than the oracle has icons, whatever the server says
    d.slotCount = 200;
    applyTradeskillContainerOpened(state, d);
    EXPECT_EQ(tradeskillVisibleSlots(state), TradeskillPanelState::MAX_SLOTS);
}

// The close event hides it, and clears the contents so a second container
// cannot show the first one's items.
TEST(TradeskillWindowT415, CloseEventHidesTheWindow) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());
    applyTradeskillSlotChanged(state, worldSlot(eqt::inventory::WORLD_BEGIN,
                                                "Water Flask", 13005, 1));
    ASSERT_TRUE(state.slots[0].hasItem);

    eqt::state::TradeskillContainerClosedEvent closed;
    closed.wasWorldObject = true;
    closed.objectId = 4242;
    closed.inventorySlot = -1;
    applyTradeskillContainerClosed(state, closed);

    EXPECT_FALSE(state.isOpen);
    EXPECT_FALSE(state.slots[0].hasItem);
    EXPECT_TRUE(state.containerName.empty());
}

// The contents. eqemu sends a world container's items as ITEM_PACKET_WORLD_
// CONTAINER, InventoryManager maps index i to WORLD_BEGIN + i, and
// zone_dispatch publishes an enriched InventorySlotChanged for it -- the
// generic fallback that already carried name/icon/quantity. Slots outside
// WORLD_BEGIN..WORLD_END are none of this panel's business.
TEST(TradeskillWindowT415, WorldSlotsFeedTheGridAndOthersDoNot) {
    TradeskillPanelState state;
    applyTradeskillContainerOpened(state, forgeOpened());

    applyTradeskillSlotChanged(state, worldSlot(eqt::inventory::WORLD_BEGIN,
                                                "Water Flask", 13005, 1));
    applyTradeskillSlotChanged(state, worldSlot(eqt::inventory::WORLD_BEGIN + 3,
                                                "Iron Ore", 16577, 4));

    EXPECT_TRUE(state.slots[0].hasItem);
    EXPECT_EQ(state.slots[0].itemName, "Water Flask");
    EXPECT_EQ(state.slots[3].itemName, "Iron Ore");
    EXPECT_EQ(state.slots[3].quantity, 4);
    EXPECT_FALSE(state.slots[1].hasItem);

    // an equipment slot and a general slot must not land in the container
    eqt::state::InventorySlotChangedData chest = worldSlot(17, "Bronze BP", 1, 1);
    applyTradeskillSlotChanged(state, chest);
    eqt::state::InventorySlotChangedData gen = worldSlot(23, "Bread Cake", 2, 1);
    applyTradeskillSlotChanged(state, gen);
    for (int i = 0; i < TradeskillPanelState::MAX_SLOTS; ++i) {
        if (i == 0 || i == 3) continue;
        EXPECT_FALSE(state.slots[i].hasItem) << "slot " << i;
    }

    // an emptied world slot clears its cell (ZoneProcessClearObject's path)
    eqt::state::InventorySlotChangedData cleared;
    cleared.slotId = eqt::inventory::WORLD_BEGIN;
    cleared.hasItem = false;
    applyTradeskillSlotChanged(state, cleared);
    EXPECT_FALSE(state.slots[0].hasItem);
    EXPECT_TRUE(state.slots[0].itemName.empty());
}

// THE PLACEMENT IS THE ORACLE'S. Every expected number below is derived by
// hand from EQUI_TradeskillWnd.xml's anchor arithmetic (see the header's
// comment for the full derivation) and mapped into the 400x350 centerPopup
// exactly the way InventoryLayout::coinRect() maps the purse.
//
//   COMBW_ComponentArea absolute origin in the 400x525 TradeskillWnd: (279,120)
//   COMBW_RequiredIcon<i>: Location ((i%2)*42, (i/2)*42), Size 42x42
TEST(TradeskillLayoutT415, SlotRectsAreTheOraclesScaled) {
    const UILayout l = layout800();
    // popup is {200,125,600,475}; x scale 400/400, y scale 350/525
    struct Case { int i; int x0, y0, x1, y1; };
    const Case cases[] = {
        {0, 479, 205, 521, 233},
        {1, 521, 205, 563, 233},
        {2, 479, 233, 521, 261},
        {8, 479, 317, 521, 345},
        {9, 521, 317, 563, 345},
    };
    for (const auto& c : cases) {
        const auto r = TradeskillLayout::slotRect(l, c.i);
        EXPECT_EQ(r.UpperLeftCorner.X, c.x0) << "slot " << c.i << " " << rectStr(r);
        EXPECT_EQ(r.UpperLeftCorner.Y, c.y0) << "slot " << c.i << " " << rectStr(r);
        EXPECT_EQ(r.LowerRightCorner.X, c.x1) << "slot " << c.i << " " << rectStr(r);
        EXPECT_EQ(r.LowerRightCorner.Y, c.y1) << "slot " << c.i << " " << rectStr(r);
    }
    // out of range is degenerate, like InventoryLayout::slotRect()
    EXPECT_EQ(TradeskillLayout::slotRect(l, -1).getWidth(), 0);
    EXPECT_EQ(TradeskillLayout::slotRect(l, 10).getWidth(), 0);
}

// COMBW_ContainerName (title), COMBW_CombineButton, COMBW_CloseButton.
TEST(TradeskillLayoutT415, TitleCombineAndCloseAreTheOraclesScaled) {
    const UILayout l = layout800();

    const auto title = TradeskillLayout::titleRect(l);
    EXPECT_EQ(rectStr(title), "{442,171,598,184}");

    const auto combine = TradeskillLayout::combineRect(l);
    EXPECT_EQ(rectStr(combine), "{460,347,580,367}");

    const auto close = TradeskillLayout::closeRect(l);
    EXPECT_EQ(rectStr(close), "{460,459,580,473}");
}

// Every rect lies inside the popup and the two buttons do not collide with
// the slot grid -- the T57/T60 containment invariant, at five resolutions.
TEST(TradeskillLayoutT415, EverythingFitsInsideTheCenterPopup) {
    const struct { int w, h; } resolutions[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    for (const auto& rc : resolutions) {
        UILayout l;
        l.computeLayout(rc.w, rc.h);
        const auto& p = l.centerPopup;

        std::vector<irr::core::rect<irr::s32>> all;
        for (int i = 0; i < TradeskillPanelState::MAX_SLOTS; ++i) {
            all.push_back(TradeskillLayout::slotRect(l, i));
        }
        all.push_back(TradeskillLayout::titleRect(l));
        all.push_back(TradeskillLayout::combineRect(l));
        all.push_back(TradeskillLayout::closeRect(l));

        for (const auto& r : all) {
            EXPECT_TRUE(p.isRectCollided(r) || p.isPointInside(r.UpperLeftCorner))
                << rc.w << "x" << rc.h << ": " << rectStr(r);
            EXPECT_GE(r.UpperLeftCorner.X, p.UpperLeftCorner.X) << rectStr(r);
            EXPECT_GE(r.UpperLeftCorner.Y, p.UpperLeftCorner.Y) << rectStr(r);
            EXPECT_LE(r.LowerRightCorner.X, p.LowerRightCorner.X) << rectStr(r);
            EXPECT_LE(r.LowerRightCorner.Y, p.LowerRightCorner.Y) << rectStr(r);
        }

        const auto combine = TradeskillLayout::combineRect(l);
        const auto close = TradeskillLayout::closeRect(l);
        for (int i = 0; i < TradeskillPanelState::MAX_SLOTS; ++i) {
            const auto s = TradeskillLayout::slotRect(l, i);
            EXPECT_FALSE(s.isRectCollided(combine))
                << rc.w << "x" << rc.h << ": slot " << i << " hits Combine";
            EXPECT_FALSE(s.isRectCollided(close))
                << rc.w << "x" << rc.h << ": slot " << i << " hits Close";
        }
    }
}

// The buttons are hit-testable, and a closed window swallows nothing.
TEST(TradeskillWindowT415, ButtonHitTests) {
    const UILayout l = layout800();
    TradeskillPanelState state;

    const auto combine = TradeskillLayout::combineRect(l);
    const int cx = (combine.UpperLeftCorner.X + combine.LowerRightCorner.X) / 2;
    const int cy = (combine.UpperLeftCorner.Y + combine.LowerRightCorner.Y) / 2;

    EXPECT_EQ(hitTestTradeskillButton(l, state, cx, cy), TradeskillButton::None)
        << "a closed tradeskill window must not consume clicks";

    applyTradeskillContainerOpened(state, forgeOpened());
    EXPECT_EQ(hitTestTradeskillButton(l, state, cx, cy), TradeskillButton::Combine);

    const auto close = TradeskillLayout::closeRect(l);
    EXPECT_EQ(hitTestTradeskillButton(
                  l, state,
                  (close.UpperLeftCorner.X + close.LowerRightCorner.X) / 2,
                  (close.UpperLeftCorner.Y + close.LowerRightCorner.Y) / 2),
              TradeskillButton::Close);

    EXPECT_EQ(hitTestTradeskillButton(l, state, 0, 0), TradeskillButton::None);

    // a slot inside the count is hit-testable; one past it is not
    auto d = forgeOpened();
    d.slotCount = 2;
    applyTradeskillContainerOpened(state, d);
    const auto s0 = TradeskillLayout::slotRect(l, 0);
    EXPECT_EQ(hitTestTradeskillSlot(
                  l, state, s0.UpperLeftCorner.X + 2, s0.UpperLeftCorner.Y + 2), 0);
    const auto s5 = TradeskillLayout::slotRect(l, 5);
    EXPECT_EQ(hitTestTradeskillSlot(
                  l, state, s5.UpperLeftCorner.X + 2, s5.UpperLeftCorner.Y + 2), -1);
}
