#pragma once

#include "client/state/event_bus.h"
#include <string>
#include <memory>
#include <functional>

namespace eqt {

// Forward declarations
namespace input { class IInputHandler; }
namespace state { class GameState; }

namespace output {

/**
 * RendererConfig - Configuration for renderer initialization.
 */
struct RendererConfig {
    // Display settings
    int width = 800;
    int height = 600;
    bool fullscreen = false;
    std::string windowTitle = "WillEQ";

    // Paths
    std::string eqClientPath;  // Path to EQ client files

    // Rendering options
    bool softwareRenderer = true;   // Use software rendering (no GPU)
    bool wireframe = false;
    bool fog = true;
    bool lighting = false;
    bool showNameTags = true;

    // Console-specific options
    bool verbose = false;           // Show spawn/despawn messages
    bool showTimestamps = true;     // Show timestamps in chat
    bool colorOutput = true;        // Use ANSI colors (Unix)
};

/**
 * RenderQuality - Quality presets for graphical renderers.
 */
enum class RenderQuality {
    Low,      // Reduced detail, no effects
    Medium,   // Default quality
    High,     // Maximum detail
    Ultra     // Everything enabled
};

/**
 * CameraMode - Camera mode for graphical renderers.
 */
enum class CameraMode {
    Free,         // Free-fly camera
    Follow,       // Third-person follow
    FirstPerson   // First-person view
};

/**
 * IOutputRenderer - Abstract interface for game output rendering.
 *
 * This interface abstracts all output/rendering for different modes:
 * - NullRenderer: For automated mode (no output)
 * - ConsoleRenderer: For headless mode (text output to stdout)
 * - GraphicalRenderer: Base class for visual renderers (Irrlicht, ASCII, etc.)
 *
 * Renderers subscribe to GameState events and react to state changes.
 * They may optionally provide an IInputHandler for mode-specific input.
 */
class IOutputRenderer {
public:
    virtual ~IOutputRenderer() = default;

    // ========== Lifecycle ==========

    /**
     * Initialize the renderer.
     * @param config Renderer configuration
     * @return true if initialization succeeded
     */
    virtual bool initialize(const RendererConfig& config) = 0;

    /**
     * Shutdown the renderer and release resources.
     */
    virtual void shutdown() = 0;

    /**
     * Check if the renderer is initialized and running.
     */
    virtual bool isRunning() const = 0;

    /**
     * Process one frame/update cycle.
     * @param deltaTime Time since last update in seconds
     * @return false if the renderer should quit
     */
    virtual bool update(float deltaTime) = 0;

    // ========== Event Bus Integration ==========

    /**
     * Connect to a GameState's event bus.
     * The renderer will subscribe to relevant events.
     * @param state The game state to observe
     */
    virtual void connectToGameState(state::GameState& state) = 0;

    /**
     * Disconnect from the game state event bus.
     */
    virtual void disconnectFromGameState() = 0;

    // ========== Zone Management ==========

    /**
     * Begin loading a zone.
     * @param zoneName Zone short name (e.g., "qeynos2")
     */
    virtual void loadZone(const std::string& zoneName) = 0;

    /**
     * Unload the current zone.
     */
    virtual void unloadZone() = 0;

    /**
     * Get the current zone name.
     */
    virtual const std::string& getCurrentZoneName() const = 0;

    /**
     * Set zone loading progress.
     * @param progress Progress value (0.0 to 1.0)
     * @param statusText Status message to display
     */
    virtual void setLoadingProgress(float progress, const std::wstring& statusText) = 0;

    /**
     * Mark the zone as fully loaded and ready.
     */
    virtual void setZoneReady(bool ready) = 0;

    /**
     * Check if the zone is ready.
     */
    virtual bool isZoneReady() const = 0;

    // ========== Player Display ==========

    /**
     * Set the player's spawn ID (for identification in entity list).
     */
    virtual void setPlayerSpawnId(uint16_t spawnId) = 0;

    /**
     * Update player position display.
     */
    virtual void setPlayerPosition(float x, float y, float z, float heading) = 0;

    /**
     * Set character info display.
     */
    virtual void setCharacterInfo(const std::wstring& name, int level, const std::wstring& className) = 0;

    /**
     * Update character stats display.
     */
    virtual void updateCharacterStats(uint32_t curHp, uint32_t maxHp,
                                       uint32_t curMana, uint32_t maxMana,
                                       uint32_t curEnd, uint32_t maxEnd) = 0;

    // ========== Target Display ==========

    /**
     * Set current target info.
     */
    virtual void setCurrentTarget(uint16_t spawnId, const std::string& name,
                                   uint8_t hpPercent = 100, uint8_t level = 0) = 0;

    /**
     * Update current target HP.
     */
    virtual void updateCurrentTargetHP(uint8_t hpPercent) = 0;

    /**
     * Clear current target.
     */
    virtual void clearCurrentTarget() = 0;

    // ========== Chat/Message Output ==========

    /**
     * Display a chat message.
     * @param channel Channel name (say, shout, tell, ooc, etc.)
     * @param sender Sender name
     * @param message Message text
     */
    virtual void displayChatMessage(const std::string& channel,
                                     const std::string& sender,
                                     const std::string& message) = 0;

    /**
     * Display a system message.
     * @param message Message text
     */
    virtual void displaySystemMessage(const std::string& message) = 0;

    /**
     * Display a combat message.
     * @param message Combat text (hit, miss, damage, etc.)
     */
    virtual void displayCombatMessage(const std::string& message) = 0;

    // ========== Input Handler (Optional) ==========

    /**
     * Get the renderer's input handler, if it provides one.
     * Graphical renderers typically provide their own input handler.
     * @return Input handler or nullptr if none provided
     */
    virtual input::IInputHandler* getInputHandler() { return nullptr; }

    // ========== Graphical Renderer Options (Optional) ==========

    /**
     * Set render quality. Only applicable to graphical renderers.
     */
    virtual void setRenderQuality(RenderQuality quality) { (void)quality; }

    /**
     * Set camera mode. Only applicable to graphical renderers.
     */
    virtual void setCameraMode(CameraMode mode) { (void)mode; }

    /**
     * Cycle to next camera mode. Only applicable to graphical renderers.
     */
    virtual void cycleCameraMode() {}

    /**
     * Toggle wireframe rendering. Only applicable to graphical renderers.
     */
    virtual void toggleWireframe() {}

    /**
     * Toggle HUD display. Only applicable to graphical renderers.
     */
    virtual void toggleHUD() {}

    /**
     * Toggle name tags. Only applicable to graphical renderers.
     */
    virtual void toggleNameTags() {}

    /**
     * Take a screenshot. Only applicable to graphical renderers.
     * @param filename Output filename
     * @return true if successful
     */
    virtual bool saveScreenshot(const std::string& filename) { (void)filename; return false; }

    /**
     * Request the renderer to quit.
     */
    virtual void requestQuit() = 0;

    // ========== Console Renderer Options (Optional) ==========

    /**
     * Set verbose mode for spawn/despawn messages.
     */
    virtual void setVerbose(bool verbose) { (void)verbose; }

    /**
     * Get verbose mode setting.
     */
    virtual bool getVerbose() const { return false; }

    /**
     * Set whether to show timestamps in chat.
     */
    virtual void setShowTimestamps(bool show) { (void)show; }

    /**
     * Get timestamp display setting.
     */
    virtual bool getShowTimestamps() const { return false; }

    /**
     * Set whether to use colored output.
     */
    virtual void setColorOutput(bool color) { (void)color; }

    /**
     * Get color output setting.
     */
    virtual bool getColorOutput() const { return false; }
};

/**
 * RendererType - Enumeration of available renderer types.
 */
enum class RendererType {
    Null,             // No output (automated mode)
    Console,          // Text output to stdout
    IrrlichtSoftware, // Irrlicht software renderer
    IrrlichtGPU,      // Irrlicht with GPU acceleration
    ASCII,            // Terminal-based ASCII graphics (future)
    MUD,              // Text adventure style (future)
    TopDown           // 2D overhead view (future)
};

/**
 * Create a renderer of the specified type.
 * @param type Renderer type to create
 * @return Unique pointer to the renderer
 */
std::unique_ptr<IOutputRenderer> createRenderer(RendererType type);

} // namespace output
} // namespace eqt
