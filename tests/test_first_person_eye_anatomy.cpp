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
    // Derived from the live constant, never pasted: the symmetrised span is
    // -0.85..+0.85, so the eye is simply that fraction up it. T431 moved the
    // fraction to 0.45 and this therefore sits a little BELOW the head joint
    // (-0.085) where at 0.65 it sat above it (+0.255). What the rule has to
    // deliver is not "above the joint" -- that was an artefact of the old
    // fraction -- but "nowhere near the beard", which the two assertions
    // below measure.
    EXPECT_NEAR(eye.z, -0.85f + kEyeHeightFractionOfSkull * 1.70f, 1e-4f);
    EXPECT_GT(eye.z, -0.85f)
        << "the eye must be inside the symmetrised skull, not at its floor";

    // The number this rule was written to kill: with the raw minimum the eye
    // lands at -0.563 in bone space, which on the real dwmhe transform is
    // model Z 1.48 -- 2.27 below the crown, in his chest.
    // Derived from the constant, not pasted in, so re-solving the fraction
    // (as T301 Leg A's measurement required) cannot silently stale this out.
    const float naive = dwarf.minZ + kEyeHeightFractionOfSkull * (dwarf.maxZ - dwarf.minZ);
    EXPECT_LT(naive, 0.0f)
        << "with the raw minimum the eye lands BELOW the head joint, which on "
           "the real dwmhe transform is model Z 1.48 -- in his chest";
    EXPECT_GT(eye.z - naive, 0.5f)
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
        float bodyMinZ = 0.0f; // the mesh's own feet
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
        d.bodyMinZ = 1e9f;
        const auto& cv = md->combinedGeometry->vertices;
        for (const auto& p : md->vertexPieces) {
            for (unsigned k = 0; k < p.count && vi < cv.size(); ++k, ++vi) {
                if (cv[vi].z > d.crownZ) d.crownZ = cv[vi].z;
                if (cv[vi].z < d.bodyMinZ) d.bodyMinZ = cv[vi].z;
                if (p.boneIndex != d.headBone) continue;
                d.headModelMinZ = std::min(d.headModelMinZ, cv[vi].z);
                d.headModelMaxZ = std::max(d.headModelMaxZ, cv[vi].z);
            }
        }
        for (; vi < cv.size(); ++vi) {
            if (cv[vi].z > d.crownZ) d.crownZ = cv[vi].z;
            if (cv[vi].z < d.bodyMinZ) d.bodyMinZ = cv[vi].z;
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

TEST_F(ShippedMeshEyeTest, EyeMatchesTheCrownOffsetTheOracleSolveImplies) {
    // NO LONGER THE DRIVER (T310 Leg C). kEyeHeightFractionOfSkull is now
    // solved against the OFFICIAL CLIENT'S OWN FRAMES -- see the header --
    // and kCrownAboveEyeUnits is the crown-to-eye distance that solve
    // implies. This test still earns its place: it is what catches the solve
    // going stale if the head-cluster derivation or the symmetrised skull
    // bound ever changes, because the two numbers must keep agreeing.
    bool covered = false;
    for (const auto& rc : kPlayableRaces) {
        if (rc.raceId != 1 || rc.gender != 0) continue;   // the reference race
        const Derived d = derive(rc);
        if (!d.ok) continue;
        covered = true;
        EXPECT_NEAR(d.crownZ - d.eyeModelZ, EQT::Graphics::kCrownAboveEyeUnits, 0.05f)
            << "the Human male eye must land at crown - kCrownAboveEyeUnits; got crown - "
            << (d.crownZ - d.eyeModelZ)
            << ". kEyeHeightFractionOfSkull is solved for this and needs "
               "re-solving.";
    }
    if (!covered) GTEST_SKIP() << "Human male model unavailable";
}

TEST_F(ShippedMeshEyeTest, StandingHeightCrossCheckAgainstLegAsPoint925) {
    // *** THE REFERENCE IS T431'S 0.887, NOT LEG A'S 0.925. *** The test name
    // is kept so the history is greppable. T301 Leg A measured 0.925 +/- 0.004
    // before T310 Leg B's collisionZOffset fix raised every eye 0.119 units
    // off the ground; T431 re-measured the Human male against the official
    // client after that fix and after B399's heading fix and got 0.887.
    // kEyeHeightFractionOfStandingHeight carries the new value.
    //
    // Leg A's OTHER number: the eye at 0.925 +/- 0.004 of standing height.
    //
    // *** T310 LEG C MADE THIS THE SPEC RATHER THAN THE CROSS-CHECK. *** The
    // reason it was demoted -- "fable stands the model CENTRED on the server
    // z, the official client stands it FEET-ON-z" -- was disproved by T310
    // Leg B from the EQEmu source, and Leg C then measured fable's eye
    // against the official client's frames at a shared server-set pose: the
    // pixel-difference curve minimised at 0.925 H to within 0.01 units.
    // kEyeHeightFractionOfSkull is solved for that. The band below stays a
    // band and not an equality because it is asserted PER RACE off each
    // mesh's own head cluster, and only Human male was measured live.
    std::printf("\n%-20s %8s %8s %8s   %s\n", "race", "H", "eye/H",
                "LegA", "delta");
    int covered = 0;
    for (const auto& rc : kPlayableRaces) {
        const Derived d = derive(rc);
        if (!d.ok) continue;
        ++covered;
        const float H = d.crownZ - d.bodyMinZ;
        ASSERT_GT(H, 0.0f) << rc.label;
        const float frac = (d.eyeModelZ - d.bodyMinZ) / H;
        std::printf("%-20s %8.3f %8.3f %8.3f   %+.3f\n", rc.label, H, frac,
                    EQT::Graphics::kEyeHeightFractionOfStandingHeight,
                    frac - EQT::Graphics::kEyeHeightFractionOfStandingHeight);
        // T431 LOWERED THIS GUARD FROM 0.80 TO 0.72, and the reason is a
        // MEASUREMENT, not a convenience. The global skull fraction was
        // re-solved against the official client at 0.65 -> 0.45, which drops
        // every race's eye by 0.20 of ITS OWN symmetrised skull span -- a
        // small drop for a Human male (0.041 of H) and a large one for a
        // Dwarf male (0.091 of H), because the Dwarf's beard is skinned to
        // the head bone and doubles that span. The Dwarf male lands at 0.756
        // and it is the ONLY race under 0.80. That is the boarded residual
        // (B470): the oracle puts the Dwarf's eye LOWER still, at 0.57 H, so
        // 0.756 is on the correct side of the error and this guard must not
        // be read as an endorsement of it.
        EXPECT_GT(frac, 0.72f)
            << rc.label << ": the eye is far too low up the body to be an eye";
        EXPECT_LT(frac, 1.0f)
            << rc.label << ": the eye is at or above the crown -- the T300 defect";
    }
    EXPECT_GE(covered, 20);
}

TEST(FirstPersonEyePosture, PostureDrivenIsWhatLegAMeasured) {
    // Leg A Q3, measured directly: the actor pinned against a wall holding
    // forward produced ONE distinct rendered frame in 12 seconds with ZERO
    // pixel change in his own view, while the observing client watched his
    // crown bob 0.16 units over ~10 gait cycles. The official eye does not
    // follow the skeleton. But sitting drops it 3.6 units, so it is not
    // posture-blind either.
    EXPECT_EQ(EQT::Graphics::kFirstPersonEyeSource,
              EQT::Graphics::EyePoseSource::PostureDriven);
    EXPECT_NE(EQT::Graphics::kFirstPersonEyeSource,
              EQT::Graphics::EyePoseSource::AnimatedPose)
        << "AnimatedPose was this leg's first choice and Leg A REFUTED it. The "
           "argument for it (an eye inside the skull cannot be bobbed through) "
           "is moot: the official client accepts the bob and never draws the "
           "body. Do not restore it on that argument.";
}

TEST(FirstPersonEyePosture, SitDropIsLegAsMeasuredFractionAndStandingIsZero) {
    using EQT::Graphics::EyePosture;
    using EQT::Graphics::eyeDropFractionForPosture;

    EXPECT_FLOAT_EQ(eyeDropFractionForPosture(EyePosture::Standing), 0.0f)
        << "standing must be the undisplaced reference";

    // MEASURED: standing H -> 0.47 H sitting. The SEATED value is the
    // measurement; the drop is (standing - 0.47) so that re-solving the
    // standing eye moves the drop and leaves the seated eye where Leg A
    // measured it. T431 took standing from 0.925 to 0.887, so the drop is
    // now 0.417 where it was 0.455. Sitting itself was NOT re-measured.
    EXPECT_NEAR(eyeDropFractionForPosture(EyePosture::Sitting),
                EQT::Graphics::kEyeHeightFractionOfStandingHeight - 0.47f, 1e-4f);
    EXPECT_NEAR(eyeDropFractionForPosture(EyePosture::Sitting), 0.417f, 1e-3f);
    EXPECT_GT(eyeDropFractionForPosture(EyePosture::Sitting), 0.0f)
        << "sitting must LOWER the eye";

    // *** CROUCHING AND LYING ARE UNMEASURED *** and deliberately reuse the
    // sit drop rather than carry invented constants. This test states that
    // out loud so the reuse is found on purpose rather than mistaken for a
    // measurement. Both are outstanding follow-ups; a prone eye in particular
    // is certainly lower than a seated one.
    EXPECT_FLOAT_EQ(eyeDropFractionForPosture(EyePosture::Crouching),
                    eyeDropFractionForPosture(EyePosture::Sitting));
    EXPECT_FLOAT_EQ(eyeDropFractionForPosture(EyePosture::Lying),
                    eyeDropFractionForPosture(EyePosture::Sitting));
}

TEST(FirstPersonEyePosture, SitDropIsAboutThreeAndAHalfUnitsForAHumanSizedBody) {
    // Leg A reports the sit drop two ways and they have never agreed: 3.6
    // UNITS, and 0.925 H standing -> 0.47 H sitting, which are consistent only
    // if H is 7.9. This mesh's Human male H is 6.67.
    //
    // *** T431 WIDENED THE DIVERGENCE AND DID NOT RE-MEASURE SITTING. *** The
    // standing eye re-solved against the official client at 0.887 H, so the
    // applied drop is 0.417 H = 2.78 units on this mesh, against Leg A's
    // reported 3.6. The gap is now 0.82 units where it was 0.56. Sitting was
    // out of T431's scope and is board B471: the seated eye needs the same
    // live A/B the standing eye just had, and until it gets one the FRACTION
    // is what is applied, because it is the frame-independent form Leg A
    // itself recommended keying off. This test pins the magnitude so the
    // discrepancy stays visible and bounded rather than growing unnoticed.
    const float humanMaleMeshHeight = 6.67f;
    const float drop = EQT::Graphics::kSitEyeDropFractionOfHeight * humanMaleMeshHeight;
    EXPECT_NEAR(drop, 2.78f, 0.10f);
    EXPECT_LT(std::fabs(drop - 3.6f), 1.0f)
        << "the applied sit drop has diverged from Leg A's reported 3.6 units "
           "by more than the known frame disagreement explains";
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

TEST_F(ShippedMeshEyeTest, NoClampCanEmptyTheDownFrustumOfTheOwnBody) {
    // *** THIS TEST PINS A REFUTATION, NOT A FIX. ***
    //
    // It began life asserting that the corrected eye puts LESS of the player's
    // own body into the clamped frustum's vertical band than the crown eye
    // did. T301 Leg A made that the wrong figure of merit: the measured clamp
    // is 94.9 degrees, roughly 5 PAST THE NADIR, and Leg A's control run -- an
    // identical Human-male model teleported onto the actor's own coordinates,
    // at that same clamp -- fills 17.8% of the frame with shoulders, arms,
    // hands, belt and hair. THE FRUSTUM CONTAINS THE BODY. THE OFFICIAL CLIENT
    // SIMPLY DOES NOT DRAW IT.
    //
    // So band occupancy no longer measures anything about hiding, and
    // asserting it monotonically improves was asserting noise (the Iksar male
    // moved by 3 vertices out of ~1000 when the eye was re-solved to Leg A's
    // spec, and "fixing" that would have meant tuning to a dead metric).
    //
    // What IS worth pinning is the refutation itself: NO CLAMP VALUE CAN EMPTY
    // THIS BAND, because from any eye inside the head the body spans
    // depression angles from near 0 (the hands) to near 90 (the feet). If a
    // future unit pulls the clamp back under 90 and claims that hides the
    // body, this test is what says otherwise.
    //
    // The per-race counts are REPORTED for the draw-rule leg, which is where
    // the real mechanism is being measured. Nothing here rasterizes.
    const float kNear = EQT::Graphics::kWorldNearPlaneUnits;
    const float clamp = EQT::Graphics::kFirstPersonMaxDownPitchDegrees;
    const float halfV = EQT::Graphics::firstPersonVerticalFovRadians(4.0f / 3.0f) *
                        0.5f * 180.0f / 3.14159265358979323846f;
    const float lo = clamp - halfV;
    const float hi = clamp + halfV;

    ASSERT_GT(clamp, 90.0f)
        << "Leg A measured the clamp past the nadir; if this ever drops back "
           "under 90 the comment above needs rereading before the number is "
           "changed";

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

        EXPECT_GT(headBand, 0)
            << rc.label << ": the down-frustum band came out EMPTY, which "
                           "would mean the clamp alone hides the body. Leg A's "
                           "control measured 17.8% of the frame filled with it "
                           "at this clamp -- check the measurement before "
                           "believing this.";
    }
    EXPECT_GE(covered, 20);
}


// =========================================================================
// Part 4 -- T431. THE TWO RACES MEASURED AGAINST THE OFFICIAL CLIENT.
// =========================================================================
//
// METHOD (review/T431/README.md has the frames, the curves and the traps).
// Both clients at the same server-set pose in qeynos2, day, weather pinned,
// first person, UI hidden, 1024x768; fable's eyeHeightFractionOfSkull swept
// with /fpcam and every captured frame scored against the official client's
// own frame with scripts/camera_match_score.py's gradient metric. THE CURVE'S
// MINIMUM IS THE MEASUREMENT AND ITS WIDTH IS THE ERROR BAR.
//
//   race         pose                       solved f    eye above own feet
//   Human male   (-74, 428, 3.751, h160)    0.43-0.45   6.02 +/- 0.08 (live)
//   Dwarf male   (-70, 428, 3.751, h160)    0.08-0.10   3.58 +/- 0.14 (live)
//
// The oracle's own session-to-session repeatability at the human pose is
// edge 0.0648, so the curves are well clear of the noise floor.
//
// LIVE UNITS ARE NOT MESH UNITS. The player node carries a per-race scale, so
// the numbers asserted below are the LIVE targets divided by that race's
// measured live/mesh ratio (Human male 1.0183, Dwarf male 0.9554, both read
// off the same runs' [FPCAM-DUMP] against this fixture's own derivation at
// the then-shipped f = 0.65).
//
//   Human male   live 6.023 -> mesh 5.915
//   Dwarf male   live 3.575 -> mesh 3.742
//
// *** THE HEADLINE, AND IT IS A NEGATIVE: ONE GLOBAL SKULL FRACTION CANNOT
// SATISFY BOTH RACES. *** The Human male solves at 0.44 and the Dwarf male at
// 0.09. The fraction shipped is the Human male's, because that is the race
// the whole first-person arc was measured on; the Dwarf is left ~1.0 units
// high and that residual is board B470. Do not "fix" it with a per-race
// table -- the derivation is per-race FROM THE MESH by construction and a
// table is what first_person_eye.h exists to forbid.

TEST_F(ShippedMeshEyeTest, HumanMaleEyeMatchesTheT431OracleSolve) {
    // RED at kEyeHeightFractionOfSkull = 0.65 (the derivation gives 6.190,
    // 0.275 above the measured 5.915); GREEN at 0.45.
    bool covered = false;
    for (const auto& rc : kPlayableRaces) {
        if (rc.raceId != 1 || rc.gender != 0) continue;
        const Derived d = derive(rc);
        if (!d.ok) continue;
        covered = true;
        const float eyeAboveFeet = d.eyeModelZ - d.bodyMinZ;
        std::printf("Human male eye above own feet: %.3f (T431 oracle solve %.3f)\n",
                    eyeAboveFeet, 5.915f);
        EXPECT_NEAR(eyeAboveFeet, 5.915f, 0.15f)
            << "the Human male eye no longer lands where the official client's "
               "own frames put it (review/T431). kEyeHeightFractionOfSkull is "
               "solved for this and needs re-solving against the oracle, not "
               "adjusting to suit this number.";
    }
    if (!covered) GTEST_SKIP() << "Human male model unavailable";
}

TEST_F(ShippedMeshEyeTest, DwarfMaleEyeIsUnderTheT431OracleUpperBound) {
    // The Dwarf is the race the symmetrised-skull rule exists for (its beard
    // is skinned to the head bone) and it is the race the single fraction
    // fits worst. The measured eye is 3.742 mesh units above its feet; the
    // bound below is deliberately LOOSE -- 4.82, i.e. live 4.6 -- because the
    // residual is boarded, not closed. It still discriminates: the shipped
    // f = 0.65 puts the eye at 5.317, which is above a Dwarf's own head and
    // fails; 0.45 puts it at 4.744 and passes.
    bool covered = false;
    for (const auto& rc : kPlayableRaces) {
        if (rc.raceId != 8 || rc.gender != 0) continue;
        const Derived d = derive(rc);
        if (!d.ok) continue;
        covered = true;
        const float eyeAboveFeet = d.eyeModelZ - d.bodyMinZ;
        std::printf("Dwarf male eye above own feet: %.3f (T431 oracle solve %.3f, "
                    "bound %.3f)\n", eyeAboveFeet, 3.742f, 4.82f);
        EXPECT_LE(eyeAboveFeet, 4.82f)
            << "the Dwarf male eye is above the loose upper bound the official "
               "client's own frames allow (measured 3.742; review/T431).";
    }
    if (!covered) GTEST_SKIP() << "Dwarf male model unavailable";
}

TEST_F(ShippedMeshEyeTest, OneSkullFractionCannotSatisfyBothMeasuredRaces) {
    // A pin on the FINDING, not on a fix. It is green before and after the
    // constant moved, and it exists so that a later unit cannot quietly
    // report B470 as closed while the residual is still there: whatever the
    // global fraction is, the two measured races cannot both be satisfied by
    // it, because their eyes move at very different rates per unit fraction
    // (Human male 1.375 mesh units, Dwarf male 2.864).
    float humanErr = -1.0f, dwarfErr = -1.0f;
    for (const auto& rc : kPlayableRaces) {
        const bool isHuman = (rc.raceId == 1 && rc.gender == 0);
        const bool isDwarf = (rc.raceId == 8 && rc.gender == 0);
        if (!isHuman && !isDwarf) continue;
        const Derived d = derive(rc);
        if (!d.ok) continue;
        const float eyeAboveFeet = d.eyeModelZ - d.bodyMinZ;
        if (isHuman) humanErr = std::fabs(eyeAboveFeet - 5.915f);
        if (isDwarf) dwarfErr = std::fabs(eyeAboveFeet - 3.742f);
    }
    if (humanErr < 0.0f || dwarfErr < 0.0f) {
        GTEST_SKIP() << "Human male or Dwarf male model unavailable";
    }
    std::printf("T431 residuals: Human male %.3f, Dwarf male %.3f (mesh units)\n",
                humanErr, dwarfErr);
    EXPECT_GT(humanErr + dwarfErr, 0.5f)
        << "both measured races are now within 0.5 units of their oracle "
           "solve. If that is real, B470 is CLOSED and this test must be "
           "replaced by two positive assertions -- but re-measure against the "
           "official client first; do not delete this on arithmetic alone.";
}

#endif  // EQT_HAS_GRAPHICS
