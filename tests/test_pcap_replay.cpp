/**
 * Test that replays packets from a pcap file through DaybreakConnection
 * to find where specific packets get lost.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <zlib.h>
#include "common/net/daybreak_connection.h"
#include "common/net/packet.h"

// PCAP file format structures
#pragma pack(push, 1)
struct PcapHeader {
    uint32_t magic_number;
    uint16_t version_major;
    uint16_t version_minor;
    int32_t thiszone;
    uint32_t sigfigs;
    uint32_t snaplen;
    uint32_t network;
};

struct PcapRecordHeader {
    uint32_t ts_sec;
    uint32_t ts_usec;
    uint32_t incl_len;
    uint32_t orig_len;
};

// Linux cooked capture v2 header (SLL2)
struct Sll2Header {
    uint16_t protocol_type;
    uint16_t reserved;
    uint32_t interface_index;
    uint16_t arphrd_type;
    uint8_t packet_type;
    uint8_t addr_len;
    uint8_t addr[8];
};

struct IpHeader {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
};

struct UdpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};
#pragma pack(pop)

struct CapturedPacket {
    uint32_t timestamp_sec;
    uint32_t timestamp_usec;
    uint16_t src_port;
    uint16_t dst_port;
    std::vector<uint8_t> data;
};

// Extract UDP packets from pcap file
std::vector<CapturedPacket> extractPacketsFromPcap(const std::string& filename) {
    std::vector<CapturedPacket> packets;
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open pcap file: " << filename << std::endl;
        return packets;
    }

    PcapHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Check magic number (handles both endianness)
    // 0xa1b2c3d4 = native byte order (little-endian on little-endian system)
    // 0xd4c3b2a1 = swapped byte order (need to swap fields)
    bool swap_bytes = false;
    if (header.magic_number == 0xa1b2c3d4) {
        swap_bytes = false;  // Native order, no swap needed
    } else if (header.magic_number == 0xd4c3b2a1) {
        swap_bytes = true;   // Swapped order, need to swap fields
    } else {
        std::cerr << "Invalid pcap magic: " << std::hex << header.magic_number << std::endl;
        return packets;
    }
    std::cout << "PCAP magic: 0x" << std::hex << header.magic_number << std::dec
              << ", swap_bytes=" << swap_bytes << std::endl;

    // Determine link type
    uint32_t network = swap_bytes ? __builtin_bswap32(header.network) : header.network;
    std::cout << "Network type: " << network << std::endl;
    size_t link_header_size = 0;
    if (network == 113) { // LINKTYPE_LINUX_SLL
        link_header_size = 16;
    } else if (network == 276) { // LINKTYPE_LINUX_SLL2
        link_header_size = 20;
    } else if (network == 1) { // LINKTYPE_ETHERNET
        link_header_size = 14;
    } else {
        std::cerr << "Unsupported link type: " << network << std::endl;
        return packets;
    }

    try {
    while (file) {
        PcapRecordHeader rec;
        file.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!file) break;

        uint32_t incl_len = swap_bytes ? __builtin_bswap32(rec.incl_len) : rec.incl_len;
        uint32_t ts_sec = swap_bytes ? __builtin_bswap32(rec.ts_sec) : rec.ts_sec;
        uint32_t ts_usec = swap_bytes ? __builtin_bswap32(rec.ts_usec) : rec.ts_usec;

        // Debug packets
        if (packets.size() < 5 || (packets.size() % 1000 == 0) || packets.size() >= 1100) {
            std::cout << "Packet " << packets.size() << ": incl_len=" << incl_len
                      << " ts_sec=" << ts_sec << std::endl;
        }

        // Sanity check on packet length
        if (incl_len > 65535 || incl_len == 0) {
            std::cerr << "Invalid packet length: " << incl_len << " at packet " << packets.size() << std::endl;
            break;
        }

        std::vector<uint8_t> raw_data(incl_len);
        file.read(reinterpret_cast<char*>(raw_data.data()), incl_len);
        if (!file) break;

        if (incl_len < link_header_size + sizeof(IpHeader) + sizeof(UdpHeader)) {
            continue;
        }

        // Skip link layer header
        size_t offset = link_header_size;

        // Parse IP header
        const IpHeader* ip = reinterpret_cast<const IpHeader*>(raw_data.data() + offset);
        uint8_t ip_header_len = (ip->version_ihl & 0x0F) * 4;
        if (ip->protocol != 17) { // Not UDP
            continue;
        }
        offset += ip_header_len;

        // Parse UDP header
        const UdpHeader* udp = reinterpret_cast<const UdpHeader*>(raw_data.data() + offset);
        uint16_t src_port = ntohs(udp->src_port);
        uint16_t dst_port = ntohs(udp->dst_port);
        uint16_t udp_len = ntohs(udp->length);
        offset += sizeof(UdpHeader);

        // Extract UDP payload
        int payload_len_signed = (int)udp_len - (int)sizeof(UdpHeader);
        if (payload_len_signed <= 0) {
            continue;  // Skip empty or invalid UDP
        }
        size_t payload_len = payload_len_signed;
        if (offset + payload_len > raw_data.size()) {
            payload_len = raw_data.size() - offset;
        }
        if (payload_len == 0 || payload_len > 65535) {
            continue;  // Skip invalid payloads
        }

        CapturedPacket pkt;
        pkt.timestamp_sec = ts_sec;
        pkt.timestamp_usec = ts_usec;
        pkt.src_port = src_port;
        pkt.dst_port = dst_port;
        pkt.data.assign(raw_data.begin() + offset, raw_data.begin() + offset + payload_len);
        packets.push_back(pkt);

        // Limit packets for debugging
        if (packets.size() >= 10000) {
            std::cout << "Reached packet limit of 10000" << std::endl;
            break;
        }
    }
    } catch (const std::exception& e) {
        std::cerr << "Exception at packet " << packets.size() << ": " << e.what() << std::endl;
    }

    return packets;
}

// Helper to decompress data with 0x5a marker
std::vector<uint8_t> decompressData(const uint8_t* data, size_t len) {
    std::vector<uint8_t> result;
    if (len < 2 || data[0] != 0x5a) {
        return result;
    }

    uint8_t buffer[8192];
    z_stream zstream;
    memset(&zstream, 0, sizeof(zstream));
    zstream.next_in = const_cast<uint8_t*>(data + 1);
    zstream.avail_in = len - 1;
    zstream.next_out = buffer;
    zstream.avail_out = sizeof(buffer);

    if (inflateInit(&zstream) != Z_OK) {
        return result;
    }

    int ret = inflate(&zstream, Z_FINISH);
    inflateEnd(&zstream);

    if (ret == Z_STREAM_END) {
        result.assign(buffer, buffer + zstream.total_out);
    }
    return result;
}

// Search for ClientUpdate in packet data
bool findClientUpdate(const std::vector<uint8_t>& data, uint16_t target_spawn_id) {
    const uint8_t opcode_low = 0xcb;
    const uint8_t opcode_high = 0x14;
    uint8_t spawn_low = target_spawn_id & 0xFF;
    uint8_t spawn_high = (target_spawn_id >> 8) & 0xFF;

    for (size_t i = 0; i + 3 < data.size(); i++) {
        if (data[i] == opcode_low && data[i+1] == opcode_high &&
            data[i+2] == spawn_low && data[i+3] == spawn_high) {
            return true;
        }
    }
    return false;
}

class PacketReplayTest : public ::testing::Test {
protected:
    std::vector<CapturedPacket> packets_;
    uint16_t target_spawn_id_ = 550;

    void SetUp() override {
        packets_ = extractPacketsFromPcap("/tmp/willeq_cap.pcap");
        std::cout << "Loaded " << packets_.size() << " packets from /tmp/willeq_cap.pcap" << std::endl;
    }
};

TEST_F(PacketReplayTest, FindClientUpdateInRawPackets) {
    // First, verify the ClientUpdate exists in the pcap
    int found_count = 0;
    int packet_index = -1;
    int combined_compressed_count = 0;

    for (size_t i = 0; i < packets_.size(); i++) {
        const auto& pkt = packets_[i];
        // Only look at S->C packets (from zone server port 7007)
        if (pkt.src_port != 7007) continue;

        // Check if this is a compressed Combined packet (00 03 5a 78...)
        if (pkt.data.size() >= 4 &&
            pkt.data[0] == 0x00 && pkt.data[1] == 0x03 &&
            pkt.data[2] == 0x5a && pkt.data[3] == 0x78) {

            combined_compressed_count++;
            // Decompress and search
            std::vector<uint8_t> decompressed = decompressData(pkt.data.data() + 2, pkt.data.size() - 2);

            // Debug first few compressed Combined packets
            if (combined_compressed_count <= 3) {
                std::cout << "Combined packet " << combined_compressed_count << " at index " << i << ": ";
                for (size_t j = 0; j < 20 && j < pkt.data.size(); j++) {
                    printf("%02x", pkt.data[j]);
                }
                std::cout << " -> decompressed " << decompressed.size() << " bytes" << std::endl;
            }

            if (findClientUpdate(decompressed, target_spawn_id_)) {
                found_count++;
                packet_index = i;
                std::cout << "Found ClientUpdate for spawn_id=" << target_spawn_id_
                          << " in packet " << i << " (compressed Combined)" << std::endl;
                std::cout << "  Raw packet first 20 bytes: ";
                for (size_t j = 0; j < 20 && j < pkt.data.size(); j++) {
                    printf("%02x", pkt.data[j]);
                }
                std::cout << std::endl;
            }
        }

        // Also check uncompressed packets
        if (findClientUpdate(pkt.data, target_spawn_id_)) {
            found_count++;
            std::cout << "Found ClientUpdate for spawn_id=" << target_spawn_id_
                      << " in packet " << i << " (uncompressed)" << std::endl;
        }
    }
    std::cout << "Found " << combined_compressed_count << " compressed Combined packets" << std::endl;

    // Look for the specific packet from Python output: 00035a78013591bd...
    for (size_t i = 0; i < packets_.size(); i++) {
        const auto& pkt = packets_[i];
        if (pkt.data.size() >= 10 &&
            pkt.data[0] == 0x00 && pkt.data[1] == 0x03 &&
            pkt.data[2] == 0x5a && pkt.data[3] == 0x78 &&
            pkt.data[4] == 0x01 && pkt.data[5] == 0x35) {
            std::cout << "Found target packet at index " << i << ": ";
            for (size_t j = 0; j < 20 && j < pkt.data.size(); j++) {
                printf("%02x", pkt.data[j]);
            }
            std::cout << std::endl;
        }
    }

    std::cout << "Total S->C packets from port 7007: "
              << std::count_if(packets_.begin(), packets_.end(),
                              [](const CapturedPacket& p) { return p.src_port == 7007; })
              << std::endl;
    std::cout << "Found " << found_count << " packets containing ClientUpdate for spawn_id="
              << target_spawn_id_ << std::endl;

    EXPECT_GT(found_count, 0) << "ClientUpdate packet not found in pcap!";
}

TEST_F(PacketReplayTest, AnalyzePacketBeforeClientUpdate) {
    // Find the packet containing our ClientUpdate
    int target_packet = -1;
    std::vector<uint8_t> target_raw;

    for (size_t i = 0; i < packets_.size(); i++) {
        const auto& pkt = packets_[i];
        if (pkt.src_port != 7007) continue;

        if (pkt.data.size() >= 4 &&
            pkt.data[0] == 0x00 && pkt.data[1] == 0x03 &&
            pkt.data[2] == 0x5a && pkt.data[3] == 0x78) {

            std::vector<uint8_t> decompressed = decompressData(pkt.data.data() + 2, pkt.data.size() - 2);
            if (findClientUpdate(decompressed, target_spawn_id_)) {
                target_packet = i;
                target_raw = pkt.data;
                break;
            }
        }
    }

    ASSERT_NE(target_packet, -1) << "Could not find packet with ClientUpdate";

    std::cout << "\n=== Packet " << target_packet << " Analysis ===" << std::endl;
    std::cout << "Raw length: " << target_raw.size() << " bytes" << std::endl;
    std::cout << "First 40 bytes: ";
    for (size_t i = 0; i < 40 && i < target_raw.size(); i++) {
        printf("%02x ", target_raw[i]);
    }
    std::cout << std::endl;

    // Parse the packet structure
    std::cout << "\nPacket structure:" << std::endl;
    std::cout << "  [0-1] Protocol header: " << std::hex << (int)target_raw[0] << " " << (int)target_raw[1] << std::dec << std::endl;
    std::cout << "  [2]   Compression marker: 0x" << std::hex << (int)target_raw[2] << std::dec << std::endl;
    std::cout << "  [3+]  Zlib data starting with: 0x" << std::hex << (int)target_raw[3] << std::dec << std::endl;

    // Decompress and analyze
    std::vector<uint8_t> decompressed = decompressData(target_raw.data() + 2, target_raw.size() - 2);
    std::cout << "\nDecompressed length: " << decompressed.size() << " bytes" << std::endl;
    std::cout << "Decompressed first 60 bytes: ";
    for (size_t i = 0; i < 60 && i < decompressed.size(); i++) {
        printf("%02x ", decompressed[i]);
    }
    std::cout << std::endl;

    // Parse Combined subpackets
    std::cout << "\n=== Parsing Combined Subpackets ===" << std::endl;
    size_t offset = 0;
    int subpacket_num = 0;
    while (offset < decompressed.size()) {
        uint8_t subpacket_len = decompressed[offset];
        offset++;

        if (offset + subpacket_len > decompressed.size()) {
            std::cout << "Subpacket " << subpacket_num << ": truncated (claims " << (int)subpacket_len
                      << " bytes, only " << (decompressed.size() - offset) << " remain)" << std::endl;
            break;
        }

        subpacket_num++;
        uint16_t opcode = 0;
        uint16_t spawn_id = 0;
        if (subpacket_len >= 2) {
            opcode = decompressed[offset] | (decompressed[offset + 1] << 8);
        }
        if (subpacket_len >= 4) {
            spawn_id = decompressed[offset + 2] | (decompressed[offset + 3] << 8);
        }

        std::cout << "Subpacket " << subpacket_num << ": " << (int)subpacket_len << " bytes, "
                  << "opcode=0x" << std::hex << opcode << std::dec << ", spawn_id=" << spawn_id;

        if (opcode == 0x14cb) {
            std::cout << " *** ClientUpdate ***";
            if (spawn_id == target_spawn_id_) {
                std::cout << " *** THIS IS OUR TARGET! ***";
            }
        }
        std::cout << std::endl;

        // Print first 10 bytes of subpacket
        std::cout << "         Data: ";
        for (size_t i = 0; i < 10 && i < subpacket_len; i++) {
            printf("%02x ", decompressed[offset + i]);
        }
        std::cout << std::endl;

        offset += subpacket_len;
    }
}

TEST_F(PacketReplayTest, ListAllServerPacketTypes) {
    // List all unique packet types from the zone server
    std::map<std::pair<uint8_t, uint8_t>, int> packet_types;

    for (const auto& pkt : packets_) {
        if (pkt.src_port != 7007) continue;
        if (pkt.data.size() < 2) continue;

        auto key = std::make_pair(pkt.data[0], pkt.data[1]);
        packet_types[key]++;
    }

    std::cout << "\n=== Server Packet Types ===" << std::endl;
    for (const auto& [key, count] : packet_types) {
        printf("  [%02x %02x]: %d packets\n", key.first, key.second, count);
    }
}

// Simulate the Combined packet parsing logic from DaybreakConnection
void simulateCombinedParsing(const std::vector<uint8_t>& decompressed, uint16_t target_spawn_id) {
    std::cout << "\n=== Simulating Combined Packet Parsing ===" << std::endl;
    std::cout << "Decompressed data: " << decompressed.size() << " bytes" << std::endl;

    // Combined packet parsing (from daybreak_connection.cpp line 769-796)
    const uint8_t* current = decompressed.data();
    const uint8_t* end = decompressed.data() + decompressed.size();
    int subpacket_count = 0;
    int clientupdate_count = 0;
    bool found_target = false;

    while (current < end) {
        uint8_t subpacket_length = *current;
        current++;

        if (end < current + subpacket_length) {
            std::cout << "Subpacket " << subpacket_count << " truncated: claims " << (int)subpacket_length
                      << " bytes but only " << (end - current) << " remain" << std::endl;
            break;
        }

        subpacket_count++;
        uint16_t app_opcode = 0;
        uint16_t spawn_id = 0;
        if (subpacket_length >= 2) {
            app_opcode = current[0] | (current[1] << 8);
            if (subpacket_length >= 4) {
                spawn_id = current[2] | (current[3] << 8);
            }
        }

        // Check if this is ClientUpdate
        if (app_opcode == 0x14cb) {
            clientupdate_count++;
            std::cout << "ClientUpdate #" << clientupdate_count << ": spawn_id=" << spawn_id;
            if (spawn_id == target_spawn_id) {
                std::cout << " *** TARGET FOUND! ***";
                found_target = true;
            }
            std::cout << std::endl;
        }

        current += subpacket_length;
    }

    std::cout << "Total subpackets: " << subpacket_count << std::endl;
    std::cout << "ClientUpdate packets: " << clientupdate_count << std::endl;
    std::cout << "Target spawn_id " << target_spawn_id << " found: " << (found_target ? "YES" : "NO") << std::endl;
}

TEST_F(PacketReplayTest, SimulateCombinedParsing) {
    // Find the packet containing our target ClientUpdate
    for (size_t i = 0; i < packets_.size(); i++) {
        const auto& pkt = packets_[i];
        if (pkt.src_port != 7007) continue;

        if (pkt.data.size() >= 4 &&
            pkt.data[0] == 0x00 && pkt.data[1] == 0x03 &&
            pkt.data[2] == 0x5a && pkt.data[3] == 0x78) {

            std::vector<uint8_t> decompressed = decompressData(pkt.data.data() + 2, pkt.data.size() - 2);
            if (findClientUpdate(decompressed, target_spawn_id_)) {
                std::cout << "Found target in packet " << i << ", simulating parsing..." << std::endl;
                simulateCombinedParsing(decompressed, target_spawn_id_);
                return;  // Only process the first matching packet
            }
        }
    }

    FAIL() << "Could not find packet with target ClientUpdate";
}

// Analyze what outer packet types contain our target
TEST_F(PacketReplayTest, AnalyzePacketSequence) {
    std::cout << "\n=== Analyzing Packet Sequence Around Target ===" << std::endl;

    int target_index = -1;
    // Find the first packet with our ClientUpdate
    for (size_t i = 0; i < packets_.size(); i++) {
        const auto& pkt = packets_[i];
        if (pkt.src_port != 7007) continue;

        if (pkt.data.size() >= 4 &&
            pkt.data[0] == 0x00 && pkt.data[1] == 0x03 &&
            pkt.data[2] == 0x5a && pkt.data[3] == 0x78) {

            std::vector<uint8_t> decompressed = decompressData(pkt.data.data() + 2, pkt.data.size() - 2);
            if (findClientUpdate(decompressed, target_spawn_id_)) {
                target_index = i;
                break;
            }
        }
    }

    if (target_index == -1) {
        std::cout << "Target packet not found" << std::endl;
        return;
    }

    std::cout << "Target packet at index " << target_index << std::endl;
    std::cout << "\nPackets around target (10 before to 10 after):" << std::endl;

    for (int i = std::max(0, target_index - 10); i < std::min((int)packets_.size(), target_index + 11); i++) {
        const auto& pkt = packets_[i];
        if (pkt.src_port != 7007) continue;

        std::cout << "  [" << i << "] ";
        if (pkt.data.size() >= 2) {
            uint8_t b0 = pkt.data[0];
            uint8_t b1 = pkt.data[1];

            if (b0 == 0x00) {
                // Daybreak protocol packet
                switch (b1) {
                    case 0x01: std::cout << "OP_SessionRequest"; break;
                    case 0x02: std::cout << "OP_SessionResponse"; break;
                    case 0x03: std::cout << "OP_Combined"; break;
                    case 0x04: std::cout << "OP_SessionDisconnect"; break;
                    case 0x05: std::cout << "OP_KeepAlive"; break;
                    case 0x07: std::cout << "OP_SessionStatRequest"; break;
                    case 0x08: std::cout << "OP_SessionStatResponse"; break;
                    case 0x09: std::cout << "OP_Packet (seq=" << (pkt.data.size() >= 4 ? (pkt.data[2] << 8 | pkt.data[3]) : 0) << ")"; break;
                    case 0x0a: std::cout << "OP_Packet2"; break;
                    case 0x0b: std::cout << "OP_Packet3"; break;
                    case 0x0c: std::cout << "OP_Packet4"; break;
                    case 0x0d: std::cout << "OP_Fragment (seq=" << (pkt.data.size() >= 4 ? (pkt.data[2] << 8 | pkt.data[3]) : 0) << ")"; break;
                    case 0x11: std::cout << "OP_Ack"; break;
                    case 0x15: std::cout << "OP_AppCombined"; break;
                    case 0x19: std::cout << "OP_OutOfOrderAck"; break;
                    default: std::cout << "OP_Unknown(0x" << std::hex << (int)b1 << std::dec << ")"; break;
                }
            } else {
                std::cout << "AppPacket(opcode=0x" << std::hex << (pkt.data[0] | (pkt.data[1] << 8)) << std::dec << ")";
            }

            std::cout << " len=" << pkt.data.size();
            if (i == target_index) std::cout << " *** TARGET ***";
        }
        std::cout << std::endl;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
