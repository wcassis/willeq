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
 * the only name source in the tree; these tests hold both name functions and
 * the scale map to it.
 *
 * Offline: opens the shipped data/willeq.db; skips if absent. No device, no
 * server, no DISPLAY.
 */

#include <gtest/gtest.h>
#include <fstream>
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
    {330, "Drakkin",  "Froglok (Playable)"},
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

TEST_F(RaceNameFromDbTest, IdWithNoRowIsUnknownEvenWithTheDbOpen) {
    ASSERT_FALSE(db_.getRaceModel(9999).found);
    EXPECT_EQ(EverQuest::GetRaceName(9999), "Unknown (9999)");
}

#ifdef EQT_HAS_GRAPHICS

// EQT::Graphics::getRaceName() is the graphics-side twin of the above. It
// already preferred the db; T271 deletes its fallback switch so the two
// cannot drift apart again.
TEST_F(RaceNameNoDatabaseTest, GraphicsNameIsUnknownWithNoDb) {
    for (uint16_t id : {1, 13, 14, 21, 33, 42, 43, 128, 130, 330}) {
        EXPECT_EQ(EQT::Graphics::getRaceName(id), "Unknown (" + std::to_string(id) + ")")
            << "race " << id;
    }
}

// getRaceScale()'s fallback switch was shifted the same way (42 "Rat" 0.3 vs
// row WOL Wolf 0.8; 43 "Bear" 1.2 vs row BEA Bear 1.0; 13 "Wolf" 0.8 vs row
// AVI Aviak 1.2). With no db there is no scale data, so 1.0 -- the switch's
// own default -- is the only honest answer.
TEST_F(RaceNameNoDatabaseTest, GraphicsScaleIsNeutralWithNoDb) {
    for (uint16_t id : {2, 8, 9, 10, 11, 12, 13, 42, 43, 85, 127, 128, 240}) {
        EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(id), 1.0f) << "race " << id;
    }
}

TEST_F(RaceNameFromDbTest, GraphicsNameAndScaleFollowEveryRow) {
    int rows = 0;
    for (uint16_t id = 0; id <= kMaxRaceId; ++id) {
        auto info = db_.getRaceModel(id);
        if (!info.found) continue;
        ++rows;
        EXPECT_EQ(EQT::Graphics::getRaceName(id), info.name) << "race " << id;
        EXPECT_FLOAT_EQ(EQT::Graphics::getRaceScale(id), info.scale) << "race " << id;
        // The two name functions must agree -- they are the same question.
        EXPECT_EQ(EQT::Graphics::getRaceName(id), EverQuest::GetRaceName(id)) << "race " << id;
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

// Names the switch got wrong the same way -- pinned so the graphics twin can
// never drift back to the pre-JSON numbering either.
TEST_F(RaceNameFromDbTest, GraphicsNamePinsTheContradictedIds) {
    EXPECT_EQ(EQT::Graphics::getRaceName(13),  "Aviak");       // switch said "Wolf"
    EXPECT_EQ(EQT::Graphics::getRaceName(21),  "Evil Eye");    // switch said "Skeleton"
    EXPECT_EQ(EQT::Graphics::getRaceName(26),  "Froglok");     // switch said "Snake"
    EXPECT_EQ(EQT::Graphics::getRaceName(35),  "Eel");         // switch said "Rat"
    EXPECT_EQ(EQT::Graphics::getRaceName(42),  "Wolf");        // switch said "Wolf" -- agreed by luck
    EXPECT_EQ(EQT::Graphics::getRaceName(46),  "Imp");         // switch said "Goblin"
    EXPECT_EQ(EQT::Graphics::getRaceName(85),  "Spectre");     // switch said "Dragon"
    EXPECT_EQ(EQT::Graphics::getRaceName(128), "Iksar");       // switch agreed here
}

#endif  // EQT_HAS_GRAPHICS
