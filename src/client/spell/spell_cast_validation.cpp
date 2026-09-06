/*
 * T407: SpellManager's pre-cast decision layer, split out of
 * spell_manager.cpp when that TU reached 1,451 of the 1,500-line ceiling.
 *
 * Two questions, in the order the cast path asks them:
 *   1. WHO is this spell actually aimed at -- adjustTargetForSpellType()
 *      rewrites the caller's target for Self / Pet / Group / PBAE spells,
 *      and isValidCreatureTarget() screens the body-type-restricted lines.
 *   2. MAY it be cast -- validateCast() is the single entry point, calling
 *      checkMana / checkEndurance / checkRange here and checkLineOfSight /
 *      checkReagents / checkPlayerState in the sibling TU
 *      spell_cast_checks.cpp (T246).
 *
 * Bodies moved verbatim; no behaviour change.
 */

#include "client/spell/spell_manager.h"
#include "client/spell/spell_body_type_target.h"
#include "client/eq.h"
#include "common/logging.h"

namespace EQ {

// ============================================================================
// Target Adjustment
// ============================================================================

uint16_t SpellManager::adjustTargetForSpellType(const SpellData& spell, uint16_t target_id) const
{
    uint16_t my_id = m_eq->GetEntityID();
    uint16_t pet_id = m_eq->GetPetSpawnId();

    switch (spell.target_type) {
        case SpellTargetType::Self:
            // Self-only spells always target self
            return my_id;

        case SpellTargetType::Pet:
            // Pet-only spells always target pet (return 0 if no pet)
            if (pet_id == 0) {
                LOG_DEBUG(MOD_SPELL, "Pet-only spell '{}' but no pet available", spell.name);
                return 0;  // Will trigger InvalidTarget
            }
            return pet_id;

        case SpellTargetType::GroupV1:
        case SpellTargetType::GroupV2:
        case SpellTargetType::GroupNoPets:
        case SpellTargetType::GroupedClients:
        case SpellTargetType::GroupClientsPets:
            // Group spells target self if no target selected
            if (target_id == 0) {
                LOG_DEBUG(MOD_SPELL, "Group spell '{}' with no target, targeting self", spell.name);
                return my_id;
            }
            return target_id;

        case SpellTargetType::AECaster:
            // PBAE spells are centered on caster, target self
            return my_id;

        default:
            // For all other spell types, use the provided target
            return target_id;
    }
}

bool SpellManager::isValidCreatureTarget(const SpellData& spell, uint16_t target_id) const
{
    // Get target entity
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(target_id);
    if (it == entities.end()) {
        return false;  // Target not found
    }

    const Entity& target = it->second;

    // T314: the body-type numbering has ONE home -- client/eq_body_types.h,
    // via BodyTypeSatisfiesTargetType(). The local BT_* block that used to sit
    // here was off by one for every value (BT_HUMANOID = 0, BT_ANIMAL = 20,
    // BT_PLANT = 24 ...), so every creature-restricted spell tested the wrong
    // body type: refused against a legitimate target, accepted against an
    // illegitimate one.
    return BodyTypeSatisfiesTargetType(spell.target_type, target.bodytype,
                                       target.is_corpse);
}

// ============================================================================
// Cast Validation
// ============================================================================

CastResult SpellManager::validateCast(const SpellData& spell, uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "validateCast: spell='{}' target_type={} target_id={}",
        spell.name, static_cast<int>(spell.target_type), target_id);

    // Check if already casting
    if (m_is_casting) {
        return CastResult::AlreadyCasting;
    }

    // Check player state (stunned / silenced / feared from the buff list)
    if (CastResult state = checkPlayerState(); state != CastResult::Success) {
        return state;
    }

    // Check mana
    if (!checkMana(spell)) {
        return CastResult::NotEnoughMana;
    }

    // T355: endurance, exactly beside mana. The server enforces this too
    // (eqemu zone/effects.cpp:900 refuses on GetEndurance() < endurance_cost)
    // and stays the backstop; this is the local refusal the official client
    // gives first, with eqstr 5805.
    if (!checkEndurance(spell)) {
        return CastResult::NotEnoughEndurance;
    }

    // Check range (if target required)
    if (spell.target_type != SpellTargetType::Self && target_id != 0) {
        if (!checkRange(spell, target_id)) {
            return CastResult::OutOfRange;
        }

        // Check line of sight
        if (!checkLineOfSight(target_id)) {
            return CastResult::NoLineOfSight;
        }
    }

    // Check creature type restrictions (Animal, Undead, Summoned, Plant, Giant, Dragon, Corpse)
    if (target_id != 0) {
        switch (spell.target_type) {
            case SpellTargetType::Animal:
            case SpellTargetType::Undead:
            case SpellTargetType::UndeadAE:
            case SpellTargetType::Summoned:
            case SpellTargetType::SummonedAE:
            case SpellTargetType::Corpse:
            case SpellTargetType::Plant:
            case SpellTargetType::Giant:
            case SpellTargetType::Dragon:
                if (!isValidCreatureTarget(spell, target_id)) {
                    LOG_DEBUG(MOD_SPELL, "Invalid creature target for spell '{}' (target_type={})",
                        spell.name, static_cast<int>(spell.target_type));
                    return CastResult::InvalidTarget;
                }
                break;
            default:
                break;
        }
    }

    // Check reagents. T251: remember WHICH component was short so the
    // refusal message can name it (castRefusalMessage(), cast_refusal.h).
    m_last_missing_reagent_id = 0;
    if (spell.requiresReagents() && !checkReagents(spell, &m_last_missing_reagent_id)) {
        return CastResult::ComponentMissing;
    }

    return CastResult::Success;
}

bool SpellManager::checkMana(const SpellData& spell) const
{
    if (spell.mana_cost <= 0) {
        return true;  // Free spell or mana-returning spell
    }

    return m_eq->GetCurrentMana() >= static_cast<uint32_t>(spell.mana_cost);
}

// T355. Mirrors checkMana() including the <= 0 early-out: spells_us.txt
// carries 0 for every spell that costs no endurance (all of Classic through
// Velious bar the disciplines), and a negative value would be an
// endurance-returning spell, never a refusal.
bool SpellManager::checkEndurance(const SpellData& spell) const
{
    if (spell.endurance_cost <= 0) {
        return true;
    }

    return m_eq->GetCurrentEndurance() >= static_cast<uint32_t>(spell.endurance_cost);
}

bool SpellManager::checkRange(const SpellData& spell, uint16_t target_id) const
{
    LOG_DEBUG(MOD_SPELL, "checkRange: spell='{}' range={} target_id={}",
        spell.name, spell.range, target_id);

    if (spell.range <= 0) {
        LOG_DEBUG(MOD_SPELL, "checkRange: no range limit, returning true");
        return true;  // No range limit
    }

    // Get player and target positions
    glm::vec3 player_pos = m_eq->GetPosition();
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(target_id);
    if (it == entities.end()) {
        LOG_DEBUG(MOD_SPELL, "checkRange: target {} not found in {} entities",
            target_id, entities.size());
        return false;  // Target doesn't exist
    }

    glm::vec3 target_pos(it->second.x, it->second.y, it->second.z);
    float distance = glm::distance(player_pos, target_pos);

    LOG_DEBUG(MOD_SPELL, "checkRange: player=({:.1f},{:.1f},{:.1f}) target=({:.1f},{:.1f},{:.1f}) distance={:.1f} range={}",
        player_pos.x, player_pos.y, player_pos.z,
        target_pos.x, target_pos.y, target_pos.z,
        distance, spell.range);

    return distance <= spell.range;
}

// T246: checkLineOfSight / countCarriedItem / checkReagents / checkPlayerState
// live in spell_cast_checks.cpp (same class, own TU).

} // namespace EQ
