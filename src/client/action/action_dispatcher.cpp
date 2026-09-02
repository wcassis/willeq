// ActionDispatcher -- movement, camera, targeting, doors, casting and item clusters.
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

// T316: this used to return Failure("Use clickWorldObject with drop ID")
// unconditionally -- the interact-object keybind could never open a forge,
// a chest or a ground container. Same shape as clickNearestDoor() ten lines
// above, except the nearest-object query lives on the handler (world objects
// are in EverQuest::m_world_objects, not GameState) -- see
// IActionHandler::findNearestWorldObject().
ActionResult ActionDispatcher::interactNearestWorldObject() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    float distance = 0.0f;
    uint32_t dropId = m_handler->findNearestWorldObject(distance);
    if (dropId == 0) {
        return ActionResult::Failure("No object nearby");
    }

    m_handler->clickWorldObject(dropId);
    return ActionResult::Success();
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
    enum class InteractType { None, Door, Object, NPC } nearest = InteractType::None;

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

    // T316: world objects. The comment at this function's call site has
    // always said "door, object, or NPC" and the renderer's own generic
    // interact (HA::Interact) tries a door then an object -- but this branch
    // was missing, so a forge or a chest was never a candidate here either.
    float objectDist = 0.0f;
    uint32_t objectId = m_handler->findNearestWorldObject(objectDist);
    if (objectId != 0 && objectDist < nearestDist) {
        nearestDist = objectDist;
        nearest = InteractType::Object;
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
        case InteractType::Object:
            m_handler->clickWorldObject(objectId);
            return ActionResult::Success("Interacting with object");
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

    if (!m_handler->lootItem(corpseId, slot)) {
        return ActionResult::Failure(
            "Nothing to loot in slot " + std::to_string(slot) + " (is a loot window open?)");
    }
    return ActionResult::Success();
}

ActionResult ActionDispatcher::lootItemByName(const std::string& name) {
    auto result = checkZoneConnection();
    if (!result.success) return result;
    if (name.empty()) {
        return ActionResult::Failure("Usage: /loot <item name>");
    }

    if (!m_handler->lootItemByName(name)) {
        return ActionResult::Failure(
            "No loot item matching '" + name + "' (is a loot window open?)");
    }
    return ActionResult::Success();
}

ActionResult ActionDispatcher::lootAll(uint16_t corpseId) {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_handler->lootAll(corpseId)) {
        return ActionResult::Failure("Nothing to loot (is a loot window open?)");
    }
    return ActionResult::Success();
}

ActionResult ActionDispatcher::closeLootWindow() {
    auto result = checkZoneConnection();
    if (!result.success) return result;

    if (!m_handler->closeLootWindow()) {
        return ActionResult::Failure("No loot window is open");
    }
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

} // namespace action
} // namespace eqt
