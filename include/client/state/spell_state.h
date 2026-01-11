#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace eqt {
namespace state {

// Forward declaration
class EventBus;

// Spell gem count (matching spell_constants.h)
constexpr uint8_t SPELL_GEM_COUNT = 8;

// Spellbook slot count
constexpr uint16_t SPELLBOOK_SLOT_COUNT = 400;

// Invalid/unknown spell ID
constexpr uint32_t SPELL_ID_UNKNOWN = 0xFFFFFFFF;

/**
 * Gem state enumeration (matching spell_constants.h GemState).
 */
enum class SpellGemState : uint8_t {
    Empty = 0,              // No spell memorized
    Ready = 1,              // Spell ready to cast
    Casting = 2,            // Currently casting this spell
    Refresh = 3,            // On cooldown after cast
    MemorizeProgress = 4,   // Being memorized
};

/**
 * SpellState - Contains spell system state data for event-driven updates.
 *
 * This class tracks spell gem states, casting state, and spellbook summary
 * information. It can be synchronized from the SpellManager and provides
 * event notifications for UI updates.
 */
class SpellState {
public:
    SpellState();
    ~SpellState() = default;

    // Set the event bus for state change notifications
    void setEventBus(EventBus* bus) { m_eventBus = bus; }

    // ========== Spell Gems ==========

    /**
     * Get spell ID in a gem slot.
     * @param gemSlot Gem slot (0-7)
     * @return Spell ID, or SPELL_ID_UNKNOWN if empty
     */
    uint32_t getGemSpellId(uint8_t gemSlot) const;

    /**
     * Get gem state.
     * @param gemSlot Gem slot (0-7)
     */
    SpellGemState getGemState(uint8_t gemSlot) const;

    /**
     * Get cooldown progress for a gem (0.0 = just cast, 1.0 = ready).
     * @param gemSlot Gem slot (0-7)
     */
    float getGemCooldownProgress(uint8_t gemSlot) const;

    /**
     * Get remaining cooldown in milliseconds.
     * @param gemSlot Gem slot (0-7)
     */
    uint32_t getGemCooldownRemaining(uint8_t gemSlot) const;

    /**
     * Check if a gem slot has a spell memorized.
     * @param gemSlot Gem slot (0-7)
     */
    bool hasSpellMemorized(uint8_t gemSlot) const;

    /**
     * Check if a gem is ready to cast.
     * @param gemSlot Gem slot (0-7)
     */
    bool isGemReady(uint8_t gemSlot) const;

    /**
     * Set gem spell and state.
     * Fires SpellGemChanged event if changed.
     * @param gemSlot Gem slot (0-7)
     * @param spellId Spell ID (or SPELL_ID_UNKNOWN to clear)
     * @param state Gem state
     */
    void setGem(uint8_t gemSlot, uint32_t spellId, SpellGemState state);

    /**
     * Set gem cooldown.
     * @param gemSlot Gem slot (0-7)
     * @param remainingMs Remaining cooldown in milliseconds
     * @param totalMs Total cooldown duration in milliseconds
     */
    void setGemCooldown(uint8_t gemSlot, uint32_t remainingMs, uint32_t totalMs);

    /**
     * Clear a gem slot.
     * Fires SpellGemChanged event.
     * @param gemSlot Gem slot (0-7)
     */
    void clearGem(uint8_t gemSlot);

    // ========== Casting State ==========

    /**
     * Check if currently casting a spell.
     */
    bool isCasting() const { return m_isCasting; }

    /**
     * Get the spell ID being cast.
     */
    uint32_t castingSpellId() const { return m_castingSpellId; }

    /**
     * Get the target ID of the spell being cast.
     */
    uint16_t castingTargetId() const { return m_castingTargetId; }

    /**
     * Get cast progress (0.0 = just started, 1.0 = complete).
     */
    float castProgress() const;

    /**
     * Get remaining cast time in milliseconds.
     */
    uint32_t castTimeRemaining() const { return m_castTimeRemaining; }

    /**
     * Get total cast time in milliseconds.
     */
    uint32_t castTimeTotal() const { return m_castTimeTotal; }

    /**
     * Set casting state.
     * Fires CastingStateChanged event if changed.
     */
    void setCasting(bool casting, uint32_t spellId = SPELL_ID_UNKNOWN,
                    uint16_t targetId = 0, uint32_t castTimeMs = 0);

    /**
     * Update cast progress.
     * @param remainingMs Remaining cast time in milliseconds
     */
    void updateCastProgress(uint32_t remainingMs);

    /**
     * Clear casting state.
     */
    void clearCasting();

    // ========== Memorization State ==========

    /**
     * Check if currently memorizing a spell.
     */
    bool isMemorizing() const { return m_isMemorizing; }

    /**
     * Get the gem slot being memorized to.
     */
    uint8_t memorizingGemSlot() const { return m_memorizingGemSlot; }

    /**
     * Get the spell ID being memorized.
     */
    uint32_t memorizingSpellId() const { return m_memorizingSpellId; }

    /**
     * Get memorization progress (0.0 = just started, 1.0 = complete).
     */
    float memorizeProgress() const;

    /**
     * Set memorization state.
     */
    void setMemorizing(bool memorizing, uint8_t gemSlot = 0,
                       uint32_t spellId = SPELL_ID_UNKNOWN, uint32_t totalMs = 0);

    /**
     * Update memorization progress.
     */
    void updateMemorizeProgress(uint32_t remainingMs);

    // ========== Spellbook Summary ==========

    /**
     * Get count of spells scribed in spellbook.
     */
    uint16_t scribedSpellCount() const { return m_scribedSpellCount; }

    /**
     * Check if a specific spell is scribed.
     */
    bool hasSpellScribed(uint32_t spellId) const;

    /**
     * Set spellbook summary.
     * @param count Number of spells scribed
     */
    void setScribedSpellCount(uint16_t count);

    /**
     * Mark a spell as scribed (for quick lookup).
     * Limited to tracking a subset of spells.
     */
    void addScribedSpell(uint32_t spellId);

    /**
     * Clear scribed spell tracking.
     */
    void clearScribedSpells();

    // ========== Reset ==========

    /**
     * Reset all spell state.
     */
    void reset();

private:
    void fireSpellGemChangedEvent(uint8_t gemSlot);
    void fireCastingStateChangedEvent();

    EventBus* m_eventBus = nullptr;

    // Spell gem data
    struct GemData {
        uint32_t spellId = SPELL_ID_UNKNOWN;
        SpellGemState state = SpellGemState::Empty;
        uint32_t cooldownRemainingMs = 0;
        uint32_t cooldownTotalMs = 0;
    };
    std::array<GemData, SPELL_GEM_COUNT> m_gems;

    // Casting state
    bool m_isCasting = false;
    uint32_t m_castingSpellId = SPELL_ID_UNKNOWN;
    uint16_t m_castingTargetId = 0;
    uint32_t m_castTimeRemaining = 0;
    uint32_t m_castTimeTotal = 0;

    // Memorization state
    bool m_isMemorizing = false;
    uint8_t m_memorizingGemSlot = 0;
    uint32_t m_memorizingSpellId = SPELL_ID_UNKNOWN;
    uint32_t m_memorizeTimeRemaining = 0;
    uint32_t m_memorizeTimeTotal = 0;

    // Spellbook summary
    uint16_t m_scribedSpellCount = 0;

    // Quick lookup for recently used spells (limited set)
    static constexpr size_t MAX_TRACKED_SPELLS = 64;
    std::array<uint32_t, MAX_TRACKED_SPELLS> m_trackedScribedSpells;
    size_t m_trackedSpellCount = 0;
};

} // namespace state
} // namespace eqt
