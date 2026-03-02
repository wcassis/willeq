#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "client/graphics/work_priority.h"

using namespace EQT::Graphics;

// ===========================================================================
// WorkPriorityKey ordering tests
// ===========================================================================

TEST(WorkPriorityKey, DepthZeroBeforeDepthOne) {
    auto d0 = WorkPriorityKey::make(0, AssetType::ZoneMesh);
    auto d1 = WorkPriorityKey::make(1, AssetType::ZoneMesh);
    EXPECT_LT(d0.value, d1.value);
    EXPECT_TRUE(d0 < d1);
}

TEST(WorkPriorityKey, DepthDominatesAssetType) {
    // Depth 0 Icon should be higher priority than depth 1 ZoneMesh
    auto d0icon = WorkPriorityKey::make(0, AssetType::Icon);
    auto d1mesh = WorkPriorityKey::make(1, AssetType::ZoneMesh);
    EXPECT_LT(d0icon.value, d1mesh.value);
}

TEST(WorkPriorityKey, SameDepthAssetTypeOrdering) {
    auto zoneMesh = WorkPriorityKey::make(0, AssetType::ZoneMesh);
    auto zoneTex  = WorkPriorityKey::make(0, AssetType::ZoneTexture);
    auto door     = WorkPriorityKey::make(0, AssetType::Door);
    auto entMesh  = WorkPriorityKey::make(0, AssetType::EntityMesh);
    auto entTex   = WorkPriorityKey::make(0, AssetType::EntityTexture);
    auto light    = WorkPriorityKey::make(0, AssetType::LightEffect);
    auto icon     = WorkPriorityKey::make(0, AssetType::Icon);

    EXPECT_LT(zoneMesh.value, zoneTex.value);
    EXPECT_LT(zoneTex.value, door.value);
    EXPECT_LT(door.value, entMesh.value);
    EXPECT_LT(entMesh.value, entTex.value);
    EXPECT_LT(entTex.value, light.value);
    EXPECT_LT(light.value, icon.value);
}

TEST(WorkPriorityKey, SameDepthSameTypeDistanceTiebreaker) {
    auto near = WorkPriorityKey::make(0, AssetType::ZoneMesh, 100.0f);
    auto far  = WorkPriorityKey::make(0, AssetType::ZoneMesh, 5000.0f);
    EXPECT_LT(near.value, far.value);
}

TEST(WorkPriorityKey, MakeNonSpatialAlwaysLowest) {
    // Non-spatial (depth 255) should sort after depth 254
    auto d254 = WorkPriorityKey::make(254, AssetType::ZoneMesh);
    auto nonSpatial = WorkPriorityKey::makeNonSpatial(AssetType::Icon);
    EXPECT_LT(d254.value, nonSpatial.value);
}

TEST(WorkPriorityKey, MakeNonSpatialWithDifferentTypes) {
    auto nsZone = WorkPriorityKey::makeNonSpatial(AssetType::ZoneMesh);
    auto nsIcon = WorkPriorityKey::makeNonSpatial(AssetType::Icon);
    EXPECT_LT(nsZone.value, nsIcon.value);
}

TEST(WorkPriorityKey, ZeroDistanceSq) {
    auto key = WorkPriorityKey::make(0, AssetType::ZoneMesh, 0.0f);
    // Bits 19-0 should be 0
    EXPECT_EQ(key.value & 0xFFFFF, 0u);
}

TEST(WorkPriorityKey, MaxDistanceSqClamped) {
    auto key = WorkPriorityKey::make(0, AssetType::ZoneMesh, 999999999.0f);
    // Bits 19-0 should be clamped to 1048575
    EXPECT_EQ(key.value & 0xFFFFF, 1048575u);
}

TEST(WorkPriorityKey, NegativeDistanceSqClamped) {
    auto key = WorkPriorityKey::make(0, AssetType::ZoneMesh, -100.0f);
    // Should clamp to 0
    EXPECT_EQ(key.value & 0xFFFFF, 0u);
}

TEST(WorkPriorityKey, EqualityAndInequality) {
    auto a = WorkPriorityKey::make(1, AssetType::Door, 50.0f);
    auto b = WorkPriorityKey::make(1, AssetType::Door, 50.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);

    auto c = WorkPriorityKey::make(2, AssetType::Door, 50.0f);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

// ===========================================================================
// Sorting a vector of priorities
// ===========================================================================

TEST(WorkPriorityKey, SortVectorByPriority) {
    std::vector<WorkPriorityKey> keys = {
        WorkPriorityKey::make(2, AssetType::ZoneMesh),
        WorkPriorityKey::make(0, AssetType::Icon),
        WorkPriorityKey::make(1, AssetType::EntityTexture),
        WorkPriorityKey::make(0, AssetType::ZoneMesh),
        WorkPriorityKey::makeNonSpatial(AssetType::Icon),
    };

    std::sort(keys.begin(), keys.end());

    // Expected order: depth0/ZoneMesh, depth0/Icon, depth1/EntTex, depth2/ZoneMesh, nonSpatial/Icon
    EXPECT_EQ(keys[0], WorkPriorityKey::make(0, AssetType::ZoneMesh));
    EXPECT_EQ(keys[1], WorkPriorityKey::make(0, AssetType::Icon));
    EXPECT_EQ(keys[2], WorkPriorityKey::make(1, AssetType::EntityTexture));
    EXPECT_EQ(keys[3], WorkPriorityKey::make(2, AssetType::ZoneMesh));
    EXPECT_EQ(keys[4], WorkPriorityKey::makeNonSpatial(AssetType::Icon));
}

// ===========================================================================
// Bit layout verification
// ===========================================================================

TEST(WorkPriorityKey, BitLayout) {
    auto key = WorkPriorityKey::make(3, AssetType::Door, 0.0f);
    // Depth 3 in bits 31-24 = 0x03000000
    // Door (2) in bits 23-20 = 0x00200000
    // Distance 0 in bits 19-0 = 0x00000000
    EXPECT_EQ(key.value, 0x03200000u);
}

TEST(WorkPriorityKey, BitLayoutWithDistance) {
    // distanceSq = 100.0f → quantized = 100/10 = 10
    auto key = WorkPriorityKey::make(0, AssetType::ZoneMesh, 100.0f);
    EXPECT_EQ(key.value & 0xFFFFF, 10u);
    EXPECT_EQ((key.value >> 20) & 0xF, 0u);  // ZoneMesh = 0
    EXPECT_EQ((key.value >> 24) & 0xFF, 0u);  // depth = 0
}
