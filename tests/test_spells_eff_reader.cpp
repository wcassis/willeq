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
#include <cstring>
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

// ============================================================================
// T73 -- validate/refute the recovered spells.eff parameter table
// (archive/fix-spell-effects-20260811:.agent/spells_eff_format.md) against
// the unknown_bytes region T70 deliberately left unnamed. See
// refactor/T73_spells_eff_parameter_decode.md's Review for the full
// per-field verdict and distributions; these tests pin the two decisive
// findings (a straight refutation, not merely "unproven") so they cannot
// silently regress, plus the structural facts supporting the fields left
// undetermined.
//
// The document's offsets are block-relative; unknown_bytes() starts at
// block offset 128 (kSpellsEffEmitterStringBytes), so
// unknown_bytes_offset = claimed_block_offset - 128. Reading directly out
// of unknown_bytes (not through named accessors -- none were added, see
// below) is deliberate: it proves these tests read the same bytes a caller
// would, with no interpretation smuggled in.
// ============================================================================

namespace {

int32_t readI32(const std::array<uint8_t, kSpellsEffEmitterUnknownBytes>& raw, size_t block_off) {
    int32_t v = 0;
    std::memcpy(&v, raw.data() + (block_off - kSpellsEffEmitterStringBytes), sizeof(v));
    return v;
}

float readF32(const std::array<uint8_t, kSpellsEffEmitterUnknownBytes>& raw, size_t block_off) {
    float v = 0.0f;
    std::memcpy(&v, raw.data() + (block_off - kSpellsEffEmitterStringBytes), sizeof(v));
    return v;
}

// Claimed block-relative offsets, from the T73 plan's table.
constexpr size_t kClaimedDurationMs = 0x274;
constexpr size_t kClaimedFadeInMs = 0x278;
constexpr size_t kClaimedFadeOutMs = 0x27C;
constexpr size_t kClaimedMinScale = 0x280;
constexpr size_t kClaimedMaxScale = 0x288;
constexpr size_t kClaimedParticlesPerSec = 0x294;
constexpr size_t kClaimedParticleLifetime = 0x298;
constexpr size_t kClaimedColorKeyframes = 0x370;  // float[12], 48 bytes, ends at 928 exactly

}  // namespace

// The document's own claimed offsets DO land on real, non-random numeric
// data -- record 0 Source (the fixture already pinned above) reads cleanly
// as small non-negative numbers at every claimed scalar offset, and the
// colour-keyframe array is exactly 12 finite floats ending precisely at the
// 928-byte block boundary. This test pins those raw values so a future
// spells.eff drop or reader change that shifts them is caught -- it does
// NOT assert the document's field names are correct; see the two refutation
// tests below for that.
TEST_F(SpellsEffReaderFileTest, T73_Record0SourceClaimedOffsetsReadAsPlausibleNumbers) {
    const SpellsEffRecord* rec = reader.getRecord(0);
    ASSERT_NE(rec, nullptr);
    const auto& raw = rec->emitter(SpellsEffEmitterRole::Source).unknown_bytes;

    EXPECT_EQ(readI32(raw, kClaimedDurationMs), 1000);
    EXPECT_EQ(readI32(raw, kClaimedFadeInMs), 250);
    EXPECT_EQ(readI32(raw, kClaimedFadeOutMs), 750);
    EXPECT_FLOAT_EQ(readF32(raw, kClaimedMinScale), 0.10000000149011612f);
    EXPECT_FLOAT_EQ(readF32(raw, kClaimedMaxScale), 0.009999999776482582f);
    EXPECT_EQ(readI32(raw, kClaimedParticlesPerSec), 15);
    EXPECT_FLOAT_EQ(readF32(raw, kClaimedParticleLifetime), 0.60f);

    float colors[12];
    std::memcpy(colors, raw.data() + (kClaimedColorKeyframes - kSpellsEffEmitterStringBytes),
                sizeof(colors));
    for (float c : colors) {
        EXPECT_FLOAT_EQ(c, 1.0f);
    }
}

// REFUTATION 1: if 0x280/0x288 genuinely encoded "minimum scale" / "maximum
// scale" bounds, min should be <= max in the overwhelming majority of
// records. Direct measurement across all 151 name-populated emitters in the
// real file: 102 have min > max (68%), only 18 have min < max, and the
// value set includes negatives (down to -10.0) far outside the document's
// own claimed validation range (0.01-100.0). This is not "unproven", it is
// a direct internal contradiction of the claimed semantics -- pinned here
// so it cannot be silently re-adopted without someone re-deriving and
// explaining away this majority.
TEST_F(SpellsEffReaderFileTest, T73_ClaimedMinMaxScaleOrderingIsRefutedByMajorityViolation) {
    int min_gt_max = 0, min_le_max = 0, populated = 0;
    for (size_t i = 0; i < SpellsEffReader::recordCount(); ++i) {
        const SpellsEffRecord* rec = reader.getRecord(static_cast<int>(i));
        ASSERT_NE(rec, nullptr);
        for (const auto& emitter : rec->emitters) {
            if (!emitter.hasContent()) continue;
            ++populated;
            float min_scale = readF32(emitter.unknown_bytes, kClaimedMinScale);
            float max_scale = readF32(emitter.unknown_bytes, kClaimedMaxScale);
            if (min_scale > max_scale) {
                ++min_gt_max;
            } else {
                ++min_le_max;
            }
        }
    }
    ASSERT_GT(populated, 0);
    // Pin the measured finding (102/151 in the shipped file): min_scale >
    // max_scale for the MAJORITY of populated emitters. A "min/max" pair
    // that violates its own ordering more often than not cannot be the
    // claimed field -- if this ratio ever flips, the claim needs
    // re-examining, not silently trusting.
    EXPECT_GT(min_gt_max, min_le_max)
        << "min_scale > max_scale in " << min_gt_max << "/" << populated
        << " populated emitters -- this refutation no longer holds, re-examine before trusting "
           "the claimed min/max-scale semantics";
}

// REFUTATION 2: the document's own "Validation Ranges" table claims
// color_keyframes values fall in [0.0, 2.0]. Direct measurement finds
// values up to 32.0 in real, non-padding records (e.g. record 10's Target
// emitter, keyframe index 2 = 25.0) -- more than an order of magnitude
// outside the document's own stated bound. Padding records read this
// region as a flat (1,1,...,1) template (see the padding test below), and
// populated records show values decaying from a large burst value down to
// that same 1.0 baseline -- a pattern much more consistent with a
// scale/intensity keyframe curve than an RGBA colour sequence, though that
// alternate interpretation is NOT proven here either (no field is named).
TEST_F(SpellsEffReaderFileTest, T73_ClaimedColorKeyframeRangeIsRefutedByOutOfRangeValues) {
    const SpellsEffRecord* rec = reader.getRecord(10);
    ASSERT_NE(rec, nullptr);
    const auto& target = rec->emitter(SpellsEffEmitterRole::Target);
    ASSERT_TRUE(target.hasContent());

    float colors[12];
    std::memcpy(colors, target.unknown_bytes.data() +
                             (kClaimedColorKeyframes - kSpellsEffEmitterStringBytes),
                sizeof(colors));
    EXPECT_FLOAT_EQ(colors[0], 6.0f);
    EXPECT_FLOAT_EQ(colors[1], 10.0f);
    EXPECT_FLOAT_EQ(colors[2], 25.0f);
    // 25.0 is 12.5x the document's own claimed upper bound of 2.0.
    EXPECT_GT(colors[2], 2.0f)
        << "claimed colour_keyframes validation range is [0.0, 2.0] per the document; this "
           "value refutes it directly";
}

// Structural corroboration (not a name claim): every claimed scalar offset
// reads as exactly zero, and the colour-keyframe array reads as exactly
// (1,1,...,1), across all three emitters of BOTH confirmed padding records
// (88 and 255, T70's fixtures) -- consistent with these being real,
// intentional fields with sane defaults, whatever their true meaning turns
// out to be. This is why duration_ms/fade_in_ms/fade_out_ms/
// particles_per_second/particle_lifetime are left as unnamed unknown_bytes
// rather than declared noise: T73 found no internal contradiction for
// these five (unlike the two refutations above), only an unconfirmed
// semantic correlation (see the plan's Review). A future unit with a
// working semantic test (e.g. a real packet capture of the client
// triggering these effects) may be able to close the gap either way.
TEST_F(SpellsEffReaderFileTest, T73_PaddingRecordsReadClaimedOffsetsAsZeroOrOneTemplate) {
    for (int idx : {88, 255}) {
        const SpellsEffRecord* rec = reader.getRecord(idx);
        ASSERT_NE(rec, nullptr) << "record " << idx;
        for (const auto& emitter : rec->emitters) {
            EXPECT_EQ(readI32(emitter.unknown_bytes, kClaimedDurationMs), 0) << "record " << idx;
            EXPECT_EQ(readI32(emitter.unknown_bytes, kClaimedFadeInMs), 0) << "record " << idx;
            EXPECT_EQ(readI32(emitter.unknown_bytes, kClaimedFadeOutMs), 0) << "record " << idx;
            EXPECT_FLOAT_EQ(readF32(emitter.unknown_bytes, kClaimedMinScale), 0.0f)
                << "record " << idx;
            EXPECT_FLOAT_EQ(readF32(emitter.unknown_bytes, kClaimedMaxScale), 0.0f)
                << "record " << idx;
            EXPECT_EQ(readI32(emitter.unknown_bytes, kClaimedParticlesPerSec), 0)
                << "record " << idx;
            EXPECT_FLOAT_EQ(readF32(emitter.unknown_bytes, kClaimedParticleLifetime), 0.0f)
                << "record " << idx;

            float colors[12];
            std::memcpy(colors,
                        emitter.unknown_bytes.data() +
                            (kClaimedColorKeyframes - kSpellsEffEmitterStringBytes),
                        sizeof(colors));
            for (float c : colors) {
                EXPECT_FLOAT_EQ(c, 1.0f) << "record " << idx;
            }
        }
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
