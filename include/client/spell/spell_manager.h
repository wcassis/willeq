/*
 * WillEQ Spell Manager
 *
 * Manages spell casting, memorization, and gem states.
 * Coordinates between player input, spell database, and server communication.
 */

#ifndef EQT_SPELL_MANAGER_H
#define EQT_SPELL_MANAGER_H

#include "spell_data.h"
#include "spell_database.h"
#include "spell_constants.h"
#include "common/packet_structs.h"
#include <chrono>
#include <array>
#include <functional>
#include <string>

// Forward declarations
class EverQuest;

namespace EQ {

// Callback types for spell events
using SpellCastCallback = std::function<void(CastResult result, uint32_t spell_id)>;
using SpellLandCallback = std::function<void(uint32_t spell_id, uint16_t caster_id, uint16_t target_id)>;
using SpellFadeCallback = std::function<void(uint32_t spell_id, uint16_t entity_id)>;

class SpellManager {
public:
    explicit SpellManager(EverQuest* eq);
    ~SpellManager();

    // ========================================================================
    // Initialization
    // ========================================================================

    // Initialize spell database from EQ client path
    bool initialize(const std::string& eq_client_path);

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

    // ========================================================================
    // Update (call every frame)
    // ========================================================================

    void update(float delta_time);

    // ========================================================================
    // Casting - Player Initiated
    // ========================================================================

    // Begin casting from a spell gem (1-8)
    CastResult beginCastFromGem(uint8_t gem_slot, uint16_t target_id = 0);

    // Begin casting by spell ID directly (for scripted/item casts)
    CastResult beginCastById(uint32_t spell_id, uint16_t target_id = 0);

    // Interrupt current cast
    void interruptCast();

    // Check if currently casting
    bool isCasting() const { return m_is_casting; }

    // Get current cast info
    uint32_t getCurrentSpellId() const { return m_current_spell_id; }
    uint16_t getCurrentTargetId() const { return m_current_target_id; }
    float getCastProgress() const;  // 0.0 to 1.0
    uint32_t getCastTimeRemaining() const;  // milliseconds

    // ========================================================================
    // Spell Gems
    // ========================================================================

    // Memorize a spell into a gem slot (0-7)
    bool memorizeSpell(uint32_t spell_id, uint8_t gem_slot);

    // Forget (clear) a spell from a gem slot
    bool forgetSpell(uint8_t gem_slot);

    // Swap two gem slots
    bool swapGems(uint8_t slot1, uint8_t slot2);

    // Get spell ID in a gem slot (returns SPELL_UNKNOWN if empty)
    uint32_t getMemorizedSpell(uint8_t gem_slot) const;

    // Get gem state
    GemState getGemState(uint8_t gem_slot) const;

    // Get cooldown progress (0.0 = just started, 1.0 = ready)
    float getGemCooldownProgress(uint8_t gem_slot) const;

    // Get remaining cooldown in milliseconds
    uint32_t getGemCooldownRemaining(uint8_t gem_slot) const;

    // Get memorization progress (0.0 = just started, 1.0 = complete)
    float getMemorizeProgress(uint8_t gem_slot) const;

    // Check if memorizing
    bool isMemorizing() const { return m_is_memorizing; }

    // ========================================================================
    // Spellbook
    // ========================================================================

    // Scribe a spell to spellbook (from server data or scroll)
    bool scribeSpell(uint32_t spell_id, uint16_t book_slot);

    // Check if a spell is scribed
    bool hasSpellScribed(uint32_t spell_id) const;

    // Get spell ID at book slot
    uint32_t getSpellbookSpell(uint16_t slot) const;

    // Get all scribed spell IDs
    std::vector<uint32_t> getScribedSpells() const;

    // Set entire spellbook (from PlayerProfile)
    void setSpellbook(const uint32_t* spells, size_t count);

    // Set spell gems from PlayerProfile
    void setSpellGems(const uint32_t* gems, size_t count);

    // ========================================================================
    // Packet Handlers (called by EverQuest class)
    // ========================================================================

    // Server confirms cast is starting (HC_OP_BeginCast)
    void handleBeginCast(uint16_t caster_id, uint16_t spell_id, uint32_t cast_time_ms);

    // Spell effect/action (HC_OP_Action) - spell landing
    void handleAction(const EQT::Action_Struct& action);

    // Cast interrupted (HC_OP_InterruptCast or damage during cast)
    void handleInterrupt(uint16_t caster_id, uint16_t spell_id, uint8_t message_type);

    // Mana change (HC_OP_ManaChange)
    void handleManaChange(uint32_t new_mana, uint32_t stamina, uint32_t spell_id);

    // ========================================================================
    // Callbacks
    // ========================================================================

    void setOnCastComplete(SpellCastCallback cb) { m_on_cast_complete = std::move(cb); }
    void setOnSpellLand(SpellLandCallback cb) { m_on_spell_land = std::move(cb); }
    void setOnSpellFade(SpellFadeCallback cb) { m_on_spell_fade = std::move(cb); }

    // ========================================================================
    // Database Access
    // ========================================================================

    const SpellDatabase& getDatabase() const { return m_spell_db; }
    SpellDatabase& getDatabase() { return m_spell_db; }

    // Convenience spell lookup
    const SpellData* getSpell(uint32_t spell_id) const { return m_spell_db.getSpell(spell_id); }

private:
    // ========================================================================
    // Target Adjustment
    // ========================================================================

    // Adjust target based on spell target type restrictions
    // Returns adjusted target_id (may change to self, pet, etc.)
    // Returns 0 if no valid target available (e.g., pet spell without pet)
    uint16_t adjustTargetForSpellType(const SpellData& spell, uint16_t target_id) const;

    // Check if target matches creature type restrictions (Animal, Undead, etc.)
    bool isValidCreatureTarget(const SpellData& spell, uint16_t target_id) const;

    // ========================================================================
    // Cast Validation
    // ========================================================================

    CastResult validateCast(const SpellData& spell, uint16_t target_id);
    bool checkMana(const SpellData& spell) const;
    bool checkRange(const SpellData& spell, uint16_t target_id) const;
    bool checkLineOfSight(uint16_t target_id) const;
    bool checkReagents(const SpellData& spell) const;
    bool checkPlayerState() const;  // Not stunned, feared, etc.

    // ========================================================================
    // Packet Sending
    // ========================================================================

    void sendCastSpellPacket(uint32_t spell_id, uint8_t gem_slot, uint16_t target_id);
    void sendInterruptPacket();
    void sendMemorizePacket(uint32_t spell_id, uint8_t gem_slot, bool memorize);

    // ========================================================================
    // Internal State Management
    // ========================================================================

    void completeCast(bool success);
    void startGemCooldown(uint8_t gem_slot, uint32_t duration_ms);
    void updateGemCooldowns(float delta_time);
    void updateMemorization(float delta_time);
    uint32_t calculateMemorizeTime(uint32_t spell_id) const;
    uint8_t findGemSlotForSpell(uint32_t spell_id) const;

    // Spell message display
    void showSpellMessages(const SpellData* spell, uint16_t caster_id,
                           uint16_t target_id, bool resisted);
    void showBeginCastMessage(uint16_t caster_id, const SpellData* spell);

    // NPC casting tracking
    uint16_t m_target_caster_id = 0;       // Entity ID of target currently casting
    uint32_t m_target_spell_id = SPELL_UNKNOWN;  // Spell target is casting

    // ========================================================================
    // Data Members
    // ========================================================================

    EverQuest* m_eq;
    SpellDatabase m_spell_db;
    bool m_initialized = false;

    // ------------------------------------------------------------------------
    // Casting State
    // ------------------------------------------------------------------------

    bool m_is_casting = false;
    uint32_t m_current_spell_id = SPELL_UNKNOWN;
    uint16_t m_current_target_id = 0;
    uint8_t m_current_gem_slot = 0;
    std::chrono::steady_clock::time_point m_cast_start_time;
    uint32_t m_cast_duration_ms = 0;
    bool m_waiting_for_server_confirm = false;

    // ------------------------------------------------------------------------
    // Spell Gems (8 slots for Classic/Velious)
    // ------------------------------------------------------------------------

    std::array<uint32_t, MAX_SPELL_GEMS> m_spell_gems;
    std::array<GemState, MAX_SPELL_GEMS> m_gem_states;
    std::array<std::chrono::steady_clock::time_point, MAX_SPELL_GEMS> m_gem_cooldown_start;
    std::array<uint32_t, MAX_SPELL_GEMS> m_gem_cooldown_duration;

    // ------------------------------------------------------------------------
    // Memorization State
    // ------------------------------------------------------------------------

    bool m_is_memorizing = false;
    uint8_t m_memorize_slot = 0;
    uint32_t m_memorize_spell_id = SPELL_UNKNOWN;
    std::chrono::steady_clock::time_point m_memorize_start_time;
    uint32_t m_memorize_duration_ms = 0;

    // ------------------------------------------------------------------------
    // Spellbook (400 slots)
    // ------------------------------------------------------------------------

    std::array<uint32_t, MAX_SPELLBOOK_SLOTS> m_spellbook;

    // ------------------------------------------------------------------------
    // Callbacks
    // ------------------------------------------------------------------------

    SpellCastCallback m_on_cast_complete;
    SpellLandCallback m_on_spell_land;
    SpellFadeCallback m_on_spell_fade;
};

} // namespace EQ

#endif // EQT_SPELL_MANAGER_H
