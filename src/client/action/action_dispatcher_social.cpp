// ActionDispatcher -- tell, chat, group, raid, pet, hotbar, spellbook and recipe clusters.
//
// Split out of src/client/action/action_dispatcher.cpp by
// refactor/J25_action_dispatcher_split.md (move-only, 2026-09-02). The
// dispatcher's method definitions now live in three TUs:
// action_dispatcher.cpp, action_dispatcher_social.cpp and
// action_dispatcher_guild_misc.cpp; all are declared in
// include/client/action/action_dispatcher.h.
//
// WARNING: tests/test_reachability_sweep.cpp reads THIS FILE AS TEXT (it
// concatenates all three TUs to parse every ActionDispatcher:: definition,
// and excludes all three by path from its external-caller sweep). Moving a
// method into or out of this file requires updating that test.

#include "client/action/action_dispatcher.h"
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include "common/packets/common.h"  // T483: the gem counts
#include <cmath>
#include <limits>

namespace eqt {
namespace action {
ActionResult ActionDispatcher::replyToLastTell(const std::string& message) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (message.empty()) {
        return ActionResult::Failure("Empty message");
    }
    // T222: refuse visibly instead of letting the handler silently no-op
    // (deferred.md T100 entry). Wording: no eqstr_us.txt entry exists for
    // this case (searched "reply"/"no one"), so this is a client-authored
    // string in the official client's register.
    if (!m_handler->hasTellToReplyTo()) {
        return ActionResult::Failure("You have no one to reply to.");
    }

    m_handler->replyToLastTell(message);
    return ActionResult::Success();
}

// T316: this used to return Success("open_reply") -- a sentinel string, no
// sender, nothing opened, and success reported even when no tell had ever
// arrived. The old TODO said "implement when ChatState is added to
// GameState"; that premise is stale (GameState has been the single store
// since T288) and it was never the missing piece anyway. What was missing
// was the last tell sender's NAME at this layer: EverQuest has tracked it in
// m_last_tell_sender since T18, and IActionHandler::getLastTellSender()
// (T316) exposes it.
//
// Success now carries the text the chat input is meant to open prefilled
// with -- "/tell <Name> " -- rather than a sentinel; with no tell received
// it refuses in the same words /reply uses (T222).
ActionResult ActionDispatcher::openReplyToTell() {
    auto result = checkHandler();
    if (!result.success) return result;

    const std::string sender = m_handler->getLastTellSender();
    if (sender.empty()) {
        return ActionResult::Failure("You have no one to reply to.");
    }

    // The raw wire name, not a display name: this text is meant to be sent
    // back as a command, and that is the name the server matches on --
    // exactly what replyToLastTell() passes to SendChatMessage().
    return ActionResult::Success("/tell " + sender + " ");
}

// ========== Group Actions ==========

ActionResult ActionDispatcher::inviteToGroup(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("No player specified");
    }

    m_handler->inviteToGroup(playerName);
    return ActionResult::Success("Invited " + playerName + " to group");
}

ActionResult ActionDispatcher::inviteTarget() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // Target check is done by the handler which has the real target state
    m_handler->inviteTarget();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::acceptGroupInvite() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.group().hasPendingInvite()) {
        return ActionResult::Failure("No pending group invite");
    }

    m_handler->acceptGroupInvite();
    return ActionResult::Success("Accepted group invite");
}

ActionResult ActionDispatcher::declineGroupInvite() {
    auto result = checkHandler();
    if (!result.success) return result;

    if (!m_state.group().hasPendingInvite()) {
        return ActionResult::Failure("No pending group invite");
    }

    m_handler->declineGroupInvite();
    return ActionResult::Success("Declined group invite");
}

ActionResult ActionDispatcher::leaveGroup() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.group().inGroup()) {
        return ActionResult::Failure("Not in a group");
    }

    m_handler->leaveGroup();
    return ActionResult::Success("Left group");
}

// ========== Character State Actions ==========

ActionResult ActionDispatcher::setAFK(bool afk) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setAFK(afk);
    return ActionResult::Success(afk ? "AFK enabled" : "AFK disabled");
}

ActionResult ActionDispatcher::toggleAFK() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    bool newState = !m_state.player().isAFK();
    m_handler->setAFK(newState);
    return ActionResult::Success(newState ? "AFK enabled" : "AFK disabled");
}

ActionResult ActionDispatcher::setAnonymous(bool anon) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setAnonymous(anon);
    return ActionResult::Success(anon ? "Anonymous enabled" : "Anonymous disabled");
}

// ========== Inventory Actions ==========

ActionResult ActionDispatcher::useItem(int16_t slot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->useItem(slot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::useItemById(uint32_t itemId) {
    // T230: keyboard hotbar Item route. The id->slot hop happens HERE, in
    // the action layer, so the bridge never has to guess -- calling
    // useItem(itemId) would fire whatever sits in inventory slot #itemId.
    auto result = checkZoneConnection();
    if (!result.success) return result;

    const int16_t slot = m_handler->findItemSlotByItemId(itemId);
    if (slot < 0) {
        return ActionResult::Failure("Item not found in inventory");
    }
    m_handler->useItem(slot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::activateHotbarCommandSlot(HotbarSlotType type, const std::string& text) {
    // T239: keyboard hotbar Emote/Macro/Combat route. The type gate lives
    // here so a Spell/Skill/Item slot can never be run as command text.
    auto result = checkZoneConnection();
    if (!result.success) return result;

    switch (type) {
        case HotbarSlotType::Emote:
        case HotbarSlotType::Macro:
            if (text.empty()) {
                return ActionResult::Failure("Hotbar slot has no command");
            }
            break;
        case HotbarSlotType::Combat:
            break;
        case HotbarSlotType::Empty:
        case HotbarSlotType::Spell:
        case HotbarSlotType::Skill:
        case HotbarSlotType::Item:
            return ActionResult::Failure("Not a command hotbar slot");
    }
    m_handler->activateHotbarCommandSlot(type, text);
    return ActionResult::Success();
}

// ========== Spellbook Actions ==========

ActionResult ActionDispatcher::forgetSpell(uint8_t gemSlot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // T101: narrowed from 1-12 to 1-8 -- forgetSpell() now routes through
    // the real EQ::SpellManager (only 8 gems exist), fixing the stub
    // ("Would need to send forget spell packet") T97 found underneath this
    // dispatcher method. See the plan's Review: this is a repoint, not a
    // new method -- forgetSpell()'s signature/callers are unchanged, no
    // test asserted on the old stub's behavior.
    if (gemSlot < 1 || gemSlot > EQT::VISIBLE_SPELL_GEM_COUNT) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }

    m_handler->forgetSpell(gemSlot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::cancelBuff(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (args.empty()) {
        return ActionResult::Failure("Usage: /removebuff <slot# | spell name>");
    }

    // Slot validity and "is that slot even occupied" need the buff model,
    // which lives on the game side -- the handler answers both.
    m_handler->cancelBuff(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (gemSlot < 1 || gemSlot > EQT::VISIBLE_SPELL_GEM_COUNT) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }
    if (spellName.empty()) {
        return ActionResult::Failure("Usage: /mem <gem# 1-8> <spell_name>");
    }

    m_handler->memorizeSpellByName(spellName, gemSlot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::listMemorizedSpells() {
    // T101: read-only report, no zone requirement -- matches game's /gems,
    // which only checked for a spell manager, not a zone connection.
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->listMemorizedSpells();
    return ActionResult::Success();
}

// ========== Trade Actions ==========

ActionResult ActionDispatcher::requestTrade(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->requestTrade(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::acceptTrade() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->acceptTrade();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::cancelTrade() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->cancelTrade();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::declineTrade() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->declineTrade();
    return ActionResult::Success();
}

// ========== Pet Actions ==========

ActionResult ActionDispatcher::sendPetCommand(uint8_t command, uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(command, targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::dismissPet() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->dismissPet();
    return ActionResult::Success("Dismissing pet");
}

ActionResult ActionDispatcher::petAttack(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // targetId=0 tells the server to use the pet owner's current target;
    // a nonzero value (T101: CommandProcessor's /pet attack <name>
    // resolution) attacks that specific entity instead.
    m_handler->sendPetCommand(EQT::PET_ATTACK, targetId);
    return ActionResult::Success("Pet attacking");
}

ActionResult ActionDispatcher::petBackOff() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_BACKOFF, 0);
    return ActionResult::Success("Pet backing off");
}

ActionResult ActionDispatcher::petFollow() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_FOLLOWME, 0);
    return ActionResult::Success("Pet following");
}

ActionResult ActionDispatcher::petGuard() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_GUARDHERE, 0);
    return ActionResult::Success("Pet guarding");
}

ActionResult ActionDispatcher::petSit() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_SITDOWN, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petTaunt() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_TAUNT, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petHold() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_HOLD, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petFocus() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_FOCUS, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petHealth() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_HEALTHREPORT, 0);
    return ActionResult::Success();
}

// T101: five wrappers added to close the vocabulary gap T97 found (game's
// /pet has leader/spellhold/notaunt/nohold/standup; the pre-T101 wrapper
// set didn't). All route through the existing generic sendPetCommand()
// IActionHandler virtual -- no interface change needed.

ActionResult ActionDispatcher::petStand() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_STANDUP, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petTauntOff() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_TAUNT_OFF, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petHoldOff() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_HOLD_OFF, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petLeader() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_LEADER, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petSpellHold() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    m_handler->sendPetCommand(EQT::PET_SPELLHOLD, 0);
    return ActionResult::Success();
}

// ========== Tradeskill Actions ==========

ActionResult ActionDispatcher::clickWorldObject(uint32_t dropId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->clickWorldObject(dropId);
    return ActionResult::Success();
}

// T149: the four recipe requests. Note what is NOT in these signatures: an
// object_type. The container's type is resolved in EqActionHandler from the
// container the server has told us is open, so these commands compose with
// T144's fix instead of asking the user to type a numeric id.

ActionResult ActionDispatcher::recipeSearch(const std::string& query,
                                            uint32_t minTrivial,
                                            uint32_t maxTrivial) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (maxTrivial < minTrivial) {
        return ActionResult::Failure("Max trivial must be >= min trivial");
    }

    m_handler->recipeSearch(query, minTrivial, maxTrivial);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::recipeDetails(uint32_t recipeId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (recipeId == 0) {
        return ActionResult::Failure("Recipe id must be non-zero");
    }

    m_handler->recipeDetails(recipeId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::recipeAutoCombine(uint32_t recipeId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (recipeId == 0) {
        return ActionResult::Failure("Recipe id must be non-zero");
    }

    m_handler->recipeAutoCombine(recipeId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::recipesFavorite(const std::vector<uint32_t>& recipeIds) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (recipeIds.empty()) {
        return ActionResult::Failure("No favorite recipe ids given");
    }
    // eqemu skips zero entries when building the id list (client_packet.cpp
    // Handle_OP_RecipesFavorite), and an all-zero list makes it return without
    // a reply -- so a 0 in the list is silently useless, not harmless.
    for (uint32_t id : recipeIds) {
        if (id == 0) {
            return ActionResult::Failure("Recipe ids must be non-zero");
        }
    }
    if (recipeIds.size() > 500) {
        return ActionResult::Failure("At most 500 favorite recipes");
    }

    m_handler->recipesFavorite(recipeIds);
    return ActionResult::Success();
}

// T278: the manual combine and the container close. Only the transport
// precondition is asserted here -- both requests are zone packets. Whether a
// container is OPEN is game state the dispatcher cannot see, so that refusal
// (and its message) lives one layer down, in
// EverQuest::RequestTradeskillCombine() / RequestCloseTradeskillContainer(),
// the same place the /recipe* family's open-container gate lives.
ActionResult ActionDispatcher::tradeskillCombine(int16_t containerSlot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->tradeskillCombine(containerSlot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::closeTradeskillContainer() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->closeTradeskillContainer();
    return ActionResult::Success();
}

// ========== Extended Movement Actions ==========

ActionResult ActionDispatcher::setMovementMode(int mode) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setMovementMode(mode);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::setPositionState(int state) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setPositionState(state);
    return ActionResult::Success();
}

// ========== Extended Character State Actions ==========

ActionResult ActionDispatcher::setRoleplay(bool rp) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setRoleplay(rp);
    return ActionResult::Success(rp ? "Roleplay enabled" : "Roleplay disabled");
}

ActionResult ActionDispatcher::setPathfinding(bool enabled) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->setPathfinding(enabled);
    return ActionResult::Success(enabled ? "Pathfinding enabled" : "Pathfinding disabled");
}

// ========== Extended Combat Actions ==========

ActionResult ActionDispatcher::setAutoHunting(bool enabled) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setAutoHunting(enabled);
    return ActionResult::Success(enabled ? "Auto-hunting enabled" : "Auto-hunting disabled");
}

ActionResult ActionDispatcher::setAutoLoot(bool enabled) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setAutoLoot(enabled);
    return ActionResult::Success(enabled ? "Auto-loot enabled" : "Auto-loot disabled");
}

ActionResult ActionDispatcher::listHuntTargets() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->listHuntTargets();
    return ActionResult::Success();
}

// ========== Entity Query Actions ==========

ActionResult ActionDispatcher::listEntities(const std::string& filter) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->listEntities(filter);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::dumpEntityAppearance(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->dumpEntityAppearance(name);
    return ActionResult::Success();
}

// ========== Utility Actions ==========

ActionResult ActionDispatcher::sendAnimation(uint8_t animationId, uint8_t speed) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->sendAnimation(animationId, speed);
    return ActionResult::Success();
}

// ========== Social Actions (T102) ==========

ActionResult ActionDispatcher::inspectTarget(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->inspectTarget(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::toggleLFG() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->toggleLFG();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::setSurname(const std::string& surname) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (surname.empty()) {
        return ActionResult::Failure("Usage: /surname <lastname>");
    }

    m_handler->setSurname(surname);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rollRandom(uint32_t low, uint32_t high) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rollRandom(low, high);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::yellForHelp() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->yellForHelp();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::requestWho(const std::string& name, int lvlLow, int lvlHigh) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->requestWho(name, lvlLow, lvlHigh);
    return ActionResult::Success();
}

} // namespace action
} // namespace eqt
