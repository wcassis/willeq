#include "client/eq_action_handler.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/pet_constants.h"
#include "client/spell/spell_manager.h"
#include "client/spell/cast_refusal.h"  // T244: official refusal strings
#include "client/skill/skill_manager.h"      // T156: useSkill() reaches SkillManager
#include "client/skill/skill_constants.h"
#include "client/inventory/inventory_manager.h"  // T230: item-id -> slot hop
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
    // T239: rule moved into CombatManager so the Combat hotbar slot and /aa
    // cannot drift.
    auto* combat = getCombatManager();
    if (combat) {
        combat->ToggleAutoAttack();
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

// T240: castSpell()/castSpellOnTarget() are the action-layer casts (spell-gem
// hotkeys via InputActionBridge::processSpellCasts, ActionDispatcher::
// castSpell). Until T240 they called CombatManager::CastSpell(SpellSlot),
// which read CombatManager::m_spell_gems -- a second gem map that only
// memorizeSpell() below ever wrote; the server's memorised gems (PlayerProfile
// -> SpellManager::setSpellGems) never reached it, so every hotkey cast
// reported success and sent nothing (T235 live AFTER run: three
// reportAction: CastSpell lines, zero BeginCast). Both now go through the
// same EQ::SpellManager path /cast uses (beginCastFromGem() below, T101
// shape), and the CombatManager map/stub are deleted.
void EqActionHandler::castSpell(uint8_t gemSlot) {
    // targetId 0 = SpellManager resolves the current CombatManager target
    // (or self/pet/group per the spell's target type).
    beginCastFromGem(gemSlot, 0);
}

void EqActionHandler::interruptCast() {
    // T101: repointed from CombatManager::InterruptCast() to
    // EQ::SpellManager -- ported from game's /interrupt (eq_commands_
    // spells.cpp), which is the subsystem the real /cast path
    // (beginCastFromGem() below) actually uses. Not covered by any
    // existing test against real CombatManager state, so this repoint was
    // safe -- see refactor/T101_migrate_spells_pet_family.md's Review. T240
    // finished the job for castSpell()/castSpellOnTarget()/memorizeSpell().
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
    // the real EQ::SpellManager casting path. Since T240 castSpell()/
    // castSpellOnTarget() above are thin wrappers over this method.
    auto* spellMgr = m_eq.GetSpellManager();
    if (!spellMgr) {
        m_eq.AddChatSystemMessage("Spell system not initialized");
        return;
    }
    // gemSlot arrives 1-indexed (ActionDispatcher::beginCastFromGem()
    // already validated 1-8); SpellManager's gems are 0-indexed.
    EQ::CastResult result = spellMgr->beginCastFromGem(static_cast<uint8_t>(gemSlot - 1), targetId);
    // T244: refusals that have an official string say WHY (eqstr ids via
    // cast_refusal.h -- "Your target is out of range, get closer!" etc.);
    // before this every result the switch below did not name fell to a
    // bare "Cannot cast spell" (T240 finding 1). T251: rendered by
    // castRefusalMessage() so a missing component is named.
    if (std::string refusal = EQ::castRefusalMessage(m_eq, result); !refusal.empty()) {
        m_eq.AddChatSystemMessage(refusal);
        return;
    }
    switch (result) {
        case EQ::CastResult::Success:
            m_eq.AddChatSystemMessage("Casting from gem " + std::to_string(gemSlot));
            break;
        case EQ::CastResult::NotMemorized:
            m_eq.AddChatSystemMessage("No spell in gem " + std::to_string(gemSlot));
            break;
        case EQ::CastResult::SpellNotReady:
            m_eq.AddChatSystemMessage("Spell not ready");
            break;
        case EQ::CastResult::InvalidSpell:
            m_eq.AddChatSystemMessage("Unknown spell in gem " + std::to_string(gemSlot));
            break;
        default:
            m_eq.AddChatSystemMessage("Cannot cast spell");
            break;
    }
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

// T268: the loot window actions. The dispatcher turns a false return into
// a refusal that names what was asked for (T178 standard: the server's own
// refusals never do); success prints what is being taken. Command-driven
// takes use autoLoot=true (straight to inventory): a cursor item would make
// eqemu refuse the next take (Character:CheckCursorEmptyWhenLooting) and a
// scripted /loot has no way to clear the cursor.
bool EqActionHandler::lootItem(uint16_t corpseId, int16_t slot) {
    if (!m_eq.IsLootWindowOpen()) {
        return false;
    }
    const auto& items = m_eq.GetLootItems();
    auto it = items.find(slot);
    if (it == items.end()) {
        return false;
    }
    if (!m_eq.LootItemFromCorpse(corpseId, slot, true)) {
        return false;
    }
    m_eq.AddChatSystemMessage(fmt::format("Looting {} (slot {})...", it->second.name, slot));
    return true;
}

bool EqActionHandler::lootItemByName(const std::string& name) {
    int16_t slot = m_eq.FindLootSlotByName(name);
    if (slot < 0) {
        return false;
    }
    return lootItem(0, slot);
}

bool EqActionHandler::lootAll(uint16_t corpseId) {
    (void)corpseId;  // the open window; CombatManager holds the corpse id
    if (!m_eq.IsLootWindowOpen()) {
        return false;
    }
    auto* combat = getCombatManager();
    if (!combat || !combat->LootAll()) {
        return false;
    }
    size_t n = m_eq.GetLootItems().size();
    m_eq.AddChatSystemMessage(fmt::format("Looting everything ({} item{})...", n, n == 1 ? "" : "s"));
    return true;
}

bool EqActionHandler::closeLootWindow() {
    if (!m_eq.IsLootWindowOpen()) {
        return false;
    }
    m_eq.CloseLootWindow(0);
    return true;
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

bool EqActionHandler::hasTellToReplyTo() const {
    return !m_eq.GetLastTellSender().empty();
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

// ========== Inventory ==========

void EqActionHandler::useItem(int16_t slot) {
    // T230: was a stub ("Use item not yet implemented") -- the keyboard
    // hotbar Item route now lands here, so it reaches the same click-effect
    // sender the mouse path uses (EverQuest::UseInventoryItem, eq_bridge.cpp).
    m_eq.UseInventoryItem(slot);
}

int16_t EqActionHandler::findItemSlotByItemId(uint32_t itemId) {
    auto* inv = m_eq.GetInventoryManager();
    if (!inv) return eqt::inventory::SLOT_INVALID;
    return inv->findItemSlotByItemId(itemId);
}

void EqActionHandler::activateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text) {
    // T239: the keyboard hotbar Emote/Macro/Combat route lands on the same
    // body the mouse HotbarActivateIntent arm calls (eq_bridge.cpp).
    m_eq.ActivateHotbarCommandSlot(type, text);
}

// T141: /equip's three input forms all arrive as one free-text string --
// same shape as warp()/debugLevel()/timeOfDay() (T140). All parsing/
// legality/execution happens in EverQuest::RequestEquip() ->
// InventoryManager; nothing is duplicated here. See
// refactor/T141_equip_extraction.md.
void EqActionHandler::equip(const std::string& args) {
    m_eq.RequestEquip(args);
}

// T273: /read -- one free-text argument (slot number or item-name
// substring), same shape as equip() above. All resolution, the
// not-a-book refusal, and the ReadItemIntent push live in
// EverQuest::RequestReadItem(); nothing is duplicated here.
void EqActionHandler::readItem(const std::string& args) {
    m_eq.RequestReadItem(args);
}

// T276: same one-line relay -- item resolution, the scroll/class/level/
// full-book refusals and the ScribeSpellIntent push all live in
// EverQuest::RequestScribeItem(); nothing is duplicated here.
void EqActionHandler::scribeItem(const std::string& args) {
    m_eq.RequestScribeItem(args);
}

// ========== Spellbook ==========

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

void EqActionHandler::cancelBuff(const std::string& args) {
    // T277: one body, one resolver. EverQuest::RequestCancelBuff() owns the
    // slot/name lookup because BuffManager is the only place the player's
    // live buffs exist; the buff-bar click reaches the same wire hop
    // (SendBuffCancelRequest) through BuffCancelIntent.
    m_eq.RequestCancelBuff(args);
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
    // T259: during an ACTIVE trade, /accept is the trade window's Accept
    // button (TradeManager::clickAccept -> OP_TradeAcceptClick). That path
    // was reachable only via a bridge intent nothing produced (deleted T264), so
    // no trade could ever be completed from a headless/scripted client.
    if (m_eq.GetGameState().trade().active()) {
        if (trade->isOwnAccepted()) {
            m_eq.AddChatSystemMessage("You have already accepted this trade");
            return;
        }
        trade->clickAccept();
        m_eq.AddChatSystemMessage(fmt::format("You accept the trade with {}",
            EQT::toDisplayName(m_eq.GetGameState().trade().partnerName())));
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
    // T222: TradeManager::rejectTradeRequest() now fires m_on_cancelled()
    // with TradeEndReason::DeclinedLocal; the setOnCancelled lambda in
    // eq_merchant.cpp prints the single "Declined trade request from X."
    // line, resets the projection and publishes TradeCancelled. T103's
    // in-territory workaround (an explicit trade().reset() here) and the
    // duplicate print are gone -- DeclineTrade_ClearsProjectionViaCancelledCallback
    // fails without the TradeManager fix.
    trade->rejectTradeRequest();
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

// T278: unlike the four recipe requests above, neither of these carries an
// object_type on the wire, so neither goes through
// resolveOpenContainerObjectType() -- a combine or a close against a
// container we have no ground-spawn record for is still a legitimate
// request, and refusing it here would strand the player with a window they
// cannot close. The open-container gate they DO need lives in EverQuest
// (RequestTradeskillCombine / RequestCloseTradeskillContainer), which owns
// m_active_tradeskill_object_id and pushes the kept intents.
void EqActionHandler::tradeskillCombine(int16_t containerSlot) {
    m_eq.RequestTradeskillCombine(containerSlot);
}

void EqActionHandler::closeTradeskillContainer() {
    m_eq.RequestCloseTradeskillContainer();
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
//   * Disband is NOT gated server-side: any raid member can remove any other.
//     T181 left it ungated here for that reason; T183 gates it, because the
//     real Titanium client offers kicking only to the leader and that is the
//     standard already applied to /raid loot and /raid lock. The gate is our
//     policy, not the server's -- said plainly rather than implied.
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
        // Checked BEFORE the leadership gate: a non-leader typing their own
        // name wants /raid leave, and telling them "only the leader can do
        // that" would send them the wrong way.
        // Identical on the wire to /raid leave; say which command they meant
        // rather than quietly doing something they did not ask for.
        m_eq.AddChatSystemMessage("To leave the raid yourself, use /raid leave.");
        return;
    }
    // T183: gated on leadership, resolving the asymmetry T182 flagged. The
    // SERVER does not gate this -- RaidCommandDisband lets any raid member
    // remove any other -- but the real Titanium client offers kicking only to
    // the leader, and that is the standard already applied to /raid loot and
    // /raid lock. Being permissive here and strict there was the inconsistency.
    if (!raidRequireLeader("remove a raid member")) {
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

// T184: raid groups. RaidCommandMoveGroup (6) -- leader_name = the member
// being moved, parameter = the target WIRE group (0..11), or anything >= 12
// for the ungrouped bank.
//
// THE SERVER ANSWERS BOTH FAILURE CASES WITH SILENCE, and the roster is what
// lets us do better (Handle_OP_RaidCommand, zone/client_packet.cpp:12804):
//   * `if (group_count < 6)` -- moving into a group that already holds six
//     falls out of the arm with no reply at all.
//   * `if (parameter == old_group) break;` -- an explicit no-op, also silent.
// Both are cheap to detect here because T180's roster carries every member's
// group, so the client says which it was instead of appearing to hang.
//
// eqemu does not validate `parameter` either: anything outside 0..11 is read
// as "ungrouped", so a user typing 0 while thinking in wire numbers would
// silently be moved OUT of their group. ParseRaidGroup rejects it.
void EqActionHandler::raidGroup(const std::string& name, const std::string& group_text) {
    if (name.empty() || group_text.empty()) {
        m_eq.AddChatSystemMessage("Usage: /raid group <name> <1-12|none>");
        return;
    }
    uint32_t wire_group = 0;
    if (!EQT::ParseRaidGroup(group_text, wire_group)) {
        m_eq.AddChatSystemMessage(fmt::format(
            "'{}' is not a raid group. Use 1-{} or none.", group_text, EQT::kMaxRaidGroups));
        return;
    }
    if (!raidRequireLeader("assign raid groups")) {
        return;
    }

    const auto& members = m_eq.GetRaidMembers();
    const EQT::RaidMemberInfo* target = nullptr;
    for (const auto& m : members) {
        if (m.name == name) {
            target = &m;
            break;
        }
    }
    if (!members.empty() && !target) {
        m_eq.AddChatSystemMessage(fmt::format("{} is not in your raid.", name));
        return;
    }

    if (target && target->group == wire_group) {
        // eqemu breaks out of the arm without a word.
        m_eq.AddChatSystemMessage(fmt::format(
            "{} is already in {}.", name, EQT::RaidGroupName(wire_group)));
        return;
    }
    if (wire_group != EQT::kRaidGroupless) {
        // Counted from the roster, which is only as current as the last
        // update we received -- so this can be wrong in the safe direction
        // (we refuse something that would have worked). It cannot be wrong in
        // the dangerous direction: if the roster says there is room and there
        // is not, the server does exactly what it did before, nothing.
        const size_t occupied = countRaidGroup(wire_group);
        if (occupied >= EQT::kMaxRaidGroupSize) {
            m_eq.AddChatSystemMessage(fmt::format(
                "{} is full ({} of {}).", EQT::RaidGroupName(wire_group),
                occupied, EQT::kMaxRaidGroupSize));
            return;
        }
    }

    m_eq.SendRaidCommand(6, m_eq.GetMyName(), name, wire_group);
    m_eq.AddChatSystemMessage(
        wire_group == EQT::kRaidGroupless
            ? fmt::format("You move {} out of their group.", name)
            : fmt::format("You move {} to {}.", name, EQT::RaidGroupName(wire_group)));
}

size_t EqActionHandler::countRaidGroup(uint32_t wire_group) const {
    size_t n = 0;
    for (const auto& m : m_eq.GetRaidMembers()) {
        if (m.group == wire_group) {
            ++n;
        }
    }
    return n;
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

void EqActionHandler::trainSkill(const std::string& args) {
    m_eq.RequestTrainSkill(args);
}

void EqActionHandler::closeTrainer() {
    m_eq.RequestCloseTrainer();
}

void EqActionHandler::bankCoinMove(uint8_t coinType, int32_t amount, bool fromBank) {
    m_eq.RequestBankCoinMove(coinType, amount, fromBank);
}

void EqActionHandler::bankCoinConvert(uint8_t fromCoinType, uint8_t toCoinType, int32_t amount) {
    m_eq.RequestBankCoinConvert(fromCoinType, toCoinType, amount);
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
