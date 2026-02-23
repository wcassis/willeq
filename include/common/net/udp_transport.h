#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace EQ {
namespace Net {

// Abstract UDP transport — one connected socket to one server.
// Implementations: PosixUdpTransport (Linux/macOS/POSIX), future WasmUdpTransport, etc.
class UdpTransport {
public:
	virtual ~UdpTransport() = default;

	// Resolve addr, create a non-blocking UDP socket, connect() it.
	// Returns true on success.
	virtual bool open(const std::string& addr, int port) = 0;

	// Close the socket. Safe to call if not open.
	virtual void close() = 0;

	// Non-blocking receive. Returns bytes read, 0 if nothing pending, -1 on error.
	virtual int recv(uint8_t* buf, size_t buf_len) = 0;

	// Blocking-ish send (UDP sends rarely block). Returns bytes sent, -1 on error.
	virtual int send(const uint8_t* data, size_t len) = 0;

	// True if open() succeeded and close() has not been called.
	virtual bool is_open() const = 0;
};

} // namespace Net
} // namespace EQ
