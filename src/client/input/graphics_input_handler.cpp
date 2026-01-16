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

    // Check for toggle requests from event receiver
    if (m_eventReceiver->quitRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::Quit)] = true;
    }
    if (m_eventReceiver->screenshotRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::Screenshot)] = true;
    }
    if (m_eventReceiver->wireframeToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleWireframe)] = true;
    }
    if (m_eventReceiver->hudToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleHUD)] = true;
    }
    if (m_eventReceiver->nameTagToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleNameTags)] = true;
    }
    if (m_eventReceiver->zoneLightsToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleZoneLights)] = true;
    }
    if (m_eventReceiver->cycleObjectLightsRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::CycleObjectLights)] = true;
    }
    if (m_eventReceiver->cameraModeToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleCameraMode)] = true;
    }
    if (m_eventReceiver->oldModelsToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleOldModels)] = true;
    }
    if (m_eventReceiver->rendererModeToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleRendererMode)] = true;
    }
    if (m_eventReceiver->autorunToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleAutorun)] = true;
    }
    if (m_eventReceiver->collisionToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleCollision)] = true;
    }
    if (m_eventReceiver->collisionDebugToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleCollisionDebug)] = true;
    }
    if (m_eventReceiver->lightingToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleLighting)] = true;
    }
    if (m_eventReceiver->helmDebugToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleHelmDebug)] = true;
    }
    if (m_eventReceiver->saveEntitiesRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::SaveEntities)] = true;
    }
    if (m_eventReceiver->clearTargetRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ClearTarget)] = true;
    }

    // Targeting actions
    if (m_eventReceiver->targetSelfRequested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetSelfRequested() returned true, setting TargetSelf action");
        m_pendingActions[static_cast<size_t>(InputAction::TargetSelf)] = true;
    }
    if (m_eventReceiver->targetGroupMember1Requested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetGroupMember1Requested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember1)] = true;
    }
    if (m_eventReceiver->targetGroupMember2Requested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetGroupMember2Requested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember2)] = true;
    }
    if (m_eventReceiver->targetGroupMember3Requested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetGroupMember3Requested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember3)] = true;
    }
    if (m_eventReceiver->targetGroupMember4Requested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetGroupMember4Requested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember4)] = true;
    }
    if (m_eventReceiver->targetGroupMember5Requested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetGroupMember5Requested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetGroupMember5)] = true;
    }
    if (m_eventReceiver->targetNearestPCRequested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetNearestPCRequested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetNearestPC)] = true;
    }
    if (m_eventReceiver->targetNearestNPCRequested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: targetNearestNPCRequested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::TargetNearestNPC)] = true;
    }
    if (m_eventReceiver->cycleTargetsRequested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: cycleTargetsRequested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::CycleTargets)] = true;
    }
    if (m_eventReceiver->cycleTargetsReverseRequested()) {
        LOG_DEBUG(MOD_INPUT, "GraphicsInputHandler: cycleTargetsReverseRequested() returned true");
        m_pendingActions[static_cast<size_t>(InputAction::CycleTargetsReverse)] = true;
    }

    if (m_eventReceiver->autoAttackToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleAutoAttack)] = true;
    }
    if (m_eventReceiver->inventoryToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleInventory)] = true;
    }
    if (m_eventReceiver->groupToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleGroup)] = true;
    }
    if (m_eventReceiver->doorInteractRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::InteractDoor)] = true;
    }
    if (m_eventReceiver->hailRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::Hail)] = true;
    }
    if (m_eventReceiver->vendorToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleVendor)] = true;
    }
    if (m_eventReceiver->skillsToggleRequested()) {
        m_pendingActions[static_cast<size_t>(InputAction::ToggleSkills)] = true;
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
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->helmUVSwapRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeHelmVFlipRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->helmVFlipRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeHelmUFlipRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->helmUFlipRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeHelmResetRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->helmResetRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeHelmPrintStateRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->helmPrintStateRequested() : false;
#else
    return false;
#endif
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
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->repairFlipXRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeRepairFlipYRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->repairFlipYRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeRepairFlipZRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->repairFlipZRequested() : false;
#else
    return false;
#endif
}

bool GraphicsInputHandler::consumeRepairResetRequest() {
#ifdef EQT_HAS_GRAPHICS
    return m_eventReceiver ? m_eventReceiver->repairResetRequested() : false;
#else
    return false;
#endif
}

} // namespace input
} // namespace eqt
