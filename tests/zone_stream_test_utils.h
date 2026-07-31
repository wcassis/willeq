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

}  // namespace zonetest
