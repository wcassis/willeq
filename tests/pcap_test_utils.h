#pragma once

/**
 * PCAP Test Utilities
 *
 * Provides utilities for reading and parsing pcap files for testing
 * DaybreakConnection packet processing.
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <arpa/inet.h>  // for ntohs

namespace EQ {
namespace Test {

/**
 * Resolve the path to the pcap capture used by the protocol-level tests.
 *
 * Overridable via the WILLEQ_PCAP environment variable; defaults to
 * data/captures/session.pcap (relative to cwd, matching other asset-test
 * conventions). No capture ships in the repo, so tests that need one must
 * check for its existence and GTEST_SKIP() from the TEST body directly.
 */
inline std::string getTestPcapPath() {
    const char* env = std::getenv("WILLEQ_PCAP");
    if (env && *env) {
        return env;
    }
    return "data/captures/session.pcap";
}

// PCAP file format structures
#pragma pack(push, 1)
struct PcapFileHeader {
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

/**
 * Represents a captured UDP packet from a pcap file.
 */
struct CapturedPacket {
    uint32_t frame_number;      // Frame number in pcap
    uint32_t timestamp_sec;     // Timestamp seconds
    uint32_t timestamp_usec;    // Timestamp microseconds
    std::string src_ip;         // Source IP address
    std::string dst_ip;         // Destination IP address
    uint16_t src_port;          // Source UDP port
    uint16_t dst_port;          // Destination UDP port
    std::vector<uint8_t> data;  // UDP payload data

    // Helper to get timestamp as double
    double timestamp() const {
        return timestamp_sec + timestamp_usec / 1000000.0;
    }

    // Helper to get hex dump of first N bytes
    std::string hexDump(size_t max_bytes = 40) const {
        std::string result;
        for (size_t i = 0; i < data.size() && i < max_bytes; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x", data[i]);
            result += buf;
        }
        if (data.size() > max_bytes) {
            result += "...";
        }
        return result;
    }

    // Helper to check if this is a Daybreak protocol packet
    bool isDaybreakProtocol() const {
        return data.size() >= 2 && data[0] == 0x00;
    }

    // Get Daybreak opcode (if protocol packet)
    uint8_t getDaybreakOpcode() const {
        if (data.size() >= 2 && data[0] == 0x00) {
            return data[1];
        }
        return 0xFF;
    }
};

/**
 * Result of reading a pcap file.
 */
struct PcapReadResult {
    bool success;
    std::string error;
    uint32_t network_type;
    std::vector<CapturedPacket> packets;

    // Statistics
    size_t total_frames;
    size_t udp_packets;
    size_t duplicate_packets;
};

// Forward declaration: the default server_ports set below is derived from
// discovery against the active test capture (see discoverPorts() further
// down this header). Only a declaration is needed here — by the time any
// PcapReadOptions is actually constructed at runtime, the definition further
// down this same header is available.
inline std::set<uint16_t> defaultServerPorts();

/**
 * Options for filtering packets when reading pcap.
 */
struct PcapReadOptions {
    // Server ports to filter for (S->C = src_port in this set). Seeded with
    // the well-known login/world ports (5998/5999/9000) plus whatever zone
    // server ports discovery finds via the OP_SessionResponse handshake in
    // the active capture (see getTestZonePort()/discoverPorts()). Falls back
    // to the bare well-known set if no capture is available.
    std::set<uint16_t> server_ports = defaultServerPorts();

    // If true, only include S->C packets (src_port in server_ports)
    bool server_to_client_only = false;

    // If true, only include C->S packets (dst_port in server_ports)
    bool client_to_server_only = false;

    // If true, remove duplicate packets (same payload data)
    bool remove_duplicates = true;

    // Maximum number of packets to read (0 = unlimited)
    size_t max_packets = 0;

    // If set, only include packets from this specific connection
    uint16_t filter_src_port = 0;
    uint16_t filter_dst_port = 0;
};

/**
 * Helper to swap bytes if needed.
 */
inline uint16_t swapBytes16(uint16_t val) {
    return (val >> 8) | (val << 8);
}

inline uint32_t swapBytes32(uint32_t val) {
    return ((val >> 24) & 0xFF) |
           ((val >> 8) & 0xFF00) |
           ((val << 8) & 0xFF0000) |
           ((val << 24) & 0xFF000000);
}

/**
 * Convert IP address bytes to string.
 */
inline std::string ipToString(uint32_t ip) {
    return std::to_string((ip >> 0) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." +
           std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 24) & 0xFF);
}

/**
 * Get link layer header size based on network type.
 */
inline size_t getLinkHeaderSize(uint32_t network) {
    switch (network) {
        case 1:   return 14;  // LINKTYPE_ETHERNET
        case 113: return 16;  // LINKTYPE_LINUX_SLL
        case 276: return 20;  // LINKTYPE_LINUX_SLL2
        default:  return 0;
    }
}

/**
 * Read and parse a pcap file, extracting EVERY UDP packet with no port
 * filtering and no deduplication.
 *
 * This is the shared low-level parser: readPcapFile() applies
 * PcapReadOptions filtering/dedup/limits on top of this, and discoverPorts()
 * (below) uses it directly so port discovery never depends on
 * PcapReadOptions (which itself derives its default server_ports from
 * discovery — going through readPcapFile()/PcapReadOptions here would be
 * circular).
 *
 * @param filename Path to the pcap file
 * @return PcapReadResult containing every UDP packet found, unfiltered
 */
inline PcapReadResult readPcapFileRaw(const std::string& filename) {
    PcapReadResult result;
    result.success = false;
    result.total_frames = 0;
    result.udp_packets = 0;
    result.duplicate_packets = 0;

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        result.error = "Failed to open file: " + filename;
        return result;
    }

    // Read pcap file header
    PcapFileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        result.error = "Failed to read pcap header";
        return result;
    }

    // Check magic number and determine byte order
    bool swap_bytes = false;
    if (header.magic_number == 0xa1b2c3d4) {
        swap_bytes = false;
    } else if (header.magic_number == 0xd4c3b2a1) {
        swap_bytes = true;
    } else {
        result.error = "Invalid pcap magic number: " + std::to_string(header.magic_number);
        return result;
    }

    // Get network type
    result.network_type = swap_bytes ? swapBytes32(header.network) : header.network;
    size_t link_header_size = getLinkHeaderSize(result.network_type);
    if (link_header_size == 0) {
        result.error = "Unsupported network type: " + std::to_string(result.network_type);
        return result;
    }

    // Read packets
    while (file) {
        PcapRecordHeader rec;
        file.read(reinterpret_cast<char*>(&rec), sizeof(rec));
        if (!file) break;

        result.total_frames++;

        uint32_t incl_len = swap_bytes ? swapBytes32(rec.incl_len) : rec.incl_len;
        uint32_t ts_sec = swap_bytes ? swapBytes32(rec.ts_sec) : rec.ts_sec;
        uint32_t ts_usec = swap_bytes ? swapBytes32(rec.ts_usec) : rec.ts_usec;

        // Sanity check packet length
        if (incl_len > 65535 || incl_len == 0) {
            result.error = "Invalid packet length at frame " + std::to_string(result.total_frames);
            break;
        }

        // Read raw packet data
        std::vector<uint8_t> raw_data(incl_len);
        file.read(reinterpret_cast<char*>(raw_data.data()), incl_len);
        if (!file) break;

        // Skip if too short for headers
        size_t min_size = link_header_size + sizeof(IpHeader) + sizeof(UdpHeader);
        if (incl_len < min_size) {
            continue;
        }

        // Skip link layer header
        size_t offset = link_header_size;

        // Parse IP header
        const IpHeader* ip = reinterpret_cast<const IpHeader*>(raw_data.data() + offset);
        uint8_t ip_header_len = (ip->version_ihl & 0x0F) * 4;

        // Only process UDP (protocol 17)
        if (ip->protocol != 17) {
            continue;
        }

        offset += ip_header_len;

        // Parse UDP header
        if (offset + sizeof(UdpHeader) > raw_data.size()) {
            continue;
        }
        const UdpHeader* udp = reinterpret_cast<const UdpHeader*>(raw_data.data() + offset);
        uint16_t src_port = ntohs(udp->src_port);
        uint16_t dst_port = ntohs(udp->dst_port);
        uint16_t udp_len = ntohs(udp->length);
        offset += sizeof(UdpHeader);

        // Calculate payload length
        int payload_len = static_cast<int>(udp_len) - static_cast<int>(sizeof(UdpHeader));
        if (payload_len <= 0) {
            continue;
        }
        if (offset + payload_len > raw_data.size()) {
            payload_len = static_cast<int>(raw_data.size() - offset);
        }
        if (payload_len <= 0) {
            continue;
        }

        result.udp_packets++;

        // Extract payload
        std::vector<uint8_t> payload(raw_data.begin() + offset, raw_data.begin() + offset + payload_len);

        // Create packet record
        CapturedPacket pkt;
        pkt.frame_number = result.total_frames;
        pkt.timestamp_sec = ts_sec;
        pkt.timestamp_usec = ts_usec;
        pkt.src_ip = ipToString(ip->src_addr);
        pkt.dst_ip = ipToString(ip->dst_addr);
        pkt.src_port = src_port;
        pkt.dst_port = dst_port;
        pkt.data = std::move(payload);

        result.packets.push_back(std::move(pkt));
    }

    result.success = true;
    return result;
}

/**
 * Read and parse a pcap file, extracting UDP packets, applying
 * PcapReadOptions filtering/deduplication/limits on top of the raw packet
 * list from readPcapFileRaw().
 *
 * @param filename Path to the pcap file
 * @param options Options for filtering packets
 * @return PcapReadResult containing packets and statistics
 */
inline PcapReadResult readPcapFile(const std::string& filename, const PcapReadOptions& options = PcapReadOptions()) {
    PcapReadResult raw = readPcapFileRaw(filename);

    PcapReadResult result;
    result.success = raw.success;
    result.error = raw.error;
    result.network_type = raw.network_type;
    result.total_frames = raw.total_frames;
    result.udp_packets = raw.udp_packets;
    result.duplicate_packets = 0;

    if (!raw.success) {
        return result;
    }

    std::set<std::string> seen_data;

    for (const auto& pkt : raw.packets) {
        // Apply port filters
        bool is_s2c = options.server_ports.count(pkt.src_port) > 0;
        bool is_c2s = options.server_ports.count(pkt.dst_port) > 0;

        if (options.server_to_client_only && !is_s2c) {
            continue;
        }
        if (options.client_to_server_only && !is_c2s) {
            continue;
        }

        // Apply specific port filter
        if (options.filter_src_port != 0 && pkt.src_port != options.filter_src_port) {
            continue;
        }
        if (options.filter_dst_port != 0 && pkt.dst_port != options.filter_dst_port) {
            continue;
        }

        // Check for duplicates
        if (options.remove_duplicates) {
            std::string data_hash(pkt.data.begin(), pkt.data.end());
            if (seen_data.count(data_hash) > 0) {
                result.duplicate_packets++;
                continue;
            }
            seen_data.insert(data_hash);
        }

        result.packets.push_back(pkt);

        // Check packet limit
        if (options.max_packets > 0 && result.packets.size() >= options.max_packets) {
            break;
        }
    }

    return result;
}

/**
 * Discovered server ports/IPs for a capture, keyed off the OP_SessionResponse
 * handshake rather than a hardcoded port heuristic (see getTestZonePort()).
 */
struct DiscoveredZoneSession {
    std::string server_ip;
    uint16_t server_port = 0;
    size_t packet_count = 0;  // total packets (either direction) touching this port
};

struct DiscoveredPorts {
    bool valid = false;  // true once a scan of a readable capture has run
    std::set<uint16_t> server_ports;             // well-known + discovered
    std::vector<DiscoveredZoneSession> zone_sessions;  // busiest first
};

/**
 * Scan a pcap capture for OP_SessionResponse (opcode 0x02) handshake
 * packets. The port that SENDS OP_SessionResponse is a server port; its peer
 * is the client. This is robust against client ephemeral ports landing
 * anywhere in the range, unlike a fixed port-number heuristic.
 *
 * Well-known login/world ports (5998/5999/9000) are classified as such;
 * any other discovered server port is a zone-session candidate. If more than
 * one zone session is found (e.g. multiple zones visited in one capture),
 * all are returned, busiest (by total packet count) first.
 *
 * Cached per path so repeated calls (one per test fixture) do not re-scan
 * the capture file. Never throws/crashes on a missing or corrupt file —
 * returns a DiscoveredPorts with valid=false and only the well-known seed
 * ports.
 */
inline const DiscoveredPorts& discoverPorts(const std::string& path) {
    static std::map<std::string, DiscoveredPorts> cache;

    auto it = cache.find(path);
    if (it != cache.end()) {
        return it->second;
    }

    DiscoveredPorts& result = cache[path];
    static const std::set<uint16_t> well_known = {5998, 5999, 9000};
    result.server_ports = well_known;

    PcapReadResult raw = readPcapFileRaw(path);
    if (!raw.success) {
        // Missing/corrupt capture: leave valid=false, well-known-only seed.
        return result;
    }

    // Find the server side (src) of every OP_SessionResponse.
    std::map<uint16_t, std::string> session_response_ports;  // port -> server ip
    for (const auto& pkt : raw.packets) {
        if (pkt.data.size() >= 2 && pkt.data[0] == 0x00 && pkt.data[1] == 0x02) {
            session_response_ports[pkt.src_port] = pkt.src_ip;
        }
    }

    std::vector<std::pair<uint16_t, std::string>> zone_candidates;
    for (const auto& entry : session_response_ports) {
        result.server_ports.insert(entry.first);
        if (well_known.count(entry.first) == 0) {
            zone_candidates.emplace_back(entry.first, entry.second);
        }
    }

    // Count total packets (either direction) touching each zone-candidate
    // port so getTestZonePort() can pick the busiest session.
    for (const auto& candidate : zone_candidates) {
        uint16_t port = candidate.first;
        size_t count = 0;
        for (const auto& pkt : raw.packets) {
            if (pkt.src_port == port || pkt.dst_port == port) {
                count++;
            }
        }
        DiscoveredZoneSession session;
        session.server_ip = candidate.second;
        session.server_port = port;
        session.packet_count = count;
        result.zone_sessions.push_back(session);
    }

    std::sort(result.zone_sessions.begin(), result.zone_sessions.end(),
              [](const DiscoveredZoneSession& a, const DiscoveredZoneSession& b) {
                  return a.packet_count > b.packet_count;
              });

    result.valid = true;
    return result;
}

/**
 * Default server_ports set for PcapReadOptions: well-known login/world
 * ports seeded, plus whatever zone-server ports discovery finds in the
 * active test capture. Falls back to just the well-known seed if no capture
 * is available (matches pre-discovery behavior enough that direction
 * classification degrades gracefully rather than crashing).
 */
inline std::set<uint16_t> defaultServerPorts() {
    return discoverPorts(getTestPcapPath()).server_ports;
}

/**
 * Resolve the zone server port to use in tests.
 *
 * Resolution order:
 *   1. WILLEQ_ZONE_PORT environment variable, if set and non-empty.
 *   2. Autodiscovery from the capture (busiest zone session found via the
 *      OP_SessionResponse handshake).
 *
 * Returns 0 if neither source yields a port (e.g. no capture present) —
 * callers already gate on hasPcapData()/GTEST_SKIP() before using this, so a
 * 0 simply yields no matching packets rather than a crash.
 */
inline uint16_t getTestZonePort() {
    const char* env = std::getenv("WILLEQ_ZONE_PORT");
    if (env && *env) {
        return static_cast<uint16_t>(std::atoi(env));
    }
    const DiscoveredPorts& discovered = discoverPorts(getTestPcapPath());
    if (!discovered.zone_sessions.empty()) {
        return discovered.zone_sessions.front().server_port;
    }
    return 0;
}

/**
 * Resolve the zone server IP to use in tests (companion to
 * getTestZonePort()). Same resolution order, via WILLEQ_ZONE_IP.
 */
inline std::string getTestZoneIp() {
    const char* env = std::getenv("WILLEQ_ZONE_IP");
    if (env && *env) {
        return env;
    }
    const DiscoveredPorts& discovered = discoverPorts(getTestPcapPath());
    if (!discovered.zone_sessions.empty()) {
        return discovered.zone_sessions.front().server_ip;
    }
    return "";
}

/**
 * Expose the full list of discovered zone sessions (busiest first) for
 * tests that want to iterate more than just "the" zone server.
 */
inline const std::vector<DiscoveredZoneSession>& getTestZoneSessions() {
    return discoverPorts(getTestPcapPath()).zone_sessions;
}

/**
 * Daybreak protocol opcode names for debugging.
 */
inline const char* getDaybreakOpcodeName(uint8_t opcode) {
    switch (opcode) {
        case 0x00: return "OP_Padding";
        case 0x01: return "OP_SessionRequest";
        case 0x02: return "OP_SessionResponse";
        case 0x03: return "OP_Combined";
        case 0x05: return "OP_SessionDisconnect";
        case 0x06: return "OP_KeepAlive";
        case 0x07: return "OP_SessionStatRequest";
        case 0x08: return "OP_SessionStatResponse";
        case 0x09: return "OP_Packet";
        case 0x0a: return "OP_Packet2";
        case 0x0b: return "OP_Packet3";
        case 0x0c: return "OP_Packet4";
        case 0x0d: return "OP_Fragment";
        case 0x0e: return "OP_Fragment2";
        case 0x0f: return "OP_Fragment3";
        case 0x10: return "OP_Fragment4";
        case 0x11: return "OP_OutOfOrderAck";
        case 0x12: return "OP_OutOfOrderAck2";
        case 0x13: return "OP_OutOfOrderAck3";
        case 0x14: return "OP_OutOfOrderAck4";
        case 0x15: return "OP_Ack";
        case 0x16: return "OP_Ack2";
        case 0x17: return "OP_Ack3";
        case 0x18: return "OP_Ack4";
        case 0x19: return "OP_AppCombined";
        case 0x1c: return "OP_OutboundPing";
        case 0x1d: return "OP_OutOfSession";
        default:   return "Unknown";
    }
}

/**
 * Check if an opcode is a fragment opcode.
 */
inline bool isFragmentOpcode(uint8_t opcode) {
    return opcode >= 0x0d && opcode <= 0x10;
}

/**
 * Check if an opcode is a reliable packet opcode.
 */
inline bool isReliableOpcode(uint8_t opcode) {
    return (opcode >= 0x09 && opcode <= 0x10);
}

/**
 * Check if an opcode is an ack opcode.
 */
inline bool isAckOpcode(uint8_t opcode) {
    return (opcode >= 0x11 && opcode <= 0x18);
}

/**
 * Print packet summary for debugging.
 */
inline void printPacketSummary(const CapturedPacket& pkt, std::ostream& out = std::cout) {
    out << "Frame " << pkt.frame_number << ": ";
    out << pkt.src_ip << ":" << pkt.src_port << " -> ";
    out << pkt.dst_ip << ":" << pkt.dst_port << " ";
    out << "(" << pkt.data.size() << " bytes) ";

    if (pkt.isDaybreakProtocol()) {
        uint8_t opcode = pkt.getDaybreakOpcode();
        out << getDaybreakOpcodeName(opcode);

        // Show sequence for reliable packets
        if (isReliableOpcode(opcode) && pkt.data.size() >= 4) {
            uint16_t seq = (pkt.data[2] << 8) | pkt.data[3];
            out << " seq=" << seq;
        }
    } else {
        out << "App packet";
    }

    out << "\n";
    out << "  Data: " << pkt.hexDump(60) << "\n";
}

/**
 * Get statistics about packet types in a collection.
 */
struct PacketStats {
    size_t total = 0;
    size_t protocol_packets = 0;
    size_t app_packets = 0;
    size_t fragment_packets = 0;
    size_t combined_packets = 0;
    size_t compressed_packets = 0;  // Packets with 0x5a marker
    std::map<uint8_t, size_t> opcode_counts;
};

inline PacketStats getPacketStats(const std::vector<CapturedPacket>& packets) {
    PacketStats stats;
    stats.total = packets.size();

    for (const auto& pkt : packets) {
        if (pkt.isDaybreakProtocol()) {
            stats.protocol_packets++;
            uint8_t opcode = pkt.getDaybreakOpcode();
            stats.opcode_counts[opcode]++;

            if (isFragmentOpcode(opcode)) {
                stats.fragment_packets++;
            }
            if (opcode == 0x03) {  // OP_Combined
                stats.combined_packets++;
            }

            // Check for compression marker
            if (pkt.data.size() > 2 && pkt.data[2] == 0x5a) {
                stats.compressed_packets++;
            }
        } else {
            stats.app_packets++;
        }
    }

    return stats;
}

inline void printPacketStats(const PacketStats& stats, std::ostream& out = std::cout) {
    out << "Packet Statistics:\n";
    out << "  Total packets: " << stats.total << "\n";
    out << "  Protocol packets: " << stats.protocol_packets << "\n";
    out << "  App packets: " << stats.app_packets << "\n";
    out << "  Fragment packets: " << stats.fragment_packets << "\n";
    out << "  Combined packets: " << stats.combined_packets << "\n";
    out << "  Compressed packets: " << stats.compressed_packets << "\n";
    out << "  Opcode breakdown:\n";
    for (const auto& [opcode, count] : stats.opcode_counts) {
        out << "    " << getDaybreakOpcodeName(opcode) << " (0x"
            << std::hex << (int)opcode << std::dec << "): " << count << "\n";
    }
}

}  // namespace Test
}  // namespace EQ
