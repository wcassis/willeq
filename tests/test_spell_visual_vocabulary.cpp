/*
 * T381 (board B160): the spell visual vocabulary is reachable.
 *
 * B160, measured by T380: SpellVisualFX::setSpellDatabase() had no caller in
 * the client, so m_spell_db was permanently nullptr and every consumer took
 * its null branch -- getSpellColor() always returned the default
 * SColor(255,150,180,255), isSprayCastStyle() always returned false, and
 * resolveVisual() always left the index at kSpellsEffNoEffectIndex so the
 * whole T70/T72 spells.eff layer could never reach Resolved. Net user-visible
 * effect: every spell in the game drew one identical lavender-blue orbital
 * glow.
 *
 * The fix routes the three facts the renderer needs (resist type, casting
 * school, spell_affect_index) across the bridge BY VALUE on the
 * SpellCastVisual* events -- the SpellDatabase pointer must not cross,
 * because the only instance is game-thread-owned and its LRU has no mutex.
 *
 * These tests drive the SAME two halves the production path drives:
 *   producer  -- SpellData::visualVocabulary(), what SpellManager copies onto
 *                SpellCastVisualStartedData/CompleteData;
 *   consumer  -- SpellVisualFX::noteSpellVocabulary(), what
 *                src/client/bridge/irrlicht_bridge.cpp calls before
 *                createCastGlow()/createSpellComplete().
 * They are assembled here through a real eqt::state::GameEvent payload so the
 * test fails if either half of the wire is cut.
 *
 * EDT_NULL Irrlicht device (no window, no GPU, no DISPLAY) -- the same idiom
 * every other renderer-side test in this suite uses.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#ifdef EQT_HAS_GRAPHICS
#include <irrlicht.h>

#include "client/graphics/spell_visual_fx.h"
#include "client/spell/spell_database.h"
#include "client/spell/spell_data.h"
#include "client/state/event_bus.h"
#include "client/willeq_database.h"

#ifdef EQT_HAS_GLES2
#include "client/graphics/environment/particle_manager.h"
#endif
#endif

#ifdef EQT_HAS_GRAPHICS

using namespace EQ;

namespace {

// Spells chosen against the shipped data, verified with
// scripts/refactor/t380_spell_visual_census.py:
//   54   Frost Bolt      resist Cold(3), Evocation, spell_affect_index 23
//                        -> spells.eff record 23: Source + Middle + Target
//   93   Burst of Flame  resist Fire(2), Evocation, spell_affect_index 0
//   15   Greater Healing resist None(0), Alteration (the spray cast style)
//   3    Summon Corpse   spell_affect_index 4  -> Source + Target, NO Middle
//   6    Ignite Blood    spell_affect_index 20 -> Source + Middle + Target
//   6594 GM Cleansing    spell_affect_index -1, but field 145 = 102 (B1127: Resolved)
//   523  Tail Lashings   spell_affect_index -1 and field 145 = 0 (NoAffectIndex)
constexpr uint32_t kFrostBolt     = 54;
constexpr uint32_t kBurstOfFlame  = 93;
constexpr uint32_t kGreaterHealing = 15;
constexpr uint32_t kSummonCorpse  = 3;
constexpr uint32_t kGmCleansing   = 6594;
// B1127: no spells.eff record AND no spellsnew.eff record (field 145 = 0).
constexpr uint32_t kTailLashings  = 523;

const irr::video::SColor kPreT381DefaultColour(255, 150, 180, 255);

std::string eqClientPath() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : EQ_CLIENT_DATA_PATH;
}

std::string willeqDbPath() {
    const char* env = std::getenv("WILLEQ_DB_PATH");
    return env ? env : WILLEQ_DB_PATH;
}

class SpellVisualVocabularyTest : public ::testing::Test {
protected:
    static EQT::WilleqDatabase* db;
    static bool db_available;

    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<SpellVisualFX> fx_;
    SpellDatabase spellDb_;

    static void SetUpTestSuite() {
        const std::string path = willeqDbPath();
        if (!std::filesystem::exists(path)) return;
        db = new EQT::WilleqDatabase();
        if (db->open(path)) {
            EQT::WilleqDatabase::setGlobalInstance(db);
            db_available = true;
        }
    }

    static void TearDownTestSuite() {
        delete db;
        db = nullptr;
    }

    void SetUp() override {
        if (!db_available) {
            GTEST_SKIP() << "willeq.db not found at " << willeqDbPath();
        }
        ASSERT_TRUE(spellDb_.initFromDatabase()) << spellDb_.getLoadError();

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";
        }
        fx_ = std::make_unique<SpellVisualFX>(device_->getSceneManager(),
                                              device_->getVideoDriver(),
                                              eqClientPath());
    }

    void TearDown() override {
        fx_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // The production wire, both halves: game thread reads its own SpellData
    // onto the event, renderer thread notes the payload into the FX layer.
    void castThroughTheBridge(uint32_t spell_id) {
        const SpellData* spell = spellDb_.getSpell(spell_id);
        ASSERT_NE(spell, nullptr) << "spell " << spell_id << " missing from willeq.db";

        eqt::state::SpellCastVisualStartedData started;
        started.casterId = 1;
        started.spellId = spell_id;
        started.castTimeMs = 1000;
        started.spellName = spell->name;
        started.spellVisual = spell->visualVocabulary();

        eqt::state::GameEvent event(eqt::state::GameEventType::SpellCastVisualStarted,
                                    std::move(started));
        const auto& d = std::get<eqt::state::SpellCastVisualStartedData>(event.data);
        fx_->noteSpellVocabulary(d.spellId, d.spellVisual);
    }
};

EQT::WilleqDatabase* SpellVisualVocabularyTest::db = nullptr;
bool SpellVisualVocabularyTest::db_available = false;

// ---------------------------------------------------------------------------
// Producer half: the event payload actually carries the three facts.
// ---------------------------------------------------------------------------

TEST_F(SpellVisualVocabularyTest, EventPayloadCarriesResistSchoolAndAffectIndex) {
    const SpellData* frost = spellDb_.getSpell(kFrostBolt);
    ASSERT_NE(frost, nullptr);

    eqt::state::SpellCastVisualStartedData started;
    started.spellVisual = frost->visualVocabulary();

    EXPECT_TRUE(started.spellVisual.known);
    EXPECT_EQ(started.spellVisual.resistType, ResistType::Cold);
    EXPECT_EQ(started.spellVisual.school, SpellSchool::Evocation);
    EXPECT_EQ(started.spellVisual.spellAffectIndex, 23);
    // B1127: and the spellsnew.eff record the oracle draws (field 145).
    EXPECT_EQ(started.spellVisual.spellAnim, 82);

    // A default-constructed payload (an id the game thread could not find)
    // must read as "unknown", never as a real Fire/Abjuration spell.
    eqt::state::SpellCastVisualCompleteData complete;
    EXPECT_FALSE(complete.spellVisual.known);
    EXPECT_EQ(complete.spellVisual.spellAffectIndex, kNoSpellAffectIndex);
    EXPECT_EQ(complete.spellVisual.spellAnim, 0);
}

// ---------------------------------------------------------------------------
// T384 (board B163): the fourth fact, travelType, rides the same payload.
// T381 left the slot empty because the `spells` table had no travel_type
// column; willeq.db version 7 adds it.
// ---------------------------------------------------------------------------

TEST_F(SpellVisualVocabularyTest, VocabularyCarriesTravelType) {
    // The struct is copied by value into two bridge event payloads; T381
    // measured that it costs nothing there only because it fits the variant's
    // slack. travelType went into the padding byte after `school`.
    EXPECT_EQ(sizeof(SpellVisualVocabulary), 8u);

    const SpellData* frost = spellDb_.getSpell(kFrostBolt);
    ASSERT_NE(frost, nullptr);
    const SpellData* flame = spellDb_.getSpell(kBurstOfFlame);
    ASSERT_NE(flame, nullptr);

    // Frost Bolt is one of the 85 travel_type == 3 spells; Burst of Flame is
    // not, so a stuck-at-zero (the pre-T384 state) or a stuck-at-three read
    // both fail.
    eqt::state::SpellCastVisualStartedData started;
    started.spellVisual = frost->visualVocabulary();
    EXPECT_TRUE(started.spellVisual.known);
    EXPECT_EQ(started.spellVisual.travelType, 3);

    eqt::state::SpellCastVisualCompleteData complete;
    complete.spellVisual = flame->visualVocabulary();
    EXPECT_EQ(complete.spellVisual.travelType, 0);

    // An id the game thread could not resolve carries no travel stage.
    eqt::state::SpellCastVisualCompleteData unknown;
    EXPECT_FALSE(unknown.spellVisual.known);
    EXPECT_EQ(unknown.spellVisual.travelType, 0);
}

// ---------------------------------------------------------------------------
// The B160 headline: two resist types, two colours.
// ---------------------------------------------------------------------------

TEST_F(SpellVisualVocabularyTest, FireAndColdSpellsDrawDifferentColours) {
    castThroughTheBridge(kBurstOfFlame);
    castThroughTheBridge(kFrostBolt);

    const irr::video::SColor fire = fx_->getSpellColorForSpell(kBurstOfFlame);
    const irr::video::SColor cold = fx_->getSpellColorForSpell(kFrostBolt);

    // The defect: before T381 both of these were kPreT381DefaultColour.
    EXPECT_NE(fire.color, cold.color)
        << "a Fire spell and a Cold spell still draw the same colour (B160)";
    EXPECT_NE(fire.color, kPreT381DefaultColour.color);
    EXPECT_NE(cold.color, kPreT381DefaultColour.color);

    // The six resist colours in spell_visual_fx_nodes.cpp, now reachable.
    EXPECT_EQ(fire.color, irr::video::SColor(255, 255, 120, 50).color);
    EXPECT_EQ(cold.color, irr::video::SColor(255, 100, 180, 255).color);
}

TEST_F(SpellVisualVocabularyTest, AnUnnotedSpellKeepsThePreT381Fallback) {
    // Never noted: no cast event has been seen for this id. Behaviour must be
    // exactly what the whole game got before T381, not a crash and not a
    // fabricated colour.
    EXPECT_EQ(fx_->getSpellColorForSpell(kFrostBolt).color, kPreT381DefaultColour.color);
    EXPECT_FALSE(fx_->isSprayCastStyle(kGreaterHealing));
    EXPECT_FALSE(fx_->spellVocabulary(kFrostBolt).known);
    EXPECT_EQ(fx_->resolveVisual(kFrostBolt).source, SpellVisualSource::NoAffectIndex);
}

TEST_F(SpellVisualVocabularyTest, SchoolSelectsTheSprayCastStyle) {
    castThroughTheBridge(kGreaterHealing);  // Alteration
    castThroughTheBridge(kFrostBolt);       // Evocation

    // isSprayCastStyle() returned false for every spell in the game before
    // T381 -- the spray cast style never played.
    EXPECT_TRUE(fx_->isSprayCastStyle(kGreaterHealing));
    EXPECT_FALSE(fx_->isSprayCastStyle(kFrostBolt));
}

// ---------------------------------------------------------------------------
// The spells.eff layer reaches Resolved.
// ---------------------------------------------------------------------------

TEST_F(SpellVisualVocabularyTest, ASpellWithAnAffectIndexReachesResolved) {
    if (!std::filesystem::exists(eqClientPath() + "/spells.eff")) {
        GTEST_SKIP() << "spells.eff not present under " << eqClientPath();
    }
    castThroughTheBridge(kFrostBolt);

    const SpellVisualResolution res = fx_->resolveVisual(kFrostBolt);
    EXPECT_EQ(res.source, SpellVisualSource::Resolved)
        << "spells.eff resolution is still unreachable (B160)";
    EXPECT_FALSE(res.isFallback());
    EXPECT_EQ(res.spell_affect_index, 23);
    EXPECT_TRUE(res.hasSourceStage());
    EXPECT_TRUE(res.hasTargetStage());

    // And a spell with neither record lands on the NoAffectIndex fallback.
    castThroughTheBridge(kTailLashings);
    EXPECT_EQ(fx_->resolveVisual(kTailLashings).source, SpellVisualSource::NoAffectIndex);
    // B1127 (#447): a -1 spells.eff index with a field-145 record IS drawn by
    // the oracle (1899 Imbue Fire Opal, review/B1127/README.md), so it resolves.
    castThroughTheBridge(kGmCleansing);
    EXPECT_EQ(fx_->resolveVisual(kGmCleansing).source, SpellVisualSource::Resolved);
}

#ifdef EQT_HAS_GLES2
// B1170 (#489): the official client draws a record's Middle stage only on a
// missile OBJECT (eqgame 00495d8e / 00495d93: an anchor-2 slot with no object
// is skipped), and only a server 0x4033 message builds one (00460737). So a
// cast draws no Middle and its Target stage lands on OP_Action, even for a
// record WITH a Middle stage (Burst of Flame, record 80: EDD 279, anchor 2).
// Before B1170 the record-driven B1152 missile deferred that landing to its
// own arrival (createLanding returned 0, "deferring to the in-flight
// missile"); now the landing is drawn on OP_Action for both records.
TEST_F(SpellVisualVocabularyTest, ACastDrawsNoMiddleStageWithoutAMissileObject) {
    if (!std::filesystem::exists(eqClientPath() + "/spells.eff")) {
        GTEST_SKIP() << "spells.eff not present under " << eqClientPath();
    }
    EQT::Graphics::Environment::ParticleManager pm(device_->getSceneManager(),
                                                   device_->getVideoDriver());
    fx_->setParticleManager(&pm);
    fx_->setEntityPositionCallback(
        [](uint16_t, irr::core::vector3df& out) -> bool {
            out = irr::core::vector3df(0.0f, 0.0f, 0.0f);
            return true;
        });

    // B1127: the stages are field 145's (spellsnew.eff). Summon Corpse's
    // record 83 has no Middle emitter; Burst of Flame's record 80 has one.
    castThroughTheBridge(kSummonCorpse);
    castThroughTheBridge(kBurstOfFlame);
    ASSERT_EQ(fx_->resolveVisual(kSummonCorpse).source, SpellVisualSource::Resolved);
    ASSERT_FALSE(fx_->resolveVisual(kSummonCorpse).hasTravelStage());
    ASSERT_TRUE(fx_->resolveVisual(kBurstOfFlame).hasTravelStage());

    // A record WITH a Middle stage: the cast builds no missile object, and its
    // landing is not deferred -- it is drawn on OP_Action, on the target (2).
    fx_->createCastGlow(1, kBurstOfFlame, 1000, 60, 0);
    EXPECT_EQ(fx_->missileObjectCount(), 0u)
        << "only a server 0x4033 message builds a missile object";
    EXPECT_NE(fx_->createLanding(1, 2, kBurstOfFlame, 60), 0u)
        << "no missile object: the Target stage lands on OP_Action";

    // A record with NO Middle stage: the same.
    fx_->createCastGlow(1, kSummonCorpse, 1000, 60, 0);
    EXPECT_NE(fx_->createLanding(1, 2, kSummonCorpse, 60), 0u);

    fx_->setParticleManager(nullptr);
}

// B1170 (#489): a server 0x4033 object for a type-9 spell missile flies from
// its source to its target on the FX clock and is removed at the hit; the
// harness and the renderer feed it the same way (spawnMissileObject).
TEST_F(SpellVisualVocabularyTest, AMissileObjectFliesAndIsRemovedAtTheHit) {
    if (!std::filesystem::exists(eqClientPath() + "/spells_us.txt")) {
        GTEST_SKIP() << "spells_us.txt not present under " << eqClientPath();
    }
    EQT::Graphics::Environment::ParticleManager pm(device_->getSceneManager(),
                                                   device_->getVideoDriver());
    fx_->setParticleManager(&pm);
    // Source 1 at the origin, target 2 twenty units north (EQ y).
    fx_->setMissileEntityCallback([](uint16_t id, SpellVisualFX::MissileEntityState& out) -> bool {
        if (id != 1 && id != 2) return false;
        out = SpellVisualFX::MissileEntityState{};
        out.eqPos[0] = id == 2 ? 20.0f : 0.0f;
        out.eqPos[1] = id == 2 ? 0.0001f : 0.0f;
        return true;
    });
    // Spell 38 (field 98 = 1, travel 3: a 30000-unit life) closes the gap and
    // is removed at the hit; spell 93 (field 18 = 0, travel 0) lives 50 units,
    // so the lifetime removes it on its first pass (refactor/B1163/README.md
    // (A), "Lifetime").
    struct Case { uint32_t spell; bool hits; };
    for (const Case c : {Case{38, true}, Case{kBurstOfFlame, false}}) {
        EQ::SpellMissile::Wire w;
        w.word = 1.5f;
        w.damped = 1;
        w.type = EQ::SpellMissile::kTypeSpell;
        w.spellId = c.spell;
        w.sourceId = 1;
        w.targetId = 2;
        fx_->spawnMissileObject(w, 60);
        ASSERT_EQ(fx_->missileObjectCount(), 1u);
        float pos[3] = {0, 0, 0};
        float lastY = 0.0f;
        int frames = 0;
        while (fx_->missileObjectCount() == 1 && frames < 300) {
            fx_->update(1.0f / 30.0f);
            ++frames;
            if (fx_->missileObjectPosition(0, pos)) {
                EXPECT_GE(pos[0], lastY) << "spell " << c.spell << " frame " << frames;  // toward +y
                lastY = pos[0];
            }
        }
        EXPECT_EQ(fx_->missileObjectCount(), 0u) << "spell " << c.spell;
        if (c.hits) {
            EXPECT_GT(lastY, 15.0f) << "spell 38 closes most of the 20-unit gap before the hit";
            EXPECT_GT(frames, 4);
        } else {
            EXPECT_LE(frames, 3);
        }
    }
    fx_->setParticleManager(nullptr);
}
#endif  // EQT_HAS_GLES2

}  // namespace

#endif  // EQT_HAS_GRAPHICS
