// T221 (Option A stage 2): every domain the READ-model projection
// (GameStateSyncBridge's projection switch, deleted in T227) used to be the ONLY writer of is now
// written at the SOURCE -- EverQuest::m_game_state -- by the handler that
// owns the data. These tests drive the real handler paths (packet replay via
// the ReplayZonePacket seam, CombatManager/SpellManager public entry points)
// and assert on eq.GetGameState().<domain>() directly.
//
// Every case runs twice: once with a bridge attached and once with NO bridge
// at all. The no-bridge leg proves each source write sits BEFORE the
// `if (!m_bridge) return;` guard it was placed next to -- the stage-3
// contract is that the source is correct whether or not anything is
// listening. The bridge-attached leg proves nothing regressed for the event
// stream. T227 (stage 3) deleted the projection and Application's copy of
// GameState, folded the last three projection-only domains (vendor/bank/
// trainer windows) to the source here, and left test_game_state_sync_bridge.cpp
// covering only the forwarding hop -- this file is now THE state net.
//
// Same headless EverQuest idiom as test_combat_manager.cpp / test_spell_
// manager.cpp: no network, no graphics, no eqemu. Private access (T17
// `#define private public`) is used for the SpellManager cooldown entry
// (startGemCooldown is private) and for EverQuest's OnPet*() hooks, which are
// private but ARE the production choke points the plan names. Both headers
// are #pragma once, so this first inclusion decides access for the TU.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#include "client/spell/spell_manager.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/inventory/inventory_constants.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/item_instance.h"
#include "client/willeq_database.h"
#include "zone_stream_test_utils.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>

using namespace eqt;
using namespace zonetest;

namespace {

#ifndef WILLEQ_DB_PATH
#define WILLEQ_DB_PATH "data/willeq.db"
#endif

std::string getWilleqDbPath() {
    if (const char* env_path = std::getenv("WILLEQ_DB_PATH")) {
        return env_path;
    }
    return WILLEQ_DB_PATH;
}

// test_spell_manager.cpp precedent: the spell DB is a process-wide global,
// registered once in main() before any EverQuest is constructed.
EQT::WilleqDatabase g_willeq_db;
bool g_db_available = false;
bool hasDb() { return g_db_available; }

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

constexpr uint16_t kSelfId = 460;
constexpr uint16_t kRatId = 100;
constexpr uint16_t kPetId = 200;

// MobHealth: spawn_id (2) + hp_percent (1) after the opcode.
std::vector<uint8_t> MobHealthPayload(uint16_t spawn_id, uint8_t hp) {
    return BuildPayload(5, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt8(4, hp);
    });
}

// ClientUpdate: spawn_id (2) + eqemu's 20-byte bit-packed position after the
// opcode (eq_entities.cpp ZoneProcessClientUpdate). Fields 1-4 from the shared
// EncodePos helper (same packing the NewSpawn fixtures use); field5 (delta_z)
// zero.
std::vector<uint8_t> ClientUpdatePayload(uint16_t spawn_id, int32_t server_x, int32_t server_y,
                                         int32_t z, uint32_t raw_heading, uint32_t animation) {
    PosFields f = EncodePos(server_x, server_y, z, raw_heading, animation);
    return BuildPayload(24, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt32(4, f.field1);
        p.PutUInt32(8, f.field2);
        p.PutUInt32(12, f.field3);
        p.PutUInt32(16, f.field4);
        p.PutUInt32(20, 0);
    });
}

// WearChange: spawn_id (2) + material (2) + color (4) + wear_slot (1).
std::vector<uint8_t> WearChangePayload(uint16_t spawn_id, uint16_t material, uint32_t color,
                                       uint8_t slot) {
    return BuildPayload(11, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt16(4, material);
        p.PutUInt32(6, color);
        p.PutUInt8(10, slot);
    });
}

// Illusion: opcode + the 168-byte Illusion_Struct.
std::vector<uint8_t> IllusionPayload(uint32_t spawn_id, int32_t race, uint8_t gender,
                                     uint8_t texture, uint8_t helm, uint32_t face) {
    return BuildPayload(2 + sizeof(EQT::Illusion_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, spawn_id);
        p.PutUInt32(2 + 68, static_cast<uint32_t>(race));
        p.PutUInt8(2 + 72, gender);
        p.PutUInt8(2 + 73, texture);
        p.PutUInt8(2 + 74, helm);
        p.PutUInt32(2 + 76, face);
    });
}

// BeginCast: spawn_id (2) + spell_id (2) + cast_time (4).
std::vector<uint8_t> BeginCastPayload(uint16_t spawn_id, uint16_t spell_id, uint32_t cast_ms) {
    return BuildPayload(10, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawn_id);
        p.PutUInt16(4, spell_id);
        p.PutUInt32(6, cast_ms);
    });
}

// MoveItem_Struct: from_slot (4) + to_slot (4) + number_in_stack (4).
std::vector<uint8_t> MoveItemPayload(int32_t from, int32_t to) {
    return BuildPayload(14, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, static_cast<uint32_t>(from));
        p.PutUInt32(6, static_cast<uint32_t>(to));
        p.PutUInt32(10, 1);
    });
}

// DeleteItem_Struct: from_slot (4) + ...; the handler reads only the slot.
std::vector<uint8_t> DeleteItemPayload(int32_t slot) {
    return BuildPayload(14, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, static_cast<uint32_t>(slot));
        p.PutUInt32(6, 0xFFFFFFFFu);
        p.PutUInt32(10, 1);
    });
}

// NewSpawn for a pet owned by `owner`: is_pet at struct offset 329,
// pet_owner_id at 189 (eq_entities.cpp ZoneProcessNewSpawn).
std::vector<uint8_t> PetSpawnPayload(uint32_t spawn_id, const std::string& name, uint8_t level,
                                     uint32_t owner) {
    return BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, spawn_id, name, level, 54, 1, 0, 1, 100,
                          EncodePos(0, 10, 0, 0));
        p.PutUInt8(2 + 329, 1);
        p.PutUInt32(2 + 189, owner);
    });
}

void SeedItem(EverQuest& eq, int16_t slot, uint32_t itemId) {
    eqt::inventory::ItemInstance item;
    item.itemId = itemId;
    item.name = "Test Item";
    item.slots = 0xFFFFFFFFu;
    eq.GetInventoryManager()->setItem(slot, item);
}

class SourceWrites : public ::testing::TestWithParam<bool> {
protected:
    EverQuest eq{"127.0.0.1", 0, "test", "test", "test", "Testchar"};
    RecordingBridge bridge;

    void SetUp() override {
        if (GetParam()) {
            eq.setBridge(&bridge);
        }
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(kSelfId);
    }

    bool withBridge() const { return GetParam(); }

    void spawnRat() {
        ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
            NewSpawnPayloadAt(kRatId, "a_test_rat", 0, 10, 0, /*level=*/5)));
        ASSERT_NE(eq.GetGameState().entities().getEntity(kRatId), nullptr);
    }
};

// ---------------------------------------------------------------------
// Pet
// ---------------------------------------------------------------------

TEST_P(SourceWrites, Pet_CreatedViaNewSpawnDetection_WritesSourcePetState) {
    ASSERT_FALSE(eq.GetGameState().pet().hasPet());

    ReplayZone(eq, HC_OP_NewSpawn, PetSpawnPayload(kPetId, "Gobaner", 12, kSelfId));

    ASSERT_EQ(eq.GetPetSpawnId(), kPetId) << "fixture: pet detection did not fire";
    const auto& pet = eq.GetGameState().pet();
    EXPECT_TRUE(pet.hasPet());
    EXPECT_EQ(pet.petSpawnId(), kPetId);
    EXPECT_EQ(pet.petName(), "Gobaner");
    EXPECT_EQ(pet.petLevel(), 12);
}

TEST_P(SourceWrites, Pet_DespawnClearsSourcePetState_EvenHeadless) {
    ReplayZone(eq, HC_OP_NewSpawn, PetSpawnPayload(kPetId, "Gobaner", 12, kSelfId));
    ASSERT_TRUE(eq.GetGameState().pet().hasPet());

    // DeleteSpawn for the pet -> ZoneProcessDeleteSpawn's pet branch ->
    // OnPetRemoved() (T221 dropped its EQT_HAS_GRAPHICS gate).
    ReplayZone(eq, HC_OP_DeleteSpawn, BuildPayload(4, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, kPetId);
    }));

    EXPECT_EQ(eq.GetPetSpawnId(), 0);
    EXPECT_FALSE(eq.GetGameState().pet().hasPet());
}

TEST_P(SourceWrites, Pet_ButtonStateChanged_WritesSourceButton) {
    ReplayZone(eq, HC_OP_NewSpawn, PetSpawnPayload(kPetId, "Gobaner", 12, kSelfId));
    const auto& pet = eq.GetGameState().pet();
    ASSERT_FALSE(pet.getButtonState(static_cast<uint8_t>(EQT::PET_BUTTON_TAUNT)));

    eq.OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, true);
    EXPECT_TRUE(pet.getButtonState(static_cast<uint8_t>(EQT::PET_BUTTON_TAUNT)));

    eq.OnPetButtonStateChanged(EQT::PET_BUTTON_TAUNT, false);
    EXPECT_FALSE(pet.getButtonState(static_cast<uint8_t>(EQT::PET_BUTTON_TAUNT)));
}

TEST_P(SourceWrites, Pet_MobHealthForPet_WritesSourcePetHP) {
    ReplayZone(eq, HC_OP_NewSpawn, PetSpawnPayload(kPetId, "Gobaner", 12, kSelfId));
    ASSERT_EQ(eq.GetPetSpawnId(), kPetId);
    ASSERT_EQ(eq.GetGameState().pet().petHPPercent(), 100);

    ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(kPetId, 37));

    EXPECT_EQ(eq.GetGameState().pet().petHPPercent(), 37);
    // T40's honest derivation: no mana data for pets -> 0.
    EXPECT_EQ(eq.GetGameState().pet().petManaPercent(), 0);
}

TEST_P(SourceWrites, Pet_MobHealthForNonPet_DoesNotTouchPetHP) {
    ReplayZone(eq, HC_OP_NewSpawn, PetSpawnPayload(kPetId, "Gobaner", 12, kSelfId));
    spawnRat();

    ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(kRatId, 37));

    EXPECT_EQ(eq.GetGameState().pet().petHPPercent(), 100);
}

TEST_P(SourceWrites, Pet_SeamSetPetSpawnId_ThenMobHealth_WritesSourcePetHP) {
    // The plan's named seam (Test_SetPetSpawnIdForSeam): the MobHealth pet
    // branch keys on m_pet_spawn_id alone, so it must work without the
    // NewSpawn detection path having run.
    spawnRat();
    eq.Test_SetPetSpawnIdForSeam(kRatId);

    ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(kRatId, 61));

    EXPECT_EQ(eq.GetGameState().pet().petHPPercent(), 61);
}

// ---------------------------------------------------------------------
// Spells
// ---------------------------------------------------------------------

TEST_P(SourceWrites, Spells_BeginCastFromGem_WritesSourceCastingAndGem) {
    if (!hasDb()) GTEST_SKIP() << "willeq.db not found, skipping";
    ASSERT_TRUE(eq.InitializeSpellSystem(""));
    auto* mgr = eq.GetSpellManager();
    ASSERT_NE(mgr, nullptr);

    // Any self-target spell with a real cast time; mana is set high enough
    // for any of them.
    uint32_t id = 0;
    for (uint32_t cand = 1; cand <= 4000 && id == 0; ++cand) {
        const EQ::SpellData* s = mgr->getSpell(cand);
        if (s && s->target_type == EQ::SpellTargetType::Self && s->cast_time_ms > 0 &&
            s->mana_cost >= 0 && !s->requiresReagents()) {
            id = cand;
        }
    }
    ASSERT_NE(id, 0u) << "no self-target spell found in the DB";
    eq.GetGameState().player().setMana(50000, 50000);

    uint32_t gems[1] = {id};
    mgr->setSpellGems(gems, 1);
    ASSERT_EQ(mgr->getGemState(0), EQ::GemState::Ready);

    ASSERT_EQ(mgr->beginCastFromGem(0), EQ::CastResult::Success);

    const auto& spells = eq.GetGameState().spells();
    EXPECT_TRUE(spells.isCasting());
    EXPECT_EQ(spells.castingSpellId(), id);
    EXPECT_EQ(spells.castTimeTotal(), mgr->getSpell(id)->cast_time_ms);
    EXPECT_EQ(spells.getGemSpellId(0), id);
    EXPECT_FALSE(spells.isGemReady(0)) << "gem must be Casting, not Ready";

    // Player-initiated interrupt: casting clears, gem back to Ready.
    mgr->interruptCast();
    EXPECT_FALSE(spells.isCasting());
    EXPECT_TRUE(spells.isGemReady(0));
}

TEST_P(SourceWrites, Spells_InterruptCast_ClearsSourceCasting_NoDb) {
    auto* mgr = eq.GetSpellManager();
    ASSERT_NE(mgr, nullptr);
    // Stage a cast in progress from gem 2 (no DB needed: the interrupt path
    // only reads m_is_casting / m_current_gem_slot).
    mgr->m_spell_gems[2] = 777;
    mgr->m_gem_states[2] = EQ::GemState::Casting;
    mgr->m_is_casting = true;
    mgr->m_current_spell_id = 777;
    mgr->m_current_gem_slot = 2;
    mgr->m_current_target_id = kSelfId;
    // Source mirrors the staged state so the transition is observable.
    eq.GetGameState().spells().setCasting(true, 777, kSelfId, 1000);
    eq.GetGameState().spells().setGem(2, 777, eqt::state::SpellGemState::Casting);

    mgr->interruptCast();

    const auto& spells = eq.GetGameState().spells();
    EXPECT_FALSE(spells.isCasting());
    EXPECT_EQ(spells.getGemSpellId(2), 777u);
    EXPECT_TRUE(spells.isGemReady(2));
}

TEST_P(SourceWrites, Spells_GemCooldown_WritesSourceRefreshWithTotal) {
    auto* mgr = eq.GetSpellManager();
    ASSERT_NE(mgr, nullptr);
    uint32_t gems[1] = {777};
    mgr->setSpellGems(gems, 1);
    // setSpellGems() does not publish (test_spell_manager.cpp:244) -- so the
    // source gem is still Empty here; the cooldown transition is what writes.
    ASSERT_FALSE(eq.GetGameState().spells().hasSpellMemorized(0));

    mgr->startGemCooldown(0, 5000);

    const auto& spells = eq.GetGameState().spells();
    EXPECT_EQ(spells.getGemSpellId(0), 777u);
    EXPECT_TRUE(spells.hasSpellMemorized(0));
    EXPECT_FALSE(spells.isGemReady(0));
    const uint32_t remaining = spells.getGemCooldownRemaining(0);
    EXPECT_GT(remaining, 0u);
    EXPECT_LE(remaining, 5000u);
    // The projection would have received cooldownTotalMs=5000 too; the
    // progress getter needs the total, so a total of 0 would read as done.
    EXPECT_LT(spells.getGemCooldownProgress(0), 1.0f);
}

TEST_P(SourceWrites, Spells_ServerBeginCastEchoForSelf_WritesSourceCasting) {
    ASSERT_FALSE(eq.GetGameState().spells().isCasting());

    ReplayZone(eq, HC_OP_BeginCast, BeginCastPayload(kSelfId, 1234, 3000));

    const auto& spells = eq.GetGameState().spells();
    EXPECT_TRUE(spells.isCasting());
    EXPECT_EQ(spells.castingSpellId(), 1234u);
    EXPECT_EQ(spells.castTimeTotal(), 3000u);
}

TEST_P(SourceWrites, Spells_ServerBeginCastForOtherCaster_DoesNotWriteSource) {
    spawnRat();
    ReplayZone(eq, HC_OP_BeginCast, BeginCastPayload(kRatId, 1234, 3000));
    EXPECT_FALSE(eq.GetGameState().spells().isCasting());
}

// ---------------------------------------------------------------------
// Entity position
// ---------------------------------------------------------------------

TEST_P(SourceWrites, Move_ClientUpdateForOtherEntity_WritesSourcePosition) {
    spawnRat();
    const auto* before = eq.GetGameState().entities().getEntity(kRatId);
    ASSERT_NE(before, nullptr);
    ASSERT_FLOAT_EQ(before->x, 1.25f);  // INVERSEXY: entity.x <- server_y (raw 10 / 8)

    // server_x=80 (raw, /8 -> 10), server_y=160 (-> 20), z=24 (-> 3),
    // raw heading 512 (-> 90 deg), animation 5.
    ReplayZone(eq, HC_OP_ClientUpdate, ClientUpdatePayload(kRatId, 80, 160, 24, 512, 5));

    const auto* e = eq.GetGameState().entities().getEntity(kRatId);
    ASSERT_NE(e, nullptr);
    EXPECT_FLOAT_EQ(e->x, 20.0f);
    EXPECT_FLOAT_EQ(e->y, 10.0f);
    EXPECT_FLOAT_EQ(e->z, 3.0f);
    EXPECT_FLOAT_EQ(e->heading, 90.0f);
    EXPECT_EQ(e->animation, 5);
    // Source and legacy map agree.
    const auto& legacy = eq.GetEntities().at(kRatId);
    EXPECT_FLOAT_EQ(legacy.x, e->x);
    EXPECT_FLOAT_EQ(legacy.heading, e->heading);
}

TEST_P(SourceWrites, Move_ClientUpdateForUnknownEntity_IsNoOp) {
    ReplayZone(eq, HC_OP_ClientUpdate, ClientUpdatePayload(999, 80, 160, 24, 512, 0));
    EXPECT_EQ(eq.GetGameState().entities().getEntity(999), nullptr);
}

// T237 (T234 finding): EverQuest::SetMovementMode()/SetPositionState() are
// the ONLY writers of m_movement_mode/m_position_state (action handler,
// CombatManager sit/stand, camp). Before T237 they wrote the private member
// and the bridge event but never m_game_state.player(), so the source view
// was permanently Run/Standing. The write must precede the `if (m_bridge)`
// publish -- the NoBridge leg is the proof.
TEST_P(SourceWrites, Move_SetMovementMode_WritesSourcePlayerMovementMode) {
    ASSERT_EQ(eq.GetGameState().player().movementMode(), eqt::state::MovementMode::Run);

    eq.SetMovementMode(MOVE_MODE_WALK);
    EXPECT_EQ(eq.GetGameState().player().movementMode(), eqt::state::MovementMode::Walk);

    eq.SetMovementMode(MOVE_MODE_SNEAK);
    EXPECT_EQ(eq.GetGameState().player().movementMode(), eqt::state::MovementMode::Sneak);

    eq.SetMovementMode(MOVE_MODE_RUN);
    EXPECT_EQ(eq.GetGameState().player().movementMode(), eqt::state::MovementMode::Run);
}

TEST_P(SourceWrites, Move_SetPositionState_WritesSourcePlayerPositionState) {
    ASSERT_EQ(eq.GetGameState().player().positionState(), eqt::state::PositionState::Standing);

    const struct { PositionState in; eqt::state::PositionState want; } cases[] = {
        {POS_SITTING, eqt::state::PositionState::Sitting},
        {POS_CROUCHING, eqt::state::PositionState::Crouching},
        {POS_FEIGN_DEATH, eqt::state::PositionState::FeignDeath},
        {POS_DEAD, eqt::state::PositionState::Dead},
        {POS_STANDING, eqt::state::PositionState::Standing},
    };
    for (const auto& c : cases) {
        eq.SetPositionState(c.in);
        EXPECT_EQ(eq.GetGameState().player().positionState(), c.want)
            << "input " << static_cast<int>(c.in);
    }
}

// ---------------------------------------------------------------------
// Entity appearance
// ---------------------------------------------------------------------

TEST_P(SourceWrites, Appearance_WearChange_WritesSourceEquipment) {
    spawnRat();
    ASSERT_EQ(eq.GetGameState().entities().getEntity(kRatId)->equipment[1], 0u);

    ReplayZone(eq, HC_OP_WearChange, WearChangePayload(kRatId, /*material=*/17,
                                                       /*color=*/0x00FF8800u, /*slot=*/1));

    const auto* e = eq.GetGameState().entities().getEntity(kRatId);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->equipment[1], 17u);
    EXPECT_EQ(e->equipmentTint[1], 0x00FF8800u);
    EXPECT_EQ(e->equipment[0], 0u);
}

TEST_P(SourceWrites, Appearance_Illusion_WritesSourceRaceGenderTextureHelm) {
    spawnRat();
    const auto* before = eq.GetGameState().entities().getEntity(kRatId);
    ASSERT_EQ(before->raceId, 54u);

    ReplayZone(eq, HC_OP_Illusion, IllusionPayload(kRatId, /*race=*/1, /*gender=*/1,
                                                   /*texture=*/3, /*helm=*/2, /*face=*/7));

    const auto* e = eq.GetGameState().entities().getEntity(kRatId);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->raceId, 1u);
    EXPECT_EQ(e->gender, 1);
    EXPECT_EQ(e->equipChest2, 3);
    EXPECT_EQ(e->helm, 2);
    EXPECT_EQ(e->face, 7);
}

TEST_P(SourceWrites, Appearance_UpdatePlayerAppearanceFromInventory_MergesInventoryDerivedEquipment) {
    // Self entity present in both maps.
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(kSelfId, "Testchar", 0, 10, 0, 5, /*npc_type=*/0)));
    ASSERT_NE(eq.GetGameState().entities().getEntity(kSelfId), nullptr);

    eqt::inventory::ItemInstance chest;
    chest.itemId = 5001;
    chest.name = "Test Tunic";
    chest.material = 9;
    chest.color = 0x112233u;
    chest.slots = 0xFFFFFFFFu;
    eq.GetInventoryManager()->setItem(eqt::inventory::SLOT_CHEST, chest);

    eq.Entities().UpdatePlayerAppearanceFromInventory();

    const auto* e = eq.GetGameState().entities().getEntity(kSelfId);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->equipment[1], 9u) << "chest material must come from INVENTORY, not m_entities";
    EXPECT_EQ(e->equipmentTint[1], 0x112233u);
    // The legacy entity map is deliberately NOT written by this path (the
    // plan's reason for merging the payload rather than the entity).
    EXPECT_EQ(eq.GetEntities().at(kSelfId).equipment[1], 0u);
}

// ---------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------

TEST_P(SourceWrites, Combat_SetTarget_WritesSourceTarget) {
    spawnRat();
    auto* cm = eq.GetCombatManager();
    ASSERT_FALSE(eq.GetGameState().combat().hasTarget());

    ASSERT_TRUE(cm->SetTarget(kRatId));

    const auto& combat = eq.GetGameState().combat();
    EXPECT_TRUE(combat.hasTarget());
    EXPECT_EQ(combat.targetId(), kRatId);
    EXPECT_EQ(combat.targetName(), "a_test_rat");
    EXPECT_EQ(combat.targetLevel(), 5);
    EXPECT_EQ(combat.targetHPPercent(), 100);
}

TEST_P(SourceWrites, Combat_MobHealthOnTarget_WritesSourceTargetHP) {
    spawnRat();
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(kRatId));

    ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(kRatId, 42));

    const auto& combat = eq.GetGameState().combat();
    EXPECT_EQ(combat.targetId(), kRatId);
    EXPECT_EQ(combat.targetHPPercent(), 42);
}

TEST_P(SourceWrites, Combat_MobHealthOnNonTarget_DoesNotTouchSourceTarget) {
    spawnRat();
    ASSERT_NO_THROW(ReplayZone(eq, HC_OP_NewSpawn,
        NewSpawnPayloadAt(101, "a_second_rat", 0, 12, 0)));
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(kRatId));

    ReplayZone(eq, HC_OP_MobHealth, MobHealthPayload(101, 42));

    EXPECT_EQ(eq.GetGameState().combat().targetHPPercent(), 100);
}

TEST_P(SourceWrites, Combat_ClearTarget_ClearsSourceTarget) {
    spawnRat();
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(kRatId));
    ASSERT_TRUE(eq.GetGameState().combat().hasTarget());

    cm->ClearTarget();

    EXPECT_FALSE(eq.GetGameState().combat().hasTarget());
    EXPECT_EQ(eq.GetGameState().combat().targetId(), 0);
}

TEST_P(SourceWrites, Combat_AutoAttackToggle_WritesSourceFlag) {
    spawnRat();
    auto* cm = eq.GetCombatManager();
    ASSERT_TRUE(cm->SetTarget(kRatId));
    ASSERT_FALSE(eq.GetGameState().combat().isAutoAttacking());

    cm->EnableAutoAttack();
    EXPECT_TRUE(eq.GetGameState().combat().isAutoAttacking());

    cm->DisableAutoAttack();
    EXPECT_FALSE(eq.GetGameState().combat().isAutoAttacking());
}

// ---------------------------------------------------------------------
// Inventory occupancy
// ---------------------------------------------------------------------

TEST_P(SourceWrites, Inventory_MoveItem_WritesSourceOccupancyForBothSlots) {
    // Seed: general slot 22 (index 0), equipment slot 5, bank slot 2000
    // (index 0). A normal move is client-side (InventoryManager ignores the
    // server confirm), so the publish re-reads whatever the slot holds.
    SeedItem(eq, eqt::inventory::GENERAL_BEGIN, 1001);
    SeedItem(eq, 5, 1002);
    SeedItem(eq, eqt::inventory::BANK_BEGIN, 1003);
    const auto& inv = eq.GetGameState().inventory();
    ASSERT_FALSE(inv.hasGeneralItem(0));
    ASSERT_FALSE(inv.hasEquipment(5));
    ASSERT_FALSE(inv.hasBankItem(0));

    ReplayZone(eq, HC_OP_MoveItem, MoveItemPayload(eqt::inventory::GENERAL_BEGIN, 5));
    EXPECT_TRUE(inv.hasGeneralItem(0));
    EXPECT_TRUE(inv.hasEquipment(5));
    EXPECT_FALSE(inv.hasGeneralItem(1));

    ReplayZone(eq, HC_OP_MoveItem, MoveItemPayload(eqt::inventory::BANK_BEGIN,
                                                   eqt::inventory::GENERAL_BEGIN + 1));
    EXPECT_TRUE(inv.hasBankItem(0));
    EXPECT_FALSE(inv.hasGeneralItem(1)) << "slot 23 is empty; hasItem=false must be written";
}

TEST_P(SourceWrites, Inventory_MoveItemToMinusOne_ClearsSourceOccupancy) {
    SeedItem(eq, 5, 1002);
    ReplayZone(eq, HC_OP_MoveItem, MoveItemPayload(5, 5));
    const auto& inv = eq.GetGameState().inventory();
    ASSERT_TRUE(inv.hasEquipment(5));

    // to_slot == -1: InventoryManager removes the item, then the publish
    // re-reads the now-empty slot.
    ReplayZone(eq, HC_OP_MoveItem, MoveItemPayload(5, -1));
    EXPECT_FALSE(inv.hasEquipment(5));
}

TEST_P(SourceWrites, Inventory_DeleteItem_ClearsSourceOccupancy) {
    SeedItem(eq, eqt::inventory::GENERAL_BEGIN + 3, 1004);
    ReplayZone(eq, HC_OP_MoveItem, MoveItemPayload(eqt::inventory::GENERAL_BEGIN + 3,
                                                   eqt::inventory::GENERAL_BEGIN + 3));
    const auto& inv = eq.GetGameState().inventory();
    ASSERT_TRUE(inv.hasGeneralItem(3));

    ReplayZone(eq, HC_OP_DeleteItem, DeleteItemPayload(eqt::inventory::GENERAL_BEGIN + 3));
    EXPECT_FALSE(inv.hasGeneralItem(3));
}

// ---------------------------------------------------------------------
// Vendor / Bank / Trainer windows (T227 FOLD: the three domains T221's
// census found with NO source write at all -- only the projection ever
// wrote player().vendor/banker/trainer, so with the projection deleted
// CommandProcessor's /bank, /vendor, /train gates would read a permanently
// empty state).
// ---------------------------------------------------------------------

// ShopRequest (server ack): npc_id (4) + player_id (4) + action (4) +
// sell_rate as float bits (4) after the opcode -- eqemu Merchant_Click_Struct.
std::vector<uint8_t> ShopRequestPayload(uint32_t npc_id, uint32_t action, float sell_rate) {
    uint32_t bits = 0;
    std::memcpy(&bits, &sell_rate, sizeof(bits));
    return BuildPayload(18, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, npc_id);
        p.PutUInt32(6, kSelfId);
        p.PutUInt32(10, action);
        p.PutUInt32(14, bits);
    });
}

// GMTraining (server ack): the 448-byte GMTrainee_Struct; one trainable skill
// so the handler's skill loop has something to build.
std::vector<uint8_t> GMTrainingPayload(uint32_t npc_id) {
    return BuildPayload(2 + sizeof(EQT::GMTrainee_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, npc_id);
        p.PutUInt32(2 + 4, kSelfId);
        p.PutUInt32(2 + 8 + 4 * 1, 50);  // skill 1 trainable to 50
    });
}

TEST_P(SourceWrites, Npc_ShopRequestAck_WritesSourceVendor) {
    spawnRat();
    ASSERT_FALSE(eq.GetGameState().player().isWithVendor());

    ReplayZone(eq, HC_OP_ShopRequest, ShopRequestPayload(kRatId, 1, 1.25f));

    const auto& player = eq.GetGameState().player();
    EXPECT_TRUE(player.isWithVendor());
    EXPECT_EQ(player.vendorNpcId(), kRatId);
    EXPECT_FLOAT_EQ(player.vendorSellRate(), 1.25f);
    EXPECT_FALSE(player.vendorName().empty());
}

TEST_P(SourceWrites, Npc_ShopRequestAckFailure_DoesNotWriteSourceVendor) {
    spawnRat();
    ReplayZone(eq, HC_OP_ShopRequest, ShopRequestPayload(kRatId, 0, 1.0f));
    EXPECT_FALSE(eq.GetGameState().player().isWithVendor());
}

TEST_P(SourceWrites, Npc_ShopEndConfirm_ClearsSourceVendor) {
    spawnRat();
    ReplayZone(eq, HC_OP_ShopRequest, ShopRequestPayload(kRatId, 1, 1.0f));
    ASSERT_TRUE(eq.GetGameState().player().isWithVendor());

    ReplayZone(eq, HC_OP_ShopEndConfirm, BuildPayload(10, nullptr));

    EXPECT_FALSE(eq.GetGameState().player().isWithVendor());
    EXPECT_EQ(eq.GetGameState().player().vendorNpcId(), 0);
}

TEST_P(SourceWrites, Npc_CloseVendorWindow_ClearsSourceVendor) {
    spawnRat();
    ReplayZone(eq, HC_OP_ShopRequest, ShopRequestPayload(kRatId, 1, 1.0f));
    ASSERT_TRUE(eq.GetGameState().player().isWithVendor());

    eq.CloseVendorWindow();

    EXPECT_FALSE(eq.GetGameState().player().isWithVendor());
}

TEST_P(SourceWrites, Npc_OpenBankWindow_WritesSourceBanker) {
    ASSERT_FALSE(eq.GetGameState().player().isAtBank());

    eq.OpenBankWindow(kRatId);

    EXPECT_TRUE(eq.GetGameState().player().isAtBank());
    EXPECT_EQ(eq.GetGameState().player().bankerNpcId(), kRatId);
}

TEST_P(SourceWrites, Npc_CloseBankWindow_ClearsSourceBanker) {
    eq.OpenBankWindow(kRatId);
    ASSERT_TRUE(eq.GetGameState().player().isAtBank());

    eq.CloseBankWindow();

    EXPECT_FALSE(eq.GetGameState().player().isAtBank());
    EXPECT_EQ(eq.GetGameState().player().bankerNpcId(), 0);
}

TEST_P(SourceWrites, Npc_GMTrainingAck_WritesSourceTrainer) {
    spawnRat();
    ASSERT_FALSE(eq.GetGameState().player().isWithTrainer());

    ReplayZone(eq, HC_OP_GMTraining, GMTrainingPayload(kRatId));

    const auto& player = eq.GetGameState().player();
    EXPECT_TRUE(player.isWithTrainer());
    EXPECT_EQ(player.trainerNpcId(), kRatId);
    EXPECT_FALSE(player.trainerName().empty());
}

TEST_P(SourceWrites, Npc_CloseTrainerWindow_ClearsSourceTrainer) {
    spawnRat();
    ReplayZone(eq, HC_OP_GMTraining, GMTrainingPayload(kRatId));
    ASSERT_TRUE(eq.GetGameState().player().isWithTrainer());

    eq.CloseTrainerWindow();

    EXPECT_FALSE(eq.GetGameState().player().isWithTrainer());
    EXPECT_EQ(eq.GetGameState().player().trainerNpcId(), 0);
}

INSTANTIATE_TEST_SUITE_P(WithAndWithoutBridge, SourceWrites, ::testing::Values(true, false),
                         [](const ::testing::TestParamInfo<bool>& info) {
                             return info.param ? "WithBridge" : "NoBridge";
                         });

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    std::string db_path = getWilleqDbPath();
    std::ifstream probe(db_path);
    if (probe.good()) {
        probe.close();
        if (g_willeq_db.open(db_path)) {
            EQT::WilleqDatabase::setGlobalInstance(&g_willeq_db);
            g_db_available = true;
        }
    }

    return RUN_ALL_TESTS();
}
