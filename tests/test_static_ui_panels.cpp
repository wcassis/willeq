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
#include "client/graphics/ui/inventory_layout.h"
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/hotbar/hotbar_slot_type.h"  // T156
#include "client/graphics/text_batch.h"       // T393: purse render smoke test
#include "client/state/event_bus.h"            // T393: CurrencyChangedData

#include <irrlicht.h>
#include <vector>
#include <cstdio>
#include <iostream>

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
// Inventory popup (T57 — EQ-authentic body layout, single shared source
// InventoryLayout::slotRect() in include/client/graphics/ui/inventory_layout.h):
// two 8-slot columns flanking an empty center, a 6-slot weapon row below
// both columns, and the 8-slot general row below that. Global slot index
// 0-21 is the equipment slot id (eqt::inventory::EquipmentSlot); 22-29 is
// EQUIP_SLOTS + general index. Full slot-to-rect table (popupX=200,
// popupY=125) is in refactor/T57_inventory_paperdoll_layout.md's Review —
// spot values used by name below:
//   SLOT_EAR1(1):   {208,145,240,177}   (left col, row 0 — first slot)
//   SLOT_BACK(8):   {208,369,240,401}   (left col, row 7 — last slot)
//   SLOT_RANGE(11): {560,209,592,241}   (right col, row 2)
//   SLOT_PRIMARY(13): {299,403,331,435} (weapon row, col 0)
//   SLOT_WAIST(20): {401,403,433,435}   (weapon row — unreachable before T57)
//   SLOT_AMMO(21):  {435,403,467,435}   (weapon row — unreachable before T57)
//   General[0](22): {265,441,297,473}   (general row — unreachable before T57)
//   General[7](29): {503,441,535,473}
class StaticUIInputTest : public ::testing::Test {
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

    void SetUp() override {
        layout.computeLayout(800, 600);
    }
};

// --- Hotbar ---

TEST_F(StaticUIInputTest, HotbarFirstSlotHoverSetsIndexZero) {
    bool consumed = input.processInput(222, 570, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(hotbar.hoveredSlot, 0);
}

TEST_F(StaticUIInputTest, HotbarLastSlotHoverSetsIndexNine) {
    // Slot 9: sx = 222 + 9*36 = 546, occupies x in [546, 578).
    bool consumed = input.processInput(577, 570, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(hotbar.hoveredSlot, 9);
}

TEST_F(StaticUIInputTest, HotbarPaddingGapClearsHoverAndDoesNotConsume) {
    // Slot 0 ends at x=254 (222+32), slot 1 starts at x=258 (222+36).
    // x=256 falls in the 4px padding gap between them.
    bool consumed = input.processInput(256, 570, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_EQ(hotbar.hoveredSlot, -1);
    // Unlike the popup panels below, the hotbar/gem row only consumes the
    // point when a cell is actually hit — a gap-hover with nothing else
    // under the cursor passes through to the 3D scene.
    EXPECT_FALSE(consumed);
}

TEST_F(StaticUIInputTest, HotbarOutsideVerticalRangeClearsHover) {
    bool consumed = input.processInput(222, 540, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_EQ(hotbar.hoveredSlot, -1);
    EXPECT_FALSE(consumed);
}

TEST_F(StaticUIInputTest, HotbarClickPushesActivateIntentWithSlotIndex) {
    TestBridge bridge;
    input.init(&bridge);

    // T156: an EMPTY slot no longer pushes anything (it used to push
    // HotbarActivateIntent{0} — "cast spell 0" — for every click on every
    // slot, assigned or not). Give the slot real content first; the full
    // producer-to-consumer chain lives in tests/test_skill_reachability.cpp.
    hotbar.slots[0].type = eqt::HotbarSlotType::Spell;
    hotbar.slots[0].id = 278;

    bool consumed = input.processInput(222, 570, /*leftClicked=*/true, layout,
                                        hotbar, gems, inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::HotbarActivateIntent>(intents[0]));
    const auto& hb = std::get<eqt::events::HotbarActivateIntent>(intents[0]);
    EXPECT_EQ(hb.index, 0);
    EXPECT_EQ(hb.type, eqt::HotbarSlotType::Spell);
    EXPECT_EQ(hb.id, 278u);
}

TEST_F(StaticUIInputTest, HotbarClickOnEmptySlotPushesNothing) {
    // T156: hoveredSlot/consumed still report the hit; only the intent is
    // suppressed. Before T156 this pushed a bogus "Spell 0" activation.
    TestBridge bridge;
    input.init(&bridge);

    bool consumed = input.processInput(222, 570, /*leftClicked=*/true, layout,
                                        hotbar, gems, inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(hotbar.hoveredSlot, 0);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST_F(StaticUIInputTest, HotbarClickWithoutInitDoesNotCrash) {
    // bridge_ defaults to nullptr until init() is called (static_ui_input.h:59).
    // The plan flagged this as a possible null-deref hazard on click paths;
    // both extant click sites (hotbar here, spell gem below) already guard
    // with `if (leftClicked && bridge_)` — this pins that guard holds.
    EXPECT_NO_FATAL_FAILURE({
        input.processInput(222, 570, /*leftClicked=*/true, layout, hotbar, gems,
                            inventory, spellbook, skills, buffs, loot);
    });
    EXPECT_EQ(hotbar.hoveredSlot, 0);
}

// --- Spell gems ---

TEST_F(StaticUIInputTest, SpellGemFirstHoverSetsIndexZero) {
    bool consumed = input.processInput(770, 270, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(gems.hoveredGem, 0);
}

TEST_F(StaticUIInputTest, SpellGemLastHoverSetsIndexSeven) {
    // Gem 7: gy = 268 + 7*36 = 520, occupies y in [520, 552).
    bool consumed = input.processInput(770, 530, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(gems.hoveredGem, 7);
}

TEST_F(StaticUIInputTest, SpellGemClickPushesCastSpellIntent) {
    TestBridge bridge;
    input.init(&bridge);

    input.processInput(770, 270, /*leftClicked=*/true, layout, hotbar, gems,
                        inventory, spellbook, skills, buffs, loot);

    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<eqt::events::CastSpellIntent>(intents[0]));
    EXPECT_EQ(std::get<eqt::events::CastSpellIntent>(intents[0]).gemSlot, 0);
}

// --- Inventory popup ---
//
// T57 replaced the old broken 2x11 equipment grid + separately-computed
// general row (which put all 8 general slots and equipment slots 20-21
// outside centerPopup at every resolution — see
// refactor/T57_inventory_paperdoll_layout.md) with a single shared
// geometry source, InventoryLayout::slotRect(), consumed by both
// renderInventoryPopup() and processInput(). The tests below exercise
// that shared source directly (containment/round-trip/no-overlap,
// exhaustive over all 30 slots) as well as through processInput().

TEST_F(StaticUIInputTest, InventoryEquipLeftColumnFirstSlotHoversEar1) {
    // SLOT_EAR1 (id 1): rect {208,145,240,177} — left column, row 0.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(210, 150, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, eqt::inventory::SLOT_EAR1);
}

TEST_F(StaticUIInputTest, InventoryEquipRightColumnHoversCorrectSlotId) {
    // SLOT_RANGE (id 11): rect {560,209,592,241} — right column, row 2.
    // Slot identity now comes from a lookup table (kLeftColumn/kRightColumn/
    // kWeaponRow in inventory_layout.h), not a col-major index formula —
    // this pins that the right column's 3rd slot really is SLOT_RANGE.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(570, 220, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, eqt::inventory::SLOT_RANGE);
}

TEST_F(StaticUIInputTest, InventoryWeaponRowPaddingGapStillConsumesButNoHover) {
    // SLOT_PRIMARY spans x in [299,331), SLOT_SECONDARY x in [333,365) —
    // x=332 falls in the 2px gap between them. Unlike the hotbar/gem row,
    // ANY point inside centerPopup consumes the click (set unconditionally
    // on entry to the popup-region branch in static_ui_input.cpp) even
    // when no slot is hit.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(332, 415, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryGeneralRowPaddingGapStillConsumesButNoHover) {
    // General[0] spans x in [265,297), General[1] x in [299,331) — x=298
    // falls in the 2px gap between them.
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(298, 455, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryEquipSlotsWaistAndAmmoAreNowReachable) {
    // FIXED (was InventoryEquipGridRow10IsUnreachable — inverted per plan):
    // SLOT_WAIST (id 20, rect {401,403,433,435}) and SLOT_AMMO (id 21,
    // rect {435,403,467,435}) were two of the slots the old layout drew
    // and hit-tested past centerPopup's bottom edge (y=475 at this
    // resolution) — permanently unreachable at every resolution, since
    // centerPopup's size doesn't depend on screen size. Both now sit
    // inside the weapon row, well within the popup.
    inventory.activePopup = PopupType::Inventory;

    bool consumedWaist = input.processInput(415, 415, false, layout, hotbar, gems,
                                             inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumedWaist);
    EXPECT_EQ(inventory.hoveredSlot, eqt::inventory::SLOT_WAIST);

    bool consumedAmmo = input.processInput(450, 415, false, layout, hotbar, gems,
                                            inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumedAmmo);
    EXPECT_EQ(inventory.hoveredSlot, eqt::inventory::SLOT_AMMO);
}

TEST_F(StaticUIInputTest, InventoryGeneralSlotsAreNowReachable) {
    // FIXED (was InventoryGeneralSlotsAreWhollyUnreachable — inverted per
    // plan): the old layout's general row (genY=529) sat entirely below
    // centerPopup's bottom edge (y=475), so none of the 8 general slots
    // could ever be hovered or clicked. The new general row (y in
    // [441,473)) sits inside centerPopup. Check first and last slot.
    inventory.activePopup = PopupType::Inventory;

    bool consumedFirst = input.processInput(280, 455, false, layout, hotbar, gems,
                                             inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumedFirst);
    EXPECT_EQ(inventory.hoveredSlot, InventoryPanelState::EQUIP_SLOTS + 0);

    bool consumedLast = input.processInput(518, 455, false, layout, hotbar, gems,
                                            inventory, spellbook, skills, buffs, loot);
    EXPECT_TRUE(consumedLast);
    EXPECT_EQ(inventory.hoveredSlot, InventoryPanelState::EQUIP_SLOTS + 7);
}

// --- Inventory popup: exhaustive shared-geometry regression tests ---
// These pin the class of bug T57 fixed (renderer and hit-test drifting
// apart from duplicated arithmetic) as untestable-to-reintroduce: both
// consume InventoryLayout::slotRect(), and these tests check every one of
// its 30 outputs directly, independent of the specific EQ-body arrangement.

TEST_F(StaticUIInputTest, InventoryAllThirtySlotsAreWhollyInsideCenterPopup) {
    const auto& popup = layout.centerPopup;
    for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
        irr::core::rect<irr::s32> r = InventoryLayout::slotRect(layout, i);
        EXPECT_GE(r.UpperLeftCorner.X, popup.UpperLeftCorner.X) << "slot " << i;
        EXPECT_GE(r.UpperLeftCorner.Y, popup.UpperLeftCorner.Y) << "slot " << i;
        EXPECT_LE(r.LowerRightCorner.X, popup.LowerRightCorner.X) << "slot " << i;
        EXPECT_LE(r.LowerRightCorner.Y, popup.LowerRightCorner.Y) << "slot " << i;
    }
}

TEST_F(StaticUIInputTest, InventoryAllThirtySlotsRoundTripThroughHitTest) {
    // For every slot, the center of its shared-geometry rect must hit-test
    // back to that same slot index — pins render and hit-test to the same
    // source rather than merely both being correct today.
    inventory.activePopup = PopupType::Inventory;
    for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
        irr::core::rect<irr::s32> r = InventoryLayout::slotRect(layout, i);
        int cx = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
        int cy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
        bool consumed = input.processInput(cx, cy, false, layout, hotbar, gems,
                                            inventory, spellbook, skills, buffs, loot);
        EXPECT_TRUE(consumed) << "slot " << i;
        EXPECT_EQ(inventory.hoveredSlot, i) << "slot " << i;
    }
}

TEST_F(StaticUIInputTest, InventoryNoTwoSlotRectsOverlap) {
    std::vector<irr::core::rect<irr::s32>> rects;
    for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
        rects.push_back(InventoryLayout::slotRect(layout, i));
    }
    for (size_t i = 0; i < rects.size(); ++i) {
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const auto& a = rects[i];
            const auto& b = rects[j];
            bool overlap = a.UpperLeftCorner.X < b.LowerRightCorner.X &&
                            a.LowerRightCorner.X > b.UpperLeftCorner.X &&
                            a.UpperLeftCorner.Y < b.LowerRightCorner.Y &&
                            a.LowerRightCorner.Y > b.UpperLeftCorner.Y;
            EXPECT_FALSE(overlap) << "slots " << i << " and " << j << " overlap";
        }
    }
}

// T60: the paperdoll rect must lie wholly inside centerPopup, like the 30
// slot rects, and must not overlap any of them (it sits in the space T57
// deliberately left empty for it — see InventoryLayout::paperdollRect()).

TEST_F(StaticUIInputTest, PaperdollRectIsWhollyInsideCenterPopup) {
    const auto& popup = layout.centerPopup;
    irr::core::rect<irr::s32> doll = InventoryLayout::paperdollRect(layout);
    EXPECT_GE(doll.UpperLeftCorner.X, popup.UpperLeftCorner.X);
    EXPECT_GE(doll.UpperLeftCorner.Y, popup.UpperLeftCorner.Y);
    EXPECT_LE(doll.LowerRightCorner.X, popup.LowerRightCorner.X);
    EXPECT_LE(doll.LowerRightCorner.Y, popup.LowerRightCorner.Y);
    // Non-degenerate — must actually have area to draw into.
    EXPECT_GT(doll.getWidth(), 0);
    EXPECT_GT(doll.getHeight(), 0);
}

TEST_F(StaticUIInputTest, PaperdollRectDoesNotOverlapAnySlotRect) {
    irr::core::rect<irr::s32> doll = InventoryLayout::paperdollRect(layout);
    for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
        irr::core::rect<irr::s32> slot = InventoryLayout::slotRect(layout, i);
        bool overlap = doll.UpperLeftCorner.X < slot.LowerRightCorner.X &&
                        doll.LowerRightCorner.X > slot.UpperLeftCorner.X &&
                        doll.UpperLeftCorner.Y < slot.LowerRightCorner.Y &&
                        doll.LowerRightCorner.Y > slot.UpperLeftCorner.Y;
        EXPECT_FALSE(overlap) << "paperdoll rect overlaps slot " << i;
    }
}

// --- T351: the inventory popup frame measured at EVERY resolution (B004) ---
//
// B004 claimed centerPopup's hardcoded 400x350 leaves general and
// equipment slots drawn outside the frame and unreachable by mouse. That
// was true before T57 and is the reason T57 exists; this test is the
// per-resolution table that pins it closed, because the tests above all
// run at the fixture's single 800x600 layout and so could not have
// distinguished "inside at 800x600" from "inside everywhere".
//
// The measurement prints the full resolution x slot table (rect,
// inside-frame?, hit-testable?) to stdout, and asserts, for every
// supported resolution:
//   - the popup frame lies wholly on screen,
//   - all 30 slot rects and the paperdoll rect lie wholly inside it,
//   - the center of every slot rect hit-tests back to that slot.
//
// centerPopup's SIZE is resolution-independent by construction and
// InventoryLayout's geometry is relative to the popup's own origin and
// width, so containment cannot vary with resolution — the table proves
// that rather than assuming it.
namespace {

struct ResCase { int w; int h; const char* note; };

const ResCase kResolutions[] = {
    {640,  480,  "below the documented minimum, checked for breadth"},
    {800,  600,  "default (-r 800 600), also the orangepi preset's display"},
    {1024, 768,  ""},
    {1280, 1024, ""},
    {1920, 1080, ""},
};

} // namespace

TEST(InventoryPopupFrameT351, EverySlotIsInsideTheFrameAndHitTestableAtEveryResolution) {
    for (const auto& rc : kResolutions) {
        UILayout layout;
        layout.computeLayout(rc.w, rc.h);
        const auto& popup = layout.centerPopup;

        std::cout << "\n=== " << rc.w << "x" << rc.h;
        if (rc.note[0]) std::cout << "  (" << rc.note << ")";
        std::cout << " ===\n";
        std::cout << "frame centerPopup = {" << popup.UpperLeftCorner.X << ","
                  << popup.UpperLeftCorner.Y << "," << popup.LowerRightCorner.X << ","
                  << popup.LowerRightCorner.Y << "}  "
                  << popup.getWidth() << "x" << popup.getHeight()
                  << "  onScreen=" << ((popup.UpperLeftCorner.X >= 0 &&
                                        popup.UpperLeftCorner.Y >= 0 &&
                                        popup.LowerRightCorner.X <= rc.w &&
                                        popup.LowerRightCorner.Y <= rc.h) ? "yes" : "NO")
                  << "\n";

        // The frame itself must be on screen, or nothing inside it is
        // reachable however well it encloses its slots.
        EXPECT_GE(popup.UpperLeftCorner.X, 0) << rc.w << "x" << rc.h;
        EXPECT_GE(popup.UpperLeftCorner.Y, 0) << rc.w << "x" << rc.h;
        EXPECT_LE(popup.LowerRightCorner.X, rc.w) << rc.w << "x" << rc.h;
        EXPECT_LE(popup.LowerRightCorner.Y, rc.h) << rc.w << "x" << rc.h;

        StaticUIInput input;
        HotbarPanelState hotbar;
        SpellGemPanelState gems;
        InventoryPanelState inventory;
        SpellbookPopupState spellbook;
        SkillsPopupState skills;
        BuffBarState buffs;
        LootPanelState loot;
        inventory.activePopup = PopupType::Inventory;

        std::cout << "slot | rect                      | inside | hit\n";
        for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
            irr::core::rect<irr::s32> r = InventoryLayout::slotRect(layout, i);
            bool inside = r.UpperLeftCorner.X >= popup.UpperLeftCorner.X &&
                          r.UpperLeftCorner.Y >= popup.UpperLeftCorner.Y &&
                          r.LowerRightCorner.X <= popup.LowerRightCorner.X &&
                          r.LowerRightCorner.Y <= popup.LowerRightCorner.Y;

            int cx = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
            int cy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
            inventory.hoveredSlot = -1;
            bool consumed = input.processInput(cx, cy, false, layout, hotbar, gems,
                                               inventory, spellbook, skills, buffs, loot);
            bool hit = consumed && inventory.hoveredSlot == i;

            char line[128];
            std::snprintf(line, sizeof(line), "%4d | {%4d,%4d,%4d,%4d}       |  %-4s  | %s\n",
                          i, r.UpperLeftCorner.X, r.UpperLeftCorner.Y,
                          r.LowerRightCorner.X, r.LowerRightCorner.Y,
                          inside ? "yes" : "NO", hit ? "yes" : "NO");
            std::cout << line;

            EXPECT_TRUE(inside) << rc.w << "x" << rc.h << " slot " << i;
            EXPECT_TRUE(hit) << rc.w << "x" << rc.h << " slot " << i;
        }

        irr::core::rect<irr::s32> doll = InventoryLayout::paperdollRect(layout);
        bool dollInside = doll.UpperLeftCorner.X >= popup.UpperLeftCorner.X &&
                          doll.UpperLeftCorner.Y >= popup.UpperLeftCorner.Y &&
                          doll.LowerRightCorner.X <= popup.LowerRightCorner.X &&
                          doll.LowerRightCorner.Y <= popup.LowerRightCorner.Y;
        char line[128];
        std::snprintf(line, sizeof(line), "doll | {%4d,%4d,%4d,%4d}       |  %-4s  | n/a\n",
                      doll.UpperLeftCorner.X, doll.UpperLeftCorner.Y,
                      doll.LowerRightCorner.X, doll.LowerRightCorner.Y,
                      dollInside ? "yes" : "NO");
        std::cout << line;
        EXPECT_TRUE(dollInside) << rc.w << "x" << rc.h << " paperdoll";
    }
}

TEST_F(StaticUIInputTest, InventoryPopupInactiveNeverHoverHits) {
    // activePopup == None: even a point squarely inside centerPopup's
    // screen region must not hover or consume — the hit test is gated on
    // activePopup, not just screen position.
    inventory.activePopup = PopupType::None;
    bool consumed = input.processInput(210, 150, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(inventory.hoveredSlot, -1);
}

TEST_F(StaticUIInputTest, InventoryOutsidePopupRegionNoHover) {
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processInput(10, 10, false, layout, hotbar, gems,
                                        inventory, spellbook, skills, buffs, loot);
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
                                      inventory, spellbook, skills, loot);
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
                                      inventory, spellbook, skills, loot);
    EXPECT_TRUE(consumed);
    EXPECT_FALSE(spellbook.isOpen);
    // Inventory is untouched this call — ESC only closes one popup layer
    // at a time.
    EXPECT_EQ(inventory.activePopup, PopupType::Inventory);
}

TEST_F(StaticUIInputTest, EscapeWithNothingOpenReturnsFalse) {
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/true, false, false,
                                      inventory, spellbook, skills, loot);
    EXPECT_FALSE(consumed);
}

TEST_F(StaticUIInputTest, KeyReleaseIsAlwaysIgnored) {
    inventory.activePopup = PopupType::Inventory;
    bool consumed = input.processKey(irr::KEY_ESCAPE, /*pressed=*/false, false, false,
                                      inventory, spellbook, skills, loot);
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

// ---------------------------------------------------------------------------
// T368 (B101): the group / pet / buff windows compose their key toggle with
// the auto-driven state that used to be their ONLY gate.
//
// The three predicates below are the single body each render function's early
// return calls, so a test can assert "the panel draws" without an Irrlicht
// device. Before T368 there was no isOpen at all and the G / P / Alt+B keys
// reached empty switch arms.
//
// The default (isOpen == true) is the official client's: its defaults.ini
// writes `Show=0` only under a section whose window ships CLOSED
// ([SelectorWindow], [HotButtonWnd3], [HotButtonWnd4]) and writes no Show key
// under [GroupWindow], [PetInfoWindow] or [BuffWindow].
// ---------------------------------------------------------------------------

TEST(T368PanelVisibility, GroupPanelDefaultsOpenAndDrawsItsEmptyFrameWithNoGroup) {
    GroupPanelState g;
    EXPECT_TRUE(g.isOpen) << "the official [GroupWindow] has no Show=0 in defaults.ini";
    EXPECT_FALSE(g.inGroup);
    EXPECT_TRUE(groupPanelIsVisible(g))
        << "an open group window with no group must still draw its empty frame";
}

TEST(T368PanelVisibility, ClosedGroupPanelStaysClosedWhenYouJoinAGroup) {
    GroupPanelState g;
    g.isOpen = false;
    EXPECT_FALSE(groupPanelIsVisible(g));
    g.inGroup = true;
    g.memberCount = 3;
    EXPECT_FALSE(groupPanelIsVisible(g))
        << "joining a group reopened a window the player had closed";
}

TEST(T368PanelVisibility, PetPanelNeedsBothTheToggleAndAPet) {
    PetPanelState p;
    EXPECT_TRUE(p.isOpen);
    EXPECT_FALSE(petPanelIsVisible(p)) << "no pet -- nothing to draw";
    p.hasPet = true;
    EXPECT_TRUE(petPanelIsVisible(p));
    p.isOpen = false;
    EXPECT_FALSE(petPanelIsVisible(p)) << "P must hide a pet panel that has a pet";
}

TEST(T368PanelVisibility, BuffBarNeedsBothTheToggleAndABuff) {
    BuffBarState b;
    EXPECT_TRUE(b.isOpen);
    EXPECT_FALSE(buffBarIsVisible(b)) << "empty bar -- nothing to draw";
    b.buffs[0].spellId = 15;
    EXPECT_TRUE(buffBarIsVisible(b));
    b.isOpen = false;
    EXPECT_FALSE(buffBarIsVisible(b)) << "Alt+B must hide a bar that has buffs";
}

TEST(T368PanelVisibility, AClosedBuffBarIsUnclickableAsWellAsInvisible) {
    // The hit-test reads the same predicate, so an icon you cannot see can
    // never be clicked off (T277 pinned the geometry half of this).
    UILayout layout;
    layout.computeLayout(800, 600);
    BuffBarState b;
    b.buffs[0].spellId = 15;
    const int x = layout.buffBar.UpperLeftCorner.X + 4;
    const int y = layout.buffBar.UpperLeftCorner.Y + 4;
    EXPECT_EQ(hitTestBuffBar(layout, b, x, y), 0);
    b.isOpen = false;
    EXPECT_EQ(hitTestBuffBar(layout, b, x, y), -1);
}

} // namespace

// =============================================================================
// T382 Leg B (board B170): the EMPTY group window's shape, against the ORACLE
// =============================================================================
//
// MEASURED T377 (review/T377/oracle/): with the group window open and NO
// group, the official client paints ONLY its title bar -- a single strip
// reading "Group", no member rows, no empty body, no reserved height. T368
// gave fable the full titled FRAME, five member rows tall and empty, because
// what the oracle painted was unknown at the time (B115's second half).
//
// groupPanelFrameRect() is the seam: it returns the rect renderGroupPanel
// actually paints, so the shape is assertable with no Irrlicht device.
class GroupPanelShapeTest : public ::testing::Test {
protected:
    UILayout layout;
    GroupPanelState group;

    void SetUp() override {
        layout.computeLayout(800, 600);
    }
};

TEST_F(GroupPanelShapeTest, WithNoGroupOnlyTheTitleBarIsPainted) {
    group.isOpen = true;
    group.inGroup = false;

    const auto frame = groupPanelFrameRect(layout, group);

    EXPECT_EQ(frame.getHeight(), UILayout::BAR_HEIGHT + UILayout::MARGIN)
        << "the oracle paints a title strip, not a five-row frame";
    EXPECT_LT(frame.getHeight(), layout.groupPanel.getHeight())
        << "the empty window must be SHORTER than the full panel";
}

TEST_F(GroupPanelShapeTest, WithAGroupTheFullPanelIsPainted) {
    group.isOpen = true;
    group.inGroup = true;

    EXPECT_EQ(groupPanelFrameRect(layout, group), layout.groupPanel);
}

// The strip and the full panel share an origin and a width -- joining a group
// grows the frame downward and moves nothing.
TEST_F(GroupPanelShapeTest, TheStripSharesTheFullPanelsOriginAndWidth) {
    group.isOpen = true;
    group.inGroup = false;
    const auto strip = groupPanelFrameRect(layout, group);

    EXPECT_EQ(strip.UpperLeftCorner, layout.groupPanel.UpperLeftCorner);
    EXPECT_EQ(strip.getWidth(), layout.groupPanel.getWidth());
}

// The title is drawn at the panel's origin + 2, so the strip must be tall
// enough to contain the text row it exists for.
TEST_F(GroupPanelShapeTest, TheStripContainsTheTitleTextRow) {
    group.isOpen = true;
    group.inGroup = false;
    const auto strip = groupPanelFrameRect(layout, group);

    const irr::s32 titleTop = layout.groupPanel.UpperLeftCorner.Y + 2;
    EXPECT_GE(titleTop + UILayout::BAR_HEIGHT, strip.UpperLeftCorner.Y);
    EXPECT_LE(titleTop, strip.LowerRightCorner.Y);
}

// The window is still SHOWN with no group -- the oracle's defaults.ini writes
// no Show=0 under [GroupWindow], and T377 saw it open. Only its body is gone.
TEST_F(GroupPanelShapeTest, AnEmptyGroupWindowIsStillVisible) {
    group.isOpen = true;
    group.inGroup = false;
    EXPECT_TRUE(groupPanelIsVisible(group));

    group.isOpen = false;
    EXPECT_FALSE(groupPanelIsVisible(group));
}

// Shape at the other resolutions the layout is computed for.
TEST(GroupPanelShapeAtEveryResolution, TheEmptyWindowIsOneTitleStripEverywhere) {
    const int kRes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}};
    for (const auto& r : kRes) {
        UILayout layout;
        layout.computeLayout(r[0], r[1]);
        GroupPanelState group;
        group.isOpen = true;
        group.inGroup = false;

        const auto frame = groupPanelFrameRect(layout, group);
        EXPECT_EQ(frame.getHeight(), UILayout::BAR_HEIGHT + UILayout::MARGIN)
            << r[0] << "x" << r[1];
        EXPECT_EQ(frame.getWidth(), layout.groupPanel.getWidth())
            << r[0] << "x" << r[1];
    }
}

// ===========================================================================
// T393 (board B201) — the purse is drawn
//
// Before this unit the four coin counts reached nothing: IrrlichtBridge's
// CurrencyChanged arm was a LOG_TRACE that discarded its payload, and
// IrrlichtRenderer::updateCharacterStats() discarded platinum/gold/silver/
// copper outright, so T386 opened the inventory window right after a corpse
// loot and found no coin counters at all
// (review/T386/fable/T386_fable_arena2_purse_01_after_1024x768.png).
//
// THE PLACEMENT IS THE ORACLE'S. Quoted from the official default UI,
// /mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml:
//
//   <Screen item="InventoryWindow">  Size CX=428 CY=460
//   IW_Money0 (A_PlatinumCoin)  Location 303,121  Size 70x24
//   IW_Money1 (A_GoldCoin)      Location 303,147  Size 70x24
//   IW_Money2 (A_SilverCoin)    Location 303,171  Size 70x24
//   IW_Money3 (A_CopperCoin)    Location 303,197  Size 70x24
//   (each DecalOffset 1,3  DecalSize 18x18)
//
// centerPopup is a fixed 400x350 at every resolution (T351 measured five), so
// the scale rule is the proportional map 428x460 -> 400x350, integer
// division. Expected rects at 800x600 (popup origin 200,125):
//   x = 200 + 303*400/428 = 483, w = 70*400/428 = 65
//   y = 125 + {121,147,171,197}*350/460 = {217,236,255,274}, h = 24*350/460 = 18
// ===========================================================================

class PurseT393 : public ::testing::Test {
protected:
    UILayout layout;
    InventoryPanelState inv;
    void SetUp() override { layout.computeLayout(800, 600); }
};

TEST_F(PurseT393, EachCoinRowIsTheOracleRectScaledIntoThePopup) {
    const irr::core::rect<irr::s32> expected[4] = {
        {483, 217, 548, 235},  // IW_Money0 platinum
        {483, 236, 548, 254},  // IW_Money1 gold
        {483, 255, 548, 273},  // IW_Money2 silver
        {483, 274, 548, 292},  // IW_Money3 copper
    };
    for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
        EXPECT_EQ(InventoryLayout::coinRect(layout, d), expected[d])
            << "denomination " << d;
    }
}

TEST_F(PurseT393, TheFourRowsRunPlatinumToCopperTopDownAndDoNotOverlap) {
    for (int d = 1; d < InventoryLayout::COIN_COUNT; ++d) {
        const auto above = InventoryLayout::coinRect(layout, d - 1);
        const auto here = InventoryLayout::coinRect(layout, d);
        EXPECT_GE(here.UpperLeftCorner.Y, above.LowerRightCorner.Y)
            << "row " << d << " overlaps row " << (d - 1);
        EXPECT_EQ(here.UpperLeftCorner.X, above.UpperLeftCorner.X)
            << "the oracle's four buttons share one X";
        EXPECT_EQ(here.getWidth(), above.getWidth());
        EXPECT_EQ(here.getHeight(), above.getHeight());
    }
}

TEST_F(PurseT393, EveryCoinRowLiesWhollyInsideThePopupFrame) {
    for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
        const auto r = InventoryLayout::coinRect(layout, d);
        EXPECT_TRUE(layout.centerPopup.isRectCollided(r)) << d;
        EXPECT_GE(r.UpperLeftCorner.X, layout.centerPopup.UpperLeftCorner.X) << d;
        EXPECT_GE(r.UpperLeftCorner.Y, layout.centerPopup.UpperLeftCorner.Y) << d;
        EXPECT_LE(r.LowerRightCorner.X, layout.centerPopup.LowerRightCorner.X) << d;
        EXPECT_LE(r.LowerRightCorner.Y, layout.centerPopup.LowerRightCorner.Y) << d;
    }
}

// The purse must not cover any of the 30 item slots — a coin row over a slot
// would make that slot unreadable and (because the hit test is geometric)
// visually lie about where a click lands.
TEST_F(PurseT393, NoCoinRowOverlapsAnyInventorySlot) {
    for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
        const auto coin = InventoryLayout::coinRect(layout, d);
        for (int i = 0; i < InventoryLayout::TOTAL_SLOTS; ++i) {
            const auto slot = InventoryLayout::slotRect(layout, i);
            const bool overlaps =
                coin.UpperLeftCorner.X < slot.LowerRightCorner.X &&
                slot.UpperLeftCorner.X < coin.LowerRightCorner.X &&
                coin.UpperLeftCorner.Y < slot.LowerRightCorner.Y &&
                slot.UpperLeftCorner.Y < coin.LowerRightCorner.Y;
            EXPECT_FALSE(overlaps) << "coin row " << d << " over slot " << i;
        }
    }
}

TEST_F(PurseT393, TheDecalBoxSitsInsideItsOwnRow) {
    for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
        const auto row = InventoryLayout::coinRect(layout, d);
        const auto decal = InventoryLayout::coinDecalRect(layout, d);
        EXPECT_GE(decal.UpperLeftCorner.X, row.UpperLeftCorner.X) << d;
        EXPECT_GE(decal.UpperLeftCorner.Y, row.UpperLeftCorner.Y) << d;
        EXPECT_LE(decal.LowerRightCorner.X, row.LowerRightCorner.X) << d;
        EXPECT_LE(decal.LowerRightCorner.Y, row.LowerRightCorner.Y) << d;
    }
}

TEST_F(PurseT393, AnOutOfRangeDenominationIsDegenerate) {
    for (int d : {-1, 4, 99}) {
        EXPECT_EQ(InventoryLayout::coinRect(layout, d),
                  irr::core::rect<irr::s32>(0, 0, 0, 0)) << d;
        EXPECT_EQ(InventoryLayout::coinDecalRect(layout, d),
                  irr::core::rect<irr::s32>(0, 0, 0, 0)) << d;
        EXPECT_EQ(coinRowText(inv, d), std::string()) << d;
    }
}

// centerPopup is a fixed 400x350 at every resolution, so the purse must be at
// the same offset inside the frame everywhere — the same invariant T351
// measured for the slots.
TEST(PurseT393Resolutions, TheRowsAreIdenticalRelativeToThePopupOrigin) {
    const int kRes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    const irr::core::position2d<irr::s32> expectedOrigin[4] = {
        {283, 92}, {283, 111}, {283, 130}, {283, 149},
    };
    for (const auto& r : kRes) {
        UILayout layout;
        layout.computeLayout(r[0], r[1]);
        for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
            const auto rect = InventoryLayout::coinRect(layout, d);
            EXPECT_EQ(rect.UpperLeftCorner.X - layout.centerPopup.UpperLeftCorner.X,
                      expectedOrigin[d].X) << r[0] << "x" << r[1] << " d=" << d;
            EXPECT_EQ(rect.UpperLeftCorner.Y - layout.centerPopup.UpperLeftCorner.Y,
                      expectedOrigin[d].Y) << r[0] << "x" << r[1] << " d=" << d;
        }
    }
}

// --- the counts travel: the real publisher payload through the real applier

TEST_F(PurseT393, StartsWithAnEmptyPurse) {
    for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
        EXPECT_EQ(coinRowText(inv, d), "0") << d;
    }
}

TEST_F(PurseT393, ACurrencyChangedEventPutsTheFourCountsOnThePanel) {
    // T386 trial 2's post-loot purse, read out of character_currency:
    // 8 platinum / 18 gold / 8 silver / 16 copper.
    eqt::state::CurrencyChangedData d;
    d.platinum = 8; d.gold = 18; d.silver = 8; d.copper = 16;
    applyCurrencyChanged(inv, d);

    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_PLATINUM), "8");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_GOLD), "18");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_SILVER), "8");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_COPPER), "16");
}

// Absolute counts, not deltas — every publisher sends the whole purse, so a
// second event REPLACES the first (T386 trial 1 -> trial 2).
TEST_F(PurseT393, ASecondEventReplacesTheFirstRatherThanAccumulating) {
    eqt::state::CurrencyChangedData first;
    first.platinum = 5; first.gold = 15; first.silver = 3; first.copper = 11;
    applyCurrencyChanged(inv, first);

    eqt::state::CurrencyChangedData second;
    second.platinum = 8; second.gold = 18; second.silver = 8; second.copper = 16;
    applyCurrencyChanged(inv, second);

    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_PLATINUM), "8");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_GOLD), "18");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_SILVER), "8");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_COPPER), "16");
}

// A zero denomination still gets a row: the oracle ships four IW_Money
// buttons unconditionally, and eqemu's digit-division split produces zeros
// often (review/T386 §1d).
TEST_F(PurseT393, AZeroDenominationStillPrintsItsRow) {
    eqt::state::CurrencyChangedData d;
    d.platinum = 0; d.gold = 0; d.silver = 7; d.copper = 0;
    applyCurrencyChanged(inv, d);

    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_PLATINUM), "0");
    EXPECT_EQ(coinRowText(inv, InventoryLayout::COIN_SILVER), "7");
    EXPECT_FALSE(InventoryLayout::coinRect(layout, InventoryLayout::COIN_PLATINUM)
                     .getWidth() == 0)
        << "the row is drawn whatever the count";
}

// --- and the popup actually draws them
//
// renderInventoryPopup() emits, per frame: 1 title + 22 equipment slot labels
// + 1 "General" label + 0 general-slot labels (no items here) + 1 per coin
// row (the count beside the decal) = 28.
//
// The history is the point. Before T393 it emitted 24 and no coin row
// existed. T393 added TWO text entries per row -- the count AND a
// denomination letter standing in for the decal, because fable's atlas had
// no coin sprite (board B218) -- so it emitted 32. T398 put the oracle's own
// coin decals in the atlas and `renderInventoryPopup` now draws a SPRITE
// there, so the letter is gone and the count is the only text left: 28.
// A regression that dropped the decal back to a letter would read 32 here,
// and one that dropped the decal entirely would still read 28 -- which is
// why UIAtlasCoinDecalTest in tests/test_ui_atlas_pixels.cpp asserts on the
// pixels the decal actually paints.
TEST(PurseT393Render, TheInventoryPopupEmitsOneTextEntryPerCoinRow) {
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
    params.LoggingLevel = irr::ELL_NONE;
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) GTEST_SKIP() << "no EDT_NULL device";

    TextBatch tb;
    if (!tb.init(device->getGUIEnvironment())) {
        device->drop();
        GTEST_SKIP() << "EDT_NULL device has no built-in font";
    }

    UILayout layout;
    layout.computeLayout(800, 600);
    InventoryPanelState inv;
    inv.activePopup = PopupType::Inventory;
    eqt::state::CurrencyChangedData d;
    d.platinum = 8; d.gold = 18; d.silver = 8; d.copper = 16;
    applyCurrencyChanged(inv, d);

    UIRenderer ui;
    ui.init(device->getVideoDriver(), nullptr, &tb);
    ui.beginFrame();
    tb.begin();
    renderInventoryPopup(ui, layout, inv);

    EXPECT_EQ(tb.getQuadCount(), static_cast<size_t>(28))
        << "1 title + 22 equipment labels + 1 General + 4 coin counts "
           "(the decal is a sprite since T398, not a letter)";

    device->drop();
}

// ===========================================================================
// T409 Leg A — the paperdoll at the ORACLE's proportion (board B219)
//
// B219: fable's paperdoll filled the whole gap between the two equipment
// columns (x 48..352 of the 400-wide popup, 76% of its width), so the
// oracle-placed coin column T393 introduced (x 283..348) landed ON the
// doll's right-hand margin — confirmed on screen by T401's live pair.
// Will ruled 2026-09-06: the doll narrows to the oracle's proportion, the
// coin column stays.
//
// The oracle rect, quoted from the official Titanium/P1999 default UI,
// /mnt/projects/EverQuestP1999/uifiles/default/EQUI_Inventory.xml:
//
//   <Screen item="IW_CharacterView">  Location X=166 Y=57  Size CX=85 CY=168
//   inside <Screen item="InventoryWindow">  Size CX=428 CY=460
//
// mapped by coinRect()'s own rule (proportional, integer division, from
// 428x460 onto centerPopup's fixed 400x350):
//   x = 166*400/428 = 155   w = 85*400/428  = 79
//   y =  57*350/460 =  43   h = 168*350/460 = 127
// so, at 800x600 (popup origin 200,125), {355,168,434,295}.
// ===========================================================================

namespace {

// The oracle numbers, re-derived here rather than read from the header, so
// the test fails if the header's arithmetic changes.
constexpr int kOracleDollX = 166, kOracleDollY = 57;
constexpr int kOracleDollW = 85,  kOracleDollH = 168;
constexpr int kOracleWinW = 428,  kOracleWinH = 460;

} // namespace

TEST(PaperdollT409, TheRectIsTheOracleCharacterViewScaledIntoThePopup) {
    UILayout layout;
    layout.computeLayout(800, 600);
    EXPECT_EQ(InventoryLayout::paperdollRect(layout),
              irr::core::rect<irr::s32>(355, 168, 434, 295));
}

TEST(PaperdollT409, TheRectIsIdenticalRelativeToThePopupOriginAtEveryResolution) {
    for (const auto& rc : kResolutions) {
        UILayout layout;
        layout.computeLayout(rc.w, rc.h);
        const auto& popup = layout.centerPopup;
        const auto doll = InventoryLayout::paperdollRect(layout);

        const int expX = kOracleDollX * popup.getWidth() / kOracleWinW;
        const int expY = kOracleDollY * popup.getHeight() / kOracleWinH;
        const int expW = kOracleDollW * popup.getWidth() / kOracleWinW;
        const int expH = kOracleDollH * popup.getHeight() / kOracleWinH;

        EXPECT_EQ(doll.UpperLeftCorner.X - popup.UpperLeftCorner.X, expX)
            << rc.w << "x" << rc.h;
        EXPECT_EQ(doll.UpperLeftCorner.Y - popup.UpperLeftCorner.Y, expY)
            << rc.w << "x" << rc.h;
        EXPECT_EQ(doll.getWidth(), expW) << rc.w << "x" << rc.h;
        EXPECT_EQ(doll.getHeight(), expH) << rc.w << "x" << rc.h;
    }
}

// The whole point of B219: the doll and the purse must not share a pixel.
TEST(PaperdollT409, TheRectIsDisjointFromEveryCoinRowAtEveryResolution) {
    for (const auto& rc : kResolutions) {
        UILayout layout;
        layout.computeLayout(rc.w, rc.h);
        const auto doll = InventoryLayout::paperdollRect(layout);

        for (int d = 0; d < InventoryLayout::COIN_COUNT; ++d) {
            const auto row = InventoryLayout::coinRect(layout, d);
            const bool overlap = doll.UpperLeftCorner.X < row.LowerRightCorner.X &&
                                 doll.LowerRightCorner.X > row.UpperLeftCorner.X &&
                                 doll.UpperLeftCorner.Y < row.LowerRightCorner.Y &&
                                 doll.LowerRightCorner.Y > row.UpperLeftCorner.Y;
            EXPECT_FALSE(overlap)
                << rc.w << "x" << rc.h << ": paperdoll {"
                << doll.UpperLeftCorner.X << "," << doll.UpperLeftCorner.Y << ","
                << doll.LowerRightCorner.X << "," << doll.LowerRightCorner.Y
                << "} overlaps coin row " << d << " {"
                << row.UpperLeftCorner.X << "," << row.UpperLeftCorner.Y << ","
                << row.LowerRightCorner.X << "," << row.LowerRightCorner.Y << "}";

            const auto decal = InventoryLayout::coinDecalRect(layout, d);
            const bool decalOverlap = doll.UpperLeftCorner.X < decal.LowerRightCorner.X &&
                                      doll.LowerRightCorner.X > decal.UpperLeftCorner.X &&
                                      doll.UpperLeftCorner.Y < decal.LowerRightCorner.Y &&
                                      doll.LowerRightCorner.Y > decal.UpperLeftCorner.Y;
            EXPECT_FALSE(decalOverlap)
                << rc.w << "x" << rc.h << ": paperdoll overlaps coin decal " << d;
        }
    }
}

// The render target the doll is drawn into must carry the rect's own
// proportion, or the model is squashed on its way to the screen. Before
// T409 the RTT was a fixed 304x256 (aspect 1.19) matching the old wide
// rect; the oracle's view is a TALL box (aspect 0.62 once mapped).
TEST(PaperdollT409, TheRenderTargetCarriesTheRectsAspect) {
    UILayout layout;
    layout.computeLayout(800, 600);
    const auto doll = InventoryLayout::paperdollRect(layout);
    const auto rt = InventoryLayout::paperdollRenderTargetSize();

    ASSERT_GT(rt.Width, 0u);
    ASSERT_GT(rt.Height, 0u);
    // Same aspect to within a percent, and at least the drawn size so the
    // blit never upscales.
    const double rtAspect = static_cast<double>(rt.Width) / rt.Height;
    const double rectAspect = static_cast<double>(doll.getWidth()) / doll.getHeight();
    EXPECT_NEAR(rtAspect, rectAspect, 0.01);
    EXPECT_GE(static_cast<int>(rt.Width), doll.getWidth());
    EXPECT_GE(static_cast<int>(rt.Height), doll.getHeight());
}

// ── The health-percent label (T416, board B316) ─────────────────────────────
//
// Found by eye in T416's panel captures (review/T416/crops/group_3way.png):
// every group member row read "100%%". Three panels -- target info, each
// group member row, the pet panel -- formatted the label as
// `fmt::format("{}%%", pct)`, and fmt is not printf: a literal `%` is copied
// through verbatim, so the doubled sign printed. `%%` in a LOG_INFO in
// console_bridge.cpp's TargetChanged arm was the same bug in the same shape.
//
// percentText() is now the one body all three panels call. Pre-fix this test
// fails with "100%%".
TEST(StaticPanelPercentLabelT416, PercentTextPrintsExactlyOneSign) {
    EXPECT_EQ(EQT::Graphics::percentText(100), "100%");
    EXPECT_EQ(EQT::Graphics::percentText(0), "0%");
    EXPECT_EQ(EQT::Graphics::percentText(63), "63%");
}

TEST(StaticPanelPercentLabelT416, PercentTextNeverDoublesTheSign) {
    for (int p = 0; p <= 255; ++p) {
        const std::string s = EQT::Graphics::percentText(static_cast<uint8_t>(p));
        EXPECT_EQ(s.find("%%"), std::string::npos)
            << "percentText(" << p << ") = '" << s << "'";
        EXPECT_EQ(s.back(), '%') << "percentText(" << p << ") = '" << s << "'";
    }
}
