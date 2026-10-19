// R01: Unit tests for EQT::Net::SessionRecorder — writes synthetic packets
// and re-reads the file with raw struct reads to verify header/record
// integrity against the format documented in session_log.h.

#include "common/net/session_recorder.h"
#include "scoped_temp_dir.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <vector>

using namespace EQT::Net;

namespace {

// B1098 (#418): every case used to write a FIXED name straight under
// gtest's TempDir() (/tmp), shared by every ctest run on the host -- the
// cases went red on a runner with "failed to open ... for writing" while
// another run held the file. One mkdtemp() directory per test
// PROCESS (removed at exit) keeps the names unique across slots, runners
// and concurrent ctest workers; the T477 census does not see TempDir(),
// which is why this slipped past it.
std::string TempPath(const char* name) {
	static willeq_test::ScopedTempDir dir{"session_recorder"};
	return (dir / name).string();
}

} // namespace

TEST(SessionRecorder, OpenWritesValidHeader) {
	std::string path = TempPath("session_recorder_header.slog");

	SessionRecorder recorder;
	ASSERT_TRUE(recorder.open(path, "Testchar", "qeynos"));
	recorder.close();

	FILE* f = fopen(path.c_str(), "rb");
	ASSERT_NE(f, nullptr);

	SessionLogFileHeader header;
	ASSERT_EQ(fread(&header, sizeof(header), 1, f), 1u);
	fclose(f);

	EXPECT_EQ(header.magic, SESSION_LOG_MAGIC);
	EXPECT_EQ(header.version, SESSION_LOG_VERSION);
	EXPECT_STREQ(header.character, "Testchar");
	EXPECT_STREQ(header.zone, "qeynos");
}

TEST(SessionRecorder, RecordRoundTripsPayloadAndOpcode) {
	std::string path = TempPath("session_recorder_records.slog");

	SessionRecorder recorder;
	ASSERT_TRUE(recorder.open(path, "Foo", "bar"));

	const uint8_t payload1[] = {0xDE, 0xAD, 0xBE, 0xEF};
	recorder.record(SessionStream::Zone, SessionDirection::ServerToClient, 0x1234, payload1, sizeof(payload1));

	const uint8_t payload2[] = {0x01, 0x02, 0x03};
	recorder.record(SessionStream::Zone, SessionDirection::ClientToServer, 0x5678, payload2, sizeof(payload2));

	// Zero-length payload should also round-trip cleanly.
	recorder.record(SessionStream::World, SessionDirection::ServerToClient, 0x0009, nullptr, 0);

	EXPECT_EQ(recorder.recordCount(), 3u);
	recorder.close();

	FILE* f = fopen(path.c_str(), "rb");
	ASSERT_NE(f, nullptr);

	SessionLogFileHeader header;
	ASSERT_EQ(fread(&header, sizeof(header), 1, f), 1u);
	EXPECT_EQ(header.magic, SESSION_LOG_MAGIC);

	// Record 1
	SessionLogRecordHeader rh1;
	ASSERT_EQ(fread(&rh1, sizeof(rh1), 1, f), 1u);
	EXPECT_EQ(rh1.stream, static_cast<uint8_t>(SessionStream::Zone));
	EXPECT_EQ(rh1.direction, static_cast<uint8_t>(SessionDirection::ServerToClient));
	EXPECT_EQ(rh1.opcode, 0x1234);
	ASSERT_EQ(rh1.size, sizeof(payload1));
	std::vector<uint8_t> buf1(rh1.size);
	ASSERT_EQ(fread(buf1.data(), 1, rh1.size, f), rh1.size);
	EXPECT_EQ(0, std::memcmp(buf1.data(), payload1, sizeof(payload1)));

	// Record 2
	SessionLogRecordHeader rh2;
	ASSERT_EQ(fread(&rh2, sizeof(rh2), 1, f), 1u);
	EXPECT_EQ(rh2.stream, static_cast<uint8_t>(SessionStream::Zone));
	EXPECT_EQ(rh2.direction, static_cast<uint8_t>(SessionDirection::ClientToServer));
	EXPECT_EQ(rh2.opcode, 0x5678);
	ASSERT_EQ(rh2.size, sizeof(payload2));
	std::vector<uint8_t> buf2(rh2.size);
	ASSERT_EQ(fread(buf2.data(), 1, rh2.size, f), rh2.size);
	EXPECT_EQ(0, std::memcmp(buf2.data(), payload2, sizeof(payload2)));

	// Record 3 (zero-length payload)
	SessionLogRecordHeader rh3;
	ASSERT_EQ(fread(&rh3, sizeof(rh3), 1, f), 1u);
	EXPECT_EQ(rh3.stream, static_cast<uint8_t>(SessionStream::World));
	EXPECT_EQ(rh3.direction, static_cast<uint8_t>(SessionDirection::ServerToClient));
	EXPECT_EQ(rh3.opcode, 0x0009);
	EXPECT_EQ(rh3.size, 0u);

	// Monotonic timestamps.
	EXPECT_LE(rh1.timestampUs, rh2.timestampUs);
	EXPECT_LE(rh2.timestampUs, rh3.timestampUs);

	// EOF reached.
	uint8_t extra;
	EXPECT_EQ(fread(&extra, 1, 1, f), 0u);

	fclose(f);
}

TEST(SessionRecorder, RecordBeforeOpenIsNoOp) {
	SessionRecorder recorder;
	const uint8_t payload[] = {0x01};
	// Should not crash and should not count as a record.
	recorder.record(SessionStream::Login, SessionDirection::ServerToClient, 1, payload, sizeof(payload));
	EXPECT_EQ(recorder.recordCount(), 0u);
	EXPECT_FALSE(recorder.isOpen());
}

TEST(SessionRecorder, DoubleOpenFails) {
	std::string path1 = TempPath("session_recorder_double_open_1.slog");
	std::string path2 = TempPath("session_recorder_double_open_2.slog");

	SessionRecorder recorder;
	ASSERT_TRUE(recorder.open(path1, "A", "z1"));
	EXPECT_FALSE(recorder.open(path2, "B", "z2"));
	recorder.close();
}

TEST(SessionRecorder, CloseIsIdempotent) {
	std::string path = TempPath("session_recorder_close_idempotent.slog");

	SessionRecorder recorder;
	ASSERT_TRUE(recorder.open(path, "A", "z"));
	recorder.close();
	recorder.close(); // Should not crash.
	EXPECT_FALSE(recorder.isOpen());
}
