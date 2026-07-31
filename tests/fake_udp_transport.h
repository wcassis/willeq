#pragma once

/**
 * FakeUdpTransport (T17) — in-memory UdpTransport for driving
 * DaybreakConnection/DaybreakConnectionManager without real sockets.
 *
 * Two transports share a pair of FakeChannel queues (one per direction).
 * A FakeChannel is the "wire": Send() enqueues bytes for the peer's Recv();
 * it supports scriptable adversarial delivery (drop-nth, duplicate-next,
 * corrupt-next, reorder-next-pair) so tests can simulate a lossy/jittery
 * link without any real network I/O or wall-clock waits.
 *
 * Usage patterns supported by tests/test_daybreak_state_machine.cpp:
 *  - Single-manager tests: construct one FakeUdpTransport backed by two
 *    channels, drive DaybreakConnection privately (via #define private
 *    public, matching tests/test_daybreak_connection_pcap.cpp precedent),
 *    and inspect/craft bytes on the "peer" side of the channel directly.
 *  - Two-manager tests: wire a client transport and a server transport to
 *    the same channel pair so both directions run real production code.
 */

#include "common/net/udp_transport.h"
#include <cstring>
#include <deque>
#include <set>
#include <vector>

namespace EQ {
namespace Test {

// One direction of an in-memory "wire" between two FakeUdpTransports.
class FakeChannel {
public:
	// Called by the sending transport's send().
	void Send(const uint8_t *data, size_t len) {
		std::vector<uint8_t> pkt(data, data + len);
		size_t index = send_count_++;

		if (drop_indices_.count(index)) {
			return;
		}

		if (corrupt_next_ && !pkt.empty()) {
			pkt[corrupt_offset_ % pkt.size()] ^= 0xFF;
			corrupt_next_ = false;
		}

		pending_.push_back(pkt);

		if (duplicate_next_) {
			pending_.push_back(pkt);
			duplicate_next_ = false;
		}

		if (reorder_next_pair_ && pending_.size() >= 2) {
			std::swap(pending_[pending_.size() - 1], pending_[pending_.size() - 2]);
			reorder_next_pair_ = false;
		}
	}

	// Called by the receiving transport's recv(). Non-blocking: returns 0
	// (not -1) when nothing is pending, matching UdpTransport's contract.
	int Recv(uint8_t *buf, size_t buf_len) {
		if (pending_.empty()) {
			return 0;
		}

		auto pkt = std::move(pending_.front());
		pending_.pop_front();

		size_t n = std::min(buf_len, pkt.size());
		if (n > 0) {
			memcpy(buf, pkt.data(), n);
		}
		return (int)n;
	}

	// --- Delivery scripting (applies to the NEXT Send() call(s)) ---

	// Drop the Nth packet sent on this channel (0-based, counts all sends
	// including ones already scripted for drop/dup/corrupt).
	void DropNth(size_t n) { drop_indices_.insert(n); }

	// Duplicate the very next packet sent (appears twice in the queue).
	void DuplicateNext() { duplicate_next_ = true; }

	// XOR-corrupt one byte of the next packet sent.
	void CorruptNext(size_t byte_offset = 0) {
		corrupt_next_   = true;
		corrupt_offset_ = byte_offset;
	}

	// Swap the next packet sent with the one immediately before it in the
	// pending queue (simple adjacent-pair reorder).
	void ReorderNextPair() { reorder_next_pair_ = true; }

	// Test-side inspection/injection, bypassing the transport entirely —
	// useful when the test is manually playing "the peer" rather than
	// running a second real DaybreakConnectionManager.
	bool Empty() const { return pending_.empty(); }
	size_t Pending() const { return pending_.size(); }

	std::vector<uint8_t> PopFront() {
		auto pkt = std::move(pending_.front());
		pending_.pop_front();
		return pkt;
	}

	std::vector<std::vector<uint8_t>> DrainAll() {
		std::vector<std::vector<uint8_t>> out(pending_.begin(), pending_.end());
		pending_.clear();
		return out;
	}

	// Directly inject bytes as if the peer had sent them (skips Send()'s
	// drop/dup/corrupt scripting — that scripting models loss/jitter of
	// packets actually sent by a transport, not test-authored injection).
	void InjectRaw(std::vector<uint8_t> pkt) { pending_.push_back(std::move(pkt)); }

	size_t SendCount() const { return send_count_; }

private:
	std::deque<std::vector<uint8_t>> pending_;
	std::set<size_t> drop_indices_;
	size_t send_count_ = 0;

	bool duplicate_next_   = false;
	bool corrupt_next_     = false;
	size_t corrupt_offset_ = 0;
	bool reorder_next_pair_ = false;
};

// UdpTransport implementation backed by a pair of FakeChannels: one this
// transport sends into, one it receives from. Always reports open (no real
// socket setup needed) so DaybreakConnectionManager::Tick()'s
// `is_open()`-gated recv loop works without a Connect()/open() dance.
class FakeUdpTransport : public EQ::Net::UdpTransport {
public:
	FakeUdpTransport(FakeChannel *send_channel, FakeChannel *recv_channel)
		: m_send(send_channel), m_recv(recv_channel) {}

	bool open(const std::string & /*addr*/, int /*port*/) override {
		m_open = true;
		return true;
	}

	void close() override { m_open = false; }

	bool is_open() const override { return m_open; }

	int send(const uint8_t *data, size_t len) override {
		if (!m_open) {
			return -1;
		}
		m_send->Send(data, len);
		return (int)len;
	}

	int recv(uint8_t *buf, size_t buf_len) override {
		if (!m_open) {
			return 0;
		}
		return m_recv->Recv(buf, buf_len);
	}

	FakeChannel *SendChannel() const { return m_send; }
	FakeChannel *RecvChannel() const { return m_recv; }

private:
	FakeChannel *m_send;
	FakeChannel *m_recv;
	bool m_open = true;
};

} // namespace Test
} // namespace EQ
