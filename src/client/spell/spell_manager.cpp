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

// ============================================================================
// Packet Handlers
// ============================================================================

void SpellManager::handleBeginCast(uint16_t caster_id, uint16_t spell_id, uint32_t cast_time_ms)
{
    const SpellData* spell = m_spell_db.getSpell(spell_id);
    std::string spell_name = spell ? spell->name : "Unknown";

    LOG_DEBUG(MOD_SPELL, "BeginCast: caster={}, spell='{}' ({}), time={}ms",
        caster_id, spell_name, spell_id, cast_time_ms);

    // If this is our cast, confirm it
    if (caster_id == m_eq->GetEntityID()) {
        if (m_is_casting && m_current_spell_id == spell_id) {
            // Spell gem cast confirmed by server
            m_waiting_for_server_confirm = false;
            m_cast_duration_ms = cast_time_ms;  // Use server's cast time
            m_cast_start_time = std::chrono::steady_clock::now();  // Reset timer

            LOG_INFO(MOD_SPELL, "Server confirmed cast of '{}'", spell_name);
        } else {
            // Item click or other cast source - start tracking it now
            m_is_casting = true;
            m_current_spell_id = spell_id;
            m_cast_duration_ms = cast_time_ms;
            m_cast_start_time = std::chrono::steady_clock::now();
            m_waiting_for_server_confirm = false;
            m_current_gem_slot = 255;  // No gem (item cast)

            LOG_INFO(MOD_SPELL, "Item/other cast started: '{}'", spell_name);
        }

        // D20e1: Signal cast visual start via bridge
        if (m_bridge) {
            eqt::state::SpellCastVisualStartedData d;
            d.casterId = caster_id;
            d.spellId = spell_id;
            d.castTimeMs = cast_time_ms;
            d.spellName = spell_name;
            d.casterName = "";
            d.isPlayerCast = true;
            d.isTargetCast = false;
            // T381 (B160): the renderer has no SpellDatabase and must not be
            // handed this one (game-thread-owned, unsynchronised LRU), so the
            // visual vocabulary is copied by value here.
            if (spell) d.spellVisual = spell->visualVocabulary();
            m_bridge->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SpellCastVisualStarted, std::move(d)));
        }
    } else {
        // NPC or other player casting
        // Show chat message
        if (spell) {
            showBeginCastMessage(caster_id, spell);
        }

        // Get caster name for target casting bar (converted to display format)
        std::string caster_name = "Unknown";
        const auto& entities = m_eq->GetEntities();
        auto it = entities.find(caster_id);
        if (it != entities.end()) {
            caster_name = EQT::toDisplayName(it->second.name);
        }

        // Check if this is our current target
        bool isTargetCast = false;
        CombatManager* combat = m_eq->GetCombatManager();
        if (combat && caster_id == combat->GetTargetId()) {
            m_target_caster_id = caster_id;
            m_target_spell_id = spell_id;
            isTargetCast = true;
        }

        // D20e1: Signal cast visual start via bridge (NPC/other player)
        if (m_bridge) {
            eqt::state::SpellCastVisualStartedData d;
            d.casterId = caster_id;
            d.spellId = spell_id;
            d.castTimeMs = cast_time_ms;
            d.spellName = spell_name;
            d.casterName = caster_name;
            d.isPlayerCast = false;
            d.isTargetCast = isTargetCast;
            if (spell) d.spellVisual = spell->visualVocabulary();  // T381 (B160)
            m_bridge->pushEvent(eqt::state::GameEvent(
                eqt::state::GameEventType::SpellCastVisualStarted, std::move(d)));
        }
    }
}

void SpellManager::handleAction(const EQT::SpellActionInfo& action)
{
    // Type 231 (0xE7) indicates spell effect
    if (action.type != 231) {
        return;
    }

    uint32_t spell_id = action.spell;
    uint16_t caster_id = static_cast<uint16_t>(action.source);
    uint16_t target_id = static_cast<uint16_t>(action.target);

    const SpellData* spell = m_spell_db.getSpell(spell_id);
    std::string spell_name = spell ? spell->name : "Unknown";

    LOG_DEBUG(MOD_SPELL, "SpellAction: caster={}, target={}, spell='{}' ({}), effect_flag={}",
        caster_id, target_id, spell_name, spell_id, action.effect_flag);

    // effect_flag meanings:
    // 0 = initial spell animation packet (first of two action packets)
    // 4 = success flag (second packet, causes buff icon to appear)
    // Resists are communicated via separate message packets, not effect_flag
    bool is_success_packet = (action.effect_flag == 4);

    // Determine completion animation for NPC casts
    std::string completion_anim;
    if (is_success_packet && spell && caster_id != m_eq->GetEntityID()) {
        if (spell->isDamageSpell() || spell->isDetrimental()) {
            completion_anim = "c03";
        } else if (spell->is_beneficial) {
            completion_anim = "c04";
        } else {
            completion_anim = "c01";
        }
    }

    // Check if this was our target's cast
    bool isTargetCast = (caster_id == m_target_caster_id && spell_id == m_target_spell_id);
    if (isTargetCast) {
        m_target_caster_id = 0;
        m_target_spell_id = SPELL_UNKNOWN;
    }

    bool isPlayerCast = (caster_id == m_eq->GetEntityID());

    // D20e1: Signal cast visual completion via bridge
    if (m_bridge) {
        eqt::state::SpellCastVisualCompleteData d;
        d.casterId = caster_id;
        d.targetId = target_id;
        d.spellId = spell_id;
        d.spellName = spell_name;
        d.isSuccess = is_success_packet;
        // T395 (board B204): the CAST-ANIMATION packet is what paints the
        // spell. eqemu queues effect_flag == 0 to the target, the caster AND
        // every client in range (Mob::SpellOnTarget: "this causes the caster
        // animation and the particles around the target"), then repeats the
        // packet with effect_flag == 4 -- to the caster and target ONLY --
        // once the spell lands, purely so a buff icon appears. Gating the
        // impact FX on isSuccess therefore made a bystander's spell visual
        // unreachable by construction, which is exactly what T386 measured.
        d.isCastAnimation = (action.effect_flag == 0);
        d.isPlayerCast = isPlayerCast;
        d.isTargetCast = isTargetCast;
        d.completionAnim = completion_anim;
        if (spell) d.spellVisual = spell->visualVocabulary();  // T381 (B160)
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SpellCastVisualComplete, std::move(d)));
    }

    // Display spell landing messages only on success packet
    if (spell && is_success_packet) {
        showSpellMessages(spell, caster_id, target_id, false);  // Not resisted
    }

    // If this is our cast completing
    if (isPlayerCast && m_is_casting) {
        completeCast(true);
    }

    // T357 (B061): the m_on_spell_land callback that used to fire here is
    // GONE. It had zero setters tree-wide, and this invocation sat OUTSIDE the
    // `spell && is_success_packet` gate above -- so it fired on the animation
    // packet as well, which is not a landing at all. Spell landing is
    // OP_Action with effect_flag 4, and T348 built the real producer there
    // (refactor/T348_buff_landing_producer.md); nothing needs a second,
    // wrong-shaped hook here.
}

void SpellManager::handleInterrupt(uint16_t caster_id, uint16_t spell_id, uint8_t message_type)
{
    LOG_DEBUG(MOD_SPELL, "Interrupt: caster={}, spell={}, type={}",
        caster_id, spell_id, message_type);

    // Check if this was our target's cast
    bool isTargetCast = (caster_id == m_target_caster_id && spell_id == m_target_spell_id);
    if (isTargetCast) {
        m_target_caster_id = 0;
        m_target_spell_id = SPELL_UNKNOWN;
    }

    bool isPlayerCast = (caster_id == m_eq->GetEntityID());

    // D20e1: Signal cast visual interrupt via bridge
    if (m_bridge) {
        eqt::state::SpellCastVisualInterruptedData d;
        d.casterId = caster_id;
        d.spellId = spell_id;
        d.isPlayerCast = isPlayerCast;
        d.isTargetCast = isTargetCast;
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SpellCastVisualInterrupted, std::move(d)));
    }

    // If this is our cast being interrupted
    if (isPlayerCast && m_is_casting) {
        m_is_casting = false;
        m_waiting_for_server_confirm = false;

        // Reset gem state
        if (m_current_gem_slot < MAX_SPELL_GEMS) {
            m_gem_states[m_current_gem_slot] = GemState::Ready;
            publishGemChanged(m_current_gem_slot);
        }

        // Determine interrupt type
        CastResult result = CastResult::Interrupted;
        if (message_type == 2) {
            result = CastResult::Fizzle;
        }

        // Notify callback
        if (m_on_cast_complete) {
            m_on_cast_complete(result, spell_id);
        }

        publishCastComplete(spell_id, result);

        const SpellData* spell = m_spell_db.getSpell(spell_id);
        LOG_INFO(MOD_SPELL, "Cast of '{}' {} ",
            spell ? spell->name : "Unknown",
            result == CastResult::Fizzle ? "fizzled" : "was interrupted");

        m_current_spell_id = SPELL_UNKNOWN;
        m_current_target_id = 0;
    }
}

void SpellManager::handleManaChange(uint32_t new_mana, uint32_t stamina, uint32_t spell_id)
{
    LOG_DEBUG(MOD_SPELL, "ManaChange: mana={}, stamina={}, spell={}",
        new_mana, stamina, spell_id);

    // Mana is updated in EverQuest class, we just log it here
}

// ============================================================================
// Packet Sending
// ============================================================================

void SpellManager::sendCastSpellPacket(uint32_t spell_id, uint8_t gem_slot, uint16_t target_id)
{
    EQ::Net::DynamicPacket packet;
    packet.Resize(sizeof(EQT::CastSpell_Struct));

    EQT::CastSpell_Struct* cs = reinterpret_cast<EQT::CastSpell_Struct*>(packet.Data());
    cs->slot = gem_slot;
    cs->spell_id = spell_id;
    cs->inventoryslot = 0xFFFFFFFF;  // 0xFFFFFFFF for normal cast
    cs->target_id = target_id;
    std::memset(cs->cs_unknown, 0, sizeof(cs->cs_unknown));

    m_eq->QueuePacket(HC_OP_CastSpell, &packet);

    LOG_DEBUG(MOD_SPELL, "Sent CastSpell packet: spell={}, gem={}, target={}",
        spell_id, gem_slot, target_id);
}

void SpellManager::sendInterruptPacket()
{
    EQ::Net::DynamicPacket packet;
    packet.Resize(4);
    packet.PutUInt16(0, m_eq->GetEntityID());
    packet.PutUInt16(2, 0x01);  // Interrupt type

    m_eq->QueuePacket(HC_OP_InterruptCast, &packet);

    LOG_DEBUG(MOD_SPELL, "Sent InterruptCast packet");
}

void SpellManager::sendMemorizePacket(uint32_t spell_id, uint8_t gem_slot, bool memorize)
{
    EQ::Net::DynamicPacket packet;
    packet.Resize(sizeof(EQT::MemorizeSpell_Struct));

    EQT::MemorizeSpell_Struct* mem = reinterpret_cast<EQT::MemorizeSpell_Struct*>(packet.Data());
    mem->slot = static_cast<uint32_t>(gem_slot);
    mem->spell_id = spell_id;
    mem->scribing = memorize ? 1 : 2;  // 1=memorize, 2=forget
    mem->unknown0 = memorize ? 742 : 0;  // Match working client behavior

    m_eq->QueuePacket(HC_OP_MemorizeSpell, &packet);

    LOG_DEBUG(MOD_SPELL, "Sent MemorizeSpell packet: spell={}, gem={}, action={}",
        spell_id, gem_slot, memorize ? "memorize" : "forget");
}

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

// T44: push SpellCastStarted for our own cast, at the moment it begins
// locally -- see beginCastFromGem()/beginCastById() for why this can't wait
// for the server-echoed BeginCast packet (eq_player_buffs.cpp also publishes
// SpellCastStarted, for any caster including NPCs/other players; that one is
// unrelated to this, driven by the packet, not local intent).
void SpellManager::publishCastStarted(uint32_t spell_id, uint16_t target_id, uint32_t cast_time_ms) {
    // T221: source-side write on EverQuest's own GameState, ahead of the
    // bridge guard -- the SpellCastStarted projection is redundant with this.
    if (m_eq) {
        m_eq->GetGameState().spells().setCasting(true, spell_id, target_id, cast_time_ms);
    }
    if (!m_bridge) return;
    eqt::state::SpellCastStartedData data;
    data.casterId = m_eq->GetEntityID();
    data.spellId = spell_id;
    data.castTimeMs = cast_time_ms;
    data.targetId = target_id;
    m_bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::SpellCastStarted, std::move(data)));
}

// T44: push SpellCastComplete for our own cast, at every place m_is_casting
// flips back to false for it (success, interrupt, fizzle) -- this event was
// defined but never published before this unit, the one gap in reconstructing
// casting state from bridge events (SpellGemChanged/SpellCastStarted already
// covered start and gem lifecycle).
void SpellManager::publishCastComplete(uint32_t spell_id, CastResult result) {
    // T221: source-side write; SpellCastComplete projection redundant.
    if (m_eq) {
        m_eq->GetGameState().spells().clearCasting();
    }
    if (!m_bridge) return;
    eqt::state::SpellCastCompleteData data;
    data.casterId = m_eq->GetEntityID();
    data.spellId = spell_id;
    data.result = static_cast<uint8_t>(result);
    m_bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::SpellCastComplete, std::move(data)));
}

// U06b: Push SpellGemChanged to bridge for new UI
void SpellManager::publishGemChanged(uint8_t gem_slot) {
    if (gem_slot >= MAX_SPELL_GEMS) return;
    // T221: source-side write; SpellGemChanged projection redundant. The two
    // gem-state enums are value-identical (spell_constants.h GemState vs
    // spell_state.h SpellGemState) -- same cast the projection makes.
    if (m_eq) {
        auto& spells = m_eq->GetGameState().spells();
        auto gemState = static_cast<eqt::state::SpellGemState>(m_gem_states[gem_slot]);
        spells.setGem(gem_slot, m_spell_gems[gem_slot], gemState);
        if (gemState == eqt::state::SpellGemState::Refresh) {
            spells.setGemCooldown(gem_slot, getGemCooldownRemaining(gem_slot),
                                  m_gem_cooldown_duration[gem_slot]);
        }
    }
    if (!m_bridge) return;
    uint32_t spellId = m_spell_gems[gem_slot];
    eqt::state::SpellGemChangedData data;
    data.gemSlot = gem_slot;
    data.spellId = spellId;
    data.gemState = static_cast<uint8_t>(m_gem_states[gem_slot]);
    data.cooldownRemainingMs = getGemCooldownRemaining(gem_slot);
    data.cooldownTotalMs = m_gem_cooldown_duration[gem_slot];
    data.memorizeTotalMs = m_memorize_duration_ms;
    if (spellId != SPELL_UNKNOWN) {
        const auto* spell = m_spell_db.getSpell(spellId);
        data.spellName = spell ? spell->name : "";
        data.iconId = spell ? spell->gem_icon : 0;
    }
    m_bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::SpellGemChanged, std::move(data)));
}

// ============================================================================
// Spell Message Display
// ============================================================================

void SpellManager::showSpellMessages(const SpellData* spell, uint16_t caster_id,
                                      uint16_t target_id, bool resisted)
{
    if (!spell) {
        return;
    }

    // Get entity names (converted to display format)
    std::string caster_name = "Unknown";
    std::string target_name = "Unknown";
    bool target_is_player = (target_id == m_eq->GetEntityID());
    bool caster_is_player = (caster_id == m_eq->GetEntityID());

    const auto& entities = m_eq->GetEntities();

    auto caster_it = entities.find(caster_id);
    if (caster_it != entities.end()) {
        caster_name = EQT::toDisplayName(caster_it->second.name);
    }

    auto target_it = entities.find(target_id);
    if (target_it != entities.end()) {
        target_name = EQT::toDisplayName(target_it->second.name);
    }

    // Handle resist message
    if (resisted) {
        if (target_is_player) {
            m_eq->AddChatSystemMessage("You resist the spell!");
        } else if (!target_name.empty()) {
            m_eq->AddChatSystemMessage(target_name + " resists the spell!");
        }
        return;
    }

    // Display landing message
    if (target_is_player) {
        // "You" message (cast_on_you)
        if (!spell->cast_on_you.empty()) {
            m_eq->AddChatSystemMessage(spell->cast_on_you);
        }
    } else {
        // "Other" message with name substitution (cast_on_other)
        if (!spell->cast_on_other.empty()) {
            std::string msg = spell->cast_on_other;
            size_t pos = msg.find("%s");
            if (pos != std::string::npos) {
                msg.replace(pos, 2, target_name);
            }
            m_eq->AddChatSystemMessage(msg);
        }
    }
}

void SpellManager::showBeginCastMessage(uint16_t caster_id, const SpellData* spell)
{
    if (!spell) {
        return;
    }

    // Get caster name
    std::string caster_name = "Unknown";
    const auto& entities = m_eq->GetEntities();
    auto it = entities.find(caster_id);
    if (it != entities.end()) {
        caster_name = EQT::toDisplayName(it->second.name);
    }

    // Display "X begins casting Y" message
    std::string message = caster_name + " begins casting " + spell->name + ".";
    m_eq->AddChatSystemMessage(message);
}

} // namespace EQ
