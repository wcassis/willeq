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
            if (m_current_gem_slot < EQT::SPELL_GEM_COUNT) {
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

    // Last resort for a confirmed cast whose end packets never arrived.
    // T482 (board B631): every way eqemu ends a confirmed cast sends a packet
    // -- the success tail (ManaChange keepcasting 0 + OP_MemorizeSpell
    // memSpellSpellbar, spells.cpp:1817-1824, handled by handleManaChange /
    // handleSpellBarRefresh), StopCasting's ManaChange, or InterruptSpell's
    // OP_InterruptCast -- so reaching this means a packet was lost. It used to
    // call completeCast(true) and paint a synthetic impact: a stopped cast was
    // reported as a success with its recast started. It now claims nothing.
    if (m_is_casting && !m_waiting_for_server_confirm) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_cast_start_time).count();

        uint32_t timeout = m_cast_duration_ms + 5000;
        if (elapsed > timeout) {
            LOG_WARN(MOD_SPELL, "Cast of spell {} got no end packet from the server after {}ms "
                "(expected {}ms); ending it without a result", m_current_spell_id, elapsed,
                m_cast_duration_ms);
            endOwnCastByServer(CastResult::Interrupted);
        }
    }
}

void SpellManager::updateGemCooldowns(float delta_time)
{
    auto now = std::chrono::steady_clock::now();

    for (uint8_t i = 0; i < EQT::SPELL_GEM_COUNT; ++i) {
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
    // T483 (B770 option (a)): the official client offers no ninth gem.
    if (gem_slot >= EQT::VISIBLE_SPELL_GEM_COUNT) {
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

    // T482: a previous cast whose outcome is still pending is not our next
    // cast's; publish it before this one's state replaces it.
    resolvePendingOutcome(CastResult::Interrupted);

    // Set up casting state
    m_is_casting = true;
    m_current_spell_id = spell_id;
    m_current_target_id = target_id;
    m_current_gem_slot = gem_slot;
    m_cast_start_time = std::chrono::steady_clock::now();
    m_cast_duration_ms = spell->cast_time_ms;
    m_waiting_for_server_confirm = true;

    // B1183 (#502): the client's cast-start path (the actor's animation-set,
    // FUN_00450c2f eqgame.exe.c:71181-71618) puts the actor into the cast
    // state -- a DIRECT write of 100 (standing) on the actor's +0x251
    // (71254), gated: a lying ('s') or corpse ('x') actor is left alone
    // (71225). This is the write the B1174 cast-anim gate relies on (a
    // casting already is already in its BeginCast state, byte 100).
    {
        auto& entities = m_eq->GetEntities();
        auto self = entities.find(m_eq->GetEntityID());
        if (self != entities.end() && self->second.stand_state != 115 &&
            self->second.stand_state != 120) {
            self->second.stand_state.apply(eqt::state::StandValue::Standing);
        }
    }

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
    if (m_current_gem_slot < EQT::SPELL_GEM_COUNT) {
        m_gem_states[m_current_gem_slot] = GemState::Ready;
        publishGemChanged(m_current_gem_slot);
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
// checkPlayerState in spell_cast_checks.cpp (same class, own TUs).

// T407: the gem bar and the spellbook -- memorizeSpell / forgetSpell /
// startGemCooldown / scribeSpell / setSpellbook /
// applyServerGemUpdate and their accessors -- live in spell_gems.cpp
// (same class, own TU; swapGems went in B603 with the rest of the
// unreached control API).

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
        if (m_current_gem_slot < EQT::SPELL_GEM_COUNT && spell) {
            startGemCooldown(m_current_gem_slot, spell->recast_time_ms);
        }

        LOG_INFO(MOD_SPELL, "Cast of '{}' completed successfully",
            spell ? spell->name : "Unknown");
    }

    // T44: this is the choke point for our own cast's success path. Since T482
    // its production caller is handleSpellBarRefresh() for a bard song (the
    // non-bard success resolves through resolvePendingOutcome()); handleAction()
    // and update()'s timeout no longer call it. success is the only value
    // passed by any current caller; the false branch is kept honest anyway
    // rather than silently dropping the event.
    publishCastComplete(m_current_spell_id, success ? CastResult::Success : CastResult::Interrupted);

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

} // namespace EQ
