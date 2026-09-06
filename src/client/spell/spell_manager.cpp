/*
 * WillEQ Spell Manager Implementation
 */

#include "client/spell/spell_manager.h"
#include "client/spell/spell_body_type_target.h"
#include "client/eq.h"
#include "client/combat.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "common/net/packet.h"
#include "common/logging.h"
#include "common/name_utils.h"
#include <algorithm>
#include <cmath>

namespace EQ {

// ============================================================================
// Constructor / Destructor
// ============================================================================

SpellManager::SpellManager(EverQuest* eq)
    : m_eq(eq)
{
    // Initialize spell gems to empty
    m_spell_gems.fill(SPELL_UNKNOWN);
    m_gem_states.fill(GemState::Empty);
    m_gem_cooldown_duration.fill(0);

    // Initialize spellbook to empty
    m_spellbook.fill(SPELL_UNKNOWN);
}

SpellManager::~SpellManager() = default;

// ============================================================================
// Initialization
// ============================================================================

bool SpellManager::initialize()
{
    if (!m_spell_db.initFromDatabase()) {
        LOG_ERROR(MOD_SPELL, "Failed to initialize spell database from SQLite: {}",
                  m_spell_db.getLoadError());
        return false;
    }

    LOG_INFO(MOD_SPELL, "Spell database initialized from SQLite ({} spells)", m_spell_db.getSpellCount());
    m_initialized = true;
    return true;
}

// ============================================================================
// Update
// ============================================================================

void SpellManager::update(float delta_time)
{
    // Update gem cooldowns
    updateGemCooldowns(delta_time);

    // Update memorization progress
    updateMemorization(delta_time);

    // Check if cast has timed out (server didn't respond)
    if (m_is_casting && m_waiting_for_server_confirm) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_cast_start_time).count();

        // If waiting more than 5 seconds for server confirm, something went wrong
        if (elapsed > 5000) {
            LOG_WARN(MOD_SPELL, "Cast timeout waiting for server confirmation");
            m_is_casting = false;
            m_waiting_for_server_confirm = false;
            if (m_current_gem_slot < MAX_SPELL_GEMS) {
                m_gem_states[m_current_gem_slot] = GemState::Ready;
                publishGemChanged(m_current_gem_slot);
            }
            // T44 (found live, not by code reading): this is a fourth place
            // m_is_casting flips back to false for our own cast -- server
            // never even echoed BeginCast (e.g. an invalid target the server
            // silently drops rather than rejecting with an Interrupt
            // packet). Without this, GameStateSyncBridge would have no way
            // to learn the cast ended and SpellState would show "casting"
            // forever. Missing this is exactly the kind of gap the plan
            // asked to be found before converting, not papered over.
            publishCastComplete(m_current_spell_id, CastResult::Interrupted);
        }
    }

    // Check if cast has been ongoing too long without completing
    // Some spells (like pet summons) may not send an Action packet
    if (m_is_casting && !m_waiting_for_server_confirm) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_cast_start_time).count();

        // Allow cast duration + 5 second buffer for server response
        uint32_t timeout = m_cast_duration_ms + 5000;
        if (elapsed > timeout) {
            LOG_WARN(MOD_SPELL, "Cast of spell {} timed out after {}ms (expected {}ms)",
                m_current_spell_id, elapsed, m_cast_duration_ms);

            // Complete the cast (assume success for spells like pet summons)
            completeCast(true);

            // D20e1: Signal visual completion via bridge
            if (m_bridge) {
                eqt::state::SpellCastVisualCompleteData d;
                d.casterId = m_eq->GetEntityID();
                d.targetId = m_current_target_id;
                d.spellId = m_current_spell_id;
                d.isSuccess = true;
                // T395 (board B204): no OP_Action ever arrived on this path --
                // that is what "timed out" means -- so this synthetic
                // completion is the only chance to paint the cast. Arm the FX
                // here the animation packet would have.
                d.isCastAnimation = true;
                d.isPlayerCast = true;
                d.isTargetCast = false;
                // T381 (B160): the timeout path publishes the same completion
                // event the server's success packet does, so it must carry the
                // same vocabulary or the impact burst loses its colour.
                if (const SpellData* timed_out = m_spell_db.getSpell(m_current_spell_id)) {
                    d.spellVisual = timed_out->visualVocabulary();
                }
                m_bridge->pushEvent(eqt::state::GameEvent(
                    eqt::state::GameEventType::SpellCastVisualComplete, std::move(d)));
            }
        }
    }
}

void SpellManager::updateGemCooldowns(float delta_time)
{
    auto now = std::chrono::steady_clock::now();

    for (uint8_t i = 0; i < MAX_SPELL_GEMS; ++i) {
        if (m_gem_states[i] == GemState::Refresh) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_gem_cooldown_start[i]).count();

            if (elapsed >= m_gem_cooldown_duration[i]) {
                m_gem_states[i] = GemState::Ready;
                publishGemChanged(i);
                LOG_DEBUG(MOD_SPELL, "Gem {} cooldown complete", i + 1);
            }
        }
    }
}

void SpellManager::updateMemorization(float delta_time)
{
    if (!m_is_memorizing) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_memorize_start_time).count();

    if (elapsed >= m_memorize_duration_ms) {
        // Memorization complete
        m_spell_gems[m_memorize_slot] = m_memorize_spell_id;
        m_gem_states[m_memorize_slot] = GemState::Ready;
        m_is_memorizing = false;

        const SpellData* spell = m_spell_db.getSpell(m_memorize_spell_id);
        LOG_INFO(MOD_SPELL, "Memorized spell '{}' in gem {}",
            spell ? spell->name : "Unknown", m_memorize_slot + 1);

        publishGemChanged(m_memorize_slot);

        // D20e1: Signal memorize completion via bridge
        if (m_bridge) {
            m_bridge->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SpellMemorizeVisualComplete,
                eqt::state::SpellMemorizeVisualCompleteData{}));
        }
    }
}

// ============================================================================
// Casting - Player Initiated
// ============================================================================

CastResult SpellManager::beginCastFromGem(uint8_t gem_slot, uint16_t target_id)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return CastResult::InvalidSpell;
    }

    uint32_t spell_id = m_spell_gems[gem_slot];
    if (spell_id == SPELL_UNKNOWN) {
        return CastResult::NotMemorized;
    }

    // Check gem state
    GemState state = m_gem_states[gem_slot];
    LOG_DEBUG(MOD_SPELL, "beginCastFromGem: gem={} state={} (0=Empty,1=Ready,2=Casting,3=Refresh,4=Memorize)",
        gem_slot + 1, static_cast<int>(state));
    if (state == GemState::Casting) {
        LOG_DEBUG(MOD_SPELL, "beginCastFromGem: returning AlreadyCasting");
        return CastResult::AlreadyCasting;
    }
    if (state == GemState::Refresh) {
        LOG_DEBUG(MOD_SPELL, "beginCastFromGem: returning GemCooldown");
        return CastResult::GemCooldown;
    }
    if (state == GemState::MemorizeProgress) {
        LOG_DEBUG(MOD_SPELL, "beginCastFromGem: returning SpellNotReady");
        return CastResult::SpellNotReady;
    }
    if (state == GemState::Empty) {
        LOG_DEBUG(MOD_SPELL, "beginCastFromGem: returning NotMemorized");
        return CastResult::NotMemorized;
    }

    // Get spell data
    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (!spell) {
        return CastResult::InvalidSpell;
    }

    // Use current target if none specified
    if (target_id == 0) {
        target_id = m_eq->GetCombatManager()->GetTargetId();
        LOG_DEBUG(MOD_SPELL, "beginCastFromGem: no target specified, got target_id={} from CombatManager",
            target_id);
    }

    // Adjust target based on spell type (self-only, pet-only, group spells, etc.)
    uint16_t adjusted_target = adjustTargetForSpellType(*spell, target_id);
    if (adjusted_target != target_id) {
        LOG_DEBUG(MOD_SPELL, "beginCastFromGem: adjusted target from {} to {} for spell '{}'",
            target_id, adjusted_target, spell->name);
        target_id = adjusted_target;
    }

    // Check if target adjustment failed (pet spell without pet). T244: its
    // own result, so the refusal can say "You do not have a pet." instead
    // of the creature-type InvalidTarget's "can not affect this target".
    if (target_id == 0 && spell->target_type == SpellTargetType::Pet) {
        return CastResult::NoPet;
    }

    // Validate the cast
    CastResult result = validateCast(*spell, target_id);
    LOG_DEBUG(MOD_SPELL, "beginCastFromGem: validateCast returned {} for target_id={}",
        static_cast<int>(result), target_id);
    if (result != CastResult::Success) {
        return result;
    }

    // Set up casting state
    m_is_casting = true;
    m_current_spell_id = spell_id;
    m_current_target_id = target_id;
    m_current_gem_slot = gem_slot;
    m_cast_start_time = std::chrono::steady_clock::now();
    m_cast_duration_ms = spell->cast_time_ms;
    m_waiting_for_server_confirm = true;

    // Update gem state
    m_gem_states[gem_slot] = GemState::Casting;
    publishGemChanged(gem_slot);

    // T44: publish SpellCastStarted immediately, client-side, rather than
    // waiting for the server to echo BeginCast back to us. GameStateSyncBridge
    // needs this to reconstruct casting state without polling SpellManager,
    // and the poll it replaces read this state on the very next tick after
    // this local assignment -- deferring to the server echo
    // (ZoneProcessBeginCast, eq_player_buffs.cpp) alone would add a network round-trip of latency
    // the old poll never had.
    publishCastStarted(spell_id, target_id, m_cast_duration_ms);

    // Send cast packet to server
    sendCastSpellPacket(spell_id, gem_slot, target_id);

    LOG_INFO(MOD_SPELL, "Beginning cast of '{}' (ID: {}) on target {}",
        spell->name, spell_id, target_id);

    return CastResult::Success;
}

CastResult SpellManager::beginCastById(uint32_t spell_id, uint16_t target_id)
{
    // Find which gem has this spell
    uint8_t gem_slot = findGemSlotForSpell(spell_id);
    if (gem_slot < MAX_SPELL_GEMS) {
        return beginCastFromGem(gem_slot, target_id);
    }

    // Spell not memorized - check if we can cast it anyway (item/scroll)
    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (!spell) {
        return CastResult::InvalidSpell;
    }

    // For non-memorized casts, we still need to validate
    if (target_id == 0) {
        target_id = m_eq->GetCombatManager()->GetTargetId();
    }

    // Adjust target based on spell type (self-only, pet-only, group spells, etc.)
    uint16_t adjusted_target = adjustTargetForSpellType(*spell, target_id);
    if (adjusted_target != target_id) {
        LOG_DEBUG(MOD_SPELL, "beginCastById: adjusted target from {} to {} for spell '{}'",
            target_id, adjusted_target, spell->name);
        target_id = adjusted_target;
    }

    // Check if target adjustment failed (pet spell without pet). T244: its
    // own result, so the refusal can say "You do not have a pet." instead
    // of the creature-type InvalidTarget's "can not affect this target".
    if (target_id == 0 && spell->target_type == SpellTargetType::Pet) {
        return CastResult::NoPet;
    }

    CastResult result = validateCast(*spell, target_id);
    if (result != CastResult::Success) {
        return result;
    }

    // Set up casting state (no gem slot)
    m_is_casting = true;
    m_current_spell_id = spell_id;
    m_current_target_id = target_id;
    m_current_gem_slot = 0xFF;  // Invalid slot marker for item/scroll casts
    m_cast_start_time = std::chrono::steady_clock::now();
    m_cast_duration_ms = spell->cast_time_ms;
    m_waiting_for_server_confirm = true;

    // T44: see beginCastFromGem() -- same reasoning, no gem event to piggyback
    // on here since item/scroll casts don't touch a gem slot.
    publishCastStarted(spell_id, target_id, m_cast_duration_ms);

    // Send cast packet (slot 0xFF or use inventory slot for item casts)
    sendCastSpellPacket(spell_id, 0xFF, target_id);

    LOG_INFO(MOD_SPELL, "Beginning item/scroll cast of '{}' (ID: {})",
        spell->name, spell_id);

    return CastResult::Success;
}

void SpellManager::interruptCast()
{
    if (!m_is_casting) {
        return;
    }

    LOG_DEBUG(MOD_SPELL, "Player interrupting cast of spell {}", m_current_spell_id);

    // Send interrupt packet to server
    sendInterruptPacket();

    // Reset local state
    m_is_casting = false;
    m_waiting_for_server_confirm = false;

    // Reset gem state if it was a gem cast
    if (m_current_gem_slot < MAX_SPELL_GEMS) {
        m_gem_states[m_current_gem_slot] = GemState::Ready;
        publishGemChanged(m_current_gem_slot);
    }

    // Notify callback
    if (m_on_cast_complete) {
        m_on_cast_complete(CastResult::Interrupted, m_current_spell_id);
    }

    publishCastComplete(m_current_spell_id, CastResult::Interrupted);

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

float SpellManager::getCastProgress() const
{
    if (!m_is_casting || m_cast_duration_ms == 0) {
        return 0.0f;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_cast_start_time).count();

    return std::min(1.0f, static_cast<float>(elapsed) / m_cast_duration_ms);
}

uint32_t SpellManager::getCastTimeRemaining() const
{
    if (!m_is_casting) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_cast_start_time).count();

    if (elapsed >= m_cast_duration_ms) {
        return 0;
    }

    return m_cast_duration_ms - static_cast<uint32_t>(elapsed);
}

// T246/T407: the pre-cast decision layer -- adjustTargetForSpellType /
// isValidCreatureTarget / validateCast / checkMana / checkEndurance /
// checkRange live in spell_cast_validation.cpp, and checkLineOfSight /
// countCarriedItem / checkReagents / checkPlayerState in
// spell_cast_checks.cpp (same class, own TUs).

// T407: the gem bar and the spellbook -- memorizeSpell / forgetSpell /
// swapGems / startGemCooldown / scribeSpell / setSpellbook /
// applyServerGemUpdate and their accessors -- live in spell_gems.cpp
// (same class, own TU).

// T407: the four inbound-packet arms -- handleBeginCast / handleAction /
// handleInterrupt / handleManaChange -- and the two chat-message helpers
// they are the only callers of live in spell_packet_handlers.cpp (same
// class, own TU).

// T407: everything that leaves the class -- the three outbound packets
// (sendCastSpellPacket / sendInterruptPacket / sendMemorizePacket) and the
// three bridge publishers (publishCastStarted / publishCastComplete /
// publishGemChanged) -- lives in spell_publish.cpp (same class, own TU).

// ============================================================================
// Internal State Management
// ============================================================================

void SpellManager::completeCast(bool success)
{
    if (!m_is_casting) {
        return;
    }

    const SpellData* spell = m_spell_db.getSpell(m_current_spell_id);

    m_is_casting = false;
    m_waiting_for_server_confirm = false;

    if (success) {
        // Start gem cooldown
        if (m_current_gem_slot < MAX_SPELL_GEMS && spell) {
            startGemCooldown(m_current_gem_slot, spell->recast_time_ms);
        }

        // Notify callback
        if (m_on_cast_complete) {
            m_on_cast_complete(CastResult::Success, m_current_spell_id);
        }

        LOG_INFO(MOD_SPELL, "Cast of '{}' completed successfully",
            spell ? spell->name : "Unknown");
    }

    // T44: this is the choke point for our own cast's success path (called
    // from handleAction() on the server's success packet, and from update()'s
    // no-Action-packet timeout fallback for casts like pet summons). success
    // is the only value passed by any current caller; the false branch is
    // kept honest anyway rather than silently dropping the event.
    publishCastComplete(m_current_spell_id, success ? CastResult::Success : CastResult::Interrupted);

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

} // namespace EQ
