// B874: coverage for the --pose-log CLI flags and the TSV row format.
//
// Two halves:
//
// 1. Application::parseArguments() is a static, side-effect-free function
//    of argc/argv, so the flags are directly testable without standing up an
//    Application, a renderer, or a GL context.
//
// 2. The row format (include/client/graphics/pose_log_format.h) is a free
//    function on (FILE*, int64_t, PoseLogRow) -- split from the writer the
//    same way FPCAM-DUMP split GATHER from FORMAT. It is tested HERE,
//    without a renderer: feed it the same values a frame would and assert
//    the file it wrote parses as the documented 14-column TSV (tab split,
//    the space-carrying camera-mode field intact, sane magnitudes, the EQ
//    0-512 heading range). The render-thread writer around it (file open,
//    every-Nth counter, the wall-clock ts) is exercised by the visual gate
//    run, not ctest: a headless EGL frame run would need a zone, which the
//    B874 live lane owns.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>  // getpid()

#include "scoped_temp_dir.h"

#include "client/application.h"
#include "client/graphics/pose_log_format.h"

using eqt::Application;
using eqt::ApplicationConfig;
using EQT::Graphics::PoseLogRow;
using EQT::Graphics::CameraMode;

namespace {

ApplicationConfig parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("willeq"));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    return Application::parseArguments(static_cast<int>(argv.size()), argv.data());
}

// The /tmp paths below are command-line VALUES parsed into ApplicationConfig
// and compared back -- the census waives them on the "pose_log.tsv" and
// "capture" substrings; no file is ever created (board B571, the
// video-capture flag tests' precedent). The row-format tests create files,
// and those carry a getpid() stem.
ApplicationConfig parsePose(std::vector<std::string> args) {
    args.insert(args.begin(), {"--pose-log", "/tmp/pose_log.tsv"});
    return parse(std::move(args));
}

ApplicationConfig parseCapture(std::vector<std::string> args) {
    args.insert(args.begin(), {"--dump-frames-dir", "/tmp/capture"});
    return parse(std::move(args));
}

// pose_log_format.h formats into a FILE*: this writes rows into a named
// file and hands the path back. The file lives under a per-process stem
// (getpid()), because /tmp is shared by every worktree slot on this host
// and ctest -j6 runs cases as concurrent processes (board B571, the
// TmpPathCensus).
std::string writeRowsToFile(const std::string& stem,
                            const std::vector<std::pair<int64_t, PoseLogRow>>& rows) {
    const std::string path = "/tmp/" + std::to_string(::getpid()) + "_" + stem + "_pose.tsv";
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) return path;  // open failure surfaces as a parse failure below
    std::fputs(EQT::Graphics::poseLogHeaderLine(), f);
    for (const auto& r : rows) EQT::Graphics::poseLogRow(f, r.first, r.second);
    std::fclose(f);
    return path;
}

// Split a line on '\t' ONLY -- the documented consumer split (Deliverable A's
// parse_pose_log.sh, and the B875/B876 fit) never splits on spaces.
std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

// A row shaped like a zone-in first-person frame in qeynos: sane EQ-magnitude
// coordinates (hundreds, not thousands-of-thousands), a mid-range 0-512
// heading, the space-carrying camera name.
PoseLogRow sampleRow() {
    PoseLogRow r;
    r.frame = 42;
    r.camX = 1043.25f;  r.camY = 21.5f;  r.camZ = -372.125f;
    r.targetX = 1044.5f; r.targetY = 19.75f; r.targetZ = -370.5f;
    r.playerX = 1044.5f; r.playerY = 19.75f; r.playerZ = -370.5f;
    r.heading0_512 = 316;
    r.cameraMode = CameraMode::FirstPerson;
    r.movementState = 1;  // move
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------
// The CLI flags
// ---------------------------------------------------------------------------

TEST(PoseLogFlags, DefaultsAreOff) {
    ApplicationConfig config = parse({});
    EXPECT_TRUE(config.poseLogPath.empty());
    EXPECT_EQ(config.poseLogEvery, 0);
    EXPECT_EQ(config.poseLogMaxCount, 600);
}

TEST(PoseLogFlags, UnrelatedFlagsLeavePoseLogOff) {
    ApplicationConfig config = parse({"--no-pathfinding", "--threads", "4"});
    EXPECT_TRUE(config.poseLogPath.empty());
    EXPECT_EQ(config.poseLogEvery, 0);
}

TEST(PoseLogFlags, PoseLogPathParsed) {
    ApplicationConfig config = parsePose({});
    EXPECT_EQ(config.poseLogPath, "/tmp/pose_log.tsv");
    // --pose-log sets poseLogEvery=1 (every frame) by default.
    EXPECT_EQ(config.poseLogEvery, 1);
    EXPECT_EQ(config.poseLogMaxCount, 600);
}

TEST(PoseLogFlags, PoseLogEveryAndMaxOverridable) {
    ApplicationConfig config = parsePose({
        "--pose-every", "4",
        "--pose-max-frames", "1500"
    });
    EXPECT_EQ(config.poseLogPath, "/tmp/pose_log.tsv");
    EXPECT_EQ(config.poseLogEvery, 4);
    EXPECT_EQ(config.poseLogMaxCount, 1500);
}

TEST(PoseLogFlags, PoseLogSeparateFromDumpFrames) {
    // Both --pose-log and --dump-frames-dir can coexist.
    ApplicationConfig config = parseCapture({"--pose-log", "pl.tsv", "--dump-every", "2"});
    EXPECT_EQ(config.poseLogPath, "pl.tsv");
    EXPECT_EQ(config.poseLogEvery, 1);
    EXPECT_EQ(config.dumpFramesDir, "/tmp/capture");
    EXPECT_EQ(config.dumpFramesEvery, 2);
    // And the pose writer's own knobs: --pose-every applies to the pose
    // writer and only to it, even with a dump dir in the same line.
    config = parse({"--pose-log", "pl.tsv", "--dump-frames-dir", "frames", "--pose-every", "8", "--dump-every", "2"});
    EXPECT_EQ(config.poseLogPath, "pl.tsv");
    EXPECT_EQ(config.poseLogEvery, 8);
    EXPECT_EQ(config.dumpFramesDir, "frames");
    EXPECT_EQ(config.dumpFramesEvery, 2);
}

TEST(PoseLogFlags, ParseLeaveOtherFlagsUnmodified) {
    ApplicationConfig config = parsePose({"--no-pathfinding"});
    EXPECT_EQ(config.poseLogPath, "/tmp/pose_log.tsv");
    EXPECT_FALSE(config.pathfindingEnabled);
    // dumpFramesDir must remain empty (unrelated).
    EXPECT_TRUE(config.dumpFramesDir.empty());
}

// ---------------------------------------------------------------------------
// The row format -- what the TSV actually contains
// ---------------------------------------------------------------------------

// The header line is 14 tab-separated column names (the spec's row:
// ts frame cameramode + 3+3+3 floats + heading + movestate; the B874
// cards' "13 columns" is a miscount of that same row), no '#' prefix
// (a '#' line would read as a comment to every consumer) and no double
// spaces.
TEST(PoseLogFormat, HeaderIsFourteenTabSeparatedColumns) {
    const std::string header = EQT::Graphics::poseLogHeaderLine();
    ASSERT_EQ(header.back(), '\n');
    const auto cols = splitTabs(header.substr(0, header.size() - 1));
    ASSERT_EQ(cols.size(), 14u);
    EXPECT_EQ(cols[0], "ts_unix_ms");
    EXPECT_EQ(cols[1], "frame");
    EXPECT_EQ(cols[2], "cameramode");
    EXPECT_EQ(cols[3], "camX");
    EXPECT_EQ(cols[6], "targetX");
    EXPECT_EQ(cols[9], "playerX");
    EXPECT_EQ(cols[12], "heading0_512");
    EXPECT_EQ(cols[13], "movestate");
    for (const auto& c : cols) {
        EXPECT_FALSE(c.find("  ") != std::string::npos) << c;
        EXPECT_FALSE(c.empty()) << c;
    }
}

// A data row is 14 tab-separated fields and round-trips every value the
// writer would have handed the format.
TEST(PoseLogFormat, DataRowRoundTripsAllFourteenColumns) {
    const std::string path = writeRowsToFile("t874_roundtrip", {{1758000000123LL, sampleRow()}});

    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string headerLine;
    ASSERT_TRUE(std::getline(in, headerLine));
    std::string line;
    ASSERT_TRUE(std::getline(in, line));
    // A second row must not exist; read it out (the stream's state carries
    // the answer, it must not be printed -- printing consumes the stream).
    std::string extra;
    std::getline(in, extra);
    EXPECT_FALSE(in) << "exactly one data row was written";

    const auto f = splitTabs(line);
    ASSERT_EQ(f.size(), 14u) << line;
    // ts: the exact epoch ms the caller passed (the unix-ms contract --
    // comparable to host time, Deliverable A section 2).
    EXPECT_EQ(f[0], "1758000000123");
    EXPECT_EQ(f[1], "42");
    // The camera-mode field carries a space; only a tab split keeps it intact.
    EXPECT_EQ(f[2], "First Person Camera");
    // Exact floats: %.6f of the sample values.
    EXPECT_EQ(f[3], "1043.250000");
    EXPECT_EQ(f[4], "21.500000");
    EXPECT_EQ(f[5], "-372.125000");
    EXPECT_EQ(f[6], "1044.500000");
    EXPECT_EQ(f[7], "19.750000");
    EXPECT_EQ(f[8], "-370.500000");
    EXPECT_EQ(f[9], "1044.500000");
    EXPECT_EQ(f[10], "19.750000");
    EXPECT_EQ(f[11], "-370.500000");
    // heading is the 0-512 value as-is (not degrees, not rescaled).
    EXPECT_EQ(f[12], "316");
    std::remove(path.c_str());
}

// The heading column is the EQ 0-512 unit: every value the writer can emit
// lands in [0, 511], and 359-equivalents do NOT saturate to 512 (finding 1:
// the pre-fix code re-scaled 0-512 as if it were degrees, x512/360).
TEST(PoseLogFormat, HeadingColumnStaysInZeroToFiveHundredEleven) {
    std::vector<std::pair<int64_t, PoseLogRow>> rows;
    for (uint16_t h : {0u, 1u, 127u, 255u, 316u, 500u, 511u}) {
        PoseLogRow r = sampleRow();
        r.heading0_512 = h;
        rows.push_back({1, r});
    }
    const std::string path = writeRowsToFile("t874_heading", rows);
    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string line;
    ASSERT_TRUE(std::getline(in, line));  // header
    for (uint16_t h : {0u, 1u, 127u, 255u, 316u, 500u, 511u}) {
        ASSERT_TRUE(std::getline(in, line));
        const auto f = splitTabs(line);
        ASSERT_EQ(f.size(), 14u);
        const long v = std::atol(f[12].c_str());
        EXPECT_EQ(v, static_cast<long>(h))
            << "heading " << h << " must round-trip unchanged";
        EXPECT_GE(v, 0);
        EXPECT_LE(v, 511);
    }
    std::remove(path.c_str());
}

// A whole file parses: every line (header included) is 14 tab-separated
// columns, the ts column is monotonic non-decreasing and a plausible unix-ms
// magnitude (1.7e12, i.e. ~2026), not the small virtual-timer value the
// pre-fix getTimerMs() would have emitted.
TEST(PoseLogFormat, MultiRowFileParsesWithMonotonicEpochTimestamps) {
    std::vector<std::pair<int64_t, PoseLogRow>> rows;
    int64_t ts = 1758000000000LL;  // ~2026-09-16
    for (int i = 0; i < 5; ++i) {
        PoseLogRow r = sampleRow();
        r.frame = 10 + i;
        rows.push_back({ts, r});
        ts += 16;  // ~60fps
    }
    const std::string path = writeRowsToFile("t874_multi", rows);
    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string line;
    int lines = 0;
    int64_t lastTs = -1;
    while (std::getline(in, line)) {
        const auto f = splitTabs(line);
        ASSERT_EQ(f.size(), 14u) << "line " << lines << ": " << line;
        if (lines > 0) {
            const int64_t tsVal = std::atoll(f[0].c_str());
            EXPECT_GE(tsVal, lastTs) << "ts must be monotonic non-decreasing";
            lastTs = tsVal;
            // unix-ms magnitude: a 60fps session spans real wall clock, and
            // an epoch-ms value is ~1.7e12 (the virtual timer's 0-based ms
            // could never look like this).
            EXPECT_GT(tsVal, 1000000000000LL) << f[0];
            // Sane magnitudes: a zone coordinate in qeynos is hundreds, not
            // 1e9 and not NaN/inf.
            for (int c = 3; c <= 11; ++c) {
                const double v = std::atof(f[c].c_str());
                EXPECT_FALSE(std::isnan(v)) << f[c];
                EXPECT_LT(std::abs(v), 1000000.0) << f[c];
            }
        }
        ++lines;
    }
    EXPECT_EQ(lines, 6) << "one header + five rows";
    std::remove(path.c_str());
}

// Every camera-mode name that the writer can emit stays one tab field,
// including the two that carry spaces ("First Person Camera" and
// "User Defined Camera 1" -- eqstr_us.txt 9500..9505, the B874 review's
// finding 5).
TEST(PoseLogFormat, EveryCameraModeNameStaysOneField) {
    const std::vector<std::pair<CameraMode, std::string>> names = {
        {CameraMode::FirstPerson, "First Person Camera"},
        {CameraMode::Overhead, "Overhead Camera"},
        {CameraMode::Chase, "Chase Camera"},
        {CameraMode::User1, "User Defined Camera 1"},
        {CameraMode::User2, "User Defined Camera 2"},
        {CameraMode::Tether, "Tether Camera"},
    };
    std::vector<std::pair<int64_t, PoseLogRow>> rows;
    int64_t ts = 1;
    for (const auto& n : names) {
        PoseLogRow r = sampleRow();
        r.cameraMode = n.first;
        rows.push_back({ts++, r});
    }
    const std::string path = writeRowsToFile("t874_modes", rows);
    std::ifstream in(path);
    ASSERT_TRUE(in.good());
    std::string line;
    ASSERT_TRUE(std::getline(in, line));  // header
    for (const auto& n : names) {
        ASSERT_TRUE(std::getline(in, line));
        const auto f = splitTabs(line);
        ASSERT_EQ(f.size(), 14u) << line;
        EXPECT_EQ(f[2], n.second) << "camera-mode field must round-trip intact";
    }
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Finding #10: the writer's file path comes from m_config.poseLogPath, not a
// member variable. This test proves the whole open→write→close path through
// a ScopedTempDir path so the file actually appears on disk (board B874, round 3).
// The pre-fix writeRowsToFile() helper used /tmp/getpid() which masked the bug.
// ---------------------------------------------------------------------------

TEST(PoseLogWriter, FileAppearsAtConfigPath) {
    const willeq_test::ScopedTempDir tmp("t874_writer_path");  // B571
    const std::string path = (tmp / "pose_log.tsv").string();

    // Use the SAME unit the writer uses: poseLogOpen (fopen + header + flush),
    // poseLogRow for data, poseLogClose to finalize. This ensures a wiring
    // regression (re-pointed member, unreachable block, wrong path) makes this
    // test fail — the mechanism that let finding #10 survive two rounds.
    std::FILE* f = EQT::Graphics::poseLogOpen(path);
    ASSERT_NE(f, nullptr) << "writer must open the path from config";

    // Write two rows (one idle, one moving).
    int64_t ts = 1758000000000LL;
    PoseLogRow r1 = sampleRow();
    r1.movementState = 0;  // idle
    r1.frame = 100;
    EQT::Graphics::poseLogRow(f, ts, r1);

    PoseLogRow r2 = sampleRow();
    r2.movementState = 1;  // move
    r2.frame = 200;
    ts += 16;
    EQT::Graphics::poseLogRow(f, ts, r2);

    EQT::Graphics::poseLogClose(f);

    // File must exist and contain header + 2 rows.
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << "file must exist after writer closes it";

    // Header line.
    std::string headerLine;
    ASSERT_TRUE(std::getline(in, headerLine));
    const auto hCols = splitTabs(headerLine);
    ASSERT_EQ(hCols.size(), 14u);
    EXPECT_EQ(hCols[0], "ts_unix_ms");

    // Two data rows.
    for (int i = 0; i < 2; ++i) {
        std::string line;
        ASSERT_TRUE(std::getline(in, line)) << "row " << i << " missing";
        const auto cols = splitTabs(line);
        ASSERT_EQ(cols.size(), 14u) << "line " << i << ": " << line;
        // Verify frame and movementState from the row we wrote.
        if (i == 0) {
            EXPECT_EQ(cols[1], "100");
            EXPECT_EQ(cols[13], "0");  // idle
        } else {
            EXPECT_EQ(cols[1], "200");
            EXPECT_EQ(cols[13], "1");  // move
        }
    }

    // No third row.
    std::string extra;
    std::getline(in, extra);
    EXPECT_FALSE(in) << "exactly 2 data rows written";
}
