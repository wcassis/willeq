// T308 Leg 2 -- registry 2's user-facing text reaches the on-screen chat.
//
// T304 Leg 3 established the routing and reported it as a live defect:
// Application::init() does m_commandProcessor->setOutputRenderer(
// m_gameMode->getRenderer()), GraphicalMode hands back a ConsoleRenderer for
// EVERY renderer type (it is not the Irrlicht window -- Application owns that
// separately), and ConsoleRenderer::displaySystemMessage() is a bare
// std::cout. Registry 1 and the merged /help answer through
// EverQuest::AddChatSystemMessage() (the chat window) and registry 3 goes over
// the bridge to the render thread, but registry 2 -- action::CommandProcessor,
// which owns /say, /shout, /tell, /gu, /emote, /face, /pet and ~60 messages
// including every "Usage:" error -- answered to stdout ONLY. A player in a
// graphical run typed a command wrong and saw nothing at all.
//
// The pin drives the path a USER drives: text typed into the chat window goes
// to EverQuest::ProcessChatInput(), which tries registry 1 and then the
// fallback Application installs in wireUnifiedCommandDispatch() -- the same
// production function, called here, that also installs the chat sink. The
// assertion is on the SystemMessage events that reach the bridge, i.e. what
// the chat window would render.

#include "client/action/action_dispatcher.h"
#include "client/action/command_processor.h"
#include "client/application.h"
#include "client/eq.h"
#include "client/eq_action_handler.h"
#include "client/output/console_renderer.h"

#include "zone_stream_test_utils.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace eqt {

// Builds exactly the four objects Application::init() builds between its
// EverQuest construction and its CommandProcessor construction
// (application.cpp), then calls the production wiring function. No network, no
// renderer, no graphics -- init() itself cannot run under ctest (it connects).
struct ApplicationCommandOutputTestPeer {
    static void wire(Application& app, std::unique_ptr<EverQuest> eq) {
        app.m_eqClient = std::move(eq);
        app.m_actionHandler = std::make_unique<EqActionHandler>(*app.m_eqClient);
        app.m_dispatcher = std::make_unique<action::ActionDispatcher>(
            app.m_eqClient->GetGameState());
        app.m_dispatcher->setActionHandler(app.m_actionHandler.get());
        app.m_commandProcessor = std::make_unique<action::CommandProcessor>(
            app.m_eqClient->GetGameState(), *app.m_dispatcher);
        app.wireUnifiedCommandDispatch();
    }
    static EverQuest& client(Application& app) { return *app.m_eqClient; }
    static action::CommandProcessor& processor(Application& app) {
        return *app.m_commandProcessor;
    }
};

}  // namespace eqt

namespace {

using eqt::Application;
using eqt::ApplicationCommandOutputTestPeer;

// Collects the SystemMessage events AddChatSystemMessage() pushes -- the
// chat window's own feed.
std::vector<std::string> DrainSystemMessages(zonetest::TestBridge& bridge) {
    std::vector<std::string> out;
    for (const auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            if (const auto* d = std::get_if<eqt::state::ChatMessageData>(&ev.data)) {
                out.push_back(d->message);
            }
        }
    }
    return out;
}

bool AnyContains(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) return true;
    }
    return false;
}

class CommandOutputRouting : public ::testing::Test {
protected:
    void SetUp() override {
        auto eq = std::make_unique<EverQuest>("127.0.0.1", 0, "test", "test",
                                              "test", "Testchar");
        eq->setBridge(&m_bridge);
        ApplicationCommandOutputTestPeer::wire(m_app, std::move(eq));
        DrainSystemMessages(m_bridge);  // discard construction noise, if any
    }

    EverQuest& client() { return ApplicationCommandOutputTestPeer::client(m_app); }

    // The user typing into the chat window.
    std::vector<std::string> Type(const std::string& line) {
        client().ProcessChatInput(line);
        return DrainSystemMessages(m_bridge);
    }

    zonetest::TestBridge m_bridge;
    Application m_app;
};

// ---------------------------------------------------------------------------

TEST_F(CommandOutputRouting, UsageErrorFromRegistryTwoReachesTheChatWindow) {
    // /tell lives in registry 2 and has requiresArgs. Typed bare, registry 1
    // misses, the fallback hands it to CommandProcessor::processCommand(),
    // which answers "Error: Usage: ...". Before T308 that string existed only
    // on stdout and the player saw an empty chat window.
    auto msgs = Type("/tell");
    EXPECT_TRUE(AnyContains(msgs, "Usage:"))
        << "registry-2 usage errors must reach the chat window";
    EXPECT_TRUE(AnyContains(msgs, "/tell"));
    // And it must NOT be mistaken for an unknown command: registry 2 claimed
    // it, so ProcessChatInput's own "Unknown command" branch stays quiet.
    EXPECT_FALSE(AnyContains(msgs, "Unknown command"));
}

TEST_F(CommandOutputRouting, SuccessMessagesRouteTooNotJustErrors) {
    // Not just the error helper: displayMessage() -- the success family, ~60
    // strings including the movement-mode acks, "Targeting: X", the group and
    // pet acks and this /spellbook hint -- goes the same way. /spellbook emits
    // three lines and all three must land in the chat window.
    auto msgs = Type("/spellbook");
    EXPECT_TRUE(AnyContains(msgs, "Ctrl+B")) << "the /spellbook hint must reach the chat window";
    EXPECT_TRUE(AnyContains(msgs, "memorize"));
    EXPECT_FALSE(AnyContains(msgs, "Unknown command"));
}

TEST_F(CommandOutputRouting, StdoutRoutingIsUnchangedForHeadlessAndScriptRuns) {
    // The fix ADDS a sink; it does not move the message off the renderer.
    // A --script / piped-stdin / console run still gets every line it got
    // before, because setOutputRenderer() is still what emitMessage() writes
    // first. Asserted through the same object Application hands it: an
    // eqt::output::ConsoleRenderer.
    eqt::output::ConsoleRenderer console;
    ApplicationCommandOutputTestPeer::processor(m_app).setOutputRenderer(&console);

    testing::internal::CaptureStdout();
    client().ProcessChatInput("/tell");
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_NE(out.find("Usage:"), std::string::npos)
        << "stdout must keep carrying registry-2 output; captured: " << out;
    EXPECT_TRUE(AnyContains(DrainSystemMessages(m_bridge), "Usage:"))
        << "...and the chat window must get it too, in the same call";
}

TEST_F(CommandOutputRouting, GenuinelyUnknownCommandsStillGetTheUnknownMessage) {
    // Registry 1 misses, registry 2 does not know the name, registry 3's
    // catalog does not either -- ProcessChatInput's own fallback message is
    // the one the player must see, unchanged by this unit.
    auto msgs = Type("/definitelynotacommand");
    EXPECT_TRUE(AnyContains(msgs, "Unknown command"));
}

}  // namespace
