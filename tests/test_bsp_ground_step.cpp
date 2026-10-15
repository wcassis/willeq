// B1067 (GitLab #387) -- the keyboard-movement ground ray and step decision
// (include/client/graphics/player/bsp_ground_query.h) over a synthetic
// two-level floor.
//
// The geometry: an edge at EQ y = 0 between a floor at z = `from` (y < 0) and
// a floor at z = `to` (y >= 0), each a horizontal quad. The BSP tree has ONE
// split, a horizontal plane just above the higher floor, so both floors are
// the polygons of the thin LOWER leaf (region 1) while a standing player's
// centre (feet + 5) is in the UPPER leaf (region 0), which has no triangles at
// all -- the leaf layout qeynos2's curb at Y 391.75 has (review/B1067/
// README.md: the floor in region 1549, the centre in region 1083). The
// pre-B1067 ray tested the centre's leaf only, so it found no floor anywhere
// on this street and the player could not move.
//
// The table is the one #387 asked for, against collisionStepHeight 4.0:
// rises 1.0 / 3.9 step up, 4.1 does not; drops 1.0 / 7.9 step down, 8.1 does
// not (it falls instead).
//
// The step RULE stays inline in PlayerCameraComponent::resolveIrrlichtClearPath()
// (renderer_player_physics.cpp), whose statement order test_movement_order's
// golden pins, so it cannot move into a callable seam without re-blessing that
// golden. stepRule() below is a copy of it, and StepRuleMirrorsTheRenderer
// reads the renderer's source and fails if the inline rule changes without the
// copy.

#include <gtest/gtest.h>

#include "client/graphics/door_collision.h"
#include "client/graphics/player/bsp_ground_query.h"
#include "client/graphics/region_world_triangles.h"
#include "client/zone_bsp.h"

#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace EQT::Graphics;

namespace {

constexpr float kStep = 4.0f;          // PlayerModeConfig::collisionStepHeight's default
constexpr float kModelYOffset = 5.0f;  // centre = feet + 5: above the thin floor leaf

// Irrlicht Y-up: (EQ x, EQ z, EQ y).
irr::core::vector3df P(float x, float y, float z) { return {x, z, y}; }

// A horizontal quad at height z covering EQ x in [-50, 50], y in [y0, y1].
void addQuad(std::vector<irr::core::triangle3df>& out, float y0, float y1, float z) {
    out.emplace_back(P(-50, y0, z), P(50, y0, z), P(50, y1, z));
    out.emplace_back(P(-50, y0, z), P(50, y1, z), P(-50, y1, z));
}

enum class GroundStep { Snap, Fall, Blocked };

// resolveIrrlichtClearPath()'s walking branch, verbatim in effect:
//   stepHeight = groundZ.zOr(currentFeetZ) - currentFeetZ;
//   maxStepDown = collisionStepHeight * 2;
//   hasFloor && stepHeight <= step && stepHeight >= -maxStepDown -> snap
//   hasFloor && stepHeight < -maxStepDown                        -> fall
//   otherwise                                                    -> no move
GroundStep stepRule(const GroundZ& groundZ, float currentFeetZ, float collisionStepHeight) {
    const float stepHeight = groundZ.zOr(currentFeetZ) - currentFeetZ;
    const float maxStepDown = collisionStepHeight * 2.0f;
    if (groundZ.hasFloor() && stepHeight <= collisionStepHeight && stepHeight >= -maxStepDown)
        return GroundStep::Snap;
    if (groundZ.hasFloor() && stepHeight < -maxStepDown) return GroundStep::Fall;
    return GroundStep::Blocked;
}

struct TwoLevelFloor {
    BspTree bsp;
    RegionWorldTriangles tris;

    TwoLevelFloor(float from, float to) {
        // node 0: dot = z - split; >= 0 (above) -> node 1 = region 0 (air,
        // no triangles); < 0 (below) -> node 2 = region 1 (the floor sliver).
        const float split = std::max(from, to) + 0.5f;
        BspNode root;
        root.normalZ = 1.0f;
        root.splitDistance = -split;
        root.left = 1;
        root.right = 2;
        BspNode air;
        air.regionId = 1;
        BspNode sliver;
        sliver.regionId = 2;
        bsp.nodes = {root, air, sliver};
        bsp.regions = {std::make_shared<BspRegion>(), std::make_shared<BspRegion>()};

        std::vector<irr::core::triangle3df> floor;
        addQuad(floor, -50.0f, 0.0f, from);
        addQuad(floor, 0.0f, 50.0f, to);
        tris.setOwned(1, std::move(floor));
    }

    RegionRayTest test() const {
        return [this](size_t region, const irr::core::vector3df& o, const irr::core::vector3df& d,
                      float len, irr::core::vector3df& hit, float& distSq) {
            const auto view = tris.find(region);
            if (!view) return false;
            irr::core::triangle3df t;
            return RayIntersectTriangles(o, d, len, view, hit, t, distSq);
        };
    }

    // One walking frame from feet on the `from` floor at y = -0.5 to y = +0.5.
    GroundStep walk(float from, GroundZ* out = nullptr) const {
        const float centre = from + kModelYOffset;
        const GroundZ g = FindBspGround(bsp, test(), 0.0f, 0.5f, centre, kModelYOffset, kStep);
        if (out) *out = g;
        return stepRule(g, from, kStep);
    }
};

}  // namespace

// ---- the leaf traversal ---------------------------------------------------

TEST(BspSegmentRegions, ZeroLengthSegmentIsThePointsRegion) {
    TwoLevelFloor f(0.0f, 1.0f);
    for (float z : {-3.0f, 0.0f, 1.2f, 1.5f, 1.6f, 9.0f}) {
        std::vector<size_t> out;
        f.bsp.collectRegionsAlongSegment(0, 0, z, 0, 0, z, out);
        ASSERT_EQ(out.size(), 1u) << "z=" << z;
        EXPECT_EQ(out[0], f.bsp.findRegionIndexForPoint(0, 0, z)) << "z=" << z;
    }
}

TEST(BspSegmentRegions, DownwardRayCrossesTheSplitNearestLeafFirst) {
    TwoLevelFloor f(0.0f, 1.0f);   // split at z = 1.5
    std::vector<size_t> out;
    f.bsp.collectRegionsAlongSegment(0, 0, 9.0f, 0, 0, -9.0f, out);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0], 0u);  // the air leaf, where the ray starts
    EXPECT_EQ(out[1], 1u);  // the floor sliver
    out.clear();
    f.bsp.collectRegionsAlongSegment(0, 0, 9.0f, 0, 0, 2.0f, out);  // stays above the split
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], 0u);
}

// ---- the defect -----------------------------------------------------------

TEST(BspGroundB1067, TheCentresLeafAloneHasNoFloorButTheRayFindsIt) {
    TwoLevelFloor f(0.0f, 0.0f);
    const float centre = kModelYOffset;  // feet on z = 0
    const size_t centreRegion = f.bsp.findRegionIndexForPoint(0.0f, 0.5f, centre);
    ASSERT_EQ(centreRegion, 0u);
    EXPECT_FALSE(f.tris.find(centreRegion)) << "the centre's leaf holds no triangles";

    // The pre-B1067 query: only the centre's region was tested.
    const RegionRayTest full = f.test();
    const RegionRayTest centreOnly = [&](size_t region, const irr::core::vector3df& o,
                                         const irr::core::vector3df& d, float len,
                                         irr::core::vector3df& hit, float& distSq) {
        return region == centreRegion && full(region, o, d, len, hit, distSq);
    };
    EXPECT_FALSE(FindBspGround(f.bsp, centreOnly, 0.0f, 0.5f, centre, kModelYOffset, kStep).hasFloor())
        << "pre-B1067 answer on FLAT ground: no floor -> the move is refused";

    const GroundZ g = FindBspGround(f.bsp, full, 0.0f, 0.5f, centre, kModelYOffset, kStep);
    ASSERT_TRUE(g.hasFloor());
    EXPECT_NEAR(g.z(), 0.0f, 1e-4f);
    EXPECT_EQ(stepRule(g, 0.0f, kStep), GroundStep::Snap);
}

// ---- the step table (#387) ------------------------------------------------

TEST(BspGroundStepTable, RisesOneAndThreePointNineStepUpFourPointOneDoesNot) {
    for (float rise : {1.0f, 3.9f}) {
        TwoLevelFloor f(0.0f, rise);
        GroundZ g = GroundZ::NoFloor();
        EXPECT_EQ(f.walk(0.0f, &g), GroundStep::Snap) << "rise " << rise;
        ASSERT_TRUE(g.hasFloor()) << "rise " << rise;
        EXPECT_NEAR(g.z(), rise, 1e-4f);
    }
    TwoLevelFloor f(0.0f, 4.1f);
    EXPECT_EQ(f.walk(0.0f), GroundStep::Blocked) << "rise 4.1 is over collisionStepHeight 4.0";
}

TEST(BspGroundStepTable, DropsOneAndSevenPointNineStepDownEightPointOneFalls) {
    for (float drop : {1.0f, 7.9f}) {
        TwoLevelFloor f(drop, 0.0f);
        GroundZ g = GroundZ::NoFloor();
        EXPECT_EQ(f.walk(drop, &g), GroundStep::Snap) << "drop " << drop;
        ASSERT_TRUE(g.hasFloor()) << "drop " << drop;
        EXPECT_NEAR(g.z(), 0.0f, 1e-4f);
    }
    TwoLevelFloor f(8.1f, 0.0f);
    GroundZ g = GroundZ::NoFloor();
    EXPECT_EQ(f.walk(8.1f, &g), GroundStep::Fall) << "a drop past 2 x 4.0 is a fall, not a step";
    ASSERT_TRUE(g.hasFloor());
    EXPECT_NEAR(g.z(), 0.0f, 1e-4f);
}

TEST(BspGroundStepTable, StepRuleBoundaries) {
    EXPECT_EQ(stepRule(GroundZ::NoFloor(), 0.0f, kStep), GroundStep::Blocked);
    EXPECT_EQ(stepRule(GroundZ::Floor(4.0f), 0.0f, kStep), GroundStep::Snap);
    EXPECT_EQ(stepRule(GroundZ::Floor(4.01f), 0.0f, kStep), GroundStep::Blocked);
    EXPECT_EQ(stepRule(GroundZ::Floor(-8.0f), 0.0f, kStep), GroundStep::Snap);
    EXPECT_EQ(stepRule(GroundZ::Floor(-8.01f), 0.0f, kStep), GroundStep::Fall);
}

TEST(BspGroundStepTable, StepRuleMirrorsTheRenderer) {
    const std::string path = std::string(WILLEQ_SOURCE_DIR) +
                             "/src/client/graphics/player/renderer_player_physics.cpp";
    std::ifstream in(path);
    ASSERT_TRUE(in.is_open()) << path;
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    for (const char* line : {
             "float stepHeight = groundZ.zOr(currentFeetZ) - currentFeetZ;",
             "float maxStepDown = playerConfig_.collisionStepHeight * 2.0f;",
             "if (groundZ.hasFloor() && stepHeight <= playerConfig_.collisionStepHeight && stepHeight >= -maxStepDown) {",
             "} else if (groundZ.hasFloor() && stepHeight < -maxStepDown) {",
             "GroundZ groundZ = findGroundZBspFiltered(newX, newY, newZ, modelYOffset);",
         }) {
        EXPECT_NE(src.find(line), std::string::npos)
            << "renderer_player_physics.cpp no longer has: " << line
            << "\n-- update stepRule() in this test to the new rule";
    }
}
