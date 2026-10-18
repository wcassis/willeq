/**
 * B1063 (GitLab #383) -- F12 does what /screenshot does: the one body.
 *
 * The defect: the Receiver decodes a live F12 press to RA::Screenshot, which
 * FrameComponent deferred to `owner_.saveScreenshot("screenshot.png")` -- a
 * FIXED NAME in the process's cwd, and NO chat line. The typed /screenshot
 * named its own path and printed the chat confirmation. Two routes, two
 * paths, one of them silent.
 *
 * The fix: BOTH routes run SlashCommandsComponent::runScreenshot() -- the
 * one body -- with an empty filename meaning the default
 * screenshots/<zone>_<YYYYMMDD-HHMMSS>.png under the first writable cache
 * root ($WILLEQ_CACHE_DIR walk; the client's cwd under screenshots/ when
 * none is writable). The chat line "Screenshot <path>: saved|FAILED" is
 * emitted by the body itself, so the hotkey and the command cannot disagree
 * about where the file went or which one confirmed.
 *
 * What this file pins:
 *   - Behavioural: the default-name sanitizer, the one half that is pure
 *     and unit-testable without a renderer.
 *   - Source-pinned (the idiom of test_debug_hotkey_arms.cpp for single-site
 *     invariants with no public reader): the Receiver still queues
 *     RA::Screenshot, FrameComponent's deferred arm now calls
 *     ctx_.Slash().runScreenshot("") (the one body, not saveScreenshot),
 *     and the /screenshot arm of the renderer command chain calls
 *     runScreenshot(args).
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "client/graphics/slash/slash_commands_component.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using EQT::Graphics::SlashCommandsComponent;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------- The pure half: the default filename's zone component ----------

TEST(B1063Screenshot, SanitizeZoneName_KeepsSafeCharacters) {
    EXPECT_EQ(SlashCommandsComponent::sanitizeZoneName("qeynos_hills"),
              "qeynos_hills");
    EXPECT_EQ(SlashCommandsComponent::sanitizeZoneName("Zone-1"), "Zone-1");
    EXPECT_EQ(SlashCommandsComponent::sanitizeZoneName("qeynos 1"),
              "qeynos_1")
        << "a zone name with a space still names itself; the space becomes _";
}

TEST(B1063Screenshot, SanitizeZoneName_EmptyBecomesNozone) {
    // Before zone-in the zone name is empty: the default file must not be
    // "screenshots/_.png".
    EXPECT_EQ(SlashCommandsComponent::sanitizeZoneName(""), "nozone");
    EXPECT_EQ(SlashCommandsComponent::sanitizeZoneName("///"), "nozone");
    EXPECT_EQ(SlashCommandsComponent::sanitizeZoneName("___"), "nozone");
}

// ---------- Source-pins: the one body is reachable from BOTH routes ----------

// The live F12 route starts at the Receiver: HA::Screenshot queues
// RA::Screenshot. If this queue site goes, F12 is dead again (the class of
// defect this issue is about: the feature exists end to end except the one
// line that joins the halves).
TEST(B1063Screenshot, ReceiverStillQueuesTheScreenshotAction) {
    const std::string src = readFile(
        kRoot / "src" / "client" / "graphics" / "renderer" /
        "renderer_event_receiver.cpp");
    EXPECT_NE(src.find("case HA::Screenshot: actionQueue_.push_back({RA::Screenshot})"),
              std::string::npos)
        << "the Receiver no longer queues RA::Screenshot for F12 -- the hotkey "
           "is dropped at the source again (B1063)";
}

// The deferred arm in FrameComponent must call the ONE body with an empty
// filename (the default path), not a fixed name into the cwd.
TEST(B1063Screenshot, FrameTailArmRunsTheOneBodyNotSaveScreenshot) {
    const std::string src = readFile(
        kRoot / "src" / "client" / "graphics" / "frame" /
        "renderer_frame_render.cpp");
    EXPECT_NE(src.find("ctx_.Slash().runScreenshot(\"\")"), std::string::npos)
        << "the F12 deferred arm no longer runs SlashCommandsComponent::"
           "runScreenshot() -- it must not save to a fixed cwd name again";
    EXPECT_EQ(src.find("saveScreenshot(\"screenshot.png\")"), std::string::npos)
        << "the old fixed-name cwd save is back in FrameComponent";
}

// The typed route: /screenshot's arm in the renderer command chain calls the
// same body with the argument (empty = default).
TEST(B1063Screenshot, SlashArmRunsTheOneBody) {
    const std::string src = readFile(
        kRoot / "src" / "client" / "graphics" / "slash" /
        "renderer_slash_commands.cpp");
    // The /screenshot arm and the body definition are both in this TU.
    EXPECT_NE(src.find("cmd == \"/screenshot\""), std::string::npos);
    EXPECT_NE(src.find("runScreenshot(args)"), std::string::npos)
        << "/screenshot no longer calls runScreenshot() -- the hotkey and the "
           "command have split into two bodies again";
    EXPECT_NE(src.find("bool SlashCommandsComponent::runScreenshot("),
              std::string::npos)
        << "the one body's definition is missing";
}

}  // namespace
