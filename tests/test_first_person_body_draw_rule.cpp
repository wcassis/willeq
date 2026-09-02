/**
 * T309 -- THE MEASURED FIRST-PERSON DRAW RULE.
 *
 * T307 measured the official Titanium client and found that in first person it
 * draws the player's own model -- the WHOLE model, with its equipped weapon --
 * for the duration of an ATTACK animation, and at no other time
 * (refactor/T307_official_draw_rule.md, review/T307/README.md). Eight emotes at
 * the very camera pose a sword swing fills gave 2250 consecutive frames at
 * 0.0000% maximum per-frame pixel difference while a third-person control shows
 * those emotes plainly animating; during a KICK the two-handed halberd is drawn
 * in full. So it is neither "any animation" nor "only the animating limb", and
 * it is not the /attack state either -- an out-of-range auto-attack that
 * produces no swing draws nothing.
 *
 * This file pins three things:
 *
 *  1. THE PREDICATE -- which animation codes are attacks, and that the
 *     animator must actually be PLAYING.
 *  2. THE RULE, driven through the real EntityRenderer with real meshes: the
 *     player's model is withheld from the render list at idle and while
 *     emoting, and registered for the duration of an attack.
 *  3. *** THE TRAP THAT WOULD MAKE 1 AND 2 PASS WHILE THE LIVE CLIENT IS
 *     WRONG. *** Irrlicht's ISceneNode::OnAnimate() is wrapped in
 *     `if (IsVisible)`, so hiding by setVisible(false) ships a FROZEN
 *     skeleton -- and under a rule that reveals the model MID-ANIMATION, a
 *     frozen skeleton means the swing is drawn at the wrong phase, or not at
 *     all. The model must keep animating WHILE HIDDEN. Test 3 drives
 *     Irrlicht's own animation path and asserts both halves: withheld keeps
 *     animating, invisible does not. T296 found this the hard way.
 */
#include <gtest/gtest.h>
#include <fstream>
#include <iostream>
#include <string>

#include "client/graphics/first_person_body_draw.h"

using EQT::Graphics::isAttackAnimationCode;
using EQT::Graphics::shouldDrawOwnBodyInFirstPerson;

namespace {

// ---------------------------------------------------------------------------
// 1. The predicate. Pure; no graphics needed.
// ---------------------------------------------------------------------------

TEST(FirstPersonDrawRulePredicate, TheCombatFamilyIsAnAttack) {
    // c01..c11: kick, the weapon swings, throwing, bash, hand-to-hand,
    // archery, swim attack, round kick. T307 measured c01 (the kick) and the
    // halberd swing directly.
    for (const char* code : {"c01", "c02", "c03", "c04", "c05", "c06",
                             "c07", "c08", "c09", "c10", "c11"}) {
        EXPECT_TRUE(isAttackAnimationCode(code)) << code;
    }
    // The monk special attacks, included by the family argument (the
    // combat-animation dispatch in eq_combat_actions.cpp treats them exactly
    // like c01/c11) and NOT by measurement.
    for (const char* code : {"t07", "t08", "t09"}) {
        EXPECT_TRUE(isAttackAnimationCode(code)) << code;
    }
}

TEST(FirstPersonDrawRulePredicate, EmotesAreNotAttacks) {
    // THE DISCRIMINATOR. If these ever read as attacks the predicate is wired
    // to the wrong signal, and the live gate will see the body pop up on
    // /wave -- which the official client provably does not do (2250 frames,
    // 0.0000% difference).
    for (const char* code : {"s01", "s03", "s09", "s16", "s22", "s25", "s28"}) {
        EXPECT_FALSE(isAttackAnimationCode(code)) << code;
    }
}

TEST(FirstPersonDrawRulePredicate, LocomotionPosesIdlesCastingAndDamageAreNotAttacks) {
    // Running must draw nothing (the rule's own prediction, and T307's
    // baseline). Casting (t04-t06) and the damage reactions (d01-d05) are
    // UNMEASURED -- T307 says so itself -- so they are left drawing nothing,
    // which is what fable did before the rule existed. Do not promote them
    // without shooting them.
    for (const char* code : {"l01", "l02", "l03", "l08", "l09",
                             "p01", "p02", "p05", "p07",
                             "o01", "o02", "o03",
                             "t02", "t03", "t04", "t05", "t06",
                             "d01", "d02", "d05"}) {
        EXPECT_FALSE(isAttackAnimationCode(code)) << code;
    }
}

TEST(FirstPersonDrawRulePredicate, GarbageAndOutOfRangeCodesAreNotAttacks) {
    for (const char* code : {"", "c", "c0", "c0a", "c00", "c12", "c99",
                             "t06", "t10", "C01", "xc01", "c01 "}) {
        EXPECT_FALSE(isAttackAnimationCode(code)) << "'" << code << "'";
    }
}

TEST(FirstPersonDrawRulePredicate, TheAnimationMustBeActuallyPlaying) {
    // "The trigger is the animation actually PLAYING, not the /attack state."
    // A held, paused or completed attack code is not a swing.
    EXPECT_TRUE(shouldDrawOwnBodyInFirstPerson("c01", true));
    EXPECT_FALSE(shouldDrawOwnBodyInFirstPerson("c01", false));
    EXPECT_FALSE(shouldDrawOwnBodyInFirstPerson("o01", true));
    EXPECT_FALSE(shouldDrawOwnBodyInFirstPerson("", true));
}

}  // namespace

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/willeq_database.h"
#include <irrlicht.h>

namespace {

// A scene node that counts what Irrlicht does to it. Hung as a CHILD of the
// player's animated node, it reports on the whole subtree the rule governs --
// the body and, with it, the equipped weapon, which is likewise an ordinary
// child of that node (entity_equipment.cpp).
class ProbeSceneNode : public irr::scene::ISceneNode {
public:
    ProbeSceneNode(irr::scene::ISceneNode* parent, irr::scene::ISceneManager* mgr)
        : ISceneNode(parent, mgr, -1) {}

    void OnRegisterSceneNode() override {
        if (IsVisible) {
            ++registrations;
            SceneManager->registerNodeForRendering(this);
        }
        ISceneNode::OnRegisterSceneNode();
    }
    void OnAnimate(irr::u32 timeMs) override {
        ++animations;
        ISceneNode::OnAnimate(timeMs);
    }
    void render() override { ++renders; }
    const irr::core::aabbox3d<irr::f32>& getBoundingBox() const override { return box_; }

    int registrations = 0;
    int renders = 0;
    int animations = 0;

private:
    irr::core::aabbox3d<irr::f32> box_{-1, -1, -1, 1, 1, 1};
};

class FirstPersonBodyDrawRuleTest : public ::testing::Test {
protected:
    std::string eqClientPath_;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::scene::ISceneManager* smgr_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    irr::io::IFileSystem* fs_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::EntityRenderer> entities_;
    EQT::WilleqDatabase willeqDb_;

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
        driver_ = device_->getVideoDriver();
        fs_ = device_->getFileSystem();
        smgr_->addCameraSceneNode();

        entities_ = std::make_unique<EQT::Graphics::EntityRenderer>(smgr_, driver_, fs_);
        entities_->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(driver_);
        entities_->setConstrainedTextureCache(textureCache_.get());
        if (!entities_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
    }

    void TearDown() override {
        entities_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The player node production actually built, found by walking the graph.
    EQT::Graphics::EQAnimatedMeshSceneNode* findPlayerNode() const {
        for (auto* child : smgr_->getRootSceneNode()->getChildren()) {
            auto* anim = dynamic_cast<EQT::Graphics::EQAnimatedMeshSceneNode*>(child);
            if (anim && anim->isPlayerNode()) return anim;
        }
        return nullptr;
    }

    // Run the current play-through animation to completion, the way the frame
    // loop would. It ends by LEAVING THE PLAYING STATE -- see the note in
    // BodyIsWithheldAtRestAndDrawnForTheDurationOfAnAttack; a non-looping
    // animation holds on its last frame and keeps its code.
    static void runAnimationToCompletion(EQT::Graphics::EQAnimatedMeshSceneNode* node) {
        for (int i = 0; i < 400; ++i) {
            if (node->getAnimator().getState() != EQT::Graphics::AnimationState::Playing) return;
            node->getAnimator().update(50.0f);
        }
    }

    EQT::Graphics::EQAnimatedMeshSceneNode* spawnPlayer() {
        constexpr uint16_t kSpawn = 1;
        constexpr uint16_t kHumanMale = 1;
        EQT::Graphics::EntityAppearance appearance;
        if (!entities_->createEntity(kSpawn, kHumanMale, "Testchar",
                                     100.0f, 200.0f, 3.0f, 0.0f,
                                     /*isPlayer=*/true, /*gender=*/0, appearance,
                                     /*isNPC=*/false)) {
            return nullptr;
        }
        entities_->setPlayerSpawnId(kSpawn);
        return findPlayerNode();
    }
};

// ---------------------------------------------------------------------------
// 2. The rule, through the real renderer.
// ---------------------------------------------------------------------------

TEST_F(FirstPersonBodyDrawRuleTest, BodyIsWithheldAtRestAndDrawnForTheDurationOfAnAttack) {
    auto* node = spawnPlayer();
    ASSERT_NE(node, nullptr) << "no player node in the scene graph";

    // Third person: the model is drawn, always. The rule is first-person only,
    // and the visual gate's goldens are third-person renders.
    entities_->setPlayerFirstPersonMode(false);
    entities_->updateFirstPersonBodyDrawRule();
    EXPECT_FALSE(entities_->isPlayerBodyWithheldFromRenderList())
        << "third person must be untouched by the draw rule";

    // First person, at rest: nothing is drawn.
    entities_->setPlayerEntityAnimation("p01", true);
    entities_->setPlayerFirstPersonMode(true);
    entities_->updateFirstPersonBodyDrawRule();
    EXPECT_TRUE(entities_->isPlayerBodyWithheldFromRenderList())
        << "the body must not be drawn at rest";

    // Running: still nothing (the rule's own prediction for the live gate).
    entities_->setPlayerEntityAnimation("l02", true, 23.0f);
    entities_->updateFirstPersonBodyDrawRule();
    EXPECT_TRUE(entities_->isPlayerBodyWithheldFromRenderList())
        << "the body must not be drawn while running";

    // Emoting: still nothing. THE DISCRIMINATOR -- if this fails the predicate
    // is reading "any animation", which T307 killed with 2250 frames.
    if (node->hasAnimation("s03")) {
        entities_->setPlayerEntityAnimation("s03", false, 0.0f, /*playThrough=*/true);
        entities_->updateFirstPersonBodyDrawRule();
        EXPECT_TRUE(entities_->isPlayerBodyWithheldFromRenderList())
            << "an emote must draw nothing -- T307: 2250 frames, 0.0000% difference";
        // Let it finish. A play-through animation REFUSES to be replaced while
        // it runs (SkeletalAnimator::playAnimation queues instead), so an
        // attack requested now would be queued behind the emote -- which is
        // production behaviour, and would make the next step measure nothing.
        runAnimationToCompletion(node);
    }

    // Attacking: the whole model, with its weapon, for the duration.
    ASSERT_TRUE(node->hasAnimation("c01")) << "Human male has no c01";
    entities_->setPlayerEntityAnimation("c01", false, 0.0f, /*playThrough=*/true);
    entities_->updateFirstPersonBodyDrawRule();
    EXPECT_FALSE(entities_->isPlayerBodyWithheldFromRenderList())
        << "the body must be DRAWN while an attack animation plays";

    // ...and hidden again the moment the swing is over.
    //
    // *** THIS IS WHY THE PREDICATE READS THE ANIMATOR'S STATE AND NOT ITS
    // ANIMATION CODE. *** A non-looping animation sets holdOnComplete_
    // (SkeletalAnimator::playAnimation: holdOnComplete_ = !looping_), so when
    // an attack finishes the animator FREEZES ON ITS LAST FRAME and KEEPS THE
    // CODE "c01", merely leaving the Playing state. Production gets back to
    // idle only because renderer_player_physics asks for "p01" every frame
    // once isPlayingThrough() goes false. A predicate that looked only at the
    // code would leave the body drawn permanently after the first swing --
    // for exactly the 77.5% of the time T307 says it must be invisible.
    runAnimationToCompletion(node);
    EXPECT_EQ(node->getAnimator().getCurrentAnimation(), "c01")
        << "the hold-on-last-frame behaviour this assertion documents has "
           "changed; re-read it before trusting the next line";
    EXPECT_NE(node->getAnimator().getState(), EQT::Graphics::AnimationState::Playing)
        << "the attack never ended; the rest of this assertion is meaningless";
    entities_->updateFirstPersonBodyDrawRule();
    EXPECT_TRUE(entities_->isPlayerBodyWithheldFromRenderList())
        << "the body must be hidden again once the swing is over";

    // ...and the ordinary idle that production then asks for is still hidden.
    entities_->setPlayerEntityAnimation("p01", true);
    entities_->updateFirstPersonBodyDrawRule();
    EXPECT_TRUE(entities_->isPlayerBodyWithheldFromRenderList());
}

TEST_F(FirstPersonBodyDrawRuleTest, WithheldMeansTheSubtreeIsNotRegisteredForRendering) {
    // The rule draws the whole model WITH its weapon, or none of it. The
    // equipped weapon is an ordinary child of the player's animated node, so
    // a probe child reports for the subtree.
    auto* node = spawnPlayer();
    ASSERT_NE(node, nullptr);

    auto* probe = new ProbeSceneNode(node, smgr_);
    auto* control = new ProbeSceneNode(smgr_->getRootSceneNode(), smgr_);

    entities_->setPlayerEntityAnimation("p01", true);
    entities_->setPlayerFirstPersonMode(true);
    entities_->updateFirstPersonBodyDrawRule();
    ASSERT_TRUE(entities_->isPlayerBodyWithheldFromRenderList());

    smgr_->drawAll();
    EXPECT_GT(control->registrations, 0) << "drawAll did not run; the rest proves nothing";
    EXPECT_EQ(probe->registrations, 0)
        << "the player's subtree registered for rendering while hidden";
    EXPECT_EQ(probe->renders, 0);
    // ...but Irrlicht still walked it for animation. This is the whole point.
    EXPECT_GT(probe->animations, 0)
        << "the hidden subtree stopped animating -- this is the setVisible() trap";

    const int registrationsWhileHidden = probe->registrations;

    entities_->setPlayerEntityAnimation("c01", false, 0.0f, /*playThrough=*/true);
    entities_->updateFirstPersonBodyDrawRule();
    ASSERT_FALSE(entities_->isPlayerBodyWithheldFromRenderList());
    smgr_->drawAll();
    EXPECT_GT(probe->registrations, registrationsWhileHidden)
        << "the player's subtree was not registered during an attack";

    probe->remove();
    control->remove();
}

// ---------------------------------------------------------------------------
// 3. THE TRAP. This is the test that earns its keep.
// ---------------------------------------------------------------------------

TEST_F(FirstPersonBodyDrawRuleTest, TheModelKeepsAnimatingWhileHiddenAndSetVisibleWouldNot) {
    auto* node = spawnPlayer();
    ASSERT_NE(node, nullptr);

    // The probe stands in for the equipped weapon: like it, an ordinary child
    // of the player's animated node.
    auto* probe = new ProbeSceneNode(node, smgr_);

    entities_->setPlayerFirstPersonMode(true);
    // A LOOPING animation, so it cannot end on its own and confuse the reading.
    ASSERT_TRUE(node->hasAnimation("l02")) << "Human male has no l02";
    entities_->setPlayerEntityAnimation("l02", true, 23.0f);
    entities_->updateFirstPersonBodyDrawRule();

    // FIRST: the hidden node must NOT be invisible. That is the whole
    // mechanism, and it is checked before anything else so that a naive
    // setVisible() implementation is caught by THIS assertion rather than by
    // an incidental precondition below.
    EXPECT_TRUE(node->isVisible())
        << "the draw rule hid the body with setVisible(false); it must WITHHOLD "
           "-- see first_person_body_draw.h";

    ASSERT_TRUE(entities_->isPlayerBodyWithheldFromRenderList())
        << "running must be hidden -- otherwise this test measures nothing";

    // Drive Irrlicht's own animation path -- the root's recursion, which is
    // where the `if (IsVisible)` gate lives. Calling node->OnAnimate() direct
    // would bypass the gate entirely and prove nothing.
    irr::u32 t = 1000;
    auto step = [&](int frames) {
        for (int i = 0; i < frames; ++i) {
            t += 60;
            smgr_->getRootSceneNode()->OnAnimate(t);
        }
    };

    step(1);
    const int frameA = node->getAnimator().getCurrentFrame();
    const int probeA = probe->animations;
    step(8);
    const int frameB = node->getAnimator().getCurrentFrame();
    const int probeB = probe->animations;
    std::cout << "  WITHHELD: animator frame " << frameA << " -> " << frameB
              << ", child animations " << probeA << " -> " << probeB << "\n";
    EXPECT_NE(frameA, frameB)
        << "the WITHHELD model stopped animating. Under this rule the model is "
           "revealed mid-animation, so a frozen skeleton draws the swing at the "
           "wrong phase -- or frozen.";
    EXPECT_GT(probeB, probeA)
        << "the WITHHELD model's children stopped being animated -- the weapon "
           "is one of those children";

    // ---------------------------------------------------------------------
    // THE CONTRAST, and it is not quite the folklore. Irrlicht's
    // ISceneNode::OnAnimate() wraps its body in `if (IsVisible)`, but a
    // PARENT calls its children's OnAnimate() unconditionally -- so what
    // clearing IsVisible on this node actually stops is everything the base
    // implementation does: updateAbsolutePosition(), and the recursion into
    // ITS children. EQAnimatedMeshSceneNode::OnAnimate() advances the
    // per-instance animator BEFORE delegating to the base, so the skeleton
    // itself would keep ticking here.
    //
    // *** THAT IS NOT A LICENCE TO USE setVisible(false). *** What freezes
    // instead is the ATTACHED EQUIPMENT: the weapon is a child node, its
    // OnAnimate() is never reached and its absolute transform is never
    // recomputed, so it would hang in the air at whatever pose it held when
    // the body was hidden and snap on reveal -- which is exactly the artefact
    // this rule cannot afford at a 22.5% duty cycle. It also depends on this
    // class's override ordering, which is not a property anyone should have
    // to preserve. Withholding from the render list has none of these
    // questions: it changes the draw and nothing else.
    // ---------------------------------------------------------------------
    node->setVisible(false);
    const int probeC = probe->animations;
    step(8);
    const int probeD = probe->animations;
    std::cout << "  setVisible(false): child animations " << probeC << " -> " << probeD
              << "  (the attached weapon stops being animated)\n";
    EXPECT_EQ(probeC, probeD)
        << "Irrlicht no longer skips the child recursion on invisible nodes. "
           "Verify that against the Irrlicht in third_party/ before acting on it.";
    node->setVisible(true);

    probe->remove();
}

}  // namespace
#endif  // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
