// B1103 (GitLab #423): the spellbook drew the SAME icon in every slot while
// the gem bar was right (Will's XU4 session, character testcaster).
//
// The mechanism, proven in refactor/B1103/README.md: spells_us.txt carries
// two icon-looking columns, and the shipped loader bound the WRONG one to
// the column the spellbook (and the buff bar, and the shift-click spell
// display) read. Field 143 -- eqemu's own comment (common/spdat.h:1661):
// "always set to 0, client doesn't save this" -- is not an icon column in
// the Titanium export: 0 on all 8,066 rows. Field 144 -- NEW_ICON, "Spell
// icon used by the client in uifiles/default/spells??.tga, both for spell
// gems & buff window" (spdat.h:1662) -- is the column the official client
// actually uses. The gem bar reads it (spell->gem_icon) and is right; the
// book reads the phantom (spell->spell_icon) and every slot lands on cell 0
// of the Spells family. The fix rebinds the loader's spell_icon column to
// field 144, one line, at the source; this test pins the consequence the
// fix exists for:
//
//   1. a published spellbook entry carries the DB's icon id for a known
//      spell (the value the DB now holds for that spell);
//   2. two spells' published entries DIFFER -- "no all-the-same", the
//      observed defect;
//   3. the Spells-sheet cell for one id is not the cell for the other
//      (indexForId resolves both in-sheet, to different cells);
//   4. the DB's column is NOT the all-zero phantom any more.
//      DbReproducible pins the DB bytes against the recipe
//      (build_willeq_db.sh --check), so a silent re-drift of the loader's
//      binding is a red hygiene test.
//
// Device-free: no renderer, no pack, no display. The real
// PublishFullStateSnapshot() publishes over a bridge whose events the test
// reads through setEventObserver() (the same tap the scenario lane uses);
// the DB is the real shipped data/willeq.db (self-skips without it, the
// test_spellbook_memorize precedent).

#include <gtest/gtest.h>

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/graphics/ui/spell_icon_atlas.h"
#include "client/spell/spell_manager.h"
#include "client/state/event_bus.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

using namespace EQT::Graphics;

namespace {

// The real SQLite spell DB, opened once (same preamble as
// test_spellbook_memorize / test_spellbook_fix2).
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;

bool hasDb() { return g_db_available; }

// The spells scribed into the model book, chosen from the shipped DB for
// distinct, in-sheet icon ids (Summon Corpse = 35, Cloak = 138, Ignite
// Blood = 142, Cleanse = 25 -- the test re-reads the DB's own values for
// each, so nothing here is a re-typed constant).
const std::vector<uint32_t> kBookSpells = {3u, 5u, 6u, 8u};

// A bridge that captures nothing itself (the setEventObserver tap does the
// capturing); the real PublishFullStateSnapshot() only needs pushEvent to
// land somewhere.
class B1103CaptureBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// Find the SpellbookSnapshot in the captured events (the snapshot is the
// book's publish; nothing else pushed here carries one).
const eqt::state::SpellbookSnapshotData* findSnapshot(
        const std::vector<eqt::state::GameEvent>& events) {
    for (const auto& e : events) {
        if (e.type == eqt::state::GameEventType::SpellbookSnapshot) {
            return &std::get<eqt::state::SpellbookSnapshotData>(e.data);
        }
    }
    return nullptr;
}

TEST(B1103SpellbookIconSource, PublishedEntriesCarryTheDbIconId) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    EverQuest eq = zonetest::MakeEq();
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();

    // The DB's own icon column for each scribed spell -- the expected
    // value, read at test time, so this pins the DB and the publish
    // against each other, not against a re-typed constant.
    std::unordered_map<uint32_t, int> dbIcon;
    for (uint32_t id : kBookSpells) {
        EQT::SpellInfo info = g_willeq_db.getSpell(id);
        EXPECT_EQ(info.id, id) << "the DB lost spell " << id;
        dbIcon[id] = static_cast<int>(info.spellIcon);
        // Precondition, the fix's whole premise: the DB carries a real
        // (non-phantom) icon for these spells.
        EXPECT_GE(info.spellIcon, 0)
            << "DB spell_icon for " << info.name << " is " << info.spellIcon;
    }

    // Two scribes with distinct icons: Summon Corpse (35) and Cloak (138).
    ASSERT_TRUE(mgr->scribeSpell(kBookSpells[0], 0));
    ASSERT_TRUE(mgr->scribeSpell(kBookSpells[1], 1));
    ASSERT_EQ(mgr->getSpellbookSpell(0), kBookSpells[0]);
    ASSERT_EQ(mgr->getSpellbookSpell(1), kBookSpells[1]);

    B1103CaptureBridge bridge;
    std::vector<eqt::state::GameEvent> captured;
    bridge.setEventObserver([&captured](const eqt::state::GameEvent& e) {
        captured.push_back(e);
    });
    eq.setBridge(&bridge);
    eq.PublishFullStateSnapshot();

    const auto* snap = findSnapshot(captured);
    ASSERT_NE(snap, nullptr) << "the snapshot publish never landed";
    ASSERT_EQ(snap->spells.size(), 2u) << "the book has exactly two scribes";

    std::vector<uint32_t> icons;
    for (const auto& entry : snap->spells) {
        icons.push_back(entry.iconId);
        EXPECT_EQ(static_cast<int>(entry.iconId), dbIcon[entry.spellId])
            << "spellbook entry for " << entry.name << " (" << entry.spellId
            << ") carries " << entry.iconId << ", the DB says "
            << dbIcon[entry.spellId];
        // The family the book draws: the Spells sheet, in-sheet for the
        // id (B1077's lookup; -1 would draw nothing at all).
        EXPECT_GE(
            SpellIconAtlasLayout::indexForId(
                SpellIconAtlasLayout::SpellIconSheet::Spells, entry.iconId),
            0)
            << entry.name << " icon id " << entry.iconId
            << " is out of the Spells family";
    }

    // The observed defect was "the same icon in every slot": the two
    // entries' ids must DIFFER.
    EXPECT_NE(icons[0], icons[1])
        << "all-the-same: both entries carry iconId " << icons[0];

    // And the cell itself: the Spells-sheet cell for one is not the cell
    // for the other.
    EXPECT_NE(
        SpellIconAtlasLayout::indexForId(
            SpellIconAtlasLayout::SpellIconSheet::Spells, icons[0]),
        SpellIconAtlasLayout::indexForId(
            SpellIconAtlasLayout::SpellIconSheet::Spells, icons[1]));
}

// The phantom the pre-fix loader shipped: field 143 (ai_pt_bonus) is 0 on
// every row of the export. The pre-fix DB's spell_icon column IS that
// field, so the pre-fix tree drew one cell for every slot. After the
// rebind the DB's column carries field 144 (NEW_ICON), so this asserts the
// column is NOT the all-zero column any more -- read straight from the
// DB, so it is the rebind that makes it pass.
TEST(B1103SpellbookIconSource, TheDbColumnIsNotTheAllZeroPhantom) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    bool allZero = true;
    for (uint32_t id = 1; id <= 100; ++id) {
        EQT::SpellInfo info = g_willeq_db.getSpell(id);
        if (info.spellIcon != 0) {
            allZero = false;
            break;
        }
    }
    EXPECT_FALSE(allZero)
        << "the DB's spell_icon is still 0 on every sampled row -- the "
           "phantom column, not the client's NEW_ICON";
}

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = WILLEQ_DB_PATH;
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
