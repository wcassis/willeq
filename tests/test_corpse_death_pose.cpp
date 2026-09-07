/**
 * T454 (board B473) -- a corpse holds the death pose's LAST frame from the
 * moment it is built, and nothing afterwards un-lies it.
 *
 * THE ORACLE. Two independent measurements say the same thing and neither is
 * an inference:
 *
 *   1. T346 read the official client's own wire answer to its own death
 *      (T311 Leg A's capture, four self-deaths, byte-identical in all four):
 *          C->S OP_SpawnAppearance type=14 (AT_ANIMATION) param=115
 *      115 is ANIM_LYING (include/client/eq_enums.h). The official client's
 *      statement about a dead body's pose is "lying".
 *   2. T431 photographed it. The official client's first-person frame at
 *      qeynos2 (-74, 428) shows character_corpses id 42 as a body lying FLAT
 *      -- a face and a forearm horizontal across the bottom of the frame
 *      (review/T431/probe/T431_sit_dwarf_sitting_h32.png). fable's frame at
 *      the same spot, minute and resolution is filled by an UPRIGHT model
 *      (review/T431/sweeps/human_eye/eyeHeightFractionOfSkull_0.65_/
 *      BEST_frame_000051.png). Board B473.
 *
 * fable renders "lying" as the last frame of the death animation d05, held --
 * playAnimation(d05, loop=false, playThrough=false) then
 * SkeletalAnimator::setToLastFrame(). That is not invented here: it is what
 * entity_mesh_build.cpp and entity_build_step.cpp have always done. What was
 * missing is any test at all: before this file nothing exercised the corpse
 * pose, the d05/d02/d01 ladder or setToLastFrame(), on any path.
 *
 * HOW A CORPSE IS RECOGNISED ON THE WIRE. Spawn_Struct's NPC byte at offset
 * 0083 (include/common/packets/spawn.h:76): 0=player, 1=npc, 2=pc corpse,
 * 3=npc corpse. eqemu writes 2 for EVERY corpse, player or NPC, in
 * /mnt/projects/akk-stack/code/zone/corpse.cpp:1809
 * (Corpse::FillSpawnStruct -- "ns->spawn.NPC = 2"), so the two cases are
 * indistinguishable on the wire and this client must treat them the same.
 * ZoneProcessNewSpawn stores that byte as Entity::npc_type; every consumer
 * derives corpse-ness from it (IsCorpseByFields, and the inline copies in
 * bridge_component.cpp and application_zone_loading.cpp).
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include <irrlicht.h>

#include "client/corpse_predicate.h"
#include "client/eq_enums.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/willeq_database.h"
#include "common/packet_structs.h"

namespace {

// -------------------------------------------------------------------------
// Part 1 -- the wire. Device-free.
// -------------------------------------------------------------------------

TEST(CorpseSpawnWire, TheNpcByteIsWhereTitaniumPutsIt) {
    EXPECT_EQ(offsetof(EQT::Spawn_Struct, NPC), 83u);
}

TEST(CorpseSpawnWire, EqemuSendsNpcTypeTwoForEveryCorpse) {
    // /mnt/projects/akk-stack/code/zone/corpse.cpp:1809 -- ns->spawn.NPC = 2,
    // unconditionally, for a player corpse and an NPC corpse alike.
    EXPECT_TRUE(IsCorpseByFields(false, 2, "Oraclealpha's corpse42"));
    EXPECT_TRUE(IsCorpseByFields(false, 3, "a rat's corpse17"));
    // The negative: a live player and a live NPC are not corpses.
    EXPECT_FALSE(IsCorpseByFields(false, 0, "Oraclealpha"));
    EXPECT_FALSE(IsCorpseByFields(false, 1, "a rat"));
}

TEST(CorpseSpawnWire, LyingIsTheOraclesOwnAnimationValue) {
    // T346: the official client answers its own OP_Death with exactly one
    // OP_SpawnAppearance, AT_ANIMATION param 115.
    EXPECT_EQ(static_cast<int>(ANIM_LYING), 115);
}

// -------------------------------------------------------------------------
// Part 2 -- a real corpse in a real (EDT_NULL) scene graph. Same gate as
// test_entity_size_change: skips without the shipped db or the client tree.
// -------------------------------------------------------------------------

class CorpsePoseTest : public ::testing::Test {
protected:
    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(willeqDb_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&willeqDb_);

        const char* envPath = std::getenv("EQ_CLIENT_PATH");
        eqClientPath_ = envPath ? envPath : EQ_CLIENT_DATA_PATH;
        if (!eqClientPath_.empty() && eqClientPath_.back() != '/') eqClientPath_ += '/';
        if (!fileExists(eqClientPath_ + "global_chr.s3d"))
            GTEST_SKIP() << "no client data at " << eqClientPath_;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        smgr_ = device_->getSceneManager();

        entities_ = std::make_unique<EQT::Graphics::EntityRenderer>(
            smgr_, device_->getVideoDriver(), device_->getFileSystem());
        entities_->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        entities_->setConstrainedTextureCache(textureCache_.get());
        if (!entities_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
    }

    void TearDown() override {
        entities_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The production path: registerEntity() then buildEntityMesh(). The zone
    // load registers every entity this way (application_zone_loading.cpp) and
    // post-load spawns arrive through the EntitySpawned bridge arm, which
    // lands in the same registerEntity().
    const EQT::Graphics::EntityVisual* buildCorpse(uint16_t spawn, bool isCorpse) {
        constexpr uint16_t kHumanMale = 1;
        if (!entities_->registerEntity(spawn, kHumanMale,
                                       isCorpse ? "Oraclealpha's corpse42" : "Oraclealpha",
                                       -72.09f, 427.83f, 3.75f, 0.f,
                                       /*isPlayer=*/false, /*gender=*/0,
                                       EQT::Graphics::EntityAppearance(),
                                       /*isNPC=*/false, isCorpse,
                                       /*serverSize=*/6.0f, /*level=*/10)) {
            return nullptr;
        }
        if (!entities_->buildEntityMesh(spawn)) return nullptr;
        auto it = entities_->getEntities().find(spawn);
        return (it == entities_->getEntities().end()) ? nullptr : &it->second;
    }

    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::EntityRenderer> entities_;
    EQT::WilleqDatabase willeqDb_;
};

// The death pose is the LAST frame of d05, held: state Stopped, progress 1.0.
// Measured on this tree at T454 for a Human male (race 1, gender 0):
// d05 has 13 frames and the held frame is 12.
TEST_F(CorpsePoseTest, ACorpseHoldsTheLastFrameOfTheDeathAnimation) {
    const auto* v = buildCorpse(101, /*isCorpse=*/true);
    ASSERT_NE(v, nullptr) << "the corpse did not build";
    ASSERT_TRUE(v->isCorpse);
    ASSERT_TRUE(v->isAnimated);
    ASSERT_NE(v->animatedNode, nullptr);

    // The ladder picked d05 -- the death animation -- not one of the damage
    // fallbacks, because this model has it.
    EXPECT_TRUE(v->animatedNode->hasAnimation("d05"));
    EXPECT_EQ(v->currentAnimation, "d05");

    const auto& a = v->animatedNode->getAnimator();
    EXPECT_EQ(a.getCurrentAnimation(), "d05");
    // HELD, not playing: this is what makes the body lie still rather than
    // fall over again every few seconds.
    EXPECT_EQ(a.getState(), EQT::Graphics::AnimationState::Stopped);
    EXPECT_FLOAT_EQ(a.getProgress(), 1.0f);
    EXPECT_GT(a.getCurrentFrame(), 0)
        << "frame 0 of d05 is the STANDING pose -- a corpse there is B473";
}

TEST_F(CorpsePoseTest, ALiveEntityOfTheSameRaceIsNotPutInTheDeathPose) {
    const auto* v = buildCorpse(102, /*isCorpse=*/false);
    ASSERT_NE(v, nullptr);
    ASSERT_NE(v->animatedNode, nullptr);
    EXPECT_NE(v->animatedNode->getAnimator().getCurrentAnimation(), "d05");
}

// -------------------------------------------------------------------------
// T454's live defect (board B473).
//
// EntityRenderer::updateEntityAppearance() -- reached from the
// EntityAppearanceChanged bridge arm (src/client/bridge/irrlicht_bridge.cpp)
// -- tears the entity down and rebuilds it, then restores whatever animation
// it was playing with
//
//     setEntityAnimation(spawnId, currentAnimation, /*loop=*/true, false);
//
// For a corpse that animation is "d05", and restoring it as a LOOPING
// animation restarts it at FRAME 0 -- the standing pose -- and then plays the
// character falling over, forever. The rebuild had already put the body in the
// held last frame; the restore undoes it.
//
// Measured before the fix (Human male, race 1):
//   BEFORE: anim='d05' state=Stopped frame=12
//   AFTER : anim='d05' state=Playing frame=0 progress=0.000
// -------------------------------------------------------------------------
TEST_F(CorpsePoseTest, AnAppearanceChangeDoesNotStandTheCorpseBackUp) {
    constexpr uint16_t kSpawn = 301;
    const auto* before = buildCorpse(kSpawn, /*isCorpse=*/true);
    ASSERT_NE(before, nullptr);
    ASSERT_NE(before->animatedNode, nullptr);
    ASSERT_EQ(before->animatedNode->getAnimator().getState(),
              EQT::Graphics::AnimationState::Stopped);
    const int heldFrame = before->animatedNode->getAnimator().getCurrentFrame();
    ASSERT_GT(heldFrame, 0);

    entities_->updateEntityAppearance(kSpawn, /*raceId=*/1, /*gender=*/0,
                                      EQT::Graphics::EntityAppearance());

    auto it = entities_->getEntities().find(kSpawn);
    ASSERT_NE(it, entities_->getEntities().end());
    const auto& after = it->second;
    ASSERT_TRUE(after.isCorpse);
    ASSERT_NE(after.animatedNode, nullptr);
    const auto& a = after.animatedNode->getAnimator();

    EXPECT_EQ(a.getCurrentAnimation(), "d05");
    EXPECT_EQ(a.getState(), EQT::Graphics::AnimationState::Stopped)
        << "the corpse is animating again -- it stands up and falls over on a loop";
    EXPECT_EQ(a.getCurrentFrame(), heldFrame)
        << "the corpse went back to frame 0 of d05, which is the STANDING pose";
    EXPECT_FLOAT_EQ(a.getProgress(), 1.0f);
}

}  // namespace
