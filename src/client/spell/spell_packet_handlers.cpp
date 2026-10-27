/*
 * T407: SpellManager's inbound-packet arms, split out of spell_manager.cpp
 * when that TU reached 1,451 of the 1,500-line ceiling.
 *
 * The four handlers EverQuest's zone dispatcher calls -- handleBeginCast
 * (HC_OP_BeginCast), handleAction (HC_OP_Action, the landing), handleInterrupt
 * (HC_OP_InterruptCast) and handleManaChange (HC_OP_ManaChange) -- plus
 * showSpellMessages() / showBeginCastMessage(), whose only callers they are.
 *
 * T357 (board B061) deleted a spell-land callback hook that sat at function
 * scope in handleAction(), OUTSIDE the `spell && is_success_packet` gate, so
 * it fired on the animation packet. It must stay gone; the source-reader pin
 * is SpellLandHookIsGone in tests/test_spell_manager_timing.cpp, and T407
 * repointed it at this file along with the body.
 *
 * Bodies moved verbatim; no behaviour change.
 */

#include "client/spell/spell_manager.h"

#include <cctype>
#include "client/spell/spells_new_eff_reader.h"  // B1144: SpellsNewEffGateLevel
#include "client/eq.h"
#include "client/combat.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "common/logging.h"
#include "common/name_utils.h"

namespace EQ {

// ============================================================================
// Packet Handlers
// ============================================================================

// B1144 (#464): the level eqgame 0x495cc3 gates spellsnew.eff slots with --
// the caster spawn's level (Spawn_Struct @151, kept current by
// OP_LevelUpdate for the player), 1 for an NPC (Spawn_Struct @83 != 0) or a
// caster this client has no spawn for.
static int fxGateLevelOf(const EverQuest* eq, uint16_t caster_id)
{
    const auto& entities = eq->GetEntities();
    const auto it = entities.find(caster_id);
    if (it == entities.end()) return EQ::SpellsNewEffGateLevel(false, false, 0);
    return EQ::SpellsNewEffGateLevel(true, it->second.npc_type != 0, it->second.level);
}

// B1165 (#484): what eqgame's On-For filter FUN_00491cff reads of a spawn
// (spell_fx_options.h): its type (+0x244 = fable's npc_type), whether its name
// is in my group (FUN_00485df1) or my raid (FUN_004303a6), and whether it is
// me (DAT_00905ce4). An unknown id is the client's null spawn.
static EQ::SpellFxActor fxActorOf(EverQuest* eq, uint16_t id)
{
    EQ::SpellFxActor a;
    if (id == 0) return a;
    const auto& entities = eq->GetEntities();
    const auto it = entities.find(id);
    if (it == entities.end()) return a;
    a.present = true;
    a.spawnType = it->second.npc_type;
    a.isSelf = id == eq->GetEntityID();
    const std::string name = EQT::toDisplayName(it->second.name);
    auto same = [&](const std::string& n) {
        if (n.size() != name.size()) return false;
        for (size_t i = 0; i < n.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(n[i])) != std::tolower(static_cast<unsigned char>(name[i])))
                return false;
        return true;
    };
    const auto& group = eq->GetGameState().group();
    for (int i = 0; i < group.memberCount(); ++i) {
        const auto* m = group.getMember(i);
        if (m && ((m->spawnId != 0 && m->spawnId == id) || same(m->name))) { a.inGroup = true; break; }
    }
    if (eq->Group().IsInRaid()) {
        for (const auto& r : eq->Group().GetRaidMembers())
            if (same(r.name)) { a.inRaid = true; break; }
    }
    return a;
}

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
            d.fxGateLevel = fxGateLevelOf(m_eq, caster_id);  // B1144
            if (spell) d.castAnim = spell->cast_anim;  // B1148: field 120
            // B1152 (#472): the missile flies to the player's current target.
            if (auto* combat = m_eq->GetCombatManager()) {
                d.targetId = combat->GetTargetId();
            }
            d.casterFx = fxActorOf(m_eq, caster_id);  // B1165
            d.targetFx = fxActorOf(m_eq, d.targetId);
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
            d.fxGateLevel = fxGateLevelOf(m_eq, caster_id);  // B1144
            if (spell) d.castAnim = spell->cast_anim;  // B1148: field 120
            d.casterFx = fxActorOf(m_eq, caster_id);   // B1165
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
        d.fxGateLevel = fxGateLevelOf(m_eq, caster_id);  // B1144
        d.casterFx = fxActorOf(m_eq, caster_id);         // B1165
        d.targetFx = fxActorOf(m_eq, target_id);
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SpellCastVisualComplete, std::move(d)));
    }

    // Display spell landing messages only on success packet
    if (spell && is_success_packet) {
        showSpellMessages(spell, caster_id, target_id, false);  // Not resisted
    }

    // T482 (board B631): this packet no longer ends our own cast. eqemu queues
    // the effect_flag 0 action BEFORE it knows whether the spell lands
    // (Mob::SpellOnTarget, zone/spells.cpp:3957-3962), and a beneficial buff
    // that does not take hold is then STOPPED (SpellFinished false ->
    // StopCasting, :2590-2593, :1749): no recast, no mana. The cast's outcome
    // is the tail CastedSpellFinished sends afterwards -- see handleManaChange
    // and handleSpellBarRefresh.

    // T357 (B061): the m_on_spell_land callback that used to fire here is
    // GONE. It had zero setters tree-wide, and this invocation sat OUTSIDE the
    // `spell && is_success_packet` gate above -- so it fired on the animation
    // packet as well, which is not a landing at all. Spell landing is
    // OP_Action with effect_flag 4, and T348 built the real producer there
    // (refactor/T348_buff_landing_producer.md); nothing needs a second,
    // wrong-shaped hook here.
}

// eqemu zone/string_ids.h: the two message ids that mean the cast failed
// rather than was interrupted.
static constexpr uint32_t kEqStrSpellFizzle = 173;  // SPELL_FIZZLE
static constexpr uint32_t kEqStrMissNote = 180;     // MISS_NOTE

void SpellManager::handleInterrupt(uint16_t caster_id, uint32_t message_id, const std::string& caster_name)
{
    // T469 (board B604). Mob::InterruptSpell (eqemu zone/spells.cpp:1242)
    // sends the caster an 8-byte InterruptCast (no name) followed by a
    // ManaChange, and everyone else in range an InterruptCast carrying the
    // caster's name. No spell id either way -- resolve it from what we track.
    bool isPlayerCast = (caster_id == m_eq->GetEntityID());
    bool isTargetCast = (!isPlayerCast && caster_id != 0 && caster_id == m_target_caster_id);

    uint32_t spell_id = SPELL_UNKNOWN;
    if (isPlayerCast) {
        spell_id = m_current_spell_id;
    } else if (isTargetCast) {
        spell_id = m_target_spell_id;
        m_target_caster_id = 0;
        m_target_spell_id = SPELL_UNKNOWN;
    }

    LOG_DEBUG(MOD_SPELL, "Interrupt: caster={} ('{}'), spell={}, message={}",
        caster_id, caster_name, spell_id, message_id);

    // The packet is itself the chat line ("it works the same as a formatted
    // message", spells.cpp:1338): %1 is the caster's name on the bystander copy.
    std::string text = caster_name.empty()
        ? m_eq->Strings().GetStringMessage(message_id)
        : m_eq->Strings().GetFormattedStringMessage(message_id, {caster_name});
    if (!text.empty() && text[0] != '[') {
        m_eq->AddChatSystemMessage(text);
    }

    if (isPlayerCast) {
        if (m_is_casting) {
            bool failed = (message_id == kEqStrSpellFizzle || message_id == kEqStrMissNote);
            endOwnCastByServer(failed ? CastResult::Fizzle : CastResult::Interrupted);
        }
        return;
    }

    // Someone else's cast: cancel their cast bar and glow.
    if (m_bridge) {
        eqt::state::SpellCastVisualInterruptedData d;
        d.casterId = caster_id;
        d.spellId = spell_id;
        d.isPlayerCast = false;
        d.isTargetCast = isTargetCast;
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SpellCastVisualInterrupted, std::move(d)));
    }
}

void SpellManager::endOwnCastByServer(CastResult result)
{
    uint32_t spell_id = m_current_spell_id;

    // D20e1: cancel the cast bar and cast glow.
    if (m_bridge) {
        eqt::state::SpellCastVisualInterruptedData d;
        d.casterId = m_eq->GetEntityID();
        d.spellId = spell_id;
        d.isPlayerCast = true;
        d.isTargetCast = false;
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SpellCastVisualInterrupted, std::move(d)));
    }

    m_is_casting = false;
    m_waiting_for_server_confirm = false;

    // eqemu starts no recast on an interrupt: InterruptSpell/StopCasting only
    // zero the casting vars and re-enable the spell bar.
    if (m_current_gem_slot < EQT::SPELL_GEM_COUNT) {
        m_gem_states[m_current_gem_slot] = GemState::Ready;
        publishGemChanged(m_current_gem_slot);
    }

    publishCastComplete(spell_id, result);

    const SpellData* spell = m_spell_db.getSpell(spell_id);
    // B630 item 4: the log carries the spell id, so the lane analyzer can
    // attribute this outcome to its leg's declared spell instead of the name
    // (which the GM-prep bug could have made carry the wrong spell's name).
    LOG_INFO(MOD_SPELL, "Cast of '{}' (ID: {}) {}",
        spell ? spell->name : "Unknown", spell_id,
        result == CastResult::Fizzle ? "fizzled" : "was interrupted");

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

void SpellManager::handleManaChange(uint32_t new_mana, uint32_t stamina, uint32_t spell_id, uint8_t keepcasting)
{
    LOG_DEBUG(MOD_SPELL, "ManaChange: mana={}, stamina={}, spell={}, keepcasting={}",
        new_mana, stamina, spell_id, keepcasting);

    // Mana itself is updated by PlayerComponent::ZoneProcessManaChange.
    //
    // T469: Mob::StopCasting (eqemu zone/spells.cpp:1351) -- a fizzle
    // (DoCastSpell, spells.cpp:321-331), a refused cast -- sends ONLY this
    // packet with keepcasting 0, and before SendBeginCast. That is the one
    // case the bytes are unambiguous: once BeginCast has confirmed the cast, a
    // successful finish sends the same packet (SendSpellBarEnable,
    // spells.cpp:1817), so it is left to OP_Action / OP_InterruptCast.
    // Periodic mana ticks carry keepcasting 1 (Client::CheckManaEndUpdate).
    if (keepcasting != 0 || !m_is_casting || spell_id != m_current_spell_id) {
        return;
    }
    if (m_waiting_for_server_confirm) {
        endOwnCastByServer(CastResult::Interrupted);
        return;
    }

    // T482 (board B631): after BeginCast, keepcasting 0 is either the success
    // tail's SendSpellBarEnable (spells.cpp:1817) or a StopCasting
    // (spells.cpp:1369-1376; the recast-timer arms :1423, :1439 and a failed
    // SpellFinished :1749). Both say the spell bar is usable again, so the
    // cast is released now; the success tail's next packet,
    // OP_MemorizeSpell memSpellSpellbar (:1824), is what tells them apart.
    releaseConfirmedCast();
}

void SpellManager::publishOwnCastReleased()
{
    if (!m_bridge) {
        return;
    }
    // Ends the cast bar and the cast glow without painting anything: the only
    // paint for a cast is the server's own effect_flag 0 OP_Action.
    eqt::state::SpellCastVisualCompleteData d;
    d.casterId = m_eq->GetEntityID();
    d.targetId = m_current_target_id;
    d.spellId = m_current_spell_id;
    d.isSuccess = false;
    d.isCastAnimation = false;
    d.isPlayerCast = true;
    d.isTargetCast = false;
    if (const SpellData* spell = m_spell_db.getSpell(m_current_spell_id)) {
        d.spellVisual = spell->visualVocabulary();  // T381 (B160)
    }
    d.fxGateLevel = fxGateLevelOf(m_eq, d.casterId);  // B1144
    m_bridge->pushEvent(eqt::state::GameEvent(
        eqt::state::GameEventType::SpellCastVisualComplete, std::move(d)));
}

void SpellManager::releaseConfirmedCast()
{
    publishOwnCastReleased();

    m_outcome_pending = true;
    m_pending_spell_id = m_current_spell_id;
    m_pending_gem_slot = m_current_gem_slot;

    m_is_casting = false;
    m_waiting_for_server_confirm = false;
    if (m_current_gem_slot < EQT::SPELL_GEM_COUNT) {
        m_gem_states[m_current_gem_slot] = GemState::Ready;
        publishGemChanged(m_current_gem_slot);
    }
    if (m_eq) {
        m_eq->GetGameState().spells().clearCasting();
    }

    m_current_spell_id = SPELL_UNKNOWN;
    m_current_target_id = 0;
}

void SpellManager::resolvePendingOutcome(CastResult result)
{
    if (!m_outcome_pending) {
        return;
    }
    const uint32_t spell_id = m_pending_spell_id;
    const uint8_t gem_slot = m_pending_gem_slot;
    m_outcome_pending = false;
    m_pending_spell_id = SPELL_UNKNOWN;
    m_pending_gem_slot = 0xFF;

    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (result == CastResult::Success && gem_slot < EQT::SPELL_GEM_COUNT) {
        startGemCooldown(gem_slot, spell ? spell->recast_time_ms : 0);
    }
    publishCastComplete(spell_id, result);

    LOG_INFO(MOD_SPELL, "Cast of '{}' (ID: {}) {}", spell ? spell->name : "Unknown",
        spell_id,
        result == CastResult::Success ? "completed successfully"
                                      : "was stopped by the server (no spell bar refresh)");
}

void SpellManager::handleSpellBarRefresh(uint32_t slot, uint32_t spell_id)
{
    LOG_DEBUG(MOD_SPELL, "SpellBarRefresh: slot={}, spell={}", slot, spell_id);

    if (m_outcome_pending && spell_id == m_pending_spell_id) {
        resolvePendingOutcome(CastResult::Success);
        return;
    }

    // A bard song's tail is this packet alone (spells.cpp:1803).
    if (m_is_casting && !m_waiting_for_server_confirm && spell_id == m_current_spell_id) {
        publishOwnCastReleased();
        completeCast(true);
        return;
    }

    // No outcome pending for this spell (e.g. the duration + 5 s fallback
    // already ended it): the recast is still the server's word for that gem.
    if (slot < EQT::SPELL_GEM_COUNT && spell_id != SPELL_UNKNOWN && m_spell_gems[slot] == spell_id &&
        !(m_is_casting && m_current_gem_slot == slot)) {
        const SpellData* spell = m_spell_db.getSpell(spell_id);
        startGemCooldown(static_cast<uint8_t>(slot), spell ? spell->recast_time_ms : 0);
    }
}

void SpellManager::noteZonePacket(uint16_t opcode, bool reliable)
{
    if (!m_outcome_pending) {
        return;
    }
    // B760 (issue #45): only the RELIABLE stream is ordered. eqemu queues the
    // success tail -- SendSpellBarEnable, [OP_LinkedReuse], OP_MemorizeSpell
    // (spells.cpp:1817-1824) -- in one call on a single-threaded zone, all
    // reliable (Client::QueuePacket ack_req defaults true, client.h:444), so
    // they take consecutive sequence numbers and the client delivers them in
    // that order. An unreliable send is delivered the moment it arrives and,
    // when a tail packet is lost and resent, reaches us INSIDE the tail; it
    // is not the server's next word and says nothing about the outcome.
    if (!reliable) {
        return;
    }
    if (opcode == HC_OP_MemorizeSpell || opcode == HC_OP_LinkedReuse) {
        return;
    }
    // The next reliable packet is not the tail's: StopCasting
    // (spells.cpp:1351), whose only packet was the keepcasting 0.
    resolvePendingOutcome(CastResult::Interrupted);
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
