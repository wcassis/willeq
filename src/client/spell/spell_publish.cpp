/*
 * T407: everything SpellManager sends out, split out of spell_manager.cpp when
 * that TU reached 1,451 of the 1,500-line ceiling.
 *
 * Two directions, one seam:
 *   - to the SERVER: sendCastSpellPacket (HC_OP_CastSpell), sendInterruptPacket
 *     (HC_OP_InterruptCast), sendMemorizePacket (HC_OP_MemorizeSpell).
 *   - to the RENDERER, via GameStateBridge: publishCastStarted /
 *     publishCastComplete (T44) and publishGemChanged (U06b), each of which
 *     also does T221's source-side write on EverQuest's own GameState first.
 *
 * Bodies moved verbatim; no behaviour change.
 */

#include "client/spell/spell_manager.h"
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "common/net/packet.h"
#include "common/logging.h"

namespace EQ {

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

// T44: push SpellCastStarted for our own cast, at the moment it begins
// locally -- see beginCastFromGem() for why this can't wait
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
    if (gem_slot >= EQT::SPELL_GEM_COUNT) return;
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
    if (spellId != SPELL_UNKNOWN && spellId != 0) {
        const auto* spell = m_spell_db.getSpell(spellId);
        data.spellName = spell ? spell->name : "";
        data.iconId = spell ? spell->gem_icon : 0;
    }
    m_bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::SpellGemChanged, std::move(data)));
}

} // namespace EQ
