#include "client/eq_action_handler.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/pet_constants.h"
#include "common/logging.h"

namespace eqt {

EqActionHandler::EqActionHandler(EverQuest& eq)
    : m_eq(eq) {
}

EqActionHandler::~EqActionHandler() = default;

CombatManager* EqActionHandler::getCombatManager() {
    return m_eq.GetCombatManager();
}

std::string EqActionHandler::channelToString(action::ChatChannel channel) {
    switch (channel) {
        case action::ChatChannel::Say:     return "say";
        case action::ChatChannel::Shout:   return "shout";
        case action::ChatChannel::OOC:     return "ooc";
        case action::ChatChannel::Auction: return "auction";
        case action::ChatChannel::Tell:    return "tell";
        case action::ChatChannel::Group:   return "group";
        case action::ChatChannel::Guild:   return "guild";
        case action::ChatChannel::Raid:    return "raid";
        case action::ChatChannel::Emote:   return "emote";
        default:                           return "say";
    }
}

// ========== Movement ==========

void EqActionHandler::startMoving(action::Direction dir) {
    switch (dir) {
        case action::Direction::Forward:
            m_eq.StartMoveForward();
            break;
        case action::Direction::Backward:
            m_eq.StartMoveBackward();
            break;
        case action::Direction::Left:
            // EverQuest doesn't have native strafe - use turn for now
            // Actual strafe could be implemented via heading + forward movement
            m_eq.StartTurnLeft();
            break;
        case action::Direction::Right:
            m_eq.StartTurnRight();
            break;
        case action::Direction::Up:
        case action::Direction::Down:
            // Not directly supported
            break;
    }
}

void EqActionHandler::stopMoving(action::Direction dir) {
    switch (dir) {
        case action::Direction::Forward:
            m_eq.StopMoveForward();
            break;
        case action::Direction::Backward:
            m_eq.StopMoveBackward();
            break;
        case action::Direction::Left:
            m_eq.StopTurnLeft();
            break;
        case action::Direction::Right:
            m_eq.StopTurnRight();
            break;
        case action::Direction::Up:
        case action::Direction::Down:
            break;
    }
}

void EqActionHandler::setHeading(float heading) {
    // Convert from degrees (0-360) to EQ heading units (0-512)
    float eqHeading = heading * 512.0f / 360.0f;
    while (eqHeading < 0.0f) eqHeading += 512.0f;
    while (eqHeading >= 512.0f) eqHeading -= 512.0f;
    m_eq.SetHeading(eqHeading);
    m_eq.SendPositionUpdate();
}

void EqActionHandler::jump() {
    m_eq.Jump();
}

void EqActionHandler::sit() {
    m_eq.SetPositionState(POS_SITTING);
}

void EqActionHandler::stand() {
    m_eq.SetPositionState(POS_STANDING);
}

void EqActionHandler::toggleAutorun() {
    // EverQuest doesn't have native autorun yet
    // This would need to be implemented in EverQuest class
    LOG_DEBUG(MOD_INPUT, "Autorun toggle not yet implemented in EverQuest class");
}

void EqActionHandler::stopAllMovement() {
    m_eq.StopMoveForward();
    m_eq.StopMoveBackward();
    m_eq.StopTurnLeft();
    m_eq.StopTurnRight();
    // Note: StopMovement() is private - the above calls handle keyboard movement
}

void EqActionHandler::moveToLocation(float x, float y, float z) {
    m_eq.Move(x, y, z);
}

void EqActionHandler::moveToEntity(const std::string& name) {
    m_eq.MoveToEntity(name);
}

void EqActionHandler::moveToEntityWithinRange(const std::string& name, float distance) {
    m_eq.MoveToEntityWithinRange(name, distance);
}

void EqActionHandler::followEntity(const std::string& name) {
    m_eq.Follow(name);
}

void EqActionHandler::stopFollow() {
    m_eq.StopFollow();
}

// ========== Combat ==========

void EqActionHandler::targetEntity(uint16_t spawnId) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetTarget(spawnId);
    }
}

void EqActionHandler::targetEntityByName(const std::string& name) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetTarget(name);
    }
}

void EqActionHandler::targetNearest() {
    // Would need to be implemented using entity manager to find nearest NPC
    LOG_DEBUG(MOD_COMBAT, "Target nearest not yet implemented");
}

void EqActionHandler::clearTarget() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->ClearTarget();
    }
}

void EqActionHandler::enableAutoAttack() {
    auto* combat = getCombatManager();
    if (combat && combat->HasTarget()) {
        combat->EnableAutoAttack();
    }
}

void EqActionHandler::disableAutoAttack() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->DisableAutoAttack();
    }
}

void EqActionHandler::toggleAutoAttack() {
    auto* combat = getCombatManager();
    if (combat) {
        if (combat->IsAutoAttackEnabled()) {
            combat->DisableAutoAttack();
        } else if (combat->HasTarget()) {
            combat->EnableAutoAttack();
        }
    }
}

void EqActionHandler::castSpell(uint8_t gemSlot) {
    auto* combat = getCombatManager();
    if (combat) {
        // Convert from 1-indexed to SpellSlot enum
        combat->CastSpell(static_cast<SpellSlot>(gemSlot - 1));
    }
}

void EqActionHandler::castSpellOnTarget(uint8_t gemSlot, uint16_t targetId) {
    // First set target, then cast
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetTarget(targetId);
        combat->CastSpell(static_cast<SpellSlot>(gemSlot - 1));
    }
}

void EqActionHandler::interruptCast() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->InterruptCast();
    }
}

void EqActionHandler::useAbility(uint32_t abilityId) {
    // Abilities use similar slot-based activation as spells
    LOG_DEBUG(MOD_COMBAT, "Use ability {} not yet implemented", abilityId);
}

void EqActionHandler::useSkill(uint32_t skillId) {
    // Skills would need dedicated implementation
    LOG_DEBUG(MOD_COMBAT, "Use skill {} not yet implemented", skillId);
}

// ========== Interaction ==========

void EqActionHandler::hail() {
    m_eq.SendChatMessage("Hail", "say");
}

void EqActionHandler::hailTarget() {
    auto* combat = getCombatManager();
    if (combat && combat->HasTarget()) {
        uint16_t targetId = combat->GetTargetId();
        const auto& entities = m_eq.GetGameState().entities();
        auto* entity = entities.getEntity(targetId);
        if (entity && !entity->name.empty()) {
            m_eq.SendChatMessage("Hail, " + entity->name, "say");
        }
    }
}

void EqActionHandler::clickDoor(uint8_t doorId) {
    m_eq.SendClickDoor(doorId);
}

void EqActionHandler::clickNearestDoor() {
    // Find nearest door and click it
    const auto& gameState = m_eq.GetGameState();
    auto nearestDoor = gameState.doors().getNearestDoor(
        gameState.player().x(),
        gameState.player().y(),
        gameState.player().z()
    );
    if (nearestDoor) {
        m_eq.SendClickDoor(nearestDoor->doorId);
    }
}

void EqActionHandler::lootCorpse(uint16_t corpseId) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->LootCorpse(corpseId);
    }
}

void EqActionHandler::lootItem(uint16_t corpseId, int16_t slot) {
    // First ensure we're looting this corpse, then loot the item
    m_eq.LootItemFromCorpse(corpseId, slot);
}

void EqActionHandler::lootAll(uint16_t corpseId) {
    m_eq.LootAllFromCorpse(corpseId);
}

void EqActionHandler::consider() {
    auto* combat = getCombatManager();
    if (combat && combat->HasTarget()) {
        combat->ConsiderTarget();
    }
}

// ========== Chat ==========

void EqActionHandler::sendChatMessage(action::ChatChannel channel, const std::string& message) {
    m_eq.SendChatMessage(message, channelToString(channel));
}

void EqActionHandler::sendTell(const std::string& target, const std::string& message) {
    m_eq.SendChatMessage(message, "tell", target);
}

void EqActionHandler::replyToLastTell(const std::string& message) {
    // Would need to track last tell sender in EverQuest class
    LOG_DEBUG(MOD_INPUT, "Reply to last tell not yet implemented");
}

// ========== Group ==========

void EqActionHandler::inviteToGroup(const std::string& playerName) {
    m_eq.SendGroupInvite(playerName);
}

void EqActionHandler::inviteTarget() {
    auto* combat = getCombatManager();
    if (combat && combat->HasTarget()) {
        uint16_t targetId = combat->GetTargetId();
        const auto& entities = m_eq.GetGameState().entities();
        auto* entity = entities.getEntity(targetId);
        if (entity && !entity->name.empty()) {
            m_eq.SendGroupInvite(entity->name);
        }
    }
}

void EqActionHandler::acceptGroupInvite() {
    m_eq.AcceptGroupInvite();
}

void EqActionHandler::declineGroupInvite() {
    // Get pending inviter and decline
    const auto& groupState = m_eq.GetGameState().group();
    if (groupState.hasPendingInvite()) {
        m_eq.SendGroupDecline(groupState.pendingInviterName());
    }
}

void EqActionHandler::leaveGroup() {
    m_eq.SendLeaveGroup();
}

// ========== Character State ==========

void EqActionHandler::setAFK(bool afk) {
    m_eq.SetAFK(afk);
}

void EqActionHandler::setAnonymous(bool anon) {
    m_eq.SetAnonymous(anon);
}

void EqActionHandler::toggleSneak() {
    // Sneak toggle would require public accessor or state tracking
    // For now, we can't check current sneak state - just toggle
    LOG_DEBUG(MOD_INPUT, "Sneak toggle - requires state tracking implementation");
}

void EqActionHandler::startCamp() {
    m_eq.StartCampTimer();
}

void EqActionHandler::cancelCamp() {
    m_eq.CancelCamp();
}

// ========== Inventory ==========

void EqActionHandler::moveItem(int16_t fromSlot, int16_t toSlot, uint32_t quantity) {
    // Item move is handled internally by inventory manager
    // The public interface would be through inventory window clicks
    LOG_DEBUG(MOD_INVENTORY, "Move item not yet exposed publicly");
    (void)fromSlot; (void)toSlot; (void)quantity;
}

void EqActionHandler::deleteItem(int16_t slot) {
    // Item delete is handled internally by inventory manager
    LOG_DEBUG(MOD_INVENTORY, "Delete item not yet exposed publicly");
    (void)slot;
}

void EqActionHandler::useItem(int16_t slot) {
    // Right-click use would need cast item or activate
    LOG_DEBUG(MOD_INVENTORY, "Use item not yet implemented");
}

// ========== Spellbook ==========

void EqActionHandler::memorizeSpell(uint8_t gemSlot, uint32_t spellId) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->MemorizeSpell(spellId, static_cast<SpellSlot>(gemSlot - 1));
    }
}

void EqActionHandler::forgetSpell(uint8_t gemSlot) {
    // Would need to send forget spell packet
    LOG_DEBUG(MOD_COMBAT, "Forget spell not yet implemented");
}

void EqActionHandler::openSpellbook() {
    // UI state - would need renderer integration
    LOG_DEBUG(MOD_UI, "Open spellbook not yet implemented");
}

void EqActionHandler::closeSpellbook() {
    LOG_DEBUG(MOD_UI, "Close spellbook not yet implemented");
}

// ========== Trade ==========

void EqActionHandler::requestTrade(uint16_t targetId) {
    // Would need to construct and send trade request
    LOG_DEBUG(MOD_INPUT, "Request trade not yet implemented");
}

void EqActionHandler::acceptTrade() {
    LOG_DEBUG(MOD_INPUT, "Accept trade not yet implemented");
}

void EqActionHandler::cancelTrade() {
    LOG_DEBUG(MOD_INPUT, "Cancel trade not yet implemented");
}

// ========== Zone ==========

void EqActionHandler::requestZone(const std::string& zoneName) {
    // Zone requests typically happen through doors/zone lines, not directly
    LOG_DEBUG(MOD_ZONE, "Zone request not yet implemented");
}

// ========== Pet ==========

void EqActionHandler::sendPetCommand(uint8_t command, uint16_t targetId) {
    m_eq.SendPetCommand(static_cast<EQT::PetCommand>(command), targetId);
}

void EqActionHandler::dismissPet() {
    // PET_GETLOST = 29 from pet_constants.h
    m_eq.SendPetCommand(EQT::PET_GETLOST, 0);
}

// ========== Tradeskill ==========

void EqActionHandler::clickWorldObject(uint32_t dropId) {
    m_eq.SendClickObject(dropId);
}

void EqActionHandler::tradeskillCombine() {
    // Combine is typically triggered through the tradeskill window combine button
    LOG_DEBUG(MOD_UI, "Tradeskill combine would need to be routed through tradeskill window");
}

// ========== Utility ==========

void EqActionHandler::sendAnimation(uint8_t animationId, uint8_t speed) {
    m_eq.SendAnimation(animationId, speed);
}

void EqActionHandler::sendPositionUpdate() {
    m_eq.SendPositionUpdate();
}

} // namespace eqt
