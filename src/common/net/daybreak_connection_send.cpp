// DaybreakConnection -- CRC validation, zlib compression/decompression and the
// outbound path: ProcessResend, InternalSend, InternalQueuePacket, FlushBuffer.
//
// The three file-local tuning constants in the preamble below were moved out of
// the original's shared preamble into this TU because every one of their readers
// lives here; the file-local Inflate/Deflate helpers were already in this range.
// Split from src/common/net/daybreak_connection.cpp by
// refactor/J26_daybreak_connection_split.md (move-only, 2026-09-02).

#include "common/net/daybreak_connection.h"
#include "common/util/data_verification.h"
#include "common/net/crc32.h"
#include "common/logging.h"
#include <cstring>
#include <zlib.h>
#include <fmt/format.h>

// observed client receive window is 300 packets, 140KB
constexpr size_t MAX_CLIENT_RECV_PACKETS_PER_WINDOW = 300;
constexpr size_t MAX_CLIENT_RECV_BYTES_PER_WINDOW   = 140 * 1024;

// Send buffer for InternalSend (replaces libuv pool)
static constexpr size_t SEND_BUFFER_SIZE = 4096;

bool EQ::Net::DaybreakConnection::ValidateCRC(Packet &p)
{
	if (m_crc_bytes == 0U || m_owner->m_options.skip_crc_validation) {
		return true;
	}

	if (p.Length() < (size_t)m_crc_bytes) {
		LOG_TRACE(MOD_NET, "Session [{}] ignored packet (crc bytes invalid on session)", m_connect_code);
		return false;
	}

	char *data = (char*)p.Data();
	int calculated = 0;
	int actual = 0;
	switch (m_crc_bytes) {
		case 2:
			{ int16_t tmp; memcpy(&tmp, &data[p.Length() - (size_t)m_crc_bytes], sizeof(tmp)); actual = NetworkToHost(tmp) & 0xffff; }
			calculated = Crc32(data, (int)(p.Length() - (size_t)m_crc_bytes), m_encode_key) & 0xffff;
			break;
		case 4:
			{ int32_t tmp; memcpy(&tmp, &data[p.Length() - (size_t)m_crc_bytes], sizeof(tmp)); actual = NetworkToHost(tmp); }
			calculated = Crc32(data, (int)(p.Length() - (size_t)m_crc_bytes), m_encode_key);
			break;
		default:
			// Reachable: m_crc_bytes is taken straight off the wire in
			// daybreak_connection_decode.cpp's OP_SessionResponse arm
			// (`m_crc_bytes = reply.crc_bytes;`, a uint8_t the server
			// chooses), so a server negotiating anything but 2 or 4 makes
			// every subsequent packet fail validation. Say so once per
			// packet rather than dropping the session in silence.
			LOG_WARN(MOD_NET, "Session [{}] rejecting packet: unsupported crc_bytes={} (only 2 and 4 are implemented)",
				m_connect_code, m_crc_bytes);
			return false;
	}

	if (actual == calculated) {
		return true;
	}

	return false;
}

void EQ::Net::DaybreakConnection::AppendCRC(Packet &p)
{
	if (m_crc_bytes == 0U) {
		return;
	}

	int calculated = 0;
	switch (m_crc_bytes) {
		case 2:
			calculated = Crc32(p.Data(), (int)p.Length(), m_encode_key) & 0xffff;
			p.PutInt16(p.Length(), EQ::Net::HostToNetwork((int16_t)calculated));
			break;
		case 4:
			calculated = Crc32(p.Data(), (int)p.Length(), m_encode_key);
			p.PutInt32(p.Length(), EQ::Net::HostToNetwork(calculated));
			break;
	}
}

void EQ::Net::DaybreakConnection::ChangeStatus(DbProtocolStatus new_status)
{
	const char* status_names[] = {"Connecting", "Connected", "Disconnecting", "Disconnected"};
	LOG_TRACE(MOD_NET, "ChangeStatus: {}:{} from {} to {}", m_endpoint, m_port, status_names[m_status], status_names[new_status]);

	if (m_owner->m_on_connection_state_change) {
		if (auto self = m_self.lock()) {
			LOG_TRACE(MOD_NET, "Calling status change callback...");
			m_owner->m_on_connection_state_change(self, m_status, new_status);
			LOG_TRACE(MOD_NET, "Status change callback returned");
		} else {
			LOG_WARN(MOD_NET, "Could not lock self weak_ptr");
		}
	} else {
		LOG_TRACE(MOD_NET, "No status change callback registered");
	}

	m_status = new_status;
}

bool EQ::Net::DaybreakConnection::PacketCanBeEncoded(Packet &p) const
{
	if (p.Length() < 2) {
		return false;
	}

	auto zero = p.GetInt8(0);
	if (zero != 0) {
		return true;
	}

	auto opcode = p.GetInt8(1);
	if (opcode == OP_SessionRequest || opcode == OP_SessionResponse || opcode == OP_OutOfSession) {
		return false;
	}

	return true;
}

void EQ::Net::DaybreakConnection::Decode(Packet &p, size_t offset, size_t length)
{
	int key = m_encode_key;
	char *buffer = (char*)p.Data() + offset;

	size_t i = 0;
	for (i = 0; i + 4 <= length; i += 4)
	{
		int pt = (*(int*)&buffer[i]) ^ (key);
		key = (*(int*)&buffer[i]);
		*(int*)&buffer[i] = pt;
	}

	unsigned char KC = key & 0xFF;
	for (; i < length; i++)
	{
		buffer[i] = buffer[i] ^ KC;
	}
}

void EQ::Net::DaybreakConnection::Encode(Packet &p, size_t offset, size_t length)
{
	int key = m_encode_key;
	char *buffer = (char*)p.Data() + offset;

	size_t i = 0;
	for (i = 0; i + 4 <= length; i += 4)
	{
		int pt = (*(int*)&buffer[i]) ^ (key);
		key = pt;
		*(int*)&buffer[i] = pt;
	}

	unsigned char KC = key & 0xFF;
	for (; i < length; i++)
	{
		buffer[i] = buffer[i] ^ KC;
	}
}

static uint32_t Inflate(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_len) {
	if (!in) {
		return 0;
	}

	z_stream zstream;
	memset(&zstream, 0, sizeof(zstream));
	int zerror = 0;

	zstream.next_in = const_cast<unsigned char *>(in);
	zstream.avail_in = in_len;
	zstream.next_out = out;
	zstream.avail_out = out_len;
	zstream.opaque = Z_NULL;

	int i = inflateInit2(&zstream, 15);

	if (i != Z_OK) {
		return 0;
	}

	zerror = inflate(&zstream, Z_FINISH);

	if (zerror == Z_STREAM_END) {
		inflateEnd(&zstream);
		return zstream.total_out;
	}
	else {
		if (zerror == Z_MEM_ERROR && !zstream.msg)
		{
			return 0;
		}

		zerror = inflateEnd(&zstream);
		return 0;
	}
}

static uint32_t Deflate(const uint8_t* in, uint32_t in_len, uint8_t* out, uint32_t out_len) {
	if (!in) {
		return 0;
	}

	z_stream zstream;
	memset(&zstream, 0, sizeof(zstream));
	int zerror;

	zstream.next_in = const_cast<unsigned char *>(in);
	zstream.avail_in = in_len;
	zstream.opaque = Z_NULL;

	deflateInit(&zstream, Z_BEST_SPEED);
	zstream.next_out = out;
	zstream.avail_out = out_len;

	zerror = deflate(&zstream, Z_FINISH);

	if (zerror == Z_STREAM_END)
	{
		deflateEnd(&zstream);
		return zstream.total_out;
	}
	else {
		zerror = deflateEnd(&zstream);
		return 0;
	}
}

void EQ::Net::DaybreakConnection::Decompress(Packet &p, size_t offset, size_t length)
{
	if (length < 2) {
		LOG_TRACE(MOD_NET, "Decompress: skipping, length {} < 2", length);
		return;
	}

	static thread_local uint8_t new_buffer[4096];
	uint8_t *buffer = (uint8_t*)p.Data() + offset;
	uint32_t new_length = 0;

	uint8_t compression_marker = buffer[0];
	LOG_TRACE(MOD_NET, "Decompress: offset={} length={} marker={:#04x}", offset, length, compression_marker);

	if (buffer[0] == 0x5a) {
		// Log the bytes we're about to inflate for debugging
		if (length >= 5) {
			LOG_TRACE(MOD_NET, "Decompress: 0x5a marker, next 4 bytes: {:02x} {:02x} {:02x} {:02x}",
				buffer[1], buffer[2], buffer[3], buffer[4]);
		}
		new_length = Inflate(buffer + 1, (uint32_t)length - 1, new_buffer, 4096);
		if (new_length == 0) {
			LOG_WARN(MOD_NET, "DECOMPRESS_FAIL: zlib inflate returned 0, input_len={}", length - 1);
		} else {
			LOG_TRACE(MOD_NET, "Decompress: zlib inflated {} -> {} bytes", length - 1, new_length);
		}
	}
	else if (buffer[0] == 0xa5) {
		memcpy(new_buffer, buffer + 1, length - 1);
		new_length = (uint32_t)length - 1;
		LOG_TRACE(MOD_NET, "Decompress: uncompressed marker, stripped to {} bytes", new_length);
	}
	else {
		LOG_TRACE(MOD_NET, "Decompress: unknown marker {:#04x}, no action", compression_marker);
		return;
	}

	// Log first few bytes of decompressed data for debugging
	if (new_length >= 4) {
		LOG_TRACE(MOD_NET, "Decompress: result first 4 bytes: {:02x} {:02x} {:02x} {:02x}",
			new_buffer[0], new_buffer[1], new_buffer[2], new_buffer[3]);
	}

	p.Resize(offset);
	p.PutData(offset, new_buffer, new_length);
}

void EQ::Net::DaybreakConnection::Compress(Packet &p, size_t offset, size_t length)
{
	static thread_local uint8_t new_buffer[2048] = { 0 };
	uint8_t *buffer = (uint8_t*)p.Data() + offset;
	uint32_t new_length = 0;
	bool send_uncompressed = true;

	if (length > 30) {
		new_length = Deflate(buffer, (uint32_t)length, new_buffer + 1, 2048) + 1;
		new_buffer[0] = 0x5a;
		send_uncompressed = (new_length > length);
	}
	if (send_uncompressed) {
		memcpy(new_buffer + 1, buffer, length);
		new_buffer[0] = 0xa5;
		new_length = length + 1;
	}

	p.Resize(offset);
	p.PutData(offset, new_buffer, new_length);
}

void EQ::Net::DaybreakConnection::ProcessResend()
{
	for (int i = 0; i < 4; ++i) {
		ProcessResend(i);
	}
}

void EQ::Net::DaybreakConnection::ProcessResend(int stream)
{
	if (m_status == DbProtocolStatus::StatusDisconnected) {
		return;
	}

	if (m_streams[stream].sent_packets.empty()) {
		return;
	}

	m_resend_packets_sent = 0;
	m_resend_bytes_sent = 0;

	auto now = Clock::now();
	auto s = &m_streams[stream];

	if (!s->sent_packets.empty()) {
		auto &first_packet = s->sent_packets.begin()->second;
		auto time_since_first_sent = std::chrono::duration_cast<std::chrono::milliseconds>(now - first_packet.first_sent).count();

		if (time_since_first_sent >= m_owner->m_options.resend_timeout) {
			LOG_TRACE(MOD_NET, "Closing connection for endpoint [{}] port [{}] time_since_first_sent [{}] >= resend_timeout [{}]",
				m_endpoint, m_port, time_since_first_sent, m_owner->m_options.resend_timeout);
			Close();
			return;
		}

		if (m_last_ack - now > std::chrono::milliseconds(1000)) {
			m_acked_since_last_resend = true;
		}

		if (time_since_first_sent <= first_packet.resend_delay && !m_acked_since_last_resend) {
			return;
		}
	}

	for (auto &e: s->sent_packets) {
		if (m_resend_packets_sent >= MAX_CLIENT_RECV_PACKETS_PER_WINDOW ||
			m_resend_bytes_sent >= MAX_CLIENT_RECV_BYTES_PER_WINDOW) {
			break;
		}

		auto &sp = e.second;
		auto &p  = sp.packet;

		LOG_TRACE(MOD_NET, "ProcessResend: Resending packet sequence={} length={}", e.first, p.Length());
		if (p.Length() >= DaybreakHeader::size()) {
			if (p.GetInt8(0) == 0 && p.GetInt8(1) >= OP_Fragment && p.GetInt8(1) <= OP_Fragment4) {
				m_stats.resent_fragments++;
			}
			else {
				m_stats.resent_full++;
			}
		}
		else {
			m_stats.resent_full++;
		}
		m_stats.resent_packets++;

		InternalBufferedSend(p);

		m_resend_packets_sent++;
		m_resend_bytes_sent += p.Length();
		sp.last_sent = now;
		sp.times_resent++;
		sp.resend_delay = EQ::Clamp(
			sp.resend_delay * 2,
			m_owner->m_options.resend_delay_min,
			m_owner->m_options.resend_delay_max
		);
	}

	m_acked_since_last_resend = false;
	m_last_ack = now;
}

void EQ::Net::DaybreakConnection::Ack(int stream, uint16_t seq)
{
	auto now = Clock::now();
	auto s = &m_streams[stream];
	auto iter = s->sent_packets.begin();
	while (iter != s->sent_packets.end()) {
		auto order = CompareSequence(seq, iter->first);

		if (order != SequenceFuture) {
			uint64_t round_time = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - iter->second.last_sent).count();

			m_stats.max_ping = std::max(m_stats.max_ping, round_time);
			m_stats.min_ping = std::min(m_stats.min_ping, round_time);
			m_stats.last_ping = round_time;
			m_rolling_ping = (m_rolling_ping * 2 + round_time) / 3;

			iter = s->sent_packets.erase(iter);
		}
		else {
			++iter;
		}
	}

	m_acked_since_last_resend = true;
	m_last_ack = now;
}

void EQ::Net::DaybreakConnection::OutOfOrderAck(int stream, uint16_t seq)
{
	auto now = Clock::now();
	auto s = &m_streams[stream];
	auto iter = s->sent_packets.find(seq);
	if (iter != s->sent_packets.end()) {
		uint64_t round_time = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - iter->second.last_sent).count();

		m_stats.max_ping = std::max(m_stats.max_ping, round_time);
		m_stats.min_ping = std::min(m_stats.min_ping, round_time);
		m_stats.last_ping = round_time;
		m_rolling_ping = (m_rolling_ping * 2 + round_time) / 3;

		s->sent_packets.erase(iter);
	}

	m_acked_since_last_resend = true;
	m_last_ack = now;
}

void EQ::Net::DaybreakConnection::UpdateDataBudget(double budget_add)
{
	auto outgoing_data_rate = m_owner->m_options.outgoing_data_rate;
	m_outgoing_budget = EQ::ClampUpper(m_outgoing_budget + budget_add, outgoing_data_rate);
}

void EQ::Net::DaybreakConnection::SendAck(int stream_id, uint16_t seq)
{
	DaybreakReliableHeader ack;
	ack.zero = 0;
	ack.opcode = OP_Ack + stream_id;
	ack.sequence = HostToNetwork(seq);

	DynamicPacket p;
	p.PutSerialize(0, ack);

	InternalBufferedSend(p);
}

void EQ::Net::DaybreakConnection::SendOutOfOrderAck(int stream_id, uint16_t seq)
{
	DaybreakReliableHeader ack;
	ack.zero = 0;
	ack.opcode = OP_OutOfOrderAck + stream_id;
	ack.sequence = HostToNetwork(seq);

	DynamicPacket p;
	p.PutSerialize(0, ack);

	InternalBufferedSend(p);
}

void EQ::Net::DaybreakConnection::SendDisconnect()
{
	DaybreakDisconnect disconnect;
	disconnect.zero = 0;
	disconnect.opcode = OP_SessionDisconnect;
	disconnect.connect_code = HostToNetwork(m_connect_code);
	DynamicPacket out;
	out.PutSerialize(0, disconnect);
	InternalSend(out);
}

void EQ::Net::DaybreakConnection::InternalBufferedSend(Packet &p)
{
	LOG_TRACE(MOD_NET, "InternalBufferedSend: Buffering packet length={} buffer_size={}", p.Length(), m_buffered_packets.size());

	if (p.Length() > 0xFFU) {
		if (!m_flushing_buffer) {
			FlushBuffer();
		}
		InternalSend(p);
		return;
	}

	size_t raw_size = DaybreakHeader::size() + (size_t)m_crc_bytes + m_buffered_packets_length + m_buffered_packets.size() + 1 + p.Length();
	if (raw_size > m_max_packet_size) {
		if (!m_flushing_buffer) {
			FlushBuffer();
		}
	}

	DynamicPacket copy;
	copy.PutPacket(0, p);
	m_buffered_packets.push_back(copy);
	m_buffered_packets_length += p.Length();

	if (m_buffered_packets_length + m_buffered_packets.size() > m_owner->m_options.hold_size) {
		if (!m_flushing_buffer) {
			FlushBuffer();
		}
	}
}

void EQ::Net::DaybreakConnection::SendConnect()
{
	LOG_TRACE(MOD_NET, "SendConnect() to {}:{}", m_endpoint, m_port);

	DaybreakConnect connect;
	connect.zero = 0;
	connect.opcode = OP_SessionRequest;
	connect.protocol_version = HostToNetwork(3U);
	connect.connect_code = (uint32_t)HostToNetwork(m_connect_code);
	connect.max_packet_size = HostToNetwork((uint32_t)m_owner->m_options.max_packet_size);

	DynamicPacket p;
	p.PutSerialize(0, connect);

	InternalSend(p);
	LOG_TRACE(MOD_NET, "SendConnect() packet sent");
}

void EQ::Net::DaybreakConnection::SendKeepAlive()
{
	DaybreakHeader keep_alive;
	keep_alive.zero = 0;
	keep_alive.opcode = OP_KeepAlive;

	DynamicPacket p;
	p.PutSerialize(0, keep_alive);

	InternalSend(p);
}

void EQ::Net::DaybreakConnection::InternalSend(Packet &p) {
	LOG_TRACE(MOD_NET, "InternalSend: Called with packet length={} status={}", p.Length(), static_cast<int>(m_status));

	if (m_owner->m_options.outgoing_data_rate > 0.0) {
		auto new_budget = m_outgoing_budget - (p.Length() / 1024.0);
		if (new_budget <= 0.0) {
			m_stats.dropped_datarate_packets++;
			LOG_TRACE(MOD_NET, "InternalSend: Packet dropped due to data rate limit");
			return;
		} else {
			m_outgoing_budget = new_budget;
		}
	}

	m_last_send = Clock::now();

	uint8_t send_buf[SEND_BUFFER_SIZE];
	size_t send_len = 0;

	if (PacketCanBeEncoded(p)) {
		m_stats.bytes_before_encode += p.Length();

		DynamicPacket out;
		out.PutPacket(0, p);

		for (auto &m_encode_passe: m_encode_passes) {
			switch (m_encode_passe) {
				case EncodeCompression:
					if (out.GetInt8(0) == 0) {
						Compress(out, DaybreakHeader::size(), out.Length() - DaybreakHeader::size());
					} else {
						Compress(out, 1, out.Length() - 1);
					}
					break;
				case EncodeXOR:
					if (out.GetInt8(0) == 0) {
						Encode(out, DaybreakHeader::size(), out.Length() - DaybreakHeader::size());
					} else {
						Encode(out, 1, out.Length() - 1);
					}
					break;
				default:
					break;
			}
		}

		AppendCRC(out);

		if (out.Length() > SEND_BUFFER_SIZE) {
			if (m_owner->m_on_error_message) {
				m_owner->m_on_error_message(fmt::format("Packet too large for send buffer: {} > {}", out.Length(), SEND_BUFFER_SIZE));
			}
			return;
		}

		memcpy(send_buf, out.Data(), out.Length());
		send_len = out.Length();
	} else {
		if (p.Length() > SEND_BUFFER_SIZE) {
			if (m_owner->m_on_error_message) {
				m_owner->m_on_error_message(fmt::format("Packet too large for send buffer: {} > {}", p.Length(), SEND_BUFFER_SIZE));
			}
			return;
		}

		memcpy(send_buf, p.Data(), p.Length());
		send_len = p.Length();
	}

	m_stats.sent_bytes += p.Length();
	m_stats.sent_packets++;

	if (m_owner->m_options.simulated_out_packet_loss &&
		m_owner->m_options.simulated_out_packet_loss >= m_owner->m_rand.Int(0, 100)) {
		return;
	}

	if (m_owner->m_transport && m_owner->m_transport->is_open()) {
		int result = m_owner->m_transport->send(send_buf, send_len);
		if (result < 0) {
			LOG_ERROR(MOD_NET, "Transport send failed for {}:{}", m_endpoint, m_port);
		}
	}
}

void EQ::Net::DaybreakConnection::InternalQueuePacket(Packet &p, int stream_id, bool reliable)
{
	if (p.Length() >= 2) {
		uint16_t opcode = p.GetUInt16(0);
		LOG_TRACE(MOD_NET, "InternalQueuePacket: opcode={:#06x} length={} stream={} reliable={}",
			opcode, p.Length(), stream_id, reliable ? "true" : "false");
	}

	if (!reliable) {
		auto max_raw_size = 0xFFU - m_crc_bytes;
		if (p.Length() > max_raw_size) {
			InternalQueuePacket(p, stream_id, true);
			return;
		}

		InternalBufferedSend(p);
		return;
	}

	auto stream = &m_streams[stream_id];
	auto max_raw_size = m_max_packet_size - m_crc_bytes - DaybreakReliableHeader::size() - 1;
	size_t length = p.Length();
	if (length > max_raw_size) {
		DaybreakReliableFragmentHeader first_header;
		first_header.reliable.zero = 0;
		first_header.reliable.opcode = OP_Fragment + stream_id;
		first_header.reliable.sequence = HostToNetwork(stream->sequence_out);
		first_header.total_size = (uint32_t)HostToNetwork((uint32_t)length);

		size_t used = 0;
		size_t sublen = m_max_packet_size - m_crc_bytes - DaybreakReliableFragmentHeader::size() - 1;
		DynamicPacket first_packet;
		first_packet.PutSerialize(0, first_header);
		first_packet.PutData(DaybreakReliableFragmentHeader::size(), (char*)p.Data() + used, sublen);
		used += sublen;

		DaybreakSentPacket sent;
		sent.packet.PutPacket(0, first_packet);
		sent.last_sent = Clock::now();
		sent.first_sent = Clock::now();
		sent.times_resent = 0;
		sent.resend_delay = EQ::Clamp(
			static_cast<size_t>((m_rolling_ping * m_owner->m_options.resend_delay_factor) + m_owner->m_options.resend_delay_ms),
			m_owner->m_options.resend_delay_min,
			m_owner->m_options.resend_delay_max);
		stream->sent_packets.emplace(std::make_pair(stream->sequence_out, sent));
		stream->sequence_out++;

		InternalBufferedSend(first_packet);

		while (used < length) {
			auto left = length - used;
			DynamicPacket packet;
			DaybreakReliableHeader header;
			header.zero = 0;
			header.opcode = OP_Fragment + stream_id;
			header.sequence = HostToNetwork(stream->sequence_out);
			packet.PutSerialize(0, header);

			if (left > max_raw_size) {
				packet.PutData(DaybreakReliableHeader::size(), (char*)p.Data() + used, max_raw_size);
				used += max_raw_size;
			}
			else {
				packet.PutData(DaybreakReliableHeader::size(), (char*)p.Data() + used, left);
				used += left;
			}

			DaybreakSentPacket sent;
			sent.packet.PutPacket(0, packet);
			sent.last_sent = Clock::now();
			sent.first_sent = Clock::now();
			sent.times_resent = 0;
			sent.resend_delay = EQ::Clamp(
				static_cast<size_t>((m_rolling_ping * m_owner->m_options.resend_delay_factor) + m_owner->m_options.resend_delay_ms),
				m_owner->m_options.resend_delay_min,
				m_owner->m_options.resend_delay_max);
			stream->sent_packets.emplace(std::make_pair(stream->sequence_out, sent));
			stream->sequence_out++;

			InternalBufferedSend(packet);
		}
	}
	else {
		DynamicPacket packet;
		DaybreakReliableHeader header;
		header.zero = 0;
		header.opcode = OP_Packet + stream_id;
		header.sequence = HostToNetwork(stream->sequence_out);
		packet.PutSerialize(0, header);
		packet.PutPacket(DaybreakReliableHeader::size(), p);

		DaybreakSentPacket sent;
		sent.packet.PutPacket(0, packet);
		sent.last_sent = Clock::now();
		sent.first_sent = Clock::now();
		sent.times_resent = 0;
		sent.resend_delay = EQ::Clamp(
			static_cast<size_t>((m_rolling_ping * m_owner->m_options.resend_delay_factor) + m_owner->m_options.resend_delay_ms),
			m_owner->m_options.resend_delay_min,
			m_owner->m_options.resend_delay_max);
		stream->sent_packets.emplace(std::make_pair(stream->sequence_out, sent));
		stream->sequence_out++;

		InternalBufferedSend(packet);
	}
}

void EQ::Net::DaybreakConnection::FlushBuffer()
{
	LOG_TRACE(MOD_NET, "FlushBuffer: buffer has {} packets, flushing={}", m_buffered_packets.size(), m_flushing_buffer ? "true" : "false");

	if (m_flushing_buffer) {
		return;
	}

	if (m_buffered_packets.empty()) {
		return;
	}

	m_flushing_buffer = true;

	while (!m_buffered_packets.empty()) {
		if (m_buffered_packets.size() == 1) {
			auto &front = m_buffered_packets.front();
			InternalSend(front);
			m_buffered_packets.pop_front();
		}
		else {
			StaticPacket out(m_combined.get(), 512);
			out.PutUInt8(0, 0);
			out.PutUInt8(1, OP_Combined);
			size_t length = 2;

			auto it = m_buffered_packets.begin();
			int combined_count = 0;
			while (it != m_buffered_packets.end()) {
				if (length + 1 + it->Length() > 512) {
					if (combined_count == 0) {
						InternalSend(*it);
						it = m_buffered_packets.erase(it);
						continue;
					}
					break;
				}

				out.PutUInt8(length, (uint8_t)it->Length());
				out.PutPacket(length + 1, *it);
				length += (1 + it->Length());
				combined_count++;
				it = m_buffered_packets.erase(it);
			}

			if (combined_count > 0) {
				out.Resize(length);
				InternalSend(out);
			}
		}
	}

	m_buffered_packets_length = 0;

	m_flushing_buffer = false;
}

EQ::Net::SequenceOrder EQ::Net::DaybreakConnection::CompareSequence(uint16_t expected, uint16_t actual) const
{
	int diff = (int)actual - (int)expected;

	if (diff == 0) {
		return SequenceCurrent;
	}

	if (diff > 0) {
		if (diff > 10000) {
			return SequencePast;
		}

		return SequenceFuture;
	}

	if (diff < -10000) {
		return SequenceFuture;
	}

	return SequencePast;
}
