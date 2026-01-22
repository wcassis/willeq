/**
 * Test the ACTUAL DaybreakConnection code with pcap replay
 *
 * This test feeds real packets from a pcap capture directly through
 * the production DaybreakConnection::ProcessPacket() method to verify
 * the actual code handles compressed/fragmented packets correctly.
 */

// Include standard library headers BEFORE the private/public hack
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <chrono>
#include <functional>
#include <cstring>

#include <gtest/gtest.h>

// Access private members for testing
#define private public
#define protected public

#include "common/net/daybreak_connection.h"
#include "pcap_test_utils.h"

#undef private
#undef protected

using namespace EQ::Net;
using namespace EQ::Test;

// Path to test pcap file
static const char* TEST_PCAP_FILE = "/tmp/willeq_audit_capture2.pcap";

// Helper to dump packet bytes
static std::string hexDump(const uint8_t* data, size_t len, size_t max = 64) {
    std::ostringstream oss;
    for (size_t i = 0; i < len && i < max; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    if (len > max) oss << "...";
    return oss.str();
}

class DaybreakConnectionPcapTest : public ::testing::Test {
protected:
    std::unique_ptr<DaybreakConnectionManager> manager_;
    std::shared_ptr<DaybreakConnection> connection_;
    std::vector<CapturedPacket> pcap_packets_;
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> received_app_packets_;
    int error_count_ = 0;
    std::vector<std::string> errors_;

    void SetUp() override {
        // Load pcap packets
        std::ifstream f(TEST_PCAP_FILE);
        if (!f.good()) {
            return;
        }

        PcapReadOptions options;
        options.remove_duplicates = true;
        options.server_to_client_only = true;
        options.filter_src_port = 7000;  // Zone server only

        auto result = readPcapFile(TEST_PCAP_FILE, options);
        if (result.success) {
            pcap_packets_ = std::move(result.packets);
        }
    }

    void TearDown() override {
        connection_.reset();
        manager_.reset();
    }

    bool hasPcapData() const {
        return !pcap_packets_.empty();
    }

    // Create a manager without attaching to event loop (for testing)
    void createTestManager() {
        DaybreakConnectionManagerOptions opts;
        opts.port = 0;  // Don't bind
        opts.crc_length = 2;
        opts.encode_passes[0] = EncodeNone;
        opts.encode_passes[1] = EncodeNone;

        // Create manager - we'll access internals directly
        manager_ = std::make_unique<DaybreakConnectionManager>();

        // Set up packet receive callback
        manager_->OnPacketRecv([this](std::shared_ptr<DaybreakConnection> conn, const Packet& p) {
            if (p.Length() >= 2) {
                uint16_t opcode = p.GetUInt16(0);
                std::vector<uint8_t> data((uint8_t*)p.Data(), (uint8_t*)p.Data() + p.Length());
                received_app_packets_.push_back({opcode, data});
            }
        });

        manager_->OnErrorMessage([this](const std::string& msg) {
            error_count_++;
            errors_.push_back(msg);
            std::cout << "ERROR: " << msg << std::endl;
        });
    }

    // Create a connection directly (bypassing normal connection flow)
    void createTestConnection(const std::string& endpoint, int port) {
        // We need to create a connection manually
        // Using the client constructor and then setting it up as connected
        connection_ = std::shared_ptr<DaybreakConnection>(
            new DaybreakConnection(manager_.get(), endpoint, port));
        connection_->m_self = connection_;

        // Add to manager's connection map
        manager_->m_connections[std::make_pair(endpoint, port)] = connection_;
    }

    // Process a SessionResponse to set up the connection properly
    void processSessionResponse(const CapturedPacket& pkt, bool bypass_crc = false) {
        if (pkt.data.size() < 2) return;
        if (pkt.data[0] != 0x00 || pkt.data[1] != OP_SessionResponse) return;

        // Parse session response manually to set up connection
        if (pkt.data.size() >= 15) {
            // DaybreakConnectReply structure:
            // zero(1) + opcode(1) + connect_code(4) + encode_key(4) + crc_bytes(1) + encode_pass1(1) + encode_pass2(1) + max_packet_size(4)
            uint32_t connect_code = *(uint32_t*)&pkt.data[2];
            uint32_t encode_key = *(uint32_t*)&pkt.data[6];
            uint8_t crc_bytes = pkt.data[10];
            uint8_t encode_pass1 = pkt.data[11];
            uint8_t encode_pass2 = pkt.data[12];
            uint32_t max_packet_size = *(uint32_t*)&pkt.data[13];

            std::cout << "SessionResponse: connect_code=0x" << std::hex << connect_code
                      << " encode_key=0x" << encode_key
                      << " crc_bytes=" << std::dec << (int)crc_bytes
                      << " encode_pass1=" << (int)encode_pass1
                      << " encode_pass2=" << (int)encode_pass2
                      << " max_packet_size=" << max_packet_size << std::endl;

            // Set up the connection with these parameters
            connection_->m_connect_code = connect_code;
            connection_->m_encode_key = encode_key;
            // If bypassing CRC, set crc_bytes to 0 so ValidateCRC always returns true
            connection_->m_crc_bytes = bypass_crc ? 0 : crc_bytes;
            connection_->m_encode_passes[0] = (DaybreakEncodeType)encode_pass1;
            connection_->m_encode_passes[1] = (DaybreakEncodeType)encode_pass2;
            connection_->m_max_packet_size = max_packet_size;
            connection_->m_status = StatusConnected;

            if (bypass_crc) {
                std::cout << "  ** CRC BYPASSED for testing **" << std::endl;
            }
        }
    }

    // Strip CRC bytes from packet data (for when we want to bypass CRC but still have correct data)
    std::vector<uint8_t> stripCRC(const std::vector<uint8_t>& data, size_t crc_bytes = 2) {
        if (data.size() <= crc_bytes) return data;
        return std::vector<uint8_t>(data.begin(), data.end() - crc_bytes);
    }

    // Feed a packet through the REAL ProcessPacket
    void feedPacket(const CapturedPacket& pkt) {
        StaticPacket p((void*)pkt.data.data(), pkt.data.size());
        connection_->ProcessPacket(p);
    }

    // Feed a packet with CRC stripped (for bypass_crc mode)
    void feedPacketNoCRC(const CapturedPacket& pkt, size_t crc_bytes = 2) {
        auto stripped = stripCRC(pkt.data, crc_bytes);
        StaticPacket p((void*)stripped.data(), stripped.size());
        connection_->ProcessPacket(p);
    }
};

TEST_F(DaybreakConnectionPcapTest, ReplayZoneServerPackets) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    createTestManager();
    createTestConnection("172.18.0.3", 7000);

    std::cout << "\n========================================" << std::endl;
    std::cout << "REPLAYING " << pcap_packets_.size() << " ZONE SERVER PACKETS" << std::endl;
    std::cout << "Using REAL DaybreakConnection::ProcessPacket()" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Find and process SessionResponse first
    bool session_established = false;
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            std::cout << "Processing SessionResponse from frame " << pkt.frame_number << std::endl;
            processSessionResponse(pkt);
            session_established = true;
            break;
        }
    }

    ASSERT_TRUE(session_established) << "No SessionResponse found in pcap";
    ASSERT_TRUE(connection_->m_encode_passes[0] == EncodeCompression)
        << "Expected compression to be enabled for zone server";

    std::cout << "\nConnection configured:" << std::endl;
    std::cout << "  CRC bytes: " << connection_->m_crc_bytes << std::endl;
    std::cout << "  Encode pass 1: " << (int)connection_->m_encode_passes[0]
              << " (" << (connection_->m_encode_passes[0] == EncodeCompression ? "Compression" : "Other") << ")" << std::endl;
    std::cout << "  Encode pass 2: " << (int)connection_->m_encode_passes[1] << std::endl;

    // Now process all packets
    int packets_processed = 0;
    int fragment_packets = 0;
    int combined_packets = 0;
    int regular_packets = 0;
    int crc_failures = 0;

    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() < 2) continue;
        if (pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) continue;  // Skip session response

        // Track packet types
        if (pkt.data[0] == 0x00) {
            uint8_t opcode = pkt.data[1];
            if (opcode >= OP_Fragment && opcode <= OP_Fragment4) {
                fragment_packets++;
            } else if (opcode == OP_Combined) {
                combined_packets++;
            } else if (opcode >= OP_Packet && opcode <= OP_Packet4) {
                regular_packets++;
            }
        }

        // Feed through real ProcessPacket
        size_t errors_before = errors_.size();
        feedPacket(pkt);

        if (errors_.size() > errors_before) {
            // New error occurred
            if (packets_processed < 20) {
                std::cout << "Frame " << pkt.frame_number << " error: " << errors_.back() << std::endl;
                std::cout << "  Data: " << hexDump(pkt.data.data(), pkt.data.size(), 40) << std::endl;
            }
        }

        packets_processed++;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "REPLAY RESULTS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Packets processed: " << packets_processed << std::endl;
    std::cout << "  Fragment packets: " << fragment_packets << std::endl;
    std::cout << "  Combined packets: " << combined_packets << std::endl;
    std::cout << "  Regular packets: " << regular_packets << std::endl;
    std::cout << "App packets received: " << received_app_packets_.size() << std::endl;
    std::cout << "Errors: " << errors_.size() << std::endl;

    // Check fragment assembly state
    for (int i = 0; i < 4; i++) {
        auto& stream = connection_->m_streams[i];
        if (stream.fragment_total_bytes > 0) {
            std::cout << "\nStream " << i << " fragment state:" << std::endl;
            std::cout << "  Total bytes: " << stream.fragment_total_bytes << std::endl;
            std::cout << "  Current bytes: " << stream.fragment_current_bytes << std::endl;
            std::cout << "  Sequence in: " << stream.sequence_in << std::endl;

            // Check for suspicious values
            if (stream.fragment_total_bytes > 1000000) {
                std::cout << "  *** SUSPICIOUS: total_bytes > 1MB! ***" << std::endl;
            }
        }
    }

    // We should have received some app packets
    EXPECT_GT(received_app_packets_.size(), 0u) << "No application packets received";

    // Print first few app packets
    std::cout << "\nFirst 10 app packets received:" << std::endl;
    for (size_t i = 0; i < received_app_packets_.size() && i < 10; i++) {
        std::cout << "  [" << i << "] opcode=0x" << std::hex << received_app_packets_[i].first
                  << std::dec << " len=" << received_app_packets_[i].second.size() << std::endl;
    }
}

TEST_F(DaybreakConnectionPcapTest, DetailedFragmentTracking) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    createTestManager();
    createTestConnection("172.18.0.3", 7000);

    // Find and process SessionResponse
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            processSessionResponse(pkt);
            break;
        }
    }

    ASSERT_EQ(connection_->m_status, StatusConnected);

    std::cout << "\n========================================" << std::endl;
    std::cout << "DETAILED FRAGMENT TRACKING" << std::endl;
    std::cout << "========================================\n" << std::endl;

    int fragment_count = 0;
    int first_fragment_count = 0;

    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() < 4) continue;
        if (pkt.data[0] != 0x00) continue;

        uint8_t opcode = pkt.data[1];
        if (opcode < OP_Fragment || opcode > OP_Fragment4) continue;

        fragment_count++;

        // Check if this is a first fragment (has total_size header)
        bool is_first = (connection_->m_streams[0].fragment_total_bytes == 0);

        // Capture state BEFORE processing
        uint32_t total_before = connection_->m_streams[0].fragment_total_bytes;
        uint32_t current_before = connection_->m_streams[0].fragment_current_bytes;

        // Show raw packet details for first few fragments
        if (fragment_count <= 10) {
            std::cout << "Frame " << pkt.frame_number << " (Fragment #" << fragment_count << "):" << std::endl;
            std::cout << "  Raw (" << pkt.data.size() << " bytes): " << hexDump(pkt.data.data(), pkt.data.size(), 32) << std::endl;

            // Check for compression marker
            if (pkt.data.size() > 2 && pkt.data[2] == 0x5a) {
                std::cout << "  Compression: ZLIB (0x5a marker at offset 2)" << std::endl;
            } else if (pkt.data.size() > 2 && pkt.data[2] == 0xa5) {
                std::cout << "  Compression: None (0xa5 marker)" << std::endl;
            } else {
                std::cout << "  Compression: None (no marker, byte[2]=0x"
                          << std::hex << (int)pkt.data[2] << std::dec << ")" << std::endl;
            }
            std::cout << "  State before: total=" << total_before << " current=" << current_before << std::endl;
        }

        // Process through real code
        feedPacket(pkt);

        // Capture state AFTER processing
        uint32_t total_after = connection_->m_streams[0].fragment_total_bytes;
        uint32_t current_after = connection_->m_streams[0].fragment_current_bytes;

        if (fragment_count <= 10) {
            std::cout << "  State after:  total=" << total_after << " current=" << current_after << std::endl;

            if (total_before == 0 && total_after > 0) {
                first_fragment_count++;
                std::cout << "  ** FIRST FRAGMENT: total_size=" << total_after << " **" << std::endl;

                // Check if total_size is suspiciously large
                if (total_after > 100000) {
                    std::cout << "  *** WARNING: total_size=" << total_after << " seems too large! ***" << std::endl;
                }
            }

            if (total_after == 0 && total_before > 0) {
                std::cout << "  ** FRAGMENT ASSEMBLY COMPLETE **" << std::endl;
            }

            std::cout << std::endl;
        }
    }

    std::cout << "Total fragments processed: " << fragment_count << std::endl;
    std::cout << "First fragments seen: " << first_fragment_count << std::endl;
    std::cout << "App packets received: " << received_app_packets_.size() << std::endl;
}

TEST_F(DaybreakConnectionPcapTest, SingleCompressedFragmentDebug) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    createTestManager();
    createTestConnection("172.18.0.3", 7000);

    // Find and process SessionResponse
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            processSessionResponse(pkt);
            break;
        }
    }

    ASSERT_EQ(connection_->m_status, StatusConnected);

    std::cout << "\n========================================" << std::endl;
    std::cout << "SINGLE COMPRESSED FRAGMENT DEBUG" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Find the first compressed fragment packet
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() < 4) continue;
        if (pkt.data[0] != 0x00) continue;

        uint8_t opcode = pkt.data[1];
        if (opcode < OP_Fragment || opcode > OP_Fragment4) continue;

        // Look for compression marker
        if (pkt.data[2] != 0x5a) continue;

        std::cout << "Found compressed fragment at frame " << pkt.frame_number << std::endl;
        std::cout << "Raw packet (" << pkt.data.size() << " bytes):" << std::endl;

        // Full hex dump
        for (size_t i = 0; i < pkt.data.size(); i += 16) {
            std::cout << std::setw(4) << std::setfill('0') << std::hex << i << ": ";
            for (size_t j = i; j < i + 16 && j < pkt.data.size(); j++) {
                std::cout << std::setw(2) << (int)pkt.data[j] << " ";
            }
            std::cout << std::endl;
        }
        std::cout << std::dec;

        std::cout << "\nStream 0 state BEFORE:" << std::endl;
        std::cout << "  fragment_total_bytes: " << connection_->m_streams[0].fragment_total_bytes << std::endl;
        std::cout << "  fragment_current_bytes: " << connection_->m_streams[0].fragment_current_bytes << std::endl;
        std::cout << "  sequence_in: " << connection_->m_streams[0].sequence_in << std::endl;

        // Process the packet
        std::cout << "\nCalling ProcessPacket()..." << std::endl;
        feedPacket(pkt);

        std::cout << "\nStream 0 state AFTER:" << std::endl;
        std::cout << "  fragment_total_bytes: " << connection_->m_streams[0].fragment_total_bytes << std::endl;
        std::cout << "  fragment_current_bytes: " << connection_->m_streams[0].fragment_current_bytes << std::endl;
        std::cout << "  sequence_in: " << connection_->m_streams[0].sequence_in << std::endl;

        // Check for problems
        if (connection_->m_streams[0].fragment_total_bytes > 100000) {
            std::cout << "\n*** BUG DETECTED: fragment_total_bytes is suspiciously large! ***" << std::endl;
            std::cout << "Expected: ~20000-100000 bytes for typical fragments" << std::endl;
            std::cout << "Got: " << connection_->m_streams[0].fragment_total_bytes << " bytes" << std::endl;
        }

        break;  // Only process first compressed fragment
    }
}

TEST_F(DaybreakConnectionPcapTest, ReplayWithCRCBypassed) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    createTestManager();
    createTestConnection("172.18.0.3", 7000);

    std::cout << "\n========================================" << std::endl;
    std::cout << "REPLAYING WITH CRC BYPASSED" << std::endl;
    std::cout << "Using REAL DaybreakConnection::ProcessPacket()" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Find and process SessionResponse with CRC bypass
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            processSessionResponse(pkt, true);  // bypass_crc = true
            break;
        }
    }

    ASSERT_EQ(connection_->m_status, StatusConnected);
    ASSERT_EQ(connection_->m_crc_bytes, 0u) << "CRC should be bypassed";
    ASSERT_TRUE(connection_->m_encode_passes[0] == EncodeCompression);

    // Process all packets (skipping session response)
    int packets_processed = 0;
    int fragment_first = 0;
    int fragment_continuation = 0;
    int fragment_complete = 0;

    std::vector<uint32_t> total_sizes_seen;

    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() < 4) continue;
        if (pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) continue;

        // Track fragment state before
        uint32_t total_before = connection_->m_streams[0].fragment_total_bytes;

        // Feed packet with CRC stripped
        feedPacketNoCRC(pkt, 2);

        // Track fragment state after
        uint32_t total_after = connection_->m_streams[0].fragment_total_bytes;

        // Detect first fragments
        if (total_before == 0 && total_after > 0) {
            fragment_first++;
            total_sizes_seen.push_back(total_after);

            if (total_after > 1000000) {
                std::cout << "*** SUSPICIOUS total_size=" << total_after
                          << " at frame " << pkt.frame_number << " ***" << std::endl;
                std::cout << "  Raw data: " << hexDump(pkt.data.data(), pkt.data.size(), 32) << std::endl;
            }
        }

        // Detect fragment continuations
        if (total_before > 0 && total_after > 0 && total_after == total_before) {
            fragment_continuation++;
        }

        // Detect fragment completions
        if (total_before > 0 && total_after == 0) {
            fragment_complete++;
        }

        packets_processed++;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "RESULTS (CRC BYPASSED)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Packets processed: " << packets_processed << std::endl;
    std::cout << "First fragments: " << fragment_first << std::endl;
    std::cout << "Continuation fragments: " << fragment_continuation << std::endl;
    std::cout << "Completed fragments: " << fragment_complete << std::endl;
    std::cout << "App packets received: " << received_app_packets_.size() << std::endl;
    std::cout << "Errors: " << errors_.size() << std::endl;

    // Print total_sizes seen
    std::cout << "\nFragment total_sizes seen:" << std::endl;
    for (size_t i = 0; i < total_sizes_seen.size() && i < 20; i++) {
        std::cout << "  [" << i << "] " << total_sizes_seen[i] << " bytes" << std::endl;
    }
    if (total_sizes_seen.size() > 20) {
        std::cout << "  ... and " << (total_sizes_seen.size() - 20) << " more" << std::endl;
    }

    // Check for suspicious values
    bool has_suspicious = false;
    for (auto size : total_sizes_seen) {
        if (size > 1000000) {  // > 1MB is suspicious
            has_suspicious = true;
            break;
        }
    }

    if (has_suspicious) {
        std::cout << "\n*** BUG DETECTED: Suspicious fragment_total_bytes values! ***" << std::endl;
    } else {
        std::cout << "\nAll fragment total_sizes appear reasonable." << std::endl;
    }

    // Final stream state
    std::cout << "\nFinal stream states:" << std::endl;
    for (int i = 0; i < 4; i++) {
        auto& stream = connection_->m_streams[i];
        if (stream.sequence_in > 0 || stream.fragment_total_bytes > 0) {
            std::cout << "  Stream " << i << ": seq_in=" << stream.sequence_in
                      << " frag_total=" << stream.fragment_total_bytes
                      << " frag_current=" << stream.fragment_current_bytes << std::endl;
        }
    }

    // We should have received many app packets with CRC bypassed
    EXPECT_GT(received_app_packets_.size(), 100u)
        << "Expected to receive >100 app packets with CRC bypassed";
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
