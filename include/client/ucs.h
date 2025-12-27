#pragma once

#include "common/net/daybreak_connection.h"
#include "common/logging.h"
#include "common/util/types.h"
#include <memory>
#include <string>
#include <fstream>

enum class UCSConnectionState {
	Disconnected,
	Connecting,
	Connected,
	Authenticating,
	Authenticated,
	Error
};

class UCSConnection {
public:
	UCSConnection(const std::string& character_name, uint32_t dbid, const std::string& key);
	~UCSConnection();

	// Connection management
	void ConnectToUCS(const std::string& host, int port, const std::string& client_ip);
	void Disconnect();
	bool IsConnected() const;

	// State management
	UCSConnectionState GetState() const { return m_state; }
	void SetState(UCSConnectionState state) { m_state = state; }

	// Chat functionality
	void SendChatMessage(const std::string& message, uint8_t channel_type = 0);
	void SendTell(const std::string& target, const std::string& message);
	void SendChannelMessage(const std::string& channel, const std::string& message);
	void SendGuildMessage(const std::string& message);
	void SendGroupMessage(const std::string& message);

	// Connection callbacks
	void OnNewConnection(std::shared_ptr<EQ::Net::DaybreakConnection> connection);
	void OnStatusChangeActive(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void OnStatusChangeInactive(std::shared_ptr<EQ::Net::DaybreakConnection> conn, EQ::Net::DbProtocolStatus from, EQ::Net::DbProtocolStatus to);
	void OnPacketRecv(std::shared_ptr<EQ::Net::DaybreakConnection> conn, const EQ::Net::Packet &p);

private:
	// Connection management
	std::unique_ptr<EQ::Net::DaybreakConnectionManager> m_connection_manager;
	std::shared_ptr<EQ::Net::DaybreakConnection> m_connection;
	UCSConnectionState m_state;

	// Authentication data
	std::string m_character_name;
	uint32_t m_dbid;
	std::string m_key;

	// Connection details
	std::string m_host;
	int m_port;
	std::string m_client_ip;

	// Logging
	std::ofstream m_log_file;
	void LogToFile(const std::string& message);

	// Protocol handlers
	void SendUCSAuth();
	void SendSessionReady();
	void ProcessUCSAuthResponse(const EQ::Net::Packet &p);
	void ProcessUCSServerResponse(const EQ::Net::Packet &p);
	void ProcessMailMessage(const EQ::Net::Packet &p);
	void ProcessBuddyMessage(const EQ::Net::Packet &p);
	void ProcessChatMessage(const EQ::Net::Packet &p);
	void ProcessChannelMessage(const EQ::Net::Packet &p);
	void ProcessTellMessage(const EQ::Net::Packet &p);
};
