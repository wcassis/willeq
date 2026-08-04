// Unit tests for the eqt::state state modules, part 1: EntityManager,
// PlayerState, SpellState, InventoryState (the priority-1 modules per the
// plan -- largest and highest-risk). GroupState, PetState, WorldState,
// CombatState, TradeskillState, and GameState are covered in
// test_state_modules_secondary.cpp (split purely to stay under the
// project's file-size check, scripts/refactor/check_file_sizes.sh).
//
// Territory: tests/ only (T02, refactor/T02_state_event_bus.md). No source
// changes were made to produce this file. Tests assert behavior through
// public accessors only (charter rule) -- no reaching into internals.

#include "client/state/entity_manager.h"
#include "client/state/player_state.h"
#include "client/state/spell_state.h"
#include "client/state/inventory_state.h"
#include "client/state/event_bus.h"

#include <gtest/gtest.h>

using namespace eqt::state;

// =============================================================================
// EntityManager -- largest module, priority 1.
// =============================================================================

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

TEST(EntityManagerTest, DefaultConstructedIsEmpty) {
    EntityManager mgr;
    EXPECT_EQ(mgr.getEntityCount(), 0u);
    EXPECT_TRUE(mgr.getAllEntities().empty());
}

TEST(EntityManagerTest, AddEntitySucceedsAndFiresSpawnedEvent) {
    EntityManager mgr;
    EventBus bus;
    mgr.setEventBus(&bus);

    int spawnedCount = 0;
    uint16_t spawnedId = 0;
    bus.subscribe(GameEventType::EntitySpawned, [&](const GameEvent& e) {
        ++spawnedCount;
        spawnedId = std::get<EntitySpawnedData>(e.data).spawnId;
    });

    EXPECT_TRUE(mgr.addEntity(makeEntity(100, "Guard")));
    EXPECT_EQ(mgr.getEntityCount(), 1u);
    EXPECT_EQ(spawnedCount, 1);
    EXPECT_EQ(spawnedId, 100);
}

TEST(EntityManagerTest, AddEntityDuplicateSpawnIdFails) {
    EntityManager mgr;
    EXPECT_TRUE(mgr.addEntity(makeEntity(1)));
    EXPECT_FALSE(mgr.addEntity(makeEntity(1)));
    EXPECT_EQ(mgr.getEntityCount(), 1u);
}

TEST(EntityManagerTest, RemoveEntitySucceedsAndFiresDespawnedEvent) {
    EntityManager mgr;
    EventBus bus;
    mgr.setEventBus(&bus);
    mgr.addEntity(makeEntity(5, "Rat"));

    int despawnCount = 0;
    std::string despawnedName;
    bus.subscribe(GameEventType::EntityDespawned, [&](const GameEvent& e) {
        ++despawnCount;
        despawnedName = std::get<EntityDespawnedData>(e.data).name;
    });

    EXPECT_TRUE(mgr.removeEntity(5));
    EXPECT_EQ(mgr.getEntityCount(), 0u);
    EXPECT_EQ(despawnCount, 1);
    EXPECT_EQ(despawnedName, "Rat");
}

TEST(EntityManagerTest, RemoveEntityNotFoundReturnsFalse) {
    EntityManager mgr;
    EXPECT_FALSE(mgr.removeEntity(999));
}

TEST(EntityManagerTest, RemoveThenAddSameSpawnIdWorks) {
    EntityManager mgr;
    EXPECT_TRUE(mgr.addEntity(makeEntity(7, "First")));
    EXPECT_TRUE(mgr.removeEntity(7));
    EXPECT_TRUE(mgr.addEntity(makeEntity(7, "Second")));
    ASSERT_NE(mgr.getEntity(7), nullptr);
    EXPECT_EQ(mgr.getEntity(7)->name, "Second");
}

TEST(EntityManagerTest, DoubleRemoveSecondCallReturnsFalse) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(3));
    EXPECT_TRUE(mgr.removeEntity(3));
    EXPECT_FALSE(mgr.removeEntity(3));
}

TEST(EntityManagerTest, UpdateEntityPositionUpdatesAndFiresMovedEvent) {
    EntityManager mgr;
    EventBus bus;
    mgr.setEventBus(&bus);
    mgr.addEntity(makeEntity(10));

    int movedCount = 0;
    bus.subscribe(GameEventType::EntityMoved, [&](const GameEvent&) { ++movedCount; });

    EXPECT_TRUE(mgr.updateEntityPosition(10, 1, 2, 3, 45.0f, 0.1f, 0.2f, 0.3f, 7));
    EXPECT_EQ(movedCount, 1);

    const Entity* e = mgr.getEntity(10);
    ASSERT_NE(e, nullptr);
    EXPECT_FLOAT_EQ(e->x, 1);
    EXPECT_FLOAT_EQ(e->y, 2);
    EXPECT_FLOAT_EQ(e->z, 3);
    EXPECT_FLOAT_EQ(e->heading, 45.0f);
    EXPECT_EQ(e->animation, 7);
}

TEST(EntityManagerTest, UpdateEntityPositionNotFoundReturnsFalse) {
    EntityManager mgr;
    EXPECT_FALSE(mgr.updateEntityPosition(404, 0, 0, 0, 0));
}

TEST(EntityManagerTest, UpdateEntityHPFiresOnlyWhenChanged) {
    EntityManager mgr;
    EventBus bus;
    mgr.setEventBus(&bus);
    mgr.addEntity(makeEntity(20));

    int statsChangedCount = 0;
    bus.subscribe(GameEventType::EntityStatsChanged, [&](const GameEvent&) { ++statsChangedCount; });

    // Default hpPercent is 100; setting to 100 again should not fire.
    EXPECT_TRUE(mgr.updateEntityHP(20, 100));
    EXPECT_EQ(statsChangedCount, 0);

    EXPECT_TRUE(mgr.updateEntityHP(20, 50));
    EXPECT_EQ(statsChangedCount, 1);

    // Same value again -- no additional event.
    EXPECT_TRUE(mgr.updateEntityHP(20, 50));
    EXPECT_EQ(statsChangedCount, 1);
}

TEST(EntityManagerTest, UpdateEntityHPNotFoundReturnsFalse) {
    EntityManager mgr;
    EXPECT_FALSE(mgr.updateEntityHP(404, 50));
}

TEST(EntityManagerTest, UpdateEntityEquipmentBoundsChecked) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1));
    EXPECT_FALSE(mgr.updateEntityEquipment(1, -1, 5, 0));
    EXPECT_FALSE(mgr.updateEntityEquipment(1, 9, 5, 0));
    EXPECT_TRUE(mgr.updateEntityEquipment(1, 0, 5, 0));
    EXPECT_TRUE(mgr.updateEntityEquipment(1, 8, 5, 0)); // max valid index
}

TEST(EntityManagerTest, UpdateEntityEquipmentNotFoundReturnsFalse) {
    EntityManager mgr;
    EXPECT_FALSE(mgr.updateEntityEquipment(404, 0, 5, 0));
}

TEST(EntityManagerTest, MarkAsCorpseSetsFlag) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1));
    mgr.markAsCorpse(1);
    EXPECT_TRUE(mgr.getEntity(1)->isCorpse);
}

TEST(EntityManagerTest, MarkAsCorpseNotFoundIsNoOp) {
    EntityManager mgr;
    EXPECT_NO_THROW(mgr.markAsCorpse(404));
}

TEST(EntityManagerTest, ClearFiresDespawnForAllAndEmptiesMap) {
    EntityManager mgr;
    EventBus bus;
    mgr.setEventBus(&bus);
    mgr.addEntity(makeEntity(1));
    mgr.addEntity(makeEntity(2));

    int despawnCount = 0;
    bus.subscribe(GameEventType::EntityDespawned, [&](const GameEvent&) { ++despawnCount; });

    mgr.clear();
    EXPECT_EQ(despawnCount, 2);
    EXPECT_EQ(mgr.getEntityCount(), 0u);
}

TEST(EntityManagerTest, GetEntityAbsentReturnsNullptr) {
    EntityManager mgr;
    EXPECT_EQ(mgr.getEntity(1), nullptr);
    EXPECT_EQ(mgr.getEntityMutable(1), nullptr);
}

TEST(EntityManagerTest, HasEntityReflectsPresence) {
    EntityManager mgr;
    EXPECT_FALSE(mgr.hasEntity(1));
    mgr.addEntity(makeEntity(1));
    EXPECT_TRUE(mgr.hasEntity(1));
}

TEST(EntityManagerTest, FindEntityByNamePartialCaseInsensitiveMatch) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1, "Sir Lucan"));

    EXPECT_NE(mgr.findEntityByName("lucan"), nullptr);
    EXPECT_NE(mgr.findEntityByName("SIR"), nullptr);
    EXPECT_EQ(mgr.findEntityByName("nobody"), nullptr);
    EXPECT_EQ(mgr.findEntityByName(""), nullptr);
}

TEST(EntityManagerTest, FindEntityByExactNameRequiresFullMatch) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1, "Sir Lucan"));

    EXPECT_NE(mgr.findEntityByExactName("Sir Lucan"), nullptr);
    EXPECT_EQ(mgr.findEntityByExactName("sir lucan"), nullptr); // case-sensitive
    EXPECT_EQ(mgr.findEntityByExactName("Sir"), nullptr);       // partial no match
}

TEST(EntityManagerTest, GetEntitiesInRangeFiltersByDistance) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1, "Near", 0, 0, 0));
    mgr.addEntity(makeEntity(2, "Far", 100, 0, 0));

    auto inRange = mgr.getEntitiesInRange(0, 0, 0, 10.0f);
    ASSERT_EQ(inRange.size(), 1u);
    EXPECT_EQ(inRange[0], 1u);
}

TEST(EntityManagerTest, GetEntitiesInRangeEmptyWhenNoneMatch) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1, "Far", 1000, 0, 0));
    EXPECT_TRUE(mgr.getEntitiesInRange(0, 0, 0, 1.0f).empty());
}

TEST(EntityManagerTest, GetNearestEntityWithFilter) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1, "Close", 1, 0, 0));
    mgr.addEntity(makeEntity(2, "Closer", 0.5f, 0, 0));

    const Entity* nearest = mgr.getNearestEntity(0, 0, 0);
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->spawnId, 2u);
}

TEST(EntityManagerTest, GetNearestEntityEmptyManagerReturnsNullptr) {
    EntityManager mgr;
    EXPECT_EQ(mgr.getNearestEntity(0, 0, 0), nullptr);
}

TEST(EntityManagerTest, GetNearestNPCExcludesPlayersAndCorpses) {
    EntityManager mgr;
    Entity player = makeEntity(1, "Player", 1, 0, 0);
    player.npcType = 0;
    mgr.addEntity(player);

    Entity npc = makeEntity(2, "Orc", 2, 0, 0);
    npc.npcType = 1;
    mgr.addEntity(npc);

    Entity corpse = makeEntity(3, "DeadOrc", 0.5f, 0, 0);
    corpse.npcType = 3; // npc corpse
    mgr.addEntity(corpse);

    const Entity* nearestNPC = mgr.getNearestNPC(0, 0, 0);
    ASSERT_NE(nearestNPC, nullptr);
    EXPECT_EQ(nearestNPC->spawnId, 2u);
}

TEST(EntityManagerTest, GetNearestPlayerExcludesSelfAndCorpses) {
    EntityManager mgr;
    Entity self = makeEntity(1, "Self", 0, 0, 0);
    self.npcType = 0;
    mgr.addEntity(self);

    Entity otherPlayer = makeEntity(2, "Other", 5, 0, 0);
    otherPlayer.npcType = 0;
    mgr.addEntity(otherPlayer);

    const Entity* nearest = mgr.getNearestPlayer(0, 0, 0, /*excludeSpawnId=*/1);
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->spawnId, 2u);
}

TEST(EntityManagerTest, GetNearestCorpseFindsOnlyCorpses) {
    EntityManager mgr;
    Entity npc = makeEntity(1, "Alive", 1, 0, 0);
    npc.npcType = 1;
    mgr.addEntity(npc);

    Entity corpse = makeEntity(2, "Dead", 2, 0, 0);
    corpse.npcType = 2; // pc_corpse
    mgr.addEntity(corpse);

    const Entity* nearest = mgr.getNearestCorpse(0, 0, 0);
    ASSERT_NE(nearest, nullptr);
    EXPECT_EQ(nearest->spawnId, 2u);
}

TEST(EntityManagerTest, PetQueriesTrackOwnerRelationship) {
    EntityManager mgr;
    Entity pet = makeEntity(50, "Pet");
    pet.isPet = 1;
    pet.petOwnerId = 1;
    mgr.addEntity(pet);
    mgr.addEntity(makeEntity(1, "Owner"));

    EXPECT_TRUE(mgr.isPet(50));
    EXPECT_FALSE(mgr.isPet(1));
    EXPECT_EQ(mgr.getPetOwnerId(50), 1u);
    EXPECT_EQ(mgr.getPetOwnerId(1), 0u); // not a pet -> 0
    EXPECT_EQ(mgr.findPetByOwner(1), 50u);
    EXPECT_EQ(mgr.findPetByOwner(999), 0u); // no pet for that owner

    auto pets = mgr.getAllPets();
    ASSERT_EQ(pets.size(), 1u);
    EXPECT_EQ(pets[0], 50u);
}

TEST(EntityManagerTest, IsPetAbsentEntityReturnsFalse) {
    EntityManager mgr;
    EXPECT_FALSE(mgr.isPet(404));
    EXPECT_EQ(mgr.getPetOwnerId(404), 0u);
}

TEST(EntityManagerTest, ForEachEntityVisitsAll) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1));
    mgr.addEntity(makeEntity(2));

    int visited = 0;
    mgr.forEachEntity([&](const Entity&) { ++visited; });
    EXPECT_EQ(visited, 2);
}

TEST(EntityManagerTest, ForEachEntityMutableAllowsModification) {
    EntityManager mgr;
    mgr.addEntity(makeEntity(1));
    mgr.forEachEntityMutable([](Entity& e) { e.hpPercent = 42; });
    EXPECT_EQ(mgr.getEntity(1)->hpPercent, 42);
}

// =============================================================================
// PlayerState -- priority 1.
// =============================================================================

TEST(PlayerStateTest, DefaultConstructedHasSaneValues) {
    PlayerState ps;
    EXPECT_FLOAT_EQ(ps.x(), 0.0f);
    EXPECT_FLOAT_EQ(ps.y(), 0.0f);
    EXPECT_FLOAT_EQ(ps.z(), 0.0f);
    EXPECT_FLOAT_EQ(ps.size(), 6.0f);
    EXPECT_EQ(ps.level(), 1);
    EXPECT_EQ(ps.movementMode(), MovementMode::Run);
    EXPECT_EQ(ps.positionState(), PositionState::Standing);
    EXPECT_FALSE(ps.isMoving());
    EXPECT_FALSE(ps.hasMovementTarget());
    EXPECT_FALSE(ps.isFollowing());
    EXPECT_FALSE(ps.isWithVendor());
    EXPECT_FALSE(ps.isAtBank());
    EXPECT_FALSE(ps.isWithTrainer());
    EXPECT_FALSE(ps.isLooting());
}

TEST(PlayerStateTest, SetPositionUpdatesAndFiresMovedEvent) {
    PlayerState ps;
    EventBus bus;
    ps.setEventBus(&bus);

    int movedCount = 0;
    bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++movedCount; });

    ps.setPosition(10, 20, 30);
    EXPECT_FLOAT_EQ(ps.x(), 10);
    EXPECT_FLOAT_EQ(ps.y(), 20);
    EXPECT_FLOAT_EQ(ps.z(), 30);
    EXPECT_EQ(movedCount, 1);

    ps.setPositionAndHeading(1, 2, 3, 90.0f);
    EXPECT_FLOAT_EQ(ps.heading(), 90.0f);
    EXPECT_EQ(movedCount, 2);
}

TEST(PlayerStateTest, SetHPFiresStatsChangedOnlyWhenChanged) {
    PlayerState ps;
    EventBus bus;
    ps.setEventBus(&bus);

    int statsCount = 0;
    bus.subscribe(GameEventType::PlayerStatsChanged, [&](const GameEvent&) { ++statsCount; });

    ps.setHP(100, 100);
    EXPECT_EQ(statsCount, 1);
    ps.setHP(100, 100); // unchanged
    EXPECT_EQ(statsCount, 1);
    ps.setCurHP(50);
    EXPECT_EQ(statsCount, 2);
    EXPECT_EQ(ps.curHP(), 50u);
}

TEST(PlayerStateTest, SetLevelFiresOnlyOnChange) {
    PlayerState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int statsCount = 0;
    bus.subscribe(GameEventType::PlayerStatsChanged, [&](const GameEvent&) { ++statsCount; });

    ps.setLevel(1); // same as default -- no change
    EXPECT_EQ(statsCount, 0);
    ps.setLevel(10);
    EXPECT_EQ(statsCount, 1);
    EXPECT_EQ(ps.level(), 10);
}

TEST(PlayerStateTest, MovementTargetLifecycle) {
    PlayerState ps;
    EXPECT_FALSE(ps.hasMovementTarget());
    ps.setMovementTarget(1, 2, 3);
    EXPECT_TRUE(ps.hasMovementTarget());
    EXPECT_FLOAT_EQ(ps.targetX(), 1);
    ps.clearMovementTarget();
    EXPECT_FALSE(ps.hasMovementTarget());
    EXPECT_FLOAT_EQ(ps.targetX(), 0.0f);
}

TEST(PlayerStateTest, FollowTargetLifecycle) {
    PlayerState ps;
    EXPECT_FALSE(ps.isFollowing());
    ps.setFollowTarget("Bob");
    EXPECT_TRUE(ps.isFollowing());
    EXPECT_EQ(ps.followTarget(), "Bob");
    ps.clearFollowTarget();
    EXPECT_FALSE(ps.isFollowing());
}

TEST(PlayerStateTest, CurrencyRoundTripAndTotalValue) {
    PlayerState ps;
    ps.setCurrency(1, 2, 3, 4);
    EXPECT_EQ(ps.platinum(), 1u);
    EXPECT_EQ(ps.gold(), 2u);
    EXPECT_EQ(ps.silver(), 3u);
    EXPECT_EQ(ps.copper(), 4u);
    // 1*1000 + 2*100 + 3*10 + 4 = 1234
    EXPECT_EQ(ps.totalCopperValue(), 1234u);
}

TEST(PlayerStateTest, BankCurrencyRoundTripAndTotalValue) {
    PlayerState ps;
    ps.setBankCurrency(5, 0, 0, 0);
    EXPECT_EQ(ps.totalBankCopperValue(), 5000u);
}

TEST(PlayerStateTest, DecrementPracticePointsFloorsAtZero) {
    PlayerState ps;
    ps.setPracticePoints(1);
    ps.decrementPracticePoints();
    EXPECT_EQ(ps.practicePoints(), 0u);
    ps.decrementPracticePoints(); // already zero -- must not underflow
    EXPECT_EQ(ps.practicePoints(), 0u);
}

TEST(PlayerStateTest, VendorInteractionFiresWindowEvents) {
    PlayerState ps;
    EventBus bus;
    ps.setEventBus(&bus);

    int opened = 0, closed = 0;
    bus.subscribe(GameEventType::VendorWindowOpened, [&](const GameEvent&) { ++opened; });
    bus.subscribe(GameEventType::VendorWindowClosed, [&](const GameEvent&) { ++closed; });

    EXPECT_FALSE(ps.isWithVendor());
    ps.setVendor(5, 1.05f, "Merchant");
    EXPECT_TRUE(ps.isWithVendor());
    EXPECT_EQ(opened, 1);

    ps.clearVendor();
    EXPECT_FALSE(ps.isWithVendor());
    EXPECT_EQ(closed, 1);
}

TEST(PlayerStateTest, ClearVendorWithoutSetDoesNotFireCloseEvent) {
    PlayerState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int closed = 0;
    bus.subscribe(GameEventType::VendorWindowClosed, [&](const GameEvent&) { ++closed; });

    ps.clearVendor(); // never opened
    EXPECT_EQ(closed, 0);
}

TEST(PlayerStateTest, BankAndTrainerAndLootStateLifecycles) {
    PlayerState ps;
    EXPECT_FALSE(ps.isAtBank());
    ps.setBanker(7);
    EXPECT_TRUE(ps.isAtBank());
    ps.clearBanker();
    EXPECT_FALSE(ps.isAtBank());

    EXPECT_FALSE(ps.isWithTrainer());
    ps.setTrainer(8, "Trainer Bob");
    EXPECT_TRUE(ps.isWithTrainer());
    ps.clearTrainer();
    EXPECT_FALSE(ps.isWithTrainer());

    EXPECT_FALSE(ps.isLooting());
    ps.setLootingCorpse(99);
    EXPECT_TRUE(ps.isLooting());
    EXPECT_EQ(ps.lootingCorpseId(), 99u);
    ps.clearLootingCorpse();
    EXPECT_FALSE(ps.isLooting());
}

TEST(PlayerStateTest, LoadFromProfileBulkSetsAndFiresEvents) {
    PlayerState ps;
    EventBus bus;
    ps.setEventBus(&bus);
    int moved = 0, stats = 0;
    bus.subscribe(GameEventType::PlayerMoved, [&](const GameEvent&) { ++moved; });
    bus.subscribe(GameEventType::PlayerStatsChanged, [&](const GameEvent&) { ++stats; });

    PlayerState::ProfileData profile{};
    profile.name = "Hero";
    profile.level = 50;
    profile.curHP = 1000;
    profile.maxHP = 1000;
    profile.x = 100;
    profile.y = 200;
    profile.z = 300;
    profile.heading = 45;

    ps.loadFromProfile(profile);

    EXPECT_EQ(ps.name(), "Hero");
    EXPECT_EQ(ps.level(), 50);
    EXPECT_EQ(ps.curHP(), 1000u);
    EXPECT_FLOAT_EQ(ps.x(), 100);
    EXPECT_EQ(moved, 1);
    EXPECT_EQ(stats, 1);
}

// =============================================================================
// SpellState -- priority 1.
// =============================================================================

TEST(SpellStateTest, DefaultConstructedGemsAreEmpty) {
    SpellState ss;
    for (uint8_t i = 0; i < SPELL_GEM_COUNT; ++i) {
        EXPECT_EQ(ss.getGemSpellId(i), SPELL_ID_UNKNOWN);
        EXPECT_EQ(ss.getGemState(i), SpellGemState::Empty);
        EXPECT_FALSE(ss.hasSpellMemorized(i));
        EXPECT_FALSE(ss.isGemReady(i));
    }
    EXPECT_FALSE(ss.isCasting());
    EXPECT_FALSE(ss.isMemorizing());
    EXPECT_EQ(ss.scribedSpellCount(), 0u);
}

TEST(SpellStateTest, GemAccessorsOutOfRangeReturnSafeDefaults) {
    SpellState ss;
    EXPECT_EQ(ss.getGemSpellId(SPELL_GEM_COUNT), SPELL_ID_UNKNOWN);
    EXPECT_EQ(ss.getGemState(SPELL_GEM_COUNT), SpellGemState::Empty);
    EXPECT_FLOAT_EQ(ss.getGemCooldownProgress(255), 1.0f);
    EXPECT_EQ(ss.getGemCooldownRemaining(255), 0u);
    EXPECT_FALSE(ss.hasSpellMemorized(255));
    EXPECT_FALSE(ss.isGemReady(255));
}

TEST(SpellStateTest, SetGemFiresEventOnlyWhenChanged) {
    SpellState ss;
    EventBus bus;
    ss.setEventBus(&bus);
    int gemChanged = 0;
    bus.subscribe(GameEventType::SpellGemChanged, [&](const GameEvent&) { ++gemChanged; });

    ss.setGem(0, 1001, SpellGemState::Ready);
    EXPECT_EQ(gemChanged, 1);
    EXPECT_EQ(ss.getGemSpellId(0), 1001u);
    EXPECT_TRUE(ss.isGemReady(0));

    // Setting identical state again should not fire.
    ss.setGem(0, 1001, SpellGemState::Ready);
    EXPECT_EQ(gemChanged, 1);

    ss.setGem(0, 1001, SpellGemState::Casting);
    EXPECT_EQ(gemChanged, 2);
}

TEST(SpellStateTest, SetGemOutOfRangeIsNoOp) {
    SpellState ss;
    EXPECT_NO_THROW(ss.setGem(SPELL_GEM_COUNT, 1, SpellGemState::Ready));
}

TEST(SpellStateTest, ClearGemResetsToEmpty) {
    SpellState ss;
    ss.setGem(3, 500, SpellGemState::Ready);
    ss.clearGem(3);
    EXPECT_EQ(ss.getGemSpellId(3), SPELL_ID_UNKNOWN);
    EXPECT_EQ(ss.getGemState(3), SpellGemState::Empty);
}

TEST(SpellStateTest, GemCooldownProgressComputesFraction) {
    SpellState ss;
    ss.setGem(0, 1, SpellGemState::Refresh);
    ss.setGemCooldown(0, /*remainingMs=*/2500, /*totalMs=*/5000);
    EXPECT_FLOAT_EQ(ss.getGemCooldownProgress(0), 0.5f);
    EXPECT_EQ(ss.getGemCooldownRemaining(0), 2500u);
}

TEST(SpellStateTest, GemCooldownZeroTotalReturnsFullProgress) {
    SpellState ss;
    // Never set a cooldown -- totalMs defaults to 0.
    EXPECT_FLOAT_EQ(ss.getGemCooldownProgress(0), 1.0f);
}

TEST(SpellStateTest, SetGemCooldownTransitionsStateByRemaining) {
    SpellState ss;
    ss.setGem(0, 42, SpellGemState::Ready);
    ss.setGemCooldown(0, 100, 1000);
    EXPECT_EQ(ss.getGemState(0), SpellGemState::Refresh);

    ss.setGemCooldown(0, 0, 0);
    EXPECT_EQ(ss.getGemState(0), SpellGemState::Ready); // spell still memorized
}

TEST(SpellStateTest, CastingStateLifecycleFiresOnChange) {
    SpellState ss;
    EventBus bus;
    ss.setEventBus(&bus);
    int castingChanged = 0;
    bus.subscribe(GameEventType::CastingStateChanged, [&](const GameEvent&) { ++castingChanged; });

    EXPECT_FALSE(ss.isCasting());
    ss.setCasting(true, 100, 5, 3000);
    EXPECT_TRUE(ss.isCasting());
    EXPECT_EQ(ss.castingSpellId(), 100u);
    EXPECT_EQ(castingChanged, 1);

    ss.updateCastProgress(1500);
    EXPECT_FLOAT_EQ(ss.castProgress(), 0.5f);

    ss.clearCasting();
    EXPECT_FALSE(ss.isCasting());
    EXPECT_EQ(castingChanged, 2);
}

TEST(SpellStateTest, CastProgressZeroWhenNotCasting) {
    SpellState ss;
    EXPECT_FLOAT_EQ(ss.castProgress(), 0.0f);
}

TEST(SpellStateTest, MemorizingLifecycleAlsoUpdatesGemState) {
    SpellState ss;
    EventBus bus;
    ss.setEventBus(&bus);
    int memorizingEvents = 0;
    bus.subscribe(GameEventType::SpellMemorizing, [&](const GameEvent&) { ++memorizingEvents; });

    ss.setMemorizing(true, 2, 777, 4000);
    EXPECT_TRUE(ss.isMemorizing());
    EXPECT_EQ(ss.memorizingGemSlot(), 2);
    EXPECT_EQ(ss.getGemState(2), SpellGemState::MemorizeProgress);
    EXPECT_EQ(memorizingEvents, 1);

    ss.updateMemorizeProgress(2000);
    EXPECT_FLOAT_EQ(ss.memorizeProgress(), 0.5f);
}

TEST(SpellStateTest, MemorizeProgressZeroWhenNotMemorizing) {
    SpellState ss;
    EXPECT_FLOAT_EQ(ss.memorizeProgress(), 0.0f);
}

TEST(SpellStateTest, ScribedSpellTrackingAddFindClear) {
    SpellState ss;
    EXPECT_FALSE(ss.hasSpellScribed(123));
    ss.addScribedSpell(123);
    EXPECT_TRUE(ss.hasSpellScribed(123));

    // Adding the same spell again must not duplicate (no crash / still found once).
    ss.addScribedSpell(123);
    EXPECT_TRUE(ss.hasSpellScribed(123));

    ss.clearScribedSpells();
    EXPECT_FALSE(ss.hasSpellScribed(123));
}

TEST(SpellStateTest, ScribedSpellUnknownIdNeverTracked) {
    SpellState ss;
    ss.addScribedSpell(SPELL_ID_UNKNOWN);
    EXPECT_FALSE(ss.hasSpellScribed(SPELL_ID_UNKNOWN));
}

TEST(SpellStateTest, ScribedSpellTrackingCapAtMaxTrackedSpells) {
    SpellState ss;
    // MAX_TRACKED_SPELLS is 64 (private, but behavior is observable):
    // adding 100 distinct spells should not crash, and the earliest ones
    // added (within capacity) remain found.
    for (uint32_t id = 1; id <= 100; ++id) {
        ss.addScribedSpell(id);
    }
    EXPECT_TRUE(ss.hasSpellScribed(1));
    EXPECT_TRUE(ss.hasSpellScribed(64));
    // Spells beyond capacity are silently dropped (documented behavior:
    // "Limited to tracking a subset of spells").
    EXPECT_FALSE(ss.hasSpellScribed(65));
}

TEST(SpellStateTest, ResetClearsGemsCastingMemorizingAndScribed) {
    SpellState ss;
    ss.setGem(0, 1, SpellGemState::Ready);
    ss.setCasting(true, 1, 1, 1000);
    ss.setMemorizing(true, 1, 2, 1000);
    ss.addScribedSpell(5);

    ss.reset();

    EXPECT_EQ(ss.getGemSpellId(0), SPELL_ID_UNKNOWN);
    EXPECT_FALSE(ss.isCasting());
    EXPECT_FALSE(ss.isMemorizing());
    EXPECT_FALSE(ss.hasSpellScribed(5));
}

// =============================================================================
// InventoryState -- priority 1.
// =============================================================================

TEST(InventoryStateTest, DefaultConstructedAllSlotsEmpty) {
    InventoryState inv;
    for (int16_t i = 0; i < INV_EQUIPMENT_COUNT; ++i) {
        EXPECT_FALSE(inv.hasEquipment(i));
    }
    for (int16_t i = 0; i < INV_GENERAL_COUNT; ++i) {
        EXPECT_FALSE(inv.hasGeneralItem(i));
    }
    for (int16_t i = 0; i < INV_BANK_COUNT; ++i) {
        EXPECT_FALSE(inv.hasBankItem(i));
    }
    EXPECT_FALSE(inv.hasCursorItem());
    EXPECT_EQ(inv.equippedCount(), 0u);
    EXPECT_EQ(inv.generalItemCount(), 0u);
    EXPECT_EQ(inv.bankItemCount(), 0u);
    EXPECT_EQ(inv.freeGeneralSlots(), INV_GENERAL_COUNT);
}

TEST(InventoryStateTest, SlotAccessorsOutOfRangeReturnFalse) {
    InventoryState inv;
    EXPECT_FALSE(inv.hasEquipment(-1));
    EXPECT_FALSE(inv.hasEquipment(INV_EQUIPMENT_COUNT));
    EXPECT_FALSE(inv.hasGeneralItem(-1));
    EXPECT_FALSE(inv.hasGeneralItem(INV_GENERAL_COUNT));
    EXPECT_FALSE(inv.hasBankItem(-1));
    EXPECT_FALSE(inv.hasBankItem(INV_BANK_COUNT));
}

TEST(InventoryStateTest, SetEquipmentOccupiedFiresOnlyOnChange) {
    InventoryState inv;
    EventBus bus;
    inv.setEventBus(&bus);
    int changed = 0;
    bus.subscribe(GameEventType::InventorySlotChanged, [&](const GameEvent&) { ++changed; });

    inv.setEquipmentOccupied(0, true);
    EXPECT_TRUE(inv.hasEquipment(0));
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(inv.equippedCount(), 1u);

    inv.setEquipmentOccupied(0, true); // no change
    EXPECT_EQ(changed, 1);

    inv.setEquipmentOccupied(0, false);
    EXPECT_EQ(changed, 2);
    EXPECT_EQ(inv.equippedCount(), 0u);
}

TEST(InventoryStateTest, SetEquipmentOccupiedOutOfRangeIsNoOp) {
    InventoryState inv;
    EXPECT_NO_THROW(inv.setEquipmentOccupied(-1, true));
    EXPECT_NO_THROW(inv.setEquipmentOccupied(INV_EQUIPMENT_COUNT, true));
}

TEST(InventoryStateTest, SetGeneralOccupiedUpdatesFreeCount) {
    InventoryState inv;
    inv.setGeneralOccupied(0, true);
    EXPECT_TRUE(inv.hasGeneralItem(0));
    EXPECT_EQ(inv.generalItemCount(), 1u);
    EXPECT_EQ(inv.freeGeneralSlots(), INV_GENERAL_COUNT - 1);
}

TEST(InventoryStateTest, CursorQueueSizeDrivesHasCursorItem) {
    InventoryState inv;
    EventBus bus;
    inv.setEventBus(&bus);
    int cursorChanged = 0;
    bus.subscribe(GameEventType::CursorItemChanged, [&](const GameEvent&) { ++cursorChanged; });

    EXPECT_FALSE(inv.hasCursorItem());
    inv.setCursorQueueSize(2);
    EXPECT_TRUE(inv.hasCursorItem());
    EXPECT_EQ(inv.cursorQueueSize(), 2);
    EXPECT_EQ(cursorChanged, 1);

    inv.setCursorQueueSize(0);
    EXPECT_FALSE(inv.hasCursorItem());
    EXPECT_EQ(cursorChanged, 2);
}

TEST(InventoryStateTest, BagInfoTracksSizeAndIsBag) {
    InventoryState inv;
    EXPECT_FALSE(inv.isBag(0));
    EXPECT_EQ(inv.bagSize(0), 0);
    inv.setBagInfo(0, 8);
    EXPECT_TRUE(inv.isBag(0));
    EXPECT_EQ(inv.bagSize(0), 8);
}

TEST(InventoryStateTest, FreeBagSlotsSumsAllBagSizes) {
    InventoryState inv;
    inv.setBagInfo(0, 8);
    inv.setBagInfo(1, 10);
    EXPECT_EQ(inv.freeBagSlots(), 18);
}

TEST(InventoryStateTest, SetEquipmentStatsFiresOnlyOnChange) {
    InventoryState inv;
    EventBus bus;
    inv.setEventBus(&bus);
    int statsChanged = 0;
    bus.subscribe(GameEventType::EquipmentStatsChanged, [&](const GameEvent&) { ++statsChanged; });

    inv.setEquipmentStats(10, 5, 100, 50, 25.5f);
    EXPECT_EQ(statsChanged, 1);
    EXPECT_EQ(inv.equipmentAC(), 10);
    EXPECT_FLOAT_EQ(inv.totalWeight(), 25.5f);

    inv.setEquipmentStats(10, 5, 100, 50, 25.5f); // unchanged
    EXPECT_EQ(statsChanged, 1);
}

TEST(InventoryStateTest, ResetClearsAllSlotsAndStats) {
    InventoryState inv;
    inv.setEquipmentOccupied(0, true);
    inv.setGeneralOccupied(0, true);
    inv.setBankOccupied(0, true);
    inv.setCursorQueueSize(1);
    inv.setEquipmentStats(1, 1, 1, 1, 1.0f);

    inv.reset();

    EXPECT_FALSE(inv.hasEquipment(0));
    EXPECT_FALSE(inv.hasGeneralItem(0));
    EXPECT_FALSE(inv.hasBankItem(0));
    EXPECT_FALSE(inv.hasCursorItem());
    EXPECT_EQ(inv.equipmentAC(), 0);
}

TEST(InventoryStateTest, ClearBankOnlyAffectsBankSlots) {
    InventoryState inv;
    inv.setEquipmentOccupied(0, true);
    inv.setBankOccupied(0, true);

    inv.clearBank();

    EXPECT_TRUE(inv.hasEquipment(0)); // untouched
    EXPECT_FALSE(inv.hasBankItem(0));
}

// T28 (fixed the hardcoded lie; was pinned wrong behavior under the name
// InventorySlotChangedEventAlwaysReportsHasItemTrue): fireInventoryChangedEvent()
// used to always publish hasItem=true regardless of the actual occupied
// state passed to the setter. It now reports the real value.
// T39: setEquipmentOccupied()/setGeneralOccupied()/setBankOccupied() are now
// wired -- Application::syncInventoryState() (application.cpp) polls the
// authoritative InventoryManager every frame and mirrors slot occupancy
// through these setters. Still no production READER of the resulting
// occupancy arrays (confirmed unchanged from T28's finding) -- the live
// InventorySlotChanged events consumers care about (enriched by S2) come
// from the eq_bridge path instead -- see refactor/deferred.md "Dead
// parallel state copies in the state layer, WITH LIVE READERS". This test
// guards the payload contract now that a real caller exists.
TEST(InventoryStateTest, InventorySlotChangedEventReportsActualOccupiedState) {
    InventoryState inv;
    EventBus bus;
    inv.setEventBus(&bus);

    bool lastHasItem = false;
    bus.subscribe(GameEventType::InventorySlotChanged, [&](const GameEvent& e) {
        lastHasItem = std::get<InventorySlotChangedData>(e.data).hasItem;
    });

    inv.setEquipmentOccupied(0, true);
    EXPECT_TRUE(lastHasItem);
    inv.setEquipmentOccupied(0, false);
    EXPECT_FALSE(lastHasItem) << "T28: payload must reflect the actual "
                                  "occupied state passed to the setter.";
}

