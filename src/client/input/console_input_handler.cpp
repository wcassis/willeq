#include "client/input/console_input_handler.h"

#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <conio.h>
#else
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

namespace eqt {
namespace input {

ConsoleInputHandler::ConsoleInputHandler() {
#ifndef _WIN32
    m_origTermios = new ::termios;
#endif
    // Start in keyboard mode
    enableRawMode();
    m_inputThread = std::thread(&ConsoleInputHandler::inputThreadFunc, this);
}

ConsoleInputHandler::~ConsoleInputHandler() {
    shutdown();
#ifndef _WIN32
    delete m_origTermios;
#endif
}

void ConsoleInputHandler::shutdown() {
    if (m_active.load()) {
        m_active.store(false);
        disableRawMode();
        if (m_inputThread.joinable()) {
            m_inputThread.join();
        }
    }
}

void ConsoleInputHandler::update() {
    // Input is handled in the background thread
    // This method just needs to exist for the interface
}

bool ConsoleInputHandler::isActive() const {
    return m_active.load();
}

bool ConsoleInputHandler::hasAction(InputAction action) const {
    if (action == InputAction::Count) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pendingActions[static_cast<size_t>(action)];
}

bool ConsoleInputHandler::consumeAction(InputAction action) {
    if (action == InputAction::Count) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t index = static_cast<size_t>(action);
    bool was_pending = m_pendingActions[index];
    m_pendingActions[index] = false;
    return was_pending;
}

void ConsoleInputHandler::resetMouseDeltas() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state.mouseDeltaX = 0;
    m_state.mouseDeltaY = 0;
}

std::optional<SpellCastRequest> ConsoleInputHandler::consumeSpellCastRequest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_spellCastRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_spellCastRequests.front();
    m_spellCastRequests.pop();
    return request;
}

std::optional<HotbarRequest> ConsoleInputHandler::consumeHotbarRequest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_hotbarRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_hotbarRequests.front();
    m_hotbarRequests.pop();
    return request;
}

std::optional<TargetRequest> ConsoleInputHandler::consumeTargetRequest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_targetRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_targetRequests.front();
    m_targetRequests.pop();
    return request;
}

std::optional<LootRequest> ConsoleInputHandler::consumeLootRequest() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_lootRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_lootRequests.front();
    m_lootRequests.pop();
    return request;
}

std::optional<ChatMessage> ConsoleInputHandler::consumeChatMessage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_chatMessages.empty()) {
        return std::nullopt;
    }
    auto message = m_chatMessages.front();
    m_chatMessages.pop();
    return message;
}

std::optional<MoveCommand> ConsoleInputHandler::consumeMoveCommand() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_moveCommands.empty()) {
        return std::nullopt;
    }
    auto command = m_moveCommands.front();
    m_moveCommands.pop();
    return command;
}

std::optional<std::string> ConsoleInputHandler::consumeRawCommand() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_rawCommands.empty()) {
        return std::nullopt;
    }
    auto command = m_rawCommands.front();
    m_rawCommands.pop();
    return command;
}

void ConsoleInputHandler::inputThreadFunc() {
    std::string commandBuffer;

    while (m_active.load()) {
        if (m_keyboardMode.load()) {
            // Keyboard mode - check for key presses
            if (keyPressed()) {
                char key = readKey();
                if (key == '\n' || key == '\r') {
                    // Switch to command mode
                    m_keyboardMode.store(false);
                    disableRawMode();

                    // Clear movement state when switching modes
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_state.moveForward = false;
                        m_state.moveBackward = false;
                        m_state.strafeLeft = false;
                        m_state.strafeRight = false;
                        m_state.turnLeft = false;
                        m_state.turnRight = false;
                    }

                    std::cout << "\r\n[Command mode] Enter command (or press Enter for keyboard mode): ";
                    std::cout.flush();
                } else {
                    processKeyboardInput(key);
                }
            }
            // Small sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            // Command mode - read full line
            if (std::getline(std::cin, commandBuffer)) {
                if (commandBuffer.empty()) {
                    // Empty line - switch back to keyboard mode
                    m_keyboardMode.store(true);
                    enableRawMode();
                    std::cout << "[Keyboard mode] WASD to move, Enter for commands\r\n";
                    std::cout.flush();
                } else {
                    processCommand(commandBuffer);
                    if (m_active.load()) {
                        std::cout << "> ";
                        std::cout.flush();
                    }
                }
            } else {
                // EOF or error - shutdown
                m_active.store(false);
            }
        }
    }
}

void ConsoleInputHandler::processKeyboardInput(char key) {
    std::lock_guard<std::mutex> lock(m_mutex);

    switch (key) {
        // Movement keys (WASD)
        case 'w':
        case 'W':
            m_state.moveForward = true;
            break;
        case 's':
        case 'S':
            m_state.moveBackward = true;
            break;
        case 'a':
        case 'A':
            m_state.turnLeft = true;
            break;
        case 'd':
        case 'D':
            m_state.turnRight = true;
            break;
        case 'q':
        case 'Q':
            m_state.strafeLeft = true;
            break;
        case 'e':
        case 'E':
            m_state.strafeRight = true;
            break;

        // Key release simulation (lowercase to stop)
        // Note: In real terminal, we can't detect key release, so this is simplified
        // Movement will need to be toggled or use a different approach

        // Jump
        case ' ':
            m_pendingActions[static_cast<size_t>(InputAction::Jump)] = true;
            break;

        // Auto-attack toggle
        case '`':
        case '~':
            m_pendingActions[static_cast<size_t>(InputAction::ToggleAutoAttack)] = true;
            break;

        // Autorun toggle
        case 'r':
        case 'R':
            m_pendingActions[static_cast<size_t>(InputAction::ToggleAutorun)] = true;
            break;

        // Stop all movement
        case 'x':
        case 'X':
            m_state.moveForward = false;
            m_state.moveBackward = false;
            m_state.strafeLeft = false;
            m_state.strafeRight = false;
            m_state.turnLeft = false;
            m_state.turnRight = false;
            break;

        // Quit
        case 27:  // Escape
            m_pendingActions[static_cast<size_t>(InputAction::Quit)] = true;
            break;

        default:
            break;
    }
}

void ConsoleInputHandler::processCommand(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string command;
    iss >> command;

    std::lock_guard<std::mutex> lock(m_mutex);

    // System commands
    if (command == "quit" || command == "exit") {
        m_pendingActions[static_cast<size_t>(InputAction::Quit)] = true;
        return;
    }

    // Chat commands
    if (command == "say") {
        std::string message;
        std::getline(iss, message);
        if (!message.empty() && message[0] == ' ') message = message.substr(1);
        m_chatMessages.push(ChatMessage{message, "say", ""});
        return;
    }
    if (command == "tell") {
        std::string target, message;
        iss >> target;
        std::getline(iss, message);
        if (!message.empty() && message[0] == ' ') message = message.substr(1);
        m_chatMessages.push(ChatMessage{message, "tell", target});
        return;
    }
    if (command == "shout") {
        std::string message;
        std::getline(iss, message);
        if (!message.empty() && message[0] == ' ') message = message.substr(1);
        m_chatMessages.push(ChatMessage{message, "shout", ""});
        return;
    }
    if (command == "ooc") {
        std::string message;
        std::getline(iss, message);
        if (!message.empty() && message[0] == ' ') message = message.substr(1);
        m_chatMessages.push(ChatMessage{message, "ooc", ""});
        return;
    }
    if (command == "auction") {
        std::string message;
        std::getline(iss, message);
        if (!message.empty() && message[0] == ' ') message = message.substr(1);
        m_chatMessages.push(ChatMessage{message, "auction", ""});
        return;
    }

    // Movement commands
    if (command == "move") {
        float x, y, z;
        if (iss >> x >> y >> z) {
            MoveCommand mc;
            mc.type = MoveCommand::Type::Coordinates;
            mc.x = x;
            mc.y = y;
            mc.z = z;
            m_moveCommands.push(mc);
        }
        return;
    }
    if (command == "moveto") {
        std::string entity;
        std::getline(iss, entity);
        if (!entity.empty() && entity[0] == ' ') entity = entity.substr(1);
        if (!entity.empty()) {
            MoveCommand mc;
            mc.type = MoveCommand::Type::Entity;
            mc.entityName = entity;
            m_moveCommands.push(mc);
        }
        return;
    }
    if (command == "face") {
        std::string arg;
        if (iss >> arg) {
            float x, y, z;
            std::istringstream coord_stream(arg);
            if (coord_stream >> x && iss >> y >> z) {
                MoveCommand mc;
                mc.type = MoveCommand::Type::Face;
                mc.x = x;
                mc.y = y;
                mc.z = z;
                m_moveCommands.push(mc);
            } else {
                std::string rest;
                std::getline(iss, rest);
                std::string full_name = arg + rest;
                MoveCommand mc;
                mc.type = MoveCommand::Type::Face;
                mc.entityName = full_name;
                m_moveCommands.push(mc);
            }
        }
        return;
    }
    if (command == "stopfollow" || command == "stop") {
        MoveCommand mc;
        mc.type = MoveCommand::Type::Stop;
        m_moveCommands.push(mc);
        return;
    }

    // Combat commands
    if (command == "attack") {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleAutoAttack)] = true;
        return;
    }
    if (command == "~" || command == "aa") {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleAutoAttack)] = true;
        return;
    }

    // UI commands
    if (command == "inventory" || command == "inv") {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleInventory)] = true;
        return;
    }
    if (command == "skills") {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleSkills)] = true;
        return;
    }
    if (command == "group") {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleGroup)] = true;
        return;
    }

    // Spell casting (cast 1-8)
    if (command == "cast") {
        int gem;
        if (iss >> gem && gem >= 1 && gem <= 8) {
            m_spellCastRequests.push(SpellCastRequest{static_cast<uint8_t>(gem - 1)});
        }
        return;
    }

    // Default: pass through as raw command
    m_rawCommands.push(cmd);
}

// Platform-specific terminal handling

#ifndef _WIN32
void ConsoleInputHandler::enableRawMode() {
    if (m_rawModeEnabled) return;

    tcgetattr(STDIN_FILENO, m_origTermios);

    ::termios raw = *m_origTermios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    m_rawModeEnabled = true;
}

void ConsoleInputHandler::disableRawMode() {
    if (!m_rawModeEnabled) return;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, m_origTermios);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);

    m_rawModeEnabled = false;
}

bool ConsoleInputHandler::keyPressed() {
    struct timeval tv = {0, 0};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

char ConsoleInputHandler::readKey() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == 1) {
        return c;
    }
    return 0;
}

#else  // Windows

void ConsoleInputHandler::enableRawMode() {
    // Windows uses _kbhit and _getch which don't need raw mode setup
}

void ConsoleInputHandler::disableRawMode() {
    // Nothing to do on Windows
}

bool ConsoleInputHandler::keyPressed() {
    return _kbhit() != 0;
}

char ConsoleInputHandler::readKey() {
    return static_cast<char>(_getch());
}

#endif

} // namespace input
} // namespace eqt
