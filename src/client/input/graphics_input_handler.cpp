#include "client/input/graphics_input_handler.h"
#include "client/input/hotkey_manager.h"
#include "common/logging.h"

#ifdef EQT_HAS_GRAPHICS
#include "client/graphics/irrlicht_renderer.h"
#endif

namespace eqt {
namespace input {

GraphicsInputHandler::GraphicsInputHandler(EQT::Graphics::RendererEventReceiver* eventReceiver)
    : m_eventReceiver(eventReceiver) {
}

void GraphicsInputHandler::update() {
    updateFromEventReceiver();
}

bool GraphicsInputHandler::isActive() const {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver != nullptr && !m_eventReceiver->quitRequested();
#else
    return false;
#endif
}

void GraphicsInputHandler::updateFromEventReceiver() {
#ifdef EQT_HAS_GRAPHICS
    if (!m_eventReceiver) return;

    // Reset movement state
    m_state.moveForward = false;
    m_state.moveBackward = false;
    m_state.strafeLeft = false;
    m_state.strafeRight = false;
    m_state.turnLeft = false;
    m_state.turnRight = false;
    m_state.shiftHeld = m_eventReceiver->isKeyDown(irr::KEY_LSHIFT) ||
                        m_eventReceiver->isKeyDown(irr::KEY_RSHIFT);

    // Update movement state from held keys using HotkeyManager
    auto& hotkeyMgr = HotkeyManager::instance();
    for (int keyCode = 0; keyCode < irr::KEY_KEY_CODES_COUNT; ++keyCode) {
        if (m_eventReceiver->isKeyDown(static_cast<irr::EKEY_CODE>(keyCode))) {
            HotkeyAction action;
            if (hotkeyMgr.isMovementKey(static_cast<irr::EKEY_CODE>(keyCode), action)) {
                switch (action) {
                    case HotkeyAction::MoveForward: m_state.moveForward = true; break;
                    case HotkeyAction::MoveBackward: m_state.moveBackward = true; break;
                    case HotkeyAction::StrafeLeft: m_state.strafeLeft = true; break;
                    case HotkeyAction::StrafeRight: m_state.strafeRight = true; break;
                    case HotkeyAction::TurnLeft: m_state.turnLeft = true; break;
                    case HotkeyAction::TurnRight: m_state.turnRight = true; break;
                    default: break;
                }
            }
        }
    }

    // Update mouse state
    m_state.mouseX = m_eventReceiver->getMouseX();
    m_state.mouseY = m_eventReceiver->getMouseY();
    m_state.mouseDeltaX = m_eventReceiver->getMouseDeltaX();
    m_state.mouseDeltaY = m_eventReceiver->getMouseDeltaY();
    m_state.leftButtonDown = m_eventReceiver->isLeftButtonDown();
    m_state.rightButtonDown = m_eventReceiver->isRightButtonDown();
    m_state.leftButtonClicked = m_eventReceiver->wasLeftButtonClicked();
    m_state.leftButtonReleased = m_eventReceiver->wasLeftButtonReleased();
    m_state.clickMouseX = m_eventReceiver->getClickMouseX();
    m_state.clickMouseY = m_eventReceiver->getClickMouseY();

    // Check for quit
    if (m_eventReceiver->quitRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::Quit)] = true;
    }

    // Drain bridge queue and map RendererAction -> InputAction
    // Game actions are routed to bridgeQueue_ (not actionQueue_) to avoid
    // double-consumption with processFrame()'s drainActions().
    using RA = EQT::Graphics::RendererAction;
    bool chatFocused = m_eventReceiver->isChatInputFocused();
    auto bridgeActions = m_eventReceiver->drainBridgeActions();
    for (const auto& event : bridgeActions) {
        switch (event.action) {
            // Targeting works even when chat is focused
            case RA::TargetSelf:
                m_pendingActions[static_cast<size_t>(InputAction::TargetSelf)] = true; break;
            case RA::TargetGroupMember1:
                m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember1)] = true; break;
            case RA::TargetGroupMember2:
                m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember2)] = true; break;
            case RA::TargetGroupMember3:
                m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember3)] = true; break;
            case RA::TargetGroupMember4:
                m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember4)] = true; break;
            case RA::TargetGroupMember5:
                m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember5)] = true; break;
            case RA::TargetNearestPC:
                m_pendingActions[static_cast<size_t>(InputAction::TargetNearestPC)] = true; break;
            case RA::TargetNearestNPC:
                m_pendingActions[static_cast<size_t>(InputAction::TargetNearestNPC)] = true; break;
            case RA::CycleTargets:
                m_pendingActions[static_cast<size_t>(InputAction::CycleTargets)] = true; break;
            case RA::CycleTargetsReverse:
                m_pendingActions[static_cast<size_t>(InputAction::CycleTargetsReverse)] = true; break;

            // Non-targeting actions are gated by chat focus
            case RA::ToggleAutorun:
                if (!chatFocused) m_pendingActions[static_cast<size_t>(InputAction::ToggleAutorun)] = true;
                break;
            case RA::ToggleAutoAttack:
                if (!chatFocused) m_pendingActions[static_cast<size_t>(InputAction::ToggleAutoAttack)] = true;
                break;
            case RA::Hail:
                if (!chatFocused) m_pendingActions[static_cast<size_t>(InputAction::Hail)] = true;
                break;
            case RA::Consider:
                if (!chatFocused) m_pendingActions[static_cast<size_t>(InputAction::Consider)] = true;
                break;
            case RA::ClearTarget:
                if (!chatFocused) m_pendingActions[static_cast<size_t>(InputAction::ClearTarget)] = true;
                break;
            default:
                break;
        }
    }

    // Chat input keys
    if (m_eventReceiver->enterKeyPressed()) {
        m_pendingActions[static_cast<size_t>(InputAction::OpenChat)] = true;
    }
    if (m_eventReceiver->slashKeyPressed()) {
        m_pendingActions[static_cast<size_t>(InputAction::OpenChatSlash)] = true;
    }
    if (m_eventReceiver->escapeKeyPressed()) {
        m_pendingActions[static_cast<size_t>(InputAction::CloseChat)] = true;
    }

    // Check for jump (space key)
    if (m_eventReceiver->wasKeyPressed(irr::KEY_SPACE)) {
        m_pendingActions[static_cast<size_t>(InputAction::Jump)] = true;
    }
#endif
}

bool GraphicsInputHandler::hasAction(InputAction action) const {
    if (action == InputAction::Count) {
        return false;
    }
    return m_pendingActions[static_cast<size_t>(action)];
}

bool GraphicsInputHandler::consumeAction(InputAction action) {
    if (action == InputAction::Count) {
        return false;
    }
    size_t index = static_cast<size_t>(action);
    bool was_pending = m_pendingActions[index];
    m_pendingActions[index] = false;
    return was_pending;
}

void GraphicsInputHandler::resetMouseDeltas() {
    m_state.mouseDeltaX = 0;
    m_state.mouseDeltaY = 0;
}

std::optional<SpellCastRequest> GraphicsInputHandler::consumeSpellCastRequest() {
#ifdef EQT_HAS_GRAPHICS
    if (!m_eventReceiver) return std::nullopt;

    int8_t gem = m_eventReceiver->getSpellGemCastRequest();
    if (gem >= 0 && gem < 8) {
        return SpellCastRequest{static_cast<uint8_t>(gem)};
    }
#endif
    return std::nullopt;
}

std::optional<HotbarRequest> GraphicsInputHandler::consumeHotbarRequest() {
#ifdef EQT_HAS_GRAPHICS
    if (!m_eventReceiver) return std::nullopt;

    int8_t slot = m_eventReceiver->getHotbarActivationRequest();
    if (slot >= 0 && slot < 10) {
        return HotbarRequest{static_cast<uint8_t>(slot)};
    }
#endif
    return std::nullopt;
}

std::optional<TargetRequest> GraphicsInputHandler::consumeTargetRequest() {
    if (m_targetRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_targetRequests.front();
    m_targetRequests.pop();
    return request;
}

std::optional<LootRequest> GraphicsInputHandler::consumeLootRequest() {
    if (m_lootRequests.empty()) {
        return std::nullopt;
    }
    auto request = m_lootRequests.front();
    m_lootRequests.pop();
    return request;
}

bool GraphicsInputHandler::hasPendingKeyEvents() const {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver && m_eventReceiver->hasPendingKeyEvents();
#else
    return false;
#endif
}

std::optional<KeyEvent> GraphicsInputHandler::popKeyEvent() {
#ifdef EQT_HAS_GRAPHICS
    if (!m_eventReceiver || !m_eventReceiver->hasPendingKeyEvents()) {
        return std::nullopt;
    }

    auto irrEvent = m_eventReceiver->popKeyEvent();
    KeyEvent event;
    event.keyCode = static_cast<uint32_t>(irrEvent.key);
    event.character = irrEvent.character;
    event.shift = irrEvent.shift;
    event.ctrl = irrEvent.ctrl;
    return event;
#else
    return std::nullopt;
#endif
}

void GraphicsInputHandler::clearPendingKeyEvents() {
#ifdef EQT_HAS_GRAPHICS
    if (m_eventReceiver) {
        m_eventReceiver->clearPendingKeyEvents();
    }
#endif
}

void GraphicsInputHandler::injectTargetRequest(uint16_t spawnId) {
    m_targetRequests.push(TargetRequest{spawnId});
}

void GraphicsInputHandler::injectLootRequest(uint16_t corpseId) {
    m_lootRequests.push(LootRequest{corpseId});
}

// Graphics-specific debug adjustments

float GraphicsInputHandler::consumeAnimSpeedDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getAnimSpeedDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeAmbientLightDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getAmbientLightDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeCorpseZOffsetDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getCorpseZOffsetDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeEyeHeightDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getEyeHeightDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeParticleMultiplierDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getParticleMultiplierDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeCollisionHeightDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getCollisionHeightDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeStepHeightDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getStepHeightDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeOffsetXDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getOffsetXDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeOffsetYDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getOffsetYDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeOffsetZDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getOffsetZDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeRotationXDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getRotationXDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeRotationYDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getRotationYDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeRotationZDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getRotationZDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeHelmUOffsetDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getHelmUOffsetDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeHelmVOffsetDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getHelmVOffsetDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeHelmUScaleDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getHelmUScaleDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeHelmVScaleDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getHelmVScaleDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeHelmRotationDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getHelmRotationDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

bool GraphicsInputHandler::consumeHelmUVSwapRequest() {
    // Now handled via action queue in IrrlichtRenderer::dispatchRendererAction()
    return false;
}

bool GraphicsInputHandler::consumeHelmVFlipRequest() {
    return false;
}

bool GraphicsInputHandler::consumeHelmUFlipRequest() {
    return false;
}

bool GraphicsInputHandler::consumeHelmResetRequest() {
    return false;
}

bool GraphicsInputHandler::consumeHelmPrintStateRequest() {
    return false;
}

int GraphicsInputHandler::consumeHeadVariantCycleDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getHeadVariantCycleDelta() : 0;
#else
    return 0;
#endif
}

float GraphicsInputHandler::consumeRepairRotateXDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getRepairRotateXDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeRepairRotateYDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getRepairRotateYDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

float GraphicsInputHandler::consumeRepairRotateZDelta() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->getRepairRotateZDelta() : 0.0f;
#else
    return 0.0f;
#endif
}

bool GraphicsInputHandler::consumeRepairFlipXRequest() {
    // Now handled via action queue in IrrlichtRenderer::dispatchRendererAction()
    return false;
}

bool GraphicsInputHandler::consumeRepairFlipYRequest() {
    return false;
}

bool GraphicsInputHandler::consumeRepairFlipZRequest() {
    return false;
}

bool GraphicsInputHandler::consumeRepairResetRequest() {
    return false;
}

} // namespace input
} // namespace eqt
