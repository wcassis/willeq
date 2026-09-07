// T04 -- zone-request phase gating, dispatcher robustness, and the
// observable state a completed handshake leaves behind.
//
// T446 split test_zone_connection_flow.cpp (1,367 lines) by functional
// group; the packet builders and the RecordingBridge fake live in
// zone_connection_flow_test_fixture.h, whose header comment carries the
// unit's scope notes. Same target, same suites, same case names.

#include "zone_connection_flow_test_fixture.h"

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
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);
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
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);
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
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 4u) << "ReqClientSpawn must not have been sent";
}

TEST(ZoneRequestPhaseGating, WithholdingTributeInfo_DoesNotAdvance) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4
    SendAATable(eq);
    SendGuildTributes(eq);

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 4u);
}

TEST(ZoneRequestPhaseGating, WithholdingGuildTributes_DoesNotAdvance) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4
    SendAATable(eq);
    SendTributeInfo(eq);

    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 4u);
}

TEST(ZoneRequestPhaseGating, AllFourPresent_AdvancesExactlyOnce) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);          // suppressed += 4
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);    // last gate satisfied here -> fires ReqClientSpawn (+1)

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u);
}

TEST(ZoneRequestPhaseGating, FireOnceGuard_AdditionalPacketsDoNotRefire) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    SendNewZone(eq);
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    ASSERT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u);
    ASSERT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);

    // Keep feeding more of the same packets: m_req_client_spawn_sent guards
    // against a second ZoneSendReqClientSpawn.
    SendAATable(eq);
    SendTributeInfo(eq);
    SendGuildTributes(eq);
    SendRespondAA(eq);

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u)
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
        EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u);
    }
    {
        EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
        SendNewZone(eq);
        SendTributeInfo(eq);
        SendGuildTributes(eq);
        SendAATable(eq);
        EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
        EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u);
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
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);

    SendNewZone(eq); // suppressed += 4 (AA/UpdateAA/tributes/guild-tributes)
                      // and, now, += 1 for ReqClientSpawn.

    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE)
        << "NewZone arriving last must complete the zone-request phase";
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u)
        << "ReqClientSpawn must be sent once all four prerequisites are met";

    // Idempotence: a later gate packet re-runs the check but must not send
    // ReqClientSpawn a second time (m_req_client_spawn_sent guard).
    SendRespondAA(eq);
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 5u);
}

TEST(ZoneRequestPhaseGating, NewZoneFirst_CheckFromNewZoneIsANoOp) {
    // The check added to ZoneProcessNewZone runs while the three counters
    // are still zero; it must neither advance the phase nor send anything
    // beyond NewZone's own four requests.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    SendNewZone(eq);
    EXPECT_NE(eq.GetLoadingPhase(), LoadingPhase::ZONE_REQUEST_PHASE);
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 4u);
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
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);
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
// src/client/zone/zone_entry.cpp) so a truncated packet -- exactly what a lossy
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
