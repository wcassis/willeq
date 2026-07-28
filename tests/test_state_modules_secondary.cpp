// Unit tests for the eqt::state state modules, part 2 (GroupState, PetState,
// WorldState, CombatState, TradeskillState, GameState). Split from
// test_state_modules.cpp (which covers EntityManager, PlayerState,
// SpellState, InventoryState) purely to stay under the project's
// file-size check (scripts/refactor/check_file_sizes.sh) -- no other
// significance to the split.
//
// Territory: tests/ only (T02, refactor/T02_state_event_bus.md). No source
// changes were made to produce this file. Tests assert behavior through
// public accessors only (charter rule) -- no reaching into internals.

#include "client/state/entity_manager.h"
#include "client/state/player_state.h"
#include "client/state/group_state.h"
#include "client/state/pet_state.h"
#include "client/state/world_state.h"
#include "client/state/combat_state.h"
#include "client/state/tradeskill_state.h"
#include "client/state/game_state.h"
#include "client/state/event_bus.h"

#include <gtest/gtest.h>

using namespace eqt::state;

namespace {
Entity makeEntity(uint16_t spawnId, const std::string& name = "Bob",
                   float x = 0, float y = 0, float z = 0) {
    Entity e;
    e.spawnId = spawnId;
    e.name = name;
    e.x = x;
    e.y = y;
    e.z = z;
    e.npcType = 1; // NPC by default
    return e;
}
} // namespace

// =============================================================================
// GroupState -- priority 2.
// =============================================================================

TEST(GroupStateTest, DefaultConstructedNotInGroup) {
    GroupState gs;
    EXPECT_FALSE(gs.inGroup());
    EXPECT_FALSE(gs.isLeader());
    EXPECT_EQ(gs.memberCount(), 0);
    EXPECT_FALSE(gs.hasPendingInvite());
}

TEST(GroupStateTest, MemberAccessorsOutOfRangeReturnNullptr) {
    GroupState gs;
    EXPECT_EQ(gs.getMember(-1), nullptr);
    EXPECT_EQ(gs.getMember(MAX_GROUP_MEMBERS), nullptr);
    EXPECT_EQ(gs.getMemberMutable(-1), nullptr);
}

TEST(GroupStateTest, SetInGroupFiresEventAndLeavingClearsGroup) {
    GroupState gs;
    EventBus bus;
    gs.setEventBus(&bus);
    int changed = 0;
    bus.subscribe(GameEventType::GroupChanged, [&](const GameEvent&) { ++changed; });

    gs.setInGroup(true);
    EXPECT_TRUE(gs.inGroup());
    EXPECT_EQ(changed, 1);

    GroupMember m;
    m.name = "Ally";
    gs.setMember(0, m);
    EXPECT_EQ(gs.memberCount(), 1);

    gs.setInGroup(false); // leaving clears the group
    EXPECT_FALSE(gs.inGroup());
    EXPECT_EQ(gs.memberCount(), 0);
    EXPECT_EQ(gs.getMember(0)->name, "");
}

TEST(GroupStateTest, SetMemberUpdatesAndRecalculatesCount) {
    GroupState gs;
    GroupMember m;
    m.name = "Ally";
    m.spawnId = 5;
    gs.setMember(2, m);

    EXPECT_EQ(gs.memberCount(), 1);
    ASSERT_NE(gs.getMember(2), nullptr);
    EXPECT_EQ(gs.getMember(2)->name, "Ally");
    EXPECT_EQ(gs.findMemberByName("Ally"), 2);
    EXPECT_EQ(gs.findMemberBySpawnId(5), 2);
}

TEST(GroupStateTest, SetMemberOutOfRangeIsNoOp) {
    GroupState gs;
    GroupMember m;
    EXPECT_NO_THROW(gs.setMember(-1, m));
    EXPECT_NO_THROW(gs.setMember(MAX_GROUP_MEMBERS, m));
}

TEST(GroupStateTest, FindMemberByNameOrSpawnIdAbsentReturnsMinusOne) {
    GroupState gs;
    EXPECT_EQ(gs.findMemberByName("Nobody"), -1);
    EXPECT_EQ(gs.findMemberBySpawnId(0), -1); // 0 is documented "not in zone"
    EXPECT_EQ(gs.findMemberBySpawnId(12345), -1);
}

TEST(GroupStateTest, UpdateMemberSpawnIdTogglesInZone) {
    GroupState gs;
    GroupMember m;
    m.name = "Ally";
    gs.setMember(0, m);

    gs.updateMemberSpawnId(0, 42);
    EXPECT_TRUE(gs.getMember(0)->inZone);
    EXPECT_EQ(gs.getMember(0)->spawnId, 42u);

    gs.updateMemberSpawnId(0, 0);
    EXPECT_FALSE(gs.getMember(0)->inZone);
}

TEST(GroupStateTest, SetMemberInZoneFalseClearsSpawnId) {
    GroupState gs;
    GroupMember m;
    m.name = "Ally";
    m.spawnId = 7;
    gs.setMember(0, m);

    gs.setMemberInZone(0, false);
    EXPECT_FALSE(gs.getMember(0)->inZone);
    EXPECT_EQ(gs.getMember(0)->spawnId, 0u);
}

TEST(GroupStateTest, PendingInviteLifecycle) {
    GroupState gs;
    EXPECT_FALSE(gs.hasPendingInvite());
    gs.setPendingInvite("Inviter");
    EXPECT_TRUE(gs.hasPendingInvite());
    EXPECT_EQ(gs.pendingInviterName(), "Inviter");
    gs.clearPendingInvite();
    EXPECT_FALSE(gs.hasPendingInvite());
}

TEST(GroupStateTest, RecalculateMemberCountCountsNonEmptyMembers) {
    GroupState gs;
    GroupMember m1;
    m1.name = "A";
    GroupMember m2;
    m2.name = "B";
    gs.setMember(0, m1);
    gs.setMember(1, m2);
    EXPECT_EQ(gs.memberCount(), 2);
}

// =============================================================================
// PetState -- priority 2.
// =============================================================================

TEST(PetStateTest, DefaultConstructedHasNoPet) {
    PetState ps;
    EXPECT_FALSE(ps.hasPet());
    EXPECT_EQ(ps.petSpawnId(), 0u);
    EXPECT_EQ(ps.petHPPercent(), 100);
    EXPECT_EQ(ps.petManaPercent(), 100);
}

TEST(PetStateTest, SetPetFiresCreatedEvent) {
    PetState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int created = 0, removed = 0;
    bus.subscribe(GameEventType::PetCreated, [&](const GameEvent&) { ++created; });
    bus.subscribe(GameEventType::PetRemoved, [&](const GameEvent&) { ++removed; });

    ps.setPet(100, "Fido", 10);
    EXPECT_TRUE(ps.hasPet());
    EXPECT_EQ(ps.petName(), "Fido");
    EXPECT_EQ(created, 1);
    EXPECT_EQ(removed, 0);
}

TEST(PetStateTest, ClearPetFiresRemovedEventOnlyWhenHadPet) {
    PetState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int removed = 0;
    bus.subscribe(GameEventType::PetRemoved, [&](const GameEvent&) { ++removed; });

    ps.clearPet(); // no pet -- no-op
    EXPECT_EQ(removed, 0);

    ps.setPet(1, "Fido", 1);
    ps.clearPet();
    EXPECT_FALSE(ps.hasPet());
    EXPECT_EQ(removed, 1);
}

TEST(PetStateTest, UpdatePetHPAndManaFireOnlyOnChange) {
    PetState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int statsChanged = 0;
    bus.subscribe(GameEventType::PetStatsChanged, [&](const GameEvent&) { ++statsChanged; });

    ps.updatePetHP(100); // same as default
    EXPECT_EQ(statsChanged, 0);
    ps.updatePetHP(50);
    EXPECT_EQ(statsChanged, 1);
    ps.updatePetMana(50);
    EXPECT_EQ(statsChanged, 2);

    ps.updatePetStats(50, 50); // both unchanged
    EXPECT_EQ(statsChanged, 2);
    ps.updatePetStats(10, 10);
    EXPECT_EQ(statsChanged, 3);
}

TEST(PetStateTest, ButtonStateBoundsAndToggle) {
    PetState ps;
    EXPECT_FALSE(ps.getButtonState(PET_BUTTON_COUNT)); // out of range -> false
    EXPECT_FALSE(ps.getButtonState(0));

    ps.setButtonState(0, true);
    EXPECT_TRUE(ps.getButtonState(0));

    EXPECT_NO_THROW(ps.setButtonState(PET_BUTTON_COUNT, true)); // out of range no-op
}

TEST(PetStateTest, ResetClearsPetAndFiresRemovedIfHadPet) {
    PetState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int removed = 0;
    bus.subscribe(GameEventType::PetRemoved, [&](const GameEvent&) { ++removed; });

    ps.setPet(1, "Fido", 1);
    ps.setButtonState(0, true);
    ps.reset();

    EXPECT_FALSE(ps.hasPet());
    EXPECT_FALSE(ps.getButtonState(0));
    EXPECT_EQ(removed, 1);
}

// =============================================================================
// WorldState -- priority 2.
// =============================================================================

TEST(WorldStateTest, DefaultConstructedHasSaneValues) {
    WorldState ws;
    EXPECT_EQ(ws.zoneName(), "");
    EXPECT_EQ(ws.zoneId(), 0u);
    EXPECT_FALSE(ws.isZoneLoading());
    EXPECT_EQ(ws.timeHour(), 12);
    EXPECT_TRUE(ws.isDay());
    EXPECT_FALSE(ws.isNight());
    EXPECT_FALSE(ws.hasPendingZone());
    EXPECT_FALSE(ws.isZoneConnected());
    EXPECT_FALSE(ws.isFullyZonedIn());
}

TEST(WorldStateTest, SetZoneFiresOnlyOnChange) {
    WorldState ws;
    EventBus bus;
    ws.setEventBus(&bus);
    int zoneChanged = 0;
    bus.subscribe(GameEventType::ZoneChanged, [&](const GameEvent&) { ++zoneChanged; });

    ws.setZone("qeynos", 1);
    EXPECT_EQ(zoneChanged, 1);
    ws.setZone("qeynos", 1); // unchanged
    EXPECT_EQ(zoneChanged, 1);
    ws.setZone("freeport", 2);
    EXPECT_EQ(zoneChanged, 2);
}

TEST(WorldStateTest, IsNightBoundary) {
    WorldState ws;
    ws.setTimeOfDay(5, 59);
    EXPECT_TRUE(ws.isNight());
    ws.setTimeOfDay(6, 0);
    EXPECT_FALSE(ws.isNight());
    ws.setTimeOfDay(19, 59);
    EXPECT_FALSE(ws.isNight());
    ws.setTimeOfDay(20, 0);
    EXPECT_TRUE(ws.isNight());
}

TEST(WorldStateTest, SetTimeOfDayFiresOnlyOnChange) {
    WorldState ws;
    EventBus bus;
    ws.setEventBus(&bus);
    int timeChanged = 0;
    bus.subscribe(GameEventType::TimeOfDayChanged, [&](const GameEvent&) { ++timeChanged; });

    ws.setTimeOfDay(10, 0); // 12->10 is a change
    EXPECT_EQ(timeChanged, 1);
    ws.setTimeOfDay(10, 0); // unchanged
    EXPECT_EQ(timeChanged, 1);
}

TEST(WorldStateTest, PendingZoneLifecycle) {
    WorldState ws;
    EXPECT_FALSE(ws.hasPendingZone());
    ws.setPendingZone(5, 1, 2, 3, 45.0f);
    EXPECT_TRUE(ws.hasPendingZone());
    EXPECT_EQ(ws.pendingZoneId(), 5u);
    ws.clearPendingZone();
    EXPECT_FALSE(ws.hasPendingZone());
    EXPECT_EQ(ws.pendingZoneId(), 0u);
}

TEST(WorldStateTest, IsFullyZonedInRequiresBothFlags) {
    WorldState ws;
    ws.setZoneConnected(true);
    EXPECT_FALSE(ws.isFullyZonedIn());
    ws.setClientReady(true);
    EXPECT_TRUE(ws.isFullyZonedIn());
}

TEST(WorldStateTest, ResetZoneStateClearsTransitionAndConnectionState) {
    WorldState ws;
    ws.setZoneConnected(true);
    ws.setClientReady(true);
    ws.setPendingZone(1, 1, 1, 1, 1);
    ws.setZoneLineTriggered(true);
    ws.setZoning(true);

    ws.resetZoneState();

    EXPECT_FALSE(ws.isZoneConnected());
    EXPECT_FALSE(ws.isClientReady());
    EXPECT_FALSE(ws.hasPendingZone());
    EXPECT_FALSE(ws.zoneLineTriggered());
    EXPECT_FALSE(ws.isZoning());
}

// Documented behavior (not fixed here): fireZoneChangedEvent() populates the
// event's x/y/z/heading from the *pending zone* fields, not from any
// position passed to setZone()/setZoneName(). Calling setZone() without a
// prior setPendingZone() publishes a ZoneChanged event with position
// (0,0,0) / heading 0, even if the zone actually has a different entry
// point. Pinning current behavior per the plan's STOP condition (real
// production code is presumably not exercising this path in a way that
// matters, since eq.cpp likely calls setPendingZone first -- but the two
// setters are not coupled at this layer).
TEST(WorldStateTest, ZoneChangedEventPositionComesFromPendingZoneNotSetZoneArgs) {
    WorldState ws;
    EventBus bus;
    ws.setEventBus(&bus);

    ZoneChangedData lastData{};
    bus.subscribe(GameEventType::ZoneChanged, [&](const GameEvent& e) {
        lastData = std::get<ZoneChangedData>(e.data);
    });

    // setZone() takes no position args at all -- the event's position comes
    // from whatever the pending-zone fields currently hold (0 here, since
    // none was set).
    ws.setZone("qeynos", 1);
    EXPECT_EQ(lastData.zoneName, "qeynos");
    EXPECT_FLOAT_EQ(lastData.x, 0.0f);
    EXPECT_FLOAT_EQ(lastData.y, 0.0f);
    EXPECT_FLOAT_EQ(lastData.z, 0.0f);

    // Now set a pending zone position and change zone again: the event
    // picks up the pending-zone coordinates, not anything passed to setZone.
    ws.setPendingZone(2, 111.0f, 222.0f, 333.0f, 45.0f);
    ws.setZone("freeport", 2);
    EXPECT_FLOAT_EQ(lastData.x, 111.0f);
    EXPECT_FLOAT_EQ(lastData.y, 222.0f);
    EXPECT_FLOAT_EQ(lastData.z, 333.0f);
    EXPECT_FLOAT_EQ(lastData.heading, 45.0f);
}

// =============================================================================
// CombatState -- priority 3 (small, last per plan's ordering).
// =============================================================================

TEST(CombatStateTest, DefaultConstructedHasNoTarget) {
    CombatState cs;
    EXPECT_FALSE(cs.hasTarget());
    EXPECT_EQ(cs.targetId(), 0u);
    EXPECT_EQ(cs.targetHPPercent(), 100);
    EXPECT_FALSE(cs.isAutoAttacking());
    EXPECT_FALSE(cs.hasCombatTarget());
}

TEST(CombatStateTest, SetTargetUpdatesAccessors) {
    CombatState cs;
    cs.setTarget(5, "Orc", 80, 10);
    EXPECT_TRUE(cs.hasTarget());
    EXPECT_EQ(cs.targetId(), 5u);
    EXPECT_EQ(cs.targetName(), "Orc");
    EXPECT_EQ(cs.targetHPPercent(), 80);
    EXPECT_EQ(cs.targetLevel(), 10);
}

TEST(CombatStateTest, ClearTargetResetsToDefaults) {
    CombatState cs;
    cs.setTarget(5, "Orc", 80, 10);
    cs.clearTarget();
    EXPECT_FALSE(cs.hasTarget());
    EXPECT_EQ(cs.targetName(), "");
    EXPECT_EQ(cs.targetHPPercent(), 100);
}

TEST(CombatStateTest, ClearTargetWithNoTargetIsNoOp) {
    CombatState cs;
    EXPECT_NO_THROW(cs.clearTarget());
    EXPECT_FALSE(cs.hasTarget());
}

TEST(CombatStateTest, UpdateTargetHPAndLevel) {
    CombatState cs;
    cs.setTarget(1, "Foo");
    cs.updateTargetHP(30);
    EXPECT_EQ(cs.targetHPPercent(), 30);
    cs.updateTargetLevel(5);
    EXPECT_EQ(cs.targetLevel(), 5);
}

TEST(CombatStateTest, AutoAttackTogglesOnChange) {
    CombatState cs;
    EXPECT_FALSE(cs.isAutoAttacking());
    cs.setAutoAttacking(true);
    EXPECT_TRUE(cs.isAutoAttacking());
    cs.setAutoAttacking(true); // no-op, same value
    EXPECT_TRUE(cs.isAutoAttacking());
    cs.setAutoAttacking(false);
    EXPECT_FALSE(cs.isAutoAttacking());
}

TEST(CombatStateTest, CombatMovementTargetLifecycle) {
    CombatState cs;
    EXPECT_FALSE(cs.hasCombatTarget());
    cs.setCombatTarget("Goblin");
    EXPECT_TRUE(cs.hasCombatTarget());
    EXPECT_EQ(cs.combatTarget(), "Goblin");
    cs.clearCombatTarget();
    EXPECT_FALSE(cs.hasCombatTarget());
}

TEST(CombatStateTest, ResetClearsAllState) {
    CombatState cs;
    cs.setTarget(1, "Foo", 50, 5);
    cs.setAutoAttacking(true);
    cs.setCombatTarget("Foo");
    cs.setLastSlainEntityName("Bar");

    cs.reset();

    EXPECT_FALSE(cs.hasTarget());
    EXPECT_FALSE(cs.isAutoAttacking());
    EXPECT_EQ(cs.combatTarget(), "");
    EXPECT_EQ(cs.lastSlainEntityName(), "");
}

// Real finding, NOT fixed here per the plan's STOP condition (base-layer
// bug -- documenting current behavior, not repairing it):
// CombatState::fireTargetChangedEvent() and fireCombatEvent() are stubs --
// both check m_eventBus is non-null but the bodies are empty (just
// comments: "Target changed event would go here"). So setTarget(),
// clearTarget(), and setAutoAttacking() NEVER actually publish anything on
// the EventBus, despite GameEventType::TargetChanged and
// GameEventType::CombatEvent existing in the enum and being documented
// (event_bus.h) as the mechanism for this. Any renderer/UI code that
// expects to hear about target or auto-attack changes via the EventBus
// gets nothing from CombatState today. This is the single clearest gap
// found in this unit; reported prominently per the plan's instructions.
TEST(CombatStateTest, KNOWN_GAP_SetTargetDoesNotActuallyPublishAnyEvent) {
    CombatState cs;
    EventBus bus;
    cs.setEventBus(&bus);

    int anyEventCount = 0;
    bus.subscribe([&](const GameEvent&) { ++anyEventCount; });

    cs.setTarget(1, "Orc", 100, 5);
    cs.clearTarget();
    cs.setAutoAttacking(true);
    cs.setAutoAttacking(false);

    EXPECT_EQ(anyEventCount, 0)
        << "CombatState's fireTargetChangedEvent()/fireCombatEvent() are "
           "empty stubs -- if this ever fires, someone wired up the "
           "missing publish() calls and this test (and its comment) "
           "should be updated to assert the real event instead.";
}

// =============================================================================
// TradeskillState -- priority 3 (small, last per plan's ordering).
// =============================================================================

TEST(TradeskillStateTest, DefaultConstructedNoContainerOpen) {
    TradeskillState ts;
    EXPECT_FALSE(ts.isContainerOpen());
    EXPECT_FALSE(ts.isWorldContainer());
    EXPECT_FALSE(ts.isInventoryContainer());
    EXPECT_EQ(ts.activeInventorySlot(), -1);
}

TEST(TradeskillStateTest, OpenWorldContainerFiresOpenedEvent) {
    TradeskillState ts;
    EventBus bus;
    ts.setEventBus(&bus);
    int opened = 0;
    bus.subscribe(GameEventType::TradeskillContainerOpened, [&](const GameEvent&) { ++opened; });

    ts.openWorldContainer(123, "Forge", 1, 4);
    EXPECT_TRUE(ts.isContainerOpen());
    EXPECT_TRUE(ts.isWorldContainer());
    EXPECT_FALSE(ts.isInventoryContainer());
    EXPECT_EQ(ts.activeObjectId(), 123u);
    EXPECT_EQ(opened, 1);
}

TEST(TradeskillStateTest, OpenInventoryContainerFiresOpenedEvent) {
    TradeskillState ts;
    EventBus bus;
    ts.setEventBus(&bus);
    int opened = 0;
    bus.subscribe(GameEventType::TradeskillContainerOpened, [&](const GameEvent&) { ++opened; });

    ts.openInventoryContainer(30, "Tradeskill Bag", 2, 8);
    EXPECT_TRUE(ts.isInventoryContainer());
    EXPECT_FALSE(ts.isWorldContainer());
    EXPECT_EQ(ts.activeInventorySlot(), 30);
    EXPECT_EQ(opened, 1);
}

TEST(TradeskillStateTest, OpeningSecondContainerClosesFirst) {
    TradeskillState ts;
    EventBus bus;
    ts.setEventBus(&bus);
    int opened = 0, closed = 0;
    bus.subscribe(GameEventType::TradeskillContainerOpened, [&](const GameEvent&) { ++opened; });
    bus.subscribe(GameEventType::TradeskillContainerClosed, [&](const GameEvent&) { ++closed; });

    ts.openWorldContainer(1, "Forge", 1, 4);
    ts.openInventoryContainer(2, "Bag", 2, 8);

    EXPECT_EQ(opened, 2);
    EXPECT_EQ(closed, 1); // the forge was auto-closed
    EXPECT_TRUE(ts.isInventoryContainer());
    EXPECT_FALSE(ts.isWorldContainer());
}

TEST(TradeskillStateTest, CloseContainerWhenNoneOpenIsNoOp) {
    TradeskillState ts;
    EventBus bus;
    ts.setEventBus(&bus);
    int closed = 0;
    bus.subscribe(GameEventType::TradeskillContainerClosed, [&](const GameEvent&) { ++closed; });

    ts.closeContainer();
    EXPECT_EQ(closed, 0);
}

TEST(TradeskillStateTest, ResetDoesNotFireEvents) {
    TradeskillState ts;
    EventBus bus;
    ts.setEventBus(&bus);
    int events = 0;
    bus.subscribe([&](const GameEvent&) { ++events; });

    ts.openWorldContainer(1, "Forge", 1, 4);
    events = 0; // ignore the open event
    ts.reset();
    EXPECT_EQ(events, 0);
    EXPECT_FALSE(ts.isContainerOpen());
}

// =============================================================================
// GameState -- aggregate/owner. Verifies wiring: EventBus is shared across
// all sub-state objects, and reset/clear behavior does what it documents.
// =============================================================================

TEST(GameStateTest, SubStatesShareTheSameEventBus) {
    GameState gs;
    int totalEvents = 0;
    gs.events().subscribe([&](const GameEvent&) { ++totalEvents; });

    gs.player().setPosition(1, 2, 3);
    EXPECT_EQ(totalEvents, 1);

    gs.entities().addEntity(makeEntity(1));
    EXPECT_EQ(totalEvents, 2);

    gs.pet().setPet(1, "Fido", 1);
    EXPECT_EQ(totalEvents, 3);
}

TEST(GameStateTest, ResetForZoneChangeClearsEntitiesAndZoneTransitionState) {
    GameState gs;
    gs.entities().addEntity(makeEntity(1));
    gs.world().setZoneConnected(true);
    gs.player().setMovementTarget(1, 2, 3);
    gs.player().setVendor(5, 1.0f, "Merchant");

    gs.resetForZoneChange();

    EXPECT_EQ(gs.entities().getEntityCount(), 0u);
    EXPECT_FALSE(gs.world().isZoneConnected());
    EXPECT_FALSE(gs.player().hasMovementTarget());
    EXPECT_FALSE(gs.player().isWithVendor());
}

TEST(GameStateTest, ClearAllResetsPlayerStateToDefaultsAndClearsListeners) {
    GameState gs;
    int events = 0;
    gs.events().subscribe([&](const GameEvent&) { ++events; });

    gs.player().setLevel(50);
    gs.player().setPosition(10, 20, 30);
    EXPECT_EQ(gs.player().level(), 50);

    gs.clearAll();

    // Player state reset to a fresh PlayerState (level defaults back to 1).
    EXPECT_EQ(gs.player().level(), 1);
    EXPECT_FLOAT_EQ(gs.player().x(), 0.0f);

    // clearAll() clears all listeners -- the subscription made above (and
    // any GameState-internal wiring for door callbacks) must no longer see
    // events published after clearAll() runs.
    events = 0;
    gs.player().setPosition(1, 1, 1);
    EXPECT_EQ(events, 0) << "clearAll() must clear all EventBus listeners";
}

TEST(GameStateTest, IsFullyZonedInDelegatesToWorldState) {
    GameState gs;
    EXPECT_FALSE(gs.isFullyZonedIn());
    gs.world().setZoneConnected(true);
    gs.world().setClientReady(true);
    EXPECT_TRUE(gs.isFullyZonedIn());
}

TEST(GameStateTest, PlayerPositionAndCurrentZoneNameConvenienceAccessors) {
    GameState gs;
    gs.player().setPosition(5, 6, 7);
    EXPECT_FLOAT_EQ(gs.playerPosition().x, 5.0f);
    EXPECT_FLOAT_EQ(gs.playerPosition().y, 6.0f);
    EXPECT_FLOAT_EQ(gs.playerPosition().z, 7.0f);

    gs.world().setZone("qeynos", 1);
    EXPECT_EQ(gs.currentZoneName(), "qeynos");
}
