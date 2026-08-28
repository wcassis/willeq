/**
 * T301 Leg C -- the held weapon must sit in the same place relative to the
 * FIRST-PERSON CAMERA whatever way the player is facing.
 *
 * T300 Leg C's live gate captured 285 first-person frames covering seven
 * auto-attack damage exchanges with a two-handed Halberd and found ZERO
 * weapon geometry, while the same script at the same capture rate in
 * `/camera follow` showed the swing plainly. The cause is not the camera and
 * not the animation:
 *
 *   EQAnimatedMeshSceneNode::applyAnimation() BAKES a Y rotation of
 *   (nodeRot.Y + 90) degrees into the PLAYER's per-instance vertices
 *   (animated_mesh_scene_node.cpp:734-741) and
 *   EQAnimatedMeshSceneNode::render() then draws that node with a
 *   TRANSLATION-ONLY world transform (:776-786), deliberately dropping the
 *   node's own rotation and scale. Equipment, though, is an ordinary CHILD
 *   scene node of that same animated node (entity_equipment.cpp's
 *   attachEquipment()), so it still inherits R(nodeRot.Y) and the scale.
 *   Body and weapon therefore end up in two different frames, (-2*nodeRot.Y
 *   - 90) degrees apart -- 90 degrees at heading 0, and zero only near
 *   heading 45. In third person the weapon is still on screen (misplaced,
 *   and swinging round the player at twice his turn rate); in first person,
 *   where the frustum is 62.8 degrees wide and centred on the body's
 *   forward, it lands outside the frustum at nearly every heading and is
 *   never seen.
 *
 * NPCs bake nothing and draw through their full AbsoluteTransformation, so
 * their equipment was always right -- which is why this survived so long.
 *
 * WHAT THIS TEST DRIVES: the real EntityRenderer, the real createEntity()/
 * setPlayerSpawnId()/updateMainThreadEntityState() sequence production uses,
 * real S3D character and equipment meshes, and the real first-person camera
 * formula out of first_person_camera.h. The equipment node is found by
 * WALKING THE SCENE GRAPH, not by re-deriving where it ought to be, and no
 * placement math is duplicated here.
 *
 * HONEST LIMIT: this does not rasterize. It measures where the weapon is in
 * the camera's frame. It cannot prove the weapon is VISIBLE -- whether a
 * swing enters the frustum also depends on the eye height (T301 Leg B) and
 * on the pitch clamp. The live video gate (Leg E) is the only thing that can
 * judge that.
 */
#include <gtest/gtest.h>
#include <fstream>
#include <cmath>
#include <iostream>
#include <vector>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/first_person_camera.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/willeq_database.h"
#include <irrlicht.h>

namespace {

class FirstPersonWeaponAttachmentTest : public ::testing::Test {
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

        entities_ = std::make_unique<EQT::Graphics::EntityRenderer>(smgr_, driver_, fs_);
        entities_->setClientPath(eqClientPath_);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(driver_);
        entities_->setConstrainedTextureCache(textureCache_.get());
        if (!entities_->loadGlobalCharacters()) GTEST_SKIP() << "global_chr.s3d would not load";
        entities_->loadEquipmentModels();
    }

    void TearDown() override {
        entities_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The player's animated node, found in the scene graph (the renderer
    // exposes no accessor -- and finding it this way is the point: it is the
    // node production actually built).
    EQT::Graphics::EQAnimatedMeshSceneNode* findPlayerNode() const {
        for (auto* child : smgr_->getRootSceneNode()->getChildren()) {
            auto* anim = dynamic_cast<EQT::Graphics::EQAnimatedMeshSceneNode*>(child);
            if (anim && anim->isPlayerNode()) return anim;
        }
        return nullptr;
    }

    // The equipment child of that node: a plain IMeshSceneNode. (The name tag
    // is an ITextSceneNode, so a mesh-node test is enough to tell them apart.)
    static irr::scene::ISceneNode* findEquipChild(irr::scene::ISceneNode* parent) {
        for (auto* child : parent->getChildren()) {
            if (dynamic_cast<irr::scene::IMeshSceneNode*>(child)) return child;
        }
        return nullptr;
    }
};

// Model ids tried in order; the first one whose mesh loads out of the
// client's gequip archives is used. Direct IT model ids, the same form NPC
// spawn packets carry (EquipmentModelLoader::getEquipmentMesh() falls
// through to them when the item-id mapping has no row).
const uint32_t kWeaponModelIds[] = {6, 10, 15, 16, 63};

TEST_F(FirstPersonWeaponAttachmentTest, HeldWeaponIsHeadingInvariantInTheCameraFrame) {
    using namespace EQT::Graphics;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr uint16_t kSpawn = 1;
    constexpr uint16_t kHumanMale = 1;
    const float px = 100.0f, py = 200.0f, pz = 3.0f;   // EQ x, y, z

    // Pick a weapon model that this client tree actually has.
    uint32_t modelId = 0;
    for (uint32_t candidate : kWeaponModelIds) {
        EntityAppearance appearance;
        appearance.equipment[7] = candidate;
        entities_->clearEntities();
        ASSERT_TRUE(entities_->createEntity(kSpawn, kHumanMale, "Testchar", px, py, pz, 0.0f,
                                            /*isPlayer=*/true, /*gender=*/0, appearance,
                                            /*isNPC=*/false));
        entities_->setPlayerSpawnId(kSpawn);
        auto* node = findPlayerNode();
        if (node && findEquipChild(node)) { modelId = candidate; break; }
    }
    if (modelId == 0) GTEST_SKIP() << "no equipment model out of gequip*.s3d would load";

    struct Sample { float headingDeg, right, up, forward; };
    std::vector<Sample> samples;

    for (float headingDeg : {0.0f, 45.0f, 90.0f, 180.0f, 270.0f}) {
        EntityAppearance appearance;
        appearance.equipment[7] = modelId;
        entities_->clearEntities();
        ASSERT_TRUE(entities_->createEntity(kSpawn, kHumanMale, "Testchar", px, py, pz, headingDeg,
                                            /*isPlayer=*/true, /*gender=*/0, appearance,
                                            /*isNPC=*/false));
        entities_->setPlayerSpawnId(kSpawn);
        // c01 is the primary attack (the animation T300's halberd exchanges played).
        entities_->setPlayerEntityAnimation("c01", true);

        auto* node = findPlayerNode();
        ASSERT_NE(node, nullptr) << "no player node in the scene graph";
        node->OnAnimate(200);
        // The production per-frame equipment placement, called exactly as
        // IrrlichtRenderer calls it.
        entities_->updateMainThreadEntityState(0.016f);

        auto* equip = findEquipChild(node);
        ASSERT_NE(equip, nullptr) << "player has no equipment child node";
        node->updateAbsolutePosition();
        equip->updateAbsolutePosition();
        const irr::core::vector3df weapon = equip->getAbsoluteTransformation().getTranslation();

        // The real first-person camera, from the one formula, called exactly
        // as IrrlichtRenderer::updateFirstPersonCamera() calls it. MERGE NOTE
        // (fable, 2026-08-28): T301 Leg B replaced
        // getPlayerEyeHeightAboveNodeOrigin() with the per-race
        // getPlayerFirstPersonEyeOffset() and dropped the unused
        // verticalFovRadians parameter from computeFirstPersonCamera(); this
        // call site is mirrored onto the new production call verbatim. The
        // property under test is unchanged.
        const auto eyeOffset = entities_->getPlayerFirstPersonEyeOffset();
        const float headingUnits = headingDeg * 512.0f / 360.0f;
        const FirstPersonCameraPlacement cam = computeFirstPersonCamera(
            px, py, pz, eyeOffset.irrY,
            /*eyeAdjustUnits=*/0.0f, headingUnits, /*lookPitchDegrees=*/0.0f,
            /*targetDistance=*/100.0f, eyeOffset.irrX, eyeOffset.irrZ);

        irr::core::vector3df eye(cam.posX, cam.posY, cam.posZ);
        irr::core::vector3df fwd(cam.targetX - cam.posX, 0.0f, cam.targetZ - cam.posZ);
        fwd.normalize();
        const irr::core::vector3df right(fwd.Z, 0.0f, -fwd.X);  // fwd x up(0,1,0)
        const irr::core::vector3df rel = weapon - eye;

        Sample s{headingDeg, rel.dotProduct(right), rel.Y, rel.dotProduct(fwd)};
        samples.push_back(s);
        std::cout << "  heading " << headingDeg << " deg: weapon in the camera's frame -- right "
                  << s.right << ", up " << s.up << ", forward " << s.forward
                  << "  (yaw off the look axis "
                  << (std::atan2(s.right, s.forward) * 180.0f / kPi) << " deg)\n";
    }

    ASSERT_GE(samples.size(), 2u);
    const Sample& ref = samples.front();
    for (const Sample& s : samples) {
        // THE PROPERTY: the player's own weapon does not move relative to his
        // own eyes when he turns on the spot. Before T301 Leg C's fix this
        // failed at every heading but 45 degrees, by up to 2x the arm's reach
        // (the weapon swung a full 180 degrees around him between heading 0
        // and heading 90) -- and that is why first person never showed it.
        EXPECT_NEAR(s.right, ref.right, 0.05f) << "heading " << s.headingDeg;
        EXPECT_NEAR(s.up, ref.up, 0.05f) << "heading " << s.headingDeg;
        EXPECT_NEAR(s.forward, ref.forward, 0.05f) << "heading " << s.headingDeg;
    }

    // ...and it is held IN FRONT OF THE EYE, on the right, where a right hand
    // is -- not behind the head, which is where heading 0 used to put it.
    //
    // T309 KEPT THIS ASSERTION DELIBERATELY, AND IT IS NOW THE CANARY FOR THE
    // DRAW RULE, not merely a sanity check on Leg C's attachment fix.
    //
    // The merge of T301 Leg B and Leg C left it FAILING at -0.1527: Leg B had
    // pushed the eye 1.0 unit forward along the look ray (net +0.83 for a
    // Human male) to keep the player's own face out of frame, which carried
    // the eye past the grip. T309 did NOT delete the assertion to get green.
    // T307 measured that the official client DRAWS the weapon for the whole
    // duration of a swing, so a fable eye placement that leaves the weapon
    // behind the camera at the attack frame is refuted by the official
    // client's own pixels -- which makes this the cheapest automated guard
    // there is on the rule's premise. The eye offset was re-decided against
    // that constraint instead (kEyeForwardOffsetUnits: 1.0 -> 0.0; the
    // argument is on the constant in first_person_eye.h).
    //
    // So: if this fails again, the eye has moved forward past the hand and
    // the draw rule will reveal an empty view mid-swing. Fix the offset, do
    // not relax the assertion. The margin is printed so a future change can
    // see how much room it has.
    std::cout << "  DRAW-RULE CANARY: weapon grip is " << ref.forward
              << " units in FRONT of the first-person eye at the c01 attack "
                 "frame (must stay > 0; T307: the official client draws the "
                 "weapon for the whole swing)\n";
    EXPECT_GT(ref.forward, 0.0f) << "the weapon is BEHIND the first-person camera";
    EXPECT_LT(std::fabs(ref.right), 2.5f) << "the weapon is out beside the player, not in his hand";
}

}  // namespace
#endif  // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
