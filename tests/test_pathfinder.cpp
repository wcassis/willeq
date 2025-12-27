#include <gtest/gtest.h>
#include "client/pathfinder_interface.h"
#include "client/pathfinder_null.h"
#include <glm/glm.hpp>

class PathfinderTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test PathingPolyFlags enum values
TEST_F(PathfinderTest, PathingPolyFlagsValues) {
    EXPECT_EQ(PathingNormal, 1);
    EXPECT_EQ(PathingWater, 2);
    EXPECT_EQ(PathingLava, 4);
    EXPECT_EQ(PathingZoneLine, 8);
    EXPECT_EQ(PathingPvP, 16);
    EXPECT_EQ(PathingSlime, 32);
    EXPECT_EQ(PathingIce, 64);
    EXPECT_EQ(PathingVWater, 128);
    EXPECT_EQ(PathingGeneralArea, 256);
    EXPECT_EQ(PathingPortal, 512);
    EXPECT_EQ(PathingPrefer, 1024);
    EXPECT_EQ(PathingDisabled, 2048);
    EXPECT_EQ(PathingAll, 65535);
    EXPECT_EQ(PathingNotDisabled, PathingAll ^ PathingDisabled);
}

// Test PathfinderOptions default values
TEST_F(PathfinderTest, PathfinderOptionsDefaults) {
    PathfinderOptions opts;

    EXPECT_EQ(opts.flags, PathingNotDisabled);
    EXPECT_TRUE(opts.smooth_path);
    EXPECT_FLOAT_EQ(opts.step_size, 10.0f);
    EXPECT_FLOAT_EQ(opts.offset, 3.25f);

    // Check flag costs
    EXPECT_FLOAT_EQ(opts.flag_cost[0], 1.0f);  // Normal
    EXPECT_FLOAT_EQ(opts.flag_cost[1], 3.0f);  // Water
    EXPECT_FLOAT_EQ(opts.flag_cost[2], 5.0f);  // Lava
    EXPECT_FLOAT_EQ(opts.flag_cost[3], 1.0f);
    EXPECT_FLOAT_EQ(opts.flag_cost[4], 2.0f);
    EXPECT_FLOAT_EQ(opts.flag_cost[5], 2.0f);
    EXPECT_FLOAT_EQ(opts.flag_cost[6], 4.0f);
    EXPECT_FLOAT_EQ(opts.flag_cost[7], 1.0f);
    EXPECT_FLOAT_EQ(opts.flag_cost[8], 0.1f);
    EXPECT_FLOAT_EQ(opts.flag_cost[9], 0.1f);
}

// Test IPathNode construction with position
TEST_F(PathfinderTest, IPathNodeWithPosition) {
    glm::vec3 pos(100.0f, 200.0f, 50.0f);
    IPathfinder::IPathNode node(pos);

    EXPECT_FLOAT_EQ(node.pos.x, 100.0f);
    EXPECT_FLOAT_EQ(node.pos.y, 200.0f);
    EXPECT_FLOAT_EQ(node.pos.z, 50.0f);
    EXPECT_FALSE(node.teleport);
}

// Test IPathNode construction with teleport flag
TEST_F(PathfinderTest, IPathNodeWithTeleport) {
    IPathfinder::IPathNode node(true);
    EXPECT_TRUE(node.teleport);

    IPathfinder::IPathNode node2(false);
    EXPECT_FALSE(node2.teleport);
}

// Test NullPathfinder (fallback implementation)
TEST_F(PathfinderTest, NullPathfinderFindRoute) {
    PathfinderNull pathfinder;

    glm::vec3 start(0.0f, 0.0f, 0.0f);
    glm::vec3 end(100.0f, 100.0f, 0.0f);
    bool partial = false;
    bool stuck = false;

    auto path = pathfinder.FindRoute(start, end, partial, stuck);

    // NullPathfinder returns a direct path from start to end
    EXPECT_EQ(path.size(), 2u);
    EXPECT_FALSE(partial);
    EXPECT_FALSE(stuck);

    auto it = path.begin();
    EXPECT_FLOAT_EQ(it->pos.x, start.x);
    EXPECT_FLOAT_EQ(it->pos.y, start.y);
    EXPECT_FLOAT_EQ(it->pos.z, start.z);

    ++it;
    EXPECT_FLOAT_EQ(it->pos.x, end.x);
    EXPECT_FLOAT_EQ(it->pos.y, end.y);
    EXPECT_FLOAT_EQ(it->pos.z, end.z);
}

// Test NullPathfinder FindPath
TEST_F(PathfinderTest, NullPathfinderFindPath) {
    PathfinderNull pathfinder;

    glm::vec3 start(0.0f, 0.0f, 0.0f);
    glm::vec3 end(100.0f, 100.0f, 0.0f);
    bool partial = false;
    bool stuck = false;
    PathfinderOptions opts;

    auto path = pathfinder.FindPath(start, end, partial, stuck, opts);

    // NullPathfinder returns a direct path from start to end
    EXPECT_EQ(path.size(), 2u);
    EXPECT_FALSE(partial);
    EXPECT_FALSE(stuck);
}

// Test NullPathfinder GetRandomLocation
TEST_F(PathfinderTest, NullPathfinderRandomLocation) {
    PathfinderNull pathfinder;

    glm::vec3 start(50.0f, 50.0f, 10.0f);

    glm::vec3 result = pathfinder.GetRandomLocation(start);

    // NullPathfinder returns (0,0,0) as it can't compute random locations
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

// Test that flag combinations work correctly
TEST_F(PathfinderTest, PathingFlagCombinations) {
    // Test combining flags
    int water_and_normal = PathingWater | PathingNormal;
    EXPECT_EQ(water_and_normal, 3);

    int lava_and_ice = PathingLava | PathingIce;
    EXPECT_EQ(lava_and_ice, 68);

    // Test checking flags
    EXPECT_TRUE((water_and_normal & PathingWater) != 0);
    EXPECT_TRUE((water_and_normal & PathingNormal) != 0);
    EXPECT_FALSE((water_and_normal & PathingLava) != 0);
}

// Test IPath (list of path nodes)
TEST_F(PathfinderTest, IPathList) {
    IPathfinder::IPath path;

    path.push_back(IPathfinder::IPathNode(glm::vec3(0.0f, 0.0f, 0.0f)));
    path.push_back(IPathfinder::IPathNode(glm::vec3(10.0f, 10.0f, 0.0f)));
    path.push_back(IPathfinder::IPathNode(true)); // Teleport node
    path.push_back(IPathfinder::IPathNode(glm::vec3(100.0f, 100.0f, 50.0f)));

    EXPECT_EQ(path.size(), 4u);

    auto it = path.begin();
    EXPECT_FLOAT_EQ(it->pos.x, 0.0f);
    EXPECT_FALSE(it->teleport);

    ++it;
    EXPECT_FLOAT_EQ(it->pos.x, 10.0f);
    EXPECT_FALSE(it->teleport);

    ++it;
    EXPECT_TRUE(it->teleport);

    ++it;
    EXPECT_FLOAT_EQ(it->pos.x, 100.0f);
    EXPECT_FALSE(it->teleport);
}
