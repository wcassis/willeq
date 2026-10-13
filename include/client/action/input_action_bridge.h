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

// T174: the standalone hotbar data model (client/hotbar/hotbar_model.h). The
// bridge only reads it, so a forward declaration and a pointer member keep the
// json/json.h weight that header pulls in out of every translation unit that
// includes this one.
class HotbarModel;

namespace action {

// Forward declaration
class CommandProcessor;

/**
 * InputActionBridge - Connects input handlers to the action dispatcher.
 *
 * This class translates input events (keyboard, console commands) into game
 * actions. It handles:
 * - One-shot actions from key presses
 * - Console command parsing and execution
 * - Typed requests (chat, move, spell, target, loot, hotbar)
 *
 * Movement, keyboard turning and mouse-look are NOT bridge territory: the
 * renderer owns them (collision, PlayerPositionChanged intents). T228
 * deleted the dead bridge-side copies.
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
     * T29: an independent source of raw console commands, drained every
     * update() regardless of which handler setInputHandler() currently
     * points at.
     *
     * Why this exists: setInputHandler() is swapped when graphics comes up
     * (ConsoleInputHandler -> GraphicsInputHandler, see application.cpp),
     * so movement/mouse start coming from evdev/X11 instead of stdin. But
     * ConsoleInputHandler's stdin-reading thread never stops -- it's still
     * the ONE stdin reader for the process's whole lifetime (see
     * console_input_handler.cpp) -- it just stops being drained once it's
     * no longer "the" active input handler. Wiring the same
     * ConsoleInputHandler instance here too means its raw commands (piped
     * slash commands, in particular) keep reaching the app after graphics
     * initializes, without adding a second stdin reader.
     * @param source Console command source (may be nullptr to disable)
     */
    void setConsoleCommandSource(input::IInputHandler* source) { m_consoleCommandSource = source; }

    /**
     * T174: give the bridge read access to the one hotbar model the rest of
     * the client uses (owned by EverQuest, see EverQuest::GetHotbarModel()).
     * Without it, processHotbarRequests() has no slot to activate.
     * Injected by setter to match the other collaborators
     * (input handler, command processor, console source) rather than the
     * constructor, since the model is created after the bridge.
     * @param model Hotbar model (may be nullptr; the bridge only reads it)
     */
    void setHotbarModel(const HotbarModel* model) { m_hotbarModel = model; }

    /**
     * T29: unified slash-command dispatcher, used for raw commands from
     * setConsoleCommandSource() instead of going straight to
     * setCommandProcessor()'s CommandProcessor. When set, this is expected
     * to try the full dispatch order documented in
     * refactor/T29_unified_command_channel.md (EverQuest's own registry,
     * then CommandProcessor, then the renderer command bridge) and print
     * a result a script can synchronise on. Falls back to calling
     * m_commandProcessor directly (legacy behavior) if unset.
     */
    using UnifiedCommandDispatcher = std::function<void(const std::string& rawLine)>;
    void setUnifiedCommandDispatcher(UnifiedCommandDispatcher dispatcher) { m_unifiedDispatcher = std::move(dispatcher); }

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

    // ========== Callbacks ==========

    /**
     * Callback for when an action is executed.
     * Useful for logging or UI feedback.
     */
    using ActionCallback = std::function<void(const std::string& actionName, const ActionResult& result)>;
    void setActionCallback(ActionCallback callback) { m_actionCallback = std::move(callback); }

private:
    // T228: no longer read by any bridge path (the dead mouse-look / keyboard
    // turn code was its only reader). Kept so the constructor signature that
    // application.cpp uses is unchanged.
    state::GameState& m_state;
    ActionDispatcher& m_dispatcher;
    input::IInputHandler* m_input = nullptr;
    CommandProcessor* m_commandProcessor = nullptr;
    input::IInputHandler* m_consoleCommandSource = nullptr;  // T29
    UnifiedCommandDispatcher m_unifiedDispatcher;             // T29
    const HotbarModel* m_hotbarModel = nullptr;              // T174

    bool m_enabled = true;

    // Callback
    ActionCallback m_actionCallback;

    // Processing methods
    void processDiscreteActions();
    void processConsoleInput();
    void processChatMessages();
    void processMoveCommands();
    void processSpellCasts();
    void processTargetRequests();
    void processLootRequests();
    void processHotbarRequests();

    // Helper to report action result
    void reportAction(const std::string& name, const ActionResult& result);
};

} // namespace action
} // namespace eqt
