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

}  // namespace
