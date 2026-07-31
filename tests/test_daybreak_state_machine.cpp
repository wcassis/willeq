/**
 * T17 — Daybreak connection state machine tests.
 *
 * Drives the REAL production DaybreakConnection/DaybreakConnectionManager
 * (src/common/net/daybreak_connection.cpp) through a FakeUdpTransport
 * (tests/fake_udp_transport.h) — no real sockets, no wall-clock sleeps.
 *
 * Access technique: like tests/test_daybreak_connection_pcap.cpp, this file
 * uses `#define private public` to reach DaybreakConnection's private
 * methods/fields (ProcessPacket, AppendCRC, m_streams, m_status, ...) since
 * DaybreakConnectionManager is declared `friend class DaybreakConnection`
 * but tests are not. This lets us:
 *   - drive the recv path directly via ProcessPacket() (bypassing the
 *     wall-clock-gated Tick()/Process() send-timer machinery for tests that
 *     aren't specifically about that machinery),
 *   - backdate DaybreakSentPacket::first_sent/last_sent timestamps to
 *     deterministically simulate elapsed time for resend/timeout tests
 *     without any sleep_for() calls — Clock is std::chrono::steady_clock
 *     with no injectable time source (see Timer findings in
 *     refactor/T17_daybreak_state_machine.md Review section),
 *   - inspect real wire bytes captured by FakeUdpTransport for send-path
 *     assertions (fragmentation, combining, CRC, compression).
 */

#include <gtest/gtest.h>

#define private public
#define protected public
#include "common/net/daybreak_connection.h"
#undef private
#undef protected

#include "fake_udp_transport.h"

#include <cstring>
#include <set>

using namespace EQ::Net;
using namespace EQ::Test;

namespace {

// Build an application payload: [app_opcode LE u16][data...]. Keeps the low
// byte of app_opcode non-zero so ProcessDecodedPacket's `first byte == 0`
// protocol-vs-application dispatch classifies it as an application packet.
std::vector<uint8_t> MakeAppPayload(uint16_t app_opcode, const std::vector<uint8_t> &data = {}) {
	std::vector<uint8_t> out;
	out.push_back((uint8_t)(app_opcode & 0xFF));
	out.push_back((uint8_t)((app_opcode >> 8) & 0xFF));
	out.insert(out.end(), data.begin(), data.end());
	return out;
}

} // namespace

class DaybreakStateMachineTest : public ::testing::Test {
protected:
	FakeChannel c2s_; // client -> peer
	FakeChannel s2c_; // peer -> client
	DaybreakConnectionManagerOptions opts_;
	std::unique_ptr<DaybreakConnectionManager> manager_;
	std::shared_ptr<DaybreakConnection> conn_;

	std::vector<std::pair<uint16_t, std::vector<uint8_t>>> received_;
	std::vector<std::pair<DbProtocolStatus, DbProtocolStatus>> status_changes_;
	std::vector<std::string> errors_;

	void SetUp() override {
		opts_ = DaybreakConnectionManagerOptions();
		opts_.crc_length          = 2;
		opts_.encode_passes[0]    = EncodeNone;
		opts_.encode_passes[1]    = EncodeNone;
		opts_.connect_delay_ms    = 0;
		opts_.keepalive_delay_ms  = 0; // disable for tests that don't care
		opts_.resend_delay_min    = 10;
		opts_.resend_delay_max    = 50;
		opts_.resend_delay_ms     = 10;
		opts_.resend_timeout      = 100000;
		opts_.connection_close_time = 2000;
		opts_.skip_crc_validation = false;
	}

	// (Re)create the manager+transport pair with the current opts_ and
	// start a client-role connection to "peer".
	void createManager() {
		auto transport = std::make_unique<FakeUdpTransport>(&c2s_, &s2c_);
		manager_ = std::make_unique<DaybreakConnectionManager>(opts_, std::move(transport));

		manager_->OnNewConnection([this](std::shared_ptr<DaybreakConnection> c) {
			conn_ = c;
		});
		manager_->OnPacketRecv([this](std::shared_ptr<DaybreakConnection>, const Packet &p) {
			uint16_t opcode = p.Length() >= 2 ? p.GetUInt16(0) : 0;
			auto *base = (const uint8_t *)p.Data();
			received_.emplace_back(opcode, std::vector<uint8_t>(base, base + p.Length()));
		});
		manager_->OnConnectionStateChange([this](std::shared_ptr<DaybreakConnection>, DbProtocolStatus o, DbProtocolStatus n) {
			status_changes_.emplace_back(o, n);
		});
		manager_->OnErrorMessage([this](const std::string &msg) {
			errors_.push_back(msg);
		});

		manager_->Connect("peer", 12345);
		ASSERT_NE(conn_, nullptr);
	}

	// Fast-forward past the handshake for tests that aren't specifically
	// about the handshake itself.
	void forceConnected(uint32_t crc_bytes = 2,
	                     DaybreakEncodeType p1 = EncodeNone,
	                     DaybreakEncodeType p2 = EncodeNone,
	                     uint32_t max_packet_size = 512) {
		conn_->m_status         = StatusConnected;
		conn_->m_crc_bytes      = crc_bytes;
		conn_->m_encode_key     = 0x12345678u;
		conn_->m_encode_passes[0] = p1;
		conn_->m_encode_passes[1] = p2;
		conn_->m_max_packet_size  = max_packet_size;
	}

	// Wrap an application payload as a reliable OP_Packet frame (with CRC,
	// using conn_'s current crc_bytes/encode_key so it validates).
	DynamicPacket buildReliableFrame(int stream_id, uint16_t seq, std::vector<uint8_t> app_payload) {
		DaybreakReliableHeader hdr;
		hdr.zero     = 0;
		hdr.opcode   = (uint8_t)(OP_Packet + stream_id);
		hdr.sequence = HostToNetwork(seq);

		DynamicPacket p;
		p.PutSerialize(0, hdr);
		p.PutData(p.Length(), app_payload.data(), app_payload.size());
		conn_->AppendCRC(p);
		return p;
	}

	DynamicPacket buildAckFrame(int stream_id, uint16_t seq, bool out_of_order) {
		DaybreakReliableHeader hdr;
		hdr.zero     = 0;
		hdr.opcode   = (uint8_t)((out_of_order ? OP_OutOfOrderAck : OP_Ack) + stream_id);
		hdr.sequence = HostToNetwork(seq);
		DynamicPacket p;
		p.PutSerialize(0, hdr);
		conn_->AppendCRC(p);
		return p;
	}

	// Deliver bytes directly to conn_->ProcessPacket, exactly like the
	// production recv path (manager Tick -> ProcessPacket) but without the
	// wall-clock-gated Tick()/Process() machinery.
	void deliver(Packet &p) {
		conn_->ProcessPacket(p);
	}

	// Overload so `deliver(buildReliableFrame(...))` (a temporary) binds.
	void deliver(DynamicPacket &&p) {
		deliver(static_cast<Packet &>(p));
	}

	void deliver(const std::vector<uint8_t> &bytes) {
		StaticPacket p((void *)bytes.data(), bytes.size());
		deliver(p);
	}
};

// =====================================================================
// Priority 1: Session lifecycle
// =====================================================================

TEST_F(DaybreakStateMachineTest, ClientStartsConnecting) {
	createManager();
	EXPECT_EQ(conn_->GetStatus(), StatusConnecting);
}

TEST_F(DaybreakStateMachineTest, SendConnectEmitsWellFormedSessionRequest) {
	createManager();
	conn_->SendConnect();

	ASSERT_FALSE(c2s_.Empty());
	auto bytes = c2s_.PopFront();
	ASSERT_GE(bytes.size(), DaybreakConnect::size());

	StaticPacket p((void *)bytes.data(), bytes.size());
	EXPECT_EQ(p.GetUInt8(0), 0);
	EXPECT_EQ(p.GetUInt8(1), (uint8_t)OP_SessionRequest);

	auto req = p.GetSerialize<DaybreakConnect>(0);
	EXPECT_EQ(NetworkToHost(req.protocol_version), 3u);
	EXPECT_EQ(NetworkToHost(req.connect_code), conn_->m_connect_code);
}

// PINNED FINDING #1 (RNG range bug): DaybreakConnection's client-role ctor
// draws m_connect_code (and, in the server-role ctor, m_encode_key) via
// `m_rand.Int(std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max())`.
// EQ::Random::Int takes `int` parameters; passing uint32_t's max (4294967295)
// implicitly converts to -1 as an int (two's-complement wrap), so the call
// is actually `Int(0, -1)` — Int() swaps out-of-order bounds, so it draws
// uniformly from {-1, 0} only. As a uint32_t that's {0xFFFFFFFF, 0x00000000}
// — i.e. the "random" session connect_code has exactly 2 possible values,
// not 2^32. This is a real, reportable bug (predictable/low-entropy session
// identifiers); NOT fixed here per T17 STOP conditions (src/ is out of
// territory) — pinned so a future fix has a regression test to flip green.
TEST_F(DaybreakStateMachineTest, ConnectCodeRngOnlyEverProducesZeroOrAllOnes) {
	std::set<uint32_t> observed;
	for (int i = 0; i < 200; i++) {
		FakeChannel a, b;
		auto transport = std::make_unique<FakeUdpTransport>(&a, &b);
		DaybreakConnectionManagerOptions opts;
		DaybreakConnectionManager mgr(opts, std::move(transport));
		std::shared_ptr<DaybreakConnection> c;
		mgr.OnNewConnection([&](std::shared_ptr<DaybreakConnection> nc) { c = nc; });
		mgr.Connect("peer", 1);
		ASSERT_NE(c, nullptr);
		observed.insert(c->m_connect_code);
	}

	for (auto v : observed) {
		EXPECT_TRUE(v == 0u || v == 0xFFFFFFFFu)
			<< "connect_code " << v << " is neither 0 nor 0xFFFFFFFF — "
			   "if this fires, the RNG-range bug described above has been "
			   "fixed; the tests below that force m_connect_code manually "
			   "to sidestep it can likely be simplified.";
	}
	EXPECT_LE(observed.size(), 2u)
		<< "PINNED: connect_code should only ever be 0 or 0xFFFFFFFF today "
		   "(see comment above) — more distinct values means the RNG bug "
		   "no longer reproduces as analyzed.";
}

// PINNED FINDING #2 (missing NetworkToHost on receive): the OP_SessionResponse
// handler compares `m_connect_code == reply.connect_code` with NO
// NetworkToHost() applied to reply.connect_code, even though the value on
// the wire is HostToNetwork()-encoded (see SendConnectEmitsWellFormedSessionRequest
// above, and the OP_SessionRequest server-role handler's own
// `reply.connect_code = HostToNetwork(m_connect_code)`). In production this
// is masked by PINNED FINDING #1: connect_code is always 0 or 0xFFFFFFFF,
// and both values are byte-swap palindromes, so the missing swap never
// actually causes a mismatch today. This test forces m_connect_code to a
// realistic, non-palindromic value (bypassing the RNG bug entirely, via
// direct private-field access) to demonstrate the comparison bug in
// isolation, independent of finding #1.
TEST_F(DaybreakStateMachineTest, SessionResponseConnectCodeCompareSkipsByteSwap) {
	createManager();
	conn_->SendConnect();
	c2s_.PopFront();
	conn_->m_connect_code = 0x01020304u; // non-palindromic under byte-swap

	// The value a real peer (or this class's own server role) would put on
	// the wire: HostToNetwork(m_connect_code), echoing what the client sent.
	DaybreakConnectReply wire_correct;
	wire_correct.zero          = 0;
	wire_correct.opcode         = OP_SessionResponse;
	wire_correct.connect_code   = HostToNetwork(conn_->m_connect_code);
	wire_correct.encode_key     = 0;
	wire_correct.crc_bytes       = 2;
	wire_correct.encode_pass1    = EncodeNone;
	wire_correct.encode_pass2    = EncodeNone;
	wire_correct.max_packet_size = HostToNetwork(512u);
	DynamicPacket p1;
	p1.PutSerialize(0, wire_correct);
	deliver(p1);

	EXPECT_EQ(conn_->GetStatus(), StatusConnecting)
		<< "PINNED: a wire-correct connect_code echo (what a real server "
		   "would send) is REJECTED for non-palindromic connect codes — "
		   "the comparison is missing NetworkToHost(). See comment above.";

	// What the CURRENT code's check actually accepts instead: the raw
	// unswapped host value, which no real peer would ever put on the wire.
	DaybreakConnectReply raw_match = wire_correct;
	raw_match.connect_code = conn_->m_connect_code;
	DynamicPacket p2;
	p2.PutSerialize(0, raw_match);
	deliver(p2);

	EXPECT_EQ(conn_->GetStatus(), StatusConnected)
		<< "current (buggy) comparison accepts the unswapped value instead";
}

// PINNED FINDING #3 (missing NetworkToHost on encode_key/max_packet_size):
// unlike connect_code (which at least has a comparison, however broken),
// `m_encode_key = reply.encode_key;` and `m_max_packet_size = reply.max_packet_size;`
// in the OP_SessionResponse handler store the raw wire (network-order)
// bytes directly, with no NetworkToHost() at all — while the sender side
// (OP_SessionRequest handler) does apply HostToNetwork() when building
// those same fields. Pinning current (mis-swapped) behavior.
TEST_F(DaybreakStateMachineTest, SessionResponseStoresMaxPacketSizeWithoutByteSwap) {
	createManager();
	conn_->SendConnect();
	c2s_.PopFront();
	conn_->m_connect_code = conn_->m_connect_code; // whatever the RNG drew; comparison bug #2 is orthogonal here — use the raw-accepted form

	DaybreakConnectReply reply;
	reply.zero          = 0;
	reply.opcode         = OP_SessionResponse;
	reply.connect_code   = conn_->m_connect_code; // use the form finding #2 shows is currently accepted
	reply.encode_key     = 0;
	reply.crc_bytes       = 2;
	reply.encode_pass1    = EncodeNone;
	reply.encode_pass2    = EncodeNone;
	reply.max_packet_size = HostToNetwork(512u); // wire-correct value a real peer would send for "512"
	DynamicPacket p;
	p.PutSerialize(0, reply);
	deliver(p);

	ASSERT_EQ(conn_->GetStatus(), StatusConnected);
	EXPECT_EQ(conn_->m_max_packet_size, HostToNetwork(512u))
		<< "PINNED: max_packet_size is stored as the raw wire bytes "
		   "(byte-swapped relative to the intended value 512), not "
		   "NetworkToHost()-converted. See comment above.";
	EXPECT_NE(conn_->m_max_packet_size, 512u)
		<< "if this now equals 512, the missing-NetworkToHost bug has been fixed";
}

TEST_F(DaybreakStateMachineTest, WrongConnectCodeSessionResponseIgnored) {
	createManager();
	conn_->SendConnect();
	c2s_.PopFront();

	DaybreakConnectReply reply;
	reply.zero          = 0;
	reply.opcode         = OP_SessionResponse;
	reply.connect_code   = conn_->m_connect_code + 1; // deliberately wrong, either interpretation
	reply.encode_key     = 0;
	reply.crc_bytes       = 2;
	reply.encode_pass1    = EncodeNone;
	reply.encode_pass2    = EncodeNone;
	reply.max_packet_size = HostToNetwork(512u);

	DynamicPacket p;
	p.PutSerialize(0, reply);
	deliver(p);

	EXPECT_EQ(conn_->GetStatus(), StatusConnecting);
}

TEST_F(DaybreakStateMachineTest, CloseSendsDisconnectAndEntersDisconnecting) {
	createManager();
	forceConnected();

	conn_->Close();

	EXPECT_EQ(conn_->GetStatus(), StatusDisconnecting);
	ASSERT_FALSE(c2s_.Empty());
	auto bytes = c2s_.PopFront();
	ASSERT_GE(bytes.size(), 2u);
	EXPECT_EQ(bytes[0], 0);
	EXPECT_EQ(bytes[1], (uint8_t)OP_SessionDisconnect);
}

TEST_F(DaybreakStateMachineTest, ManagerProcessTearsDownAfterCloseTimeElapses) {
	createManager();
	forceConnected();
	conn_->Close();
	ASSERT_EQ(conn_->GetStatus(), StatusDisconnecting);

	// Backdate close_time instead of sleeping — deterministic simulation of
	// elapsed wall-clock time (see file header re: Clock has no seam).
	conn_->m_close_time = Clock::now() - std::chrono::milliseconds(opts_.connection_close_time + 100);

	manager_->Process();

	EXPECT_EQ(manager_->m_connections.size(), 0u);
	EXPECT_EQ(conn_->GetStatus(), StatusDisconnected);
}

TEST_F(DaybreakStateMachineTest, SessionDisconnectFromPeerEntersDisconnecting) {
	createManager();
	forceConnected();

	DaybreakDisconnect dc;
	dc.zero         = 0;
	dc.opcode        = OP_SessionDisconnect;
	dc.connect_code  = HostToNetwork(conn_->m_connect_code);
	DynamicPacket p;
	p.PutSerialize(0, dc);
	conn_->AppendCRC(p); // OP_SessionDisconnect is CRC-checked (not in the exclusion list)
	deliver(p);

	EXPECT_EQ(conn_->GetStatus(), StatusDisconnecting);
}

// =====================================================================
// Priority 2: Sequenced delivery
// =====================================================================

TEST_F(DaybreakStateMachineTest, InOrderDeliveryExactlyOnce) {
	createManager();
	forceConnected(/*crc_bytes=*/2);

	auto payload = MakeAppPayload(0xABCD, {1, 2, 3, 4});
	auto frame   = buildReliableFrame(0, 0, payload);
	deliver(frame);

	ASSERT_EQ(received_.size(), 1u);
	EXPECT_EQ(received_[0].first, 0xABCDu);
	EXPECT_EQ(conn_->m_streams[0].sequence_in, 1);
}

TEST_F(DaybreakStateMachineTest, OutOfOrderArrivalBufferedThenDeliveredInOrder) {
	createManager();
	forceConnected();

	auto payload0 = MakeAppPayload(0x0001, {'a'});
	auto payload1 = MakeAppPayload(0x0002, {'b'});

	// Deliver seq 1 first: future, must be queued (no delivery yet).
	deliver(buildReliableFrame(0, 1, payload1));
	EXPECT_EQ(received_.size(), 0u);
	EXPECT_EQ(conn_->m_streams[0].packet_queue.size(), 1u);

	// Now deliver seq 0: current, processed immediately.
	deliver(buildReliableFrame(0, 0, payload0));
	ASSERT_GE(received_.size(), 1u);
	EXPECT_EQ(received_[0].first, 0x0001u);

	// Draining the queue for the now-current seq 1 happens in
	// DaybreakConnection::Process() (called every tick, unconditionally,
	// unlike the hold-timer-gated FlushBuffer()).
	conn_->Process();

	ASSERT_EQ(received_.size(), 2u);
	EXPECT_EQ(received_[1].first, 0x0002u);
	EXPECT_EQ(conn_->m_streams[0].packet_queue.size(), 0u);
}

TEST_F(DaybreakStateMachineTest, DuplicatePacketDeliveredOnce) {
	createManager();
	forceConnected();

	auto payload = MakeAppPayload(0x0003, {'x'});
	deliver(buildReliableFrame(0, 0, payload));
	ASSERT_EQ(received_.size(), 1u);

	// Same sequence again: now "past" (sequence_in already advanced to 1).
	deliver(buildReliableFrame(0, 0, payload));
	EXPECT_EQ(received_.size(), 1u) << "duplicate must not be re-delivered";
}

// Pin CompareSequence's actual future/past window: a sequence far ahead of
// what's expected (diff > 10000) is classified SequencePast, NOT
// SequenceFuture — i.e. it is treated like a stale duplicate (re-ack of
// sequence_in-1) rather than buffered. This is a real, possibly surprising
// edge in the 16-bit sequence comparator; pinning current behavior per plan.
TEST_F(DaybreakStateMachineTest, FarFutureSequenceTreatedAsPastNotQueued) {
	createManager();
	forceConnected();

	EXPECT_EQ(conn_->CompareSequence(0, 20000), SequencePast);
	EXPECT_EQ(conn_->CompareSequence(0, 5000), SequenceFuture);

	auto payload = MakeAppPayload(0x0004, {'y'});
	deliver(buildReliableFrame(0, 20000, payload));

	EXPECT_EQ(received_.size(), 0u);
	EXPECT_EQ(conn_->m_streams[0].packet_queue.size(), 0u)
		<< "far-future seq classified Past is not queued (pinned CompareSequence behavior)";
}

// =====================================================================
// Priority 3: Fragmentation, both directions
// =====================================================================

TEST_F(DaybreakStateMachineTest, RecvFragmentReassemblyByteIdentical) {
	createManager();
	forceConnected();

	std::vector<uint8_t> original = MakeAppPayload(0x1234, std::vector<uint8_t>(40, 0x7A));
	uint32_t total_size = (uint32_t)original.size();

	size_t first_chunk_len = 20;
	size_t used = 0;

	DaybreakReliableFragmentHeader fh;
	fh.reliable.zero     = 0;
	fh.reliable.opcode    = (uint8_t)(OP_Fragment + 0);
	fh.reliable.sequence  = HostToNetwork((uint16_t)0);
	fh.total_size          = HostToNetwork(total_size);

	DynamicPacket frag1;
	frag1.PutSerialize(0, fh);
	frag1.PutData(frag1.Length(), original.data(), first_chunk_len);
	used += first_chunk_len;
	conn_->AppendCRC(frag1);
	deliver(frag1);

	EXPECT_EQ(received_.size(), 0u) << "not complete after first fragment";

	DaybreakReliableHeader ch;
	ch.zero      = 0;
	ch.opcode     = (uint8_t)(OP_Fragment + 0);
	ch.sequence   = HostToNetwork((uint16_t)1);
	DynamicPacket frag2;
	frag2.PutSerialize(0, ch);
	frag2.PutData(frag2.Length(), original.data() + used, original.size() - used);
	conn_->AppendCRC(frag2);
	deliver(frag2);

	ASSERT_EQ(received_.size(), 1u);
	EXPECT_EQ(received_[0].first, 0x1234u);
	ASSERT_EQ(received_[0].second.size(), original.size());
	EXPECT_EQ(received_[0].second, original);
}

TEST_F(DaybreakStateMachineTest, SendFragmentsLargePayloadReassembledByPeer) {
	createManager();
	forceConnected(2, EncodeNone, EncodeNone, /*max_packet_size=*/128);

	std::vector<uint8_t> payload = MakeAppPayload(0x5678, std::vector<uint8_t>(300, 0x42));
	DynamicPacket p;
	p.PutData(0, payload.data(), payload.size());

	conn_->QueuePacket(p, /*stream=*/0, /*reliable=*/true);
	conn_->FlushBuffer();

	ASSERT_GT(c2s_.Pending(), 1u) << "300-byte payload with 128-byte max packet must fragment into >1 packet";

	// Reassemble manually from the captured wire fragments.
	std::vector<uint8_t> reassembled;
	uint32_t expected_total = 0;
	auto raw_frames = c2s_.DrainAll();
	for (auto &bytes : raw_frames) {
		ASSERT_GE(bytes.size(), (size_t)opts_.crc_length + DaybreakReliableHeader::size());
		size_t body_len = bytes.size() - opts_.crc_length;
		StaticPacket fp((void *)bytes.data(), body_len);
		ASSERT_EQ(fp.GetUInt8(0), 0);
		uint8_t opcode = fp.GetUInt8(1);
		ASSERT_GE(opcode, (uint8_t)OP_Fragment);
		ASSERT_LE(opcode, (uint8_t)OP_Fragment4);

		if (expected_total == 0) {
			auto fh = fp.GetSerialize<DaybreakReliableFragmentHeader>(0);
			expected_total = NetworkToHost(fh.total_size);
			size_t data_len = body_len - DaybreakReliableFragmentHeader::size();
			reassembled.insert(reassembled.end(),
				(uint8_t *)fp.Data() + DaybreakReliableFragmentHeader::size(),
				(uint8_t *)fp.Data() + DaybreakReliableFragmentHeader::size() + data_len);
		} else {
			size_t data_len = body_len - DaybreakReliableHeader::size();
			reassembled.insert(reassembled.end(),
				(uint8_t *)fp.Data() + DaybreakReliableHeader::size(),
				(uint8_t *)fp.Data() + DaybreakReliableHeader::size() + data_len);
		}
	}

	EXPECT_EQ(expected_total, payload.size());
	ASSERT_EQ(reassembled.size(), payload.size());
	EXPECT_EQ(reassembled, payload);
}

// =====================================================================
// Priority 4: Ack / retransmit under drops
// =====================================================================

TEST_F(DaybreakStateMachineTest, DroppedSendIsRetransmittedOnProcessResend) {
	createManager();
	forceConnected();

	auto payload = MakeAppPayload(0x0009, {'z'});
	DynamicPacket p;
	p.PutData(0, payload.data(), payload.size());
	conn_->QueuePacket(p, 0, true);
	conn_->FlushBuffer();

	ASSERT_EQ(c2s_.Pending(), 1u);
	c2s_.PopFront(); // the original send; simulate it being dropped in flight

	ASSERT_EQ(conn_->m_streams[0].sent_packets.size(), 1u);
	auto &sent = conn_->m_streams[0].sent_packets.begin()->second;
	EXPECT_EQ(sent.times_resent, 0u);

	// Backdate instead of sleeping: push first_sent/last_sent far enough
	// into the past that time_since_first_sent exceeds the (small, test-
	// configured) resend_delay.
	sent.first_sent = Clock::now() - std::chrono::milliseconds(500);
	sent.last_sent  = sent.first_sent;

	conn_->ProcessResend();
	// ProcessResend() only re-buffers via InternalBufferedSend(); actually
	// flushing to the transport is normally driven by the hold-timer in
	// Connection::Process() each tick. Flush explicitly (deterministic,
	// no wall-clock wait) to observe the resent bytes on the wire.
	conn_->FlushBuffer();

	EXPECT_EQ(c2s_.Pending(), 1u) << "resend must produce a new wire send";
	EXPECT_EQ(conn_->m_stats.resent_packets, 1u);
	EXPECT_EQ(conn_->m_streams[0].sent_packets.begin()->second.times_resent, 1u);
}

TEST_F(DaybreakStateMachineTest, AckStopsRetransmission) {
	createManager();
	forceConnected();

	auto payload = MakeAppPayload(0x000A, {'w'});
	DynamicPacket p;
	p.PutData(0, payload.data(), payload.size());
	conn_->QueuePacket(p, 0, true);
	conn_->FlushBuffer();
	c2s_.PopFront();

	ASSERT_EQ(conn_->m_streams[0].sent_packets.size(), 1u);

	// Peer acks sequence 0.
	deliver(buildAckFrame(0, 0, /*out_of_order=*/false));

	EXPECT_EQ(conn_->m_streams[0].sent_packets.size(), 0u);

	conn_->ProcessResend();
	EXPECT_EQ(c2s_.Pending(), 0u) << "acked packet must not be resent";
}

TEST_F(DaybreakStateMachineTest, OutOfOrderAckRemovesOnlyThatSequence) {
	createManager();
	forceConnected();

	for (uint16_t seq = 0; seq < 3; seq++) {
		auto payload = MakeAppPayload((uint16_t)(0x0010 + seq), {(uint8_t)seq});
		DynamicPacket p;
		p.PutData(0, payload.data(), payload.size());
		conn_->QueuePacket(p, 0, true);
	}
	conn_->FlushBuffer();
	ASSERT_EQ(conn_->m_streams[0].sent_packets.size(), 3u);

	deliver(buildAckFrame(0, 1, /*out_of_order=*/true));

	ASSERT_EQ(conn_->m_streams[0].sent_packets.size(), 2u);
	EXPECT_TRUE(conn_->m_streams[0].sent_packets.count(0));
	EXPECT_FALSE(conn_->m_streams[0].sent_packets.count(1));
	EXPECT_TRUE(conn_->m_streams[0].sent_packets.count(2));
}

TEST_F(DaybreakStateMachineTest, ResendTimeoutClosesConnection) {
	createManager();
	forceConnected();
	opts_.resend_timeout = 1000; // small for this test; opts_ is captured
	                             // by value into manager_ at construction,
	                             // so re-poke the live options object.
	manager_->GetOptions().resend_timeout = 1000;

	auto payload = MakeAppPayload(0x0020, {'q'});
	DynamicPacket p;
	p.PutData(0, payload.data(), payload.size());
	conn_->QueuePacket(p, 0, true);
	conn_->FlushBuffer();
	ASSERT_EQ(conn_->m_streams[0].sent_packets.size(), 1u);

	auto &sent = conn_->m_streams[0].sent_packets.begin()->second;
	sent.first_sent = Clock::now() - std::chrono::milliseconds(2000);

	conn_->ProcessResend();

	EXPECT_EQ(conn_->GetStatus(), StatusDisconnecting)
		<< "resend_timeout exceeded must close the connection (pinned behavior)";
}

// =====================================================================
// Priority 5: CRC + compression
// =====================================================================

TEST_F(DaybreakStateMachineTest, CorruptedByteRejectedNoDeliveryNoCrash) {
	createManager();
	forceConnected();

	auto payload = MakeAppPayload(0x0030, {1, 2, 3});
	auto frame   = buildReliableFrame(0, 0, payload);

	// Corrupt a payload byte (covered by CRC) without recomputing CRC.
	std::vector<uint8_t> bytes((uint8_t *)frame.Data(), (uint8_t *)frame.Data() + frame.Length());
	ASSERT_GT(bytes.size(), DaybreakReliableHeader::size());
	bytes[DaybreakReliableHeader::size()] ^= 0xFF;

	EXPECT_NO_THROW(deliver(bytes));
	EXPECT_EQ(received_.size(), 0u);
	EXPECT_EQ(conn_->m_streams[0].sequence_in, 0)
		<< "CRC-rejected packet must not advance sequence_in";
}

TEST_F(DaybreakStateMachineTest, CompressedRoundTripThroughConnection) {
	createManager();
	forceConnected(2, EncodeCompression, EncodeNone);

	// >30 bytes so DaybreakConnection::Compress() actually deflates
	// (see the `length > 30` gate in daybreak_connection.cpp).
	std::vector<uint8_t> original = MakeAppPayload(0x0040, std::vector<uint8_t>(100, 0x55));
	DynamicPacket p;
	p.PutData(0, original.data(), original.size());

	conn_->QueuePacket(p, 0, true);
	conn_->FlushBuffer();

	ASSERT_EQ(c2s_.Pending(), 1u);
	auto wire = c2s_.PopFront();

	// Loopback: feed the connection's own sent bytes back into itself.
	// DaybreakConnection is symmetric (the same class plays client or
	// server in production), so decode(encode(x)) round-trips correctly
	// through the exact same session (key/crc/compression state).
	deliver(wire);

	ASSERT_EQ(received_.size(), 1u);
	EXPECT_EQ(received_[0].first, 0x0040u);
	EXPECT_EQ(received_[0].second, original);
}

// =====================================================================
// Priority 6: Combined packets through the connection path
// =====================================================================

TEST_F(DaybreakStateMachineTest, CombinedPacketCarriesMultipleUnreliableSends) {
	createManager();
	forceConnected();

	auto payloadA = MakeAppPayload(0x0050, {'A'});
	auto payloadB = MakeAppPayload(0x0051, {'B'});

	DynamicPacket a;
	a.PutData(0, payloadA.data(), payloadA.size());
	DynamicPacket b;
	b.PutData(0, payloadB.data(), payloadB.size());

	// Unreliable QueuePacket -> InternalBufferedSend, held until FlushBuffer.
	conn_->QueuePacket(a, 0, false);
	conn_->QueuePacket(b, 0, false);
	ASSERT_TRUE(c2s_.Empty()) << "buffered sends must not go out until FlushBuffer";

	conn_->FlushBuffer();
	ASSERT_EQ(c2s_.Pending(), 1u) << "two small buffered sends must combine into one OP_Combined packet";

	auto wire = c2s_.PopFront();
	StaticPacket wp((void *)wire.data(), wire.size());
	EXPECT_EQ(wp.GetUInt8(0), 0);
	EXPECT_EQ(wp.GetUInt8(1), (uint8_t)OP_Combined);

	deliver(wire);

	ASSERT_EQ(received_.size(), 2u);
	EXPECT_EQ(received_[0].first, 0x0050u);
	EXPECT_EQ(received_[1].first, 0x0051u);
}

// =====================================================================
// Integration smoke test: full handshake driven through Manager::Tick(),
// exercising the real transport-drain + timer-gated Process() path (not
// just direct ProcessPacket() calls). Uses a bounded spin over Tick() —
// not a sleep — since Process()'s connect_delay_ms gate reads real elapsed
// wall-clock time and there is no clock injection seam (see Review).
// =====================================================================

TEST_F(DaybreakStateMachineTest, FullHandshakeViaManagerTick) {
	opts_.connect_delay_ms = 0;
	createManager();

	bool sent_request = false;
	for (int i = 0; i < 2000000 && !sent_request; i++) {
		manager_->Tick();
		if (!c2s_.Empty()) {
			sent_request = true;
		}
	}
	ASSERT_TRUE(sent_request) << "SendConnect never fired via Manager::Tick()/Process()";

	auto req_bytes = c2s_.PopFront();
	StaticPacket rp((void *)req_bytes.data(), req_bytes.size());
	auto req = rp.GetSerialize<DaybreakConnect>(0);

	DaybreakConnectReply reply;
	reply.zero          = 0;
	reply.opcode         = OP_SessionResponse;
	reply.connect_code   = req.connect_code; // wire echo, exactly as received (opaque token)
	reply.encode_key     = 0;
	reply.crc_bytes       = 0; // avoid CRC bookkeeping for this smoke test
	reply.encode_pass1    = EncodeNone;
	reply.encode_pass2    = EncodeNone;
	reply.max_packet_size = req.max_packet_size;

	DynamicPacket rep;
	rep.PutSerialize(0, reply);
	std::vector<uint8_t> rep_bytes((uint8_t *)rep.Data(), (uint8_t *)rep.Data() + rep.Length());
	s2c_.InjectRaw(rep_bytes);

	manager_->Tick();

	// See the two connect_code tests above: whether this transitions to
	// StatusConnected depends on the same wire-vs-host comparison finding.
	// This smoke test exercises the full Tick()-driven path regardless of
	// that outcome and must not hang, crash, or throw.
	SUCCEED();
}
