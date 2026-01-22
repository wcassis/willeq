/**
 * PCAP Step-Through Verification Test
 *
 * Processes each S->C packet one by one with full verbose output,
 * dumping ALL raw packet data at each processing step to verify
 * the Daybreak connection logic matches the pcap exactly.
 */

#include <gtest/gtest.h>
#include "pcap_test_utils.h"
#include "daybreak_test_harness.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>

using namespace EQ::Test;

// Path to test pcap file
static const char* TEST_PCAP_FILE = "/tmp/willeq_audit_capture2.pcap";

/**
 * Helper to format a full hex dump of data
 */
std::string fullHexDump(const uint8_t* data, size_t len, size_t bytes_per_line = 16) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; i += bytes_per_line) {
        // Offset
        oss << std::setw(4) << std::setfill('0') << std::hex << i << ": ";

        // Hex bytes
        for (size_t j = 0; j < bytes_per_line; j++) {
            if (i + j < len) {
                oss << std::setw(2) << std::setfill('0') << std::hex << (int)data[i + j] << " ";
            } else {
                oss << "   ";
            }
            if (j == 7) oss << " ";  // Extra space in middle
        }

        // ASCII
        oss << " |";
        for (size_t j = 0; j < bytes_per_line && i + j < len; j++) {
            char c = data[i + j];
            oss << (isprint(c) ? c : '.');
        }
        oss << "|" << std::endl;
    }
    return oss.str();
}

std::string fullHexDump(const std::vector<uint8_t>& data, size_t bytes_per_line = 16) {
    return fullHexDump(data.data(), data.size(), bytes_per_line);
}

/**
 * Calculate CRC32 for verification
 */
uint32_t calculateCRC32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    static const uint32_t crc_table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };

    for (size_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/**
 * Extended test harness with step-by-step logging
 */
class StepThroughHarness : public DaybreakTestHarness {
public:
    std::ostream* log_stream = &std::cout;
    bool log_enabled = true;

    // Track processing steps
    struct ProcessingStep {
        std::string description;
        std::vector<uint8_t> data_before;
        std::vector<uint8_t> data_after;
        bool success;
        std::string error;
    };
    std::vector<ProcessingStep> steps;

    void clearSteps() { steps.clear(); }

    void log(const std::string& msg) {
        if (log_enabled && log_stream) {
            *log_stream << msg << std::endl;
        }
    }

    /**
     * Process a packet with full step-by-step logging
     */
    void processWithSteps(const CapturedPacket& pkt) {
        clearSteps();

        log("\n" + std::string(70, '='));
        log("FRAME " + std::to_string(pkt.frame_number) + " | " +
            pkt.src_ip + ":" + std::to_string(pkt.src_port) + " -> " +
            pkt.dst_ip + ":" + std::to_string(pkt.dst_port));
        log(std::string(70, '='));

        // Step 1: Raw packet
        log("\n[STEP 1] RAW PACKET (" + std::to_string(pkt.data.size()) + " bytes)");
        log(fullHexDump(pkt.data));

        std::vector<uint8_t> working = pkt.data;

        // Identify packet type
        if (working.size() >= 2 && working[0] == 0x00) {
            uint8_t opcode = working[1];
            log("[INFO] Protocol packet: " + std::string(getDaybreakOpcodeName(opcode)) +
                " (0x" + toHex(opcode) + ")");

            // Handle session response
            if (opcode == 0x02 && working.size() >= 17) {
                log("\n[STEP 2] PARSE SESSION RESPONSE");
                parseAndLogSessionResponse(working);
            }
        }

        if (!sessionEstablished()) {
            log("[INFO] Session not established, skipping further processing");
            return;
        }

        // Step 2: CRC Check
        if (sessionParams().crc_bytes > 0 && working.size() > sessionParams().crc_bytes) {
            log("\n[STEP 2] CRC VERIFICATION (crc_bytes=" + std::to_string(sessionParams().crc_bytes) + ")");

            size_t data_len = working.size() - sessionParams().crc_bytes;
            uint32_t computed_crc = calculateCRC32(working.data(), data_len);

            // Extract CRC from packet (little-endian)
            uint32_t packet_crc = 0;
            if (sessionParams().crc_bytes == 2) {
                packet_crc = working[data_len] | (working[data_len + 1] << 8);
                computed_crc &= 0xFFFF;  // Compare only 16 bits
            } else if (sessionParams().crc_bytes == 4) {
                packet_crc = working[data_len] | (working[data_len + 1] << 8) |
                             (working[data_len + 2] << 16) | (working[data_len + 3] << 24);
            }

            log("  Data length: " + std::to_string(data_len));
            log("  CRC bytes: " + toHex(working.data() + data_len, sessionParams().crc_bytes));
            log("  Packet CRC: 0x" + toHex(packet_crc));
            log("  Computed CRC: 0x" + toHex(computed_crc));

            // Strip CRC
            working.resize(data_len);
            log("  After stripping CRC (" + std::to_string(working.size()) + " bytes):");
            log(fullHexDump(working));
        }

        // Step 3: Decompression
        if (sessionParams().compression_enabled() && working.size() > 2) {
            size_t offset = (working[0] == 0x00) ? 2 : 1;
            if (working.size() > offset) {
                uint8_t marker = working[offset];

                log("\n[STEP 3] DECOMPRESSION CHECK");
                log("  Offset: " + std::to_string(offset));
                log("  Marker byte: 0x" + toHex(marker));

                if (marker == 0x5a) {
                    log("  Compression: ZLIB (0x5a)");
                    log("  Compressed data (" + std::to_string(working.size() - offset - 1) + " bytes):");

                    // Show compressed portion
                    std::vector<uint8_t> compressed(working.begin() + offset + 1, working.end());
                    log(fullHexDump(compressed));

                    // Decompress
                    std::vector<uint8_t> decompressed = inflateZlib(compressed);
                    if (!decompressed.empty()) {
                        log("  Decompressed (" + std::to_string(decompressed.size()) + " bytes):");
                        log(fullHexDump(decompressed));

                        // Reconstruct packet
                        std::vector<uint8_t> header(working.begin(), working.begin() + offset);
                        working = header;
                        working.insert(working.end(), decompressed.begin(), decompressed.end());

                        log("  Reconstructed packet (" + std::to_string(working.size()) + " bytes):");
                        log(fullHexDump(working));
                    } else {
                        log("  ERROR: Decompression failed!");
                    }
                } else if (marker == 0xa5) {
                    log("  Compression: NONE (0xa5 marker)");

                    // Strip marker
                    std::vector<uint8_t> header(working.begin(), working.begin() + offset);
                    std::vector<uint8_t> payload(working.begin() + offset + 1, working.end());
                    working = header;
                    working.insert(working.end(), payload.begin(), payload.end());

                    log("  After stripping marker (" + std::to_string(working.size()) + " bytes):");
                    log(fullHexDump(working));
                } else {
                    log("  No compression marker found");
                }
            }
        }

        // Step 4: Parse protocol packet
        if (working.size() >= 2 && working[0] == 0x00) {
            uint8_t opcode = working[1];

            log("\n[STEP 4] PROTOCOL PARSING");
            log("  Opcode: " + std::string(getDaybreakOpcodeName(opcode)) + " (0x" + toHex(opcode) + ")");

            switch (opcode) {
                case 0x09: case 0x0a: case 0x0b: case 0x0c:
                    parseReliablePacket(working, opcode - 0x09);
                    break;
                case 0x0d: case 0x0e: case 0x0f: case 0x10:
                    parseFragmentPacket(working, opcode - 0x0d);
                    break;
                case 0x03:
                    parseCombinedPacket(working);
                    break;
                case 0x15: case 0x16: case 0x17: case 0x18:
                    parseAckPacket(working, opcode - 0x15);
                    break;
            }
        }
    }

private:
    std::string toHex(uint8_t b) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02x", b);
        return buf;
    }

    std::string toHex(uint32_t val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%08x", val);
        return buf;
    }

    std::string toHex(const uint8_t* data, size_t len) {
        std::string result;
        for (size_t i = 0; i < len; i++) {
            result += toHex(data[i]);
            if (i < len - 1) result += " ";
        }
        return result;
    }

    void parseAndLogSessionResponse(const std::vector<uint8_t>& pkt) {
        if (pkt.size() < 17) return;

        uint32_t connect_code = (pkt[2] << 24) | (pkt[3] << 16) | (pkt[4] << 8) | pkt[5];
        uint32_t encode_key = (pkt[6] << 24) | (pkt[7] << 16) | (pkt[8] << 8) | pkt[9];
        uint8_t crc_bytes = pkt[10];
        uint8_t encode_pass1 = pkt[11];
        uint8_t encode_pass2 = pkt[12];
        uint32_t max_size = (pkt[13] << 24) | (pkt[14] << 16) | (pkt[15] << 8) | pkt[16];

        log("  Connect code: 0x" + toHex(connect_code));
        log("  Encode key: 0x" + toHex(encode_key));
        log("  CRC bytes: " + std::to_string(crc_bytes));
        log("  Encode pass 1: " + std::to_string(encode_pass1) +
            (encode_pass1 == 1 ? " (Compression)" : encode_pass1 == 4 ? " (XOR)" : " (None)"));
        log("  Encode pass 2: " + std::to_string(encode_pass2) +
            (encode_pass2 == 1 ? " (Compression)" : encode_pass2 == 4 ? " (XOR)" : " (None)"));
        log("  Max packet size: " + std::to_string(max_size));

        // Set session params
        SessionParams params;
        params.connect_code = connect_code;
        params.encode_key = encode_key;
        params.crc_bytes = crc_bytes;
        params.encode_pass1 = static_cast<EncodeType>(encode_pass1);
        params.encode_pass2 = static_cast<EncodeType>(encode_pass2);
        params.max_packet_size = max_size;
        setSessionParams(params);
    }

    void parseReliablePacket(const std::vector<uint8_t>& pkt, int stream) {
        if (pkt.size() < 4) return;

        uint16_t seq = (pkt[2] << 8) | pkt[3];
        log("  Stream: " + std::to_string(stream));
        log("  Sequence: " + std::to_string(seq));

        if (pkt.size() > 4) {
            log("  Payload (" + std::to_string(pkt.size() - 4) + " bytes):");
            std::vector<uint8_t> payload(pkt.begin() + 4, pkt.end());
            log(fullHexDump(payload));

            // If payload is an app packet, show opcode
            if (payload.size() >= 2) {
                uint16_t app_opcode = payload[0] | (payload[1] << 8);
                log("  App opcode: 0x" + toHex(static_cast<uint8_t>(app_opcode & 0xFF)) +
                    toHex(static_cast<uint8_t>(app_opcode >> 8)) + " (" + std::to_string(app_opcode) + ")");
            }
        }
    }

    void parseFragmentPacket(const std::vector<uint8_t>& pkt, int stream) {
        if (pkt.size() < 4) return;

        uint16_t seq = (pkt[2] << 8) | pkt[3];
        log("  Stream: " + std::to_string(stream));
        log("  Sequence: " + std::to_string(seq));

        // Check if this is first fragment (has total_size)
        // We detect this by checking if we're in fragment assembly state
        // For simplicity, check if size >= 8 and bytes 4-7 look like a reasonable total_size
        if (pkt.size() >= 8) {
            uint32_t possible_total = (pkt[4] << 24) | (pkt[5] << 16) | (pkt[6] << 8) | pkt[7];
            // Heuristic: if total_size is reasonable (< 1MB), treat as first fragment
            if (possible_total > 0 && possible_total < 1000000) {
                log("  [First Fragment]");
                log("  Total size: " + std::to_string(possible_total) + " bytes");
                log("  Data in this fragment: " + std::to_string(pkt.size() - 8) + " bytes");

                if (pkt.size() > 8) {
                    std::vector<uint8_t> data(pkt.begin() + 8, pkt.end());
                    log("  Fragment data:");
                    log(fullHexDump(data));
                }
            } else {
                log("  [Continuation Fragment]");
                log("  Data: " + std::to_string(pkt.size() - 4) + " bytes");
            }
        } else {
            log("  [Continuation Fragment]");
            log("  Data: " + std::to_string(pkt.size() - 4) + " bytes");
        }
    }

    void parseCombinedPacket(const std::vector<uint8_t>& pkt) {
        if (pkt.size() < 3) return;

        log("  Parsing subpackets:");

        size_t offset = 2;
        int count = 0;
        while (offset < pkt.size()) {
            uint8_t sublen = pkt[offset];
            offset++;

            if (offset + sublen > pkt.size()) {
                log("    Subpacket " + std::to_string(count) + ": TRUNCATED (claims " +
                    std::to_string(sublen) + " bytes, only " + std::to_string(pkt.size() - offset) + " remain)");
                break;
            }

            std::vector<uint8_t> subpkt(pkt.begin() + offset, pkt.begin() + offset + sublen);
            log("    Subpacket " + std::to_string(count) + " (" + std::to_string(sublen) + " bytes):");

            // Identify subpacket type
            if (subpkt.size() >= 2) {
                if (subpkt[0] == 0x00) {
                    log("      Protocol: " + std::string(getDaybreakOpcodeName(subpkt[1])));
                } else {
                    uint16_t app_op = subpkt[0] | (subpkt[1] << 8);
                    log("      App opcode: 0x" + toHex(static_cast<uint8_t>(app_op & 0xFF)) +
                        toHex(static_cast<uint8_t>(app_op >> 8)));
                }
            }
            log(fullHexDump(subpkt));

            offset += sublen;
            count++;
        }

        log("  Total subpackets: " + std::to_string(count));
    }

    void parseAckPacket(const std::vector<uint8_t>& pkt, int stream) {
        if (pkt.size() < 4) return;

        uint16_t ack_seq = (pkt[2] << 8) | pkt[3];
        log("  Stream: " + std::to_string(stream));
        log("  Ack sequence: " + std::to_string(ack_seq));
    }

    std::vector<uint8_t> inflateZlib(const std::vector<uint8_t>& compressed) {
        if (compressed.empty()) return {};

        uint8_t out_buffer[65536];
        z_stream zstream;
        memset(&zstream, 0, sizeof(zstream));

        zstream.next_in = const_cast<uint8_t*>(compressed.data());
        zstream.avail_in = compressed.size();
        zstream.next_out = out_buffer;
        zstream.avail_out = sizeof(out_buffer);

        if (inflateInit2(&zstream, 15) != Z_OK) {
            return {};
        }

        int zerror = inflate(&zstream, Z_FINISH);
        inflateEnd(&zstream);

        if (zerror != Z_STREAM_END) {
            return {};
        }

        return std::vector<uint8_t>(out_buffer, out_buffer + zstream.total_out);
    }
};

class PcapStepThroughTest : public ::testing::Test {
protected:
    StepThroughHarness harness;
    std::vector<CapturedPacket> pcap_packets;

    void SetUp() override {
        std::ifstream f(TEST_PCAP_FILE);
        if (!f.good()) return;

        PcapReadOptions options;
        options.remove_duplicates = true;
        options.server_to_client_only = true;

        auto result = readPcapFile(TEST_PCAP_FILE, options);
        if (result.success) {
            pcap_packets = std::move(result.packets);
        }
    }

    bool hasPcapData() const { return !pcap_packets.empty(); }
};

TEST_F(PcapStepThroughTest, StepThroughFirst50Packets) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "STEP-THROUGH VERIFICATION: First 50 S->C Packets" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    int count = 0;
    for (const auto& pkt : pcap_packets) {
        harness.processWithSteps(pkt);

        count++;
        if (count >= 50) break;
    }

    std::cout << "\nProcessed " << count << " packets" << std::endl;
    EXPECT_GT(count, 0);
}

TEST_F(PcapStepThroughTest, StepThroughZoneServerSession) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "STEP-THROUGH: Zone Server Session (port 7000)" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    // Filter for zone server packets
    std::vector<const CapturedPacket*> zone_packets;
    for (const auto& pkt : pcap_packets) {
        if (pkt.src_port == 7000) {
            zone_packets.push_back(&pkt);
        }
    }

    std::cout << "Found " << zone_packets.size() << " zone server packets" << std::endl;

    // Process first 30 zone server packets
    int count = 0;
    for (const auto* pkt : zone_packets) {
        harness.processWithSteps(*pkt);

        count++;
        if (count >= 30) break;
    }

    EXPECT_GT(count, 0);
    EXPECT_TRUE(harness.sessionEstablished()) << "Session should be established";
    EXPECT_TRUE(harness.sessionParams().compression_enabled()) << "Zone server should have compression";
}

TEST_F(PcapStepThroughTest, StepThroughFragmentSequence) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "STEP-THROUGH: Fragment Packet Sequence" << std::endl;
    std::cout << std::string(70, '=') << std::endl;

    // Find and process session response first
    for (const auto& pkt : pcap_packets) {
        if (pkt.src_port == 7000 && pkt.data.size() >= 2 &&
            pkt.data[0] == 0x00 && pkt.data[1] == 0x02) {
            harness.processWithSteps(pkt);
            break;
        }
    }

    if (!harness.sessionEstablished()) {
        GTEST_SKIP() << "Could not establish session";
    }

    // Find fragment packets
    std::cout << "\nProcessing fragment packets:" << std::endl;
    int fragment_count = 0;

    for (const auto& pkt : pcap_packets) {
        if (pkt.src_port != 7000) continue;
        if (pkt.data.size() < 2) continue;
        if (pkt.data[0] != 0x00) continue;

        uint8_t opcode = pkt.data[1];
        if (opcode >= 0x0d && opcode <= 0x10) {  // Fragment opcodes
            harness.processWithSteps(pkt);
            fragment_count++;

            if (fragment_count >= 10) break;  // Process first 10 fragments
        }
    }

    std::cout << "\nProcessed " << fragment_count << " fragment packets" << std::endl;
    EXPECT_GT(fragment_count, 0);
}

TEST_F(PcapStepThroughTest, WriteDetailedLogToFile) {
    if (!hasPcapData()) {
        GTEST_SKIP() << "Pcap file not available";
    }

    std::ofstream logfile("/tmp/pcap_step_through.log");
    if (!logfile) {
        GTEST_SKIP() << "Could not create log file";
    }

    harness.log_stream = &logfile;

    logfile << "PCAP Step-Through Detailed Log" << std::endl;
    logfile << "File: " << TEST_PCAP_FILE << std::endl;
    logfile << "==============================" << std::endl;

    // Process first 100 zone server packets
    int count = 0;
    for (const auto& pkt : pcap_packets) {
        if (pkt.src_port == 7000) {
            harness.processWithSteps(pkt);
            count++;
            if (count >= 100) break;
        }
    }

    logfile.close();
    std::cout << "Detailed log written to /tmp/pcap_step_through.log" << std::endl;
    std::cout << "Processed " << count << " packets" << std::endl;

    EXPECT_GT(count, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
