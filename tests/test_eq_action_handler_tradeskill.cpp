// T149 -- real-EverQuest/EqActionHandler coverage for the four recipe
// commands, and specifically for THE design point of the unit: object_type is
// sourced from the container the server has told us is open, never from a
// user-typed numeric argument.
//
// test_command_processor.cpp covers the argument PARSING against a recording
// fake. What it cannot show is that no object_type appears anywhere in the
// action layer because it is resolved BELOW it. That is what these tests
// pin, against a real EverQuest driven through the production packet path:
//
//   OP_GroundSpawn (T144's REAL CAPTURED forge bytes, drop_id 190, type 17)
//       -> EverQuest::m_world_objects[190].object_type == 17
//   OP_ClickObjectAction open=1 drop_id=190
//       -> EverQuest::m_active_tradeskill_object_id == 190
//   EqActionHandler::recipeSearch(...)
//       -> resolveOpenContainerObjectType() finds 17
//
// Observability: EqActionHandler's refusal path goes through
// AddChatSystemMessage(), which pushes a SystemMessage bridge event -- the
// same RecordingBridge pattern test_eq_action_handler_trade.cpp uses. The
// senders themselves queue onto m_zone_connection, which is null in a replay
// harness, so THE BYTES ON THE WIRE ARE NOT ASSERTED HERE. That leg is owed
// and is named as owed in refactor/T149_tradeskill_recipes.md's Review; these
// tests prove the resolution chain, not the packet.

#include <gtest/gtest.h>

#include "captured_packets.h"
#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/world_object.h"
#include "common/packet_structs.h"

using namespace eqt;

namespace {

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::string lastSystemMessage(const std::vector<eqt::state::GameEvent>& events) {
    std::string last;
    for (auto& e : events) {
        if (e.type == eqt::state::GameEventType::SystemMessage) {
            last = std::get<eqt::state::ChatMessageData>(e.data).message;
        }
    }
    return last;
}

constexpr const char* kNoContainer = "You must have a tradeskill container open.";

// Replays the captured OP_GroundSpawn (opcode included in the byte array).
void ReplayCapturedForgeGroundSpawn(EverQuest& eq) {
    const uint8_t* bytes = eqt_test::kGroundSpawnForgeBytes;
    size_t size = sizeof(eqt_test::kGroundSpawnForgeBytes);
    uint16_t opcode = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    eq.ReplayZonePacket(opcode, bytes + 2, size - 2);
}

// Replays an OP_ClickObjectAction, which is how the SERVER tells the client a
// container opened (open=1) or closed (open=0). This is the only writer of
// EverQuest::m_active_tradeskill_object_id.
void ReplayClickObjectAction(EverQuest& eq, uint32_t dropId, uint32_t open, uint32_t type) {
    EQT::ClickObjectAction_Struct action;
    memset(&action, 0, sizeof(action));
    action.player_id = 1;
    action.drop_id = dropId;
    action.open = open;
    action.type = type;
    action.unknown16 = 0xA;
    action.icon = 1115;
    snprintf(action.object_name, sizeof(action.object_name), "%s", "Half Elf Forge");

    std::vector<uint8_t> body(sizeof(action));
    memcpy(body.data(), &action, sizeof(action));
    eq.ReplayZonePacket(HC_OP_ClickObjectAction, body.data(), body.size());
}

} // namespace

// ---------------------------------------------------------------------
// The plumbing this unit relies on (T144's half)
// ---------------------------------------------------------------------

TEST(EqActionHandlerTradeskill, OpenContainerCarriesRealObjectTypeFromCapturedBytes) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);

    ReplayCapturedForgeGroundSpawn(eq);

    const eqt::WorldObject* obj = eq.Entities().GetWorldObject(190);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->object_type, 17u);  // CONTAINER_BLACKSMITHING
    EXPECT_TRUE(obj->isTradeskillContainer());

    // Nothing is "open" until the server says so.
    EXPECT_EQ(eq.GetActiveTradeskillObjectId(), 0u);

    ReplayClickObjectAction(eq, 190, /*open=*/1, /*type=*/17);
    EXPECT_EQ(eq.GetActiveTradeskillObjectId(), 190u);

    // This is the exact lookup EqActionHandler::resolveOpenContainerObjectType
    // performs: active drop_id -> WorldObject -> object_type.
    const eqt::WorldObject* open = eq.Entities().GetWorldObject(eq.GetActiveTradeskillObjectId());
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->object_type, 17u);

    ReplayClickObjectAction(eq, 190, /*open=*/0, /*type=*/0);
    EXPECT_EQ(eq.GetActiveTradeskillObjectId(), 0u);
}

// ---------------------------------------------------------------------
// The refusal path: no open container -> no packet, and the player is told
// ---------------------------------------------------------------------

TEST(EqActionHandlerTradeskill, NoContainerOpen_AllFourRecipeCommandsRefuseAndSayWhy) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    handler.recipeSearch("iron", 0, 1000);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), kNoContainer);

    handler.recipeDetails(4321);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), kNoContainer);

    handler.recipeAutoCombine(4321);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), kNoContainer);

    handler.recipesFavorite({10, 20});
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), kNoContainer);
}

TEST(EqActionHandlerTradeskill, ContainerOpen_RecipeCommandsProceedWithoutComplaining) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    ReplayCapturedForgeGroundSpawn(eq);
    ReplayClickObjectAction(eq, 190, /*open=*/1, /*type=*/17);
    bridge.drainEvents();  // discard spawn/open events

    handler.recipeSearch("iron", 0, 1000);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), "");

    handler.recipeDetails(4321);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), "");

    handler.recipeAutoCombine(4321);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), "");

    handler.recipesFavorite({10, 20});
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), "");
}

TEST(EqActionHandlerTradeskill, ContainerClosedAgain_RecipeCommandsRefuseOnceMore) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    ReplayCapturedForgeGroundSpawn(eq);
    ReplayClickObjectAction(eq, 190, /*open=*/1, /*type=*/17);
    ReplayClickObjectAction(eq, 190, /*open=*/0, /*type=*/0);
    bridge.drainEvents();

    handler.recipeSearch("iron", 0, 1000);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), kNoContainer);
}

// A container the server says is open but that we have no ground-spawn record
// for must be reported, not guessed at -- guessing an object_type is how a
// "technically wired" command silently sends nonsense.
TEST(EqActionHandlerTradeskill, OpenContainerWithNoWorldObjectRecord_IsReportedNotGuessed) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    RecordingBridge bridge;
    eq.setBridge(&bridge);
    EqActionHandler handler(eq);

    // No OP_GroundSpawn replayed: m_world_objects is empty.
    ReplayClickObjectAction(eq, 777, /*open=*/1, /*type=*/17);
    ASSERT_EQ(eq.GetActiveTradeskillObjectId(), 777u);
    ASSERT_EQ(eq.Entities().GetWorldObject(777), nullptr);
    bridge.drainEvents();

    handler.recipeSearch("iron", 0, 1000);
    EXPECT_EQ(lastSystemMessage(bridge.drainEvents()), "The open container is not recognized.");
}
