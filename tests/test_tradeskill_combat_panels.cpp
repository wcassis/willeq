// T415 (board B321 + B320): the tradeskill container window and the combat
// abilities window.
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
//   Leg B (B320) -- SkillActivationFeedbackData::cooldownMs had no panel to
//   be drawn in. CAW_CombatEffectTimeRemainingGauge (EQType 26) is that
//   panel's gauge; the eight CAW_Button1..8 rows come from the skills the
//   character actually has.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/tradeskill_layout.h"
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/combat_ability_layout.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/inventory/inventory_constants.h"
#include "client/skill/skill_constants.h"
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
    // B1078's single 73% factor is still height-limited (middleBand height
    // 384); B1077's 48px gem column moves its right edge 16px left and the
    // centre-slot/window origin 8px left: {256,48,548,431} at 800x600.
    struct Case { int i; int x0, y0, x1, y1; };
    const Case cases[] = {
        {0, 459, 135, 489, 165},
        {1, 490, 135, 520, 165},
        {2, 459, 166, 489, 196},
        {8, 459, 258, 489, 288},
        {9, 490, 258, 520, 288},
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
    EXPECT_EQ(rectStr(title), "{432,99,545,113}");

    const auto combine = TradeskillLayout::combineRect(l);
    EXPECT_EQ(rectStr(combine), "{445,291,532,312}");

    const auto close = TradeskillLayout::closeRect(l);
    EXPECT_EQ(rectStr(close), "{445,413,532,429}");
}

// Every rect lies inside the window and the two buttons do not collide with
// the slot grid -- the T57/T60 containment invariant, at five resolutions.
TEST(TradeskillLayoutT415, EverythingFitsInsideTheWindow) {
    const struct { int w, h; } resolutions[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    for (const auto& rc : resolutions) {
        UILayout l;
        l.computeLayout(rc.w, rc.h);
        // B1078 (GitLab #398): the frame is the tradeskill's own window,
        // not the centre slot it sits in.
        const auto& p = TradeskillLayout::windowRect(l);

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

// ===========================================================================
// Leg B -- B320, the combat abilities window
// ===========================================================================

// The gauge. cooldownMs is the field B320 named; it had nowhere to go.
TEST(CombatAbilityWindowT415, SkillFeedbackDrivesTheGauge) {
    CombatAbilityPanelState state;
    EXPECT_FALSE(state.hasEffect);
    EXPECT_EQ(state.effectLabelText(), "No Effect");  // the oracle's <Text>
    EXPECT_FLOAT_EQ(state.gaugeProgress(), 0.0f);

    eqt::state::SkillActivationFeedbackData d;
    d.skillId = 10;  // Bash
    d.success = true;
    d.cooldownMs = 8000;
    applyCombatAbilityFeedback(state, d);

    EXPECT_TRUE(state.hasEffect);
    EXPECT_EQ(state.effectSkillId, 10);
    EXPECT_EQ(state.cooldownMs, 8000u);
    EXPECT_EQ(state.effectLabelText(), "Bash");
    // freshly started: the remaining-time gauge is (near) full
    EXPECT_GT(state.gaugeProgress(), 0.9f);
    EXPECT_LE(state.gaugeProgress(), 1.0f);
}

// A failed activation and a zero cooldown are not effects.
TEST(CombatAbilityWindowT415, NoGaugeWithoutACooldown) {
    CombatAbilityPanelState state;

    eqt::state::SkillActivationFeedbackData fail;
    fail.skillId = 10;
    fail.success = false;
    fail.cooldownMs = 8000;
    applyCombatAbilityFeedback(state, fail);
    EXPECT_FALSE(state.hasEffect);

    eqt::state::SkillActivationFeedbackData nocd;
    nocd.skillId = 30;
    nocd.success = true;
    nocd.cooldownMs = 0;
    applyCombatAbilityFeedback(state, nocd);
    EXPECT_FALSE(state.hasEffect);
}

// The ability rows: the character's own ACTIVATABLE COMBAT skills, in skill
// id order, capped at the oracle's eight buttons. A skill at value 0 is one
// the character does not have.
TEST(CombatAbilityWindowT415, AbilityRowsComeFromTheCharactersSkills) {
    CombatAbilityPanelState state;
    std::vector<SkillDisplayItem> skills;
    auto add = [&skills](uint8_t id, const char* name, uint32_t v) {
        SkillDisplayItem s;
        s.skillId = id;
        s.name = name;
        s.value = v;
        s.maxValue = 200;
        skills.push_back(s);
    };
    add(1, "1H Slash", 100);      // Weapon, not activatable
    add(10, "Bash", 55);          // Combat + activatable
    add(15, "Defense", 120);      // Defense, not activatable
    add(27, "Forage", 40);        // activatable but Utility, not Combat
    add(30, "Kick", 61);          // Combat + activatable
    add(73, "Taunt", 12);         // Combat + activatable
    add(8, "Backstab", 0);        // Combat + activatable but NOT LEARNED

    rebuildCombatAbilities(state, skills);

    ASSERT_EQ(state.abilities.size(), 3u);
    EXPECT_EQ(state.abilities[0].skillId, 10);
    EXPECT_EQ(state.abilities[0].name, "Bash");
    EXPECT_EQ(state.abilities[1].skillId, 30);
    EXPECT_EQ(state.abilities[2].skillId, 73);
}

TEST(CombatAbilityWindowT415, AbilityRowsAreCappedAtTheOraclesEightButtons) {
    CombatAbilityPanelState state;
    std::vector<SkillDisplayItem> skills;
    // every activatable Combat skill in skill_constants.h
    const uint8_t ids[] = {6, 8, 10, 16, 21, 23, 26, 30, 38, 52, 71, 72, 73};
    for (uint8_t id : ids) {
        SkillDisplayItem s;
        s.skillId = id;
        s.name = EQ::getSkillName(id);
        s.value = 50;
        s.maxValue = 200;
        skills.push_back(s);
    }
    rebuildCombatAbilities(state, skills);
    EXPECT_EQ(static_cast<int>(state.abilities.size()),
              CombatAbilityPanelState::BUTTON_COUNT);
}

// THE PLACEMENT IS THE ORACLE'S -- EQUI_CombatAbilityWnd.xml, verbatim, at
// the window's own 174x150 (Style_Sizable false), so no scaling at all.
//
//   <Screen item="CombatAbilityWnd"> Size 174x150
//   <Gauge item="CAW_CombatEffectTimeRemainingGauge"> (11,30) 116x10 EQType 26
//   <Button item="CAW_CombatEffectButton"> (5,4) 20x20
//   <Label  item="CAW_CombatEffectLabel">  (28,5) 129x20
//   <Button item="CAW_Button1..8"> 40x40 at (2|42|82|122, 45) and (.., 85)
TEST(CombatAbilityLayoutT415, RectsAreTheOraclesVerbatim) {
    const UILayout l = layout800();

    // The CombatAbilityWnd is right-aligned to middleBand. B1077's wider
    // gem window shifts that band's right edge from 760 to 744 (-16px);
    // B1078 keeps the 174x150 oracle window unscaled, so every X moves -16.

    const auto win = CombatAbilityLayout::windowRect(l);
    EXPECT_EQ(win.getWidth(), CombatAbilityLayout::ORACLE_WINDOW_W);
    EXPECT_EQ(win.getHeight(), CombatAbilityLayout::ORACLE_WINDOW_H);
    EXPECT_EQ(rectStr(win), "{570,112,744,262}");

    EXPECT_EQ(rectStr(CombatAbilityLayout::effectIconRect(l)), "{575,136,595,156}");
    EXPECT_EQ(rectStr(CombatAbilityLayout::effectLabelRect(l)), "{598,137,727,157}");
    EXPECT_EQ(rectStr(CombatAbilityLayout::gaugeRect(l)), "{581,162,697,172}");
    EXPECT_EQ(rectStr(CombatAbilityLayout::skillSelectRect(l)), "{694,160,731,174}");

    EXPECT_EQ(rectStr(CombatAbilityLayout::buttonRect(l, 0)), "{572,177,612,217}");
    EXPECT_EQ(rectStr(CombatAbilityLayout::buttonRect(l, 3)), "{692,177,732,217}");
    EXPECT_EQ(rectStr(CombatAbilityLayout::buttonRect(l, 4)), "{572,217,612,257}");
    EXPECT_EQ(rectStr(CombatAbilityLayout::buttonRect(l, 7)), "{692,217,732,257}");

    EXPECT_EQ(CombatAbilityLayout::buttonRect(l, -1).getWidth(), 0);
    EXPECT_EQ(CombatAbilityLayout::buttonRect(l, 8).getWidth(), 0);
}

// The window is anchored, not absolute: at every resolution it stays wholly
// on screen, keeps the oracle's size, and does not land on the target panel
// above it or the spell gems below it.
TEST(CombatAbilityLayoutT415, TheWindowStaysOnScreenAndClearOfItsNeighbours) {
    const struct { int w, h; } resolutions[] = {
        {640, 480}, {800, 600}, {1024, 768}, {1280, 1024}, {1920, 1080}};
    for (const auto& rc : resolutions) {
        UILayout l;
        l.computeLayout(rc.w, rc.h);
        const auto win = CombatAbilityLayout::windowRect(l);

        EXPECT_EQ(win.getWidth(), CombatAbilityLayout::ORACLE_WINDOW_W);
        EXPECT_EQ(win.getHeight(), CombatAbilityLayout::ORACLE_WINDOW_H);
        EXPECT_GE(win.UpperLeftCorner.X, 0) << rc.w << "x" << rc.h;
        EXPECT_GE(win.UpperLeftCorner.Y, 0) << rc.w << "x" << rc.h;
        EXPECT_LE(win.LowerRightCorner.X, rc.w) << rc.w << "x" << rc.h;
        EXPECT_LE(win.LowerRightCorner.Y, rc.h) << rc.w << "x" << rc.h;

        EXPECT_FALSE(win.isRectCollided(l.targetInfo))
            << rc.w << "x" << rc.h << ": overlaps target info";
        EXPECT_FALSE(win.isRectCollided(l.spellGems))
            << rc.w << "x" << rc.h << ": overlaps spell gems";

        for (int i = 0; i < CombatAbilityPanelState::BUTTON_COUNT; ++i) {
            const auto b = CombatAbilityLayout::buttonRect(l, i);
            EXPECT_GE(b.UpperLeftCorner.X, win.UpperLeftCorner.X) << rectStr(b);
            EXPECT_GE(b.UpperLeftCorner.Y, win.UpperLeftCorner.Y) << rectStr(b);
            EXPECT_LE(b.LowerRightCorner.X, win.LowerRightCorner.X) << rectStr(b);
            EXPECT_LE(b.LowerRightCorner.Y, win.LowerRightCorner.Y) << rectStr(b);
        }
        const auto g = CombatAbilityLayout::gaugeRect(l);
        EXPECT_LE(g.LowerRightCorner.Y,
                  CombatAbilityLayout::buttonRect(l, 0).UpperLeftCorner.Y)
            << "the gauge must sit above the button grid";
    }
}

// A closed window consumes nothing; an open one hit-tests only rows that
// exist.
TEST(CombatAbilityWindowT415, ButtonHitTests) {
    const UILayout l = layout800();
    CombatAbilityPanelState state;
    std::vector<SkillDisplayItem> skills;
    for (uint8_t id : {uint8_t(10), uint8_t(30)}) {
        SkillDisplayItem s;
        s.skillId = id;
        s.name = EQ::getSkillName(id);
        s.value = 50;
        s.maxValue = 200;
        skills.push_back(s);
    }
    rebuildCombatAbilities(state, skills);
    ASSERT_EQ(state.abilities.size(), 2u);

    const auto b0 = CombatAbilityLayout::buttonRect(l, 0);
    const int x = b0.UpperLeftCorner.X + 4;
    const int y = b0.UpperLeftCorner.Y + 4;

    EXPECT_EQ(hitTestCombatAbility(l, state, x, y), -1)
        << "a closed combat abilities window must not consume clicks";

    state.isOpen = true;
    EXPECT_EQ(hitTestCombatAbility(l, state, x, y), 0);

    const auto b2 = CombatAbilityLayout::buttonRect(l, 2);
    EXPECT_EQ(hitTestCombatAbility(l, state, b2.UpperLeftCorner.X + 4,
                                   b2.UpperLeftCorner.Y + 4), -1)
        << "an empty button row is not clickable";

    EXPECT_EQ(hitTestCombatAbility(l, state, 0, 0), -1);
}

// The oracle's own default: MEASURED at Options > Keys > UI,
// review/T377/oracle/T377_oracle_options_keys_UI_page3.png --
// "Toggle Combat Ability Window   Alt C", Alternate column empty.
TEST(CombatAbilityWindowT415, DefaultIsClosedAndTheToggleFlips) {
    CombatAbilityPanelState state;
    EXPECT_FALSE(state.isOpen);
    EXPECT_TRUE(combatAbilityWindowIsVisible(state) == false);
    state.isOpen = true;
    EXPECT_TRUE(combatAbilityWindowIsVisible(state));
}
