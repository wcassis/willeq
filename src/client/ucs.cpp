#include "client/ucs.h"
#include "common/net/packet.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

UCSConnection::UCSConnection(const std::string& character_name, uint32_t dbid, const std::string& key)
	: m_character_name(character_name), m_dbid(dbid), m_key(key), m_state(UCSConnectionState::Disconnected), m_port(0)
{
	// Open log file for this UCS connection
	std::string log_filename = "hc_ucs_" + m_character_name + ".log";
	m_log_file.open(log_filename, std::ios::app);

	LogToFile("Initializing UCS connection for character: " + m_character_name);
}

UCSConnection::~UCSConnection()
{
	LogToFile("UCS connection destructor called");

	if (m_connection) {
		m_connection->Close();
	}

	if (m_log_file.is_open()) {
		m_log_file.close();
	}
}

void UCSConnection::LogToFile(const std::string& message)
{
	if (m_log_file.is_open()) {
		auto now = std::chrono::system_clock::now();
		auto time_t = std::chrono::system_clock::to_time_t(now);
		auto tm = *std::localtime(&time_t);

		m_log_file << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] " << message << std::endl;
		m_log_file.flush();
	}
}

void UCSConnection::ConnectToUCS(const std::string& host, int port, const std::string& client_ip)
{
	m_host = host;
	m_port = port;
	m_client_ip = client_ip;

	LogToFile("Connecting to UCS server at " + host + ":" + std::to_string(port) + " from client IP: " + client_ip);

	// Create connection manager
	m_connection_manager.reset(new EQ::Net::DaybreakConnectionManager());

	m_connection_manager->OnNewConnection(std::bind(&UCSConnection::OnNewConnection, this, std::placeholders::_1));
	m_connection_manager->OnConnectionStateChange(std::bind(&UCSConnection::OnStatusChangeActive, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
	m_connection_manager->OnPacketRecv(std::bind(&UCSConnection::OnPacketRecv, this, std::placeholders::_1, std::placeholders::_2));

	SetState(UCSConnectionState::Connecting);
	m_connection_manager->Connect(host, port);
}

void UCSConnection::Disconnect()
{
	LogToFile("Disconnecting from UCS server");

	if (m_connection) {
		m_connection->Close();
		m_connection.reset();
	}

	SetState(UCSConnectionState::Disconnected);
}

bool UCSConnection::IsConnected() const
{
	return m_connection && m_connection->GetStatus() == EQ::Net::StatusConnected;
}

void UCSConnection::OnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection)
{
	m_connection = connection;
	LogToFile("New UCS connection established");
}

void UCSConnection::OnStatusChangeActive(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusConnected) {
		LogToFile("UCS server connected - sending authentication");
		SetState(UCSConnectionState::Connected);
		SendUCSAuth();
	}

	if (to == EQ::Net::StatusDisconnected) {
		LogToFile("UCS connection lost");
		m_connection.reset();
		SetState(UCSConnectionState::Disconnected);
	}
}

void UCSConnection::OnStatusChangeInactive(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to)
{
	if (to == EQ::Net::StatusDisconnected) {
		LogToFile("UCS connection inactive and disconnected");
		m_connection.reset();
		SetState(UCSConnectionState::Disconnected);
	}
}

void UCSConnection::OnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p)
{
	auto opcode = p.GetUInt16(0);
	LogToFile("UCS packet received, opcode: 0x" + std::to_string(opcode) + " (decimal: " + std::to_string(opcode) + ")");
	LogToFile("Packet data:\n" + p.ToString());

	// Handle UCS opcodes
	switch (opcode) {
		case 0x0000: // OP_SessionReady
			LogToFile("Received OP_SessionReady from UCS server");
			// Send session ready response back to complete handshake
			SendSessionReady();
			break;

		// UCS authentication response
		case 0x0001: // OP_MailLogin response
			LogToFile("Received UCS authentication response");
			ProcessUCSAuthResponse(p);
			break;

		case 0x0101: // UCS server response (observed in logs) - 0x0101 = 257 decimal
			LogToFile("Received UCS server response (0x0101)");
			ProcessUCSServerResponse(p);
			break;

		case 0x0002: // OP_Mail
			ProcessMailMessage(p);
			break;

		case 0x0003: // OP_ChannelMessage
			ProcessChannelMessage(p);
			break;

		case 0x0006: // OP_Buddy
			ProcessBuddyMessage(p);
			break;

		default:
			LogToFile("Unhandled UCS opcode: 0x" + std::to_string(opcode));
			break;
	}
}

void UCSConnection::SendUCSAuth()
{
	LogToFile("Sending UCS authentication");

	EQ::Net::DynamicPacket p;

	// UCS OP_MailLogin packet format:
	// Byte 0: 0x01 (header)
	// Bytes 1+: "SOE.EQ.{ShortName}.{CharacterName}\0"
	// Next byte: Connection type ('B' for Titanium chat)
	// Next bytes: 16-character hex mail key (IP + key) + null terminator

	std::string mailbox_name = "SOE.EQ.Hatville." + m_character_name;
	char connection_type = 'B'; // ucsTitaniumChat

	// The UCS server expects a hex string key with the client's IP embedded
	// Format: 8 hex chars for IP + 8 hex chars for key
	std::string full_key;

	// Get IP address and convert to hex
	uint32_t ip_binary = 0;
	if (!m_client_ip.empty()) {
		// Convert string IP to binary (network byte order)
		ip_binary = inet_addr(m_client_ip.c_str());
		LogToFile("Using client IP: " + m_client_ip);
	} else {
		LogToFile("Warning: No client IP provided, using localhost");
		ip_binary = inet_addr("127.0.0.1");
	}

	// Convert IP to hex string (8 characters)
	char ip_hex[9];
	snprintf(ip_hex, sizeof(ip_hex), "%08X", ntohl(ip_binary));

	// Create the full key: IP hex + mail key hex
	if (m_key.length() >= 8) {
		full_key = std::string(ip_hex) + m_key.substr(0, 8);
		LogToFile("Full UCS key: " + full_key);
	} else {
		LogToFile("Warning: Key too short: " + m_key);
		full_key = std::string(ip_hex) + "00000000";
	}

	// Calculate packet size: opcode(2) + mailbox_name + null + connection_type(1) + key_string + null
	size_t packet_size = 2 + mailbox_name.length() + 1 + 1 + 8 + 1;
	p.Resize(packet_size);

	// Build packet
	p.PutUInt16(0, 0x01); // OP_MailLogin opcode
	p.PutCString(2, mailbox_name.c_str()); // Mailbox name
	size_t offset = 2 + mailbox_name.length() + 1;
	p.PutUInt8(offset, connection_type); // Connection type
	offset++;

	// Put just the 8-character mail key (not the full 16-char key)
	// The server already knows our IP from the connection
	p.PutString(offset, m_key.substr(0, 8));
	p.PutUInt8(offset + 8, 0); // Null terminator

	m_connection->QueuePacket(p);

	LogToFile("UCS authentication packet sent - mailbox: " + mailbox_name + ", key: " + m_key.substr(0, 8) + " (expecting IP: " + m_client_ip + ")");
	SetState(UCSConnectionState::Authenticating);
}

void UCSConnection::ProcessUCSAuthResponse(const EQ::Net::Packet &p)
{
	LogToFile("Processing UCS authentication response");

	if (p.Length() > 2) {
		// Check authentication result
		uint8_t auth_result = p.GetUInt8(2);
		if (auth_result == 1) {
			LogToFile("UCS authentication successful");
			SetState(UCSConnectionState::Authenticated);
		} else {
			LogToFile("UCS authentication failed with result: " + std::to_string(auth_result));
			SetState(UCSConnectionState::Error);
		}
	}
}

void UCSConnection::ProcessUCSServerResponse(const EQ::Net::Packet &p)
{
	LogToFile("Processing UCS server response (0x257)");

	if (p.Length() > 2) {
		// This appears to be a mailbox confirmation response
		// The packet contains our mailbox name echoed back
		std::string mailbox_echo = p.GetCString(2);
		LogToFile("UCS mailbox confirmation: " + mailbox_echo);

		// This indicates successful authentication
		SetState(UCSConnectionState::Authenticated);
		LogToFile("UCS authentication successful");
	}
}

void UCSConnection::ProcessMailMessage(const EQ::Net::Packet &p)
{
	LogToFile("Processing mail message");
	// Handle mail-related operations
}

void UCSConnection::ProcessBuddyMessage(const EQ::Net::Packet &p)
{
	LogToFile("Processing buddy message");
	// Handle buddy list operations
}

void UCSConnection::ProcessChatMessage(const EQ::Net::Packet &p)
{
	LogToFile("Processing chat message");

	if (p.Length() > 10) {
		// Extract basic chat message info (format would depend on UCS protocol)
		std::string from = p.GetCString(2);
		std::string message = p.GetCString(2 + from.length() + 1);

		LogToFile("Chat from " + from + ": " + message);

		// Here you would typically emit an event or call a callback
		// to notify the main client about the received chat message
	}
}

void UCSConnection::ProcessChannelMessage(const EQ::Net::Packet &p)
{
	LogToFile("Processing channel message");

	// Similar to chat message but with channel information
	if (p.Length() > 15) {
		std::string channel = p.GetCString(2);
		std::string from = p.GetCString(2 + channel.length() + 1);
		std::string message = p.GetCString(2 + channel.length() + 1 + from.length() + 1);

		LogToFile("Channel [" + channel + "] " + from + ": " + message);
	}
}

void UCSConnection::ProcessTellMessage(const EQ::Net::Packet &p)
{
	LogToFile("Processing tell message");

	if (p.Length() > 10) {
		std::string from = p.GetCString(2);
		std::string message = p.GetCString(2 + from.length() + 1);

		LogToFile("Tell from " + from + ": " + message);
	}
}

void UCSConnection::SendChatMessage(const std::string& message, uint8_t channel_type)
{
	if (!IsConnected()) {
		LogToFile("Cannot send chat message - not connected to UCS");
		return;
	}

	LogToFile("Sending chat message: " + message);

	// UCS expects OP_Mail (0x0002) for chat messages
	// Format: opcode(2) + placeholder(1) + command string + null
	EQ::Net::DynamicPacket p;
	p.Resize(2 + 1 + message.length() + 1);

	p.PutUInt16(0, 0x0002); // OP_Mail
	p.PutUInt8(2, 0); // Placeholder byte (UCS expects command at offset 3)
	p.PutCString(3, message.c_str());

	m_connection->QueuePacket(p);
}

void UCSConnection::SendTell(const std::string& target, const std::string& message)
{
	if (!IsConnected()) {
		LogToFile("Cannot send tell - not connected to UCS");
		return;
	}

	LogToFile("Sending tell to " + target + ": " + message);

	// UCS uses OP_Mail (0x0002) for tells with "tell" command
	// Format: "tell targetname message"
	std::string command = "tell " + target + " " + message;

	EQ::Net::DynamicPacket p;
	p.Resize(2 + 1 + command.length() + 1);

	p.PutUInt16(0, 0x0002); // OP_Mail
	p.PutUInt8(2, 0); // Placeholder byte (UCS expects command at offset 3)
	p.PutCString(3, command.c_str());

	m_connection->QueuePacket(p);
}

void UCSConnection::SendChannelMessage(const std::string& channel, const std::string& message)
{
	if (!IsConnected()) {
		LogToFile("Cannot send channel message - not connected to UCS");
		return;
	}

	LogToFile("Sending channel message to " + channel + ": " + message);

	// For channel messages, we need to join the channel first, then send
	// Format: "#channelname message" or use channel number
	std::string command;

	if (channel == "general" || channel == "General") {
		// General chat is channel 0
		command = "0 " + message;
	} else if (channel == "ooc" || channel == "OOC") {
		// OOC is channel 5
		command = "5 " + message;
	} else if (channel == "auction" || channel == "Auction") {
		// Auction is channel 6
		command = "6 " + message;
	} else if (channel == "shout" || channel == "Shout") {
		// Shout is channel 3
		command = "3 " + message;
	} else {
		// Custom channel - need to join first
		command = "#" + channel + " " + message;
	}

	EQ::Net::DynamicPacket p;
	p.Resize(2 + 1 + command.length() + 1);

	p.PutUInt16(0, 0x0002); // OP_Mail
	p.PutUInt8(2, 0); // Placeholder byte (UCS expects command at offset 3)
	p.PutCString(3, command.c_str());

	m_connection->QueuePacket(p);
}

void UCSConnection::SendGuildMessage(const std::string& message)
{
	SendChannelMessage("guild", message);
}

void UCSConnection::SendGroupMessage(const std::string& message)
{
	SendChannelMessage("group", message);
}

void UCSConnection::SendSessionReady()
{
	LogToFile("Sending OP_SessionReady response to UCS server");

	// OP_SessionReady response packet
	EQ::Net::DynamicPacket p;
	p.Resize(3); // opcode(2) + data(1)

	p.PutUInt16(0, 0x0000); // OP_SessionReady
	p.PutUInt8(2, 1); // Ready flag

	m_connection->QueuePacket(p);
}
