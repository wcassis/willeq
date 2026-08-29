/**
 * T310 Leg B -- collisionZOffset is the ORIGIN-TO-FEET distance, not half the
 * bounding box.
 *
 * THE DEFECT (class-A item 9 on refactor/deferred.md): every site that filled
 * EntityVisual::collisionZOffset computed `(bboxHeight / 2.0f) * scale`.
 * The value is consumed as "the distance from the node origin down to the
 * mesh's feet" -- entity_renderer.cpp snaps an NPC with
 * `targetZ = groundZ + collisionZOffset`, and renderer_player_physics.cpp
 * snaps the player the same way -- so it is correct only for a mesh whose
 * origin happens to sit exactly halfway up it. No EQ mesh does. A Human male
 * spans Y = [-3.05, +3.65] about its origin: origin-to-feet is 3.05, half the
 * height is 3.35, and every entity in the world therefore stood ~0.30 above
 * its own collision ground -- carrying the first-person eye up with it.
 *
 * HONEST LIMIT: nothing here rasterizes. These are geometry assertions over
 * the shipped meshes plus arithmetic on the helper. They pin the formula and
 * they report the numbers; the visual gate and T310 Leg C judge the pixels.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "client/graphics/entity_renderer.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/willeq_database.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#endif

using EQT::Graphics::collisionZOffsetFromBounds;

namespace {

irr::core::aabbox3df boxY(float minY, float maxY) {
    return irr::core::aabbox3df(-1.0f, minY, -1.0f, 1.0f, maxY, 1.0f);
}

}  // namespace

// =========================================================================
// Part 1 -- the formula itself. Pure arithmetic, no device, no assets.
// =========================================================================

TEST(CollisionZOffset, IsTheDistanceFromTheOriginDownToTheLowestPoint) {
    // A Human male's real span. The old form returned half the height.
    EXPECT_FLOAT_EQ(collisionZOffsetFromBounds(boxY(-3.05f, 3.65f), 1.0f), 3.05f);
}

TEST(CollisionZOffset, IsNotHalfTheHeightWheneverTheOriginIsOffCentre) {
    const irr::core::aabbox3df human = boxY(-3.05f, 3.65f);
    const float halfHeight = (human.MaxEdge.Y - human.MinEdge.Y) / 2.0f;
    EXPECT_NEAR(halfHeight - collisionZOffsetFromBounds(human, 1.0f), 0.30f, 0.005f)
        << "the whole defect in one number: the old form stood the entity this "
           "far above its own collision ground";
}

TEST(CollisionZOffset, ScalesWithTheNode) {
    EXPECT_FLOAT_EQ(collisionZOffsetFromBounds(boxY(-3.05f, 3.65f), 2.0f), 6.10f);
    EXPECT_FLOAT_EQ(collisionZOffsetFromBounds(boxY(-3.05f, 3.65f), 0.5f), 1.525f);
}

TEST(CollisionZOffset, IsZeroForAMeshAuthoredOnItsBase) {
    // The placeholder cube: createPlaceholderMesh() spans Y = [0, size], so
    // its origin IS its feet and it must not be lifted at all. The old
    // `cubeSize / 2` floated every placeholder by half its height.
    EXPECT_FLOAT_EQ(collisionZOffsetFromBounds(boxY(0.0f, 6.0f), 1.0f), 0.0f);
}

TEST(CollisionZOffset, SnappingWithItPutsTheFeetExactlyOnTheGround) {
    const irr::core::aabbox3df human = boxY(-3.05f, 3.65f);
    const float scale = 1.3f;
    const float groundZ = -17.25f;
    const float nodeZ = groundZ + collisionZOffsetFromBounds(human, scale);
    const float feetZ = nodeZ + human.MinEdge.Y * scale;
    EXPECT_NEAR(feetZ, groundZ, 1e-4f);
}

// =========================================================================
// Part 2 -- against the REAL shipped meshes. Skips without the client tree.
// =========================================================================

#ifdef EQT_HAS_GRAPHICS

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

class ShippedMeshCollisionZTest : public ::testing::Test {
protected:
    EQT::WilleqDatabase db_;
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<EQT::Graphics::ConstrainedTextureCache> textureCache_;
    std::unique_ptr<EQT::Graphics::RaceModelLoader> loader_;

    void SetUp() override {
        const std::string dbPath = WILLEQ_DB_REAL_PATH;
        if (!fileExists(dbPath)) GTEST_SKIP() << "no shipped db at " << dbPath;
        ASSERT_TRUE(db_.open(dbPath)) << dbPath;
        EQT::WilleqDatabase::setGlobalInstance(&db_);

        const std::string eq = clientPath();
        if (!fileExists(eq + "global_chr.s3d")) {
            GTEST_SKIP() << "EQ client tree not at " << eq;
        }
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";

        loader_ = std::make_unique<EQT::Graphics::RaceModelLoader>(
            device_->getSceneManager(), device_->getVideoDriver(),
            device_->getFileSystem());
        loader_->setClientPath(eq);
        auto config = EQT::Graphics::ConstrainedRendererConfig::fromPreset(
            EQT::Graphics::ConstrainedRenderingPreset::TNT);
        textureCache_ =
            std::make_unique<EQT::Graphics::ConstrainedTextureCache>(config);
        textureCache_->setDriver(device_->getVideoDriver());
        loader_->setConstrainedTextureCache(textureCache_.get());
        loader_->getMeshBuilder()->setConstrainedTextureCache(textureCache_.get());
    }

    void TearDown() override {
        loader_.reset();
        textureCache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
        EQT::WilleqDatabase::setGlobalInstance(nullptr);
    }

    // The mesh's own vertical extent about its origin, in model space -- the
    // same span production reads out of the scene node's bounding box.
    bool extent(uint16_t raceId, uint8_t gender, float& minZ, float& maxZ) {
        auto md = loader_->ensureModelDataLoaded(raceId, gender);
        if (!md || !md->combinedGeometry ||
            md->combinedGeometry->vertices.empty()) {
            return false;
        }
        minZ = 1e9f;
        maxZ = -1e9f;
        for (const auto& v : md->combinedGeometry->vertices) {
            if (v.z < minZ) minZ = v.z;
            if (v.z > maxZ) maxZ = v.z;
        }
        return maxZ > minZ;
    }
};

struct RaceRow { uint16_t raceId; uint8_t gender; const char* label; };

const RaceRow kRows[] = {
    {1, 0, "Human male"},      {1, 1, "Human female"},
    {2, 0, "Barbarian male"},  {8, 0, "Dwarf male"},
    {9, 0, "Troll male"},      {10, 0, "Ogre male"},
    {11, 0, "Halfling male"},  {12, 0, "Gnome male"},
    {128, 0, "Iksar male"},    {130, 0, "Vah Shir male"},
};

}  // namespace

TEST_F(ShippedMeshCollisionZTest, NoShippedMeshHasItsOriginAtItsMidpoint) {
    // If any mesh did, the old half-height form would have been right for it.
    // The table is the measurement: it reports, per race, exactly how far the
    // old form lifted that race off its own ground.
    std::printf("\n%-18s %8s %8s %8s %8s %8s\n", "race", "minY", "maxY", "H",
                "feet", "old-new");
    int covered = 0;
    for (const auto& r : kRows) {
        float minZ = 0.0f, maxZ = 0.0f;
        if (!extent(r.raceId, r.gender, minZ, maxZ)) continue;
        ++covered;
        const irr::core::aabbox3df bbox(-1.0f, minZ, -1.0f, 1.0f, maxZ, 1.0f);
        const float H = maxZ - minZ;
        const float corrected = collisionZOffsetFromBounds(bbox, 1.0f);
        const float old = H / 2.0f;
        std::printf("%-18s %8.3f %8.3f %8.3f %8.3f %8.3f\n", r.label, minZ,
                    maxZ, H, corrected, old - corrected);
        EXPECT_GT(old - corrected, 0.01f)
            << r.label
            << ": the old half-height form lifted this race off its ground";
        EXPECT_GT(corrected, 0.0f)
            << r.label << ": the origin must sit above the mesh's lowest point";
        EXPECT_LT(corrected, H)
            << r.label << ": the origin must sit below the mesh's crown";
    }
    EXPECT_GE(covered, 5) << "shipped meshes did not load";
}

TEST_F(ShippedMeshCollisionZTest, HumanMaleMatchesTheDocumentedThreeOhFive) {
    // The number quoted throughout entity_renderer.h and first_person_eye.h.
    float minZ = 0.0f, maxZ = 0.0f;
    if (!extent(1, 0, minZ, maxZ)) GTEST_SKIP() << "Human male unavailable";
    const irr::core::aabbox3df bbox(-1.0f, minZ, -1.0f, 1.0f, maxZ, 1.0f);
    EXPECT_NEAR(collisionZOffsetFromBounds(bbox, 1.0f), 3.05f, 0.10f);
    EXPECT_NEAR((maxZ - minZ) / 2.0f, 3.35f, 0.10f);
}

#endif  // EQT_HAS_GRAPHICS
