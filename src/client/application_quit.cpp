// B1099 (GitLab #419): quit is reachable from every loading phase.
//
// Will, 2026-10-19 (the XU4, play.sh against a stopped server): the client sat
// on "Connecting to login server..." and NOTHING could quit it -- the DRM
// device puts the VT keyboard in K_OFF so Ctrl+C never became SIGINT, SIGINT
// had no handler anyway, no key was read while the loading screen was up
// (render() never pumped the device there), and the login connect had no
// bound. "I should be able to ctrl+c or quit the application at any time."
// The four halves:
//   - quit_signal.h: SIGINT and SIGTERM latch, serviceQuitSignals() below
//     turns the latch into requestQuit() on the render thread;
//   - drm_key_translation.h (isQuitChord): the DRM device raises SIGINT for
//     Ctrl+C itself, restoring the VT's own meaning of the chord;
//   - loading_screen_quit.h: pollLoadingScreenQuit() below pumps the device
//     on loading-screen frames and quits on ESC / a closed window;
//   - login_connect_watchdog.h: the pre-zone connect bound;
//     serviceConnectFailure() below decides what a give-up does.

#include "client/application.h"

#include "client/eq.h"
#include "client/login_world/login_world_component.h"
#include "client/quit_signal.h"
#include "common/logging.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#endif

namespace eqt {

void Application::serviceQuitSignals() {
    if (const int sig = TakeQuitSignal()) {
        LOG_INFO(MOD_MAIN, "{} received, requesting clean shutdown (a second one forces the exit)",
                 QuitSignalName(sig));
        requestQuit();
    }
}

void Application::serviceConnectFailure() {
    if (m_connectFailureHandled || !m_eqClient) return;
    if (!m_eqClient->LoginWorld().PreZoneConnectFailed()) return;
    m_connectFailureHandled = true;
    if (m_graphicsInitialized && !scenarioRequested()) {
        // The loading screen already reads "Could not connect to ..."; the
        // person quits with ESC, Ctrl+C or a window close.
        LOG_INFO(MOD_MAIN, "Pre-zone connect failed; the loading screen keeps the message "
                 "up (ESC / Ctrl+C quits)");
        return;
    }
    LOG_INFO(MOD_MAIN, "Pre-zone connect failed; quitting (no screen to keep the message on)");
    requestQuit();
}

#ifdef EQT_HAS_GRAPHICS
void Application::pollLoadingScreenQuit(bool loaderOwnsContext) {
    if (!m_renderer) return;
    const auto q = m_renderer->pollLoadingScreenQuit(loaderOwnsContext);
    if (q == EQT::Graphics::LoadingScreenQuit::None) return;
    LOG_INFO(MOD_MAIN, "Quit from the loading screen ({}), requesting clean shutdown",
             EQT::Graphics::loadingScreenQuitName(q));
    requestQuit();
}
#endif

}  // namespace eqt
