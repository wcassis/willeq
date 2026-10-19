#pragma once

// B1099 (GitLab #419): the loading screen's quit decision, pure.
//
// While a loading screen is up, Application::render() draws it INSTEAD of
// IrrlichtRenderer::processFrame(), and processFrame() was the only caller of
// device->run() -- which on the DRM device is the only place evdev is read.
// So no key, not even a window close, reached the client during
// "Connecting to login server..." (Will, 2026-10-19, the XU4). The renderer
// now pumps the device on those frames (IrrlichtRenderer::
// pollLoadingScreenQuit, renderer_pre_zone.cpp) and this function turns what
// the pump saw into a quit: the device closed (window close), ESC, or the
// event receiver's own quit flag. Ctrl+C needs no case here: the DRM device
// raises SIGINT for it (eqt_drm::isQuitChord), which the quit-signal path
// (quit_signal.h) takes on every frame, loading screen or not.

namespace EQT {
namespace Graphics {

enum class LoadingScreenQuit { None, DeviceClosed, Escape, QuitFlag };

inline LoadingScreenQuit loadingScreenQuitDecision(bool deviceRunning, bool escapePressed,
                                                   bool quitFlag) {
    if (!deviceRunning) return LoadingScreenQuit::DeviceClosed;
    if (escapePressed) return LoadingScreenQuit::Escape;
    if (quitFlag) return LoadingScreenQuit::QuitFlag;
    return LoadingScreenQuit::None;
}

// While the zone loading thread owns the GL context, only the DRM device may
// be pumped from the render thread: its run() is a timer tick plus evdev
// reads, no GL or windowing call. The X11 device's run() talks to the Display
// the loader's EGL surface uses, so it waits for the loader (a terminal
// Ctrl+C / SIGTERM still works there: quit_signal.h).
inline bool loadingScreenMayPump(bool loaderOwnsContext, bool drmDevice) {
    return !loaderOwnsContext || drmDevice;
}

inline const char* loadingScreenQuitName(LoadingScreenQuit q) {
    switch (q) {
    case LoadingScreenQuit::DeviceClosed: return "window closed";
    case LoadingScreenQuit::Escape: return "ESC";
    case LoadingScreenQuit::QuitFlag: return "quit flag";
    default: return "none";
    }
}

}  // namespace Graphics
}  // namespace EQT
