// B654 (issue #56): end-to-end regression that a numpad-digit press under
// --egl-x11 actually FIRES its camera action, through the REAL
// CIrrDeviceFB::run() -> pollX11Events() -> x11KeysymToIrrlicht() ->
// HotkeyManager::getAction() chain.
//
// This is the display-side companion to test_x11_keysym_map.cpp. That test pins
// the map in isolation (display-less); this one proves the whole path: a real
// Xvfb, a real CIrrDeviceFB created via createDeviceEx (EDT_OGLES2, the
// WILLEQ_EGL_X11 opt-in the --egl-x11 flag sets), a real xdotool keypress, and
// the resulting EKEY_CODE resolved through the shipped HotkeyManager so that
// KP_7 -> KEY_NUMPAD7 -> CameraZoomIn and KP_1 -> KEY_NUMPAD1 -> CameraZoomOut.
//
// Before the fix, pollX11Events() looked up the keysym at index 0, which on
// Xvfb's default keymap is the NAVIGATION alias (keypad-7 -> XK_KP_Home) in
// both NumLock states; the old map dropped it (-> EKEY_CODE 0), so no action
// fired. This test was RED: the device delivered EKEY_CODE 0 for KP_7/KP_1 and
// getAction resolved to nothing. After the fix it is GREEN: the device delivers
// KEY_NUMPAD7/KEY_NUMPAD1 and the actions fire.
//
// LIFECYCLE: the test spawns its OWN Xvfb on a private display (default :97,
// override with B654_XVFB_DISPLAY), so it is independent of any pre-existing
// $DISPLAY and runs the same way on this host and on CI (hermes-ext). It
// SKIPs -- loudly, with a reason, not silently -- only when the Xvfb or
// xdotool BINARY is missing (genuinely absent from the host). Everything else
// (device creation, window map, key delivery) failing is a hard failure, not a
// skip: a broken device path is exactly the defect this test exists to catch.
//
// The assertion is on the RESOLVED HotkeyAction (the card's "the action fires"),
// which is strictly stronger than grepping a stdout line: it asserts the exact
// enum the shipped HotkeyManager returns for the exact key the device
// delivered. A [B654-E2E] line is still emitted per press as the run-log
// evidence.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>    // kill, SIGTERM
#include <unistd.h>    // fork, execlp
#include <sys/wait.h>  // waitpid
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>

#include <irrlicht.h>
#include <IEventReceiver.h>

#include "client/input/hotkey_manager.h"

using namespace irr;

namespace {

// Resolve the Xvfb display number. Fixed default :97 to avoid the persistent
// :99 used by the dev VM; overridable so a host with :97 busy can point
// elsewhere.
std::string xvfbDisplay()
{
    const char* env = std::getenv("B654_XVFB_DISPLAY");
    if (env && *env) return env;
    return "97";
}

// Is an executable on PATH? (Used to SKIP loudly rather than fail when the
// X11 tooling genuinely is not installed.)
bool whichExists(const char* name)
{
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// Spawn a private Xvfb on the given display; return its pid (0 on failure).
int spawnXvfb(const std::string& display)
{
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // child
        std::string d = ":" + display;
        execlp("Xvfb", "Xvfb", d.c_str(),
               "-screen", "0", "800x600x24", "+render", "-nolisten", "tcp",
               (char*)nullptr);
        _exit(127);
    }
    return pid;
}

// The event receiver: records every key press with the EKEY_CODE the device
// delivered and the HotkeyAction the shipped HotkeyManager resolves for it.
struct NumpadReceiver : public IEventReceiver {
    struct Hit { EKEY_CODE key; bool pressed; std::string actionName; };
    std::vector<Hit> hits;
    std::atomic<int> zoomInPresses{0};
    std::atomic<int> zoomOutPresses{0};

    bool OnEvent(const SEvent& e) override {
        if (e.EventType != EET_KEY_INPUT_EVENT) return false;
        if (!e.KeyInput.PressedDown) return false;
        Hit h;
        h.key = e.KeyInput.Key;
        h.pressed = true;
        auto action = eqt::input::HotkeyManager::instance().getAction(
            e.KeyInput.Key, e.KeyInput.Control, e.KeyInput.Shift,
            /*alt=*/false, eqt::input::HotkeyMode::Player);
        h.actionName = action.has_value()
            ? eqt::input::HotkeyManager::actionEnumToName(*action)
            : std::string("(none)");
        if (action == eqt::input::HotkeyAction::CameraZoomIn) zoomInPresses++;
        if (action == eqt::input::HotkeyAction::CameraZoomOut) zoomOutPresses++;
        hits.push_back(h);
        std::cerr << "[B654-E2E] press key=" << h.key
                  << " action=" << h.actionName << std::endl;
        return false;
    }
};

// Wait for the Xvfb to accept connections (poll the socket via a trivial
// xdotool getdisplaygeometry; returns true when the display answers).
bool xvfbReady(const std::string& display)
{
    std::string cmd = "DISPLAY=:" + display + " xdotool getdisplaygeometry >/dev/null 2>&1";
    for (int i = 0; i < 30; ++i) {
        if (std::system(cmd.c_str()) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// Find the device's window id ("WillEQ", set by initEGLX11()) via xdotool.
std::string findWindowId(const std::string& display)
{
    std::string cmd = "DISPLAY=:" + display +
        " xdotool search --name WillEQ 2>/dev/null | head -1";
    std::vector<char> buf(128, 0);
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        while (std::fgets(buf.data(), buf.size(), p)) {
            out += buf.data();
        }
        pclose(p);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

void pressKey(const std::string& display, const std::string& wid, const char* keyName)
{
    std::string cmd = "DISPLAY=:" + display + " xdotool key --window " + wid + " " + keyName;
    // xdotool's exit code is not load-bearing here (the receiver counts the
    // presses); swallow warn_unused_result with a real branch, not a cast.
    if (std::system(cmd.c_str()) != 0) {}
}

} // namespace

// KP_7 (zoom in) and KP_1 (zoom out) must fire their camera actions through the
// real device + hotkey path. This is THE regression for B654.
TEST(X11NumpadE2E, NumpadDigitFiresCameraAction)
{
    // SKIP loudly only when the X11 tooling genuinely is not installed.
    if (!whichExists("Xvfb")) {
        GTEST_SKIP() << "Xvfb not installed; cannot run the --egl-x11 numpad "
                        "end-to-end test. Install xvfb to enable it.";
    }
    if (!whichExists("xdotool")) {
        GTEST_SKIP() << "xdotool not installed; cannot synthesize the numpad "
                        "keypresses. Install xdotool to enable it.";
    }

    const std::string display = xvfbDisplay();
    std::string disp = ":" + display;

    // HotkeyManager must resolve the shipped defaults (CameraZoomIn<-NUMPAD7,
    // CameraZoomOut<-NUMPAD1). Reset to defaults so the test does not depend
    // on any stale config file in the working directory.
    auto& hotkeys = eqt::input::HotkeyManager::instance();
    hotkeys.resetToDefaults();

    int xvfbPid = spawnXvfb(display);
    ASSERT_GE(xvfbPid, 0) << "failed to fork Xvfb";

    // Give Xvfb a moment to accept connections.
    ASSERT_TRUE(xvfbReady(display))
        << "Xvfb on " << disp << " did not become ready in 6s";

    // Opt into the X11-windowed EGL backend (what the --egl-x11 flag sets) and
    // point at our private display, BEFORE the device is created.
    setenv("WILLEQ_EGL_X11", "1", 1);
    setenv("DISPLAY", disp.c_str(), 1);

    SIrrlichtCreationParameters p;
    p.DriverType = video::EDT_OGLES2;
    p.DeviceType = EIDT_BEST; // resolves to CIrrDeviceFB in the patched build
    p.WindowSize = core::dimension2d<u32>(640, 480);
    p.Fullscreen = false;
    p.Stencilbuffer = true;
    p.Vsync = false;
    p.AntiAlias = 0;

    IrrlichtDevice* dev = createDeviceEx(p);
    if (!dev) {
        // A hard failure, not a skip: the device path is exactly what we test.
        FAIL() << "createDeviceEx(EDT_OGLES2, EIDT_BEST) returned null under "
                  "WILLEQ_EGL_X11 on " << disp
               << " -- the --egl-x11 device path failed to initialise (this is "
                  "the defect under test, not a missing dependency).";
    }
    SCOPED_TRACE("CIrrDeviceFB created on " + disp);

    NumpadReceiver recv;
    dev->setEventReceiver(&recv);

    // The device window is created and mapped synchronously in
    // initEGLX11(); poll xdotool for it (map/propagation can lag a beat).
    std::string wid;
    for (int i = 0; i < 40 && wid.empty(); ++i) {
        wid = findWindowId(display);
        if (wid.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    ASSERT_FALSE(wid.empty())
        << "could not find the device's X11 window (name 'WillEQ') on " << disp;

    // Send the two numpad digit presses (the card's KP_7 / KP_1) and pump the
    // real run() -> pollX11Events() until both actions are observed (or timeout).
    pressKey(display, wid, "KP_7");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    pressKey(display, wid, "KP_1");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline &&
           (recv.zoomInPresses.load() < 1 || recv.zoomOutPresses.load() < 1)) {
        if (!dev->run()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Drain any final pending events.
    for (int i = 0; i < 20 && dev->run(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::cerr << "[B654-E2E] total presses recorded=" << recv.hits.size()
              << " CameraZoomIn presses=" << recv.zoomInPresses.load()
              << " CameraZoomOut presses=" << recv.zoomOutPresses.load()
              << std::endl;

    // THE assertions: the numpad-digit presses fired their camera actions.
    EXPECT_GE(recv.zoomInPresses.load(), 1)
        << "KP_7 press did not fire CameraZoomIn (device delivered no usable "
           "key). B654: the index-0 keysym is the navigation alias; the map "
           "must resolve it to KEY_NUMPAD7 so the Numpad7 -> CameraZoomIn "
           "binding fires.";
    EXPECT_GE(recv.zoomOutPresses.load(), 1)
        << "KP_1 press did not fire CameraZoomOut (device delivered no usable "
           "key). B654: the index-0 keysym is the navigation alias; the map "
           "must resolve it to KEY_NUMPAD1 so the Numpad1 -> CameraZoomOut "
           "binding fires.";

    dev->drop();
    if (xvfbPid > 0) { kill(xvfbPid, SIGTERM); waitpid(xvfbPid, nullptr, 0); }
    unsetenv("WILLEQ_EGL_X11");
}
