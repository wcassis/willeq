#include <gtest/gtest.h>
#include "common/net/packet.h"
#include <string>
#include <vector>
#include <cstring>

class PacketTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// Test DynamicPacket construction
TEST_F(PacketTest, DynamicPacket_DefaultConstruct) {
    EQ::Net::DynamicPacket packet;
    EXPECT_EQ(packet.Length(), 0u);
}

TEST_F(PacketTest, DynamicPacket_Resize) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(100);
    EXPECT_EQ(packet.Length(), 100u);
    EXPECT_NE(packet.Data(), nullptr);
}

TEST_F(PacketTest, DynamicPacket_PutGetUInt8) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(10);

    packet.PutUInt8(0, 0x42);
    packet.PutUInt8(5, 0xFF);

    EXPECT_EQ(packet.GetUInt8(0), 0x42u);
    EXPECT_EQ(packet.GetUInt8(5), 0xFFu);
}

TEST_F(PacketTest, DynamicPacket_PutGetUInt16) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(10);

    packet.PutUInt16(0, 0x1234);
    packet.PutUInt16(4, 0xABCD);

    EXPECT_EQ(packet.GetUInt16(0), 0x1234u);
    EXPECT_EQ(packet.GetUInt16(4), 0xABCDu);
}

TEST_F(PacketTest, DynamicPacket_PutGetUInt32) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(16);

    packet.PutUInt32(0, 0x12345678);
    packet.PutUInt32(8, 0xDEADBEEF);

    EXPECT_EQ(packet.GetUInt32(0), 0x12345678u);
    EXPECT_EQ(packet.GetUInt32(8), 0xDEADBEEFu);
}

TEST_F(PacketTest, DynamicPacket_PutGetFloat) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(16);

    packet.PutFloat(0, 3.14159f);
    packet.PutFloat(8, -123.456f);

    EXPECT_FLOAT_EQ(packet.GetFloat(0), 3.14159f);
    EXPECT_FLOAT_EQ(packet.GetFloat(8), -123.456f);
}

TEST_F(PacketTest, DynamicPacket_PutGetCString) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(64);

    std::string test_str = "Hello, World!";
    packet.PutString(0, test_str);

    // GetCString for null-terminated strings
    std::string result = packet.GetCString(0);
    EXPECT_EQ(result, test_str);
}

// T08: GetCString used to build a std::string straight from the raw
// pointer, so an unterminated string field read past the end of the
// packet — a real out-of-bounds read. The scan is now bounded by the
// packet, treating the end as a terminator. These use StaticPacket over
// an exactly-sized buffer so the packet end is a hard boundary: with a
// DynamicPacket the surrounding heap slack could mask the over-read.

TEST_F(PacketTest, GetCString_UnterminatedRunsToPacketEnd) {
    // No NUL anywhere in the buffer. Pre-fix this ran off the end.
    char buffer[4] = { 'a', 'b', 'c', 'd' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_EQ(packet.GetCString(0), std::string("abcd"));
}

TEST_F(PacketTest, GetCString_UnterminatedFromOffsetRunsToPacketEnd) {
    char buffer[6] = { 'x', 'y', 'a', 'b', 'c', 'd' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_EQ(packet.GetCString(2), std::string("abcd"));
}

TEST_F(PacketTest, GetCString_StopsAtEmbeddedTerminator) {
    // Terminated well before the end: unchanged behavior, and the trailing
    // bytes must not leak into the result.
    char buffer[8] = { 'a', 'b', '\0', 'j', 'u', 'n', 'k', '!' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_EQ(packet.GetCString(0), std::string("ab"));
}

TEST_F(PacketTest, GetCString_TerminatorAtFinalByte) {
    char buffer[3] = { 'h', 'i', '\0' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_EQ(packet.GetCString(0), std::string("hi"));
}

TEST_F(PacketTest, GetCString_EmptyStringAtOffset) {
    char buffer[3] = { '\0', 'x', 'y' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_EQ(packet.GetCString(0), std::string(""));
}

TEST_F(PacketTest, GetCString_LastByteOfPacketIsBounded) {
    // offset == Length()-1 with no terminator: exactly one readable byte.
    char buffer[3] = { 'a', 'b', 'c' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_EQ(packet.GetCString(2), std::string("c"));
}

TEST_F(PacketTest, GetCString_OffsetPastEndStillThrows) {
    char buffer[3] = { 'a', 'b', 'c' };
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));

    EXPECT_THROW(packet.GetCString(3), std::out_of_range);
    EXPECT_THROW(packet.GetCString(99), std::out_of_range);
}

TEST_F(PacketTest, DynamicPacket_GetString_WithLength) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(64);

    std::string test_str = "Hello, World!";
    packet.PutString(0, test_str);

    // GetString requires offset and length
    std::string result = packet.GetString(0, test_str.length());
    EXPECT_EQ(result, test_str);
}

TEST_F(PacketTest, DynamicPacket_PutData) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(64);

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    packet.PutData(10, data, sizeof(data));

    EXPECT_EQ(packet.GetUInt8(10), 0x01u);
    EXPECT_EQ(packet.GetUInt8(14), 0x05u);
}

TEST_F(PacketTest, DynamicPacket_Clear) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(100);
    packet.PutUInt32(0, 0xDEADBEEF);

    packet.Clear();
    EXPECT_EQ(packet.Length(), 0u);
}

// Test StaticPacket
TEST_F(PacketTest, StaticPacket_FromBuffer) {
    uint8_t buffer[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));
    EXPECT_EQ(packet.Length(), sizeof(buffer));
    EXPECT_EQ(packet.GetUInt8(0), 0x01u);
    EXPECT_EQ(packet.GetUInt8(15), 0x10u);
}

TEST_F(PacketTest, StaticPacket_GetUInt16_LittleEndian) {
    uint8_t buffer[] = {0x34, 0x12};  // Little endian 0x1234
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));
    EXPECT_EQ(packet.GetUInt16(0), 0x1234u);
}

TEST_F(PacketTest, StaticPacket_GetUInt32_LittleEndian) {
    uint8_t buffer[] = {0x78, 0x56, 0x34, 0x12};  // Little endian 0x12345678
    EQ::Net::StaticPacket packet(buffer, sizeof(buffer));
    EXPECT_EQ(packet.GetUInt32(0), 0x12345678u);
}

// Test packet serialization
TEST_F(PacketTest, Serialize_BasicTypes) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(32);

    // Write various types
    packet.PutUInt8(0, 0x42);
    packet.PutUInt16(1, 0x1234);
    packet.PutUInt32(3, 0xDEADBEEF);
    packet.PutFloat(7, 3.14159f);

    // Read them back
    EXPECT_EQ(packet.GetUInt8(0), 0x42u);
    EXPECT_EQ(packet.GetUInt16(1), 0x1234u);
    EXPECT_EQ(packet.GetUInt32(3), 0xDEADBEEFu);
    EXPECT_FLOAT_EQ(packet.GetFloat(7), 3.14159f);
}

TEST_F(PacketTest, Serialize_NullTerminatedString) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(64);

    std::string original = "Test String";
    packet.PutString(0, original);

    // String should be null-terminated
    const char* data = static_cast<const char*>(packet.Data());
    EXPECT_EQ(strlen(data), original.length());

    std::string result = packet.GetCString(0);
    EXPECT_EQ(result, original);
}

// Test boundary conditions
TEST_F(PacketTest, BoundaryConditions_ZeroLength) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(0);
    EXPECT_EQ(packet.Length(), 0u);
}

TEST_F(PacketTest, BoundaryConditions_LargePacket) {
    EQ::Net::DynamicPacket packet;
    packet.Resize(65536);  // 64KB
    EXPECT_EQ(packet.Length(), 65536u);

    // Write at the end
    packet.PutUInt32(65532, 0xDEADBEEF);
    EXPECT_EQ(packet.GetUInt32(65532), 0xDEADBEEFu);
}
