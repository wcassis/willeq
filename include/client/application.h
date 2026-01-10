#pragma once

#include "client/state/game_state.h"
#include "client/mode/game_mode.h"
#include "client/mode/graphical_mode.h"
#include "client/action/action_dispatcher.h"
#include "client/action/input_action_bridge.h"
#include "client/action/command_processor.h"

#include <memory>
#include <string>
#include <vector>
#include <atomic>

// Forward declarations
class EverQuest;

namespace eqt {

// Forward declarations
class EqActionHandler;

/**
 * ApplicationConfig - Configuration for the Application.
 *
 * Contains all settings needed to initialize and run the application,
 * including connection info, paths, display settings, and mode selection.
 */
struct ApplicationConfig {
    // Connection settings
    std::string host;
    int port = 5998;
    std::string user;
    std::string pass;
    std::string server;
    std::string character;

    // Path settings
    std::string configFile = "willeq.json";
    std::string navmeshPath;
    std::string mapsPath;
    std::string eqClientPath;

    // Display settings
    int displayWidth = 800;
    int displayHeight = 600;
    bool fullscreen = false;

    // Mode settings
    mode::OperatingMode operatingMode = mode::OperatingMode::GraphicalInteractive;
    mode::GraphicalRendererType graphicalRendererType = mode::GraphicalRendererType::IrrlichtSoftware;

    // Feature flags
    bool pathfindingEnabled = true;
    bool graphicsEnabled = true;

    // Logging
    int debugLevel = 0;
};

/**
 * Application - Main application class that orchestrates all components.
 *
 * This class brings together:
 * - GameState: Single source of truth for all game data
 * - IGameMode: Input/output handling for the selected operating mode
 * - ActionDispatcher: Unified action handling
 * - InputActionBridge: Translates input to actions
 * - EverQuest: Network client and game logic (via EqActionHandler adapter)
 *
 * Usage:
 *   ApplicationConfig config = parseArguments(argc, argv);
 *   Application app;
 *   if (app.initialize(config)) {
 *       app.run();
 *   }
 *   app.shutdown();
 */
class Application {
public:
    Application();
    ~Application();

    // Non-copyable, non-movable
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    /**
     * Initialize the application with the given configuration.
     *
     * @param config Application configuration
     * @return true if initialization succeeded, false otherwise
     */
    bool initialize(const ApplicationConfig& config);

    /**
     * Run the main application loop.
     *
     * This method blocks until the application is ready to exit.
     * Call requestQuit() from another thread or from within the
     * application to signal that the loop should terminate.
     */
    void run();

    /**
     * Shutdown the application and release all resources.
     */
    void shutdown();

    /**
     * Request the application to quit.
     *
     * This can be called from any thread to signal that the
     * main loop should terminate.
     */
    void requestQuit();

    /**
     * Check if the application is still running.
     */
    bool isRunning() const { return m_running.load(); }

    // ========== Component Access ==========

    /**
     * Get the game state.
     */
    state::GameState* getGameState() { return m_gameState.get(); }
    const state::GameState* getGameState() const { return m_gameState.get(); }

    /**
     * Get the current game mode.
     */
    mode::IGameMode* getGameMode() { return m_gameMode.get(); }
    const mode::IGameMode* getGameMode() const { return m_gameMode.get(); }

    /**
     * Get the action dispatcher.
     */
    action::ActionDispatcher* getActionDispatcher() { return m_dispatcher.get(); }
    const action::ActionDispatcher* getActionDispatcher() const { return m_dispatcher.get(); }

    /**
     * Get the command processor.
     */
    action::CommandProcessor* getCommandProcessor() { return m_commandProcessor.get(); }
    const action::CommandProcessor* getCommandProcessor() const { return m_commandProcessor.get(); }

    /**
     * Get the EverQuest client (for direct access when needed).
     */
    EverQuest* getEverQuestClient() { return m_eqClient.get(); }
    const EverQuest* getEverQuestClient() const { return m_eqClient.get(); }

    // ========== Static Helpers ==========

    /**
     * Parse command line arguments into an ApplicationConfig.
     *
     * @param argc Argument count
     * @param argv Argument values
     * @return Parsed configuration
     */
    static ApplicationConfig parseArguments(int argc, char* argv[]);

    /**
     * Load configuration from a JSON file.
     *
     * @param configFile Path to the config file
     * @param config Output configuration (will be modified)
     * @return true if loading succeeded, false otherwise
     */
    static bool loadConfigFile(const std::string& configFile, ApplicationConfig& config);

private:
    // ========== Main Loop Stages ==========

    void mainLoop();
    void processNetworkEvents();
    void processInput(float deltaTime);
    void updateGameState(float deltaTime);
    void render(float deltaTime);

    // ========== Synchronization ==========

    /**
     * Sync game state from EverQuest client.
     *
     * This updates the GameState from the EverQuest client's internal state.
     * Called periodically to keep state in sync during the transition period.
     */
    void syncGameStateFromClient();

    /**
     * Update loading progress for the renderer.
     */
    void updateLoadingProgress();

    // ========== Components ==========

    std::unique_ptr<state::GameState> m_gameState;
    std::unique_ptr<mode::IGameMode> m_gameMode;
    std::unique_ptr<action::ActionDispatcher> m_dispatcher;
    std::unique_ptr<action::InputActionBridge> m_inputBridge;
    std::unique_ptr<action::CommandProcessor> m_commandProcessor;

    // EverQuest client and adapter
    std::unique_ptr<EverQuest> m_eqClient;
    std::unique_ptr<EqActionHandler> m_actionHandler;

    // ========== State ==========

    std::atomic<bool> m_running{false};
    bool m_fullyConnected = false;
    ApplicationConfig m_config;

    // Timing
    std::chrono::steady_clock::time_point m_lastUpdate;
    std::chrono::steady_clock::time_point m_lastGraphicsUpdate;
};

} // namespace eqt
