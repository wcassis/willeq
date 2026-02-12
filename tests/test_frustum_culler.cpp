#include <gtest/gtest.h>
#include "client/graphics/frustum_culler.h"
#include <cmath>

using namespace EQT::Graphics;

// EQ coordinate system: Z-up
// Camera forward direction provided directly (from Irrlicht camera, converted to EQ)

class FrustumCullerTest : public ::testing::Test {
protected:
    FrustumCuller culler;
    static constexpr float PI = 3.14159265358979323846f;

    // Helper: set up camera at position looking in a direction derived from yaw/pitch
    // yawDeg: 0 = +Y (North), 90 = +X (East) in EQ coords
    // pitchDeg: positive = looking up
    void setupCamera(float camX, float camY, float camZ,
                     float yawDeg, float pitchDeg,
                     float fovDeg = 72.0f, float aspect = 4.0f/3.0f,
                     float nearDist = 1.0f, float farDist = 300.0f) {
        float yawRad = yawDeg * PI / 180.0f;
        float pitchRad = pitchDeg * PI / 180.0f;

        // Forward direction in EQ coords (Z-up)
        // yaw=0 -> +Y (North), yaw=90 -> +X (East)
        float fwdX = std::sin(yawRad) * std::cos(pitchRad);
        float fwdY = std::cos(yawRad) * std::cos(pitchRad);
        float fwdZ = std::sin(pitchRad);

        float fovRad = fovDeg * PI / 180.0f;
        culler.update(camX, camY, camZ, fwdX, fwdY, fwdZ,
                      fovRad, aspect, nearDist, farDist);
    }
};

// Camera at origin looking North (+Y), objects in front should be visible
TEST_F(FrustumCullerTest, AABBInFrontIsVisible) {
    setupCamera(0, 0, 0, 0, 0);  // Looking along +Y

    // Box 100 units ahead along +Y
    EXPECT_TRUE(culler.testAABB(-5, 95, -5, 5, 105, 5));
}

// Object behind camera should be invisible
TEST_F(FrustumCullerTest, AABBBehindIsInvisible) {
    setupCamera(0, 0, 0, 0, 0);  // Looking along +Y

    // Box behind camera (negative Y)
    EXPECT_FALSE(culler.testAABB(-5, -105, -5, 5, -95, 5));
}

// Object far to the left should be invisible
TEST_F(FrustumCullerTest, AABBFarLeftIsInvisible) {
    setupCamera(0, 0, 0, 0, 0);  // Looking along +Y

    // Box far to the right (+X) at same Y distance
    EXPECT_FALSE(culler.testAABB(195, 95, -5, 205, 105, 5));
}

// Object beyond far plane should be invisible
TEST_F(FrustumCullerTest, AABBBeyondFarPlaneIsInvisible) {
    setupCamera(0, 0, 0, 0, 0, 72, 4.0f/3.0f, 1.0f, 300.0f);

    // Box 400 units ahead (beyond 300 far plane)
    EXPECT_FALSE(culler.testAABB(-5, 395, -5, 5, 405, 5));
}

// Object inside far plane should be visible
TEST_F(FrustumCullerTest, AABBInsideFarPlaneIsVisible) {
    setupCamera(0, 0, 0, 0, 0, 72, 4.0f/3.0f, 1.0f, 300.0f);

    // Box 200 units ahead (within 300 far plane)
    EXPECT_TRUE(culler.testAABB(-5, 195, -5, 5, 205, 5));
}

// AABB partially overlapping the frustum edge should be visible
TEST_F(FrustumCullerTest, AABBPartialOverlapIsVisible) {
    setupCamera(0, 0, 0, 0, 0);  // Looking along +Y

    // Large box that straddles the frustum edge
    EXPECT_TRUE(culler.testAABB(0, 45, -5, 100, 55, 5));
}

// Sphere in front of camera should be visible
TEST_F(FrustumCullerTest, SphereInFrontIsVisible) {
    setupCamera(0, 0, 0, 0, 0);

    // Sphere at (0, 100, 0) radius 5
    EXPECT_TRUE(culler.testSphere(0, 100, 0, 5.0f));
}

// Sphere behind camera should be invisible
TEST_F(FrustumCullerTest, SphereBehindIsInvisible) {
    setupCamera(0, 0, 0, 0, 0);

    // Sphere at (0, -100, 0) radius 5
    EXPECT_FALSE(culler.testSphere(0, -100, 0, 5.0f));
}

// Large sphere near frustum edge should be visible (partially inside)
TEST_F(FrustumCullerTest, LargeSphereAtEdgeIsVisible) {
    setupCamera(0, 0, 0, 0, 0);

    // Large sphere just barely behind but with big radius
    EXPECT_TRUE(culler.testSphere(0, -5, 0, 20.0f));
}

// Test 90-degree rotation: what was visible becomes invisible
TEST_F(FrustumCullerTest, RotationChangesVisibility) {
    // Looking North (+Y)
    setupCamera(0, 0, 0, 0, 0);
    EXPECT_TRUE(culler.testAABB(-5, 95, -5, 5, 105, 5));  // Ahead = visible

    // Rotate 90 degrees (yaw=90 = looking East/+X)
    setupCamera(0, 0, 0, 90, 0);
    EXPECT_FALSE(culler.testAABB(-5, 95, -5, 5, 105, 5));  // Was ahead, now to the side
}

// Test looking straight up
TEST_F(FrustumCullerTest, LookingStraightUp) {
    setupCamera(0, 0, 0, 0, 89);  // Looking almost straight up

    // Object above camera should be visible
    EXPECT_TRUE(culler.testAABB(-5, -5, 95, 5, 5, 105));

    // Object below camera should be invisible
    EXPECT_FALSE(culler.testAABB(-5, -5, -105, 5, 5, -95));
}

// Test looking straight down
TEST_F(FrustumCullerTest, LookingStraightDown) {
    setupCamera(0, 0, 100, 0, -89);  // Looking almost straight down from Z=100

    // Object below camera should be visible
    EXPECT_TRUE(culler.testAABB(-5, -5, -5, 5, 5, 5));

    // Object above camera should be invisible
    EXPECT_FALSE(culler.testAABB(-5, -5, 195, 5, 5, 205));
}

// Test that dirty checking works (same params don't rebuild)
TEST_F(FrustumCullerTest, DirtyCheckingWorks) {
    setupCamera(0, 0, 0, 0, 0);
    bool result1 = culler.testAABB(-5, 95, -5, 5, 105, 5);

    // Call update again with same params - should give same results
    setupCamera(0, 0, 0, 0, 0);
    bool result2 = culler.testAABB(-5, 95, -5, 5, 105, 5);

    EXPECT_EQ(result1, result2);
}

// Test camera offset position
TEST_F(FrustumCullerTest, CameraAtNonOriginPosition) {
    setupCamera(100, 200, 50, 0, 0);  // Camera at (100, 200, 50), looking North

    // Object 100 units ahead of camera
    EXPECT_TRUE(culler.testAABB(95, 295, 45, 105, 305, 55));

    // Object 100 units behind camera
    EXPECT_FALSE(culler.testAABB(95, 95, 45, 105, 105, 55));
}

// Test with very wide FOV
TEST_F(FrustumCullerTest, WideFOV) {
    setupCamera(0, 0, 0, 0, 0, 120);  // 120 degree vertical FOV

    // Object directly ahead should be visible
    EXPECT_TRUE(culler.testAABB(-5, 95, -5, 5, 105, 5));

    // Object behind camera should still be invisible even with wide FOV
    EXPECT_FALSE(culler.testAABB(-5, -105, -5, 5, -95, 5));

    // Object at a moderate angle ahead should be visible with wide FOV
    EXPECT_TRUE(culler.testAABB(95, 45, -5, 105, 55, 5));
}

// Test with narrow FOV
TEST_F(FrustumCullerTest, NarrowFOV) {
    setupCamera(0, 0, 0, 0, 0, 30);  // 30 degree vertical FOV

    // Object at a very steep angle should be invisible with narrow FOV
    EXPECT_FALSE(culler.testAABB(195, 45, -5, 205, 55, 5));

    // Object directly ahead should still be visible
    EXPECT_TRUE(culler.testAABB(-5, 95, -5, 5, 105, 5));
}

// Test that disabled returns true for everything
TEST_F(FrustumCullerTest, DisabledReturnsTrue) {
    culler.setEnabled(false);
    setupCamera(0, 0, 0, 0, 0);

    // Behind camera should still return true when disabled
    EXPECT_TRUE(culler.testAABB(-5, -105, -5, 5, -95, 5));
    EXPECT_TRUE(culler.testSphere(0, -100, 0, 5.0f));
}

// Test toggle
TEST_F(FrustumCullerTest, ToggleWorks) {
    EXPECT_TRUE(culler.isEnabled());  // Default on

    culler.toggle();
    EXPECT_FALSE(culler.isEnabled());

    culler.toggle();
    EXPECT_TRUE(culler.isEnabled());
}

// Test yaw=90 looks East (+X)
TEST_F(FrustumCullerTest, Yaw90LooksEast) {
    setupCamera(0, 0, 0, 90, 0);

    // yaw=90 -> fwd=(sin90, cos90, 0) = (1, 0, 0) = East (+X)
    // Box at +X should be visible
    EXPECT_TRUE(culler.testAABB(95, -5, -5, 105, 5, 5));

    // Box at -X should be invisible (behind)
    EXPECT_FALSE(culler.testAABB(-105, -5, -5, -95, 5, 5));
}

// Test with direct direction vector (not via yaw/pitch helper)
TEST_F(FrustumCullerTest, DirectDirectionVector) {
    // Pass direction directly: looking along +X
    float fovRad = 72.0f * PI / 180.0f;
    culler.update(0, 0, 0, 1.0f, 0, 0, fovRad, 4.0f/3.0f, 1.0f, 300.0f);

    // Box at +X should be visible
    EXPECT_TRUE(culler.testAABB(95, -5, -5, 105, 5, 5));

    // Box at -X should be invisible
    EXPECT_FALSE(culler.testAABB(-105, -5, -5, -95, 5, 5));

    // Box at +Y should be invisible (to the side)
    EXPECT_FALSE(culler.testAABB(-5, 195, -5, 5, 205, 5));
}

// Test with a downward-looking direction (simulates third-person camera)
TEST_F(FrustumCullerTest, DownwardLookingDirection) {
    // Camera 20 units above and behind, looking forward and down
    // Simulates a third-person camera: at (0, -20, 20) looking at (0, 10, 0)
    // Direction: (0, 30, -20) (forward and down)
    float fovRad = 72.0f * PI / 180.0f;
    culler.update(0, -20, 20, 0, 30.0f, -20.0f, fovRad, 4.0f/3.0f, 1.0f, 300.0f);

    // Object ahead and below should be visible
    EXPECT_TRUE(culler.testAABB(-5, 95, -5, 5, 105, 5));

    // Object behind camera should be invisible
    EXPECT_FALSE(culler.testAABB(-5, -105, -5, 5, -95, 5));
}
