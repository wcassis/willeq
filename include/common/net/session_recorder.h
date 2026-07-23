#pragma once

// R01: Session recorder — writes decoded application packets to a session
// log file (format defined in session_log.h) for later deterministic replay
// (Batch R). Hooked into the three packet-recv dispatchers (Login/World/Zone
// S->C) and the two zone C->S send paths (QueuePacket/SafeQueueZonePacket).
//
// Usage: open() once at startup (wired from --record-session), then record()
// on every decoded application packet. close() flushes and closes the file;
// the destructor also closes if still open.

#include "common/net/session_log.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

namespace EQT {
namespace Net {

class SessionRecorder {
public:
	SessionRecorder() = default;
	~SessionRecorder();

	// Non-copyable, non-movable (owns a FILE* and a mutex).
	SessionRecorder(const SessionRecorder&) = delete;
	SessionRecorder& operator=(const SessionRecorder&) = delete;

	// Opens `path` for writing and emits the file header. `character` and
	// `zone` are informational only (truncated to fit the header fields).
	// Returns false if the file could not be opened.
	bool open(const std::string& path, const std::string& character, const std::string& zone);

	// Records one decoded application packet. `data`/`size` are the payload
	// EXCLUDING the 2-byte opcode (matching session_log.h's convention). A
	// no-op if the recorder is not open. Thread-safe.
	void record(SessionStream stream, SessionDirection direction, uint16_t opcode,
		const void* data, uint32_t size);

	// Flushes and closes the file. Safe to call multiple times.
	void close();

	bool isOpen() const { return m_file != nullptr; }
	uint64_t recordCount() const { return m_record_count; }

private:
	std::mutex m_mutex;
	FILE* m_file = nullptr;
	std::chrono::steady_clock::time_point m_start_time{};
	uint64_t m_record_count = 0;

	static constexpr uint64_t FLUSH_INTERVAL = 64;
};

} // namespace Net
} // namespace EQT
