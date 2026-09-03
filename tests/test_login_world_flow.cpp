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
    eq.LoginWorld().ReplayLoginPacket(opcode, payload.data(), payload.size());
}

void ReplayWorld(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.LoginWorld().ReplayWorldPacket(opcode, payload.data(), payload.size());
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

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

std::string DefaultSessionLogPath() {
    if (const char* env = std::getenv("WILLEQ_SESSION_LOG")) {
        return env;
    }
    // T36: repo-root-relative -- see tests/CMakeLists.txt's WILLEQ_REPO_ROOT
    // note for why a CWD-relative default silently skipped under ctest.
    return std::string(WILLEQ_REPO_ROOT) + "/data/captures/session.sleq";
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
            ASSERT_NO_THROW(eq.LoginWorld().ReplayLoginPacket(rec.opcode, payload.data(), payload.size()))
                << "Login record #" << loginRecords << " (opcode " << rec.opcode
                << ", " << payload.size() << " bytes) threw during replay";
            ++loginRecords;
            if (rec.opcode == HC_OP_ChatMessage) {
                sawLoginAuthenticating = (eq.GetLoadingPhase() == LoadingPhase::LOGIN_AUTHENTICATING);
            }
        } else if (isServerToClient && rec.stream == static_cast<uint8_t>(EQT::Net::SessionStream::World)) {
            ASSERT_NO_THROW(eq.LoginWorld().ReplayWorldPacket(rec.opcode, payload.data(), payload.size()))
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
// it (EverQuest::ConnectToLogin, eq.cpp), and m_login_connection is set synchronously inside
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

// ============================================================================
// 4. T09: eleven read-only login/world accessors + the state assertions they
// unlock. See refactor/T09_login_world_accessors.md. Real values below (dbid,
// key, server list, character slot, zone host/port, chat-server string) are
// decoded directly from data/captures/session.sleq (the same P29 recording
// used by section 1's real-capture replay), verified independently with a
// scratch DES-CBC decrypt (all-zero key/IV, matching eqcrypt_block) before
// being pinned here -- not guessed.
// ============================================================================

namespace {

// Locates the first S->C record on the given stream/opcode in the session
// capture. Used by the accessor tests below, each of which only needs one
// representative record rather than section 1's full sequential walk.
// Returns false (leaving outPayload untouched) if the capture is absent or
// no matching record exists -- callers GTEST_SKIP in that case, same as
// section 1.
bool FindFirstCaptureRecord(EQT::Net::SessionStream stream, uint16_t opcode,
                             std::vector<uint8_t>& outPayload) {
    std::ifstream logFile(DefaultSessionLogPath(), std::ios::binary);
    if (!logFile.is_open()) {
        return false;
    }
    EQT::Net::SessionLogFileHeader header{};
    logFile.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!logFile.good() || header.magic != EQT::Net::SESSION_LOG_MAGIC) {
        return false;
    }
    while (logFile.good()) {
        EQT::Net::SessionLogRecordHeader rec{};
        logFile.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!logFile.good()) {
            break;
        }
        std::vector<uint8_t> payload(rec.size);
        if (rec.size > 0) {
            logFile.read(reinterpret_cast<char*>(payload.data()), rec.size);
            if (!logFile.good()) {
                break;
            }
        }
        if (rec.stream == static_cast<uint8_t>(stream) &&
            rec.direction == static_cast<uint8_t>(EQT::Net::SessionDirection::ServerToClient) &&
            rec.opcode == opcode) {
            outPayload = std::move(payload);
            return true;
        }
    }
    return false;
}

// Builds a Titanium CharacterSelect_Struct payload (1706-byte full packet,
// matching WorldProcessCharacterSelect's >=1706 guard) with a single named
// character in slot 0. Used for the not-found accessor case; the real
// capture (used for the found case) already exercises the true field
// layout.
std::vector<uint8_t> BuildCharacterSelectPayload(const std::string& slot0Name) {
    return BuildPayload(2 + 1704, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2 + 1024, slot0Name.c_str());  // names[0] at payload offset 1024
    });
}

// T14: a synthetic slot with every field the handler decodes, written at the
// documented Titanium offsets (names 1024, levels 1694, classes 1004,
// races 0, zones 964; +2 for the opcode).
struct SyntheticCharSlot {
    int slot;
    std::string name;
    uint8_t level;
    uint8_t char_class;
    uint32_t race;
    uint32_t zone;
};

std::vector<uint8_t> BuildCharacterSelectPayload(const std::vector<SyntheticCharSlot>& slots) {
    return BuildPayload(2 + 1704, [&](EQ::Net::DynamicPacket& p) {
        for (const auto& s : slots) {
            p.PutCString(2 + 1024 + s.slot * 64, s.name.c_str());
            p.PutUInt8(2 + 1694 + s.slot, s.level);
            p.PutUInt8(2 + 1004 + s.slot, s.char_class);
            p.PutUInt32(2 + s.slot * 4, s.race);
            p.PutUInt32(2 + 964 + s.slot * 4, s.zone);
        }
    });
}

}  // namespace

TEST(LoginWorldAccessors, LoginAccepted_RealCapture_ParsesAccountIdAndSessionKey) {
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::Login, HC_OP_LoginAccepted, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture LoginAccepted accessor test.";
    }

    EverQuest eq("127.0.0.1", 0, "test", "test", "WillEQ", "Testchar");
    EXPECT_EQ(eq.LoginWorld().GetAccountId(), 0u);
    EXPECT_TRUE(eq.LoginWorld().GetSessionKey().empty());

    ASSERT_NO_THROW(ReplayLogin(eq, HC_OP_LoginAccepted, payload));

    // Real capture values: response_error=101 (the <=101 "success" branch),
    // dbid=1, key="BW8BZW8UW4". Comparing GetSessionKey() to a known literal
    // here (not printing/logging it) is the extent of this unit's use of the
    // new accessor, per the plan's note on m_key being a live credential.
    EXPECT_EQ(eq.LoginWorld().GetAccountId(), 1u);
    EXPECT_EQ(eq.LoginWorld().GetSessionKey(), "BW8BZW8UW4");
}

TEST(LoginWorldAccessors, ServerListResponse_RealCapture_PopulatesWorldServers) {
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::Login, HC_OP_ServerListResponse, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture ServerListResponse accessor test.";
    }

    // "WillEQ" matches the recorded server's long_name (same fixture fact
    // section 1 documents), so this replays the found/LoginSendPlayRequest
    // branch rather than LoginDisableReconnect's not-found branch -- either
    // is safe post-T07, but this keeps the test on the branch real replay
    // actually takes.
    EverQuest eq("127.0.0.1", 0, "test", "test", "WillEQ", "Testchar");

    ASSERT_NO_THROW(ReplayLogin(eq, HC_OP_ServerListResponse, payload));

    const auto& servers = eq.LoginWorld().GetWorldServers();
    ASSERT_EQ(servers.size(), 1u);
    ASSERT_NE(servers.find(2u), servers.end()) << "expected world-server id 2 (real capture)";
    const auto& ws = servers.at(2u);
    EXPECT_EQ(ws.long_name, "WillEQ");
    EXPECT_EQ(ws.address, "10.0.30.23");
    EXPECT_EQ(ws.lang, "us");
    EXPECT_EQ(ws.region, "en");
}

TEST(LoginWorldAccessors, ServerListResponse_NoMatchingServer_StillPopulatesWorldServers) {
    // T06 finding: the "not found" branch calls LoginDisableReconnect(),
    // guarded since T07, so this is a safe no-crash path. GetWorldServers()
    // is populated BEFORE the match loop runs, so it holds the entry
    // regardless of whether m_server matched -- this is the "server-list
    // contents" visibility the plan authorizes (in contrast to the OUT
    // character-list contents, which are never stored at all).
    EverQuest eq("127.0.0.1", 0, "test", "test", "NoSuchServer", "Testchar");
    auto payload = BuildServerListResponsePayload("127.0.0.1", "TestServer", "us", "en");

    EXPECT_NO_THROW(ReplayLogin(eq, HC_OP_ServerListResponse, payload));

    const auto& servers = eq.LoginWorld().GetWorldServers();
    ASSERT_EQ(servers.size(), 1u);
    ASSERT_NE(servers.find(42u), servers.end());
    EXPECT_EQ(servers.at(42u).long_name, "TestServer");
    EXPECT_EQ(servers.at(42u).address, "127.0.0.1");
}

TEST(LoginWorldAccessors, SendCharInfo_RealCapture_FindsConfiguredCharacter) {
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::World, HC_OP_SendCharInfo, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture SendCharInfo accessor test.";
    }

    // Real capture slot layout (P29 recording): 0="Testcaster", 1="Testchar",
    // 2="Testmelee", slots 3-9 empty. "Testchar" is this test's configured
    // character, so the expected match index is 1.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectIndex(), -1);

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));

    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectIndex(), 1);
}

TEST(LoginWorldAccessors, SendCharInfo_CharacterNotInList_LeavesIndexAtNegativeOne) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildCharacterSelectPayload("SomeoneElse");

    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));

    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectIndex(), -1);
    // T14: the non-matching character still occupies a slot, so it must
    // appear in the stored list even though the index stayed unset.
    ASSERT_EQ(eq.LoginWorld().GetCharacterSelectList().size(), 1u);
    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectList()[0].name, "SomeoneElse");
}

TEST(LoginWorldAccessors, SendCharInfo_RealCapture_PopulatesCharacterList) {
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::World, HC_OP_SendCharInfo, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture character-list test.";
    }

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_TRUE(eq.LoginWorld().GetCharacterSelectList().empty());

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));

    // Real capture slot layout (P29 recording): 0="Testcaster",
    // 1="Testchar", 2="Testmelee"; slots 3-9 carry eqemu's "<none>"
    // empty-slot sentinel and must be filtered out. Numeric values pinned
    // from a discovery run against the capture: class 12=Wizard/1=Warrior,
    // race 6=High Elf/1=Human, zones 25/36.
    const auto& list = eq.LoginWorld().GetCharacterSelectList();
    ASSERT_EQ(list.size(), 3u);
    EXPECT_EQ(list[0].slot, 0);
    EXPECT_EQ(list[0].name, "Testcaster");
    EXPECT_EQ(list[0].level, 1);
    EXPECT_EQ(list[0].char_class, 12);
    EXPECT_EQ(list[0].race, 6u);
    EXPECT_EQ(list[0].zone, 25u);
    EXPECT_EQ(list[1].slot, 1);
    EXPECT_EQ(list[1].name, "Testchar");
    EXPECT_EQ(list[1].level, 1);
    EXPECT_EQ(list[1].char_class, 1);
    EXPECT_EQ(list[1].race, 1u);
    EXPECT_EQ(list[1].zone, 36u);
    EXPECT_EQ(list[2].slot, 2);
    EXPECT_EQ(list[2].name, "Testmelee");
    EXPECT_EQ(list[2].level, 1);
    EXPECT_EQ(list[2].char_class, 1);
    EXPECT_EQ(list[2].race, 6u);
    EXPECT_EQ(list[2].zone, 25u);
}

TEST(LoginWorldAccessors, SendCharInfo_SyntheticSlots_DecodesAllFieldsAndSkipsEmpty) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    // Configured character in slot 0, a second character in slot 2: the old
    // handler returned at the slot-0 match, so slot 2 also asserts that later
    // slots are now parsed. Slot 1 is empty (all-zero names bytes) and slot 3
    // carries eqemu's "<none>" empty-slot sentinel; both must be skipped.
    auto payload = BuildCharacterSelectPayload(std::vector<SyntheticCharSlot>{
        {0, "Testchar", 12, 14, 128, 75},
        {2, "Otherguy", 60, 1, 2, 8},
        {3, "<none>", 0, 0, 0, 0},
    });

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));

    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectIndex(), 0);
    const auto& list = eq.LoginWorld().GetCharacterSelectList();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].slot, 0);
    EXPECT_EQ(list[0].name, "Testchar");
    EXPECT_EQ(list[0].level, 12);
    EXPECT_EQ(list[0].char_class, 14);
    EXPECT_EQ(list[0].race, 128u);
    EXPECT_EQ(list[0].zone, 75u);
    EXPECT_EQ(list[1].slot, 2);
    EXPECT_EQ(list[1].name, "Otherguy");
    EXPECT_EQ(list[1].level, 60);
    EXPECT_EQ(list[1].char_class, 1);
    EXPECT_EQ(list[1].race, 2u);
    EXPECT_EQ(list[1].zone, 8u);
}

TEST(LoginWorldAccessors, SendCharInfo_RedeliveryRebuildsListInsteadOfAppending) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    auto payload = BuildCharacterSelectPayload(std::vector<SyntheticCharSlot>{
        {0, "Testchar", 12, 14, 128, 75},
    });

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));
    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SendCharInfo, payload));

    EXPECT_EQ(eq.LoginWorld().GetCharacterSelectList().size(), 1u);
}

TEST(LoginWorldAccessors, ZoneServerInfo_RealCapture_ParsesHostAndPort) {
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::World, HC_OP_ZoneServerInfo, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture ZoneServerInfo accessor test.";
    }

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_TRUE(eq.GetZoneServerHost().empty());
    EXPECT_EQ(eq.GetZoneServerPort(), 0);

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_ZoneServerInfo, payload));

    // Real capture values (first ZoneServerInfo record, record index 14).
    EXPECT_EQ(eq.GetZoneServerHost(), "10.0.30.23");
    EXPECT_EQ(eq.GetZoneServerPort(), 7012);
}

// --- WorldProcessSetChatServer: FIXED in T13. ---
//
// T09 found that the handler required >=5 comma-separated fields and read
// the mail key from parts[4], so it never populated anything against a real
// server. T13 settled it from the eqemu source
// (akk-stack/code/world/client.cpp), which formats
// "{},{},{}.{},{}{:08X}" -- FOUR fields, era-independent:
//
//     host , port , shortname.charname , <connection_type><mail_key:08X>
//
// The connection type is one character concatenated onto the 8-hex-digit
// mail key with no separator. Titanium uses 'B' for chat
// (OP_SetChatServer) and 'b' for mail (OP_SetChatServer2), per
// emu_versions.h ucsTitaniumChat / ucsTitaniumMail. So the client was
// wrong, not the server, and there is no 5-field shape to contrast
// against -- the old contrast test asserted a format nothing produces.
TEST(LoginWorldAccessors, SetChatServer_RealCapture_PopulatesHostPortTypeAndMailKey) {
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::World, HC_OP_SetChatServer, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture SetChatServer accessor test.";
    }

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SetChatServer, payload));

    // Recorded payload:
    //   "10.0.30.23,7778,WillEQ Private Server.Testchar,B48FB5F37"
    EXPECT_EQ(eq.LoginWorld().GetChatServerHost(), "10.0.30.23");
    EXPECT_EQ(eq.LoginWorld().GetChatServerPort(), 7778);
    EXPECT_EQ(eq.LoginWorld().GetChatConnectionType(), 'B');  // ucsTitaniumChat
    EXPECT_EQ(eq.LoginWorld().GetMailKey(), "48FB5F37");      // 8 hex digits, type char stripped
}

TEST(LoginWorldAccessors, SetChatServer2_RealCapture_UsesLowercaseMailConnectionType) {
    // The world server sends the same string twice with a different type
    // character: 'B' on OP_SetChatServer and 'b' on OP_SetChatServer2. That
    // case difference is the chat-vs-mail discriminator, which is why the
    // type is now stored rather than discarded.
    std::vector<uint8_t> payload;
    if (!FindFirstCaptureRecord(EQT::Net::SessionStream::World, HC_OP_SetChatServer2, payload)) {
        GTEST_SKIP() << "No session capture at " << DefaultSessionLogPath()
                     << " -- skipping real-capture SetChatServer2 accessor test.";
    }

    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SetChatServer2, payload));

    EXPECT_EQ(eq.LoginWorld().GetChatConnectionType(), 'b');  // ucsTitaniumMail
    EXPECT_EQ(eq.LoginWorld().GetMailKey(), "48FB5F37");
}

TEST(LoginWorldAccessors, SetChatServer_FourFieldSynthetic_StripsTypeCharacterFromMailKey) {
    // Synthetic case in the real 4-field shape, with values chosen so a
    // regression that returned the whole field (or the wrong substring)
    // would be visible.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    const std::string chat_info = "chat.example.com,9876,TestServer.Testchar,XDEADBEEF";
    auto payload = BuildPayload(2 + chat_info.size() + 1, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, chat_info.c_str());
    });

    ASSERT_NO_THROW(ReplayWorld(eq, HC_OP_SetChatServer, payload));

    EXPECT_EQ(eq.LoginWorld().GetChatServerHost(), "chat.example.com");
    EXPECT_EQ(eq.LoginWorld().GetChatServerPort(), 9876);
    EXPECT_EQ(eq.LoginWorld().GetChatConnectionType(), 'X');
    EXPECT_EQ(eq.LoginWorld().GetMailKey(), "DEADBEEF");
}

TEST(LoginWorldAccessors, SetChatServer_MalformedInputsLeaveStateUntouched) {
    // T13 added guards for four shapes that previously either warned-and-
    // ignored, threw out of the network callback, or silently truncated.
    // None may crash, and none may half-populate.
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    auto replay = [&](const std::string& s) {
        auto payload = BuildPayload(2 + s.size() + 1, [&](EQ::Net::DynamicPacket& p) {
            p.PutCString(2, s.c_str());
        });
        ReplayWorld(eq, HC_OP_SetChatServer, payload);
    };

    // Too few fields.
    EXPECT_NO_THROW(replay("host,1234,only.three"));
    // Empty type/mail-key field -- substr(1) would have thrown.
    EXPECT_NO_THROW(replay("host,1234,server.char,"));
    // Non-numeric port -- std::stoi would have thrown out of the callback.
    EXPECT_NO_THROW(replay("host,notaport,server.char,Xkey"));
    // In-int-range but out-of-uint16-range port -- stoi succeeds, so this
    // would have silently truncated to 4464 without the range check.
    EXPECT_NO_THROW(replay("host,70000,server.char,Xkey"));

    EXPECT_TRUE(eq.LoginWorld().GetChatServerHost().empty());
    EXPECT_EQ(eq.LoginWorld().GetChatServerPort(), 0);
    EXPECT_EQ(eq.LoginWorld().GetChatConnectionType(), 0);
    EXPECT_TRUE(eq.LoginWorld().GetMailKey().empty());
}

TEST(LoginWorldAccessors, ExpansionInfo_FirstDelivery_SetsWorldReadyAndEnterWorldSent) {
    // WorldSendWorldClientReady() sets m_world_ready=true;
    // WorldSendEnterWorld() sets m_enter_world_sent=true. Both fire on the
    // first ExpansionInfo delivery per WorldProcessExpansionInfo's
    // !m_world_ready guard (already pinned via send-count deltas in section
    // 2's WorldExpansionInfo_FirstDelivery_TriggersThreeSends -- this test
    // observes the same transition directly via the new accessors instead).
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");
    EXPECT_FALSE(eq.LoginWorld().IsWorldReady());
    EXPECT_FALSE(eq.LoginWorld().HasSentEnterWorld());

    auto payload = BuildPayload(2 + 4, [](EQ::Net::DynamicPacket& p) {
        p.PutUInt32(2, 0x1);  // expansions bitmask
    });
    EXPECT_NO_THROW(ReplayWorld(eq, HC_OP_ExpansionInfo, payload));

    EXPECT_TRUE(eq.LoginWorld().IsWorldReady());
    EXPECT_TRUE(eq.LoginWorld().HasSentEnterWorld());
}
