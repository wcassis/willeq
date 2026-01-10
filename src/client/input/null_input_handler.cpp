#include "client/input/null_input_handler.h"

namespace eqt {
namespace input {

void NullInputHandler::update() {
    // Nothing to poll in null handler
}

bool NullInputHandler::hasAction(InputAction action) const {
    if (action == InputAction::Count) {
        return false;
    }
    return m_pendingActions[static_cast<size_t>(action)];
}

bool NullInputHandler::consumeAction(InputAction action) {
    if (action == InputAction::Count) {
        return false;
    }
    size_t index = static_cast<size_t>(action);
    bool was_pending = m_pendingActions[index];
    m_pendingActions[index] = false;
    return was_pending;
}

void NullInputHandler::resetMouseDeltas() {
    m_state.mouseDeltaX = 0;
    m_state.mouseDeltaY = 0;
}

std::optional<SpellCastRequest> NullInputHandler::consumeSpellCastRequest() {
    if (m_spellCastRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_spellCastRequests.front();
    m_spellCastRequests.pop();
    return request;
}

std::optional<HotbarRequest> NullInputHandler::consumeHotbarRequest() {
    if (m_hotbarRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_hotbarRequests.front();
    m_hotbarRequests.pop();
    return request;
}

std::optional<TargetRequest> NullInputHandler::consumeTargetRequest() {
    if (m_targetRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_targetRequests.front();
    m_targetRequests.pop();
    return request;
}

std::optional<LootRequest> NullInputHandler::consumeLootRequest() {
    if (m_lootRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_lootRequests.front();
    m_lootRequests.pop();
    return request;
}

std::optional<ChatMessage> NullInputHandler::consumeChatMessage() {
    if (m_chatMessages.empty()) {
        return std::nullopt;
    }
    auto message = m_chatMessages.front();
    m_chatMessages.pop();
    return message;
}

std::optional<MoveCommand> NullInputHandler::consumeMoveCommand() {
    if (m_moveCommands.empty()) {
        return std::nullopt;
    }
    auto command = m_moveCommands.front();
    m_moveCommands.pop();
    return command;
}

std::optional<std::string> NullInputHandler::consumeRawCommand() {
    if (m_rawCommands.empty()) {
        return std::nullopt;
    }
    auto command = m_rawCommands.front();
    m_rawCommands.pop();
    return command;
}

// ========== Programmatic Action Injection ==========

void NullInputHandler::injectAction(InputAction action) {
    if (action != InputAction::Count) {
        m_pendingActions[static_cast<size_t>(action)] = true;
    }
}

void NullInputHandler::injectSpellCast(uint8_t gemSlot) {
    m_spellCastRequests.push(SpellCastRequest{gemSlot});
}

void NullInputHandler::injectHotbar(uint8_t slot) {
    m_hotbarRequests.push(HotbarRequest{slot});
}

void NullInputHandler::injectTarget(uint16_t spawnId) {
    m_targetRequests.push(TargetRequest{spawnId});
}

void NullInputHandler::injectLoot(uint16_t corpseId) {
    m_lootRequests.push(LootRequest{corpseId});
}

void NullInputHandler::injectChatMessage(const std::string& text,
                                          const std::string& channel,
                                          const std::string& target) {
    m_chatMessages.push(ChatMessage{text, channel, target});
}

void NullInputHandler::injectMoveCommand(float x, float y, float z) {
    MoveCommand cmd;
    cmd.type = MoveCommand::Type::Coordinates;
    cmd.x = x;
    cmd.y = y;
    cmd.z = z;
    m_moveCommands.push(cmd);
}

void NullInputHandler::injectMoveToEntity(const std::string& entityName) {
    MoveCommand cmd;
    cmd.type = MoveCommand::Type::Entity;
    cmd.entityName = entityName;
    m_moveCommands.push(cmd);
}

void NullInputHandler::injectFace(float x, float y, float z) {
    MoveCommand cmd;
    cmd.type = MoveCommand::Type::Face;
    cmd.x = x;
    cmd.y = y;
    cmd.z = z;
    m_moveCommands.push(cmd);
}

void NullInputHandler::injectFaceEntity(const std::string& entityName) {
    MoveCommand cmd;
    cmd.type = MoveCommand::Type::Face;
    cmd.entityName = entityName;
    m_moveCommands.push(cmd);
}

void NullInputHandler::injectStopMovement() {
    MoveCommand cmd;
    cmd.type = MoveCommand::Type::Stop;
    m_moveCommands.push(cmd);
}

void NullInputHandler::injectRawCommand(const std::string& command) {
    m_rawCommands.push(command);
}

} // namespace input
} // namespace eqt
