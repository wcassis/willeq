// T64: coverage for the --dump-frames-dir/--dump-every/--dump-max-frames
// video-capture evidence tool's argument parsing.
//
// Application::parseArguments() is a static, side-effect-free (beyond the
// ApplicationConfig it returns and --help's stdout print) function of
// argc/argv, so it is directly testable without standing up an Application,
// a renderer, or a GL context. What is NOT testable here: the actual
// render-thread frame dump hook in Application::render() (needs a live
// renderer + processFrame() loop) -- that path is proven instead by the
// end-to-end capture run documented in refactor/T64_video_capture_extraction.md's
// Review section and delivered to review/T64/.
#include <gtest/gtest.h>
#include "client/application.h"

using eqt::Application;
using eqt::ApplicationConfig;

namespace {

// argv[0] is conventionally the program name; parseArguments starts at
// index 1, so every case below needs a leading placeholder.
ApplicationConfig parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("willeq"));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    return Application::parseArguments(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST(VideoCaptureFlags, DefaultsAreOff) {
    ApplicationConfig config = parse({});
    EXPECT_TRUE(config.dumpFramesDir.empty());
    EXPECT_EQ(config.dumpFramesEvery, 0);
    EXPECT_EQ(config.dumpFramesMaxCount, 600);
}

TEST(VideoCaptureFlags, UnrelatedFlagsLeaveCaptureOff) {
    // A normal run with other flags set must not accidentally enable capture.
    ApplicationConfig config = parse({"--no-pathfinding", "--threads", "4"});
    EXPECT_TRUE(config.dumpFramesDir.empty());
    EXPECT_EQ(config.dumpFramesEvery, 0);
}

TEST(VideoCaptureFlags, DumpFramesDirParsed) {
    ApplicationConfig config = parse({"--dump-frames-dir", "/tmp/capture", "--dump-every", "2"});
    EXPECT_EQ(config.dumpFramesDir, "/tmp/capture");
    EXPECT_EQ(config.dumpFramesEvery, 2);
    EXPECT_EQ(config.dumpFramesMaxCount, 600) << "default cap applies when --dump-max-frames is not passed";
}

TEST(VideoCaptureFlags, DumpMaxFramesParsed) {
    ApplicationConfig config = parse({"--dump-frames-dir", "/tmp/capture", "--dump-every", "1",
                                       "--dump-max-frames", "50"});
    EXPECT_EQ(config.dumpFramesDir, "/tmp/capture");
    EXPECT_EQ(config.dumpFramesEvery, 1);
    EXPECT_EQ(config.dumpFramesMaxCount, 50);
}

TEST(VideoCaptureFlags, ZeroDumpEveryIsExplicitNoOp) {
    // Directory given but --dump-every 0 (or omitted): the render hook checks
    // dumpFramesEvery > 0, so this must behave as fully disabled even though
    // dumpFramesDir is non-empty.
    ApplicationConfig config = parse({"--dump-frames-dir", "/tmp/capture", "--dump-every", "0"});
    EXPECT_EQ(config.dumpFramesDir, "/tmp/capture");
    EXPECT_EQ(config.dumpFramesEvery, 0);
}

TEST(VideoCaptureFlags, DirWithoutEveryIsNoOp) {
    // --dump-frames-dir alone, with no --dump-every at all, must also stay a
    // no-op: dumpFramesEvery keeps its zero default.
    ApplicationConfig config = parse({"--dump-frames-dir", "/tmp/capture"});
    EXPECT_EQ(config.dumpFramesDir, "/tmp/capture");
    EXPECT_EQ(config.dumpFramesEvery, 0);
}
