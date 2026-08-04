// T40: Unit tests for GameStateSyncBridge, the decorator that feeds
// Application::m_gameState's PetState from bridge events, replacing the
// polling syncPetState() used to do. Feeds synthetic events and asserts
// the resulting GameState::pet() values, plus that events this decorator
// doesn't recognize delegate to the wrapped bridge untouched.

#include <gtest/gtest.h>

#include "client/bridge/game_state_sync_bridge.h"
#include "client/state/game_state.h"

using namespace eqt;
using namespace eqt::bridge;
using namespace eqt::state;

namespace {

// Minimal GameStateBridge fake that just records every event it was asked
// to apply, so tests can assert delegation happened (or didn't) without
// depending on IrrlichtBridge/ConsoleBridge.
class RecordingBridge : public GameStateBridge {
public:
    void applyEvent(const GameEvent& event) override {
        appliedTypes.push_back(event.type);
    }

    std::vector<GameEventType> appliedTypes;
};

class GameStateSyncBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        state = std::make_unique<GameState>();
        wrapped = std::make_unique<RecordingBridge>();
        bridge = std::make_unique<GameStateSyncBridge>(*wrapped, *state);
    }

    std::unique_ptr<GameState> state;
    std::unique_ptr<RecordingBridge> wrapped;
    std::unique_ptr<GameStateSyncBridge> bridge;
};

} // namespace

TEST_F(GameStateSyncBridgeTest, PetCreatedSetsPetState) {
    PetCreatedData data;
    data.spawnId = 1234;
    data.name = "Fido";
    data.level = 42;

    bridge->applyEvent(GameEvent(GameEventType::PetCreated, data));

    EXPECT_TRUE(state->pet().hasPet());
    EXPECT_EQ(state->pet().petSpawnId(), 1234);
    EXPECT_EQ(state->pet().petName(), "Fido");
    EXPECT_EQ(state->pet().petLevel(), 42);
    // setPet() resets stats to full on creation, mirroring the deleted
    // poll's m_lastPetHpPercent/m_lastPetManaPercent reset to 100.
    EXPECT_EQ(state->pet().petHPPercent(), 100);
    EXPECT_EQ(state->pet().petManaPercent(), 100);
}

TEST_F(GameStateSyncBridgeTest, PetStatsChangedUpdatesHpAndMana) {
    PetCreatedData created;
    created.spawnId = 1;
    created.name = "Pet";
    created.level = 1;
    bridge->applyEvent(GameEvent(GameEventType::PetCreated, created));

    PetStatsChangedData stats;
    stats.spawnId = 1;
    stats.hpPercent = 57;
    stats.manaPercent = 33;
    bridge->applyEvent(GameEvent(GameEventType::PetStatsChanged, stats));

    EXPECT_EQ(state->pet().petHPPercent(), 57);
    EXPECT_EQ(state->pet().petManaPercent(), 33);
}

TEST_F(GameStateSyncBridgeTest, PetRemovedClearsPetState) {
    PetCreatedData created;
    created.spawnId = 99;
    created.name = "Gone Soon";
    created.level = 10;
    bridge->applyEvent(GameEvent(GameEventType::PetCreated, created));
    ASSERT_TRUE(state->pet().hasPet());

    PetRemovedData removed;
    removed.spawnId = 99;
    removed.name = "";
    bridge->applyEvent(GameEvent(GameEventType::PetRemoved, removed));

    EXPECT_FALSE(state->pet().hasPet());
    EXPECT_EQ(state->pet().petSpawnId(), 0);
}

TEST_F(GameStateSyncBridgeTest, PetButtonStateChangedTogglesButton) {
    PetButtonStateChangedData data;
    data.button = 3;
    data.state = true;
    bridge->applyEvent(GameEvent(GameEventType::PetButtonStateChanged, data));

    EXPECT_TRUE(state->pet().getButtonState(3));
    EXPECT_FALSE(state->pet().getButtonState(0));

    data.button = 3;
    data.state = false;
    bridge->applyEvent(GameEvent(GameEventType::PetButtonStateChanged, data));
    EXPECT_FALSE(state->pet().getButtonState(3));
}

TEST_F(GameStateSyncBridgeTest, HandledEventsStillDelegateToWrappedBridge) {
    PetCreatedData data;
    data.spawnId = 5;
    data.name = "Wolf";
    data.level = 20;
    bridge->applyEvent(GameEvent(GameEventType::PetCreated, data));

    ASSERT_EQ(wrapped->appliedTypes.size(), 1u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::PetCreated);
}

TEST_F(GameStateSyncBridgeTest, UnhandledEventDelegatesUntouchedAndLeavesPetStateAlone) {
    PetCreatedData created;
    created.spawnId = 7;
    created.name = "Bear";
    created.level = 5;
    bridge->applyEvent(GameEvent(GameEventType::PetCreated, created));

    PlayerMovedData moved;
    moved.x = 1.0f;
    moved.y = 2.0f;
    moved.z = 3.0f;
    moved.heading = 0.0f;
    moved.dx = moved.dy = moved.dz = 0.0f;
    moved.isMoving = true;
    bridge->applyEvent(GameEvent(GameEventType::PlayerMoved, moved));

    // Pet state from before is untouched by the unrelated event.
    EXPECT_TRUE(state->pet().hasPet());
    EXPECT_EQ(state->pet().petName(), "Bear");

    // But the event itself still reached the wrapped bridge.
    ASSERT_EQ(wrapped->appliedTypes.size(), 2u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::PetCreated);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::PlayerMoved);
}

// T41 found real production mutation paths -- vendor buy, vendor sell, and
// bank-window population -- that mutated inventory slots without ever
// publishing InventorySlotChanged, and STOPped rather than convert on top of
// that gap (see refactor/T41_inventory_state_via_bridge.md Review). T42
// closed all three (eq.cpp's generic ItemPacket fallback and
// eq_merchant.cpp's vendor-sell mutation now publish; see
// refactor/T42_inventory_publishes_and_convert.md Review), so this decorator
// now handles InventorySlotChanged for real, replacing
// Application::syncInventoryState()'s poll of InventoryManager occupancy
// into GameState::inventory().
TEST_F(GameStateSyncBridgeTest, InventorySlotChangedEquipmentSlotSetsOccupancy) {
    InventorySlotChangedData data;
    data.slotId = 2;  // equipment slot (0-21)
    data.hasItem = true;
    data.itemId = 1001;
    data.itemName = "Test Item";
    data.iconId = 500;
    data.quantity = 1;

    bridge->applyEvent(GameEvent(GameEventType::InventorySlotChanged, data));

    EXPECT_TRUE(state->inventory().hasEquipment(2));

    data.hasItem = false;
    data.itemId = 0;
    bridge->applyEvent(GameEvent(GameEventType::InventorySlotChanged, data));
    EXPECT_FALSE(state->inventory().hasEquipment(2));
}

TEST_F(GameStateSyncBridgeTest, InventorySlotChangedGeneralSlotSetsOccupancy) {
    InventorySlotChangedData data;
    data.slotId = 23;  // general slot 1 (absolute 22-29)
    data.hasItem = true;
    data.itemId = 2002;
    data.itemName = "Bag Item";
    data.iconId = 501;
    data.quantity = 5;

    bridge->applyEvent(GameEvent(GameEventType::InventorySlotChanged, data));

    // General slot occupancy is indexed relative to GENERAL_BEGIN (22).
    EXPECT_TRUE(state->inventory().hasGeneralItem(1));
    EXPECT_FALSE(state->inventory().hasGeneralItem(0));
}

TEST_F(GameStateSyncBridgeTest, InventorySlotChangedBankSlotSetsOccupancy) {
    InventorySlotChangedData data;
    data.slotId = 2001;  // bank slot 1 (absolute 2000-2015)
    data.hasItem = true;
    data.itemId = 3003;
    data.itemName = "Banked Item";
    data.iconId = 502;
    data.quantity = 1;

    bridge->applyEvent(GameEvent(GameEventType::InventorySlotChanged, data));

    // Bank slot occupancy is indexed relative to BANK_BEGIN (2000).
    EXPECT_TRUE(state->inventory().hasBankItem(1));
    EXPECT_FALSE(state->inventory().hasBankItem(0));
}

TEST_F(GameStateSyncBridgeTest, InventorySlotChangedStillDelegatesToWrappedBridge) {
    InventorySlotChangedData data;
    data.slotId = 22;
    data.hasItem = true;
    data.itemId = 1001;
    data.itemName = "Test Item";
    data.iconId = 500;
    data.quantity = 1;

    bridge->applyEvent(GameEvent(GameEventType::InventorySlotChanged, data));

    // Same as pet events: the wrapped bridge (IrrlichtBridge/ConsoleBridge)
    // still gets the raw event -- this decorator only intercepts, it never
    // swallows.
    ASSERT_EQ(wrapped->appliedTypes.size(), 1u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::InventorySlotChanged);
}
