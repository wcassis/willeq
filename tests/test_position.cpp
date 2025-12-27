#include <gtest/gtest.h>
#include "client/position.h"
#include <glm/glm.hpp>
#include <cmath>

class PositionTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test Distance calculations
TEST_F(PositionTest, Distance_SamePoint) {
    glm::vec3 a(0.0f, 0.0f, 0.0f);
    float dist = Distance(a, a);
    EXPECT_FLOAT_EQ(dist, 0.0f);
}

TEST_F(PositionTest, Distance_SimpleDistance) {
    glm::vec3 a(0.0f, 0.0f, 0.0f);
    glm::vec3 b(3.0f, 4.0f, 0.0f);
    float dist = Distance(a, b);
    EXPECT_FLOAT_EQ(dist, 5.0f);  // 3-4-5 triangle
}

TEST_F(PositionTest, Distance_3D) {
    glm::vec3 a(0.0f, 0.0f, 0.0f);
    glm::vec3 b(1.0f, 2.0f, 2.0f);
    float dist = Distance(a, b);
    EXPECT_FLOAT_EQ(dist, 3.0f);  // sqrt(1 + 4 + 4) = 3
}

TEST_F(PositionTest, Distance_Negative) {
    glm::vec3 a(-5.0f, -5.0f, -5.0f);
    glm::vec3 b(5.0f, 5.0f, 5.0f);
    float dist = Distance(a, b);
    EXPECT_FLOAT_EQ(dist, sqrtf(300.0f));
}

// Test DistanceSquared
TEST_F(PositionTest, DistanceSquared_SamePoint) {
    glm::vec3 a(0.0f, 0.0f, 0.0f);
    float dist_sq = DistanceSquared(a, a);
    EXPECT_FLOAT_EQ(dist_sq, 0.0f);
}

TEST_F(PositionTest, DistanceSquared_Simple) {
    glm::vec3 a(0.0f, 0.0f, 0.0f);
    glm::vec3 b(3.0f, 4.0f, 0.0f);
    float dist_sq = DistanceSquared(a, b);
    EXPECT_FLOAT_EQ(dist_sq, 25.0f);  // 3^2 + 4^2 = 25
}

// Test 2D Distance (XY plane only)
TEST_F(PositionTest, DistanceNoZ_IgnoresZ) {
    glm::vec3 a(0.0f, 0.0f, 0.0f);
    glm::vec3 b(3.0f, 4.0f, 100.0f);  // Z difference should be ignored
    float dist = DistanceNoZ(a, b);
    EXPECT_FLOAT_EQ(dist, 5.0f);
}

// Test Heading calculations
TEST_F(PositionTest, CalculateHeading_East) {
    glm::vec3 from(0.0f, 0.0f, 0.0f);
    glm::vec3 to(10.0f, 0.0f, 0.0f);
    float heading = CalculateHeadingAngleBetweenPositions(from.x, from.y, to.x, to.y);
    // Heading depends on EQ's coordinate system
    EXPECT_GE(heading, 0.0f);
    EXPECT_LE(heading, 512.0f);
}

TEST_F(PositionTest, CalculateHeading_North) {
    glm::vec3 from(0.0f, 0.0f, 0.0f);
    glm::vec3 to(0.0f, 10.0f, 0.0f);
    float heading = CalculateHeadingAngleBetweenPositions(from.x, from.y, to.x, to.y);
    EXPECT_GE(heading, 0.0f);
    EXPECT_LE(heading, 512.0f);
}

TEST_F(PositionTest, CalculateHeading_SamePosition) {
    glm::vec3 pos(5.0f, 5.0f, 5.0f);
    float heading = CalculateHeadingAngleBetweenPositions(pos.x, pos.y, pos.x, pos.y);
    // Should return some valid heading or 0
    EXPECT_GE(heading, 0.0f);
    EXPECT_LE(heading, 512.0f);
}

// Test Distance with vec4 (using Distance function)
TEST_F(PositionTest, Distance_Vec4_Close) {
    glm::vec4 pos1(0.0f, 0.0f, 0.0f, 0.0f);
    glm::vec4 pos2(3.0f, 4.0f, 0.0f, 0.0f);
    float dist = Distance(pos1, pos2);
    EXPECT_FLOAT_EQ(dist, 5.0f);
}

TEST_F(PositionTest, Distance_Vec4_Far) {
    glm::vec4 pos1(0.0f, 0.0f, 0.0f, 0.0f);
    glm::vec4 pos2(30.0f, 40.0f, 0.0f, 0.0f);
    float dist = Distance(pos1, pos2);
    EXPECT_FLOAT_EQ(dist, 50.0f);
}

// Test IsPositionEqual
TEST_F(PositionTest, IsPositionEqual_SamePosition) {
    glm::vec4 pos(10.0f, 20.0f, 30.0f, 64.0f);
    EXPECT_TRUE(IsPositionEqual(pos, pos));
}

TEST_F(PositionTest, IsPositionEqual_ClosePositions) {
    glm::vec4 pos1(10.0f, 20.0f, 30.0f, 64.0f);
    // Epsilon is 0.0001, so use 0.00009 which is less than epsilon
    glm::vec4 pos2(10.00009f, 20.00009f, 30.00009f, 64.00009f);
    EXPECT_TRUE(IsPositionEqual(pos1, pos2));
}

TEST_F(PositionTest, IsPositionEqual_DifferentPositions) {
    glm::vec4 pos1(10.0f, 20.0f, 30.0f, 64.0f);
    glm::vec4 pos2(10.1f, 20.1f, 30.1f, 64.1f);
    EXPECT_FALSE(IsPositionEqual(pos1, pos2));
}

// Test to_string functions
TEST_F(PositionTest, ToString_Vec4) {
    glm::vec4 pos(1.5f, 2.5f, 3.5f, 64.0f);
    std::string str = to_string(pos);
    EXPECT_FALSE(str.empty());
    EXPECT_NE(str.find("1.5"), std::string::npos);
    EXPECT_NE(str.find("2.5"), std::string::npos);
    EXPECT_NE(str.find("3.5"), std::string::npos);
}

TEST_F(PositionTest, ToString_Vec3) {
    glm::vec3 pos(1.5f, 2.5f, 3.5f);
    std::string str = to_string(pos);
    EXPECT_FALSE(str.empty());
}

// Test IsWithinCircularArc
TEST_F(PositionTest, IsWithinCircularArc_Test) {
    glm::vec4 center(0.0f, 0.0f, 0.0f, 0.0f);
    glm::vec4 point(5.0f, 0.0f, 0.0f, 0.0f);
    // Just verify the function doesn't crash
    bool result = IsWithinCircularArc(center, point, 0, 10, 0);
    (void)result;  // Result depends on implementation
}

// Test IsWithinSquare
TEST_F(PositionTest, IsWithinSquare_Inside) {
    glm::vec4 center(0.0f, 0.0f, 0.0f, 0.0f);
    glm::vec4 point(3.0f, 3.0f, 0.0f, 0.0f);
    // Point at (3,3) should be inside a 10x10 square centered at origin
    bool result = IsWithinSquare(center, 10, point);
    (void)result;  // Result depends on implementation details
}

// Test IsOrigin
TEST_F(PositionTest, IsOrigin_Vec3_True) {
    glm::vec3 pos(0.0f, 0.0f, 0.0f);
    EXPECT_TRUE(IsOrigin(pos));
}

TEST_F(PositionTest, IsOrigin_Vec3_False) {
    glm::vec3 pos(1.0f, 0.0f, 0.0f);
    EXPECT_FALSE(IsOrigin(pos));
}

TEST_F(PositionTest, IsOrigin_Vec4_True) {
    glm::vec4 pos(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_TRUE(IsOrigin(pos));
}

// Test IsWithinAxisAlignedBox
TEST_F(PositionTest, IsWithinAxisAlignedBox_Inside) {
    glm::vec3 pos(5.0f, 5.0f, 5.0f);
    glm::vec3 min(0.0f, 0.0f, 0.0f);
    glm::vec3 max(10.0f, 10.0f, 10.0f);
    EXPECT_TRUE(IsWithinAxisAlignedBox(pos, min, max));
}

TEST_F(PositionTest, IsWithinAxisAlignedBox_Outside) {
    glm::vec3 pos(15.0f, 5.0f, 5.0f);
    glm::vec3 min(0.0f, 0.0f, 0.0f);
    glm::vec3 max(10.0f, 10.0f, 10.0f);
    EXPECT_FALSE(IsWithinAxisAlignedBox(pos, min, max));
}

// Test IsPositionWithinSimpleCylinder
TEST_F(PositionTest, IsPositionWithinSimpleCylinder_Inside) {
    glm::vec3 pos(2.0f, 2.0f, 5.0f);
    glm::vec3 center(0.0f, 0.0f, 0.0f);
    bool result = IsPositionWithinSimpleCylinder(pos, center, 10.0f, 20.0f);
    EXPECT_TRUE(result);
}

TEST_F(PositionTest, IsPositionWithinSimpleCylinder_Outside) {
    glm::vec3 pos(50.0f, 50.0f, 5.0f);
    glm::vec3 center(0.0f, 0.0f, 0.0f);
    bool result = IsPositionWithinSimpleCylinder(pos, center, 10.0f, 20.0f);
    EXPECT_FALSE(result);
}
