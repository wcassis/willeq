#include "client/action/action_dispatcher.h"
#include "client/state/game_state.h"
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

    auto& entities = m_state.entities();
    auto* entity = entities.getEntity(spawnId);
    if (!entity) {
        return ActionResult::Failure("Entity not found");
    }

    m_handler->targetEntity(spawnId);
    return ActionResult::Success("Targeting " + entity->name);
}

ActionResult ActionDispatcher::targetEntityByName(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    auto& entities = m_state.entities();
    auto* entity = entities.findEntityByName(name);
    if (!entity) {
        return ActionResult::Failure("Entity not found: " + name);
    }

    m_handler->targetEntityByName(name);
    return ActionResult::Success("Targeting " + entity->name);
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

    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("No target");
    }

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

    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("No target");
    }

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

    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("No target");
    }

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

    if (!m_state.combat().hasTarget()) {
        return ActionResult::Failure("No target");
    }

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

    if (gemSlot < 1 || gemSlot > 12) {
        return ActionResult::Failure("Invalid gem slot (1-12)");
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

ActionResult ActionDispatcher::petAttack() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    uint16_t targetId = m_state.combat().targetId();
    if (targetId == 0) {
        return ActionResult::Failure("No target selected");
    }

    // PET_ATTACK = 2 (from pet_constants.h)
    m_handler->sendPetCommand(2, targetId);
    return ActionResult::Success("Pet attacking");
}

ActionResult ActionDispatcher::petBackOff() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_BACKOFF = 28 (from pet_constants.h)
    m_handler->sendPetCommand(28, 0);
    return ActionResult::Success("Pet backing off");
}

ActionResult ActionDispatcher::petFollow() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_FOLLOWME = 4 (from pet_constants.h)
    m_handler->sendPetCommand(4, 0);
    return ActionResult::Success("Pet following");
}

ActionResult ActionDispatcher::petGuard() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_GUARDHERE = 5 (from pet_constants.h)
    m_handler->sendPetCommand(5, 0);
    return ActionResult::Success("Pet guarding");
}

ActionResult ActionDispatcher::petSit() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_SIT = 6 (from pet_constants.h)
    m_handler->sendPetCommand(6, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petTaunt() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_TAUNT = 12 (from pet_constants.h)
    m_handler->sendPetCommand(12, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petHold() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_HOLD = 15 (from pet_constants.h)
    m_handler->sendPetCommand(15, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petFocus() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_FOCUS = 24 (from pet_constants.h)
    m_handler->sendPetCommand(24, 0);
    return ActionResult::Success();
}

ActionResult ActionDispatcher::petHealth() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_state.pet().hasPet()) {
        return ActionResult::Failure("You don't have a pet");
    }

    // PET_HEALTHREPORT = 0 (from pet_constants.h)
    m_handler->sendPetCommand(0, 0);
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

} // namespace action
} // namespace eqt
