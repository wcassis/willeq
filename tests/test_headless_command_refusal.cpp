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

using eqt::Application;
using eqt::CommandDispatchResult;

TEST(HeadlessCommandRefusalTest, KnownRendererCommandIsRefusedNotQueued) {
    Application app;
    CommandDispatchResult r = app.DispatchSlashCommand("/newskills");
    EXPECT_TRUE(r.recognized);
    EXPECT_EQ(r.registry, "renderer");
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.queued);
    EXPECT_NE(r.message.find("requires graphics mode"), std::string::npos) << r.message;
    EXPECT_NE(r.message.find("/newskills"), std::string::npos) << r.message;
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
