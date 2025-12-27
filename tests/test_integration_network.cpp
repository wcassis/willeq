#include <gtest/gtest.h>
#include "common/net/daybreak_structs.h"
#include "common/net/daybreak_connection.h"
#include "common/net/packet.h"
#include "common/net/crc32.h"
#include "common/util/compression.h"
#include <cereal/archives/binary.hpp>
#include <sstream>
#include <vector>

using namespace EQ;
using namespace EQ::Net;

class NetworkIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test DaybreakHeader serialization round-trip
TEST_F(NetworkIntegrationTest, DaybreakHeader_Serialization) {
    DaybreakHeader header;
    header.zero = 0;
    header.opcode = OP_SessionRequest;

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(header);
    }

    // Deserialize
    DaybreakHeader result;
    std::istringstream is(os.str());
    {
        cereal::BinaryInputArchive archive(is);
        archive(result);
    }

    EXPECT_EQ(result.zero, 0);
    EXPECT_EQ(result.opcode, OP_SessionRequest);
}

// Test DaybreakConnect serialization
TEST_F(NetworkIntegrationTest, DaybreakConnect_Serialization) {
    DaybreakConnect connect;
    connect.zero = 0;
    connect.opcode = OP_SessionRequest;
    connect.protocol_version = 3;
    connect.connect_code = 0x12345678;
    connect.max_packet_size = 512;

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(connect);
    }

    // Deserialize
    DaybreakConnect result;
    std::istringstream is(os.str());
    {
        cereal::BinaryInputArchive archive(is);
        archive(result);
    }

    EXPECT_EQ(result.zero, 0);
    EXPECT_EQ(result.opcode, OP_SessionRequest);
    EXPECT_EQ(result.protocol_version, 3u);
    EXPECT_EQ(result.connect_code, 0x12345678u);
    EXPECT_EQ(result.max_packet_size, 512u);
}

// Test DaybreakConnectReply serialization
TEST_F(NetworkIntegrationTest, DaybreakConnectReply_Serialization) {
    DaybreakConnectReply reply;
    reply.zero = 0;
    reply.opcode = OP_SessionResponse;
    reply.connect_code = 0xABCDEF01;
    reply.encode_key = 0x55667788;
    reply.crc_bytes = 2;
    reply.encode_pass1 = EncodeCompression;
    reply.encode_pass2 = EncodeXOR;
    reply.max_packet_size = 512;

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(reply);
    }

    // Deserialize
    DaybreakConnectReply result;
    std::istringstream is(os.str());
    {
        cereal::BinaryInputArchive archive(is);
        archive(result);
    }

    EXPECT_EQ(result.zero, 0);
    EXPECT_EQ(result.opcode, OP_SessionResponse);
    EXPECT_EQ(result.connect_code, 0xABCDEF01u);
    EXPECT_EQ(result.encode_key, 0x55667788u);
    EXPECT_EQ(result.crc_bytes, 2);
    EXPECT_EQ(result.encode_pass1, EncodeCompression);
    EXPECT_EQ(result.encode_pass2, EncodeXOR);
    EXPECT_EQ(result.max_packet_size, 512u);
}

// Test DaybreakReliableHeader sequence handling
TEST_F(NetworkIntegrationTest, DaybreakReliableHeader_Sequence) {
    DaybreakReliableHeader header;
    header.zero = 0;
    header.opcode = OP_Packet;
    header.sequence = 1234;

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(header);
    }

    // Deserialize
    DaybreakReliableHeader result;
    std::istringstream is(os.str());
    {
        cereal::BinaryInputArchive archive(is);
        archive(result);
    }

    EXPECT_EQ(result.sequence, 1234);
}

// Test DaybreakReliableFragmentHeader for fragmented packets
TEST_F(NetworkIntegrationTest, DaybreakReliableFragmentHeader_Serialization) {
    DaybreakReliableFragmentHeader header;
    header.reliable.zero = 0;
    header.reliable.opcode = OP_Fragment;
    header.reliable.sequence = 5678;
    header.total_size = 16384;  // Large packet that was fragmented

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(header);
    }

    // Deserialize
    DaybreakReliableFragmentHeader result;
    std::istringstream is(os.str());
    {
        cereal::BinaryInputArchive archive(is);
        archive(result);
    }

    EXPECT_EQ(result.reliable.opcode, OP_Fragment);
    EXPECT_EQ(result.reliable.sequence, 5678);
    EXPECT_EQ(result.total_size, 16384u);
}

// Test DaybreakSessionStatRequest/Response
TEST_F(NetworkIntegrationTest, SessionStats_Serialization) {
    DaybreakSessionStatRequest request;
    request.zero = 0;
    request.opcode = OP_SessionStatRequest;
    request.timestamp = 1000;
    request.stat_ping = 50;
    request.avg_ping = 55;
    request.min_ping = 30;
    request.max_ping = 100;
    request.last_ping = 48;
    request.packets_sent = 1000;
    request.packets_recv = 950;

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(request);
    }

    // Deserialize
    DaybreakSessionStatRequest result;
    std::istringstream is(os.str());
    {
        cereal::BinaryInputArchive archive(is);
        archive(result);
    }

    EXPECT_EQ(result.timestamp, 1000);
    EXPECT_EQ(result.avg_ping, 55u);
    EXPECT_EQ(result.packets_sent, 1000u);
    EXPECT_EQ(result.packets_recv, 950u);
}

// Integration test: Build a complete session packet with CRC
TEST_F(NetworkIntegrationTest, CompletePacket_WithCRC) {
    // Build a session request packet
    DynamicPacket packet;
    packet.PutUInt8(0, 0);                      // zero byte
    packet.PutUInt8(1, OP_SessionRequest);      // opcode
    packet.PutUInt32(2, 3);                     // protocol version
    packet.PutUInt32(6, 0x12345678);            // connect code
    packet.PutUInt32(10, 512);                  // max packet size

    // Calculate CRC on packet data
    uint32_t crc = Crc32(packet.Data(), packet.Length());

    // Verify we can append CRC
    packet.Resize(packet.Length() + 4);
    packet.PutUInt32(14, crc);

    EXPECT_EQ(packet.Length(), 18u);
    EXPECT_EQ(packet.GetUInt8(1), OP_SessionRequest);
    EXPECT_EQ(packet.GetUInt32(14), crc);
}

// Integration test: Compress and decompress packet data
TEST_F(NetworkIntegrationTest, PacketCompression_RoundTrip) {
    // Create a packet with repetitive data (compresses well)
    DynamicPacket packet;
    for (int i = 0; i < 100; i++) {
        packet.PutUInt8(i, static_cast<uint8_t>(i % 10));
    }

    // Compress - returns size directly
    std::vector<char> compressed;
    compressed.resize(EstimateDeflateBuffer(packet.Length()));
    uint32_t compressed_size = DeflateData(reinterpret_cast<const char*>(packet.Data()),
                                           packet.Length(),
                                           compressed.data(),
                                           static_cast<uint32_t>(compressed.size()));
    ASSERT_GT(compressed_size, 0u);

    // Decompress - returns size directly
    std::vector<char> decompressed;
    decompressed.resize(packet.Length());
    uint32_t decompressed_size = InflateData(compressed.data(),
                                             compressed_size,
                                             decompressed.data(),
                                             static_cast<uint32_t>(decompressed.size()));
    ASSERT_EQ(decompressed_size, packet.Length());

    // Verify data matches
    for (uint32_t i = 0; i < packet.Length(); i++) {
        EXPECT_EQ(static_cast<uint8_t>(decompressed[i]), packet.GetUInt8(i));
    }
}

// Test multiple opcode types
TEST_F(NetworkIntegrationTest, Opcodes_AllTypes) {
    // Test each protocol opcode value
    EXPECT_EQ(OP_Padding, 0x00);
    EXPECT_EQ(OP_SessionRequest, 0x01);
    EXPECT_EQ(OP_SessionResponse, 0x02);
    EXPECT_EQ(OP_Combined, 0x03);
    EXPECT_EQ(OP_SessionDisconnect, 0x05);
    EXPECT_EQ(OP_KeepAlive, 0x06);
    EXPECT_EQ(OP_SessionStatRequest, 0x07);
    EXPECT_EQ(OP_SessionStatResponse, 0x08);
    EXPECT_EQ(OP_Packet, 0x09);
    EXPECT_EQ(OP_Fragment, 0x0d);
    EXPECT_EQ(OP_OutOfOrderAck, 0x11);
    EXPECT_EQ(OP_Ack, 0x15);
    EXPECT_EQ(OP_AppCombined, 0x19);
    EXPECT_EQ(OP_OutboundPing, 0x1c);
    EXPECT_EQ(OP_OutOfSession, 0x1d);
}

// Test encode types
TEST_F(NetworkIntegrationTest, EncodeTypes) {
    EXPECT_EQ(EncodeNone, 0);
    EXPECT_EQ(EncodeCompression, 1);
    EXPECT_EQ(EncodeXOR, 4);
}

// Test status enum values
TEST_F(NetworkIntegrationTest, StatusValues) {
    EXPECT_EQ(StatusConnecting, 0);
    EXPECT_EQ(StatusConnected, 1);
    EXPECT_EQ(StatusDisconnecting, 2);
    EXPECT_EQ(StatusDisconnected, 3);
}

// Test sequence order enum
TEST_F(NetworkIntegrationTest, SequenceOrderValues) {
    EXPECT_EQ(SequenceCurrent, 0);
    EXPECT_EQ(SequenceFuture, 1);
    EXPECT_EQ(SequencePast, 2);
}

// Integration test: Build and verify disconnect packet
TEST_F(NetworkIntegrationTest, DisconnectPacket) {
    DaybreakDisconnect disconnect;
    disconnect.zero = 0;
    disconnect.opcode = OP_SessionDisconnect;
    disconnect.connect_code = 0x12345678;

    // Serialize
    std::ostringstream os;
    {
        cereal::BinaryOutputArchive archive(os);
        archive(disconnect);
    }

    std::string data = os.str();
    EXPECT_EQ(data.size(), 6u);  // 1 + 1 + 4 bytes

    // Verify we can calculate CRC on the serialized data
    uint32_t crc = Crc32(data.data(), data.size());
    EXPECT_NE(crc, 0u);
}

// Test struct sizes are correct
TEST_F(NetworkIntegrationTest, StructSizes) {
    EXPECT_EQ(DaybreakHeader::size(), 2u);
    EXPECT_EQ(DaybreakConnect::size(), 14u);
    EXPECT_EQ(DaybreakConnectReply::size(), 17u);
    EXPECT_EQ(DaybreakDisconnect::size(), 8u);
    EXPECT_EQ(DaybreakReliableHeader::size(), 4u);
    EXPECT_EQ(DaybreakReliableFragmentHeader::size(), 8u);
    EXPECT_EQ(DaybreakSessionStatRequest::size(), 40u);
    EXPECT_EQ(DaybreakSessionStatResponse::size(), 40u);
}

// Test connection stats initialization
TEST_F(NetworkIntegrationTest, ConnectionStats_Init) {
    DaybreakConnectionStats stats;
    EXPECT_EQ(stats.recv_bytes, 0u);
    EXPECT_EQ(stats.sent_bytes, 0u);
    EXPECT_EQ(stats.recv_packets, 0u);
    EXPECT_EQ(stats.sent_packets, 0u);
}
