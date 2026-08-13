#include "client/eq_action_handler.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/pet_constants.h"
#include "common/logging.h"
#include "common/name_utils.h"

namespace eqt {

EqActionHandler::EqActionHandler(EverQuest& eq)
    : m_eq(eq) {
}

EqActionHandler::~EqActionHandler() = default;

CombatManager* EqActionHandler::getCombatManager() {
    return m_eq.GetCombatManager();
}

// ---------------------------------------------------------------------
// T95: enum-bounds guards for setMovementMode()/setPositionState()/
// sendPetCommand(). See refactor/T95_unvalidated_enum_casts.md.
//
// MovementMode and PositionState (client/eq.h) are plain enums with no
// fixed underlying type and a contiguous 0..N range, so a simple min/max
// check suffices. PetCommand (client/pet_constants.h) has a FIXED
// underlying type (uint32_t) but a NON-contiguous valid set (gaps at 0,
// 15, and 22+), so it needs membership checking, not a range check --
// deliberately not sharing the same helper shape as the other two.
// ---------------------------------------------------------------------

bool EqActionHandler::isValidMovementMode(int mode) {
    return mode >= static_cast<int>(MOVE_MODE_RUN) &&
           mode <= static_cast<int>(MOVE_MODE_SNEAK);
}

bool EqActionHandler::isValidPositionState(int state) {
    return state >= static_cast<int>(POS_STANDING) &&
           state <= static_cast<int>(POS_DEAD);
}

bool EqActionHandler::isValidPetCommand(uint8_t command) {
    switch (command) {
        case EQT::PET_BACKOFF:
        case EQT::PET_GETLOST:
        case EQT::PET_ASYOUWERE:
        case EQT::PET_HEALTHREPORT:
        case EQT::PET_GUARDHERE:
        case EQT::PET_GUARDME:
        case EQT::PET_ATTACK:
        case EQT::PET_FOLLOWME:
        case EQT::PET_SITDOWN:
        case EQT::PET_STANDUP:
        case EQT::PET_TAUNT:
        case EQT::PET_HOLD:
        case EQT::PET_TAUNT_ON:
        case EQT::PET_TAUNT_OFF:
        case EQT::PET_LEADER:
        case EQT::PET_FEIGN:
        case EQT::PET_SPELLHOLD:
        case EQT::PET_FOCUS:
        case EQT::PET_HOLD_ON:
        case EQT::PET_HOLD_OFF:
            return true;
        default:
            return false;
    }
}

void EqActionHandler::updateRendererTargetInfo(uint16_t spawnId) {
    // D20e1: TargetChanged event is already published by ProcessBridgeIntents
    // handler in eq.cpp when TargetIntent is processed. This method only
    // needs to update the game-state target ID.
    m_eq.SetCurrentTargetId(spawnId);
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
    // T90: EverQuest::SetHeading() stores its argument raw into m_heading,
    // which every other site (PlayerProfile zone-in, eq_movement.cpp's
    // outbound server_heading = 90 - m_heading, the self ClientUpdate echo)
    // treats as plain 0-360 degrees -- confirmed by reading SetHeading()
    // itself (eq.cpp) rather than assuming. This function used to also
    // convert degrees to EQ's 0-512 unit range before calling it, double-
    // converting (e.g. setHeading(90) produced m_heading == 128). Pass
    // degrees straight through; only normalize into [0, 360).
    float normalizedHeading = heading;
    while (normalizedHeading < 0.0f) normalizedHeading += 360.0f;
    while (normalizedHeading >= 360.0f) normalizedHeading -= 360.0f;
    m_eq.SetHeading(normalizedHeading);
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

// T88: FRAME CONTRACT -- x, y, z are CLIENT frame (same as EverQuest::Move(),
// which this forwards to verbatim -- see that function's comment in
// eq_movement.cpp for the full swap relationship to server/wire frame).
// Callers reading coordinates off another client's own wire self-report
// (e.g. a join spec captured from a live session) are reading SERVER frame
// and must swap x/y before calling this.
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
        updateRendererTargetInfo(spawnId);
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

void EqActionHandler::targetNearestPC() {
    const auto& gameState = m_eq.GetGameState();
    auto& player = gameState.player();
    auto& entities = gameState.entities();

    // Exclude self from nearest PC search
    auto* nearestPC = entities.getNearestPlayer(player.x(), player.y(), player.z(), player.spawnId());
    if (nearestPC) {
        auto* combat = getCombatManager();
        if (combat) {
            combat->SetTarget(nearestPC->spawnId);
            updateRendererTargetInfo(nearestPC->spawnId);
        }
    }
}

void EqActionHandler::targetNearestNPC() {
    const auto& gameState = m_eq.GetGameState();
    auto& player = gameState.player();
    auto& entities = gameState.entities();

    auto* nearestNPC = entities.getNearestNPC(player.x(), player.y(), player.z());
    if (nearestNPC) {
        auto* combat = getCombatManager();
        if (combat) {
            combat->SetTarget(nearestNPC->spawnId);
            updateRendererTargetInfo(nearestNPC->spawnId);
        }
    }
}

void EqActionHandler::cycleTargets() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->CycleTargets(true);  // Forward
        updateRendererTargetInfo(combat->GetTargetId());
    }
}

void EqActionHandler::cycleTargetsReverse() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->CycleTargets(false);  // Reverse
        updateRendererTargetInfo(combat->GetTargetId());
    }
}

void EqActionHandler::clearTarget() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->ClearTarget();
        updateRendererTargetInfo(0);
    }
}

void EqActionHandler::targetSelf() {
    auto* combat = getCombatManager();
    if (!combat) return;

    uint16_t playerId = m_eq.GetMySpawnID();
    uint16_t petId = m_eq.GetPetSpawnId();
    uint16_t currentTarget = combat->GetTargetId();

    uint16_t newTarget;
    if (currentTarget == playerId && petId != 0) {
        // Already targeting self and have a pet — toggle to pet
        newTarget = petId;
    } else if (currentTarget == petId) {
        // Currently targeting pet — toggle back to self
        newTarget = playerId;
    } else {
        // Default: target self
        newTarget = playerId;
    }

    if (newTarget != 0) {
        combat->SetTarget(newTarget);
        updateRendererTargetInfo(newTarget);
    }
}

void EqActionHandler::targetGroupMember(int index) {
    const GroupMember* member = m_eq.GetGroupMember(index);
    if (member && member->spawn_id != 0) {
        auto* combat = getCombatManager();
        if (combat) {
            combat->SetTarget(member->spawn_id);
            updateRendererTargetInfo(member->spawn_id);
        }
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
            m_eq.SendChatMessage("Hail, " + EQT::toDisplayName(entity->name), "say");
            return;
        }
    }
    // No target or target name not found — fall back to plain hail
    hail();
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
    const std::string& lastSender = m_eq.GetLastTellSender();
    if (lastSender.empty()) {
        LOG_DEBUG(MOD_INPUT, "Reply to last tell: No one to reply to");
        return;
    }
    m_eq.SendChatMessage(message, "tell", lastSender);
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
    // T95: `command` arrives here as a caller-supplied raw byte (currently
    // only ever a compile-time constant from ActionDispatcher's /pet
    // convenience wrappers -- see the plan's Review for the reachability
    // finding). EQT::PetCommand has a fixed uint32_t underlying type, so an
    // out-of-range static_cast is not technically UB the way the plain
    // MovementMode/PositionState casts below are -- but it still silently
    // sends a meaningless command byte to the server, which is exactly the
    // caller-bug-hiding failure mode this unit exists to close. Validate
    // against the actual (non-contiguous) enumerator set, not a min/max
    // range: PetCommand has gaps (no 0, no 15, no 22+).
    if (!isValidPetCommand(command)) {
        LOG_ERROR(MOD_COMBAT,
                  "sendPetCommand: {} is not a valid pet command byte "
                  "(valid: 1-14, 16-21 -- see EQT::PetCommand in "
                  "pet_constants.h); command rejected, nothing sent",
                  static_cast<int>(command));
        m_eq.AddChatSystemMessage(
            "Pet command " + std::to_string(static_cast<int>(command)) +
            " is not valid and was not sent.");
        return;
    }
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

// ========== Extended Movement ==========

void EqActionHandler::setMovementMode(int mode) {
    // T95: `mode` arrives here as a caller-supplied int from
    // ActionDispatcher::setMovementMode(), which every current call site
    // (CommandProcessor's /run,/walk,/sneak) passes as a compile-time
    // constant -- but the public IActionHandler surface accepts any int,
    // and MovementMode is a plain enum with no fixed underlying type: an
    // out-of-range static_cast into it is undefined behaviour, not merely
    // a wrong result. Validate before casting.
    if (!isValidMovementMode(mode)) {
        LOG_ERROR(MOD_MOVEMENT,
                  "setMovementMode: {} is not a valid movement mode "
                  "(valid: 0=Run, 1=Walk, 2=Sneak); rejected, mode unchanged",
                  mode);
        m_eq.AddChatSystemMessage(
            "Movement mode " + std::to_string(mode) +
            " is not valid (must be 0=Run, 1=Walk, or 2=Sneak).");
        return;
    }
    m_eq.SetMovementMode(static_cast<MovementMode>(mode));
}

void EqActionHandler::setPositionState(int state) {
    // T95: same shape as setMovementMode() above -- PositionState is also
    // a plain enum with no fixed underlying type, so an unvalidated
    // out-of-range cast is undefined behaviour.
    if (!isValidPositionState(state)) {
        LOG_ERROR(MOD_MOVEMENT,
                  "setPositionState: {} is not a valid position state "
                  "(valid: 0=Standing, 1=Sitting, 2=Crouching, "
                  "3=FeignDeath, 4=Dead); rejected, state unchanged",
                  state);
        m_eq.AddChatSystemMessage(
            "Position state " + std::to_string(state) +
            " is not valid (must be 0-4: Standing/Sitting/Crouching/"
            "FeignDeath/Dead).");
        return;
    }
    m_eq.SetPositionState(static_cast<PositionState>(state));
}

// ========== Extended Character State ==========

void EqActionHandler::setRoleplay(bool rp) {
    m_eq.SetRoleplay(rp);
}

void EqActionHandler::setPathfinding(bool enabled) {
    m_eq.SetPathfinding(enabled);
}

// ========== Extended Combat ==========

void EqActionHandler::setAutoHunting(bool enabled) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetAutoHunting(enabled);
    }
}

void EqActionHandler::setAutoLoot(bool enabled) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->SetAutoLootEnabled(enabled);
    }
}

void EqActionHandler::listHuntTargets() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->ListHuntTargets();
    }
}

// ========== Entity Query ==========

void EqActionHandler::listEntities(const std::string& filter) {
    m_eq.ListEntities(filter);
}

void EqActionHandler::dumpEntityAppearance(const std::string& name) {
    m_eq.DumpEntityAppearance(name);
}

// ========== Utility ==========

void EqActionHandler::sendAnimation(uint8_t animationId, uint8_t speed) {
    m_eq.SendAnimation(animationId, speed);
}

void EqActionHandler::sendPositionUpdate() {
    m_eq.SendPositionUpdate();
}

} // namespace eqt
