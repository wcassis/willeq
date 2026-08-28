/**
 * T301 Leg B -- the first-person eye is inside the head, and it is derived
 * per race from the mesh.
 *
 * *** HONEST LIMIT, STATED ONCE AND LOUDLY: NOTHING IN THIS FILE RASTERIZES.
 * *** These are geometry assertions over the real shipped S3D meshes. No GPU
 * is involved, no frame is drawn, no pixel is read. They CANNOT settle
 * whether the player sees his own face, whether the head bobs into frame, or
 * whether the weapon is visible. T296 shipped a viewmodel pass whose depth
 * clear was silently compiled out and every headless test in this repo still
 * passed. T301 LEG E IS THE LIVE GATE AND IT DECIDES.
 *
 * What these tests CAN do, and are for:
 *   - pin that a head anchor exists and resolves for every shipped playable
 *     race/gender, so the T295 `headBoneIdx=-1` silent fallback cannot return;
 *   - pin that the eye lands INSIDE the head rather than on the crown;
 *   - pin that it is derived from THE MESH, per race, with no table;
 *   - measure, against the crown eye T300 shipped, how much of the player's
 *     own body the change removes from the down-frustum -- which is a
 *     direction-of-improvement claim, not a "the body is hidden" claim.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/first_person_eye.h"
#include "client/graphics/first_person_camera.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/willeq_database.h"
#include "client/graphics/eq/race_model_loader.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#endif

using EQT::Graphics::BoneVertexRun;
using EQT::Graphics::HeadVertexCluster;
using EQT::Graphics::accumulateHeadCluster;
using EQT::Graphics::computeEyeInHeadBoneSpace;
using EQT::Graphics::findHeadBoneIndex;
using EQT::Graphics::isAttachmentPointBoneName;
using EQT::Graphics::kEyeHeightFractionOfSkull;
using EQT::Graphics::skullLowerBoundLocalZ;

// =========================================================================
// Part 1 -- name resolution. No client tree, no device: these are the exact
// bone names read out of the shipped archives, pasted in as literals.
// =========================================================================

TEST(FirstPersonEyeAnchor, HeadBoneIsNotCalledHEAD) {
    // T76 added `getBoneIndexByName("HEAD")` and T295 logged
    // `[FP-EYE] source=BBOX_FALLBACK headBoneIdx=-1` in every live session.
    // T300 concluded the lever was disabled. It was not: NO EQ SKELETON HAS
    // EVER CONTAINED A BONE CALLED "HEAD". The lookup was misspelled, and the
    // whole-body bbox fallback it fell through to is the defect this unit
    // exists to fix. Re-enabling the old spelling would have changed nothing.
    const std::vector<std::string> human = {
        "hum", "humpe", "humth_r", "humca_r", "humbo_r", "humto_r", "humth_l",
        "humca_l", "humbo_l", "humto_l", "humch", "humbi_r", "humfo_r",
        "humfi_r", "humfi_r2", "humr_point", "humne", "humhe",
        "humhead_point", "humbi_l", "humfo_l", "humfi_l", "humfi_l2",
        "huml_point", "humshield_point"};
    for (const auto& name : human) {
        EXPECT_NE(name, "HEAD");
        EXPECT_NE(name, "head");
    }
    EXPECT_EQ(findHeadBoneIndex("HUM", human), 17) << "humhe is the head";
}

TEST(FirstPersonEyeAnchor, AttachmentPointsAreRejected) {
    // `<code>head_point` hangs a helm. It begins with `<code>he`, it is not
    // the head, and it carries no geometry -- taking it would put the eye at
    // an empty transform.
    EXPECT_TRUE(isAttachmentPointBoneName("humhead_point"));
    EXPECT_TRUE(isAttachmentPointBoneName("frmhehead_point"));
    EXPECT_TRUE(isAttachmentPointBoneName("humshield_point"));
    EXPECT_FALSE(isAttachmentPointBoneName("humhe"));
    EXPECT_FALSE(isAttachmentPointBoneName("frmhehead"));

    // With ONLY attachment points available there is no head, and the caller
    // must be told so rather than handed a helm hanger.
    const std::vector<std::string> pointsOnly = {"hum", "humhead_point",
                                                 "humshield_point"};
    EXPECT_EQ(findHeadBoneIndex("HUM", pointsOnly), -1);
}

TEST(FirstPersonEyeAnchor, ShortestRemainderWinsSoTheSecondHeadPartLoses) {
    // Barbarian male carries `bamhe2` alongside `bamhe`.
    const std::vector<std::string> barbarian = {"bam", "bamne", "bamhe",
                                                "bamhe2", "bamguild_point",
                                                "bamhead_point"};
    EXPECT_EQ(findHeadBoneIndex("BAM", barbarian), 2) << "bamhe, not bamhe2";
}

TEST(FirstPersonEyeAnchor, LuclinFroglokUsesTheLongerHeheadName) {
    // The playable Froglok (race 330) is a Luclin-era skeleton with 128 bones
    // and a different naming scheme: `frmhehead`, next to a `frmhead_point`
    // AND a `frmhehead_point`, both of which must lose.
    const std::vector<std::string> froglok = {
        "frm", "frmpebip01", "frmchchest", "frmneneck01", "frmhehead",
        "frmjaw01", "frmhead_point", "frmhehead_point"};
    EXPECT_EQ(findHeadBoneIndex("FRM", froglok), 4);
}

TEST(FirstPersonEyeAnchor, MatchIsCaseInsensitive) {
    const std::vector<std::string> mixed = {"TRM", "TRMPE", "TRMCH", "TRMNE",
                                            "TRMHE", "TRMHEAD_POINT"};
    EXPECT_EQ(findHeadBoneIndex("trm", mixed), 4);
    EXPECT_EQ(findHeadBoneIndex("TRM", mixed), 4);
}

// =========================================================================
// Part 2 -- the eye within a head cluster. Pure geometry.
// =========================================================================

namespace {
HeadVertexCluster makeCluster(float minZ, float maxZ) {
    HeadVertexCluster c;
    c.count = 100;
    c.minX = -0.5f; c.maxX = 0.5f;
    c.minY = -0.5f; c.maxY = 0.5f;
    c.minZ = minZ;  c.maxZ = maxZ;
    c.centroidX = 0.0f;
    c.centroidY = 0.0f;
    return c;
}
}  // namespace

TEST(FirstPersonEyeGeometry, EyeIsBetweenChinAndCrownAndAtNeither) {
    // The whole point: not the crown (that was the defect) and not the chin.
    const HeadVertexCluster head = makeCluster(-0.57f, 0.69f);  // Human male
    const auto eye = computeEyeInHeadBoneSpace(head);
    EXPECT_GT(eye.z, head.minZ) << "the eye must not be at the chin";
    EXPECT_LT(eye.z, head.maxZ) << "THE EYE MUST NOT BE AT THE CROWN -- that is "
                                   "exactly what T300 shipped";
    // And a real margin, not a rounding-error's worth.
    EXPECT_GT(head.maxZ - eye.z, 0.1f * (head.maxZ - head.minZ));
}

TEST(FirstPersonEyeGeometry, ABeardIsNotMistakenForTheSkull) {
    // Dwarf male: `dwmhe` owns the beard, which hangs to local Z -2.29 while
    // the crown is at +0.85. A naive min..max fraction put the "eye" 2.27
    // below the crown -- in his chest. The lower bound is symmetrised about
    // the head JOINT (local Z 0) instead.
    const HeadVertexCluster dwarf = makeCluster(-2.29f, 0.85f);
    EXPECT_FLOAT_EQ(skullLowerBoundLocalZ(dwarf), -0.85f)
        << "the beard must be discarded, not averaged in";

    const auto eye = computeEyeInHeadBoneSpace(dwarf);
    EXPECT_GT(eye.z, 0.0f)
        << "a symmetrised skull puts a 0.55 eye above the head joint";

    // The number this rule was written to kill: with the raw minimum the eye
    // lands at -0.563 in bone space, which on the real dwmhe transform is
    // model Z 1.48 -- 2.27 below the crown, in his chest.
    const float naive = dwarf.minZ + kEyeHeightFractionOfSkull * (dwarf.maxZ - dwarf.minZ);
    EXPECT_NEAR(naive, -0.563f, 0.01f) << "the naive value this test exists to reject";
    EXPECT_GT(eye.z - naive, 0.6f)
        << "the beard is still dragging the eye down the skull";

    // A cluster that is ALREADY tighter than symmetric is left alone: this
    // rule must not shrink the Troll, Iksar or Froglok head.
    const HeadVertexCluster troll = makeCluster(-0.53f, 1.11f);
    EXPECT_FLOAT_EQ(skullLowerBoundLocalZ(troll), -0.53f);
    const HeadVertexCluster iksar = makeCluster(0.01f, 0.74f);
    EXPECT_FLOAT_EQ(skullLowerBoundLocalZ(iksar), 0.01f);
}

TEST(FirstPersonEyeGeometry, EyeTracksTheClusterRatherThanBeingAConstant) {
    // "Derived from the mesh" has to mean the value MOVES when the mesh does.
    const auto small = computeEyeInHeadBoneSpace(makeCluster(-0.30f, 0.40f));
    const auto large = computeEyeInHeadBoneSpace(makeCluster(-0.90f, 1.20f));
    EXPECT_NE(small.z, large.z);
    EXPECT_LT(small.z, large.z);

    // The horizontal component is the cluster's own centroid -- which is how
    // a snouted race's eye ends up ahead of its origin without a table.
    HeadVertexCluster snout = makeCluster(-0.30f, 0.40f);
    snout.centroidX = 1.48f;   // Froglok
    EXPECT_FLOAT_EQ(computeEyeInHeadBoneSpace(snout).x, 1.48f);
}

TEST(FirstPersonEyeGeometry, AnEmptyClusterYieldsNoAnchorRatherThanZero) {
    HeadVertexCluster empty;
    EXPECT_FALSE(empty.valid());
    const auto eye = computeEyeInHeadBoneSpace(empty);
    EXPECT_FLOAT_EQ(eye.x, 0.0f);
    EXPECT_FLOAT_EQ(eye.y, 0.0f);
    EXPECT_FLOAT_EQ(eye.z, 0.0f);
    // The caller (EQAnimatedMeshSceneNode::getFirstPersonEyeOffset) checks
    // valid() and refuses the anchor; this pins that an invalid cluster does
    // not silently look like a legitimate eye at the node origin.
}

TEST(FirstPersonEyeGeometry, ClusterScanHonoursTheVertexPieceRuns) {
    // accumulateHeadCluster() walks the EQ vertex-piece table, which is a run
    // encoding, not a per-vertex bone index. Getting the walk wrong reads
    // another limb's vertices as the head -- which is what a whole-body bbox
    // effectively did.
    const float verts[] = {
        0.0f, 0.0f, -9.0f,   // run 0, bone 3  (a foot)
        1.0f, 0.0f, -9.0f,
        0.0f, 0.0f,  1.0f,   // run 1, bone 7  (the head)
        0.5f, 0.2f,  2.0f,
        0.0f, 0.0f, 50.0f,   // run 2, bone 3  (must NOT be picked up)
    };
    const std::vector<BoneVertexRun> runs = {{3, 2}, {7, 2}, {3, 1}};
    const auto head = accumulateHeadCluster(runs, 7, verts, 5, 3);
    ASSERT_TRUE(head.valid());
    EXPECT_EQ(head.count, 2u);
    EXPECT_FLOAT_EQ(head.minZ, 1.0f);
    EXPECT_FLOAT_EQ(head.maxZ, 2.0f);
    EXPECT_FLOAT_EQ(head.centroidX, 0.25f);
    EXPECT_FLOAT_EQ(head.centroidY, 0.1f);
}

// =========================================================================
// Part 3 -- against the REAL shipped meshes. Skips without the client tree.
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

struct RaceCase {
    uint16_t raceId;
    uint8_t gender;
    const char* label;
};

// Every playable race, both genders. Troll, Ogre and Iksar are the harshest
// live cases (T300 Leg C) and Froglok is the only Luclin-era skeleton here.
const RaceCase kPlayableRaces[] = {
    {1, 0, "Human male"},      {1, 1, "Human female"},
    {2, 0, "Barbarian male"},  {2, 1, "Barbarian female"},
    {3, 0, "Erudite male"},    {3, 1, "Erudite female"},
    {4, 0, "Wood Elf male"},   {4, 1, "Wood Elf female"},
    {5, 0, "High Elf male"},   {5, 1, "High Elf female"},
    {6, 0, "Dark Elf male"},   {6, 1, "Dark Elf female"},
    {7, 0, "Half Elf male"},   {7, 1, "Half Elf female"},
    {8, 0, "Dwarf male"},      {8, 1, "Dwarf female"},
    {9, 0, "Troll male"},      {9, 1, "Troll female"},
    {10, 0, "Ogre male"},      {10, 1, "Ogre female"},
    {11, 0, "Halfling male"},  {11, 1, "Halfling female"},
    {12, 0, "Gnome male"},     {12, 1, "Gnome female"},
    {128, 0, "Iksar male"},    {128, 1, "Iksar female"},
    {130, 0, "Vah Shir male"}, {130, 1, "Vah Shir female"},
    {330, 0, "Froglok male"},  {330, 1, "Froglok female"},
};

class ShippedMeshEyeTest : public ::testing::Test {
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

    // Everything the assertions below need about one race, computed exactly
    // the way production computes it: the same findHeadBoneIndex(), the same
    // accumulateHeadCluster(), the same computeEyeInHeadBoneSpace(), and the
    // skeleton's own REST bone matrix to lift the result into model space.
    struct Derived {
        bool ok = false;
        int headBone = -1;
        std::string headBoneName;
        HeadVertexCluster cluster;
        float eyeModelX = 0.0f, eyeModelY = 0.0f, eyeModelZ = 0.0f;
        float headModelMinZ = 0.0f, headModelMaxZ = 0.0f;
        float crownZ = 0.0f;   // whole-body bbox top: what T300 used as "the eye"
    };

    Derived derive(const RaceCase& rc) {
        Derived d;
        auto md = loader_->ensureModelDataLoaded(rc.raceId, rc.gender);
        if (!md || !md->skeleton || !md->rawGeometry || md->vertexPieces.empty()) {
            return d;
        }

        std::vector<std::string> names;
        for (const auto& b : md->skeleton->bones) names.push_back(b.name);
        d.headBone = findHeadBoneIndex(md->skeleton->modelCode, names);
        if (d.headBone < 0) return d;
        d.headBoneName = md->skeleton->bones[d.headBone].name;

        std::vector<BoneVertexRun> runs;
        for (const auto& p : md->vertexPieces) runs.push_back({p.boneIndex, p.count});
        d.cluster = accumulateHeadCluster(
            runs, d.headBone, &md->rawGeometry->vertices[0].x,
            md->rawGeometry->vertices.size(),
            sizeof(md->rawGeometry->vertices[0]) / sizeof(float));
        if (!d.cluster.valid()) return d;

        const auto eyeLocal = computeEyeInHeadBoneSpace(d.cluster);

        // A never-updated animator holds the bind pose, which is the frame
        // the raw vertices are stored in.
        EQT::Graphics::SkeletalAnimator rest;
        rest.setSkeleton(md->skeleton);
        const auto& states = rest.getBoneStates();
        if (d.headBone >= static_cast<int>(states.size())) return d;
        const auto& xform = states[d.headBone].worldTransform;

        d.eyeModelX = eyeLocal.x; d.eyeModelY = eyeLocal.y; d.eyeModelZ = eyeLocal.z;
        xform.transformPoint(d.eyeModelX, d.eyeModelY, d.eyeModelZ);

        // The head's own extent in MODEL space, from its skinned vertices --
        // the containment test has to be in the same frame as the eye.
        std::size_t vi = 0;
        d.headModelMinZ = 1e9f;
        d.headModelMaxZ = -1e9f;
        d.crownZ = -1e9f;
        const auto& cv = md->combinedGeometry->vertices;
        for (const auto& p : md->vertexPieces) {
            for (unsigned k = 0; k < p.count && vi < cv.size(); ++k, ++vi) {
                if (cv[vi].z > d.crownZ) d.crownZ = cv[vi].z;
                if (p.boneIndex != d.headBone) continue;
                d.headModelMinZ = std::min(d.headModelMinZ, cv[vi].z);
                d.headModelMaxZ = std::max(d.headModelMaxZ, cv[vi].z);
            }
        }
        for (; vi < cv.size(); ++vi) {
            if (cv[vi].z > d.crownZ) d.crownZ = cv[vi].z;
        }
        d.ok = (d.headModelMaxZ > d.headModelMinZ);
        return d;
    }
};

}  // namespace

TEST_F(ShippedMeshEyeTest, HeadAnchorResolvesForEveryShippedPlayableRace) {
    // THE FINDING THIS PINS: 30 of 30 race/gender combinations name their head
    // bone `<modelCode>he...` and every one of them carries real skinned
    // geometry on it. If this ever fails, the bbox-crown fallback silently
    // takes over and the T300 defect is back -- which is precisely how it
    // survived from T76 to T301.
    int covered = 0;
    for (const auto& rc : kPlayableRaces) {
        auto md = loader_->ensureModelDataLoaded(rc.raceId, rc.gender);
        if (!md || !md->skeleton) continue;   // archive absent in this tree
        ++covered;

        std::vector<std::string> names;
        for (const auto& b : md->skeleton->bones) names.push_back(b.name);
        const int head = findHeadBoneIndex(md->skeleton->modelCode, names);
        ASSERT_GE(head, 0) << rc.label << " (" << md->skeleton->modelCode
                           << "): no head bone resolved -- the eye would fall "
                              "back to the whole-body bbox crown";
        EXPECT_FALSE(isAttachmentPointBoneName(names[head]))
            << rc.label << ": resolved to the attachment point " << names[head];

        std::vector<BoneVertexRun> runs;
        for (const auto& p : md->vertexPieces) runs.push_back({p.boneIndex, p.count});
        const auto cluster = accumulateHeadCluster(
            runs, head, &md->rawGeometry->vertices[0].x,
            md->rawGeometry->vertices.size(),
            sizeof(md->rawGeometry->vertices[0]) / sizeof(float));
        EXPECT_TRUE(cluster.valid())
            << rc.label << ": head bone " << names[head] << " has no geometry";
        EXPECT_GT(cluster.count, 20u) << rc.label << ": suspiciously small head";
    }
    EXPECT_GE(covered, 20) << "too few race archives present to prove anything";
}

TEST_F(ShippedMeshEyeTest, EyeIsInsideTheHeadAndWellBelowTheCrown) {
    // THE UNIT'S CENTRAL ASSERTION. T300 put the camera at the whole-body
    // bbox crown; this pins that the eye is now strictly inside the head's own
    // vertical extent and a real distance below the crown for every race.
    int covered = 0;
    std::printf("\n%-20s %-12s %8s %8s %8s %8s\n", "race", "headbone", "eyeZ",
                "headMinZ", "headMaxZ", "crownZ");
    for (const auto& rc : kPlayableRaces) {
        const Derived d = derive(rc);
        if (!d.ok) continue;
        ++covered;
        std::printf("%-20s %-12s %8.3f %8.3f %8.3f %8.3f\n", rc.label,
                    d.headBoneName.c_str(), d.eyeModelZ, d.headModelMinZ,
                    d.headModelMaxZ, d.crownZ);

        EXPECT_LT(d.eyeModelZ, d.crownZ)
            << rc.label << ": THE EYE IS AT OR ABOVE THE CROWN -- that is the "
                           "T300 defect, see include/client/graphics/"
                           "first_person_eye.h";
        EXPECT_GT(d.crownZ - d.eyeModelZ, 0.15f)
            << rc.label << ": the eye is only " << (d.crownZ - d.eyeModelZ)
            << " below the crown, which is not meaningfully inside the head";
        EXPECT_GE(d.eyeModelZ, d.headModelMinZ)
            << rc.label << ": the eye dropped out of the bottom of the head";
        EXPECT_LE(d.eyeModelZ, d.headModelMaxZ)
            << rc.label << ": the eye is above the top of the head";
    }
    EXPECT_GE(covered, 20);
}

TEST_F(ShippedMeshEyeTest, EyeIsDerivedPerRaceFromTheMeshNotFromATable) {
    // Two independent ways of saying "no table":
    //  1. the value differs between races, and differs the way the MESHES do;
    //  2. it is NOT the whole-body bbox crown for any race, which is the one
    //     value a table would most plausibly have been seeded with.
    std::vector<float> eyes;
    std::vector<float> crowns;
    for (const auto& rc : kPlayableRaces) {
        const Derived d = derive(rc);
        if (!d.ok) continue;
        eyes.push_back(d.eyeModelZ);
        crowns.push_back(d.crownZ);
        EXPECT_NE(d.eyeModelZ, d.crownZ) << rc.label;

        // It tracks the head cluster: recomputing from the cluster reproduces
        // it exactly, so nothing else is feeding the number.
        const auto again = computeEyeInHeadBoneSpace(d.cluster);
        EXPECT_FLOAT_EQ(again.z,
                        skullLowerBoundLocalZ(d.cluster) +
                            kEyeHeightFractionOfSkull *
                                (d.cluster.maxZ - skullLowerBoundLocalZ(d.cluster)));
    }
    ASSERT_GE(eyes.size(), 20u);

    int distinct = 0;
    for (std::size_t i = 1; i < eyes.size(); ++i) {
        if (std::fabs(eyes[i] - eyes[0]) > 1e-4f) ++distinct;
    }
    EXPECT_GT(distinct, 15) << "the eye height barely varies across races -- "
                               "that would mean it is not coming from the mesh";
}

TEST_F(ShippedMeshEyeTest, TheEyeMovesTheOwnHeadInsideTheNearPlane) {
    // THE PREDICTED MECHANISM, measured. T301's hypothesis is that with the
    // eye inside the head the player's own head falls within the ORDINARY
    // 1.0-unit near plane and is swallowed by it, where a crown-mounted eye
    // leaves the whole face outside and below -- which is what
    // review/T300/T300_C_12 is a photograph of.
    //
    // *** THIS IS A DISTANCE MEASUREMENT, NOT A VISIBILITY MEASUREMENT. ***
    // Nothing here rasterizes. It says the head geometry is nearer than the
    // near plane, not that the GPU discarded it.
    const float kNear = EQT::Graphics::kWorldNearPlaneUnits;
    int covered = 0;
    std::printf("\n%-20s %14s %14s\n", "race", "crownEyeOutside", "headEyeOutside");
    for (const auto& rc : kPlayableRaces) {
        const Derived d = derive(rc);
        if (!d.ok) continue;
        ++covered;

        auto outsideNear = [&](float ex, float ey, float ez) {
            auto md = loader_->ensureModelDataLoaded(rc.raceId, rc.gender);
            int outside = 0;
            std::size_t vi = 0;
            const auto& cv = md->combinedGeometry->vertices;
            for (const auto& p : md->vertexPieces) {
                for (unsigned k = 0; k < p.count && vi < cv.size(); ++k, ++vi) {
                    if (p.boneIndex != d.headBone) continue;
                    const float dx = cv[vi].x - ex, dy = cv[vi].y - ey,
                                dz = cv[vi].z - ez;
                    if (std::sqrt(dx * dx + dy * dy + dz * dz) > kNear) ++outside;
                }
            }
            return outside;
        };

        // T300's eye: the whole-body crown, on the model's centre line.
        const int crownOutside = outsideNear(0.0f, 0.0f, d.crownZ);
        const int headOutside =
            outsideNear(d.eyeModelX, d.eyeModelY, d.eyeModelZ);
        std::printf("%-20s %14d %14d\n", rc.label, crownOutside, headOutside);

        EXPECT_LE(headOutside, crownOutside)
            << rc.label << ": moving the eye into the head left MORE of the "
                           "player's own head outside the near plane than the "
                           "crown did -- that is a regression, not a fix";
    }
    EXPECT_GE(covered, 20);
}

TEST_F(ShippedMeshEyeTest, TheDownFrustumHoldsLessOwnBodyThanWithTheCrownEye) {
    // What can honestly be said headlessly about "does the player see his own
    // body at max down-pitch", and what cannot.
    //
    // CANNOT: anything involving the look DIRECTION. Deciding whether a vertex
    // is inside the frustum needs the camera's forward axis, and T300 Leg B's
    // per-race entry-angle table died on exactly that -- live vs headless was
    // 26.5 vs 39.5 for a Human male and 60.6 vs 0 for a Troll, THE ERROR
    // CHANGING SIGN BETWEEN RACES. That table is struck (see
    // tests/test_inventory_model_view.cpp) and is not rebuilt here.
    //
    // CAN: the DEPRESSION ANGLE below horizontal, which needs only the up
    // axis and is therefore heading-independent. Counting the own-body
    // vertices whose depression falls in the clamped frustum's vertical band
    // over ALL headings is an upper bound on what any heading could show. It
    // is a direction-of-improvement measurement and it is reported as one.
    const float kNear = EQT::Graphics::kWorldNearPlaneUnits;
    const float clamp = EQT::Graphics::kFirstPersonMaxDownPitchDegrees;
    const float halfV = EQT::Graphics::firstPersonVerticalFovRadians(4.0f / 3.0f) *
                        0.5f * 180.0f / 3.14159265358979323846f;
    const float lo = clamp - halfV;
    const float hi = clamp + halfV;

    int covered = 0;
    std::printf("\nband %.1f..%.1f deg below horizon; own-body vertices beyond "
                "the %.1f near plane\n", lo, hi, kNear);
    std::printf("%-20s %10s %10s\n", "race", "crownEye", "headEye");
    for (const auto& rc : kPlayableRaces) {
        const Derived d = derive(rc);
        if (!d.ok) continue;
        ++covered;
        auto md = loader_->ensureModelDataLoaded(rc.raceId, rc.gender);

        auto inBand = [&](float ex, float ey, float ez) {
            int n = 0;
            for (const auto& v : md->combinedGeometry->vertices) {
                const float dx = v.x - ex, dy = v.y - ey, dz = v.z - ez;
                if (std::sqrt(dx * dx + dy * dy + dz * dz) <= kNear) continue;
                if (dz >= 0.0f) continue;   // above the eye: never in a down-frustum
                const float th = std::atan2(-dz, std::sqrt(dx * dx + dy * dy)) *
                                 180.0f / 3.14159265358979323846f;
                if (th > lo && th < hi) ++n;
            }
            return n;
        };
        const int crownBand = inBand(0.0f, 0.0f, d.crownZ);
        const int headBand = inBand(d.eyeModelX, d.eyeModelY, d.eyeModelZ);
        std::printf("%-20s %10d %10d\n", rc.label, crownBand, headBand);

        EXPECT_LE(headBand, crownBand)
            << rc.label << ": the corrected eye put MORE of the body into the "
                           "down-frustum band than the crown eye did";
    }
    EXPECT_GE(covered, 20);

    // *** AND THE PART THAT DOES NOT PASS, STATED RATHER THAN HIDDEN: the
    // counts above are NOT zero. A clamp alone cannot empty this band --
    // the body spans depression angles from near 0 (the hands) to near 90
    // (the feet) from ANY eye inside the head, so for every clamp value some
    // part of the body lies in [clamp - vFOV/2, clamp + vFOV/2]. Whether it
    // is actually ON SCREEN depends on the azimuth, which is the axis
    // question above and is LIVE-ONLY. T301 Leg A is measuring whether the
    // official client shows a sliver at its own max down-pitch; Leg E judges
    // fable's. Do not assert zero here -- it would be a false pass.
}

#endif  // EQT_HAS_GRAPHICS
