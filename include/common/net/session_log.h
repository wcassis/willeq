#pragma once

// Session log format — records decoded application packets (post-Daybreak:
// after decompression, fragment reassembly, and CRC stripping) for
// deterministic session replay (Batch R).
//
// Binary layout (little-endian, packed):
//   FileHeader, then Record* until EOF.
//   Each Record is RecordHeader followed by `size` payload bytes.
//
// The payload is the application packet EXCLUDING the 2-byte app opcode,
// which is stored in RecordHeader::opcode (matching what the
// ZoneOnPacketRecv dispatcher extracts before dispatching to handlers).

#include <cstdint>

namespace EQT {
namespace Net {

constexpr uint32_t SESSION_LOG_MAGIC = 0x51454C53;  // "SLEQ"
constexpr uint16_t SESSION_LOG_VERSION = 1;

enum class SessionStream : uint8_t {
    Login = 0,
    World = 1,
    Zone  = 2,
};

enum class SessionDirection : uint8_t {
    ServerToClient = 0,
    ClientToServer = 1,
};

#pragma pack(push, 1)
struct SessionLogFileHeader {
    uint32_t magic = SESSION_LOG_MAGIC;
    uint16_t version = SESSION_LOG_VERSION;
    uint16_t reserved = 0;
    uint64_t startTimestampUs = 0;   // wall-clock at recording start (info only)
    char character[64] = {};         // character name (info only)
    char zone[32] = {};              // starting zone short name if known (info only)
};

struct SessionLogRecordHeader {
    uint64_t timestampUs;   // microseconds since recording start
    uint8_t stream;         // SessionStream
    uint8_t direction;      // SessionDirection
    uint16_t opcode;        // application opcode (HC_OP_*)
    uint32_t size;          // payload byte count following this header
};
#pragma pack(pop)

static_assert(sizeof(SessionLogFileHeader) == 112, "session log file header layout");
static_assert(sizeof(SessionLogRecordHeader) == 16, "session log record header layout");

} // namespace Net
} // namespace EQT
