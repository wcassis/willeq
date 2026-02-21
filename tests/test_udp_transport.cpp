#include <gtest/gtest.h>
#include "common/net/posix_udp_transport.h"

TEST(PosixUdpTransport, DefaultStateIsClosed)
{
	EQ::Net::PosixUdpTransport transport;
	EXPECT_FALSE(transport.is_open());
}

TEST(PosixUdpTransport, CloseWhenNotOpenIsSafe)
{
	EQ::Net::PosixUdpTransport transport;
	transport.close();  // Should not crash
	EXPECT_FALSE(transport.is_open());
}

TEST(PosixUdpTransport, RecvWhenClosedReturnsError)
{
	EQ::Net::PosixUdpTransport transport;
	uint8_t buf[64];
	EXPECT_EQ(transport.recv(buf, sizeof(buf)), -1);
}

TEST(PosixUdpTransport, SendWhenClosedReturnsError)
{
	EQ::Net::PosixUdpTransport transport;
	uint8_t data[] = {1, 2, 3};
	EXPECT_EQ(transport.send(data, sizeof(data)), -1);
}

TEST(PosixUdpTransport, OpenToUnreachableSucceeds)
{
	// connect() on a UDP socket doesn't actually verify reachability,
	// it just sets the default destination. So open() should succeed.
	EQ::Net::PosixUdpTransport transport;
	EXPECT_TRUE(transport.open("127.0.0.1", 19999));
	EXPECT_TRUE(transport.is_open());
	transport.close();
	EXPECT_FALSE(transport.is_open());
}

TEST(PosixUdpTransport, RecvReturnsZeroWhenNothingPending)
{
	EQ::Net::PosixUdpTransport transport;
	ASSERT_TRUE(transport.open("127.0.0.1", 19999));

	uint8_t buf[1024];
	int n = transport.recv(buf, sizeof(buf));
	EXPECT_EQ(n, 0);  // Non-blocking, nothing to receive
}

TEST(PosixUdpTransport, SendToUnreachableReturnsPositive)
{
	// UDP send to an unreachable port succeeds from the sender's perspective
	EQ::Net::PosixUdpTransport transport;
	ASSERT_TRUE(transport.open("127.0.0.1", 19999));

	uint8_t data[] = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x00};
	int n = transport.send(data, sizeof(data));
	EXPECT_GT(n, 0);
}

TEST(PosixUdpTransport, OpenBadHostFails)
{
	EQ::Net::PosixUdpTransport transport;
	EXPECT_FALSE(transport.open("this.host.does.not.exist.invalid", 5998));
	EXPECT_FALSE(transport.is_open());
}

TEST(PosixUdpTransport, DoubleOpenClosesFirst)
{
	EQ::Net::PosixUdpTransport transport;
	ASSERT_TRUE(transport.open("127.0.0.1", 19999));
	ASSERT_TRUE(transport.is_open());

	// Second open should close the first and open a new one
	ASSERT_TRUE(transport.open("127.0.0.1", 19998));
	EXPECT_TRUE(transport.is_open());
}
