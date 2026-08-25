// T234: the five EqActionHandler methods T93 (refactor/T93_fake_substitution_
// sweep.md) classified as TRANSFORMS / VALIDATES / MUTATES and left untested
// against the real EverQuest, because RecordingActionHandler
// (tests/action_test_fakes.h) records a bare call and never runs the logic:
//
//   targetSelf()             self <-> pet toggle (MUTATES)
//   targetGroupMember(index) index validation + spawn_id==0 guard (VALIDATES)
//   hailTarget()             named hail vs plain "Hail" fallback (MUTATES)
//   setMovementMode(int)     int -> MovementMode (TRANSFORMS)
//   setPositionState(int)    int -> PositionState (TRANSFORMS)
//
// Why T93 could not test them: CombatManager::SetTarget() validates against
// EverQuest::m_entities (populated only by the zone packet handlers) and
// hailTarget()'s only observable is the C->S ChannelMessage packet, which
// SendChatMessage() refuses to build without a live zone connection. Neither
// needs a new production seam:
//
//   - m_entities / m_pet_spawn_id / m_group_members are populated the way
//     production populates them: NewSpawn and GroupUpdate packets replayed
//     through the ReplayZonePacket seam (zone_stream_test_utils.h).
//   - The zone connection is a REAL DaybreakConnection on a REAL
//     DaybreakConnectionManager over the FakeUdpTransport that
//     test_daybreak_state_machine.cpp already drives; it is installed into
//     EverQuest::m_zone_connection via the T17 `#define private public`
//     idiom (test_game_state_source_writes.cpp precedent). Every reliable
//     packet EverQuest queues lands, framed but not yet encoded, in
//     DaybreakConnection::m_streams[0].sent_packets -- so the asserts below
//     are on the actual outbound TargetMouse / ChannelMessage bytes.
//
// No network, no graphics, no eqemu.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#undef private
#undef protected

#include "client/bridge/game_state_bridge.h"
#include "client/combat.h"
#include "client/eq_action_handler.h"
#include "common/net/daybreak_connection.h"
#include "common/packet_structs.h"
#include "fake_udp_transport.h"
#include "zone_stream_test_utils.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace eqt;
using namespace zonetest;

namespace {

constexpr uint16_t kSelfId = 460;
constexpr uint16_t kPetId = 200;
constexpr uint16_t kGuardId = 100;
constexpr uint16_t kBuddyId = 300;

class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// One application packet as EverQuest queued it: opcode + body.
struct SentAppPacket {
    uint16_t opcode = 0;
    std::vector<uint8_t> body;
};

// NewSpawn for a pet owned by `owner` (test_game_state_source_writes.cpp
// precedent): is_pet at Spawn_Struct offset 329, petOwnerId at 189.
std::vector<uint8_t> PetSpawnPayload(uint32_t spawn_id, const std::string& name, uint8_t level,
                                     uint32_t owner) {
    return BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, spawn_id, name, level, 54, 1, 0, 1, 100, EncodePos(0, 10, 0, 0));
        p.PutUInt8(2 + 329, 1);
        p.PutUInt32(2 + 189, owner);
    });
}

// GroupUpdate_Struct (452 bytes after the opcode): action(4) yourname[64]
// membername[5][64] leadersname[64]. `others` are the up-to-5 non-self
// members exactly as the server lists them (self is NOT among them).
std::vector<uint8_t> GroupUpdatePayload(uint32_t action, const std::string& yourname,
                                        const std::vector<std::string>& others,
                                        const std::string& leader) {
    return BuildPayload(2 + sizeof(EQT::GroupUpdate_Struct), [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 0, action);
        p.PutCString(2 + 4, yourname.c_str());
        for (size_t i = 0; i < others.size() && i < 5; ++i) {
            p.PutCString(2 + 68 + 64 * i, others[i].c_str());
        }
        p.PutCString(2 + 388, leader.c_str());
    });
}

// Headless EverQuest + real EqActionHandler, wired to a real (never
// handshaken) DaybreakConnection so C->S packets can be read back.
class WireHarness {
public:
    WireHarness()
        : eq("127.0.0.1", 0, "test", "test", "test", "Testchar"), handler(eq) {
        EQ::Net::DaybreakConnectionManagerOptions opts;
        opts.crc_length = 2;
        opts.encode_passes[0] = EQ::Net::EncodeNone;
        opts.encode_passes[1] = EQ::Net::EncodeNone;
        opts.connect_delay_ms = 0;
        opts.keepalive_delay_ms = 0;
        manager = std::make_unique<EQ::Net::DaybreakConnectionManager>(
            opts, std::make_unique<EQ::Test::FakeUdpTransport>(&c2s, &s2c));
        manager->OnNewConnection([this](std::shared_ptr<EQ::Net::DaybreakConnection> c) {
            conn = c;
        });
        manager->Connect("peer", 12345);
        EXPECT_NE(conn, nullptr);

        eq.setBridge(&bridge);
        eq.Test_SetBypassSendGuardForSeam(true);
        eq.Test_SetMySpawnIdForSeam(kSelfId);
        eq.Test_SetZoneConnectedForSeam(true);
        eq.m_zone_connection = conn;  // private: T17 idiom, see header comment
    }

    ~WireHarness() {
        // EverQuest's destructor resets m_zone_connection itself; the manager
        // (declared first, destroyed last) must outlive that.
        eq.m_zone_connection.reset();
    }

    void spawnSelf() {
        ReplayZone(eq, HC_OP_NewSpawn,
                   NewSpawnPayloadAt(kSelfId, "Testchar", 0, 10, 0, 5, /*npc_type=*/0));
        ASSERT_NE(eq.GetEntities().find(kSelfId), eq.GetEntities().end());
        ASSERT_EQ(eq.GetMySpawnID(), kSelfId);
    }

    void spawnGuard() {
        ReplayZone(eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kGuardId, "Guard_Hezlan000", 0, 12, 0));
        ASSERT_NE(eq.GetEntities().find(kGuardId), eq.GetEntities().end());
    }

    void spawnPet() {
        ReplayZone(eq, HC_OP_NewSpawn, PetSpawnPayload(kPetId, "Gobaner", 12, kSelfId));
        ASSERT_EQ(eq.GetPetSpawnId(), kPetId) << "pet detection via the packet path";
    }

    // Everything EverQuest has queued on the reliable stream so far, in
    // sequence order. Reliable frames are [zero][OP_Packet][seq BE] + app
    // bytes; DaybreakConnection::QueuePacket prepends one 0x00 when the app
    // opcode's low byte is 0 (neither opcode used here has that, but the
    // decoder honours it anyway so a future opcode cannot skew the parse).
    std::vector<SentAppPacket> sentPackets() {
        std::vector<SentAppPacket> out;
        for (auto& [seq, sent] : conn->m_streams[0].sent_packets) {
            const auto* d = static_cast<const uint8_t*>(sent.packet.Data());
            size_t n = sent.packet.Length();
            size_t off = EQ::Net::DaybreakReliableHeader::size();
            if (off < n && d[off] == 0) ++off;
            if (off + 2 > n) continue;
            SentAppPacket pkt;
            pkt.opcode = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
            pkt.body.assign(d + off + 2, d + n);
            out.push_back(std::move(pkt));
        }
        return out;
    }

    void clearSent() { conn->m_streams[0].sent_packets.clear(); }

    // The TargetMouse packets sent since the last clearSent(), as target ids.
    std::vector<uint32_t> sentTargetIds() {
        std::vector<uint32_t> ids;
        for (auto& p : sentPackets()) {
            if (p.opcode == HC_OP_TargetMouse && p.body.size() >= 4) {
                uint32_t id;
                std::memcpy(&id, p.body.data(), 4);
                ids.push_back(id);
            }
        }
        return ids;
    }

    // The ChannelMessage packets sent since the last clearSent(), decoded
    // as (channel, message) -- offsets per ZoneSendChannelMessage: channel
    // u32 at packet offset 134, message cstring at 150 (2-byte opcode
    // included, so body offsets are 132 / 148).
    std::vector<std::pair<uint32_t, std::string>> sentChat() {
        std::vector<std::pair<uint32_t, std::string>> out;
        for (auto& p : sentPackets()) {
            if (p.opcode != HC_OP_ChannelMessage || p.body.size() < 149) continue;
            uint32_t channel;
            std::memcpy(&channel, p.body.data() + 132, 4);
            std::string msg(reinterpret_cast<const char*>(p.body.data() + 148));
            out.emplace_back(channel, msg);
        }
        return out;
    }

    std::string lastSystemMessage() {
        std::string last;
        for (auto& e : bridge.drainEvents()) {
            if (e.type == eqt::state::GameEventType::SystemMessage) {
                last = std::get<eqt::state::ChatMessageData>(e.data).message;
            }
        }
        return last;
    }

    EQ::Test::FakeChannel c2s;
    EQ::Test::FakeChannel s2c;
    std::unique_ptr<EQ::Net::DaybreakConnectionManager> manager;
    std::shared_ptr<EQ::Net::DaybreakConnection> conn;
    EverQuest eq;
    RecordingBridge bridge;
    EqActionHandler handler;
};

// ---------------------------------------------------------------------
// Harness self-check: the wire is real. CombatManager::SetTarget on a spawned
// entity must produce exactly one TargetMouse with that id.
// ---------------------------------------------------------------------

TEST(EqActionHandlerTransformsReal, Harness_SetTargetReachesTheWire) {
    WireHarness h;
    h.spawnGuard();
    h.clearSent();

    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));

    EXPECT_EQ(h.sentTargetIds(), std::vector<uint32_t>{kGuardId});
    EXPECT_EQ(h.eq.GetGameState().combat().targetId(), kGuardId);
}

// ---------------------------------------------------------------------
// targetSelf()
// ---------------------------------------------------------------------

TEST(EqActionHandlerTransformsReal, TargetSelf_NoTargetNoPet_TargetsSelf) {
    WireHarness h;
    h.spawnSelf();
    ASSERT_EQ(h.eq.GetPetSpawnId(), 0u);
    ASSERT_FALSE(h.eq.GetCombatManager()->HasTarget());
    h.clearSent();

    h.handler.targetSelf();

    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId);
    EXPECT_EQ(h.eq.GetCurrentTargetId(), kSelfId);
    EXPECT_EQ(h.eq.GetGameState().combat().targetId(), kSelfId);
    EXPECT_EQ(h.sentTargetIds(), std::vector<uint32_t>{kSelfId});
}

TEST(EqActionHandlerTransformsReal, TargetSelf_AlreadySelfNoPet_StaysSelfDoesNotClear) {
    // The toggle's first branch requires petId != 0; with no pet, pressing
    // the key again must re-target self, never fall through to target 0.
    WireHarness h;
    h.spawnSelf();
    h.handler.targetSelf();
    ASSERT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId);
    h.clearSent();

    h.handler.targetSelf();

    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId);
    EXPECT_EQ(h.sentTargetIds(), std::vector<uint32_t>{kSelfId});
}

TEST(EqActionHandlerTransformsReal, TargetSelf_WithPet_TogglesSelfPetSelf) {
    WireHarness h;
    h.spawnSelf();
    h.spawnPet();
    h.clearSent();

    h.handler.targetSelf();  // nothing -> self
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId);

    h.handler.targetSelf();  // self -> pet
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kPetId)
        << "second press with a pet must toggle to the pet";
    EXPECT_EQ(h.eq.GetGameState().combat().targetId(), kPetId);

    h.handler.targetSelf();  // pet -> self
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId)
        << "third press must toggle back to self";

    EXPECT_EQ(h.sentTargetIds(), (std::vector<uint32_t>{kSelfId, kPetId, kSelfId}));
}

TEST(EqActionHandlerTransformsReal, TargetSelf_WithPetButTargetingSomethingElse_TargetsSelfNotPet) {
    WireHarness h;
    h.spawnSelf();
    h.spawnPet();
    h.spawnGuard();
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();

    h.handler.targetSelf();

    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId)
        << "from a third-party target the default branch targets self, not the pet";
    EXPECT_EQ(h.sentTargetIds(), std::vector<uint32_t>{kSelfId});
}

TEST(EqActionHandlerTransformsReal, TargetSelf_SpawnIdUnknown_SendsNothing) {
    // Before our own spawn id is known (zone-in), newTarget == 0 and the
    // method must not send a TargetMouse for id 0.
    WireHarness h;
    h.eq.Test_SetMySpawnIdForSeam(0);
    h.clearSent();

    h.handler.targetSelf();

    EXPECT_FALSE(h.eq.GetCombatManager()->HasTarget());
    EXPECT_TRUE(h.sentTargetIds().empty());
}

// ---------------------------------------------------------------------
// targetGroupMember(index)
// ---------------------------------------------------------------------

class TargetGroupMember : public ::testing::Test {
protected:
    WireHarness h;

    void SetUp() override {
        h.spawnSelf();
        h.spawnGuard();
        ReplayZone(h.eq, HC_OP_NewSpawn, NewSpawnPayloadAt(kBuddyId, "Buddy", 0, 14, 0, 20,
                                                           /*npc_type=*/0));
        ASSERT_NE(h.eq.GetEntities().find(kBuddyId), h.eq.GetEntities().end());

        // Server-shaped group: leader Testchar, members Buddy (in zone) and
        // Ghost (not in zone -> spawn_id stays 0).
        ReplayZone(h.eq, HC_OP_GroupUpdate,
                   GroupUpdatePayload(EQT::GROUP_ACT_UPDATE, "Testchar", {"Buddy", "Ghost"},
                                      "Testchar"));
        ASSERT_TRUE(h.eq.IsInGroup());
        ASSERT_EQ(h.eq.GetGroupMemberCount(), 3);
        ASSERT_EQ(h.eq.GetGroupMember(0)->spawn_id, kSelfId);
        ASSERT_EQ(h.eq.GetGroupMember(1)->name, "Buddy");
        ASSERT_EQ(h.eq.GetGroupMember(1)->spawn_id, kBuddyId);
        ASSERT_EQ(h.eq.GetGroupMember(2)->name, "Ghost");
        ASSERT_EQ(h.eq.GetGroupMember(2)->spawn_id, 0u);

        // Start on an unrelated target so "no change" is distinguishable
        // from "targeted self".
        ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
        h.clearSent();
    }
};

TEST_F(TargetGroupMember, Index0_IsSelf) {
    h.handler.targetGroupMember(0);
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kSelfId);
    EXPECT_EQ(h.sentTargetIds(), std::vector<uint32_t>{kSelfId});
}

TEST_F(TargetGroupMember, Index1_InZoneMember_TargetsTheirSpawn) {
    h.handler.targetGroupMember(1);
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kBuddyId);
    EXPECT_EQ(h.eq.GetGameState().combat().targetId(), kBuddyId);
    EXPECT_EQ(h.sentTargetIds(), std::vector<uint32_t>{kBuddyId});
}

TEST_F(TargetGroupMember, Index2_MemberNotInZone_NoChangeNoPacket) {
    h.handler.targetGroupMember(2);
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kGuardId)
        << "spawn_id==0 member must not clear or retarget";
    EXPECT_EQ(h.eq.GetCurrentTargetId(), kGuardId)
        << "updateRendererTargetInfo(0) must not run either -- SetTarget(0) fails "
           "validation but the handler ignores its return value";
    EXPECT_TRUE(h.sentTargetIds().empty());
}

TEST_F(TargetGroupMember, IndexPastCount_NoChangeNoPacket) {
    h.handler.targetGroupMember(3);  // count is 3; first empty slot
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kGuardId);
    EXPECT_TRUE(h.sentTargetIds().empty());
}

TEST_F(TargetGroupMember, IndexAtMaxGroupSize_NoChangeNoPacket) {
    h.handler.targetGroupMember(6);  // MAX_GROUP_MEMBERS: one past the array
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kGuardId);
    EXPECT_TRUE(h.sentTargetIds().empty());
}

TEST_F(TargetGroupMember, NegativeIndex_NoChangeNoPacket) {
    h.handler.targetGroupMember(-1);
    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kGuardId);
    EXPECT_TRUE(h.sentTargetIds().empty());
}

TEST(EqActionHandlerTransformsReal, TargetGroupMember_NotInGroup_NoChangeNoPacket) {
    WireHarness h;
    h.spawnSelf();
    h.spawnGuard();
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    ASSERT_FALSE(h.eq.IsInGroup());
    h.clearSent();

    h.handler.targetGroupMember(0);

    EXPECT_EQ(h.eq.GetCombatManager()->GetTargetId(), kGuardId)
        << "index 0 is only 'self' once a group exists (m_group_member_count==0 otherwise)";
    EXPECT_TRUE(h.sentTargetIds().empty());
}

// ---------------------------------------------------------------------
// hailTarget()
// ---------------------------------------------------------------------

TEST(EqActionHandlerTransformsReal, HailTarget_NoTarget_FallsBackToPlainHail) {
    WireHarness h;
    ASSERT_FALSE(h.eq.GetCombatManager()->HasTarget());
    h.clearSent();

    h.handler.hailTarget();

    auto chat = h.sentChat();
    ASSERT_EQ(chat.size(), 1u);
    EXPECT_EQ(chat[0].first, static_cast<uint32_t>(CHAT_CHANNEL_SAY));
    EXPECT_EQ(chat[0].second, "Hail");
}

TEST(EqActionHandlerTransformsReal, HailTarget_WithTarget_HailsDisplayName) {
    WireHarness h;
    h.spawnGuard();
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.clearSent();

    h.handler.hailTarget();

    auto chat = h.sentChat();
    ASSERT_EQ(chat.size(), 1u);
    EXPECT_EQ(chat[0].first, static_cast<uint32_t>(CHAT_CHANNEL_SAY));
    EXPECT_EQ(chat[0].second, "Hail, Guard Hezlan")
        << "server name Guard_Hezlan000 must be rendered through toDisplayName()";
}

TEST(EqActionHandlerTransformsReal, HailTarget_AfterClearTarget_FallsBackAgain) {
    WireHarness h;
    h.spawnGuard();
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.handler.clearTarget();
    h.clearSent();

    h.handler.hailTarget();

    auto chat = h.sentChat();
    ASSERT_EQ(chat.size(), 1u);
    EXPECT_EQ(chat[0].second, "Hail");
}

TEST(EqActionHandlerTransformsReal, HailTarget_TargetVanishedFromGameState_FallsBack) {
    // The name lookup is against GameState::entities(), not CombatManager's
    // cached CombatTarget. If the target has been despawned there but the
    // combat target id is stale, the method must degrade to "Hail".
    WireHarness h;
    h.spawnGuard();
    ASSERT_TRUE(h.eq.GetCombatManager()->SetTarget(kGuardId));
    h.eq.m_game_state.entities().removeEntity(kGuardId);
    ASSERT_EQ(h.eq.GetGameState().entities().getEntity(kGuardId), nullptr);
    ASSERT_TRUE(h.eq.GetCombatManager()->HasTarget());
    h.clearSent();

    h.handler.hailTarget();

    auto chat = h.sentChat();
    ASSERT_EQ(chat.size(), 1u);
    EXPECT_EQ(chat[0].second, "Hail");
}

// ---------------------------------------------------------------------
// setMovementMode(int) / setPositionState(int)
//
// T95 (test_eq_action_handler_enum_guards.cpp) pins the range guards and
// the bridge events. What it does not pin is the int -> enum VALUE mapping
// at the source: EverQuest::m_movement_mode / m_position_state are private
// with no getter, so the T95 tests read the event payload (a re-cast of the
// same int). These read the members directly.
// ---------------------------------------------------------------------

TEST(EqActionHandlerTransformsReal, SetMovementMode_EveryValidIntMapsToItsEnumerator) {
    WireHarness h;
    const struct { int in; MovementMode want; } cases[] = {
        {1, MOVE_MODE_WALK}, {2, MOVE_MODE_SNEAK}, {0, MOVE_MODE_RUN},
    };
    for (auto& c : cases) {
        h.handler.setMovementMode(c.in);
        EXPECT_EQ(h.eq.m_movement_mode, c.want) << "input " << c.in;
        EXPECT_EQ(h.lastSystemMessage(), "") << "valid input must not be rejected";
    }
}

TEST(EqActionHandlerTransformsReal, SetMovementMode_Rejected_LeavesSourceUnchangedAndReports) {
    WireHarness h;
    h.handler.setMovementMode(1);
    ASSERT_EQ(h.eq.m_movement_mode, MOVE_MODE_WALK);
    h.bridge.drainEvents();

    h.handler.setMovementMode(3);

    EXPECT_EQ(h.eq.m_movement_mode, MOVE_MODE_WALK);
    EXPECT_EQ(h.lastSystemMessage(),
              "Movement mode 3 is not valid (must be 0=Run, 1=Walk, or 2=Sneak).");
}

TEST(EqActionHandlerTransformsReal, SetPositionState_EveryValidIntMapsToItsEnumerator) {
    WireHarness h;
    const struct { int in; PositionState want; } cases[] = {
        {1, POS_SITTING}, {2, POS_CROUCHING}, {3, POS_FEIGN_DEATH}, {4, POS_DEAD},
        {0, POS_STANDING},
    };
    for (auto& c : cases) {
        h.handler.setPositionState(c.in);
        EXPECT_EQ(h.eq.m_position_state, c.want) << "input " << c.in;
        EXPECT_EQ(h.lastSystemMessage(), "") << "valid input must not be rejected";
    }
}

TEST(EqActionHandlerTransformsReal, SetPositionState_Rejected_LeavesSourceUnchangedAndReports) {
    WireHarness h;
    h.handler.setPositionState(1);
    ASSERT_EQ(h.eq.m_position_state, POS_SITTING);
    h.bridge.drainEvents();

    h.handler.setPositionState(5);

    EXPECT_EQ(h.eq.m_position_state, POS_SITTING);
    EXPECT_EQ(h.lastSystemMessage(),
              "Position state 5 is not valid (must be 0-4: Standing/Sitting/Crouching/"
              "FeignDeath/Dead).");
}

// T234 DEFECT (not fixed here -- tests-only unit): state::PlayerState has
// movementMode()/positionState() with setters, but PlayerState::
// setMovementMode()/setPositionState() have ZERO production callers.
// EverQuest::SetMovementMode()/SetPositionState() update the private
// m_movement_mode/m_position_state and push a bridge event but never write
// m_game_state.player(), so the GameState projection of the player's
// movement mode / position state is permanently Run/Standing. This is the
// T221 "written at the source" contract not being met for these two fields
// (the reachability pattern: a setter that shipped with no caller). Enable
// once EverQuest::SetMovementMode/SetPositionState write the projection.
TEST(EqActionHandlerTransformsReal, DISABLED_SetMovementMode_WritesGameStatePlayerProjection) {
    WireHarness h;
    h.handler.setMovementMode(2);
    EXPECT_EQ(h.eq.GetGameState().player().movementMode(), eqt::state::MovementMode::Sneak);
}

TEST(EqActionHandlerTransformsReal, DISABLED_SetPositionState_WritesGameStatePlayerProjection) {
    WireHarness h;
    h.handler.setPositionState(1);
    EXPECT_EQ(h.eq.GetGameState().player().positionState(), eqt::state::PositionState::Sitting);
}

}  // namespace
