// T437 (board B435, B436) -- the ceiling sentinel is not a floor.
//
// Two live defects, both found by reading in T435 Leg C, neither with any
// regression net because the comparisons lived inline in bodies that need a
// live Irrlicht device, a loaded zone and an entity renderer:
//
//   B435  findGroundZBspFiltered() returned `feetZ + 1000.0f` as its "that was
//         a CEILING, not a floor" answer, and the four jump landings compared
//         `feetZ <= groundZ` with no upper bound. Fed the sentinel they all
//         evaluated true and set `newZ = groundZ + modelYOffset`, ending the
//         jump 1,000 units in the air.
//   B436  HCMap::FindBestZ() re-cast UPWARD when the downward cast missed and
//         returned the ceiling's z with no marker at all.
//
// T440 added a third (board B438): findGroundZBspFiltered() answered
// Floor(feetZ) when its ray hit NOTHING AT ALL, so a player falling over a hole
// "landed" in mid-air. That verdict now lives in
// EQT::Graphics::ClassifyBspGroundRay(), tested in the GroundMissT440 suite
// below.
//
// The B435 half is tested through EQT::Graphics::ShouldLandJump() /
// ShouldClampSwimToFloor(), which is where those comparisons now live (T435's
// RayTestDoorCollision extraction is the model). The B436 half is tested
// against a real HCMap built from a hand-written V1 map file: three triangle
// sets -- floor below, ceiling only, and both -- fed through the actual
// loader and the actual raycast mesh.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include <glm/glm.hpp>

#include "client/graphics/player/ground_query.h"
#include "client/hc_map.h"

using EQT::Graphics::GroundZ;
using EQT::Graphics::ShouldClampSwimToFloor;
using EQT::Graphics::ShouldLandJump;
using EQT::Graphics::ClassifyBspGroundRay;

namespace {

// The value findGroundZBspFiltered() used to return for a ceiling. It is named
// here so the case below asserts on the ACTUAL defect, not on a paraphrase.
float LegacyCeilingSentinel(float feetZ) { return feetZ + 1000.0f; }

// ---- a hand-built V1 map file -----------------------------------------------
// V1 layout, straight from HCMap::Load(): u32 version 0x01000000, u32
// face_count, u16 node_count, u32 facelist_count, then per face three EQ-space
// vec3 vertices followed by four floats of (unused) normal data.
struct Tri { glm::vec3 a, b, c; };

void WriteV1Map(const std::filesystem::path& path, const std::vector<Tri>& tris) {
    FILE* f = fopen(path.string().c_str(), "wb");
    ASSERT_NE(f, nullptr) << "cannot write " << path;
    const uint32_t version = 0x01000000u;
    const uint32_t faceCount = static_cast<uint32_t>(tris.size());
    const uint16_t nodeCount = 0;
    const uint32_t facelistCount = 0;
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&faceCount, sizeof(faceCount), 1, f);
    fwrite(&nodeCount, sizeof(nodeCount), 1, f);
    fwrite(&facelistCount, sizeof(facelistCount), 1, f);
    for (const auto& t : tris) {
        fwrite(&t.a, sizeof(glm::vec3), 1, f);
        fwrite(&t.b, sizeof(glm::vec3), 1, f);
        fwrite(&t.c, sizeof(glm::vec3), 1, f);
        const float normals[4] = {0.0f, 0.0f, 1.0f, 0.0f};
        fwrite(normals, sizeof(normals), 1, f);
    }
    fclose(f);
}

// A big horizontal quad (two triangles) at EQ z == `z`, centred on the origin.
std::vector<Tri> Slab(float z, float half = 200.0f) {
    return {
        {{-half, -half, z}, {half, -half, z}, {half, half, z}},
        {{-half, -half, z}, {half, half, z}, {-half, half, z}},
    };
}

std::vector<Tri> Concat(std::vector<Tri> a, const std::vector<Tri>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

class HandBuiltMap {
public:
    explicit HandBuiltMap(const std::string& name, const std::vector<Tri>& tris) {
        path_ = std::filesystem::temp_directory_path() /
                ("willeq_t437_" + name + "_" + std::to_string(::getpid()) + ".map");
        WriteV1Map(path_, tris);
    }
    ~HandBuiltMap() { std::error_code ec; std::filesystem::remove(path_, ec); }
    const std::string path() const { return path_.string(); }
private:
    std::filesystem::path path_;
};

}  // namespace

// ---------------------------------------------------------------------------
// B435 -- the answer shape
// ---------------------------------------------------------------------------

TEST(GroundSentinelsT437, NoFloorIsNotAFloorAndCarriesNoNumber) {
    const GroundZ none = GroundZ::NoFloor();
    EXPECT_FALSE(none.hasFloor());
    // The whole point of the type: "no floor" answers with the CALLER's
    // fallback, never with a number of its own that could be snapped to.
    EXPECT_FLOAT_EQ(none.zOr(12.5f), 12.5f);
    EXPECT_FLOAT_EQ(none.zOr(-99.0f), -99.0f);

    const GroundZ floor = GroundZ::Floor(31.25f);
    EXPECT_TRUE(floor.hasFloor());
    EXPECT_FLOAT_EQ(floor.z(), 31.25f);
    EXPECT_FLOAT_EQ(floor.zOr(12.5f), 31.25f);
}

#ifdef EQT_HAS_GRAPHICS
// B1038: the jump/swim/miss verdicts live in ground_query.cpp, a renderer TU
// (GRAPHICS_SOURCES); compiled out of an EQT_GRAPHICS=OFF build. The HCMap
// half below (B436) is game-side and runs headless.
// ---------------------------------------------------------------------------
// B435 -- the four jump landings
// ---------------------------------------------------------------------------

TEST(GroundSentinelsT437, AJumpWhoseRayMeetsOnlyACeilingDoesNotLand) {
    const float feetZ = 10.0f;
    const float fallingVelocity = -4.0f;

    // THE DEFECT, stated as the arithmetic it was: the old comparison was
    // `feetZ <= groundZ && verticalVelocity <= 0` against the raw float, and
    // the ceiling answer was feetZ + 1000. Both halves were true.
    const float sentinel = LegacyCeilingSentinel(feetZ);
    EXPECT_TRUE(feetZ <= sentinel && fallingVelocity <= 0.0f)
        << "the pre-T437 comparison accepted the ceiling sentinel -- this line "
           "is the defect, restated";
    EXPECT_FLOAT_EQ(sentinel, 1010.0f);

    // The fix: a ceiling is NoFloor, and NoFloor never lands.
    EXPECT_FALSE(ShouldLandJump(GroundZ::NoFloor(), feetZ, fallingVelocity));
}

TEST(GroundSentinelsT437, AJumpStillLandsOnARealFloor) {
    // Falling, feet at or below the floor: lands.
    EXPECT_TRUE(ShouldLandJump(GroundZ::Floor(10.0f), 10.0f, -4.0f));
    EXPECT_TRUE(ShouldLandJump(GroundZ::Floor(10.0f), 9.0f, -4.0f));
    EXPECT_TRUE(ShouldLandJump(GroundZ::Floor(10.0f), 10.0f, 0.0f));
    // Still above the floor: keeps falling.
    EXPECT_FALSE(ShouldLandJump(GroundZ::Floor(10.0f), 10.5f, -4.0f));
    // Rising: a jump does not land on the way up, even through the floor plane.
    EXPECT_FALSE(ShouldLandJump(GroundZ::Floor(10.0f), 9.0f, 4.0f));
}

TEST(GroundSentinelsT437, NoLandingEverPutsTheFeetAThousandUnitsUp) {
    // The whole family, swept: for any falling jump and any ground answer, a
    // landing may only ever be at the floor the query actually reported.
    for (float feetZ : {-50.0f, 0.0f, 12.5f, 300.0f}) {
        EXPECT_FALSE(ShouldLandJump(GroundZ::NoFloor(), feetZ, -1.0f));
        // A floor 1,000 units up is not reachable as an answer any more: the
        // BSP query returns NoFloor above its step-up limit. But if one ever
        // were reported, it is reported AS a floor, and the landing z is that
        // floor -- never feetZ + a constant.
        const GroundZ high = GroundZ::Floor(LegacyCeilingSentinel(feetZ));
        EXPECT_TRUE(ShouldLandJump(high, feetZ, -1.0f));
        EXPECT_FLOAT_EQ(high.z(), feetZ + 1000.0f);
    }
}

// ---------------------------------------------------------------------------
// B435 -- the two swim clamps (the +100 heuristic they used to need)
// ---------------------------------------------------------------------------

TEST(GroundSentinelsT437, SwimDoesNotClampToACeiling) {
    EXPECT_FALSE(ShouldClampSwimToFloor(GroundZ::NoFloor(), 10.0f));
    EXPECT_FALSE(ShouldClampSwimToFloor(GroundZ::NoFloor(), -500.0f));
}

TEST(GroundSentinelsT437, SwimStillClampsToARealBottom) {
    EXPECT_TRUE(ShouldClampSwimToFloor(GroundZ::Floor(10.0f), 9.5f));
    // Resting exactly on the bottom is not a clamp -- the strict `<` is the
    // pre-T437 comparison, kept.
    EXPECT_FALSE(ShouldClampSwimToFloor(GroundZ::Floor(10.0f), 10.0f));
    EXPECT_FALSE(ShouldClampSwimToFloor(GroundZ::Floor(10.0f), 11.0f));
}

TEST(GroundSentinelsT437, TheOldPlusHundredHeuristicWasBothLeakyAndWrong) {
    // Why the guard the swim clamps used to carry is gone rather than kept.
    // (a) It was the ONLY thing keeping the sentinel out, and it leaked: with
    //     the player's feet 1,000+ units below their centre-z reference the
    //     sentinel slipped under `ctx_.playerZ + 100`.
    // (b) It also excluded a GENUINE floor more than 100 units above, which is
    //     a bottom a swimmer rising in a flooded shaft should clamp to.
    const float playerZ = 0.0f;
    const float maxReasonableGround = playerZ + 100.0f;
    const float realFloorHighAbove = 150.0f;
    EXPECT_FALSE(realFloorHighAbove < maxReasonableGround)
        << "a real floor 150 units up was rejected by the old heuristic";
    EXPECT_TRUE(ShouldClampSwimToFloor(GroundZ::Floor(realFloorHighAbove), 140.0f));
}

// ---------------------------------------------------------------------------
// T440 / B438 -- a TOTAL MISS is not a floor at your feet
//
// findGroundZBspFiltered() used to open its verdict with
// `float groundZ = feetZ;` and, when neither the near nor the full raycast hit
// anything at all, return Floor(feetZ). ShouldLandJump() then read
// `feetZ <= feetZ`, agreed, and ended a fall in mid-air over a hole. The
// verdict now lives in ClassifyBspGroundRay(), which is the seam these cases
// drive -- the query itself needs a live Irrlicht device, a loaded zone with
// region triangles and a BSP tree.
// ---------------------------------------------------------------------------

TEST(GroundMissT440, ARayThatHitsNothingReportsNoFloor) {
    // THE DEFECT, stated as the arithmetic it was.
    const float feetZ = 42.0f;
    const GroundZ legacyMissAnswer = GroundZ::Floor(feetZ);
    EXPECT_TRUE(ShouldLandJump(legacyMissAnswer, feetZ, -12.0f))
        << "the pre-T440 miss answer WAS a landing, at the falling player's own "
           "feet -- this line is the defect, restated";

    // The fix.
    const GroundZ miss = ClassifyBspGroundRay(/*hit=*/false, /*hitZ=*/0.0f, feetZ,
                                              /*maxStepUp=*/5.0f);
    EXPECT_FALSE(miss.hasFloor());
    EXPECT_FLOAT_EQ(miss.zOr(feetZ), feetZ) << "and it carries no number of its own";
}

TEST(GroundMissT440, AFallingPlayerOverAHoleKeepsFalling) {
    // The player-visible consequence, swept over a fall: at every height down
    // the hole the query misses, and at no height does the jump end.
    const float maxStepUp = 5.0f;
    for (float feetZ : {100.0f, 50.0f, 0.0f, -250.0f, -4000.0f}) {
        const GroundZ ground = ClassifyBspGroundRay(false, 0.0f, feetZ, maxStepUp);
        EXPECT_FALSE(ShouldLandJump(ground, feetZ, -30.0f)) << "at feetZ " << feetZ;
        EXPECT_FALSE(ShouldClampSwimToFloor(ground, feetZ)) << "at feetZ " << feetZ;
    }
}

TEST(GroundMissT440, TheFallEndsOnTheFirstRealFloorTheRayFinds) {
    // ... and it is a real landing when the ray does find one, at THAT floor,
    // not at the feet. This is the other half of "falls until the next floor".
    const float maxStepUp = 5.0f;
    const float pitFloorZ = -302.5f;

    // Still above the pit floor: the ray reports it, and the fall continues.
    const GroundZ seenFromAbove = ClassifyBspGroundRay(true, pitFloorZ, -300.0f, maxStepUp);
    ASSERT_TRUE(seenFromAbove.hasFloor());
    EXPECT_FLOAT_EQ(seenFromAbove.z(), pitFloorZ);
    EXPECT_FALSE(ShouldLandJump(seenFromAbove, -300.0f, -30.0f));

    // The frame the feet reach it: the jump ends AT the pit floor, and the
    // caller snaps to that z -- not to the feet, which is what the miss answer
    // used to hand it.
    const GroundZ reached = ClassifyBspGroundRay(true, pitFloorZ, -303.0f, maxStepUp);
    ASSERT_TRUE(reached.hasFloor());
    EXPECT_FLOAT_EQ(reached.z(), pitFloorZ);
    EXPECT_TRUE(ShouldLandJump(reached, -303.0f, -30.0f));
}

TEST(GroundMissT440, TheCeilingVerdictIsUnchangedByTheMissFix) {
    // T437's B435 case, now spelled by the same seam: geometry ABOVE the
    // step-up limit is still not a floor, and the boundary is still the
    // pre-existing `feetZ + maxStepUp + 0.1f`.
    const float feetZ = 10.0f;
    const float maxStepUp = 5.0f;
    EXPECT_FALSE(ClassifyBspGroundRay(true, 1010.0f, feetZ, maxStepUp).hasFloor());
    EXPECT_FALSE(ClassifyBspGroundRay(true, 15.2f, feetZ, maxStepUp).hasFloor());
    // Exactly at the limit is still reachable ground.
    EXPECT_TRUE(ClassifyBspGroundRay(true, 15.1f, feetZ, maxStepUp).hasFloor());
    EXPECT_TRUE(ClassifyBspGroundRay(true, 9.0f, feetZ, maxStepUp).hasFloor());
}

TEST(GroundMissT440, AMissAndACeilingGiveTheSameAnswerAndItIsNotAFloor) {
    // Three inputs, one verdict shape -- the property that lets every consumer
    // written since T437 handle a hole with the code it already has.
    const float feetZ = 0.0f;
    const float maxStepUp = 5.0f;
    for (const GroundZ& g : {ClassifyBspGroundRay(false, 0.0f, feetZ, maxStepUp),
                             ClassifyBspGroundRay(true, 200.0f, feetZ, maxStepUp)}) {
        EXPECT_FALSE(g.hasFloor());
        // The walking step-height path: `zOr(currentFeetZ)` makes the step zero
        // and the `hasFloor()` guard refuses the move, so a walker does not
        // stride out over a column with nothing under it.
        EXPECT_FLOAT_EQ(g.zOr(feetZ) - feetZ, 0.0f);
    }
}

#endif  // EQT_HAS_GRAPHICS

// ---------------------------------------------------------------------------
// B436 -- HCMap::FindBestZ() and its callers
// ---------------------------------------------------------------------------

TEST(FindBestZT437, AFloorBelowIsFound) {
    HandBuiltMap m("floor", Slab(0.0f));
    HCMap map;
    ASSERT_TRUE(map.Load(m.path()));
    glm::vec3 hit;
    const float z = map.FindBestZ(glm::vec3(0.0f, 0.0f, 50.0f), &hit);
    EXPECT_NE(z, (float)BEST_Z_INVALID);
    EXPECT_NEAR(z, 0.0f, 0.01f);
}

TEST(FindBestZT437, ACeilingOnlyIsNotAFloor) {
    // The B436 repro: geometry ONLY above the query point. Before T437 the
    // downward cast missed, the upward re-cast hit, and the ceiling's z came
    // back indistinguishable from a floor.
    HandBuiltMap m("ceiling", Slab(100.0f));
    HCMap map;
    ASSERT_TRUE(map.Load(m.path()));
    glm::vec3 hit;
    const float z = map.FindBestZ(glm::vec3(0.0f, 0.0f, 0.0f), &hit);
    EXPECT_EQ(z, (float)BEST_Z_INVALID)
        << "FindBestZ resolved a CEILING at z=" << z
        << " as ground for a query at z=0 (board B436)";
}

TEST(FindBestZT437, WithBothAFloorAndACeilingTheFloorWins) {
    HandBuiltMap m("both", Concat(Slab(0.0f), Slab(100.0f)));
    HCMap map;
    ASSERT_TRUE(map.Load(m.path()));
    glm::vec3 hit;
    const float z = map.FindBestZ(glm::vec3(0.0f, 0.0f, 50.0f), &hit);
    EXPECT_NEAR(z, 0.0f, 0.01f);
}

TEST(FindBestZT437, AnEmptyColumnIsInvalidNotAGuess) {
    HandBuiltMap m("offset", Slab(0.0f, 10.0f));
    HCMap map;
    ASSERT_TRUE(map.Load(m.path()));
    // Well outside the 10-unit slab in X.
    const float z = map.FindBestZ(glm::vec3(500.0f, 0.0f, 50.0f), nullptr);
    EXPECT_EQ(z, (float)BEST_Z_INVALID);
}

// The FixZ half of B436, expressed as the arithmetic ZoneComponent::FixZ()
// performs: GetBestZ() returns the caller's own z on BEST_Z_INVALID, so the
// difference is zero, so the `abs_diff > 1.5f` gate never fires and the
// position is left alone. Under a ceiling that used to be a write.
TEST(FindBestZT437, FixZLeavesThePositionWhenOnlyACeilingIsAbove) {
    HandBuiltMap m("ceiling_fixz", Slab(100.0f));
    HCMap map;
    ASSERT_TRUE(map.Load(m.path()));

    const float playerZ = 0.0f;
    const float best = map.FindBestZ(glm::vec3(0.0f, 0.0f, playerZ), nullptr);
    // GetBestZ()'s contract, verbatim: "Return current Z if no valid ground".
    const float getBestZ = (best == (float)BEST_Z_INVALID) ? playerZ : best;
    const float zDiff = getBestZ - playerZ;
    const float absDiff = zDiff < 0 ? -zDiff : zDiff;
    EXPECT_FLOAT_EQ(getBestZ, playerZ);
    EXPECT_FALSE(absDiff > 1.5f && absDiff < 20.0f)
        << "FixZ would have adjusted the player toward a ceiling";
}
