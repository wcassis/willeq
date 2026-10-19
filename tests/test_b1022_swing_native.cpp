/**
 * B1022 (#342) -- the swing plays at the model's NATIVE animation length;
 * the swing RATE is the server's OP_Animation cadence (100 x item delay ms).
 *
 * THE ORACLE. The deployed eqemu's own zone logs (T455's lane-2 capture, run
 * 1302, refactor/B1022/README.md) show every melee OP_Animation body as
 * `06 00 0A 05` / `AF 00 0A 08`: wire layout
 * [2-3]=spawn_id, [4]=animation_speed (uint8), [5]=animation_id (uint8).
 * The speed byte is 10 (0x0A) for EVERY swing, constant across both spawns
 * and both anim ids (1.0 = 100% per the server's `animation_speed ?
 * animation_speed : 10` convention, attack.cpp DoAnim). A constant speed
 * byte cannot carry the weapon delay, so the official client's swing PERIOD
 * is the server's attack timer (100 x item delay ms) = the spacing between
 * consecutive OP_Animation packets, and the per-swing animation plays at
 * one fixed rate -- the model's own animation length, because 1.0x means
 * no speed change.
 *
 * The per-swing NATIVE length is a property of the model, measured
 * headlessly (tools/b1022_anim_probe, rbuild): animationTimeMs = frameMs x
 * frames (s3d_loader_skinning.cpp:340); all humanoid males (incl. HUM) use
 * the ELM elf-male donor (animation_mapping.cpp):
 *   c06 1H slash = 1000 ms, c02 1H pierce = 800 ms, c01 kick = 900,
 *   c03 2H slash = 1300, c04 2H blunt = 1200, c05 throw = 1000,
 *   c07 bash = 1800, c08 hand = 900, c09 archery = 1100, c10 swim = 1200,
 *   c11 round-kick = 1200.
 *
 * The DEFECT: fable stretched the attack animation to
 * weaponDelayMs * 0.5 (1.5 s at the 3000 ms default) in
 * entity_updates.cpp:612-613 and recomputed a delay-based duration in the
 * combat buffer (entity_combat_anim.cpp), fed by the dead writer
 * setEntityWeaponDelay (B494 zero-caller census). That stretched the swing
 * to a fixed 1.5 s regardless of the model's real length and of the weapon
 * the oracle says is irrelevant to per-swing duration.
 *
 * THIS TEST. Two halves. The source half (no graphics needed, runs in every
 * build) asserts the stretch is gone from the production paths -- it is
 * RED on the parent (which still has `setTargetDuration(weaponDelayMs *
 * 0.5f)`) and GREEN at the head. The graphics half (skips without the db /
 * client tree) drives the real animation-5 update path and asserts the
 * animator's playback speed is the native 1.0, not the parent's 2.0
 * (3000*0.5 = 1500 target over the model's native length).
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "common/willeq_paths.h"

namespace {

// The one expression that IS the defect: stretching the attack animation to
// half the (dead-default) weapon delay. Absent after the B1022 fix.
const std::string kWeaponDelayStretch = "weaponDelayMs * 0.5f";
const std::string kWeaponDelayStretch2 = "weaponDelayMs*0.5f";

std::string ReadCodeOnly(const std::string& path) {
    std::ifstream f(path);
    if (!f.good()) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Every production TU that the parent routed the swing through. The source
// half scans all of src/graphics for the stretch; that is the regression net
// that stays green after the fix and red if anyone re-adds a delay-based
// swing duration.
std::vector<std::string> GraphicsSources() {
    std::vector<std::string> out;
    // The source-tree root, resolved at run time (willeq_paths.txt in the
    // build tree, or the checkout marker) -- absolute, no CWD assumption.
    const std::string root = eqt::paths::RepoRoot();
    const char* dirs[] = {
        "src/client/graphics/entity_updates.cpp",
        "src/client/graphics/entity_combat_anim.cpp",
        "src/client/graphics/entity_renderer.cpp",
    };
    for (const auto* d : dirs) out.push_back(root + "/" + d);
    return out;
}

}  // namespace

// The regression net: the delay-based swing stretch must not exist in any of
// the three production paths. RED on the parent (entity_updates.cpp still has
// `setTargetDuration(visual.weaponDelayMs * 0.5f)`), GREEN at the head.
TEST(B1022SwingNative, NoDelayBasedSwingStretchInAnyProductionPath) {
    const auto files = GraphicsSources();
    ASSERT_FALSE(files.empty()) << "no production sources resolved (REPO_ROOT unset?)";
    std::string offenders;
    int scanned = 0;
    for (const auto& p : files) {
        // The resolved path is relative to the build's tests/ dir (../../)
        // or absolute (REPO_ROOT); match on the tail, not a leading slash.
        if (p.find("src/client/graphics/") == std::string::npos) continue;
        const std::string src = ReadCodeOnly(p);
        if (src.empty()) continue;
        ++scanned;
        if (src.find(kWeaponDelayStretch) != std::string::npos ||
            src.find(kWeaponDelayStretch2) != std::string::npos) {
            offenders += "\n  " + p;
        }
    }
    ASSERT_GE(scanned, 3) << "the census scanned almost nothing -- it is broken, not clean";
    EXPECT_TRUE(offenders.empty())
        << "B1022 (#342): a production path still stretches the swing to "
           "weaponDelayMs*0.5 (1.5 s at the 3000 ms default). The oracle's "
           "speed byte is constant 10 (1.0x) -- the swing plays at the "
           "model's native animation length; the rate is the server's "
           "OP_Animation cadence (100 x delay ms), never a per-entity "
           "weapon delay. Sites:" << offenders;
}

#ifdef EQT_HAS_GRAPHICS

#include <irrlicht.h>
#include "client/willeq_database.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/entity_renderer.h"

namespace {

// The behavioral half. Mirrors test_corpse_death_pose's device-free setup:
// an EDT_NULL device, a real EntityRenderer, a real (remote-player) entity
// that the animation-5 update path drives. After a melee (animation == 5)
// update, the animator's playback speed must be the native 1.0 -- the model
// plays its own animation length. The parent instead calls
// setTargetDuration(weaponDelayMs*0.5) = setTargetDuration(1500), which sets
// playbackSpeed_ = native/1500 != 1.0 for a c06 1H slash (native 1000 ms ->
// speed 0.667) and leaves it at the stretched rate. We assert 1.0.
class B1022SwingBehavior : public ::testing::Test {
protected:
    static bool fileExists(const std::string& p) { std::ifstream f(p); return f.good(); }

    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::EntityRenderer> ents_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

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
        auto* smgr = device_->getSceneManager();

        ents_ = std::make_unique<EQT::Graphics::EntityRenderer>(
            smgr, device_->getVideoDriver(), device_->getFileSystem());
        ents_->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
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

    // A remote (non-local) humanoid player, animated, with a 1H slash skill --
    // the exact entity the animation-5 melee path drives. Not a corpse, not
    // the local player (which the update path skips), not an NPC (whose
    // animation field is a speed, not an id).
    const EQT::Graphics::EntityVisual* buildRemotePlayer(uint16_t spawn) {
        constexpr uint16_t kHumanMale = 1;
        if (!ents_->registerEntity(spawn, kHumanMale, "SwingOracle",
                                   0.0f, 0.0f, 0.0f, 0.0f,
                                   /*isPlayer=*/false, /*gender=*/0,
                                   EQT::Graphics::EntityAppearance(),
                                   /*isNPC=*/false, /*isCorpse=*/false,
                                   /*serverSize=*/6.0f, /*level=*/10)) {
            return nullptr;
        }
        ents_->setEntityWeaponSkills(spawn, /*primaryWeaponSkill=*/0,
                                     /*secondaryWeaponSkill=*/255);
        if (!ents_->buildEntityMesh(spawn)) return nullptr;
        auto it = ents_->getEntities().find(spawn);
        return (it == ents_->getEntities().end()) ? nullptr : &it->second;
    }

    std::string eqClientPath_;
};

// A melee (animation == 5) update leaves the animator at its NATIVE playback
// speed 1.0 -- the model plays its own animation length. RED on the parent
// (which stretches to weaponDelayMs*0.5 = 1500 ms, a non-1.0 speed), GREEN at
// the head.
TEST_F(B1022SwingBehavior, MeleeSwingPlaysAtNativeLength) {
    constexpr uint16_t kSpawn = 4242;
    const auto* v = buildRemotePlayer(kSpawn);
    ASSERT_NE(v, nullptr) << "the remote player entity did not build";
    ASSERT_NE(v->animatedNode, nullptr) << "the entity has no animation node";

    // A melee swing from the server: updateEntity(..., animation = 5), then
    // flush so the animation-5 branch runs.
    ents_->updateEntity(kSpawn, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, /*animation=*/5);
    ents_->flushPendingUpdatesForAnimations();

    // Re-read the live entity (updateEntity mutates it).
    auto it = ents_->getEntities().find(kSpawn);
    ASSERT_NE(it, ents_->getEntities().end());
    auto& live = it->second;
    ASSERT_NE(live.animatedNode, nullptr);

    const float speed = live.animatedNode->getAnimator().getPlaybackSpeed();
    // The oracle: speed byte 10 = 1.0x = the model's native animation length.
    // The parent sets this to nativeLength/1500 (e.g. 1000/1500 = 0.667 for a
    // c06 1H slash), never 1.0.
    EXPECT_FLOAT_EQ(speed, 1.0f)
        << "B1022 (#342): the melee swing's playback speed is " << speed
        << ", not the native 1.0. The oracle's OP_Animation speed byte is "
           "constant 10 (1.0x) for every swing -- the model plays its own "
           "animation length and the swing RATE is the server's packet "
           "cadence (100 x delay ms), not a per-entity weapon-delay stretch.";
}

}  // namespace
#endif  // EQT_HAS_GRAPHICS
