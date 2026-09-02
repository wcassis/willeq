// DaybreakConnection -- the inbound decode path: ProcessPacket, ProcessQueue
// and ProcessDecodedPacket (raw packet in, decoded packet dispatched).
//
// ProcessDecodedPacket is 453 lines and is a recorded Batch-G decomposition
// candidate that J26 deliberately did NOT touch: this unit is move-only.
// Split from src/common/net/daybreak_connection.cpp by
// refactor/J26_daybreak_connection_split.md (move-only, 2026-09-02).

#include "common/net/daybreak_connection.h"
#include "common/util/data_verification.h"
#include "common/net/crc32.h"
#include "common/logging.h"
#include <cstring>
#include <zlib.h>
#include <fmt/format.h>


void EQ::Net::DaybreakConnection::ProcessPacket(Packet &p)
{
	m_last_recv = Clock::now();
	m_stats.recv_packets++;
	m_stats.recv_bytes += p.Length();

	// Helper lambda for FULL hex dump - NO TRUNCATION
	auto full_hex_dump = [](const Packet& pkt) -> std::string {
		std::string hex_dump;
		for (size_t i = 0; i < pkt.Length(); i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", pkt.GetUInt8(i));
			hex_dump += buf;
		}
		return hex_dump;
	};

	auto full_hex_dump_data = [](const char* data, size_t len) -> std::string {
		std::string hex_dump;
		for (size_t i = 0; i < len; i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", (uint8_t)data[i]);
			hex_dump += buf;
		}
		return hex_dump;
	};

	// Log EVERY byte of EVERY packet received
	LOG_TRACE(MOD_NET, "RECV[{}] len={} data={}", m_stats.recv_packets, p.Length(), full_hex_dump(p));

	if (p.Length() < 1) {
		LOG_WARN(MOD_NET, "DROPPED[{}]: packet too short (len={})", m_stats.recv_packets, p.Length());
		return;
	}

	// Keep old helper for compatibility
	auto hex_dump_packet = [](const Packet& pkt, size_t max_bytes = 200) -> std::string {
		std::string hex_dump;
		for (size_t i = 0; i < pkt.Length() && i < max_bytes; i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", pkt.GetUInt8(i));
			hex_dump += buf;
		}
		if (pkt.Length() > max_bytes) hex_dump += "...";
		return hex_dump;
	};

	if (p.Length() >= 2) {
		LOG_TRACE(MOD_NET, "ProcessPacket: first_byte={:#04x}, opcode={:#04x}, length={}",
			p.GetInt8(0), p.GetInt8(1), p.Length());
	}

	auto opcode = p.GetInt8(1);
	if (p.GetInt8(0) == 0 && (opcode == OP_KeepAlive || opcode == OP_OutboundPing)) {
		m_stats.bytes_after_decode += p.Length();
		return;
	}

	if (PacketCanBeEncoded(p)) {
		if (!ValidateCRC(p)) {
			LOG_WARN(MOD_NET, "DROPPED_CRC[{}] len={} data={}", m_stats.recv_packets, p.Length(), full_hex_dump(p));
			if (m_owner->m_on_error_message) {
				m_owner->m_on_error_message(fmt::format("Tossed packet that failed CRC of type {0:#x}", p.Length() >= 2 ? p.GetInt8(1) : 0));
			}

			m_stats.bytes_after_decode += p.Length();
			return;
		}

		if (m_encode_passes[0] == EncodeCompression || m_encode_passes[1] == EncodeCompression)
		{
			EQ::Net::DynamicPacket temp;
			temp.PutPacket(0, p);
			temp.Resize(temp.Length() - m_crc_bytes);

			size_t pre_decode_len = temp.Length();
			LOG_TRACE(MOD_NET, "ProcessPacket: compression enabled, raw_len={} (after CRC strip), opcode={:#04x}",
				pre_decode_len, temp.Length() >= 2 ? temp.GetInt8(1) : 0);

			for (int i = 1; i >= 0; --i) {
				switch (m_encode_passes[i]) {
					case EncodeCompression:
						if(temp.GetInt8(0) == 0)
							Decompress(temp, DaybreakHeader::size(), temp.Length() - DaybreakHeader::size());
						else
							Decompress(temp, 1, temp.Length() - 1);
						break;
					case EncodeXOR:
						if (temp.GetInt8(0) == 0)
							Decode(temp, DaybreakHeader::size(), temp.Length() - DaybreakHeader::size());
						else
							Decode(temp, 1, temp.Length() - 1);
						break;
					default:
						break;
				}
			}

			LOG_TRACE(MOD_NET, "DECODED[{}] len={} data={}", m_stats.recv_packets, temp.Length(), full_hex_dump_data((const char*)temp.Data(), temp.Length()));

			m_stats.bytes_after_decode += temp.Length();
			ProcessDecodedPacket(StaticPacket(temp.Data(), temp.Length()));
		}
		else {
			EQ::Net::StaticPacket temp(p.Data(), p.Length() - m_crc_bytes);

			for (int i = 1; i >= 0; --i) {
				switch (m_encode_passes[i]) {
					case EncodeXOR:
						if (temp.GetInt8(0) == 0)
							Decode(temp, DaybreakHeader::size(), temp.Length() - DaybreakHeader::size());
						else
							Decode(temp, 1, temp.Length() - 1);
						break;
					default:
						break;
				}
			}

			LOG_TRACE(MOD_NET, "DECODED_NOCMP[{}] len={} data={}", m_stats.recv_packets, temp.Length(), full_hex_dump_data((const char*)temp.Data(), temp.Length()));
			m_stats.bytes_after_decode += temp.Length();
			ProcessDecodedPacket(StaticPacket(temp.Data(), temp.Length()));
		}
	}
	else {
		LOG_TRACE(MOD_NET, "DECODED_RAW[{}] len={} data={}", m_stats.recv_packets, p.Length(), full_hex_dump(p));
		m_stats.bytes_after_decode += p.Length();
		ProcessDecodedPacket(p);
	}
}

void EQ::Net::DaybreakConnection::ProcessQueue()
{
	for (int i = 0; i < 4; ++i) {
		auto stream = &m_streams[i];
		if (!stream->packet_queue.empty()) {
			LOG_TRACE(MOD_NET, "ProcessQueue: stream={} queue_size={} looking_for_seq={}", i, stream->packet_queue.size(), stream->sequence_in);
		}
		for (;;) {
			auto iter = stream->packet_queue.find(stream->sequence_in);
			if (iter == stream->packet_queue.end()) {
				break;
			}

			auto packet = iter->second;
			stream->packet_queue.erase(iter);

			uint16_t seq_being_processed = stream->sequence_in;

			// Check if this is a fragment packet - fragments need special handling
			// Fragment opcodes are 0x0d-0x10 (OP_Fragment through OP_Fragment4)
			if (packet->Length() >= 2 && packet->GetInt8(0) == 0) {
				uint8_t opcode = packet->GetUInt8(1);
				if (opcode >= OP_Fragment && opcode <= OP_Fragment4) {
					// This is a fragment packet - process it through fragment assembly
					LOG_TRACE(MOD_NET, "ProcessQueue: fragment seq={}, sequence_in: {} -> {}", seq_being_processed, stream->sequence_in, stream->sequence_in + 1);
					stream->sequence_in++;

					if (stream->fragment_total_bytes == 0) {
						// First fragment - has the total size header
						auto fragheader = packet->GetSerialize<DaybreakReliableFragmentHeader>(0);
						stream->fragment_total_bytes = NetworkToHost(fragheader.total_size);
						stream->fragment_current_bytes = 0;

						stream->fragment_packet.Clear();
						stream->fragment_packet.Resize(stream->fragment_total_bytes);

						size_t data_size = packet->Length() - DaybreakReliableFragmentHeader::size();
						if (stream->fragment_current_bytes + data_size > stream->fragment_total_bytes) {
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
							delete packet;
							continue;
						}

						memcpy((char*)stream->fragment_packet.Data() + stream->fragment_current_bytes,
							(char*)packet->Data() + DaybreakReliableFragmentHeader::size(), data_size);

						stream->fragment_current_bytes += (uint32_t)data_size;

						// Check if first fragment contains complete packet
						if (stream->fragment_current_bytes >= stream->fragment_total_bytes) {
							stream->fragment_packet.Resize(stream->fragment_current_bytes);
							uint8_t first_byte = stream->fragment_packet.Length() > 0 ? stream->fragment_packet.GetUInt8(0) : 0;
							LOG_TRACE(MOD_NET, "Fragment complete after first queued packet: {} bytes, first_byte={:#04x}", stream->fragment_current_bytes, first_byte);

							// Check if assembled fragment needs decompression
							if ((first_byte == 0x5a && stream->fragment_packet.Length() > 1 && stream->fragment_packet.GetUInt8(1) == 0x78) || first_byte == 0xa5) {
								LOG_TRACE(MOD_NET, "Assembled fragment has compression marker {:#04x}, decompressing", first_byte);
								Decompress(stream->fragment_packet, 0, stream->fragment_packet.Length());
								LOG_TRACE(MOD_NET, "After decompression: {} bytes, first_byte={:#04x}",
									stream->fragment_packet.Length(),
									stream->fragment_packet.Length() > 0 ? stream->fragment_packet.GetUInt8(0) : 0);
							}

							ProcessDecodedPacket(stream->fragment_packet);
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
						}
					}
					else {
						// Continuation fragment
						size_t data_size = packet->Length() - DaybreakReliableHeader::size();
						if (stream->fragment_current_bytes + data_size > stream->fragment_total_bytes) {
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
							delete packet;
							continue;
						}

						memcpy((char*)stream->fragment_packet.Data() + stream->fragment_current_bytes,
							(char*)packet->Data() + DaybreakReliableHeader::size(), data_size);

						stream->fragment_current_bytes += (uint32_t)data_size;

						// Check if fragment is complete
						if (stream->fragment_current_bytes >= stream->fragment_total_bytes) {
							stream->fragment_packet.Resize(stream->fragment_current_bytes);
							uint8_t first_byte = stream->fragment_packet.Length() > 0 ? stream->fragment_packet.GetUInt8(0) : 0;
							LOG_TRACE(MOD_NET, "Fragment complete after queued continuation: {} bytes, first_byte={:#04x}", stream->fragment_current_bytes, first_byte);

							// Check if assembled fragment needs decompression
							if ((first_byte == 0x5a && stream->fragment_packet.Length() > 1 && stream->fragment_packet.GetUInt8(1) == 0x78) || first_byte == 0xa5) {
								LOG_TRACE(MOD_NET, "Assembled fragment has compression marker {:#04x}, decompressing", first_byte);
								Decompress(stream->fragment_packet, 0, stream->fragment_packet.Length());
								LOG_TRACE(MOD_NET, "After decompression: {} bytes, first_byte={:#04x}",
									stream->fragment_packet.Length(),
									stream->fragment_packet.Length() > 0 ? stream->fragment_packet.GetUInt8(0) : 0);
							}

							ProcessDecodedPacket(stream->fragment_packet);
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
						}
					}

					delete packet;
					continue;
				}
			}

			// Non-fragment packet - process normally (OP_Packet handler will increment sequence_in)
			LOG_TRACE(MOD_NET, "ProcessQueue: non-fragment seq={}, sequence_in={}", seq_being_processed, stream->sequence_in);
			ProcessDecodedPacket(*packet);
			delete packet;
		}
	}
}

void EQ::Net::DaybreakConnection::RemoveFromQueue(int stream, uint16_t seq)
{
	auto s = &m_streams[stream];
	auto iter = s->packet_queue.find(seq);
	if (iter != s->packet_queue.end()) {
		auto packet = iter->second;
		s->packet_queue.erase(iter);
		delete packet;
	}
}

void EQ::Net::DaybreakConnection::AddToQueue(int stream, uint16_t seq, const Packet &p)
{
	auto s = &m_streams[stream];
	auto iter = s->packet_queue.find(seq);
	if (iter == s->packet_queue.end()) {
		DynamicPacket *out = new DynamicPacket();
		out->PutPacket(0, p);

		s->packet_queue.emplace(std::make_pair(seq, out));
		LOG_TRACE(MOD_NET, "AddToQueue: stream={} seq={} ({} bytes), queue_size={}", stream, seq, p.Length(), s->packet_queue.size());
	}
}

void EQ::Net::DaybreakConnection::ProcessDecodedPacket(const Packet &p)
{
	// Helper lambda for FULL hex dump - NO TRUNCATION
	auto full_hex_dump_pkt = [](const Packet& pkt) -> std::string {
		std::string hex_dump;
		for (size_t i = 0; i < pkt.Length(); i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", pkt.GetUInt8(i));
			hex_dump += buf;
		}
		return hex_dump;
	};

	auto full_hex_dump_data = [](const char* data, size_t len) -> std::string {
		std::string hex_dump;
		for (size_t i = 0; i < len; i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", (uint8_t)data[i]);
			hex_dump += buf;
		}
		return hex_dump;
	};

	// Helper lambda for hex dump in ProcessDecodedPacket (limited for compatibility)
	auto hex_dump_pkt = [](const Packet& pkt, size_t max_bytes = 200) -> std::string {
		std::string hex_dump;
		for (size_t i = 0; i < pkt.Length() && i < max_bytes; i++) {
			char buf[4];
			snprintf(buf, sizeof(buf), "%02x ", pkt.GetUInt8(i));
			hex_dump += buf;
		}
		if (pkt.Length() > max_bytes) hex_dump += "...";
		return hex_dump;
	};

	if (p.Length() >= 2) {
		LOG_TRACE(MOD_NET, "ProcessDecodedPacket: first_byte={:#04x}, opcode={:#04x}, length={}",
			p.GetInt8(0), p.GetInt8(1), p.Length());
	}

	if (p.GetInt8(0) == 0) {
		if (p.Length() < 2) {
			LOG_WARN(MOD_NET, "DROPPED: ProcessDecodedPacket packet too short (len={})", p.Length());
			LOG_TRACE(MOD_NET, "DROPPED data: {}", hex_dump_pkt(p));
			return;
		}

		switch (p.GetInt8(1)) {
			case OP_Combined: {
				if (m_status == StatusDisconnecting) {
					SendDisconnect();
					return;
				}

				LOG_TRACE(MOD_NET, "OP_Combined packet ({} bytes), processing sub-packets", p.Length());

				char *current = (char*)p.Data() + 2;
				char *end = (char*)p.Data() + p.Length();
				int subpacket_count = 0;
				while (current < end) {
					uint8_t subpacket_length = *(uint8_t*)current;
					current += 1;

					if (end < current + subpacket_length) {
						LOG_WARN(MOD_NET, "OP_Combined truncated: subpacket {} claims {} bytes but only {} remain",
							subpacket_count, subpacket_length, (int)(end - current));
						LOG_TRACE(MOD_NET, "DROPPED OP_Combined data: {}", hex_dump_pkt(p));
						return;
					}

					subpacket_count++;
					uint16_t app_opcode = 0;
					uint16_t spawn_id = 0;
					if (subpacket_length >= 2) {
						memcpy(&app_opcode, current, sizeof(app_opcode));
						if (subpacket_length >= 4) {
							memcpy(&spawn_id, current + 2, sizeof(spawn_id));
						}
					}
					LOG_TRACE(MOD_NET, "OP_Combined subpacket {}: {} bytes, first_byte={:#04x}, opcode={:#06x}, spawn_id={}",
						subpacket_count, subpacket_length, subpacket_length > 0 ? (uint8_t)current[0] : 0,
						app_opcode, spawn_id);
					ProcessDecodedPacket(StaticPacket(current, subpacket_length));
					current += subpacket_length;
				}
				LOG_TRACE(MOD_NET, "OP_Combined: processed {} sub-packets", subpacket_count);
				break;
			}

			case OP_AppCombined:
			{
				if (m_status == StatusDisconnecting) {
					SendDisconnect();
					return;
				}

				uint8_t *current = (uint8_t*)p.Data() + 2;
				uint8_t *end = (uint8_t*)p.Data() + p.Length();

				while (current < end) {
					uint32_t subpacket_length = 0;
					if (*current == 0xFF)
					{
						if (end < current + 3) {
							throw std::out_of_range("Error in OP_AppCombined, end < current + 3");
						}

						if (*(current + 1) == 0xFF && *(current + 2) == 0xFF) {
							if (end < current + 7) {
								throw std::out_of_range("Error in OP_AppCombined, end < current + 7");
							}

							subpacket_length = (uint32_t)(
								(*(current + 3) << 24) |
								(*(current + 4) << 16) |
								(*(current + 5) << 8) |
								(*(current + 6))
								);
							current += 7;
						}
						else {
							subpacket_length = (uint32_t)(
								(*(current + 1) << 8) |
								(*(current + 2))
								);
							current += 3;
						}
					}
					else {
						subpacket_length = (uint32_t)((*(current + 0)));
						current += 1;
					}

					ProcessDecodedPacket(StaticPacket(current, subpacket_length));
					current += subpacket_length;
				}

				break;
			}

			case OP_SessionRequest:
			{
				if (m_status == StatusConnected) {
					auto request = p.GetSerialize<DaybreakConnect>(0);

					if (NetworkToHost(request.connect_code) != m_connect_code) {
						return;
					}

					DaybreakConnectReply reply;
					reply.zero = 0;
					reply.opcode = OP_SessionResponse;
					reply.connect_code = HostToNetwork(m_connect_code);
					reply.encode_key = HostToNetwork(m_encode_key);
					reply.crc_bytes = m_crc_bytes;
					reply.max_packet_size = HostToNetwork(m_max_packet_size);
					reply.encode_pass1 = m_encode_passes[0];
					reply.encode_pass2 = m_encode_passes[1];
					DynamicPacket p;
					p.PutSerialize(0, reply);
					InternalSend(p);

					LOG_TRACE(MOD_NET, "[OP_SessionRequest] Session [{}] started with encode key [{}]", m_connect_code, HostToNetwork(m_encode_key));
				}

				break;
			}

			case OP_SessionResponse:
			{
				if (m_status == StatusConnecting) {
					auto reply = p.GetSerialize<DaybreakConnectReply>(0);

					// T21: these three fields arrive in network order (the
					// server-role OP_SessionRequest path below wire-encodes
					// them with HostToNetwork, and SendConnect does the same
					// for our own connect_code), but were being compared and
					// stored raw. The compare and the key were masked by the
					// RNG collapse above — 0 and 0xFFFFFFFF are byte-swap
					// palindromes, and the recorded server sends encode_key=0
					// — which is why fixing the RNG without this would have
					// broken every handshake. max_packet_size was genuinely
					// wrong: the capture carries network-order 512, read raw
					// as 131072, so the client believed it could send packets
					// 256x larger than the server allows.
					if (m_connect_code == NetworkToHost(reply.connect_code)) {
						m_encode_key = NetworkToHost(reply.encode_key);
						m_crc_bytes = reply.crc_bytes;
						m_encode_passes[0] = (DaybreakEncodeType)reply.encode_pass1;
						m_encode_passes[1] = (DaybreakEncodeType)reply.encode_pass2;
						m_max_packet_size = NetworkToHost(reply.max_packet_size);
						ChangeStatus(StatusConnected);

						LOG_TRACE(MOD_NET, "[OP_SessionResponse] Session [{}] encode_passes=[{},{}] crc_bytes={} max_packet={}",
							m_connect_code, (int)m_encode_passes[0], (int)m_encode_passes[1], m_crc_bytes, m_max_packet_size);
					}
				}
				break;
			}

			case OP_Packet:
			case OP_Packet2:
			case OP_Packet3:
			case OP_Packet4:
			{
				if (m_status == StatusDisconnecting) {
					SendDisconnect();
					return;
				}

				auto header = p.GetSerialize<DaybreakReliableHeader>(0);
				auto sequence = NetworkToHost(header.sequence);
				auto stream_id = header.opcode - OP_Packet;
				auto stream = &m_streams[stream_id];

				auto order = CompareSequence(stream->sequence_in, sequence);
				if (order == SequenceFuture) {
					SendOutOfOrderAck(stream_id, sequence);
					AddToQueue(stream_id, sequence, p);
					LOG_TRACE(MOD_NET, "OP_Packet seq={} is future (expected={}), queued", sequence, stream->sequence_in);
				}
				else if (order == SequencePast) {
					SendAck(stream_id, stream->sequence_in - 1);
					LOG_TRACE(MOD_NET, "OP_Packet seq={} is PAST (expected={}), skipped", sequence, stream->sequence_in);
				}
				else {
					RemoveFromQueue(stream_id, sequence);
					SendAck(stream_id, stream->sequence_in);
					LOG_TRACE(MOD_NET, "OP_Packet seq={} CURRENT, sequence_in: {} -> {}", sequence, stream->sequence_in, stream->sequence_in + 1);
					stream->sequence_in++;
					StaticPacket next((char*)p.Data() + DaybreakReliableHeader::size(), p.Length() - DaybreakReliableHeader::size());
					ProcessDecodedPacket(next);
				}

				break;
			}

			case OP_Fragment:
			case OP_Fragment2:
			case OP_Fragment3:
			case OP_Fragment4:
			{
				auto header = p.GetSerialize<DaybreakReliableHeader>(0);
				auto sequence = NetworkToHost(header.sequence);
				auto stream_id = header.opcode - OP_Fragment;
				auto stream = &m_streams[stream_id];

				LOG_TRACE(MOD_NET, "FRAG_RECV stream={} seq={} len={} data={}", stream_id, sequence, p.Length(), full_hex_dump_pkt(p));

				auto order = CompareSequence(stream->sequence_in, sequence);

				if (order == SequenceFuture) {
					SendOutOfOrderAck(stream_id, sequence);
					AddToQueue(stream_id, sequence, p);
					LOG_TRACE(MOD_NET, "FRAG_QUEUED stream={} seq={} (expected={}) len={}", stream_id, sequence, stream->sequence_in, p.Length());
					break;
				}
				else if (order == SequencePast) {
					SendAck(stream_id, stream->sequence_in - 1);
					LOG_TRACE(MOD_NET, "FRAG_SKIP_PAST stream={} seq={} (expected={})", stream_id, sequence, stream->sequence_in);
					break;
				}

				// CURRENT - process the fragment
				RemoveFromQueue(stream_id, sequence);
				SendAck(stream_id, stream->sequence_in);
				LOG_TRACE(MOD_NET, "FRAG_CURRENT stream={} seq={} sequence_in: {} -> {}", stream_id, sequence, stream->sequence_in, stream->sequence_in + 1);
				stream->sequence_in++;

				// Fragment reassembly

					if (stream->fragment_total_bytes == 0) {
						auto fragheader = p.GetSerialize<DaybreakReliableFragmentHeader>(0);
						stream->fragment_total_bytes = NetworkToHost(fragheader.total_size);
						stream->fragment_current_bytes = 0;

						LOG_TRACE(MOD_NET, "FRAG_FIRST stream={} seq={} total_size={} packet_len={} data={}", stream_id, sequence, stream->fragment_total_bytes, p.Length(), full_hex_dump_pkt(p));

						stream->fragment_packet.Clear();
						stream->fragment_packet.Resize(stream->fragment_total_bytes);

						size_t data_size = p.Length() - DaybreakReliableFragmentHeader::size();
						if (stream->fragment_current_bytes + data_size > stream->fragment_total_bytes) {
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
							break;
						}

						memcpy((char*)stream->fragment_packet.Data() + stream->fragment_current_bytes,
							(char*)p.Data() + DaybreakReliableFragmentHeader::size(), data_size);

						stream->fragment_current_bytes += (uint32_t)data_size;

						// Check if first fragment contains complete packet
						if (stream->fragment_current_bytes >= stream->fragment_total_bytes) {
							stream->fragment_packet.Resize(stream->fragment_current_bytes);
							uint8_t first_byte = stream->fragment_packet.Length() > 0 ? stream->fragment_packet.GetUInt8(0) : 0;
							LOG_TRACE(MOD_NET, "FRAG_COMPLETE_FIRST stream={} len={} data={}", stream_id, stream->fragment_current_bytes, full_hex_dump_pkt(stream->fragment_packet));

							// Check if assembled fragment needs decompression
							if ((first_byte == 0x5a && stream->fragment_packet.Length() > 1 && stream->fragment_packet.GetUInt8(1) == 0x78) || first_byte == 0xa5) {
								LOG_TRACE(MOD_NET, "FRAG_DECOMPRESS marker={:#04x}", first_byte);
								Decompress(stream->fragment_packet, 0, stream->fragment_packet.Length());
								LOG_TRACE(MOD_NET, "FRAG_DECOMPRESSED len={} data={}", stream->fragment_packet.Length(), full_hex_dump_pkt(stream->fragment_packet));
							}

							ProcessDecodedPacket(stream->fragment_packet);
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
						}
					}
					else {
						size_t data_size = p.Length() - DaybreakReliableHeader::size();
						LOG_TRACE(MOD_NET, "FRAG_CONT stream={} seq={} data_size={} progress={}/{}", stream_id, sequence, data_size, stream->fragment_current_bytes, stream->fragment_total_bytes);

						if (stream->fragment_current_bytes + data_size > stream->fragment_total_bytes) {
							LOG_WARN(MOD_NET, "FRAG_OVERFLOW stream={} current={} + data={} > total={}", stream_id, stream->fragment_current_bytes, data_size, stream->fragment_total_bytes);
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
							break;
						}

						memcpy((char*)stream->fragment_packet.Data() + stream->fragment_current_bytes,
							(char*)p.Data() + DaybreakReliableHeader::size(), data_size);

						stream->fragment_current_bytes += (uint32_t)data_size;

						if (stream->fragment_current_bytes >= stream->fragment_total_bytes) {
							stream->fragment_packet.Resize(stream->fragment_current_bytes);
							uint8_t first_byte = stream->fragment_packet.Length() > 0 ? stream->fragment_packet.GetUInt8(0) : 0;
							LOG_TRACE(MOD_NET, "FRAG_COMPLETE stream={} len={} data={}", stream_id, stream->fragment_current_bytes, full_hex_dump_pkt(stream->fragment_packet));

							// Check if assembled fragment needs decompression
							if ((first_byte == 0x5a && stream->fragment_packet.Length() > 1 && stream->fragment_packet.GetUInt8(1) == 0x78) || first_byte == 0xa5) {
								LOG_TRACE(MOD_NET, "FRAG_DECOMPRESS marker={:#04x}", first_byte);
								Decompress(stream->fragment_packet, 0, stream->fragment_packet.Length());
								LOG_TRACE(MOD_NET, "FRAG_DECOMPRESSED len={} data={}", stream->fragment_packet.Length(), full_hex_dump_pkt(stream->fragment_packet));
							}

							ProcessDecodedPacket(stream->fragment_packet);
							stream->fragment_packet.Clear();
							stream->fragment_total_bytes = 0;
							stream->fragment_current_bytes = 0;
						}
					}

				break;
			}

			case OP_Ack:
			case OP_Ack2:
			case OP_Ack3:
			case OP_Ack4:
			{
				auto header = p.GetSerialize<DaybreakReliableHeader>(0);
				auto sequence = NetworkToHost(header.sequence);
				auto stream_id = header.opcode - OP_Ack;
				Ack(stream_id, sequence);
				break;
			}

			case OP_OutOfOrderAck:
			case OP_OutOfOrderAck2:
			case OP_OutOfOrderAck3:
			case OP_OutOfOrderAck4:
			{
				auto header = p.GetSerialize<DaybreakReliableHeader>(0);
				auto sequence = NetworkToHost(header.sequence);
				auto stream_id = header.opcode - OP_OutOfOrderAck;
				OutOfOrderAck(stream_id, sequence);
				break;
			}

			case OP_SessionDisconnect:
			{
				if (m_status == StatusConnected || m_status == StatusDisconnecting) {
					FlushBuffer();
					SendDisconnect();
				}

				LOG_TRACE(MOD_NET, "[OP_SessionDisconnect] Session [{}] disconnect with encode key [{}]",
					m_connect_code, HostToNetwork(m_encode_key));

				ChangeStatus(StatusDisconnecting);
				break;
			}

			case OP_Padding:
			{
				auto self = m_self.lock();
				if (m_owner->m_on_packet_recv && self) {
					m_owner->m_on_packet_recv(self, StaticPacket((char*)p.Data() + 1, p.Length() - 1));
				}
				break;
			}
			case OP_SessionStatRequest:
			{
				auto request = p.GetSerialize<DaybreakSessionStatRequest>(0);
				m_stats.sync_remote_sent_packets = EQ::Net::NetworkToHost(request.packets_sent);
				m_stats.sync_remote_recv_packets = EQ::Net::NetworkToHost(request.packets_recv);
				m_stats.sync_sent_packets = m_stats.sent_packets;
				m_stats.sync_recv_packets = m_stats.recv_packets;

				DaybreakSessionStatResponse response;
				response.zero = 0;
				response.opcode = OP_SessionStatResponse;
				response.timestamp = request.timestamp;
				response.our_timestamp = EQ::Net::HostToNetwork(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
				response.client_sent = request.packets_sent;
				response.client_recv = request.packets_recv;
				response.server_sent = EQ::Net::HostToNetwork(m_stats.sent_packets);
				response.server_recv = EQ::Net::HostToNetwork(m_stats.recv_packets);
				DynamicPacket out;
				out.PutSerialize(0, response);
				InternalSend(out);
				break;
			}
			case OP_SessionStatResponse: {
				auto response = p.GetSerialize<DaybreakSessionStatResponse>(0);
				m_stats.sync_remote_sent_packets = EQ::Net::NetworkToHost(response.server_sent);
				m_stats.sync_remote_recv_packets = EQ::Net::NetworkToHost(response.server_recv);
				m_stats.sync_sent_packets = m_stats.sent_packets;
				m_stats.sync_recv_packets = m_stats.recv_packets;
				break;
			}
			default:
				if (m_owner->m_on_error_message) {
					m_owner->m_on_error_message(fmt::format("Unhandled opcode {0:#x}", p.GetInt8(1)));
				}
				break;
		}
	}
	else {
		// Application packet - deliver to callback
		auto self = m_self.lock();
		if (m_owner->m_on_packet_recv && self) {
			// Log ALL application packet deliveries for debugging
			if (p.Length() >= 2) {
				uint16_t app_opcode = p.GetUInt16(0);
				uint16_t spawn_id = p.Length() >= 4 ? p.GetUInt16(2) : 0;
				LOG_TRACE(MOD_NET, "Delivering app packet: opcode={:#06x} len={} spawn_id={}", app_opcode, p.Length(), spawn_id);
				// ClientUpdate opcode is 0x14cb
				if (app_opcode == 0x14cb) {
					LOG_TRACE(MOD_NET, "Delivering ClientUpdate: spawn_id={}", spawn_id);
				}
			}
			m_owner->m_on_packet_recv(self, p);
		}
	}
}

