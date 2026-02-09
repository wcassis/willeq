#include "common/net/daybreak_connection.h"
#include "common/event/event_loop.h"
#include "common/util/data_verification.h"
#include "common/net/crc32.h"
#include "common/logging.h"
#include <cstring>
#include <zlib.h>
#include <fmt/format.h>

// observed client receive window is 300 packets, 140KB
constexpr size_t MAX_CLIENT_RECV_PACKETS_PER_WINDOW = 300;
constexpr size_t MAX_CLIENT_RECV_BYTES_PER_WINDOW   = 140 * 1024;

// buffer pools
SendBufferPool send_buffer_pool;

// Static debug level definition
int EQ::Net::DaybreakConnection::s_debug_level = 0;

EQ::Net::DaybreakConnectionManager::DaybreakConnectionManager()
{
	m_attached = nullptr;
	memset(&m_timer, 0, sizeof(uv_timer_t));
	memset(&m_socket, 0, sizeof(uv_udp_t));

	Attach(EQ::EventLoop::Get().Handle());
}

EQ::Net::DaybreakConnectionManager::DaybreakConnectionManager(const DaybreakConnectionManagerOptions &opts)
{
	m_attached = nullptr;
	m_options = opts;
	memset(&m_timer, 0, sizeof(uv_timer_t));
	memset(&m_socket, 0, sizeof(uv_udp_t));

	Attach(EQ::EventLoop::Get().Handle());
}

EQ::Net::DaybreakConnectionManager::~DaybreakConnectionManager()
{
	Detach();
}

void EQ::Net::DaybreakConnectionManager::Attach(uv_loop_t *loop)
{
	if (!m_attached) {
		static int manager_counter = 0;
		int manager_id = manager_counter++;
		LOG_TRACE(MOD_NET, "Attach() called, manager_id={} manager_ptr={} loop={} loop_alive={}", manager_id, (void*)this, (void*)loop, uv_loop_alive(loop));

		int timer_init_result = uv_timer_init(loop, &m_timer);
		LOG_TRACE(MOD_NET, "Attach() timer_init={}", timer_init_result);

		m_timer.data = this;

		auto update_rate = (uint64_t)(1000.0 / m_options.tic_rate_hertz);

		uv_timer_start(&m_timer, [](uv_timer_t *handle) {
			static int timer_tick_counter = 0;
			timer_tick_counter++;

			DaybreakConnectionManager *c = (DaybreakConnectionManager*)handle->data;
			if (!c) {
				LOG_ERROR(MOD_NET, "Timer callback: manager is null!");
				return;
			}

			// Log periodically
			if (timer_tick_counter % 60 == 0) {
				LOG_TRACE(MOD_NET, "Timer tick {} connections={}", timer_tick_counter, c->m_connections.size());
			}

			c->UpdateDataBudget();
			c->Process();
			c->ProcessResend();
		}, update_rate, update_rate);

		int udp_init_result = uv_udp_init(loop, &m_socket);
		LOG_TRACE(MOD_NET, "Attach() udp_init={}", udp_init_result);

		m_socket.data = this;
		struct sockaddr_in recv_addr;
		uv_ip4_addr("0.0.0.0", m_options.port, &recv_addr);
		int rc = uv_udp_bind(&m_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
		LOG_TRACE(MOD_NET, "Attach() udp_bind={} port={}", rc, m_options.port);

		// Increase socket receive buffer to handle packet bursts (1MB)
		// Use SO_RCVBUFFORCE to bypass kernel limit (requires CAP_NET_ADMIN)
		uv_os_fd_t fd;
		uv_fileno((uv_handle_t*)&m_socket, &fd);
		int rcvbuf_size = 512 * 1024;
		if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
			// Fall back to regular SO_RCVBUF with kernel max
			rcvbuf_size = 212992;
			if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
				LOG_WARN(MOD_NET, "Attach() failed to set SO_RCVBUF: {}", strerror(errno));
			}
		}
		socklen_t optlen = sizeof(rcvbuf_size);
		getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, &optlen);
		LOG_TRACE(MOD_NET, "Attach() socket_fd={} rcvbuf_size={}", fd, rcvbuf_size);

		rc = uv_udp_recv_start(
			&m_socket,
			[](uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
				static uint64_t alloc_counter = 0;
				alloc_counter++;
				if (alloc_counter % 100 == 1) {
					LOG_TRACE(MOD_NET, "ALLOC_CB[{}] handle={} suggested_size={}", alloc_counter, (void*)handle, suggested_size);
				}

				if (suggested_size > 65536) {
					buf->base = new char[suggested_size];
					buf->len  = suggested_size;
					return;
				}

				static thread_local char temp_buf[65536];
				buf->base = temp_buf;
				buf->len  = 65536;
			},
			[](uv_udp_t* handle, ssize_t nread, const uv_buf_t* buf, const struct sockaddr* addr, unsigned flags) {
			DaybreakConnectionManager *c = (DaybreakConnectionManager*)handle->data;
			if (nread < 0 || addr == nullptr) {
				return;
			}

			char endpoint[16];
			uv_ip4_name((const sockaddr_in*)addr, endpoint, 16);
			auto port = ntohs(((const sockaddr_in*)addr)->sin_port);

			c->ProcessPacket(endpoint, port, buf->base, nread);

			if (buf->len > 65536) {
				delete[] buf->base;
			}
		});

		LOG_TRACE(MOD_NET, "Attach() udp_recv_start={}", rc);
		LOG_INFO(MOD_NET, "Attach() complete, loop_alive={}", uv_loop_alive(loop));

		m_attached = loop;
	}
}

void EQ::Net::DaybreakConnectionManager::Detach()
{
	if (m_attached) {
		LOG_TRACE(MOD_NET, "Detach() called, closing handles properly...");

		uv_loop_t* loop = m_attached;
		int pending_closes = 0;

		// Close callback that decrements the pending count
		auto close_cb = [](uv_handle_t* handle) {
			int* pending = static_cast<int*>(handle->data);
			if (pending) {
				(*pending)--;
			}
		};

		// Stop timer first, then close it
		uv_timer_stop(&m_timer);
		if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&m_timer))) {
			m_timer.data = &pending_closes;
			pending_closes++;
			uv_close(reinterpret_cast<uv_handle_t*>(&m_timer), close_cb);
		}

		// Stop UDP recv first, then close it
		uv_udp_recv_stop(&m_socket);
		if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(&m_socket))) {
			m_socket.data = &pending_closes;
			pending_closes++;
			uv_close(reinterpret_cast<uv_handle_t*>(&m_socket), close_cb);
		}

		// Run the event loop until all closes complete
		// This ensures handles are fully closed before we return
		while (pending_closes > 0 && uv_loop_alive(loop)) {
			uv_run(loop, UV_RUN_ONCE);
		}

		LOG_TRACE(MOD_NET, "Detach() handles closed properly");

		m_attached = nullptr;
	}
}

void EQ::Net::DaybreakConnectionManager::Connect(const std::string &addr, int port)
{
	auto connection = std::shared_ptr<DaybreakConnection>(new DaybreakConnection(this, addr, port));
	connection->m_self = connection;

	if (m_on_new_connection) {
		m_on_new_connection(connection);
	}

	m_connections.emplace(std::make_pair(std::make_pair(addr, port), connection));
}

void EQ::Net::DaybreakConnectionManager::Process()
{
	auto now = Clock::now();
	auto iter = m_connections.begin();

	while (iter != m_connections.end()) {
		auto connection = iter->second;
		auto status = connection->m_status;

		if (status == StatusDisconnecting) {
			auto time_since_close = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_close_time);
			if (time_since_close.count() > m_options.connection_close_time) {
				connection->FlushBuffer();
				connection->SendDisconnect();
				connection->ChangeStatus(StatusDisconnected);
				iter = m_connections.erase(iter);
				continue;
			}
		}

		if (status == StatusConnecting) {
			auto time_since_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_last_recv);
			if ((size_t)time_since_last_recv.count() > m_options.connect_stale_ms) {
				iter = m_connections.erase(iter);
				connection->ChangeStatus(StatusDisconnecting);
				continue;
			}
		}
		else if (status == StatusConnected) {
			auto time_since_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_last_recv);
			if ((size_t)time_since_last_recv.count() > m_options.stale_connection_ms) {
				iter = m_connections.erase(iter);
				connection->ChangeStatus(StatusDisconnecting);
				continue;
			}
		}

		switch (status)
		{
			case StatusConnecting: {
				auto time_since_last_send = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_last_send);
				if ((size_t)time_since_last_send.count() > m_options.connect_delay_ms) {
					connection->SendConnect();
				}
				break;
			}
			case StatusConnected: {
				if (m_options.keepalive_delay_ms != 0) {
					auto time_since_last_send = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_last_send);
					if ((size_t)time_since_last_send.count() > m_options.keepalive_delay_ms) {
						connection->SendKeepAlive();
					}
				}
			}
			case StatusDisconnecting:
				connection->Process();
				break;
			default:
				break;
		}

		iter++;
	}
}

void EQ::Net::DaybreakConnectionManager::UpdateDataBudget()
{
	auto outgoing_data_rate = m_options.outgoing_data_rate;
	if (outgoing_data_rate <= 0.0) {
		return;
	}

	auto update_rate = (uint64_t)(1000.0 / m_options.tic_rate_hertz);
	auto budget_add = update_rate * outgoing_data_rate / 1000.0;

	auto iter = m_connections.begin();
	while (iter != m_connections.end()) {
		auto &connection = iter->second;
		connection->UpdateDataBudget(budget_add);

		iter++;
	}
}

void EQ::Net::DaybreakConnectionManager::ProcessResend()
{
	auto iter = m_connections.begin();
	while (iter != m_connections.end()) {
		auto &connection = iter->second;
		auto status = connection->m_status;

		switch (status)
		{
			case StatusConnected:
			case StatusDisconnecting:
				connection->ProcessResend();
				break;
			default:
				break;
		}

		iter++;
	}
}

void EQ::Net::DaybreakConnectionManager::ProcessPacket(const std::string &endpoint, int port, const char *data, size_t size)
{
	static uint64_t mgr_packet_counter = 0;
	mgr_packet_counter++;

	// Log ALL bytes at manager entry
	std::string hex;
	for (size_t i = 0; i < size; i++) {
		char tmp[4];
		snprintf(tmp, sizeof(tmp), "%02x ", (uint8_t)data[i]);
		hex += tmp;
	}
	LOG_TRACE(MOD_NET, "MGR_PROC[{}] from={}:{} len={} data={}", mgr_packet_counter, endpoint, port, size, hex);

	if (m_options.simulated_in_packet_loss && m_options.simulated_in_packet_loss >= m_rand.Int(0, 100)) {
		LOG_WARN(MOD_NET, "MGR_PROC[{}] DROPPED by simulated_in_packet_loss", mgr_packet_counter);
		return;
	}

	if (size < DaybreakHeader::size()) {
		LOG_WARN(MOD_NET, "MGR_PROC[{}] DROPPED size {} < DaybreakHeader::size {}", mgr_packet_counter, size, DaybreakHeader::size());
		if (m_on_error_message) {
			m_on_error_message(fmt::format("Packet of size {0} which is less than {1}", size, DaybreakHeader::size()));
		}
		return;
	}

	try {
		auto connection = FindConnectionByEndpoint(endpoint, port);
		LOG_TRACE(MOD_NET, "MGR_PROC[{}] connection={}", mgr_packet_counter, connection ? "found" : "null");
		if (connection) {
			LOG_TRACE(MOD_NET, "MGR_PROC[{}] calling connection->ProcessPacket", mgr_packet_counter);
			StaticPacket p((void*)data, size);
			connection->ProcessPacket(p);
			LOG_TRACE(MOD_NET, "MGR_PROC[{}] connection->ProcessPacket returned", mgr_packet_counter);
		}
		else {
			LOG_TRACE(MOD_NET, "MGR_PROC[{}] no connection, checking opcode data[0]={:#04x} data[1]={:#04x}", mgr_packet_counter, (uint8_t)data[0], (uint8_t)data[1]);
			if (data[0] == 0 && data[1] == OP_SessionRequest) {
				LOG_TRACE(MOD_NET, "MGR_PROC[{}] OP_SessionRequest, creating new connection", mgr_packet_counter);
				StaticPacket p((void*)data, size);
				auto request = p.GetSerialize<DaybreakConnect>(0);

				connection = std::shared_ptr<DaybreakConnection>(new DaybreakConnection(this, request, endpoint, port));
				connection->m_self = connection;

				if (m_on_new_connection) {
					m_on_new_connection(connection);
				}
				m_connections.emplace(std::make_pair(std::make_pair(endpoint, port), connection));
				connection->ProcessPacket(p);
			}
			else if (data[1] != OP_OutOfSession) {
				LOG_WARN(MOD_NET, "MGR_PROC[{}] no connection, not SessionRequest, sending disconnect", mgr_packet_counter);
				SendDisconnect(endpoint, port);
			}
			else {
				LOG_TRACE(MOD_NET, "MGR_PROC[{}] OP_OutOfSession, ignoring", mgr_packet_counter);
			}
		}
	}
	catch (std::exception &ex) {
		LOG_ERROR(MOD_NET, "MGR_PROC[{}] EXCEPTION: {}", mgr_packet_counter, ex.what());
		if (m_on_error_message) {
			m_on_error_message(fmt::format("Error processing packet: {0}", ex.what()));
		}
	}
}

std::shared_ptr<EQ::Net::DaybreakConnection> EQ::Net::DaybreakConnectionManager::FindConnectionByEndpoint(std::string addr, int port)
{
	auto p = std::make_pair(addr, port);
	auto iter = m_connections.find(p);
	if (iter != m_connections.end()) {
		return iter->second;
	}

	return nullptr;
}

void EQ::Net::DaybreakConnectionManager::SendDisconnect(const std::string &addr, int port)
{
	DaybreakDisconnect header;
	header.zero = 0;
	header.opcode = OP_OutOfSession;
	header.connect_code = 0;

	DynamicPacket out;
	out.PutSerialize(0, header);

	uv_udp_send_t *send_req = new uv_udp_send_t;
	sockaddr_in send_addr;
	uv_ip4_addr(addr.c_str(), port, &send_addr);
	uv_buf_t send_buffers[1];

	char *data = new char[out.Length()];
	memcpy(data, out.Data(), out.Length());
	send_buffers[0] = uv_buf_init(data, out.Length());
	send_req->data = send_buffers[0].base;
	int ret = uv_udp_send(send_req, &m_socket, send_buffers, 1, (sockaddr*)&send_addr,
		[](uv_udp_send_t* req, int status) {
		delete[](char*)req->data;
		delete req;
	});
}

//new connection made as server
EQ::Net::DaybreakConnection::DaybreakConnection(DaybreakConnectionManager *owner, const DaybreakConnect &connect, const std::string &endpoint, int port)
{
	m_owner = owner;
	m_last_send = Clock::now();
	m_last_recv = Clock::now();
	m_status = StatusConnected;
	m_endpoint = endpoint;
	m_port = port;
	m_connect_code = NetworkToHost(connect.connect_code);
	m_encode_key = m_owner->m_rand.Int(std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max());
	m_max_packet_size = (uint32_t)std::min(owner->m_options.max_packet_size, (size_t)NetworkToHost(connect.max_packet_size));
	m_crc_bytes = (uint32_t)owner->m_options.crc_length;
	m_encode_passes[0] = owner->m_options.encode_passes[0];
	m_encode_passes[1] = owner->m_options.encode_passes[1];
	m_hold_time = Clock::now();
	m_buffered_packets_length = 0;
	m_rolling_ping = 500;
	m_combined.reset(new char[512]);
	m_combined[0] = 0;
	m_combined[1] = OP_Combined;
	m_last_session_stats = Clock::now();
	m_outgoing_budget = owner->m_options.outgoing_data_rate;
	m_flushing_buffer = false;

	LOG_TRACE(MOD_NET, "New session [{}] with encode key [{}]", m_connect_code, HostToNetwork(m_encode_key));
}

//new connection made as client
EQ::Net::DaybreakConnection::DaybreakConnection(DaybreakConnectionManager *owner, const std::string &endpoint, int port)
{
	LOG_TRACE(MOD_NET, "DaybreakConnection created as client to {}:{}", endpoint, port);

	m_owner = owner;
	m_last_send = Clock::now();
	m_last_recv = Clock::now();
	m_status = StatusConnecting;
	m_endpoint = endpoint;
	m_port = port;
	m_connect_code = m_owner->m_rand.Int(std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::max());
	m_encode_key = 0;
	m_max_packet_size = (uint32_t)owner->m_options.max_packet_size;
	m_crc_bytes = 0;
	m_hold_time = Clock::now();
	m_buffered_packets_length = 0;
	m_rolling_ping = 500;
	m_combined.reset(new char[512]);
	m_combined[0] = 0;
	m_combined[1] = OP_Combined;
	m_last_session_stats = Clock::now();
	m_outgoing_budget = owner->m_options.outgoing_data_rate;
	m_flushing_buffer = false;

	LOG_TRACE(MOD_NET, "DaybreakConnection constructor complete, status=StatusConnecting");
}

EQ::Net::DaybreakConnection::~DaybreakConnection()
{
}

void EQ::Net::DaybreakConnection::Close()
{
	if (m_status != StatusDisconnected && m_status != StatusDisconnecting) {
		FlushBuffer();
		SendDisconnect();
	}

	if (m_status != StatusDisconnecting) {
		m_close_time = Clock::now();
	}

	ChangeStatus(StatusDisconnecting);
}

void EQ::Net::DaybreakConnection::QueuePacket(Packet &p)
{
	QueuePacket(p, 0, true);
}

void EQ::Net::DaybreakConnection::QueuePacket(Packet &p, int stream)
{
	QueuePacket(p, stream, true);
}

void EQ::Net::DaybreakConnection::QueuePacket(Packet &p, int stream, bool reliable)
{
	if (*(char*)p.Data() == 0) {
		DynamicPacket packet;
		packet.PutUInt8(0, 0);
		packet.PutPacket(1, p);
		InternalQueuePacket(packet, stream, reliable);
		return;
	}

	InternalQueuePacket(p, stream, reliable);
}

EQ::Net::DaybreakConnectionStats EQ::Net::DaybreakConnection::GetStats()
{
	EQ::Net::DaybreakConnectionStats ret = m_stats;
	ret.datarate_remaining = m_outgoing_budget;
	ret.avg_ping = m_rolling_ping;

	return ret;
}

void EQ::Net::DaybreakConnection::ResetStats()
{
	m_stats.Reset();
}

void EQ::Net::DaybreakConnection::Process()
{
	try {
		auto now = Clock::now();
		auto time_since_hold = (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(now - m_hold_time).count();

		if (time_since_hold >= m_owner->m_options.hold_length_ms) {
			if (!m_buffered_packets.empty()) {
				LOG_TRACE(MOD_NET, "Hold time expired ({} ms >= {} ms), flushing {} packets",
					time_since_hold, m_owner->m_options.hold_length_ms, m_buffered_packets.size());
			}
			FlushBuffer();
			m_hold_time = Clock::now();
		}

		ProcessQueue();
	}
	catch (std::exception &ex) {
		if (m_owner->m_on_error_message) {
			m_owner->m_on_error_message(fmt::format("Error processing connection: {0}", ex.what()));
		}
	}
}

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

					if (m_connect_code == reply.connect_code) {
						m_encode_key = reply.encode_key;
						m_crc_bytes = reply.crc_bytes;
						m_encode_passes[0] = (DaybreakEncodeType)reply.encode_pass1;
						m_encode_passes[1] = (DaybreakEncodeType)reply.encode_pass2;
						m_max_packet_size = reply.max_packet_size;
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

	auto pooled_opt = send_buffer_pool.acquire();
	if (!pooled_opt) {
		m_stats.dropped_datarate_packets++;
		if (m_owner->m_on_error_message) {
			m_owner->m_on_error_message("Failed to acquire send buffer from pool - pool exhausted");
		}
		return;
	}

	auto [send_req, data, ctx] = *pooled_opt;
	ctx->pool = &send_buffer_pool;

	sockaddr_in send_addr{};
	uv_ip4_addr(m_endpoint.c_str(), m_port, &send_addr);
	uv_buf_t send_buffers[1];

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

		if (out.Length() > UDP_BUFFER_SIZE) {
			if (m_owner->m_on_error_message) {
				m_owner->m_on_error_message(fmt::format("Packet too large for send buffer: {} > {}", out.Length(), UDP_BUFFER_SIZE));
			}
			send_buffer_pool.release(ctx);
			return;
		}

		memcpy(data, out.Data(), out.Length());
		send_buffers[0] = uv_buf_init(data, out.Length());
	} else {
		if (p.Length() > UDP_BUFFER_SIZE) {
			if (m_owner->m_on_error_message) {
				m_owner->m_on_error_message(fmt::format("Packet too large for send buffer: {} > {}", p.Length(), UDP_BUFFER_SIZE));
			}
			send_buffer_pool.release(ctx);
			return;
		}

		memcpy(data, p.Data(), p.Length());
		send_buffers[0] = uv_buf_init(data, p.Length());
	}

	m_stats.sent_bytes += p.Length();
	m_stats.sent_packets++;

	if (m_owner->m_options.simulated_out_packet_loss &&
		m_owner->m_options.simulated_out_packet_loss >= m_owner->m_rand.Int(0, 100)) {
		send_buffer_pool.release(ctx);
		return;
	}

	int send_result = uv_udp_send(
		send_req, &m_owner->m_socket, send_buffers, 1, (sockaddr *)&send_addr,
		[](uv_udp_send_t *req, int status) {
			auto *ctx = reinterpret_cast<EmbeddedContext *>(req->data);
			if (!ctx) {
				LOG_ERROR(MOD_NET, "send_req->data is null in callback!");
				return;
			}

			if (status < 0) {
				LOG_ERROR(MOD_NET, "uv_udp_send failed: {}", uv_strerror(status));
			}

			ctx->pool->release(ctx);
		}
	);

	if (send_result < 0) {
		LOG_ERROR(MOD_NET, "uv_udp_send() failed: {}", uv_strerror(send_result));
		send_buffer_pool.release(ctx);
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
