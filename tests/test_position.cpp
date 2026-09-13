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
