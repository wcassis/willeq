#pragma once

/**
 * Daybreak Protocol Test Harness
 *
 * A standalone packet processor that replicates DaybreakConnection's
 * packet processing logic for testing purposes. This allows testing
 * the decompression, fragment assembly, and packet parsing without
 * needing a full network stack.
 */

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <zlib.h>

namespace EQ {
namespace Test {

// Daybreak protocol opcodes (matching daybreak_connection.h)
enum DaybreakOpcode {
    OP_Padding = 0x00,
    OP_SessionRequest = 0x01,
    OP_SessionResponse = 0x02,
    OP_Combined = 0x03,
    OP_SessionDisconnect = 0x05,
    OP_KeepAlive = 0x06,
    OP_SessionStatRequest = 0x07,
    OP_SessionStatResponse = 0x08,
    OP_Packet = 0x09,
    OP_Packet2 = 0x0a,
    OP_Packet3 = 0x0b,
    OP_Packet4 = 0x0c,
    OP_Fragment = 0x0d,
    OP_Fragment2 = 0x0e,
    OP_Fragment3 = 0x0f,
    OP_Fragment4 = 0x10,
    OP_OutOfOrderAck = 0x11,
    OP_OutOfOrderAck2 = 0x12,
    OP_OutOfOrderAck3 = 0x13,
    OP_OutOfOrderAck4 = 0x14,
    OP_Ack = 0x15,
    OP_Ack2 = 0x16,
    OP_Ack3 = 0x17,
    OP_Ack4 = 0x18,
    OP_AppCombined = 0x19,
    OP_OutboundPing = 0x1c,
    OP_OutOfSession = 0x1d
};

// Encode types
enum EncodeType {
    EncodeNone = 0,
    EncodeCompression = 1,
    EncodeXOR = 4
};

/**
 * Result of processing a packet
 */
struct ProcessResult {
    bool success = false;
    std::string error;
    size_t packets_decoded = 0;
    size_t fragments_received = 0;
    size_t fragments_completed = 0;
};

/**
 * Decoded packet delivered to callback
 */
struct DecodedPacket {
    std::vector<uint8_t> data;
    bool is_protocol;       // True if Daybreak protocol, false if app packet
    uint8_t protocol_opcode; // If is_protocol, the opcode
    uint16_t app_opcode;     // If !is_protocol, the app opcode (little-endian)
    uint16_t sequence;       // Sequence number (for reliable packets)
    int stream;              // Stream ID (0-3)

    // Helper to get hex dump
    std::string hexDump(size_t max_bytes = 40) const {
        std::string result;
        for (size_t i = 0; i < data.size() && i < max_bytes; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x", data[i]);
            result += buf;
        }
        if (data.size() > max_bytes) result += "...";
        return result;
    }
};

/**
 * Session parameters extracted from OP_SessionResponse
 */
struct SessionParams {
    uint32_t connect_code = 0;
    uint32_t encode_key = 0;
    uint8_t crc_bytes = 2;
    EncodeType encode_pass1 = EncodeNone;
    EncodeType encode_pass2 = EncodeNone;
    uint32_t max_packet_size = 512;

    bool compression_enabled() const {
        return encode_pass1 == EncodeCompression || encode_pass2 == EncodeCompression;
    }
};

/**
 * Fragment assembly state for one stream
 */
struct FragmentState {
    std::vector<uint8_t> buffer;
    uint32_t total_bytes = 0;
    uint32_t current_bytes = 0;
    uint16_t start_sequence = 0;
    bool in_progress = false;
};

/**
 * Daybreak Protocol Test Harness
 *
 * Processes raw packets and delivers decoded app packets via callback.
 */
class DaybreakTestHarness {
public:
    // Callback for decoded packets
    using PacketCallback = std::function<void(const DecodedPacket&)>;

    // Callback for logging/debugging
    using LogCallback = std::function<void(const std::string&)>;

    DaybreakTestHarness() {
        resetState();
    }

    /**
     * Configure session parameters (normally from OP_SessionResponse)
     */
    void setSessionParams(const SessionParams& params) {
        m_params = params;
        m_session_established = true;
    }

    /**
     * Set callback for decoded packets
     */
    void onPacketDecoded(PacketCallback callback) {
        m_packet_callback = callback;
    }

    /**
     * Set callback for debug logging
     */
    void onLog(LogCallback callback) {
        m_log_callback = callback;
    }

    /**
     * Enable/disable verbose logging
     */
    void setVerbose(bool verbose) {
        m_verbose = verbose;
    }

    /**
     * Reset all state (fragments, sequences, etc.)
     */
    void resetState() {
        for (int i = 0; i < 4; i++) {
            m_streams[i] = StreamState();
        }
        m_session_established = false;
        m_packets_processed = 0;
        m_app_packets_decoded = 0;
        m_fragments_received = 0;
        m_fragments_completed = 0;
        m_decode_errors = 0;
    }

    /**
     * Process a raw packet from the network.
     * Returns true if packet was processed successfully.
     */
    ProcessResult processPacket(const uint8_t* data, size_t length) {
        ProcessResult result;
        result.success = false;

        if (length < 2) {
            result.error = "Packet too short";
            return result;
        }

        m_packets_processed++;

        // Make a working copy
        std::vector<uint8_t> packet(data, data + length);

        // Check for OP_SessionResponse first (establishes session params)
        if (packet[0] == 0x00 && packet[1] == OP_SessionResponse && length >= 17) {
            parseSessionResponse(packet);
        }

        // If session not established, only process session packets
        if (!m_session_established) {
            if (packet[0] == 0x00 && (packet[1] == OP_SessionRequest ||
                                       packet[1] == OP_SessionResponse ||
                                       packet[1] == OP_SessionDisconnect)) {
                result.success = true;
                return result;
            }
            result.error = "Session not established";
            return result;
        }

        // Validate and strip CRC
        if (!validateAndStripCRC(packet)) {
            result.error = "CRC validation failed";
            m_decode_errors++;
            return result;
        }

        // Decompress if needed
        if (m_params.compression_enabled()) {
            if (!decompressPacket(packet)) {
                result.error = "Decompression failed";
                m_decode_errors++;
                return result;
            }
        }

        // Process the decoded packet
        if (!processDecodedPacket(packet, 0)) {
            result.error = "Failed to process decoded packet";
            m_decode_errors++;
            return result;
        }

        result.success = true;
        result.packets_decoded = m_app_packets_decoded;
        result.fragments_received = m_fragments_received;
        result.fragments_completed = m_fragments_completed;
        return result;
    }

    /**
     * Process a raw packet without CRC (for testing)
     */
    ProcessResult processPacketNoCRC(const uint8_t* data, size_t length) {
        ProcessResult result;
        result.success = false;

        if (length < 2) {
            result.error = "Packet too short";
            return result;
        }

        m_packets_processed++;

        // Make a working copy
        std::vector<uint8_t> packet(data, data + length);

        // Check for OP_SessionResponse first
        if (packet[0] == 0x00 && packet[1] == OP_SessionResponse && length >= 17) {
            parseSessionResponse(packet);
        }

        // Decompress if needed
        if (m_params.compression_enabled()) {
            if (!decompressPacket(packet)) {
                result.error = "Decompression failed";
                m_decode_errors++;
                return result;
            }
        }

        // Process the decoded packet
        if (!processDecodedPacket(packet, 0)) {
            result.error = "Failed to process decoded packet";
            m_decode_errors++;
            return result;
        }

        result.success = true;
        result.packets_decoded = m_app_packets_decoded;
        result.fragments_received = m_fragments_received;
        result.fragments_completed = m_fragments_completed;
        return result;
    }

    // Statistics
    size_t packetsProcessed() const { return m_packets_processed; }
    size_t appPacketsDecoded() const { return m_app_packets_decoded; }
    size_t fragmentsReceived() const { return m_fragments_received; }
    size_t fragmentsCompleted() const { return m_fragments_completed; }
    size_t decodeErrors() const { return m_decode_errors; }

    // Access session params
    const SessionParams& sessionParams() const { return m_params; }
    bool sessionEstablished() const { return m_session_established; }

private:
    SessionParams m_params;
    bool m_session_established = false;
    bool m_verbose = false;

    PacketCallback m_packet_callback;
    LogCallback m_log_callback;

    // Statistics
    size_t m_packets_processed = 0;
    size_t m_app_packets_decoded = 0;
    size_t m_fragments_received = 0;
    size_t m_fragments_completed = 0;
    size_t m_decode_errors = 0;

    // Stream state (4 streams like DaybreakConnection)
    struct StreamState {
        uint16_t sequence_in = 0;
        FragmentState fragment;
        std::map<uint16_t, std::vector<uint8_t>> packet_queue;
    };
    StreamState m_streams[4];

    void log(const std::string& msg) {
        if (m_verbose && m_log_callback) {
            m_log_callback(msg);
        }
    }

    void parseSessionResponse(const std::vector<uint8_t>& packet) {
        // OP_SessionResponse structure:
        // [0-1] header (00 02)
        // [2-5] connect_code (BE)
        // [6-9] encode_key (BE)
        // [10]  crc_bytes
        // [11]  encode_pass1
        // [12]  encode_pass2
        // [13-16] max_packet_size (BE)

        if (packet.size() < 17) return;

        m_params.connect_code = (packet[2] << 24) | (packet[3] << 16) | (packet[4] << 8) | packet[5];
        m_params.encode_key = (packet[6] << 24) | (packet[7] << 16) | (packet[8] << 8) | packet[9];
        m_params.crc_bytes = packet[10];
        m_params.encode_pass1 = static_cast<EncodeType>(packet[11]);
        m_params.encode_pass2 = static_cast<EncodeType>(packet[12]);
        m_params.max_packet_size = (packet[13] << 24) | (packet[14] << 16) | (packet[15] << 8) | packet[16];

        m_session_established = true;

        log("Session established: crc=" + std::to_string(m_params.crc_bytes) +
            " enc1=" + std::to_string(m_params.encode_pass1) +
            " enc2=" + std::to_string(m_params.encode_pass2));
    }

    bool validateAndStripCRC(std::vector<uint8_t>& packet) {
        if (m_params.crc_bytes == 0) return true;
        if (packet.size() <= m_params.crc_bytes) return false;

        // For now, just strip the CRC bytes without validating
        // (we'd need the full CRC implementation for proper validation)
        packet.resize(packet.size() - m_params.crc_bytes);
        return true;
    }

    bool decompressPacket(std::vector<uint8_t>& packet) {
        if (packet.size() < 2) return true;

        // Determine offset based on packet type
        size_t offset = (packet[0] == 0x00) ? 2 : 1;
        if (packet.size() <= offset) return true;

        // Check compression marker
        uint8_t marker = packet[offset];

        if (marker == 0x5a) {
            // zlib compressed
            return inflateData(packet, offset);
        } else if (marker == 0xa5) {
            // Uncompressed, just strip marker
            std::vector<uint8_t> result(packet.begin(), packet.begin() + offset);
            result.insert(result.end(), packet.begin() + offset + 1, packet.end());
            packet = std::move(result);
            return true;
        }

        // No compression marker, leave as-is
        return true;
    }

    bool inflateData(std::vector<uint8_t>& packet, size_t offset) {
        if (offset + 1 >= packet.size()) return false;

        // Preserve header bytes
        std::vector<uint8_t> header(packet.begin(), packet.begin() + offset);

        // Inflate the compressed portion (skip 0x5a marker)
        const uint8_t* compressed = packet.data() + offset + 1;
        size_t compressed_len = packet.size() - offset - 1;

        uint8_t out_buffer[8192];
        z_stream zstream;
        memset(&zstream, 0, sizeof(zstream));

        zstream.next_in = const_cast<uint8_t*>(compressed);
        zstream.avail_in = compressed_len;
        zstream.next_out = out_buffer;
        zstream.avail_out = sizeof(out_buffer);

        if (inflateInit2(&zstream, 15) != Z_OK) {
            log("inflateInit2 failed");
            return false;
        }

        int zerror = inflate(&zstream, Z_FINISH);
        inflateEnd(&zstream);

        if (zerror != Z_STREAM_END) {
            log("inflate failed: " + std::to_string(zerror));
            return false;
        }

        // Reconstruct packet: header + decompressed data
        packet = std::move(header);
        packet.insert(packet.end(), out_buffer, out_buffer + zstream.total_out);

        log("Decompressed: " + std::to_string(compressed_len) + " -> " + std::to_string(zstream.total_out));
        return true;
    }

    bool processDecodedPacket(const std::vector<uint8_t>& packet, int depth) {
        if (packet.size() < 2) return false;
        if (depth > 10) {
            log("Max recursion depth exceeded");
            return false;
        }

        // Protocol packet (first byte = 0x00)
        if (packet[0] == 0x00) {
            uint8_t opcode = packet[1];

            switch (opcode) {
                case OP_Combined:
                    return processCombined(packet, depth);

                case OP_AppCombined:
                    return processAppCombined(packet, depth);

                case OP_Packet:
                case OP_Packet2:
                case OP_Packet3:
                case OP_Packet4:
                    return processReliablePacket(packet, opcode - OP_Packet, depth);

                case OP_Fragment:
                case OP_Fragment2:
                case OP_Fragment3:
                case OP_Fragment4:
                    return processFragment(packet, opcode - OP_Fragment, depth);

                case OP_Ack:
                case OP_Ack2:
                case OP_Ack3:
                case OP_Ack4:
                case OP_OutOfOrderAck:
                case OP_OutOfOrderAck2:
                case OP_OutOfOrderAck3:
                case OP_OutOfOrderAck4:
                    // Acks don't need processing
                    return true;

                case OP_SessionRequest:
                case OP_SessionResponse:
                case OP_SessionDisconnect:
                case OP_KeepAlive:
                case OP_SessionStatRequest:
                case OP_SessionStatResponse:
                case OP_OutboundPing:
                case OP_OutOfSession:
                    // Session management packets
                    return true;

                default:
                    log("Unknown protocol opcode: " + std::to_string(opcode));
                    return true;  // Don't fail on unknown opcodes
            }
        }

        // Application packet - deliver to callback
        deliverAppPacket(packet);
        return true;
    }

    bool processCombined(const std::vector<uint8_t>& packet, int depth) {
        // OP_Combined: [00 03] [len1 data1] [len2 data2] ...
        if (packet.size() < 3) return true;

        size_t offset = 2;
        int subpacket_count = 0;

        while (offset < packet.size()) {
            uint8_t sublen = packet[offset];
            offset++;

            if (offset + sublen > packet.size()) {
                log("Combined subpacket truncated");
                break;
            }

            std::vector<uint8_t> subpacket(packet.begin() + offset, packet.begin() + offset + sublen);
            processDecodedPacket(subpacket, depth + 1);
            offset += sublen;
            subpacket_count++;
        }

        log("Processed OP_Combined with " + std::to_string(subpacket_count) + " subpackets");
        return true;
    }

    bool processAppCombined(const std::vector<uint8_t>& packet, int depth) {
        // OP_AppCombined: [00 19] [variable-length subpackets]
        if (packet.size() < 3) return true;

        size_t offset = 2;

        while (offset < packet.size()) {
            uint32_t sublen = 0;

            if (packet[offset] == 0xFF) {
                if (offset + 3 > packet.size()) break;
                if (packet[offset + 1] == 0xFF && packet[offset + 2] == 0xFF) {
                    if (offset + 7 > packet.size()) break;
                    sublen = (packet[offset + 3] << 24) | (packet[offset + 4] << 16) |
                             (packet[offset + 5] << 8) | packet[offset + 6];
                    offset += 7;
                } else {
                    sublen = (packet[offset + 1] << 8) | packet[offset + 2];
                    offset += 3;
                }
            } else {
                sublen = packet[offset];
                offset += 1;
            }

            if (offset + sublen > packet.size()) break;

            std::vector<uint8_t> subpacket(packet.begin() + offset, packet.begin() + offset + sublen);
            processDecodedPacket(subpacket, depth + 1);
            offset += sublen;
        }

        return true;
    }

    bool processReliablePacket(const std::vector<uint8_t>& packet, int stream, int depth) {
        // OP_Packet: [00 09] [seq:2 BE] [payload]
        if (packet.size() < 4) return true;

        uint16_t seq = (packet[2] << 8) | packet[3];
        auto& s = m_streams[stream];

        log("OP_Packet stream=" + std::to_string(stream) + " seq=" + std::to_string(seq) +
            " expected=" + std::to_string(s.sequence_in));

        // For testing, we process all packets regardless of sequence order
        // In production, this would check SequenceCurrent/Future/Past
        s.sequence_in = seq + 1;

        if (packet.size() > 4) {
            std::vector<uint8_t> payload(packet.begin() + 4, packet.end());
            processDecodedPacket(payload, depth + 1);
        }

        return true;
    }

    bool processFragment(const std::vector<uint8_t>& packet, int stream, int depth) {
        // First fragment: [00 0d] [seq:2 BE] [total_size:4 BE] [data]
        // Continuation:   [00 0d] [seq:2 BE] [data]
        if (packet.size() < 4) return false;

        uint16_t seq = (packet[2] << 8) | packet[3];
        auto& s = m_streams[stream];
        auto& frag = s.fragment;

        m_fragments_received++;

        log("OP_Fragment stream=" + std::to_string(stream) + " seq=" + std::to_string(seq) +
            " frag_in_progress=" + std::to_string(frag.in_progress));

        if (!frag.in_progress) {
            // First fragment - has total_size header
            if (packet.size() < 8) {
                log("First fragment too short");
                return false;
            }

            uint32_t total_size = (packet[4] << 24) | (packet[5] << 16) |
                                   (packet[6] << 8) | packet[7];

            frag.total_bytes = total_size;
            frag.current_bytes = 0;
            frag.start_sequence = seq;
            frag.buffer.clear();
            frag.buffer.resize(total_size);
            frag.in_progress = true;

            // Copy first fragment data (after 8-byte header)
            size_t data_len = packet.size() - 8;
            if (frag.current_bytes + data_len > frag.total_bytes) {
                data_len = frag.total_bytes - frag.current_bytes;
            }
            memcpy(frag.buffer.data() + frag.current_bytes, packet.data() + 8, data_len);
            frag.current_bytes += data_len;

            log("First fragment: total=" + std::to_string(total_size) +
                " received=" + std::to_string(data_len));
        } else {
            // Continuation fragment
            size_t data_len = packet.size() - 4;
            if (frag.current_bytes + data_len > frag.total_bytes) {
                data_len = frag.total_bytes - frag.current_bytes;
            }
            memcpy(frag.buffer.data() + frag.current_bytes, packet.data() + 4, data_len);
            frag.current_bytes += data_len;

            log("Continuation fragment: received=" + std::to_string(data_len) +
                " total=" + std::to_string(frag.current_bytes) + "/" + std::to_string(frag.total_bytes));
        }

        // Check if fragment assembly complete
        if (frag.current_bytes >= frag.total_bytes) {
            log("Fragment assembly complete: " + std::to_string(frag.total_bytes) + " bytes");
            m_fragments_completed++;

            // Check if assembled data needs decompression
            std::vector<uint8_t> assembled = std::move(frag.buffer);
            frag = FragmentState();  // Reset

            if (assembled.size() > 0 && assembled[0] == 0x5a) {
                // Assembled data is compressed
                if (!inflateAssembledFragment(assembled)) {
                    log("Failed to decompress assembled fragment");
                    return false;
                }
            } else if (assembled.size() > 0 && assembled[0] == 0xa5) {
                // Uncompressed marker, strip it
                assembled.erase(assembled.begin());
            }

            // Process the assembled packet
            processDecodedPacket(assembled, depth + 1);
        }

        s.sequence_in = seq + 1;
        return true;
    }

    bool inflateAssembledFragment(std::vector<uint8_t>& data) {
        if (data.size() < 2) return false;

        // Skip 0x5a marker
        const uint8_t* compressed = data.data() + 1;
        size_t compressed_len = data.size() - 1;

        uint8_t out_buffer[65536];  // Larger buffer for assembled fragments
        z_stream zstream;
        memset(&zstream, 0, sizeof(zstream));

        zstream.next_in = const_cast<uint8_t*>(compressed);
        zstream.avail_in = compressed_len;
        zstream.next_out = out_buffer;
        zstream.avail_out = sizeof(out_buffer);

        if (inflateInit2(&zstream, 15) != Z_OK) {
            return false;
        }

        int zerror = inflate(&zstream, Z_FINISH);
        inflateEnd(&zstream);

        if (zerror != Z_STREAM_END) {
            return false;
        }

        data.assign(out_buffer, out_buffer + zstream.total_out);
        log("Decompressed assembled fragment: " + std::to_string(compressed_len) +
            " -> " + std::to_string(data.size()));
        return true;
    }

    void deliverAppPacket(const std::vector<uint8_t>& packet) {
        if (packet.empty()) return;

        m_app_packets_decoded++;

        DecodedPacket decoded;
        decoded.data = packet;
        decoded.is_protocol = false;
        decoded.protocol_opcode = 0;
        decoded.sequence = 0;
        decoded.stream = 0;

        if (packet.size() >= 2) {
            decoded.app_opcode = packet[0] | (packet[1] << 8);
        }

        if (m_packet_callback) {
            m_packet_callback(decoded);
        }
    }
};

}  // namespace Test
}  // namespace EQ
