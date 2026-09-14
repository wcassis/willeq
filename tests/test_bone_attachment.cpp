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

// --- computeHeldItemTransform: T472, the one held-item transform ---
//
// In EQ space it is B * diag(-1, 1, -1); in Irrlicht (x, z, y) it is
// S * B * C * S. The derivation from the S3D data is on the function; these
// pin the algebra, and tests/test_held_item_attachment.cpp pins the result
// against real skeletons and real items.

namespace {
// EQ-space reference: S * (B * C * S*p) for an Irrlicht item-mesh point p.
irr::core::vector3df referenceHeld(const BoneMat4& b, const irr::core::vector3df& irrPoint) {
    const float ex = -irrPoint.X, ey = irrPoint.Z, ez = -irrPoint.Y;  // C * (S * p)
    const float wx = b.m[0] * ex + b.m[4] * ey + b.m[8] * ez + b.m[12];
    const float wy = b.m[1] * ex + b.m[5] * ey + b.m[9] * ez + b.m[13];
    const float wz = b.m[2] * ex + b.m[6] * ey + b.m[10] * ez + b.m[14];
    return irr::core::vector3df(wx, wz, wy);  // back to Irrlicht
}
}  // namespace

TEST(BoneAttachmentTest, HeldItemAtAnIdentityBoneIsAHalfTurnAboutTheItemsY) {
    const irr::core::matrix4 m = computeHeldItemTransform(BoneMat4::identity());
    irr::core::vector3df eqX(1, 0, 0), eqY(0, 0, 1), eqZ(0, 1, 0);  // EQ axes, in Irrlicht coordinates
    m.transformVect(eqX);
    m.transformVect(eqY);
    m.transformVect(eqZ);
    EXPECT_NEAR(eqX.X, -1.0f, 1e-6f) << "the item's long axis must be reversed";
    EXPECT_NEAR(eqY.Z, 1.0f, 1e-6f) << "the item's Y (a shield's painted face) must be kept";
    EXPECT_NEAR(eqZ.Y, -1.0f, 1e-6f);
}

TEST(BoneAttachmentTest, HeldItemOriginIsTheBoneOrigin) {
    const irr::core::matrix4 m = computeHeldItemTransform(BoneMat4::translate(1.0f, 2.0f, 3.0f));
    const irr::core::vector3df t = m.getTranslation();
    EXPECT_FLOAT_EQ(t.X, 1.0f);
    EXPECT_FLOAT_EQ(t.Y, 3.0f);  // EQ z is Irrlicht Y
    EXPECT_FLOAT_EQ(t.Z, 2.0f);
}

TEST(BoneAttachmentTest, HeldItemTransformMatchesTheEqSpaceCompositionForARotatedBone) {
    const float n = std::sqrt(0.2f * 0.2f + 0.5f * 0.5f + 0.1f * 0.1f + 0.83f * 0.83f);
    const BoneMat4 b = BoneMat4::translate(0.54f, -0.84f, 1.25f) *
                       BoneMat4::fromQuaternion(0.2f / n, 0.5f / n, 0.1f / n, 0.83f / n);
    const irr::core::matrix4 m = computeHeldItemTransform(b);
    for (const irr::core::vector3df& p : {irr::core::vector3df(2.24f, 0.0f, 0.0f), irr::core::vector3df(0.0f, 0.3f, -0.2f),
                                          irr::core::vector3df(-0.51f, 0.1f, 0.4f)}) {
        irr::core::vector3df got = p;
        m.transformVect(got);
        const irr::core::vector3df want = referenceHeld(b, p);
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
