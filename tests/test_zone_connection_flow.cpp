// T04: zone connection-flow sequencing tests.
//
// Targets CheckZoneRequestPhaseComplete (eq_zone_entry.cpp) and the
// surrounding zone-stream dispatcher (DispatchZonePacket,
// eq_zone_dispatch.cpp) -- both split out of eq.cpp by J02 -- driven entirely
// through the Batch R replay/test seam (EverQuest::ReplayZonePacket,
// eq.h:697) -- no network, no graphics, no live connection.
//
// Scope: zone stream ONLY. Login/world handlers are private with no replay
// equivalent (see refactor/T04_zone_connection_flow.md "Scope boundary");
// they are explicitly out of scope for this unit.
//
// This file follows tests/test_session_replay.cpp's idiom for building
// synthetic packets: BuildPayload() fills a DynamicPacket at the packet
// offsets the ZoneProcess* handlers actually read (offsets documented in
// the handler being exercised), Replay() feeds it through the same opcode
// switch ZoneOnPacketRecv uses.
//
// See refactor/T04_zone_connection_flow.md for the design/review notes.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "client/eq.h"
#include "client/graphics/eq/wld_loader.h"
#include "client/zone_bsp.h"
#include "client/bridge/game_state_bridge.h"
#include "client/inventory/inventory_constants.h"
#include "common/net/packet.h"

namespace {

// Builds the payload bytes (packet minus the 2-byte opcode prefix) for a
// synthetic Zone S->C packet. `totalPacketSize` includes the opcode prefix
// so callers can write field offsets exactly as documented in the
// ZoneProcess* handlers (which read at those same packet offsets).
std::vector<uint8_t> BuildPayload(size_t totalPacketSize,
                                   const std::function<void(EQ::Net::DynamicPacket&)>& fill) {
    EQ::Net::DynamicPacket p;
    p.Resize(totalPacketSize);
    if (fill) {
        fill(p);
    }
    std::vector<uint8_t> payload(totalPacketSize >= 2 ? totalPacketSize - 2 : 0);
    if (!payload.empty()) {
        std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    }
    return payload;
}

// A zero-length payload -- opcode only, no body.
std::vector<uint8_t> EmptyPayload() {
    return {};
}

void Replay(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayZonePacket(opcode, payload.data(), payload.size());
}

// The four gating-relevant packets, built with the minimal (empty) payload
// their handlers actually need -- none of ZoneProcessSendAATable,
// ZoneProcessRespondAA, ZoneProcessTributeInfo, ZoneProcessSendGuildTributes
// read any packet fields (all four in eq_player_info.cpp); they only log, bump a
// counter (except RespondAA), and call CheckZoneRequestPhaseComplete().
void SendNewZone(EverQuest& eq) {
    // Zero-length payload: ZoneProcessNewZone guards every field read behind
    // p.Length() thresholds (ZoneProcessNewZone, eq_zone_entry.cpp), so an empty payload safely
    // skips zone-name/environment parsing and falls through to
    // m_new_zone_received = true plus the four AA/tribute/guild-tribute
    // request sends (suppressed, no live connection).
    Replay(eq, HC_OP_NewZone, EmptyPayload());
}
void SendAATable(EverQuest& eq) { Replay(eq, HC_OP_SendAATable, EmptyPayload()); }
void SendRespondAA(EverQuest& eq) { Replay(eq, HC_OP_RespondAA, EmptyPayload()); }
void SendTributeInfo(EverQuest& eq) { Replay(eq, HC_OP_TributeInfo, EmptyPayload()); }
void SendGuildTributes(EverQuest& eq) { Replay(eq, HC_OP_SendGuildTributes, EmptyPayload()); }

// T41: minimal GameStateBridge fake that just records every pushed event's
// type, so tests can assert PublishFullStateSnapshot() fired (and fired only
// once) without depending on IrrlichtBridge/ConsoleBridge or a real renderer.
// applyEvent() is unused here -- this test only drains and inspects the
// event queue EverQuest pushed into, it never calls applyEvent() itself.
class RecordingBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// T41 addendum: writes one 385-byte Titanium Spawn_Struct at packet offset
// `base`, same field offsets test_zone_entities.cpp's WriteSpawnStruct uses
// (this file doesn't share that header, so a small local copy -- extended
// with is_pet/pet_owner_id, which that helper doesn't need for its own
// tests), for building a synthetic "our pet, already in the zone" NPC spawn.
// is_pet at struct offset 329 (uint8), pet_owner_id at 189 (uint32) --
// confirmed against eq_entities.cpp, the same offsets
// ZoneProcessZoneSpawns/ZoneProcessNewSpawn read.
void WritePetSpawnStruct(EQ::Net::DynamicPacket& p, size_t base, uint32_t spawnId,
                          const std::string& name, uint32_t petOwnerId) {
    p.PutCString(base + 7, name.c_str());
    p.PutUInt8(base + 83, 1);     // npc_type: NPC
    p.PutUInt8(base + 86, 100);   // hp_percent
    p.PutUInt8(base + 151, 1);    // level
    p.PutUInt32(base + 189, petOwnerId);
    p.PutUInt32(base + 284, 54);  // race
    p.PutUInt8(base + 329, 1);    // is_pet
    p.PutUInt8(base + 331, 1);    // char_class
    p.PutUInt32(base + 340, spawnId);
}

// T43: builds a minimal but well-formed Titanium serialized-item string, the
// same wire format TitaniumItemParser::parseItem() (inventory_item_parser.cpp)
// expects from a real HC_OP_CharInventory/HC_OP_ItemPacket -- instance
// fields, then a quoted block of >=135 static fields, per
// "titanium.cpp SerializeItem" (see parseInstanceData/parseStaticData's own
// field-index comments). Only the fields this test actually asserts on
// (slot, itemId, name, icon) are set to nonzero/nonempty; everything else is
// left empty (TitaniumItemParser::toInt() defaults an empty field to 0).
std::string BuildItemString(int16_t slot, uint32_t itemId, const std::string& name, uint32_t icon) {
    // Instance fields (11): stackCount|unknown|slot|price|count|exp|serial|recast|charges|attuned|unknown
    std::string instance = "0|0|" + std::to_string(slot) + "|0|1|0|0|0|0|0|0";

    // Static fields (135 minimum -- parseStaticData rejects fewer).
    std::vector<std::string> fields(135, "");
    fields[0] = "0";                    // itemType (0 = Common)
    fields[1] = name;                   // name
    fields[2] = name;                   // lore
    fields[3] = "IT63";                 // idfile (unused by the parser beyond presence)
    fields[4] = std::to_string(itemId); // itemId
    fields[5] = "10";                   // weight (tenths -> 1.0)
    fields[6] = "1";                    // noRent field: nonzero -> noRent=false
    fields[7] = "1";                    // noDrop field: nonzero -> noDrop=false
    fields[8] = "0";                    // size
    fields[9] = "0";                    // equip slots bitmask
    fields[10] = "0";                   // vendor price
    fields[11] = std::to_string(icon);  // icon

    std::string staticData;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) staticData += "|";
        staticData += fields[i];
    }

    return instance + "\"" + staticData + "\"";
}

} // namespace

// ============================================================================
// 1. Zone-request phase gating -- the core of the unit.
//
// CheckZoneRequestPhaseComplete() fires ZoneSendReqClientSpawn() exactly
// once, and only when m_new_zone_received && m_aa_table_count>0 &&
// m_tribute_count>0 && m_guild_tribute_count>0 && !m_req_client_spawn_sent.
//
// The check is invoked from all FIVE gate handlers: ZoneProcessSendAATable,
// ZoneProcessRespondAA, ZoneProcessTributeInfo, ZoneProcessSendGuildTributes
// (eq_player_info.cpp) and -- since T228 -- ZoneProcessNewZone (eq_zone_entry.cpp).
// T04 originally found NewZone missing from that list, which meant a
// NewZone arriving LAST never completed the phase; see
// NewZoneArrivingLast_CompletesPhase below.
// ============================================================================

TEST(ZoneRequestPhaseGating, FreshClient_PhaseNotYetRequested) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 0u);
}

TEST(ZoneRequestPhaseGating, WithholdingNewZone_DoesNotAdvance) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Three of four prerequisites present; NewZone withheld entirely.
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    // None of these three handlers send anything themselves (no live
    // connection needed either way), so the suppressed-send counter must
    // still be zero -- proof ZoneSendReqClientSpawn was never reached.
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 0u);
}

TEST(ZoneRequestPhaseGating, WithholdingAATable_DoesNotAdvance) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4 (AA table/UpdateAA/tributes/guild-tributes requests)
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    // m_aa_table_count stays 0: RespondAA calls the check but does not
    // increment the AA counter (ZoneProcessRespondAA, eq_player_info.cpp), so it cannot
    // substitute for SendAATable.
    SendRespondAA(eq);

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 4u) << "ReqClientSpawn must not have been sent";
}

TEST(ZoneRequestPhaseGating, WithholdingTributeInfo_DoesNotAdvance) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4
    SendAATable(eq);
    SendGuildTributes(eq);

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 4u);
}

TEST(ZoneRequestPhaseGating, WithholdingGuildTributes_DoesNotAdvance) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4
    SendAATable(eq);
    SendTributeInfo(eq);

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 4u);
}

TEST(ZoneRequestPhaseGating, AllFourPresent_AdvancesExactlyOnce) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);    // last gate satisfied here -> fires ReqClientSpawn (+1)

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u);
}

TEST(ZoneRequestPhaseGating, FireOnceGuard_AdditionalPacketsDoNotRefire) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    ASSERT_EQ(eq.GetReplaySuppressedSendCount(), 5u);
    ASSERT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);

    // Keep feeding more of the same packets: m_req_client_spawn_sent guards
    // against a second ZoneSendReqClientSpawn.
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    SendRespondAA(eq);

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u)
        << "ReqClientSpawn fired again -- fire-once guard broken";
}

TEST(ZoneRequestPhaseGating, OrderIndependent_DifferentOrderingsOfTrailingThreeConverge) {
    // The server does not promise an order for AA table / tribute info /
    // guild tributes relative to each other (NewZoneArrivingLast_CompletesPhase
    // below covers NewZone itself arriving last). Try two different
    // permutations of the trailing three.
    {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        SendNewZone(eq);
        SendGuildTributes(eq);
        SendAATable(eq);
        SendTributeInfo(eq);
        EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
        EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u);
    }
    {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        SendNewZone(eq);
        SendTributeInfo(eq);
        SendGuildTributes(eq);
        SendAATable(eq);
        EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
        EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u);
    }
}

TEST(ZoneRequestPhaseGating, NewZoneArrivingLast_CompletesPhase) {
    // T04 found and pinned the gap: CheckZoneRequestPhaseComplete() was
    // called only from the AA-table/RespondAA/tribute-info/guild-tribute
    // handlers, never from ZoneProcessNewZone, so a NewZone arriving LAST
    // left all four prerequisites true in memory with nothing re-running
    // the check -- ReqClientSpawn was never sent unless a fifth gate packet
    // happened to follow. T228 added the call at the end of
    // ZoneProcessNewZone; this test is the red/green for it (red at
    // dc494ef7: phase stays ZONE_CONNECTING, suppressed sends == 4).
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 0u);

    SendNewZone(eq); // suppressed += 4 (AA/UpdateAA/tributes/guild-tributes)
                      // and, now, += 1 for ReqClientSpawn.

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE)
        << "NewZone arriving last must complete the zone-request phase";
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u)
        << "ReqClientSpawn must be sent once all four prerequisites are met";

    // Idempotence: a later gate packet re-runs the check but must not send
    // ReqClientSpawn a second time (m_req_client_spawn_sent guard).
    SendRespondAA(eq);
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u);
}

TEST(ZoneRequestPhaseGating, NewZoneFirst_CheckFromNewZoneIsANoOp) {
    // The check added to ZoneProcessNewZone runs while the three counters
    // are still zero; it must neither advance the phase nor send anything
    // beyond NewZone's own four requests.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    SendNewZone(eq);
    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 4u);
}

// ============================================================================
// 2. Robustness of the dispatcher.
// ============================================================================

TEST(DispatcherRobustness, UnknownOpcode_IgnoredWithoutCrash) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    // 0xFFFF does not appear as any HC_OP_* value in eq.h.
    EXPECT_NO_THROW(Replay(eq, 0xFFFF, EmptyPayload()));
    EXPECT_NO_THROW(Replay(eq, 0xFFFF, std::vector<uint8_t>(50, 0xAB)));
    // State must be untouched.
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 0u);
    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
}

TEST(DispatcherRobustness, ZeroLengthPayload_GuardedHandlersSafe) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Handlers that explicitly guard on p.Length() before reading any
    // field, so a zero-length payload must be a safe no-op / early return.
    EXPECT_NO_THROW(Replay(eq, HC_OP_DeleteSpawn, EmptyPayload()));     // ZoneProcessDeleteSpawn (needs 4)
    EXPECT_NO_THROW(Replay(eq, HC_OP_ZoneSpawns, EmptyPayload()));      // loop condition offset+385<=Length()
    EXPECT_NO_THROW(Replay(eq, HC_OP_NewSpawn, EmptyPayload()));        // ZoneProcessNewSpawn (needs 387)
    EXPECT_NO_THROW(Replay(eq, HC_OP_NewZone, EmptyPayload()));         // ZoneProcessNewZone (needs 96)
    EXPECT_NO_THROW(Replay(eq, HC_OP_SendAATable, EmptyPayload()));     // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_RespondAA, EmptyPayload()));       // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_TributeInfo, EmptyPayload()));     // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_SendGuildTributes, EmptyPayload())); // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_ClientUpdate, EmptyPayload()));    // ZoneProcessClientUpdate (needs 24)
    EXPECT_NO_THROW(Replay(eq, HC_OP_GuildMOTD, EmptyPayload()));       // reads nothing
}

TEST(DispatcherRobustness, TruncatedPayload_GuardedHandlersSafe) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // One byte short of each handler's documented minimum.
    EXPECT_NO_THROW(Replay(eq, HC_OP_DeleteSpawn, std::vector<uint8_t>(1, 0)));    // needs 2
    EXPECT_NO_THROW(Replay(eq, HC_OP_NewSpawn, std::vector<uint8_t>(384, 0)));     // needs 385
    EXPECT_NO_THROW(Replay(eq, HC_OP_ClientUpdate, std::vector<uint8_t>(21, 0)));  // needs 22
    EXPECT_NO_THROW(Replay(eq, HC_OP_ZoneSpawns, std::vector<uint8_t>(384, 0)));   // one spawn short
}

TEST(DispatcherRobustness, DuplicateDelivery_Harmless) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    const uint16_t kSpawnId = 4004;
    auto spawnPayload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
        size_t off = 2;
        p.PutCString(off + 7, "a_gnoll");
        p.PutUInt32(off + 340, kSpawnId);
        p.PutUInt8(off + 151, 10);
        p.PutUInt8(off + 86, 100);
        p.PutUInt8(off + 83, 1);
        p.PutFloat(off + 75, 5.0f);
        p.PutUInt32(off + 94, 0);
        p.PutUInt32(off + 98, 0);
        p.PutUInt32(off + 102, 0);
        p.PutUInt32(off + 106, 0);
    });

    // Deliver the same ZoneSpawns entry three times.
    Replay(eq, HC_OP_ZoneSpawns, spawnPayload);
    Replay(eq, HC_OP_ZoneSpawns, spawnPayload);
    Replay(eq, HC_OP_ZoneSpawns, spawnPayload);

    ASSERT_EQ(eq.GetEntities().count(kSpawnId), 1u);
    EXPECT_EQ(eq.GetEntities().at(kSpawnId).name, "a_gnoll");

    // Deliver DeleteSpawn for it twice; the second is a no-op, not a crash.
    auto deletePayload = BuildPayload(2 + 2, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, kSpawnId);
    });
    EXPECT_NO_THROW(Replay(eq, HC_OP_DeleteSpawn, deletePayload));
    EXPECT_EQ(eq.GetEntities().count(kSpawnId), 0u);
    EXPECT_NO_THROW(Replay(eq, HC_OP_DeleteSpawn, deletePayload));
    EXPECT_EQ(eq.GetEntities().count(kSpawnId), 0u);
}

// --- Truncated-packet guards (T05 Part B). Both handlers below used to skip
// the initial EQ::Net::Packet accessor bounds check (Packet::GetUInt8/16/32/
// GetFloat throw std::out_of_range rather than reading out of bounds -- see
// src/common/net/packet.cpp:207-250), which was UNCAUGHT anywhere between
// DispatchZonePacket and the network receive callback in production. T05
// added length guards to both handlers (ZoneProcessTimeOfDay in
// src/client/eq_entities.cpp, ZoneProcessPlayerProfile in
// src/client/eq_zone_entry.cpp) so a truncated packet -- exactly what a lossy
// network produces -- now returns safely instead of throwing.
//
// These tests were pinned as BUG_*_ThrowsOutOfRange in T04; T05 flips them to
// assert the fixed (return-safely, no state change) behavior. See the Review
// section of refactor/T05_login_world_seam.md.

TEST(DispatcherRobustness, TimeOfDay_ZeroLengthPayload_ReturnsSafely) {
    // ZoneProcessTimeOfDay (eq_entities.cpp) now guards p.Length() < 8
    // before reading GetUInt8(2)/GetUInt8(3)/GetUInt8(4)/GetUInt8(5)/
    // GetUInt16(6). A zero-length (or any <8-byte) TimeOfDay payload no
    // longer throws, and time-of-day state is left unchanged.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_NO_THROW(Replay(eq, HC_OP_TimeOfDay, EmptyPayload()));
}

TEST(DispatcherRobustness, PlayerProfile_TruncatedPayload_ReturnsSafely) {
    // ZoneProcessPlayerProfile (eq_zone_entry.cpp) logs a LOG_WARN when
    // p.Length() < base+19588 and now returns immediately instead of
    // falling through to unconditional field reads (GetUInt32(base+4)
    // etc.), so any payload shorter than the full 19588-byte
    // PlayerProfile_Struct is handled gracefully instead of throwing.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_NO_THROW(Replay(eq, HC_OP_PlayerProfile, EmptyPayload()));
    EXPECT_NO_THROW(Replay(eq, HC_OP_PlayerProfile, std::vector<uint8_t>(10, 0)));
    EXPECT_EQ(eq.GetEntities().size(), 0u);
}

// ============================================================================
// 3. Observable zone state after a handshake.
// ============================================================================

TEST(ObservableZoneState, FullHandshake_ProducesCoherentState) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    const uint16_t kMySpawnId = 5005;

    // NewZone: sets the zone name.
    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }
    EXPECT_EQ(eq.GetCurrentZoneName(), "qeytoqrg");
    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_FALSE(eq.IsGameStateReady());
    EXPECT_FALSE(eq.IsFullyZonedIn());

    // PlayerProfile: establishes our own spawn id (full-size payload -- see
    // PlayerProfile_TruncatedPayload_ReturnsSafely above for what a short
    // one does).
    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);
            p.PutUInt32(2 + 12, 1);
            p.PutUInt8(2 + 20, 10);
            p.PutUInt32(2 + 2232, 100);
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    // Zone-request phase: all four gates, in order.
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);

    // GuildMOTD: last packet of SendZoneInPackets() -- sends ServerFilter +
    // ClientReady, advances to ZONE_PLAYER_READY.
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_PLAYER_READY);
    EXPECT_FALSE(eq.IsFullyZonedIn()) << "no live m_zone_connection in replay mode";

    // ClientUpdate for our own spawn id confirms position and completes the
    // game-state phase (no graphics initialized in this test binary, so the
    // phase runs all the way to COMPLETE rather than stopping at
    // GRAPHICS_LOADING_ZONE).
    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }

    EXPECT_TRUE(eq.IsGameStateReady());
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::COMPLETE);
    // IsFullyZonedIn() additionally requires m_zone_connected, which is only
    // set true by the real DaybreakConnection status callback
    // (EverQuest::ZoneOnStatusChange*, eq.cpp) -- unreachable through the replay seam. This is a known
    // ceiling of ReplayZonePacket, not a bug: the replay harness proves the
    // zone S->C *packet handling* is correct, not end-to-end liveness of a
    // real socket.
    EXPECT_FALSE(eq.IsFullyZonedIn());
}

// ============================================================================
// 4. T41: PublishFullStateSnapshot() fires exactly once per zone-in in
// headless mode, from OnGameStateComplete()'s no-graphics branch. This test
// binary never initializes graphics, so it exercises the same
// !m_graphics_initialized path a real --no-graphics run (or an
// EQT_GRAPHICS=OFF build) takes -- before T41 this path never called
// PublishFullStateSnapshot() at all, so headless GameState started empty
// and stayed that way.
// ============================================================================

TEST(FullStateSnapshot, HeadlessZoneInPublishesSnapshotExactlyOnce) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    const uint16_t kMySpawnId = 5006;

    // Same handshake shape as ObservableZoneState.FullHandshake above, just
    // with a bridge attached so the events PublishFullStateSnapshot() pushes
    // are observable.
    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);
            p.PutUInt32(2 + 12, 1);
            p.PutUInt8(2 + 20, 10);
            p.PutUInt32(2 + 2232, 100);
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());

    // Drain whatever the handshake pushed so far (NetworkReady etc. are not
    // this test's concern) and confirm the snapshot has NOT fired yet --
    // OnGameStateComplete() (and therefore PublishFullStateSnapshot()) only
    // runs once ClientUpdate confirms our own spawn id, matching the
    // existing ObservableZoneState.FullHandshake test's EXPECT_FALSE(
    // eq.IsGameStateReady()) at the equivalent point.
    auto preClientUpdate = bridge.drainEvents();
    int preCount = 0;
    for (const auto& e : preClientUpdate) {
        if (e.type == eqt::state::GameEventType::SkillsSnapshot) preCount++;
    }
    EXPECT_EQ(preCount, 0);

    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }

    ASSERT_TRUE(eq.IsGameStateReady());
    ASSERT_EQ(eq.GetLoadingPhase(), LoadingPhase::COMPLETE);

    // SkillsSnapshot is only ever pushed from PublishFullStateSnapshot()
    // (grep-confirmed: no per-field packet handler publishes it -- unlike
    // CharacterInfoChanged, which ZoneProcessPlayerProfile also pushes, so
    // it can't be used as a snapshot-only proxy), so counting it is a clean
    // proxy for "the snapshot fired" and "how many times".
    auto postClientUpdate = bridge.drainEvents();
    int snapshotFires = 0;
    for (const auto& e : postClientUpdate) {
        if (e.type == eqt::state::GameEventType::SkillsSnapshot) snapshotFires++;
    }
    EXPECT_EQ(snapshotFires, 1)
        << "PublishFullStateSnapshot() should fire exactly once from "
           "OnGameStateComplete()'s headless branch on zone-in";

    // Further packets in the same zone session must not refire it --
    // OnGameStateComplete() is gated on !m_update_running (StartUpdateLoop()
    // sets it), so a second, unrelated ClientUpdate for the same spawn must
    // not push a second snapshot.
    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }
    auto afterSecondUpdate = bridge.drainEvents();
    int refires = 0;
    for (const auto& e : afterSecondUpdate) {
        if (e.type == eqt::state::GameEventType::SkillsSnapshot) refires++;
    }
    EXPECT_EQ(refires, 0) << "A second ClientUpdate must not republish the snapshot";
}

// ============================================================================
// 5. T41 addendum (orchestrator request, 2026-08-04): a pet that already
// exists when the player zones in must still surface as PetCreated in
// headless mode. T40 event-fed pet state via GameStateSyncBridge and
// deleted syncPetState() -- PetCreated is one of the events
// PublishFullStateSnapshot() republishes (eq.cpp, "--- Pet ---" block,
// `if (m_pet_spawn_id != 0)`), so a pre-existing pet is a second thing this
// unit's headless-snapshot fix needed to reach, not just inventory.
//
// eq_entities.cpp has two places that discover a pre-existing pet from the
// zone's spawn list: the "second pass" in ZoneProcessSpawnAll (fires once,
// right after the ZoneSpawns packet finishes parsing, for the case where our
// own spawn already has a spawn_id by then) and the deferred check inside
// ClientUpdate (for the case where spawn IDs arrive out of order). Either
// way, `m_pet_spawn_id = ...` itself is NOT graphics-gated -- only the
// immediate `OnPetCreated()` delta-publish alongside it is
// (`#ifdef EQT_HAS_GRAPHICS`). So:
//   - In an EQT_GRAPHICS=OFF build (this test runs in both configs), the
//     delta never fires -- PublishFullStateSnapshot()'s pet block is the
//     ONLY path a pre-existing pet's PetCreated event reaches the bridge.
//     Before this unit's headless snapshot fix, that path never fired
//     either, so a pre-existing pet was invisible outright in that build.
//   - In an EQT_HAS_GRAPHICS build (regardless of runtime --no-graphics),
//     the delta fires immediately during spawn processing AND the snapshot
//     republishes it later -- pre-existing, harmless duplication (the same
//     thing already happened in graphics mode before this unit, since
//     OnGraphicsComplete always called the snapshot after any pre-existing
//     pet had already been spawn-detected).
// This test accepts either shape (asserts total coverage >= 1) but records
// which one happened, and additionally asserts the strict single-source
// count when the delta didn't fire -- which is what actually runs when this
// test executes in the build-nogfx tree.
// ============================================================================

TEST(FullStateSnapshot, PreExistingPetSurvivesHeadlessZoneInViaSnapshot) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    const uint16_t kMySpawnId = 5007;
    const uint32_t kPetSpawnId = 9001;

    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    // PlayerProfile establishes our own spawn id -- the pet-detection passes
    // below key off m_my_spawn_id being nonzero.
    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);
            p.PutUInt32(2 + 12, 1);
            p.PutUInt8(2 + 20, 10);
            p.PutUInt32(2 + 2232, 100);
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    // The pet is ALREADY in the zone's spawn list -- this is what makes it
    // "pre-existing" rather than a mid-session GM-summon delta (which is all
    // T40's live leg exercised). ZoneProcessSpawnAll's "second pass" runs
    // synchronously inside this call, since m_my_spawn_id is already nonzero.
    {
        auto payload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
            WritePetSpawnStruct(p, 2, kPetSpawnId, "Testchars_pet", kMySpawnId);
        });
        Replay(eq, HC_OP_ZoneSpawns, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kPetSpawnId), 1u);

    auto afterSpawns = bridge.drainEvents();
    bool deltaFired = false;
    for (const auto& e : afterSpawns) {
        if (e.type == eqt::state::GameEventType::PetCreated) deltaFired = true;
    }

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());
    bridge.drainEvents();  // discard NetworkReady etc., not this test's concern

    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }
    ASSERT_TRUE(eq.IsGameStateReady());

    auto afterClientUpdate = bridge.drainEvents();
    int postSnapshotPetCreated = 0;
    for (const auto& e : afterClientUpdate) {
        if (e.type == eqt::state::GameEventType::PetCreated) postSnapshotPetCreated++;
    }

    if (deltaFired) {
        // EQT_HAS_GRAPHICS build: the pre-existing-spawn delta already
        // published PetCreated during ZoneProcessSpawnAll; the snapshot
        // republishes it once more (pre-existing behavior, not introduced by
        // this unit -- see comment block above).
        EXPECT_EQ(postSnapshotPetCreated, 1)
            << "Snapshot should still republish PetCreated for the pet even "
               "though a delta already fired for it";
    } else {
        // EQT_GRAPHICS=OFF build: no delta path exists at all (OnPetCreated
        // is compile-gated out), so the snapshot is the ONLY way this event
        // reaches the bridge -- exactly the addendum's concern, and exactly
        // what this unit's headless-snapshot fix closes.
        EXPECT_EQ(postSnapshotPetCreated, 1)
            << "PublishFullStateSnapshot() should be the sole source of "
               "PetCreated for a pre-existing pet in a headless "
               "(EQT_GRAPHICS=OFF) build";
    }
    EXPECT_TRUE(deltaFired || postSnapshotPetCreated >= 1)
        << "A pre-existing pet must reach the bridge as PetCreated one way "
           "or the other -- it did neither";
}

// ============================================================================
// 6. T43: the regression test the deferred.md "EQT_HAS_GRAPHICS gates around
// non-graphics code" pattern entry asked for -- a headless zone-in, driven
// entirely through the server-free ReplayZonePacket seam (this file's own
// idiom, extended rather than replaced), that asserts game state actually
// POPULATED across every dimension the pattern's five historical instances
// broke: inventory, pet, spell gems, and player stats. Runs in both the
// EQT_GRAPHICS=ON and EQT_GRAPHICS=OFF trees (like every test in this file);
// the point is that it must pass in EQT_GRAPHICS=OFF without ever touching a
// renderer or a live server.
//
// Of the five historical instances (refactor/deferred.md's pattern entry),
// this test would have caught THREE outright:
//   - P28 (eq.h guards enclosing non-graphics members): the EQT_GRAPHICS=OFF
//     tree wouldn't have compiled this test binary at all -- a build failure
//     is "failed immediately" in the bluntest possible sense.
//   - T41 (m_inventory_manager never constructed): the InventorySlotChanged
//     assertion below would find no populated slot -- the manager would be
//     null and PublishFullStateSnapshot()'s inventory loop would silently
//     skip.
//   - T42 (HC_OP_ItemPacket/CharInventory dispatch dropped): even with the
//     manager constructed, the CharInventory replay below wouldn't reach it,
//     so the slot this test targets would stay empty.
// The other two would NOT have been caught here, and that's expected, not a
// gap in this test -- they need different oracles:
//   - T29 (no slash commands registered): a command-availability check, not
//     a zone-in state-population check.
//   - T42 (SetEQClientPath() never called): only bites over a REAL network
//     connection (LoadZoneSources() calling RequestQuit() on empty client
//     path) -- ReplayZonePacket bypasses networking entirely, so this test
//     cannot exercise it. That is exactly why the plan's verification bar
//     separately requires a live eqemu EQT_GRAPHICS=OFF acceptance leg.
// ============================================================================

TEST(FullStateSnapshot, HeadlessZoneInPopulatesGameState) {
    RecordingBridge bridge;
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.setBridge(&bridge);
    const uint16_t kMySpawnId = 6001;
    const uint32_t kPetSpawnId = 9101;
    const uint32_t kMemorizedSpellId = 42;   // arbitrary nonzero spell id -- gem storage
                                              // doesn't require a loaded spell database
    const uint32_t kItemId = 10001;
    const uint32_t kItemIcon = 500;
    const std::string kItemName = "T43 Test Sword";

    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            p.PutCString(66, "qeytoqrg");
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    // PlayerProfile: identity, HP/mana (player stats), and one memorized
    // spell gem (spellbook/mem_spells offsets per eq_zone_entry.cpp's
    // ZoneProcessPlayerProfile comment: spell_book at 2312, mem_spells at
    // 4360, 8 x uint32 each).
    {
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);            // race
            p.PutUInt32(2 + 12, 1);           // class
            p.PutUInt8(2 + 20, 10);           // level
            p.PutUInt32(2 + 2232, 100);       // cur_hp
            p.PutUInt32(2 + 2228, 50);        // mana
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId);
            p.PutUInt32(2 + 4360, kMemorizedSpellId);  // mem_spells[0] (gem slot 1)
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);

    // Pre-existing pet, already in the zone's spawn list (T41/T43 residual
    // fix: OnPetCreated() is un-guarded as of this unit, so the delta fires
    // here too, not just via the snapshot).
    {
        auto payload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
            WritePetSpawnStruct(p, 2, kPetSpawnId, "Testchars_pet", kMySpawnId);
        });
        Replay(eq, HC_OP_ZoneSpawns, payload);
    }
    ASSERT_EQ(eq.GetEntities().count(kPetSpawnId), 1u);

    // Inventory: one item at the primary weapon slot, via HC_OP_CharInventory
    // -- the same dispatch path T42 found dropped entirely under
    // EQT_GRAPHICS=OFF before its fix.
    {
        std::string itemStr = BuildItemString(eqt::inventory::SLOT_PRIMARY, kItemId, kItemName, kItemIcon);
        auto payload = BuildPayload(2 + itemStr.size(), [&](EQ::Net::DynamicPacket& p) {
            p.PutData(2, const_cast<char*>(itemStr.data()), itemStr.size());
        });
        Replay(eq, HC_OP_CharInventory, payload);
    }

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    Replay(eq, HC_OP_GuildMOTD, EmptyPayload());
    bridge.drainEvents();  // discard NetworkReady etc., not this test's concern

    // ClientUpdate completes the zone-in handshake -> OnGameStateComplete()
    // -> PublishFullStateSnapshot() (headless path, T41).
    {
        auto payload = BuildPayload(2 + 22, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kMySpawnId);
            p.PutUInt32(4, 0);
            p.PutUInt32(8, 0);
            p.PutUInt32(12, 0);
            p.PutUInt32(16, 0);
            p.PutUInt32(20, 0);
        });
        Replay(eq, HC_OP_ClientUpdate, payload);
    }
    ASSERT_TRUE(eq.IsGameStateReady());

    auto events = bridge.drainEvents();

    bool sawPlayerStats = false;
    bool sawInventoryItem = false;
    bool sawSpellGem = false;
    bool sawPetCreated = false;

    for (const auto& e : events) {
        switch (e.type) {
        case eqt::state::GameEventType::PlayerStatsChanged: {
            const auto& d = std::get<eqt::state::PlayerStatsChangedData>(e.data);
            if (d.curHP == 100 && d.maxHP == 100 && d.curMana == 50) {
                sawPlayerStats = true;
            }
            break;
        }
        case eqt::state::GameEventType::InventorySlotChanged: {
            const auto& d = std::get<eqt::state::InventorySlotChangedData>(e.data);
            if (d.slotId == eqt::inventory::SLOT_PRIMARY && d.hasItem && d.itemId == kItemId) {
                sawInventoryItem = true;
            }
            break;
        }
        case eqt::state::GameEventType::SpellGemChanged: {
            const auto& d = std::get<eqt::state::SpellGemChangedData>(e.data);
            if (d.spellId == kMemorizedSpellId) {
                sawSpellGem = true;
            }
            break;
        }
        case eqt::state::GameEventType::PetCreated: {
            const auto& d = std::get<eqt::state::PetCreatedData>(e.data);
            if (d.spawnId == kPetSpawnId) {
                sawPetCreated = true;
            }
            break;
        }
        default:
            break;
        }
    }

    EXPECT_TRUE(sawPlayerStats) << "PlayerStatsChanged with the PlayerProfile's HP/mana never reached the bridge";
    EXPECT_TRUE(sawInventoryItem) << "InventorySlotChanged for the CharInventory item never reached the bridge";
    EXPECT_TRUE(sawSpellGem) << "SpellGemChanged for the memorized spell never reached the bridge";
    EXPECT_TRUE(sawPetCreated) << "PetCreated for the pre-existing pet never reached the bridge";
}

// ============================================================================
// 4. T133: m_guild_id population from the player's own PlayerProfile.
//
// Before T133, EverQuest::m_guild_id was declared, read by every
// SendGuild*() sender's `if (m_guild_id == 0) return;` guard in
// eq_guild.cpp, and assigned nowhere -- so guildinvite/remove/demote/leader
// and guildmotd's set-path were permanently dead on the wire (T132's
// finding). ZoneProcessPlayerProfile already decoded a `guild_id` local at
// offset 13036 and wrote it into self_entity.guild_id (display-only, per-
// entity data) but never into m_guild_id.
//
// Two things this test suite has to pin down (see the plan's "two things
// that decide whether the minimal fix actually works"):
//   1. The player's own PlayerProfile sets GetGuildID() -- the fix works.
//   2. Another entity's ZoneSpawns/NewSpawn entry, which also carries a
//      guildID field (Spawn_Struct offset 238, decoded into per-entity
//      Entity::guild_id in eq_entities.cpp), must NOT leak into
//      m_guild_id -- that field describes the OTHER entity's guild, not
//      necessarily the player's, and conflating them would be a new bug,
//      not a fix. This is the "second half that would rot" the plan warns
//      about.
// ============================================================================

TEST(GuildIdPopulation, OwnPlayerProfile_PopulatesGuildId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_EQ(eq.GetGuildID(), 0u) << "guild id must start at 0 (guildless) before any PlayerProfile arrives";

    const uint32_t kGuildId = 17;
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2 + 8, 1);     // race
        p.PutUInt32(2 + 12, 1);    // class
        p.PutUInt8(2 + 20, 10);    // level
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 13036, kGuildId);   // guildID -- the field this unit wires up
        p.PutUInt32(2 + 14384, 5005);       // our own spawn id
    });
    Replay(eq, HC_OP_PlayerProfile, payload);

    EXPECT_EQ(eq.GetGuildID(), kGuildId)
        << "PlayerProfile's guildID (offset 13036) must reach m_guild_id, not just self_entity.guild_id";
}

TEST(GuildIdPopulation, GuildlessPlayerProfile_LeavesGuildIdZero) {
    // A guild id of 0 in the profile (the common case -- most characters
    // are guildless) must not be misread as "unset" vs "explicitly zero";
    // both look identical on the wire and both correctly leave the
    // SendGuild*() guards active.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, 5005);
        // guildID at offset 13036 left at the packet's default zero fill.
    });
    Replay(eq, HC_OP_PlayerProfile, payload);
    EXPECT_EQ(eq.GetGuildID(), 0u);
}

TEST(GuildIdPopulation, OtherEntitySpawn_DoesNotPopulateOwnGuildId) {
    // The second half the plan calls out as "the one that would rot": a
    // ZoneSpawns entry for a DIFFERENT spawn (some other player/NPC, not
    // us) carries its own guildID at Spawn_Struct offset 238. That value
    // belongs to Entity::guild_id for THAT spawn (ZoneProcessZoneSpawns,
    // eq_entities.cpp) --
    // it must never be mistaken for, or copied into, the player's own
    // m_guild_id.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Establish our own spawn id first (via a minimal PlayerProfile, guild
    // id left at 0 -- guildless), the same way a real zone-in sequences it.
    const uint16_t kMySpawnId = 5005;
    auto profilePayload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
    });
    Replay(eq, HC_OP_PlayerProfile, profilePayload);
    ASSERT_EQ(eq.GetGuildID(), 0u);

    // Now a ZoneSpawns entry for a DIFFERENT spawn, guilded.
    const uint16_t kOtherSpawnId = 4242;
    auto spawnPayload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
        size_t off = 2;
        p.PutCString(off + 7, "a_guilded_rat");
        p.PutUInt32(off + 340, kOtherSpawnId);
        p.PutUInt8(off + 151, 10);
        p.PutUInt8(off + 86, 100);
        p.PutUInt8(off + 83, 1);
        p.PutFloat(off + 75, 5.0f);
        p.PutUInt32(off + 238, 99);  // guildID -- belongs to the OTHER entity
    });
    Replay(eq, HC_OP_ZoneSpawns, spawnPayload);

    ASSERT_EQ(eq.GetEntities().count(kOtherSpawnId), 1u);
    EXPECT_EQ(eq.GetEntities().at(kOtherSpawnId).guild_id, 99u)
        << "the other entity's own guild_id field should still be recorded (display data)";
    EXPECT_EQ(eq.GetGuildID(), 0u)
        << "another entity's guildID must NOT leak into the player's own m_guild_id";
}

// ----------------------------------------------------------------------------
// Mid-session join/leave: the plan's scope-boundary question. Checked against
// the eqemu server source (zone/guild.cpp): every Client::SetGuild() call --
// which is what a guild invite accept, kick, or leave triggers server-side --
// sends OP_SpawnAppearance(type=AppearanceType::GuildID) to the WHOLE ZONE
// (whole_zone=true), including self. So the wire mechanism for mid-session
// updates DOES exist; before T133, ZoneProcessSpawnAppearance's switch had no
// AT_GUILD_ID case, so the packet arrived and was silently dropped (logged
// as "Unhandled SpawnAppearance type"). This is a small addition to an
// EXISTING handler, not a new packet handler, so it's covered in this unit
// per the plan's "small" carve-out.
// ----------------------------------------------------------------------------

std::vector<uint8_t> SpawnAppearancePayload(uint16_t spawnId, uint16_t type, uint32_t parameter) {
    return BuildPayload(2 + 8, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt16(2, spawnId);
        p.PutUInt16(4, type);
        p.PutUInt32(6, parameter);
    });
}

TEST(GuildIdPopulation, MidSessionSpawnAppearance_UpdatesOwnGuildId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Establish our own spawn id, guildless, as at a normal zone-in.
    const uint16_t kMySpawnId = 5005;
    auto profilePayload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
    });
    Replay(eq, HC_OP_PlayerProfile, profilePayload);
    ASSERT_EQ(eq.GetGuildID(), 0u);

    // Server-driven mid-session join (e.g. accepting a guild invite):
    // OP_SpawnAppearance(spawn_id=us, type=AT_GUILD_ID, parameter=new id).
    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kMySpawnId, AT_GUILD_ID, 42));
    EXPECT_EQ(eq.GetGuildID(), 42u)
        << "a mid-session AT_GUILD_ID SpawnAppearance for our own spawn must update m_guild_id";

    // Server-driven mid-session leave/kick: parameter is GUILD_NONE
    // (0xFFFFFFFF, common/guilds.h on the server), which must normalize to
    // 0 -- not latch as a bogus nonzero id that would defeat the
    // `m_guild_id == 0` guildless guards.
    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kMySpawnId, AT_GUILD_ID, 0xFFFFFFFFu));
    EXPECT_EQ(eq.GetGuildID(), 0u)
        << "GUILD_NONE (0xFFFFFFFF) on leave must normalize to 0, not latch as a nonzero guild id";
}

TEST(GuildIdPopulation, MidSessionSpawnAppearance_OtherEntity_DoesNotAffectOwnGuildId) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    const uint16_t kMySpawnId = 5005;
    auto profilePayload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
    });
    Replay(eq, HC_OP_PlayerProfile, profilePayload);

    // AT_GUILD_ID for a DIFFERENT spawn (another player in the zone joining
    // some guild) must not touch our own m_guild_id.
    const uint16_t kOtherSpawnId = 4242;
    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kOtherSpawnId, AT_GUILD_ID, 7));
    EXPECT_EQ(eq.GetGuildID(), 0u);
}


// ============================================================================
// 5. T148: m_gm_mode population from the player's own PlayerProfile.
//
// PlayerProfile.gm sits at offset 13054 -- 18 bytes past the guild_id at
// 13036 that suite 4 above covers, with pvp@13052 and anon@13053 in between.
// eqemu's own header comments the field `// 0=no, 1=yes (guessing!)`, so it
// was captured off the wire across three logins with `#set gm on|off`
// toggled between them BEFORE any of this was written --
// review/T148/playerprofile_gm_offset_annotated.txt has the annotated bytes.
//
// What the flag means matters as much as where it lives: its only
// server-side writer is Client::SetGM(bool), so it is "GM MODE WAS ON AT
// LOGIN", not "this account has GM privileges". Account status never
// reaches a Titanium client at all. The read is deliberately once-per-login;
// the AT_GM live-update path was explicitly declined (Will, 2026-08-19), and
// GMModeIsLoginTimeOnly_NoMidSessionRefresh below PINS that decision so a
// future reader does not "fix" it by accident.
// ============================================================================

TEST(GMModeGate, OwnPlayerProfile_GMFlagSet_PopulatesGMMode) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_FALSE(eq.GetGMMode()) << "GM mode must start false before any PlayerProfile arrives";

    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt8(2 + 13054, 1);       // gm -- the field this unit wires up
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, payload);

    EXPECT_TRUE(eq.GetGMMode())
        << "PlayerProfile.gm (offset 13054) must reach m_gm_mode";
}

TEST(GMModeGate, OwnPlayerProfile_GMFlagClear_LeavesGMModeFalse) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, 5005);
        // gm at offset 13054 left at the packet's default zero fill.
    });
    Replay(eq, HC_OP_PlayerProfile, payload);
    EXPECT_FALSE(eq.GetGMMode());
}

TEST(GMModeGate, NeighbouringPvpAndAnonBytesDoNotBleedIntoGMMode) {
    // The captured region is pvp@13052, anon@13053, gm@13054, guildrank@13055.
    // An off-by-one in either direction would make one of these look like the
    // GM flag. Set every neighbour and leave gm itself clear.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 13036, 42);     // guild_id
        p.PutUInt8(2 + 13052, 1);       // pvp
        p.PutUInt8(2 + 13053, 2);       // anon (2 = roleplay)
        p.PutUInt8(2 + 13055, 2);       // guildrank
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, payload);

    EXPECT_FALSE(eq.GetGMMode())
        << "an off-by-one at the parse site would read pvp, anon or guildrank as the GM flag";
    EXPECT_EQ(eq.GetGuildID(), 42u) << "guild_id must still parse from the same region";
}

TEST(GMModeGate, GMModeIsLoginTimeOnly_NoMidSessionRefresh) {
    // PINS Will's decision of 2026-08-19. An AT_GM SpawnAppearance for our
    // OWN spawn -- exactly the packet an AT_GM live-update path would have
    // consumed, and exactly what T133 DID wire up for AT_GUILD_ID -- must
    // NOT change GetGMMode(). A GM who toggles mid-session relogs. If this
    // test ever fails because someone "fixed" the staleness, read
    // refactor/T148_gm_status_gate.md before changing the test.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    const uint16_t kMySpawnId = 5005;
    auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, kMySpawnId);
        // gm left clear -- logged in with GM mode OFF.
    });
    Replay(eq, HC_OP_PlayerProfile, payload);
    ASSERT_FALSE(eq.GetGMMode());

    Replay(eq, HC_OP_SpawnAppearance, SpawnAppearancePayload(kMySpawnId, AT_GM, 1));
    EXPECT_FALSE(eq.GetGMMode())
        << "GM mode is checked ONCE AT LOGIN by design; AT_GM must not refresh it mid-session";
}

TEST(GMModeGate, SecondPlayerProfileRefreshesGMMode) {
    // The flip side of the staleness decision: a relog (or any subsequent
    // PlayerProfile) DOES pick up the new value. That is the sanctioned way
    // for a GM to refresh, so it has to actually work.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto off = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, off);
    ASSERT_FALSE(eq.GetGMMode());

    auto on = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(2 + 20, 10);
        p.PutCString(2 + 12940, "Testchar");
        p.PutUInt8(2 + 13054, 1);
        p.PutUInt32(2 + 14384, 5005);
    });
    Replay(eq, HC_OP_PlayerProfile, on);
    EXPECT_TRUE(eq.GetGMMode());
}

// ============================================================================
// T260: the zone-load snapshot's player BSP region.
//
// CreateZoneLoadSnapshot() resolves the player's start position against
// the game-side BSP tree that LoadZoneSources() built from the new zone's
// own WLD. Real zone data (befallen.s3d / arena.s3d) is required; the
// expected region indices below were measured with an out-of-process probe
// that mirrors LoadZoneSources' main-WLD selection and the snapshot's
// findRegionIndexForPoint(m_x, m_y, m_z) call (see
// refactor/T260_bsp_region_size_max.md Review).
// ============================================================================

namespace {

std::string EqClientDataPath() {
    const char* envPath = std::getenv("EQ_CLIENT_PATH");
    return envPath ? std::string(envPath) : std::string(EQ_CLIENT_DATA_PATH);
}

bool FileReadable(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// NewZone with a real short name: ZoneProcessNewZone reads it at packet
// offset 66 and runs the per-zone loaders, including LoadZoneSources().
void SendNamedNewZone(EverQuest& eq, const char* zoneName, uint16_t zoneId) {
    auto payload = BuildPayload(2 + 700, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(66, zoneName);
        p.PutUInt16(686, zoneId);
    });
    Replay(eq, HC_OP_NewZone, payload);
}

// Testchar's documented restore coordinate in befallen (zone 36) -- the
// coordinate P15 recorded a "pre-existing BSP-FATAL" at. Probe: region 2220.
constexpr float kBefallenStartX = -77.9232f;
constexpr float kBefallenStartY = 21.9086f;
constexpr float kBefallenStartZ = 3.0f;
constexpr size_t kBefallenStartRegion = 2220;
// arena (0,0,0): probe region 93. The same point against befallen's tree
// is region 2216 -- distinct, so a wrong-tree lookup is detectable.
constexpr size_t kArenaOriginRegion = 93;
constexpr size_t kBefallenOriginRegion = 2216;

} // namespace

TEST(ZoneLoadSnapshot, PlayerRegionResolvedAfterZoneIn) {
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d")) {
        GTEST_SKIP() << "befallen.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);

    SendNamedNewZone(eq, "befallen", 36);
    ASSERT_EQ(eq.GetCurrentZoneName(), "befallen");
    eq.SetPosition(kBefallenStartX, kBefallenStartY, kBefallenStartZ);

    const auto snap = eq.CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "befallen");
    EXPECT_NE(snap.playerBspRegion, SIZE_MAX)
        << "start position must resolve against the zone's own freshly loaded tree";
    EXPECT_EQ(snap.playerBspRegion, kBefallenStartRegion);
}

TEST(ZoneLoadSnapshot, TwoZoneSequenceResolvesAgainstSecondZoneTree) {
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d") || !FileReadable(dataPath + "/arena.s3d")) {
        GTEST_SKIP() << "befallen.s3d / arena.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);

    SendNamedNewZone(eq, "befallen", 36);
    eq.SetPosition(kBefallenStartX, kBefallenStartY, kBefallenStartZ);
    ASSERT_EQ(eq.CreateZoneLoadSnapshot().playerBspRegion, kBefallenStartRegion);

    // Re-zone: the next NewZone rebuilds the game-side tree for arena.
    SendNamedNewZone(eq, "arena", 77);
    ASSERT_EQ(eq.GetCurrentZoneName(), "arena");
    eq.SetPosition(0.0f, 0.0f, 0.0f);

    const auto snap = eq.CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "arena");
    EXPECT_NE(snap.playerBspRegion, SIZE_MAX);
    EXPECT_EQ(snap.playerBspRegion, kArenaOriginRegion)
        << "region must come from arena's tree (befallen's tree would give "
        << kBefallenOriginRegion << ")";
}

TEST(ZoneLoadSnapshot, LateFinalizationTreeForPreviousZoneIsRejected) {
    // Application's graphicsCompleteReady finalization hands the renderer's
    // tree for the JUST-COMPLETED load back to EverQuest. If a fast re-zone
    // has already received the next zone's NewZone by then, that tree is the
    // previous zone's and must not displace the tree LoadZoneSources built
    // for the current zone (P15's "SIZE_MAX with zone=<source>" FATAL was
    // exactly this pairing: qeynos2's position against befallen's tree).
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d") || !FileReadable(dataPath + "/arena.s3d")) {
        GTEST_SKIP() << "befallen.s3d / arena.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);

    SendNamedNewZone(eq, "arena", 77);
    eq.SetPosition(0.0f, 0.0f, 0.0f);
    ASSERT_EQ(eq.CreateZoneLoadSnapshot().playerBspRegion, kArenaOriginRegion);

    // Build befallen's tree the way the renderer would have for the previous
    // load, and deliver it as the (late) finalization for that zone.
    EQT::Graphics::WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(dataPath + "/befallen.s3d", "befallen.wld"));
    auto befallenTree = loader.getBspTree();
    ASSERT_TRUE(befallenTree);
    ASSERT_EQ(befallenTree->findRegionIndexForPoint(0.0f, 0.0f, 0.0f), kBefallenOriginRegion);

    eq.SetZoneBspTree(befallenTree, "befallen");

    const auto snap = eq.CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "arena");
    EXPECT_NE(snap.playerBspRegion, kBefallenOriginRegion)
        << "snapshot resolved the arena position against befallen's tree";
    EXPECT_EQ(snap.playerBspRegion, kArenaOriginRegion)
        << "the current zone's own tree must survive a stale finalization";
}

TEST(ZoneLoadSnapshot, MatchingFinalizationTreeIsInstalled) {
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/arena.s3d")) {
        GTEST_SKIP() << "arena.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);
    SendNamedNewZone(eq, "arena", 77);
    eq.SetPosition(0.0f, 0.0f, 0.0f);

    EQT::Graphics::WldLoader loader;
    ASSERT_TRUE(loader.parseFromArchive(dataPath + "/arena.s3d", "arena.wld"));
    auto arenaTree = loader.getBspTree();
    ASSERT_TRUE(arenaTree);
    eq.SetZoneBspTree(arenaTree, "arena");

    EXPECT_EQ(eq.CreateZoneLoadSnapshot().playerBspRegion, kArenaOriginRegion);
}

TEST(ZoneLoadSnapshot, OutOfTreeStartPositionIsNotFatal) {
    // Legitimate start positions can fall outside every BSP region (P44's
    // befallen zone-in at x=-195,y=-982 probes SIZE_MAX against befallen's
    // own tree). Nothing consumes playerBspRegion, so SIZE_MAX is a
    // diagnostic, never an exit.
    const std::string dataPath = EqClientDataPath();
    if (!FileReadable(dataPath + "/befallen.s3d")) {
        GTEST_SKIP() << "befallen.s3d not found under " << dataPath;
    }
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    eq.SetEQClientPath(dataPath);
    SendNamedNewZone(eq, "befallen", 36);
    eq.SetPosition(-195.0f, -982.0f, 3.0f);

    const auto snap = eq.CreateZoneLoadSnapshot();
    EXPECT_EQ(snap.zoneName, "befallen");
    EXPECT_EQ(snap.playerBspRegion, SIZE_MAX);
    EXPECT_FALSE(eq.IsQuitRequested());
}
