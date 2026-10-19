#pragma once

// B1099 (GitLab #419): SIGINT and SIGTERM both request the CLEAN quit.
//
// P36 gave SIGTERM a flag handler (application.cpp) but SIGINT kept its
// default disposition, and on the DRM device both were then overwritten by the
// device's keyboard-restore handler (docker/irrlicht-drm/CIrrDeviceFB.cpp,
// restoreKeyboardSignalHandler: restore K_OFF -> the saved mode, re-raise with
// SIG_DFL), because the device is created in initGraphics() AFTER
// initSignalHandlers() ran. So on the XU4 a SIGTERM killed the process without
// Application::shutdown(), and a SIGINT did the same everywhere.
//
// InstallQuitSignalHandlers() runs at the top of Application::mainLoop(), after
// the device exists, and installs one handler for both signals:
//   - the FIRST signal only latches (async-signal-safe); mainLoop() takes it
//     with TakeQuitSignal() and calls Application::requestQuit() from ordinary
//     code -- the path /camp, window-close and SIGTERM already use;
//   - a SECOND signal is the way out of a clean shutdown that does not finish:
//     it calls the handler that was installed before (the device's
//     keyboard-restore handler on DRM, so the VT keyboard comes back), then
//     re-raises with the default disposition.
// The DRM device's Ctrl+C (eqt_drm::isQuitChord, drm_key_translation.h) raises
// SIGINT, so a keyboard Ctrl+C under K_OFF lands on this same path.

namespace eqt {

/** Install the SIGINT/SIGTERM quit handler (see above). Idempotent. */
void InstallQuitSignalHandlers();

/** The latched quit signal (SIGINT / SIGTERM), or 0; clears the latch. */
int TakeQuitSignal();

/** "SIGINT" / "SIGTERM" / "signal". */
const char* QuitSignalName(int sig);

/** Tests only: put the previous dispositions back and clear all state. */
void ResetQuitSignalsForTest();

}  // namespace eqt
