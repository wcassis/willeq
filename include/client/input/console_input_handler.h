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

    std::optional<ChatMessage> consumeChatMessage() override;
    std::optional<MoveCommand> consumeMoveCommand() override;
    std::optional<std::string> consumeRawCommand() override;
    std::optional<std::string> peekRawCommand() const override;

    /**
     * Shutdown the input handler (stops input thread).
     */
    void shutdown();

    /**
     * Check if in keyboard control mode.
     */
    bool isKeyboardMode() const { return m_keyboardMode; }

    /**
     * T29: true if stdin is a real terminal (isatty(STDIN_FILENO)).
     * false means stdin is a pipe/file -- no raw mode, no keyboard-mode
     * toggle (there's no human at a keyboard to toggle for), just a plain
     * line-oriented read/dispatch/repeat loop that exits cleanly on EOF.
     * See docs/decisions.md 2026-08-02, refactor/T29_unified_command_channel.md.
     */
    bool isStdinTty() const { return m_isTty; }

private:
    void inputThreadFunc();
    void pipedInputThreadFunc();  // T29: non-tty stdin path (no raw mode)
    void processCommand(const std::string& cmd);
    void processKeyboardInput(char key);

    // Platform-specific terminal handling
    void enableRawMode();
    void disableRawMode();
    bool keyPressed();
    char readKey();

    std::atomic<bool> m_active{true};
    std::atomic<bool> m_keyboardMode{true};  // Start in keyboard mode
    bool m_isTty = true;  // T29: detected once at construction, see isStdinTty()

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
