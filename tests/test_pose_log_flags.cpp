// B874: coverage for the --pose-log CLI flag's argument parsing.
//
// Application::parseArguments() is a static, side-effect-free function
// of argc/argv, so it is directly testable without standing up an
// Application, a renderer, or a GL context.
#include <gtest/gtest.h>
#include "client/application.h"

using eqt::Application;
using eqt::ApplicationConfig;

namespace {

ApplicationConfig parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>("willeq"));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    return Application::parseArguments(static_cast<int>(argv.size()), argv.data());
}

} // namespace

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
    ApplicationConfig config = parse({"--pose-log", "/tmp/pose.tsv"});
    EXPECT_EQ(config.poseLogPath, "/tmp/pose.tsv");
    // --pose-log sets poseLogEvery=1 (every frame) by default.
    EXPECT_EQ(config.poseLogEvery, 1);
    EXPECT_EQ(config.poseLogMaxCount, 600);
}

TEST(PoseLogFlags, PoseLogSeparateFromDumpFrames) {
    // Both --pose-log and --dump-frames-dir can coexist.
    ApplicationConfig config = parse({
        "--pose-log", "/tmp/pose.tsv",
        "--dump-frames-dir", "/tmp/capture",
        "--dump-every", "2"
    });
    EXPECT_EQ(config.poseLogPath, "/tmp/pose.tsv");
    EXPECT_EQ(config.poseLogEvery, 1);
    EXPECT_EQ(config.dumpFramesDir, "/tmp/capture");
    EXPECT_EQ(config.dumpFramesEvery, 2);
}

TEST(PoseLogFlags, ParseLeaveOtherFlagsUnmodified) {
    ApplicationConfig config = parse({"--pose-log", "/tmp/pose.tsv", "--no-pathfinding"});
    EXPECT_EQ(config.poseLogPath, "/tmp/pose.tsv");
    EXPECT_FALSE(config.pathfindingEnabled);
    // dumpFramesDir must remain empty (unrelated).
    EXPECT_TRUE(config.dumpFramesDir.empty());
}
