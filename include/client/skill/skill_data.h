/*
 * WillEQ Skill Data
 *
 * Defines the SkillData structure that holds all information about a player skill.
 */

#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include "skill_constants.h"

namespace EQ {

// ============================================================================
// Skill Data Structure
// ============================================================================

struct SkillData {
    // Skill identification
    uint8_t skill_id = 0;               // Maps to CastingSkill enum (0-74)
    std::string name;                   // Display name
    SkillCategory category = SkillCategory::Unknown;

    // Skill values
    uint32_t current_value = 0;         // Current skill level (0-252 typically)
    // T159: this is the LIFETIME cap for the player's class, taken from
    // config/skill_caps.json -- NOT the cap at the current level. The
    // per-level curve was collapsed out of that export (see skill_cap_table.h)
    // and there is no client-side source for it today.
    uint32_t max_value = 0;             // Class lifetime skill cap (0 if the class cannot have it)
    uint32_t base_value = 0;            // Base skill without modifiers

    // T159: does the player's class appear in config/skill_caps.json for this
    // skill at ANY level? This is the possession predicate, set by
    // SkillManager::loadClassSkillCaps(). Defaults true so a SkillData that
    // has never been through a cap load behaves the way it did before T159.
    bool class_can_have = true;
    // MIN(level) with cap>0 for this class, from the same file. INFORMATIONAL:
    // nothing gates on it -- T158 proved eqemu does not either (a level-10
    // Rogue applies poison at first_level 18).
    uint8_t class_first_level = 1;

    // Requirements
    uint8_t min_level = 1;              // Minimum level to use
    bool requires_target = false;       // Needs a valid target
    // Must be in combat. T325: NO shipped skill sets this -- eqemu enforces no
    // in-combat requirement on any opcode fable sends for a skill (see the
    // per-opcode derivation in initializeSkillData() below). The mechanism is
    // kept, working and tested, for anything that ever genuinely needs it.
    bool requires_combat = false;
    bool requires_behind = false;       // Must be behind target (Backstab)

    // T353: there is NO endurance_cost / mana_cost here any more, and adding
    // one back needs a measured source, not a guess. Skill activation costs no
    // resource in this era on either side: not one handler in
    // /mnt/projects/akk-stack/code/zone/client_packet.cpp for any opcode fable
    // sends for a skill reads or spends endurance or mana (endurance is spent
    // only by OP_Jump, by spell endurance_cost, and by a damage-absorb bonus),
    // and no client-side skill-cost table exists to load from. Spells DO have
    // costs -- that is SpellData::mana_cost, a different struct.

    // Timing
    uint32_t recast_time_ms = 0;        // Recast timer in milliseconds
    std::chrono::steady_clock::time_point last_used;  // When skill was last used

    // Animation
    uint8_t animation_id = 0;           // Animation ID to send to server
    // T440 (board B415): there is NO animation_code here. initializeSkillData()
    // used to copy getSkillAnimationCode(skill_id) into a std::string field that
    // nothing ever read -- SkillManager::playSkillAnimation(), the one consumer
    // that wants a skill's skeletal code, calls the free function directly. A
    // second copy of a value the table already owns is a store that can only go
    // stale; the function is the source.

    // State flags
    bool is_activatable = false;        // Can be manually activated
    // B502 (GitLab #177) DELETED is_toggle and is_active: their only reader
    // was the deleted SkillManager::toggleSkill()/isSkillActive() pair. The
    // toggle-skill set (Hide 29, Sneak 42, Meditate 31) is still known to
    // initializeSkillData() by id if a toggle UI is ever wired -- it goes
    // through activateSkill(), not a second field pair.
    // T440 (board B416): there is NO usable_while_sitting here, and adding one
    // back needs a measured refusal, not a guess -- the same bar T353 set for
    // endurance_cost and T325 for requires_combat.
    //
    // eqemu refuses NOTHING a fable skill activation can send on account of
    // sitting. The whole zone server has exactly two sitting refusals for an
    // action the player initiates, both in
    // /mnt/projects/akk-stack/code/zone/special_attacks.cpp -- Client::RangedAttack()
    // (line 976) and Client::ThrowingAttack() (line 1597), each an
    // `IsSitting() ||` term in a refusal list -- and neither is reachable from
    // this client: OPCombatAbility() only routes to them when
    // `ca_atk->m_atk == EQ::invslot::slotRange`, while CombatManager::UseAbility()
    // hardcodes that field to 100, and Archery(7)/Throwing(51) are not in
    // isActivatableSkill() at all. Not one of the seventeen dedicated skill
    // opcode handlers in client_packet.cpp reads the appearance. The only
    // stand-up gate in the server is for SPELLS (aa.cpp:1323, STAND_TO_CAST),
    // and it stands the caster UP rather than refusing.
    //
    // The field was also backwards as a gate: it was true for Meditate(31)
    // alone, so a predicate over it would have refused every OTHER skill while
    // sitting -- precisely the over-strictness that cost the rogue his opener
    // in T325.
    bool usable_while_stunned = false;  // Can use while stunned

    // ========================================================================
    // Helper Methods
    // ========================================================================

    // Check if the skill is currently on cooldown
    bool isOnCooldown() const {
        if (recast_time_ms == 0) {
            return false;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_used).count();
        return elapsed < static_cast<int64_t>(recast_time_ms);
    }

    // Get remaining cooldown in milliseconds
    uint32_t getCooldownRemaining() const {
        if (recast_time_ms == 0) {
            return 0;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_used).count();
        if (elapsed >= static_cast<int64_t>(recast_time_ms)) {
            return 0;
        }
        return static_cast<uint32_t>(recast_time_ms - elapsed);
    }

    // Get cooldown progress (0.0 = just used, 1.0 = ready)
    float getCooldownProgress() const {
        if (recast_time_ms == 0) {
            return 1.0f;
        }
        auto remaining = getCooldownRemaining();
        if (remaining == 0) {
            return 1.0f;
        }
        return 1.0f - (static_cast<float>(remaining) / static_cast<float>(recast_time_ms));
    }

    // Mark the skill as just used
    void markUsed() {
        last_used = std::chrono::steady_clock::now();
    }

    // ------------------------------------------------------------------
    // T159: the two predicates that used to be one.
    //
    // Before T159 this struct had a single `hasSkill()` -- the DISPLAY test
    // below -- and SkillManager::canUseSkill()/getSkillUseError() used it as
    // a POSSESSION test. That is why activateSkill had no possession gate at
    // all: T158 watched a Rogue with Taunt 0 send OP_Taunt, print "You use
    // Taunt!", and have the server bin the packet in silence.
    //
    // They answer different questions and must stay separate:
    //   isDisplayableSkill()  -- "should the skills window list this row?"
    //   canClassHaveSkill()   -- "can my CLASS ever have this skill at all?"
    // ------------------------------------------------------------------

    // Display test: every valid skill id with a real name, whatever its value
    // and whatever the class. This is what the skills window wants.
    bool isDisplayableSkill() const {
        // Show all valid skills (0-74) regardless of current value
        return skill_id < 75 && name != "Unknown";
    }

    // Possession test: set from config/skill_caps.json at load time (see
    // SkillCapTable). This is PRESENCE of a (class, skill) row at any level,
    // NOT "cap at my current level > 0" -- see skill_cap_table.h for the
    // Apply Poison counter-example that rules the level form out.
    bool canClassHaveSkill() const {
        return class_can_have;
    }
};

// ============================================================================
// Skill Initialization Helper
// ============================================================================

// Initialize a SkillData struct with default values for a given skill ID
inline void initializeSkillData(SkillData& skill, uint8_t skill_id) {
    skill.skill_id = skill_id;
    skill.name = getSkillName(skill_id);
    skill.category = getSkillCategory(skill_id);
    skill.is_activatable = isActivatableSkill(skill_id);
    skill.animation_id = getSkillAnimationId(skill_id);
    skill.recast_time_ms = getSkillRecastTime(skill_id);

    // Set skill-specific flags
    //
    // T325 (Will's ruling, 2026-09-02): NO skill sets requires_combat any more.
    // T317 set it on eleven skills -- Backstab(8), Bash(10), Disarm(16),
    // DragonPunch(21), EagleStrike(23), FlyingKick(26), Kick(30),
    // RoundKick(38), TigerClaw(52), Intimidation(71) -- and implemented a real
    // in-combat gate behind it, then flagged in its own Review that the result
    // was DELIBERATELY STRICTER THAN THE SERVER. It was, and it cost the rogue
    // his opener: an opening Backstab from stealth, and an opening Kick, were
    // refused client-side with auto-attack off and no aggro, both of which
    // eqemu accepts.
    //
    // Re-derived per skill against the opcode fable actually sends
    // (SkillManager::getSkillOpcode) and that opcode's eqemu handler:
    //   * 8, 10, 21, 23, 26, 30, 38, 52 -> OP_CombatAbility ->
    //     Client::OPCombatAbility (zone/special_attacks.cpp:325): target
    //     exists, not casting/feared/stunned/mezzed/DivineAura/dead, MaxSkill,
    //     target id matches, IsAttackAllowed, CombatRange, reuse timer. NO
    //     in-combat requirement. (Backstab's extra rules live in
    //     Mob::TryBackstab -- 1H piercing weapon and BehindMob -- also no
    //     in-combat test.)
    //   * 16 -> OP_Disarm -> Client::Handle_OP_Disarm (client_packet.cpp:5749):
    //     not dead/zoning, HasSkill, timer, source id sanity, no corpse, has
    //     target, targets match, range <= 400, not feigned, IsAttackAllowed.
    //     NO in-combat requirement.
    //   * 71 -> OP_InstillDoubt -> Client::Handle_OP_InstillDoubt
    //     (client_packet.cpp:9095) -> Mob::InstillDoubt: timer, target is an
    //     NPC, no DivineAura, CombatRange. NO in-combat requirement.
    //
    // So all eleven were cleared, and none was kept. The FIELD, the predicate
    // SkillManager::checkCombatRequirement() and the "You must be in combat"
    // message all stay -- the mechanism is intact and correct for anything
    // that ever genuinely needs it. Reverting by re-hollowing the predicate
    // instead would have recreated the no-op gate T317 was dispatched to fix.
    //
    // requires_target stays on all eleven: every one of those handlers really
    // does need a target.
    switch (skill_id) {
        // Combat skills requiring a target (but NOT combat -- see above)
        case 8:  // Backstab
            skill.requires_target = true;
            skill.requires_behind = true;
            break;

        case 10: // Bash
            skill.requires_target = true;
            break;

        case 16: // Disarm
            skill.requires_target = true;
            break;

        case 21: // DragonPunch
        case 23: // EagleStrike
        case 26: // FlyingKick
        case 30: // Kick
        case 38: // RoundKick
        case 52: // TigerClaw
            skill.requires_target = true;
            break;

        case 71: // Intimidation
            skill.requires_target = true;
            break;

        // Skills requiring target but not necessarily combat
        case 48: // Pick Pocket
            skill.requires_target = true;
            break;

        case 67: // Begging
            skill.requires_target = true;
            break;

        case 73: // Taunt
            skill.requires_target = true;
            break;

        // Self-only skills (no target required)
        case 6:  // Apply Poison (applies to weapon)
        case 17: // Disarm Traps (area in front)
        case 25: // Feign Death
        case 27: // Forage
        case 32: // Mend
        case 40: // Sense Heading
        case 53: // Tracking
        case 55: // Fishing
        case 62: // Sense Traps
            // No target required
            break;

        // B502 (GitLab #177): the toggle-skills cases (Hide 29, Sneak 42,
        // Meditate 31) set is_toggle, which went with the deleted
        // toggleSkill() -- the ids stay on the list for a future toggle UI.

        default:
            break;
    }
}

} // namespace EQ
