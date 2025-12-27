/*
 * WillEQ Spell Type Processor
 *
 * Handles different spell targeting types and multi-target spell delivery.
 * Routes spells to appropriate targets based on SpellTargetType.
 * Covers Classic through Velious expansions.
 */

#ifndef EQT_SPELL_TYPE_PROCESSOR_H
#define EQT_SPELL_TYPE_PROCESSOR_H

#include "spell_data.h"
#include "spell_constants.h"
#include <vector>
#include <chrono>
#include <random>
#include <cstdint>

// Forward declarations
class EverQuest;

namespace EQ {

class SpellDatabase;
class SpellEffects;

// ============================================================================
// Rain Spell Instance - tracks active rain spells
// ============================================================================

struct RainSpellInstance {
    uint32_t spell_id = 0;
    uint16_t caster_id = 0;
    uint8_t caster_level = 0;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float center_z = 0.0f;
    float radius = 0.0f;
    uint8_t waves_remaining = 0;
    uint32_t wave_interval_ms = 3000;  // 3 seconds between waves
    std::chrono::steady_clock::time_point next_wave_time;

    static constexpr uint8_t DEFAULT_RAIN_WAVES = 3;
    static constexpr uint8_t MAX_TARGETS_PER_WAVE = 4;
};

// ============================================================================
// Spell Type Processor
// ============================================================================

class SpellTypeProcessor {
public:
    SpellTypeProcessor(EverQuest* eq, SpellDatabase* spell_db, SpellEffects* spell_effects);
    ~SpellTypeProcessor();

    // ========================================================================
    // Update (call every frame)
    // ========================================================================

    void update(float delta_time);

    // ========================================================================
    // Spell Processing by Target Type
    // ========================================================================

    // Main entry point - routes spell to appropriate handler
    void processSpell(uint32_t spell_id, uint16_t caster_id, uint16_t target_id,
                      uint8_t caster_level, bool from_server = true);

    // ========================================================================
    // Target Finding
    // ========================================================================

    // Find targets in radius around a point (for AE spells)
    std::vector<uint16_t> findTargetsInRadius(float center_x, float center_y, float center_z,
                                               float radius, bool beneficial_only);

    // Find PBAE targets around caster
    std::vector<uint16_t> findPBAETargets(uint16_t caster_id, float radius, bool beneficial);

    // Find group members in range
    std::vector<uint16_t> findGroupTargets(uint16_t caster_id, float range);

    // ========================================================================
    // Rain Spell Management
    // ========================================================================

    // Start a rain spell
    void startRainSpell(uint32_t spell_id, uint16_t caster_id, uint16_t target_id,
                        uint8_t caster_level);

    // Get active rain count (for debugging/UI)
    size_t getActiveRainCount() const { return m_active_rains.size(); }

private:
    // ========================================================================
    // Spell Type Handlers
    // ========================================================================

    // Single target spells
    void processSingleTarget(const SpellData& spell, uint16_t caster_id,
                             uint16_t target_id, uint8_t caster_level);

    // Self-only spells
    void processSelfSpell(const SpellData& spell, uint16_t caster_id,
                          uint8_t caster_level);

    // Point Blank AE (centered on caster)
    void processPBAE(const SpellData& spell, uint16_t caster_id, uint8_t caster_level);

    // Targeted AE (centered on target location)
    void processTargetedAE(const SpellData& spell, uint16_t caster_id,
                           uint16_t target_id, uint8_t caster_level);

    // Group spells
    void processGroupSpell(const SpellData& spell, uint16_t caster_id,
                           uint8_t caster_level);

    // Rain spells (multiple waves)
    void processRainWave(RainSpellInstance& rain);

    // ========================================================================
    // Helper Methods
    // ========================================================================

    // Get entity position (returns false if entity not found)
    bool getEntityPosition(uint16_t entity_id, float& x, float& y, float& z) const;

    // Calculate distance between two points
    float calculateDistance(float x1, float y1, float z1,
                           float x2, float y2, float z2) const;

    // Check if entity is valid target for spell
    bool isValidTarget(uint16_t entity_id, const SpellData& spell, uint16_t caster_id) const;

    // Check if entity is NPC
    bool isNPC(uint16_t entity_id) const;

    // Check if entity is player character
    bool isPlayer(uint16_t entity_id) const;

    // ========================================================================
    // Data Members
    // ========================================================================

    EverQuest* m_eq = nullptr;
    SpellDatabase* m_spell_db = nullptr;
    SpellEffects* m_spell_effects = nullptr;

    // Active rain spells
    std::vector<RainSpellInstance> m_active_rains;

    // Random number generator for rain target selection
    std::mt19937 m_rng;
};

} // namespace EQ

#endif // EQT_SPELL_TYPE_PROCESSOR_H
