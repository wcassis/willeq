/*
 * WillEQ Spell Effects Processor Implementation
 */

#include <client/spell/spell_effects.h>
#include <client/spell/spell_database.h>
#include <client/spell/buff_manager.h>
#include <client/eq.h>
#include <client/graphics/irrlicht_renderer.h>
#include <common/logging.h>
#include <algorithm>
#include <cmath>

namespace EQ {

// Forward declaration for helper function
static std::string getStatName(SpellEffect effect);

// Helper function to format stat modifiers with +/- sign
static std::string formatModifier(const std::string& prefix, int32_t value) {
    return prefix + (value >= 0 ? "+" : "") + std::to_string(value);
}

SpellEffects::SpellEffects(EverQuest* eq, SpellDatabase* spell_db, BuffManager* buff_mgr)
    : m_eq(eq)
    , m_spell_db(spell_db)
    , m_buff_mgr(buff_mgr)
{
}

SpellEffects::~SpellEffects() = default;

// ============================================================================
// Spell Processing
// ============================================================================

std::vector<EffectResult> SpellEffects::processSpell(
    uint32_t spell_id,
    uint16_t caster_id,
    uint16_t target_id,
    uint8_t caster_level,
    bool fully_resisted)
{
    std::vector<EffectResult> results;

    if (!m_spell_db) {
        LOG_WARN(MOD_SPELL, "SpellEffects::processSpell - no spell database");
        return results;
    }

    const SpellData* spell = m_spell_db->getSpell(spell_id);
    if (!spell) {
        LOG_WARN(MOD_SPELL, "SpellEffects::processSpell - unknown spell ID {}", spell_id);
        return results;
    }

    LOG_DEBUG(MOD_SPELL, "Processing spell '{}' (ID:{}) from caster {} on target {}",
              spell->name, spell_id, caster_id, target_id);

    // If fully resisted, no effects apply
    if (fully_resisted) {
        EffectResult result;
        result.effect = SpellEffect::InvalidEffect;
        result.resisted = true;
        result.applied = false;
        result.message = spell->name + " was resisted!";
        results.push_back(result);
        return results;
    }

    // Process each effect slot
    for (const auto& effect : spell->effects) {
        if (!effect.isValid()) {
            continue;
        }

        EffectResult result = processEffect(effect, *spell, caster_id, target_id, caster_level);
        if (result.effect != SpellEffect::InvalidEffect) {
            results.push_back(result);
        }
    }

    return results;
}

EffectResult SpellEffects::processEffect(
    const SpellEffectSlot& effect,
    const SpellData& spell,
    uint16_t caster_id,
    uint16_t target_id,
    uint8_t caster_level)
{
    EffectResult result;
    result.effect = effect.effect_id;
    result.value = calculateEffectValue(effect, caster_level);
    result.applied = true;

    LOG_TRACE(MOD_SPELL, "Processing effect {} with value {} on target {}",
              static_cast<int>(effect.effect_id), result.value, target_id);

    switch (effect.effect_id) {
        // ====================================================================
        // Damage and Healing
        // ====================================================================
        case SpellEffect::CurrentHP:
            // Positive = damage, Negative = heal
            if (result.value > 0) {
                handleDirectDamage(target_id, result.value, spell.resist_type);
                result.message = "dealt " + std::to_string(result.value) + " damage";
            } else if (result.value < 0) {
                handleHeal(target_id, -result.value);
                result.message = "healed for " + std::to_string(-result.value);
            }
            break;

        case SpellEffect::CurrentHPOnce:
            // Instant HP effect (like CurrentHP but no duration component)
            if (result.value > 0) {
                handleDirectDamage(target_id, result.value, spell.resist_type);
                result.message = "dealt " + std::to_string(result.value) + " damage";
            } else if (result.value < 0) {
                handleHeal(target_id, -result.value);
                result.message = "healed for " + std::to_string(-result.value);
            }
            break;

        case SpellEffect::HealOverTime:
            handleHealOverTime(target_id, -result.value);
            result.message = "healing over time";
            break;

        // ====================================================================
        // Stats
        // ====================================================================
        case SpellEffect::ArmorClass:
            handleACMod(target_id, result.value);
            result.message = formatModifier("AC ", result.value);
            break;

        case SpellEffect::ATK:
            handleATKMod(target_id, result.value);
            result.message = formatModifier("ATK ", result.value);
            break;

        case SpellEffect::STR:
        case SpellEffect::STA:
        case SpellEffect::AGI:
        case SpellEffect::DEX:
        case SpellEffect::WIS:
        case SpellEffect::INT:
        case SpellEffect::CHA:
            handleStatMod(target_id, effect.effect_id, result.value);
            result.message = formatModifier(getStatName(effect.effect_id) + " ", result.value);
            break;

        case SpellEffect::TotalHP:
            handleHPMod(target_id, result.value);
            result.message = formatModifier("Max HP ", result.value);
            break;

        case SpellEffect::ManaPool:
            handleManaMod(target_id, result.value);
            result.message = formatModifier("Mana ", result.value);
            break;

        // ====================================================================
        // Regeneration
        // ====================================================================
        case SpellEffect::HealRate:
            handleHPRegen(target_id, result.value);
            result.message = formatModifier("HP regen ", result.value);
            break;

        // ====================================================================
        // Movement Speed
        // ====================================================================
        case SpellEffect::MovementSpeed:
            handleMovementSpeed(target_id, result.value);
            if (result.value > 0) {
                result.message = "movement speed +" + std::to_string(result.value) + "%";
            } else {
                result.message = "movement speed " + std::to_string(result.value) + "%";
            }
            break;

        // ====================================================================
        // Crowd Control
        // ====================================================================
        case SpellEffect::Mez:
            handleMez(target_id, spell.calculateDuration(caster_level));
            result.message = "mesmerized";
            break;

        case SpellEffect::Stun:
            handleStun(target_id, static_cast<uint32_t>(result.value * 100));  // value in tenths of seconds
            result.message = "stunned";
            break;

        case SpellEffect::Root:
            handleRoot(target_id, spell.calculateDuration(caster_level));
            result.message = "rooted";
            break;

        case SpellEffect::Fear:
            handleFear(target_id, spell.calculateDuration(caster_level));
            result.message = "feared";
            break;

        case SpellEffect::Charm:
            handleCharm(target_id, caster_id, spell.calculateDuration(caster_level));
            result.message = "charmed";
            break;

        // ====================================================================
        // Visibility
        // ====================================================================
        case SpellEffect::Invisibility:
            handleInvisibility(target_id, 0);  // Normal invis
            result.message = "invisible";
            break;

        case SpellEffect::InvisVsUndead:
            handleInvisibility(target_id, 1);  // Invis vs undead
            result.message = "invisible to undead";
            break;

        case SpellEffect::InvisVsAnimals:
            handleInvisibility(target_id, 2);  // Invis vs animals
            result.message = "invisible to animals";
            break;

        case SpellEffect::SeeInvis:
            handleSeeInvisible(target_id);
            result.message = "can see invisible";
            break;

        case SpellEffect::UltraVision:
            handleUltraVision(target_id);
            result.message = "ultravision";
            break;

        case SpellEffect::InfraVision:
            handleInfraVision(target_id);
            result.message = "infravision";
            break;

        // ====================================================================
        // Movement Abilities
        // ====================================================================
        case SpellEffect::Levitate:
            handleLevitate(target_id, result.value != 0);
            result.message = result.value != 0 ? "levitating" : "levitation ended";
            break;

        case SpellEffect::WaterBreathing:
            handleWaterBreathing(target_id);
            result.message = "water breathing";
            break;

        // ====================================================================
        // Teleportation
        // ====================================================================
        case SpellEffect::BindAffinity:
            // Bind location set by server
            result.message = "bind affinity set";
            break;

        case SpellEffect::Gate:
            handleGate(target_id);
            result.message = "gating";
            break;

        case SpellEffect::Teleport:
        case SpellEffect::Teleport2:
        case SpellEffect::Translocate:
            // Teleport location handled by server
            result.message = "teleporting";
            break;

        // ====================================================================
        // Summoning
        // ====================================================================
        case SpellEffect::SummonItem:
            // Item summoning handled by server
            result.message = "summoning item";
            break;

        case SpellEffect::SummonPet:
        case SpellEffect::NecPet:
        case SpellEffect::SummonBSTPet:
            // Pet summoning handled by server
            result.message = "summoning pet";
            break;

        case SpellEffect::SummonCorpse:
            handleSummonCorpse(target_id);
            result.message = "summoning corpse";
            break;

        // ====================================================================
        // Dispelling and Curing
        // ====================================================================
        case SpellEffect::CancelMagic:
        case SpellEffect::DispelDetrimental:
            handleDispel(target_id, static_cast<uint8_t>(std::abs(result.value)));
            result.message = "dispelling";
            break;

        case SpellEffect::DiseaseCounter:
            handleCureDisease(target_id, result.value);
            result.message = "cured disease";
            break;

        case SpellEffect::PoisonCounter:
            handleCurePoison(target_id, result.value);
            result.message = "cured poison";
            break;

        case SpellEffect::CurseCounter:
            handleCureCurse(target_id, result.value);
            result.message = "removed curse";
            break;

        case SpellEffect::Blind:
            // Blind is applying blindness, not curing it
            // Curing blindness would be handled by DispelDetrimental
            result.message = "blinded";
            break;

        // ====================================================================
        // Defensive Abilities
        // ====================================================================
        case SpellEffect::Rune:
            handleRune(target_id, result.value);
            result.message = "rune (" + std::to_string(result.value) + " damage absorption)";
            break;

        case SpellEffect::AbsorbMagicAtt:
            handleAbsorbMagicDamage(target_id, result.value);
            result.message = "magic damage absorption";
            break;

        case SpellEffect::DamageShield:
            handleDamageShield(target_id, result.value);
            result.message = "damage shield (" + std::to_string(std::abs(result.value)) + ")";
            break;

        case SpellEffect::Reflect:
            handleReflectSpell(target_id, result.value);
            result.message = "spell reflect (" + std::to_string(result.value) + "%)";
            break;

        // ====================================================================
        // Resists
        // ====================================================================
        case SpellEffect::ResistFire:
            handleResistMod(target_id, ResistType::Fire, result.value);
            result.message = formatModifier("Fire Resist ", result.value);
            break;

        case SpellEffect::ResistCold:
            handleResistMod(target_id, ResistType::Cold, result.value);
            result.message = formatModifier("Cold Resist ", result.value);
            break;

        case SpellEffect::ResistPoison:
            handleResistMod(target_id, ResistType::Poison, result.value);
            result.message = formatModifier("Poison Resist ", result.value);
            break;

        case SpellEffect::ResistDisease:
            handleResistMod(target_id, ResistType::Disease, result.value);
            result.message = formatModifier("Disease Resist ", result.value);
            break;

        case SpellEffect::ResistMagic:
            handleResistMod(target_id, ResistType::Magic, result.value);
            result.message = formatModifier("Magic Resist ", result.value);
            break;

        case SpellEffect::ResistAll:
            handleResistMod(target_id, ResistType::Fire, result.value);
            handleResistMod(target_id, ResistType::Cold, result.value);
            handleResistMod(target_id, ResistType::Poison, result.value);
            handleResistMod(target_id, ResistType::Disease, result.value);
            handleResistMod(target_id, ResistType::Magic, result.value);
            result.message = formatModifier("All Resists ", result.value);
            break;

        // ====================================================================
        // Appearance
        // ====================================================================
        case SpellEffect::Illusion:
            handleIllusion(target_id, static_cast<uint16_t>(effect.base_value),
                          static_cast<uint8_t>(effect.base2_value), 0);
            result.message = "illusioned";
            break;

        case SpellEffect::ModelSize:
            // Negative value = shrink, positive = grow
            if (result.value < 0) {
                handleShrink(target_id);
                result.message = "shrunk";
            } else {
                handleGrow(target_id);
                result.message = "enlarged";
            }
            break;

        // ====================================================================
        // Aggro
        // ====================================================================
        case SpellEffect::ChangeAggro:
        case SpellEffect::SpellHateMod:
            handleHateMod(target_id, result.value);
            result.message = "hate modified";
            break;

        case SpellEffect::WipeHateList:
            handleCancelAggro(target_id);
            result.message = "aggro cleared";
            break;

        // ====================================================================
        // Special
        // ====================================================================
        case SpellEffect::Revive:
            // Resurrection handled by server
            result.message = "resurrection offered";
            break;

        case SpellEffect::CompleteHeal:
            // Full heal
            handleHeal(target_id, 32000);  // Large number to ensure full heal
            result.message = "completely healed";
            break;

        default:
            // Effect not handled - log for debugging
            LOG_TRACE(MOD_SPELL, "Unhandled spell effect: {} (value: {})",
                      static_cast<int>(effect.effect_id), result.value);
            result.applied = false;
            break;
    }

    return result;
}

// ============================================================================
// Effect Value Calculation
// ============================================================================

int32_t SpellEffects::calculateEffectValue(const SpellEffectSlot& effect, uint8_t caster_level)
{
    int32_t base = effect.base_value;
    int32_t max_val = effect.max_value;
    int32_t formula = effect.formula;
    int32_t result = base;

    // Apply formula-based scaling
    switch (formula) {
        case 100:  // No scaling
            result = base;
            break;

        case 101:  // Linear scaling: base + (level * 1)
            result = base + caster_level;
            break;

        case 102:  // Linear scaling: base + (level * 2)
            result = base + (caster_level * 2);
            break;

        case 103:  // Linear scaling: base + (level * 3)
            result = base + (caster_level * 3);
            break;

        case 104:  // base + (level * 4)
            result = base + (caster_level * 4);
            break;

        case 105:  // base + (level * 5)
            result = base + (caster_level * 5);
            break;

        case 107:  // (level - 12) scaling (for high level spells)
            result = base + (caster_level > 12 ? caster_level - 12 : 0);
            break;

        case 108:  // (level - 16) scaling
            result = base + (caster_level > 16 ? caster_level - 16 : 0);
            break;

        case 109:  // (level - 20) scaling
            result = base + (caster_level > 20 ? caster_level - 20 : 0);
            break;

        case 110:  // (level / 2) scaling
            result = base + (caster_level / 2);
            break;

        case 111:  // (level / 3) scaling
            result = base + (caster_level / 3);
            break;

        case 112:  // (level / 4) scaling
            result = base + (caster_level / 4);
            break;

        case 113:  // (level / 5) scaling
            result = base + (caster_level / 5);
            break;

        case 119:  // (level - 24) scaling
            result = base + (caster_level > 24 ? caster_level - 24 : 0);
            break;

        case 121:  // (level - 30) scaling
            result = base + (caster_level > 30 ? caster_level - 30 : 0);
            break;

        case 122:  // (level - 40) scaling
            result = base + (caster_level > 40 ? caster_level - 40 : 0);
            break;

        case 123:  // (level - 50) scaling
            result = base + (caster_level > 50 ? caster_level - 50 : 0);
            break;

        case 124:  // 2 * level scaling
            result = base + (caster_level * 2);
            break;

        case 125:  // 3 * level scaling (for DoTs/HoTs)
            result = base + (caster_level * 3);
            break;

        default:
            result = base;
            break;
    }

    // Cap at max if specified (for both positive and negative values)
    if (max_val != 0) {
        if (result > 0 && max_val > 0 && result > max_val) {
            result = max_val;
        } else if (result < 0 && max_val < 0 && result < max_val) {
            result = max_val;
        } else if (result > 0 && max_val < 0) {
            // Negative max is a cap for positive values (like DD spells)
            if (result > -max_val) {
                result = -max_val;
            }
        }
    }

    return result;
}

// ============================================================================
// Helper Functions
// ============================================================================

void* SpellEffects::getEntity(uint16_t spawn_id)
{
    // Note: This returns nullptr since we can't get a mutable Entity reference
    // from the const GetEntities() method. The handlers work through the EverQuest class.
    return nullptr;
}

bool SpellEffects::isPlayer(uint16_t spawn_id) const
{
    if (!m_eq) return false;
    return spawn_id == m_eq->GetEntityID();
}

void SpellEffects::sendEffectMessage(const std::string& message, bool is_beneficial)
{
    if (m_eq && !message.empty()) {
        m_eq->AddChatSystemMessage(message);
    }
}

// Helper function to get stat name
static std::string getStatName(SpellEffect effect)
{
    switch (effect) {
        case SpellEffect::STR: return "STR";
        case SpellEffect::STA: return "STA";
        case SpellEffect::AGI: return "AGI";
        case SpellEffect::DEX: return "DEX";
        case SpellEffect::WIS: return "WIS";
        case SpellEffect::INT: return "INT";
        case SpellEffect::CHA: return "CHA";
        default: return "STAT";
    }
}

// ============================================================================
// Damage and Healing Handlers
// ============================================================================

void SpellEffects::handleDirectDamage(uint16_t target_id, int32_t amount, ResistType resist_type)
{
    LOG_DEBUG(MOD_SPELL, "Direct damage: {} to target {}", amount, target_id);
    // Actual HP modification is handled by server via HP update packets
    // Client-side we just log/display the damage
}

void SpellEffects::handleHeal(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Heal: {} on target {}", amount, target_id);
    // Actual HP modification is handled by server via HP update packets
}

void SpellEffects::handleHealOverTime(uint16_t target_id, int32_t amount_per_tick)
{
    LOG_DEBUG(MOD_SPELL, "HoT: {} per tick on target {}", amount_per_tick, target_id);
    // Buff application handles the ongoing effect
}

void SpellEffects::handleDamageOverTime(uint16_t target_id, int32_t amount_per_tick, ResistType resist_type)
{
    LOG_DEBUG(MOD_SPELL, "DoT: {} per tick on target {}", amount_per_tick, target_id);
    // Buff application handles the ongoing effect
}

// ============================================================================
// Stat Modification Handlers
// ============================================================================

void SpellEffects::handleStatMod(uint16_t target_id, SpellEffect stat, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Stat mod: {} {} on target {}",
              getStatName(stat), amount, target_id);
    // Stats tracked via buff manager
}

void SpellEffects::handleACMod(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "AC mod: {} on target {}", amount, target_id);
}

void SpellEffects::handleATKMod(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "ATK mod: {} on target {}", amount, target_id);
}

void SpellEffects::handleHPMod(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Max HP mod: {} on target {}", amount, target_id);
}

void SpellEffects::handleManaMod(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Mana mod: {} on target {}", amount, target_id);
}

void SpellEffects::handleManaRegen(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Mana regen mod: {} on target {}", amount, target_id);
}

void SpellEffects::handleHPRegen(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "HP regen mod: {} on target {}", amount, target_id);
}

// ============================================================================
// Movement and Control Handlers
// ============================================================================

void SpellEffects::handleMovementSpeed(uint16_t target_id, int32_t percent)
{
    LOG_DEBUG(MOD_SPELL, "Movement speed: {}% on target {}", percent, target_id);
    // Speed modifications tracked via buff manager
}

void SpellEffects::handleMez(uint16_t target_id, uint32_t duration_ticks)
{
    LOG_DEBUG(MOD_SPELL, "Mez: {} ticks on target {}", duration_ticks, target_id);
}

void SpellEffects::handleStun(uint16_t target_id, uint32_t duration_ms)
{
    LOG_DEBUG(MOD_SPELL, "Stun: {}ms on target {}", duration_ms, target_id);
}

void SpellEffects::handleRoot(uint16_t target_id, uint32_t duration_ticks)
{
    LOG_DEBUG(MOD_SPELL, "Root: {} ticks on target {}", duration_ticks, target_id);
}

void SpellEffects::handleFear(uint16_t target_id, uint32_t duration_ticks)
{
    LOG_DEBUG(MOD_SPELL, "Fear: {} ticks on target {}", duration_ticks, target_id);
}

void SpellEffects::handleCharm(uint16_t target_id, uint16_t caster_id, uint32_t duration_ticks)
{
    LOG_DEBUG(MOD_SPELL, "Charm: target {} by caster {} for {} ticks",
              target_id, caster_id, duration_ticks);
}

void SpellEffects::handleSnare(uint16_t target_id, int32_t percent)
{
    LOG_DEBUG(MOD_SPELL, "Snare: {}% on target {}", percent, target_id);
}

// ============================================================================
// Visibility Handlers
// ============================================================================

void SpellEffects::handleInvisibility(uint16_t target_id, uint8_t invis_type)
{
    const char* invis_names[] = {"normal", "vs undead", "vs animals"};
    LOG_DEBUG(MOD_SPELL, "Invisibility ({}): target {}",
              invis_type < 3 ? invis_names[invis_type] : "unknown", target_id);
}

void SpellEffects::handleSeeInvisible(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "See Invisible: target {}", target_id);
}

void SpellEffects::handleTrueSight(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "True Sight: target {}", target_id);
}

void SpellEffects::handleUltraVision(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Ultravision: target {}", target_id);

    // Only affects player's vision
    if (!isPlayer(target_id)) return;

    if (m_eq) {
        auto* renderer = m_eq->GetRenderer();
        if (renderer) {
            renderer->setVisionType(EQT::Graphics::VisionType::Ultravision);
            LOG_INFO(MOD_SPELL, "Player vision upgraded to Ultravision");
        }
    }
}

void SpellEffects::handleInfraVision(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Infravision: target {}", target_id);

    // Only affects player's vision
    if (!isPlayer(target_id)) return;

    if (m_eq) {
        auto* renderer = m_eq->GetRenderer();
        if (renderer) {
            renderer->setVisionType(EQT::Graphics::VisionType::Infravision);
            LOG_INFO(MOD_SPELL, "Player vision upgraded to Infravision");
        }
    }
}

// ============================================================================
// Movement Ability Handlers
// ============================================================================

void SpellEffects::handleLevitate(uint16_t target_id, bool enable)
{
    LOG_DEBUG(MOD_SPELL, "Levitate {}: target {}", enable ? "ON" : "OFF", target_id);
}

void SpellEffects::handleFly(uint16_t target_id, bool enable)
{
    LOG_DEBUG(MOD_SPELL, "Fly {}: target {}", enable ? "ON" : "OFF", target_id);
}

void SpellEffects::handleWaterBreathing(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Water Breathing: target {}", target_id);
}

// ============================================================================
// Teleportation Handlers
// ============================================================================

void SpellEffects::handleBindAffinity(uint16_t target_id, float x, float y, float z, uint16_t zone_id)
{
    LOG_DEBUG(MOD_SPELL, "Bind Affinity: target {} at ({}, {}, {}) zone {}",
              target_id, x, y, z, zone_id);
}

void SpellEffects::handleGate(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Gate: target {}", target_id);
}

void SpellEffects::handleTeleport(uint16_t target_id, float x, float y, float z, uint16_t zone_id)
{
    LOG_DEBUG(MOD_SPELL, "Teleport: target {} to ({}, {}, {}) zone {}",
              target_id, x, y, z, zone_id);
}

void SpellEffects::handleTranslocate(uint16_t target_id, float x, float y, float z, uint16_t zone_id)
{
    LOG_DEBUG(MOD_SPELL, "Translocate: target {} to ({}, {}, {}) zone {}",
              target_id, x, y, z, zone_id);
}

// ============================================================================
// Summoning Handlers
// ============================================================================

void SpellEffects::handleSummonItem(uint16_t target_id, uint32_t item_id, uint8_t charges)
{
    LOG_DEBUG(MOD_SPELL, "Summon Item: item {} (charges {}) to target {}",
              item_id, charges, target_id);
}

void SpellEffects::handleSummonPet(uint16_t target_id, uint32_t pet_npc_type_id)
{
    LOG_DEBUG(MOD_SPELL, "Summon Pet: NPC type {} for target {}", pet_npc_type_id, target_id);
}

void SpellEffects::handleSummonCorpse(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Summon Corpse: target {}", target_id);
}

// ============================================================================
// Dispelling and Curing Handlers
// ============================================================================

void SpellEffects::handleDispel(uint16_t target_id, uint8_t num_slots)
{
    LOG_DEBUG(MOD_SPELL, "Dispel: {} slots on target {}", num_slots, target_id);
}

void SpellEffects::handleCureDisease(uint16_t target_id, int32_t counter_amount)
{
    LOG_DEBUG(MOD_SPELL, "Cure Disease: {} counters on target {}", counter_amount, target_id);
}

void SpellEffects::handleCurePoison(uint16_t target_id, int32_t counter_amount)
{
    LOG_DEBUG(MOD_SPELL, "Cure Poison: {} counters on target {}", counter_amount, target_id);
}

void SpellEffects::handleCureCurse(uint16_t target_id, int32_t counter_amount)
{
    LOG_DEBUG(MOD_SPELL, "Cure Curse: {} counters on target {}", counter_amount, target_id);
}

void SpellEffects::handleCureBlindness(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Cure Blindness: target {}", target_id);
}

// ============================================================================
// Defensive Ability Handlers
// ============================================================================

void SpellEffects::handleRune(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Rune: {} absorption on target {}", amount, target_id);
}

void SpellEffects::handleAbsorbMagicDamage(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Absorb Magic: {} on target {}", amount, target_id);
}

void SpellEffects::handleDamageShield(uint16_t target_id, int32_t damage_per_hit)
{
    LOG_DEBUG(MOD_SPELL, "Damage Shield: {} per hit on target {}", damage_per_hit, target_id);
}

void SpellEffects::handleReflectSpell(uint16_t target_id, int32_t percent_chance)
{
    LOG_DEBUG(MOD_SPELL, "Reflect Spell: {}% on target {}", percent_chance, target_id);
}

// ============================================================================
// Appearance Handlers
// ============================================================================

void SpellEffects::handleIllusion(uint16_t target_id, uint16_t race_id, uint8_t gender, uint8_t texture)
{
    LOG_DEBUG(MOD_SPELL, "Illusion: race {} gender {} texture {} on target {}",
              race_id, gender, texture, target_id);
}

void SpellEffects::handleShrink(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Shrink: target {}", target_id);
}

void SpellEffects::handleGrow(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Grow: target {}", target_id);
}

// ============================================================================
// Resurrection Handler
// ============================================================================

void SpellEffects::handleResurrection(uint16_t target_id, float x, float y, float z, uint8_t exp_percent)
{
    LOG_DEBUG(MOD_SPELL, "Resurrection: target {} at ({}, {}, {}) exp {}%",
              target_id, x, y, z, exp_percent);
}

// ============================================================================
// Aggro Handlers
// ============================================================================

void SpellEffects::handleHateMod(uint16_t target_id, int32_t amount)
{
    LOG_DEBUG(MOD_SPELL, "Hate mod: {} on target {}", amount, target_id);
}

void SpellEffects::handleCancelAggro(uint16_t target_id)
{
    LOG_DEBUG(MOD_SPELL, "Cancel Aggro: target {}", target_id);
}

// ============================================================================
// Resist Modification Handler
// ============================================================================

void SpellEffects::handleResistMod(uint16_t target_id, ResistType resist_type, int32_t amount)
{
    const char* resist_names[] = {"None", "Magic", "Fire", "Cold", "Poison", "Disease"};
    int resist_idx = static_cast<int>(resist_type);
    LOG_DEBUG(MOD_SPELL, "{} Resist mod: {} on target {}",
              resist_idx < 6 ? resist_names[resist_idx] : "Unknown", amount, target_id);
}

} // namespace EQ
