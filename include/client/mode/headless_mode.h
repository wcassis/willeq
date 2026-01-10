#pragma once

#include "client/mode/game_mode.h"
#include "client/input/console_input_handler.h"
#include "client/output/console_renderer.h"
#include <memory>

namespace eqt {
namespace mode {

/**
 * HeadlessMode - Mode for text-based interactive operation.
 *
 * In this mode, the user interacts through the console:
 * - Input: Commands typed at stdin (command mode) or WASD keys (keyboard mode)
 * - Output: Formatted text to stdout with timestamps and colors
 *
 * Use cases:
 * - Running on headless servers
 * - SSH sessions without X forwarding
 * - Low-bandwidth connections
 * - Accessibility (screen readers)
 * - Logging and monitoring
 *
 * Features:
 * - Two input modes: keyboard (WASD) and command (text commands)
 * - Press Enter to toggle between input modes
 * - Color-coded chat channels
 * - Periodic status line updates
 * - Entity spawn/despawn notifications (verbose mode)
 */
class HeadlessMode : public IGameMode {
public:
    HeadlessMode();
    ~HeadlessMode() override;

    // ========== IGameMode Interface ==========

    OperatingMode getMode() const override { return OperatingMode::HeadlessInteractive; }

    input::IInputHandler* getInputHandler() override { return m_inputHandler.get(); }
    output::IOutputRenderer* getRenderer() override { return m_renderer.get(); }

    bool initialize(state::GameState& state, const ModeConfig& config) override;
    void shutdown() override;
    bool isRunning() const override { return m_running; }
    bool update(float deltaTime) override;

    void requestQuit() override;
    bool isQuitRequested() const override { return m_quitRequested; }

    // ========== Headless-Specific API ==========

    /**
     * Get direct access to the console input handler.
     */
    input::ConsoleInputHandler* consoleInput() {
        return m_inputHandler.get();
    }

    /**
     * Get direct access to the console renderer.
     */
    output::ConsoleRenderer* consoleRenderer() {
        return m_renderer.get();
    }

    /**
     * Set verbose mode (show entity spawn/despawn).
     */
    void setVerbose(bool verbose);

    /**
     * Set whether to show timestamps in output.
     */
    void setShowTimestamps(bool show);

    /**
     * Set whether to use color output.
     */
    void setColorOutput(bool color);

    /**
     * Get the game state.
     */
    state::GameState* getGameState() { return m_gameState; }

private:
    void processInput();

    std::unique_ptr<input::ConsoleInputHandler> m_inputHandler;
    std::unique_ptr<output::ConsoleRenderer> m_renderer;

    state::GameState* m_gameState = nullptr;
    bool m_running = false;
    bool m_quitRequested = false;
};

} // namespace mode
} // namespace eqt
