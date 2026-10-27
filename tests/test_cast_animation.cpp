/**
 * B1157 (#476) -- the caster's cast animation: the official client's rule,
 * one code path for the client and the FX harness.
 *
 * THE ORACLE IS THE CLIENT'S CODE (refactor/B1157/README.md; the decompiles
 * are review/B1141/ghidra/out/):
 *   - OP_BeginCast (eqgame.exe.c 122131-122132, the 0x49b39d.. handler):
 *     the self-cast path gives the spawn's animation manager (FUN_0047c08e)
 *     and calls its set function (vtbl+8) with (CastingAnim id, 1, 0, speed
 *     1.0f, 1) -- the track is named through eqgame's id -> code table
 *     (0x8ffbc8 + id x 20, B1148's anim_code_table.tsv: 42/43/44 = t04/t05/t06)
 *     and played at speed 1.0 at the packet -- no delay. 122136 is the same
 *     word's length query, FUN_0047ddd4(spell+0x186) (B1145's Source term).
 *   - FUN_00450c2f's already-playing no-op path (actor+0x134 == the
 *     requested id): the state machine does not re-request the cast track
 *     per frame; the model plays it and, when it finishes, HOLDS its last
 *     frame (looped tracks loop -- the model's own looped flag; fable's
 *     SkeletalAnimator carries the same split) through the rest of the
 *     channel -- the raised-hands pose the oracle's caster keeps (issue #476).
 *   - OP_Action / interrupt: the cast state ends and the state machine plays
 *     the new state's animation (the standing caster's idle).
 *
 * fable's pre-B1157 rule, replaced: entity_castbars.cpp played a hard-coded
 * "t04" (playThrough, stretched to 20 % of the cast time) for every NPC
 * caster and not at all for the player; the harness (model_viewer_spell_test.
 * cpp) looped an o01 idle during the channel and played a GUESSED t04/t05/t06
 * once at completion. Every B1149-B1152 gate number was measured on that idle
 * caster.
 *
 * The device-free half pins the code table; the EDT_NULL half drives a real
 * EntityRenderer through startEntityCast / completeEntityCast /
 * cancelEntityCast (the bridge's bodies) on the Dark Elf female -- the
 * B1130 oracle's caster, char 67 -- and reads the tracks off the real model
 * (skips without the db or the client tree, the test_corpse_fade gate).
 * 93/38/54: the three re-gated hand-anchored spells of the B1151 sweep --
 * the db gives all three CastingAnim 44 (t06).
 */

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <string>

#include "client/graphics/spell_cast_animation.h"
#include "client/spell/spell_action_anim.h"
#include "client/spell/spell_fx_timing.h"

using namespace EQT::Graphics;

// eqgame's id -> code table (0x8ffbc8 + id * 20), as B1148 read it: the ids
// the three re-gated spells carry, and the table's own sentinels.
TEST(CastAnimation, TheSpellIdsNameTheirTracksThroughEqgamesTable) {
    // B1149-B1152: 93 Burst of Flame, 38, 54 all carry CastingAnim 44 in the
    // shipped db (the device-free pin; the db half re-reads the rows).
    EXPECT_STREQ(EQ::SpellCastAnimationCode(44), "t06");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(42), "t04");
    EXPECT_STREQ(EQ::SpellCastAnimationCode(43), "t05");
    EXPECT_EQ(EQ::SpellCastAnimationCode(0), nullptr) << "0 = unknown (the payload's default)";
    EXPECT_EQ(EQ::SpellCastAnimationCode(79), nullptr);
}

// B1174 (#493): eqgame's OP_Action spell handler FUN_0047f4af returns before
// anything unless the spell id is in 1..9999 (FUN_0044d0d0, 0x47f4c2), the
// client has a record (0x47f4de) and its byte +0x180, spells_us.txt field 98
// (target type), is not 0 (0x47f4e6..0x47f4ed). It then asks the SOURCE for
// the spell's CastingAnim (0x47f54e..0x47f55c) when its stand byte is 'd'
// (standing) or 'f' (0x47f519..0x47f525), the spell's field 123 is not 61..64
// (FUN_0044ced9, 0x47f527..0x47f530) and the source is not a Bard (class byte
// 8) unless the packet's instrument_mod, a signed int, is above 10
// (0x47f53c..0x47f54c).
namespace {
EQ::ActionCastAnimInputs Disc8604() {
    // The lane-5 /disc: 8604, client target type 6 (self), field 123 = 51,
    // a standing Shadow Knight (5), instrument_mod 10.
    EQ::ActionCastAnimInputs in;
    in.spellId = 8604;
    in.recordExists = true;
    in.targetType = 6;
    in.sourceStandState = 100;
    in.spellAffectIndex = 51;
    in.sourceClass = 5;
    in.instrumentMod = 10;
    return in;
}
}  // namespace

TEST(CastAnimation, OPActionAsksTheStandingSourceForTheCastingAnim) {
    EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(Disc8604()));

    // The handler's outer early returns.
    for (uint32_t id : {0u, 10000u, 0xffffu}) {
        auto in = Disc8604();
        in.spellId = id;
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(in)) << "spell id " << id << " is outside 1..9999";
    }
    for (uint32_t id : {1u, 9999u}) {
        auto in = Disc8604();
        in.spellId = id;
        EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(in)) << "spell id " << id;
    }
    {
        auto in = Disc8604();
        in.recordExists = false;
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(in)) << "no client record";
    }
    {
        // 2246 Call of Alarm (CastingAnim 43) and 3077 Storm Stomp (44) have
        // target type 0 in the client's spells_us.txt: no swing.
        auto in = Disc8604();
        in.targetType = 0;
        in.spellId = 2246;
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(in)) << "2246: target type 0";
        in.spellId = 3077;
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(in)) << "3077: target type 0";
        in.targetType = 1;
        EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(in)) << "any non-zero target type";
    }

    // The stand byte: 100 or 102 only.
    for (int stand : {100, 102}) {
        auto in = Disc8604();
        in.sourceStandState = static_cast<uint8_t>(stand);
        EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(in)) << "stand " << stand;
    }
    for (int stand : {0, 101, 105, 110, 111, 115, 120}) {
        auto in = Disc8604();
        in.sourceStandState = static_cast<uint8_t>(stand);
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(in)) << "stand " << stand << " (105 = looting)";
    }

    // Field 123.
    for (int idx : {61, 62, 63, 64}) {
        auto in = Disc8604();
        in.spellAffectIndex = idx;
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(in)) << "field 123 = " << idx;
    }
    for (int idx : {60, 65, -1}) {
        auto in = Disc8604();
        in.spellAffectIndex = idx;
        EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(in)) << "field 123 = " << idx << " (-1 = no spells.eff record)";
    }

    // Bard and instrument_mod, compared signed.
    auto bard = Disc8604();
    bard.sourceClass = 8;
    EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(bard)) << "a Bard at instrument_mod 10";
    bard.instrumentMod = 11;
    EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(bard)) << "a Bard above instrument_mod 10";
    for (uint32_t mod : {0x80000000u, 0xfffffff5u, 0xffffffffu}) {
        bard.instrumentMod = mod;
        EXPECT_FALSE(EQ::ActionPlaysSourceCastAnim(bard)) << "instrument_mod " << mod << " is negative as an int";
    }
    auto nobody = Disc8604();
    nobody.sourceClass = 0;
    nobody.instrumentMod = 0;
    EXPECT_TRUE(EQ::ActionPlaysSourceCastAnim(nobody)) << "an unknown class is no Bard";
}

// The stand byte fable keeps per spawn (Entity::stand_state): the server's
// AT_Anim stand values as sent, ANIM_STAND (0) = 100, anything else unchanged.
TEST(CastAnimation, TheSpawnStandStateFollowsTheServersAppearance) {
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(100, 105), 105) << "looting";
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(105, 100), 100);
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(100, 110), 110);
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(110, 0), 100) << "ANIM_STAND";
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(100, 102), 102);
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(110, 18), 110) << "an action animation leaves the byte";
    EXPECT_EQ(EQ::SpawnStandStateAfterAppearance(110, 300), 110);
}

#ifdef EQT_HAS_GRAPHICS

#include <irrlicht.h>
#include "client/willeq_database.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"

namespace {

class CastAnimationEntity : public ::testing::Test {
protected:
    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EntityRenderer> ents_;
    std::string eqClientPath_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);
        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') eqClientPath_ += '/';
        if (!fileExists(eqClientPath_ + "global_chr.s3d")) GTEST_SKIP() << "no client data at " << eqClientPath_;
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        ents_ = std::make_unique<EntityRenderer>(device_->getSceneManager(), device_->getVideoDriver(),
                                                 device_->getFileSystem());
        ents_->setClientPath(eqClientPath_);
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        ents_->setConstrainedTextureCache(textureCache_.get());
        if (!ents_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
    }

    void TearDown() override {
        ents_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The B1130 oracle's caster: a Dark Elf female (race 6, gender 1).
    const EntityVisual* build(uint16_t spawn, bool isPlayer) {
        if (!ents_->registerEntity(spawn, 6, isPlayer ? "Burst caster" : "Other caster", 0.0f, 0.0f, 0.0f,
                                   0.0f, isPlayer, /*gender=*/1, EntityAppearance(), /*npc=*/!isPlayer,
                                   /*corpse=*/false, /*serverSize=*/6.0f, /*level=*/10))
            return nullptr;
        if (!ents_->buildEntityMesh(spawn)) return nullptr;
        const auto it = ents_->getEntities().find(spawn);
        return it == ents_->getEntities().end() ? nullptr : &it->second;
    }

    const EntityVisual* find(uint16_t spawn) const {
        const auto it = ents_->getEntities().find(spawn);
        return it == ents_->getEntities().end() ? nullptr : &it->second;
    }

    void walk(float seconds) {
        for (float t = 0.0f; t + 1e-4f < seconds; t += 0.05f) ents_->updateMainThreadEntityState(0.05f);
    }
};

// The db's word: all three re-gated spells carry CastingAnim 44 (t06) and
// their cast times -- the start-time side of the assertion (the track starts
// AT the packet, for the cast's duration; 0 = unknown would leave the
// model's current animation standing).
TEST_F(CastAnimationEntity, TheThreeReGatedSpellsCarryCastingAnim44) {
    for (const uint32_t id : {93u, 38u, 54u}) {
        const auto info = db_.getSpell(id);
        ASSERT_EQ(info.id, id) << "the row is in the shipped db";
        EXPECT_EQ(info.castAnim, 44) << "spell " << id << ": the db's field 120";
        EXPECT_GT(info.castTimeMs, 0u) << "spell " << id << " is a channeled cast";
    }
}

// OP_BeginCast for each of 93/38/54: startEntityCast (the bridge's body) selects
// the track the spell's CastingAnim names on this model (44 -> t06), starts
// it AT the packet (progress ~0 at the return, not after a delay), and holds
// it through the channel past the track's own length.
TEST_F(CastAnimationEntity, BeginCastSelectsTheSpellTrackAndStartsItAtThePacket) {
    uint16_t spawn = 5101;
    for (const uint32_t id : {93u, 38u, 54u}) {
        const auto info = db_.getSpell(id);
        ASSERT_EQ(info.id, id);
        const auto* v = build(spawn, false);
        ASSERT_NE(v, nullptr) << "the Dark Elf female built (spell " << id << ")";
        ASSERT_NE(v->animatedNode, nullptr);

        ents_->startEntityCast(spawn, id, info.name, info.castTimeMs, static_cast<int>(info.castAnim));
        const auto* c = find(spawn);
        ASSERT_NE(c, nullptr);
        ASSERT_TRUE(c->isCasting);
        EXPECT_EQ(c->castAnimCode, "t06") << "spell " << id << ": CastingAnim 44 names t06 on this model";
        // The track starts AT the packet: the animator is already on it, at
        // its first frame.
        EXPECT_EQ(c->animatedNode->getAnimator().getCurrentAnimation(), "t06") << "spell " << id;
        EXPECT_LT(c->animatedNode->getAnimator().getProgress(), 0.05f) << "no delay past BeginCast (spell " << id
                                                                        << ")";
        // Held through the channel: the t06 track of the Dark Elf female is
        // ~4.3 s (B1145's fit); a cast of ~1.5 s leaves the model on the
        // track well past its end, holding (or looping, per the model's own
        // flag) -- the raised hands, not a return to idle.
        walk(4.6f);
        const auto* mid = find(spawn);
        ASSERT_NE(mid, nullptr);
        EXPECT_TRUE(mid->isCasting) << "the cast state outlives the walk (spell " << id << ")";
        EXPECT_EQ(mid->animatedNode->getAnimator().getCurrentAnimation(), "t06")
            << "the cast pose is held past the track's length (spell " << id << ")";
        ents_->completeEntityCast(spawn);
        ++spawn;
    }
}

// OP_Action: completeEntityCast ends the state and the pose; the model takes
// the standing caster's idle (o01 for an NPC).
TEST_F(CastAnimationEntity, OPActionEndsTheTrackAndTakesTheIdle) {
    const auto* v = build(5110, false);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(v->animatedNode, nullptr);
    ents_->startEntityCast(5110, 93, "Burst of Fire", 950, 44);
    walk(0.3f);
    ASSERT_EQ(find(5110)->animatedNode->getAnimator().getCurrentAnimation(), "t06");
    ents_->completeEntityCast(5110);
    const auto* c = find(5110);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->isCasting);
    EXPECT_TRUE(c->castAnimCode.empty());
    EXPECT_EQ(c->animatedNode->getAnimator().getCurrentAnimation(), "o01") << "the standing idle";
}

// Interrupt: cancelEntityCast ends the state and the pose the same way.
TEST_F(CastAnimationEntity, InterruptEndsTheTrackTheSameWay) {
    build(5111, false);
    ASSERT_NE(find(5111), nullptr);
    ents_->startEntityCast(5111, 54, "Frost of Winter", 1500, 44);
    walk(0.2f);
    ASSERT_EQ(find(5111)->animatedNode->getAnimator().getCurrentAnimation(), "t06");
    ents_->cancelEntityCast(5111);
    const auto* c = find(5111);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->isCasting);
    EXPECT_TRUE(c->castAnimCode.empty());
    EXPECT_EQ(c->animatedNode->getAnimator().getCurrentAnimation(), "o01") << "the standing idle";
}

// The player is INCLUDED (pre-B1157 startEntityCast returned early for her):
// her model plays the same track, and her end takes the player idle (p01).
TEST_F(CastAnimationEntity, ThePlayerCastsTheSameTrack) {
    const auto* v = build(5112, /*isPlayer=*/true);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(v->animatedNode, nullptr);
    ents_->startEntityCast(5112, 38, "Frost of Winter", 1500, 44);
    const auto* c = find(5112);
    ASSERT_NE(c, nullptr);
    ASSERT_TRUE(c->isCasting);
    EXPECT_EQ(c->castAnimCode, "t06") << "the player's model plays the spell's track";
    EXPECT_EQ(c->animatedNode->getAnimator().getCurrentAnimation(), "t06");
    walk(0.2f);
    ents_->completeEntityCast(5112);
    const auto* p = find(5112);
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->isCasting);
    EXPECT_TRUE(p->castAnimCode.empty());
    if (p->animatedNode->hasAnimation("p01"))
        EXPECT_EQ(p->animatedNode->getAnimator().getCurrentAnimation(), "p01") << "the player idle";
}

// B1174 (#493): a /disc -- OP_Action with no OP_BeginCast -- plays the
// spell's CastingAnim on its standing source at the packet (8604 Leechcurse
// Discipline: 44 = t06, what the lane-5 observer saw from Act+0.42), and the
// model returns to the standing idle once the one-shot track has played out
// (the observer's caster is back in its idle pose at Act+4.36).
TEST_F(CastAnimationEntity, ADiscActionSwingsTheIdleSourceThenReturnsToIdle) {
    const auto info = db_.getSpell(8604);
    ASSERT_EQ(info.id, 8604u) << "the row is in the shipped db";
    EXPECT_EQ(info.castAnim, 44) << "8604's field 120";
    build(5120, false);
    ASSERT_NE(find(5120), nullptr);
    EXPECT_EQ(info.targetType, 6) << "8604's field 98 (self)";
    EQ::ActionCastAnimInputs in = Disc8604();
    in.targetType = static_cast<uint8_t>(info.targetType);
    EXPECT_EQ(ents_->actionEntityCastAnim(5120, static_cast<int>(info.castAnim), in), "t06");
    const auto* c = find(5120);
    ASSERT_NE(c, nullptr);
    EXPECT_FALSE(c->isCasting) << "an OP_Action starts no cast state";
    EXPECT_EQ(c->actionAnimCode, "t06");
    EXPECT_EQ(c->animatedNode->getAnimator().getCurrentAnimation(), "t06");
    EXPECT_LT(c->animatedNode->getAnimator().getProgress(), 0.05f) << "the swing starts AT the packet";
    // walk() runs only the per-frame state; the scene graph's OnAnimate is
    // what advances a model's animator, so step both here.
    irr::u32 ms = 1;
    auto play = [&](float seconds) {
        for (float t = 0.0f; t + 1e-4f < seconds; t += 0.05f) {
            ms += 50;
            find(5120)->animatedNode->OnAnimate(ms);
            ents_->updateMainThreadEntityState(0.05f);
        }
    };
    play(2.0f);
    EXPECT_EQ(find(5120)->animatedNode->getAnimator().getCurrentAnimation(), "t06") << "mid-swing";
    play(3.0f);  // past the Dark Elf female's 4100 ms t06 (B1148)
    const auto* e = find(5120);
    EXPECT_TRUE(e->actionAnimCode.empty());
    EXPECT_EQ(e->animatedNode->getAnimator().getCurrentAnimation(), "o01") << "the standing idle returns";
}

// A source still in its BeginCast state already plays the track (the
// client's same-id no-op): the action request changes nothing and B1157's
// completeEntityCast still ends the pose. A Bard at instrument_mod 10 is not
// asked at all.
TEST_F(CastAnimationEntity, AnActionOnACastingOrBardSourceStartsNothing) {
    build(5121, false);
    ASSERT_NE(find(5121), nullptr);
    ents_->startEntityCast(5121, 93, "Burst of Fire", 950, 44);
    walk(0.3f);
    EQ::ActionCastAnimInputs casting = Disc8604();
    casting.spellId = 93;
    casting.spellAffectIndex = 25;
    casting.sourceClass = 12;
    EXPECT_EQ(ents_->actionEntityCastAnim(5121, 44, casting), "");
    EXPECT_TRUE(find(5121)->actionAnimCode.empty());
    EXPECT_EQ(find(5121)->castAnimCode, "t06") << "the BeginCast track stands";
    ents_->completeEntityCast(5121);
    EXPECT_EQ(find(5121)->animatedNode->getAnimator().getCurrentAnimation(), "o01");

    build(5122, false);
    ASSERT_NE(find(5122), nullptr);
    EQ::ActionCastAnimInputs bard = Disc8604();
    bard.sourceClass = 8;
    EXPECT_EQ(ents_->actionEntityCastAnim(5122, 44, bard), "") << "a Bard at instrument_mod 10";
    EXPECT_NE(find(5122)->animatedNode->getAnimator().getCurrentAnimation(), "t06");
}

// The stand byte and the target type gate the swing too: a looting source
// (the server's AT_Anim 105) is not asked, nor is a sitting one, and a spell
// whose client record has target type 0 (2246 Call of Alarm, CastingAnim 43,
// read from the shipped db) plays nothing on a standing source.
TEST_F(CastAnimationEntity, ALootingSourceOrATargetTypeZeroSpellStartsNothing) {
    build(5123, false);
    ASSERT_NE(find(5123), nullptr);
    EQ::ActionCastAnimInputs looting = Disc8604();
    looting.sourceStandState = EQ::SpawnStandStateAfterAppearance(100, 105);
    EXPECT_EQ(ents_->actionEntityCastAnim(5123, 44, looting), "") << "a looting source";
    EQ::ActionCastAnimInputs sitting = Disc8604();
    sitting.sourceStandState = EQ::SpawnStandStateAfterAppearance(100, 110);
    EXPECT_EQ(ents_->actionEntityCastAnim(5123, 44, sitting), "") << "a sitting source";
    EXPECT_TRUE(find(5123)->actionAnimCode.empty());

    const auto alarm = db_.getSpell(2246);
    ASSERT_EQ(alarm.id, 2246u) << "the row is in the shipped db";
    EXPECT_EQ(alarm.targetType, 0) << "2246's field 98";
    EXPECT_EQ(alarm.castAnim, 43) << "2246's field 120";
    EQ::ActionCastAnimInputs alarmIn = Disc8604();
    alarmIn.spellId = 2246;
    alarmIn.targetType = static_cast<uint8_t>(alarm.targetType);
    EXPECT_EQ(ents_->actionEntityCastAnim(5123, static_cast<int>(alarm.castAnim), alarmIn), "")
        << "target type 0: the handler returns first";
    EXPECT_NE(find(5123)->animatedNode->getAnimator().getCurrentAnimation(), "t05");
}

// A CastingAnim id that names a track the model does not have keeps the
// model's current animation (the client's no-track path, FUN_00450c2f): no
// crash, no state change beyond the cast bookkeeping.
TEST_F(CastAnimationEntity, AModelWithoutTheTrackKeepsItsCurrentAnimation) {
    // "p01" does not exist on every model; find one that lacks a cast track
    // by requesting an id whose code is absent. The Dark Elf female has all
    // of t04/t05/t06, so use a race without the donor (Troll, race 92).
    if (!ents_->registerEntity(5113, 92, "Troll caster", 0.0f, 0.0f, 0.0f, 0.0f, false, 0,
                               EntityAppearance(), true, false, 6.0f, 10) ||
        !ents_->buildEntityMesh(5113))
        GTEST_SKIP() << "the Troll model would not build";
    const auto* t = find(5113);
    ASSERT_NE(t, nullptr);
    ASSERT_NE(t->animatedNode, nullptr);
    const char* code = EQ::SpellCastAnimationCode(44);
    if (!t->animatedNode->hasAnimation(code)) {
        ents_->startEntityCast(5113, 93, "Burst of Fire", 950, 44);
        const auto* c = find(5113);
        ASSERT_NE(c, nullptr);
        ASSERT_TRUE(c->isCasting);
        EXPECT_TRUE(c->castAnimCode.empty()) << "the model has no t06: nothing played";
        ents_->completeEntityCast(5113);
        EXPECT_TRUE(find(5113)->castAnimCode.empty());
    } else {
        GTEST_SKIP() << "this model has t06; the no-track path is covered by castAnimTrackFor(nullptr)";
    }
}

}  // namespace

#endif  // EQT_HAS_GRAPHICS
