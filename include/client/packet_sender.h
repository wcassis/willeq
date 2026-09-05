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
#include "common/net/session_log.h"
#include "common/net/session_recorder.h"
#include "common/logging.h"

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

	// T379 (BOARD B146): the LOGIN and WORLD streams. Both are the same
	// three-step body SafeQueueZone has -- T268 seam, then the R01 recorder
	// with this stream's SessionStream, then the connection's queue -- and
	// neither is a variant of SafeQueueZone: they run BEFORE a zone connection
	// exists, so routing them there would refuse every packet on the null-zone
	// gate. Before T379 the twelve senders in
	// src/client/login_world/login_world_component.cpp called
	// connection->QueuePacket() directly, so a --record-session .sleq held the
	// SERVER's half of the handshake and none of the client's, even though
	// EQT::Net::SessionStream has had Login and World members all along.
	//
	// Null-safe on BOTH the binding and the connection: a call before
	// BindLoginWorldStreams(), or with the connection down, logs, bumps the
	// replay-suppression counter (what the twelve call sites did themselves in
	// their else branch) and returns false. No dereference either way.
	bool QueueLogin(EQ::Net::Packet &p);
	bool QueueWorld(EQ::Net::Packet &p);

	// The login and world connections are owned by
	// eqt::login_world::LoginWorldComponent, which EverQuest constructs in its
	// BODY -- after this service's members are bound in the initialiser list.
	// So these two arrive as pointers set once, from EverQuest's constructor,
	// rather than as references in the ctor list like m_zone_connection. The
	// component is heap-allocated and never moved, so the addresses are stable
	// for the life of the client.
	void BindLoginWorldStreams(std::shared_ptr<EQ::Net::DaybreakConnection>* login_connection,
	                           std::shared_ptr<EQ::Net::DaybreakConnection>* world_connection) {
		m_login_connection = login_connection;
		m_world_connection = world_connection;
	}

	// Batch R: bumped whenever a Zone packet sender no-ops because the zone
	// connection is null (replay/test mode has no live connection). Was
	// EverQuest::m_replay_suppressed_sends; the 24 `++owner_.m_replay_
	// suppressed_sends;` statements in the components are now this call.
	void NoteSuppressedSend() { ++m_replay_suppressed_sends; }
	uint64_t ReplaySuppressedSendCount() const { return m_replay_suppressed_sends; }

	// T88 test seam (T366, B038 cluster 1): the send guards in the movement
	// and group families refuse to build a packet without a live zone
	// connection and a fully-zoned-in player, which no unit test has. The
	// bypass belongs to the thing that SENDS, so it lives here rather than on
	// EverQuest: the guards read ctx_.net.TestBypassSendGuard() and a test
	// arms it with eq.Net().Test_SetBypassSendGuardForSeam(true). Off by
	// default (production never touches it).
	void Test_SetBypassSendGuardForSeam(bool bypass) { m_test_bypass_send_guard = bypass; }
	bool TestBypassSendGuard() const { return m_test_bypass_send_guard; }

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
	// T379: the body QueueLogin and QueueWorld share. `slot` is the pointer
	// BindLoginWorldStreams was given (may be null, and may point at a null
	// connection); `stream` and `module` are all that differ between them.
	bool QueueOnStream(std::shared_ptr<EQ::Net::DaybreakConnection>* slot,
	                   EQT::Net::SessionStream stream, LogModule module,
	                   const char* stream_name, EQ::Net::Packet &p);

	eqt::state::GameState& m_game_state;
	std::shared_ptr<EQ::Net::DaybreakConnection>& m_zone_connection;
	// T379: see BindLoginWorldStreams above -- pointers, not references, and
	// null until EverQuest's constructor has built LoginWorldComponent.
	std::shared_ptr<EQ::Net::DaybreakConnection>* m_login_connection = nullptr;
	std::shared_ptr<EQ::Net::DaybreakConnection>* m_world_connection = nullptr;
	std::unique_ptr<EQ::Net::DaybreakConnectionManager>& m_zone_connection_manager;
	std::unique_ptr<EQT::Net::SessionRecorder>& m_session_recorder;
	std::thread::id& m_main_thread_id;

	uint64_t m_replay_suppressed_sends = 0;
	bool m_test_bypass_send_guard = false;
	bool m_test_capture_queued_packets = false;
	std::vector<TestQueuedPacket> m_test_queued_packets;
};

}  // namespace eqt
