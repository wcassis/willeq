#pragma once

#include "udp_transport.h"

namespace EQ {
namespace Net {

class PosixUdpTransport : public UdpTransport {
public:
	PosixUdpTransport();
	~PosixUdpTransport() override;

	bool open(const std::string& addr, int port) override;
	void close() override;
	int recv(uint8_t* buf, size_t buf_len) override;
	int send(const uint8_t* data, size_t len) override;
	bool is_open() const override;

private:
	int m_fd = -1;
};

} // namespace Net
} // namespace EQ
