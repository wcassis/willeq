#pragma once

// T18: shared helpers for zone-stream tests (test_zone_entities.cpp,
// test_zone_chat.cpp). Follows the established replay-seam idiom from
// test_zone_connection_flow.cpp / test_login_world_flow.cpp: synthetic
// packets are built at the packet offsets the ZoneProcess* handlers read
// (opcode-inclusive), real-capture records come from the P29 session log.

#include "client/bridge/game_state_bridge.h"
#include "client/eq.h"
#include "common/net/packet.h"
#include "common/net/session_log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace zonetest {

inline std::string DefaultSessionLogPath() {
    if (const char* env = std::getenv("WILLEQ_SESSION_LOG")) {
        return env;
    }
    return "data/captures/session.sleq";
}

// Builds the payload bytes (packet minus the 2-byte opcode prefix) for a
// synthetic S->C packet. `totalPacketSize` includes the opcode prefix so
// callers write field offsets exactly as documented in the handlers.
inline std::vector<uint8_t> BuildPayload(
        size_t totalPacketSize,
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

inline void ReplayZone(EverQuest& eq, uint16_t opcode, const std::vector<uint8_t>& payload) {
    eq.ReplayZonePacket(opcode, payload.data(), payload.size());
}

// Walks every record on `stream` in capture order, invoking `fn(direction,
// opcode, payload)`. Returns false (without invoking fn) if the capture is
// absent or malformed -- callers GTEST_SKIP in that case.
inline bool ForEachCaptureRecord(
        EQT::Net::SessionStream stream,
        const std::function<void(EQT::Net::SessionDirection, uint16_t,
                                 const std::vector<uint8_t>&)>& fn) {
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
        if (rec.stream == static_cast<uint8_t>(stream)) {
            fn(static_cast<EQT::Net::SessionDirection>(rec.direction), rec.opcode, payload);
        }
    }
    return true;
}

// GameStateBridge is abstract (applyEvent is the renderer-side hook). Tests
// only need the queue side: pushEvent from the game thread, drainEvents to
// assert. applyEvent is never called in this usage.
class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

// T23: spawn-injection helpers promoted from test_zone_entities.cpp (T18)
// for reuse by test_combat_manager.cpp. Deliberately NOT deduplicated
// against test_zone_entities.cpp's own (anonymous-namespace, internal-
// linkage) copies -- those are untouched per this unit's territory rules.
// Same Spawn_Struct offsets/semantics as T18: base=2 for a single NewSpawn
// packet, INVERSEXY means entity.x <- server_y, entity.y <- server_x.

inline EverQuest MakeEq() {
    return EverQuest("127.0.0.1", 0, "test", "test", "test", "Testchar");
}

struct PosFields {
    uint32_t field1;  // deltaHeading | x<<10
    uint32_t field2;  // y | animation<<19
    uint32_t field3;  // z | deltaY<<19
    uint32_t field4;  // deltaX | heading<<13
};

inline PosFields EncodePos(int32_t server_x, int32_t server_y, int32_t z, uint32_t raw_heading,
                            uint32_t animation = 0) {
    PosFields f{};
    f.field1 = (static_cast<uint32_t>(server_x) & 0x7FFFF) << 10;
    f.field2 = (static_cast<uint32_t>(server_y) & 0x7FFFF) |
               ((animation & 0x3FF) << 19);
    f.field3 = static_cast<uint32_t>(z) & 0x7FFFF;
    f.field4 = (raw_heading & 0x7FF) << 13;
    return f;
}

// Writes one 385-byte Titanium Spawn_Struct at packet offset `base`
// (base = 2 for a single NewSpawn packet).
inline void WriteSpawnStruct(EQ::Net::DynamicPacket& p, size_t base, uint32_t spawn_id,
                              const std::string& name, uint8_t level, uint32_t race,
                              uint8_t char_class, uint8_t gender, uint8_t npc_type,
                              uint8_t hp_percent, const PosFields& pos) {
    p.PutCString(base + 7, name.c_str());
    p.PutUInt8(base + 83, npc_type);
    p.PutUInt8(base + 86, hp_percent);
    p.PutUInt32(base + 94, pos.field1);
    p.PutUInt32(base + 98, pos.field2);
    p.PutUInt32(base + 102, pos.field3);
    p.PutUInt32(base + 106, pos.field4);
    p.PutUInt8(base + 151, level);
    p.PutUInt32(base + 284, race);
    p.PutUInt8(base + 331, char_class);
    p.PutUInt8(base + 334, gender);
    p.PutUInt32(base + 340, spawn_id);
}

// Convenience: a single NewSpawn payload with an explicit world position.
// entity.x <- server_y, entity.y <- server_x, entity.z <- z (INVERSEXY).
inline std::vector<uint8_t> NewSpawnPayloadAt(uint32_t spawn_id, const std::string& name,
                                               int32_t server_x, int32_t server_y, int32_t z,
                                               uint8_t level = 5, uint8_t npc_type = 1,
                                               uint8_t char_class = 1, uint8_t hp_percent = 100) {
    return BuildPayload(387, [&](EQ::Net::DynamicPacket& p) {
        WriteSpawnStruct(p, 2, spawn_id, name, level, 54, char_class, 0, npc_type, hp_percent,
                          EncodePos(server_x, server_y, z, 0));
    });
}

}  // namespace zonetest
