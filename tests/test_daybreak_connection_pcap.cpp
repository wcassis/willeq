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
#include "common/net/posix_udp_transport.h"
#include "pcap_test_utils.h"

#undef private
#undef protected

using namespace EQ::Net;
using namespace EQ::Test;

// Path to test pcap file: WILLEQ_PCAP env var, else data/captures/session.pcap
static const std::string TEST_PCAP_FILE = getTestPcapPath();

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
        options.filter_src_port = getTestZonePort();  // Zone server only (autodiscovered)

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

        // Create manager with a real (but unopened) transport - we don't call
        // Tick()/open() in these tests, we drive ProcessPacket() directly, so
        // no socket is ever bound. We'll access internals directly.
        manager_ = std::make_unique<DaybreakConnectionManager>(opts, std::make_unique<PosixUdpTransport>());

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

    // Drive a captured OP_SessionResponse through the PRODUCTION handshake.
    //
    // T364/B009: this used to memcpy connect_code / encode_key /
    // max_packet_size straight off the wire into the connection's private
    // members -- i.e. it re-implemented, in host byte order, exactly the bug
    // T21 fixed in `daybreak_connection_decode.cpp`'s OP_SessionResponse arm.
    // A regression of that fix could not fail this test. It now feeds the
    // recorded bytes through `DaybreakConnection::ProcessPacket()`, so the
    // production parse is the thing under test; the only value the test
    // supplies is our own connect code, which the live client picks at random
    // and the recorded server echoes back.
    //
    // OP_SessionResponse is exempt from `PacketCanBeEncoded()`, so it reaches
    // the handshake arm without CRC or decompression.
    void processSessionResponse(const CapturedPacket& pkt, bool bypass_crc = false) {
        ASSERT_GE(pkt.data.size(), DaybreakConnectReply::size());
        ASSERT_EQ(pkt.data[0], 0);
        ASSERT_EQ(pkt.data[1], OP_SessionResponse);

        uint32_t wire_code = 0;
        memcpy(&wire_code, &pkt.data[2], 4);
        connection_->m_connect_code = NetworkToHost(wire_code);
        connection_->m_status = StatusConnecting;

        std::vector<uint8_t> buf(pkt.data);
        StaticPacket p(buf.data(), buf.size());
        connection_->ProcessPacket(p);

        ASSERT_EQ(connection_->m_status, StatusConnected)
            << "recorded OP_SessionResponse was rejected by the production handshake";

        std::cout << "SessionResponse (via production parse): connect_code=0x" << std::hex
                  << connection_->m_connect_code
                  << " encode_key=0x" << connection_->m_encode_key
                  << " crc_bytes=" << std::dec << connection_->m_crc_bytes
                  << " encode_pass1=" << (int)connection_->m_encode_passes[0]
                  << " encode_pass2=" << (int)connection_->m_encode_passes[1]
                  << " max_packet_size=" << connection_->m_max_packet_size << std::endl;

        // Only after the production parse: zeroing crc_bytes makes ValidateCRC
        // a no-op so the CRC-bypass replay can feed CRC-stripped payloads.
        if (bypass_crc) {
            connection_->m_crc_bytes = 0;
            std::cout << "  ** CRC BYPASSED for testing **" << std::endl;
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
        GTEST_SKIP() << "no capture at " << TEST_PCAP_FILE;
    }

    createTestManager();
    createTestConnection(getTestZoneIp(), getTestZonePort());

    std::cout << "\n========================================" << std::endl;
    std::cout << "REPLAYING " << pcap_packets_.size() << " ZONE SERVER PACKETS" << std::endl;
    std::cout << "Using REAL DaybreakConnection::ProcessPacket()" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Find and process SessionResponse first
    bool session_established = false;
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            std::cout << "Processing SessionResponse from frame " << pkt.frame_number << std::endl;
            ASSERT_NO_FATAL_FAILURE(processSessionResponse(pkt));
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
        GTEST_SKIP() << "no capture at " << TEST_PCAP_FILE;
    }

    createTestManager();
    createTestConnection(getTestZoneIp(), getTestZonePort());

    // Find and process SessionResponse
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            ASSERT_NO_FATAL_FAILURE(processSessionResponse(pkt));
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
        GTEST_SKIP() << "no capture at " << TEST_PCAP_FILE;
    }

    createTestManager();
    createTestConnection(getTestZoneIp(), getTestZonePort());

    // Find and process SessionResponse
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            ASSERT_NO_FATAL_FAILURE(processSessionResponse(pkt));
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
        GTEST_SKIP() << "no capture at " << TEST_PCAP_FILE;
    }

    createTestManager();
    createTestConnection(getTestZoneIp(), getTestZonePort());

    std::cout << "\n========================================" << std::endl;
    std::cout << "REPLAYING WITH CRC BYPASSED" << std::endl;
    std::cout << "Using REAL DaybreakConnection::ProcessPacket()" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // Find and process SessionResponse with CRC bypass
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            ASSERT_NO_FATAL_FAILURE(processSessionResponse(pkt, true));  // bypass_crc = true
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

// --- B009: the handshake fields are byte-swapped by PRODUCTION ------------
//
// T21 fixed `DaybreakConnection`'s OP_SessionResponse arm to read
// connect_code / encode_key / max_packet_size through `NetworkToHost()`.
// Nothing could catch a regression of that fix, because this file parsed the
// same three fields itself, raw, and every OP_SessionResponse in the recorded
// capture carries a byte-swap PALINDROME for two of them: eqemu's connect_code
// is 0x00000000 or 0xFFFFFFFF and its encode_key 0x00000000 or 0xFFFFFFFF
// (T21's own finding -- the RNG had collapsed to those two values). Only
// max_packet_size differs raw vs swapped in the capture: network-order 512
// reads as 131072 raw.
//
// So the recorded capture can bite on ONE of the three fields, and the other
// two need synthesised bytes. Both are covered below.

// Capture-driven: the one field the recorded handshake can discriminate.
TEST_F(DaybreakConnectionPcapTest, RecordedHandshakeYieldsNetworkOrderMaxPacketSize) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "no capture at " << TEST_PCAP_FILE;
    }

    createTestManager();
    createTestConnection(getTestZoneIp(), getTestZonePort());

    const CapturedPacket* session_response = nullptr;
    for (const auto& pkt : pcap_packets_) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == OP_SessionResponse) {
            session_response = &pkt;
            break;
        }
    }
    ASSERT_NE(session_response, nullptr) << "No SessionResponse found in pcap";

    ASSERT_NO_FATAL_FAILURE(processSessionResponse(*session_response));

    // eqemu sends max_packet_size in network order. Read raw (the pre-T21
    // production bug, and what this test file used to do itself) the same
    // four bytes are 131072 -- 256x the real limit.
    EXPECT_EQ(connection_->m_max_packet_size, 512u)
        << "max_packet_size was not byte-swapped by the production handshake";
    EXPECT_EQ(connection_->m_crc_bytes, 2u);
    EXPECT_EQ(connection_->m_encode_passes[0], EncodeCompression);
    EXPECT_EQ(connection_->m_encode_passes[1], EncodeNone);
}

// Synthesised: non-palindromic connect_code and encode_key, which no recorded
// eqemu handshake provides. The wire bytes are written out big-endian BY HAND
// so the expectation does not depend on the same endian helper the production
// code uses.
TEST_F(DaybreakConnectionPcapTest, SynthesisedHandshakeByteSwapsAllThreeFields) {
    createTestManager();
    createTestConnection("10.0.0.1", 7000);

    // zero, opcode, connect_code(4 BE), encode_key(4 BE), crc_bytes,
    // encode_pass1, encode_pass2, max_packet_size(4 BE)
    const std::vector<uint8_t> wire = {
        0x00, OP_SessionResponse,
        0x11, 0x22, 0x33, 0x44,   // connect_code  -> host 0x11223344
        0xA1, 0xB2, 0xC3, 0xD4,   // encode_key    -> host 0xA1B2C3D4
        0x02,                     // crc_bytes
        0x01,                     // encode_pass1 (EncodeCompression)
        0x00,                     // encode_pass2 (EncodeNone)
        0x00, 0x00, 0x02, 0x00,   // max_packet_size -> host 512
    };
    ASSERT_EQ(wire.size(), DaybreakConnectReply::size());

    // What SendConnect() would have picked for us, in host order.
    connection_->m_connect_code = 0x11223344u;
    connection_->m_status = StatusConnecting;

    std::vector<uint8_t> buf(wire);
    StaticPacket p(buf.data(), buf.size());
    connection_->ProcessPacket(p);

    // The connect_code compare itself is a byte-swap: read raw it is
    // 0x44332211, which does not match, and the handshake is silently ignored.
    ASSERT_EQ(connection_->m_status, StatusConnected)
        << "handshake rejected: connect_code was not compared in host order";

    EXPECT_EQ(connection_->m_encode_key, 0xA1B2C3D4u)
        << "encode_key was not byte-swapped (raw would be 0xD4C3B2A1)";
    EXPECT_EQ(connection_->m_max_packet_size, 512u)
        << "max_packet_size was not byte-swapped (raw would be 131072)";
    EXPECT_EQ(connection_->m_crc_bytes, 2u);
    EXPECT_EQ(connection_->m_encode_passes[0], EncodeCompression);
    EXPECT_EQ(connection_->m_encode_passes[1], EncodeNone);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
