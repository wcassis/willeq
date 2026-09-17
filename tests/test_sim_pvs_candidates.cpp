// T490 -- SimulationZoneData::pvsCandidatesFor(): the PVS candidate list for
// the camera region is built on demand and reused while the region holds.
//
// Before T490 the sim bridge pre-built one list PER REGION at zone load
// (pvsVisibleBoundsIndices): regions x visible-per-region entries, a second
// regions^2-shaped term beside the dense PVS bitsets. Now there is one list,
// for the camera region, rebuilt when it changes.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "client/zone_bsp.h"   // BspTree/BspRegion are only forward-declared by simulation_worker.h
#include "client/graphics/simulation_worker.h"

using namespace EQT::Graphics;

namespace {

SimulationZoneData makeZone() {
    SimulationZoneData z;
    auto bsp = std::make_shared<BspTree>();
    // Three regions: 0 sees {0,2}; 1 sees {1}; 2 has no PVS.
    for (int i = 0; i < 3; ++i) {
        auto r = std::make_shared<BspRegion>();
        r->containsPolygons = true;
        bsp->addRegion(r);
    }
    bsp->regions[0]->setPvs({true, false, true});
    bsp->regions[1]->setPvs({false, true, false});
    z.bspTree = bsp;
    z.usePvsCulling = true;
    // regionBounds slots: region 2 -> slot 0, region 0 -> slot 1 (region 1 has none)
    z.regionBounds.push_back({2, 0, 0, 0, 1, 1, 1});
    z.regionBounds.push_back({0, 0, 0, 0, 1, 1, 1});
    z.regionBoundsIndex[2] = 0;
    z.regionBoundsIndex[0] = 1;
    z.allBoundsIndices = {0, 1};
    return z;
}

TEST(SimPvsCandidates, ListsOnlyBoundsSlotsVisibleFromTheCameraRegion) {
    auto z = makeZone();
    const auto* c0 = z.pvsCandidatesFor(0);
    ASSERT_NE(c0, nullptr);
    EXPECT_EQ(*c0, (std::vector<size_t>{1, 0}));  // region 0 -> slot 1, region 2 -> slot 0, ascending region order
    const auto* c1 = z.pvsCandidatesFor(1);
    ASSERT_NE(c1, nullptr);
    EXPECT_TRUE(c1->empty()) << "region 1 sees only itself, which has no bounds slot";
}

TEST(SimPvsCandidates, RegionWithoutPvsFallsBackToNull) {
    auto z = makeZone();
    EXPECT_EQ(z.pvsCandidatesFor(2), nullptr);
    EXPECT_EQ(z.pvsCandidatesFor(99), nullptr);
    EXPECT_EQ(z.pvsCandidatesFor(SIZE_MAX), nullptr);
}

TEST(SimPvsCandidates, ReusesTheListWhileTheCameraRegionHolds) {
    auto z = makeZone();
    const auto* a = z.pvsCandidatesFor(0);
    const auto* b = z.pvsCandidatesFor(0);
    EXPECT_EQ(a, b);
    EXPECT_EQ(z.pvsCandidatesRegion, 0u);
    const auto* c = z.pvsCandidatesFor(1);
    EXPECT_EQ(z.pvsCandidatesRegion, 1u);
    EXPECT_TRUE(c->empty());
    const auto* d = z.pvsCandidatesFor(0);
    EXPECT_EQ(*d, (std::vector<size_t>{1, 0}));
}

TEST(SimPvsCandidates, NoBspTreeMeansNoList) {
    SimulationZoneData z;
    z.usePvsCulling = true;
    EXPECT_EQ(z.pvsCandidatesFor(0), nullptr);
}

}  // namespace
