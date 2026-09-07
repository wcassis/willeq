// T429 Leg A (board B390) -- the display composers take their format strings
// from the SHIPPED STRING TABLE, not from their own source.
//
// WHAT WAS BROKEN. T421's BuildItemDisplayText() and T425's
// BuildSpellDisplayText() carried every line of the official client's item and
// spell windows as a C++ string literal, hand-transcribed out of
// /mnt/projects/EverQuestP1999/eqstr_us.txt with the id in a comment. The whole
// table already ships in data/willeq.db (`eqstrings`, 5,901 rows) and is
// already reachable through EQT::StringDatabase, which chat_component.cpp,
// merchant.cpp and combat_actions.cpp all use. So the composers transcribed by
// hand what one lookup fetches: a localised eqstr_XX.txt could never reach
// them, and a transcription typo was invisible.
//
// Three things are pinned here, and the first two FAIL against literals:
//   1. THE TABLE IS THE SOURCE. A StringDatabase loaded from a deliberately
//      REWORDED eqstr file must change what the composers draw. A composer
//      that owns its text cannot pass this.
//   2. A MISS DRAWS NOTHING. An empty table must yield no lines at all --
//      never a fallback copy of the format, which is the defect B390 names.
//   3. EVERY ID EXISTS in the db actually shipped, so the fetch cannot be
//      silently drawing nothing on the real client.
//
// And one surprise the fetch exposed: eqstr mixes two placeholder conventions.
// 3030/3033 are C PRINTF formats -- "(Casting Time: %2.1f)" -- where the
// client's positional %n engine would render "4.5.1f".

#include <gtest/gtest.h>

#include "eqstr_test_db.h"

#include "client/display_eqstr.h"
#include "client/inventory/item_display_text.h"
#include "client/inventory/item_instance.h"
#include "client/spell/spell_data.h"
#include "client/spell/spell_display_text.h"
#include "client/string_database.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

using eqt::inventory::BuildItemDisplayText;
using eqt::spell::BuildSpellDisplayText;

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

// A StringDatabase in LEGACY (file) mode over `rows`, written to a temp
// eqstr_us.txt. The loader skips two header lines, so they are supplied.
class TempEqStrFile {
public:
    explicit TempEqStrFile(const std::vector<std::pair<uint32_t, std::string>>& rows) {
        path_ = ::testing::TempDir() + "/t429_eqstr_" +
                std::to_string(reinterpret_cast<uintptr_t>(this)) + ".txt";
        std::ofstream f(path_);
        f << "EQST0002\n0 " << rows.size() << "\n";
        for (const auto& [id, text] : rows) {
            f << id << " " << text << "\n";
        }
    }
    ~TempEqStrFile() { std::remove(path_.c_str()); }

    // Filled in place: StringDatabase holds an atomic query counter, so it
    // is neither copyable nor movable.
    void loadInto(EQT::StringDatabase& db) const {
        EXPECT_TRUE(db.loadEqStrFile(path_)) << path_;
    }

private:
    std::string path_;
};

eqt::inventory::ItemInstance plainItem() {
    eqt::inventory::ItemInstance it;
    it.itemId = 4231;
    it.name = "Bladed Thulian Claws";
    it.ac = 12;
    it.damage = 19;
    it.delay = 21;
    it.skillType = 1;
    it.str = 5;
    it.weight = 4.5f;
    it.size = eqt::inventory::SIZE_MEDIUM;
    return it;
}

EQ::SpellData plainSpell() {
    EQ::SpellData s;
    s.id = 15;
    s.name = "Word of Health";
    s.mana_cost = 200;
    s.cast_time_ms = 4500;
    s.casting_skill = EQ::CastingSkill::Alteration;
    for (auto& lvl : s.class_levels) lvl = 255;
    s.class_levels[1] = 44;   // Cleric
    return s;
}

}  // namespace

// ===================================================================
// 1. The table is the source -- reword it and the window rewords.
// ===================================================================

TEST(DisplayTextFromDbT429, ItemLinesFollowTheStringTableNotTheSource) {
    // The shipped rows are "AC: %1" and "DMG: %1". A composer that owns its
    // text draws those whatever the table says.
    const TempEqStrFile file({
        {eqt::inventory::kItemStrAc,  "ARMOUR CLASS >> %1"},
        {eqt::inventory::kItemStrDmg, "WALLOP >> %1"},
    });
    EQT::StringDatabase db;
    file.loadInto(db);

    const auto lines = BuildItemDisplayText(db, plainItem());
    EXPECT_TRUE(anyLineContains(lines, "ARMOUR CLASS >> 12")) << dump(lines);
    EXPECT_TRUE(anyLineContains(lines, "WALLOP >> 19")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "AC: 12")) << dump(lines);
}

TEST(DisplayTextFromDbT429, SpellLinesFollowTheStringTableNotTheSource) {
    const TempEqStrFile file({
        {eqt::spell::kSpellStrManaCost, "MANA >>> %1"},
        {eqt::spell::kSpellStrCastTime, "CASTS IN %1"},
    });
    EQT::StringDatabase db;
    file.loadInto(db);

    const auto lines = BuildSpellDisplayText(db, plainSpell(), 2, 50);
    EXPECT_TRUE(anyLineContains(lines, "MANA >>> 200")) << dump(lines);
    EXPECT_TRUE(anyLineContains(lines, "CASTS IN 4.5")) << dump(lines);
    EXPECT_FALSE(anyLineContains(lines, "Mana Cost: 200")) << dump(lines);
}

// ===================================================================
// 2. A table miss draws nothing -- never a hardcoded fallback.
// ===================================================================

TEST(DisplayTextFromDbT429, AnEmptyTableDrawsNoItemLines) {
    const TempEqStrFile file({{1, "unrelated"}});
    EQT::StringDatabase db;
    file.loadInto(db);

    // plainItem() sets no flags, so ItemInstance::getFlagsString() -- a WORD
    // table on the struct, not an eqstr format -- contributes nothing either.
    const auto lines = BuildItemDisplayText(db, plainItem());
    EXPECT_TRUE(lines.empty()) << dump(lines);
}

TEST(DisplayTextFromDbT429, AnEmptyTableDrawsNoSpellLines) {
    const TempEqStrFile file({{1, "unrelated"}});
    EQT::StringDatabase db;
    file.loadInto(db);

    const auto lines = BuildSpellDisplayText(db, plainSpell(), 2, 50);
    EXPECT_TRUE(lines.empty()) << dump(lines);
}

// ===================================================================
// 3. Every id the composers can fetch is in the db that ships.
// ===================================================================

TEST(DisplayTextFromDbT429, EveryItemDisplayIdExistsInTheShippedDb) {
    const EQT::StringDatabase& db = eqt_test::ShippedStrings();
    ASSERT_TRUE(db.isLoaded()) << "shipped db not open: " << WILLEQ_DB_REAL_PATH;

    for (uint32_t id : eqt::inventory::ItemDisplayEqStrIds()) {
        EXPECT_FALSE(db.getString(id).empty())
            << "eqstr " << id << " is drawn by BuildItemDisplayText() and is "
            << "not in data/willeq.db's eqstrings table";
    }
}

TEST(DisplayTextFromDbT429, EverySpellDisplayIdExistsInTheShippedDb) {
    const EQT::StringDatabase& db = eqt_test::ShippedStrings();
    ASSERT_TRUE(db.isLoaded()) << "shipped db not open: " << WILLEQ_DB_REAL_PATH;

    for (uint32_t id : eqt::spell::SpellDisplayEqStrIds()) {
        EXPECT_FALSE(db.getString(id).empty())
            << "eqstr " << id << " is drawn by BuildSpellDisplayText() and is "
            << "not in data/willeq.db's eqstrings table";
    }
}

// ===================================================================
// The printf rows -- the surprise. 3030/3033 are NOT positional.
// ===================================================================

TEST(DisplayTextFromDbT429, TheShippedCastTimeRowsAreCPrintfFormats) {
    const EQT::StringDatabase& db = eqt_test::ShippedStrings();
    ASSERT_TRUE(db.isLoaded());

    // This is what the table actually ships -- not "%1".
    EXPECT_EQ(db.getString(eqt::inventory::kItemStrEffectCastTime),
              "(Casting Time: %2.1f)");
    EXPECT_EQ(db.getString(eqt::inventory::kItemStrEffectMustEquipCastTime),
              "(Must Equip.  Casting Time: %2.1f)");

    // The positional engine cannot render it; EqStrPrintfFloat() can.
    EXPECT_EQ(db.formatString(eqt::inventory::kItemStrEffectCastTime, {"2.5"}),
              "(Casting Time: .1f)")
        << "if this ever changes, the printf branch may no longer be needed";
    EXPECT_EQ(eqt::EqStrPrintfFloat(db, eqt::inventory::kItemStrEffectCastTime, 2.5),
              "(Casting Time: 2.5)");
}

TEST(DisplayTextFromDbT429, TheClickEffectQualifierRendersItsCastTime) {
    const EQT::StringDatabase& db = eqt_test::ShippedStrings();
    ASSERT_TRUE(db.isLoaded());

    eqt::inventory::ItemInstance it = plainItem();
    it.clickEffect.effectId = 300;
    it.clickEffect.name = "Shield of Words";
    it.clickEffect.castTime = 2500;

    const auto lines = BuildItemDisplayText(db, it);
    EXPECT_TRUE(anyLineContains(lines, "Effect: Shield of Words (Casting Time: 2.5)"))
        << dump(lines);
}

TEST(DisplayTextFromDbT429, PrintfFloatLeavesAPositionalRowAlone) {
    // 3016 "AC: %1" has no float conversion, so it comes back untouched --
    // the guard that keeps a future table edit from silently mangling a row.
    const EQT::StringDatabase& db = eqt_test::ShippedStrings();
    ASSERT_TRUE(db.isLoaded());
    EXPECT_EQ(eqt::EqStrPrintfFloat(db, eqt::inventory::kItemStrAc, 2.5), "AC: %1");
    EXPECT_EQ(eqt::EqStrPrintfFloat(db, 999999, 2.5), "");
}
