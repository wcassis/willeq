#include "client/action/input_action_bridge.h"
#include "client/action/command_processor.h"
#include "client/state/game_state.h"

namespace eqt {
namespace action {

InputActionBridge::InputActionBridge(state::GameState& state, ActionDispatcher& dispatcher)
    : m_state(state)
    , m_dispatcher(dispatcher) {
}

InputActionBridge::~InputActionBridge() = default;

void InputActionBridge::setInputHandler(input::IInputHandler* input) {
    m_input = input;
}

void InputActionBridge::setCommandProcessor(CommandProcessor* processor) {
    m_commandProcessor = processor;
}

void InputActionBridge::update(float deltaTime) {
    if (!m_enabled || !m_input) {
        return;
    }

    // Update the input handler first
    m_input->update();

    // Process discrete one-shot actions
    processDiscreteActions();

    // Process continuous input (movement, turning)
    processContinuousInput(deltaTime);

    // Process mouse input (camera, targeting)
    processMouseInput(deltaTime);

    // Process console input (commands, chat)
    processConsoleInput();

    // Process typed input from input handler
    processChatMessages();
    processMoveCommands();
    processSpellCasts();
    processTargetRequests();
    processLootRequests();
    processHotbarRequests();
}

void InputActionBridge::processDiscreteActions() {
    // System actions
    if (m_input->consumeAction(input::InputAction::Quit)) {
        // Quit is handled at a higher level, not through dispatcher
    }

    if (m_input->consumeAction(input::InputAction::Screenshot)) {
        // Screenshot is handled by renderer, not through dispatcher
    }

    // Movement toggles
    if (m_input->consumeAction(input::InputAction::ToggleAutorun)) {
        reportAction("ToggleAutorun", m_dispatcher.toggleAutorun());
    }

    if (m_input->consumeAction(input::InputAction::Jump)) {
        reportAction("Jump", m_dispatcher.jump());
    }

    // Combat actions
    if (m_input->consumeAction(input::InputAction::ToggleAutoAttack)) {
        reportAction("ToggleAutoAttack", m_dispatcher.toggleAutoAttack());
    }

    if (m_input->consumeAction(input::InputAction::ClearTarget)) {
        reportAction("ClearTarget", m_dispatcher.clearTarget());
    }

    // Interaction
    if (m_input->consumeAction(input::InputAction::Hail)) {
        if (m_state.combat().hasTarget()) {
            reportAction("HailTarget", m_dispatcher.hailTarget());
        } else {
            reportAction("Hail", m_dispatcher.hail());
        }
    }

    if (m_input->consumeAction(input::InputAction::InteractDoor)) {
        reportAction("ClickNearestDoor", m_dispatcher.clickNearestDoor());
    }
}

void InputActionBridge::processContinuousInput(float deltaTime) {
    // Update movement state based on current input
    updateMovementState();

    // Get current movement input state
    const auto& inputState = m_input->getState();

    // Handle forward/backward movement
    bool wantForward = inputState.moveForward;
    bool wantBackward = inputState.moveBackward;

    if (wantForward && !m_movingForward) {
        m_dispatcher.startMoving(Direction::Forward);
        m_movingForward = true;
    } else if (!wantForward && m_movingForward) {
        m_dispatcher.stopMoving(Direction::Forward);
        m_movingForward = false;
    }

    if (wantBackward && !m_movingBackward) {
        m_dispatcher.startMoving(Direction::Backward);
        m_movingBackward = true;
    } else if (!wantBackward && m_movingBackward) {
        m_dispatcher.stopMoving(Direction::Backward);
        m_movingBackward = false;
    }

    // Handle strafing
    bool wantStrafeLeft = inputState.strafeLeft;
    bool wantStrafeRight = inputState.strafeRight;

    if (wantStrafeLeft && !m_strafingLeft) {
        m_dispatcher.startMoving(Direction::Left);
        m_strafingLeft = true;
    } else if (!wantStrafeLeft && m_strafingLeft) {
        m_dispatcher.stopMoving(Direction::Left);
        m_strafingLeft = false;
    }

    if (wantStrafeRight && !m_strafingRight) {
        m_dispatcher.startMoving(Direction::Right);
        m_strafingRight = true;
    } else if (!wantStrafeRight && m_strafingRight) {
        m_dispatcher.stopMoving(Direction::Right);
        m_strafingRight = false;
    }

    // Handle keyboard turning
    bool wantTurnLeft = inputState.turnLeft;
    bool wantTurnRight = inputState.turnRight;

    if (wantTurnLeft || wantTurnRight) {
        // Calculate turn amount
        float turnAmount = m_turnSpeed * deltaTime;
        float currentHeading = m_state.player().heading();

        if (wantTurnLeft) {
            currentHeading -= turnAmount;
        }
        if (wantTurnRight) {
            currentHeading += turnAmount;
        }

        // Normalize heading
        while (currentHeading < 0) currentHeading += 360.0f;
        while (currentHeading >= 360.0f) currentHeading -= 360.0f;

        m_dispatcher.setHeading(currentHeading);
    }

    m_turningLeft = wantTurnLeft;
    m_turningRight = wantTurnRight;
}

void InputActionBridge::processMouseInput(float deltaTime) {
    const auto& inputState = m_input->getState();

    // Handle mouse look (camera rotation)
    if (inputState.leftButtonDown) {
        float deltaX = static_cast<float>(inputState.mouseDeltaX) * m_mouseSensitivity;
        float deltaY = static_cast<float>(inputState.mouseDeltaY) * m_mouseSensitivity;

        if (m_invertMouseY) {
            deltaY = -deltaY;
        }

        // Apply horizontal rotation to heading
        if (deltaX != 0.0f) {
            float currentHeading = m_state.player().heading();
            currentHeading += deltaX * 0.1f;  // Sensitivity adjustment

            while (currentHeading < 0) currentHeading += 360.0f;
            while (currentHeading >= 360.0f) currentHeading -= 360.0f;

            m_dispatcher.setHeading(currentHeading);
        }

        // Vertical rotation would be handled by camera controller, not player heading
    }
}

void InputActionBridge::processConsoleInput() {
    if (!m_input || !m_commandProcessor) {
        return;
    }

    // Process raw commands from console input
    while (auto rawCmd = m_input->consumeRawCommand()) {
        m_commandProcessor->processCommand(*rawCmd);
    }
}

void InputActionBridge::processChatMessages() {
    if (!m_input) return;

    while (auto chatMsg = m_input->consumeChatMessage()) {
        // ChatMessage.channel is a string ("say", "tell", "shout", etc.)
        ChatChannel channel = ChatChannel::Say;

        if (chatMsg->channel == "say") {
            channel = ChatChannel::Say;
        } else if (chatMsg->channel == "shout") {
            channel = ChatChannel::Shout;
        } else if (chatMsg->channel == "ooc") {
            channel = ChatChannel::OOC;
        } else if (chatMsg->channel == "auction") {
            channel = ChatChannel::Auction;
        } else if (chatMsg->channel == "group" || chatMsg->channel == "gsay") {
            channel = ChatChannel::Group;
        } else if (chatMsg->channel == "guild" || chatMsg->channel == "gu") {
            channel = ChatChannel::Guild;
        } else if (chatMsg->channel == "tell") {
            if (!chatMsg->target.empty()) {
                reportAction("SendTell", m_dispatcher.sendTell(chatMsg->target, chatMsg->text));
            }
            continue;
        } else if (chatMsg->channel == "emote") {
            channel = ChatChannel::Emote;
        }

        reportAction("SendChat", m_dispatcher.sendChatMessage(channel, chatMsg->text));
    }
}

void InputActionBridge::processMoveCommands() {
    if (!m_input) return;

    while (auto moveCmd = m_input->consumeMoveCommand()) {
        switch (moveCmd->type) {
            case input::MoveCommand::Type::Coordinates:
                reportAction("MoveToLocation",
                    m_dispatcher.moveToLocation(moveCmd->x, moveCmd->y, moveCmd->z));
                break;
            case input::MoveCommand::Type::Entity:
                reportAction("MoveToEntity",
                    m_dispatcher.moveToEntity(moveCmd->entityName));
                break;
            case input::MoveCommand::Type::Face:
                if (!moveCmd->entityName.empty()) {
                    reportAction("FaceEntity",
                        m_dispatcher.faceEntity(moveCmd->entityName));
                } else {
                    reportAction("FaceLocation",
                        m_dispatcher.faceLocation(moveCmd->x, moveCmd->y, moveCmd->z));
                }
                break;
            case input::MoveCommand::Type::Stop:
                reportAction("StopMovement", m_dispatcher.stopAllMovement());
                break;
        }
    }
}

void InputActionBridge::processSpellCasts() {
    if (!m_input) return;

    while (auto spellReq = m_input->consumeSpellCastRequest()) {
        // SpellCastRequest only has gemSlot, so we always cast on current target
        reportAction("CastSpell", m_dispatcher.castSpell(spellReq->gemSlot));
    }
}

void InputActionBridge::processTargetRequests() {
    if (!m_input) return;

    while (auto targetReq = m_input->consumeTargetRequest()) {
        // TargetRequest only has spawnId
        if (targetReq->spawnId != 0) {
            reportAction("TargetEntity", m_dispatcher.targetEntity(targetReq->spawnId));
        }
    }
}

void InputActionBridge::processLootRequests() {
    if (!m_input) return;

    while (auto lootReq = m_input->consumeLootRequest()) {
        // LootRequest only has corpseId, so we open the loot window
        reportAction("LootCorpse", m_dispatcher.lootCorpse(lootReq->corpseId));
    }
}

void InputActionBridge::processHotbarRequests() {
    if (!m_input) return;

    while (auto hotbarReq = m_input->consumeHotbarRequest()) {
        // Hotbar can contain spells, abilities, items, etc.
        // For now, we treat all hotbar slots as spell gems (1-indexed)
        reportAction("CastSpell", m_dispatcher.castSpell(hotbarReq->slot + 1));
    }
}

void InputActionBridge::reportAction(const std::string& name, const ActionResult& result) {
    if (m_actionCallback) {
        m_actionCallback(name, result);
    }
}

void InputActionBridge::updateMovementState() {
    // This method can be used to sync internal state with input state
    // Currently, the state is updated in processContinuousInput
}

} // namespace action
} // namespace eqt
