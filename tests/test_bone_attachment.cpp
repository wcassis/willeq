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
#include "client/graphics/eq/bone_attachment.h"
#include "client/graphics/eq/s3d_loader.h"

using EQT::Graphics::findBoneIndex;
using EQT::Graphics::extractBoneRotation;
using EQT::Graphics::computeWeaponGripOffset;
using EQT::Graphics::computeShieldGripOffset;

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

// --- extractBoneRotation ---

TEST(BoneAttachmentTest, ExtractBoneRotationOfIdentityIsZero) {
    EQT::Graphics::BoneMat4 identity{};
    // Column-major identity: diagonal 1s, rest 0.
    identity.m[0] = 1.0f; identity.m[5] = 1.0f; identity.m[10] = 1.0f; identity.m[15] = 1.0f;
    irr::core::vector3df rot = extractBoneRotation(identity);
    EXPECT_NEAR(rot.X, 0.0f, 0.01f);
    EXPECT_NEAR(rot.Y, 0.0f, 0.01f);
    EXPECT_NEAR(rot.Z, 0.0f, 0.01f);
}

TEST(BoneAttachmentTest, ExtractBoneRotationIgnoresTranslation) {
    // Two matrices differing only in translation (columns 12-14) must
    // extract to the same rotation — extractBoneRotation() intentionally
    // discards translation (see the header's comment).
    EQT::Graphics::BoneMat4 a{};
    a.m[0] = 1.0f; a.m[5] = 1.0f; a.m[10] = 1.0f; a.m[15] = 1.0f;
    EQT::Graphics::BoneMat4 b = a;
    b.m[12] = 100.0f; b.m[13] = -50.0f; b.m[14] = 25.0f;

    irr::core::vector3df rotA = extractBoneRotation(a);
    irr::core::vector3df rotB = extractBoneRotation(b);
    EXPECT_NEAR(rotA.X, rotB.X, 0.01f);
    EXPECT_NEAR(rotA.Y, rotB.Y, 0.01f);
    EXPECT_NEAR(rotA.Z, rotB.Z, 0.01f);
}

// --- Weapon/shield grip offset ---

TEST(BoneAttachmentTest, TwoHandedWeaponGripsAtQuarterLength) {
    // Length along X > 3 units => two-handed formula: length * 0.25.
    irr::core::aabbox3df bbox(irr::core::vector3df(-2.0f, 0, 0), irr::core::vector3df(3.0f, 0, 0));
    float length = bbox.MaxEdge.X - bbox.MinEdge.X;  // 5.0
    ASSERT_GT(length, 3.0f);
    float offset = computeWeaponGripOffset(bbox);
    EXPECT_NEAR(offset, length * 0.25f, 0.001f);
}

TEST(BoneAttachmentTest, OneHandedWeaponGripsNearHilt) {
    // Length along X <= 3 units => one-handed formula: -MinEdge.X * 0.8.
    irr::core::aabbox3df bbox(irr::core::vector3df(-1.0f, 0, 0), irr::core::vector3df(1.0f, 0, 0));
    float length = bbox.MaxEdge.X - bbox.MinEdge.X;  // 2.0
    ASSERT_LE(length, 3.0f);
    float offset = computeWeaponGripOffset(bbox);
    EXPECT_NEAR(offset, -bbox.MinEdge.X * 0.8f, 0.001f);
}

TEST(BoneAttachmentTest, TwoHandedThresholdIsExclusiveAtExactlyThreeUnits) {
    // Exactly 3.0 units is NOT > 3.0, so it takes the one-handed branch —
    // pins the boundary the production code uses (weaponLength > 3.0f).
    irr::core::aabbox3df bbox(irr::core::vector3df(0.0f, 0, 0), irr::core::vector3df(3.0f, 0, 0));
    float offset = computeWeaponGripOffset(bbox);
    EXPECT_NEAR(offset, -bbox.MinEdge.X * 0.8f, 0.001f);
}

TEST(BoneAttachmentTest, ShieldGripOffsetIsFlatConstantIndependentOfBoundingBox) {
    // T60 finding: production (entity_equipment.cpp) uses a flat 0.3
    // constant for the shield grip offset, NOT a bounding-box-derived
    // fraction (the deleted CharacterModelView and model_viewer.cpp's
    // pre-convergence initial placement both used bbox width * 0.175 —
    // see bone_attachment.h's comment on computeShieldGripOffset()). The
    // shared function must match production exactly since in-world
    // placement must not change.
    EXPECT_FLOAT_EQ(computeShieldGripOffset(), 0.3f);
}

// --- T62: pinned against real, measured EQ equipment mesh dimensions ---
//
// Measured via ui_inventory_capture's "BBOX:" diagnostic lines
// (tools/hwcaps/ui_inventory_capture.cpp) against real gequip.s3d data
// (/mnt/projects/EverQuestP1999). These establish that EQ weapon meshes
// use "world units" of the same scale as the character skeleton (a human
// is roughly 7-8 units tall), and that the existing 3.0f two-handed
// threshold correctly separates a real one-handed sword from a real
// two-handed staff -- see refactor/T62_held_item_alignment.md's Review
// for the full investigation (the original "huge untextured mass" report
// turned out to be a wrong test item -- a sack model, not a weapon -- and
// a missing texture-cache wire-up in the capture tool itself, not a grip-
// math defect).

TEST(BoneAttachmentTest, RealOneHandedSwordIT1GripsNearHilt) {
    // IT1 (gequip.s3d, "sword"): min=(-1.374,-0.091,-0.052) max=(1.373,0.478,0.096).
    // Length 2.746 < 3.0 -- one-handed branch, matching a real sword.
    irr::core::aabbox3df bbox(irr::core::vector3df(-1.374f, -0.091f, -0.052f),
                               irr::core::vector3df(1.373f, 0.478f, 0.096f));
    float length = bbox.MaxEdge.X - bbox.MinEdge.X;
    ASSERT_LT(length, 3.0f);
    float offset = computeWeaponGripOffset(bbox);
    EXPECT_NEAR(offset, 1.0992f, 0.001f);  // -MinEdge.X * 0.8 = 1.374 * 0.8
}

TEST(BoneAttachmentTest, RealTwoHandedStaffIT45GripsAtQuarterLength) {
    // IT45 (gequip.s3d, "wizard staff"): min=(-5.823,-0.154,-0.220) max=(0.063,0.156,0.092).
    // Length 5.887 > 3.0 -- two-handed branch, matching a real staff/polearm.
    irr::core::aabbox3df bbox(irr::core::vector3df(-5.823f, -0.154f, -0.220f),
                               irr::core::vector3df(0.063f, 0.156f, 0.092f));
    float length = bbox.MaxEdge.X - bbox.MinEdge.X;
    ASSERT_GT(length, 3.0f);
    float offset = computeWeaponGripOffset(bbox);
    EXPECT_NEAR(offset, 1.4715f, 0.001f);  // length * 0.25 = 5.886 * 0.25
}

TEST(BoneAttachmentTest, RealShieldIT202BoundingBoxDoesNotAffectFlatOffset) {
    // IT202 (gequip.s3d, "kiteshield-m"): min=(-1.504,-1.132,-0.241) max=(1.166,1.132,0.015).
    // computeShieldGripOffset() ignores the bbox entirely (see the
    // ShieldGripOffsetIsFlatConstantIndependentOfBoundingBox test above) --
    // this test exists purely to record the real shield's measured extent
    // (2.671 x 2.264 x 0.256) alongside that decision, so a future reader
    // has the real numbers the A/B comparison in the plan's Review used
    // (flat 0.3 vs bbox.X-width*0.175=0.467 -- visually near-identical for
    // this shield, per the Review; production's flat 0.3 was kept).
    irr::core::aabbox3df bbox(irr::core::vector3df(-1.504f, -1.132f, -0.241f),
                               irr::core::vector3df(1.166f, 1.132f, 0.015f));
    (void)bbox;
    EXPECT_FLOAT_EQ(computeShieldGripOffset(), 0.3f);
}
