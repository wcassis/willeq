/**
 * Tests for DaybreakTestHarness
 *
 * Verifies the test harness correctly processes Daybreak protocol packets
 * including compression, fragment assembly, and combined packets.
 */

#include <gtest/gtest.h>
#include "daybreak_test_harness.h"
#include "pcap_test_utils.h"
#include <iostream>

using namespace EQ::Test;

// Path to test pcap file
static const char* TEST_PCAP_FILE = "/tmp/willeq_audit_capture2.pcap";

class DaybreakHarnessTest : public ::testing::Test {
protected:
    DaybreakTestHarness harness;
    std::vector<DecodedPacket> decoded_packets;

    void SetUp() override {
        decoded_packets.clear();
        harness.resetState();
        harness.onPacketDecoded([this](const DecodedPacket& pkt) {
            decoded_packets.push_back(pkt);
        });
    }
};

// Test basic session setup
TEST_F(DaybreakHarnessTest, SessionSetup) {
    // OP_SessionResponse with compression enabled (from pcap frame 833)
    // 0002 ffffffff ffffffff 02 01 00 00000200
    uint8_t session_response[] = {
        0x00, 0x02,  // header
        0xff, 0xff, 0xff, 0xff,  // connect_code
        0xff, 0xff, 0xff, 0xff,  // encode_key
        0x02,  // crc_bytes
        0x01,  // encode_pass1 = EncodeCompression
        0x00,  // encode_pass2 = EncodeNone
        0x00, 0x00, 0x02, 0x00  // max_packet_size = 512
    };

    auto result = harness.processPacketNoCRC(session_response, sizeof(session_response));
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(harness.sessionEstablished());
    EXPECT_TRUE(harness.sessionParams().compression_enabled());
    EXPECT_EQ(harness.sessionParams().crc_bytes, 2);
    EXPECT_EQ(harness.sessionParams().encode_pass1, EncodeCompression);
}

// Test decompression of a simple packet
TEST_F(DaybreakHarnessTest, DecompressSimplePacket) {
    // Set up session with compression
    SessionParams params;
    params.crc_bytes = 0;  // No CRC for this test
    params.encode_pass1 = EncodeCompression;
    harness.setSessionParams(params);

    // OP_Packet with uncompressed marker: [00 09] [a5] [seq:00 01] [app_data]
    // After decompression: [00 09] [seq:00 01] [app_data]
    // The 0xa5 marker is at offset 2 (after header), and gets stripped
    uint8_t packet[] = {
        0x00, 0x09,  // OP_Packet
        0xa5,        // uncompressed marker
        0x00, 0x01,  // sequence = 1 (now part of decompressed payload)
        0xAB, 0xCD,  // app opcode (little-endian: 0xCDAB)
        0x01, 0x02, 0x03, 0x04  // app data
    };

    auto result = harness.processPacketNoCRC(packet, sizeof(packet));
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(decoded_packets.size(), 1u);

    if (!decoded_packets.empty()) {
        auto& pkt = decoded_packets[0];
        EXPECT_FALSE(pkt.is_protocol);
        EXPECT_EQ(pkt.app_opcode, 0xCDAB);
        EXPECT_EQ(pkt.data.size(), 6u);  // opcode(2) + data(4)
    }
}

// Test OP_Combined packet parsing
TEST_F(DaybreakHarnessTest, CombinedPacket) {
    SessionParams params;
    params.crc_bytes = 0;
    params.encode_pass1 = EncodeNone;  // No compression for this test
    harness.setSessionParams(params);

    // OP_Combined with two subpackets:
    // [00 03] [len1] [subpkt1] [len2] [subpkt2]
    uint8_t combined[] = {
        0x00, 0x03,  // OP_Combined
        0x04,        // subpacket 1 length = 4
        0x01, 0x00, 0xAA, 0xBB,  // app packet: opcode 0x0001, data AA BB
        0x03,        // subpacket 2 length = 3
        0x02, 0x00, 0xCC  // app packet: opcode 0x0002, data CC
    };

    auto result = harness.processPacketNoCRC(combined, sizeof(combined));
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(decoded_packets.size(), 2u);

    if (decoded_packets.size() >= 2) {
        EXPECT_EQ(decoded_packets[0].app_opcode, 0x0001);
        EXPECT_EQ(decoded_packets[1].app_opcode, 0x0002);
    }
}

// Test fragment assembly (uncompressed)
TEST_F(DaybreakHarnessTest, FragmentAssembly_Uncompressed) {
    SessionParams params;
    params.crc_bytes = 0;
    params.encode_pass1 = EncodeNone;
    harness.setSessionParams(params);

    // First fragment: total_size = 10 bytes
    // [00 0d] [seq:00 00] [total:00 00 00 0a] [data: 01 02]
    uint8_t frag1[] = {
        0x00, 0x0d,  // OP_Fragment
        0x00, 0x00,  // sequence = 0
        0x00, 0x00, 0x00, 0x0a,  // total_size = 10
        0x01, 0x00,  // first 2 bytes of app packet (opcode 0x0001)
    };

    // Continuation fragment
    // [00 0d] [seq:00 01] [data: 03 04 05 06 07 08 09 0a]
    uint8_t frag2[] = {
        0x00, 0x0d,  // OP_Fragment
        0x00, 0x01,  // sequence = 1
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a
    };

    auto result1 = harness.processPacketNoCRC(frag1, sizeof(frag1));
    EXPECT_TRUE(result1.success) << result1.error;
    EXPECT_EQ(harness.fragmentsReceived(), 1u);
    EXPECT_EQ(harness.fragmentsCompleted(), 0u);
    EXPECT_EQ(decoded_packets.size(), 0u);  // Not complete yet

    auto result2 = harness.processPacketNoCRC(frag2, sizeof(frag2));
    EXPECT_TRUE(result2.success) << result2.error;
    EXPECT_EQ(harness.fragmentsReceived(), 2u);
    EXPECT_EQ(harness.fragmentsCompleted(), 1u);
    EXPECT_EQ(decoded_packets.size(), 1u);  // Now complete

    if (!decoded_packets.empty()) {
        auto& pkt = decoded_packets[0];
        EXPECT_EQ(pkt.app_opcode, 0x0001);
        EXPECT_EQ(pkt.data.size(), 10u);
    }
}

// Test with real pcap data
class PcapDaybreakHarnessTest : public ::testing::Test {
protected:
    DaybreakTestHarness harness;
    std::vector<DecodedPacket> decoded_packets;
    std::vector<CapturedPacket> pcap_packets;

    void SetUp() override {
        decoded_packets.clear();
        harness.resetState();
        harness.setVerbose(false);
        harness.onPacketDecoded([this](const DecodedPacket& pkt) {
            decoded_packets.push_back(pkt);
        });

        // Load pcap file
        std::ifstream f(TEST_PCAP_FILE);
        if (!f.good()) {
            return;  // Will skip tests
        }

        PcapReadOptions options;
        options.remove_duplicates = true;
        options.server_to_client_only = true;
        options.filter_src_port = 7000;  // Zone server

        auto result = readPcapFile(TEST_PCAP_FILE, options);
        if (result.success) {
            pcap_packets = std::move(result.packets);
        }
    }

    bool hasPcapData() const {
        return !pcap_packets.empty();
    }
};

TEST_F(PcapDaybreakHarnessTest, ProcessZoneServerPackets) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    std::cout << "Processing " << pcap_packets.size() << " zone server packets" << std::endl;

    // Find and process OP_SessionResponse first
    for (const auto& pkt : pcap_packets) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == 0x02) {
            auto result = harness.processPacketNoCRC(pkt.data.data(), pkt.data.size());
            EXPECT_TRUE(result.success);
            break;
        }
    }

    ASSERT_TRUE(harness.sessionEstablished()) << "No session response found in pcap";
    std::cout << "Session established: compression="
              << harness.sessionParams().compression_enabled() << std::endl;

    // Process all packets
    size_t errors = 0;
    for (const auto& pkt : pcap_packets) {
        auto result = harness.processPacketNoCRC(pkt.data.data(), pkt.data.size());
        if (!result.success) {
            errors++;
            if (errors <= 5) {
                std::cout << "Error at frame " << pkt.frame_number << ": " << result.error << std::endl;
                std::cout << "  Data: " << pkt.hexDump(30) << std::endl;
            }
        }
    }

    std::cout << "\nProcessing Statistics:" << std::endl;
    std::cout << "  Packets processed: " << harness.packetsProcessed() << std::endl;
    std::cout << "  App packets decoded: " << harness.appPacketsDecoded() << std::endl;
    std::cout << "  Fragments received: " << harness.fragmentsReceived() << std::endl;
    std::cout << "  Fragments completed: " << harness.fragmentsCompleted() << std::endl;
    std::cout << "  Decode errors: " << harness.decodeErrors() << std::endl;
    std::cout << "  Process errors: " << errors << std::endl;

    // We expect some decoded packets
    EXPECT_GT(harness.appPacketsDecoded(), 0u) << "No app packets decoded";

    // Error rate should be reasonable (some errors expected with partial capture)
    double error_rate = static_cast<double>(errors) / pcap_packets.size();
    std::cout << "  Error rate: " << (error_rate * 100) << "%" << std::endl;
}

TEST_F(PcapDaybreakHarnessTest, ProcessFirstFewPackets_Verbose) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    // Enable verbose logging
    harness.setVerbose(true);
    harness.onLog([](const std::string& msg) {
        std::cout << "  [LOG] " << msg << std::endl;
    });

    // Find session response
    for (const auto& pkt : pcap_packets) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == 0x02) {
            std::cout << "Processing OP_SessionResponse (frame " << pkt.frame_number << ")" << std::endl;
            harness.processPacketNoCRC(pkt.data.data(), pkt.data.size());
            break;
        }
    }

    if (!harness.sessionEstablished()) {
        GTEST_SKIP() << "No session response found";
    }

    // Process first 20 non-session packets
    int count = 0;
    for (const auto& pkt : pcap_packets) {
        if (pkt.data.size() < 2) continue;
        if (pkt.data[0] == 0x00 && pkt.data[1] == 0x02) continue;  // Skip session response

        std::cout << "\n=== Frame " << pkt.frame_number << " ===" << std::endl;
        std::cout << "Raw (" << pkt.data.size() << " bytes): " << pkt.hexDump(40) << std::endl;

        decoded_packets.clear();
        auto result = harness.processPacketNoCRC(pkt.data.data(), pkt.data.size());

        std::cout << "Result: " << (result.success ? "OK" : "FAILED: " + result.error) << std::endl;
        std::cout << "Decoded " << decoded_packets.size() << " app packets" << std::endl;

        for (size_t i = 0; i < decoded_packets.size() && i < 3; i++) {
            std::cout << "  App[" << i << "]: opcode=0x" << std::hex << decoded_packets[i].app_opcode
                      << std::dec << " len=" << decoded_packets[i].data.size() << std::endl;
        }

        count++;
        if (count >= 20) break;
    }
}

TEST_F(PcapDaybreakHarnessTest, AnalyzeCompressedFragments) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    // Find session response first
    for (const auto& pkt : pcap_packets) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == 0x02) {
            harness.processPacketNoCRC(pkt.data.data(), pkt.data.size());
            break;
        }
    }

    if (!harness.sessionEstablished()) {
        GTEST_SKIP() << "No session response found";
    }

    std::cout << "\nAnalyzing compressed fragment packets:" << std::endl;

    int fragment_count = 0;
    int compressed_count = 0;

    for (const auto& pkt : pcap_packets) {
        if (pkt.data.size() < 4) continue;
        if (pkt.data[0] != 0x00) continue;

        uint8_t opcode = pkt.data[1];
        if (opcode >= 0x0d && opcode <= 0x10) {  // Fragment opcodes
            fragment_count++;

            // Check if payload starts with compression marker
            if (pkt.data.size() > 4 && pkt.data[2] == 0x5a) {
                compressed_count++;

                if (compressed_count <= 5) {
                    std::cout << "\nFrame " << pkt.frame_number << " (compressed fragment):" << std::endl;
                    std::cout << "  Raw: " << pkt.hexDump(40) << std::endl;

                    // The 5a is at position 2, meaning the entire payload after
                    // the 2-byte header is compressed. This includes the sequence!
                    std::cout << "  Note: Compression marker at offset 2 - sequence is compressed!" << std::endl;
                }
            }
        }
    }

    std::cout << "\nFragment analysis:" << std::endl;
    std::cout << "  Total fragments: " << fragment_count << std::endl;
    std::cout << "  Compressed fragments: " << compressed_count << std::endl;

    // This confirms our suspicion - fragments have compressed payloads
    // where the sequence number is part of the compressed data
    if (compressed_count > 0) {
        std::cout << "\n*** KEY FINDING: Fragment packets have compression marker at offset 2 ***" << std::endl;
        std::cout << "This means the sequence number is INSIDE the compressed payload!" << std::endl;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
