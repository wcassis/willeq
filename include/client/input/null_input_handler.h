#pragma once

#include "client/input/input_handler.h"

namespace eqt {
namespace input {

/**
 * NullInputHandler - Input handler for automated mode.
 *
 * This handler provides no user input. All queries return false/empty.
 * It's used when the client runs in fully automated mode where all
 * actions are driven by scripts or AI, not user input.
 *
 * Actions can be injected programmatically using the inject* methods.
 */
class NullInputHandler : public IInputHandler {
public:
    NullInputHandler() = default;
    ~NullInputHandler() override = default;

    // IInputHandler interface
    void update() override;
    bool isActive() const override { return m_active; }

    bool hasAction(InputAction action) const override;
    bool consumeAction(InputAction action) override;

    const InputState& getState() const override { return m_state; }
    void resetMouseDeltas() override;

    std::optional<SpellCastRequest> consumeSpellCastRequest() override;
    std::optional<HotbarRequest> consumeHotbarRequest() override;
    std::optional<TargetRequest> consumeTargetRequest() override;
    std::optional<LootRequest> consumeLootRequest() override;

    bool hasPendingKeyEvents() const override { return false; }
    std::optional<KeyEvent> popKeyEvent() override { return std::nullopt; }
    void clearPendingKeyEvents() override {}

    std::optional<ChatMessage> consumeChatMessage() override;
    std::optional<MoveCommand> consumeMoveCommand() override;
    std::optional<std::string> consumeRawCommand() override;

    // ========== Programmatic Action Injection ==========

    /**
     * Inject an action to be processed next frame.
     */
    void injectAction(InputAction action);

    /**
     * Inject a spell cast request.
     */
    void injectSpellCast(uint8_t gemSlot);

    /**
     * Inject a hotbar activation request.
     */
    void injectHotbar(uint8_t slot);

    /**
     * Inject a target request.
     */
    void injectTarget(uint16_t spawnId);

    /**
     * Inject a loot request.
     */
    void injectLoot(uint16_t corpseId);

    /**
     * Inject a chat message.
     */
    void injectChatMessage(const std::string& text,
                           const std::string& channel = "say",
                           const std::string& target = "");

    /**
     * Inject a move command.
     */
    void injectMoveCommand(float x, float y, float z);
    void injectMoveToEntity(const std::string& entityName);
    void injectFace(float x, float y, float z);
    void injectFaceEntity(const std::string& entityName);
    void injectStopMovement();

    /**
     * Inject a raw command.
     */
    void injectRawCommand(const std::string& command);

    /**
     * Set movement state directly (for scripted movement).
     */
    void setMoveForward(bool value) { m_state.moveForward = value; }
    void setMoveBackward(bool value) { m_state.moveBackward = value; }
    void setStrafeLeft(bool value) { m_state.strafeLeft = value; }
    void setStrafeRight(bool value) { m_state.strafeRight = value; }
    void setTurnLeft(bool value) { m_state.turnLeft = value; }
    void setTurnRight(bool value) { m_state.turnRight = value; }

    /**
     * Deactivate the handler (for shutdown).
     */
    void deactivate() { m_active = false; }

private:
    bool m_active = true;
    InputState m_state;

    // Pending actions (injected programmatically)
    bool m_pendingActions[static_cast<size_t>(InputAction::Count)] = {false};

    // Pending requests
    std::queue<SpellCastRequest> m_spellCastRequests;
    std::queue<HotbarRequest> m_hotbarRequests;
    std::queue<TargetRequest> m_targetRequests;
    std::queue<LootRequest> m_lootRequests;
    std::queue<ChatMessage> m_chatMessages;
    std::queue<MoveCommand> m_moveCommands;
    std::queue<std::string> m_rawCommands;
};

} // namespace input
} // namespace eqt
