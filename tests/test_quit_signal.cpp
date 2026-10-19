// B1099 (GitLab #419): quit is reachable from every loading phase.
//
// Two pure halves, no client, no device, no display:
//   - quit_signal.h: SIGINT and SIGTERM latch the CLEAN quit (Application's
//     mainLoop takes the latch -> requestQuit()); a second signal runs the
//     handler installed before (the DRM device's keyboard restore) and then
//     the default disposition;
//   - loading_screen_quit.h: what a loading-screen frame's device pump turns
//     into a quit (ESC, a closed window, the receiver's quit flag), and when
//     the render thread may pump at all.
// The device's own Ctrl+C -> SIGINT chord is pinned in
// test_drm_key_translation (DRMQuitChord).

#include <gtest/gtest.h>

#include "client/graphics/loading_screen_quit.h"
#include "client/quit_signal.h"

#include <csignal>
#include <unistd.h>

namespace {

struct QuitSignals : ::testing::Test {
    void SetUp() override { eqt::ResetQuitSignalsForTest(); }
    void TearDown() override { eqt::ResetQuitSignalsForTest(); }
};

TEST_F(QuitSignals, SigintLatchesTheCleanQuit) {
    eqt::InstallQuitSignalHandlers();
    EXPECT_EQ(eqt::TakeQuitSignal(), 0);
    raise(SIGINT);  // before B1099: the default disposition killed the process
    EXPECT_EQ(eqt::TakeQuitSignal(), SIGINT);
    EXPECT_EQ(eqt::TakeQuitSignal(), 0) << "taken once";
    EXPECT_STREQ(eqt::QuitSignalName(SIGINT), "SIGINT");
}

TEST_F(QuitSignals, SigtermLatchesTheSameWay) {
    eqt::InstallQuitSignalHandlers();
    raise(SIGTERM);
    EXPECT_EQ(eqt::TakeQuitSignal(), SIGTERM);
    EXPECT_STREQ(eqt::QuitSignalName(SIGTERM), "SIGTERM");
}

// The DRM device's restoreKeyboardSignalHandler stand-in: proves the second
// signal reaches the handler that was installed BEFORE ours (the device is
// created before mainLoop installs the quit handler).
void PreviousHandlerExits(int) { _exit(42); }

TEST(QuitSignalsDeathTest, SecondSignalChainsToThePreviousHandler) {
    EXPECT_EXIT(
        {
            eqt::ResetQuitSignalsForTest();
            signal(SIGINT, PreviousHandlerExits);
            eqt::InstallQuitSignalHandlers();
            raise(SIGINT);  // first: latched, the process lives
            if (eqt::TakeQuitSignal() != SIGINT) _exit(1);
            raise(SIGINT);  // second: the previous handler runs
            _exit(2);
        },
        ::testing::ExitedWithCode(42), "");
}

TEST(QuitSignalsDeathTest, SecondSignalWithNoPreviousHandlerDies) {
    EXPECT_EXIT(
        {
            eqt::ResetQuitSignalsForTest();
            signal(SIGTERM, SIG_DFL);
            eqt::InstallQuitSignalHandlers();
            raise(SIGTERM);
            raise(SIGTERM);
            _exit(2);
        },
        ::testing::KilledBySignal(SIGTERM), "");
}

using EQT::Graphics::LoadingScreenQuit;
using EQT::Graphics::loadingScreenQuitDecision;

TEST(LoadingScreenQuitDecision, EscOnTheLoadingScreenQuits) {
    EXPECT_EQ(loadingScreenQuitDecision(true, true, false), LoadingScreenQuit::Escape);
}

TEST(LoadingScreenQuitDecision, AClosedDeviceQuits) {
    EXPECT_EQ(loadingScreenQuitDecision(false, false, false), LoadingScreenQuit::DeviceClosed);
    EXPECT_EQ(loadingScreenQuitDecision(false, true, true), LoadingScreenQuit::DeviceClosed);
}

TEST(LoadingScreenQuitDecision, TheReceiverQuitFlagQuits) {
    EXPECT_EQ(loadingScreenQuitDecision(true, false, true), LoadingScreenQuit::QuitFlag);
}

TEST(LoadingScreenQuitDecision, NothingPressedKeepsLoading) {
    EXPECT_EQ(loadingScreenQuitDecision(true, false, false), LoadingScreenQuit::None);
}

TEST(LoadingScreenQuitDecision, OnlyTheDrmDeviceIsPumpedWhileTheLoaderOwnsGl) {
    EXPECT_TRUE(EQT::Graphics::loadingScreenMayPump(false, false));
    EXPECT_TRUE(EQT::Graphics::loadingScreenMayPump(false, true));
    EXPECT_TRUE(EQT::Graphics::loadingScreenMayPump(true, true));
    EXPECT_FALSE(EQT::Graphics::loadingScreenMayPump(true, false));
}

}  // namespace
