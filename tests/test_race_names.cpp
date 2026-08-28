/**
 * T271: race DISPLAY NAMES come from the db, the same way T270 made race
 * CODES db-only.
 *
 * The defect T270 found: EverQuest::GetRaceName() carried a pre-JSON table
 * that is shifted against the audited race_models rows --
 *   14  -> "Iksar"    (row 14 is WER Werewolf; Iksar is 128)
 *   128 -> "Vah Shir" (row 128 is IKM Iksar; Vah Shir is 130)
 *   130 -> "Froglok"  (row 130 is KEM Vah Shir; the playable Froglok is 330)
 *   330 -> "Drakkin"  (row 330 is FRM Froglok)
 * and it is printed to the player (/who, /consider, inspect, target info),
 * so those four are user-visible wrong text.
 *
 * eqstr_us.txt carries no race-name table (its 3200-block is the LANGUAGE
 * list -- Barbarian/Erudian/Elvish/... -- not races), so race_models.name is
 * the only name source in the tree; these tests hold the name function and
 * the scale map to it.
 *
 * T274: race_models.name is a DISPLAY string. Row 330's was
 * "Froglok (Playable)" -- a data annotation that, once names became db-only,
 * was printed to players; it is now plain "Froglok" (the disambiguation moved
 * to the row's `note`), and a sweep here forbids any annotation in any row's
 * name. T274 also deleted EQT::Graphics::getRaceName()/getGenderName(), which
 * had zero callers.
 *
 * Offline: opens the shipped data/willeq.db; skips if absent. No device, no
 * server, no DISPLAY.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <map>
#include <string>

#include "client/eq.h"
#include "client/willeq_database.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_codes.h"
#endif

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

// The highest race_id in the audited rows is 457; walk past it so a future
// row can never silently fall outside the sweep.
constexpr uint16_t kMaxRaceId = 600;
constexpr int kExpectedRowCount = 190;

// Every id the deleted EverQuest::GetRaceName() switch answered for, with
// what it used to claim. The four shifted ones are called out by name below.
struct ShiftedId { uint16_t id; const char* wasClaimed; const char* row; };
const ShiftedId kShifted[] = {
    {14,  "Iksar",    "Werewolf"},
    {128, "Vah Shir", "Iksar"},
    {130, "Froglok",  "Vah Shir"},
    {330, "Drakkin",  "Froglok"},
};

// With no db open there is no name source at all: the honest answer is
// "unknown", not a confident wrong one.
class RaceNameNoDatabaseTest : public ::testing::Test {
protected:
    void SetUp() override { EQT::WilleqDatabase::setGlobalInstance(nullptr); }
};

class RaceNameFromDbTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
    }
    void TearDown() override {
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }
};

}  // namespace

TEST_F(RaceNameNoDatabaseTest, EveryFormerlyHardcodedIdIsUnknown) {
    // The 16 ids the switch answered for, plus the four shifted ones.
    for (uint16_t id : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 128, 130, 330}) {
        EXPECT_EQ(EverQuest::GetRaceName(id), "Unknown (" + std::to_string(id) + ")")
            << "race " << id;
    }
}

TEST_F(RaceNameNoDatabaseTest, UnmappedIdIsUnknownToo) {
    EXPECT_EQ(EverQuest::GetRaceName(9999), "Unknown (9999)");
}

TEST_F(RaceNameFromDbTest, EveryRowServesItsOwnName) {
    int rows = 0;
    for (uint16_t id = 0; id <= kMaxRaceId; ++id) {
        auto info = db_.getRaceModel(id);
        if (!info.found) continue;
        ++rows;
        ASSERT_FALSE(info.name.empty()) << "race_models row " << id << " has no name";
        EXPECT_EQ(EverQuest::GetRaceName(id), info.name) << "race " << id;
    }
    EXPECT_EQ(rows, kExpectedRowCount);
}

TEST_F(RaceNameFromDbTest, ShiftedIdsAnswerTheRow) {
    for (const auto& s : kShifted) {
        EXPECT_EQ(EverQuest::GetRaceName(s.id), s.row)
            << "race " << s.id << " used to be printed as \"" << s.wasClaimed << "\"";
    }
}

// T274: race_models.name is a DISPLAY name -- it is what /who, /consider,
// inspect and target info print. Row 330 carried "Froglok (Playable)", a
// data annotation distinguishing the playable Froglok model (FRM/FRF,
// globalpcfroglok_chr) from the monster froglok of row 26; T271 made names
// db-only, so that annotation started reaching players. Titanium prints
// "Froglok" for both. The disambiguation now lives in the row's `note`
// field, which nothing displays.
TEST_F(RaceNameFromDbTest, PlayableFroglokPrintsAsFroglok) {
    EXPECT_EQ(EverQuest::GetRaceName(330), "Froglok");
    EXPECT_EQ(EverQuest::GetRaceName(26),  "Froglok");
}

// The general rule the above is one instance of: no row's display name may
// carry a parenthetical annotation, a bracket, a race code or an id. Those
// belong in a comment or a separate column, never in the string a player
// reads.
TEST_F(RaceNameFromDbTest, NoRowNameCarriesAnAnnotation) {
    int rows = 0;
    for (uint16_t id = 0; id <= kMaxRaceId; ++id) {
        auto info = db_.getRaceModel(id);
        if (!info.found) continue;
        ++rows;
        const std::string& n = info.name;
        EXPECT_EQ(n.find('('), std::string::npos) << "race " << id << " name \"" << n << "\"";
        EXPECT_EQ(n.find(')'), std::string::npos) << "race " << id << " name \"" << n << "\"";
        EXPECT_EQ(n.find('['), std::string::npos) << "race " << id << " name \"" << n << "\"";
        EXPECT_EQ(n.find(']'), std::string::npos) << "race " << id << " name \"" << n << "\"";
        EXPECT_EQ(n.find('_'), std::string::npos) << "race " << id << " name \"" << n << "\"";
        for (char c : n) {
            EXPECT_FALSE(c >= '0' && c <= '9')
                << "race " << id << " name \"" << n << "\" contains a digit";
        }
        EXPECT_FALSE(n.empty()) << "race " << id;
        EXPECT_NE(n.front(), ' ') << "race " << id;
        EXPECT_NE(n.back(),  ' ') << "race " << id;
    }
    EXPECT_EQ(rows, kExpectedRowCount);
}

// The names the deleted pre-JSON switches got wrong, pinned against the one
// surviving name function so it can never drift back to that numbering.
// (T271 pinned these on EQT::Graphics::getRaceName(); T274 deleted that
// caller-less twin and moved the pins here.)
TEST_F(RaceNameFromDbTest, NamePinsTheContradictedIds) {
    EXPECT_EQ(EverQuest::GetRaceName(13),  "Aviak");       // switch said "Wolf"
    EXPECT_EQ(EverQuest::GetRaceName(21),  "Evil Eye");    // switch said "Skeleton"
    EXPECT_EQ(EverQuest::GetRaceName(26),  "Froglok");     // switch said "Snake"
    EXPECT_EQ(EverQuest::GetRaceName(35),  "Eel");         // switch said "Rat"
    EXPECT_EQ(EverQuest::GetRaceName(42),  "Wolf");        // switch said "Wolf" -- agreed by luck
    EXPECT_EQ(EverQuest::GetRaceName(46),  "Imp");         // switch said "Goblin"
    EXPECT_EQ(EverQuest::GetRaceName(85),  "Spectre");     // switch said "Dragon"
    EXPECT_EQ(EverQuest::GetRaceName(128), "Iksar");       // switch agreed here
}

TEST_F(RaceNameFromDbTest, IdWithNoRowIsUnknownEvenWithTheDbOpen) {
    ASSERT_FALSE(db_.getRaceModel(9999).found);
    EXPECT_EQ(EverQuest::GetRaceName(9999), "Unknown (9999)");
}

#ifdef EQT_HAS_GRAPHICS

// T274: EQT::Graphics::getRaceName() -- the graphics-side twin these tests
// used to hold against EverQuest::GetRaceName() -- is DELETED. It had zero
// callers (T271), so the only way the two could disagree was in this file.
// EverQuest::GetRaceName() is the single name function and every assertion
// below that used to name the twin now names it instead.

// getRaceScale()'s fallback switch was shifted the same way (42 "Rat" 0.3 vs
// row WOL Wolf 0.8; 43 "Bear" 1.2 vs row BEA Bear 1.0; 13 "Wolf" 0.8 vs row
// AVI Aviak 1.2). With no db there is no scale data, so 1.0 -- the switch's
// own default -- is the only honest answer.
TEST_F(RaceNameNoDatabaseTest, GraphicsScaleIsNeutralWithNoDb) {
    for (uint16_t id : {2, 8, 9, 10, 11, 12, 13, 42, 43, 85, 127, 128, 240}) {
        EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(id), 1.0f) << "race " << id;
    }
}

TEST_F(RaceNameFromDbTest, GraphicsScaleFollowsEveryRow) {
    int rows = 0;
    for (uint16_t id = 0; id <= kMaxRaceId; ++id) {
        auto info = db_.getRaceModel(id);
        if (!info.found) continue;
        ++rows;
        EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(id), info.scale) << "race " << id;
    }
    EXPECT_EQ(rows, kExpectedRowCount);
}

TEST_F(RaceNameFromDbTest, GraphicsScalePinsTheContradictedIds) {
    // id -> row scale, with what the deleted fallback switch claimed. Each of
    // these is an id where the switch's number came from a different race.
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(13),  1.2f);  // AVI Aviak;     switch 0.8 "Wolf"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(35),  1.0f);  // EEL Eel;       switch 0.2 "Rat"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(36),  0.3f);  // RAT Rat;       switch 0.4 "Giant Rat"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(39),  1.0f);  // GNN Gnoll;     switch 0.6 "Gnoll Pup"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(43),  1.0f);  // BEA Bear;      switch 1.2 "Bear"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(46),  0.5f);  // IMP Imp;       switch 0.8 "Goblin"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(48),  0.7f);  // KOB Kobold;    switch 0.3 "Spider (small)"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(49),  3.0f);  // DRA Lava Dragon; switch 0.6 "Spider (large)"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(91),  1.3f);  // ALL Alligator; switch 0.7 "Kobold"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(94),  0.7f);  // KAM Kaladim Citizen; switch 1.5 "Minotaur"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(95),  3.0f);  // CAZ Cazic Thule; switch 1.5 "Drake"
    EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(127), 0.0f);  // IVM Invisible Man
}

// T304 Leg 2: the placeholder-cube tint. EntityRenderer::getColorForRace()
// carried a hand-written switch that was the LAST surviving copy of the
// pre-JSON race numbering (T267/T270/T271 killed the other three, all of
// which were live and wrong): it tinted 21 as a Skeleton (row 21 is BEH Evil
// Eye), 26 as a Snake (FRO Froglok), 33 as a Ghost (GHU Ghoul), 35 as a Rat
// (EEL Eel), 42 as a Rat (WOL Wolf), 43 as a Bat (BEA Bear), 46 as a Goblin
// (IMP Imp), 48/49 as Spiders (KOB Kobold / DRA Lava Dragon) and 85 as a
// Dragon (SPE Spectre). The tint now comes from race_models.color.
//
// THOSE COLUMN VALUES ARE INVENTED. Will ruled on 2026-08-28 that inventing
// them is acceptable, because the cube is reached only when no model could be
// built for a race (T213), so the bar is "plausible and distinguishable" and
// NOT authenticity. They are not sourced from EverQuest data. The assertions
// below are therefore RELATIONAL -- every row has a tint, tints are distinct,
// and no id is tinted as the race the stale switch named -- never assertions
// on a particular RGB triple, which nobody may cite as sourced.

TEST_F(RaceNameNoDatabaseTest, GraphicsColourIsTheNeutralGreyWithNoDb) {
    for (uint16_t id : {2, 21, 26, 33, 35, 42, 43, 46, 48, 49, 85, 127, 240}) {
        EXPECT_EQ(EQT::Graphics::getRaceColorARGB(id), EQT::Graphics::kDefaultRaceColorARGB)
            << "race " << id;
    }
}

TEST_F(RaceNameFromDbTest, GraphicsColourFollowsEveryRow) {
    int rows = 0;
    for (uint16_t id = 0; id <= kMaxRaceId; ++id) {
        auto info = db_.getRaceModel(id);
        if (!info.found) continue;
        ++rows;
        EXPECT_EQ(EQT::Graphics::getRaceColorARGB(id), info.colorARGB) << "race " << id;
        if (info.invisible) {
            // The one deliberate zero: an invisible race gets a fully
            // transparent cube, not a grey one.
            EXPECT_EQ(info.colorARGB, 0u) << "race " << id;
        } else {
            EXPECT_NE(info.colorARGB, 0u)
                << "race " << id << " has no colour in race_models";
            EXPECT_GT((info.colorARGB >> 24) & 0xFFu, 0u)
                << "race " << id << " would draw a fully transparent cube";
        }
    }
    EXPECT_EQ(rows, kExpectedRowCount);
}

// "Distinguishable" is the whole bar Will set, so hold it: no two races may
// share a tint. The invisible races are the sole sanctioned exception -- they
// are all equally invisible.
TEST_F(RaceNameFromDbTest, EveryRaceTintIsDistinct) {
    std::map<uint32_t, uint16_t> seen;
    for (uint16_t id = 0; id <= kMaxRaceId; ++id) {
        auto info = db_.getRaceModel(id);
        if (!info.found || info.invisible) continue;
        auto it = seen.find(info.colorARGB);
        EXPECT_TRUE(it == seen.end())
            << "races " << id << " and " << (it == seen.end() ? 0 : it->second)
            << " share a placeholder tint";
        seen[info.colorARGB] = id;
    }
}

// The ids the deleted switch mis-tinted, each pinned against the race it was
// borrowing the colour from. If the numbering ever creeps back, these fail.
TEST_F(RaceNameFromDbTest, ColourPinsTheContradictedIds) {
    struct Pin { uint16_t id; uint16_t borrowedFrom; const char* wasCalled; };
    const Pin pins[] = {
        {21, 60,  "Skeleton"},      // row 21 is BEH Evil Eye; 60 is SKE Skeleton
        {26, 37,  "Snake"},         // row 26 is FRO Froglok;  37 is SNA Snake
        {33, 32,  "Ghost"},         // row 33 is GHU Ghoul;    32 is GHM Ghost
        {35, 36,  "Rat"},           // row 35 is EEL Eel;      36 is RAT Rat
        {42, 36,  "Rat"},           // row 42 is WOL Wolf
        {43, 34,  "Bat"},           // row 43 is BEA Bear;     34 is BAT Giant Bat
        {46, 40,  "Goblin"},        // row 46 is IMP Imp;      40 is GOB Goblin
        {48, 38,  "Spider"},        // row 48 is KOB Kobold;   38 is SPI Spider
        {49, 38,  "Spider"},        // row 49 is DRA Lava Dragon
        {85, 122, "Dragon"},        // row 85 is SPE Spectre
    };
    for (const auto& p : pins) {
        ASSERT_TRUE(db_.getRaceModel(p.id).found) << "race " << p.id;
        ASSERT_TRUE(db_.getRaceModel(p.borrowedFrom).found) << "race " << p.borrowedFrom;
        EXPECT_NE(EQT::Graphics::getRaceColorARGB(p.id),
                  EQT::Graphics::getRaceColorARGB(p.borrowedFrom))
            << "race " << p.id << " is tinted as the \"" << p.wasCalled
            << "\" the deleted switch called it";
    }
}

TEST_F(RaceNameFromDbTest, InvisibleRacesStayFullyTransparent) {
    EXPECT_EQ(EQT::Graphics::getRaceColorARGB(127), 0u);  // IVM Invisible Man
    EXPECT_EQ(EQT::Graphics::getRaceColorARGB(240), 0u);  // IVM Teleport Man
}

TEST_F(RaceNameFromDbTest, IdWithNoRowGetsTheNeutralGrey) {
    ASSERT_FALSE(db_.getRaceModel(9999).found);
    EXPECT_EQ(EQT::Graphics::getRaceColorARGB(9999), EQT::Graphics::kDefaultRaceColorARGB);
}

#endif  // EQT_HAS_GRAPHICS
