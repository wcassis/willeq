#pragma once

// Batch K (refactor/K_component_extraction.md), unit K13.
//
// PacketSender is the third CONTEXT SERVICE: the zone-stream outbound path.
// It holds the two send primitives every family calls (QueuePacket and
// SafeQueueZonePacket), the replay-suppression counter they feed, and the T268
// queued-packet capture seam that sits inside QueuePacket.
//
// Moving the counter here closes K09's deviation 1: m_replay_suppressed_sends
// was left on EverQuest because "a member whose test-facing accessor has
// dozens of call sites stays under §3" (K09 correction (bb)). It has 24
// component reaches (owner_.m_replay_suppressed_sends) and 49 test reads; the
// former become ctx_.net.NoteSuppressedSend() and the latter
// eq.Net().ReplaySuppressedSendCount().
//
// The service owns the counter and the capture seam and NOTHING else. Its
// other five members are references to EverQuest's, under the SAME NAMES, so
// both bodies below are byte-identical to the bodies they were moved from.
//
// EverQuest keeps QueuePacket() as a forwarder (the plan's explicit exception
// to §Mechanism 5, for the non-family callers) and keeps the three T268 Test_*
// entry points as forwarders plus a type alias, so the 30+ existing test call
// sites are untouched.

#include "client/state/game_state.h"
#include "common/net/daybreak_connection.h"
#include "common/net/session_recorder.h"

#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace eqt {

class PacketSender {
public:
	PacketSender(eqt::state::GameState& game_state,
	             std::shared_ptr<EQ::Net::DaybreakConnection>& zone_connection,
	             std::unique_ptr<EQ::Net::DaybreakConnectionManager>& zone_connection_manager,
	             std::unique_ptr<EQT::Net::SessionRecorder>& session_recorder,
	             std::thread::id& main_thread_id)
		: m_game_state(game_state),
		  m_zone_connection(zone_connection),
		  m_zone_connection_manager(zone_connection_manager),
		  m_session_recorder(session_recorder),
		  m_main_thread_id(main_thread_id) {}

	// Was EverQuest::QueuePacket(uint16_t, EQ::Net::DynamicPacket*).
	void Queue(uint16_t opcode, EQ::Net::DynamicPacket* packet);

	// Was EverQuest::SafeQueueZonePacket(EQ::Net::Packet&, int, bool).
	bool SafeQueueZone(EQ::Net::Packet &p, int stream = 0, bool reliable = true);

	// Batch R: bumped whenever a Zone packet sender no-ops because the zone
	// connection is null (replay/test mode has no live connection). Was
	// EverQuest::m_replay_suppressed_sends; the 24 `++owner_.m_replay_
	// suppressed_sends;` statements in the components are now this call.
	void NoteSuppressedSend() { ++m_replay_suppressed_sends; }
	uint64_t ReplaySuppressedSendCount() const { return m_replay_suppressed_sends; }

	// T268 test seam: every packet Queue() was asked to send (opcode + body
	// bytes), captured before the connection gate so a headless test can
	// assert the exact wire sequence a loot action produces (OP_LootRequest,
	// N x OP_LootItem, OP_EndLootRequest) instead of re-deriving it. Off by
	// default (zero cost in production). Read-only; test-only.
	struct TestQueuedPacket {
		uint16_t opcode = 0;
		std::vector<uint8_t> bytes;  // body only (no opcode prefix)
	};
	void Test_SetCaptureQueuedPacketsForSeam(bool capture) { m_test_capture_queued_packets = capture; }
	const std::vector<TestQueuedPacket>& Test_GetQueuedPackets() const { return m_test_queued_packets; }
	void Test_ClearQueuedPackets() { m_test_queued_packets.clear(); }
	// Called from Queue() BEFORE its connection gate; no-op unless armed.
	void Test_RecordQueuedPacket(uint16_t opcode, const void* body, size_t len) {
		if (!m_test_capture_queued_packets) {
			return;
		}
		const auto* d = static_cast<const uint8_t*>(body);
		m_test_queued_packets.push_back(TestQueuedPacket{opcode, std::vector<uint8_t>(d, d + len)});
	}

private:
	eqt::state::GameState& m_game_state;
	std::shared_ptr<EQ::Net::DaybreakConnection>& m_zone_connection;
	std::unique_ptr<EQ::Net::DaybreakConnectionManager>& m_zone_connection_manager;
	std::unique_ptr<EQT::Net::SessionRecorder>& m_session_recorder;
	std::thread::id& m_main_thread_id;

	uint64_t m_replay_suppressed_sends = 0;
	bool m_test_capture_queued_packets = false;
	std::vector<TestQueuedPacket> m_test_queued_packets;
};

}  // namespace eqt
