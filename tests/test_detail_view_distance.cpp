// T445 / board B336 -- the configured detail view distance drives the chunk
// cull radius.
//
// Before this, ZoneDetailConfig::viewDistance parsed out of
// config/detail_objects.json and nothing read it, while DetailManager culled
// by a hardcoded viewDistanceChunks_ = 2 (a 5x5 chunk grid). The shipped
// config says 150 over a chunk size of 50, i.e. a radius of 3 and a 7x7 grid,
// so the shipped number was inert.
//
// These tests pin both halves: the arithmetic itself, and the fact that
// DetailManager actually applies it when a config is installed. They fail on
// the pre-fix tree (detailChunkRadius does not exist; the radius stays 2).

#include <gtest/gtest.h>

#include "client/graphics/detail/detail_config_loader.h"
#include "client/graphics/detail/detail_manager.h"
#include "client/graphics/detail/detail_types.h"

using EQT::Graphics::Detail::DetailConfigLoader;
using EQT::Graphics::Detail::DetailManager;
using EQT::Graphics::Detail::ZoneDetailConfig;
using EQT::Graphics::Detail::detailChunkRadius;

TEST(DetailViewDistance, RadiusIsTheDistanceInChunksRoundedUp) {
    // The plan's two worked cases.
    EXPECT_EQ(4, detailChunkRadius(200.0f, 50.0f));
    EXPECT_EQ(2, detailChunkRadius(100.0f, 50.0f));

    // The shipped pair.
    EXPECT_EQ(3, detailChunkRadius(150.0f, 50.0f));

    // A chunk whose NEAR edge is inside the view distance must be resident,
    // so the rounding is up, not to nearest and not down.
    EXPECT_EQ(3, detailChunkRadius(101.0f, 50.0f));
    EXPECT_EQ(2, detailChunkRadius(100.0f, 50.0f));
    EXPECT_EQ(4, detailChunkRadius(151.0f, 50.0f));

    // A different chunk size moves it too -- the radius is not a distance.
    EXPECT_EQ(2, detailChunkRadius(150.0f, 100.0f));
    EXPECT_EQ(6, detailChunkRadius(150.0f, 25.0f));
}

TEST(DetailViewDistance, DegenerateConfigFallsBackToOneChunk) {
    // A zero radius would leave only the camera's own chunk; a non-positive
    // chunk size has no answer at all. Neither may divide by zero or return 0.
    EXPECT_EQ(1, detailChunkRadius(150.0f, 0.0f));
    EXPECT_EQ(1, detailChunkRadius(150.0f, -50.0f));
    EXPECT_EQ(1, detailChunkRadius(0.0f, 50.0f));
    EXPECT_EQ(1, detailChunkRadius(-1.0f, 50.0f));
    EXPECT_EQ(1, detailChunkRadius(1.0f, 50.0f));
}

TEST(DetailViewDistance, ManagerDerivesItsRadiusFromTheInstalledConfig) {
    DetailManager mgr(nullptr, nullptr);

    ZoneDetailConfig config;
    config.zoneName = "test";
    config.chunkSize = 50.0f;
    config.viewDistance = 200.0f;
    mgr.setZoneConfig(config);
    EXPECT_EQ(4, mgr.getViewDistanceChunks());

    config.viewDistance = 100.0f;
    mgr.setZoneConfig(config);
    EXPECT_EQ(2, mgr.getViewDistanceChunks());

    // And it tracks the chunk size, not just the distance.
    config.viewDistance = 200.0f;
    config.chunkSize = 100.0f;
    mgr.setZoneConfig(config);
    EXPECT_EQ(2, mgr.getViewDistanceChunks());
}

TEST(DetailViewDistance, ShippedConfigYieldsASevenBySevenGrid) {
    DetailConfigLoader loader;
    auto config = loader.loadDefaultConfig("data");
    ASSERT_NE(nullptr, config) << "config/detail_objects.json did not load";

    // If someone edits the shipped numbers, this test says so out loud rather
    // than letting the grid change silently. It is not a claim that 150 is
    // right: the oracle's own detail draw distance is UNMEASURED.
    EXPECT_FLOAT_EQ(150.0f, config->viewDistance);
    EXPECT_FLOAT_EQ(50.0f, config->chunkSize);

    DetailManager mgr(nullptr, nullptr);
    mgr.setZoneConfig(*config);
    EXPECT_EQ(3, mgr.getViewDistanceChunks());
}
