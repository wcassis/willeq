/**
 * T321 — CRC validation against REAL captured wire bytes.
 *
 * `src/client/eq.cpp` disables CRC validation on every zone connect
 * (`zone_opts.skip_crc_validation = true; // TODO: Remove after CRC bug is
 * fixed`). This test pins what `ValidateCRC()` actually does with real
 * traffic, so the diagnosis survives whether or not that flag is ever
 * flipped.
 *
 * The bytes come from `review/T311/legA/T311A_official_client_session.pcap`
 * (T311 Leg A: one official Titanium client against a local eqemu, 73,506
 * UDP packets, login + world + zone, both directions). The fixture
 * `tests/data/t321_crc_capture.txt` carries the first session of each of the
 * three streams: its OP_SessionResponse followed by a sample covering every
 * (direction, protocol opcode) pair seen on that stream.
 *
 * The session parameters are NOT hand-set: the recorded OP_SessionResponse is
 * fed through the production `ProcessPacket()` so the real handshake code
 * negotiates encode_key / crc_bytes / encode passes, exactly as it does live.
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

// Same private-access precedent as tests/test_daybreak_connection_pcap.cpp.
#define private public
#define protected public
#include "common/net/daybreak_connection.h"
#undef private
#undef protected

#include "common/net/crc32.h"
#include "fake_udp_transport.h"

using namespace EQ::Net;
using namespace EQ::Test;

#ifndef T321_CRC_CAPTURE
#define T321_CRC_CAPTURE "tests/data/t321_crc_capture.txt"
#endif

namespace {

struct Record {
	std::string stream;   // "login" | "world" | "zone"
	char        dir;      // 'S' = server->client, 'C' = client->server
	std::vector<uint8_t> bytes;
};

std::vector<Record> LoadCapture() {
	std::vector<Record> out;
	std::ifstream f(T321_CRC_CAPTURE);
	if (!f.good()) {
		return out;
	}
	std::string line;
	while (std::getline(f, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream iss(line);
		Record r;
		std::string hex;
		if (!(iss >> r.stream >> r.dir >> hex)) {
			continue;
		}
		r.bytes.reserve(hex.size() / 2);
		for (size_t i = 0; i + 1 < hex.size(); i += 2) {
			r.bytes.push_back((uint8_t)std::stoul(hex.substr(i, 2), nullptr, 16));
		}
		out.push_back(std::move(r));
	}
	return out;
}

// Mirrors ProcessPacket()'s own pre-CRC gates: a packet that never reaches
// ValidateCRC() live must not be asserted on here either.
bool ReachesValidateCRC(const std::vector<uint8_t> &b) {
	if (b.size() < 2) {
		return false;
	}
	if (b[0] == 0 && (b[1] == OP_KeepAlive || b[1] == OP_OutboundPing)) {
		return false;
	}
	if (b[0] != 0) {
		return true;
	}
	return !(b[1] == OP_SessionRequest || b[1] == OP_SessionResponse || b[1] == OP_OutOfSession);
}

class DaybreakCrcCaptureTest : public ::testing::Test {
protected:
	FakeChannel c2s_;
	FakeChannel s2c_;
	DaybreakConnectionManagerOptions opts_;
	std::unique_ptr<DaybreakConnectionManager> manager_;
	std::shared_ptr<DaybreakConnection> conn_;

	void SetUp() override {
		opts_.port                = 0;
		opts_.crc_length          = 2;
		opts_.encode_passes[0]    = EncodeNone;
		opts_.encode_passes[1]    = EncodeNone;
		opts_.connect_delay_ms    = 0;
		opts_.keepalive_delay_ms  = 0;
		opts_.skip_crc_validation = false;
	}

	// Bring up a client-role connection and drive the RECORDED
	// OP_SessionResponse through the production handshake handler.
	void OpenSession(const std::vector<uint8_t> &session_response) {
		auto transport = std::make_unique<FakeUdpTransport>(&c2s_, &s2c_);
		manager_ = std::make_unique<DaybreakConnectionManager>(opts_, std::move(transport));
		manager_->OnNewConnection([this](std::shared_ptr<DaybreakConnection> c) { conn_ = c; });
		manager_->Connect("peer", 12345);
		ASSERT_NE(conn_, nullptr);

		ASSERT_GE(session_response.size(), DaybreakConnectReply::size());
		ASSERT_EQ(session_response[0], 0);
		ASSERT_EQ(session_response[1], OP_SessionResponse);

		// The one value the test supplies: our own connect code, which the
		// live client picks at random and the recorded server echoed back.
		uint32_t wire_code = 0;
		memcpy(&wire_code, &session_response[2], 4);
		conn_->m_connect_code = NetworkToHost(wire_code);

		std::vector<uint8_t> buf(session_response);
		StaticPacket p(buf.data(), buf.size());
		conn_->ProcessPacket(p);

		ASSERT_EQ(conn_->m_status, StatusConnected)
			<< "recorded OP_SessionResponse was rejected by the production handshake";
	}

	// Returns {checked, failed}.
	std::pair<int, int> ValidateAll(const std::vector<Record> &recs, const std::string &stream) {
		int checked = 0, failed = 0;
		bool first = true;
		for (const auto &r : recs) {
			if (r.stream != stream) continue;
			if (first) { first = false; continue; }  // the OP_SessionResponse
			if (!ReachesValidateCRC(r.bytes)) continue;
			std::vector<uint8_t> buf(r.bytes);
			StaticPacket p(buf.data(), buf.size());
			++checked;
			if (!conn_->ValidateCRC(p)) {
				++failed;
				if (failed <= 3) {
					ADD_FAILURE() << stream << " " << r.dir << " packet failed CRC, len="
					              << r.bytes.size() << " opcode=" << (int)r.bytes[1];
				}
			}
		}
		return {checked, failed};
	}
};

const std::vector<uint8_t> *FirstOf(const std::vector<Record> &recs, const std::string &stream) {
	for (const auto &r : recs) {
		if (r.stream == stream) return &r.bytes;
	}
	return nullptr;
}

// --- the handshake the three streams actually negotiate -------------------

TEST_F(DaybreakCrcCaptureTest, RecordedSessionResponsesNegotiateTwoCrcBytes) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;

	for (const char *stream : {"login", "world", "zone"}) {
		const auto *sr = FirstOf(recs, stream);
		ASSERT_NE(sr, nullptr) << stream;
		SCOPED_TRACE(stream);
		ASSERT_NO_FATAL_FAILURE(OpenSession(*sr));
		// Every eqemu stream asks for a 2-byte CRC.
		EXPECT_EQ(conn_->m_crc_bytes, 2u);
		EXPECT_EQ(conn_->m_max_packet_size, 512u);
	}
}

// The single structural difference between the zone stream and the other two:
// zone negotiates compression, login and world do not. Everything else about
// the three handshakes is identical, which is why the zone-only
// skip_crc_validation flag has no handshake-level justification.
TEST_F(DaybreakCrcCaptureTest, OnlyTheZoneStreamNegotiatesCompression) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;

	for (const char *stream : {"login", "world"}) {
		SCOPED_TRACE(stream);
		ASSERT_NO_FATAL_FAILURE(OpenSession(*FirstOf(recs, stream)));
		EXPECT_EQ(conn_->m_encode_passes[0], EncodeNone);
		EXPECT_EQ(conn_->m_encode_passes[1], EncodeNone);
	}

	ASSERT_NO_FATAL_FAILURE(OpenSession(*FirstOf(recs, "zone")));
	EXPECT_EQ(conn_->m_encode_passes[0], EncodeCompression);
	EXPECT_EQ(conn_->m_encode_passes[1], EncodeNone);
}

// --- the finding ----------------------------------------------------------

// Every recorded packet on all three streams passes ValidateCRC() with
// validation ENABLED. Run over the whole 73,506-packet capture off-line this
// was 73,356/73,356 with zero failures (the remainder are handshake and
// keepalive frames that never reach ValidateCRC); the committed fixture is a
// covering sample of that.
TEST_F(DaybreakCrcCaptureTest, EveryCapturedZonePacketPassesCrcValidation) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;

	ASSERT_NO_FATAL_FAILURE(OpenSession(*FirstOf(recs, "zone")));
	ASSERT_FALSE(conn_->m_owner->m_options.skip_crc_validation);

	auto [checked, failed] = ValidateAll(recs, "zone");
	EXPECT_GT(checked, 100);
	EXPECT_EQ(failed, 0);
}

TEST_F(DaybreakCrcCaptureTest, EveryCapturedLoginAndWorldPacketPassesCrcValidation) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;

	for (const char *stream : {"login", "world"}) {
		SCOPED_TRACE(stream);
		ASSERT_NO_FATAL_FAILURE(OpenSession(*FirstOf(recs, stream)));
		auto [checked, failed] = ValidateAll(recs, stream);
		EXPECT_GT(checked, 5);
		EXPECT_EQ(failed, 0);
	}
}

// --- validation actually discriminates ------------------------------------

TEST_F(DaybreakCrcCaptureTest, CorruptedAndTruncatedZonePacketsAreRejected) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;
	ASSERT_NO_FATAL_FAILURE(OpenSession(*FirstOf(recs, "zone")));

	// A big S->C zone frame with a real payload to damage.
	const std::vector<uint8_t> *victim = nullptr;
	for (const auto &r : recs) {
		if (r.stream == "zone" && r.dir == 'S' && r.bytes.size() > 40 && ReachesValidateCRC(r.bytes)) {
			victim = &r.bytes;
			break;
		}
	}
	ASSERT_NE(victim, nullptr);

	{
		std::vector<uint8_t> ok(*victim);
		StaticPacket p(ok.data(), ok.size());
		ASSERT_TRUE(conn_->ValidateCRC(p)) << "control: the unmodified packet must pass";
	}

	// One flipped payload bit -> rejected.
	for (size_t off : {size_t(2), victim->size() / 2, victim->size() - 3}) {
		std::vector<uint8_t> bad(*victim);
		bad[off] ^= 0x01;
		StaticPacket p(bad.data(), bad.size());
		EXPECT_FALSE(conn_->ValidateCRC(p)) << "flipped byte at offset " << off;
	}

	// Truncation (the socket-buffer-overflow failure mode) -> rejected.
	{
		std::vector<uint8_t> bad(victim->begin(), victim->end() - 1);
		StaticPacket p(bad.data(), bad.size());
		EXPECT_FALSE(conn_->ValidateCRC(p));
	}
}

// This is the live defect, pinned: with the flag eq.cpp sets on every zone
// connect, a packet that fails CRC is accepted and parsed as valid.
TEST_F(DaybreakCrcCaptureTest, SkipCrcValidationAcceptsACorruptedZonePacket) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;

	opts_.skip_crc_validation = true;  // what src/client/eq.cpp does today
	ASSERT_NO_FATAL_FAILURE(OpenSession(*FirstOf(recs, "zone")));

	const std::vector<uint8_t> *victim = nullptr;
	for (const auto &r : recs) {
		if (r.stream == "zone" && r.dir == 'S' && r.bytes.size() > 40 && ReachesValidateCRC(r.bytes)) {
			victim = &r.bytes;
			break;
		}
	}
	ASSERT_NE(victim, nullptr);

	std::vector<uint8_t> bad(*victim);
	bad[bad.size() / 2] ^= 0xFF;
	StaticPacket p(bad.data(), bad.size());
	EXPECT_TRUE(conn_->ValidateCRC(p))
		<< "skip_crc_validation is supposed to accept everything; if this ever "
		   "fails the flag has changed meaning";
}

// --- the algorithm itself, against a byte-exact recorded frame ------------

// A regression pin on Crc32's key seeding and the trailing-CRC byte order,
// independent of any session plumbing: the exact bytes of one recorded zone
// frame, its negotiated key, and its recorded trailing CRC.
TEST(DaybreakCrcAlgorithm, RecordedZoneFrameCrcIsReproducible) {
	auto recs = LoadCapture();
	ASSERT_FALSE(recs.empty()) << "fixture missing: " << T321_CRC_CAPTURE;

	const auto *sr = FirstOf(recs, "zone");
	ASSERT_NE(sr, nullptr);
	uint32_t wire_key = 0;
	memcpy(&wire_key, &(*sr)[6], 4);
	const uint32_t key = NetworkToHost(wire_key);

	int checked = 0;
	bool first = true;
	for (const auto &r : recs) {
		if (r.stream != "zone") continue;
		if (first) { first = false; continue; }
		if (!ReachesValidateCRC(r.bytes)) continue;

		const size_t body = r.bytes.size() - 2;
		int16_t tmp = 0;
		memcpy(&tmp, &r.bytes[body], sizeof(tmp));
		const int actual = NetworkToHost(tmp) & 0xffff;
		const int calculated = EQ::Crc32(r.bytes.data(), (int)body, (int)key) & 0xffff;
		ASSERT_EQ(actual, calculated) << "len=" << r.bytes.size();
		++checked;
	}
	EXPECT_GT(checked, 100);
}

}  // namespace
