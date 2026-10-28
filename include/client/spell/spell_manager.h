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
namespace eqt { namespace bridge { class GameStateBridge; } }

namespace EQT {

/*
 * SpellActionInfo -- IN-MEMORY DTO, NOT A WIRE STRUCT.
 *
 * This was `EQT::Action_Struct` in include/common/packet_structs.h until
 * T147 (2026-08-19). It never was a wire layout: the Titanium OP_Action
 * packet is 31 bytes (eqemu common/patches/titanium_structs.h:1120 ==
 * common/eq_packet_structs.h:1310, uint16 target/source/level/spell), while
 * this struct is 44 bytes of uint32s plus an invented `unknown_action`. It
 * carried authoritative-looking byte-offset comments the whole time, sitting
 * in a header whose banner says every struct in it matches the network
 * protocol -- a reinterpret_cast waiting to happen.
 *
 * The real wire parse lives in EverQuest::ZoneProcessAction
 * (src/client/eq_combat_actions.cpp), which declares a private, correct,
 * 31-byte `ActionPacket_Struct` and hand-copies the fields into this DTO.
 * Keep it that way: if you need the wire layout, use the one at the parse
 * site; do not make this struct "wire-accurate", because that recreates the
 * duplicated-then-drifted defect this arc exists to remove.
 */
struct SpellActionInfo {
    uint32_t target;
    uint32_t source;
    uint32_t level;
    uint32_t instrument_mod;
    float    force;
    float    hit_heading;
    float    hit_pitch;
    uint32_t type;
    uint32_t spell;
    uint8_t  level2;         // caster level as reported in the action packet
    uint8_t  effect_flag;    // 4 == a buff icon is made
};

} // namespace EQT

namespace EQ {

class SpellManager {
public:
    explicit SpellManager(EverQuest* eq);
    ~SpellManager();

    // ========================================================================
    // Initialization
    // ========================================================================

    // Initialize spell database from WilleqDatabase (SQLite-backed)
    bool initialize();

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

    // ========================================================================
    // Update (call every frame)
    // ========================================================================

    void update(float delta_time);

    // ========================================================================
    // Casting - Player Initiated
    // ========================================================================

    // Begin casting from a spell gem (0-based; only the first
    // EQT::VISIBLE_SPELL_GEM_COUNT are castable -- T483). B603 deleted
    // beginCastById(): nothing called it (the hotbar spell search calls
    // beginCastFromGem directly -- T483).
    CastResult beginCastFromGem(uint8_t gem_slot, uint16_t target_id = 0);

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
    // (defined in src/client/spell/spell_gems.cpp)
    // ========================================================================

    // Memorize a spell into a gem slot (0-7)
    bool memorizeSpell(uint32_t spell_id, uint8_t gem_slot);

    // Forget (clear) a spell from a gem slot
    bool forgetSpell(uint8_t gem_slot);

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
    // (defined in src/client/spell/spell_gems.cpp)
    // ========================================================================

    // Scribe a spell to spellbook (from server data or scroll)
    bool scribeSpell(uint32_t spell_id, uint16_t book_slot);

    // Check if a spell is scribed
    bool hasSpellScribed(uint32_t spell_id) const;

    // Get spell ID at book slot
    uint32_t getSpellbookSpell(uint16_t slot) const;

    // B1076 fix-2 (UIGAP-ORACLE 205): exchange the two spells currently in
    // the given spellbook slots, in the model. The caller (the
    // SwapSpellbookSlotsIntent consumer) sends OP_SwapSpell; this is the
    // client's own copy of the exchange the server makes. Both slots must
    // be in range, and at least one must be occupied (an empty slot is
    // SPELL_UNKNOWN or 0); swapping two empties is a no-op. Returns false
    // on any out-of-range slot.
    bool swapSpellbookSlots(uint16_t slot_a, uint16_t slot_b);

    // Get all scribed spell IDs
    std::vector<uint32_t> getScribedSpells() const;

    // T276: the first free spellbook slot, or -1 when all 400 are taken.
    // A slot is free when it holds SPELL_UNKNOWN (0xFFFFFFFF) or 0 -- the
    // PlayerProfile spellbook uses BOTH sentinels for "empty" (the client's own
    // scribed-count loop skips both), and the array is filled with
    // SPELL_UNKNOWN before any profile arrives. Returns int32_t, not
    // uint16_t, so "none free" is representable.
    int32_t findFreeSpellbookSlot() const;

    // Set entire spellbook (from PlayerProfile)
    void setSpellbook(const uint32_t* spells, size_t count);

    // Set spell gems from PlayerProfile
    void setSpellGems(const uint32_t* gems, size_t count);

    // T404 (board B254): the SERVER's own OP_MemorizeSpell response for a gem
    // slot. `scribing` is the wire field verbatim -- 1 = memorise, 2 =
    // un-memorise, 0 = a scribe-to-spellbook confirmation this does not
    // handle. Before T404 no arm of the response handler touched a gem at
    // all, so a server-driven gem change (#memspell, and any future
    // server-side memorisation) left the bar showing -- and CASTING -- the
    // spell that used to be there. Applied in ARRIVAL ORDER: eqemu sends the
    // un-memorise for the occupied slot and then the memorise, and each
    // packet is one edit, so the order takes care of itself.
    void applyServerGemUpdate(uint32_t gem_slot, uint32_t spell_id, uint32_t scribing);

    // ========================================================================
    // Packet Handlers (called by EverQuest class)
    // (defined in src/client/spell/spell_packet_handlers.cpp)
    // ========================================================================

    // Server confirms cast is starting (HC_OP_BeginCast)
    void handleBeginCast(uint16_t caster_id, uint16_t spell_id, uint32_t cast_time_ms);
    // B1170 (#489): the server's 0x4033 missile object message (the payload
    // after the opcode). Decoded here, drawn by the renderer's SpellVisualFX.
    void handleMissileObject(const uint8_t* payload, size_t len);

    // Spell effect/action (HC_OP_Action) - spell landing
    void handleAction(const EQT::SpellActionInfo& action);

    // Cast interrupted (HC_OP_InterruptCast). T469 (board B604): the wire
    // struct is eqemu's InterruptCast_Struct {uint32 spawnid; uint32
    // messageid; char message[];} -- it carries NO spell id, so the spell is
    // resolved here from our own cast (caster == us) or from the target-cast
    // tracking (caster == the target whose BeginCast we saw). `message_id` is
    // an eqstr id (INTERRUPT_SPELL 439 to the caster, INTERRUPT_SPELL_OTHER
    // 12478 to bystanders, the song variants for bards); `caster_name` is the
    // NUL-terminated name only the bystander copy carries (empty otherwise).
    // Before T469 zone_dispatch.cpp listed the opcode as ignored and this had
    // only test callers.
    void handleInterrupt(uint16_t caster_id, uint32_t message_id, const std::string& caster_name);

    // Mana change (HC_OP_ManaChange). T469: `keepcasting == 0` on a cast the
    // server has NOT yet confirmed with BeginCast is eqemu's StopCasting() --
    // a fizzle or a refused cast, which sends no OP_InterruptCast at all --
    // and ends the cast as Interrupted.
    // T482 (board B631): after BeginCast the same bytes end the cast too --
    // eqemu sends them on a success (SendSpellBarEnable) AND on a stop
    // (StopCasting), and in both cases they mean "the spell bar is usable
    // again" -- so the cast bar and the gem are released at once, and only the
    // OUTCOME waits for the server's next word: handleSpellBarRefresh() for a
    // success, noteZonePacket() for anything else.
    void handleManaChange(uint32_t new_mana, uint32_t stamina, uint32_t spell_id, uint8_t keepcasting);

    // OP_MemorizeSpell with scribing 3 (memSpellSpellbar). T482: the last
    // packet of eqemu's successful-cast tail (CastedSpellFinished,
    // zone/spells.cpp:1803 for a bard song, :1824 otherwise) and the Titanium
    // client's gem-refresh trigger. A stop never sends it. It completes our
    // cast as a Success and starts the gem's recast; a refresh for a gem
    // holding `spell_id` starts that gem's recast even when no cast is pending
    // (since B760 an unreliable packet can no longer settle the outcome first;
    // the cast may still have ended on the duration + 5 s fallback).
    void handleSpellBarRefresh(uint32_t slot, uint32_t spell_id);

    // T482: called by the zone dispatcher BEFORE each packet's arm. While a
    // confirmed cast's outcome is pending (keepcasting 0 seen, MemorizeSpell
    // not yet), the next RELIABLE packet other than the tail's own
    // (OP_MemorizeSpell, OP_LinkedReuse) means the server did not send the
    // success tail: the outcome is published as Interrupted (StopCasting).
    // B760 (issue #45): an unreliable packet (`reliable` false,
    // DaybreakConnection::DeliveringReliable()) is outside the ordered stream
    // and never settles the outcome. A replayed packet counts as reliable.
    void noteZonePacket(uint16_t opcode, bool reliable = true);

    // ========================================================================
    // Database Access
    // ========================================================================

    // D20e1: Bridge for visual events (set by EverQuest::InitGraphics)
    void setBridge(eqt::bridge::GameStateBridge* bridge) { m_bridge = bridge; }

    const SpellDatabase& getDatabase() const { return m_spell_db; }
    SpellDatabase& getDatabase() { return m_spell_db; }

    // Convenience spell lookup
    const SpellData* getSpell(uint32_t spell_id) const { return m_spell_db.getSpell(spell_id); }

private:
    // ========================================================================
    // Target Adjustment  (defined in src/client/spell/spell_cast_validation.cpp)
    // ========================================================================

    // Adjust target based on spell target type restrictions
    // Returns adjusted target_id (may change to self, pet, etc.)
    // Returns 0 if no valid target available (e.g., pet spell without pet)
    uint16_t adjustTargetForSpellType(const SpellData& spell, uint16_t target_id) const;

    // Check if target matches creature type restrictions (Animal, Undead, etc.)
    bool isValidCreatureTarget(const SpellData& spell, uint16_t target_id) const;

    // ========================================================================
    // Cast Validation
    //
    // T407: validateCast / checkMana / checkEndurance / checkRange are defined
    // in src/client/spell/spell_cast_validation.cpp; checkLineOfSight /
    // checkPlayerState in src/client/spell/spell_cast_checks.cpp (T246).
    // B1140: no reagent check -- components are judged by the server.
    // ========================================================================

    CastResult validateCast(const SpellData& spell, uint16_t target_id);
    bool checkMana(const SpellData& spell) const;
    // T355: the mana check's sibling. SpellData::endurance_cost was loaded
    // by nothing until T355, so every discipline read as free here.
    bool checkEndurance(const SpellData& spell) const;
    bool checkRange(const SpellData& spell, uint16_t target_id) const;
    // T246: these were `return true` stubs until T246, which made T244's
    // Stunned / Silenced / NoLineOfSight refusals unreachable. See the
    // definitions for exactly what each one checks and what it
    // deliberately leaves to the server.
    bool checkLineOfSight(uint16_t target_id) const;
    // Success, or the first of Stunned / Silenced / Feared the player's
    // buff list shows (a Mez buff reports as Stunned).
    CastResult checkPlayerState() const;

    // ========================================================================
    // Packet Sending  (defined in src/client/spell/spell_publish.cpp, beside
    // the three bridge publishers below)
    // ========================================================================

    void sendCastSpellPacket(uint32_t spell_id, uint8_t gem_slot, uint16_t target_id);
    void sendInterruptPacket();
    void sendMemorizePacket(uint32_t spell_id, uint8_t gem_slot, bool memorize);

    // ========================================================================
    // Internal State Management
    // ========================================================================

    void completeCast(bool success);
    // T469: the server ended our own cast (OP_InterruptCast, or StopCasting's
    // ManaChange). Cancels the cast visuals, frees the gem (eqemu starts no
    // recast on an interrupt), publishes SpellCastComplete with `result`.
    void endOwnCastByServer(CastResult result);
    // T482: keepcasting 0 after BeginCast. Releases the cast bar, cast glow
    // and gem now, and leaves the outcome pending (m_outcome_pending).
    void releaseConfirmedCast();
    // T482: publish the pending outcome (callback + SpellCastComplete).
    // Success also starts the gem's recast. No-op when nothing is pending.
    void resolvePendingOutcome(CastResult result);
    // T482: SpellCastVisualComplete for our own cast with isCastAnimation
    // false -- ends the cast bar and glow, paints nothing.
    void publishOwnCastReleased();
    void startGemCooldown(uint8_t gem_slot, uint32_t duration_ms);
    void publishGemChanged(uint8_t gem_slot);  // U06b: push to bridge
    // T44: push SpellCastStarted/SpellCastComplete to the bridge so
    // GameStateSyncBridge can reconstruct casting state without polling this
    // class. publishCastStarted() is called at the moment our own cast
    // begins locally (both gem and item/scroll casts); publishCastComplete()
    // at every place m_is_casting flips back to false for our own cast
    // (success, interrupt, fizzle).
    void publishCastStarted(uint32_t spell_id, uint16_t target_id, uint32_t cast_time_ms);
    void publishCastComplete(uint32_t spell_id, CastResult result);
    void updateGemCooldowns(float delta_time);
    void updateMemorization(float delta_time);
    uint32_t calculateMemorizeTime(uint32_t spell_id) const;

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
    eqt::bridge::GameStateBridge* m_bridge = nullptr;  // D20e1
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

    // T482: a confirmed cast the server has released (keepcasting 0) whose
    // outcome -- success tail or StopCasting -- is not yet known.
    bool m_outcome_pending = false;
    uint32_t m_pending_spell_id = SPELL_UNKNOWN;
    uint8_t m_pending_gem_slot = 0xFF;

    // ------------------------------------------------------------------------
    // Spell Gems: EQT::SPELL_GEM_COUNT (9) stored; the first
    // EQT::VISIBLE_SPELL_GEM_COUNT (8) shown and offered (T483)
    // ------------------------------------------------------------------------

    std::array<uint32_t, EQT::SPELL_GEM_COUNT> m_spell_gems;
    std::array<GemState, EQT::SPELL_GEM_COUNT> m_gem_states;
    std::array<std::chrono::steady_clock::time_point, EQT::SPELL_GEM_COUNT> m_gem_cooldown_start;
    std::array<uint32_t, EQT::SPELL_GEM_COUNT> m_gem_cooldown_duration;

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
};

} // namespace EQ

#endif // EQT_SPELL_MANAGER_H
