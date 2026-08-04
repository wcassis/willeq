// T40: Unit tests for GameStateSyncBridge, the decorator that feeds
// Application::m_gameState's PetState from bridge events, replacing the
// polling syncPetState() used to do. Feeds synthetic events and asserts
// the resulting GameState::pet() values, plus that events this decorator
// doesn't recognize delegate to the wrapped bridge untouched.

#include <gtest/gtest.h>

#include "client/bridge/game_state_sync_bridge.h"
#include "client/state/game_state.h"

#include <chrono>
#include <thread>

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

// ============================================================================
// T44: SpellGemChanged / SpellCastStarted / SpellCastComplete, replacing
// Application::syncSpellState(). SpellCastStarted/SpellCastComplete carry a
// casterId that can be any entity (self, NPC, other player), so these cases
// filter to our own casts via GameState::player().spawnId() -- fed by a new
// PlayerSpawnIdSet case, published reliably at zone-in (T41's full-state
// snapshot) well before any cast could occur.
// ============================================================================

TEST_F(GameStateSyncBridgeTest, PlayerSpawnIdSetUpdatesPlayerState) {
    PlayerSpawnIdSetData data;
    data.spawnId = 4242;

    bridge->applyEvent(GameEvent(GameEventType::PlayerSpawnIdSet, data));

    EXPECT_EQ(state->player().spawnId(), 4242);
}

TEST_F(GameStateSyncBridgeTest, SpellGemChangedCastingSetsGemState) {
    SpellGemChangedData data;
    data.gemSlot = 2;
    data.spellId = 501;
    data.gemState = static_cast<uint8_t>(SpellGemState::Casting);
    data.cooldownRemainingMs = 0;
    data.cooldownTotalMs = 0;

    bridge->applyEvent(GameEvent(GameEventType::SpellGemChanged, data));

    EXPECT_EQ(state->spells().getGemState(2), SpellGemState::Casting);
    EXPECT_EQ(state->spells().getGemSpellId(2), 501u);
}

TEST_F(GameStateSyncBridgeTest, SpellGemChangedRefreshSetsCooldown) {
    SpellGemChangedData data;
    data.gemSlot = 3;
    data.spellId = 77;
    data.gemState = static_cast<uint8_t>(SpellGemState::Refresh);
    data.cooldownRemainingMs = 4000;
    data.cooldownTotalMs = 8000;

    bridge->applyEvent(GameEvent(GameEventType::SpellGemChanged, data));

    EXPECT_EQ(state->spells().getGemState(3), SpellGemState::Refresh);
    // T44: computed on read from the single event above, not a per-tick
    // poll -- allow a small tolerance for real time elapsed since the call.
    EXPECT_NEAR(state->spells().getGemCooldownProgress(3), 0.5f, 0.02f);
    EXPECT_NEAR(static_cast<double>(state->spells().getGemCooldownRemaining(3)), 4000.0, 100.0);
}

TEST_F(GameStateSyncBridgeTest, SpellGemChangedReadySetsGemReadyAndClearsCooldown) {
    SpellGemChangedData refresh;
    refresh.gemSlot = 4;
    refresh.spellId = 88;
    refresh.gemState = static_cast<uint8_t>(SpellGemState::Refresh);
    refresh.cooldownRemainingMs = 1000;
    refresh.cooldownTotalMs = 1000;
    bridge->applyEvent(GameEvent(GameEventType::SpellGemChanged, refresh));
    ASSERT_EQ(state->spells().getGemState(4), SpellGemState::Refresh);

    SpellGemChangedData ready = refresh;
    ready.gemState = static_cast<uint8_t>(SpellGemState::Ready);
    ready.cooldownRemainingMs = 0;
    bridge->applyEvent(GameEvent(GameEventType::SpellGemChanged, ready));

    EXPECT_EQ(state->spells().getGemState(4), SpellGemState::Ready);
    EXPECT_EQ(state->spells().getGemCooldownRemaining(4), 0u);
}

TEST_F(GameStateSyncBridgeTest, SpellCastStartedForSelfSetsCastingState) {
    PlayerSpawnIdSetData spawn;
    spawn.spawnId = 100;
    bridge->applyEvent(GameEvent(GameEventType::PlayerSpawnIdSet, spawn));

    SpellCastStartedData data;
    data.casterId = 100;  // matches player().spawnId()
    data.spellId = 909;
    data.castTimeMs = 3000;
    data.targetId = 55;

    bridge->applyEvent(GameEvent(GameEventType::SpellCastStarted, data));

    EXPECT_TRUE(state->spells().isCasting());
    EXPECT_EQ(state->spells().castingSpellId(), 909u);
    EXPECT_EQ(state->spells().castingTargetId(), 55);
    EXPECT_EQ(state->spells().castTimeTotal(), 3000u);
}

TEST_F(GameStateSyncBridgeTest, SpellCastStartedForOtherCasterIsIgnored) {
    PlayerSpawnIdSetData spawn;
    spawn.spawnId = 100;
    bridge->applyEvent(GameEvent(GameEventType::PlayerSpawnIdSet, spawn));

    SpellCastStartedData data;
    data.casterId = 200;  // an NPC or other player, not us
    data.spellId = 909;
    data.castTimeMs = 3000;
    data.targetId = 55;

    bridge->applyEvent(GameEvent(GameEventType::SpellCastStarted, data));

    EXPECT_FALSE(state->spells().isCasting());
}

TEST_F(GameStateSyncBridgeTest, SpellCastCompleteForSelfClearsCastingState) {
    PlayerSpawnIdSetData spawn;
    spawn.spawnId = 100;
    bridge->applyEvent(GameEvent(GameEventType::PlayerSpawnIdSet, spawn));

    SpellCastStartedData started;
    started.casterId = 100;
    started.spellId = 909;
    started.castTimeMs = 3000;
    bridge->applyEvent(GameEvent(GameEventType::SpellCastStarted, started));
    ASSERT_TRUE(state->spells().isCasting());

    SpellCastCompleteData complete;
    complete.casterId = 100;
    complete.spellId = 909;
    complete.result = 0;  // success
    bridge->applyEvent(GameEvent(GameEventType::SpellCastComplete, complete));

    EXPECT_FALSE(state->spells().isCasting());
}

TEST_F(GameStateSyncBridgeTest, SpellCastCompleteForOtherCasterDoesNotClearOurCastingState) {
    PlayerSpawnIdSetData spawn;
    spawn.spawnId = 100;
    bridge->applyEvent(GameEvent(GameEventType::PlayerSpawnIdSet, spawn));

    SpellCastStartedData started;
    started.casterId = 100;
    started.spellId = 909;
    started.castTimeMs = 3000;
    bridge->applyEvent(GameEvent(GameEventType::SpellCastStarted, started));
    ASSERT_TRUE(state->spells().isCasting());

    SpellCastCompleteData complete;
    complete.casterId = 200;  // someone else's cast completing
    complete.spellId = 111;
    complete.result = 0;
    bridge->applyEvent(GameEvent(GameEventType::SpellCastComplete, complete));

    EXPECT_TRUE(state->spells().isCasting()) << "our own cast must be unaffected";
}

// The countdown-model test: a single SpellCastStarted event is enough for
// castProgress()/castTimeRemaining() to advance purely from wall-clock time
// -- no second event, no per-tick publish, needs to be applied for the value
// to move. This is the behavior the plan explicitly rejected the
// alternative of ("publishing a progress event per tick").
TEST_F(GameStateSyncBridgeTest, CastProgressAdvancesFromSingleEventWithNoPerTickPublish) {
    PlayerSpawnIdSetData spawn;
    spawn.spawnId = 100;
    bridge->applyEvent(GameEvent(GameEventType::PlayerSpawnIdSet, spawn));

    SpellCastStartedData data;
    data.casterId = 100;
    data.spellId = 909;
    data.castTimeMs = 5000;
    bridge->applyEvent(GameEvent(GameEventType::SpellCastStarted, data));

    // Exactly two events applied so far (PlayerSpawnIdSet, SpellCastStarted).
    ASSERT_EQ(wrapped->appliedTypes.size(), 2u);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Progress moved even though nothing was applied to the bridge in
    // between -- proving the countdown is computed on read, not ticked by
    // repeated events.
    EXPECT_GT(state->spells().castProgress(), 0.0f);
    EXPECT_LT(state->spells().castTimeRemaining(), 5000u);
    EXPECT_EQ(wrapped->appliedTypes.size(), 2u)
        << "no additional event should have been needed or applied for progress to advance";
}

TEST_F(GameStateSyncBridgeTest, SpellEventsStillDelegateToWrappedBridge) {
    SpellGemChangedData gemData;
    gemData.gemSlot = 0;
    gemData.spellId = 1;
    gemData.gemState = static_cast<uint8_t>(SpellGemState::Ready);
    bridge->applyEvent(GameEvent(GameEventType::SpellGemChanged, gemData));

    SpellCastStartedData startData;
    startData.casterId = 999;
    startData.spellId = 2;
    startData.castTimeMs = 1000;
    bridge->applyEvent(GameEvent(GameEventType::SpellCastStarted, startData));

    SpellCastCompleteData completeData;
    completeData.casterId = 999;
    completeData.spellId = 2;
    completeData.result = 0;
    bridge->applyEvent(GameEvent(GameEventType::SpellCastComplete, completeData));

    ASSERT_EQ(wrapped->appliedTypes.size(), 3u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::SpellGemChanged);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::SpellCastStarted);
    EXPECT_EQ(wrapped->appliedTypes[2], GameEventType::SpellCastComplete);
}
