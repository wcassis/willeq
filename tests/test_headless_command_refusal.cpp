// T223: Application::dispatchToActionOrRenderer()'s registry-3 stage
// without a renderer.
//
// A bare Application (constructed, never initialize()d) has
// m_graphicsInitialized == false -- the same state a --no-graphics run or an
// EQT_GRAPHICS=OFF build is in when a renderer command reaches the fallback.
// Before T223 that fallback pushed EVERY unclaimed name onto the bridge and
// reported outcome=queued, even though nothing was listening (T139/T142,
// refactor/deferred.md). With no registry 1/2/bridge wired here, the only
// stage that can claim a name is the T223 catalog check, which is exactly
// the behaviour under test:
//   - a known renderer command -> recognized, registry=renderer,
//     success=false, queued=false, message names graphics mode;
//   - an unknown name -> unrecognized (registry=none), never queued.
//
// The process-level shape (test_headless_shutdown.cpp) cannot reach this
// stage: pre-connection, everything except /quit and /help is HELD, and
// there is no server to connect to under ctest.

#include "client/application.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using eqt::Application;
using eqt::CommandDispatchResult;

TEST(HeadlessCommandRefusalTest, KnownRendererCommandIsRefusedNotQueued) {
    Application app;
    // T363 (B058): the command is /skills now, not /newskills.
    CommandDispatchResult r = app.DispatchSlashCommand("/skills");
    EXPECT_TRUE(r.recognized);
    EXPECT_EQ(r.registry, "renderer");
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.queued);
    EXPECT_NE(r.message.find("requires graphics mode"), std::string::npos) << r.message;
    EXPECT_NE(r.message.find("/skills"), std::string::npos) << r.message;
}

// T363 (B058): Will ruled rename, no alias. The old name must be claimed by
// NO registry at all -- registry 3 classifies by the static catalog, so
// deleting the catalog row is what makes this unrecognized rather than
// "queued to a handler that only logs a warning" (the T139 queued lie).
TEST(HeadlessCommandRefusalTest, NewSkillsIsNoLongerRecognizedByAnyRegistry) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("/newskills");
    EXPECT_FALSE(r.recognized);
    EXPECT_EQ(r.registry, "none");
    EXPECT_FALSE(r.queued);
}

// T363: the bare `skills` word on piped stdin. Before this unit
// ConsoleInputHandler intercepted it into InputAction::ToggleSkills, which
// NOTHING consumes (InputActionBridge::processDiscreteActions has no arm for
// it) -- so the keyword did nothing at all, which is not what B058 recorded.
// With the interception deleted it falls through to m_rawCommands, which
// Application's unified dispatcher hands to DispatchSlashCommand VERBATIM,
// i.e. with no leading slash. That is the line this asserts: the same
// registry-3 classification a typed /skills gets.
TEST(HeadlessCommandRefusalTest, BareSkillsKeywordIsClaimedByTheRendererRegistry) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("skills");
    EXPECT_TRUE(r.recognized);
    EXPECT_EQ(r.registry, "renderer");
    EXPECT_NE(r.message.find("/skills requires graphics mode"), std::string::npos) << r.message;
}

// The source-text half of the same claim: ConsoleInputHandler must no longer
// carry the dead InputAction::ToggleSkills interception. A behavioural test
// cannot drive processCommand() -- it is private, and the constructor spawns
// a stdin-reading thread, which is not something a ctest may do.
TEST(HeadlessCommandRefusalTest, ConsoleHandlerNoLongerInterceptsTheSkillsKeyword) {
    std::ifstream in(CONSOLE_INPUT_SOURCE);
    ASSERT_TRUE(in.is_open()) << CONSOLE_INPUT_SOURCE;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string src = buffer.str();
    EXPECT_EQ(src.find("InputAction::ToggleSkills"), std::string::npos)
        << "ConsoleInputHandler still sets the dead ToggleSkills action; the "
           "`skills` keyword cannot reach the renderer's one body";
    EXPECT_NE(src.find("// T363"), std::string::npos)
        << "the deletion is not explained at the site";
}

// ---------------------------------------------------------------------------
// T365 (B090): the `inventory` / `inv` / `group` stdin keywords. All three set
// an InputAction nothing consumed, exactly like `skills` before T363.
// ---------------------------------------------------------------------------

// The bare word now falls through to the unified dispatcher, which hands it
// to DispatchSlashCommand with no leading slash. Registry 3 claims it because
// this unit renamed the catalog row `newinv` -> `inventory`.
TEST(HeadlessCommandRefusalTest, T365_BareInventoryKeywordIsClaimedByTheRendererRegistry) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("inventory");
    EXPECT_TRUE(r.recognized);
    EXPECT_EQ(r.registry, "renderer");
    EXPECT_NE(r.message.find("/inventory requires graphics mode"), std::string::npos) << r.message;
}

// The old spelling must be claimed by NO registry -- the rename removed the
// catalog row, and no alias was added (Will's B058 shape).
TEST(HeadlessCommandRefusalTest, T365_NewInvIsNoLongerRecognizedByAnyRegistry) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("/newinv");
    EXPECT_FALSE(r.recognized);
    EXPECT_EQ(r.registry, "none");
    EXPECT_FALSE(r.queued);
}

// Why `inv` cannot simply fall through: registry 2 owns it as an alias of
// /invite and is tried BEFORE registry 3, so a bare `inv` line would invite a
// group member. Registry 3 must NOT claim it -- adding `inv` as an alias of
// `/inventory` would be dead weight (registry 2 wins) AND would make the
// catalog lie about what the word does. The registry-2 half of the collision
// is pinned in test_merged_help.cpp (T365_InvIsInvitesAliasNotInventorys),
// which has a real CommandProcessor; a bare Application has none.
TEST(HeadlessCommandRefusalTest, T365_RegistryThreeDoesNotClaimInv) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("inv");
    EXPECT_FALSE(r.recognized)
        << "registry 3 claims `inv`; registry 2's /invite alias wins the real "
           "dispatch anyway, so the ConsoleInputHandler rewrite is what makes "
           "the keyword work -- revisit it if this changes";
    EXPECT_EQ(r.registry, "none");
}

// `group` had no consumer at all: RendererAction::ToggleGroup is an empty arm
// in renderer_frame_input.cpp, so there is no group-window toggle to route to.
// The keyword is deleted; the word is now honestly unrecognized.
TEST(HeadlessCommandRefusalTest, T365_GroupKeywordIsUnrecognizedNotSilentlySwallowed) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("group");
    EXPECT_FALSE(r.recognized);
    EXPECT_EQ(r.registry, "none");
    EXPECT_FALSE(r.queued);
}

// The source-text half, the T363 shape: processCommand() is private and its
// constructor spawns a stdin thread, so the interceptions are pinned as text.
TEST(HeadlessCommandRefusalTest, T365_ConsoleHandlerNoLongerInterceptsTheThreeUIToggles) {
    std::ifstream in(CONSOLE_INPUT_SOURCE);
    ASSERT_TRUE(in.is_open()) << CONSOLE_INPUT_SOURCE;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string src = buffer.str();
    EXPECT_EQ(src.find("InputAction::ToggleInventory"), std::string::npos)
        << "ConsoleInputHandler still sets the dead ToggleInventory action";
    EXPECT_EQ(src.find("InputAction::ToggleGroup"), std::string::npos)
        << "ConsoleInputHandler still sets the dead ToggleGroup action";
    EXPECT_NE(src.find("m_rawCommands.push(\"inventory\")"), std::string::npos)
        << "`inv` must be rewritten to the primary name, not passed through "
           "to registry 2's /invite alias";
}

TEST(HeadlessCommandRefusalTest, RendererAliasWithArgsIsRefusedByName) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("wq high");
    EXPECT_TRUE(r.recognized);
    EXPECT_EQ(r.registry, "renderer");
    EXPECT_FALSE(r.queued);
    EXPECT_NE(r.message.find("/wq requires graphics mode"), std::string::npos) << r.message;
}

TEST(HeadlessCommandRefusalTest, UnknownNameIsUnrecognizedNotQueued) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("/definitelynotacommand");
    EXPECT_FALSE(r.recognized);
    EXPECT_EQ(r.registry, "none");
    EXPECT_FALSE(r.queued);
}

// ---------------------------------------------------------------------------
// T238: a --script command step held pre-connection must announce itself
// ONCE as "CMD outcome=held" (the same line T30 prints for a held piped-stdin
// command), not stay silent (T233's review/T233/script_run_no_server.log:
// a no-server tour sat at step 1 forever with nothing on stdout, so "held"
// and "hung" were indistinguishable). Once connected the step dispatches
// normally and no held line is printed.
// ---------------------------------------------------------------------------

namespace eqt {
struct ApplicationScriptTestPeer {
    static void loadSteps(Application& app, const std::vector<std::string>& lines) {
        for (const auto& l : lines) {
            ScriptStep step;
            step.isWait = false;
            step.commandText = l;
            app.m_scriptSteps.push_back(step);
        }
    }
    static void tick(Application& app, int n) {
        for (int i = 0; i < n; ++i) app.updateScript(1.0f / 60.0f);
    }
    static void setConnected(Application& app, bool v) { app.m_fullyConnected = v; }
    static size_t index(const Application& app) { return app.m_scriptIndex; }
};
}  // namespace eqt

namespace {
size_t countOf(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + needle.size())) ++n;
    return n;
}
}  // namespace

TEST(ScriptHeldEchoTest, HeldScriptCommandAnnouncesOnceNotPerTick) {
    Application app;
    eqt::ApplicationScriptTestPeer::loadSteps(app, {"/attack"});
    eqt::ApplicationScriptTestPeer::setConnected(app, false);

    testing::internal::CaptureStdout();
    eqt::ApplicationScriptTestPeer::tick(app, 300);  // ~5s of game ticks
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(countOf(out, "CMD outcome=held"), 1u) << out;
    // Same vocabulary as T30's piped-stdin intercept
    // (Application::processPreConnectionLifecycleCommands in
    // application_commands.cpp since J06: registry=pending,
    // message="waiting for connection").
    EXPECT_NE(out.find("CMD outcome=held registry=pending recognized=false "
                       "message=\"waiting for connection\" cmd=\"/attack\""),
              std::string::npos) << out;
    EXPECT_EQ(eqt::ApplicationScriptTestPeer::index(app), 0u) << "held step must not be consumed";
}

TEST(ScriptHeldEchoTest, ConnectedScriptCommandAdvancesWithoutHeldLine) {
    Application app;
    eqt::ApplicationScriptTestPeer::loadSteps(app, {"/attack"});
    eqt::ApplicationScriptTestPeer::setConnected(app, true);

    testing::internal::CaptureStdout();
    eqt::ApplicationScriptTestPeer::tick(app, 1);
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(countOf(out, "CMD outcome=held"), 0u) << out;
    EXPECT_EQ(countOf(out, "CMD outcome="), 1u) << out;  // dispatched exactly once
    EXPECT_EQ(eqt::ApplicationScriptTestPeer::index(app), 1u);
}

TEST(ScriptHeldEchoTest, HeldThenConnectedAnnouncesOncePerCommand) {
    Application app;
    eqt::ApplicationScriptTestPeer::loadSteps(app, {"/attack", "/sit"});
    eqt::ApplicationScriptTestPeer::setConnected(app, false);

    testing::internal::CaptureStdout();
    eqt::ApplicationScriptTestPeer::tick(app, 50);
    eqt::ApplicationScriptTestPeer::setConnected(app, true);
    eqt::ApplicationScriptTestPeer::tick(app, 1);   // dispatches /attack
    eqt::ApplicationScriptTestPeer::setConnected(app, false);
    eqt::ApplicationScriptTestPeer::tick(app, 50);  // /sit held: a NEW held line
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_EQ(countOf(out, "CMD outcome=held"), 2u) << out;
    EXPECT_NE(out.find("cmd=\"/sit\""), std::string::npos) << out;
    EXPECT_EQ(eqt::ApplicationScriptTestPeer::index(app), 1u);
}
