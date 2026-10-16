// T290 -- the loot window, restored.
//
// Before this unit the whole renderer half of looting was missing. The old
// LootWindow (include/client/graphics/ui/loot_window.h + .cpp, 163 + 867
// lines) went with WindowManager in U07c2 `0f25cb57` and nothing replaced
// it, so:
//
//   * The four loot game events reached IrrlichtBridge only to LOG_TRACE
//     and DISCARDED their payloads (irrlicht_bridge.cpp:455-467, T283's
//     audit) -- the renderer literally did not know what was on a corpse.
//   * PopupType::Loot was a bare enumerator with no render function. It
//     stayed bare after T290 gave the window one -- the loot popup tracks
//     its own visibility on LootPanelState -- and T442 (board B450) deleted
//     the enumerator along with four siblings in the same state.
//   * LootItemIntent / LootAllIntent / CloseLootIntent sat on
//     test_reachability_sweep's kBlockedIntents: consumer arms live (T268
//     wired the /loot family onto them), no producer anywhere.
//
// Three legs, in the order a corpse travels:
//   1. applyLoot*()      -- the four bridge-arm bodies, fed real payload
//                           structs; the corpse-contents state itself.
//   2. hitTestLootRow()/hitTestLootButton() -- geometry, the twins of
//                           renderLootPopup()'s own loop.
//   3. StaticUIInput::processInput()/processKey() -- the real producer,
//                           real intent queue, asserting the intent carries
//                           the CORPSE SLOT and not the on-screen row.
//
// The load-bearing case is #3 under scroll: the list is PACKED (a corpse's
// slots are sparse), so display row != list index != corpse slot. That is
// T277's buff-bar lesson, and it is what the old window's
// getCorpseSlotFromDisplayIndex() existed for.

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/state/event_bus.h"

#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

eqt::state::LootWindowOpenedData Opened(uint16_t id, const char* name) {
    eqt::state::LootWindowOpenedData d;
    d.corpseId = id;
    d.corpseName = name;
    return d;
}
eqt::state::LootItemAddedData Added(uint16_t id, uint8_t slot, uint32_t itemId,
                                    const char* name) {
    eqt::state::LootItemAddedData d;
    d.corpseId = id;
    d.slot = slot;
    d.itemId = itemId;
    d.itemName = name;
    return d;
}
eqt::state::LootItemRemovedData Removed(uint16_t id, uint8_t slot) {
    eqt::state::LootItemRemovedData d;
    d.corpseId = id;
    d.slot = slot;
    return d;
}
eqt::state::LootWindowClosedData Closed(uint16_t id) {
    eqt::state::LootWindowClosedData d;
    d.corpseId = id;
    return d;
}

// ===================================================================
// Leg 1 -- the four bridge-arm bodies
// ===================================================================

class LootPanelStateTest : public ::testing::Test {
protected:
    LootPanelState loot;
};

TEST_F(LootPanelStateTest, StartsClosedAndEmpty) {
    EXPECT_FALSE(loot.isOpen);
    EXPECT_TRUE(loot.items.empty());
    EXPECT_EQ(loot.maxScrollOffset(), 0);
}

TEST_F(LootPanelStateTest, OpenedCarriesCorpseIdAndName) {
    applyLootWindowOpened(loot, Opened(4242, "a decaying skeleton"));
    EXPECT_TRUE(loot.isOpen);
    EXPECT_EQ(loot.corpseId, 4242);
    EXPECT_EQ(loot.corpseName, "a decaying skeleton");
    EXPECT_TRUE(loot.items.empty());
}

TEST_F(LootPanelStateTest, ItemsArrivePackedInAscendingCorpseSlotOrder) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    // Deliberately out of order and SPARSE -- a corpse's slots are not dense.
    applyLootItemAdded(loot, Added(7, 9, 1001, "Rusty Sword"));
    applyLootItemAdded(loot, Added(7, 2, 1002, "Bone Chips"));
    applyLootItemAdded(loot, Added(7, 30, 1003, "Tattered Cloak"));

    ASSERT_EQ(loot.items.size(), 3u);
    EXPECT_EQ(loot.items[0].slot, 2);
    EXPECT_EQ(loot.items[1].slot, 9);
    EXPECT_EQ(loot.items[2].slot, 30);
    EXPECT_EQ(loot.items[0].itemName, "Bone Chips");
    EXPECT_EQ(loot.items[2].itemId, 1003u);
}

TEST_F(LootPanelStateTest, RepeatedSlotUpdatesInPlaceRatherThanDuplicating) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(7, 3, 10, "Old Name"));
    applyLootItemAdded(loot, Added(7, 3, 11, "New Name"));
    ASSERT_EQ(loot.items.size(), 1u);
    EXPECT_EQ(loot.items[0].itemName, "New Name");
    EXPECT_EQ(loot.items[0].itemId, 11u);
}

TEST_F(LootPanelStateTest, ItemsForAnotherCorpseAreIgnored) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(8, 1, 10, "Not Ours"));
    EXPECT_TRUE(loot.items.empty());
}

TEST_F(LootPanelStateTest, ItemsBeforeAnyOpenAreIgnored) {
    applyLootItemAdded(loot, Added(7, 1, 10, "Too Early"));
    EXPECT_TRUE(loot.items.empty());
}

TEST_F(LootPanelStateTest, RemovedTakesExactlyThatCorpseSlot) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(7, 2, 1, "A"));
    applyLootItemAdded(loot, Added(7, 9, 2, "B"));
    applyLootItemRemoved(loot, Removed(7, 2));
    ASSERT_EQ(loot.items.size(), 1u);
    EXPECT_EQ(loot.items[0].slot, 9);
}

TEST_F(LootPanelStateTest, RemovingAnUnknownSlotIsANoOp) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(7, 2, 1, "A"));
    applyLootItemRemoved(loot, Removed(7, 5));
    EXPECT_EQ(loot.items.size(), 1u);
}

TEST_F(LootPanelStateTest, EmptyingTheCorpseDoesNotCloseThePanel) {
    // The old window auto-closed here. It must not: the SERVER owns the
    // looting state (CombatManager only leaves LootingItems on the real
    // close), and a local close would desync it.
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(7, 2, 1, "A"));
    applyLootItemRemoved(loot, Removed(7, 2));
    EXPECT_TRUE(loot.isOpen);
    EXPECT_TRUE(loot.items.empty());
}

TEST_F(LootPanelStateTest, ClosedResetsEverything) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(7, 2, 1, "A"));
    applyLootWindowClosed(loot, Closed(7));
    EXPECT_FALSE(loot.isOpen);
    EXPECT_EQ(loot.corpseId, 0);
    EXPECT_TRUE(loot.items.empty());
    EXPECT_TRUE(loot.corpseName.empty());
}

TEST_F(LootPanelStateTest, CloseForAnotherCorpseLeavesOursAlone) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootWindowClosed(loot, Closed(8));
    EXPECT_TRUE(loot.isOpen);
    EXPECT_EQ(loot.corpseId, 7);
}

TEST_F(LootPanelStateTest, ReopeningClearsThePreviousCorpsesItems) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    applyLootItemAdded(loot, Added(7, 2, 1, "A"));
    applyLootWindowOpened(loot, Opened(9, "a bat"));
    EXPECT_EQ(loot.corpseId, 9);
    EXPECT_TRUE(loot.items.empty());
    EXPECT_EQ(loot.scrollOffset, 0);
}

TEST_F(LootPanelStateTest, ScrollOffsetIsClampedWhenTheListShrinks) {
    applyLootWindowOpened(loot, Opened(7, "a rat"));
    for (uint8_t i = 0; i < 12; ++i) {
        applyLootItemAdded(loot, Added(7, i, 100 + i, "item"));
    }
    EXPECT_EQ(loot.maxScrollOffset(), 12 - LootPanelState::VISIBLE_ROWS);
    loot.scrollOffset = loot.maxScrollOffset();
    applyLootItemRemoved(loot, Removed(7, 0));
    EXPECT_EQ(loot.scrollOffset, 11 - LootPanelState::VISIBLE_ROWS);
}

// ===================================================================
// Leg 2 -- geometry
// ===================================================================

class LootGeometryTest : public ::testing::Test {
protected:
    UILayout layout;
    LootPanelState loot;

    void SetUp() override {
        layout.computeLayout(800, 600);
        applyLootWindowOpened(loot, Opened(100, "a corpse"));
    }
    void fill(int n) {
        for (int i = 0; i < n; ++i) {
            // Sparse corpse slots: 0, 3, 6, 9, ... so row index, list index
            // and corpse slot are three different numbers.
            applyLootItemAdded(loot, Added(100, static_cast<uint8_t>(i * 3),
                                           500u + i, "item"));
        }
    }
    // Centre of the Nth VISIBLE ROW, from the same corners the renderer uses.
    int rowY(int visibleRow) const {
        return layout.partnerPopup.UpperLeftCorner.Y + 20 + 8 + 16 + 8
             + visibleRow * (22 + 2) + 11;
    }
    int rowX() const { return layout.partnerPopup.UpperLeftCorner.X + 40; }
};

TEST_F(LootGeometryTest, ClosedPanelHitTestsNothing) {
    fill(3);
    int y = rowY(0);
    applyLootWindowClosed(loot, Closed(100));
    EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), y), -1);
    EXPECT_EQ(hitTestLootButton(layout, loot, rowX(), y), LootButton::None);
}

TEST_F(LootGeometryTest, EachVisibleRowMapsToItsOwnListIndex) {
    fill(5);
    for (int r = 0; r < 5; ++r) {
        EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), rowY(r)), r)
            << "visible row " << r;
    }
}

TEST_F(LootGeometryTest, RowsPastTheEndOfTheListMissEvenThoughTheyAreInThePanel) {
    fill(2);
    EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), rowY(4)), -1);
}

TEST_F(LootGeometryTest, ScrollingShiftsWhichListIndexARowHits) {
    fill(12);
    loot.scrollOffset = 3;
    EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), rowY(0)), 3);
    EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), rowY(4)), 7);
}

TEST_F(LootGeometryTest, PointsOutsideThePanelWidthMiss) {
    fill(3);
    EXPECT_EQ(hitTestLootRow(layout, loot,
        layout.partnerPopup.UpperLeftCorner.X - 5, rowY(0)), -1);
}

TEST_F(LootGeometryTest, ButtonsAreFoundAndDoNotOverlapTheRows) {
    fill(3);
    // Loot All sits above the first row; Close below the last.
    const int lootAllY = layout.partnerPopup.UpperLeftCorner.Y + 20 + 8 + 8;
    const int closeY = layout.partnerPopup.LowerRightCorner.Y - 8 - 8;
    const int cx = (layout.partnerPopup.UpperLeftCorner.X +
                    layout.partnerPopup.LowerRightCorner.X) / 2;
    EXPECT_EQ(hitTestLootButton(layout, loot,
        layout.partnerPopup.UpperLeftCorner.X + 40, lootAllY), LootButton::LootAll);
    EXPECT_EQ(hitTestLootButton(layout, loot, cx, closeY), LootButton::Close);
    // and neither y is a row
    EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), lootAllY), -1);
    EXPECT_EQ(hitTestLootRow(layout, loot, rowX(), closeY), -1);
}

// ===================================================================
// Leg 3 -- the real producer
// ===================================================================

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

class LootClickTest : public ::testing::Test {
protected:
    UILayout layout;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;
    // T443 (B454): processKey() now asks the tradeskill container first --
    // it is the topmost centre popup. Closed here, so the loot arm runs.
    TradeskillPanelState tradeskill;
    CapturingBridge bridge;
    StaticUIInput input;

    void SetUp() override {
        layout.computeLayout(800, 600);
        input.init(&bridge);
        applyLootWindowOpened(loot, Opened(100, "a corpse"));
        for (int i = 0; i < 12; ++i) {
            applyLootItemAdded(loot, Added(100, static_cast<uint8_t>(i * 3),
                                           500u + i, "item"));
        }
    }
    bool click(int x, int y) {
        return input.processInput(x, y, /*leftClicked=*/true, layout, hotbar,
                                  gems, inventory, spellbook, skills, buffs, loot);
    }
    bool hover(int x, int y) {
        return input.processInput(x, y, /*leftClicked=*/false, layout, hotbar,
                                  gems, inventory, spellbook, skills, buffs, loot);
    }
    std::vector<eqt::events::RendererIntent> drain() { return bridge.drainIntents(); }

    int rowY(int visibleRow) const {
        return layout.partnerPopup.UpperLeftCorner.Y + 20 + 8 + 16 + 8
             + visibleRow * (22 + 2) + 11;
    }
    int rowX() const { return layout.partnerPopup.UpperLeftCorner.X + 40; }
};

TEST_F(LootClickTest, ClickingARowPushesLootItemIntentCarryingTheCorpseSlot) {
    ASSERT_TRUE(click(rowX(), rowY(2)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* li = std::get_if<eqt::events::LootItemIntent>(&intents[0]);
    ASSERT_NE(li, nullptr);
    EXPECT_EQ(li->corpseId, 100);
    // Row 2 -> list index 2 -> CORPSE SLOT 6. Not 2.
    EXPECT_EQ(li->slot, 6);
}

TEST_F(LootClickTest, ScrolledClickStillCarriesTheRightCorpseSlot) {
    // The regression the old window's getCorpseSlotFromDisplayIndex()
    // guarded: on-screen row 0 with scrollOffset 4 is list index 4, which
    // is corpse slot 12.
    loot.scrollOffset = 4;
    ASSERT_TRUE(click(rowX(), rowY(0)));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* li = std::get_if<eqt::events::LootItemIntent>(&intents[0]);
    ASSERT_NE(li, nullptr);
    EXPECT_EQ(li->slot, 12);
}

TEST_F(LootClickTest, HoverSetsTheOnScreenRowNotTheListIndex) {
    loot.scrollOffset = 4;
    ASSERT_TRUE(hover(rowX(), rowY(1)));
    EXPECT_EQ(loot.hoveredRow, 1);
    EXPECT_TRUE(drain().empty());
}

TEST_F(LootClickTest, LootAllButtonPushesLootAllIntent) {
    const int lootAllY = layout.partnerPopup.UpperLeftCorner.Y + 20 + 8 + 8;
    ASSERT_TRUE(click(layout.partnerPopup.UpperLeftCorner.X + 40, lootAllY));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    const auto* la = std::get_if<eqt::events::LootAllIntent>(&intents[0]);
    ASSERT_NE(la, nullptr);
    EXPECT_EQ(la->corpseId, 100);
}

TEST_F(LootClickTest, LootAllOnAnEmptyCorpsePushesNothing) {
    loot.items.clear();
    const int lootAllY = layout.partnerPopup.UpperLeftCorner.Y + 20 + 8 + 8;
    ASSERT_TRUE(click(layout.partnerPopup.UpperLeftCorner.X + 40, lootAllY));
    EXPECT_TRUE(drain().empty());
}

TEST_F(LootClickTest, CloseButtonPushesCloseLootIntentAndLeavesThePanelUp) {
    const int closeY = layout.partnerPopup.LowerRightCorner.Y - 8 - 8;
    const int cx = (layout.partnerPopup.UpperLeftCorner.X +
                    layout.partnerPopup.LowerRightCorner.X) / 2;
    ASSERT_TRUE(click(cx, closeY));
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_NE(std::get_if<eqt::events::CloseLootIntent>(&intents[0]), nullptr);
    // Ask, never assume: only LootWindowClosed closes the panel.
    EXPECT_TRUE(loot.isOpen);
}

TEST_F(LootClickTest, EscapePushesCloseLootIntentAndLeavesThePanelUp) {
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/true, false, false,
                                     inventory, spellbook, skills, loot, tradeskill);
    EXPECT_TRUE(consumed);
    auto intents = drain();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_NE(std::get_if<eqt::events::CloseLootIntent>(&intents[0]), nullptr);
    EXPECT_TRUE(loot.isOpen);
}

TEST_F(LootClickTest, EscapeClosesTheLootPanelBeforeAnyOtherPopup) {
    spellbook.isOpen = true;
    inventory.activePopup = PopupType::Inventory;
    EXPECT_TRUE(input.processKey(irr::KEY_ESCAPE, true, false, false,
                                 inventory, spellbook, skills, loot, tradeskill));
    EXPECT_TRUE(spellbook.isOpen);
    EXPECT_EQ(inventory.activePopup, PopupType::Inventory);
}

TEST_F(LootClickTest, ClicksInsideThePanelButOnNothingAreStillConsumed) {
    // A miss between rows must not fall through to the 3D scene (the
    // buff bar's rule).
    // The 8px gap between the Loot All button (ends at +44) and the first
    // row (starts at +52).
    const int deadY = layout.partnerPopup.UpperLeftCorner.Y + 48;
    EXPECT_TRUE(click(rowX(), deadY));
    EXPECT_TRUE(drain().empty());
}

TEST_F(LootClickTest, AClosedPanelConsumesNothing) {
    applyLootWindowClosed(loot, Closed(100));
    EXPECT_FALSE(click(rowX(), rowY(0)));
    EXPECT_TRUE(drain().empty());
}

}  // namespace
