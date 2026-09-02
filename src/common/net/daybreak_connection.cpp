// DaybreakConnectionManager -- connection lifecycle, the tick/process loop,
// the manager-side packet path, Connect/NewConnection and the option accessors.
//
// This TU also carries the out-of-class definition of DaybreakConnection's
// static debug-level member. That definition has external linkage, so it must
// appear in this TU alone and must never be copied into a sibling TU; doing so
// is a duplicate-symbol link error.
// Split from src/common/net/daybreak_connection.cpp by
// refactor/J26_daybreak_connection_split.md (move-only, 2026-09-02).

#include "common/net/daybreak_connection.h"
#include "common/util/data_verification.h"
#include "common/net/crc32.h"
#include "common/logging.h"
#include <cstring>
#include <zlib.h>
#include <fmt/format.h>


// Static debug level definition
int EQ::Net::DaybreakConnection::s_debug_level = 0;

EQ::Net::DaybreakConnectionManager::DaybreakConnectionManager(std::unique_ptr<UdpTransport> transport)
	: m_transport(std::move(transport))
{
}

EQ::Net::DaybreakConnectionManager::DaybreakConnectionManager(const DaybreakConnectionManagerOptions &opts, std::unique_ptr<UdpTransport> transport)
	: m_transport(std::move(transport))
{
	m_options = opts;
}

EQ::Net::DaybreakConnectionManager::~DaybreakConnectionManager()
{
	if (m_transport) {
		m_transport->close();
	}
}

void EQ::Net::DaybreakConnectionManager::Tick()
{
	// 1. Drain all pending datagrams from the transport
	if (m_transport && m_transport->is_open()) {
		for (;;) {
			int n = m_transport->recv(m_recv_buf, sizeof(m_recv_buf));
			if (n <= 0) break;

			// With a connected UDP socket we know the peer; use the first connection's endpoint.
			if (!m_connections.empty()) {
				// T306: hold the connection by VALUE (shared_ptr copy), not by
				// reference into the map. ProcessPacket() runs application
				// callbacks that may erase this entry (a disconnect handler,
				// a reconnect), which destroys the mapped shared_ptr and with
				// it the DaybreakConnection whose m_endpoint the `endpoint`
				// parameter is bound to. A copy keeps the object alive for the
				// whole dispatch. Also copy the endpoint string, since the
				// parameter binds by const reference to m_endpoint.
				auto conn = m_connections.begin()->second;
				std::string endpoint = conn->RemoteEndpoint();
				int port = conn->RemotePort();
				ProcessPacket(endpoint, port,
					reinterpret_cast<const char*>(m_recv_buf), static_cast<size_t>(n));
			}
		}
	}

	// 2. Process thread-safe async queue
	ProcessAsyncQueue();

	// 3. Budget, process, resend
	UpdateDataBudget();
	Process();
	ProcessResend();
}

void EQ::Net::DaybreakConnectionManager::Connect(const std::string &addr, int port)
{
	// Open transport if not already connected to this destination
	if (m_transport && !m_transport->is_open()) {
		if (!m_transport->open(addr, port)) {
			LOG_ERROR(MOD_NET, "Failed to open transport to {}:{}", addr, port);
			if (m_on_error_message) {
				m_on_error_message(fmt::format("Failed to open UDP transport to {}:{}", addr, port));
			}
			return;
		}
	}

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

	// T306: iterate a SNAPSHOT of the map, not live iterators. Everything in
	// this loop -- ChangeStatus() (m_on_connection_state_change),
	// connection->Process() (m_on_packet_recv) -- can run application code
	// that inserts into or erases from m_connections. A raw iterator into the
	// map is undefined behaviour the moment a callback erases the node it
	// points at. Each snapshot entry is re-looked-up by key and identity-
	// checked before we touch the map again, so an entry a callback already
	// removed or replaced is skipped rather than acted on twice.
	std::vector<std::pair<std::pair<std::string, int>, std::shared_ptr<DaybreakConnection>>>
		snapshot(m_connections.begin(), m_connections.end());

	for (auto &entry : snapshot) {
		const auto &key       = entry.first;
		auto        connection = entry.second;

		auto iter = m_connections.find(key);
		if (iter == m_connections.end() || iter->second.get() != connection.get()) {
			// Erased or replaced by a callback earlier in this same pass.
			continue;
		}

		auto status = connection->m_status;

		if (status == StatusDisconnecting) {
			auto time_since_close = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_close_time);
			if (time_since_close.count() > m_options.connection_close_time) {
				connection->FlushBuffer();
				connection->SendDisconnect();
				// T306: ERASE BEFORE the status callback. ChangeStatus()
				// dispatches m_on_connection_state_change, and a disconnect
				// handler that reconnects the same endpoint calls Connect(),
				// whose m_connections.emplace() SILENTLY NO-OPS while this
				// key is still present. The old code then erased that key,
				// leaving the manager with no entry at all for the new
				// connection: the reconnect dangles and every inbound packet
				// goes undispatched (FindConnectionByEndpoint returns null).
				// That is the zone-transition reconnect path.
				m_connections.erase(iter);
				connection->ChangeStatus(StatusDisconnected);
				continue;
			}
		}

		if (status == StatusConnecting) {
			auto time_since_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_last_recv);
			if ((size_t)time_since_last_recv.count() > m_options.connect_stale_ms) {
				m_connections.erase(iter);
				connection->ChangeStatus(StatusDisconnecting);
				continue;
			}
		}
		else if (status == StatusConnected) {
			auto time_since_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(now - connection->m_last_recv);
			if ((size_t)time_since_last_recv.count() > m_options.stale_connection_ms) {
				m_connections.erase(iter);
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
			[[fallthrough]];
			case StatusDisconnecting:
				connection->Process();
				break;
			default:
				break;
		}
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

	// T306: snapshot -- see Process(). Held by value so a callback that
	// erases the entry cannot destroy the connection mid-call.
	std::vector<std::shared_ptr<DaybreakConnection>> snapshot;
	snapshot.reserve(m_connections.size());
	for (auto &kv : m_connections) {
		snapshot.push_back(kv.second);
	}

	for (auto &connection : snapshot) {
		connection->UpdateDataBudget(budget_add);
	}
}

void EQ::Net::DaybreakConnectionManager::ProcessResend()
{
	// T306: snapshot -- see Process(). ProcessResend() re-sends buffered
	// packets, which can drive status changes and application callbacks.
	std::vector<std::shared_ptr<DaybreakConnection>> snapshot;
	snapshot.reserve(m_connections.size());
	for (auto &kv : m_connections) {
		snapshot.push_back(kv.second);
	}

	for (auto &connection : snapshot) {
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
				LOG_WARN(MOD_NET, "MGR_PROC[{}] no connection, not SessionRequest, ignoring (connected socket)", mgr_packet_counter);
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

void EQ::Net::DaybreakConnectionManager::QueuePacketAsync(
	std::shared_ptr<DaybreakConnection> conn, Packet& p, int stream, bool reliable)
{
	std::lock_guard<std::mutex> lock(m_async_mutex);
	PendingAsyncPacket pkt;
	pkt.connection = conn;
	pkt.packet.PutPacket(0, p);
	pkt.stream = stream;
	pkt.reliable = reliable;
	m_async_queue.push_back(std::move(pkt));
}

void EQ::Net::DaybreakConnectionManager::ProcessAsyncQueue()
{
	std::vector<PendingAsyncPacket> packets;
	{
		std::lock_guard<std::mutex> lock(m_async_mutex);
		packets.swap(m_async_queue);
	}
	for (auto& pkt : packets) {
		if (auto conn = pkt.connection.lock()) {
			conn->QueuePacket(pkt.packet, pkt.stream, pkt.reliable);
		}
	}
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
	// T21: was Int(uint32 min, uint32 max) — the max truncated to -1 as int,
	// so the draw collapsed to {0, 0xFFFFFFFF}.
	m_encode_key = m_owner->m_rand.UInt32();
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
	// T21: same truncation as the server-role ctor above — every client
	// session identified itself as either 0 or 0xFFFFFFFF. Confirmed against
	// the recorded capture, whose three sessions use exactly those two values.
	m_connect_code = m_owner->m_rand.UInt32();
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

