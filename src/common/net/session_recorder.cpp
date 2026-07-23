#include "common/net/session_recorder.h"
#include "common/logging.h"

#include <cstring>

namespace EQT {
namespace Net {

SessionRecorder::~SessionRecorder()
{
	close();
}

bool SessionRecorder::open(const std::string& path, const std::string& character, const std::string& zone)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (m_file) {
		LOG_WARN(MOD_NET, "SessionRecorder::open() called while already open, ignoring");
		return false;
	}

	m_file = fopen(path.c_str(), "wb");
	if (!m_file) {
		LOG_ERROR(MOD_NET, "SessionRecorder: failed to open '{}' for writing", path);
		return false;
	}

	SessionLogFileHeader header;
	header.startTimestampUs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());

	std::strncpy(header.character, character.c_str(), sizeof(header.character) - 1);
	std::strncpy(header.zone, zone.c_str(), sizeof(header.zone) - 1);

	if (fwrite(&header, sizeof(header), 1, m_file) != 1) {
		LOG_ERROR(MOD_NET, "SessionRecorder: failed to write header to '{}'", path);
		fclose(m_file);
		m_file = nullptr;
		return false;
	}

	fflush(m_file);

	m_start_time = std::chrono::steady_clock::now();
	m_record_count = 0;

	return true;
}

void SessionRecorder::record(SessionStream stream, SessionDirection direction, uint16_t opcode,
	const void* data, uint32_t size)
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (!m_file) {
		return;
	}

	SessionLogRecordHeader header;
	header.timestampUs = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::microseconds>(
			std::chrono::steady_clock::now() - m_start_time).count());
	header.stream = static_cast<uint8_t>(stream);
	header.direction = static_cast<uint8_t>(direction);
	header.opcode = opcode;
	header.size = size;

	fwrite(&header, sizeof(header), 1, m_file);
	if (size > 0 && data) {
		fwrite(data, 1, size, m_file);
	}

	++m_record_count;
	if (m_record_count % FLUSH_INTERVAL == 0) {
		fflush(m_file);
	}
}

void SessionRecorder::close()
{
	std::lock_guard<std::mutex> lock(m_mutex);

	if (!m_file) {
		return;
	}

	fflush(m_file);
	fclose(m_file);
	m_file = nullptr;

	LOG_INFO(MOD_NET, "SessionRecorder: closed session log ({} records)", m_record_count);
}

} // namespace Net
} // namespace EQT
