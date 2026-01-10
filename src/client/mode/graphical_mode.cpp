#include "client/mode/graphical_mode.h"
#include "client/state/game_state.h"
#include "client/output/console_renderer.h"
#include "client/input/console_input_handler.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/input/graphics_input_handler.h"
// Note: When IrrlichtRenderer is adapted, include it here
// #include "client/graphics/irrlicht_renderer_adapter.h"
#endif

namespace eqt {
namespace mode {

GraphicalMode::GraphicalMode(GraphicalRendererType rendererType)
    : m_rendererType(rendererType) {
}

GraphicalMode::~GraphicalMode() {
    shutdown();
}

bool GraphicalMode::initialize(state::GameState& state, const ModeConfig& config) {
    m_gameState = &state;

    // Create renderer based on type
    output::RendererConfig rendererConfig;
    rendererConfig.width = config.width;
    rendererConfig.height = config.height;
    rendererConfig.fullscreen = config.fullscreen;
    rendererConfig.windowTitle = config.windowTitle;
    rendererConfig.eqClientPath = config.eqClientPath;
    rendererConfig.softwareRenderer = (m_rendererType == GraphicalRendererType::IrrlichtSoftware);
    rendererConfig.wireframe = config.wireframe;
    rendererConfig.fog = config.fog;
    rendererConfig.lighting = config.lighting;
    rendererConfig.showNameTags = config.showNameTags;

#ifdef EQT_HAS_GRAPHICS
    // When IrrlichtRenderer is adapted, create it here based on m_rendererType
    // For now, we fall back to console renderer
    switch (m_rendererType) {
        case GraphicalRendererType::IrrlichtSoftware:
        case GraphicalRendererType::IrrlichtGPU:
            // TODO: Create IrrlichtRendererAdapter when available
            // m_renderer = std::make_unique<IrrlichtRendererAdapter>(rendererConfig);
            // m_inputHandler = std::make_unique<GraphicsInputHandler>(m_renderer->getEventReceiver());
            // For now, fall through to console
            [[fallthrough]];
        case GraphicalRendererType::ASCII:
        case GraphicalRendererType::TopDown:
        case GraphicalRendererType::LowRes:
        default:
            // Fall back to console renderer until graphical renderers are adapted
            m_renderer = std::make_unique<output::ConsoleRenderer>();
            m_inputHandler = std::make_unique<input::ConsoleInputHandler>();
            break;
    }
#else
    // No graphics support - use console
    m_renderer = std::make_unique<output::ConsoleRenderer>();
    m_inputHandler = std::make_unique<input::ConsoleInputHandler>();
#endif

    if (!m_renderer->initialize(rendererConfig)) {
        return false;
    }

    // Connect renderer to game state events
    m_renderer->connectToGameState(state);

    m_running = true;
    m_quitRequested = false;

    return true;
}

void GraphicalMode::shutdown() {
    if (m_running) {
        // Disconnect from game state
        if (m_renderer) {
            m_renderer->disconnectFromGameState();
            m_renderer->shutdown();
        }

        m_running = false;
        m_gameState = nullptr;
    }
}

input::IInputHandler* GraphicalMode::getInputHandler() {
    // First check if renderer provides its own input handler
    if (m_renderer) {
        auto* rendererInput = m_renderer->getInputHandler();
        if (rendererInput) {
            return rendererInput;
        }
    }
    // Fall back to our own input handler
    return m_inputHandler.get();
}

bool GraphicalMode::update(float deltaTime) {
    if (!m_running || m_quitRequested) {
        return false;
    }

    // Update input
    auto* inputHandler = getInputHandler();
    if (inputHandler) {
        inputHandler->update();

        // Check for quit action
        if (inputHandler->consumeAction(input::InputAction::Quit)) {
            m_quitRequested = true;
            return false;
        }

        // Process graphical-specific input
        processInput();
    }

    // Update renderer
    if (m_renderer && !m_renderer->update(deltaTime)) {
        return false;
    }

    return true;
}

void GraphicalMode::requestQuit() {
    m_quitRequested = true;
    if (m_renderer) {
        m_renderer->requestQuit();
    }
}

output::GraphicalRenderer* GraphicalMode::graphicalRenderer() {
    return dynamic_cast<output::GraphicalRenderer*>(m_renderer.get());
}

void GraphicalMode::processInput() {
    auto* inputHandler = getInputHandler();
    if (!inputHandler) return;

    // Process toggle actions
    if (inputHandler->consumeAction(input::InputAction::ToggleWireframe)) {
        toggleWireframe();
    }
    if (inputHandler->consumeAction(input::InputAction::ToggleHUD)) {
        toggleHUD();
    }
    if (inputHandler->consumeAction(input::InputAction::ToggleNameTags)) {
        toggleNameTags();
    }
    if (inputHandler->consumeAction(input::InputAction::ToggleCameraMode)) {
        cycleCameraMode();
    }
    if (inputHandler->consumeAction(input::InputAction::Screenshot)) {
        // Generate timestamp-based filename
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char filename[64];
        std::strftime(filename, sizeof(filename), "screenshot_%Y%m%d_%H%M%S.png", std::localtime(&time));
        saveScreenshot(filename);
    }
}

// ========== Camera Control ==========

void GraphicalMode::setCameraMode(output::CameraMode mode) {
    if (m_renderer) {
        m_renderer->setCameraMode(mode);
    }
}

void GraphicalMode::cycleCameraMode() {
    if (m_renderer) {
        m_renderer->cycleCameraMode();
    }
}

output::CameraMode GraphicalMode::getCameraMode() const {
    auto* graphical = const_cast<GraphicalMode*>(this)->graphicalRenderer();
    if (graphical) {
        return graphical->getCameraMode();
    }
    return output::CameraMode::FirstPerson;
}

// ========== Rendering Options ==========

void GraphicalMode::toggleWireframe() {
    if (m_renderer) {
        m_renderer->toggleWireframe();
    }
}

void GraphicalMode::toggleHUD() {
    if (m_renderer) {
        m_renderer->toggleHUD();
    }
}

void GraphicalMode::toggleNameTags() {
    if (m_renderer) {
        m_renderer->toggleNameTags();
    }
}

bool GraphicalMode::saveScreenshot(const std::string& filename) {
    if (m_renderer) {
        return m_renderer->saveScreenshot(filename);
    }
    return false;
}

// ========== Zone Management ==========

void GraphicalMode::loadZone(const std::string& zoneName) {
    if (m_renderer) {
        m_renderer->loadZone(zoneName);
    }
}

void GraphicalMode::unloadZone() {
    if (m_renderer) {
        m_renderer->unloadZone();
    }
}

void GraphicalMode::setLoadingProgress(float progress, const std::wstring& statusText) {
    if (m_renderer) {
        m_renderer->setLoadingProgress(progress, statusText);
    }
}

void GraphicalMode::setZoneReady(bool ready) {
    if (m_renderer) {
        m_renderer->setZoneReady(ready);
    }
}

} // namespace mode
} // namespace eqt
