#include "client/mode/headless_mode.h"
#include "client/state/game_state.h"

namespace eqt {
namespace mode {

HeadlessMode::HeadlessMode() = default;

HeadlessMode::~HeadlessMode() {
    shutdown();
}

bool HeadlessMode::initialize(state::GameState& state, const ModeConfig& config) {
    m_gameState = &state;

    // Create input handler
    m_inputHandler = std::make_unique<input::ConsoleInputHandler>();

    // Create and configure renderer
    m_renderer = std::make_unique<output::ConsoleRenderer>();

    output::RendererConfig rendererConfig;
    rendererConfig.eqClientPath = config.eqClientPath;
    rendererConfig.verbose = config.verbose;
    rendererConfig.showTimestamps = config.showTimestamps;
    rendererConfig.colorOutput = config.colorOutput;

    if (!m_renderer->initialize(rendererConfig)) {
        return false;
    }

    // Connect renderer to game state events
    m_renderer->connectToGameState(state);

    m_running = true;
    m_quitRequested = false;

    return true;
}

void HeadlessMode::shutdown() {
    if (m_running) {
        // Disconnect from game state
        if (m_renderer) {
            m_renderer->disconnectFromGameState();
            m_renderer->shutdown();
        }

        // Shutdown input handler
        if (m_inputHandler) {
            m_inputHandler->shutdown();
        }

        m_running = false;
        m_gameState = nullptr;
    }
}

bool HeadlessMode::update(float deltaTime) {
    if (!m_running || m_quitRequested) {
        return false;
    }

    // Update input handler
    if (m_inputHandler) {
        m_inputHandler->update();

        // Check for quit action
        if (m_inputHandler->consumeAction(input::InputAction::Quit)) {
            m_quitRequested = true;
            return false;
        }

        // Process other input
        processInput();
    }

    // Update renderer
    if (m_renderer && !m_renderer->update(deltaTime)) {
        return false;
    }

    return true;
}

void HeadlessMode::requestQuit() {
    m_quitRequested = true;
    if (m_renderer) {
        m_renderer->requestQuit();
    }
}

void HeadlessMode::processInput() {
    if (!m_inputHandler) return;

    // Process chat messages from console input
    while (auto chatMsg = m_inputHandler->consumeChatMessage()) {
        // Chat messages would be handled by the application layer
        // that connects to the EverQuest network client
        // Here we just display them back for confirmation
        if (m_renderer) {
            m_renderer->displayChatMessage(chatMsg->channel, "You", chatMsg->text);
        }
    }

    // Process move commands
    while (auto moveCmd = m_inputHandler->consumeMoveCommand()) {
        // Move commands would be handled by the application layer
        // Just display confirmation for now
        if (m_renderer) {
            switch (moveCmd->type) {
                case input::MoveCommand::Type::Coordinates:
                    m_renderer->displaySystemMessage(
                        "Moving to: " + std::to_string(moveCmd->x) + ", " +
                        std::to_string(moveCmd->y) + ", " + std::to_string(moveCmd->z));
                    break;
                case input::MoveCommand::Type::Entity:
                    m_renderer->displaySystemMessage("Moving to: " + moveCmd->entityName);
                    break;
                case input::MoveCommand::Type::Face:
                    if (!moveCmd->entityName.empty()) {
                        m_renderer->displaySystemMessage("Facing: " + moveCmd->entityName);
                    } else {
                        m_renderer->displaySystemMessage(
                            "Facing: " + std::to_string(moveCmd->x) + ", " +
                            std::to_string(moveCmd->y) + ", " + std::to_string(moveCmd->z));
                    }
                    break;
                case input::MoveCommand::Type::Stop:
                    m_renderer->displaySystemMessage("Stopping movement");
                    break;
            }
        }
    }

    // Process raw commands (pass through to application layer)
    // These would be handled by a command processor in the application
}

void HeadlessMode::setVerbose(bool verbose) {
    if (m_renderer) {
        m_renderer->setVerbose(verbose);
    }
}

void HeadlessMode::setShowTimestamps(bool show) {
    if (m_renderer) {
        m_renderer->setShowTimestamps(show);
    }
}

void HeadlessMode::setColorOutput(bool color) {
    if (m_renderer) {
        m_renderer->setColorOutput(color);
    }
}

} // namespace mode
} // namespace eqt
