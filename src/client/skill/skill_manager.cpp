/*
 * WillEQ Skill Manager Implementation
 */

#include "client/skill/skill_manager.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/display_eqstr.h"
#include "client/skill/skill_cap_table.h"
#include "client/skill/skill_packet_bodies.h"
#include "client/skill/behind_target.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "client/state/game_state.h"
#include "common/logging.h"
#include "common/net/packet.h"

namespace EQ {

using namespace eqt::inventory;  // slot ranges (EQUIPMENT_*, GENERAL_*), B894

// ============================================================================
// Constructor / Destructor
// ============================================================================

// B894 (GitLab #210): the two ItemType values eqemu's Client::CanFish /
// Client::ForageItem branch on (common/item_data.h's ItemType enum,
// ItemType1HSlash = 0): ItemTypeFishingPole is 36 and ItemTypeFishingBait is
// 37. ItemInstance::skillType carries the full ItemType, parsed from
// serialized item field 56 (titanium.cpp writes item->ItemType) -- the same
// field findPoisonInventorySlot() already reads for kItemTypePoison (42).
constexpr uint8_t kItemTypeFishingPole = 36;
constexpr uint8_t kItemTypeFishingBait = 37;

// The eqstr ids B894's precondition table names (eqstr_us.txt, verified
// against data/willeq.db's eqstrings by tests/test_forage_fishing.cpp).
namespace {
constexpr uint32_t kEqstrForageAttacking  = 148;
constexpr uint32_t kEqstrForageStanding   = 149;
constexpr uint32_t kEqstrAlreadyFishing   = 156;
constexpr uint32_t kEqstrFishingAttacking = 159;
constexpr uint32_t kEqstrNoPole           = 160;
constexpr uint32_t kEqstrPolePrimary      = 161;
constexpr uint32_t kEqstrNoBait           = 162;
constexpr uint32_t kEqstrStopFishing      = 165;
}  // namespace

SkillManager::SkillManager(EverQuest* eq)
    : m_eq(eq)
{
    // Initialize all skills with default data
    for (uint8_t i = 0; i < EQT::MAX_PP_SKILL; ++i) {
        initializeSkillData(m_skills[i], i);
    }

    // B894: moving or leaving Standing stops a live cast. The official client
    // ends fishing the moment the player moves (eqstr 165); this client's
    // movement integrator (movement_update.cpp) publishes PlayerMoved onto
    // the game-state bus, and every position-state write publishes
    // PlayerPositionStateChanged, so the gate listens there rather than
    // reaching into the movement component.
    if (m_eq) {
        auto self = this;  // a non-const reference: the listener arms call
        // non-const members (stopFishingOnMove / stopFishing), so a
        // `const auto&` capture would fail at the call.
        m_fishing_stop_handles.push_back(
            m_eq->GetGameState().events().subscribe(
                [self](const eqt::state::GameEvent& ev) {
                    if (ev.type == eqt::state::GameEventType::PlayerMoved) {
                        self->stopFishingOnMove(
                            std::get<eqt::state::PlayerMovedData>(ev.data).isMoving);
                    } else if (ev.type ==
                               eqt::state::GameEventType::PlayerPositionStateChanged) {
                        self->stopFishingOnPositionState(
                            std::get<eqt::state::PlayerPositionStateChangedData>(ev.data)
                                .state);
                    }
                }));
    }
}

SkillManager::~SkillManager() {
    // Unsubscribe before m_eq is destroyed; the bus outlives the manager.
    if (m_eq) {
        for (auto h : m_fishing_stop_handles) {
            m_eq->GetGameState().events().unsubscribe(h);
        }
    }
}

// ----------------------------------------------------------------------------
// B894: the preconditions the official client checks before OP_Forage (148/
// 149) and OP_Fishing (156/159/160/161/162), in the order the shipped eqstr
// ids run. Returns "" when the send may go out; the SHIPPED eqstr line
// otherwise. The strings come from the one eqstr fetch (eqt::EqStrLine, over
// the StringDb service) with a literal fallback only so a table that never
// loaded degrades to the oracle's own wording rather than to silence --
// tests fail on the fallback, so the shipped row is what the player sees.
//
// The ATTACKING witness is the client's own in-combat signal (the one
// checkCombatRequirement uses since T317/T325): auto-attack or auto-fire on,
// COMBAT_STATE_ENGAGED, or the selected target recently hit us.
//
// eqemu repeats a subset of these on the wire (forage.cpp CanFish: pole in
// slotPrimary + pole type 36, bait type 37 somewhere worn/personal, water
// check), so this gate is the local refusal the official client gives first,
// not a replacement for the server.
// ----------------------------------------------------------------------------
bool SkillManager::isAttacking() const {
    auto* combat = m_eq ? m_eq->GetCombatManager() : nullptr;
    if (!combat) {
        return false;
    }
    if (combat->IsAutoAttackEnabled() || combat->IsAutoFireEnabled()) {
        return true;
    }
    if (combat->GetCombatState() == COMBAT_STATE_ENGAGED) {
        return true;
    }
    return combat->HasTarget() && combat->IsRecentAttacker(combat->GetTargetId());
}

std::string SkillManager::forageOrFishingRefusal(uint8_t skill_id) const {
    // The strings service over EverQuest's own m_string_db (K14); the one
    // eqstr fetch, "" on a table miss.
    auto fetch = [this](uint32_t id, const char* fallback) -> std::string {
        auto* eq = m_eq;
        if (!eq) {
            return fallback;
        }
        const std::string line =
            eqt::EqStrLine(eq->Strings().Db(), id);
        return line.empty() ? std::string(fallback) : line;
    };

    switch (skill_id) {
    case 27:  // Forage
        if (isAttacking()) {
            return fetch(kEqstrForageAttacking, "You can't try to forage while attacking.");
        }
        if (!m_eq ||
            m_eq->GetGameState().player().positionState() !=
                eqt::state::PositionState::Standing) {
            return fetch(kEqstrForageStanding, "You must be standing to forage.");
        }
        return "";
    case 55:  // Fishing
    {
        if (m_is_fishing) {
            return fetch(kEqstrAlreadyFishing, "You are already fishing!");
        }
        if (isAttacking()) {
            return fetch(kEqstrFishingAttacking, "You can't start fishing while attacking!");
        }
        auto* inv = m_eq ? m_eq->GetInventoryManager() : nullptr;
        if (!inv) {
            return fetch(kEqstrNoPole, "You can't fish without a fishing pole, go buy one.");
        }
        const auto* pole = inv->getItem(SLOT_PRIMARY);
        if (!pole || pole->itemId == 0 ||
            pole->skillType != kItemTypeFishingPole) {
            // eqemu's CanFish wording: a pole SOMEWHERE gives 161 (equip it),
            // none at all gives 160 (buy one). Same HasItemByUse scan shape,
            // over the worn + general ranges this model tracks.
            auto hasPoleAnywhere = [](const eqt::inventory::ItemInstance* i) {
                return i && i->itemId != 0 && i->skillType == kItemTypeFishingPole;
            };
            bool anywhere = false;
            for (int16_t slot = EQUIPMENT_BEGIN;
                 slot <= EQUIPMENT_END && !anywhere; ++slot) {
                anywhere = hasPoleAnywhere(inv->getItem(slot));
            }
            for (int16_t slot = GENERAL_BEGIN;
                 slot <= GENERAL_END && !anywhere; ++slot) {
                anywhere = hasPoleAnywhere(inv->getItem(slot));
            }
            return anywhere
                ? fetch(kEqstrPolePrimary, "You need to put your fishing pole in your primary hand.")
                : fetch(kEqstrNoPole, "You can't fish without a fishing pole, go buy one.");
        }
        auto hasBait = [](const eqt::inventory::ItemInstance* i) {
            return i && i->itemId != 0 && i->skillType == kItemTypeFishingBait;
        };
        bool bait = false;
        for (int16_t slot = EQUIPMENT_BEGIN; slot <= EQUIPMENT_END && !bait; ++slot) {
            bait = hasBait(inv->getItem(slot));
        }
        for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END && !bait; ++slot) {
            bait = hasBait(inv->getItem(slot));
        }
        if (!bait) {
            return fetch(kEqstrNoBait, "You can't fish without fishing bait, go buy some.");
        }
        return "";
    }
    default:
        return "";
    }
}

// A PlayerMoved arrives with isMoving true while the integrator is walking
// the player and false on discrete relocations.
void SkillManager::stopFishingOnMove(bool is_moving) {
    if (!m_is_fishing || !m_eq) {
        return;
    }
    if (!is_moving) {
        return;  // a discrete relocation, not a walk
    }
    stopFishing();
}

void SkillManager::stopFishingOnPositionState(uint8_t state) {
    // Sitting, crouching or dying all end a cast; Standing and FeignDeath do
    // not (feign death is its own state the official client leaves a fisher
    // in). Dead ends it -- there is no fisher left.
    if (!m_is_fishing ||
        state == static_cast<uint8_t>(eqt::state::PositionState::Standing) ||
        state == static_cast<uint8_t>(eqt::state::PositionState::FeignDeath)) {
        return;
    }
    stopFishing();
}

void SkillManager::stopFishing() {
    m_is_fishing = false;
    if (m_eq) {
        // The one eqstr fetch, 165's line, with the oracle's own wording as
        // the fallback (tests fail on the fallback).
        const std::string line = eqt::EqStrLine(m_eq->Strings().Db(), kEqstrStopFishing);
        m_eq->AddChatSystemMessage(
            line.empty() ? std::string("You stop fishing and go on your way.") : line);
    }
    LOG_DEBUG(MOD_MAIN, "B894: fishing ended (eqstr 165)");
}

// ============================================================================
// Initialization
// ============================================================================

void SkillManager::initialize(uint8_t player_class, uint8_t player_race, uint8_t player_level) {
    m_player_class = player_class;
    // B344 deleted m_player_race (write-only); player_race stays in the log line.
    m_player_level = player_level;

    loadClassSkillCaps(player_class, player_level);

    LOG_DEBUG(MOD_MAIN, "SkillManager initialized: class={} race={} level={}",
              player_class, player_race, player_level);
}

// T159: this used to be a stub that zeroed every cap and then invented eight of
// them (player_level * 5 for Defense/Dodge/Offense/BindWound, flat 200 for
// Swimming/SenseHeading/AlcoholTolerance/Begging) FOR EVERY CLASS ALIKE. It now
// loads the real per-class table exported from the server's peq.skill_caps.
//
// `player_level` is deliberately unused for the possession flag: presence of a
// (class, skill) row at ANY level is what decides, never the level threshold.
// See skill_cap_table.h for the Apply Poison evidence behind that.
void SkillManager::loadClassSkillCaps(uint8_t player_class, uint8_t player_level) {
    auto& table = SkillCapTable::instance();
    table.ensureLoaded();

    if (!table.isLoaded()) {
        // Permissive fallback. SkillCapTable::load() has already logged loudly
        // why; do NOT block every skill on a data-file regression.
        for (auto& skill : m_skills) {
            skill.class_can_have = true;
            skill.class_first_level = 1;
            // Leave max_value alone: without a table the only cap information
            // this client has is the echo updateSkill()/updateAllSkills() keep.
        }
        LOG_WARN(MOD_MAIN,
                 "Skill caps unavailable ('{}') -- class {} runs WITHOUT a skill "
                 "possession gate (pre-T159 behaviour)",
                 table.getPath(), player_class);
        return;
    }

    size_t owned = 0;
    for (uint8_t i = 0; i < EQT::MAX_PP_SKILL; ++i) {
        const auto* entry = table.getEntry(player_class, i);
        if (entry) {
            m_skills[i].class_can_have = true;
            m_skills[i].class_first_level = entry->first_level;
            m_skills[i].max_value = entry->max_cap;
            ++owned;
        } else {
            m_skills[i].class_can_have = false;
            m_skills[i].class_first_level = 0;
            m_skills[i].max_value = 0;
        }
    }

    LOG_DEBUG(MOD_MAIN, "Loaded skill caps for class {} level {}: {} skills this class can ever have",
              player_class, player_level, owned);
}

void SkillManager::setPlayerLevel(uint8_t level) {
    if (level != m_player_level) {
        m_player_level = level;
        loadClassSkillCaps(m_player_class, level);
    }
}

// ============================================================================
// Skill Value Management
// ============================================================================

void SkillManager::updateSkill(uint8_t skill_id, uint32_t value) {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        LOG_WARN(MOD_MAIN, "Invalid skill_id {} in updateSkill", skill_id);
        return;
    }

    uint32_t old_value = m_skills[skill_id].current_value;
    m_skills[skill_id].current_value = value;

    // T159: the old unconditional `max_value = current_value` patch-up is gone
    // when a real cap table is loaded -- it made the cap part invention (the
    // stub's eight guesses) and part echo of whatever the server last sent,
    // which is not a cap at all. It survives ONLY as the permissive fallback
    // for a missing/unparseable config/skill_caps.json.
    if (value > 0 && m_skills[skill_id].max_value == 0 &&
        !SkillCapTable::instance().isLoaded()) {
        m_skills[skill_id].max_value = value;  // At minimum, max is current
    }

    // Notify listeners if value changed
    if (old_value != value && m_on_skill_update) {
        m_on_skill_update(skill_id, old_value, value);
    }

    LOG_DEBUG(MOD_MAIN, "Skill {} ({}) updated: {} -> {}",
              skill_id, m_skills[skill_id].name, old_value, value);
}

void SkillManager::updateAllSkills(const uint32_t* skills, size_t count) {
    size_t max_count = std::min(count, static_cast<size_t>(EQT::MAX_PP_SKILL));

    for (size_t i = 0; i < max_count; ++i) {
        m_skills[i].current_value = skills[i];

        // T159: same patch-up, same reason, same fallback-only condition as
        // updateSkill() above.
        if (skills[i] > 0 && m_skills[i].max_value == 0 &&
            !SkillCapTable::instance().isLoaded()) {
            m_skills[i].max_value = skills[i];
        }
        // Note: Don't trigger callback here - this is bulk initial loading,
        // not individual skill-up events. Use updateSkill() for skill-ups.
    }

    LOG_DEBUG(MOD_MAIN, "Updated {} skills from profile", max_count);
}

uint32_t SkillManager::getSkillValue(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return 0;
    }
    return m_skills[skill_id].current_value;
}

uint32_t SkillManager::getSkillMax(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return 0;
    }
    return m_skills[skill_id].max_value;
}

// ============================================================================
// Skill Queries
// ============================================================================

// T159: this is the POSSESSION query its declaration always claimed to be
// ("Check if player has a specific skill"). Before T159 it forwarded to the
// display predicate and was therefore true for every skill 0-74 on every
// class. Display listings use SkillData::isDisplayableSkill() instead.
bool SkillManager::hasSkill(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }
    return m_skills[skill_id].isDisplayableSkill() &&
           m_skills[skill_id].canClassHaveSkill();
}

bool SkillManager::canUseSkill(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }

    const SkillData& skill = m_skills[skill_id];

    // Must be a real skill at all
    if (!skill.isDisplayableSkill()) {
        return false;
    }

    // T159: THE POSSESSION GATE. Can this class ever have this skill? Presence
    // in config/skill_caps.json, not the cap at the current level -- see
    // skill_cap_table.h. Without this, activateSkill had no possession gate
    // whatsoever and happily sent packets the server binned in silence.
    if (!skill.canClassHaveSkill()) {
        return false;
    }

    // Must be activatable
    if (!skill.is_activatable) {
        return false;
    }

    // Check cooldown
    if (skill.isOnCooldown()) {
        return false;
    }

    // B894: the Forage/Fishing preconditions (attacking, standing, pole,
    // primary, bait, already-fishing). Refuses with the SHIPPED eqstr line
    // through getSkillUseError's paired branch; a refusal here sends nothing.
    if (!forageOrFishingRefusal(skill_id).empty()) {
        return false;
    }

    // Check combat requirement
    if (!checkCombatRequirement(skill_id)) {
        return false;
    }

    // Check target requirement
    uint16_t target_id = 0;
    if (m_eq && m_eq->GetCombatManager()) {
        target_id = m_eq->GetCombatManager()->GetTargetId();
    }
    if (!checkTargetRequirement(skill_id, target_id)) {
        return false;
    }

    // T317: checkPositionRequirement() had NO caller -- canUseSkill() never
    // consulted it. Implementing it without wiring it here would have left
    // Backstab's facing rule as unreachable as it was before.
    if (!checkPositionRequirement(skill_id, target_id)) {
        return false;
    }

    return true;
}

std::string SkillManager::getSkillUseError(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return "Invalid skill";
    }

    const SkillData& skill = m_skills[skill_id];

    if (!skill.isDisplayableSkill()) {
        return "You do not have this skill";
    }

    // T159: previously unreachable, because the predicate above was the display
    // test. Say why, not just that -- "Rogues cannot use Taunt" is true, and
    // "you have not trained it yet" would not be.
    if (!skill.canClassHaveSkill()) {
        return "Your class cannot use " + skill.name;
    }

    if (!skill.is_activatable) {
        return "This skill cannot be activated";
    }

    if (skill.isOnCooldown()) {
        uint32_t remaining = skill.getCooldownRemaining();
        return "Skill is recharging (" + std::to_string(remaining / 1000) + "s)";
    }

    uint16_t target_id = 0;
    if (m_eq && m_eq->GetCombatManager()) {
        target_id = m_eq->GetCombatManager()->GetTargetId();
    }

    if (skill.requires_target && target_id == 0) {
        return "You must have a target";
    }

    // B894: paired with canUseSkill's forageOrFishingRefusal() branch. The
    // refusal text IS the check's text -- the same eqstr line the gate
    // computed -- so the player never hears a reason the gate didn't act on.
    // Forage/Fishing only; every other skill gets "".
    if (skill_id == 27 || skill_id == 55) {
        const std::string refusal = forageOrFishingRefusal(skill_id);
        if (!refusal.empty()) {
            return refusal;
        }
    }

    // T317: this branch was empty, so canUseSkill() could refuse on the combat
    // gate while getSkillUseError() returned "" -- activateSkill() then fired
    // its failure callback with a blank reason.
    //
    // T325: no SHIPPED skill sets requires_combat any more (Will's ruling --
    // see skill_data.h's initializeSkillData()), so this message is dormant in
    // production, the way board item 35's "Not enough endurance" also was --
    // though that one is now DELETED, not dormant: T353 measured that skill
    // activation has no resource cost on either side of the wire. It is
    // NOT the T159 defect class: T159's message LIED, blaming facing for an
    // endurance failure. This one still says exactly what refused, and it can
    // only be reached when the predicate really did refuse -- it is left paired
    // with its check rather than deleted, so the mechanism stays whole. The
    // pairing is exercised by test_skill_gates' MechanismIsIntact tests.
    if (skill.requires_combat && !checkCombatRequirement(skill_id)) {
        return "You must be in combat";
    }

    // T159 (T158 defect 3) removed a message that lied: this used to
    // `return "You must be behind your target"` for ANY requires_behind skill,
    // unconditionally and ahead of the resource check, while
    // checkPositionRequirement() returned true unconditionally -- so a
    // Backstab that failed for lack of endurance blamed the player's facing,
    // and canUseSkill() could never actually fail for facing at all. T159 left
    // a standing instruction: when facing is really implemented, the check and
    // the message go back in TOGETHER.
    //
    // T317 honours it. checkPositionRequirement() now implements eqemu's
    // Mob::BehindMob (client/skill/behind_target.h) and canUseSkill() calls
    // it, so this message is produced only when facing really is the reason.
    //
    // T353: the resource predicate that used to be ANDed in here (so that a
    // Backstab short of endurance said endurance rather than facing) is gone
    // with the costs themselves -- there is no resource failure left for it to
    // out-rank. Facing is now the only reason this branch can fire, which is
    // exactly what T159 asked for. See refactor/T353_skill_costs.md.
    if (skill.requires_behind && !checkPositionRequirement(skill_id, target_id)) {
        return "You must be behind your target";
    }

    return "";  // No error
}

bool SkillManager::isSkillOnCooldown(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }
    return m_skills[skill_id].isOnCooldown();
}

uint32_t SkillManager::getSkillCooldownRemaining(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return 0;
    }
    return m_skills[skill_id].getCooldownRemaining();
}

const SkillData* SkillManager::getSkill(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return nullptr;
    }
    return &m_skills[skill_id];
}

SkillData* SkillManager::getSkill(uint8_t skill_id) {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return nullptr;
    }
    return &m_skills[skill_id];
}

// ============================================================================
// Skill Lists for UI
// ============================================================================

std::vector<const SkillData*> SkillManager::getAllSkills() const {
    std::vector<const SkillData*> result;
    result.reserve(EQT::MAX_PP_SKILL);

    for (const auto& skill : m_skills) {
        if (skill.isDisplayableSkill()) {
            result.push_back(&skill);
        }
    }

    return result;
}

size_t SkillManager::getSkillCount() const {
    size_t count = 0;
    for (const auto& skill : m_skills) {
        if (skill.isDisplayableSkill()) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Skill Activation
// ============================================================================

bool SkillManager::activateSkill(uint8_t skill_id, uint16_t target_id) {
    if (!canUseSkill(skill_id)) {
        std::string error = getSkillUseError(skill_id);
        if (m_on_skill_activated) {
            m_on_skill_activated(skill_id, false, error);
        }
        LOG_DEBUG(MOD_MAIN, "Cannot activate skill {}: {}", skill_id, error);
        return false;
    }

    const SkillData& skill = m_skills[skill_id];

    // For skills that require a target, get from combat manager if not provided
    // For skills that don't require a target, always use 0
    if (skill.requires_target) {
        if (target_id == 0 && m_eq && m_eq->GetCombatManager()) {
            target_id = m_eq->GetCombatManager()->GetTargetId();
        }
    } else {
        // Skills that don't require a target always send target=0
        target_id = 0;
    }

    // Send packet to server
    sendSkillUsePacket(skill_id, target_id);

    // B894: the cast is live from here. It ends on movement or a pose change
    // through the state-bus listeners (stopFishingOnMove /
    // stopFishingOnPositionState); the cooldown is the server's own reuse
    // timer, not a stop condition.
    if (skill_id == 55) {
        m_is_fishing = true;
    }

    // Play animation locally
    playSkillAnimation(skill_id);

    // Mark skill as used (start cooldown)
    m_skills[skill_id].markUsed();

    if (m_on_skill_activated) {
        m_on_skill_activated(skill_id, true, "");
    }

    LOG_DEBUG(MOD_MAIN, "Activated skill {} ({}) on target {}",
              skill_id, m_skills[skill_id].name, target_id);

    return true;
}

// B502 (GitLab #177) DELETED SkillManager::toggleSkill(): the ungated
// toggle-skill emitter (Hide/Sneak/Meditate via sendSkillUsePacket()
// bypassing activateSkill()'s gate) that had ZERO callers tree-wide --
// the client has no toggle-skill UI and no command that reaches it, so
// nothing could bypass the gate through it. The T162 note that rode its
// old body stands for the whole class: whoever wires a toggle UI routes
// through activateSkill(); see refactor/T162_skill_gate_bypasses.md.
// Its companions -- SkillData::is_toggle (writer: initializeSkillData),
// is_active and isSkillActive() -- went with it as the fields and query
// this was the only one that touched.

// ============================================================================
// Callbacks
// ============================================================================

void SkillManager::setOnSkillUpdate(SkillUpdateCallback callback) {
    m_on_skill_update = std::move(callback);
}

void SkillManager::setOnSkillActivated(SkillActivatedCallback callback) {
    m_on_skill_activated = std::move(callback);
}

// ============================================================================
// Private Helpers
// ============================================================================

void SkillManager::sendSkillUsePacket(uint8_t skill_id, uint16_t target_id) {
    if (!m_eq) {
        return;
    }

    // Route skills to their correct opcodes
    // Many skills have dedicated opcodes rather than using OP_CombatAbility
    uint16_t opcode = getSkillOpcode(skill_id);

    if (opcode == HC_OP_CombatAbility) {
        // Combat abilities use the CombatAbility struct via CombatManager
        if (m_eq->GetCombatManager()) {
            m_eq->GetCombatManager()->UseAbility(skill_id, target_id);
        }
        LOG_DEBUG(MOD_MAIN, "Sent skill use packet: skill={} target={}", skill_id, target_id);
        return;
    }

    // T152: five of the seventeen dedicated skill opcodes REQUIRE a body. The
    // audit (refactor/T152_skill_opcode_audit.md) checked all seventeen against
    // /mnt/projects/claude/EQEmu/zone/client_packet.cpp; the other twelve are
    // genuine empty-packet activations where a zero-length body is correct.
    // Three of the five already had a correct sender in this client, so they
    // route through it rather than growing a second copy.
    if (sendBodiedSkillPacket(skill_id, target_id)) {
        LOG_DEBUG(MOD_MAIN, "Sent skill use packet: skill={} target={}", skill_id, target_id);
        return;
    }

    // Remaining twelve: the handler reads nothing off the buffer, so an empty
    // body is the correct wire form. Verified per opcode in T152's Review.
    EQ::Net::DynamicPacket packet;
    packet.Resize(0);
    m_eq->QueuePacket(opcode, &packet);
    LOG_DEBUG(MOD_MAIN, "Sent skill packet: opcode=0x{:04x} skill={}", opcode, skill_id);
    if (opcode == HC_OP_Track) {
        // B895, MEASURED (review/UIGAP-ORACLE/211/tracking.tsv row 1,
        // run1.opcodes.tsv): the official client sends an EMPTY
        // OP_TrackUnknown at the same instant as OP_Track. eqemu's handler
        // reads nothing ("size 0 send right after OP_Track").
        EQ::Net::DynamicPacket unknown;
        unknown.Resize(0);
        m_eq->QueuePacket(HC_OP_TrackUnknown, &unknown);
    }

    LOG_DEBUG(MOD_MAIN, "Sent skill use packet: skill={} target={}", skill_id, target_id);
}

// ----------------------------------------------------------------------------
// T152: the five skill opcodes eqemu will not accept with an empty body.
//
// Returns true if this skill was handled here (sent, or deliberately not sent
// because a precondition failed); false means "no body required, fall through
// to the zero-length send".
// ----------------------------------------------------------------------------
bool SkillManager::sendBodiedSkillPacket(uint8_t skill_id, uint16_t target_id) {
    switch (skill_id) {

    // --- Apply Poison (6) -------------------------------------------------
    // eqemu DECODE(OP_ApplyPoison) is DECODE_LENGTH_EXACT(8) and the handler
    // needs the poison's inventory slot. A zero-length body was rewritten to
    // OP_Unknown before Handle_OP_ApplyPoison ever ran (T151). EverQuest::
    // SendApplyPoison already builds those 8 bytes correctly; it just had no
    // caller. Route through it.
    case 6: {
        const int16_t slot = findPoisonInventorySlot();
        if (slot < 0) {
            m_eq->AddChatSystemMessage("You do not have any poison to apply.");
            LOG_DEBUG(MOD_MAIN, "ApplyPoison: no poison item in inventory");
            return true;
        }
        m_eq->Player().SendApplyPoison(static_cast<uint32_t>(slot));
        return true;
    }

    // --- Bind Wound (9) ---------------------------------------------------
    // Handle_OP_Bind_Wound logs a size mismatch but does NOT return, then
    // dereferences app->pBuffer -- which is nullptr for a zero-length packet
    // (common/base_packet.cpp:30). EverQuest::SendBindWound is the correct,
    // already-live sender. T162: this "current target, else self" fallback is
    // now the ONLY copy -- EqActionHandler::bandage() used to carry the same
    // three lines and now routes here through activateSkill() instead.
    case 9: {
        uint16_t resolved = target_id;
        if (resolved == 0) {
            auto* combat = m_eq->GetCombatManager();
            resolved = (combat && combat->HasTarget()) ? combat->GetTargetId()
                                                       : m_eq->GetMySpawnID();
        }
        m_eq->Player().SendBindWound(resolved);
        return true;
    }

    // --- Disarm (16) ------------------------------------------------------
    // Gated on sizeof(Disarm_Struct) == 16 with a return, so an empty body was
    // dropped in silence. No existing sender; body built from the eqemu struct.
    // `source` must be our own entity id (mismatch files a POSSIBLE_HACK
    // player-event) and `skill` must equal the server's Disarm skill value.
    case 16: {
        if (target_id == 0) {
            LOG_DEBUG(MOD_MAIN, "Disarm: no target");
            return true;
        }
        auto body = EQ::skill::MakeDisarmBody(
            m_eq->GetMySpawnID(), target_id, m_skills[16].current_value);
        EQ::Net::DynamicPacket packet;
        packet.PutData(0, body.data(), body.size());
        m_eq->QueuePacket(HC_OP_Disarm, &packet);
        LOG_DEBUG(MOD_MAIN, "Sent Disarm: source={} target={} skill={}",
                  m_eq->GetMySpawnID(), target_id, m_skills[16].current_value);
        return true;
    }

    // --- Pick Pocket (48) -------------------------------------------------
    // Same shape as Bind Wound: the size check logs and falls through, then
    // reads pick_in->to off a null buffer. No existing sender.
    case 48: {
        if (target_id == 0) {
            LOG_DEBUG(MOD_MAIN, "PickPocket: no target");
            return true;
        }
        auto body = EQ::skill::MakePickPocketBody(
            target_id, m_eq->GetMySpawnID(),
            static_cast<uint16_t>(m_skills[48].current_value));
        EQ::Net::DynamicPacket packet;
        packet.PutData(0, body.data(), body.size());
        m_eq->QueuePacket(HC_OP_PickPocket, &packet);
        LOG_DEBUG(MOD_MAIN, "Sent PickPocket: to={} from={}", target_id,
                  m_eq->GetMySpawnID());
        return true;
    }

    // --- Taunt (73) -------------------------------------------------------
    // Gated on sizeof(ClientTarget_Struct) == 4. CombatManager::Taunt() already
    // sends exactly that and is live from /taunt; the skill path was the broken
    // twin. Route through it -- do NOT add a second Taunt sender.
    case 73: {
        auto* combat = m_eq->GetCombatManager();
        if (!combat) {
            LOG_DEBUG(MOD_MAIN, "Taunt: no combat manager");
            return true;
        }
        combat->Taunt(target_id);
        return true;
    }

    default:
        return false;
    }
}

// Find a poison item to hand OP_ApplyPoison. eqemu's handler resolves the slot
// through GetInv().GetItem() after TitaniumToServerSlot() translation
// (titanium.cpp:2475), so the Titanium-native slot ids this client already uses
// are exactly what goes on the wire.
//
// ItemInstance::skillType carries eqemu's ItemType (see skill_packet_bodies.h);
// ItemTypePoison is 42.
int16_t SkillManager::findPoisonInventorySlot() const {
    auto* inv = m_eq ? m_eq->GetInventoryManager() : nullptr;
    if (!inv) {
        return -1;
    }

    using namespace eqt::inventory;

    auto isPoison = [](const ItemInstance* item) {
        return item && item->itemId != 0 &&
               item->skillType == EQ::skill::kItemTypePoison;
    };

    for (int16_t slot = GENERAL_BEGIN; slot <= GENERAL_END; ++slot) {
        if (isPoison(inv->getItem(slot))) {
            return slot;
        }
    }
    for (int16_t slot = GENERAL_BAGS_BEGIN; slot <= GENERAL_BAGS_END; ++slot) {
        if (isPoison(inv->getItem(slot))) {
            return slot;
        }
    }
    return -1;
}

uint16_t SkillManager::getSkillOpcode(uint8_t skill_id) const {
    // Map skill IDs to their specific opcodes
    // Skills not listed here use OP_CombatAbility
    switch (skill_id) {
        // Skills with dedicated opcodes
        case 6:  return HC_OP_ApplyPoison;    // Apply Poison
        case 9:  return HC_OP_BindWound;      // Bind Wound
        case 16: return HC_OP_Disarm;         // Disarm
        case 17: return HC_OP_DisarmTraps;    // Disarm Traps
        case 25: return HC_OP_FeignDeath;     // Feign Death
        case 27: return HC_OP_Forage;         // Forage
        case 29: return HC_OP_Hide;           // Hide
        case 32: return HC_OP_Mend;           // Mend
        case 40: return HC_OP_SenseHeading;   // Sense Heading
        case 42: return HC_OP_Sneak;          // Sneak
        case 48: return HC_OP_PickPocket;     // Pick Pocket
        case 53: return HC_OP_Track;          // Tracking
        case 55: return HC_OP_Fishing;        // Fishing
        case 62: return HC_OP_SenseTraps;     // Sense Traps
        case 67: return HC_OP_Begging;        // Begging
        case 71: return HC_OP_InstillDoubt;   // Intimidation
        case 73: return HC_OP_Taunt;          // Taunt

        // Combat abilities that use OP_CombatAbility
        case 8:  // Backstab
        case 10: // Bash
        case 21: // Dragon Punch
        case 23: // Eagle Strike
        case 26: // Flying Kick
        case 30: // Kick
        case 38: // Round Kick
        case 52: // Tiger Claw
        default:
            return HC_OP_CombatAbility;
    }
}

void SkillManager::playSkillAnimation(uint8_t skill_id) {
    if (!m_eq) {
        return;
    }

    // Get the skill-specific animation code (e.g., "t07" for Flying Kick)
    const char* animCode = getSkillAnimationCode(skill_id);

    // Also get the generic animation ID to send to server
    uint8_t anim_id = getSkillAnimationId(skill_id);

    // Send animation ID to server (server may echo this back)
    if (anim_id != 0) {
        m_eq->Entities().SendAnimation(anim_id);
    }

    // D20e1: Play skill animation via bridge event
    if (animCode != nullptr && m_bridge) {
        uint16_t playerSpawnId = m_eq->GetMySpawnID();
        if (playerSpawnId != 0) {
            eqt::state::EntityAnimationEventData d;
            d.spawnId = playerSpawnId;
            d.animCode = 0;
            d.animName = animCode;
            d.loop = false;
            d.playThrough = true;
            m_bridge->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::EntityAnimationEvent, std::move(d)));
            LOG_DEBUG(MOD_MAIN, "Playing skill animation '{}' for skill {} on player {}",
                      animCode, skill_id, playerSpawnId);
        }
    }
}

bool SkillManager::checkCombatRequirement(uint8_t skill_id) const {
    const SkillData& skill = m_skills[skill_id];
    if (!skill.requires_combat) {
        return true;
    }

    // T317: this used to `return GetCombatManager()->HasTarget()` -- "I have
    // something selected" standing in for "I am fighting". They are not the
    // same thing, and every requires_combat skill also sets requires_target,
    // so the substitute made this gate a no-op: checkTargetRequirement() had
    // already asked the same question.
    //
    // The real in-combat signal is CombatManager's own COMBAT_STATE_ENGAGED.
    // It is NOT auto-combat-only: EnableAutoAttack()/EnableAutoFire() set it
    // directly (combat.cpp:489, :607) and DisableAutoAttack()/DisableAutoFire()
    // clear it, and both are reachable from /aa and the Combat hotbar slot
    // without /combat ever being enabled. Being attacked counts too -- T247's
    // aggro memory is how this client knows an NPC is on it, and a player who
    // is being hit is in combat whether or not their own swing is on.
    //
    // ⚠ T317 flagged this as DELIBERATELY STRICTER THAN THE SERVER: eqemu's
    // OPCombatAbility (zone/special_attacks.cpp:325) has NO in-combat
    // requirement at all, so the gate refused sends eqemu would have accepted
    // -- an opening Kick, and an opening Backstab from stealth, with
    // auto-attack off and no aggro.
    //
    // T325 (Will's ruling, 2026-09-02) reverted that, THE WAY T317 SAID TO:
    // by clearing requires_combat in skill_data.h, not by re-hollowing this
    // function. Nothing shipped sets the flag now, so this predicate's early
    // return above is the only path production takes. Everything below stays
    // implemented and tested so the mechanism is ready if a real requirement
    // ever turns up -- DO NOT replace it with `return true`, which is the
    // no-op gate T317 was dispatched to fix.
    auto* combat = m_eq ? m_eq->GetCombatManager() : nullptr;
    if (!combat) {
        return false;
    }
    if (combat->GetCombatState() == COMBAT_STATE_ENGAGED ||
        combat->IsAutoAttackEnabled() || combat->IsAutoFireEnabled()) {
        return true;
    }
    // Under attack by the thing we have selected.
    return combat->HasTarget() && combat->IsRecentAttacker(combat->GetTargetId());
}

bool SkillManager::checkTargetRequirement(uint8_t skill_id, uint16_t target_id) const {
    const SkillData& skill = m_skills[skill_id];
    if (!skill.requires_target) {
        return true;
    }

    return target_id != 0;
}

bool SkillManager::checkPositionRequirement(uint8_t skill_id, uint16_t target_id) const {
    const SkillData& skill = m_skills[skill_id];
    if (!skill.requires_behind) {
        return true;
    }

    // T317: was `return true;  // For now, assume position is valid` -- an
    // unconditional pass, which is also why T159 had to delete the refusal
    // message that quoted it (see canUseSkill's requires_behind comment).
    // Both go back in together, as T159's note instructed.
    //
    // The test is eqemu's own Mob::BehindMob, ported in
    // client/skill/behind_target.h. It needs the TARGET's heading and both
    // positions in SERVER coordinates; this client stores entity positions
    // x/y-swapped (entity.x = server_y) and holds every entity's heading in
    // the unflipped server convention, so the swap is undone here and no
    // heading flip is applied. Read behind_target.h before changing any of
    // that -- docs/CLAUDE.md's Heading/Rotation Conventions warning applies.
    if (!m_eq) {
        return true;  // no world to measure against; do not refuse
    }
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(target_id);
    if (it == entities.end()) {
        // Target not in our entity map (out of range, or not spawned yet).
        // Refusing here would turn a missing spawn into a facing complaint.
        return true;
    }
    const Entity& target = it->second;

    // GetPosition() is the one player-position store (T288). Same x/y swap as
    // the entity map: .x is server y, .y is server x.
    const glm::vec3 me = m_eq->GetPosition();

    return EQ::skill::IsBehindTarget(/*target_server_x=*/target.y,
                                      /*target_server_y=*/target.x,
                                      /*target_heading_deg=*/target.heading,
                                      /*attacker_server_x=*/me.y,
                                      /*attacker_server_y=*/me.x);
}

} // namespace EQ
