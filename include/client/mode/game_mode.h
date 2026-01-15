#pragma once

#include <memory>
#include <string>

namespace eqt {

// Forward declarations
namespace input { class IInputHandler; }
namespace output { class IOutputRenderer; }
namespace state { class GameState; }

namespace mode {

/**
 * OperatingMode - The three operational modes for WillEQ.
 */
enum class OperatingMode {
    Automated,           // No user input, all actions scripted/dynamic
    HeadlessInteractive, // Console input (stdin) and output (stdout)
    GraphicalInteractive // Visual rendering with keyboard/mouse input
};

/**
 * GraphicalRendererType - Types of graphical renderers available.
 */
enum class GraphicalRendererType {
    IrrlichtSoftware,  // Irrlicht software renderer (default)
    IrrlichtGPU,       // Irrlicht with GPU acceleration
    ASCII,             // Terminal-based ASCII graphics (future)
    TopDown,           // 2D overhead view (future)
    LowRes             // Reduced resolution/detail (future)
};

/**
 * Get the string name of an operating mode.
 */
inline const char* operatingModeToString(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Automated: return "Automated";
        case OperatingMode::HeadlessInteractive: return "Headless Interactive";
        case OperatingMode::GraphicalInteractive: return "Graphical Interactive";
        default: return "Unknown";
    }
}

/**
 * ModeConfig - Configuration for mode initialization.
 */
struct ModeConfig {
    // Display settings (for graphical mode)
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    std::string windowTitle = "WillEQ";

    // Paths
    std::string eqClientPath;  // Path to EQ client files

    // Rendering options (for graphical mode)
    bool softwareRenderer = true;  // Use software rendering (no GPU)
    bool wireframe = false;
    bool fog = true;
    bool lighting = false;
    bool showNameTags = true;

    // Console options (for headless mode)
    bool verbose = false;          // Show entity spawn/despawn
    bool showTimestamps = true;    // Show timestamps in chat
    bool colorOutput = true;       // Use ANSI colors

    // Automation options
    std::string scriptPath;        // Path to automation script (optional)
};

/**
 * IGameMode - Abstract interface for operational modes.
 *
 * A game mode combines an input handler and an output renderer
 * to provide a complete user interaction experience. The three
 * modes are:
 *
 * - Automated: No user input, scripted actions, no output
 * - Headless: Console input/output for text-based interaction
 * - Graphical: Visual rendering with keyboard/mouse input
 *
 * Usage:
 *   auto mode = createMode(OperatingMode::HeadlessInteractive);
 *   mode->initialize(gameState, config);
 *
 *   while (mode->update(deltaTime)) {
 *       // Process game logic
 *   }
 *
 *   mode->shutdown();
 */
class IGameMode {
public:
    virtual ~IGameMode() = default;

    // ========== Mode Identity ==========

    /**
     * Get the operating mode type.
     */
    virtual OperatingMode getMode() const = 0;

    /**
     * Get the mode name as a string.
     */
    virtual const char* getModeName() const {
        return operatingModeToString(getMode());
    }

    // ========== Component Access ==========

    /**
     * Get the input handler for this mode.
     * May return nullptr for automated mode.
     */
    virtual input::IInputHandler* getInputHandler() = 0;

    /**
     * Get the renderer for this mode.
     */
    virtual output::IOutputRenderer* getRenderer() = 0;

    // ========== Lifecycle ==========

    /**
     * Initialize the mode with game state and configuration.
     * @param state The game state to observe and interact with
     * @param config Mode-specific configuration
     * @return true if initialization succeeded
     */
    virtual bool initialize(state::GameState& state, const ModeConfig& config) = 0;

    /**
     * Shutdown the mode and release resources.
     */
    virtual void shutdown() = 0;

    /**
     * Check if the mode is initialized and running.
     */
    virtual bool isRunning() const = 0;

    /**
     * Process one frame/update cycle.
     * Updates input, processes actions, and renders output.
     * @param deltaTime Time since last update in seconds
     * @return false if the mode should quit
     */
    virtual bool update(float deltaTime) = 0;

    // ========== Mode-Specific Features ==========

    /**
     * Request the mode to quit.
     */
    virtual void requestQuit() = 0;

    /**
     * Check if a quit has been requested.
     */
    virtual bool isQuitRequested() const = 0;
};

/**
 * Create a game mode of the specified type.
 * @param mode Operating mode to create
 * @param rendererType Renderer type for graphical modes (default: IrrlichtSoftware)
 * @return Unique pointer to the mode
 */
std::unique_ptr<IGameMode> createMode(OperatingMode mode,
    GraphicalRendererType rendererType = GraphicalRendererType::IrrlichtSoftware);

/**
 * Parse operating mode from string.
 * @param str Mode string ("automated", "headless", "graphical")
 * @return Operating mode (defaults to GraphicalInteractive if unknown)
 */
OperatingMode parseModeString(const std::string& str);

} // namespace mode
} // namespace eqt
