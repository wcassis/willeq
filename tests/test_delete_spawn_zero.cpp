// B1125 (#445, B1118 M9): OP_DeleteSpawn with spawn id 0 names NO entity.
//
// eqemu's NPC::Death (zone/attack.cpp) hands the dying NPC's entity id to
// its new corpse (`entity_list.AddCorpse(corpse, GetID())`) and then calls
// `SetID(0)` on the NPC object; when that object is destroyed, Mob::~Mob
// (zone/mob.cpp) queues `CreateDespawnPacket()` with `GetID()` -- now 0.
// So every NPC death that leaves a corpse is followed ~40 ms later by
// DeleteSpawn(0), and the corpse itself (same id as the NPC) is removed by
// its own real-id DeleteSpawn when it decays. Both B1118 pcaps show it
// (review/B1125/pcap_deletespawn.tsv). The client used to read id 0 as
// "delete the current target": with the player self-targeted (B1118 run A1,
// self-targeted AE spell 458 killed a rodent) it erased the local player.
//
// The fix ignores id 0 entirely. These cases replay the packets with the
// exact bytes of review/B1118/fable_A1_selfdelete/capture.pcap where they
// come from it. Headless EverQuest + ReplayZonePacket seam, no network, no
// graphics, no eqemu.

#include <gtest/gtest.h>

#include "client/combat.h"
#include "client/eq.h"
#include "zone_stream_test_utils.h"

#include <cstdint>
#include <variant>
#include <vector>

using namespace zonetest;

namespace {

constexpr uint16_t kSelfId = 259;    // Testcaster in run A1
constexpr uint16_t kRodentId = 31;   // the rodent spell 458 killed in run A1
constexpr uint16_t kTrollId = 261;   // TrollL, a live bystander NPC

std::vector<uint8_t> DeleteSpawnPayload(uint32_t id) {
    // Titanium DeleteSpawn_Struct: uint32 spawn_id (handler reads 16 bits).
    return BuildPayload(6, [&](EQ::Net::DynamicPacket& p) { p.PutUInt32(2, id); });
}

// The A1 pcap's OP_Death at 19:25:48.897, byte for byte:
// 1f000000 03010000 1f000000 00000000 ca010000 e7000000 4c000000 00000000
std::vector<uint8_t> A1RodentDeathPayload() {
    return {0x1f, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x1f, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0x01, 0x00, 0x00, 0xe7, 0x00,
            0x00, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
}

class DeleteSpawnZero : public ::testing::Test {
protected:
    EverQuest eq = MakeEq();

    void SetUp() override {
        eq.Net().Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(kSelfId);
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kSelfId, "Testcaster", 0, 0, 0, 60, /*npc_type=*/0, 12));
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kRodentId, "a_large_rat", 5, 5, 0, 1));
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kTrollId, "TrollL", 9, 9, 0, 10));
        ASSERT_EQ(eq.GetEntities().size(), 3u);
    }

    bool Has(uint16_t id) const {
        return eq.GetEntities().count(id) == 1 &&
               eq.GetGameState().entities().getEntity(id) != nullptr;
    }

    void Target(uint16_t id) {
        ASSERT_TRUE(eq.GetCombatManager()->SetTarget(id));
        ASSERT_EQ(eq.GetCombatManager()->GetTargetId(), id);
    }
};

// The B1118 A1 defect: self-targeted, DeleteSpawn(0) erased the player.
TEST_F(DeleteSpawnZero, SelfTargeted_LocalPlayerSurvives) {
    Target(kSelfId);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(0));
    EXPECT_TRUE(Has(kSelfId)) << "DeleteSpawn(0) erased the local player";
    EXPECT_EQ(eq.GetCombatManager()->GetTargetId(), kSelfId) << "self target was dropped";
}

// The A1 packet order: Death(rodent, killer=self, spell 458), then
// DeleteSpawn(0) 36 ms later, with the player self-targeted.
TEST_F(DeleteSpawnZero, A1PcapSequence_PlayerAndCorpseSurvive) {
    Target(kSelfId);
    ReplayZone(eq, HC_OP_Death, A1RodentDeathPayload());
    ASSERT_TRUE(eq.GetEntities().at(kRodentId).is_corpse);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(0));
    EXPECT_TRUE(Has(kSelfId));
    EXPECT_TRUE(Has(kRodentId)) << "the corpse keeps the NPC id and outlives DeleteSpawn(0)";
    EXPECT_TRUE(Has(kTrollId));
}

// The same mapping erased any live target that was not the victim (an AE
// kill while another NPC is targeted).
TEST_F(DeleteSpawnZero, LiveNpcTargeted_TargetSurvives) {
    Target(kTrollId);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(0));
    EXPECT_TRUE(Has(kTrollId));
    EXPECT_EQ(eq.GetCombatManager()->GetTargetId(), kTrollId);
}

TEST_F(DeleteSpawnZero, NoTarget_IsNoOp) {
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(0));
    EXPECT_EQ(eq.GetEntities().size(), 3u);
}

// Negative: a real-id DeleteSpawn still deletes, and clears that target.
TEST_F(DeleteSpawnZero, RealId_StillDeletesAndClearsTarget) {
    Target(kTrollId);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kTrollId));
    EXPECT_FALSE(Has(kTrollId));
    EXPECT_FALSE(eq.GetCombatManager()->HasTarget());
    EXPECT_TRUE(Has(kSelfId));
}

TEST_F(DeleteSpawnZero, RealId_OtherEntityLeavesSelfTarget) {
    Target(kSelfId);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kTrollId));
    EXPECT_FALSE(Has(kTrollId));
    EXPECT_TRUE(Has(kSelfId));
    EXPECT_EQ(eq.GetCombatManager()->GetTargetId(), kSelfId);
}

// ---------------------------------------------------------------------------
// B1133 (#453): the SERVER owns corpse lifetime. A real-id DeleteSpawn of a
// corpse removes it, loot window or not (Will, 2026-10-23: "the client
// deletes what the server tells it to delete, nothing more and nothing
// less"). The removed gate honoured a corpse delete only when it landed with
// the loot window open on that corpse, so the server's ~1.03 s DeleteSpawn of
// an empty NPC corpse (17 of 17 kills in review/B1125/pcap_deletespawn.tsv)
// was dropped and the corpse stayed on screen.

std::vector<uint8_t> DeathPayload(uint32_t victim, uint32_t killer) {
    std::vector<uint8_t> d = A1RodentDeathPayload();
    for (int i = 0; i < 4; ++i) {
        d[i] = static_cast<uint8_t>(victim >> (8 * i));
        d[4 + i] = static_cast<uint8_t>(killer >> (8 * i));
        d[8 + i] = static_cast<uint8_t>(victim >> (8 * i));
    }
    return d;
}

class CorpseDeleteSpawn : public DeleteSpawnZero {
protected:
    TestBridge bridge;

    void SetUp() override {
        DeleteSpawnZero::SetUp();
        eq.setBridge(&bridge);
        ReplayZone(eq, HC_OP_Death, A1RodentDeathPayload());
        ASSERT_TRUE(eq.GetEntities().at(kRodentId).is_corpse);
        (void)bridge.drainEvents();
    }
    void TearDown() override { eq.setBridge(nullptr); }

    int LootWindowClosedEvents(uint16_t corpse) {
        int n = 0;
        for (const auto& ev : bridge.drainEvents()) {
            if (ev.type != eqt::state::GameEventType::LootWindowClosed) continue;
            const auto& d = std::get<eqt::state::LootWindowClosedData>(ev.data);
            if (d.corpseId == corpse) ++n;
        }
        return n;
    }
};

// The B1125 pcap order (Death, DeleteSpawn(0), DeleteSpawn(corpse) ~1 s
// later) with no loot window ever opened: the corpse goes.
TEST_F(CorpseDeleteSpawn, NoLootWindow_ServerDeleteRemovesCorpse) {
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(0));
    ASSERT_TRUE(Has(kRodentId));
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kRodentId));
    EXPECT_FALSE(Has(kRodentId)) << "server DeleteSpawn of a corpse was refused";
    EXPECT_TRUE(Has(kSelfId));
    EXPECT_TRUE(Has(kTrollId));
}

// The corpse is targeted (the usual state after a kill): the delete removes
// it and drops the target.
TEST_F(CorpseDeleteSpawn, TargetedCorpse_ServerDeleteRemovesAndClearsTarget) {
    Target(kRodentId);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kRodentId));
    EXPECT_FALSE(Has(kRodentId));
    EXPECT_FALSE(eq.GetCombatManager()->HasTarget());
}

// Delete while the loot window is open on that corpse: the window closes
// through LootWindowClosed, the looting state clears, the combat manager
// leaves LOOTING, and the corpse is removed.
TEST_F(CorpseDeleteSpawn, LootWindowOpen_ClosesWindowAndRemovesCorpse) {
    eq.GetCombatManager()->LootCorpse(kRodentId);
    ASSERT_EQ(eq.GetGameState().player().lootingCorpseId(), kRodentId);
    ASSERT_TRUE(eq.GetCombatManager()->IsLooting());
    eq.Bridge().LootItems()[0] = eqt::bridge::BridgeComponent::GameLootItem{1001, "Rat Whiskers"};
    eq.Bridge().PendingLootSlots().push_back(0);
    (void)bridge.drainEvents();

    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kRodentId));

    EXPECT_FALSE(Has(kRodentId));
    EXPECT_EQ(LootWindowClosedEvents(kRodentId), 1);
    EXPECT_EQ(eq.GetGameState().player().lootingCorpseId(), 0u);
    EXPECT_FALSE(eq.IsLootWindowOpen());
    EXPECT_TRUE(eq.Bridge().GetLootItems().empty());
    EXPECT_TRUE(eq.Bridge().PendingLootSlots().empty());
    EXPECT_FALSE(eq.GetCombatManager()->IsLooting());
}

// A loot window open on a DIFFERENT corpse is left alone by this delete.
TEST_F(CorpseDeleteSpawn, LootWindowOnOtherCorpse_LeftOpen) {
    ReplayZone(eq, HC_OP_Death, DeathPayload(kTrollId, kSelfId));
    ASSERT_TRUE(eq.GetEntities().at(kTrollId).is_corpse);
    eq.GetCombatManager()->LootCorpse(kTrollId);
    (void)bridge.drainEvents();

    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kRodentId));

    EXPECT_FALSE(Has(kRodentId));
    EXPECT_TRUE(Has(kTrollId));
    EXPECT_EQ(eq.GetGameState().player().lootingCorpseId(), kTrollId);
    EXPECT_TRUE(eq.GetCombatManager()->IsLooting());
    EXPECT_EQ(LootWindowClosedEvents(kRodentId), 0);
}

// Nothing less: a corpse no DeleteSpawn names stays.
TEST_F(CorpseDeleteSpawn, CorpseNotNamed_Stays) {
    ReplayZone(eq, HC_OP_Death, DeathPayload(kTrollId, kSelfId));
    ASSERT_TRUE(eq.GetEntities().at(kTrollId).is_corpse);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(0));
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kRodentId));
    EXPECT_FALSE(Has(kRodentId));
    EXPECT_TRUE(Has(kTrollId)) << "a corpse the server did not delete was removed";
    EXPECT_TRUE(eq.GetEntities().at(kTrollId).is_corpse);
}

// The user closed the window (OP_EndLootRequest) and the server deletes the
// emptied corpse: still removed, and a second, later corpse delete with no
// window works too (no state left over from the first).
TEST_F(CorpseDeleteSpawn, AfterUserClose_ThenAnotherCorpse_BothRemoved) {
    eq.GetCombatManager()->LootCorpse(kRodentId);
    eq.GetCombatManager()->CloseLootWindow();
    ASSERT_EQ(eq.GetGameState().player().lootingCorpseId(), 0u);
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kRodentId));
    EXPECT_FALSE(Has(kRodentId));

    ReplayZone(eq, HC_OP_Death, DeathPayload(kTrollId, kSelfId));
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kTrollId));
    EXPECT_FALSE(Has(kTrollId));
    EXPECT_TRUE(Has(kSelfId));
}

// Non-corpse delete unchanged: a live NPC is removed and a loot window open
// on a corpse is not touched by it.
TEST_F(CorpseDeleteSpawn, LiveNpcDelete_Unchanged) {
    eq.GetCombatManager()->LootCorpse(kRodentId);
    (void)bridge.drainEvents();
    ReplayZone(eq, HC_OP_DeleteSpawn, DeleteSpawnPayload(kTrollId));
    EXPECT_FALSE(Has(kTrollId));
    EXPECT_TRUE(Has(kRodentId));
    EXPECT_EQ(eq.GetGameState().player().lootingCorpseId(), kRodentId);
    EXPECT_EQ(LootWindowClosedEvents(kRodentId), 0);
}

}  // namespace
