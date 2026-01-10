#pragma once

#include "client/input/input_handler.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#ifndef _WIN32
#include <termios.h>
#endif

namespace eqt {
namespace input {

/**
 * ConsoleInputHandler - Input handler for headless interactive mode.
 *
 * This handler reads commands from stdin and converts them to input actions.
 * It supports two modes:
 * - Keyboard mode: WASD keys for movement (raw terminal mode)
 * - Command mode: Full command line input (normal terminal mode)
 *
 * Press Enter to toggle between modes.
 */
class ConsoleInputHandler : public IInputHandler {
public:
    ConsoleInputHandler();
    ~ConsoleInputHandler() override;

    // Disable copy/move
    ConsoleInputHandler(const ConsoleInputHandler&) = delete;
    ConsoleInputHandler& operator=(const ConsoleInputHandler&) = delete;
    ConsoleInputHandler(ConsoleInputHandler&&) = delete;
    ConsoleInputHandler& operator=(ConsoleInputHandler&&) = delete;

    // IInputHandler interface
    void update() override;
    bool isActive() const override;

    bool hasAction(InputAction action) const override;
    bool consumeAction(InputAction action) override;

    const InputState& getState() const override { return m_state; }
    void resetMouseDeltas() override;

    std::optional<SpellCastRequest> consumeSpellCastRequest() override;
    std::optional<HotbarRequest> consumeHotbarRequest() override;
    std::optional<TargetRequest> consumeTargetRequest() override;
    std::optional<LootRequest> consumeLootRequest() override;

    bool hasPendingKeyEvents() const override { return false; }
    std::optional<KeyEvent> popKeyEvent() override { return std::nullopt; }
    void clearPendingKeyEvents() override {}

    std::optional<ChatMessage> consumeChatMessage() override;
    std::optional<MoveCommand> consumeMoveCommand() override;
    std::optional<std::string> consumeRawCommand() override;

    /**
     * Shutdown the input handler (stops input thread).
     */
    void shutdown();

    /**
     * Check if in keyboard control mode.
     */
    bool isKeyboardMode() const { return m_keyboardMode; }

private:
    void inputThreadFunc();
    void processCommand(const std::string& cmd);
    void processKeyboardInput(char key);

    // Platform-specific terminal handling
    void enableRawMode();
    void disableRawMode();
    bool keyPressed();
    char readKey();

    std::atomic<bool> m_active{true};
    std::atomic<bool> m_keyboardMode{true};  // Start in keyboard mode

    // Input thread
    std::thread m_inputThread;
    mutable std::mutex m_mutex;

    // State
    InputState m_state;
    bool m_pendingActions[static_cast<size_t>(InputAction::Count)] = {false};

    // Pending requests (thread-safe queues)
    std::queue<SpellCastRequest> m_spellCastRequests;
    std::queue<HotbarRequest> m_hotbarRequests;
    std::queue<TargetRequest> m_targetRequests;
    std::queue<LootRequest> m_lootRequests;
    std::queue<ChatMessage> m_chatMessages;
    std::queue<MoveCommand> m_moveCommands;
    std::queue<std::string> m_rawCommands;

    // Platform-specific terminal state
#ifndef _WIN32
    bool m_rawModeEnabled = false;
    ::termios* m_origTermios = nullptr;
#endif
};

} // namespace input
} // namespace eqt
