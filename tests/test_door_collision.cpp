// T435 Leg A (BOARD B425): doors become solid.
//
// Before this unit nothing in the tree ever wrote `ctx_.doorCollisionData`.
// T433 took the census: two `clear()` calls, one `.size()` in a log line, one
// context binding and one read in `rayTestDoorsAndObjects()` -- and no insert
// anywhere under src/ include/ tests/ tools/ benchmarks/ docker/. The map was
// always empty, so every door in every zone was walk-through and
// non-standable, and `DoorCollisionData::hasCollision` was never assigned at
// all: its comment ("false for invisible, teleporter, spinning, lifts,
// hazardous") described an intent nobody implemented.
//
// This file pins both halves:
//   * the WRITER -- DoorManager::computeDoorCollision() / markCollisionDirty()
//     / drainCollisionUpdates(), driven through a real DoorManager on an
//     EDT_NULL device; and
//   * the READER -- RayTestDoorCollision(), the exact function the movement
//     code's wall ray and ground ray both call, so "walk into a closed door",
//     "walk through the same door open" and "stand on a door surface" are
//     asserted on the path a player reaches rather than on a mock.
//
// No server, no display, no EQ client data: the doors here use DoorManager's
// own procedural placeholder mesh.

#include <gtest/gtest.h>

#include <irrlicht.h>

#include "client/graphics/door_collision.h"
#include "client/graphics/door_manager.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

using namespace EQT::Graphics;

namespace {

// --------------------------------------------------------------------------
// The opentype table
// --------------------------------------------------------------------------

// T433's census: "The name is not a reliable classifier and the opentype is."
// 52 rows named OBJ_BLUETORCHSWITCH are opentype 59 (the lift type) and 16
// rows named FAYLEVATOR in gfaydark are opentype 54 (the invisible type), so a
// name-derived rule gets both wrong.
TEST(DoorOpentypeCollision, CollidingTypes) {
    EXPECT_TRUE(DoorOpentypeHasCollision(0));   // 1,678 era rows: plain doors
    EXPECT_TRUE(DoorOpentypeHasCollision(5));   // 395 era rows
    EXPECT_TRUE(DoorOpentypeHasCollision(56));  // 6 era rows
    EXPECT_TRUE(DoorOpentypeHasCollision(55));
    EXPECT_TRUE(DoorOpentypeHasCollision(40));  // levers
    // The lifts. A platform is a standing surface wherever it is, which is the
    // whole point of B425 for Kelethin; the stale comment on DoorCollisionData
    // said lifts do NOT collide and was never true of anything.
    EXPECT_TRUE(DoorOpentypeHasCollision(59));
}

TEST(DoorOpentypeCollision, NonCollidingTypes) {
    // Teleport doors: eqemu resolves them with MovePC(), so the player has to
    // be able to walk into the trigger.
    EXPECT_FALSE(DoorOpentypeHasCollision(57));
    EXPECT_FALSE(DoorOpentypeHasCollision(58));
    // Hazards: the damage is the server's OP_Damage, not a wall.
    EXPECT_FALSE(DoorOpentypeHasCollision(71));   // sebilis SBDOOR103, bazaar gates
    EXPECT_FALSE(DoorOpentypeHasCollision(120));  // soldunga SPEARDOWN
    // Spinners: no settled pose to bake.
    EXPECT_FALSE(DoorOpentypeHasCollision(100));
    EXPECT_FALSE(DoorOpentypeHasCollision(105));
    // Invisible types never get a node at all; listed so that if B427 ever
    // makes them real they do not silently become walls.
    EXPECT_FALSE(DoorOpentypeHasCollision(50));
    EXPECT_FALSE(DoorOpentypeHasCollision(53));
    EXPECT_FALSE(DoorOpentypeHasCollision(54));
}

// --------------------------------------------------------------------------
// The reader: the door half of the movement ray
// --------------------------------------------------------------------------

constexpr size_t kRegion = 7;
constexpr uint8_t kDoorId = 12;

// A quad, as two triangles, in the Irrlicht frame (Y is up = EQ Z).
std::vector<irr::core::triangle3df> Quad(const irr::core::vector3df& a,
                                        const irr::core::vector3df& b,
                                        const irr::core::vector3df& c,
                                        const irr::core::vector3df& d) {
    return {irr::core::triangle3df(a, b, c), irr::core::triangle3df(a, c, d)};
}

DoorRuntimeStateFn StateOf(bool isOpen, bool isAnimating = false, bool known = true,
                           bool isLift = false) {
    return [isOpen, isAnimating, known, isLift](uint8_t) {
        DoorRuntimeState s;
        s.known = known;
        s.isOpen = isOpen;
        s.isAnimating = isAnimating;
        s.isLift = isLift;
        return s;
    };
}

// A door leaf standing in the plane x = 10, and its open pose swung well out of
// the doorway. A player walking along +X at head height passes through x = 10.
std::map<uint8_t, DoorCollisionData> DoorwayMap(bool hasCollision = true) {
    DoorCollisionData d;
    d.bspRegion = kRegion;
    d.hasCollision = hasCollision;
    d.closedTriangles = Quad({10.0f, 0.0f, -4.0f}, {10.0f, 0.0f, 4.0f},
                             {10.0f, 8.0f, 4.0f}, {10.0f, 8.0f, -4.0f});
    // Swung 90 degrees about the hinge at z = -4: the leaf now lies along the
    // wall and no longer blocks the doorway.
    d.openTriangles = Quad({10.0f, 0.0f, -4.0f}, {18.0f, 0.0f, -4.0f},
                           {18.0f, 8.0f, -4.0f}, {10.0f, 8.0f, -4.0f});
    std::map<uint8_t, DoorCollisionData> m;
    m[kDoorId] = std::move(d);
    return m;
}

struct RayResult {
    bool hit = false;
    irr::core::vector3df point;
    irr::core::triangle3df tri;
    float distSq = 0.0f;
};

RayResult CastThroughDoorway(const std::map<uint8_t, DoorCollisionData>& doors,
                             const DoorRuntimeStateFn& stateOf,
                             size_t region = kRegion) {
    RayResult r;
    const irr::core::vector3df origin(0.0f, 4.0f, 0.0f);
    const irr::core::vector3df dir(1.0f, 0.0f, 0.0f);
    r.hit = RayTestDoorCollision(doors, stateOf, origin, dir, 20.0f, region,
                                 r.point, r.tri, r.distSq, false);
    return r;
}

// Walking into a closed door STOPS. Pre-T435 this was impossible: the map the
// movement ray reads was never written, so there was nothing to hit.
TEST(DoorCollisionRay, ClosedDoorBlocksTheWalk) {
    const auto doors = DoorwayMap();
    RayResult r = CastThroughDoorway(doors, StateOf(/*isOpen=*/false));
    ASSERT_TRUE(r.hit) << "a closed door must stop the player";
    EXPECT_NEAR(r.point.X, 10.0f, 0.01f);
}

// The SAME door, open, lets the player through.
TEST(DoorCollisionRay, OpenDoorLetsThePlayerThrough) {
    const auto doors = DoorwayMap();
    RayResult r = CastThroughDoorway(doors, StateOf(/*isOpen=*/true));
    EXPECT_FALSE(r.hit) << "an open door must not block the doorway";
}

// Standing on a door surface finds ground on it. This is the lift case
// (opentype 59): the platform is a standing surface wherever it is.
TEST(DoorCollisionRay, GroundIsFoundOnADoorSurface) {
    DoorCollisionData platform;
    platform.bspRegion = kRegion;
    platform.hasCollision = true;
    // A horizontal slab at Irrlicht Y = 50 (EQ z = 50), 20 x 20 units.
    platform.closedTriangles = Quad({-10.0f, 50.0f, -10.0f}, {10.0f, 50.0f, -10.0f},
                                    {10.0f, 50.0f, 10.0f}, {-10.0f, 50.0f, 10.0f});
    platform.openTriangles = platform.closedTriangles;
    std::map<uint8_t, DoorCollisionData> doors;
    doors[kDoorId] = std::move(platform);

    irr::core::vector3df point;
    irr::core::triangle3df tri;
    float distSq = 0.0f;
    const bool hit = RayTestDoorCollision(
        doors, StateOf(false), irr::core::vector3df(0.0f, 60.0f, 0.0f),
        irr::core::vector3df(0.0f, -1.0f, 0.0f), 100.0f, kRegion, point, tri,
        distSq, false);

    ASSERT_TRUE(hit) << "a downward ground ray must land on the platform";
    EXPECT_NEAR(point.Y, 50.0f, 0.01f) << "ground z is the platform's surface";
}

TEST(DoorCollisionRay, NonCollidingDoorIsWalkedThrough) {
    const auto doors = DoorwayMap(/*hasCollision=*/false);
    EXPECT_FALSE(CastThroughDoorway(doors, StateOf(false)).hit);
}

TEST(DoorCollisionRay, AnimatingDoorIsWalkedThrough) {
    const auto doors = DoorwayMap();
    EXPECT_FALSE(CastThroughDoorway(doors, StateOf(false, /*isAnimating=*/true)).hit)
        << "walk-through during animation is the original EQ behaviour";
}

// T438 (board B423): ...and it is the WRONG rule for a lift, which is animating
// for its whole ride. Scoping it to non-lifts is what makes a moving platform a
// surface you can stand on, which T438 measured the official client doing.
TEST(DoorCollisionRay, AnAnimatingLiftStillCollides) {
    const auto doors = DoorwayMap();
    EXPECT_TRUE(CastThroughDoorway(doors,
                                   StateOf(false, /*isAnimating=*/true,
                                           /*known=*/true, /*isLift=*/true)).hit)
        << "a lift you are riding must not become walk-through the moment it "
           "starts moving";
}

TEST(DoorCollisionRay, TheRayTestNamesTheDoorItHit) {
    // T438: the rider probe needs the id, so it can ask whether THAT door is
    // one of the lifts that moved this frame.
    const auto doors = DoorwayMap();
    irr::core::vector3df point;
    irr::core::triangle3df tri;
    float distSq = 0.0f;
    uint8_t hitId = 0;
    ASSERT_TRUE(RayTestDoorCollision(doors, StateOf(false),
                                     irr::core::vector3df(0.0f, 4.0f, 0.0f),
                                     irr::core::vector3df(1.0f, 0.0f, 0.0f), 20.0f,
                                     kRegion, point, tri, distSq, false, &hitId));
    EXPECT_EQ(hitId, kDoorId);
}

TEST(DoorCollisionRay, DoorInAnotherBspRegionIsNotTested) {
    const auto doors = DoorwayMap();
    EXPECT_FALSE(CastThroughDoorway(doors, StateOf(false), kRegion + 1).hit);
}

TEST(DoorCollisionRay, DoorTheManagerDoesNotKnowIsSkipped) {
    const auto doors = DoorwayMap();
    EXPECT_FALSE(
        CastThroughDoorway(doors, StateOf(false, false, /*known=*/false)).hit);
}

// The caller's already-found hit survives when it is nearer, and loses when the
// door is nearer -- this is how the door test composes with zone geometry.
TEST(DoorCollisionRay, NearestHitWins) {
    const auto doors = DoorwayMap();
    irr::core::vector3df point(99.0f, 99.0f, 99.0f);
    irr::core::triangle3df tri;

    float nearerDistSq = 4.0f;  // something already hit 2 units out
    EXPECT_TRUE(RayTestDoorCollision(doors, StateOf(false),
                                     irr::core::vector3df(0.0f, 4.0f, 0.0f),
                                     irr::core::vector3df(1.0f, 0.0f, 0.0f), 20.0f,
                                     kRegion, point, tri, nearerDistSq, true));
    EXPECT_FLOAT_EQ(nearerDistSq, 4.0f) << "the nearer existing hit must stand";
    EXPECT_FLOAT_EQ(point.X, 99.0f);

    float fartherDistSq = 900.0f;  // something hit 30 units out
    EXPECT_TRUE(RayTestDoorCollision(doors, StateOf(false),
                                     irr::core::vector3df(0.0f, 4.0f, 0.0f),
                                     irr::core::vector3df(1.0f, 0.0f, 0.0f), 20.0f,
                                     kRegion, point, tri, fartherDistSq, true));
    EXPECT_NEAR(point.X, 10.0f, 0.01f) << "the door is nearer and must win";
}

// --------------------------------------------------------------------------
// The writer: a real DoorManager
// --------------------------------------------------------------------------

class DoorCollisionWriterTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    std::unique_ptr<DoorManager> mgr_;

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_NULL;
        params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_NULL device";
        // No zone data: DoorManager falls back to its own procedural
        // placeholder mesh, which is real geometry and needs no EQ client tree.
        mgr_ = std::make_unique<DoorManager>(device_->getSceneManager(),
                                             device_->getVideoDriver());
    }

    void TearDown() override {
        mgr_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    bool MakeDoor(uint8_t id, uint8_t opentype) {
        return mgr_->createDoor(id, "DOOR_T435", 100.0f, 200.0f, 0.0f,
                                /*heading=*/0.0f, /*incline=*/0, /*size=*/100,
                                opentype, /*initiallyOpen=*/false);
    }
};

// The defect itself: creating a door must offer it to the collision map.
TEST_F(DoorCollisionWriterTest, CreatingADoorOffersItForCollision) {
    ASSERT_TRUE(MakeDoor(kDoorId, 0));

    std::vector<uint8_t> changed, removed;
    mgr_->drainCollisionUpdates(changed, removed);

    ASSERT_EQ(changed.size(), 1u)
        << "createDoor() must mark the door's collision geometry dirty -- "
           "without this ctx_.doorCollisionData has no writer at all (B425)";
    EXPECT_EQ(changed[0], kDoorId);
    EXPECT_TRUE(removed.empty());

    // And a drain is a drain: the same door is not offered twice.
    mgr_->drainCollisionUpdates(changed, removed);
    EXPECT_TRUE(changed.empty());
}

TEST_F(DoorCollisionWriterTest, BakedGeometryHasBothPosesAndTheRightRegion) {
    ASSERT_TRUE(MakeDoor(kDoorId, 0));

    DoorCollisionData data;
    ASSERT_TRUE(mgr_->computeDoorCollision(kDoorId, data));
    EXPECT_TRUE(data.hasCollision);
    EXPECT_FALSE(data.closedTriangles.empty()) << "a closed door must have triangles";
    EXPECT_FALSE(data.openTriangles.empty());
    EXPECT_EQ(data.closedTriangles.size(), data.openTriangles.size())
        << "the two poses are the same mesh at two headings";

    // The two poses are genuinely different geometry -- a hinged door with
    // incline 0 swings 90 degrees (calculateOpenHeading substitutes 128).
    bool anyMoved = false;
    for (size_t i = 0; i < data.closedTriangles.size(); ++i) {
        if (data.closedTriangles[i].pointA.getDistanceFrom(
                data.openTriangles[i].pointA) > 0.5f) {
            anyMoved = true;
            break;
        }
    }
    EXPECT_TRUE(anyMoved) << "open and closed must not be the same triangles";

    // World space, not model space: the door was placed at EQ (100, 200, 0),
    // i.e. Irrlicht (100, 0, 200).
    const irr::core::vector3df& p = data.closedTriangles.front().pointA;
    EXPECT_NEAR(p.X, 100.0f, 20.0f);
    EXPECT_NEAR(p.Z, 200.0f, 20.0f);
}

// The whole chain, writer to reader: bake a real door, then fire the movement
// ray straight at one of its own baked triangles.
TEST_F(DoorCollisionWriterTest, ARealBakedDoorStopsTheMovementRay) {
    ASSERT_TRUE(MakeDoor(kDoorId, 0));

    DoorCollisionData data;
    ASSERT_TRUE(mgr_->computeDoorCollision(kDoorId, data));
    ASSERT_FALSE(data.closedTriangles.empty());
    data.bspRegion = kRegion;

    const irr::core::triangle3df& t = data.closedTriangles.front();
    const irr::core::vector3df target =
        (t.pointA + t.pointB + t.pointC) / 3.0f;   // on the surface by construction
    irr::core::vector3df normal = t.getNormal();
    ASSERT_GT(normal.getLength(), 0.0f);
    normal.normalize();

    std::map<uint8_t, DoorCollisionData> doors;
    doors[kDoorId] = std::move(data);

    irr::core::vector3df hit;
    irr::core::triangle3df hitTri;
    float distSq = 0.0f;
    EXPECT_TRUE(RayTestDoorCollision(doors, StateOf(false), target + normal * 5.0f,
                                     -normal, 10.0f, kRegion, hit, hitTri, distSq,
                                     false))
        << "a door the manager baked must be hittable by the movement ray";
    EXPECT_NEAR(hit.getDistanceFrom(target), 0.0f, 0.01f);
}

TEST_F(DoorCollisionWriterTest, OpentypeDecidesCollisionNotTheName) {
    // Same name, same everything, different opentype.
    ASSERT_TRUE(MakeDoor(1, 59));   // a lift platform
    ASSERT_TRUE(MakeDoor(2, 100));  // a spinner
    ASSERT_TRUE(MakeDoor(3, 58));   // a teleporter

    DoorCollisionData lift, spinner, teleporter;
    ASSERT_TRUE(mgr_->computeDoorCollision(1, lift));
    ASSERT_TRUE(mgr_->computeDoorCollision(2, spinner));
    ASSERT_TRUE(mgr_->computeDoorCollision(3, teleporter));

    EXPECT_TRUE(lift.hasCollision);
    EXPECT_FALSE(lift.closedTriangles.empty());
    EXPECT_FALSE(spinner.hasCollision);
    EXPECT_TRUE(spinner.closedTriangles.empty())
        << "a non-colliding door carries no triangles";
    EXPECT_FALSE(teleporter.hasCollision);
}

TEST_F(DoorCollisionWriterTest, ClearingDoorsRetractsThem) {
    ASSERT_TRUE(MakeDoor(kDoorId, 0));
    std::vector<uint8_t> changed, removed;
    mgr_->drainCollisionUpdates(changed, removed);
    ASSERT_EQ(changed.size(), 1u);

    mgr_->clearDoors();
    mgr_->drainCollisionUpdates(changed, removed);
    EXPECT_TRUE(changed.empty());
    ASSERT_EQ(removed.size(), 1u)
        << "a zone change must retract the door from the collision map, or the "
           "next zone inherits the last one's walls";
    EXPECT_EQ(removed[0], kDoorId);

    DoorCollisionData gone;
    EXPECT_FALSE(mgr_->computeDoorCollision(kDoorId, gone))
        << "the door record itself is gone after clearDoors()";
}

TEST_F(DoorCollisionWriterTest, MarkAllCollisionDirtyReoffersEveryDoor) {
    ASSERT_TRUE(MakeDoor(1, 0));
    ASSERT_TRUE(MakeDoor(2, 0));
    std::vector<uint8_t> changed, removed;
    mgr_->drainCollisionUpdates(changed, removed);
    ASSERT_EQ(changed.size(), 2u);

    // The renderer clears its own map at three lifecycle points; each calls
    // this so the two cannot desync.
    mgr_->markAllCollisionDirty();
    mgr_->drainCollisionUpdates(changed, removed);
    EXPECT_EQ(changed.size(), 2u);
}

TEST_F(DoorCollisionWriterTest, AMovingDoorIsReOfferedEveryFrame) {
    ASSERT_TRUE(MakeDoor(kDoorId, 0));
    std::vector<uint8_t> changed, removed;
    mgr_->drainCollisionUpdates(changed, removed);  // consume creation

    mgr_->setDoorState(kDoorId, true);
    // Two short frames, still mid-swing (ANIM_SPEED completes in 0.5 s).
    mgr_->update(0.05f);
    mgr_->drainCollisionUpdates(changed, removed);
    EXPECT_EQ(changed.size(), 1u) << "a door that moved this frame must re-bake";

    mgr_->update(0.05f);
    mgr_->drainCollisionUpdates(changed, removed);
    EXPECT_EQ(changed.size(), 1u);
}

}  // namespace
