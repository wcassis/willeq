/*
 * Tests for SpellsEffReader (T70).
 *
 * Deliberately NOT the parked fix-spell-effects branch's tautologies
 * (asserting "textures start with 'spel'" -- trivially true because the
 * function under test constructs them that way). Record 0 is a known
 * fixture with its exact sprite-name/attach-tag content written into the
 * T70 plan; it is asserted exactly here, not loosely. The
 * SpellAffectIndex <-> record relationship is asserted end-to-end against
 * real spells in the shipped data/willeq.db, including the specific
 * "referenced index lands on empty padding" finding from the T70 Review
 * (indices 70, 71, 80, 84, 88).
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "client/spell/spells_eff_reader.h"
#include "client/spell/spell_database.h"
#include "client/willeq_database.h"

using namespace EQ;

namespace {

std::string getEqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}

std::string getSpellsEffPath() {
    return getEqClientPath() + "/spells.eff";
}

std::string getWilleqDbPath() {
    const char* env_path = std::getenv("WILLEQ_DB_PATH");
    return env_path ? env_path : WILLEQ_DB_PATH;
}

}  // namespace

// ============================================================================
// File-shape tests -- gated honestly on the real spells.eff being present.
// ============================================================================

class SpellsEffReaderFileTest : public ::testing::Test {
protected:
    SpellsEffReader reader;
    bool loaded = false;

    void SetUp() override {
        std::string path = getSpellsEffPath();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "spells.eff not found at: " << path
                         << " (set EQ_CLIENT_PATH to override) -- SKIPPING, not passing.";
        }
        loaded = reader.load(path);
        ASSERT_TRUE(loaded) << "spells.eff present but failed to load: " << reader.loadError();
    }
};

TEST_F(SpellsEffReaderFileTest, FileSizeDividesExactlyInto256Records) {
    std::string path = getSpellsEffPath();
    auto size = std::filesystem::file_size(path);
    EXPECT_EQ(size, kSpellsEffFileSize)
        << "spells.eff is not 714752 bytes -- the whole T70 layout rests on this dividing exactly";
    EXPECT_EQ(size % kSpellsEffRecordSize, 0u);
    EXPECT_EQ(size / kSpellsEffRecordSize, kSpellsEffRecordCount);
}

// Record 0's nine sprite-name slots and two non-empty attach tags, exactly
// as written into the T70 plan -- not a loose "starts with GEN" check.
TEST_F(SpellsEffReaderFileTest, Record0MatchesKnownFixtureExactly) {
    const SpellsEffRecord* rec = reader.getRecord(0);
    ASSERT_NE(rec, nullptr);

    EXPECT_EQ(rec->header_a, 1);
    EXPECT_EQ(rec->header_b, 0);

    const auto& source = rec->emitter(SpellsEffEmitterRole::Source);
    EXPECT_EQ(source.sprite_names[0], "GENC10_SPRITE");
    EXPECT_EQ(source.sprite_names[1], "GENC30_SPRITE");
    EXPECT_EQ(source.sprite_names[2], "GENC10_SPRITE");
    EXPECT_EQ(source.attach_tag, "Source");

    const auto& middle = rec->emitter(SpellsEffEmitterRole::Middle);
    EXPECT_EQ(middle.sprite_names[0], "GENC30_SPRITE");
    EXPECT_EQ(middle.sprite_names[1], "");
    EXPECT_EQ(middle.sprite_names[2], "");
    EXPECT_EQ(middle.attach_tag, "");

    const auto& target = rec->emitter(SpellsEffEmitterRole::Target);
    EXPECT_EQ(target.sprite_names[0], "GENC20_SPRITE");
    EXPECT_EQ(target.sprite_names[1], "GENC10_SPRITE");
    EXPECT_EQ(target.sprite_names[2], "GENG00_SPRITE");
    EXPECT_EQ(target.attach_tag, "Target");

    EXPECT_TRUE(rec->hasContent());
}

// Record 255 is the last of the file's padding records: it carries no
// sprite-name/attach-tag content, though (T70 Review finding) it is NOT
// literally all-zero bytes -- the unknown region beyond byte 128 of each
// emitter is a repeated non-zero filler pattern, identical across all 164
// padding records. This test asserts only the provable claim (no name
// content), not the disproven "all zero" one.
TEST_F(SpellsEffReaderFileTest, Record255HasNoSpriteContent) {
    const SpellsEffRecord* rec = reader.getRecord(255);
    ASSERT_NE(rec, nullptr);
    EXPECT_FALSE(rec->hasContent());
    for (const auto& e : rec->emitters) {
        EXPECT_FALSE(e.hasContent());
    }
}

// Record 88 is the highest SpellAffectIndex value referenced by any spell
// in the shipped database (see the index-resolution test below) -- and,
// per the T70 Review, it is one of the padding records: byte-identical to
// record 255 past the 8-byte header. This is exactly the "referenced index
// lands on empty padding" finding the T70 plan asked this unit to check
// for; it is a genuine feature of the data, not a bug in this reader.
TEST_F(SpellsEffReaderFileTest, Record88IsEmptyPaddingDespiteBeingReferenced) {
    const SpellsEffRecord* rec = reader.getRecord(88);
    ASSERT_NE(rec, nullptr);
    EXPECT_FALSE(rec->hasContent());
}

TEST_F(SpellsEffReaderFileTest, OutOfRangeAndNoEffectIndexReturnNull) {
    EXPECT_EQ(reader.getRecord(-1), nullptr) << "SpellAffectIndex's own \"no effect\" marker";
    EXPECT_EQ(reader.getRecord(kSpellsEffNoEffectIndex), nullptr);
    EXPECT_EQ(reader.getRecord(256), nullptr);
    EXPECT_EQ(reader.getRecord(-2), nullptr);
    EXPECT_EQ(reader.getRecord(100000), nullptr);
}

// Populated-record census (also reported in the T70 Review): as of the
// shipped spells.eff, 90 of 256 records carry sprite-name content.
TEST_F(SpellsEffReaderFileTest, PopulatedRecordCountIsIndependentlyMeasured) {
    size_t populated = reader.populatedRecordCount();
    EXPECT_GT(populated, 0u);
    EXPECT_LT(populated, kSpellsEffRecordCount);
    // Loose bound, not pinned to the exact count: the file is real EQ
    // client data (not a fixture this unit owns), so pin the fixture
    // (record 0) exactly and bound the aggregate instead of hardcoding a
    // count that would need updating if this ever runs against a
    // different client data drop.
    EXPECT_GE(populated, 50u);
    EXPECT_LE(populated, 150u);
}

TEST_F(SpellsEffReaderFileTest, LoadFailsLoudlyOnWrongSize) {
    // Build a truncated copy so the size-mismatch STOP path is actually
    // exercised, not just asserted never to trigger.
    std::string src = getSpellsEffPath();
    std::string tmp_path = (std::filesystem::temp_directory_path() / "t70_truncated_spells.eff").string();
    {
        std::ifstream in(src, std::ios::binary);
        std::ofstream out(tmp_path, std::ios::binary);
        std::vector<char> buf(kSpellsEffRecordSize * 3);
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        out.write(buf.data(), in.gcount());
    }

    SpellsEffReader truncated_reader;
    EXPECT_FALSE(truncated_reader.load(tmp_path));
    EXPECT_FALSE(truncated_reader.isLoaded());
    EXPECT_FALSE(truncated_reader.loadError().empty());

    std::filesystem::remove(tmp_path);
}

TEST(SpellsEffReaderTest, LoadFailsGracefullyOnMissingFile) {
    SpellsEffReader r;
    EXPECT_FALSE(r.load("/nonexistent/path/spells.eff"));
    EXPECT_FALSE(r.isLoaded());
    EXPECT_FALSE(r.loadError().empty());
    EXPECT_EQ(r.getRecord(0), nullptr);
}

// ============================================================================
// effectNameToTexture -- salvaged mapping, verified (not trusted) against
// the real filenames in /mnt/projects/EverQuestP1999/SpellEffects/. These
// are NOT tautological: they assert specific, independently-verified
// filenames exist or do not, rather than re-deriving the expected string
// from the function under test.
// ============================================================================

TEST(SpellsEffReaderMappingTest, NonzeroDigitNamesMapToVerifiedRealFiles) {
    // Each of these has been confirmed to exist in SpellEffects/ (T70 Review).
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENC10_SPRITE"), "spelc1.tga");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENC30_SPRITE"), "spelc3.tga");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENC20_SPRITE"), "spelc2.tga");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENB30_SPRITE"), "spelb3.tga");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENU10_SPRITE"), "spelu1.tga");
}

// Confirmed counterexamples: the naive letter+digit rule constructs a
// plausible-looking name, but no such file exists in SpellEffects/. The
// reader still returns the constructed name (texture *existence* is a
// rendering-unit concern, out of scope here) -- this test exists so a
// future reader of this mapping does not assume it is 100% accurate.
TEST(SpellsEffReaderMappingTest, KnownCounterexamplesDocumented) {
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENA30_SPRITE"), "spela3.tga");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENY10_SPRITE"), "spely1.tga");
    // Neither spela3.tga nor spely1.tga exists in SpellEffects/ as of T70 --
    // verified directly by directory listing, not asserted here (this test
    // has no filesystem dependency), see T70 Review for the listing.
}

// Every "00"-digit form observed in spells.eff (16 distinct names) was
// checked against SpellEffects/ in the T70 Review: zero of sixteen
// resolved to a real file. The reader deliberately refuses to construct a
// name known not to exist, returning empty instead.
TEST(SpellsEffReaderMappingTest, ZeroDigitFormNeverResolves) {
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENA00_SPRITE"), "");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENC00_SPRITE"), "");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GENZ00_SPRITE"), "");
}

TEST(SpellsEffReaderMappingTest, NonGenPatternNamesReturnEmpty) {
    // Observed in spells.eff but not GEN[A-Z]#0_SPRITE-shaped; no evidenced
    // mapping exists for these, so they must not silently produce a guess.
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("GEN_Z_SPB"), "");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("I_SNOWFLAKESPRITE"), "");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("I_TFIRE_SPB"), "");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture(""), "");
    EXPECT_EQ(SpellsEffReader::effectNameToTexture("garbage"), "");
}

// ============================================================================
// Index-resolution: end-to-end against real spells in the shipped
// data/willeq.db, whose spell_affect_index T68 carries. This is the test
// the parked branch never had: nothing on it ever asserted a specific
// spell maps to its correct effect.
// ============================================================================

class SpellsEffIndexResolutionTest : public ::testing::Test {
protected:
    static EQT::WilleqDatabase* db;
    static bool db_available;
    static SpellsEffReader* eff_reader;
    static bool eff_available;

    SpellDatabase spellDb;

    static void SetUpTestSuite() {
        std::string db_path = getWilleqDbPath();
        if (std::filesystem::exists(db_path)) {
            db = new EQT::WilleqDatabase();
            if (db->open(db_path)) {
                EQT::WilleqDatabase::setGlobalInstance(db);
                db_available = true;
            }
        }

        std::string eff_path = getSpellsEffPath();
        if (std::filesystem::exists(eff_path)) {
            eff_reader = new SpellsEffReader();
            eff_available = eff_reader->load(eff_path);
        }
    }

    static void TearDownTestSuite() {
        delete db;
        db = nullptr;
        delete eff_reader;
        eff_reader = nullptr;
    }

    void SetUp() override {
        if (!db_available) {
            GTEST_SKIP() << "willeq.db not found at " << getWilleqDbPath() << ", skipping";
        }
        if (!eff_available) {
            GTEST_SKIP() << "spells.eff not found/loadable at " << getSpellsEffPath()
                         << ", skipping";
        }
        ASSERT_TRUE(spellDb.initFromDatabase()) << spellDb.getLoadError();
    }
};

EQT::WilleqDatabase* SpellsEffIndexResolutionTest::db = nullptr;
bool SpellsEffIndexResolutionTest::db_available = false;
SpellsEffReader* SpellsEffIndexResolutionTest::eff_reader = nullptr;
bool SpellsEffIndexResolutionTest::eff_available = false;

// Spell id 523 has spell_affect_index == -1 (confirmed by T68's
// NegativeSpellAffectIndexRoundTrips test). -1 must resolve to no record.
TEST_F(SpellsEffIndexResolutionTest, NegativeOneIndexResolvesToNoRecord) {
    const SpellData* spell = spellDb.getSpell(523);
    ASSERT_NE(spell, nullptr);
    ASSERT_EQ(spell->spell_affect_index, -1);

    const SpellsEffRecord* rec = eff_reader->getRecord(spell->spell_affect_index);
    EXPECT_EQ(rec, nullptr);
}

// Spell id 68 ("Bolt of Flame") has spell_affect_index == 0 (confirmed
// directly against data/willeq.db during T70) -- record 0, the known
// fixture asserted exactly above.
TEST_F(SpellsEffIndexResolutionTest, SpellAffectIndex0ResolvesToKnownFixtureRecord) {
    const SpellData* spell = spellDb.getSpell(68);
    ASSERT_NE(spell, nullptr);
    ASSERT_EQ(spell->spell_affect_index, 0);

    const SpellsEffRecord* rec = eff_reader->getRecord(spell->spell_affect_index);
    ASSERT_NE(rec, nullptr);
    EXPECT_TRUE(rec->hasContent());
    EXPECT_EQ(rec->emitter(SpellsEffEmitterRole::Source).sprite_names[0], "GENC10_SPRITE");
}

// Spell id 3 ("Summon Corpse") has spell_affect_index == 4 (confirmed
// directly against data/willeq.db during T70) -- record 4, independently
// verified populated (Source: '', GENB10_SPRITE, GENB20_SPRITE / tag
// "Source"; Target: GENB30_SPRITE, GENB20_SPRITE, GENB20_SPRITE / tag
// "Target").
TEST_F(SpellsEffIndexResolutionTest, SpellAffectIndex4ResolvesToVerifiedRecord) {
    const SpellData* spell = spellDb.getSpell(3);
    ASSERT_NE(spell, nullptr);
    ASSERT_EQ(spell->spell_affect_index, 4);

    const SpellsEffRecord* rec = eff_reader->getRecord(spell->spell_affect_index);
    ASSERT_NE(rec, nullptr);
    EXPECT_TRUE(rec->hasContent());
    const auto& source = rec->emitter(SpellsEffEmitterRole::Source);
    EXPECT_EQ(source.sprite_names[1], "GENB10_SPRITE");
    EXPECT_EQ(source.sprite_names[2], "GENB20_SPRITE");
    EXPECT_EQ(source.attach_tag, "Source");
}

// Scans the DB for every distinct non-(-1) spell_affect_index actually
// referenced by a spell and asserts each resolves to *some* record (not
// necessarily populated -- that is the next assertion). A referenced index
// that resolves to nullptr (out of the reader's [0,255] range) would be a
// genuine bug in either this reader or the upstream field mapping.
TEST_F(SpellsEffIndexResolutionTest, EveryReferencedIndexResolvesToARecordSlot) {
    std::vector<int32_t> referenced;
    for (uint32_t id = 1; id <= 9000; ++id) {
        const SpellData* spell = spellDb.getSpell(id);
        if (spell && spell->spell_affect_index != -1) {
            referenced.push_back(spell->spell_affect_index);
        }
    }
    ASSERT_GT(referenced.size(), 0u) << "no spells with a real spell_affect_index found";

    for (int32_t idx : referenced) {
        EXPECT_NE(eff_reader->getRecord(idx), nullptr)
            << "spell_affect_index " << idx << " is out of the reader's [0,255] range";
    }
}

// The genuine finding this unit's verification section asked for: do any
// referenced indices land on empty padding? Confirmed yes, five of them
// (70, 71, 80, 84, 88) -- see the T70 Review for the exact spell counts
// per index. This test pins that finding so a future spells.eff drop (or
// a bug in this reader) that silently "fixes" or breaks it is caught
// either way.
TEST_F(SpellsEffIndexResolutionTest, KnownEmptyPaddingIndicesAreConfirmedEmpty) {
    for (int idx : {70, 71, 80, 84, 88}) {
        const SpellsEffRecord* rec = eff_reader->getRecord(idx);
        ASSERT_NE(rec, nullptr) << "index " << idx;
        EXPECT_FALSE(rec->hasContent())
            << "index " << idx << " was expected to be empty padding per the T70 Review; "
            << "if this now has content, the finding needs updating, not silently dropping";
    }
}

// Positive control for the same mechanism: pick a handful of low,
// definitely-real indices and confirm they DO have content, so the "empty
// padding" test above isn't trivially true because hasContent() is broken.
TEST_F(SpellsEffIndexResolutionTest, KnownRealIndicesAreConfirmedPopulated) {
    for (int idx : {0, 1, 2, 3, 10, 20, 30, 50}) {
        const SpellsEffRecord* rec = eff_reader->getRecord(idx);
        ASSERT_NE(rec, nullptr) << "index " << idx;
        EXPECT_TRUE(rec->hasContent()) << "index " << idx;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
