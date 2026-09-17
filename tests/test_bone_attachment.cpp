/*
 * T60: unit tests for the shared bone-lookup / weapon-attachment math
 * (client/graphics/eq/bone_attachment.h). Before this unit, this fallback
 * chain existed as three independently-duplicated, untested copies
 * (entity_equipment.cpp, tools/model_viewer.cpp, the deleted
 * CharacterModelView) — see refactor/T60_paperdoll_restore.md.
 *
 * findBoneIndex()'s fallback chain is pure string logic over a skeleton;
 * these tests build a synthetic CharacterSkeleton rather than loading real
 * S3D data, so they need no device, no Irrlicht driver, and no test assets.
 */

#include <gtest/gtest.h>
#include <cmath>
#include "client/graphics/eq/bone_attachment.h"
#include "client/graphics/eq/s3d_loader.h"

using EQT::Graphics::findBoneIndex;
using EQT::Graphics::computeHeldItemTransform;
using EQT::Graphics::BoneMat4;

namespace {

std::shared_ptr<EQT::Graphics::CharacterSkeleton> makeSkeleton(std::vector<std::string> boneNames) {
    auto skeleton = std::make_shared<EQT::Graphics::CharacterSkeleton>();
    skeleton->modelCode = "hum";
    for (auto& name : boneNames) {
        EQT::Graphics::AnimatedBone bone;
        bone.name = std::move(name);
        bone.parentIndex = -1;
        skeleton->bones.push_back(std::move(bone));
    }
    return skeleton;
}

} // namespace

// --- findBoneIndex: fallback chain, tier by tier ---

TEST(BoneAttachmentTest, FindsExactUppercaseRaceCodePlusSuffix) {
    auto skeleton = makeSkeleton({"HUMR_POINT"});
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT"});
    ASSERT_EQ(idx, 0);
}

TEST(BoneAttachmentTest, TriesEachSuffixInOrderAndReturnsFirstHit) {
    // Neither R_POINT_DAG nor R_POINT exist, but a later suffix in the
    // list does — the fallback chain must try suffixes in the given order
    // and return the first that resolves anywhere in ITS OWN case chain,
    // not stop at the first suffix regardless of match.
    auto skeleton = makeSkeleton({"HUMBO_R_DAG"});
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT_DAG", "R_POINT", "BO_R_DAG"});
    ASSERT_EQ(idx, 0);
}

TEST(BoneAttachmentTest, FallsBackToLowercaseRaceCodePlusSuffix) {
    auto skeleton = makeSkeleton({"humr_point"});
    int idx = findBoneIndex(skeleton, "HUM", {"r_point"});
    ASSERT_EQ(idx, 0);
}

TEST(BoneAttachmentTest, FallsBackToUnderscoreStrippedLowercaseVariant) {
    // Suffix "_R_POINT" (leading underscore): neither "HUM_R_POINT" nor
    // "hum_r_point" exist, but "humR_POINT" (lowerRace + suffix.substr(1))
    // does — the quirky underscore-stripped branch in the shared chain.
    auto skeleton = makeSkeleton({"humR_POINT"});
    int idx = findBoneIndex(skeleton, "HUM", {"_R_POINT"});
    ASSERT_EQ(idx, 0);
}

TEST(BoneAttachmentTest, FallsBackToDAGSuffixStrippedUppercase) {
    // "R_POINT_DAG" doesn't exist in either case, but "HUMR_POINT" (the
    // _DAG-stripped uppercase variant) does.
    auto skeleton = makeSkeleton({"HUMR_POINT"});
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT_DAG"});
    ASSERT_EQ(idx, 0);
}

TEST(BoneAttachmentTest, FallsBackToDAGStrippedVariantKeepsSuffixCase) {
    // Documents a real quirk in the shared (production) fallback chain,
    // found while writing these tests: the `_DAG`-stripped "lowercase"
    // variant is `lowerRace + noDAG`, and noDAG is a substring of the
    // ORIGINAL suffix — it is never independently re-cased. So an
    // uppercase suffix ("R_POINT_DAG") strips to a MIXED-case candidate
    // ("humR_POINT"), not the fully-lowercase "humr_point" the variable
    // name ("lowerRace + ...") suggests. This is existing, in-production
    // behaviour (entity_equipment.cpp) preserved verbatim by the shared
    // header per the plan's behaviour-preservation requirement — not
    // something T60 may "fix".
    auto skeleton = makeSkeleton({"humR_POINT"});
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT_DAG"});
    ASSERT_EQ(idx, 0);
}

TEST(BoneAttachmentTest, DAGStrippedBranchNeverTriggersForLowercaseDagSuffix) {
    // Second real quirk found while writing these tests: the DAG-suffix
    // check is case-sensitive (`suffix.substr(...) == "_DAG"`, uppercase
    // literal), so a suffix ending in lowercase "_dag" (e.g. production's
    // own "r_point_dag" fallback entries) NEVER enters the DAG-stripped
    // branch at all — only suffixes ending in uppercase "_DAG" do. A bone
    // reachable only via the DAG-stripped fallback is therefore
    // unreachable through a lowercase-"_dag" suffix, full stop (not merely
    // case-mismatched — the branch's `if` is false, so it isn't tried).
    // Preserved verbatim, not "fixed" — see the header's/plan's
    // behaviour-preservation requirement.
    auto skeleton = makeSkeleton({"humr_point"});  // Exists, but unreachable this way:
    int idx = findBoneIndex(skeleton, "HUM", {"r_point_dag"});
    ASSERT_EQ(idx, -1);
}

TEST(BoneAttachmentTest, ReturnsMinusOneWhenNoVariantMatches) {
    auto skeleton = makeSkeleton({"UNRELATED_BONE"});
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT_DAG", "R_POINT"});
    ASSERT_EQ(idx, -1);
}

TEST(BoneAttachmentTest, ReturnsMinusOneForNullSkeleton) {
    std::shared_ptr<EQT::Graphics::CharacterSkeleton> skeleton;
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT"});
    ASSERT_EQ(idx, -1);
}

TEST(BoneAttachmentTest, ReturnsMinusOneForEmptySkeleton) {
    auto skeleton = makeSkeleton({});
    int idx = findBoneIndex(skeleton, "HUM", {"R_POINT"});
    ASSERT_EQ(idx, -1);
}

TEST(BoneAttachmentTest, MatchesTheSameFallbackChainRealRaceCodesUse) {
    // Regression pin for the actual suffix lists entity_equipment.cpp uses
    // in production (updateEquipmentTransforms) — right-hand weapon point
    // for a Dark Elf Female-style gendered race code ("DAF").
    auto skeleton = makeSkeleton({"DAFR_POINT_DAG"});
    std::vector<std::string> suffixes = {"R_POINT_DAG", "R_POINT", "r_point_dag", "r_point"};
    int idx = findBoneIndex(skeleton, "DAF", suffixes);
    ASSERT_EQ(idx, 0);
}

// --- computeHeldItemTransform / placeHeldItem: the one held-item transform ---
//
// In EQ space it is B * C with C = kHeldItemCorrection (T485: item X -> -Z,
// Y -> Y, Z -> X, a quarter turn about the item's Y, chosen by search against
// the official client); in Irrlicht (x, z, y) it is S * B * C * S. These pin
// the algebra, and tests/test_held_item_attachment.cpp pins the result against
// real skeletons and real items.

namespace {
// EQ-space reference: S * (B * C * S*p) for an Irrlicht item-mesh point p and
// an explicit correction (column j = image of item axis j).
irr::core::vector3df referenceHeld(const BoneMat4& b, const irr::core::vector3df& irrPoint,
                                   const EQT::Graphics::HeldItemCorrection& c) {
    const float px = irrPoint.X, py = irrPoint.Z, pz = irrPoint.Y;  // S * p, in EQ axes
    const float ex = c.c[0][0] * px + c.c[1][0] * py + c.c[2][0] * pz;
    const float ey = c.c[0][1] * px + c.c[1][1] * py + c.c[2][1] * pz;
    const float ez = c.c[0][2] * px + c.c[1][2] * py + c.c[2][2] * pz;
    const float wx = b.m[0] * ex + b.m[4] * ey + b.m[8] * ez + b.m[12];
    const float wy = b.m[1] * ex + b.m[5] * ey + b.m[9] * ez + b.m[13];
    const float wz = b.m[2] * ex + b.m[6] * ey + b.m[10] * ez + b.m[14];
    return irr::core::vector3df(wx, wz, wy);  // back to Irrlicht
}

BoneMat4 rotatedBone() {
    const float n = std::sqrt(0.2f * 0.2f + 0.5f * 0.5f + 0.1f * 0.1f + 0.83f * 0.83f);
    return BoneMat4::translate(0.54f, -0.84f, 1.25f) * BoneMat4::fromQuaternion(0.2f / n, 0.5f / n, 0.1f / n, 0.83f / n);
}
}  // namespace

TEST(BoneAttachmentTest, ShippedCorrectionIsAQuarterTurnAboutTheItemsY) {
    // The winner of T485's search, spelled out: item X -> bone -Z, Y -> Y, Z -> X.
    const auto& c = EQT::Graphics::kHeldItemCorrection;
    EXPECT_FLOAT_EQ(c.c[0][0], 0.0f);  EXPECT_FLOAT_EQ(c.c[0][1], 0.0f);  EXPECT_FLOAT_EQ(c.c[0][2], -1.0f);
    EXPECT_FLOAT_EQ(c.c[1][0], 0.0f);  EXPECT_FLOAT_EQ(c.c[1][1], 1.0f);  EXPECT_FLOAT_EQ(c.c[1][2], 0.0f);
    EXPECT_FLOAT_EQ(c.c[2][0], 1.0f);  EXPECT_FLOAT_EQ(c.c[2][1], 0.0f);  EXPECT_FLOAT_EQ(c.c[2][2], 0.0f);
    // A proper rotation (det +1), so Irrlicht's Euler round trip holds.
    const float det = c.c[0][0] * (c.c[1][1] * c.c[2][2] - c.c[2][1] * c.c[1][2]) -
                      c.c[1][0] * (c.c[0][1] * c.c[2][2] - c.c[2][1] * c.c[0][2]) +
                      c.c[2][0] * (c.c[0][1] * c.c[1][2] - c.c[1][1] * c.c[0][2]);
    EXPECT_FLOAT_EQ(det, 1.0f);
}

TEST(BoneAttachmentTest, HeldItemAtAnIdentityBoneIsAQuarterTurnAboutTheItemsY) {
    const irr::core::matrix4 m = computeHeldItemTransform(BoneMat4::identity());
    irr::core::vector3df eqX(1, 0, 0), eqY(0, 0, 1), eqZ(0, 1, 0);  // EQ axes, in Irrlicht coordinates
    m.transformVect(eqX);
    m.transformVect(eqY);
    m.transformVect(eqZ);
    EXPECT_NEAR(eqX.Y, -1.0f, 1e-6f) << "the item's long axis (+X) must land on the bone's -Z";
    EXPECT_NEAR(eqY.Z, 1.0f, 1e-6f) << "the item's Y (a shield's painted face) must be kept";
    EXPECT_NEAR(eqZ.X, 1.0f, 1e-6f) << "the item's Z must land on the bone's +X";
}

TEST(BoneAttachmentTest, HeldItemOriginIsTheBoneOrigin) {
    const irr::core::matrix4 m = computeHeldItemTransform(BoneMat4::translate(1.0f, 2.0f, 3.0f));
    const irr::core::vector3df t = m.getTranslation();
    EXPECT_FLOAT_EQ(t.X, 1.0f);
    EXPECT_FLOAT_EQ(t.Y, 3.0f);  // EQ z is Irrlicht Y
    EXPECT_FLOAT_EQ(t.Z, 2.0f);
}

TEST(BoneAttachmentTest, HeldItemTransformMatchesTheEqSpaceCompositionForARotatedBone) {
    const BoneMat4 b = rotatedBone();
    // The shipped correction and, through the explicit parameter, T472's old
    // one and the identity -- the same algebra for every candidate the search
    // rendered.
    const EQT::Graphics::HeldItemCorrection t472{{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}};
    const EQT::Graphics::HeldItemCorrection identity{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    for (const auto* c : {&EQT::Graphics::kHeldItemCorrection, &t472, &identity}) {
        const irr::core::matrix4 m = computeHeldItemTransform(b, *c);
        for (const irr::core::vector3df& p : {irr::core::vector3df(2.24f, 0.0f, 0.0f), irr::core::vector3df(0.0f, 0.3f, -0.2f),
                                              irr::core::vector3df(-0.51f, 0.1f, 0.4f)}) {
            irr::core::vector3df got = p;
            m.transformVect(got);
            const irr::core::vector3df want = referenceHeld(b, p, *c);
            EXPECT_NEAR(got.X, want.X, 1e-5f);
            EXPECT_NEAR(got.Y, want.Y, 1e-5f);
            EXPECT_NEAR(got.Z, want.Z, 1e-5f);
        }
        // A proper rotation (no mirror), so Irrlicht's Euler round trip holds.
        irr::core::matrix4 r = m;
        r.setTranslation(irr::core::vector3df(0, 0, 0));
        const float det = r[0] * (r[5] * r[10] - r[9] * r[6]) - r[4] * (r[1] * r[10] - r[9] * r[2]) +
                          r[8] * (r[1] * r[6] - r[5] * r[2]);
        EXPECT_NEAR(det, 1.0f, 1e-5f);
        irr::core::matrix4 roundTrip;
        roundTrip.setRotationDegrees(r.getRotationDegrees());
        for (int i = 0; i < 16; ++i) EXPECT_NEAR(roundTrip[i], r[i], 1e-3f) << "element " << i;
    }
}

// T485: the one placement. An NPC's node takes the transform's rotation and
// translation as they are; the player's takes T301's yaw compensation on both
// and the un-scaled position. Pinned on an empty scene node (EDT_NULL).
TEST(BoneAttachmentTest, PlaceHeldItemSetsTheNodeFromTheOneTransform) {
    irr::SIrrlichtCreationParameters p;
    p.DriverType = irr::video::EDT_NULL;
    p.WindowSize = irr::core::dimension2d<irr::u32>(16, 16);
    irr::IrrlichtDevice* device = irr::createDeviceEx(p);
    ASSERT_NE(device, nullptr);
    irr::scene::ISceneNode* node = device->getSceneManager()->addEmptySceneNode();
    ASSERT_NE(node, nullptr);
    const BoneMat4 b = rotatedBone();
    irr::core::matrix4 held = computeHeldItemTransform(b);
    const irr::core::vector3df position = held.getTranslation();
    held.setTranslation(irr::core::vector3df(0, 0, 0));

    EQT::Graphics::placeHeldItem(node, b);
    EXPECT_LT(node->getPosition().getDistanceFrom(position), 1e-5f);
    irr::core::matrix4 fromNode;
    fromNode.setRotationDegrees(node->getRotation());
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(fromNode[i], held[i], 1e-3f) << "element " << i;

    // Player: yaw fix Y = 30 deg, scale 2 -> position halved then rotated, rotation composed.
    irr::core::matrix4 yawFix;
    yawFix.setRotationDegrees(irr::core::vector3df(0, 30.0f, 0));
    EQT::Graphics::placeHeldItem(node, b, &yawFix, 2.0f);
    irr::core::vector3df wantPos = position / 2.0f;
    yawFix.rotateVect(wantPos);
    EXPECT_LT(node->getPosition().getDistanceFrom(wantPos), 1e-5f);
    const irr::core::matrix4 wantRot = yawFix * held;
    fromNode.setRotationDegrees(node->getRotation());
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(fromNode[i], wantRot[i], 1e-3f) << "element " << i;

    // A null node is a no-op, not a crash.
    EQT::Graphics::placeHeldItem(nullptr, b);
    device->drop();
}
