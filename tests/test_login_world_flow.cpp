// T06: login/world flow coverage -- the thing T05's replay seam
// (ReplayLoginPacket/ReplayWorldPacket -> DispatchLoginPacket/
// DispatchWorldPacket) was built for. See refactor/T06_login_world_flow.md.
//
// The assertion vocabulary here is deliberately thin: there are NO public
// accessors for login/world state (no character list, no server list, no
// m_dbid, no m_key, no world-ready flag -- all private). What's observable
// through the seam is GetLoadingPhase() (two reachable transitions plus a
// third discovered by this unit -- see below), GetReplaySuppressedSendCount()
// deltas, and absence of crash/exception. So this suite is protocol
// sequencing and robustness, not deep state assertions. See this file's
// closing comment block and the plan's Review section for the precise list
// of behaviors that could not be asserted for lack of an accessor.

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
#include "common/net/packet.h"
#include "common/net/session_log.h"

namespace {

std::vector<uint8_t> EmptyPayload() {
    return {};
}

// Same idiom as test_session_replay.cpp's BuildPayload: build the full
// packet (2-byte opcode prefix + fields at the packet offsets the handlers
// actually read), then strip the prefix into a payload vector suitable for
// ReplayLoginPacket/ReplayWorldPacket (which re-add the 2-byte opcode
// themselves).
std::vector<uint8_t> BuildPayload(size_t totalPacketSize,
                                   const std::function<void(EQ::Net::DynamicPacket&)>& fill) {
    EQ::Net::DynamicPacket p;
    p.Resize(totalPacketSize);
    fill(p);
    std::vector<uint8_t> payload(totalPacketSize - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    return payload;
}

void ReplayLogin(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayLoginPacket(opcode, payload.data(), payload.size());
}

void ReplayWorld(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayWorldPacket(opcode, payload.data(), payload.size());
}

// Builds a Titanium ServerListResponse payload with exactly one server
// entry, whose long_name is `serverName`, at the offsets
// LoginProcessServerPacketList reads (eq_login_world.cpp:237): count at
// packet offset 18, entries starting at offset 22.
std::vector<uint8_t> BuildServerListResponsePayload(const std::string& address,
                                                      const std::string& serverName,
                                                      const std::string& lang,
                                                      const std::string& region) {
    size_t idx = 22;
    idx += address.size() + 1;
    idx += 4;  // type
    idx += 4;  // id
    idx += serverName.size() + 1;
    idx += lang.size() + 1;
    idx += region.size() + 1;
    idx += 4;  // status
    idx += 4;  // players
    size_t totalPacketSize = idx;

    return BuildPayload(totalPacketSize, [&](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(18, 1);  // number_of_servers
        size_t off = 22;
        p.PutCString(off, address.c_str());
        off += address.size() + 1;
        p.PutInt32(off, 1);  // type
        off += 4;
        p.PutUInt32(off, 42);  // id
        off += 4;
        p.PutCString(off, serverName.c_str());
        off += serverName.size() + 1;
        p.PutCString(off, lang.c_str());
        off += lang.size() + 1;
        p.PutCString(off, region.c_str());
        off += region.size() + 1;
        p.PutInt32(off, 0);  // status
        off += 4;
        p.PutInt32(off, 0);  // players
    });
}

} // namespace

// ============================================================================
// 1. Real-capture replay: the regression oracle that didn't exist before.
// ============================================================================

namespace {

std::string DefaultSessionLogPath() {
    if (const char* env = std::getenv("WILLEQ_SESSION_LOG")) {
        return env;
    }
    return "data/captures/session.sleq";
}

} // namespace

TEST(LoginWorldCaptureReplay, RealSessionReplaysLoginAndWorldStreamsWithoutCrash) {
    const std::string path = DefaultSessionLogPath();
    std::ifstream logFile(path, std::ios::binary);
    if (!logFile.is_open()) {
        GTEST_SKIP() << "No session capture at " << path
                     << " (set WILLEQ_SESSION_LOG to override) -- skipping real-capture login/world replay.";
    }

    EQT::Net::SessionLogFileHeader header{};
    logFile.read(reinterpret_cast<char*>(&header), sizeof(header));
    ASSERT_TRUE(logFile.good()) << "Failed to read session log header from " << path;
    ASSERT_EQ(header.magic, EQT::Net::SESSION_LOG_MAGIC) << "Not a session log file: " << path;
    ASSERT_EQ(header.version, EQT::Net::SESSION_LOG_VERSION) << "Unsupported session log version";

    // "WillEQ" is the recorded server's long_name (decoded directly from the
    // ServerListResponse record in session.sleq, P29 capture). Matching it
    // here matters: LoginProcessServerPacketList's "not found" branch calls
    // LoginDisableReconnect(), which unconditionally dereferences
    // m_login_connection_manager and m_login_connection -- both null in this
    // headless replay-only construction (no ConnectToLoginServer() was ever
    // called). That branch is a real finding of this unit; see the plan's
    // Review section. Using the real server name keeps this test on the
    // "found" (LoginSendPlayRequest) branch, which matches actual replay of
    // this fixture.
    EverQuest eq("127.0.0.1", 0, "test", "test", "WillEQ",
                 header.character[0] ? header.character : "Testchar");

    ASSERT_NE(eq.GetLoadingPhase(), LoadingPhase::LOGIN_AUTHENTICATING);
    ASSERT_NE(eq.GetLoadingPhase(), LoadingPhase::WORLD_CHARACTER_SELECT);

    size_t loginRecords = 0, worldRecords = 0;
    size_t recordIndex = 0;
    bool sawLoginAuthenticating = false;
    bool sawWorldCharacterSelect = false;

    while (logFile.good()) {
        EQT::Net::SessionLogRecordHeader rec{};
        logFile.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!logFile.good()) {
            break;
        }
        std::vector<uint8_t> payload(rec.size);
        if (rec.size > 0) {
            logFile.read(reinterpret_cast<char*>(payload.data()), rec.size);
            ASSERT_TRUE(logFile.good()) << "Truncated record " << recordIndex << " in " << path;
        }

        const bool isServerToClient =
            rec.direction == static_cast<uint8_t>(EQT::Net::SessionDirection::ServerToClient);

        // T07: HC_OP_PlayEverquestResponse used to be excluded here, because
        // LoginProcessServerPlayResponse reaches LoginDisableReconnect, which
        // dereferenced m_login_connection_manager/m_login_connection with no
        // null guard and SEGFAULTed under replay. Those guards now exist, so
        // the record replays like any other and the exclusion is gone — the
        // whole recorded login stream is now covered.

        if (isServerToClient && rec.stream == static_cast<uint8_t>(EQT::Net::SessionStream::Login)) {
            ASSERT_NO_THROW(eq.ReplayLoginPacket(rec.opcode, payload.data(), payload.size()))
                << "Login record #" << loginRecords << " (opcode " << rec.opcode
                << ", " << payload.size() << " bytes) threw during replay";
            ++loginRecords;
            if (rec.opcode == HC_OP_ChatMessage) {
                sawLoginAuthenticating = (eq.GetLoadingPhase() == LoadingPhase::LOGIN_AUTHENTICATING);
            }
        } else if (isServerToClient && rec.stream == static_cast<uint8_t>(EQT::Net::SessionStream::World)) {
            ASSERT_NO_THROW(eq.ReplayWorldPacket(rec.opcode, payload.data(), payload.size()))
                << "World record #" << worldRecords << " (opcode " << rec.opcode
                << ", " << payload.size() << " bytes) threw during replay";
            ++worldRecords;
            if (rec.opcode == HC_OP_SendCharInfo) {
                sawWorldCharacterSelect = (eq.GetLoadingPhase() == LoadingPhase::WORLD_CHARACTER_SELECT);
            }
        }
        // Zone-stream and C->S records are out of scope for this unit
        // (zone is covered by test_session_replay.cpp / test_zone_connection_flow.cpp;
        // C->S records were never recorded, see the note below).
        ++recordIndex;
    }

    // Verified by direct parse of session.sleq (P29 recording): the login
    // stream contains exactly 4 S->C application records (ChatMessage,
    // LoginAccepted, ServerListResponse, PlayEverquestResponse -- one each,
    // covering the entire DispatchLoginPacket switch), and the world stream
    // contains 22 (11 opcodes x 2 zone-ins).
    //
    // The T06 plan quoted "10 login, 22 world" with "six 0x0000 records";
    // the orchestrator then repeated that when reviewing T06 and wrongly
    // overrode this file's count. Settled in T07: SessionLogFileHeader is
    // **112 bytes** (static_assert, session_log.h), not the 16 the
    // orchestrator's ad-hoc parser assumed. Starting at offset 16 misreads
    // the header's zeroed character[64]/zone[32] tail as 96/16 = exactly
    // six phantom 0x0000 size-0 records. This count of 4 is correct; the
    // lesson is to use the session_log.h structs, which is what this file
    // does.
    //
    // T07: all 4 login records now replay -- PlayEverquestResponse is no
    // longer excluded, because LoginDisableReconnect's null guards landed.
    EXPECT_EQ(loginRecords, 4u);
    EXPECT_EQ(worldRecords, 22u);

    EXPECT_TRUE(sawLoginAuthenticating)
        << "Login ChatMessage record did not produce LoadingPhase::LOGIN_AUTHENTICATING";
    EXPECT_TRUE(sawWorldCharacterSelect)
        << "World SendCharInfo record did not produce LoadingPhase::WORLD_CHARACTER_SELECT "
        << "(the recorded payload is exactly 1704 bytes -- 1706 total with the opcode prefix, "
        << "exactly at WorldProcessCharacterSelect's >=1706 guard)";

    // The final ZoneServerInfo record (replayed twice, once per zone-in)
    // calls ConnectToZone(), which unconditionally sets ZONE_CONNECTING --
    // a THIRD real phase transition reachable from the world stream that
    // the plan's "only 2 transitions" table did not document. See Review.
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::ZONE_CONNECTING);

    // Coarse robustness signal: replaying a real handshake produces at least
    // one suppressed send (every C->S response the handlers issue is
    // suppressed, since no live connection exists in this headless replay).
    // Not pinning an exact total: too many contributing call sites (login
    // send-login/send-server-request/send-play-request, world
    // client-auth/approve/CRC-x2/expansion-ack/client-ready/enter-world/
    // world-complete x2) to keep a precise count meaningful/robust against
    // fixture changes; the per-opcode deltas are pinned individually in the
    // synthetic sequencing tests below instead.
    EXPECT_GT(eq.GetReplaySuppressedSendCount(), 0u);
}

// ============================================================================
// 2. Sequencing (synthetic): client responses to server packets, observed
//    via send-count deltas and phase transitions. HC_OP_ChatMessage on login
//    and HC_OP_ChatMessage/unknown-opcode-ignored on both streams are
//    already pinned by test_login_world_seam.cpp (T05); not duplicated here.
// ============================================================================

TEST(LoginWorldSequencing, ServerListResponse_MatchingServer_TriggersPlayRequest) {
    // LoginProcessServerPacketList's "found" branch calls LoginSendPlayRequest(),
    // which attempts one QueuePacket on the (null, in replay) m_login_connection
    // -- suppressed, not crashed.
    EverQuest eq("127.0.0.1", 0, "test", "test", "TestServer", "Testchar");

    auto payload = BuildServerListResponsePayload("127.0.0.1", "TestServer", "us", "en");
    auto before = eq.GetReplaySuppressedSendCount();

    EXPECT_NO_THROW(ReplayLogin(eq, HC_OP_ServerListResponse, payload));

    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before + 1);
}

TEST(LoginWorldSequencing, WorldSessionReady_TriggersClientAuth_SuppressedSend) {
    // DispatchWorldPacket's HC_OP_SessionReady case is a distinct switch
    // label from HC_OP_ChatMessage (T05 tests only the latter) but shares
    // the same handler, WorldSendClientAuth().
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto before = eq.GetReplaySuppressedSendCount();

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_SessionReady, EmptyPayload()));

    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before + 1);
}

TEST(LoginWorldSequencing, WorldApproveWorld_TriggersApproveAndCRCSends) {
    // WorldProcessApproveWorld sends ApproveWorld, then WorldClientCRC1 and
    // WorldClientCRC2 (WorldSendWorldClientCRC sends two packets) -- three
    // suppressed sends total. Doesn't read its packet argument at all, so
    // this is well-defined regardless of payload.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto before = eq.GetReplaySuppressedSendCount();

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_ApproveWorld, EmptyPayload()));

    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before + 3);
}

TEST(LoginWorldSequencing, WorldExpansionInfo_FirstDelivery_TriggersThreeSends) {
    // WorldProcessExpansionInfo (eq_login_world.cpp:678), on a fresh client
    // (m_world_ready == false, m_enter_world_sent == false), sends: an
    // AckPacket, then WorldClientReady (WorldSendWorldClientReady), then
    // EnterWorld (WorldSendEnterWorld, since !m_enter_world_sent) -- THREE
    // suppressed sends. The plan text says "two sends (an inline ack, then
    // WorldSendWorldClientReady)"; the actual observed count is three,
    // because m_enter_world_sent defaults false so the EnterWorld send also
    // fires on this path. Pinning the real observed behavior per the plan's
    // own instruction. See Review.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto payload = BuildPayload(2 + 4, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 0x1);  // expansions bitmask
    });
    auto before = eq.GetReplaySuppressedSendCount();

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_ExpansionInfo, payload));

    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), before + 3);
}

TEST(LoginWorldSequencing, WorldExpansionInfo_SecondDelivery_IsNoOp) {
    // Confirms the m_world_ready guard is meaningful: a second ExpansionInfo
    // delivery (as happens on the real capture's second zone-in) must not
    // resend the ack/ready/enter-world sequence. m_world_ready has no public
    // accessor, so this is the only way to observe the guard held -- via the
    // absence of additional suppressed sends.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto payload = BuildPayload(2 + 4, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 0x1);
    });

    ReplayWorld(eq, HC_OP_ExpansionInfo, payload);
    auto afterFirst = eq.GetReplaySuppressedSendCount();

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_ExpansionInfo, payload));

    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), afterFirst);
}

TEST(LoginWorldSequencing, UnknownOpcode_NonEmptyGarbagePayload_IgnoredSafely) {
    // T05 already pins the null/zero-size case for both streams
    // (LoginUnknownOpcode_IgnoredSafely, WorldUnknownOpcode_IgnoredSafely).
    // Extending with a non-trivial garbage payload, matching T04's zone-side
    // idiom of covering both the empty and non-empty case.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    std::vector<uint8_t> garbage(64, 0xAB);

    auto loginPhaseBefore = eq.GetLoadingPhase();
    auto loginSendsBefore = eq.GetReplaySuppressedSendCount();
    EXPECT_NO_THROW(ReplayLogin(eq, 0xFFFE, garbage));
    EXPECT_EQ(eq.GetLoadingPhase(), loginPhaseBefore);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), loginSendsBefore);

    auto worldPhaseBefore = eq.GetLoadingPhase();
    auto worldSendsBefore = eq.GetReplaySuppressedSendCount();
    EXPECT_NO_THROW(ReplayWorld(eq, 0xFFFE, garbage));
    EXPECT_EQ(eq.GetLoadingPhase(), worldPhaseBefore);
    EXPECT_EQ(eq.GetReplaySuppressedSendCount(), worldSendsBefore);
}

// ============================================================================
// 3. Robustness: T04 treatment (zero-length / truncated payloads) applied to
//    login and world handlers. Handlers with no explicit length guard throw
//    std::out_of_range from EQ::Net::Packet's bounds-checked accessors
//    (packet.cpp:200-278) -- uncaught anywhere between DispatchLoginPacket/
//    DispatchWorldPacket and the network receive callback in production,
//    same class of finding as T04's zone-side BUG_* tests. Pinned as
//    observed behavior, not fixed (territory is tests/ only).
// ============================================================================

TEST(LoginWorldRobustness, WorldGuildsListLogServerEnterWorldPostEnterWorldMOTD_ZeroLengthSafe) {
    // These handlers are log-only (no field reads from `p`), so any payload
    // length -- including zero -- is a safe no-op.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_GuildsList, EmptyPayload()));
    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_LogServer, EmptyPayload()));
    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_EnterWorld, EmptyPayload()));
    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_PostEnterWorld, EmptyPayload()));
    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_MOTD, EmptyPayload()));
}

TEST(LoginWorldRobustness, WorldProcessCharacterSelect_OneByteUnderThreshold_ReturnsSafely) {
    // Contrast case (the plan calls this out explicitly): WorldProcessCharacterSelect
    // (eq_login_world.cpp:529) sets WORLD_CHARACTER_SELECT unconditionally,
    // *then* guards p.Length() < 1706 before any field read -- so it warns
    // and returns safely rather than throwing. T05 already covers the
    // zero-length case; this covers the exact boundary (one byte short of
    // the guard, rather than empty).
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    std::vector<uint8_t> payload(1703, 0);  // p.Length() = 2 + 1703 = 1705, one under 1706.

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));
    EXPECT_EQ(eq.GetLoadingPhase(), LoadingPhase::WORLD_CHARACTER_SELECT);
}

TEST(LoginWorldRobustness, BUG_LoginProcessServerPacketList_ZeroLengthPacket_ThrowsOutOfRange) {
    // LoginProcessServerPacketList (eq_login_world.cpp:237) reads
    // number_of_servers via p.GetUInt32(18) with no preceding length guard.
    // A zero-length payload (packet length 2) is short of the required 22
    // bytes, so Packet::GetUInt32's bounds check throws.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_THROW(ReplayLogin(eq, HC_OP_ServerListResponse, EmptyPayload()), std::out_of_range);
}

TEST(LoginWorldRobustness, BUG_LoginProcessServerPlayResponse_ZeroLengthPacket_ThrowsOutOfRange) {
    // LoginProcessServerPlayResponse (eq_login_world.cpp:284) reads
    // p.GetUInt8(12) with no length guard; packet length 2 is short of the
    // required 13 bytes.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_THROW(ReplayLogin(eq, HC_OP_PlayEverquestResponse, EmptyPayload()), std::out_of_range);
}

TEST(LoginWorldRobustness, BUG_WorldProcessExpansionInfo_ZeroLengthPacket_ThrowsOutOfRange) {
    // WorldProcessExpansionInfo (eq_login_world.cpp:678) reads
    // p.GetUInt32(2) with no length guard; packet length 2 is short of the
    // required 6 bytes.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_THROW(ReplayWorld(eq, HC_OP_ExpansionInfo, EmptyPayload()), std::out_of_range);
}

TEST(LoginWorldRobustness, BUG_WorldProcessSetChatServer_ZeroLengthPacket_ThrowsOutOfRange) {
    // WorldProcessSetChatServer (eq_login_world.cpp:712, shared by
    // HC_OP_SetChatServer and HC_OP_SetChatServer2) reads p.GetCString(2)
    // with no length guard; packet length 2 is short of the required 3
    // bytes (GetCString needs at least offset+1).
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_THROW(ReplayWorld(eq, HC_OP_SetChatServer, EmptyPayload()), std::out_of_range);
    EXPECT_THROW(ReplayWorld(eq, HC_OP_SetChatServer2, EmptyPayload()), std::out_of_range);
}

TEST(LoginWorldRobustness, BUG_WorldProcessZoneServerInfo_ZeroLengthPacket_ThrowsOutOfRange) {
    // WorldProcessZoneServerInfo (eq_login_world.cpp:749) reads
    // p.GetCString(2) with no length guard, same shape as SetChatServer
    // above -- throws before ever reaching ConnectToZone().
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_THROW(ReplayWorld(eq, HC_OP_ZoneServerInfo, EmptyPayload()), std::out_of_range);
}

// --- LoginProcessLoginResponse (eq_login_world.cpp:210): the bug the plan
// specifically asked this unit to find. ---
//
// auto encrypt_size = p.Length() - 12;
//
// Packet::Length() returns size_t. For any packet shorter than 12 bytes,
// this subtraction underflows to roughly 1.8e19. The next few lines are:
//
//   if (encrypt_size % 8 > 0) encrypt_size = (encrypt_size / 8) * 8;
//   std::unique_ptr<char[]> decrypted(new char[encrypt_size]);
//   eqcrypt_block((char*)p.Data() + 12, encrypt_size, &decrypted[0], false);
//
// Verified in isolation (scratch probe, matching g++/libstdc++ semantics
// used by this build) before writing these tests: the unsigned subtraction
// itself is well-defined (modular wraparound, not UB for unsigned types);
// (18446744073709551606 % 8) == 6, so the round-down leaves
// encrypt_size == 18446744073709551600 (~1.8e19, still enormous). The
// subsequent `new char[encrypt_size]` throws std::bad_alloc -- it does NOT
// succeed, and it does NOT corrupt memory or read/write out of bounds
// before throwing; libstdc++'s array-new path checks the requested size
// against the allocator's capacity and throws cleanly. No sanitizer report,
// no crash. This is the plan's expected (not the "worse") outcome -- see
// the plan's Review section and this unit's final report for the explicit
// confirmation.
//
// DispatchLoginPacket has no try/catch around this call, so the exception
// propagates out through LoginOnPacketRecv (production) or
// ReplayLoginPacket (this test) uncaught. In production this would
// terminate the client (std::terminate, since nothing upstream catches
// std::bad_alloc either) from a single malformed/truncated packet on the
// login path, before any authentication -- worth escalating to Will on its
// own merits; not fixed here per the plan's territory restriction.

// T07 FIXED the underflow: LoginProcessLoginResponse now returns early when
// the packet is under the 12-byte header, and again when the decrypt-sized
// buffer is under the 13 bytes its reads require. These tests, which
// previously pinned the std::bad_alloc / std::out_of_range failure modes,
// now assert the guards hold.

TEST(LoginWorldRobustness, LoginProcessLoginResponse_ZeroLengthPacket_ReturnsSafely) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    // packet length = 2 (opcode only), well under the 12-byte minimum.
    EXPECT_NO_THROW(ReplayLogin(eq, HC_OP_LoginAccepted, EmptyPayload()));
}

TEST(LoginWorldRobustness, LoginProcessLoginResponse_OneByteUnderMinimum_ReturnsSafely) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    // payload size 9 -> packet length 11, one byte short of the 12-byte minimum.
    EXPECT_NO_THROW(ReplayLogin(eq, HC_OP_LoginAccepted, std::vector<uint8_t>(9, 0)));
}

TEST(LoginWorldRobustness, LoginProcessLoginResponse_ExactlyHeaderLength_ReturnsSafely) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    // payload 10 -> packet length 12: no underflow, but a zero-length
    // decrypted buffer. Previously threw std::out_of_range from the reads.
    EXPECT_NO_THROW(ReplayLogin(eq, HC_OP_LoginAccepted, std::vector<uint8_t>(10, 0)));
}

// --- LoginDisableReconnect() (eq_login_world.cpp:310): discovered while
// building the real-capture replay above, NOT one of the cases the plan
// asked for -- flagged with equal prominence per the plan's instruction to
// report anything worse than an allocation failure immediately and
// prominently. This is worse: a raw SIGSEGV, not a C++ exception, and
// reachable from realistic well-formed data (not a malformed/truncated
// packet). ---
//
// LoginProcessServerPlayResponse (eq_login_world.cpp:284) calls
// LoginDisableReconnect() UNCONDITIONALLY on both branches: allowed-and-
// server-found (the normal successful-login path) and not-allowed. Every
// other send site in this file (LoginSendLogin, LoginSendServerRequest,
// LoginSendPlayRequest, WorldSend*, ~9 sites) guards its connection
// dereference with `if (m_x) {...} else {++m_replay_suppressed_sends;}`.
// LoginDisableReconnect is the sole exception:
//
//   m_login_connection_manager->OnConnectionStateChange(...);
//   m_login_connection->Close();
//
// Neither is null-checked. On a headless EverQuest built via the
// replay-seam idiom (this whole test file, and T05's) -- i.e. one that
// never called the real Connect()/handshake sequence -- both are default-
// constructed null (unique_ptr/shared_ptr), so this segfaults. Confirmed
// with gdb: SIGSEGV inside std::function::operator= inside
// DaybreakConnectionManager::OnConnectionStateChange, called from
// LoginDisableReconnect, called from LoginProcessServerPlayResponse,
// called from DispatchLoginPacket/ReplayLoginPacket.
//
// In production this is very likely unreachable: m_login_connection_manager
// is only ever non-null once LoginOnPacketRecv's callback has been wired to
// it (eq.cpp:250-254), and m_login_connection is set synchronously inside
// Connect() before any packet could arrive on it -- so by the time
// DispatchLoginPacket runs via the real network path, both are already
// set. This appears to be a gap specific to replaying a login sequence
// through the T05 seam without the real Connect() setup, not a live
// reachable bug -- but it means "replay a real captured login sequence to
// its natural conclusion" is NOT achievable with the current seam, and the
// missing guard is a real inconsistency against the rest of this file's
// pattern (worth Will's judgment on whether to add it, matching every
// other send site). Not fixed here -- tests/ only.
//
// T07 FIXED this: LoginDisableReconnect now null-guards both members,
// matching every other connection use in the file. The death test that
// pinned the SIGSEGV is replaced by its inverse — the same packet that
// used to kill the process must now be handled cleanly. No death test
// remains in this suite.
TEST(LoginWorldRobustness, LoginProcessServerPlayResponse_NotAllowed_DoesNotCrash) {
    auto payload = BuildPayload(2 + 15, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt8(12, 0);     // allowed = false
        p.PutUInt16(13, 0);    // message id (unused by the former crash path)
    });

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_NO_THROW(ReplayLogin(eq, HC_OP_PlayEverquestResponse, payload));
}
