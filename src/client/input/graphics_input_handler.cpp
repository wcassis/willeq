#include "client/input/graphics_input_handler.h"
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

    // T315: everything that used to happen here -- the movement-key scan,
    // the shift check, the mouse position, the button states and the two
    // click one-shots -- wrote into InputState, a snapshot struct with no
    // reader anywhere in the tree. The struct is gone; so is this fill.
    //
    // Two of those reads were actively harmful, not merely wasted:
    //
    //   * getMouseDeltaX/Y() (deleted by T312) -- a destructive
    //     read-and-reset whose real consumer is the render thread's
    //     mouse-look in IrrlichtRenderer::updatePlayerMovement(). A game
    //     tick landing between two frames froze the camera for that frame.
    //   * wasLeftButtonClicked()/wasLeftButtonReleased() (deleted here) --
    //     the same shape. Their real consumer is
    //     IrrlichtRenderer::processFrameInput(), which drives
    //     pendingUIClick_, the static-UI hit test and handleMouseTargeting().
    //     Whenever this poll won the atomic exchange, the click was
    //     consumed and NO UI EVER SAW IT: an inventory slot, loot row or
    //     spell gem that silently did nothing.
    //
    // The rule, now pinned by tests/test_read_and_reset_ownership.cpp:
    // a destructive read-and-reset accessor gets exactly one caller unless
    // the accumulator is made per-consumer. Do not re-add a poll here.
    //
    // Movement is read where it is used: renderer_player_physics.cpp polls
    // isKeyDown() through the same HotkeyManager bindings, on the render
    // thread, non-destructively.

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
    if (!bridgeActions.empty()) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: draining bridgeQueue, got {} actions, chatFocused={}",
            bridgeActions.size(), chatFocused);
    }
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
            // T343 (B002): the RA::ToggleAutorun arm was DELETED here. Nothing
            // pushes that action onto the bridge queue any more -- autorun is
            // renderer-local movement state (PlayerMovementState::autorun,
            // read by the renderer's per-frame movement phase), so the
            // receiver queues it on actionQueue_ and processCommonInput()
            // toggles it directly. An arm here could only have set a pending
            // InputAction that ends in EqActionHandler::toggleAutorun(), which
            // cannot reach the flag from the game thread.
            case RA::ToggleAutoAttack:
                if (!chatFocused) { m_pendingActions[static_cast<size_t>(InputAction::ToggleAutoAttack)] = true;
                    LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: ToggleAutoAttack -> pendingAction set");
                } else { LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: ToggleAutoAttack BLOCKED by chatFocused"); }
                break;
            case RA::Hail:
                if (!chatFocused) { m_pendingActions[static_cast<size_t>(InputAction::Hail)] = true;
                    LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: Hail -> pendingAction set");
                } else { LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: Hail BLOCKED by chatFocused"); }
                break;
            case RA::Consider:
                if (!chatFocused) { m_pendingActions[static_cast<size_t>(InputAction::Consider)] = true;
                    LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: Consider -> pendingAction set");
                } else { LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: Consider BLOCKED by chatFocused"); }
                break;
            // T319: R key. Gated by chat focus like the other non-targeting
            // actions -- with the chat box open R is a typed character, and
            // OnEvent never decodes it as a hotkey anyway.
            case RA::ReplyToTell:
                if (!chatFocused) { m_pendingActions[static_cast<size_t>(InputAction::ReplyToTell)] = true;
                    LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: ReplyToTell -> pendingAction set");
                } else { LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: ReplyToTell BLOCKED by chatFocused"); }
                break;
            case RA::ClearTarget:
                if (!chatFocused) { m_pendingActions[static_cast<size_t>(InputAction::ClearTarget)] = true;
                    LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: ClearTarget -> pendingAction set");
                } else { LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] GraphicsInputHandler: ClearTarget BLOCKED by chatFocused"); }
                break;
            default:
                break;
        }
    }

    // T241: the Enter / slash / Escape reads that used to sit here were
    // deleted. They raced the render thread for the same one-shot flags
    // (RendererEventReceiver::enterKeyPressed() etc.) and the actions they
    // set (OpenChat / OpenChatSlash / CloseChat) had no consumer; chat
    // open/close and Escape target-clear are handled entirely in
    // IrrlichtRenderer::processChatInput() on the render thread.

    // Check for jump (space key) — skip when chat input is focused
    if (!m_eventReceiver->isChatInputFocused() && m_eventReceiver->wasKeyPressed(irr::KEY_SPACE)) {
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

void GraphicsInputHandler::injectTargetRequest(uint16_t spawnId) {
    m_targetRequests.push(TargetRequest{spawnId});
}

void GraphicsInputHandler::injectLootRequest(uint16_t corpseId) {
    m_lootRequests.push(LootRequest{corpseId});
}

} // namespace input
} // namespace eqt
