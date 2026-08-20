/*
 * WillEQ Skill Manager Implementation
 */

#include "client/skill/skill_manager.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/skill/skill_packet_bodies.h"
#include "client/inventory/inventory_manager.h"
#include "client/inventory/inventory_constants.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "common/logging.h"
#include "common/net/packet.h"

namespace EQ {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SkillManager::SkillManager(EverQuest* eq)
    : m_eq(eq)
{
    // Initialize all skills with default data
    for (uint8_t i = 0; i < EQT::MAX_PP_SKILL; ++i) {
        initializeSkillData(m_skills[i], i);
    }
}

SkillManager::~SkillManager() = default;

// ============================================================================
// Initialization
// ============================================================================

void SkillManager::initialize(uint8_t player_class, uint8_t player_race, uint8_t player_level) {
    m_player_class = player_class;
    m_player_race = player_race;
    m_player_level = player_level;

    loadClassSkillCaps(player_class, player_level);

    LOG_DEBUG(MOD_MAIN, "SkillManager initialized: class={} race={} level={}",
              player_class, player_race, player_level);
}

void SkillManager::loadClassSkillCaps(uint8_t player_class, uint8_t player_level) {
    // TODO: Load actual class skill caps from data
    // For now, set reasonable defaults - skills with max_value > 0 are available
    // This would normally be loaded from a database or data file

    // Set default max values for common skills based on level
    // In real EQ, skill caps vary by class/level combination
    for (auto& skill : m_skills) {
        // By default, assume skill is not available
        skill.max_value = 0;
    }

    // Common skills available to most classes (simplified)
    // Actual implementation would check class-specific skill tables
    m_skills[15].max_value = player_level * 5;  // Defense
    m_skills[19].max_value = player_level * 5;  // Dodge
    m_skills[33].max_value = player_level * 5;  // Offense
    m_skills[50].max_value = 200;               // Swimming
    m_skills[40].max_value = 200;               // Sense Heading
    m_skills[9].max_value = player_level * 5;   // Bind Wound
    m_skills[66].max_value = 200;               // Alcohol Tolerance
    m_skills[67].max_value = 200;               // Begging

    LOG_DEBUG(MOD_MAIN, "Loaded skill caps for class {} level {}", player_class, player_level);
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

    // If we got a skill update, assume the skill is available
    if (value > 0 && m_skills[skill_id].max_value == 0) {
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

        // If we got a value, assume skill is available
        if (skills[i] > 0 && m_skills[i].max_value == 0) {
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

bool SkillManager::hasSkill(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }
    return m_skills[skill_id].hasSkill();
}

bool SkillManager::canUseSkill(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }

    const SkillData& skill = m_skills[skill_id];

    // Must have the skill
    if (!skill.hasSkill()) {
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

    // Check resource requirements
    if (!checkResourceRequirement(skill_id)) {
        return false;
    }

    return true;
}

std::string SkillManager::getSkillUseError(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return "Invalid skill";
    }

    const SkillData& skill = m_skills[skill_id];

    if (!skill.hasSkill()) {
        return "You do not have this skill";
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

    if (skill.requires_combat) {
        // TODO: Check if in combat
    }

    if (skill.requires_behind) {
        // TODO: Check if behind target
        return "You must be behind your target";
    }

    if (!checkResourceRequirement(skill_id)) {
        if (skill.endurance_cost > 0) {
            return "Not enough endurance";
        }
        if (skill.mana_cost > 0) {
            return "Not enough mana";
        }
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

bool SkillManager::isSkillReady(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }
    return !m_skills[skill_id].isOnCooldown();
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
        if (skill.hasSkill()) {
            result.push_back(&skill);
        }
    }

    return result;
}

std::vector<const SkillData*> SkillManager::getSkillsByCategory(SkillCategory category) const {
    std::vector<const SkillData*> result;

    for (const auto& skill : m_skills) {
        if (skill.hasSkill() && skill.category == category) {
            result.push_back(&skill);
        }
    }

    return result;
}

std::vector<const SkillData*> SkillManager::getActivatableSkills() const {
    std::vector<const SkillData*> result;

    for (const auto& skill : m_skills) {
        if (skill.hasSkill() && skill.is_activatable) {
            result.push_back(&skill);
        }
    }

    return result;
}

size_t SkillManager::getSkillCount() const {
    size_t count = 0;
    for (const auto& skill : m_skills) {
        if (skill.hasSkill()) {
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

bool SkillManager::toggleSkill(uint8_t skill_id) {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }

    SkillData& skill = m_skills[skill_id];
    if (!skill.is_toggle) {
        return false;
    }

    skill.is_active = !skill.is_active;

    // Send appropriate packet
    sendSkillUsePacket(skill_id, 0);

    LOG_DEBUG(MOD_MAIN, "Toggled skill {} ({}): now {}",
              skill_id, skill.name, skill.is_active ? "ON" : "OFF");

    return true;
}

bool SkillManager::isSkillActive(uint8_t skill_id) const {
    if (skill_id >= EQT::MAX_PP_SKILL) {
        return false;
    }
    return m_skills[skill_id].is_active;
}

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
        m_eq->SendApplyPoison(static_cast<uint32_t>(slot));
        return true;
    }

    // --- Bind Wound (9) ---------------------------------------------------
    // Handle_OP_Bind_Wound logs a size mismatch but does NOT return, then
    // dereferences app->pBuffer -- which is nullptr for a zero-length packet
    // (common/base_packet.cpp:30). EverQuest::SendBindWound is the correct,
    // already-live sender; targeting mirrors EqActionHandler::bandage().
    case 9: {
        uint16_t resolved = target_id;
        if (resolved == 0) {
            auto* combat = m_eq->GetCombatManager();
            resolved = (combat && combat->HasTarget()) ? combat->GetTargetId()
                                                       : m_eq->GetMySpawnID();
        }
        m_eq->SendBindWound(resolved);
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
        m_eq->SendAnimation(anim_id);
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

    // TODO: Check if player is in combat
    // For now, assume combat skills can be used if we have a target
    if (m_eq && m_eq->GetCombatManager()) {
        return m_eq->GetCombatManager()->HasTarget();
    }

    return false;
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

    // TODO: Check if player is behind target
    // This requires position and heading calculations
    return true;  // For now, assume position is valid
}

bool SkillManager::checkResourceRequirement(uint8_t skill_id) const {
    const SkillData& skill = m_skills[skill_id];

    if (!m_eq) {
        return true;  // Can't check without EQ reference
    }

    // Check endurance
    if (skill.endurance_cost > 0) {
        if (m_eq->GetCurrentEndurance() < skill.endurance_cost) {
            return false;
        }
    }

    // Check mana
    if (skill.mana_cost > 0) {
        if (m_eq->GetCurrentMana() < skill.mana_cost) {
            return false;
        }
    }

    return true;
}

} // namespace EQ
