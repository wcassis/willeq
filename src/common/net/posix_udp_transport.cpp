#include "common/net/posix_udp_transport.h"
#include "common/logging.h"

#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

namespace EQ {
namespace Net {

PosixUdpTransport::PosixUdpTransport() = default;

PosixUdpTransport::~PosixUdpTransport()
{
	close();
}

bool PosixUdpTransport::open(const std::string& addr, int port)
{
	close();

	// Resolve address
	struct addrinfo hints{};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	std::string port_str = std::to_string(port);
	struct addrinfo* result = nullptr;
	int rc = getaddrinfo(addr.c_str(), port_str.c_str(), &hints, &result);
	if (rc != 0 || !result) {
		LOG_ERROR(MOD_NET, "PosixUdpTransport: getaddrinfo({}, {}) failed: {}", addr, port, gai_strerror(rc));
		return false;
	}

	// Create socket
	m_fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (m_fd < 0) {
		LOG_ERROR(MOD_NET, "PosixUdpTransport: socket() failed: {}", strerror(errno));
		freeaddrinfo(result);
		return false;
	}

	// Set non-blocking
	int flags = fcntl(m_fd, F_GETFL, 0);
	if (flags < 0 || fcntl(m_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		LOG_ERROR(MOD_NET, "PosixUdpTransport: fcntl(O_NONBLOCK) failed: {}", strerror(errno));
		::close(m_fd);
		m_fd = -1;
		freeaddrinfo(result);
		return false;
	}

	// Increase receive buffer to handle packet bursts during zone loading.
	// 895aad1e (2026-01) asked for 512 KiB via SO_RCVBUFFORCE (CAP_NET_ADMIN,
	// see CLAUDE.md's setcap recipe) and fell back to a hard-coded 212992.
	// B751 (#73, measured 2026-09-23): a hole zone-in on the Orange Pi is
	// ~2,600 datagrams; the kernel dropped 42 % of them for receive-buffer
	// overflow at the 1 MiB the forced request (kernel-doubled) yields, and
	// every unreliable packet in the burst -- including the one Phase 10
	// waits for -- was lost. So: ask for 8 MiB with the capability; without
	// it, ask for net.core.rmem_max (what the kernel allows, not 212992);
	// and say at INFO what was actually granted, so a run's log shows it.
	int requested = 8 * 1024 * 1024;
	// B1039 (#359): SO_RCVBUFFORCE is Linux-only; elsewhere (macOS) only the
	// SO_RCVBUF path below exists, and the INFO line says so.
	bool forced = false;
#ifdef SO_RCVBUFFORCE
	forced = setsockopt(m_fd, SOL_SOCKET, SO_RCVBUFFORCE, &requested, sizeof(requested)) == 0;
	const char* unforcedPath = "SO_RCVBUF, no CAP_NET_ADMIN: capped by net.core.rmem_max";
#else
	const char* unforcedPath = "SO_RCVBUF; this platform has no SO_RCVBUFFORCE";
#endif
	if (!forced) {
		int rmem_max = 212992;
		if (FILE* f = fopen("/proc/sys/net/core/rmem_max", "r")) {
			if (fscanf(f, "%d", &rmem_max) != 1) rmem_max = 212992;
			fclose(f);
		}
		requested = rmem_max;
		if (setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &requested, sizeof(requested)) < 0) {
			LOG_WARN(MOD_NET, "PosixUdpTransport: failed to set SO_RCVBUF: {}", strerror(errno));
		}
	}

	int rcvbuf_size = 0;
	socklen_t optlen = sizeof(rcvbuf_size);
	getsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, &optlen);
	LOG_INFO(MOD_NET, "PosixUdpTransport: fd={} rcvbuf={} bytes ({}; asked {})", m_fd, rcvbuf_size,
	         forced ? "SO_RCVBUFFORCE, CAP_NET_ADMIN" : unforcedPath,
	         requested);

	// Connect (so send()/recv() work without per-packet address)
	if (::connect(m_fd, result->ai_addr, result->ai_addrlen) < 0) {
		LOG_ERROR(MOD_NET, "PosixUdpTransport: connect({}:{}) failed: {}", addr, port, strerror(errno));
		::close(m_fd);
		m_fd = -1;
		freeaddrinfo(result);
		return false;
	}

	freeaddrinfo(result);

	LOG_INFO(MOD_NET, "PosixUdpTransport: connected to {}:{} fd={}", addr, port, m_fd);
	return true;
}

void PosixUdpTransport::close()
{
	if (m_fd >= 0) {
		LOG_TRACE(MOD_NET, "PosixUdpTransport: closing fd={}", m_fd);
		::close(m_fd);
		m_fd = -1;
	}
}

int PosixUdpTransport::recv(uint8_t* buf, size_t buf_len)
{
	if (m_fd < 0) return -1;

	ssize_t n = ::recv(m_fd, buf, buf_len, 0);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0; // Nothing pending
		}
		return -1;
	}
	return static_cast<int>(n);
}

int PosixUdpTransport::send(const uint8_t* data, size_t len)
{
	if (m_fd < 0) return -1;

#ifdef MSG_NOSIGNAL
	ssize_t n = ::send(m_fd, data, len, MSG_NOSIGNAL);
#else
	ssize_t n = ::send(m_fd, data, len, 0);
#endif
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}
		LOG_ERROR(MOD_NET, "PosixUdpTransport: send() failed: {}", strerror(errno));
		return -1;
	}
	return static_cast<int>(n);
}

bool PosixUdpTransport::is_open() const
{
	return m_fd >= 0;
}

} // namespace Net
} // namespace EQ
