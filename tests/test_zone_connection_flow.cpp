// T04: zone connection-flow sequencing tests.
//
// Targets CheckZoneRequestPhaseComplete (eq.cpp:2724) and the surrounding
// zone-stream dispatcher (DispatchZonePacket, eq.cpp:749), driven entirely
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
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>

#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
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
// read any packet fields (eq_player_info.cpp:38-81); they only log, bump a
// counter (except RespondAA), and call CheckZoneRequestPhaseComplete().
void SendNewZone(EverQuest& eq) {
    // Zero-length payload: ZoneProcessNewZone guards every field read behind
    // p.Length() thresholds (eq.cpp:1854 onward), so an empty payload safely
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
// confirmed via eq_entities.cpp:360-361/1202-1203, the same offsets
// ZoneProcessSpawnAll/NewSpawn read.
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

} // namespace

// ============================================================================
// 1. Zone-request phase gating -- the core of the unit.
//
// CheckZoneRequestPhaseComplete() fires ZoneSendReqClientSpawn() exactly
// once, and only when m_new_zone_received && m_aa_table_count>0 &&
// m_tribute_count>0 && m_guild_tribute_count>0 && !m_req_client_spawn_sent.
//
// IMPORTANT (deviation from the plan's description, confirmed by reading
// eq.cpp/eq_player_info.cpp): the check is invoked ONLY from the four
// handlers ZoneProcessSendAATable, ZoneProcessRespondAA,
// ZoneProcessTributeInfo, ZoneProcessSendGuildTributes
// (eq_player_info.cpp:54,62,71,80). ZoneProcessNewZone (eq.cpp:1849-1974)
// sets m_new_zone_received = true but never calls
// CheckZoneRequestPhaseComplete() itself. This has a real consequence for
// ordering, tested below in NewZoneArrivingLast_DoesNotFireUntilAnotherGateArrives.
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
    // increment the AA counter (eq_player_info.cpp:57-63), so it cannot
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
    // guild tributes relative to each other, as long as NewZone precedes
    // them (see the NewZoneArrivingLast case below for what happens when it
    // doesn't). Try two different permutations of the trailing three.
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

TEST(ZoneRequestPhaseGating, NewZoneArrivingLast_DoesNotFireUntilAnotherGateArrives) {
    // Real finding (see file header comment): CheckZoneRequestPhaseComplete
    // is called only from the AA-table/RespondAA/tribute-info/guild-tribute
    // handlers, never from ZoneProcessNewZone. So if NewZone is the LAST of
    // the four to arrive, the four prerequisites are all simultaneously true
    // in memory, but nothing re-runs the check -- the phase never advances,
    // and the client would hang waiting for ReqClientSpawn to be sent. This
    // is a live risk: the plan text notes "the server does not promise an
    // order", and this ordering is exactly the one the gating logic doesn't
    // handle. Flagged for Will; not fixed here (tests/ territory only).
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    SendNewZone(eq); // suppressed += 4; all four prerequisites now true, but
                      // NewZone's handler never calls the check.

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE)
        << "This assertion documents the current (buggy) behavior: the "
           "phase does NOT advance even though all four prerequisites have "
           "arrived, because NewZone arrived last and its handler doesn't "
           "call CheckZoneRequestPhaseComplete().";
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 4u)
        << "ReqClientSpawn was never sent despite all prerequisites being met";

    // Recovery path: any subsequent packet that DOES call the check (e.g. a
    // duplicate/late AA table packet) will notice all four are now true and
    // fire. In production this means the client only recovers if the server
    // sends a *fifth* gate-relevant packet after the late NewZone -- not
    // guaranteed.
    SendRespondAA(eq);
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), 5u);
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
    EXPECT_NO_THROW(Replay(eq, HC_OP_DeleteSpawn, EmptyPayload()));     // eq_entities.cpp:1598 (needs 4)
    EXPECT_NO_THROW(Replay(eq, HC_OP_ZoneSpawns, EmptyPayload()));      // loop condition offset+385<=Length()
    EXPECT_NO_THROW(Replay(eq, HC_OP_NewSpawn, EmptyPayload()));        // eq_entities.cpp:1035 (needs 387)
    EXPECT_NO_THROW(Replay(eq, HC_OP_NewZone, EmptyPayload()));         // eq.cpp:1854 (needs 96)
    EXPECT_NO_THROW(Replay(eq, HC_OP_SendAATable, EmptyPayload()));     // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_RespondAA, EmptyPayload()));       // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_TributeInfo, EmptyPayload()));     // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_SendGuildTributes, EmptyPayload())); // reads nothing
    EXPECT_NO_THROW(Replay(eq, HC_OP_ClientUpdate, EmptyPayload()));    // eq_entities.cpp:1323 (needs 24)
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
// added length guards to both handlers (src/client/eq_entities.cpp:484,
// src/client/eq.cpp:1999) so a truncated packet -- exactly what a lossy
// network produces -- now returns safely instead of throwing.
//
// These tests were pinned as BUG_*_ThrowsOutOfRange in T04; T05 flips them to
// assert the fixed (return-safely, no state change) behavior. See the Review
// section of refactor/T05_login_world_seam.md.

TEST(DispatcherRobustness, TimeOfDay_ZeroLengthPayload_ReturnsSafely) {
    // ZoneProcessTimeOfDay (eq_entities.cpp:484) now guards p.Length() < 8
    // before reading GetUInt8(2)/GetUInt8(3)/GetUInt8(4)/GetUInt8(5)/
    // GetUInt16(6). A zero-length (or any <8-byte) TimeOfDay payload no
    // longer throws, and time-of-day state is left unchanged.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_NO_THROW(Replay(eq, HC_OP_TimeOfDay, EmptyPayload()));
}

TEST(DispatcherRobustness, PlayerProfile_TruncatedPayload_ReturnsSafely) {
    // ZoneProcessPlayerProfile (eq.cpp:1986) logs a LOG_WARN when
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
    // (eq.cpp:687) -- unreachable through the replay seam. This is a known
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
