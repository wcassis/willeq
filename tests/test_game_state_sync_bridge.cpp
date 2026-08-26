// GameStateSyncBridge unit tests.
//
// T227: the decorator no longer projects anything -- there is ONE
// state::GameState (EverQuest::m_game_state), written at the source. What is
// left of this class is the forwarding hop: applyEvent() forwards to the
// wrapped bridge on the calling thread (headless), and the T220 two-stage
// path (stagePendingEvents() on the game thread, forwardStagedEvents() on
// the render thread) delivers every event exactly once, in push order, with
// the D24 coalesce re-applied renderer-side. The 68 projection cases this
// file carried from T40 to T221 died with the switch; the state itself is
// covered by tests/test_game_state_source_writes.cpp.

#include <gtest/gtest.h>

#include "client/bridge/game_state_sync_bridge.h"

using namespace eqt;
using namespace eqt::bridge;
using namespace eqt::state;

namespace {

// Records the full event so order AND payload can be asserted.
class FullRecordingBridge : public GameStateBridge {
public:
    void applyEvent(const GameEvent& event) override {
        applied.push_back(event);
    }
    std::vector<GameEvent> applied;
};

class GameStateSyncBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        wrapped = std::make_unique<FullRecordingBridge>();
        bridge = std::make_unique<GameStateSyncBridge>(*wrapped);
    }

    static GameEvent moved(uint16_t spawnId, float x) {
        EntityMovedData d{};
        d.spawnId = spawnId;
        d.x = x;
        d.y = 0.0f;
        d.z = 0.0f;
        d.heading = 0.0f;
        d.dx = d.dy = d.dz = 0.0f;
        d.animation = 0;
        return GameEvent(GameEventType::EntityMoved, d);
    }

    // T264: was EntityStateSnapshot, deleted (published, never consumed).
    static GameEvent snapshot(uint16_t spawnId, const std::string& name) {
        EntitySpawnedData d{};
        d.spawnId = spawnId;
        d.name = name;
        return GameEvent(GameEventType::EntitySpawned, d);
    }

    static GameEvent pet(uint16_t spawnId) {
        PetCreatedData d;
        d.spawnId = spawnId;
        d.name = "Fido";
        d.level = 1;
        return GameEvent(GameEventType::PetCreated, d);
    }

    std::unique_ptr<FullRecordingBridge> wrapped;
    std::unique_ptr<GameStateSyncBridge> bridge;
};

} // namespace

// ---------------------------------------------------------------------------
// Single-stage applyEvent(): every event type forwards exactly once.
// ---------------------------------------------------------------------------

TEST_F(GameStateSyncBridgeTest, EveryEventTypeForwardsOnceUnchanged) {
    // One event of each type this decorator used to intercept, plus one it
    // never did -- all must reach the wrapped bridge untouched, in order.
    std::vector<GameEvent> in;
    in.push_back(pet(5));
    in.push_back(GameEvent(GameEventType::PetRemoved, PetRemovedData{}));
    in.push_back(GameEvent(GameEventType::PlayerSpawnIdSet, PlayerSpawnIdSetData{460}));
    in.push_back(GameEvent(GameEventType::SpellGemChanged, SpellGemChangedData{}));
    in.push_back(GameEvent(GameEventType::SpellCastStarted, SpellCastStartedData{}));
    in.push_back(GameEvent(GameEventType::SpellCastComplete, SpellCastCompleteData{}));
    in.push_back(GameEvent(GameEventType::VendorWindowOpened, WindowOpenedData{}));
    in.push_back(GameEvent(GameEventType::VendorWindowClosed, WindowClosedData{}));
    in.push_back(GameEvent(GameEventType::BankWindowOpened, WindowOpenedData{}));
    in.push_back(GameEvent(GameEventType::BankWindowClosed, WindowClosedData{}));
    in.push_back(GameEvent(GameEventType::TrainerWindowOpened, TrainerWindowOpenedData{}));
    in.push_back(GameEvent(GameEventType::TrainerWindowClosed, WindowClosedData{}));
    in.push_back(snapshot(9, "Guard_Test"));
    in.push_back(GameEvent(GameEventType::EntityDespawned, EntityDespawnedData{}));
    in.push_back(moved(9, 1.0f));
    in.push_back(GameEvent(GameEventType::EntityAppearanceChanged, EntityAppearanceChangedData{}));
    in.push_back(GameEvent(GameEventType::ZoneUnloading, ZoneUnloadingData{}));
    in.push_back(GameEvent(GameEventType::GroupChanged, GroupChangedData{}));
    in.push_back(GameEvent(GameEventType::GroupMemberUpdated, GroupMemberUpdatedData{}));
    in.push_back(GameEvent(GameEventType::GroupInviteReceived, GroupInviteReceivedData{}));
    in.push_back(GameEvent(GameEventType::TradeRequestReceived, TradeRequestReceivedData{}));
    in.push_back(GameEvent(GameEventType::TradeStarted, TradeStartedData{}));
    in.push_back(GameEvent(GameEventType::TradeItemUpdated, TradeItemUpdatedData{}));
    in.push_back(GameEvent(GameEventType::TradeMoneyUpdated, TradeMoneyUpdatedData{}));
    in.push_back(GameEvent(GameEventType::TradeAcceptStateChanged, TradeAcceptStateChangedData{}));
    in.push_back(GameEvent(GameEventType::TradeCancelled, TradeCancelledData{}));
    in.push_back(GameEvent(GameEventType::TradeCompleted, TradeCompletedData{}));
    in.push_back(GameEvent(GameEventType::TargetChanged, TargetChangedData{}));
    in.push_back(GameEvent(GameEventType::PlayerStatsChanged, PlayerStatsChangedData{}));
    in.push_back(GameEvent(GameEventType::PlayerMovementModeChanged, PlayerMovementModeChangedData{}));
    in.push_back(GameEvent(GameEventType::InventorySlotChanged, InventorySlotChangedData{}));
    in.push_back(GameEvent(GameEventType::DoorSpawned, DoorSpawnedData{}));
    in.push_back(GameEvent(GameEventType::DoorStateChanged, DoorStateChangedData{}));
    in.push_back(GameEvent(GameEventType::PlayerMoved, PlayerMovedData{}));

    for (const auto& e : in) {
        bridge->applyEvent(e);
    }

    ASSERT_EQ(wrapped->applied.size(), in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(wrapped->applied[i].type, in[i].type) << "index " << i;
    }
    // Payload survives untouched (spot check).
    EXPECT_EQ(std::get<PetCreatedData>(wrapped->applied[0].data).name, "Fido");
    EXPECT_EQ(std::get<PlayerSpawnIdSetData>(wrapped->applied[2].data).spawnId, 460);
    EXPECT_EQ(std::get<EntitySpawnedData>(wrapped->applied[12].data).name, "Guard_Test");

    // Single-stage leaves nothing behind in the render-side queue.
    bridge->forwardStagedEvents();
    EXPECT_EQ(wrapped->applied.size(), in.size());
}

// ---------------------------------------------------------------------------
// T220 two-stage delivery (game thread stages, render thread forwards).
// ---------------------------------------------------------------------------

TEST_F(GameStateSyncBridgeTest, StageStepStagesButForwardsNothing) {
    bridge->pushEvent(pet(7));
    bridge->pushEvent(GameEvent(GameEventType::SystemMessage, ChatMessageData{}));

    bridge->stagePendingEvents();

    // Stage 1 handed NOTHING to the wrapped (render-side) bridge yet...
    EXPECT_TRUE(wrapped->applied.empty());
    // ...and the base queue is now empty (drained into the staging FIFO).
    EXPECT_TRUE(bridge->drainEvents().empty());
}

TEST_F(GameStateSyncBridgeTest, ForwardStepDeliversEachEventOnceInPushOrder) {
    bridge->pushEvent(pet(7));
    bridge->pushEvent(GameEvent(GameEventType::SystemMessage, ChatMessageData{}));
    bridge->pushEvent(snapshot(9, "Guard_Test"));

    bridge->stagePendingEvents();
    bridge->forwardStagedEvents();

    ASSERT_EQ(wrapped->applied.size(), 3u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::PetCreated);
    EXPECT_EQ(wrapped->applied[1].type, GameEventType::SystemMessage);
    EXPECT_EQ(wrapped->applied[2].type, GameEventType::EntitySpawned);

    // Forwarding is destructive: a second render drain delivers nothing
    // more, and a second staging pass finds nothing to stage.
    bridge->forwardStagedEvents();
    bridge->stagePendingEvents();
    bridge->forwardStagedEvents();
    EXPECT_EQ(wrapped->applied.size(), 3u);
}

TEST_F(GameStateSyncBridgeTest, ForwardStepWithoutStageDeliversNothing) {
    bridge->pushEvent(pet(7));
    bridge->forwardStagedEvents();
    EXPECT_TRUE(wrapped->applied.empty());
    // The event is still in the base queue, waiting for the game thread.
    bridge->stagePendingEvents();
    bridge->forwardStagedEvents();
    ASSERT_EQ(wrapped->applied.size(), 1u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::PetCreated);
}

TEST_F(GameStateSyncBridgeTest, OrderPreservedAcrossSeveralStageBatchesBeforeOneForward) {
    // Several game ticks may elapse between two rendered frames (loading,
    // slow llvmpipe frame). The render thread must still see push order.
    bridge->pushEvent(snapshot(1, "A"));
    bridge->stagePendingEvents();
    bridge->pushEvent(snapshot(2, "B"));
    bridge->pushEvent(GameEvent(GameEventType::SystemMessage, ChatMessageData{}));
    bridge->stagePendingEvents();
    bridge->pushEvent(snapshot(3, "C"));
    bridge->stagePendingEvents();

    EXPECT_TRUE(wrapped->applied.empty());

    bridge->forwardStagedEvents();

    ASSERT_EQ(wrapped->applied.size(), 4u);
    EXPECT_EQ(std::get<EntitySpawnedData>(wrapped->applied[0].data).name, "A");
    EXPECT_EQ(std::get<EntitySpawnedData>(wrapped->applied[1].data).name, "B");
    EXPECT_EQ(wrapped->applied[2].type, GameEventType::SystemMessage);
    EXPECT_EQ(std::get<EntitySpawnedData>(wrapped->applied[3].data).name, "C");
}

TEST_F(GameStateSyncBridgeTest, D24CoalescingHoldsOnRendererSideAcrossBatches) {
    bridge->pushEvent(snapshot(5, "Mover"));
    bridge->pushEvent(snapshot(6, "Still"));
    bridge->stagePendingEvents();

    // Batch 1: two moves for 5, one for 6.
    bridge->pushEvent(moved(5, 10.0f));
    bridge->pushEvent(moved(6, 1.0f));
    bridge->pushEvent(moved(5, 20.0f));
    bridge->stagePendingEvents();

    // Batch 2: another move for 5 before the renderer drains.
    bridge->pushEvent(moved(5, 30.0f));
    bridge->stagePendingEvents();

    bridge->forwardStagedEvents();

    // Renderer side: 2 snapshots + exactly ONE EntityMoved per entity, the
    // latest (30 for 5, 1 for 6), in order -- the same contract the
    // renderer had when it drained eventQueue_ directly.
    ASSERT_EQ(wrapped->applied.size(), 4u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::EntitySpawned);
    EXPECT_EQ(wrapped->applied[1].type, GameEventType::EntitySpawned);
    EXPECT_EQ(wrapped->applied[2].type, GameEventType::EntityMoved);
    EXPECT_EQ(std::get<EntityMovedData>(wrapped->applied[2].data).spawnId, 6);
    EXPECT_FLOAT_EQ(std::get<EntityMovedData>(wrapped->applied[2].data).x, 1.0f);
    EXPECT_EQ(wrapped->applied[3].type, GameEventType::EntityMoved);
    EXPECT_EQ(std::get<EntityMovedData>(wrapped->applied[3].data).spawnId, 5);
    EXPECT_FLOAT_EQ(std::get<EntityMovedData>(wrapped->applied[3].data).x, 30.0f);
}

TEST_F(GameStateSyncBridgeTest, SingleStageApplyEventForwardsImmediately) {
    // Headless path (ConsoleBridge, one game-thread drain) is unchanged:
    // applyEvent() forwards on the calling thread, and nothing is left
    // behind in the render-side queue.
    bridge->applyEvent(pet(7));
    ASSERT_EQ(wrapped->applied.size(), 1u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::PetCreated);

    bridge->forwardStagedEvents();
    EXPECT_EQ(wrapped->applied.size(), 1u);
}
