#pragma once

#include "client/action/action_dispatcher.h"
#include "client/input/input_handler.h"
#include <functional>
#include <string>

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace action {

// Forward declaration
class CommandProcessor;

/**
 * InputActionBridge - Connects input handlers to the action dispatcher.
 *
 * This class translates input events (keyboard, mouse, console commands)
 * into game actions. It handles:
 * - Continuous movement from held keys
 * - One-shot actions from key presses
 * - Mouse targeting and camera control
 * - Console command parsing and execution
 *
 * The bridge runs each frame and processes all pending input, converting
 * it to appropriate action dispatcher calls.
 */
class InputActionBridge {
public:
    /**
     * Create an input-to-action bridge.
     * @param state Game state reference for validation
     * @param dispatcher Action dispatcher for executing actions
     */
    InputActionBridge(state::GameState& state, ActionDispatcher& dispatcher);
    ~InputActionBridge();

    /**
     * Set the input handler to read from.
     * @param input Input handler (may be nullptr to disable)
     */
    void setInputHandler(input::IInputHandler* input);

    /**
     * Set the command processor for text commands.
     * @param processor Command processor (may be nullptr)
     */
    void setCommandProcessor(CommandProcessor* processor);

    /**
     * Update the bridge, processing all pending input.
     * Should be called once per frame.
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * Enable/disable the bridge.
     * When disabled, no input is processed.
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // ========== Input Configuration ==========

    /**
     * Set mouse sensitivity for camera control.
     */
    void setMouseSensitivity(float sensitivity) { m_mouseSensitivity = sensitivity; }
    float getMouseSensitivity() const { return m_mouseSensitivity; }

    /**
     * Set whether to invert mouse Y axis.
     */
    void setInvertMouseY(bool invert) { m_invertMouseY = invert; }
    bool getInvertMouseY() const { return m_invertMouseY; }

    /**
     * Set turn speed for keyboard turning (degrees per second).
     */
    void setTurnSpeed(float speed) { m_turnSpeed = speed; }
    float getTurnSpeed() const { return m_turnSpeed; }

    // ========== Callbacks ==========

    /**
     * Callback for when an action is executed.
     * Useful for logging or UI feedback.
     */
    using ActionCallback = std::function<void(const std::string& actionName, const ActionResult& result)>;
    void setActionCallback(ActionCallback callback) { m_actionCallback = std::move(callback); }

private:
    state::GameState& m_state;
    ActionDispatcher& m_dispatcher;
    input::IInputHandler* m_input = nullptr;
    CommandProcessor* m_commandProcessor = nullptr;

    bool m_enabled = true;

    // Configuration
    float m_mouseSensitivity = 1.0f;
    bool m_invertMouseY = false;
    float m_turnSpeed = 180.0f;  // Degrees per second

    // Callback
    ActionCallback m_actionCallback;

    // Movement state tracking
    bool m_movingForward = false;
    bool m_movingBackward = false;
    bool m_strafingLeft = false;
    bool m_strafingRight = false;
    bool m_turningLeft = false;
    bool m_turningRight = false;

    // Processing methods
    void processDiscreteActions();
    void processContinuousInput(float deltaTime);
    void processMouseInput(float deltaTime);
    void processConsoleInput();
    void processChatMessages();
    void processMoveCommands();
    void processSpellCasts();
    void processTargetRequests();
    void processLootRequests();
    void processHotbarRequests();

    // Helper to report action result
    void reportAction(const std::string& name, const ActionResult& result);

    // Update movement state based on input
    void updateMovementState();
};

} // namespace action
} // namespace eqt
