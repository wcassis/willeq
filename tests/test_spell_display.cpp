// T425 Leg A (board B298, spell half) -- the spell display window.
//
// `SpellDatabase` fills the whole spells_us.txt row into `SpellData` and
// fable drew NONE of it: a gem showed an icon, the spellbook showed a name,
// and mana cost / cast time / recast / duration / range / the sixteen class
// levels had no home anywhere in the client.
//
// THE ORACLE HAS NO SECOND WINDOW. There is no EQUI_SpellDisplay.xml and no
// SpellDisplay screen in /mnt/projects/EverQuestP1999/uifiles/default at all
// -- the official client shows a spell in the SAME ItemDisplayWindow it shows
// an item in, whose icon button decals the SPELL sheet
// (<NormalDecal>BuffIcons</NormalDecal>). So this unit adds no panel, no
// layout, no event and no scroll target: it composes lines for T421's window.
//
// Three layers, all device-free and server-free (the T421 shape):
//   1. BuildSpellDisplayText() -- the composer, over a real SpellData. Every
//      line is a format string quoted from the official client's own
//      eqstr_us.txt; the test asserts the FORMAT, so a reworded line fails.
//   2. The oracle's OMISSIONS -- the fields with no eqstr line stay hidden.
//   3. StaticUIInput -- the real intent producer for the real click, and the
//      proof that a PLAIN click still casts and still memorises.

#include <gtest/gtest.h>

#include "eqstr_test_db.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/item_display_layout.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_display_text.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace EQT::Graphics;
using eqt::spell::BuildSpellDisplayText;

namespace {

// T429 (board B390): the composers read their formats out of the shipped
// eqstr table now, so every case below drives the REAL strings.
const EQT::StringDatabase& strings() { return eqt_test::ShippedStrings(); }


UILayout layout800() {
    UILayout l;
    l.computeLayout(800, 600);
    return l;
}

bool hasLine(const std::vector<std::string>& lines, const std::string& want) {
    return std::find(lines.begin(), lines.end(), want) != lines.end();
}

bool anyLineContains(const std::vector<std::string>& lines, const std::string& frag) {
    for (const auto& l : lines) {
        if (l.find(frag) != std::string::npos) return true;
    }
    return false;
}

std::string dump(const std::vector<std::string>& lines) {
    std::string s;
    for (const auto& l : lines) { s += "\n    |"; s += l; s += "|"; }
    return s;
}

// PlayerClass::Cleric is class id 2, so class_levels index 1.
constexpr uint32_t kClericClassId = 2;

// A spell carrying every field the oracle HAS a line for, plus the ones from
// B298's spell census that it has none for.
EQ::SpellData loadedSpell() {
    EQ::SpellData s;
    s.id = 15;
    s.name = "Word of Health";
    s.mana_cost = 200;
    s.cast_time_ms = 4500;
    s.recast_time_ms = 9000;
    s.recovery_time_ms = 2500;
    s.duration_formula = static_cast<uint8_t>(EQ::DurationFormula::Fixed50);
    s.range = 100.0f;
    s.casting_skill = EQ::CastingSkill::Alteration;
    s.spell_icon = 331;
    for (auto& lvl : s.class_levels) lvl = 255;
    s.class_levels[kClericClassId - 1] = 44;

    // B298's spell census -- parsed, and with no oracle line to print on.
    s.player_tag = "PLAYER_1";
    s.teleport_zone = "qeynos";
    s.spell_fades = "Your health returns to normal.";
    s.resist_diff = -150;
    s.zone_type = 255;
    s.environment_type = 3;
    s.time_of_day = 1;
    s.aoe_duration = 5;
    s.push_up = 2.5f;
    s.allow_rest = true;
    s.is_dispellable = false;
    s.no_partial_resist = true;
    s.endurance_cost = 40;
    s.resist_type = EQ::ResistType::Magic;
    s.reagent_id[0] = 13073;
    s.reagent_count[0] = 1;
    return s;
}

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

}  // namespace

// ===================================================================
// Layer 1 -- the composer. One case per oracle line.
// ===================================================================

// eqstr 3006 "Mana Cost: %1", 3107 "Cast Time: %1", 9234 "Recast Delay: %1",
// 6730 "Range = %1", 3105 "Skill: %1".
TEST(SpellDisplayT425, TheCoreLinesAreTheOraclesOwnFormats) {
    const auto lines = BuildSpellDisplayText(strings(), loadedSpell(), kClericClassId, 50);

    EXPECT_TRUE(hasLine(lines, "Mana Cost: 200")) << dump(lines);      // 3006
    EXPECT_TRUE(hasLine(lines, "Cast Time: 4.5")) << dump(lines);      // 3107
    EXPECT_TRUE(hasLine(lines, "Recast Delay: 9.0")) << dump(lines);   // 9234
    EXPECT_TRUE(hasLine(lines, "Range = 100")) << dump(lines);         // 6730
    EXPECT_TRUE(hasLine(lines, "Skill: Alteration")) << dump(lines);   // 3105
}

// eqstr 3102 "Spell Duration: %1." / 3103 "Spell Duration: Permanent."
TEST(SpellDisplayT425, DurationTakesTheOraclesTwoForms) {
    EXPECT_TRUE(hasLine(BuildSpellDisplayText(strings(), loadedSpell(), kClericClassId, 50),
                        "Spell Duration: 50."))                        // 3102
        << "Fixed50 at level 50";

    EQ::SpellData permanent = loadedSpell();
    permanent.duration_formula = static_cast<uint8_t>(EQ::DurationFormula::Permanent);
    permanent.duration_cap = 0;
    EXPECT_TRUE(hasLine(BuildSpellDisplayText(strings(), permanent, kClericClassId, 50),
                        "Spell Duration: Permanent."));                // 3103

    EQ::SpellData instantOnly = loadedSpell();
    instantOnly.duration_formula = 0;
    instantOnly.duration_cap = 0;
    EXPECT_FALSE(anyLineContains(BuildSpellDisplayText(strings(), instantOnly, kClericClassId, 50),
                                 "Spell Duration"))
        << "a spell with no duration prints no duration line, it does not print 0";
}

// eqstr 3004 "Level Needed: %1" / 3005 "Level Needed: CANNOT USE"
TEST(SpellDisplayT425, LevelNeededReadsThePlayersOwnClassColumn) {
    const auto cleric = BuildSpellDisplayText(strings(), loadedSpell(), kClericClassId, 50);
    EXPECT_TRUE(hasLine(cleric, "Level Needed: 44")) << dump(cleric);   // 3004

    // Class 1 (Warrior) is 255 in the fixture -- the oracle's own wording.
    const auto warrior = BuildSpellDisplayText(strings(), loadedSpell(), 1, 50);
    EXPECT_TRUE(hasLine(warrior, "Level Needed: CANNOT USE")) << dump(warrior);  // 3005

    // An unknown class prints neither, rather than guessing a column.
    const auto unknown = BuildSpellDisplayText(strings(), loadedSpell(), 0, 50);
    EXPECT_FALSE(anyLineContains(unknown, "Level Needed"));
}

// The ascending-eqstr-id order BuildItemDisplayText() uses, applied here.
TEST(SpellDisplayT425, LinesComeOutInAscendingStringIdOrder) {
    const auto lines = BuildSpellDisplayText(strings(), loadedSpell(), kClericClassId, 50);
    const std::vector<std::string> want{
        "Level Needed: 44",        // 3004
        "Mana Cost: 200",          // 3006
        "Spell Duration: 50.",     // 3102
        "Skill: Alteration",       // 3105
        "Cast Time: 4.5",          // 3107
        "Range = 100",             // 6730
        "Recast Delay: 9.0",       // 9234
    };
    EXPECT_EQ(lines, want) << dump(lines);
}

// ===================================================================
// Layer 2 -- the oracle's OMISSIONS. Each of these was measured by
// grepping eqstr_us.txt; none of them may appear.
// ===================================================================

TEST(SpellDisplayT425, TheOraclesOmissionsStayOmitted) {
    const auto lines = BuildSpellDisplayText(strings(), loadedSpell(), kClericClassId, 50);

    // B298's spell census: no format string exists for any of these.
    EXPECT_FALSE(anyLineContains(lines, "PLAYER_1"))   << "player_tag has no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "qeynos"))     << "teleport_zone has no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "Resist"))     << "resist_type/resist_diff have no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "-150"))       << "resist_diff has no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "Indoor"))     << "zone_type has no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "Outdoor"))    << "zone_type has no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "Target Type"))<< "target_type has no eqstr line";
    EXPECT_FALSE(anyLineContains(lines, "Endurance"))  << "5809 is the ITEM endurance stat";
    EXPECT_FALSE(anyLineContains(lines, "Reagent"))
        << "6732 exists but wants an item NAME, and the client has no item-name-by-id source";

    // spell_fades is NOT a window line -- it is the buff-fade chat message,
    // wired at BuffManager's fade callback in T425.
    EXPECT_FALSE(anyLineContains(lines, "returns to normal"));

    // 3101 "Spell:" is the ITEM window's label for an item's spell effect;
    // the spell's own name goes on the Style_Titlebar row, as the item's does.
    EXPECT_FALSE(anyLineContains(lines, "Word of Health"));
}

TEST(SpellDisplayT425, ZeroFieldsAreOmittedNotPrintedAsZero) {
    EQ::SpellData bare;
    bare.id = 3;
    bare.name = "Cure Poison";
    bare.casting_skill = EQ::CastingSkill::Alteration;
    for (auto& lvl : bare.class_levels) lvl = 255;
    bare.class_levels[kClericClassId - 1] = 1;

    const auto lines = BuildSpellDisplayText(strings(), bare, kClericClassId, 10);
    EXPECT_FALSE(anyLineContains(lines, "Mana Cost")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "Cast Time")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "Recast Delay")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "Range")) << dump(lines);
    // The skill line always draws -- every spell has a casting skill.
    EXPECT_TRUE(hasLine(lines, "Skill: Alteration")) << dump(lines);
}

// ===================================================================
// Layer 3 -- the REAL producer, and the clicks it must NOT break.
// ===================================================================

TEST(SpellDisplayT425, ShiftClickingAGemAsksForTheSpellDisplay) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);

    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inv;
    SpellbookPopupState book;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;

    gems.gems[2].spellId = 15;
    gems.gems[2].spellName = "Word of Health";

    const int gx = l.spellGems.UpperLeftCorner.X + 3;
    const int gy = l.spellGems.UpperLeftCorner.Y
                 + 2 * (UILayout::SLOT_SIZE + UILayout::MARGIN) + 3;

    input.processInput(gx, gy, /*leftClicked=*/true, l, hotbar, gems, inv, book,
                       skills, buffs, loot, /*shift=*/true);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* req = std::get_if<eqt::events::SpellDisplayRequestIntent>(&intents[0]);
    ASSERT_NE(req, nullptr) << "shift+click on a gem produced no SpellDisplayRequestIntent";
    EXPECT_EQ(req->spellId, 15u);

    // An EMPTY gem describes nothing.
    const int emptyY = l.spellGems.UpperLeftCorner.Y
                     + 5 * (UILayout::SLOT_SIZE + UILayout::MARGIN) + 3;
    input.processInput(gx, emptyY, /*leftClicked=*/true, l, hotbar, gems, inv, book,
                       skills, buffs, loot, /*shift=*/true);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

// The gesture must not steal the two clicks that already had a job.
TEST(SpellDisplayT425, APlainGemClickStillCasts) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);

    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inv;
    SpellbookPopupState book;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;

    gems.gems[2].spellId = 15;

    const int gx = l.spellGems.UpperLeftCorner.X + 3;
    const int gy = l.spellGems.UpperLeftCorner.Y
                 + 2 * (UILayout::SLOT_SIZE + UILayout::MARGIN) + 3;

    input.processInput(gx, gy, /*leftClicked=*/true, l, hotbar, gems, inv, book,
                       skills, buffs, loot, /*shift=*/false);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::CastSpellIntent>(&intents[0]), nullptr)
        << "a plain gem click must still cast (T272)";
}

TEST(SpellDisplayT425, ShiftClickingASpellbookRowAsksForTheDisplayAndPlainStillMemorises) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);

    HotbarPanelState hotbar;
    SpellGemPanelState gems;
    InventoryPanelState inv;
    SpellbookPopupState book;
    SkillsPopupState skills;
    BuffBarState buffs;
    LootPanelState loot;

    book.isOpen = true;
    SpellbookDisplayEntry e;
    e.slot = 0;
    e.spellId = 15;
    e.name = "Word of Health";
    book.spells.push_back(e);

    const int rx = l.centerPopup.UpperLeftCorner.X + 10;
    const int ry = l.centerPopup.UpperLeftCorner.Y + 24 + 2;

    input.processInput(rx, ry, /*leftClicked=*/true, l, hotbar, gems, inv, book,
                       skills, buffs, loot, /*shift=*/true);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* req = std::get_if<eqt::events::SpellDisplayRequestIntent>(&intents[0]);
    ASSERT_NE(req, nullptr) << "shift+click in the spellbook produced no display request";
    EXPECT_EQ(req->spellId, 15u);

    input.processInput(rx, ry, /*leftClicked=*/true, l, hotbar, gems, inv, book,
                       skills, buffs, loot, /*shift=*/false);
    auto plain = bridge.drainIntents();
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_NE(std::get_if<eqt::events::MemorizeSpellIntent>(&plain[0]), nullptr)
        << "a plain spellbook click must still memorise (T272)";
}

// The window is T421's. A spell drives the SAME applier, panel and rects --
// there is no second window, because the oracle has none.
TEST(SpellDisplayT425, TheSpellReusesTheOraclesOneDisplayWindow) {
    const UILayout l = layout800();
    ItemDisplayPanelState state;

    eqt::state::ItemDisplayShownData d;
    d.itemId = 15;
    d.name = "Word of Health";
    d.iconId = 331;
    d.lines = BuildSpellDisplayText(strings(), loadedSpell(), kClericClassId, 50);
    applyItemDisplayShown(state, d);

    EXPECT_TRUE(state.isOpen);
    EXPECT_EQ(state.name, "Word of Health") << "-> the Style_Titlebar row";
    EXPECT_EQ(state.iconId, 331u) << "-> IDW_IconButton's <NormalDecal>BuffIcons</NormalDecal>";
    EXPECT_EQ(state.lines.size(), 7u);

    // EQUI_ItemDisplay.xml's own geometry, unchanged by this unit.
    const auto w = ItemDisplayLayout::windowRect(l);
    EXPECT_EQ(w.getWidth(), ItemDisplayLayout::WINDOW_W);
    EXPECT_EQ(w.getHeight(), ItemDisplayLayout::WINDOW_H);
    EXPECT_TRUE(itemDisplayContains(l, state,
                                    w.UpperLeftCorner.X + 4, w.UpperLeftCorner.Y + 4));
}
