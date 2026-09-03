// Batch R (R02): session replay harness / game-state regression oracle.
//
// Two modes:
//  1. Synthetic (ALWAYS RUNS): builds a minimal in-memory packet sequence
//     using the same offsets the real ZoneProcess* handlers parse, feeds it
//     through EverQuest::ReplayZonePacket (the replay/test seam declared in
//     client/eq.h), then asserts resulting game state via public accessors.
//     No network, no EQ client files, no graphics.
//  2. Golden (activates only when a real capture exists): replays every
//     Zone-stream S->C record from a recorded session log
//     (data/captures/session.sleq, overridable via WILLEQ_SESSION_LOG),
//     builds a deterministic state summary, and compares it against
//     data/captures/session.golden. If the golden file doesn't exist yet,
//     it is written and the test GTEST_SKIPs with instructions to re-run.
//
// See refactor/R02_replay_harness.md for the design/review notes.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>

#include "client/eq.h"
#include "common/net/packet.h"
#include "common/net/session_log.h"

namespace {

// Builds the payload bytes (packet minus the 2-byte opcode prefix) for a
// synthetic Zone S->C packet. `totalPacketSize` includes the opcode prefix
// so callers can write field offsets exactly as documented in the
// ZoneProcess* handlers (which read at those same packet offsets).
std::vector<uint8_t> BuildPayload(size_t totalPacketSize,
                                   const std::function<void(EQ::Net::DynamicPacket&)>& fill) {
    EQ::Net::DynamicPacket p;
    p.Resize(totalPacketSize);
    fill(p);
    std::vector<uint8_t> payload(totalPacketSize - 2);
    std::memcpy(payload.data(), static_cast<const uint8_t*>(p.Data()) + 2, payload.size());
    return payload;
}

void Replay(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayZonePacket(opcode, payload.data(), payload.size());
}

} // namespace

// ============================================================================
// Synthetic mode -- always passes, no external dependencies.
// ============================================================================

TEST(SessionReplaySynthetic, DrivesGameStateThroughPublicAccessors) {
    EverQuest eq("127.0.0.1", 0, "test", "test", "test", "Testchar");

    // Sanity: constructed headless, no live zone connection.
    ASSERT_EQ(eq.Net().ReplaySuppressedSendCount(), 0u);

    // --- NewZone: sets current zone name/id, triggers AA/tribute request
    // sends (all suppressed since there's no live m_zone_connection). ---
    {
        auto payload = BuildPayload(2 + 700, [](EQ::Net::DynamicPacket& p) {
            // zone_short_name at packet offset 66 (2 opcode + 64 char_name).
            p.PutCString(66, "qeytoqrg");
            // zone_id at packet offset 686 (2 + 684).
            p.PutUInt16(686, 100);
        });
        Replay(eq, HC_OP_NewZone, payload);
    }

    EXPECT_EQ(eq.GetCurrentZoneName(), "qeytoqrg");
    // ZoneProcessNewZone unconditionally sends SendAATable/UpdateAA/
    // SendTributes/RequestGuildTributes the first time through; all four
    // must have been suppressed (null m_zone_connection) rather than crash.
    EXPECT_EQ(eq.Net().ReplaySuppressedSendCount(), 4u);

    // --- PlayerProfile: our own stats, position, spawn id. ---
    const uint16_t kMySpawnId = 1001;
    {
        // ZoneProcessPlayerProfile reads every field at packet offset
        // `base + structOffset` with base=2 (2-byte opcode prefix), so each
        // write below is structOffset+2 to land exactly where the handler reads.
        auto payload = BuildPayload(2 + 19588, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2 + 8, 1);       // race
            p.PutUInt32(2 + 12, 1);      // class
            p.PutUInt8(2 + 20, 25);      // level
            p.PutUInt32(2 + 2232, 250);  // cur_hp
            p.PutUInt32(2 + 2228, 0);    // mana
            p.PutUInt32(2 + 2236, 75);   // STR
            p.PutUInt32(2 + 2240, 75);   // STA
            p.PutUInt32(2 + 2244, 75);   // CHA
            p.PutUInt32(2 + 2248, 75);   // DEX
            p.PutUInt32(2 + 2252, 75);   // INT
            p.PutUInt32(2 + 2256, 75);   // AGI
            p.PutUInt32(2 + 2260, 75);   // WIS
            p.PutUInt32(2 + 4428, 10);   // platinum
            // Position: server_x/server_y are swapped into m_x/m_y by the handler.
            p.PutFloat(2 + 13116, 1600.0f); // server_x -> m_y
            p.PutFloat(2 + 13120, 800.0f);  // server_y -> m_x
            p.PutFloat(2 + 13124, 80.0f);   // z
            p.PutFloat(2 + 13128, 0.0f);    // heading (raw 512-scale)
            p.PutCString(2 + 12940, "Testchar");
            p.PutUInt32(2 + 14384, kMySpawnId); // entity_id -> m_my_spawn_id
        });
        Replay(eq, HC_OP_PlayerProfile, payload);
    }

    EXPECT_EQ(eq.GetLevel(), 25);
    EXPECT_EQ(eq.GetMaxHP(), 250u);
    EXPECT_EQ(eq.GetCurrentHP(), 250u);
    EXPECT_FLOAT_EQ(eq.GetPosition().x, 800.0f);
    EXPECT_FLOAT_EQ(eq.GetPosition().y, 1600.0f);

    ASSERT_EQ(eq.GetEntities().count(kMySpawnId), 1u);
    EXPECT_EQ(eq.GetEntities().at(kMySpawnId).name, "Testchar");
    EXPECT_EQ(eq.GetEntities().at(kMySpawnId).level, 25);

    // --- ZoneSpawns: one NPC entity, parsed via the Spawn_Struct offsets. ---
    const uint16_t kRatSpawnId = 2002;
    {
        auto payload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
            size_t off = 2; // Spawn_Struct starts right after the opcode.
            p.PutCString(off + 7, "a_rat");
            p.PutUInt32(off + 340, kRatSpawnId);
            p.PutUInt8(off + 151, 5);     // level
            p.PutUInt8(off + 86, 100);    // hp_percent
            p.PutUInt8(off + 83, 1);      // npc_type = npc
            p.PutFloat(off + 75, 4.0f);   // size
            // Position bitfields: x_raw=1600 (->entity.y), y_raw=800 (->entity.x), z_raw=80.
            uint32_t field1 = (1600u & 0x7FFFF) << 10;
            uint32_t field2 = (800u & 0x7FFFF);
            uint32_t field3 = (80u & 0x7FFFF);
            uint32_t field4 = (0u & 0x7FF) << 13; // heading raw = 0
            p.PutUInt32(off + 94, field1);
            p.PutUInt32(off + 98, field2);
            p.PutUInt32(off + 102, field3);
            p.PutUInt32(off + 106, field4);
        });
        Replay(eq, HC_OP_ZoneSpawns, payload);
    }

    ASSERT_EQ(eq.GetEntities().count(kRatSpawnId), 1u);
    {
        const Entity& rat = eq.GetEntities().at(kRatSpawnId);
        EXPECT_EQ(rat.name, "a_rat");
        EXPECT_EQ(rat.level, 5);
        EXPECT_FLOAT_EQ(rat.x, 100.0f);  // server_y/8 = 800/8
        EXPECT_FLOAT_EQ(rat.y, 200.0f);  // server_x/8 = 1600/8
        EXPECT_FLOAT_EQ(rat.z, 10.0f);   // z_raw/8 = 80/8
    }

    // --- NewSpawn: a second entity via the single-spawn path. ---
    const uint16_t kSnakeSpawnId = 3003;
    {
        auto payload = BuildPayload(2 + 385, [&](EQ::Net::DynamicPacket& p) {
            size_t off = 2;
            p.PutCString(off + 7, "a_snake");
            p.PutUInt32(off + 340, kSnakeSpawnId);
            p.PutUInt8(off + 151, 8);
            p.PutUInt8(off + 86, 100);
            p.PutUInt8(off + 83, 1);
            p.PutFloat(off + 75, 3.0f);
            uint32_t field1 = (0u & 0x7FFFF) << 10;
            uint32_t field2 = (0u & 0x7FFFF);
            uint32_t field3 = (0u & 0x7FFFF);
            uint32_t field4 = (0u & 0x7FF) << 13;
            p.PutUInt32(off + 94, field1);
            p.PutUInt32(off + 98, field2);
            p.PutUInt32(off + 102, field3);
            p.PutUInt32(off + 106, field4);
        });
        Replay(eq, HC_OP_NewSpawn, payload);
    }

    ASSERT_EQ(eq.GetEntities().count(kSnakeSpawnId), 1u);
    EXPECT_EQ(eq.GetEntities().at(kSnakeSpawnId).name, "a_snake");
    EXPECT_EQ(eq.GetEntities().at(kSnakeSpawnId).level, 8);

    // --- TimeOfDay ---
    {
        auto payload = BuildPayload(2 + 6, [](EQ::Net::DynamicPacket& p) {
            p.PutUInt8(2, 14);   // hour
            p.PutUInt8(3, 30);   // minute
            p.PutUInt8(4, 5);    // day
            p.PutUInt8(5, 3);    // month
            p.PutUInt16(6, 3100); // year
        });
        Replay(eq, HC_OP_TimeOfDay, payload);
    }

    uint8_t hour = 0, minute = 0;
    eq.GetTimeOfDay(hour, minute);
    EXPECT_EQ(hour, 14);
    EXPECT_EQ(minute, 30);

    // --- HPUpdate for our own spawn. ---
    {
        auto payload = BuildPayload(2 + 10, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt32(2, 123);            // cur_hp
            p.PutInt32(6, 250);              // max_hp
            p.PutUInt16(10, kMySpawnId);     // spawn_id
        });
        Replay(eq, HC_OP_HPUpdate, payload);
    }

    EXPECT_EQ(eq.GetCurrentHP(), 123u);
    EXPECT_EQ(eq.GetMaxHP(), 250u);

    // --- DeleteSpawn removes the snake entity. ---
    {
        auto payload = BuildPayload(2 + 2, [&](EQ::Net::DynamicPacket& p) {
            p.PutUInt16(2, kSnakeSpawnId);
        });
        Replay(eq, HC_OP_DeleteSpawn, payload);
    }

    EXPECT_EQ(eq.GetEntities().count(kSnakeSpawnId), 0u);
    // The rat and our own spawn are untouched by the delete.
    EXPECT_EQ(eq.GetEntities().count(kRatSpawnId), 1u);
    EXPECT_EQ(eq.GetEntities().count(kMySpawnId), 1u);
}

// ============================================================================
// Golden mode -- only runs when a real capture is present.
// ============================================================================

namespace {

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

std::string DefaultSessionLogPath() {
    if (const char* env = std::getenv("WILLEQ_SESSION_LOG")) {
        return env;
    }
    // T36: resolve against the repo root, not the process CWD. ctest runs
    // this from <build>/tests (gtest_discover_tests' default WORKING_
    // DIRECTORY), where "data/captures/..." does not exist -- so the golden
    // replay silently skipped in every fresh build dir, indistinguishable
    // from the legitimate "no capture recorded" skip.
    return std::string(WILLEQ_REPO_ROOT) + "/data/captures/session.sleq";
}

std::string GoldenPathFor(const std::string& sessionLogPath) {
    // Sibling ".golden" file next to the session log, mirroring the
    // data/captures/session.sleq -> data/captures/session.golden precedent
    // from the unit plan.
    auto dot = sessionLogPath.find_last_of('.');
    if (dot == std::string::npos) {
        return sessionLogPath + ".golden";
    }
    return sessionLogPath.substr(0, dot) + ".golden";
}

// Deterministic text summary of replay-visible game state: zone, player
// stats, and a sorted entity table. Sorted so record ordering / hash-map
// iteration order can't make the golden comparison flaky.
std::string SummarizeState(EverQuest& eq) {
    std::ostringstream out;
    out << "zone=" << eq.GetCurrentZoneName() << "\n";
    out << "level=" << static_cast<int>(eq.GetLevel())
        << " hp=" << eq.GetCurrentHP() << "/" << eq.GetMaxHP()
        << " mana=" << eq.GetCurrentMana() << "/" << eq.GetMaxMana() << "\n";
    uint8_t hour = 0, minute = 0;
    eq.GetTimeOfDay(hour, minute);
    out << "time=" << static_cast<int>(hour) << ":" << static_cast<int>(minute) << "\n";

    std::vector<uint16_t> ids;
    for (const auto& kv : eq.GetEntities()) {
        ids.push_back(kv.first);
    }
    std::sort(ids.begin(), ids.end());
    out << "entities=" << ids.size() << "\n";
    for (uint16_t id : ids) {
        const Entity& e = eq.GetEntities().at(id);
        out << "  [" << id << "] " << e.name << " level=" << static_cast<int>(e.level)
            << " pos=(" << e.x << "," << e.y << "," << e.z << ")\n";
    }
    return out.str();
}

} // namespace

TEST(SessionReplayGolden, MatchesRecordedSessionOrCreatesBaseline) {
    const std::string sessionLogPath = DefaultSessionLogPath();

    std::ifstream logFile(sessionLogPath, std::ios::binary);
    if (!logFile.is_open()) {
        GTEST_SKIP() << "No session capture at " << sessionLogPath
                     << " (set WILLEQ_SESSION_LOG to override) -- skipping golden replay.";
    }

    EQT::Net::SessionLogFileHeader header{};
    logFile.read(reinterpret_cast<char*>(&header), sizeof(header));
    ASSERT_TRUE(logFile.good()) << "Failed to read session log header from " << sessionLogPath;
    ASSERT_EQ(header.magic, EQT::Net::SESSION_LOG_MAGIC) << "Not a session log file: " << sessionLogPath;
    ASSERT_EQ(header.version, EQT::Net::SESSION_LOG_VERSION) << "Unsupported session log version";

    EverQuest eq("127.0.0.1", 0, "test", "test", "test",
                 header.character[0] ? header.character : "Testchar");

    size_t recordCount = 0;
    while (logFile.good()) {
        EQT::Net::SessionLogRecordHeader rec{};
        logFile.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!logFile.good()) {
            break;
        }
        std::vector<uint8_t> payload(rec.size);
        if (rec.size > 0) {
            logFile.read(reinterpret_cast<char*>(payload.data()), rec.size);
            ASSERT_TRUE(logFile.good()) << "Truncated record " << recordCount << " in " << sessionLogPath;
        }

        if (rec.stream == static_cast<uint8_t>(EQT::Net::SessionStream::Zone) &&
            rec.direction == static_cast<uint8_t>(EQT::Net::SessionDirection::ServerToClient)) {
            eq.ReplayZonePacket(rec.opcode, payload.data(), payload.size());
        }
        ++recordCount;
    }

    ASSERT_GT(recordCount, 0u) << "Session log " << sessionLogPath << " contained no records";

    std::string actual = SummarizeState(eq);
    const std::string goldenPath = GoldenPathFor(sessionLogPath);

    std::ifstream goldenIn(goldenPath);
    if (!goldenIn.is_open()) {
        std::ofstream goldenOut(goldenPath);
        ASSERT_TRUE(goldenOut.is_open()) << "Could not create golden file at " << goldenPath;
        goldenOut << actual;
        goldenOut.close();
        GTEST_SKIP() << "Golden created at " << goldenPath << " -- re-run to verify.";
    }

    std::ostringstream goldenContent;
    goldenContent << goldenIn.rdbuf();
    EXPECT_EQ(actual, goldenContent.str())
        << "Replay of " << sessionLogPath << " no longer matches golden state at " << goldenPath;
}
