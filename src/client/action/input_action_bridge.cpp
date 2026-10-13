#include "client/action/input_action_bridge.h"
#include "client/action/command_processor.h"
#include "client/hotbar/hotbar_model.h"  // T174: read slot content type
#include "client/hotbar/hotbar_spell.h"
#include "client/state/game_state.h"
#include "common/logging.h"

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
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] InputActionBridge::update: SKIPPED (enabled={}, input={})",
            m_enabled, (m_input != nullptr));
        return;
    }

    // Update the input handler first
    m_input->update();

    // Process discrete one-shot actions
    processDiscreteActions();

    // Movement, keyboard turning and mouse-look are handled by the renderer
    // (console or 3D), which applies collision and pushes PlayerPositionChanged
    // intents via the bridge. The bridge-side processContinuousInput() /
    // processMouseInput() paths that duplicated this were deleted in T228:
    // the first had no callers, the second's mouse-look branch never fired
    // (the render thread consumes the same destructive mouse delta first)
    // and would have turned the player from an unprojected heading of 0 had
    // it ever run (T218 probe, refactor/deferred.md).
    (void)deltaTime;

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
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] InputActionBridge: consumed ToggleAutorun");
        reportAction("ToggleAutorun", m_dispatcher.toggleAutorun());
    }

    if (m_input->consumeAction(input::InputAction::Jump)) {
        reportAction("Jump", m_dispatcher.jump());
    }

    // Combat actions
    if (m_input->consumeAction(input::InputAction::ToggleAutoAttack)) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] InputActionBridge: consumed ToggleAutoAttack");
        reportAction("ToggleAutoAttack", m_dispatcher.toggleAutoAttack());
    }

    if (m_input->consumeAction(input::InputAction::Attack)) {
        reportAction("Attack", m_dispatcher.enableAutoAttack());
    }

    // B900 (#216): one ranged shot. The dispatcher gates zone-connection; the
    // combat manager does the no-target / no-range-weapon handling.
    if (m_input->consumeAction(input::InputAction::RangeAttack)) {
        reportAction("RangeAttack", m_dispatcher.rangedAttack());
    }

    if (m_input->consumeAction(input::InputAction::ClearTarget)) {
        reportAction("ClearTarget", m_dispatcher.clearTarget());
    }

    if (m_input->consumeAction(input::InputAction::Consider)) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] InputActionBridge: consumed Consider");
        reportAction("Consider", m_dispatcher.consider());
    }

    // Targeting actions
    if (m_input->consumeAction(input::InputAction::TargetSelf)) {
        reportAction("TargetSelf", m_dispatcher.targetSelf());
    }

    if (m_input->consumeAction(input::InputAction::TargetGroupMember1)) {
        reportAction("TargetGroupMember1", m_dispatcher.targetGroupMember(0));
    }

    if (m_input->consumeAction(input::InputAction::TargetGroupMember2)) {
        reportAction("TargetGroupMember2", m_dispatcher.targetGroupMember(1));
    }

    if (m_input->consumeAction(input::InputAction::TargetGroupMember3)) {
        reportAction("TargetGroupMember3", m_dispatcher.targetGroupMember(2));
    }

    if (m_input->consumeAction(input::InputAction::TargetGroupMember4)) {
        reportAction("TargetGroupMember4", m_dispatcher.targetGroupMember(3));
    }

    if (m_input->consumeAction(input::InputAction::TargetGroupMember5)) {
        reportAction("TargetGroupMember5", m_dispatcher.targetGroupMember(4));
    }

    if (m_input->consumeAction(input::InputAction::TargetNearestPC)) {
        reportAction("TargetNearestPC", m_dispatcher.targetNearestPC());
    }

    if (m_input->consumeAction(input::InputAction::TargetNearestNPC)) {
        reportAction("TargetNearestNPC", m_dispatcher.targetNearestNPC());
    }

    if (m_input->consumeAction(input::InputAction::CycleTargets)) {
        reportAction("CycleTargets", m_dispatcher.cycleTargets());
    }

    if (m_input->consumeAction(input::InputAction::CycleTargetsReverse)) {
        reportAction("CycleTargetsReverse", m_dispatcher.cycleTargetsReverse());
    }

    // Interaction
    if (m_input->consumeAction(input::InputAction::Hail)) {
        LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] InputActionBridge: consumed Hail");
        // hailTarget() checks for target internally via CombatManager and falls back to plain hail
        reportAction("Hail", m_dispatcher.hailTarget());
    }

    if (m_input->consumeAction(input::InputAction::InteractDoor)) {
        reportAction("ClickNearestDoor", m_dispatcher.clickNearestDoor());
    }

    if (m_input->consumeAction(input::InputAction::InteractWorldObject)) {
        // World object interaction requires a drop ID, use nearest
        reportAction("InteractWorldObject", m_dispatcher.interactNearestWorldObject());
    }

    if (m_input->consumeAction(input::InputAction::Interact)) {
        // Unified interact - find nearest interactable (door, object, or NPC)
        reportAction("Interact", m_dispatcher.interactNearest());
    }

    if (m_input->consumeAction(input::InputAction::ReplyToTell)) {
        // Reply to last tell - opens chat with pre-filled /tell command
        // This is handled by the chat system, not directly through dispatcher
        reportAction("ReplyToTell", m_dispatcher.openReplyToTell());
    }
}

void InputActionBridge::processConsoleInput() {
    // T29: prefer the dedicated console command source (kept alive and
    // drained regardless of which handler setInputHandler() currently
    // points at -- see setConsoleCommandSource()); fall back to m_input so
    // headless mode, where they're the same ConsoleInputHandler instance
    // anyway, behaves exactly as before if the caller never wires the
    // separate source.
    input::IInputHandler* source = m_consoleCommandSource ? m_consoleCommandSource : m_input;
    if (!source || (!m_unifiedDispatcher && !m_commandProcessor)) {
        // No dispatcher of any kind wired up -- leave the queue alone
        // rather than silently draining and discarding commands.
        return;
    }

    // Process raw commands from console input
    while (auto rawCmd = source->consumeRawCommand()) {
        if (m_unifiedDispatcher) {
            m_unifiedDispatcher(*rawCmd);
        } else if (m_commandProcessor) {
            // Legacy fallback (pre-T29 behavior): CommandProcessor only.
            m_commandProcessor->processCommand(*rawCmd);
        }
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
        // SpellCastRequest.gemSlot is 0-based (console pushes gem-1, graphics
        // accepts clicks 0-7) while the dispatcher's contract is 1-based
        // (validates 1-12, handler does gemSlot-1). Convert here, like the
        // hotbar path — forwarding it unchanged made gem 1 unrequestable and
        // cast every other gem one low (T15 finding, fixed in T16).
        reportAction("CastSpell", m_dispatcher.castSpell(spellReq->gemSlot + 1));
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
        // HotbarRequest.slot is the 0-based hotbar position (0-9), which is
        // also the index into HotbarModel.
        const int slotIndex = hotbarReq->slot;

        if (!m_hotbarModel) continue;

        // T174: route by the slot's ACTUAL content type, read from the one
        // HotbarModel the rest of the client uses (the mouse path in
        // eq_bridge.cpp switches on this same enum). No default: -Wswitch then
        // catches the next enumerator that grows a keyboard route.
        const eqt::HotbarSlot& slot = m_hotbarModel->getSlot({m_hotbarModel->currentPage(), slotIndex});
        switch (slot.type) {
            case eqt::HotbarSlotType::Spell:
                // A hotbar Spell stores a spell ID, not a gem position.
                // FindHotbarSpellGem is shared with HotbarActivateIntent.
                if (const int gem = eqt::FindHotbarSpellGem(m_state.spells(), slot.id); gem >= 0) {
                    reportAction("CastSpell",
                        m_dispatcher.beginCastFromGem(static_cast<uint8_t>(gem + 1)));
                }
                break;
            case eqt::HotbarSlotType::Skill:
                // slot.id is a skill id; passed straight through, no offset.
                reportAction("UseSkill", m_dispatcher.useSkill(slot.id));
                break;
            case eqt::HotbarSlotType::Item:
                // T230: slot.id is an ITEM id; ActionDispatcher::useItemById()
                // resolves it to an INVENTORY slot through the handler
                // (IActionHandler::findItemSlotByItemId, the same
                // InventoryManager lookup the mouse path does) and only then
                // calls useItem(slot). Not carried -> Failure("Item not found
                // in inventory"), nothing sent. Was a logged skip (T174).
                reportAction("UseItem", m_dispatcher.useItemById(slot.id));
                break;
            case eqt::HotbarSlotType::Emote:
            case eqt::HotbarSlotType::Macro:
            case eqt::HotbarSlotType::Combat:
                // T239: command-type slots. Emote/Macro carry their command
                // line in slot.emoteText; Combat carries nothing. The
                // dispatcher gates type and empty text, the handler runs the
                // same body as the mouse path
                // (EverQuest::ActivateHotbarCommandSlot). Was a logged skip
                // (T174, T230).
                reportAction("HotbarCommand",
                    m_dispatcher.activateHotbarCommandSlot(slot.type, slot.emoteText));
                break;
            case eqt::HotbarSlotType::Empty:
                // Nothing to fire. Log and skip.
                LOG_DEBUG(MOD_INPUT,
                    "[INPUT-TRACE] processHotbarRequests: hotbar slot {} type {} "
                    "has no keyboard dispatch route; skipping",
                    slotIndex, eqt::hotbarSlotTypeName(slot.type));
                break;
        }
    }
}

void InputActionBridge::reportAction(const std::string& name, const ActionResult& result) {
    LOG_DEBUG(MOD_INPUT, "[INPUT-TRACE] InputActionBridge::reportAction: {} -> success={}, msg='{}'",
        name, result.success, result.message);
    if (m_actionCallback) {
        m_actionCallback(name, result);
    }
}

} // namespace action
} // namespace eqt
