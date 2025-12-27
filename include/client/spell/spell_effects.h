/*
 * WillEQ Spell Effects Processor
 *
 * Processes spell effect types (SE_ values) and applies stat modifications,
 * damage/healing calculations, and special effects.
 */

#ifndef EQT_SPELL_EFFECTS_H
#define EQT_SPELL_EFFECTS_H

#include "spell_data.h"
#include "spell_constants.h"
#include <vector>
#include <string>
#include <functional>

// Forward declarations
class EverQuest;

namespace EQ {

class BuffManager;
class SpellDatabase;

// ============================================================================
// Effect Result
// ============================================================================

struct EffectResult {
    SpellEffect effect = SpellEffect::InvalidEffect;
    int32_t value = 0;
    bool resisted = false;
    bool applied = true;
    std::string message;
};

// ============================================================================
// Spell Effects Processor
// ============================================================================

class SpellEffects {
public:
    SpellEffects(EverQuest* eq, SpellDatabase* spell_db, BuffManager* buff_mgr);
    ~SpellEffects();

    // ========================================================================
    // Spell Processing
    // ========================================================================

    // Process all effects of a spell landing on a target
    // Returns list of effect results for display/logging
    std::vector<EffectResult> processSpell(
        uint32_t spell_id,
        uint16_t caster_id,
        uint16_t target_id,
        uint8_t caster_level,
        bool fully_resisted = false);

    // Process a single effect slot
    EffectResult processEffect(
        const SpellEffectSlot& effect,
        const SpellData& spell,
        uint16_t caster_id,
        uint16_t target_id,
        uint8_t caster_level);

    // ========================================================================
    // Effect Value Calculation
    // ========================================================================

    // Calculate effect value with level-based scaling formula
    static int32_t calculateEffectValue(
        const SpellEffectSlot& effect,
        uint8_t caster_level);

    // ========================================================================
    // Direct Effect Handlers
    // ========================================================================

    // Damage and Healing
    void handleDirectDamage(uint16_t target_id, int32_t amount, ResistType resist_type);
    void handleHeal(uint16_t target_id, int32_t amount);
    void handleHealOverTime(uint16_t target_id, int32_t amount_per_tick);
    void handleDamageOverTime(uint16_t target_id, int32_t amount_per_tick, ResistType resist_type);

    // Stat Modifications
    void handleStatMod(uint16_t target_id, SpellEffect stat, int32_t amount);
    void handleACMod(uint16_t target_id, int32_t amount);
    void handleATKMod(uint16_t target_id, int32_t amount);
    void handleHPMod(uint16_t target_id, int32_t amount);  // Max HP modifier
    void handleManaMod(uint16_t target_id, int32_t amount);  // Max Mana modifier
    void handleManaRegen(uint16_t target_id, int32_t amount);
    void handleHPRegen(uint16_t target_id, int32_t amount);

    // Movement and Control
    void handleMovementSpeed(uint16_t target_id, int32_t percent);
    void handleMez(uint16_t target_id, uint32_t duration_ticks);
    void handleStun(uint16_t target_id, uint32_t duration_ms);
    void handleRoot(uint16_t target_id, uint32_t duration_ticks);
    void handleFear(uint16_t target_id, uint32_t duration_ticks);
    void handleCharm(uint16_t target_id, uint16_t caster_id, uint32_t duration_ticks);
    void handleSnare(uint16_t target_id, int32_t percent);

    // Visibility
    void handleInvisibility(uint16_t target_id, uint8_t invis_type);  // 0=normal, 1=undead, 2=animals
    void handleSeeInvisible(uint16_t target_id);
    void handleTrueSight(uint16_t target_id);

    // Movement Abilities
    void handleLevitate(uint16_t target_id, bool enable);
    void handleFly(uint16_t target_id, bool enable);
    void handleWaterBreathing(uint16_t target_id);

    // Teleportation
    void handleBindAffinity(uint16_t target_id, float x, float y, float z, uint16_t zone_id);
    void handleGate(uint16_t target_id);
    void handleTeleport(uint16_t target_id, float x, float y, float z, uint16_t zone_id);
    void handleTranslocate(uint16_t target_id, float x, float y, float z, uint16_t zone_id);

    // Summoning
    void handleSummonItem(uint16_t target_id, uint32_t item_id, uint8_t charges);
    void handleSummonPet(uint16_t target_id, uint32_t pet_npc_type_id);
    void handleSummonCorpse(uint16_t target_id);

    // Dispelling and Curing
    void handleDispel(uint16_t target_id, uint8_t num_slots);
    void handleCureDisease(uint16_t target_id, int32_t counter_amount);
    void handleCurePoison(uint16_t target_id, int32_t counter_amount);
    void handleCureCurse(uint16_t target_id, int32_t counter_amount);
    void handleCureBlindness(uint16_t target_id);

    // Defensive Abilities
    void handleRune(uint16_t target_id, int32_t amount);
    void handleAbsorbMagicDamage(uint16_t target_id, int32_t amount);
    void handleDamageShield(uint16_t target_id, int32_t damage_per_hit);
    void handleReflectSpell(uint16_t target_id, int32_t percent_chance);

    // Appearance
    void handleIllusion(uint16_t target_id, uint16_t race_id, uint8_t gender, uint8_t texture);
    void handleShrink(uint16_t target_id);
    void handleGrow(uint16_t target_id);

    // Resurrection
    void handleResurrection(uint16_t target_id, float x, float y, float z, uint8_t exp_percent);

    // Aggro and Hate
    void handleHateMod(uint16_t target_id, int32_t amount);
    void handleCancelAggro(uint16_t target_id);

    // Resist Modifications
    void handleResistMod(uint16_t target_id, ResistType resist_type, int32_t amount);

private:
    // Get entity from EverQuest by spawn ID
    void* getEntity(uint16_t spawn_id);

    // Check if target is player
    bool isPlayer(uint16_t spawn_id) const;

    // Send effect message to chat
    void sendEffectMessage(const std::string& message, bool is_beneficial);

    // References
    EverQuest* m_eq;
    SpellDatabase* m_spell_db;
    BuffManager* m_buff_mgr;
};

} // namespace EQ

#endif // EQT_SPELL_EFFECTS_H
