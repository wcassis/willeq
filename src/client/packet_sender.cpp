// The zone-stream outbound path, as a context service.
//
// K13 (Batch K, refactor/K_component_extraction.md): both bodies below came
// from src/client/eq.cpp VERBATIM -- they were EverQuest::SafeQueueZonePacket
// and EverQuest::QueuePacket. Every member they touch is spelled exactly as it
// was, because PacketSender holds references to EverQuest's members under the
// same names (m_zone_connection, m_zone_connection_manager, m_session_recorder,
// m_main_thread_id, m_game_state) and owns the two the plan moved here
// (m_replay_suppressed_sends and the T268 capture seam).
//
// TWO substitutions beyond §Mechanism 4's list, both forced by the fact that a
// context service is NOT a friend of EverQuest the way a component is (K13
// correction: the fifth substitution shape):
//   s_debug_level      -> EverQuest::GetDebugLevel()   (the public accessor
//                         over the same private static)
//   GetOpcodeName(...) -> eqt::strings::GetOpcodeName(...) (K14 moved it to a
//                         free function in include/client/eq_strings.h, same
//                         standing T180's GetClassName and T271's GetRaceName
//                         had as public EverQuest statics -- a pure lookup
//                         table, no EverQuest state)
// Neither changes behaviour: GetDebugLevel() returns s_debug_level and nothing
// else, and GetOpcodeName is a static switch over opcode values.

#include "client/packet_sender.h"

#include "client/eq.h"
#include "common/logging.h"

#include <cstring>

bool eqt::PacketSender::SafeQueueZone(EQ::Net::Packet &p, int stream, bool reliable)
{
	// T346: the T268 seam covers this sender too. Queue() alone was not
	// enough to pin an ORDERED outbound sequence: OP_ClientUpdate goes out
	// through here unreliable (SendPositionUpdate's SafeQueueZone(p, 0,
	// false)), and an unreliable packet never lands in the connection's
	// sent_packets resend map, so the DaybreakConnection seam T344 used
	// cannot see it at all. Recorded before the gates, exactly as Queue()
	// does, so the seam means "the client decided to send this". No-op
	// unless armed.
	if (p.Length() >= 2) {
		Test_RecordQueuedPacket(p.GetUInt16(0), static_cast<const uint8_t*>(p.Data()) + 2,
			p.Length() - 2);
	}

	if (!m_zone_connection) {
		LOG_WARN(MOD_ZONE, "Attempted to send packet with null zone connection");
		return false;
	}

	if (m_session_recorder && p.Length() >= 2) {
		m_session_recorder->record(EQT::Net::SessionStream::Zone, EQT::Net::SessionDirection::ClientToServer,
			p.GetUInt16(0), (const uint8_t*)p.Data() + 2, static_cast<uint32_t>(p.Length() - 2));
	}

	// Update thread must use async to avoid libuv thread-safety violation
	if (std::this_thread::get_id() != m_main_thread_id) {
		if (m_zone_connection_manager) {
			m_zone_connection_manager->QueuePacketAsync(m_zone_connection, p, stream, reliable);
			return true;
		}
		return false;
	}

	// Add debugging for large reliable packets
	if (reliable && p.Length() > 400 && EverQuest::GetDebugLevel() >= 2) {
		uint16_t opcode = p.GetUInt16(0);
		LOG_TRACE(MOD_ZONE, "SafeQueueZonePacket: Large reliable packet - opcode={:#06x} ({}), length={}, stream={}",
			opcode, eqt::strings::GetOpcodeName(opcode), p.Length(), stream);

		// Check connection state before sending large packets
		try {
			auto stats = m_zone_connection->GetStats();
			LOG_DEBUG(MOD_ZONE, "Connection stats before large packet: sent={}, recv={}, resent={}", stats.sent_packets, stats.recv_packets, stats.resent_packets);
		} catch (...) {
			LOG_WARN(MOD_ZONE, "Failed to get connection stats");
		}
	}

	// Check if connection is still valid
	try {
		// Get connection stats to verify the connection is still valid
		auto stats = m_zone_connection->GetStats();

		// Additional check - if stats look corrupted, don't send
		if (stats.recv_packets == 0 && stats.sent_packets == 0 && m_game_state.world().isZoneConnected()) {
			LOG_WARN(MOD_ZONE, "Zone connection appears invalid (zero stats)");
			return false;
		}

		m_zone_connection->QueuePacket(p, stream, reliable);

		// Log successful queueing of large reliable packets
		if (reliable && p.Length() > 400 && EverQuest::GetDebugLevel() >= 2) {
			LOG_DEBUG(MOD_ZONE, "Large reliable packet queued successfully");
		}

		return true;
	}
	catch (const std::exception& e) {
		LOG_ERROR(MOD_ZONE, "Exception in SafeQueueZonePacket: {} (opcode={:#06x}, len={}, reliable={})",
			e.what(), p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_game_state.world().setZoneConnected(false);  // T286: only store
		return false;
	}
	catch (...) {
		LOG_ERROR(MOD_ZONE, "Unknown exception in SafeQueueZonePacket (opcode={:#06x}, len={}, reliable={})",
			p.GetUInt16(0), p.Length(), reliable);
		m_zone_connection.reset();  // Clear the corrupted connection
		m_game_state.world().setZoneConnected(false);  // T286: only store
		return false;
	}
}

void eqt::PacketSender::Queue(uint16_t opcode, EQ::Net::DynamicPacket* packet)
{
	if (!packet) {
		return;
	}
	Test_RecordQueuedPacket(opcode, packet->Data(), packet->Length());  // T268 seam (no-op unless armed)
	if (!m_zone_connection) {
		return;
	}

	if (m_session_recorder) {
		m_session_recorder->record(EQT::Net::SessionStream::Zone, EQT::Net::SessionDirection::ClientToServer,
			opcode, packet->Data(), static_cast<uint32_t>(packet->Length()));
	}

	EQ::Net::DynamicPacket p;
	p.Resize(packet->Length() + 2); // Add space for opcode
	p.PutUInt16(0, opcode);
	if (packet->Length() > 0) {
		memcpy(static_cast<uint8_t*>(p.Data()) + 2, packet->Data(), packet->Length());
	}

	// Update thread must use async to avoid libuv thread-safety violation
	if (std::this_thread::get_id() != m_main_thread_id) {
		if (m_zone_connection_manager) {
			m_zone_connection_manager->QueuePacketAsync(m_zone_connection, p);
			return;
		}
		return;
	}

	m_zone_connection->QueuePacket(p);
}
