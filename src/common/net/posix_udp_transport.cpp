#include "common/net/posix_udp_transport.h"
#include "common/logging.h"

#include <cerrno>
#include <cstring>
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
	// Try SO_RCVBUFFORCE first (requires CAP_NET_ADMIN), fall back to SO_RCVBUF.
	int rcvbuf_size = 512 * 1024;
	if (setsockopt(m_fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
		rcvbuf_size = 212992;
		if (setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
			LOG_WARN(MOD_NET, "PosixUdpTransport: failed to set SO_RCVBUF: {}", strerror(errno));
		}
	}

	socklen_t optlen = sizeof(rcvbuf_size);
	getsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, &optlen);
	LOG_TRACE(MOD_NET, "PosixUdpTransport: fd={} rcvbuf_size={}", m_fd, rcvbuf_size);

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
