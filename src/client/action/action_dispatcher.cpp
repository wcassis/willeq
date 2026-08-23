#include "client/action/action_dispatcher.h"
#include "client/state/game_state.h"
#include "client/pet_constants.h"
#include <cmath>
#include <limits>

namespace eqt {
namespace action {

ActionDispatcher::ActionDispatcher(state::GameState& state)
    : m_state(state) {
}

ActionDispatcher::~ActionDispatcher() = default;

void ActionDispatcher::setActionHandler(IActionHandler* handler) {
    m_handler = handler;
}

ActionResult ActionDispatcher::checkHandler() const {
    if (!m_handler) {
        return ActionResult::Failure("No action handler set");
    }
    return ActionResult::Success();
}

ActionResult ActionDispatcher::checkZoneConnection() const {
    auto result = checkHandler();
    if (!result.success) return result;

    if (!m_state.world().isZoneConnected()) {
        return ActionResult::Failure("Not connected to zone");
    }
    return ActionResult::Success();
}

float ActionDispatcher::calculateHeadingTo(float x, float y) const {
    auto& player = m_state.player();
    float dx = x - player.x();
    float dy = y - player.y();

    // Calculate heading in degrees (0 = north, 90 = east, etc.)
    float heading = std::atan2(dx, dy) * 180.0f / 3.14159265f;
    if (heading < 0) heading += 360.0f;

    return heading;
}

// ========== Movement Actions ==========

ActionResult ActionDispatcher::startMoving(Direction dir) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->startMoving(dir);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::stopMoving(Direction dir) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->stopMoving(dir);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::stopAllMovement() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->stopAllMovement();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::setHeading(float heading) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // Normalize heading to 0-360
    while (heading < 0) heading += 360.0f;
    while (heading >= 360.0f) heading -= 360.0f;

    m_handler->setHeading(heading);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::faceLocation(float x, float y, float z) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    float heading = calculateHeadingTo(x, y);
    m_handler->setHeading(heading);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::faceEntity(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& entities = m_state.entities();
    auto* entity = entities.findEntityByName(name);
    if (!entity) {
        return ActionResult::Failure("Entity not found: " + name);
    }

    float heading = calculateHeadingTo(entity->x, entity->y);
    m_handler->setHeading(heading);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::jump() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->jump();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::sit() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->sit();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::stand() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->stand();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::toggleAutorun() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->toggleAutorun();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::moveToLocation(float x, float y, float z) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->moveToLocation(x, y, z);
    return ActionResult::Success("Moving to location");
}

ActionResult ActionDispatcher::moveToEntity(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& entities = m_state.entities();
    if (!entities.findEntityByName(name)) {
        return ActionResult::Failure("Entity not found: " + name);
    }

    m_handler->moveToEntity(name);
    return ActionResult::Success("Moving to " + name);
}

ActionResult ActionDispatcher::moveToEntityWithinRange(const std::string& name, float distance) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& entities = m_state.entities();
    if (!entities.findEntityByName(name)) {
        return ActionResult::Failure("Entity not found: " + name);
    }

    m_handler->moveToEntityWithinRange(name, distance);
    return ActionResult::Success("Moving to " + name);
}

ActionResult ActionDispatcher::followEntity(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& entities = m_state.entities();
    if (!entities.findEntityByName(name)) {
        return ActionResult::Failure("Entity not found: " + name);
    }

    m_handler->followEntity(name);
    return ActionResult::Success("Following " + name);
}

ActionResult ActionDispatcher::stopFollow() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->stopFollow();
    return ActionResult::Success("Stopped following");
}

// ========== Combat Actions ==========

ActionResult ActionDispatcher::targetEntity(uint16_t spawnId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetEntity(spawnId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::targetEntityByName(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetEntityByName(name);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::targetSelf() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetSelf();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::targetGroupMember(int index) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetGroupMember(index);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::targetNearest() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetNearest();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::targetNearestPC() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetNearestPC();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::targetNearestNPC() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->targetNearestNPC();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::cycleTargets() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->cycleTargets();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::cycleTargetsReverse() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->cycleTargetsReverse();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::clearTarget() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->clearTarget();
    return ActionResult::Success("Target cleared");
}

ActionResult ActionDispatcher::enableAutoAttack() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // Target check is done by the handler (CombatManager) which has the real target state
    m_handler->enableAutoAttack();
    return ActionResult::Success("Auto-attack enabled");
}

ActionResult ActionDispatcher::disableAutoAttack() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->disableAutoAttack();
    return ActionResult::Success("Auto-attack disabled");
}

ActionResult ActionDispatcher::toggleAutoAttack() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->toggleAutoAttack();
    return ActionResult::Success();
}

// ===== T102: Combat family additions =====
// All the target-precondition/messaging logic (no target, already trading,
// no pending duel, etc.) lives in EqActionHandler -- it has access to
// EverQuest's live CombatManager/TradeManager/duel state, which
// ActionDispatcher (only state::GameState + the IActionHandler interface)
// does not. This matches the established "target check is done by the
// handler" pattern above.

ActionResult ActionDispatcher::assist(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->assist(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::shieldTarget(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->shieldTarget(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::taunt() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->taunt();
    return ActionResult::Success();
}

// ========== Raid (T179) ==========
// Guard the connection, then delegate to the handler which does its own
// reporting (invite/accept/leave reach EverQuest::SendRaidCommand; status is a
// read). Same shape as vendorList()/T140 utility commands.
ActionResult ActionDispatcher::raidInvite(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidInvite(name);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidAccept() {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidAccept();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidLeave() {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidLeave();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidStatus() {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidStatus();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidList() {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidList();
    return ActionResult::Success();
}

// T181 leader-only ops. Same shape: guard the connection, delegate, let the
// handler do its own reporting (it is where the leadership and membership
// pre-checks live).
ActionResult ActionDispatcher::raidRemove(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidRemove(name);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidMakeLeader(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidMakeLeader(name);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidLootType(const std::string& mode) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidLootType(mode);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidLooter(bool add, const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidLooter(add, name);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidLock(bool lock) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidLock(lock);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::raidGroup(const std::string& name, const std::string& group_text) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    m_handler->raidGroup(name, group_text);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::toggleAutoFire() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->toggleAutoFire();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::enableCombatManager() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->enableCombatManager();
    return ActionResult::Success("Combat manager enabled");
}

ActionResult ActionDispatcher::disableCombatManager() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->disableCombatManager();
    return ActionResult::Success("Combat manager disabled");
}

ActionResult ActionDispatcher::toggleCombatManager() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->toggleCombatManager();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::duelRequest(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->duelRequest(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::duelAccept() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->duelAccept();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::duelDecline() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->duelDecline();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::castSpell(uint8_t gemSlot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (gemSlot < 1 || gemSlot > 12) {
        return ActionResult::Failure("Invalid gem slot (1-12)");
    }

    m_handler->castSpell(gemSlot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::castSpellOnTarget(uint8_t gemSlot, uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (gemSlot < 1 || gemSlot > 12) {
        return ActionResult::Failure("Invalid gem slot (1-12)");
    }

    m_handler->castSpellOnTarget(gemSlot, targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::interruptCast() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->interruptCast();
    return ActionResult::Success("Cast interrupted");
}

ActionResult ActionDispatcher::beginCastFromGem(uint8_t gemSlot, uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // T101: matches game's /cast bound (1-8, EQ::SpellManager's real gem
    // count) -- distinct from castSpell()'s 1-12 (the older CombatManager
    // path's more permissive, and less correct, bound).
    if (gemSlot < 1 || gemSlot > 8) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }

    m_handler->beginCastFromGem(gemSlot, targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::useAbility(uint32_t abilityId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->useAbility(abilityId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::useSkill(uint32_t skillId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->useSkill(skillId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::consider() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // Target check is done by the handler (CombatManager) which has the real target state
    m_handler->consider();
    return ActionResult::Success();
}

// ========== Interaction Actions ==========

ActionResult ActionDispatcher::hail() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->hail();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::hailTarget() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // Target check is done by the handler (CombatManager) which has the real target state.
    // Falls back to plain hail if no target.
    m_handler->hailTarget();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::clickDoor(uint8_t doorId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& doors = m_state.doors();
    if (!doors.getDoor(doorId)) {
        return ActionResult::Failure("Door not found");
    }

    m_handler->clickDoor(doorId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::clickNearestDoor() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& player = m_state.player();
    auto& doors = m_state.doors();
    auto* door = doors.getNearestDoor(player.x(), player.y(), player.z());
    if (!door) {
        return ActionResult::Failure("No door nearby");
    }

    m_handler->clickNearestDoor();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::interactNearestWorldObject() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // TODO: Implement when ObjectState is added to GameState
    // For now, world object interaction requires explicit drop ID
    return ActionResult::Failure("Use clickWorldObject with drop ID");
}

ActionResult ActionDispatcher::interactNearest() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& player = m_state.player();
    float px = player.x();
    float py = player.y();
    float pz = player.z();

    // Find the nearest interactable: door or NPC
    float nearestDist = std::numeric_limits<float>::max();
    enum class InteractType { None, Door, NPC } nearest = InteractType::None;

    // Check doors
    auto& doors = m_state.doors();
    auto* door = doors.getNearestDoor(px, py, pz);
    if (door) {
        float dx = door->x - px;
        float dy = door->y - py;
        float dz = door->z - pz;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = InteractType::Door;
        }
    }

    // Check NPCs (non-player entities that are not hostile)
    auto& entities = m_state.entities();
    auto* npc = entities.getNearestNPC(px, py, pz);
    if (npc) {
        float dx = npc->x - px;
        float dy = npc->y - py;
        float dz = npc->z - pz;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = InteractType::NPC;
        }
    }

    // Interact with nearest
    switch (nearest) {
        case InteractType::Door:
            m_handler->clickNearestDoor();
            return ActionResult::Success("Interacting with door");
        case InteractType::NPC:
            m_handler->targetEntity(npc->spawnId);
            m_handler->hailTarget();
            return ActionResult::Success("Hailing " + npc->name);
        default:
            return ActionResult::Failure("Nothing to interact with nearby");
    }
}

ActionResult ActionDispatcher::lootCorpse(uint16_t corpseId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->lootCorpse(corpseId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::lootItem(uint16_t corpseId, int16_t slot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->lootItem(corpseId, slot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::lootAll(uint16_t corpseId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->lootAll(corpseId);
    return ActionResult::Success();
}

// ========== Chat Actions ==========

ActionResult ActionDispatcher::sendChatMessage(ChatChannel channel, const std::string& message) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (message.empty()) {
        return ActionResult::Failure("Empty message");
    }

    m_handler->sendChatMessage(channel, message);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::sendTell(const std::string& target, const std::string& message) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (target.empty()) {
        return ActionResult::Failure("No target specified");
    }
    if (message.empty()) {
        return ActionResult::Failure("Empty message");
    }

    m_handler->sendTell(target, message);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::replyToLastTell(const std::string& message) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (message.empty()) {
        return ActionResult::Failure("Empty message");
    }

    m_handler->replyToLastTell(message);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::openReplyToTell() {
    auto result = checkHandler();
    if (!result.success) return result;

    // TODO: Implement when ChatState is added to GameState
    // The UI layer should track the last tell sender and handle this
    // For now, return a special message that the UI can recognize
    return ActionResult::Success("open_reply");
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

ActionResult ActionDispatcher::toggleSneak() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->toggleSneak();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::startCamp() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->startCamp();
    return ActionResult::Success("Camping... Stand up to cancel");
}

ActionResult ActionDispatcher::cancelCamp() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->cancelCamp();
    return ActionResult::Success("Camp cancelled");
}

// ========== Inventory Actions ==========

ActionResult ActionDispatcher::moveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->moveItem(fromSlot, toSlot, quantity);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::deleteItem(int16_t slot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->deleteItem(slot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::useItem(int16_t slot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->useItem(slot);
    return ActionResult::Success();
}

// ========== Spellbook Actions ==========

ActionResult ActionDispatcher::memorizeSpell(uint8_t gemSlot, uint32_t spellId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (gemSlot < 1 || gemSlot > 12) {
        return ActionResult::Failure("Invalid gem slot (1-12)");
    }

    m_handler->memorizeSpell(gemSlot, spellId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::forgetSpell(uint8_t gemSlot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    // T101: narrowed from 1-12 to 1-8 -- forgetSpell() now routes through
    // the real EQ::SpellManager (only 8 gems exist), fixing the stub
    // ("Would need to send forget spell packet") T97 found underneath this
    // dispatcher method. See the plan's Review: this is a repoint, not a
    // new method -- forgetSpell()'s signature/callers are unchanged, no
    // test asserted on the old stub's behavior.
    if (gemSlot < 1 || gemSlot > 8) {
        return ActionResult::Failure("Gem slot must be 1-8");
    }

    m_handler->forgetSpell(gemSlot);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::openSpellbook() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->openSpellbook();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::closeSpellbook() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->closeSpellbook();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (gemSlot < 1 || gemSlot > 8) {
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

// ========== Zone Actions ==========

ActionResult ActionDispatcher::requestZone(const std::string& zoneName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (zoneName.empty()) {
        return ActionResult::Failure("No zone specified");
    }

    m_handler->requestZone(zoneName);
    return ActionResult::Success("Requesting zone: " + zoneName);
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

ActionResult ActionDispatcher::tradeskillCombine() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->tradeskillCombine();
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

ActionResult ActionDispatcher::sendPositionUpdate() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->sendPositionUpdate();
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

// ========== Guild Actions (T132) ==========
//
// Net-new: EqActionHandler wraps the pre-existing EverQuest::SendGuild*()
// senders, which already implement the guild-membership/pending-invite
// guards and player-facing messages (game's handlers did no additional
// checks of their own -- see refactor/T132_migrate_guild_family.md). The
// empty-name usage checks below mirror game's own pre-dispatch guard so the
// exact usage string is preserved even before reaching the handler.

ActionResult ActionDispatcher::guildInvite(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guildinvite <name>");
    }

    m_handler->guildInvite(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildAcceptInvite() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->guildAcceptInvite();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildDeclineInvite() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->guildDeclineInvite();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildRemove(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guildremove <name>");
    }

    m_handler->guildRemove(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildDemote(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guilddemote <name>");
    }

    m_handler->guildDemote(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::guildMakeLeader(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /guildleader <name>");
    }

    m_handler->guildMakeLeader(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::requestGuildMOTD() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->requestGuildMOTD();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::setGuildMOTD(const std::string& motd) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->setGuildMOTD(motd);
    return ActionResult::Success();
}

// ========== Corpse / Resurrection (T134) ==========
// Usage-string guards below mirror game's original corpsedrag/consent/deny
// handlers verbatim (eq_commands.cpp), matching the Guild family's
// belt-and-suspenders shape -- the empty-argument check is duplicated here
// AND inside game's ported usage strings would have been if game still
// registered them; corpsedrop/rezAccept/rezDecline take no argument so have
// no usage guard, matching game's shape for those too. The stateful guards
// (m_is_dragging_corpse, m_has_pending_rezz) are NOT duplicated here -- they
// live in the senders (SendCorpseDrop, SendRezzAnswer) and are preserved
// there unchanged.

ActionResult ActionDispatcher::corpseDrag(const std::string& corpseName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (corpseName.empty()) {
        return ActionResult::Failure("Usage: /corpsedrag <corpse_name>");
    }

    m_handler->corpseDrag(corpseName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::corpseDrop() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->corpseDrop();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::consent(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /consent <player_name>");
    }

    m_handler->consent(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::consentDeny(const std::string& playerName) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (playerName.empty()) {
        return ActionResult::Failure("Usage: /deny <player_name>");
    }

    m_handler->consentDeny(playerName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rezAccept() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rezAccept();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rezDecline() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rezDecline();
    return ActionResult::Success();
}

// ========== Utility (T135) ==========
// Net-new, same shape as Guild (T132) / Corpse-Rez (T134). split/save/
// rewind/bandage take no usage-string guard here because game's original
// handlers had none for them either (only split_cmd/bug_cmd/petition_cmd
// had one, and split's is a parsing concern handled by
// CommandProcessor::cmdSplit -- see its comment). bug()/petition() port
// game's empty-argument usage guard verbatim, matching corpsedrag/consent's
// belt-and-suspenders shape. The stateful guards (m_in_group + affordability
// in SendSplit) are NOT duplicated here -- they stay in the sender.

ActionResult ActionDispatcher::split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->split(platinum, gold, silver, copper);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::save() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->save();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bandage(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->bandage(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::rewind() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->rewind();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bug(const std::string& description) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (description.empty()) {
        return ActionResult::Failure("Usage: /bug <description>");
    }

    m_handler->bug(description);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petition(const std::string& text) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (text.empty()) {
        return ActionResult::Failure("Usage: /petition <text>");
    }

    m_handler->petition(text);
    return ActionResult::Success();
}

// ========== Merchant (T136) ==========
// No usage-string guards ported here beyond checkZoneConnection() -- none
// of game's original vendor_cmd/vendorlist_cmd existed at the dispatcher
// level either (their "You are not at a vendor."/"no target" messages are
// stateful guards, so they live inside the new EverQuest methods /
// EqActionHandler::vendorList(), per T134's rule). buy/sell's slot and
// quantity ARGUMENT PARSING (numeric-token validation, matching
// ParseVendorSlot/ParseVendorQuantity's old idiom) moved up to
// CommandProcessor::cmdBuy/cmdSell -- a formatting concern, not a guard,
// same precedent as cmdSplit/cmdWho's own token parsing.

ActionResult ActionDispatcher::vendorToggle() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->vendorToggle();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::vendorList() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->vendorList();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::buyFromVendor(uint32_t slot, uint8_t quantity) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->buyFromVendor(slot, quantity);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::sellToVendor(uint32_t slot, uint8_t quantity) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->sellToVendor(slot, quantity);
    return ActionResult::Success();
}

// ========== Equip (T141) ==========
// No usage-string guard here either, same precedent as vendorToggle()/
// vendorList() above -- /equip's own "Usage: ..." message is a stateful
// (args-shape-dependent) reply that lives inside EverQuest::RequestEquip(),
// not duplicated at the dispatcher level. checkZoneConnection() is added
// here (equip had no equivalent guard in the pre-migration game registry)
// because every other migrated inventory/vendor action in this file gates
// on it, and a disconnected EverQuest has no meaningful inventory state to
// move items through -- see refactor/T141_equip_extraction.md's Review for
// the full argument.

ActionResult ActionDispatcher::equip(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->equip(args);
    return ActionResult::Success();
}

// ========== Movement / Utility / Skills Actions (T140) ==========

ActionResult ActionDispatcher::showLocation() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->showLocation();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::warp(const std::string& args) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->warp(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::showOrSetZoning(const std::string& args) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->showOrSetZoning(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::quit() {
    // T30: quit must work whether connected or not -- checkHandler() only,
    // deliberately not checkZoneConnection().
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->requestQuit();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::camp() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->campNow();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::debugLevel(const std::string& args) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->debugLevel(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::perfReport() {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->perfReport();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::timeOfDay(const std::string& args) {
    auto result = checkHandler();
    if (!result.success) return result;

    m_handler->timeOfDay(args);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::bankToggle() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->bankToggle();
    return ActionResult::Success();
}

ActionResult ActionDispatcher::trainWithTarget() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->trainWithTarget();
    return ActionResult::Success();
}

// ===== T150: Social / Find family =====
// Same shape as assist()/shieldTarget() above: guard the zone connection,
// route, return a generic Success(). All user-visible reporting (the
// no-target message, and the eventual server reply) happens elsewhere --
// EqActionHandler for the target fallback, the ZoneProcess* parsers for
// the replies. See refactor/T150_social_find_family.md.

ActionResult ActionDispatcher::assistGroup(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->assistGroup(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::trackTarget(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->trackTarget(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::findPerson(uint16_t targetId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->findPerson(targetId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::friendsWho(const std::string& friendsList) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (friendsList.empty()) {
        return ActionResult::Failure("Usage: /friendswho <name>[,<name>...]");
    }

    m_handler->friendsWho(friendsList);
    return ActionResult::Success();
}

// ========== GM Actions (T148) ==========
//
// Net-new plumbing over the pre-existing EverQuest::SendGM*() senders
// (src/client/eq_gm.cpp), all nine of which had zero production callers
// until this unit. The senders already own their own zone-connection guard
// (`if (m_zone_connection && m_zone_connected)`), exactly like the Guild
// family's did (T132); the wrappers below are the gate plus routing.

ActionResult ActionDispatcher::checkGMMode() const {
    auto result = checkHandler();
    if (!result.success) return result;

    if (!m_handler->isGMMode()) {
        // Deliberately specific. "Unknown command" is what a player gets for
        // a command that does not exist; this one exists and is refused, and
        // the message says both why and what to do about it. The relog
        // instruction is not a workaround for a bug -- the check is
        // login-time BY DESIGN (Will, 2026-08-19), and the AT_GM live-update
        // path was explicitly declined.
        return ActionResult::Failure(
            "That command is a GM command and you were not in GM mode when you "
            "logged in. Use #gm on, then log out and back in.");
    }
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmZoneRequest(const std::string& charName, uint16_t zoneId) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /gmzone <name> <zone_id>");
    }

    m_handler->gmZoneRequest(charName, zoneId);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmSummon(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /summon <name>");
    }

    m_handler->gmSummon(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmGoto(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /goto <name>");
    }

    m_handler->gmGoto(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmFind(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /gmfind <name>");
    }

    m_handler->gmFind(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmKick(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /kick <name>");
    }

    m_handler->gmKick(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmKill(const std::string& charName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty()) {
        return ActionResult::Failure("Usage: /kill <name>");
    }

    m_handler->gmKill(charName);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmHideMe(bool hide) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    m_handler->gmHideMe(hide);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmEmoteZone(const std::string& text) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (text.empty()) {
        return ActionResult::Failure("Usage: /emotezone <message>");
    }

    m_handler->gmEmoteZone(text);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::gmLastName(const std::string& charName, const std::string& lastName) {
    auto result = checkGMMode();
    if (!result.success) return result;
    result = checkZoneConnection();
    if (!result.success) return result;

    if (charName.empty() || lastName.empty()) {
        return ActionResult::Failure("Usage: /gmlastname <name> <lastname>");
    }

    m_handler->gmLastName(charName, lastName);
    return ActionResult::Success();
}

// T153: /appearance -> EverQuest::SendFaceChange(). Zone-gated like every
// other zone-scoped send; the empty-change guard is here rather than in
// CommandProcessor so a programmatic caller cannot send a no-op packet either.
ActionResult ActionDispatcher::setAppearance(const AppearanceChange& change) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (change.empty()) {
        return ActionResult::Failure(
            "Usage: /appearance <field> <0-255> [<field> <0-255> ...]");
    }

    m_handler->setAppearance(change);
    return ActionResult::Success();
}

// T171: /inspectmessage. Deliberately NOT zone-gated, unlike every sender
// above it: this changes local state and writes the config file, and a player
// should be able to set it at a character-select or loading screen. The value
// reaches the server on its own schedule -- the next time somebody inspects
// us -- so there is nothing here that needs a zone connection.
ActionResult ActionDispatcher::setInspectMessage(const std::optional<std::string>& message) {
    if (!m_handler) {
        return ActionResult::Failure("No action handler");
    }
    m_handler->setInspectMessage(message);
    return ActionResult::Success();
}

} // namespace action
} // namespace eqt
