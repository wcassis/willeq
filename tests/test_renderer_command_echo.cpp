// T233 -- renderer-registry (registry 3) command replies must echo to the
// console log, not only to the in-game chat panel (T142 leftover, confirmed
// open by T223's Review).
//
// Background: game-registry handlers reply through
// EverQuest::AddChatSystemMessage() (eq_chat.cpp), which pushes a
// SystemMessage event to the bridge AND does LOG_INFO(MOD_MAIN, ...).
// IrrlichtRenderer::processSlashCommand()'s chat() lambda
// (irrlicht_renderer.cpp) is the registry-3 equivalent and only did the
// first half, so a --script run or a grep of the log saw nothing for
// /fps, /wireframe, /newskills, /camera, ... (review/T142/ has the live A/B
// where the "[MAIN] Spell effect settings reloaded" line vanished when
// /spellreload moved to registry 3).
//
// This test drives the REAL processSlashCommand() on a bare, never-Init()'d
// IrrlichtRenderer (the T66 idiom from test_irrlicht_camera_command.cpp --
// /camera touches no device state) and asserts BOTH halves:
//   1. the chat-panel SystemMessage still arrives on the bridge, unchanged;
//   2. the same text reaches stdout through the logging drain thread as a
//      "[MAIN] <text>" line, at LOG_INFO, with the logging thread still
//      alive (the T31 steady-state capture idiom from test_logging.cpp --
//      redirect fd 1, poll for the line, restore).
//
// Red (pre-fix): case 2 fails -- no "[MAIN] Camera mode: Follow" reaches
// stdout within the window while the bridge still receives the message.

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/state/event_bus.h"
#include "common/logging.h"

using namespace EQT::Graphics;

namespace {

class TestBridge : public eqt::bridge::GameStateBridge {
public:
    void applyEvent(const eqt::state::GameEvent&) override {}
};

std::vector<std::string> DrainSystemMessages(eqt::bridge::GameStateBridge& bridge) {
    std::vector<std::string> out;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            out.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        }
    }
    return out;
}

// Redirects fd 1 to a temp file for the lifetime of the object, then hands
// back whatever the logging drain thread wrote there. Polls for `needle`
// (up to `deadline`) so the test does not depend on the drain cadence.
class StdoutCapture {
public:
    StdoutCapture() {
        char tmpl[] = "/tmp/willeq_test_renderer_echo_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) close(fd);
        path_ = tmpl;
        fflush(stdout);
        std::cout.flush();
        int file_fd = open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        saved_ = dup(1);
        dup2(file_fd, 1);
        close(file_fd);
    }

    std::string waitFor(const std::string& needle,
                        std::chrono::milliseconds deadline = std::chrono::milliseconds(2000)) {
        auto start = std::chrono::steady_clock::now();
        std::string content;
        while (std::chrono::steady_clock::now() - start < deadline) {
            // ThreadLogBuffer only flips a side on append()/flush(); the
            // drain thread reads the inactive side. A single logged line
            // whose flip was refused (drain side still busy) waits for the
            // NEXT append on this thread -- in the app that is the next
            // render-thread log line, here we nudge it explicitly.
            FlushThreadLog();
            content = read();
            if (content.find(needle) != std::string::npos) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return content;
    }

    ~StdoutCapture() {
        fflush(stdout);
        std::cout.flush();
        if (saved_ >= 0) {
            dup2(saved_, 1);
            close(saved_);
        }
        unlink(path_.c_str());
    }

private:
    std::string read() const {
        std::ifstream in(path_);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    std::string path_;
    int saved_ = -1;
};

class RendererCommandEchoTest : public ::testing::Test {
protected:
    void SetUp() override {
        SetLogLevel(LOG_INFO);
        ClearModuleFilters();
        // Let anything a previous test's teardown logged (renderer
        // destructor "shutdown" line) drain before this test captures, so
        // a capture never contains a stale line from an earlier case.
        FlushThreadLog();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        FlushThreadLog();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    void TearDown() override {
        SetLogLevel(LOG_NONE);
    }
};

}  // namespace

TEST_F(RendererCommandEchoTest, ReplyReachesChatPanelAndConsoleLog) {
    IrrlichtRenderer renderer;
    TestBridge bridge;
    renderer.setBridge(&bridge);

    std::string captured;
    {
        StdoutCapture capture;
        renderer.processSlashCommand("/camera");
        captured = capture.waitFor("Camera mode: Follow");
    }

    // Half 1: chat panel path unchanged (exactly one SystemMessage).
    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "Camera mode: Follow");

    // Half 2: the console echo, same module/shape as
    // EverQuest::AddChatSystemMessage()'s LOG_INFO(MOD_MAIN, "{}", text).
    EXPECT_NE(captured.find("] [MAIN] Camera mode: Follow"), std::string::npos)
        << "renderer command reply did not reach the console log. Captured "
           "stdout: [" << captured << "]";
}

TEST_F(RendererCommandEchoTest, EchoDoesNotDependOnBridge) {
    // AddChatSystemMessage() logs even with no bridge; the renderer path
    // must too -- the log is the only observer in that configuration.
    IrrlichtRenderer renderer;

    std::string captured;
    {
        StdoutCapture capture;
        renderer.processSlashCommand("/camera");
        captured = capture.waitFor("Camera mode: Follow");
    }

    EXPECT_NE(captured.find("] [MAIN] Camera mode: Follow"), std::string::npos)
        << "captured stdout: [" << captured << "]";
}

TEST_F(RendererCommandEchoTest, EchoIsGatedByLogLevel) {
    // Same gate as every other LOG_INFO: at the default level (NONE) the
    // chat panel still gets the reply, the console stays quiet.
    SetLogLevel(LOG_NONE);
    IrrlichtRenderer renderer;
    TestBridge bridge;
    renderer.setBridge(&bridge);

    std::string captured;
    {
        StdoutCapture capture;
        renderer.processSlashCommand("/camera");
        captured = capture.waitFor("Camera mode: Follow", std::chrono::milliseconds(300));
    }

    auto msgs = DrainSystemMessages(bridge);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(captured.find("Camera mode: Follow"), std::string::npos)
        << "captured stdout: [" << captured << "]";
}
