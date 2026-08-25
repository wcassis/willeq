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

// ---------------------------------------------------------------------------
// T49: NPC interaction state (vendor, banker, trainer), replacing
// Application::syncNPCInteractionState() -- the last poll in the
// state-layer sweep (T39->T49). Mirrors the pet/spell test shape above:
// open sets id/name(/rate), close clears.
// ---------------------------------------------------------------------------

TEST_F(GameStateSyncBridgeTest, VendorWindowOpenedSetsVendorState) {
    WindowOpenedData data;
    data.npcId = 500;
    data.npcName = "A Merchant";
    data.sellRate = 1.25f;

    bridge->applyEvent(GameEvent(GameEventType::VendorWindowOpened, data));

    EXPECT_TRUE(state->player().isWithVendor());
    EXPECT_EQ(state->player().vendorNpcId(), 500);
    EXPECT_EQ(state->player().vendorName(), "A Merchant");
    EXPECT_FLOAT_EQ(state->player().vendorSellRate(), 1.25f);
}

TEST_F(GameStateSyncBridgeTest, VendorWindowClosedClearsVendorState) {
    WindowOpenedData openData;
    openData.npcId = 500;
    openData.npcName = "A Merchant";
    openData.sellRate = 1.25f;
    bridge->applyEvent(GameEvent(GameEventType::VendorWindowOpened, openData));
    ASSERT_TRUE(state->player().isWithVendor());

    WindowClosedData closeData;
    closeData.npcId = 500;
    bridge->applyEvent(GameEvent(GameEventType::VendorWindowClosed, closeData));

    EXPECT_FALSE(state->player().isWithVendor());
    EXPECT_EQ(state->player().vendorNpcId(), 0);
}

TEST_F(GameStateSyncBridgeTest, BankWindowOpenedSetsBankerState) {
    WindowOpenedData data;
    data.npcId = 600;
    data.npcName = "A Banker"; // unused by setBanker(), included for realism
    data.sellRate = 0.0f;

    bridge->applyEvent(GameEvent(GameEventType::BankWindowOpened, data));

    EXPECT_TRUE(state->player().isAtBank());
    EXPECT_EQ(state->player().bankerNpcId(), 600);
}

TEST_F(GameStateSyncBridgeTest, BankWindowClosedClearsBankerState) {
    WindowOpenedData openData;
    openData.npcId = 600;
    bridge->applyEvent(GameEvent(GameEventType::BankWindowOpened, openData));
    ASSERT_TRUE(state->player().isAtBank());

    WindowClosedData closeData;
    closeData.npcId = 600;
    bridge->applyEvent(GameEvent(GameEventType::BankWindowClosed, closeData));

    EXPECT_FALSE(state->player().isAtBank());
    EXPECT_EQ(state->player().bankerNpcId(), 0);
}

TEST_F(GameStateSyncBridgeTest, TrainerWindowOpenedSetsTrainerState) {
    TrainerWindowOpenedData data;
    data.npcId = 700;
    data.npcName = "A Trainer";
    data.platinum = 0;
    data.gold = 0;
    data.silver = 0;
    data.copper = 0;
    data.practicePoints = 0;

    bridge->applyEvent(GameEvent(GameEventType::TrainerWindowOpened, data));

    EXPECT_TRUE(state->player().isWithTrainer());
    EXPECT_EQ(state->player().trainerNpcId(), 700);
    EXPECT_EQ(state->player().trainerName(), "A Trainer");
}

TEST_F(GameStateSyncBridgeTest, TrainerWindowClosedClearsTrainerState) {
    TrainerWindowOpenedData openData;
    openData.npcId = 700;
    openData.npcName = "A Trainer";
    bridge->applyEvent(GameEvent(GameEventType::TrainerWindowOpened, openData));
    ASSERT_TRUE(state->player().isWithTrainer());

    WindowClosedData closeData;
    closeData.npcId = 700;
    bridge->applyEvent(GameEvent(GameEventType::TrainerWindowClosed, closeData));

    EXPECT_FALSE(state->player().isWithTrainer());
    EXPECT_EQ(state->player().trainerNpcId(), 0);
}

TEST_F(GameStateSyncBridgeTest, NpcInteractionEventsStillDelegateToWrappedBridge) {
    WindowOpenedData vendorData;
    vendorData.npcId = 500;
    bridge->applyEvent(GameEvent(GameEventType::VendorWindowOpened, vendorData));

    WindowClosedData vendorClose;
    vendorClose.npcId = 500;
    bridge->applyEvent(GameEvent(GameEventType::VendorWindowClosed, vendorClose));

    WindowOpenedData bankData;
    bankData.npcId = 600;
    bridge->applyEvent(GameEvent(GameEventType::BankWindowOpened, bankData));

    WindowClosedData bankClose;
    bankClose.npcId = 600;
    bridge->applyEvent(GameEvent(GameEventType::BankWindowClosed, bankClose));

    TrainerWindowOpenedData trainerData;
    trainerData.npcId = 700;
    bridge->applyEvent(GameEvent(GameEventType::TrainerWindowOpened, trainerData));

    WindowClosedData trainerClose;
    trainerClose.npcId = 700;
    bridge->applyEvent(GameEvent(GameEventType::TrainerWindowClosed, trainerClose));

    ASSERT_EQ(wrapped->appliedTypes.size(), 6u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::VendorWindowOpened);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::VendorWindowClosed);
    EXPECT_EQ(wrapped->appliedTypes[2], GameEventType::BankWindowOpened);
    EXPECT_EQ(wrapped->appliedTypes[3], GameEventType::BankWindowClosed);
    EXPECT_EQ(wrapped->appliedTypes[4], GameEventType::TrainerWindowOpened);
    EXPECT_EQ(wrapped->appliedTypes[5], GameEventType::TrainerWindowClosed);
}

// ---------------------------------------------------------------------------
// T81: entities() projection. Application::m_gameState->entities() was
// permanently empty before this unit -- nothing populated it, which is why
// /who, /face, moveToEntity/followEntity, and the NPC half of
// interactNearest() always reported "not found" against real, nearby
// entities (refactor/T78_two_gamestate_analysis.md Review, items 2/5).
// EntityStateSnapshot mirrors EverQuest::SyncEntityToGameState()'s own
// add-or-replace semantics (eq_player_info.cpp:261): the sender always holds
// the complete Entity struct, so publishing it as a snapshot is sufficient --
// no completion state machine needed on this side. EntityMoved (already
// D24-coalesced) carries position instead of a full snapshot;
// EntityAppearanceChanged (already published for ZoneProcessWearChange())
// carries post-spawn equipment/appearance changes.
// ---------------------------------------------------------------------------

namespace {
EntityStateSnapshotData makeSnapshot(uint16_t spawnId, const std::string& name,
                                      float x, float y, float z) {
    EntityStateSnapshotData data;
    data.entity.spawnId = spawnId;
    data.entity.name = name;
    data.entity.x = x;
    data.entity.y = y;
    data.entity.z = z;
    data.entity.heading = 90.0f;
    data.entity.level = 10;
    data.entity.classId = 1;
    data.entity.raceId = 2;
    data.entity.npcType = 1;  // NPC
    return data;
}
} // namespace

TEST_F(GameStateSyncBridgeTest, EntityStateSnapshotSpawnsEntity) {
    auto data = makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f);

    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, data));

    ASSERT_TRUE(state->entities().hasEntity(500));
    const auto* entity = state->entities().getEntity(500);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->name, "a_gnoll");
    EXPECT_FLOAT_EQ(entity->x, 100.0f);
    EXPECT_FLOAT_EQ(entity->y, 200.0f);
    EXPECT_FLOAT_EQ(entity->z, 5.0f);
    EXPECT_EQ(entity->level, 10);
}

TEST_F(GameStateSyncBridgeTest, EntityStateSnapshotForSelfSpawnsEntityToo) {
    // The self-entity path (eq.cpp:2378) calls the exact same
    // SyncEntityToGameState() as any other spawn -- no special-casing on
    // this side, it's just another snapshot.
    auto data = makeSnapshot(42, "Testchar", 0.0f, 0.0f, 0.0f);
    data.entity.npcType = 0;  // player

    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, data));

    ASSERT_TRUE(state->entities().hasEntity(42));
    EXPECT_EQ(state->entities().getEntity(42)->name, "Testchar");
}

TEST_F(GameStateSyncBridgeTest, EntityStateSnapshotReplacesRatherThanDuplicates) {
    auto first = makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f);
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, first));
    ASSERT_TRUE(state->entities().hasEntity(500));

    // A second snapshot for the same spawnId (e.g. re-sent on some other
    // update) must replace in place, not create a duplicate or second entry.
    auto second = makeSnapshot(500, "a_gnoll", 150.0f, 250.0f, 6.0f);
    second.entity.hpPercent = 42;
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, second));

    const auto* entity = state->entities().getEntity(500);
    ASSERT_NE(entity, nullptr);
    EXPECT_FLOAT_EQ(entity->x, 150.0f);
    EXPECT_FLOAT_EQ(entity->y, 250.0f);
    EXPECT_EQ(entity->hpPercent, 42);

    // Still exactly one entity for this spawnId -- no duplication.
    EXPECT_EQ(state->entities().getEntitiesInRange(150.0f, 250.0f, 6.0f, 1.0f).size(), 1u);
}

TEST_F(GameStateSyncBridgeTest, EntityDespawnedRemovesEntity) {
    auto data = makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f);
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, data));
    ASSERT_TRUE(state->entities().hasEntity(500));

    EntityDespawnedData despawn;
    despawn.spawnId = 500;
    despawn.name = "a_gnoll";
    bridge->applyEvent(GameEvent(GameEventType::EntityDespawned, despawn));

    EXPECT_FALSE(state->entities().hasEntity(500));
}

TEST_F(GameStateSyncBridgeTest, EntityMovedUpdatesPositionOfExistingEntity) {
    auto data = makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f);
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, data));

    EntityMovedData moved;
    moved.spawnId = 500;
    moved.x = 110.0f;
    moved.y = 210.0f;
    moved.z = 5.0f;
    moved.heading = 45.0f;
    moved.dx = 1.0f;
    moved.dy = 1.0f;
    moved.dz = 0.0f;
    moved.animation = 3;
    bridge->applyEvent(GameEvent(GameEventType::EntityMoved, moved));

    const auto* entity = state->entities().getEntity(500);
    ASSERT_NE(entity, nullptr);
    EXPECT_FLOAT_EQ(entity->x, 110.0f);
    EXPECT_FLOAT_EQ(entity->y, 210.0f);
    EXPECT_FLOAT_EQ(entity->heading, 45.0f);
    // Name/level/etc from the original snapshot are untouched by a
    // position-only update.
    EXPECT_EQ(entity->name, "a_gnoll");
    EXPECT_EQ(entity->level, 10);
}

TEST_F(GameStateSyncBridgeTest, EntityMovedForUnknownEntityIsANoOp) {
    // A movement update arriving before/without a spawn should not create a
    // partial entity -- full snapshots are the only add path.
    EntityMovedData moved;
    moved.spawnId = 999;
    moved.x = 1.0f;
    moved.y = 1.0f;
    moved.z = 1.0f;
    bridge->applyEvent(GameEvent(GameEventType::EntityMoved, moved));

    EXPECT_FALSE(state->entities().hasEntity(999));
}

TEST_F(GameStateSyncBridgeTest, EntityAppearanceChangedUpdatesEquipmentOfExistingEntity) {
    auto data = makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f);
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot, data));

    EntityAppearanceChangedData appearance;
    appearance.spawnId = 500;
    appearance.raceId = 2;
    appearance.gender = 1;
    appearance.isPlayer = false;
    appearance.face = 5;
    appearance.helm = 7;
    appearance.equipment[0] = 12345;
    appearance.equipmentTint[0] = 0xAABBCC;
    bridge->applyEvent(GameEvent(GameEventType::EntityAppearanceChanged, appearance));

    const auto* entity = state->entities().getEntity(500);
    ASSERT_NE(entity, nullptr);
    EXPECT_EQ(entity->face, 5);
    EXPECT_EQ(entity->helm, 7);
    EXPECT_EQ(entity->equipment[0], 12345u);
    EXPECT_EQ(entity->equipmentTint[0], 0xAABBCCu);
    // Position from the original snapshot is untouched by an
    // appearance-only update.
    EXPECT_FLOAT_EQ(entity->x, 100.0f);
}

TEST_F(GameStateSyncBridgeTest, ZoneUnloadingClearsAllEntities) {
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot,
        makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f)));
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot,
        makeSnapshot(501, "a_wolf", 110.0f, 210.0f, 5.0f)));
    ASSERT_TRUE(state->entities().hasEntity(500));
    ASSERT_TRUE(state->entities().hasEntity(501));

    bridge->applyEvent(GameEvent(GameEventType::ZoneUnloading, ZoneUnloadingData{}));

    EXPECT_FALSE(state->entities().hasEntity(500));
    EXPECT_FALSE(state->entities().hasEntity(501));
}

TEST_F(GameStateSyncBridgeTest, EntityEventsStillDelegateToWrappedBridge) {
    bridge->applyEvent(GameEvent(GameEventType::EntityStateSnapshot,
        makeSnapshot(500, "a_gnoll", 100.0f, 200.0f, 5.0f)));

    EntityMovedData moved;
    moved.spawnId = 500;
    moved.x = 101.0f;
    bridge->applyEvent(GameEvent(GameEventType::EntityMoved, moved));

    EntityDespawnedData despawn;
    despawn.spawnId = 500;
    despawn.name = "a_gnoll";
    bridge->applyEvent(GameEvent(GameEventType::EntityDespawned, despawn));

    bridge->applyEvent(GameEvent(GameEventType::ZoneUnloading, ZoneUnloadingData{}));

    ASSERT_EQ(wrapped->appliedTypes.size(), 4u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::EntityStateSnapshot);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::EntityMoved);
    EXPECT_EQ(wrapped->appliedTypes[2], GameEventType::EntityDespawned);
    EXPECT_EQ(wrapped->appliedTypes[3], GameEventType::ZoneUnloading);
}

// ===================================================================
// Combat (T104) -- replaces Application::syncCombatState(), the last poll
// in the state-layer sweep started at T39. TargetChanged/AutoAttackChanged
// are published from CombatManager's own choke points (combat.cpp), so
// unlike pet/inventory/spells these are exercised end-to-end (via
// EverQuest+CombatManager) in tests/test_combat_manager.cpp; the cases here
// cover only this decorator's own translation of the event into
// GameState::combat() calls, matching every other section's shape.
// ===================================================================

TEST_F(GameStateSyncBridgeTest, TargetChangedSetsCombatTarget) {
    TargetChangedData data;
    data.spawnId = 42;
    data.name = "a_gnoll";
    data.level = 7;
    data.hpPercent = 88;

    bridge->applyEvent(GameEvent(GameEventType::TargetChanged, data));

    EXPECT_TRUE(state->combat().hasTarget());
    EXPECT_EQ(state->combat().targetId(), 42u);
    EXPECT_EQ(state->combat().targetName(), "a_gnoll");
    EXPECT_EQ(state->combat().targetLevel(), 7);
    EXPECT_EQ(state->combat().targetHPPercent(), 88);
}

TEST_F(GameStateSyncBridgeTest, TargetChangedWithZeroSpawnIdClearsTarget) {
    TargetChangedData set;
    set.spawnId = 42;
    set.name = "a_gnoll";
    bridge->applyEvent(GameEvent(GameEventType::TargetChanged, set));
    ASSERT_TRUE(state->combat().hasTarget());

    TargetChangedData clear;  // spawnId=0 = cleared, matches the production convention
    bridge->applyEvent(GameEvent(GameEventType::TargetChanged, clear));

    EXPECT_FALSE(state->combat().hasTarget());
    EXPECT_EQ(state->combat().targetId(), 0u);
    EXPECT_EQ(state->combat().targetHPPercent(), 100);
}

// The correlation risk the plan named directly: re-applying TargetChanged
// for the SAME spawnId with a new hpPercent (the shape ZoneProcessMobHealth's
// re-publish takes when the current target is damaged) must update HP, not
// be dropped as a no-op identity change.
TEST_F(GameStateSyncBridgeTest, TargetChangedForSameTargetUpdatesHP) {
    TargetChangedData first;
    first.spawnId = 42;
    first.name = "a_gnoll";
    first.hpPercent = 100;
    bridge->applyEvent(GameEvent(GameEventType::TargetChanged, first));
    ASSERT_EQ(state->combat().targetHPPercent(), 100);

    TargetChangedData damaged;
    damaged.spawnId = 42;
    damaged.name = "a_gnoll";
    damaged.hpPercent = 63;
    bridge->applyEvent(GameEvent(GameEventType::TargetChanged, damaged));

    EXPECT_EQ(state->combat().targetId(), 42u);
    EXPECT_EQ(state->combat().targetHPPercent(), 63);
}

TEST_F(GameStateSyncBridgeTest, AutoAttackChangedSetsFlag) {
    AutoAttackChangedData data;
    data.enabled = true;

    bridge->applyEvent(GameEvent(GameEventType::AutoAttackChanged, data));

    EXPECT_TRUE(state->combat().isAutoAttacking());
}

TEST_F(GameStateSyncBridgeTest, AutoAttackChangedClearsFlag) {
    AutoAttackChangedData on;
    on.enabled = true;
    bridge->applyEvent(GameEvent(GameEventType::AutoAttackChanged, on));
    ASSERT_TRUE(state->combat().isAutoAttacking());

    AutoAttackChangedData off;
    off.enabled = false;
    bridge->applyEvent(GameEvent(GameEventType::AutoAttackChanged, off));

    EXPECT_FALSE(state->combat().isAutoAttacking());
}

TEST_F(GameStateSyncBridgeTest, CombatEventsStillDelegateToWrappedBridge) {
    TargetChangedData target;
    target.spawnId = 42;
    bridge->applyEvent(GameEvent(GameEventType::TargetChanged, target));

    AutoAttackChangedData autoAttack;
    autoAttack.enabled = true;
    bridge->applyEvent(GameEvent(GameEventType::AutoAttackChanged, autoAttack));

    ASSERT_EQ(wrapped->appliedTypes.size(), 2u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::TargetChanged);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::AutoAttackChanged);
}

// ===================================================================
// Group (T99) -- these cases were entirely absent before this unit.
// Application::m_gameState's GroupState never received a single update
// from any group event: EverQuest's own m_game_state/m_has_pending_invite
// updated correctly and the local chat line printed on a real invite, but
// CommandProcessor's m_state.group().hasPendingInvite() stayed false
// forever, discovered LIVE while verifying the T99 pilot's group-accept
// fix (review/T99/). Same "two GameState instances" shape T49 documented,
// this time on the write side. See refactor/T99_action_layer_unification_
// pilot.md.
// ===================================================================

TEST_F(GameStateSyncBridgeTest, GroupInviteReceivedSetsPendingInvite) {
    GroupInviteReceivedData data;
    data.inviterName = "Alice";

    bridge->applyEvent(GameEvent(GameEventType::GroupInviteReceived, data));

    EXPECT_TRUE(state->group().hasPendingInvite());
    EXPECT_EQ(state->group().pendingInviterName(), "Alice");
}

TEST_F(GameStateSyncBridgeTest, GroupChangedJoinSetsInGroupLeaderAndName) {
    GroupChangedData data;
    data.inGroup = true;
    data.isLeader = false;
    data.leaderName = "Alice";
    data.memberCount = 2;

    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, data));

    EXPECT_TRUE(state->group().inGroup());
    EXPECT_FALSE(state->group().isLeader());
    EXPECT_EQ(state->group().leaderName(), "Alice");
}

TEST_F(GameStateSyncBridgeTest, GroupChangedJoinAsLeaderSetsIsLeaderTrue) {
    GroupChangedData data;
    data.inGroup = true;
    data.isLeader = true;
    data.leaderName = "Bob";
    data.memberCount = 1;

    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, data));

    EXPECT_TRUE(state->group().inGroup());
    EXPECT_TRUE(state->group().isLeader());
    EXPECT_EQ(state->group().leaderName(), "Bob");
}

TEST_F(GameStateSyncBridgeTest, GroupChangedLeaveClearsGroupAndPendingInvite) {
    GroupChangedData joinData;
    joinData.inGroup = true;
    joinData.isLeader = false;
    joinData.leaderName = "Alice";
    joinData.memberCount = 2;
    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, joinData));
    ASSERT_TRUE(state->group().inGroup());

    // A pending invite that arrives after we're already in a group (e.g. a
    // second, unrelated invite) should also be cleared by leaving.
    GroupInviteReceivedData inviteData;
    inviteData.inviterName = "Carol";
    bridge->applyEvent(GameEvent(GameEventType::GroupInviteReceived, inviteData));
    ASSERT_TRUE(state->group().hasPendingInvite());

    GroupChangedData leaveData;
    leaveData.inGroup = false;
    leaveData.isLeader = false;
    leaveData.leaderName = "";
    leaveData.memberCount = 0;
    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, leaveData));

    EXPECT_FALSE(state->group().inGroup());
    EXPECT_FALSE(state->group().isLeader());
    EXPECT_FALSE(state->group().hasPendingInvite());
}

TEST_F(GameStateSyncBridgeTest, GroupChangedCancelInviteClearsPendingInviteWithoutTouchingGroup) {
    // Mirrors eq_group.cpp's ZoneProcessGroupCancelInvite: publishes
    // GroupChanged carrying the CURRENT (already-false) inGroup value
    // solely to signal "hide the pending-invite banner".
    GroupInviteReceivedData inviteData;
    inviteData.inviterName = "Dave";
    bridge->applyEvent(GameEvent(GameEventType::GroupInviteReceived, inviteData));
    ASSERT_TRUE(state->group().hasPendingInvite());

    GroupChangedData cancelData;
    cancelData.inGroup = false;
    cancelData.isLeader = false;
    cancelData.leaderName = "";
    cancelData.memberCount = 0;
    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, cancelData));

    EXPECT_FALSE(state->group().hasPendingInvite());
    EXPECT_FALSE(state->group().inGroup());
}

TEST_F(GameStateSyncBridgeTest, GroupMemberUpdatedSetsMemberFields) {
    GroupChangedData joinData;
    joinData.inGroup = true;
    joinData.isLeader = true;
    joinData.leaderName = "Testmelee";
    joinData.memberCount = 1;
    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, joinData));

    GroupMemberUpdatedData memberData;
    memberData.memberIndex = 1;
    memberData.name = "Testbuddy";
    memberData.spawnId = 33;
    memberData.level = 12;
    memberData.classId = 1;
    memberData.hpPercent = 88;
    memberData.manaPercent = 50;
    memberData.inZone = true;
    bridge->applyEvent(GameEvent(GameEventType::GroupMemberUpdated, memberData));

    const auto* member = state->group().getMember(1);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->name, "Testbuddy");
    EXPECT_EQ(member->spawnId, 33);
    EXPECT_EQ(member->level, 12);
    EXPECT_EQ(member->hpPercent, 88);
    EXPECT_EQ(member->manaPercent, 50);
    EXPECT_TRUE(member->inZone);
    EXPECT_FALSE(member->isLeader);
}

TEST_F(GameStateSyncBridgeTest, GroupEventsStillDelegateToWrappedBridge) {
    GroupInviteReceivedData inviteData;
    inviteData.inviterName = "Alice";
    bridge->applyEvent(GameEvent(GameEventType::GroupInviteReceived, inviteData));

    GroupChangedData changedData;
    changedData.inGroup = true;
    bridge->applyEvent(GameEvent(GameEventType::GroupChanged, changedData));

    GroupMemberUpdatedData memberData;
    memberData.memberIndex = 0;
    memberData.name = "Testmelee";
    bridge->applyEvent(GameEvent(GameEventType::GroupMemberUpdated, memberData));

    ASSERT_EQ(wrapped->appliedTypes.size(), 3u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::GroupInviteReceived);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::GroupChanged);
    EXPECT_EQ(wrapped->appliedTypes[2], GameEventType::GroupMemberUpdated);
}

// ---------------------------------------------------------------------
// Trade (T103) -- TradeManager (client/trade_manager.h) has published a
// complete, typed event for every one of these transitions since before
// this unit (eq_merchant.cpp's SetupTradeManagerCallbacks()); the gap was
// purely that nothing consumed them (IrrlichtBridge only LOG_TRACE'd each
// case). No new event type was needed -- see game_state_sync_bridge.h's
// T103 addendum and trade_state.h's class comment for the two fields
// verified NOT reconstructible (own-side items, outgoing pending-request
// sub-state) and why neither is tracked.
// ---------------------------------------------------------------------

TEST_F(GameStateSyncBridgeTest, TradeRequestReceivedSetsPendingRequest) {
    TradeRequestReceivedData data;
    data.spawnId = 555;
    data.name = "Testbuddy";

    bridge->applyEvent(GameEvent(GameEventType::TradeRequestReceived, data));

    EXPECT_TRUE(state->trade().hasPendingRequest());
    EXPECT_EQ(state->trade().pendingPartnerId(), 555u);
    EXPECT_EQ(state->trade().pendingPartnerName(), "Testbuddy");
    EXPECT_FALSE(state->trade().active());
}

TEST_F(GameStateSyncBridgeTest, TradeStartedSetsActiveAndClearsPendingRequest) {
    TradeRequestReceivedData pending;
    pending.spawnId = 555;
    pending.name = "Testbuddy";
    bridge->applyEvent(GameEvent(GameEventType::TradeRequestReceived, pending));
    ASSERT_TRUE(state->trade().hasPendingRequest());

    TradeStartedData started;
    started.partnerId = 555;
    started.partnerName = "Testbuddy";
    started.isNpc = false;
    bridge->applyEvent(GameEvent(GameEventType::TradeStarted, started));

    EXPECT_TRUE(state->trade().active());
    EXPECT_EQ(state->trade().partnerId(), 555);
    EXPECT_EQ(state->trade().partnerName(), "Testbuddy");
    EXPECT_FALSE(state->trade().isNpcTrade());
    EXPECT_FALSE(state->trade().hasPendingRequest())
        << "a request that just became active is no longer pending";
}

TEST_F(GameStateSyncBridgeTest, TradeItemUpdatedPartnerSideSetsPartnerItem) {
    TradeStartedData started;
    started.partnerId = 555;
    started.partnerName = "Testbuddy";
    bridge->applyEvent(GameEvent(GameEventType::TradeStarted, started));

    TradeItemUpdatedData item;
    item.who = 1;  // partner
    item.slot = 2;
    item.itemId = 1001;
    item.itemName = "Rusty Sword";
    bridge->applyEvent(GameEvent(GameEventType::TradeItemUpdated, item));

    EXPECT_TRUE(state->trade().hasPartnerItem(2));
    EXPECT_EQ(state->trade().partnerItemId(2), 1001u);
    EXPECT_EQ(state->trade().partnerItemName(2), "Rusty Sword");
    EXPECT_FALSE(state->trade().hasPartnerItem(0))
        << "only slot 2 was set";
}

TEST_F(GameStateSyncBridgeTest, TradeItemUpdatedOwnSideIsIgnored) {
    // who==0 (own side) is never actually published in production (see
    // trade_state.h's class comment) -- confirm this decorator doesn't
    // crash or misfile it as a partner item if it ever were.
    TradeItemUpdatedData item;
    item.who = 0;  // own
    item.slot = 0;
    item.itemId = 1001;
    item.itemName = "Rusty Sword";
    bridge->applyEvent(GameEvent(GameEventType::TradeItemUpdated, item));

    EXPECT_FALSE(state->trade().hasPartnerItem(0));
}

TEST_F(GameStateSyncBridgeTest, TradeMoneyUpdatedSetsOwnAndPartnerIndependently) {
    TradeMoneyUpdatedData own;
    own.who = 0;
    own.platinum = 1;
    own.gold = 2;
    own.silver = 3;
    own.copper = 4;
    bridge->applyEvent(GameEvent(GameEventType::TradeMoneyUpdated, own));

    TradeMoneyUpdatedData partner;
    partner.who = 1;
    partner.platinum = 10;
    partner.gold = 20;
    partner.silver = 30;
    partner.copper = 40;
    bridge->applyEvent(GameEvent(GameEventType::TradeMoneyUpdated, partner));

    EXPECT_EQ(state->trade().ownMoney().platinum, 1u);
    EXPECT_EQ(state->trade().ownMoney().copper, 4u);
    EXPECT_EQ(state->trade().partnerMoney().platinum, 10u);
    EXPECT_EQ(state->trade().partnerMoney().copper, 40u);
}

TEST_F(GameStateSyncBridgeTest, TradeAcceptStateChangedSetsBothFlags) {
    TradeAcceptStateChangedData data;
    data.ownAccepted = true;
    data.partnerAccepted = false;
    bridge->applyEvent(GameEvent(GameEventType::TradeAcceptStateChanged, data));

    EXPECT_TRUE(state->trade().ownAccepted());
    EXPECT_FALSE(state->trade().partnerAccepted());
}

TEST_F(GameStateSyncBridgeTest, TradeCancelledResetsEverything) {
    TradeStartedData started;
    started.partnerId = 555;
    started.partnerName = "Testbuddy";
    bridge->applyEvent(GameEvent(GameEventType::TradeStarted, started));
    TradeAcceptStateChangedData accept;
    accept.ownAccepted = true;
    accept.partnerAccepted = false;
    bridge->applyEvent(GameEvent(GameEventType::TradeAcceptStateChanged, accept));
    ASSERT_TRUE(state->trade().active());

    bridge->applyEvent(GameEvent(GameEventType::TradeCancelled, TradeCancelledData{}));

    EXPECT_FALSE(state->trade().active());
    EXPECT_FALSE(state->trade().hasPendingRequest());
    EXPECT_FALSE(state->trade().ownAccepted());
    EXPECT_EQ(state->trade().partnerName(), "");
}

TEST_F(GameStateSyncBridgeTest, TradeCompletedResetsEverything) {
    TradeStartedData started;
    started.partnerId = 555;
    started.partnerName = "Testbuddy";
    bridge->applyEvent(GameEvent(GameEventType::TradeStarted, started));
    ASSERT_TRUE(state->trade().active());

    bridge->applyEvent(GameEvent(GameEventType::TradeCompleted, TradeCompletedData{}));

    EXPECT_FALSE(state->trade().active());
    EXPECT_EQ(state->trade().partnerName(), "");
}

TEST_F(GameStateSyncBridgeTest, TradeEventsStillDelegateToWrappedBridge) {
    TradeRequestReceivedData pending;
    pending.spawnId = 555;
    pending.name = "Testbuddy";
    bridge->applyEvent(GameEvent(GameEventType::TradeRequestReceived, pending));

    TradeStartedData started;
    started.partnerId = 555;
    started.partnerName = "Testbuddy";
    bridge->applyEvent(GameEvent(GameEventType::TradeStarted, started));

    bridge->applyEvent(GameEvent(GameEventType::TradeCancelled, TradeCancelledData{}));

    ASSERT_EQ(wrapped->appliedTypes.size(), 3u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::TradeRequestReceived);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::TradeStarted);
    EXPECT_EQ(wrapped->appliedTypes[2], GameEventType::TradeCancelled);
}

// ============================================================================
// T218: PlayerFlagsChanged -> PlayerState AFK/anonymous/roleplay projection.
// Before this unit nothing ever wrote these three flags into
// Application::m_gameState, so ActionDispatcher::toggleAFK() and
// CommandProcessor's /anon (which read this copy to compute !current)
// always computed "turn ON". The toggle-off tests below are the ones that
// encode the live symptom: without the projection case the flag stays
// false after the ON event, so the action layer would never see it as ON.
// ============================================================================

TEST_F(GameStateSyncBridgeTest, PlayerFlagsChangedProjectsAfkOn) {
    PlayerFlagsChangedData data;
    data.afk = true;
    data.anonymous = false;
    data.roleplay = false;

    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, data));

    EXPECT_TRUE(state->player().isAFK());
    EXPECT_FALSE(state->player().isAnonymous());
    EXPECT_FALSE(state->player().isRoleplay());
}

TEST_F(GameStateSyncBridgeTest, PlayerFlagsChangedAfkOnThenOffRoundTrips) {
    PlayerFlagsChangedData on;
    on.afk = true;
    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, on));
    ASSERT_TRUE(state->player().isAFK());

    // What toggleAFK() computes next: !isAFK() must now be false.
    EXPECT_FALSE(!state->player().isAFK());

    PlayerFlagsChangedData off;
    off.afk = false;
    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, off));
    EXPECT_FALSE(state->player().isAFK());
}

TEST_F(GameStateSyncBridgeTest, PlayerFlagsChangedProjectsAnonymousAndRoleplayIndependently) {
    PlayerFlagsChangedData anon;
    anon.anonymous = true;
    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, anon));
    EXPECT_TRUE(state->player().isAnonymous());
    EXPECT_FALSE(state->player().isRoleplay());
    EXPECT_FALSE(state->player().isAFK());

    PlayerFlagsChangedData rp;
    rp.anonymous = false;
    rp.roleplay = true;
    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, rp));
    EXPECT_FALSE(state->player().isAnonymous());
    EXPECT_TRUE(state->player().isRoleplay());

    PlayerFlagsChangedData clear;
    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, clear));
    EXPECT_FALSE(state->player().isAnonymous());
    EXPECT_FALSE(state->player().isRoleplay());
}

TEST_F(GameStateSyncBridgeTest, PlayerFlagsChangedStillDelegatesToWrappedBridge) {
    PlayerFlagsChangedData data;
    data.afk = true;
    bridge->applyEvent(GameEvent(GameEventType::PlayerFlagsChanged, data));

    ASSERT_EQ(wrapped->appliedTypes.size(), 1u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::PlayerFlagsChanged);
}

// ========== T219: door projection ==========
//
// Before T219 nothing populated state::GameState::doors() on EITHER
// instance -- the live door data was a third DoorStateManager on EverQuest
// (deleted in T219). These cases are the regression net the 2026-08-25
// projection decision (docs/decisions.md) requires: each one fails when
// its GameStateSyncBridge case is removed (measured in the unit's Review).

namespace {

DoorSpawnedData makeDoorSpawned(uint8_t id, const char* name, float x, float y, float z) {
    DoorSpawnedData d;
    d.doorId = id;
    d.name = name;
    d.x = x;
    d.y = y;
    d.z = z;
    d.heading = 90.0f;
    d.incline = 7;
    d.size = 110;
    d.opentype = 58;
    d.isOpen = false;
    d.invertState = true;
    d.doorParam = 12345;
    return d;
}

} // namespace

TEST_F(GameStateSyncBridgeTest, DoorSpawnedAddsCompleteDoorToProjection) {
    ASSERT_EQ(state->doors().getDoorCount(), 0u);

    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(7, "QEYDOOR1", 10.0f, 20.0f, 3.0f)));

    ASSERT_EQ(state->doors().getDoorCount(), 1u);
    const EQT::DoorState* door = state->doors().getDoor(7);
    ASSERT_NE(door, nullptr);
    EXPECT_EQ(door->doorId, 7);
    EXPECT_EQ(door->name, "QEYDOOR1");
    EXPECT_FLOAT_EQ(door->x, 10.0f);
    EXPECT_FLOAT_EQ(door->y, 20.0f);
    EXPECT_FLOAT_EQ(door->z, 3.0f);
    EXPECT_FLOAT_EQ(door->heading, 90.0f);
    EXPECT_EQ(door->incline, 7u);
    EXPECT_EQ(door->size, 110);
    EXPECT_EQ(door->opentype, 58);
    EXPECT_FALSE(door->isOpen);
    // The two fields DoorSpawnedData did not carry before T219 -- the
    // projection must hold the FULL DoorState, not a partial.
    EXPECT_TRUE(door->invertState);
    EXPECT_EQ(door->doorParam, 12345u);
}

TEST_F(GameStateSyncBridgeTest, DoorSpawnedMakesNearestDoorQueryFindIt) {
    // The exact query ActionDispatcher::clickNearestDoor()/interactNearest()
    // run -- the one that always returned null before T219.
    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(1, "FAR", 500.0f, 500.0f, 0.0f)));
    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(2, "NEAR", 12.0f, 8.0f, 0.0f)));

    const EQT::DoorState* nearest = state->doors().getNearestDoor(10.0f, 10.0f, 0.0f);
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->doorId, 2);
    EXPECT_EQ(nearest->name, "NEAR");
}

TEST_F(GameStateSyncBridgeTest, DoorStateChangedUpdatesOpenFlag) {
    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(3, "D", 0.0f, 0.0f, 0.0f)));
    const EQT::DoorState* door = state->doors().getDoor(3);
    ASSERT_NE(door, nullptr);  // fails cleanly (no deref) when the case is absent
    ASSERT_FALSE(door->isOpen);

    DoorStateChangedData open;
    open.doorId = 3;
    open.isOpen = true;
    open.userInitiated = true;
    bridge->applyEvent(GameEvent(GameEventType::DoorStateChanged, open));
    EXPECT_TRUE(door->isOpen);

    DoorStateChangedData close;
    close.doorId = 3;
    close.isOpen = false;
    bridge->applyEvent(GameEvent(GameEventType::DoorStateChanged, close));
    EXPECT_FALSE(door->isOpen);
}

TEST_F(GameStateSyncBridgeTest, DoorStateChangedForUnknownDoorIsANoOp) {
    DoorStateChangedData open;
    open.doorId = 99;
    open.isOpen = true;
    bridge->applyEvent(GameEvent(GameEventType::DoorStateChanged, open));
    EXPECT_EQ(state->doors().getDoorCount(), 0u);
    EXPECT_EQ(state->doors().getDoor(99), nullptr);
}

TEST_F(GameStateSyncBridgeTest, ZoneUnloadingClearsAllDoors) {
    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(1, "A", 0.0f, 0.0f, 0.0f)));
    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(2, "B", 1.0f, 1.0f, 0.0f)));
    ASSERT_EQ(state->doors().getDoorCount(), 2u);

    bridge->applyEvent(GameEvent(GameEventType::ZoneUnloading, ZoneUnloadingData{}));

    EXPECT_EQ(state->doors().getDoorCount(), 0u);
    EXPECT_EQ(state->doors().getNearestDoor(0.0f, 0.0f, 0.0f), nullptr);
}

TEST_F(GameStateSyncBridgeTest, DoorEventsStillDelegateToWrappedBridge) {
    bridge->applyEvent(GameEvent(GameEventType::DoorSpawned,
                                 makeDoorSpawned(1, "A", 0.0f, 0.0f, 0.0f)));
    DoorStateChangedData open;
    open.doorId = 1;
    open.isOpen = true;
    bridge->applyEvent(GameEvent(GameEventType::DoorStateChanged, open));

    ASSERT_EQ(wrapped->appliedTypes.size(), 2u);
    EXPECT_EQ(wrapped->appliedTypes[0], GameEventType::DoorSpawned);
    EXPECT_EQ(wrapped->appliedTypes[1], GameEventType::DoorStateChanged);
}

// ===========================================================================
// T220: two-stage drain (graphics runs). projectPendingEvents() runs on the
// game thread and mutates GameState; forwardProjectedEvents() runs on the
// render thread and only forwards to the wrapped bridge. These tests pin the
// contract the split relies on: every event is projected exactly once and
// forwarded exactly once, in push order, with D24's per-entity EntityMoved
// coalescing still holding on the renderer side across several game-thread
// batches. Both entry points are driven from the test thread -- the
// property under test is once-and-in-order, not thread scheduling (the
// thread half of this unit is the TSAN before/after evidence in
// refactor/T220_state_projection_on_game_thread.md).
// ===========================================================================

namespace {

// Records the full event so order AND payload can be asserted.
class FullRecordingBridge : public GameStateBridge {
public:
    void applyEvent(const GameEvent& event) override {
        applied.push_back(event);
    }
    std::vector<GameEvent> applied;
};

class GameStateSyncBridgeSplitDrainTest : public ::testing::Test {
protected:
    void SetUp() override {
        state = std::make_unique<GameState>();
        wrapped = std::make_unique<FullRecordingBridge>();
        bridge = std::make_unique<GameStateSyncBridge>(*wrapped, *state);
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

    static GameEvent snapshot(uint16_t spawnId, const std::string& name) {
        EntityStateSnapshotData d;
        d.entity.spawnId = spawnId;
        d.entity.name = name;
        return GameEvent(GameEventType::EntityStateSnapshot, d);
    }

    static GameEvent pet(uint16_t spawnId) {
        PetCreatedData d;
        d.spawnId = spawnId;
        d.name = "Fido";
        d.level = 1;
        return GameEvent(GameEventType::PetCreated, d);
    }

    std::unique_ptr<GameState> state;
    std::unique_ptr<FullRecordingBridge> wrapped;
    std::unique_ptr<GameStateSyncBridge> bridge;
};

} // namespace

TEST_F(GameStateSyncBridgeSplitDrainTest, ProjectStageMutatesStateButForwardsNothing) {
    bridge->pushEvent(pet(7));
    bridge->pushEvent(GameEvent(GameEventType::SystemMessage, ChatMessageData{}));

    bridge->projectPendingEvents();

    // Stage 1 projected...
    EXPECT_TRUE(state->pet().hasPet());
    EXPECT_EQ(state->pet().petSpawnId(), 7);
    // ...and handed NOTHING to the wrapped (render-side) bridge yet.
    EXPECT_TRUE(wrapped->applied.empty());
}

TEST_F(GameStateSyncBridgeSplitDrainTest, ForwardStageDeliversEachEventOnceInPushOrder) {
    bridge->pushEvent(pet(7));
    bridge->pushEvent(GameEvent(GameEventType::SystemMessage, ChatMessageData{}));
    bridge->pushEvent(snapshot(9, "Guard_Test"));

    bridge->projectPendingEvents();
    bridge->forwardProjectedEvents();

    ASSERT_EQ(wrapped->applied.size(), 3u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::PetCreated);
    EXPECT_EQ(wrapped->applied[1].type, GameEventType::SystemMessage);
    EXPECT_EQ(wrapped->applied[2].type, GameEventType::EntityStateSnapshot);

    // Forwarding is destructive: a second render drain delivers nothing
    // more, and a second projection pass finds nothing to project.
    bridge->forwardProjectedEvents();
    bridge->projectPendingEvents();
    bridge->forwardProjectedEvents();
    EXPECT_EQ(wrapped->applied.size(), 3u);
}

TEST_F(GameStateSyncBridgeSplitDrainTest, ForwardStageNeverTouchesState) {
    bridge->pushEvent(pet(7));
    bridge->projectPendingEvents();
    ASSERT_TRUE(state->pet().hasPet());

    // Mutate the read model between the stages, the way a game-thread
    // command could; the render-side forward must not re-project.
    state->pet().clearPet();
    bridge->forwardProjectedEvents();

    EXPECT_FALSE(state->pet().hasPet());
    ASSERT_EQ(wrapped->applied.size(), 1u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::PetCreated);
}

TEST_F(GameStateSyncBridgeSplitDrainTest, OrderPreservedAcrossSeveralProjectBatchesBeforeOneForward) {
    // Several game ticks may elapse between two rendered frames (loading,
    // slow llvmpipe frame). The render thread must still see push order.
    bridge->pushEvent(snapshot(1, "A"));
    bridge->projectPendingEvents();
    bridge->pushEvent(snapshot(2, "B"));
    bridge->pushEvent(GameEvent(GameEventType::SystemMessage, ChatMessageData{}));
    bridge->projectPendingEvents();
    bridge->pushEvent(snapshot(3, "C"));
    bridge->projectPendingEvents();

    EXPECT_TRUE(state->entities().hasEntity(1));
    EXPECT_TRUE(state->entities().hasEntity(2));
    EXPECT_TRUE(state->entities().hasEntity(3));
    EXPECT_TRUE(wrapped->applied.empty());

    bridge->forwardProjectedEvents();

    ASSERT_EQ(wrapped->applied.size(), 4u);
    EXPECT_EQ(std::get<EntityStateSnapshotData>(wrapped->applied[0].data).entity.name, "A");
    EXPECT_EQ(std::get<EntityStateSnapshotData>(wrapped->applied[1].data).entity.name, "B");
    EXPECT_EQ(wrapped->applied[2].type, GameEventType::SystemMessage);
    EXPECT_EQ(std::get<EntityStateSnapshotData>(wrapped->applied[3].data).entity.name, "C");
}

TEST_F(GameStateSyncBridgeSplitDrainTest, D24CoalescingHoldsOnRendererSideAcrossBatches) {
    bridge->pushEvent(snapshot(5, "Mover"));
    bridge->pushEvent(snapshot(6, "Still"));
    bridge->projectPendingEvents();

    // Batch 1: two moves for 5, one for 6.
    bridge->pushEvent(moved(5, 10.0f));
    bridge->pushEvent(moved(6, 1.0f));
    bridge->pushEvent(moved(5, 20.0f));
    bridge->projectPendingEvents();
    // The projection saw batch 1's latest.
    ASSERT_NE(state->entities().getEntity(5), nullptr);
    EXPECT_FLOAT_EQ(state->entities().getEntity(5)->x, 20.0f);

    // Batch 2: another move for 5 before the renderer drains.
    bridge->pushEvent(moved(5, 30.0f));
    bridge->projectPendingEvents();
    EXPECT_FLOAT_EQ(state->entities().getEntity(5)->x, 30.0f);

    bridge->forwardProjectedEvents();

    // Renderer side: 2 snapshots + exactly ONE EntityMoved per entity, the
    // latest (30 for 5, 1 for 6), in order -- the same contract the
    // renderer had when it drained eventQueue_ directly.
    ASSERT_EQ(wrapped->applied.size(), 4u);
    EXPECT_EQ(wrapped->applied[0].type, GameEventType::EntityStateSnapshot);
    EXPECT_EQ(wrapped->applied[1].type, GameEventType::EntityStateSnapshot);
    EXPECT_EQ(wrapped->applied[2].type, GameEventType::EntityMoved);
    EXPECT_EQ(std::get<EntityMovedData>(wrapped->applied[2].data).spawnId, 6);
    EXPECT_FLOAT_EQ(std::get<EntityMovedData>(wrapped->applied[2].data).x, 1.0f);
    EXPECT_EQ(wrapped->applied[3].type, GameEventType::EntityMoved);
    EXPECT_EQ(std::get<EntityMovedData>(wrapped->applied[3].data).spawnId, 5);
    EXPECT_FLOAT_EQ(std::get<EntityMovedData>(wrapped->applied[3].data).x, 30.0f);
}

TEST_F(GameStateSyncBridgeSplitDrainTest, SingleStageApplyEventStillProjectsAndForwardsTogether) {
    // Headless path (ConsoleBridge, one game-thread drain) is unchanged:
    // applyEvent() does both halves on the calling thread, and nothing is
    // left behind in the render-side queue.
    bridge->applyEvent(pet(7));
    EXPECT_TRUE(state->pet().hasPet());
    ASSERT_EQ(wrapped->applied.size(), 1u);

    bridge->forwardProjectedEvents();
    EXPECT_EQ(wrapped->applied.size(), 1u);
}
