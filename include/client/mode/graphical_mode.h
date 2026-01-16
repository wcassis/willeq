#pragma once

#include "client/mode/game_mode.h"
#include "client/input/input_handler.h"
#include "client/output/output_renderer.h"
#include "client/output/graphical_renderer.h"
#include <memory>

namespace eqt {
namespace mode {

// GraphicalRendererType is defined in game_mode.h

/**
 * GraphicalMode - Mode for visual interactive operation.
 *
 * In this mode, the user interacts through a graphical interface:
 * - Input: Keyboard and mouse through the graphics system
 * - Output: Visual rendering of the game world
 *
 * Use cases:
 * - Standard gameplay
 * - Visual debugging
 * - Streaming/recording
 *
 * Features:
 * - Multiple renderer types (software, GPU, ASCII, top-down)
 * - Camera modes (free, follow, first-person)
 * - UI windows (inventory, skills, chat)
 * - Entity and door rendering
 * - Zone geometry loading
 */
class GraphicalMode : public IGameMode {
public:
    /**
     * Create a graphical mode with the specified renderer type.
     * @param rendererType Type of graphical renderer to use
     */
    explicit GraphicalMode(GraphicalRendererType rendererType = GraphicalRendererType::IrrlichtSoftware);
    ~GraphicalMode() override;

    // ========== IGameMode Interface ==========

    OperatingMode getMode() const override { return OperatingMode::GraphicalInteractive; }

    input::IInputHandler* getInputHandler() override;
    output::IOutputRenderer* getRenderer() override { return m_renderer.get(); }

    bool initialize(state::GameState& state, const ModeConfig& config) override;
    void shutdown() override;
    bool isRunning() const override { return m_running; }
    bool update(float deltaTime) override;

    void requestQuit() override;
    bool isQuitRequested() const override { return m_quitRequested; }

    // ========== Graphical-Specific API ==========

    /**
     * Get the graphical renderer type.
     */
    GraphicalRendererType getRendererType() const { return m_rendererType; }

    /**
     * Get the graphical renderer (if available).
     * Returns nullptr if the renderer is not a GraphicalRenderer subclass.
     */
    output::GraphicalRenderer* graphicalRenderer();

    /**
     * Get the game state.
     */
    state::GameState* getGameState() { return m_gameState; }

    // ========== Camera Control ==========

    /**
     * Set camera mode.
     */
    void setCameraMode(output::CameraMode mode);

    /**
     * Cycle to next camera mode.
     */
    void cycleCameraMode();

    /**
     * Get current camera mode.
     */
    output::CameraMode getCameraMode() const;

    // ========== Rendering Options ==========

    /**
     * Toggle wireframe rendering.
     */
    void toggleWireframe();

    /**
     * Toggle HUD display.
     */
    void toggleHUD();

    /**
     * Toggle name tags.
     */
    void toggleNameTags();

    /**
     * Take a screenshot.
     * @param filename Output filename
     * @return true if successful
     */
    bool saveScreenshot(const std::string& filename);

    // ========== Zone Management ==========

    /**
     * Load a zone for rendering.
     * @param zoneName Zone short name
     */
    void loadZone(const std::string& zoneName);

    /**
     * Unload the current zone.
     */
    void unloadZone();

    /**
     * Set zone loading progress.
     */
    void setLoadingProgress(float progress, const std::wstring& statusText);

    /**
     * Mark zone as ready.
     */
    void setZoneReady(bool ready);

private:
    void processInput();

    GraphicalRendererType m_rendererType;
    std::unique_ptr<output::IOutputRenderer> m_renderer;
    std::unique_ptr<input::IInputHandler> m_inputHandler;

    state::GameState* m_gameState = nullptr;
    bool m_running = false;
    bool m_quitRequested = false;
};

} // namespace mode
} // namespace eqt
