// T54: first tests for the static-panel UI (StaticUIInput hit-testing +
// an optional renderInventoryPopup smoke test). Prior to this unit,
// static_ui_input.cpp, static_panels.cpp and ui_renderer.cpp had zero
// coverage — see refactor/T54_inventory_view_test_reckoning.md.
//
// StaticUIInput::processInput() takes all UI state by reference and needs
// no Irrlicht device at all, so the hit-testing tests below run with a
// plain UILayout + panel state structs. The optional rendering smoke test
// at the bottom uses an EDT_NULL device, following the
// DoorManagerGraphicsTest pattern in test_door_loading.cpp.

#include <gtest/gtest.h>

#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"

#include <irrlicht.h>

using namespace EQT::Graphics;

namespace {

// Trivial concrete GameStateBridge — applyEvent is never exercised here,
// only pushIntent()/drainIntents() (both concrete, non-virtual, on the
// base class itself — see include/client/bridge/game_state_bridge.h).
// Same "minimal fake bridge" idiom as test_game_state_sync_bridge.cpp's
// RecordingBridge.
class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// Geometry for an 800x600 layout, computed once and reused by every test
// so each test's expected coordinates are visible inline rather than
// re-derived from UILayout's own formulas (which would make the test
// tautological against a regression in those formulas).
//
//   hotbar:      {222, 556, 578, 588}   (10 slots, 32px + 4px margin)
//   spellGems:   {764, 268, 796, 552}   (8 gems, 32px + 4px margin)
//   centerPopup: {200, 125, 600, 475}   (400x350, screen-centered)
//
// Inventory equipment grid (2 cols x 11 rows, slot=32, pad=2):
//   startX = 208, startY = 147
//   col0 slots (index = row):      x in [208,240), y = 147 + row*34
//   col1 slots (index = 11+row):   x in [242,274), y = 147 + row*34
// Inventory general row (8 slots, pad=2): genY = startY + 11*34 + 8 = 529
class StaticUIInputTest : public ::testing::Test {
protected:
    UILayout layout;
    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    StaticUIInput input;

    void SetUp() override {
        layout.computeLayout(800, 600);
    }
};

// --- Hotbar ---

TEST_F(StaticUIInputTest, HotbarFirstSlotHoverSetsIndexZero) {
    bool consumed = input.processInput(222, 570, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(hotbar.hoveredSlot, 0);
}

TEST_F(StaticUIInputTest, HotbarLastSlotHoverSetsIndexNine) {
    // Slot 9: sx = 222 + 9*36 = 546, occupies x in [546, 578).
    bool consumed = input.processInput(577, 570, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(hotbar.hoveredSlot, 9);
}

TEST_F(StaticUIInputTest, HotbarPaddingGapClearsHoverAndDoesNotConsume) {
    // Slot 0 ends at x=254 (222+32), slot 1 starts at x=258 (222+36).
    // x=256 falls in the 4px padding gap between them.
    bool consumed = input.processInput(256, 570, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_EQ(hotbar.hoveredSlot, -1);
    // Unlike the popup panels below, the hotbar/gem row only consumes the
    // point when a cell is actually hit — a gap-hover with nothing else
    // under the cursor passes through to the 3D scene.
    EXPECT_FALSE(consumed);
}

TEST_F(StaticUIInputTest, HotbarOutsideVerticalRangeClearsHover) {
    bool consumed = input.processInput(222, 540, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_EQ(hotbar.hoveredSlot, -1);
    EXPECT_FALSE(consumed);
}

TEST_F(StaticUIInputTest, HotbarClickPushesActivateIntentWithSlotIndex) {
    TestBridge bridge;
    input.init(&bridge);

    bool consumed = input.processInput(222, 570, /*leftClicked=*/true, layout,
                                        hotbar, gems, inventory, spellbook, skills);
    EXPECT_TRUE(consumed);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::HotbarActivateIntent>(intents[0]));
    EXPECT_EQ(std::get<eqt::events::HotbarActivateIntent>(intents[0]).index, 0);
}

TEST_F(StaticUIInputTest, HotbarClickWithoutInitDoesNotCrash) {
    // bridge_ defaults to nullptr until init() is called (static_ui_input.h:59).
    // The plan flagged this as a possible null-deref hazard on click paths;
    // both extant click sites (hotbar here, spell gem below) already guard
    // with `if (leftClicked && bridge_)` — this pins that guard holds.
    EXPECT_NO_FATAL_FAILURE({
        input.processInput(222, 570, /*leftClicked=*/true, layout, hotbar, gems,
                            inventory, spellbook, skills);
    });
    EXPECT_EQ(hotbar.hoveredSlot, 0);
}

// --- Spell gems ---

TEST_F(StaticUIInputTest, SpellGemFirstHoverSetsIndexZero) {
    bool consumed = input.processInput(770, 270, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(gems.hoveredGem, 0);
}

TEST_F(StaticUIInputTest, SpellGemLastHoverSetsIndexSeven) {
    // Gem 7: gy = 268 + 7*36 = 520, occupies y in [520, 552).
    bool consumed = input.processInput(770, 530, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(gems.hoveredGem, 7);
}

TEST_F(StaticUIInputTest, SpellGemClickPushesCastSpellIntent) {
    TestBridge bridge;
    input.init(&bridge);

    input.processInput(770, 270, /*leftClicked=*/true, layout, hotbar, gems,
                        inventory, spellbook, skills);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::CastSpellIntent>(intents[0]));
    EXPECT_EQ(std::get<eqt::events::CastSpellIntent>(intents[0]).gemSlot, 0);
}

// --- Inventory popup ---

TEST_F(StaticUIInputTest, InventoryEquipGridFirstCellHover) {
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(210, 150, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, 0);
}

TEST_F(StaticUIInputTest, InventoryEquipGridSecondColumnUsesColMajorIndex) {
    // Column 1, row 0: x in [242, 274), matching col*rows+row = 1*11+0 = 11
    // — the same col-major formula static_panels.cpp's renderInventoryPopup
    // uses to place slot i at (i/11, i%11), so render and hit-test agree.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(244, 150, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, 11);
}

TEST_F(StaticUIInputTest, InventoryEquipGridPaddingGapStillConsumesButNoHover) {
    // x=241 is the 2px horizontal gap between column 0 (ends 240) and
    // column 1 (starts 242). Unlike the hotbar/gem row, ANY point inside
    // centerPopup consumes the click (set unconditionally on entry to the
    // popup-region branch in static_ui_input.cpp) even when no slot is hit.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(241, 150, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryEquipGridLastPartiallyReachableRow) {
    // Row 9 (0-based): y = 147 + 9*34 = 453, slot spans y in [453, 485).
    // centerPopup's own bottom edge is y=475, so only y in [453, 475) of
    // this slot is actually reachable through the outer popup-bounds
    // check — the bottom ~10px of row 9 (475..485) can never be hit.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(210, 460, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, 9);
}

TEST_F(StaticUIInputTest, InventoryEquipGridRow10IsUnreachable) {
    // FINDING (recorded on the board, see refactor/deferred.md): row 10's
    // slot top (y=487) is entirely below centerPopup's bottom edge
    // (y=475). The outer bounds check in static_ui_input.cpp rejects the
    // point before hitTestSlotGrid ever runs, so this slot — and both
    // general-inventory slot rows below it — can never register a hover
    // or a click at the default 800x600 layout. This is a real, provable
    // layout-sizing defect (popH=350 is too short for 11 equip rows +
    // 1 general row at slot=32/pad=2), not a test gap. Left unfixed per
    // this unit's read-only-production territory; see deferred.md.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(210, 490, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryGeneralSlotsAreWhollyUnreachable) {
    // Same finding as above, sharper: genY = 147 + 11*34 + 8 = 529, which
    // is even further outside centerPopup's y in [125,475) bound than
    // equipment row 10 is. Every point in the general-inventory row fails
    // the outer bounds check, so none of InventoryPanelState's 8 general
    // slots can ever be hovered or clicked through this code path at
    // 800x600 — a production coverage gap this unit's tests newly expose.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(210, 534, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryPopupInactiveNeverHoverHits) {
    // activePopup == None: even a point squarely inside centerPopup's
    // screen region must not hover or consume — the hit test is gated on
    // activePopup, not just screen position.
    inventory.activePopup = PopupType::None;
    bool consumed = input.processInput(210, 150, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryOutsidePopupRegionNoHover) {
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(10, 10, false, layout, hotbar, gems,
                                        inventory, spellbook, skills);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

// --- processKey: popup dismissal ---
// StaticUIInput::processKey only ever CLOSES a popup (on ESC) — there is
// no key path that opens PopupType::Inventory in this class (that toggle
// happens elsewhere, e.g. the 'I' hotkey binding outside StaticUIInput).
// These tests cover the close behavior that does exist here.

TEST_F(StaticUIInputTest, EscapeClosesOpenInventoryPopup) {
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/true, false, false,
                                      inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.activePopup, PopupType::None);
}

TEST_F(StaticUIInputTest, EscapeClosesOpenSpellbookBeforeInventory) {
    // ESC closes spellbook/skills before it ever looks at the inventory
    // popup (see the ordering in static_ui_input.cpp's processKey) — pin
    // that precedence so a future reorder is caught.
    spellbook.isOpen = true;
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/true, false, false,
                                      inventory, spellbook, skills);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(spellbook.isOpen);
    // Inventory is untouched this call — ESC only closes one popup layer
    // at a time.
    EXPECT_EQ(inventory.activePopup, PopupType::Inventory);
}

TEST_F(StaticUIInputTest, EscapeWithNothingOpenReturnsFalse) {
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/true, false, false,
                                      inventory, spellbook, skills);
    EXPECT_FALSE(consumed);
}

TEST_F(StaticUIInputTest, KeyReleaseIsAlwaysIgnored) {
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/false, false, false,
                                      inventory, spellbook, skills);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(inventory.activePopup, PopupType::Inventory);
}

// ============================================================================
// Optional tail: rendering smoke test (EDT_NULL device). Only reached if
// the input tests above are green. Follows DoorManagerGraphicsTest's setup
// idiom in test_door_loading.cpp.
// ============================================================================

class UIRendererGraphicsTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        driver_ = device_->getVideoDriver();
    }

    void TearDown() override {
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }
};

TEST_F(UIRendererGraphicsTest, RenderInventoryPopupNoAtlasDoesNotCrash) {
    // No atlas, no text batch — exercises drawSprite/drawPanel's documented
    // atlas-nullptr fallback (immediate driver_->draw2DRectangle calls).
    UIRenderer ui;
    ui.init(driver_, /*atlas=*/nullptr, /*textBatch=*/nullptr);

    UILayout layout;
    layout.computeLayout(800, 600);

    InventoryPanelState inv;
    inv.activePopup = PopupType::Inventory;
    inv.equipSlots[0].hasItem = true;
    inv.equipSlots[0].itemName = "Rusty Sword";
    inv.hoveredSlot = 3;

    ui.beginFrame();
    EXPECT_NO_FATAL_FAILURE(renderInventoryPopup(ui, layout, inv));
    EXPECT_NO_FATAL_FAILURE(ui.endFrame());
}

TEST_F(UIRendererGraphicsTest, RenderInventoryPopupClosedIsNoOp) {
    UIRenderer ui;
    ui.init(driver_, nullptr, nullptr);

    UILayout layout;
    layout.computeLayout(800, 600);

    InventoryPanelState inv;
    inv.activePopup = PopupType::None;

    ui.beginFrame();
    EXPECT_NO_FATAL_FAILURE(renderInventoryPopup(ui, layout, inv));
    EXPECT_NO_FATAL_FAILURE(ui.endFrame());
}

} // namespace
