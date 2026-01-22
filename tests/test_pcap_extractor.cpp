/**
 * Tests for pcap_test_utils.h - PCAP packet extractor
 *
 * Verifies that the pcap utilities correctly read and parse packet captures.
 */

#include <gtest/gtest.h>
#include "pcap_test_utils.h"
#include <iostream>

using namespace EQ::Test;

// Path to the test capture file
static const char* TEST_PCAP_FILE = "/tmp/willeq_audit_capture2.pcap";

class PcapExtractorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Check if pcap file exists
        std::ifstream f(TEST_PCAP_FILE);
        if (!f.good()) {
            GTEST_SKIP() << "Test pcap file not found: " << TEST_PCAP_FILE;
        }
    }
};

TEST_F(PcapExtractorTest, ReadPcapFile_Basic) {
    PcapReadOptions options;
    options.remove_duplicates = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);

    ASSERT_TRUE(result.success) << "Failed to read pcap: " << result.error;
    EXPECT_GT(result.total_frames, 0u) << "No frames read from pcap";
    EXPECT_GT(result.udp_packets, 0u) << "No UDP packets found";
    EXPECT_GT(result.packets.size(), 0u) << "No packets extracted";

    std::cout << "PCAP Read Results:" << std::endl;
    std::cout << "  Network type: " << result.network_type << std::endl;
    std::cout << "  Total frames: " << result.total_frames << std::endl;
    std::cout << "  UDP packets: " << result.udp_packets << std::endl;
    std::cout << "  Duplicates removed: " << result.duplicate_packets << std::endl;
    std::cout << "  Unique packets: " << result.packets.size() << std::endl;
}

TEST_F(PcapExtractorTest, ReadPcapFile_ServerToClientOnly) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);

    ASSERT_TRUE(result.success) << "Failed to read pcap: " << result.error;
    EXPECT_GT(result.packets.size(), 0u) << "No S->C packets found";

    // Verify all packets are from server ports
    for (const auto& pkt : result.packets) {
        EXPECT_TRUE(options.server_ports.count(pkt.src_port) > 0)
            << "Packet from non-server port: " << pkt.src_port;
    }

    std::cout << "S->C Packets: " << result.packets.size() << std::endl;
}

TEST_F(PcapExtractorTest, ReadPcapFile_ClientToServerOnly) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.client_to_server_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);

    ASSERT_TRUE(result.success) << "Failed to read pcap: " << result.error;
    EXPECT_GT(result.packets.size(), 0u) << "No C->S packets found";

    // Verify all packets are to server ports
    for (const auto& pkt : result.packets) {
        EXPECT_TRUE(options.server_ports.count(pkt.dst_port) > 0)
            << "Packet to non-server port: " << pkt.dst_port;
    }

    std::cout << "C->S Packets: " << result.packets.size() << std::endl;
}

TEST_F(PcapExtractorTest, PacketStats_ServerToClient) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    auto stats = getPacketStats(result.packets);

    std::cout << "\nServer -> Client Packet Analysis:" << std::endl;
    printPacketStats(stats, std::cout);

    // Verify we have the expected packet types based on pcap analysis
    EXPECT_GT(stats.protocol_packets, 0u) << "No protocol packets found";
    EXPECT_GT(stats.fragment_packets, 0u) << "No fragment packets found";
    EXPECT_GT(stats.combined_packets, 0u) << "No combined packets found";
    EXPECT_GT(stats.compressed_packets, 0u) << "No compressed packets found";
}

TEST_F(PcapExtractorTest, VerifyPacketStructure) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;
    options.max_packets = 100;

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    std::cout << "\nFirst 30 S->C packets:" << std::endl;
    for (size_t i = 0; i < 30 && i < result.packets.size(); i++) {
        printPacketSummary(result.packets[i], std::cout);
    }
}

TEST_F(PcapExtractorTest, FindFragmentPackets) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    std::cout << "\nFragment packets found:" << std::endl;
    int count = 0;
    for (const auto& pkt : result.packets) {
        if (pkt.isDaybreakProtocol() && isFragmentOpcode(pkt.getDaybreakOpcode())) {
            if (count < 10) {
                printPacketSummary(pkt, std::cout);
            }
            count++;
        }
    }
    std::cout << "Total fragment packets: " << count << std::endl;

    EXPECT_GT(count, 0) << "No fragment packets found";
}

TEST_F(PcapExtractorTest, FindCompressedPackets) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    std::cout << "\nCompressed packets (0x5a marker):" << std::endl;
    int count = 0;
    for (const auto& pkt : result.packets) {
        if (pkt.data.size() > 2 && pkt.data[2] == 0x5a) {
            if (count < 10) {
                printPacketSummary(pkt, std::cout);
            }
            count++;
        }
    }
    std::cout << "Total compressed packets: " << count << std::endl;

    EXPECT_GT(count, 0) << "No compressed packets found";
}

TEST_F(PcapExtractorTest, VerifySessionResponse) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    // Find OP_SessionResponse packets
    std::vector<const CapturedPacket*> session_responses;
    for (const auto& pkt : result.packets) {
        if (pkt.isDaybreakProtocol() && pkt.getDaybreakOpcode() == 0x02) {
            session_responses.push_back(&pkt);
        }
    }

    std::cout << "\nSession Response packets found: " << session_responses.size() << std::endl;
    for (const auto* pkt : session_responses) {
        printPacketSummary(*pkt, std::cout);

        // Parse session response structure
        if (pkt->data.size() >= 17) {
            // Structure: 00 02 [connect_code:4] [encode_key:4] [crc_bytes:1] [encode1:1] [encode2:1] [max_size:4]
            uint8_t crc_bytes = pkt->data[10];
            uint8_t encode1 = pkt->data[11];
            uint8_t encode2 = pkt->data[12];

            std::cout << "  Session params: crc_bytes=" << (int)crc_bytes
                      << ", encode1=" << (int)encode1
                      << ", encode2=" << (int)encode2 << std::endl;
        }
    }

    EXPECT_GT(session_responses.size(), 0u) << "No session response packets found";
}

TEST_F(PcapExtractorTest, FilterByZoneServerPort) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.filter_src_port = 7000;  // Zone server port

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    std::cout << "\nPackets from zone server (port 7000): " << result.packets.size() << std::endl;

    // All packets should be from port 7000
    for (const auto& pkt : result.packets) {
        EXPECT_EQ(pkt.src_port, 7000u);
    }

    auto stats = getPacketStats(result.packets);
    printPacketStats(stats, std::cout);
}

TEST_F(PcapExtractorTest, PacketsAreInTimestampOrder) {
    PcapReadOptions options;
    options.remove_duplicates = true;
    options.server_to_client_only = true;

    auto result = readPcapFile(TEST_PCAP_FILE, options);
    ASSERT_TRUE(result.success);

    // Verify packets are in timestamp order
    for (size_t i = 1; i < result.packets.size(); i++) {
        double t1 = result.packets[i-1].timestamp();
        double t2 = result.packets[i].timestamp();
        EXPECT_LE(t1, t2) << "Packets not in timestamp order at index " << i;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
