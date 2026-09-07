// T421 Leg A (board B298, item half) -- the item display window.
//
// `TitaniumItemParser` fills a hundred-odd ItemInstance fields off the wire
// and fable drew NONE of them: there was no item display window in the tree
// at all. B298's item census -- accuracy, damageShield, dotShield,
// spellShield, strikethrough, stunResist, shielding, enduranceRegen,
// hpRegen, manaRegen, delay, recLevel, maxCharges, lore -- is the list of
// stats that had no home.
//
// Three layers, all device-free (the T415/T419 shape):
//   1. BuildItemDisplayText() -- the composer, over a real ItemInstance.
//      Every line it emits is a format string quoted from the official
//      client's own eqstr_us.txt; the test asserts the FORMAT, not just the
//      number, so a reworded line is a failure.
//   2. ItemDisplayLayout -- the rects, against EQUI_ItemDisplay.xml.
//   3. StaticUIInput -- the real intent producer for the real click.

#include <gtest/gtest.h>

#include "eqstr_test_db.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/item_display_layout.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/inventory/item_display_text.h"
#include "client/inventory/item_instance.h"
#include "client/inventory/inventory_manager.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <algorithm>
#include <string>
#include <vector>

using namespace EQT::Graphics;
using eqt::inventory::BuildItemDisplayText;
using eqt::inventory::ItemInstance;

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

// An item carrying every field B298 named, plus the neighbours the same
// lines print.
ItemInstance loadedItem() {
    ItemInstance it;
    it.itemId = 4231;
    it.name = "Bladed Thulian Claws";
    it.lore = "This weapon hums faintly.";
    it.icon = 640;
    it.magic = true;
    it.loreItem = true;
    it.noDrop = true;
    it.slots = eqt::inventory::EQUIPSLOT_PRIMARY | eqt::inventory::EQUIPSLOT_SECONDARY;
    it.classes = 0;
    it.races = 0;
    it.weight = 4.5f;
    it.size = eqt::inventory::SIZE_MEDIUM;

    it.ac = 12;
    it.damage = 19;
    it.delay = 21;          // B298
    it.skillType = 1;       // 1H Slash
    it.range = 0;

    it.str = 5;  it.sta = 4;  it.agi = 3;  it.dex = 2;
    it.wis = 1;  it.int_ = 6; it.cha = 7;
    it.hp = 35;  it.mana = 40;
    it.fireResist = 8; it.coldResist = 9; it.magicResist = 10;
    it.diseaseResist = 11; it.poisonResist = 12;

    it.endurance = 13;
    it.attack = 14;
    it.avoidance = 15;
    it.haste = 16;
    it.accuracy = 17;       // B298
    it.damageShield = 18;   // B298
    it.dotShield = 19;      // B298
    it.spellShield = 20;    // B298
    it.strikethrough = 21;  // B298
    it.stunResist = 22;     // B298
    it.shielding = 23;      // B298
    it.enduranceRegen = 24; // B298
    it.hpRegen = 25;        // B298
    it.manaRegen = 26;      // B298

    it.recLevel = 45;       // B298
    it.reqLevel = 40;
    it.maxCharges = 3;      // B298
    it.charges = 2;

    it.clickEffect.effectId = 300;
    it.clickEffect.name = "Shield of Words";
    it.clickEffect.castTime = 2500;
    it.wornEffect.effectId = 301;
    it.wornEffect.name = "Haste";
    it.focusEffect.effectId = 302;
    it.focusEffect.name = "Burning Affliction";
    it.procEffect.effectId = 303;
    it.procEffect.name = "Lifetap Strike";

    // The fields the ORACLE does not show. Set non-zero on purpose: the test
    // below proves they never reach a line.
    // T425 Leg B (board B375): the seven heroic* and bardEffect used to be
    // set here to prove they never reached a line. They are GONE from
    // ItemInstance -- not on Titanium's wire and not in eqstr_us.txt -- so
    // the proof is now the compiler's, and this fixture cannot name them.
    it.currentSlot = 13;
    return it;
}

class CapturingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

}  // namespace

// ===================================================================
// Layer 1 -- the composer. One case per B298 field group.
// ===================================================================

// eqstr 5836/5844/5845/5846/5847/6184/9040..9045 -- twelve of the fourteen
// B298 item fields that have an oracle line live here.
TEST(ItemDisplayT421, TheExtendedStatBlockIsTheOraclesOwnFormats) {
    const auto lines = BuildItemDisplayText(strings(), loadedItem());

    EXPECT_TRUE(hasLine(lines, "Accuracy: 17"))              << dump(lines);  // 9043
    EXPECT_TRUE(hasLine(lines, "Damage Shield: 18"))         << dump(lines);  // 5847
    EXPECT_TRUE(hasLine(lines, "DoT Shielding: 19%"))        << dump(lines);  // 5836
    EXPECT_TRUE(hasLine(lines, "Spell Shield: 20%"))         << dump(lines);  // 9041
    EXPECT_TRUE(hasLine(lines, "Strikethrough: 21%"))        << dump(lines);  // 9045
    EXPECT_TRUE(hasLine(lines, "Stun Resist: 22%"))          << dump(lines);  // 9044
    EXPECT_TRUE(hasLine(lines, "Shielding: 23%"))            << dump(lines);  // 9040
    EXPECT_TRUE(hasLine(lines, "Endurance Regeneration: 24"))<< dump(lines);  // 6184
    EXPECT_TRUE(hasLine(lines, "Regeneration: 25"))          << dump(lines);  // 5845
    EXPECT_TRUE(hasLine(lines, "Mana Regeneration: 26"))     << dump(lines);  // 5846
    // The neighbours the same block prints.
    EXPECT_TRUE(hasLine(lines, "Attack: 14"))                << dump(lines);  // 5844
    EXPECT_TRUE(hasLine(lines, "Avoidance: 15"))             << dump(lines);  // 9042
    EXPECT_TRUE(hasLine(lines, "Haste: 16%"))                << dump(lines);  // 5848
    EXPECT_TRUE(hasLine(lines, "Endurance: 13"))             << dump(lines);  // 5809
}

// eqstr 3008 "Skill: %1   Atk Delay: %2" -- B298's `delay`, with the weapon
// skill name it shares a line with.
TEST(ItemDisplayT421, TheWeaponLineCarriesTheDelay) {
    const auto lines = BuildItemDisplayText(strings(), loadedItem());
    EXPECT_TRUE(hasLine(lines, "Skill: 1H Slash   Atk Delay: 21")) << dump(lines);
    EXPECT_TRUE(hasLine(lines, "DMG: 19")) << dump(lines);   // 12455
    EXPECT_TRUE(hasLine(lines, "AC: 12"))  << dump(lines);   // 3016
}

// eqstr 3028 (recLevel), 3003 (maxCharges) and 3048 (lore) -- the last three
// B298 item fields with an oracle line.
TEST(ItemDisplayT421, RecommendedLevelChargesAndLore) {
    const auto lines = BuildItemDisplayText(strings(), loadedItem());
    EXPECT_TRUE(hasLine(lines, "Recommended level of 45."))          << dump(lines);
    EXPECT_TRUE(hasLine(lines, "Level Needed: 40"))                  << dump(lines);
    EXPECT_TRUE(hasLine(lines, "Charges: 2"))                        << dump(lines);
    EXPECT_TRUE(hasLine(lines, "LORE: This weapon hums faintly."))   << dump(lines);
}

// eqstr 3001 "EXPENDABLE     Charges: %1". T425 Leg B gave `expendable` its
// writer (see ItemDisplayT425ExpendableIsParsed below), so this line is
// reachable now; it was unreachable when T421 wrote it.
TEST(ItemDisplayT421, ExpendableTakesItsOwnChargeLine) {
    ItemInstance it = loadedItem();
    it.expendable = true;
    const auto lines = BuildItemDisplayText(strings(), it);
    EXPECT_TRUE(hasLine(lines, "EXPENDABLE     Charges: 2")) << dump(lines);
    EXPECT_FALSE(hasLine(lines, "Charges: 2")) << "one charge line, not two";
}

// The fields the ORACLE does not show stay hidden. eqstr_us.txt has no
// heroic-stat format string at all, no instrument-modifier item line, and no
// "where the item currently is" line -- `Slot:` (3000) is the EQUIPPABLE
// list, which the same item prints from `slots`.
//
// T425 Leg B: the heroic and instrument assertions are gone because the
// MEMBERS are gone -- deleted from ItemInstance, since neither is on
// Titanium's wire. A deleted member cannot leak, and the compiler enforces
// that far better than a string search did.
TEST(ItemDisplayT421, TheOraclesOmissionsStayOmitted) {
    const auto lines = BuildItemDisplayText(strings(), loadedItem());

    EXPECT_FALSE(anyLineContains(lines, "Heroic")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "Instrument")) << dump(lines);

    // `Slot:` IS printed, and it is the equippable list -- not currentSlot.
    EXPECT_TRUE(hasLine(lines, "Slot: Primary Secondary")) << dump(lines);
}

// ===================================================================
// T425 Leg B (board B375) -- the writer `expendable` never had.
// ===================================================================
//
// `expendable` is not a wire FIELD; it is derived, and eqemu's own
// ItemInstance::IsExpendable() is the definition:
//     Click.Type == item::ItemEffectExpendable (3) || ItemType == ItemTypePotion (21)
// Both inputs ARE on Titanium's wire (fields 135 and 0), which is why this
// member is wired rather than deleted like its eight B375 siblings.
namespace {

std::string serializeItem(std::vector<std::string> fields) {
    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }
    return "1|0|22|0|1|0|0|0|2|0|0\"" + staticData + "\"";
}

std::vector<std::string> baseFields() {
    std::vector<std::string> f(159, "0");
    f[1] = "a glowing potion";
    f[2] = "a glowing potion";
    f[3] = "IT63";
    f[4] = "12345";
    return f;
}

}  // namespace

TEST(ItemDisplayT425, ExpendableIsParsedFromTheClickEffectType) {
    auto f = baseFields();
    f[0] = "11";   // ItemTypeMisc -- not a potion
    f[135] = "3";  // Click.Type = ItemEffectExpendable
    int16_t slot = -1;
    auto item = eqt::inventory::TitaniumItemParser::parseItem(serializeItem(f), slot);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->expendable)
        << "Click.Type 3 is ItemEffectExpendable and must set expendable";
}

TEST(ItemDisplayT425, ExpendableIsParsedFromThePotionItemType) {
    auto f = baseFields();
    f[0] = "21";   // ItemTypePotion
    f[135] = "1";  // Click.Type = ItemEffectClick, i.e. NOT expendable itself
    int16_t slot = -1;
    auto item = eqt::inventory::TitaniumItemParser::parseItem(serializeItem(f), slot);
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->expendable) << "every potion is expendable (eqemu IsExpendable)";
}

TEST(ItemDisplayT425, AnOrdinaryItemIsNotExpendable) {
    auto f = baseFields();
    f[0] = "10";   // ItemTypeArmor
    f[135] = "1";  // ItemEffectClick
    int16_t slot = -1;
    auto item = eqt::inventory::TitaniumItemParser::parseItem(serializeItem(f), slot);
    ASSERT_NE(item, nullptr);
    EXPECT_FALSE(item->expendable);

    // ...and the composer then draws the ordinary charge line, not 3001.
    ItemInstance it = loadedItem();
    it.expendable = false;
    EXPECT_FALSE(anyLineContains(BuildItemDisplayText(strings(), it), "EXPENDABLE"));
}

// A zero-valued field is OMITTED, the way the official window omits it,
// rather than printed as "0".
TEST(ItemDisplayT421, ZeroFieldsAreOmittedNotPrintedAsZero) {
    ItemInstance plain;
    plain.name = "a rusty dagger";
    plain.weight = 1.0f;
    const auto lines = BuildItemDisplayText(strings(), plain);

    for (const char* frag : {"Accuracy", "Damage Shield", "DoT Shielding",
                             "Spell Shield", "Strikethrough", "Stun Resist",
                             "Shielding", "Endurance Regeneration",
                             "Regeneration", "Mana Regeneration", "AC:",
                             "DMG:", "Recommended level", "LORE:", "Charges"}) {
        EXPECT_FALSE(anyLineContains(lines, frag))
            << frag << " printed for an empty item" << dump(lines);
    }
    // What a bare item still says: its weight/size line and its restrictions.
    EXPECT_TRUE(hasLine(lines, "WT: 1.0   Size: MEDIUM")) << dump(lines);  // 3038
    EXPECT_TRUE(hasLine(lines, "Class: ALL")) << dump(lines);              // 3064
    EXPECT_TRUE(hasLine(lines, "Race: ALL"))  << dump(lines);              // 3049
}

// eqstr 3042/3043 -- a container prints its weight reduction and capacity
// instead of a range/size line.
TEST(ItemDisplayT421, AContainerPrintsTheContainerLines) {
    ItemInstance bag;
    bag.name = "Large Sewing Kit";
    bag.weight = 2.0f;
    bag.bagSlots = 10;
    bag.bagWR = 25;
    bag.bagSize = eqt::inventory::SIZE_LARGE;
    const auto lines = BuildItemDisplayText(strings(), bag);
    EXPECT_TRUE(hasLine(lines, "WT: 2.0    Weight Reduction: 25")) << dump(lines);
    EXPECT_TRUE(hasLine(lines, "Capacity: 10    Size Capacity: LARGE")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "Size: LARGE\n")) << dump(lines);
}

// eqstr 3036 + the 3029..3034 qualifiers, and 5530 for the focus.
TEST(ItemDisplayT421, EffectsCarryTheOraclesQualifiers) {
    const auto lines = BuildItemDisplayText(strings(), loadedItem());
    EXPECT_TRUE(hasLine(lines, "Effect: Shield of Words (Casting Time: 2.5)")) << dump(lines);
    EXPECT_TRUE(hasLine(lines, "Effect: Lifetap Strike (Combat)")) << dump(lines);
    EXPECT_TRUE(hasLine(lines, "Effect: Haste (Worn)")) << dump(lines);
    EXPECT_TRUE(hasLine(lines, "Focus Effect: Burning Affliction")) << dump(lines);
}

// ===================================================================
// Layer 2 -- the rects, against EQUI_ItemDisplay.xml
// ===================================================================

TEST(ItemDisplayT421, TheWindowIsTheOraclesOwnLocationAndSize) {
    const UILayout l = layout800();
    const auto w = ItemDisplayLayout::windowRect(l);
    //   <Location><X>0</X><Y>80</Y></Location>  <Size>400x190</Size>
    //   <RelativePosition>false</RelativePosition>  -- screen coordinates
    EXPECT_EQ(w.UpperLeftCorner.X, 0);
    EXPECT_EQ(w.UpperLeftCorner.Y, 80);
    EXPECT_EQ(w.getWidth(), 400);
    EXPECT_EQ(w.getHeight(), 190);
}

TEST(ItemDisplayT421, TheIconButtonAndDescriptionResolveTheXmlAnchors) {
    const UILayout l = layout800();
    const auto client = ItemDisplayLayout::clientRect(l);
    const auto icon = ItemDisplayLayout::iconButtonRect(l);
    const auto decal = ItemDisplayLayout::iconDecalRect(l);
    const auto desc = ItemDisplayLayout::descriptionRect(l);

    // IDW_IconButton: 70/12/26/56 with Left+Right anchored to the RIGHT edge
    // => (330,12)-(374,56), 44x44, decal 40x40 at +2.
    EXPECT_EQ(icon.UpperLeftCorner.X - client.UpperLeftCorner.X, 330);
    EXPECT_EQ(icon.UpperLeftCorner.Y - client.UpperLeftCorner.Y, 12);
    EXPECT_EQ(icon.getWidth(), 44);
    EXPECT_EQ(icon.getHeight(), 44);
    EXPECT_EQ(decal.getWidth(), 40);
    EXPECT_EQ(decal.getHeight(), 40);
    EXPECT_EQ(decal.UpperLeftCorner.X - icon.UpperLeftCorner.X, 2);

    // IDW_ItemDescription: 0/0/0(from right)/3(from bottom).
    EXPECT_EQ(desc.UpperLeftCorner.X, client.UpperLeftCorner.X);
    EXPECT_EQ(desc.UpperLeftCorner.Y, client.UpperLeftCorner.Y);
    EXPECT_EQ(desc.LowerRightCorner.X, client.LowerRightCorner.X);
    EXPECT_EQ(client.LowerRightCorner.Y - desc.LowerRightCorner.Y, 3);

    // Every child is inside the window, and the close box is in the title.
    const auto w = ItemDisplayLayout::windowRect(l);
    for (const auto& r : {icon, decal, desc, ItemDisplayLayout::closeBoxRect(l)}) {
        EXPECT_GE(r.UpperLeftCorner.X, w.UpperLeftCorner.X);
        EXPECT_GE(r.UpperLeftCorner.Y, w.UpperLeftCorner.Y);
        EXPECT_LE(r.LowerRightCorner.X, w.LowerRightCorner.X);
        EXPECT_LE(r.LowerRightCorner.Y, w.LowerRightCorner.Y);
    }
    EXPECT_TRUE(ItemDisplayLayout::titleRect(l).isRectCollided(
        ItemDisplayLayout::closeBoxRect(l)));
}

TEST(ItemDisplayT421, DescriptionRowsTileTheBoxAndAreCounted) {
    const UILayout l = layout800();
    const int fit = ItemDisplayLayout::visibleDescriptionLines(l);
    ASSERT_GT(fit, 0);
    const auto desc = ItemDisplayLayout::descriptionRect(l);
    for (int i = 0; i < fit; ++i) {
        const auto r = ItemDisplayLayout::descriptionLineRect(l, i);
        EXPECT_GE(r.UpperLeftCorner.Y, desc.UpperLeftCorner.Y);
        EXPECT_LE(r.LowerRightCorner.Y, desc.LowerRightCorner.Y)
            << "row " << i << " of " << fit << " falls out of the box";
        if (i > 0) {
            EXPECT_EQ(r.UpperLeftCorner.Y,
                      ItemDisplayLayout::descriptionLineRect(l, i - 1).LowerRightCorner.Y);
        }
    }
}

// ===================================================================
// Layer 3 -- the bridge arm and the real click
// ===================================================================

TEST(ItemDisplayT421, TheBridgeArmOpensTheWindowAndRetargetsIt) {
    ItemDisplayPanelState state;
    EXPECT_FALSE(state.isOpen);

    eqt::state::ItemDisplayShownData d;
    d.itemId = 4231;
    d.name = "Bladed Thulian Claws";
    d.iconId = 640;
    d.lines = {"AC: 12", "DMG: 19"};
    applyItemDisplayShown(state, d);
    EXPECT_TRUE(state.isOpen);
    EXPECT_EQ(state.itemId, 4231u);
    EXPECT_EQ(state.name, "Bladed Thulian Claws");
    EXPECT_EQ(state.iconId, 640u);
    ASSERT_EQ(state.lines.size(), 2u);

    state.scrollOffset = 1;
    eqt::state::ItemDisplayShownData d2;
    d2.itemId = 7;
    d2.name = "a rusty dagger";
    d2.lines = {"WT: 1.0   Size: MEDIUM"};
    applyItemDisplayShown(state, d2);
    EXPECT_EQ(state.itemId, 7u) << "a second request retargets, it does not stack";
    ASSERT_EQ(state.lines.size(), 1u);
    EXPECT_EQ(state.scrollOffset, 0) << "the new item starts at the top";
}

TEST(ItemDisplayT421, AClosedWindowConsumesNothing) {
    const UILayout l = layout800();
    ItemDisplayPanelState state;
    const auto close = ItemDisplayLayout::closeBoxRect(l);
    const int cx = close.UpperLeftCorner.X + 2;
    const int cy = close.UpperLeftCorner.Y + 2;

    EXPECT_FALSE(hitTestItemDisplayCloseBox(l, state, cx, cy));
    EXPECT_FALSE(itemDisplayContains(l, state, cx, cy));

    state.isOpen = true;
    EXPECT_TRUE(hitTestItemDisplayCloseBox(l, state, cx, cy));
    EXPECT_TRUE(itemDisplayContains(l, state, cx, cy));
    EXPECT_FALSE(hitTestItemDisplayCloseBox(
        l, state, ItemDisplayLayout::descriptionRect(l).UpperLeftCorner.X + 4,
        ItemDisplayLayout::descriptionRect(l).UpperLeftCorner.Y + 4));
}

// The REAL producer: StaticUIInput, the real bridge, the real intent queue.
TEST(ItemDisplayT421, ClickingAnOccupiedInventorySlotAsksForTheDisplay) {
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

    inv.activePopup = PopupType::Inventory;
    inv.equipSlots[13].hasItem = true;
    inv.equipSlots[13].itemName = "Bladed Thulian Claws";

    const auto occupied = InventoryLayout::slotRect(l, 13);
    input.processInput(occupied.UpperLeftCorner.X + 3, occupied.UpperLeftCorner.Y + 3,
                       /*leftClicked=*/true, l, hotbar, gems, inv, book, skills,
                       buffs, loot);
    auto intents = bridge.drainIntents();
    ASSERT_EQ(intents.size(), 1u);
    const auto* req = std::get_if<eqt::events::ItemDisplayRequestIntent>(&intents[0]);
    ASSERT_NE(req, nullptr) << "the click did not produce an ItemDisplayRequestIntent";
    EXPECT_EQ(req->slot, 13);

    // An EMPTY slot asks for nothing -- there is no item to describe.
    const auto empty = InventoryLayout::slotRect(l, 14);
    input.processInput(empty.UpperLeftCorner.X + 3, empty.UpperLeftCorner.Y + 3,
                       /*leftClicked=*/true, l, hotbar, gems, inv, book, skills,
                       buffs, loot);
    EXPECT_TRUE(bridge.drainIntents().empty());
}

TEST(ItemDisplayT421, TheCloseBoxClosesTheWindow) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);

    ItemDisplayPanelState state;
    state.isOpen = true;
    state.lines = {"AC: 12"};

    const auto close = ItemDisplayLayout::closeBoxRect(l);
    EXPECT_TRUE(input.processItemDisplayInput(close.UpperLeftCorner.X + 2,
                                              close.UpperLeftCorner.Y + 2,
                                              /*leftClicked=*/false, l, state));
    EXPECT_TRUE(state.closeHovered);
    EXPECT_TRUE(state.isOpen) << "hovering must not close it";

    EXPECT_TRUE(input.processItemDisplayInput(close.UpperLeftCorner.X + 2,
                                              close.UpperLeftCorner.Y + 2,
                                              /*leftClicked=*/true, l, state));
    EXPECT_FALSE(state.isOpen);
}

// `Style_VScroll true` on IDW_ItemDescription is not decoration: a loaded
// item composes more lines than the oracle's 190px window shows. T313's rule
// is that there is ONE scroll router, so the window is a ScrollTarget rather
// than growing a private scroll writer.
TEST(ItemDisplayT421, TheDescriptionScrollsThroughTheOneRouter) {
    UILayout l = layout800();
    CapturingBridge bridge;
    StaticUIInput input;
    input.init(&bridge);

    ChatPanelState chat;
    SkillsPopupState skills;
    LootPanelState loot;
    ItemDisplayPanelState state;

    eqt::state::ItemDisplayShownData d;
    d.name = "Bladed Thulian Claws";
    d.lines = BuildItemDisplayText(strings(), loadedItem());
    applyItemDisplayShown(state, d);

    const int fit = ItemDisplayLayout::visibleDescriptionLines(l);
    ASSERT_GT(static_cast<int>(state.lines.size()), fit)
        << "the fixture item must overflow the box or this test proves nothing";
    ASSERT_GT(state.maxScrollOffset(l), 0);

    // The window is TOPMOST: it is picked even though it is not in centerPopup.
    const auto desc = ItemDisplayLayout::descriptionRect(l);
    const int dx = desc.UpperLeftCorner.X + 8;
    const int dy = desc.UpperLeftCorner.Y + 8;
    EXPECT_EQ(StaticUIInput::scrollTargetAt(dx, dy, l, skills, loot, state),
              ScrollTarget::ItemDisplay);

    // A closed window is not a target, even under the cursor.
    ItemDisplayPanelState closed;
    EXPECT_EQ(StaticUIInput::scrollTargetAt(dx, dy, l, skills, loot, closed),
              ScrollTarget::None);

    // A wheel notch down moves it; the clamps are the panel's.
    EXPECT_TRUE(input.processWheel(dx, dy, -1.0f, l, chat, skills, loot, state));
    EXPECT_EQ(state.scrollOffset, StaticUIInput::WHEEL_LINES);
    EXPECT_TRUE(input.processWheel(dx, dy, -100.0f, l, chat, skills, loot, state));
    EXPECT_EQ(state.scrollOffset, state.maxScrollOffset(l)) << "saturates, never runs off";
    EXPECT_TRUE(input.processWheel(dx, dy, 100.0f, l, chat, skills, loot, state));
    EXPECT_EQ(state.scrollOffset, 0);

    // And the last line is reachable: at max scroll the last entry is drawn.
    state.scrollOffset = state.maxScrollOffset(l);
    const int rows = state.visibleLines(l);
    EXPECT_EQ(state.scrollOffset + rows, static_cast<int>(state.lines.size()))
        << "the bottom of the description is still unreachable";
}
