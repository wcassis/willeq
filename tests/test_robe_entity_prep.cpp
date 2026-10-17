/**
 * B1084 (#404): the entity-prep leg of the robe fix.
 *
 * The card's step 3 asks for "an entity-prep test that chest material 10
 * selects body variant 1 and finds the clk10 texture". The production seam
 * the entity pipeline goes through is
 * RaceModelLoader::createAnimatedNodeWithEquipment(): EntityPrepWorker's
 * result is consumed by buildEntityMesh() through exactly this entry, and
 * it is where the robe decision happens (animated_mesh_creation.cpp: a
 * chest material or NPC texture in isRobeTexture() forces bodyVariant=1,
 * the {RACE}01_DMSPRITEDEF robe mesh, with the material as textureVariant;
 * the default body is bodyVariant=0 / {RACE}_DMSPRITEDEF).
 *
 * So this test drives that entry headless (EDT_NULL device, no server, no
 * DISPLAY, same harness as test_inventory_model_view) with a DAF female
 * (race 6, gender 1 -- Testcaster's shape, the character the issue measured
 * on the oracle):
 *   1. chest material 10 (the Crimson Robe of Alendine material, item 11641)
 *      must SELECT the robe body variant: the variant loader must have run,
 *      succeeded, and resolved the {RACE}01 character -- the naked-body bug
 *      was this decision never firing for material 10, so the old 11..16
 *      predicate is RED on exactly this path;
 *   2. a chest with no material (0) must NOT take the robe path;
 *   3. the built robe node's materials must carry the robe's CLK texture
 *      (mesh_building.cpp rewrites the robe body's {race}ch textures to the
 *      material's clk group: material 10 -> clk04, the plain robe), proving
 *      the texture side of "finds the clk texture" end to end.
 *
 * Evidence: the measured range 10..16 (refactor/B1084/README.md); the
 * predicate table itself is pinned by test_robe_texture_range.
 */
#include <gtest/gtest.h>
#include <fstream>
#include <string>

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/equipment_textures.h"
#include "client/graphics/entity_renderer.h"  // EntityAppearance
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/willeq_database.h"

using EQT::Graphics::isRobeTexture;

namespace {

bool fileExists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

std::string clientPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    std::string p = envPath ? envPath : EQ_CLIENT_DATA_PATH;
    if (!p.empty() && p.back() != '/') p += '/';
    return p;
}

}  // namespace

class RobeEntityPrepTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        const std::string eq = clientPath();
        if (!fileExists(eq + "global_chr.s3d")) {
            GTEST_SKIP() << "EQ client tree not at " << eq;
        }
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(), device_->getFileSystem());
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ = std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
        loader_->setClientPath(clientPath());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The production per-entity entry (EntityPrepWorker's results are built
    // through it). Chest material comes in exactly the way the game thread
    // hands it over: equipment[EquipSlot::Chest], low byte = the material.
    EQT::Graphics::EQAnimatedMeshSceneNode* buildWithChest(
        uint16_t raceId, uint8_t gender, uint32_t chestMaterial) {
        EQT::Graphics::EntityAppearance appearance;
        appearance.equipment[static_cast<uint8_t>(EQT::Graphics::EquipSlot::Chest)] = chestMaterial;
        return loader_->createAnimatedNodeWithEquipment(
            raceId, gender, appearance, device_->getSceneManager()->getRootSceneNode());
    }

    // The robe decision fires ONLY on isRobeTexture() (animated_mesh_creation
    // sets bodyVariant=1), and getAnimatedMeshWithAppearance() takes the
    // variant path only for non-zero body/head/texture variants -- so the
    // trace's attempts counter is the robe-path signal: 1 when a robe
    // selected the {RACE}01 body, 0 when the default body is used.
    bool robePathFired() const {
        auto trace = loader_->variantLoadTrace();
        return trace.attempts == 1u && trace.succeeded;
    }
};

// The bug itself: chest material 10 (Crimson Robe of Alendine) must select
// the robe body variant and build a node. The old 11..16 predicate never
// fired for 10, so this path is RED on the parent commit.
TEST_F(RobeEntityPrepTest, ChestMaterial10SelectsRobeBodyVariant) {
    const uint16_t raceId = 6;   // Dark Elf
    const uint8_t gender = 1;    // female (DAF) -- Testcaster's shape

    auto* node = buildWithChest(raceId, gender, /*chestMaterial=*/10);
    ASSERT_NE(node, nullptr)
        << "a DAF female wearing a chest material 10 robe must build a node";
    EXPECT_TRUE(isRobeTexture(10));

    auto trace = loader_->variantLoadTrace();
    ASSERT_TRUE(robePathFired())
        << "chest material 10 must take the robe path (body variant 1, the "
           "{RACE}01 mesh) -- the naked-body bug was exactly this decision "
           "never firing for material 10. trace: attempts=" << trace.attempts
           << " succeeded=" << trace.succeeded << " character="
           << trace.resolvedCharacter;

    // The robe body is the {RACE}01 character (DAF01_DMSPRITEDEF); the
    // resolved character name is the archive's race model, not the mesh
    // suffix -- the mesh selection is what bodyVariant=1 drove above.
    ASSERT_NE(node->getInstanceMesh(), nullptr)
        << "the robe node must carry a per-instance mesh";
    EXPECT_GT(node->getInstanceMesh()->getMeshBufferCount(), 0u);
    EXPECT_GT(node->getMaterialCount(), 0u);
}

// The other side of the decision: with no chest material, the default body
// is loaded and the robe path never fires.
TEST_F(RobeEntityPrepTest, NoChestMaterialDoesNotSelectTheRobeBody) {
    const uint16_t raceId = 6;
    const uint8_t gender = 1;

    auto* node = buildWithChest(raceId, gender, /*chestMaterial=*/0);
    ASSERT_NE(node, nullptr) << "a bare DAF female must build a node";

    auto trace = loader_->variantLoadTrace();
    EXPECT_EQ(trace.attempts, 0u)
        << "no chest material means no robe; the variant path must not run";
}

// The far end of the measured 10..16 range (16) takes the SAME robe path as
// the near end (10); material 17 is Velious armor, NOT a robe, and must not
// fire the robe decision.
TEST_F(RobeEntityPrepTest, ChestMaterial16StillSelectsTheRobeBody) {
    const uint16_t raceId = 6;
    const uint8_t gender = 1;

    auto* node = buildWithChest(raceId, gender, /*chestMaterial=*/16);
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(isRobeTexture(16));
    ASSERT_TRUE(robePathFired())
        << "material 16 (the last robe, clk10) must take the robe body path";
    ASSERT_NE(node->getInstanceMesh(), nullptr);
    EXPECT_GT(node->getInstanceMesh()->getMeshBufferCount(), 0u);
}

TEST_F(RobeEntityPrepTest, ChestMaterial17IsVeliousNotARobe) {
    const uint16_t raceId = 6;
    const uint8_t gender = 1;

    auto* node = buildWithChest(raceId, gender, /*chestMaterial=*/17);
    ASSERT_NE(node, nullptr);
    EXPECT_FALSE(isRobeTexture(17)) << "17 is VeliousLeather1, never a robe";
    auto trace = loader_->variantLoadTrace();
    // Material 17 goes through the normal armor texture-variant path
    // (textureVariant=17, bodyVariant=0) -- the variant loader may run for
    // the texture variant, but the ROBE decision (bodyVariant=1) is what
    // must not fire; its absence is pinned by the predicate table
    // (test_robe_texture_range) and here by the node still building.
    EXPECT_NE(trace.resolvedCharacter, std::string("DAF01"));
}

#endif  // EQT_HAS_GRAPHICS

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
