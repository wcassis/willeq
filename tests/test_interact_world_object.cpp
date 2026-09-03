// T316 -- the interact-world-object no-op.
//
// ActionDispatcher::interactNearestWorldObject() used to return
// Failure("Use clickWorldObject with drop ID") unconditionally: the interact
// key could never open a forge, a chest or any ground container, and only the
// explicit /clickobject <dropId> path worked. Its TODO said "implement when
// ObjectState is added to GameState"; the fix instead follows the shape of
// hasTellToReplyTo() -- world objects live in EverQuest::m_world_objects, so
// the nearest-object query is a pure query on IActionHandler, and the
// dispatcher refuses cleanly when it returns 0.
//
// These tests drive the WHOLE path a player reaches, not the leaf:
//
//   OP_GroundSpawn (T144's real captured forge bytes, drop_id 190)
//       -> EverQuest::m_world_objects
//       -> EqActionHandler::findNearestWorldObject()
//       -> ActionDispatcher::interactNearestWorldObject()
//       -> EqActionHandler::clickWorldObject() -> EverQuest::SendClickObject()
//
// The senders queue onto m_zone_connection, which is null in a replay
// harness, so the BYTES are not asserted here (same limitation, and the same
// wording, as test_eq_action_handler_tradeskill.cpp). What is asserted is
// that a real ground spawn makes the action succeed and an empty/out-of-range
// world makes it fail with a message.

#include <gtest/gtest.h>

#include "action_test_fakes.h"
#include "captured_packets.h"
#include "client/action/action_dispatcher.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/state/game_state.h"
#include "client/world_object.h"

using namespace eqt;
using eqt::action::ActionDispatcher;
using eqt::action::ActionResult;

namespace {

// Replays the captured OP_GroundSpawn (opcode included in the byte array).
void ReplayCapturedForgeGroundSpawn(EverQuest& eq) {
    const uint8_t* bytes = eqt_test::kGroundSpawnForgeBytes;
    size_t size = sizeof(eqt_test::kGroundSpawnForgeBytes);
    uint16_t opcode = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    eq.ReplayZonePacket(opcode, bytes + 2, size - 2);
}

// Puts the player @p offset units away from the object on the X axis.
void StandNear(EverQuest& eq, const eqt::WorldObject& obj, float offset) {
    eq.GetGameState().player().setPositionNoEvent(obj.x + offset, obj.y, obj.z);
}

} // namespace

// ---------------------------------------------------------------------
// The handler-level query (the piece the dispatcher could not do itself)
// ---------------------------------------------------------------------

TEST(InteractWorldObject, NoGroundSpawns_QueryReportsNothingInRange) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EqActionHandler handler(eq);

    float distance = -1.0f;
    EXPECT_EQ(handler.findNearestWorldObject(distance), 0u);
    EXPECT_FLOAT_EQ(distance, -1.0f) << "outDistance must be left alone when "
                                        "there is no object to report on";
}

TEST(InteractWorldObject, CapturedForge_FoundInRangeAndNotFoundOutOfRange) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    ReplayCapturedForgeGroundSpawn(eq);

    const eqt::WorldObject* forge = eq.Entities().GetWorldObject(190);
    ASSERT_NE(forge, nullptr);

    EqActionHandler handler(eq);

    StandNear(eq, *forge, 5.0f);
    float distance = 0.0f;
    EXPECT_EQ(handler.findNearestWorldObject(distance), 190u);
    EXPECT_NEAR(distance, 5.0f, 0.01f);

    // 50 units is the interaction range (IrrlichtRenderer::getNearestWorldObject's
    // own default); one unit past it, the same forge is out of reach.
    StandNear(eq, *forge, 51.0f);
    distance = -1.0f;
    EXPECT_EQ(handler.findNearestWorldObject(distance), 0u);
    EXPECT_FLOAT_EQ(distance, -1.0f);
}

// ---------------------------------------------------------------------
// The action a keybind reaches
// ---------------------------------------------------------------------

TEST(InteractWorldObject, Dispatcher_InteractSucceedsWithForgeInRange) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.Test_SetBypassSendGuardForSeam(true);
    ReplayCapturedForgeGroundSpawn(eq);

    const eqt::WorldObject* forge = eq.Entities().GetWorldObject(190);
    ASSERT_NE(forge, nullptr);
    StandNear(eq, *forge, 3.0f);

    EqActionHandler handler(eq);
    ActionDispatcher dispatcher(eq.GetGameState());
    dispatcher.setActionHandler(&handler);
    eq.GetGameState().world().setZoneConnected(true);

    ActionResult result = dispatcher.interactNearestWorldObject();
    EXPECT_TRUE(result.success)
        << "before T316 this returned Failure(\"Use clickWorldObject with "
           "drop ID\") no matter what was standing in front of the player";
}

TEST(InteractWorldObject, Dispatcher_InteractFailsCleanlyWithNothingNearby) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EqActionHandler handler(eq);
    ActionDispatcher dispatcher(eq.GetGameState());
    dispatcher.setActionHandler(&handler);
    eq.GetGameState().world().setZoneConnected(true);

    ActionResult result = dispatcher.interactNearestWorldObject();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.message, "No object nearby");
}
