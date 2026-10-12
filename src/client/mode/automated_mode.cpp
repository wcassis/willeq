#include "client/mode/automated_mode.h"
#include "client/state/game_state.h"

namespace eqt {
namespace mode {

AutomatedMode::AutomatedMode() = default;

AutomatedMode::~AutomatedMode() {
    shutdown();
}

bool AutomatedMode::initialize(state::GameState& state, const ModeConfig& config) {
    // Initialize renderer (no-op for null renderer)
    output::RendererConfig rendererConfig;
    rendererConfig.eqClientPath = config.eqClientPath;
    if (!m_renderer.initialize(rendererConfig)) {
        return false;
    }

    m_running = true;
    m_quitRequested = false;

    return true;
}

void AutomatedMode::shutdown() {
    if (m_running) {
        m_renderer.shutdown();
        m_inputHandler.deactivate();

        m_running = false;
    }
}

bool AutomatedMode::update(float deltaTime) {
    if (!m_running || m_quitRequested) {
        return false;
    }

    // Update input handler (processes injected actions)
    m_inputHandler.update();

    // Check for quit action
    if (m_inputHandler.consumeAction(input::InputAction::Quit)) {
        m_quitRequested = true;
        return false;
    }

    // Call automation callback if set
    if (m_automationCallback) {
        if (!m_automationCallback(deltaTime)) {
            m_quitRequested = true;
            return false;
        }
    }

    // Update renderer (no-op for null renderer)
    if (!m_renderer.update(deltaTime)) {
        return false;
    }

    return true;
}

void AutomatedMode::requestQuit() {
    m_quitRequested = true;
    m_inputHandler.injectAction(input::InputAction::Quit);
}

} // namespace mode
} // namespace eqt
