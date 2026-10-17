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
// LIFECYCLE (B1091, issue #411): the test spawns its OWN Xvfb on a PER-RUN
// UNIQUE display -- `Xvfb -displayfd <fd>` lets the server itself pick the
// first free number and write it back over a socketpair, so NO fixed display
// number is ever used and two concurrent runs of the test (two ctest slots, or
// a second run on the same host) cannot collide. B654_XVFB_DISPLAY still
// overrides the number for an operator who wants one; in that case the test
// REFUSES to run if that display's socket or lock file already exists, so a
// squatted display FAILS loudly. The pre-B1091 defect this replaces: the old
// code returned a FIXED :97; a second Xvfb then failed to bind while
// xvfbReady() still answered from the ORPHANED server, and the test talked to
// a stale display and could block forever -- that is what hung pipeline 1453's
// build-test on the ThinkPad.
//
// The Xvfb child is killed and reaped on EVERY exit path: a normal return and
// a gtest ASSERT/FAIL both go through the RAII XvfbHandle destructor; SIGTERM/
// SIGINT to the test go through the installed handlers (which signal the child
// and _exit); and a SIGKILL of the test -- ctest's timeout kill, the operator,
// the OOM-killer -- goes through the child's PR_SET_PDEATHSIG(SIGKILL), so the
// kernel takes Xvfb with its parent. Every test-side wait carries a deadline
// (display report, Xvfb ready, window appearance, the event pump), so a dead
// display FAILs within seconds; the ctest TIMEOUT in tests/cmake/graphics is
// the backstop, not the primary bound. It SKIPs -- loudly, with a reason, not
// silently -- only when the Xvfb or xdotool BINARY is missing (genuinely absent
// from the host). Everything else (display allocation, device creation, window
// map, key delivery) failing is a hard failure, not a skip: a broken device
// path is exactly the defect this test exists to catch.
//
// The assertion is on the RESOLVED HotkeyAction (the card's "the action fires"),
// which is strictly stronger than grepping a stdout line: it asserts the exact
// enum the shipped HotkeyManager returns for the exact key the device
// delivered. A [B654-E2E] line is still emitted per press as the run-log
// evidence.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>    // kill, sigaction
#include <unistd.h>    // fork, _exit, close, read, access
#include <sys/wait.h>  // waitpid
#include <sys/prctl.h> // PR_SET_PDEATHSIG: a SIGKILLed test cannot orphan Xvfb
#include <sys/socket.h> // socketpair: the -displayfd handshake
#include <sys/select.h> // select: deadline-bounded displayfd reads
#include <sys/time.h>  // struct timeval
#include <errno.h>
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

// Is an executable on PATH? (Used to SKIP loudly rather than fail when the
// X11 tooling genuinely is not installed.)
bool whichExists(const char* name)
{
    std::string cmd = std::string("command -v ") + name + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

// B1091: the per-run display allocation. Default: a socketpair; Xvfb is told
// to pick the first free number itself with `-displayfd <child end>` and to
// write that number to the fd, and the test reads it back after the spawn --
// no fixed number, no probe race, no collision between concurrent runs. When
// the operator sets B654_XVFB_DISPLAY, that number is used verbatim and
// displayfdReadFd stays -1 (no handshake; the readiness poll is the check).
struct DisplayAlloc {
    std::string number;     // operator's number, or "" until Xvfb reports one
    int displayfdReadFd = -1; // parent end of the -displayfd socketpair (-1: no
    // handshake; -2: socketpair failed)
    int displayfdChildFd = -1; // child end, handed to Xvfb at spawn time

    void prepare()
    {
        const char* env = std::getenv("B654_XVFB_DISPLAY");
        if (env && *env) {
            number = env;
            return;
        }
        int pair[2] = {-1, -1};
        if (socketpair(AF_UNIX, SOCK_STREAM, /*proto=*/0, pair) != 0) {
            displayfdReadFd = -2;
            return;
        }
        displayfdReadFd = pair[0];
        displayfdChildFd = pair[1];
    }

    // Wait (deadline-bounded) for the number Xvfb wrote, then parse the
    // leading digits. Xvfb keeps the fd open after writing and never closes
    // it itself, so the loop takes the first arrival (the write is one small
    // chunk -- the two-digit number plus a newline) and parses the digits.
    // The PARENT'S END IS LEFT OPEN on return: the handle owns it, and
    // closing it is part of Xvfb's shutdown.
    bool waitForReportedNumber(long timeoutMs)
    {
        std::string out;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeoutMs);
        for (;;) {
            if (std::chrono::steady_clock::now() >= deadline) break;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(displayfdReadFd, &rfds);
            struct timeval tv{0, 200 * 1000};
            int r = select(displayfdReadFd + 1, &rfds, nullptr, nullptr, &tv);
            if (r > 0) {
                char buf[16];
                ssize_t n = read(displayfdReadFd, buf, sizeof(buf));
                if (n <= 0) return false; // the server closed the fd
                out.append(buf, static_cast<size_t>(n));
                // One more quick look: a short number may arrive in two reads.
                break;
            } else if (r < 0 && errno != EINTR) {
                return false;
            }
        }
        if (out.empty()) return false;
        size_t i = out.find_first_not_of(" \r\n");
        if (i == std::string::npos) return false;
        number.clear();
        for (size_t j = i; j < out.size(); ++j) {
            if (out[j] < '0' || out[j] > '9') break;
            number.push_back(out[j]);
        }
        return !number.empty();
    }
};

// B1091: the guaranteed-cleanup guard. SIGTERMs the Xvfb child, waits up to 3 s,
// SIGKILLs and reaps if it is still there. Runs from the destructor on every
// return path of the test body, gtest ASSERT/FAIL included (a failed
// assertion unwinds the test frame and the handle is destroyed with it).
//
// A note on PDEATHSIG: it is set in the child before exec, and the forking
// thread is the test's main thread, so "the parent" is this process. Linux
// delivers the signal when THAT thread exits -- a normal return, the handler's
// _exit, a crash, or a SIGKILL of the whole process group (ctest's timeout
// kill, the OOM-killer) all cover it.
struct XvfbHandle {
    pid_t pid = 0;
    int displayfdFd = -1; // the parent's end of the displayfd pair; keeping it
    // open is part of Xvfb's lifetime (it exits when it closes)

    explicit XvfbHandle(int p, int fd) : pid(p > 0 ? p : 0), displayfdFd(fd) {}
    XvfbHandle(const XvfbHandle&) = delete;
    XvfbHandle& operator=(const XvfbHandle&) = delete;
    ~XvfbHandle() { killXvfb(); }

    void killXvfb()
    {
        if (displayfdFd >= 0) {
            ::close(displayfdFd); // signals Xvfb too; the SIGTERM is the main
            displayfdFd = -1;     // ask (the fd close only ends its event loop)
        }
        if (pid <= 0) return;
        ::kill(pid, SIGTERM);
        for (int i = 0; i < 30; ++i) {
            int status = 0;
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) return; // reaped
            if (r < 0) {
                if (errno == ECHILD) return; // already gone
                if (errno != EINTR) break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        ::kill(pid, SIGKILL);
        int status = 0;
        (void)waitpid(pid, &status, 0);
        pid = 0;
    }
};

// B1091: a SIGKILLed test process cannot be caught by a handler, so the
// ORPHAN-SAFETY for that case lives in the child: prctl(PR_SET_PDEATHSIG,
// SIGKILL) makes the kernel kill Xvfb when its parent dies -- ctest's timeout
// kill, the operator's kill -9, the OOM-killer, all covered.
int spawnXvfb(DisplayAlloc& alloc)
{
    // The child end of the displayfd pair (valid only in the handshake case);
    // close the parent's copy before fork so the child's Xvfb sees exactly
    // the one fd it was promised.
    int childFd = alloc.displayfdChildFd;
    if (childFd >= 0) {
        alloc.displayfdChildFd = -1;
    } else {
        childFd = -1;
    }

    std::vector<std::string> argv = {
        "Xvfb",
        childFd >= 0 ? "-displayfd" : (":" + alloc.number),
        "-screen", "0", "800x600x24", "+render", "-nolisten", "tcp",
    };
    if (childFd >= 0) argv.insert(argv.begin() + 2, std::to_string(childFd));

    std::vector<char*> argvp;
    argvp.reserve(argv.size() + 1);
    for (auto& s : argv) argvp.push_back(const_cast<char*>(s.c_str()));
    argvp.push_back(nullptr);

    int pid = fork();
    int readFd = alloc.displayfdReadFd; // the parent's end; -1 in override mode
    if (pid < 0) return 0;
    if (pid == 0) {
        // Child. Set PDEATHSIG before exec: if the test process dies by ANY
        // means (the handler's _exit, a crash, SIGKILL, ctest timeout), the
        // kernel delivers SIGKILL to this Xvfb.
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) _exit(127);
        // Close the parent's end of the displayfd pair so Xvfb sees exactly
        // the one fd it was promised, and so the parent's read() sees EOF
        // once Xvfb exits.
        if (readFd >= 0) ::close(readFd);
        execvp("Xvfb", argvp.data());
        _exit(127);
    }
    // Parent: the child's copy of the displayfd is not ours to keep.
    if (childFd >= 0) ::close(childFd);
    return pid;
}

// Wait for a display to accept connections (poll via a trivial xdotool
// getdisplaygeometry; returns true when the display answers). Deadline:
// 62 x 100 ms = 6.2 s, then the caller FAILs.
bool xvfbReady(const std::string& display)
{
    std::string cmd = "DISPLAY=:" + display +
        " xdotool getdisplaygeometry >/dev/null 2>&1";
    for (int i = 0; i < 62; ++i) {
        if (std::system(cmd.c_str()) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// B1091: the SIGTERM/SIGINT guard. The handler signals the Xvfb child and
// _exits -- the async-signal-safe way out of a long gtest run -- and PDEATHSIG
// covers the rest. The previous dispositions are restored on the way out.
std::atomic<XvfbHandle*> g_handle{nullptr};

void xvfbSignalHandler(int)
{
    XvfbHandle* h = g_handle.load(std::memory_order_relaxed);
    if (h) {
        int p = h->pid;
        if (p > 0) ::kill(p, SIGKILL); // fire-and-forget; PDEATHSIG covers it
    }
    _exit(124);
}

struct SignalGuard {
    bool installed = false;
    struct sigaction prevTerm = {};
    struct sigaction prevInt = {};
    void install()
    {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_handler = xvfbSignalHandler;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGTERM, &sa, &prevTerm) != 0) return;
        if (sigaction(SIGINT, &sa, &prevInt) != 0) {
            sigaction(SIGTERM, &prevTerm, nullptr); // roll the first one back
            return;
        }
        installed = true;
    }
    ~SignalGuard()
    {
        if (installed) {
            // Restore whatever was installed before the test (normally the
            // default dispositions for a plain test binary).
            sigaction(SIGTERM, &prevTerm, nullptr);
            sigaction(SIGINT, &prevInt, nullptr);
        }
    }
};

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

// Is the given display number already held on this host? (The socket in
// /tmp/.X11-unix or the lock file /tmp/.X<n>-lock is enough -- the holder is
// an X server of any kind.)
bool displaySquatted(const std::string& display)
{
    std::string sock = "/tmp/.X11-unix/X" + display;
    std::string lock = "/tmp/.X" + display + "-lock";
    return ::access(sock.c_str(), F_OK) == 0 || ::access(lock.c_str(), F_OK) == 0;
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

    // B1091: the per-run display.
    DisplayAlloc alloc;
    alloc.prepare();
    if (alloc.displayfdReadFd == -2) {
        FAIL() << "socketpair() failed (errno=" << errno
               << ") -- cannot set up the Xvfb -displayfd handshake.";
    }
    if (!alloc.number.empty() && displaySquatted(alloc.number)) {
        // Explicit operator override onto a display someone else holds:
        // refuse loudly. The pre-B1091 defect was the opposite -- talk to the
        // OTHER server and hang.
        FAIL() << "B654_XVFB_DISPLAY=" << alloc.number
               << " is already in use on this host (the socket or lock file "
                  "exists). Clear it or run without the override.";
    }

    // HotkeyManager must resolve the shipped defaults (CameraZoomIn<-NUMPAD7,
    // CameraZoomOut<-NUMPAD1). Reset to defaults so the test does not depend
    // on any stale config file in the working directory.
    auto& hotkeys = eqt::input::HotkeyManager::instance();
    hotkeys.resetToDefaults();

    SignalGuard sigGuard;
    sigGuard.install();

    int pid = spawnXvfb(alloc);
    // RAII: reaps on every exit path below. The parent's displayfd end stays
    // open for Xvfb's lifetime and is closed in the handle's cleanup.
    XvfbHandle handle(pid, alloc.displayfdReadFd);
    if (pid == 0) {
        FAIL() << "failed to fork Xvfb (errno=" << errno << ")";
    }
    g_handle.store(&handle, std::memory_order_relaxed);

    // The display number: Xvfb itself reports it in the -displayfd case
    // (deadline 5 s); in the override case it is the operator's.
    if (alloc.displayfdReadFd >= 0) {
        ASSERT_TRUE(alloc.waitForReportedNumber(5000))
            << "Xvfb pid=" << pid << " did not report a display number over "
               "-displayfd within 5s (it may have failed to start; check "
                  "whether Xvfb is healthy on this host).";
    }
    const std::string& display = alloc.number;
    std::string disp = ":" + display;
    std::cerr << "[B654-E2E] Xvfb pid=" << pid << " on private display "
              << disp << std::endl;

    // Wait for the display to accept connections (deadline inside xvfbReady:
    // 6.2 s). A squatted display now FAILs here instead of talking to the
    // other server.
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
    // real run() -> pollX11Events() until both actions are observed (or the
    // 8 s deadline: a dead display FAILs below, it cannot hang -- B1091).
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
    unsetenv("WILLEQ_EGL_X11");
    g_handle.store(nullptr, std::memory_order_relaxed);
    // XvfbHandle's destructor reaps the child; sigGuard restores the signal
    // dispositions. Both run on the normal return path, and the handle's also
    // after a failed assertion unwinds the test frame.
}
