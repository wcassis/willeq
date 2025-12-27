/*
 * WillEQ Buff Manager
 *
 * Manages active buffs on the player and other entities.
 * Tracks buff durations, stacking, and stat modifications.
 */

#ifndef EQT_BUFF_MANAGER_H
#define EQT_BUFF_MANAGER_H

#include "spell_data.h"
#include "spell_constants.h"
#include <common/packet_structs.h>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <cstdio>

namespace EQ {

class SpellDatabase;

// ============================================================================
// Active Buff
// ============================================================================

struct ActiveBuff {
    uint32_t spell_id = SPELL_UNKNOWN;
    uint16_t caster_id = 0;
    uint8_t caster_level = 0;
    uint32_t remaining_seconds = 0;       // Remaining time in seconds (updated locally)
    uint32_t counters = 0;                // Disease/poison/curse counters
    BuffEffectType effect_type = BuffEffectType::None;
    int8_t slot = -1;                     // Buff slot index (-1 if not assigned)

    // Check if buff has expired
    bool isExpired() const {
        if (isPermanent()) return false;
        return remaining_seconds == 0;
    }

    // Check if buff is permanent (no duration)
    bool isPermanent() const {
        return remaining_seconds == 0xFFFFFFFF;
    }

    // Get remaining time in seconds
    uint32_t getRemainingSeconds() const {
        return remaining_seconds;
    }

    // Get formatted time string
    // >= 1 hour: "Xh Ym"
    // < 1 hour: "X min"
    // < 1 minute: "X sec"
    std::string getTimeString() const {
        if (isPermanent()) return "Perm";

        uint32_t seconds = getRemainingSeconds();
        char buf[16];

        if (seconds >= 3600) {
            // Hours and minutes format: "1h 13m"
            uint32_t hours = seconds / 3600;
            uint32_t mins = (seconds % 3600) / 60;
            snprintf(buf, sizeof(buf), "%uh %um", hours, mins);
        } else if (seconds >= 60) {
            // Minutes format: "5 min"
            uint32_t mins = seconds / 60;
            snprintf(buf, sizeof(buf), "%u min", mins);
        } else {
            // Seconds format: "30 sec"
            snprintf(buf, sizeof(buf), "%u sec", seconds);
        }
        return buf;
    }

    // Check if buff is about to expire (for flashing)
    bool isAboutToExpire() const {
        if (isPermanent()) return false;
        return remaining_seconds > 0 && remaining_seconds < 10;
    }
};

// ============================================================================
// Buff Callbacks
// ============================================================================

using BuffFadeCallback = std::function<void(uint16_t entity_id, uint32_t spell_id)>;
using BuffApplyCallback = std::function<void(uint16_t entity_id, uint32_t spell_id)>;

// ============================================================================
// Buff Manager
// ============================================================================

class BuffManager {
public:
    explicit BuffManager(SpellDatabase* spell_db);
    ~BuffManager();

    // ========================================================================
    // Update
    // ========================================================================

    // Update buff timers (call every frame)
    void update(float delta_time);

    // ========================================================================
    // Player Buffs
    // ========================================================================

    // Set player buffs from character profile (e.g., on zone-in)
    void setPlayerBuffs(const EQT::SpellBuff_Struct* buffs, size_t count);

    // Update a single player buff slot (from buff packets)
    void setPlayerBuff(uint8_t slot, const EQT::SpellBuff_Struct& buff);

    // Remove player buff by slot
    void removePlayerBuffBySlot(uint8_t slot);

    // Get all player buffs
    const std::vector<ActiveBuff>& getPlayerBuffs() const { return m_player_buffs; }

    // Check if player has a specific buff
    bool hasPlayerBuff(uint32_t spell_id) const;

    // Get player buff by spell ID (returns nullptr if not found)
    const ActiveBuff* getPlayerBuff(uint32_t spell_id) const;

    // Get player buff by slot (returns nullptr if slot empty/invalid)
    const ActiveBuff* getPlayerBuffBySlot(uint8_t slot) const;

    // Get number of player buff slots used
    size_t getPlayerBuffCount() const { return m_player_buffs.size(); }

    // ========================================================================
    // Entity Buffs
    // ========================================================================

    // Set a buff on an entity (from packets)
    void setEntityBuff(uint16_t entity_id, uint8_t slot, const EQT::SpellBuff_Struct& buff);

    // Clear all buffs on an entity
    void clearEntityBuffs(uint16_t entity_id);

    // Get all buffs on an entity
    const std::vector<ActiveBuff>* getEntityBuffs(uint16_t entity_id) const;

    // Check if entity has a specific buff
    bool hasEntityBuff(uint16_t entity_id, uint32_t spell_id) const;

    // ========================================================================
    // Buff Application (from server packets)
    // ========================================================================

    // Apply a buff to a target
    void applyBuff(uint16_t target_id, uint32_t spell_id, uint16_t caster_id,
                   uint8_t caster_level, uint32_t duration_ticks);

    // Remove a buff from a target
    void removeBuff(uint16_t target_id, uint32_t spell_id);

    // Remove buff by slot
    void removeBuffBySlot(uint16_t target_id, uint8_t slot);

    // ========================================================================
    // Stat Modifications
    // ========================================================================

    // Get total stat modification from all buffs on an entity
    int32_t getBuffedStatMod(uint16_t entity_id, SpellEffect stat) const;

    // Get total stat modification from player buffs
    int32_t getPlayerStatMod(SpellEffect stat) const;

    // ========================================================================
    // Spell Classification
    // ========================================================================

    // Check if a spell is beneficial
    bool isBeneficial(uint32_t spell_id) const;

    // Check if a spell is detrimental
    bool isDetrimental(uint32_t spell_id) const;

    // ========================================================================
    // Event Callbacks
    // ========================================================================

    // Set callback for when a buff fades
    void setBuffFadeCallback(BuffFadeCallback callback) { m_on_buff_fade = callback; }

    // Set callback for when a buff is applied
    void setBuffApplyCallback(BuffApplyCallback callback) { m_on_buff_apply = callback; }

    // Access spell database for icon lookups
    SpellDatabase* getSpellDatabase() const { return m_spell_db; }

    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr size_t MAX_PLAYER_BUFFS = 25;   // Max buffs on player
    static constexpr size_t MAX_PLAYER_SONGS = 12;   // Max song buffs (short duration)

private:
    // Check and handle expired buffs
    void checkBuffExpiration(std::vector<ActiveBuff>& buffs, uint16_t entity_id);

    // Check if a new buff can stack with existing buffs
    bool checkStacking(const std::vector<ActiveBuff>& existing, uint32_t new_spell_id) const;

    // Find a free buff slot
    int8_t findFreeSlot(const std::vector<ActiveBuff>& buffs) const;

    // Get stat modification from a single buff
    int32_t getStatModFromBuff(const ActiveBuff& buff, SpellEffect stat) const;

    // Spell database reference
    SpellDatabase* m_spell_db;

    // Player buffs
    std::vector<ActiveBuff> m_player_buffs;

    // Entity buffs (keyed by entity_id)
    std::unordered_map<uint16_t, std::vector<ActiveBuff>> m_entity_buffs;

    // Time tracking for countdown
    float m_tick_accumulator = 0.0f;

    // Callbacks
    BuffFadeCallback m_on_buff_fade;
    BuffApplyCallback m_on_buff_apply;
};

} // namespace EQ

#endif // EQT_BUFF_MANAGER_H
