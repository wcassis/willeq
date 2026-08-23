#include "client/eq_action_handler.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/pet_constants.h"
#include "client/spell/spell_manager.h"
#include "client/skill/skill_manager.h"      // T156: useSkill() reaches SkillManager
#include "client/skill/skill_constants.h"
#include "client/trade_manager.h"
#include "client/world_object.h"  // T149: open-container object_type lookup
#include "client/state/game_state.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include "common/performance_metrics.h"
#include <fmt/format.h>
#include <cmath>
#include <algorithm>  // T180: /raid list sorts the roster
#include <vector>

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

// ========== Combat (T102) ==========
// Ported from game's Combat-category handlers (eq_commands_movement.cpp/
// eq_commands.cpp). targetId==0 means "no explicit name given" --
// CommandProcessor resolves an explicit name via GameState::entities()
// before calling; when it passes 0, the live CombatManager target (read
// here, not via GameState::combat() -- see T28's comment on
// assist_cmd/shield_cmd/duel_cmd, "never populated in production") is used,
// matching game's own "use current target" fallback exactly.

void EqActionHandler::assist(uint16_t targetId) {
    auto* combat = getCombatManager();
    if (!combat) return;

    uint16_t resolved = targetId;
    if (resolved == 0) {
        if (!combat->HasTarget()) {
            m_eq.AddChatSystemMessage("You don't have a target.");
            return;
        }
        resolved = combat->GetTargetId();
    }
    m_eq.SendAssist(resolved);
}

void EqActionHandler::shieldTarget(uint16_t targetId) {
    auto* combat = getCombatManager();
    if (!combat) return;

    uint16_t resolved = targetId;
    if (resolved == 0) {
        if (!combat->HasTarget()) {
            m_eq.AddChatSystemMessage("You don't have a target to shield.");
            return;
        }
        resolved = combat->GetTargetId();
    }
    m_eq.SendShielding(resolved);
}

void EqActionHandler::taunt() {
    auto* combat = getCombatManager();
    if (!combat || !combat->HasTarget()) {
        m_eq.AddChatSystemMessage("No target to taunt");
        return;
    }
    // T162: this used to call CombatManager::Taunt() directly. That put
    // HC_OP_Taunt on the wire for ANY class and printed "Taunting target" --
    // T161 watched a Rogue do exactly that 45s after the same character had
    // been correctly refused /useskill taunt, and watched the server bin the
    // packet in silence. T159's possession gate lives inside
    // SkillManager::activateSkill(), so the fix is to reach it, NOT to grow a
    // second copy of its predicate here (the duplicated-then-drifted class
    // this codebase keeps paying for).
    //
    // useSkill() IS that entry point: it resolves the current target and
    // reports activateSkill()'s own refusal. Skill 73 still ends at
    // CombatManager::Taunt() inside sendBodiedSkillPacket(), which T152 left
    // as the ONLY Taunt sender -- so this is one send, not two. What is new
    // for /taunt is what /useskill taunt already did and T161 verified live
    // for a Warrior: the kick gesture animation, and Taunt's 5s recast, now
    // shared between the two commands instead of one of them dodging it.
    //
    // The success line is now activateSkill()'s own "You use Taunt!" via the
    // bridge; "Taunting target" is deleted rather than printed alongside it.
    useSkill(EQ::kSkillTaunt);
}

void EqActionHandler::toggleAutoFire() {
    auto* combat = getCombatManager();
    if (!combat) return;
    if (combat->IsAutoFireEnabled()) {
        combat->DisableAutoFire();
        m_eq.AddChatSystemMessage("Auto fire OFF");
    } else if (combat->HasTarget()) {
        combat->EnableAutoFire();
        m_eq.AddChatSystemMessage("Auto fire ON");
    } else {
        m_eq.AddChatSystemMessage("No target - auto fire not enabled");
    }
}

// enableCombatManager()/disableCombatManager()/toggleCombatManager() wrap
// CombatManager's own m_enabled flag (IsEnabled()/Enable()/Disable()) --
// NOT the same flag as auto-attack (above) or setAutoHunting() (below,
// m_auto_hunting_enabled). This is /combat on|off|<toggle>, T26's
// historically-unreachable command; T29's unified command channel made it
// reachable in `game`, and T102 gives it the sole (migrated) registration.
void EqActionHandler::enableCombatManager() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->Enable();
        m_eq.AddChatSystemMessage("Combat manager enabled");
    }
}

void EqActionHandler::disableCombatManager() {
    auto* combat = getCombatManager();
    if (combat) {
        combat->Disable();
        m_eq.AddChatSystemMessage("Combat manager disabled");
    }
}

void EqActionHandler::toggleCombatManager() {
    auto* combat = getCombatManager();
    if (!combat) return;
    if (combat->IsEnabled()) {
        combat->Disable();
        m_eq.AddChatSystemMessage("Combat manager disabled");
    } else {
        combat->Enable();
        m_eq.AddChatSystemMessage("Combat manager enabled");
    }
}

void EqActionHandler::duelRequest(uint16_t targetId) {
    auto* combat = getCombatManager();
    if (!combat) return;

    uint16_t resolved = targetId;
    if (resolved == 0) {
        if (!combat->HasTarget()) {
            m_eq.AddChatSystemMessage("You don't have a target.");
            return;
        }
        resolved = combat->GetTargetId();
    }
    m_eq.SendDuelRequest(resolved);
}

// duelAccept()/duelDecline(): EverQuest's own pending-duel state
// (HasPendingDuel()/GetPendingDuelInitiatorId(), both public) is read
// directly -- GameState has no duel domain to project it into, and unlike
// combat targeting there is no CombatManager-equivalent "authoritative
// live source" to delegate to instead.
void EqActionHandler::duelAccept() {
    if (!m_eq.HasPendingDuel()) {
        m_eq.AddChatSystemMessage("You have no pending duel challenge.");
        return;
    }
    m_eq.SendDuelAccept(m_eq.GetPendingDuelInitiatorId());
}

void EqActionHandler::duelDecline() {
    if (!m_eq.HasPendingDuel()) {
        m_eq.AddChatSystemMessage("You have no pending duel challenge.");
        return;
    }
    m_eq.SendDuelDecline(m_eq.GetPendingDuelInitiatorId());
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
    // T101: repointed from CombatManager::InterruptCast() to
    // EQ::SpellManager -- ported from game's /interrupt (eq_commands_
    // spells.cpp), which is the subsystem the real /cast path
    // (beginCastFromGem() below) actually uses. Not covered by any
    // existing test against real CombatManager state (T93 tests castSpell/
    // castSpellOnTarget/memorizeSpell only), so this repoint is safe -- see
    // refactor/T101_migrate_spells_pet_family.md's Review.
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    if (spellMgr->isCasting()) {
        spellMgr->interruptCast();
        m_eq.AddChatSystemMessage("Casting interrupted");
    } else {
        m_eq.AddChatSystemMessage("Not casting");
    }
}

void EqActionHandler::beginCastFromGem(uint8_t gemSlot, uint16_t targetId) {
    // T101: ported from game's /cast (eq_commands_spells.cpp) verbatim --
    // the real EQ::SpellManager casting path, distinct from castSpell()/
    // castSpellOnTarget() above (CombatManager, untouched per T93).
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    // gemSlot arrives 1-indexed (ActionDispatcher::beginCastFromGem()
    // already validated 1-8); SpellManager's gems are 0-indexed.
    EQ::CastResult result = spellMgr->beginCastFromGem(static_cast<uint8_t>(gemSlot - 1), targetId);
    switch (result) {
        case EQ::CastResult::Success:
            m_eq.AddChatSystemMessage("Casting from gem " + std::to_string(gemSlot));
            break;
        case EQ::CastResult::NotMemorized:
            m_eq.AddChatSystemMessage("No spell in gem " + std::to_string(gemSlot));
            break;
        case EQ::CastResult::NotEnoughMana:
            m_eq.AddChatSystemMessage("Insufficient mana");
            break;
        case EQ::CastResult::SpellNotReady:
            m_eq.AddChatSystemMessage("Spell not ready");
            break;
        case EQ::CastResult::AlreadyCasting:
            m_eq.AddChatSystemMessage("Already casting");
            break;
        default:
            m_eq.AddChatSystemMessage("Cannot cast spell");
            break;
    }
}

void EqActionHandler::useAbility(uint32_t abilityId) {
    // Abilities use similar slot-based activation as spells
    LOG_DEBUG(MOD_COMBAT, "Use ability {} not yet implemented", abilityId);
}

void EqActionHandler::useSkill(uint32_t skillId) {
    // T156: was a "not yet implemented" stub, and the reason T155 found all
    // five of T152's skill senders unreachable. SkillManager::activateSkill()
    // does the real work (cap/cooldown/activatable checks, packet, animation);
    // this method's job is to reach it with a validated id and a real target.
    auto* skills = m_eq.GetSkillManager();
    if (!skills) {
        m_eq.AddChatSystemMessage("Skill system not available");
        return;
    }
    if (skillId >= EQ::MAX_SKILL_ID) {
        // T95: never cast an unvalidated int into the skill table.
        m_eq.AddChatSystemMessage(fmt::format("Unknown skill id {}", skillId));
        return;
    }
    const uint8_t id = static_cast<uint8_t>(skillId);

    // The plan's explicit ask: pass the CURRENT target. activateSkill() would
    // fall back to CombatManager itself for requires_target skills, but only
    // for those — resolving here keeps the caller's intent visible and
    // testable, and matches taunt()/shieldTarget()'s established shape.
    auto* combat = getCombatManager();
    const uint16_t targetId = (combat && combat->HasTarget()) ? combat->GetTargetId() : 0;

    if (!skills->activateSkill(id, targetId)) {
        std::string error = skills->getSkillUseError(id);
        m_eq.AddChatSystemMessage(error.empty()
            ? fmt::format("You cannot use {} right now.", EQ::getSkillName(id))
            : error);
        return;
    }
    LOG_DEBUG(MOD_COMBAT, "useSkill {} ({}) on target {}", id, EQ::getSkillName(id), targetId);
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

// T141: /equip's three input forms all arrive as one free-text string --
// same shape as warp()/debugLevel()/timeOfDay() (T140). All parsing/
// legality/execution happens in EverQuest::RequestEquip() ->
// InventoryManager; nothing is duplicated here. See
// refactor/T141_equip_extraction.md.
void EqActionHandler::equip(const std::string& args) {
    m_eq.RequestEquip(args);
}

// ========== Spellbook ==========

void EqActionHandler::memorizeSpell(uint8_t gemSlot, uint32_t spellId) {
    auto* combat = getCombatManager();
    if (combat) {
        combat->MemorizeSpell(spellId, static_cast<SpellSlot>(gemSlot - 1));
    }
}

void EqActionHandler::forgetSpell(uint8_t gemSlot) {
    // T101: fixed -- ported from game's /forget (eq_commands_spells.cpp).
    // Was a stub ("Would need to send forget spell packet") that always
    // silently no-op'd; T97 flagged this as a false-success two layers
    // down from a dispatcher method that otherwise looked fully wired.
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    // gemSlot arrives 1-indexed (ActionDispatcher::forgetSpell() already
    // validated 1-8); SpellManager's gems are 0-indexed.
    if (spellMgr->forgetSpell(static_cast<uint8_t>(gemSlot - 1))) {
        m_eq.AddChatSystemMessage("Forgot spell in gem " + std::to_string(gemSlot));
    } else {
        m_eq.AddChatSystemMessage("No spell in gem " + std::to_string(gemSlot));
    }
}

void EqActionHandler::openSpellbook() {
    // UI state - would need renderer integration
    LOG_DEBUG(MOD_UI, "Open spellbook not yet implemented");
}

void EqActionHandler::closeSpellbook() {
    LOG_DEBUG(MOD_UI, "Close spellbook not yet implemented");
}

void EqActionHandler::memorizeSpellByName(const std::string& spellName, uint8_t gemSlot) {
    // T101: ported from game's /mem (eq_commands_spells.cpp) verbatim --
    // exact match, else partial match with disambiguation (first 5 shown).
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }

    const EQ::SpellData* spell = spellMgr->getDatabase().getSpellByName(spellName);
    if (!spell) {
        auto matches = spellMgr->getDatabase().findSpellsByName(spellName);
        if (matches.empty()) {
            m_eq.AddChatSystemMessage("Spell '" + spellName + "' not found");
            return;
        }
        if (matches.size() == 1) {
            spell = matches[0];
        } else {
            m_eq.AddChatSystemMessage("Multiple matches found:");
            size_t count = 0;
            for (const auto* s : matches) {
                m_eq.AddChatSystemMessage("  " + s->name + " (ID: " + std::to_string(s->id) + ")");
                if (++count >= 5) {
                    if (matches.size() > 5) {
                        m_eq.AddChatSystemMessage("  ... and " + std::to_string(matches.size() - 5) + " more");
                    }
                    break;
                }
            }
            return;
        }
    }

    // gemSlot arrives 1-indexed (ActionDispatcher::memorizeSpellByName()
    // already validated 1-8); SpellManager's gems are 0-indexed.
    if (spellMgr->memorizeSpell(spell->id, static_cast<uint8_t>(gemSlot - 1))) {
        m_eq.AddChatSystemMessage("Memorizing " + spell->name + " in gem " + std::to_string(gemSlot));
    } else {
        m_eq.AddChatSystemMessage("Cannot memorize " + spell->name + " - check if scribed and level requirement");
    }
}

void EqActionHandler::listMemorizedSpells() {
    // T101: ported from game's /gems (eq_commands_spells.cpp) verbatim --
    // fixes the "(Spell data not available in current implementation)"
    // stub T97 found underneath the pre-T101 dispatcher method.
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    m_eq.AddChatSystemMessage("=== Spell Gems ===");
    for (int i = 0; i < 8; i++) {
        uint32_t spellId = spellMgr->getMemorizedSpell(static_cast<uint8_t>(i));
        if (spellId != EQ::SPELL_UNKNOWN && spellId != 0xFFFFFFFF) {
            const EQ::SpellData* spell = spellMgr->getSpell(spellId);
            std::string stateStr;
            EQ::GemState state = spellMgr->getGemState(static_cast<uint8_t>(i));
            switch (state) {
                case EQ::GemState::Ready: stateStr = "Ready"; break;
                case EQ::GemState::Refresh:
                    stateStr = "Refresh (" + std::to_string(spellMgr->getGemCooldownRemaining(static_cast<uint8_t>(i)) / 1000) + "s)";
                    break;
                case EQ::GemState::MemorizeProgress:
                    stateStr = "Memorizing (" + std::to_string(static_cast<int>(spellMgr->getMemorizeProgress(static_cast<uint8_t>(i)) * 100)) + "%)";
                    break;
                case EQ::GemState::Casting: stateStr = "Casting"; break;
                default: stateStr = ""; break;
            }
            std::string spellLabel = spell ? spell->name : ("Unknown(" + std::to_string(spellId) + ")");
            m_eq.AddChatSystemMessage("[" + std::to_string(i + 1) + "] " + spellLabel + " " + stateStr);
        } else {
            m_eq.AddChatSystemMessage("[" + std::to_string(i + 1) + "] <empty>");
        }
    }
}

// ========== Trade ==========

// T102: repointed (not a new method -- T101's rule: repoint when the
// existing method's internals are untested; test_action_dispatcher.cpp's
// coverage of requestTrade/acceptTrade/cancelTrade is fake-handler routing
// only, confirmed before making this change). Ported from game's trade_cmd/
// accept_cmd/canceltrade_cmd (eq_commands.cpp). targetId==0 fallback and
// name resolution follow the same split as assist()/shieldTarget() above.
// The self-trade guard from game's trade_cmd is NOT ported: it needs "my
// own spawn id", which has no public accessor from this layer (unlike
// CombatManager's target, TradeManager/GameState don't expose it) --
// flagged as a deviation in the Review rather than silently dropped.
//
// T103: precondition/display reads below now go through
// m_eq.GetGameState().trade() (state::TradeState, the projection T103
// added) instead of reading TradeManager directly -- closing the layering
// compromise T102 recorded honestly rather than hiding. TradeManager
// itself is still reached for the four calls that actually perform a
// trade action (requestTrade()/acceptTradeRequest()/cancelTrade()/
// rejectTradeRequest() -- these send real packets, nothing else does that)
// -- same shape as CombatManager's calls elsewhere in this file.
void EqActionHandler::requestTrade(uint16_t targetId) {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    if (m_eq.GetGameState().trade().active()) {
        m_eq.AddChatSystemMessage("You are already trading");
        return;
    }
    auto* combat = getCombatManager();
    if (combat && combat->GetCombatState() == COMBAT_STATE_ENGAGED) {
        m_eq.AddChatSystemMessage("You cannot trade while in combat");
        return;
    }

    uint16_t resolved = targetId;
    if (resolved == 0 && combat && combat->HasTarget()) {
        resolved = combat->GetTargetId();
    }
    if (resolved == 0) {
        m_eq.AddChatSystemMessage("No target - use /trade <name> or target a player");
        return;
    }

    const auto& entities = m_eq.GetGameState().entities();
    const auto* entity = entities.getEntity(resolved);
    bool isNpc = entity && entity->npcType == 1;
    std::string targetName = entity ? entity->name : std::string();

    if (entity) {
        auto& player = m_eq.GetGameState().player();
        float dx = player.x() - entity->x;
        float dy = player.y() - entity->y;
        float dz = player.z() - entity->z;
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        constexpr float MAX_TRADE_DISTANCE = 150.0f;
        if (distance > MAX_TRADE_DISTANCE) {
            m_eq.AddChatSystemMessage("You are too far away to trade");
            return;
        }
    }

    trade->requestTrade(resolved, targetName, isNpc);
    m_eq.AddChatSystemMessage(fmt::format("Requesting trade with {}", EQT::toDisplayName(targetName)));
}

void EqActionHandler::acceptTrade() {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    if (!m_eq.GetGameState().trade().hasPendingRequest()) {
        m_eq.AddChatSystemMessage("No pending trade request");
        return;
    }
    auto* combat = getCombatManager();
    if (combat && combat->GetCombatState() == COMBAT_STATE_ENGAGED) {
        m_eq.AddChatSystemMessage("You cannot trade while in combat");
        return;
    }
    // T103: acceptTradeRequest() drives TradeManager's setState(Active),
    // which synchronously (same thread, same call) fires
    // EverQuest::SetupTradeWindowCallbacks()'s Active branch and populates
    // m_eq.GetGameState().trade() BEFORE this call returns -- so reading the
    // projection's partnerName() after the call is safe and correct, and
    // sidesteps the read-before-populate aliasing class of bug entirely
    // (the pre-T103 fix here read TradeManager::getPartnerName() after
    // acceptTradeRequest() too, for the same reason -- see T102's Review --
    // this is the same ordering, just through the projection instead of
    // TradeManager directly).
    trade->acceptTradeRequest();
    m_eq.AddChatSystemMessage(fmt::format("Accepting trade with {}",
        EQT::toDisplayName(m_eq.GetGameState().trade().partnerName())));
}

void EqActionHandler::cancelTrade() {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    if (!m_eq.GetGameState().trade().active()) {
        m_eq.AddChatSystemMessage("You are not trading");
        return;
    }
    trade->cancelTrade();
    m_eq.AddChatSystemMessage("Trade cancelled");
}

void EqActionHandler::declineTrade() {
    auto* trade = m_eq.GetTradeManager();
    if (!trade) {
        m_eq.AddChatSystemMessage("Trade not available");
        return;
    }
    auto& tradeState = m_eq.GetGameState().trade();
    if (!tradeState.hasPendingRequest()) {
        m_eq.AddChatSystemMessage("No pending trade request");
        return;
    }
    // T103: the partner name CAN now be included. TradeManager itself still
    // exposes no accessor for it on the decline path (only m_partner_name,
    // which rejectTradeRequest() never populates -- only
    // acceptTradeRequest() does; see T102's Review) -- but
    // state::TradeState::pendingPartnerName() is populated from the
    // TradeRequestReceived event fired when the request first arrived
    // (eq_merchant.cpp), independent of that gap. Read before rejecting,
    // since the reject call below clears it.
    std::string partnerName = EQT::toDisplayName(tradeState.pendingPartnerName());
    trade->rejectTradeRequest();
    // T103 finding, reported not fixed (trade_manager.cpp is outside this
    // unit's territory): TradeManager::rejectTradeRequest() is the one
    // trade-state transition that never invokes m_on_cancelled() -- unlike
    // cancelTrade() and the partner-initiated handleCancelTrade(), it calls
    // resetTradeState() directly with no callback, so a LOCAL decline
    // publishes no TradeCancelled event at all. Left as-is, this would
    // leave hasPendingRequest() stuck true in the projection forever after
    // a successful decline (until the next TradeRequestReceived/
    // TradeStarted/TradeCancelled happened to overwrite it). Worked around
    // here, in-territory: clear the projection directly rather than rely on
    // an event that TradeManager never sends for this specific transition.
    m_eq.GetGameState().trade().reset();
    m_eq.AddChatSystemMessage(fmt::format("Declined trade request from {}", partnerName));
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

// T149 -- THE design point of this unit.
//
// object_type is the tradeskill container's type (17 = forge, 15 = oven, ...).
// It is NOT typed by the user: it is read off the container the server has
// told us is open. EverQuest::ZoneProcessClickObjectAction() records the
// container's drop_id in m_active_tradeskill_object_id when the server sends
// OP_ClickObjectAction with open==1, and T144 made
// WorldObject::object_type carry the real Titanium value (before T144 it was
// read past the end of an 8-byte-wrong struct and was 0 for every object in
// the world). Both accessors already existed on EverQuest --
// GetActiveTradeskillObjectId() and GetWorldObject() -- so no new cross-layer
// state was needed and none was invented.
//
// Scope note: m_active_tradeskill_object_id tracks WORLD containers only
// (forges, ovens, looms). Tradeskill containers carried in inventory
// (mixing bowls, sewing kits) open through a different path and would need
// the wire's `some_id` set to the container's item id; every sender here
// leaves some_id at 0, which is exactly what eqemu requires for a world
// combiner and wrong for an inventory one. Inventory containers are out of
// scope for this unit and are named as owed in the plan's Review.
bool EqActionHandler::resolveOpenContainerObjectType(uint32_t& objectTypeOut) {
    uint32_t dropId = m_eq.GetActiveTradeskillObjectId();
    if (dropId == 0) {
        m_eq.AddChatSystemMessage("You must have a tradeskill container open.");
        return false;
    }

    const eqt::WorldObject* obj = m_eq.GetWorldObject(dropId);
    if (obj == nullptr) {
        // The container is open per the server but we have no ground-spawn
        // record for it -- report rather than guessing a type.
        LOG_WARN(MOD_UI, "Open tradeskill container {} has no world object record", dropId);
        m_eq.AddChatSystemMessage("The open container is not recognized.");
        return false;
    }

    objectTypeOut = obj->object_type;
    return true;
}

void EqActionHandler::recipeSearch(const std::string& query, uint32_t minTrivial,
                                   uint32_t maxTrivial) {
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    LOG_DEBUG(MOD_UI, "Recipe search in container type {} query='{}' trivial {}..{}",
              objectType, query, minTrivial, maxTrivial);
    m_eq.SendRecipesSearch(objectType, query, minTrivial, maxTrivial);
}

void EqActionHandler::recipeDetails(uint32_t recipeId) {
    // OP_RecipeDetails carries only the recipe id -- no object_type on the
    // wire (eqemu Handle_OP_RecipeDetails reads a bare uint32). Still gated
    // on an open container, because a details lookup outside one is
    // meaningless to the player and the ids come from a search reply.
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    m_eq.SendRecipeDetails(recipeId);
}

void EqActionHandler::recipeAutoCombine(uint32_t recipeId) {
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    m_eq.SendRecipeAutoCombine(objectType, recipeId);
}

void EqActionHandler::recipesFavorite(const std::vector<uint32_t>& recipeIds) {
    uint32_t objectType = 0;
    if (!resolveOpenContainerObjectType(objectType)) {
        return;
    }
    m_eq.SendRecipesFavorite(objectType, recipeIds);
}

// T153: /appearance. SendFaceChange() had zero production callers, so nothing
// had ever exercised it; this is the wiring, and the merge below is the reason
// it needs a handler rather than going straight from CommandProcessor.
//
// The wire struct is all-or-nothing -- eqemu's Handle_OP_FaceChange
// (zone/client_packet.cpp:6387) copies all seven fields into the PlayerProfile
// and Save()s -- so any field the user did not name must be re-sent at its
// current value or it is silently reset.
//
// T167: the base is now the PLAYER PROFILE, which is the same storage eqemu
// is about to overwrite, rather than the player's Entity.
//   * It is the only source for eyecolor1/eyecolor2 at all. Entity has no
//     eyecolor fields, so before T167 both eyes were reset to 0 by every
//     /appearance -- the bug that motivated this unit.
//   * It is also the CORRECT source for the other five. Entity's copies are
//     the current VISUAL, which an illusion overwrites
//     (eq_entities.cpp:2131-2134). Merging from Entity while illusioned would
//     persist the illusion's hair and beard into the character's saved
//     profile; merging from the profile cannot.
// Entity remains the fallback for its five fields when no profile has been
// parsed, which preserves T153's behaviour on that path.
//
// If NEITHER source is available the request is REFUSED rather than sent as a
// fabricated all-zero body. Zero is a legal value for every field, so an
// unknown appearance and a real all-zero one are indistinguishable, and
// guessing wrong here permanently rewrites the character on the server.
void EqActionHandler::setAppearance(const action::AppearanceChange& change) {
    EQT::ProfileAppearance base = m_eq.GetProfileAppearance();

    if (!base.valid) {
        const auto& entities = m_eq.GetEntities();
        auto self = entities.find(m_eq.GetEntityID());
        if (self == entities.end()) {
            LOG_WARN(MOD_MAIN,
                     "setAppearance: no PlayerProfile parsed and no self "
                     "entity -- refusing to send a fabricated appearance");
            m_eq.AddChatSystemMessage(
                "Your appearance is not known yet -- try again after you have "
                "fully zoned in.");
            return;
        }
        base.haircolor = self->second.haircolor;
        base.beardcolor = self->second.beardcolor;
        base.hairstyle = self->second.hairstyle;
        base.beard = self->second.beard;
        base.face = self->second.face;
        // eyecolor1/eyecolor2 stay 0: nothing on this path knows them.
        LOG_WARN(MOD_MAIN,
                 "setAppearance: no PlayerProfile parsed; merging from the "
                 "self entity, so eye colors will be sent as 0");
    }

    if (change.haircolor) base.haircolor = *change.haircolor;
    if (change.beardcolor) base.beardcolor = *change.beardcolor;
    if (change.eyecolor1) base.eyecolor1 = *change.eyecolor1;
    if (change.eyecolor2) base.eyecolor2 = *change.eyecolor2;
    if (change.hairstyle) base.hairstyle = *change.hairstyle;
    if (change.beard) base.beard = *change.beard;
    if (change.face) base.face = *change.face;

    m_eq.AddChatSystemMessage(fmt::format(
        "Requesting appearance: haircolor={} beardcolor={} eyecolor1={} "
        "eyecolor2={} hairstyle={} beard={} face={}",
        base.haircolor, base.beardcolor, base.eyecolor1, base.eyecolor2,
        base.hairstyle, base.beard, base.face));

    m_eq.SendFaceChange(base.haircolor, base.beardcolor, base.eyecolor1,
                        base.eyecolor2, base.hairstyle, base.beard, base.face);

    // Handle_OP_FaceChange applies all seven fields and Save()s with no
    // rejection path for a correctly-sized body (verified in eqemu source;
    // T155 confirmed the DB write live), so what we just sent IS the profile
    // now. Recording it keeps a SECOND /appearance from reverting the first
    // -- without this, the eyecolors this unit exists to preserve would only
    // survive one command per zone-in.
    base.valid = true;
    m_eq.SetProfileAppearance(base);
}

// T171: /inspectmessage. Three shapes, one entry point: show (nullopt), clear
// (empty string), set (anything else).
//
// The showing is done here rather than in CommandProcessor because only the
// handler can see EverQuest's copy -- the same reason setAppearance() needs a
// handler at all.
void EqActionHandler::setInspectMessage(const std::optional<std::string>& message) {
    if (!message) {
        const std::string& current = m_eq.GetInspectMessage();
        if (current.empty()) {
            m_eq.AddChatSystemMessage(
                "Your inspect message is empty. Set one with "
                "/inspectmessage <text>.");
        } else {
            m_eq.AddChatSystemMessage(
                fmt::format("Your inspect message: {}", current));
        }
        return;
    }

    m_eq.SetInspectMessage(*message);
    m_eq.SaveInspectMessage();

    // Report what was actually stored, not what was asked for: SetInspectMessage
    // truncates at the server's 255-byte limit and the user should see that.
    const std::string& stored = m_eq.GetInspectMessage();
    if (stored.empty()) {
        m_eq.AddChatSystemMessage("Inspect message cleared.");
    } else {
        m_eq.AddChatSystemMessage(fmt::format("Inspect message set: {}", stored));
    }
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

// ========== Social (T102) ==========
// Ported from game's Social-category handlers (eq_commands.cpp). /who
// (requestWho) ports the Social section's who_cmd -- see
// command_processor.cpp's cmdWho for why that one was chosen over
// Movement's later, intra-registry-shadowing-winner `who`.

void EqActionHandler::inspectTarget(uint16_t targetId) {
    uint16_t resolved = targetId;
    if (resolved == 0) {
        auto* combat = getCombatManager();
        if (combat && combat->HasTarget()) {
            resolved = combat->GetTargetId();
        }
    }
    if (resolved == 0) {
        m_eq.AddChatSystemMessage("No target - use /inspect <name> or target a player");
        return;
    }
    m_eq.SendInspectRequest(resolved);
    m_eq.AddChatSystemMessage("Inspecting...");
}

void EqActionHandler::toggleLFG() {
    m_eq.SendLFGCommand(!m_eq.IsLFG());
}

void EqActionHandler::setSurname(const std::string& surname) {
    m_eq.SendSurname(surname);
}

void EqActionHandler::rollRandom(uint32_t low, uint32_t high) {
    if (high < low) high = low;
    m_eq.SendRandom(low, high);
}

void EqActionHandler::yellForHelp() {
    m_eq.SendYellForHelp();
}

void EqActionHandler::requestWho(const std::string& name, int lvlLow, int lvlHigh) {
    m_eq.SendWhoAllRequest(name, lvlLow, lvlHigh);
    m_eq.AddChatSystemMessage("Searching for players...");
}

// ========== Guild (T132) ==========
//
// Pure routing to the pre-existing senders (src/client/eq_guild.cpp) --
// guild-membership checks, pending-invite checks, and player-facing chat
// messages ("Inviting X to join your guild.", etc.) already live inside
// those Send*() calls, so no logic is duplicated here. game's own command
// handlers called these same methods directly.

void EqActionHandler::guildInvite(const std::string& playerName) {
    m_eq.SendGuildInvite(playerName);
}

void EqActionHandler::guildAcceptInvite() {
    m_eq.SendGuildInviteAccept(true);
}

void EqActionHandler::guildDeclineInvite() {
    m_eq.SendGuildInviteAccept(false);
}

void EqActionHandler::guildRemove(const std::string& playerName) {
    m_eq.SendGuildRemove(playerName);
}

void EqActionHandler::guildDemote(const std::string& playerName) {
    m_eq.SendGuildDemote(playerName);
}

void EqActionHandler::guildMakeLeader(const std::string& playerName) {
    m_eq.SendGuildLeader(playerName);
}

void EqActionHandler::requestGuildMOTD() {
    m_eq.SendGetGuildMOTD();
}

void EqActionHandler::setGuildMOTD(const std::string& motd) {
    m_eq.SendSetGuildMOTD(motd);
}

void EqActionHandler::corpseDrag(const std::string& corpseName) {
    m_eq.SendCorpseDrag(corpseName);
}

void EqActionHandler::corpseDrop() {
    m_eq.SendCorpseDrop();
}

void EqActionHandler::consent(const std::string& playerName) {
    m_eq.SendConsent(playerName);
}

void EqActionHandler::consentDeny(const std::string& playerName) {
    m_eq.SendConsentDeny(playerName);
}

void EqActionHandler::rezAccept() {
    m_eq.SendRezzAnswer(true);
}

void EqActionHandler::rezDecline() {
    m_eq.SendRezzAnswer(false);
}

// ========== Utility (T135) ==========

void EqActionHandler::split(uint32_t platinum, uint32_t gold, uint32_t silver, uint32_t copper) {
    m_eq.SendSplit(platinum, gold, silver, copper);
}

void EqActionHandler::save() {
    m_eq.SendSave();
}

// Ported from game's bindwound_cmd (eq_commands.cpp) exactly: targetId==0
// resolves to the CombatManager's current target if one exists, otherwise
// falls back to SELF (m_my_spawn_id) -- unlike assist()/shieldTarget()
// above, which report "You don't have a target." instead of self-targeting.
// Reuses the same getCombatManager() helper T102 built for the Combat
// family rather than introducing any new targeting concept.
void EqActionHandler::bandage(uint16_t targetId) {
    // T162: this used to call EverQuest::SendBindWound() directly, which put
    // HC_OP_BindWound on the wire with no possession check at all -- the same
    // bypass shape T161 caught on /taunt, one function away. It now goes
    // through the gated entry point.
    //
    // The target rule is UNCHANGED and is now stated once instead of twice:
    // SkillManager::sendBindWound's case 9 already carries this exact
    // "current target, else self" fallback (skill_manager.cpp), copied from
    // this function when T152 wrote it. activateSkill() zeroes the target for
    // skills with requires_target == false (Bind Wound is one, because it may
    // be cast on yourself), so case 9's copy is the one that runs and the
    // duplicate here is deleted rather than left to drift.
    //
    // Bind Wound has NO animation (getSkillAnimationId(9) == 0 and no
    // animation code), so routing through activateSkill() adds no packet. It
    // does add Bind Wound's 1s recast, which /bandage did not have before;
    // that matches /useskill bind wound and is the shorter of the two rules.
    //
    // Cap data: config/skill_caps.json has a Bind Wound row for ALL SIXTEEN
    // classes, so this gate can refuse no class that could bandage before.
    if (targetId != 0) {
        LOG_DEBUG(MOD_COMBAT,
                  "bandage: explicit target {} is resolved inside the skill "
                  "path (current target, else self); no caller passes one",
                  targetId);
    }
    useSkill(EQ::kSkillBindWound);
}

void EqActionHandler::rewind() {
    m_eq.SendRewind();
}

void EqActionHandler::bug(const std::string& description) {
    m_eq.SendReport(description);
}

void EqActionHandler::petition(const std::string& text) {
    m_eq.SendPetition(text);
}


// ========== Raid (T179) ==========
// Raid command action codes (eqemu common/raid.h): 1=AcceptInvite, 3=Invite,
// 5=Disband. The vendorList() shape -- read EverQuest raid state, report here.
void EqActionHandler::raidInvite(const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid invite <name>");
        return;
    }
    // RaidCommandInvite (3): player_name = invitee, leader_name = us.
    m_eq.SendRaidCommand(3, name, m_eq.GetMyName());
    m_eq.AddChatSystemMessage(fmt::format("You invite {} to join your raid.", name));
}

void EqActionHandler::raidAccept() {
    const std::string inviter = m_eq.GetPendingRaidInviter();
    if (inviter.empty()) {
        m_eq.AddChatSystemMessage("You have no pending raid invitation.");
        return;
    }
    // RaidCommandAcceptInvite (1): player_name = the INVITER, leader_name = us.
    m_eq.SendRaidCommand(1, inviter, m_eq.GetMyName());
    m_eq.ClearPendingRaidInvite();
    m_eq.AddChatSystemMessage(fmt::format("You accept {}'s raid invitation.", inviter));
}

void EqActionHandler::raidLeave() {
    if (!m_eq.IsInRaid()) {
        m_eq.AddChatSystemMessage("You are not in a raid.");
        return;
    }
    // RaidCommandDisband (5) removing yourself: both names = us.
    const std::string me = m_eq.GetMyName();
    m_eq.SendRaidCommand(5, me, me);
    m_eq.AddChatSystemMessage("You leave the raid.");
}

void EqActionHandler::raidStatus() {
    if (m_eq.IsInRaid()) {
        const std::string& leader = m_eq.GetRaidLeader();
        if (!leader.empty()) {
            m_eq.AddChatSystemMessage(fmt::format("You are in a raid led by {}.", leader));
        } else {
            m_eq.AddChatSystemMessage("You are in a raid.");
        }
        // T182: only worth saying when it is true -- an unlocked raid is the
        // default and saying so every time is noise.
        if (m_eq.IsRaidLocked()) {
            m_eq.AddChatSystemMessage("The raid is locked.");
        }
    } else {
        m_eq.AddChatSystemMessage("You are not in a raid.");
    }
    const std::string pending = m_eq.GetPendingRaidInviter();
    if (!pending.empty()) {
        m_eq.AddChatSystemMessage(fmt::format(
            "Pending raid invitation from {} -- type /raid accept to join.", pending));
    }
}

// T180. The roster is assembled from the action-0 stream, not read out of a
// roster packet -- Titanium's ENCODE drops eqemu's member list (see
// raid_update.h). Ordered by raid group, then by name, so the printout matches
// how the raid is actually organised rather than the order members happened to
// arrive in. Members in the raid but in no group (kRaidGroupless) sort last,
// under their own heading.
void EqActionHandler::raidList() {
    if (!m_eq.IsInRaid()) {
        m_eq.AddChatSystemMessage("You are not in a raid.");
        return;
    }

    std::vector<EQT::RaidMemberInfo> members = m_eq.GetRaidMembers();
    if (members.empty()) {
        // In a raid but holding no members: either the adds have not arrived
        // yet, or something dropped them. Say which, rather than printing an
        // empty list that reads like an empty raid.
        m_eq.AddChatSystemMessage("Raid member list not received yet.");
        return;
    }

    std::sort(members.begin(), members.end(),
              [](const EQT::RaidMemberInfo& a, const EQT::RaidMemberInfo& b) {
                  if (a.group != b.group) return a.group < b.group;
                  return a.name < b.name;
              });

    const std::string& leader = m_eq.GetRaidLeader();
    m_eq.AddChatSystemMessage(fmt::format("Raid members ({}):", members.size()));

    uint32_t current_group = 0;
    bool have_group = false;
    for (const auto& m : members) {
        if (!have_group || m.group != current_group) {
            current_group = m.group;
            have_group = true;
            m_eq.AddChatSystemMessage(
                m.group == EQT::kRaidGroupless
                    ? std::string("  Not in a group:")
                    : fmt::format("  Group {}:", m.group + 1));  // 0-based on the wire
        }
        // Class/level are absent when the add arrived without the
        // RaidAddMember tail (a 136-byte general); print the name alone rather
        // than "Level 0 Unknown".
        std::string detail;
        if (m.level != 0 || m.class_id != 0) {
            detail = fmt::format(" (Level {} {})", m.level,
                                 EverQuest::GetClassName(m.class_id));
        }
        std::string tags;
        if (!leader.empty() && m.name == leader) tags += " [raid leader]";
        if (m.is_group_leader) tags += " [group leader]";
        m_eq.AddChatSystemMessage(fmt::format("    {}{}{}", m.name, detail, tags));
    }
}
// ========== Raid leader-only ops (T181) ==========
// Command codes (eqemu common/raid.h): 5 = Disband, 20 = LootType,
// 21 = AddLooter, 22 = RemoveLooter, 30 = MakeLeader.
//
// WHAT THE SERVER DOES AND DOES NOT CHECK -- this is the whole reason these
// commands need client-side guards (Handle_OP_RaidCommand, client_packet.cpp):
//   * MakeLeader IS gated: `if (strcmp(raid->leadername, GetName()) == 0)`,
//     and a non-leader's command falls out of the arm with NO reply at all.
//     Silence is the failure mode, so we pre-check and say why.
//   * Disband is NOT gated. Any raid member can remove any other member. We
//     do not pretend otherwise -- there is nothing to enforce client-side that
//     the server would honour -- but we do confirm what was sent.
//   * LootType is NOT gated and is NOT validated: `parameter` goes straight
//     into Raid::ChangeLootType. A typo'd value would be stored verbatim and
//     match no arm of the switch in attack.cpp, silently disabling looting.
//     So the PARSE is the guard (EQT::ParseRaidLootType).
//
// Membership checks use the T180 roster: naming somebody who is not in the
// raid produces a server no-op, so catching it here turns silence into a
// sentence.

bool EqActionHandler::raidRequireMembership() {
    if (!m_eq.IsInRaid()) {
        m_eq.AddChatSystemMessage("You are not in a raid.");
        return false;
    }
    return true;
}

bool EqActionHandler::raidRequireLeader(const std::string& what) {
    if (!raidRequireMembership()) {
        return false;
    }
    const std::string& leader = m_eq.GetRaidLeader();
    if (leader.empty() || leader != m_eq.GetMyName()) {
        // The server would simply drop this on the floor.
        m_eq.AddChatSystemMessage(fmt::format(
            "Only the raid leader can {}.", what));
        return false;
    }
    return true;
}

// Returns true when `name` is in the roster. The roster is assembled from the
// action-0 stream (T180), so an empty roster means "we have not been told yet",
// which is not the same as "not a member" -- treat it as unknown and let the
// command through rather than refusing on missing information.
bool EqActionHandler::raidMemberKnown(const std::string& name) const {
    const auto& members = m_eq.GetRaidMembers();
    if (members.empty()) {
        return true;
    }
    for (const auto& m : members) {
        if (m.name == name) {
            return true;
        }
    }
    return false;
}

void EqActionHandler::raidRemove(const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid remove <name>");
        return;
    }
    if (!raidRequireMembership()) {
        return;
    }
    if (name == m_eq.GetMyName()) {
        // Identical on the wire to /raid leave; say which command they meant
        // rather than quietly doing something they did not ask for.
        m_eq.AddChatSystemMessage("To leave the raid yourself, use /raid leave.");
        return;
    }
    if (!raidMemberKnown(name)) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }
    // RaidCommandDisband (5): leader_name = the member being removed,
    // player_name = whoever is doing the removing. Same shape as /raid leave,
    // which sends our own name in both.
    m_eq.SendRaidCommand(5, m_eq.GetMyName(), name);
    m_eq.AddChatSystemMessage(fmt::format("You remove {} from the raid.", name));
}

void EqActionHandler::raidMakeLeader(const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid leader <name>");
        return;
    }
    if (!raidRequireLeader("pass raid leadership")) {
        return;
    }
    if (name == m_eq.GetMyName()) {
        m_eq.AddChatSystemMessage("You are already the raid leader.");
        return;
    }
    if (!raidMemberKnown(name)) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }
    // RaidCommandMakeLeader (30): leader_name = the NEW leader.
    m_eq.SendRaidCommand(30, m_eq.GetMyName(), name);
    m_eq.AddChatSystemMessage(fmt::format("You make {} the raid leader.", name));
}

void EqActionHandler::raidLootType(const std::string& mode) {
    if (mode.empty()) {
        m_eq.AddChatSystemMessage(
            "Usage: /raid loot <leader|groupleader|selected|all>");
        return;
    }
    uint32_t loot_type = 0;
    if (!EQT::ParseRaidLootType(mode, loot_type)) {
        // The server would store the bad value verbatim; refuse here instead.
        m_eq.AddChatSystemMessage(fmt::format(
            "'{}' is not a raid loot type. Use leader, groupleader, selected or all.",
            mode));
        return;
    }
    if (!raidRequireLeader("change the raid loot type")) {
        return;
    }
    // RaidCommandLootType (20): parameter = the loot type.
    m_eq.SendRaidCommand(20, m_eq.GetMyName(), m_eq.GetMyName(), loot_type);
    m_eq.AddChatSystemMessage(fmt::format(
        "You set raid looting to {}.", EQT::RaidLootTypeName(loot_type)));
}

void EqActionHandler::raidLooter(bool add, const std::string& name) {
    if (name.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid looter <add|remove> <name>");
        return;
    }
    if (!raidRequireLeader(add ? "add a raid looter" : "remove a raid looter")) {
        return;
    }
    if (!raidMemberKnown(name)) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }
    // RaidCommandAddLooter (21) / RaidCommandRemoveLooter (22):
    // leader_name = the looter. Only meaningful under loot type 3 (Selected).
    m_eq.SendRaidCommand(add ? 21 : 22, m_eq.GetMyName(), name);
    m_eq.AddChatSystemMessage(add
        ? fmt::format("You add {} as a raid looter.", name)
        : fmt::format("You remove {} as a raid looter.", name));
}

// T182: raid lock / unlock. RaidCommandRaidLock (8) / RaidCommandRaidUnlock
// (9) -- the handler uses no fields from the body at all, it looks the raid up
// from the sender, so the names are ours by convention.
//
// The server does NOT gate these on leadership; the gate here is the same
// judgement as /raid loot, which the real Titanium client also offers only to
// the leader. See the doc for the one remaining asymmetry (/raid remove).
//
// Asking for the state it is already in is NOT an error server-side: eqemu
// answers with the same action code, sent to you alone instead of broadcast.
// We still send it -- the tracked state could be stale, and refusing on stale
// state would block a legitimate command -- and let the reply be the truth.
void EqActionHandler::raidLock(bool lock) {
    if (!raidRequireLeader(lock ? "lock the raid" : "unlock the raid")) {
        return;
    }
    m_eq.SendRaidCommand(lock ? 8 : 9, m_eq.GetMyName(), m_eq.GetMyName());
    m_eq.AddChatSystemMessage(lock ? "You lock the raid." : "You unlock the raid.");
}

// ========== Merchant (T136) ==========
// vendorToggle()/buyFromVendor()/sellToVendor() are thin wrappers over the
// three new EverQuest methods (eq_commands.cpp) that push the same
// eqt::events::Vendor*Intent the renderer's vendor-window gesture pushes --
// EqActionHandler has no access to the private m_bridge, so EverQuest keeps
// owning the push, same as every guard/send in this family.

void EqActionHandler::vendorToggle() {
    m_eq.RequestVendorToggle();
}

// Pure state read + print, ported verbatim from eq_commands.cpp's old
// vendorlist_cmd lambda -- see EverQuest::GetVendorItems()/GetVendorName()
// (eq.h, T136) for why this reads via accessor here rather than through a
// new EverQuest method (this unit's plan asked explicitly what "migrate"
// means for a command that sends nothing; see the plan's Review).
void EqActionHandler::vendorList() {
    if (!m_eq.IsVendorWindowOpen()) {
        m_eq.AddChatSystemMessage("You are not at a vendor.");
        return;
    }
    const auto& items = m_eq.GetVendorItems();
    const auto& vendorName = m_eq.GetVendorName();
    if (items.empty()) {
        m_eq.AddChatSystemMessage(fmt::format("{}'s inventory is empty (or still loading).", vendorName));
        return;
    }
    m_eq.AddChatSystemMessage(fmt::format("{}'s inventory ({} item{}):",
        vendorName, items.size(), items.size() == 1 ? "" : "s"));
    for (const auto& [slot, item] : items) {
        std::string qtyStr = (item.quantity < 0) ? "unlimited" : std::to_string(item.quantity);
        m_eq.AddChatSystemMessage(fmt::format("  [slot {}] {} - {} (qty: {})",
            slot, item.name, EverQuest::FormatVendorPrice(item.price), qtyStr));
    }
}

void EqActionHandler::buyFromVendor(uint32_t slot, uint8_t quantity) {
    m_eq.RequestVendorBuy(slot, quantity);
}

void EqActionHandler::sellToVendor(uint32_t slot, uint8_t quantity) {
    m_eq.RequestVendorSell(slot, quantity);
}

// ========== Movement / Utility / Skills (T140) ==========

// Pure state read + print, ported verbatim from eq_commands_movement.cpp's
// old loc_cmd lambda -- EverQuest::GetPosition() (eq.h) was already public
// and already reads the same m_x/m_y/m_z this used directly, so no new
// EverQuest surface was needed here.
void EqActionHandler::showLocation() {
    auto pos = m_eq.GetPosition();
    m_eq.AddChatSystemMessage(fmt::format("Your location is {:.1f}, {:.1f}, {:.1f}", pos.x, pos.y, pos.z));
}

void EqActionHandler::warp(const std::string& args) {
    m_eq.RequestWarp(args);
}

// Ported verbatim from eq_commands_movement.cpp's old zoning_cmd lambda --
// SetZoningEnabled()/IsZoningEnabled() (eq.h) were already public, so no
// new EverQuest surface was needed here (unlike warp/timeOfDay/bankToggle/
// trainWithTarget below, which do).
void EqActionHandler::showOrSetZoning(const std::string& args) {
    std::string trimmed = args;
    while (!trimmed.empty() && trimmed[0] == ' ') {
        trimmed = trimmed.substr(1);
    }
    if (trimmed.empty()) {
        m_eq.AddChatSystemMessage(fmt::format("Zoning is {}", m_eq.IsZoningEnabled() ? "on" : "off"));
        return;
    }
    if (trimmed == "on") {
        m_eq.SetZoningEnabled(true);
        m_eq.AddChatSystemMessage("Zoning is now on");
    } else if (trimmed == "off") {
        m_eq.SetZoningEnabled(false);
        m_eq.AddChatSystemMessage("Zoning is now off");
    } else {
        m_eq.AddChatSystemMessage("Usage: /zoning [on|off]");
    }
}

void EqActionHandler::requestQuit() {
    m_eq.RequestQuit();  // D20c4: Application checks quit flag
}

// Ported verbatim from eq_commands_movement.cpp's old camp_cmd lambda: sit
// down, ack, then start the 30-second camp timer. SetPositionState()/
// StartCampTimer() were already public.
void EqActionHandler::campNow() {
    m_eq.SetPositionState(POS_SITTING);
    m_eq.AddChatSystemMessage("You have begun to camp. You will log out in 30 seconds.");
    m_eq.StartCampTimer();
}

// Ported verbatim from eq_commands_movement.cpp's old debug_cmd lambda --
// EverQuest::SetDebugLevel()/GetDebugLevel() (eq.h) are static and already
// public, so no new EverQuest surface was needed here.
void EqActionHandler::debugLevel(const std::string& args) {
    if (args.empty()) {
        m_eq.AddChatSystemMessage(fmt::format("Debug level: {}", EverQuest::GetDebugLevel()));
    } else {
        try {
            int level = std::stoi(args);
            EverQuest::SetDebugLevel(level);
            m_eq.AddChatSystemMessage(fmt::format("Debug level set to {}", level));
        } catch (...) {
            m_eq.AddChatSystemMessage("Usage: /debug <0-6>");
        }
    }
}

// Ported verbatim from eq_commands_movement.cpp's old perf_cmd lambda --
// EQT::PerformanceMetrics::instance() is a process-global singleton, no
// EverQuest state involved beyond the AddChatSystemMessage/LOG_INFO calls.
void EqActionHandler::perfReport() {
    std::string report = EQT::PerformanceMetrics::instance().generateReport();
    // Output to console
    LOG_INFO(MOD_MAIN, "{}", report);
    // Also show summary in chat
    auto stats = EQT::PerformanceMetrics::instance().getStats("Frame Time");
    if (stats.count > 0) {
        double avgFps = stats.avgMs() > 0 ? 1000.0 / stats.avgMs() : 0;
        m_eq.AddChatSystemMessage(fmt::format("=== Performance Metrics ==="));
        m_eq.AddChatSystemMessage(fmt::format("Startup: {} ms", EQT::PerformanceMetrics::instance().getCategoryTotalMs(EQT::MetricCategory::Startup)));
        m_eq.AddChatSystemMessage(fmt::format("Zoning: {} ms", EQT::PerformanceMetrics::instance().getCategoryTotalMs(EQT::MetricCategory::Zoning)));
        m_eq.AddChatSystemMessage(fmt::format("Avg FPS: {:.1f} (avg frame: {:.1f} ms)", avgFps, stats.avgMs()));
        m_eq.AddChatSystemMessage(fmt::format("Frame time: min {} ms, max {} ms", stats.minMs, stats.maxMs));
        m_eq.AddChatSystemMessage("Full report written to console.");
    } else {
        m_eq.AddChatSystemMessage("No performance data collected yet.");
    }
}

void EqActionHandler::timeOfDay(const std::string& args) {
    m_eq.RequestTimeOfDay(args);
}

void EqActionHandler::bankToggle() {
    m_eq.RequestBankToggle();
}

void EqActionHandler::trainWithTarget() {
    m_eq.RequestTraining();
}

// ========== Social / Find (T150) ==========
// resolveTargetOrCurrent() repeats the targetId==0 fallback assist()/
// shieldTarget()/duel() established at the top of the Combat section: the
// live CombatManager target is read HERE, not from GameState::combat()
// (T28: that copy is never populated in production). Returns 0 and prints
// the same "You don't have a target." message when there is nothing to use.

// T162: see the declaration in eq_action_handler.h for why this exists and
// why it is not a second copy of T159's predicate. SkillManager::hasSkill()
// IS that predicate (isDisplayableSkill() && canClassHaveSkill()), and
// getSkillUseError() supplies the same wording activateSkill() would have
// produced, so a player refused by /track reads the same sentence as a player
// refused by /taunt or /useskill.
//
// Fails OPEN, exactly as T159's gate does: with no SkillManager, or with an
// unloaded cap table (canClassHaveSkill() defaults true), everything is
// permitted and behaviour is what it was before the gate existed. A data
// regression degrades the client to "no gate", never to "no skills".
bool EqActionHandler::skillGateAllows(uint8_t skillId) {
    auto* skills = m_eq.GetSkillManager();
    if (!skills) {
        return true;
    }
    if (skills->hasSkill(skillId)) {
        return true;
    }
    m_eq.AddChatSystemMessage(fmt::format(
        "Cannot use {}: {}", EQ::getSkillName(skillId),
        skills->getSkillUseError(skillId)));
    LOG_DEBUG(MOD_COMBAT, "Skill gate refused skill {} ({})", skillId,
              EQ::getSkillName(skillId));
    return false;
}

uint16_t EqActionHandler::resolveTargetOrCurrent(uint16_t targetId) {
    if (targetId != 0) {
        return targetId;
    }
    auto* combat = getCombatManager();
    if (!combat || !combat->HasTarget()) {
        m_eq.AddChatSystemMessage("You don't have a target.");
        return 0;
    }
    return combat->GetTargetId();
}

void EqActionHandler::assistGroup(uint16_t targetId) {
    uint16_t resolved = resolveTargetOrCurrent(targetId);
    if (resolved == 0) return;
    m_eq.SendAssistGroup(resolved);
}

void EqActionHandler::trackTarget(uint16_t targetId) {
    // T162: /track is the one bypass that CANNOT be routed through
    // activateSkill(). It sends HC_OP_TrackTarget ("make this entity my
    // tracking target"), while skill 53 maps to HC_OP_Track ("send me the
    // track list") -- two different opcodes and two different actions, so
    // activateSkill(53) would not be this command. eqemu class-gates it all
    // the same: Handle_OP_TrackTarget returns immediately on
    // !CanThisClassTrack(), so before this fix a Rogue's /track was
    // discarded in silence exactly like the Rogue's /taunt.
    //
    // Cap data: config/skill_caps.json has a Tracking row for classes 4
    // (Ranger), 6 (Druid) and 8 (Bard) only -- which is precisely eqemu's
    // CanThisClassTrack(). Gating on it refuses nobody who could track.
    if (!skillGateAllows(EQ::kSkillTracking)) return;
    uint16_t resolved = resolveTargetOrCurrent(targetId);
    if (resolved == 0) return;
    m_eq.SendTrackTarget(resolved);
}

void EqActionHandler::findPerson(uint16_t targetId) {
    uint16_t resolved = resolveTargetOrCurrent(targetId);
    if (resolved == 0) return;
    m_eq.SendFindPerson(resolved);
}

void EqActionHandler::friendsWho(const std::string& friendsList) {
    m_eq.SendFriendsWho(friendsList);
}

// ========== GM (T148) ==========
//
// Pure routing to the pre-existing senders (src/client/eq_gm.cpp), the same
// shape as the Guild family (T132): the senders own their own zone-
// connection guard and their own logging, so nothing is duplicated here.
//
// The GM-mode gate is NOT here -- it lives in ActionDispatcher::checkGMMode()
// so a single place owns the refusal string and the tests can drive it
// through RecordingActionHandler without an EverQuest instance.

bool EqActionHandler::isGMMode() const {
    return m_eq.GetGMMode();
}

void EqActionHandler::gmZoneRequest(const std::string& charName, uint16_t zoneId) {
    m_eq.SendGMZoneRequest(charName, zoneId);
}

void EqActionHandler::gmSummon(const std::string& charName) {
    m_eq.SendGMSummon(charName);
}

void EqActionHandler::gmGoto(const std::string& charName) {
    m_eq.SendGMGoto(charName);
}

void EqActionHandler::gmFind(const std::string& charName) {
    m_eq.SendGMFind(charName);
}

void EqActionHandler::gmKick(const std::string& charName) {
    m_eq.SendGMKick(charName);
}

void EqActionHandler::gmKill(const std::string& charName) {
    m_eq.SendGMKill(charName);
}

void EqActionHandler::gmHideMe(bool hide) {
    m_eq.SendGMHideMe(hide);
}

void EqActionHandler::gmEmoteZone(const std::string& text) {
    m_eq.SendGMEmoteZone(text);
}

void EqActionHandler::gmLastName(const std::string& charName, const std::string& lastName) {
    m_eq.SendGMLastName(charName, lastName);
}

} // namespace eqt
