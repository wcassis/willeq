#pragma once

#include "client/mode/game_mode.h"
#include "client/input/null_input_handler.h"
#include "client/output/null_renderer.h"
#include <memory>
#include <functional>
#include <queue>
#include <mutex>

namespace eqt {
namespace mode {

/**
 * AutomatedMode - Mode for fully automated operation.
 *
 * In this mode, there is no user input and no visual/text output.
 * All actions are driven programmatically through the input handler's
 * injection methods or through registered callbacks.
 *
 * Use cases:
 * - Bot automation
 * - Scripted testing
 * - Background farming
 * - Multi-boxing control
 *
 * Actions are injected using the NullInputHandler's injection API:
 *   auto* input = static_cast<NullInputHandler*>(mode->getInputHandler());
 *   input->injectAction(InputAction::ToggleAutoAttack);
 *   input->injectMoveCommand(100, 200, -50);
 */
class AutomatedMode : public IGameMode {
public:
    AutomatedMode();
    ~AutomatedMode() override;

    // ========== IGameMode Interface ==========

    OperatingMode getMode() const override { return OperatingMode::Automated; }

    input::IInputHandler* getInputHandler() override { return &m_inputHandler; }
    output::IOutputRenderer* getRenderer() override { return &m_renderer; }

    bool initialize(state::GameState& state, const ModeConfig& config) override;
    void shutdown() override;
    bool isRunning() const override { return m_running; }
    bool update(float deltaTime) override;

    void requestQuit() override;
    bool isQuitRequested() const override { return m_quitRequested; }

    // ========== Automation API ==========

    /**
     * Callback type for periodic automation updates.
     * Called each frame with delta time.
     * Return false to stop automation.
     */
    using AutomationCallback = std::function<bool(float deltaTime)>;

    /**
     * Set the automation callback.
     * This callback is called each frame to drive automated behavior.
     */
    void setAutomationCallback(AutomationCallback callback) {
        m_automationCallback = callback;
    }

    /**
     * Event callback type for reacting to game events.
     */
    using EventCallback = std::function<void(const state::GameEvent&)>;

    /**
     * Set the event callback.
     * This callback is called for each game event.
     */
    void setEventCallback(EventCallback callback) {
        m_eventCallback = callback;
    }

    /**
     * Get direct access to the null input handler for injection.
     */
    input::NullInputHandler& input() { return m_inputHandler; }

    /**
     * Get the game state (for automation logic).
     */
    state::GameState* getGameState() { return m_gameState; }

    /**
     * Queue a command for execution.
     * Commands are processed in order during update().
     */
    void queueCommand(const std::string& command);

    /**
     * Get the next pending command (consumed by the caller).
     */
    std::optional<std::string> getNextCommand();

    /**
     * Check if there are pending commands.
     */
    bool hasPendingCommands() const;

private:
    void handleEvent(const state::GameEvent& event);

    input::NullInputHandler m_inputHandler;
    output::NullRenderer m_renderer;

    state::GameState* m_gameState = nullptr;
    bool m_running = false;
    bool m_quitRequested = false;

    // Callbacks
    AutomationCallback m_automationCallback;
    EventCallback m_eventCallback;

    // Event subscription
    std::vector<state::ListenerHandle> m_listenerHandles;

    // Command queue
    std::queue<std::string> m_commandQueue;
    mutable std::mutex m_commandMutex;
};

} // namespace mode
} // namespace eqt
