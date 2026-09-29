// eq_action_handler.cpp -- EqActionHandler: the core IActionHandler domains.
// Movement, Combat, Combat (T102), Interaction, Chat, Group, Character
// State and Inventory. The spell/trade/pet/tradeskill domains live in
// eq_action_handler_spells_trade.cpp; the post-T102 extended families in
// eq_action_handler_extended.cpp. Split by refactor/J12_eq_action_handler_split.md.
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
    // handler in eq.cpp when TargetIntent is processed.
    // T286: this used to write EverQuest's own copy of the target id -- a
    // third store next to CombatManager's and CombatState's, and one that
    // was written even when the CombatManager::SetTarget just above it had
    // FAILED. The copy is gone; CombatManager's SetTarget/ClearTarget/
    // CycleTargets are the writers, into CombatState, and only on success.
    (void)spawnId;
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
    // T90: EverQuest::SetHeading() stores its argument raw into the player heading (degrees),
    // which every other site (PlayerProfile zone-in, eq_movement.cpp's
    // outbound server_heading = 90 - the client heading, the self ClientUpdate echo)
    // treats as plain 0-360 degrees -- confirmed by reading SetHeading()
    // itself (eq.cpp) rather than assuming. This function used to also
    // convert degrees to EQ's 0-512 unit range before calling it, double-
    // converting (e.g. setHeading(90) produced a stored heading of 128). Pass
    // degrees straight through; only normalize into [0, 360).
    float normalizedHeading = heading;
    while (normalizedHeading < 0.0f) normalizedHeading += 360.0f;
    while (normalizedHeading >= 360.0f) normalizedHeading -= 360.0f;
    m_eq.SetHeading(normalizedHeading);
    m_eq.Movement().SendPositionUpdate();
    // B911 (GitLab #230): and the renderer. SetHeading() writes the one store
    // and fires PlayerMoved on the STATE layer's EventBus only, which never
    // reaches the renderer, so `/turn 90` turned the server (live `#loc`:
    // heading 0) and left the rendered player and camera at the old heading.
    // Same renderer-bridge PlayerMoved the other game-side movers publish.
    m_eq.Movement().PublishSelfPoseToRenderer();
}

void EqActionHandler::jump() {
    m_eq.Movement().Jump();
}

void EqActionHandler::sit() {
    m_eq.Movement().SetPositionState(POS_SITTING);
}

void EqActionHandler::stand() {
    m_eq.Movement().SetPositionState(POS_STANDING);
}

// T343 (B002): autorun is NOT implemented here and deliberately so. The flag
// it toggles is PlayerMovementState::autorun, which lives in the renderer's
// PlayerCameraComponent and is consumed by the per-frame movement phase; the
// graphical client's hotkeys now reach it directly through
// RendererAction::ToggleAutorun (renderer_frame_input.cpp), and a game-thread
// writer could not reach it at all -- that is what T316 hit and what T334
// recorded for the identical swim case. This arm survives only for the console
// input handler's /autorun, and console mode has no continuous-movement path
// to autorun (T343 deleted the last one, board B032), so it stays a no-op with
// an honest reason instead of a "not yet implemented" that invites a rewrite.
void EqActionHandler::toggleAutorun() {
    LOG_DEBUG(MOD_INPUT, "Autorun is renderer-owned (see the note above); console mode has no runner to toggle");
}

void EqActionHandler::stopAllMovement() {
    // T343 (B032): was four StopMove*/StopTurn* calls whose flags no writer
    // could set, and whose comment claimed StopMovement() was private -- it is
    // public, and it is what /stop actually needs: it clears the active path
    // AND the movement target, so a MoveTo/Follow in flight really stops
    // instead of being resumed by the next UpdateMovement() tick.
    m_eq.Movement().StopMovement();
}

// T88: FRAME CONTRACT -- x, y, z are CLIENT frame (same as EverQuest::Move(),
// which this forwards to verbatim -- see that function's comment in
// eq_movement.cpp for the full swap relationship to server/wire frame).
// Callers reading coordinates off another client's own wire self-report
// (e.g. a join spec captured from a live session) are reading SERVER frame
// and must swap x/y before calling this.
void EqActionHandler::moveToLocation(float x, float y, float z) {
    m_eq.Movement().Move(x, y, z);
}

// T356 (B003c): spawn id, not name -- see IActionHandler's comment.
void EqActionHandler::moveToEntity(uint16_t spawnId) {
    m_eq.Movement().MoveToEntity(spawnId);
}

void EqActionHandler::followEntity(uint16_t spawnId) {
    m_eq.Movement().Follow(spawnId);
}

void EqActionHandler::stopFollow() {
    m_eq.Movement().StopFollow();
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
    const GroupMember* member = m_eq.Group().GetGroupMember(index);
    if (member && member->spawnId != 0) {
        auto* combat = getCombatManager();
        if (combat) {
            combat->SetTarget(member->spawnId);
            updateRendererTargetInfo(member->spawnId);
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
    m_eq.Player().SendAssist(resolved);
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
    m_eq.Player().SendShielding(resolved);
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
    m_eq.Player().SendDuelRequest(resolved);
}

// duelAccept()/duelDecline(): EverQuest's own pending-duel state
// (HasPendingDuel()/GetPendingDuelInitiatorId(), both public) is read
// directly -- GameState has no duel domain to project it into, and unlike
// combat targeting there is no CombatManager-equivalent "authoritative
// live source" to delegate to instead.
void EqActionHandler::duelAccept() {
    if (!m_eq.Player().HasPendingDuel()) {
        m_eq.AddChatSystemMessage("You have no pending duel challenge.");
        return;
    }
    m_eq.Player().SendDuelAccept(m_eq.Player().GetPendingDuelInitiatorId());
}

void EqActionHandler::duelDecline() {
    if (!m_eq.Player().HasPendingDuel()) {
        m_eq.AddChatSystemMessage("You have no pending duel challenge.");
        return;
    }
    m_eq.Player().SendDuelDecline(m_eq.Player().GetPendingDuelInitiatorId());
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
    if (std::string refusal = EQ::castRefusalMessage(m_eq.Context(), result); !refusal.empty()) {
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
    m_eq.Chat().SendChatMessage("Hail", "say");
}

void EqActionHandler::hailTarget() {
    auto* combat = getCombatManager();
    if (combat && combat->HasTarget()) {
        uint16_t targetId = combat->GetTargetId();
        const auto& entities = m_eq.GetGameState().entities();
        auto* entity = entities.getEntity(targetId);
        if (entity && !entity->name.empty()) {
            m_eq.Chat().SendChatMessage("Hail, " + EQT::toDisplayName(entity->name), "say");
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
        m_eq.Entities().SendClickDoor(nearestDoor->doorId);
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
    const auto& items = m_eq.Bridge().GetLootItems();
    auto it = items.find(slot);
    if (it == items.end()) {
        return false;
    }
    if (!m_eq.Bridge().LootItemFromCorpse(corpseId, slot, true)) {
        return false;
    }
    m_eq.AddChatSystemMessage(fmt::format("Looting {} (slot {})...", it->second.name, slot));
    return true;
}

bool EqActionHandler::lootItemByName(const std::string& name) {
    int16_t slot = m_eq.Bridge().FindLootSlotByName(name);
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
    size_t n = m_eq.Bridge().GetLootItems().size();
    m_eq.AddChatSystemMessage(fmt::format("Looting everything ({} item{})...", n, n == 1 ? "" : "s"));
    return true;
}

bool EqActionHandler::closeLootWindow() {
    if (!m_eq.IsLootWindowOpen()) {
        return false;
    }
    m_eq.Bridge().CloseLootWindow(0);
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
    m_eq.Chat().SendChatMessage(message, channelToString(channel));
}

void EqActionHandler::sendTell(const std::string& target, const std::string& message) {
    m_eq.Chat().SendChatMessage(message, "tell", target);
}

void EqActionHandler::replyToLastTell(const std::string& message) {
    const std::string& lastSender = m_eq.GetLastTellSender();
    if (lastSender.empty()) {
        LOG_DEBUG(MOD_INPUT, "Reply to last tell: No one to reply to");
        return;
    }
    m_eq.Chat().SendChatMessage(message, "tell", lastSender);
}

bool EqActionHandler::hasTellToReplyTo() const {
    return !m_eq.GetLastTellSender().empty();
}

std::string EqActionHandler::getLastTellSender() const {
    return m_eq.GetLastTellSender();
}

// ========== Group ==========

void EqActionHandler::inviteToGroup(const std::string& playerName) {
    m_eq.Group().SendGroupInvite(playerName);
}

void EqActionHandler::inviteTarget() {
    auto* combat = getCombatManager();
    if (combat && combat->HasTarget()) {
        uint16_t targetId = combat->GetTargetId();
        const auto& entities = m_eq.GetGameState().entities();
        auto* entity = entities.getEntity(targetId);
        if (entity && !entity->name.empty()) {
            m_eq.Group().SendGroupInvite(entity->name);
        }
    }
}

void EqActionHandler::acceptGroupInvite() {
    m_eq.Group().AcceptGroupInvite();
}

void EqActionHandler::declineGroupInvite() {
    // Get pending inviter and decline
    const auto& groupState = m_eq.GetGameState().group();
    if (groupState.hasPendingInvite()) {
        m_eq.Group().SendGroupDecline(groupState.pendingInviterName());
    }
}

void EqActionHandler::leaveGroup() {
    m_eq.Group().SendLeaveGroup();
}

// ========== Character State ==========

void EqActionHandler::setAFK(bool afk) {
    m_eq.Player().SetAFK(afk);
}

void EqActionHandler::setAnonymous(bool anon) {
    m_eq.Player().SetAnonymous(anon);
}

// ========== Inventory ==========

void EqActionHandler::useItem(int16_t slot) {
    // T230: was a stub ("Use item not yet implemented") -- the keyboard
    // hotbar Item route now lands here, so it reaches the same click-effect
    // sender the mouse path uses (EverQuest::UseInventoryItem, eq_bridge.cpp).
    m_eq.Bridge().UseInventoryItem(slot);
}

int16_t EqActionHandler::findItemSlotByItemId(uint32_t itemId) {
    auto* inv = m_eq.GetInventoryManager();
    if (!inv) return eqt::inventory::SLOT_INVALID;
    return inv->findItemSlotByItemId(itemId);
}

void EqActionHandler::activateHotbarCommandSlot(eqt::HotbarSlotType type, const std::string& text) {
    // T239: the keyboard hotbar Emote/Macro/Combat route lands on the same
    // body the mouse HotbarActivateIntent arm calls (eq_bridge.cpp).
    m_eq.Bridge().ActivateHotbarCommandSlot(type, text);
}

// T141: /equip's three input forms all arrive as one free-text string --
// same shape as warp()/debugLevel()/timeOfDay() (T140). All parsing/
// legality/execution happens in EverQuest::RequestEquip() ->
// InventoryManager; nothing is duplicated here. See
// refactor/T141_equip_extraction.md.
void EqActionHandler::equip(const std::string& args) {
    m_eq.Commands().RequestEquip(args);
}

// T273: /read -- one free-text argument (slot number or item-name
// substring), same shape as equip() above. All resolution, the
// not-a-book refusal, and the ReadItemIntent push live in
// EverQuest::RequestReadItem(); nothing is duplicated here.
void EqActionHandler::readItem(const std::string& args) {
    m_eq.Commands().RequestReadItem(args);
}

// T276: same one-line relay -- item resolution, the scroll/class/level/
// full-book refusals and the ScribeSpellIntent push all live in
// EverQuest::RequestScribeItem(); nothing is duplicated here.
void EqActionHandler::scribeItem(const std::string& args) {
    m_eq.Commands().RequestScribeItem(args);
}

} // namespace eqt
