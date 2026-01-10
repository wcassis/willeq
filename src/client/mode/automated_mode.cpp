#include "client/mode/automated_mode.h"
#include "client/state/game_state.h"

namespace eqt {
namespace mode {

AutomatedMode::AutomatedMode() = default;

AutomatedMode::~AutomatedMode() {
    shutdown();
}

bool AutomatedMode::initialize(state::GameState& state, const ModeConfig& config) {
    m_gameState = &state;

    // Initialize renderer (no-op for null renderer)
    output::RendererConfig rendererConfig;
    rendererConfig.eqClientPath = config.eqClientPath;
    if (!m_renderer.initialize(rendererConfig)) {
        return false;
    }

    // Subscribe to all events for automation callback
    m_listenerHandles.push_back(
        state.events().subscribe([this](const state::GameEvent& event) {
            handleEvent(event);
        }));

    m_running = true;
    m_quitRequested = false;

    return true;
}

void AutomatedMode::shutdown() {
    if (m_running) {
        // Unsubscribe from events
        if (m_gameState) {
            for (auto handle : m_listenerHandles) {
                m_gameState->events().unsubscribe(handle);
            }
            m_listenerHandles.clear();
        }

        m_renderer.shutdown();
        m_inputHandler.deactivate();

        m_running = false;
        m_gameState = nullptr;
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

void AutomatedMode::handleEvent(const state::GameEvent& event) {
    // Forward to event callback if set
    if (m_eventCallback) {
        m_eventCallback(event);
    }
}

void AutomatedMode::queueCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    m_commandQueue.push(command);
}

std::optional<std::string> AutomatedMode::getNextCommand() {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    if (m_commandQueue.empty()) {
        return std::nullopt;
    }
    std::string cmd = m_commandQueue.front();
    m_commandQueue.pop();
    return cmd;
}

bool AutomatedMode::hasPendingCommands() const {
    std::lock_guard<std::mutex> lock(m_commandMutex);
    return !m_commandQueue.empty();
}

} // namespace mode
} // namespace eqt
