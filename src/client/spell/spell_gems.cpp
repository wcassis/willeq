/*
 * T407: SpellManager's gem bar and spellbook, split out of spell_manager.cpp
 * when that TU reached 1,451 of the 1,500-line ceiling.
 *
 * One storage family: m_spell_gems / m_gem_states / m_gem_cooldown_start /
 * m_gem_cooldown_duration / m_memorize_* / m_spellbook. Everything that reads
 * or writes those arrays is here -- the eight gem operations and their
 * accessors, the memorisation timing helper, the gem cooldown starter, and
 * the eight spellbook operations including T404's server-driven
 * applyServerGemUpdate().
 *
 * The per-frame drivers stay in spell_manager.cpp (updateGemCooldowns /
 * updateMemorization); this TU holds the state transitions they drive.
 *
 * Bodies moved verbatim; no behaviour change.
 */

#include "client/spell/spell_manager.h"
#include "client/eq.h"
#include "client/bridge/game_state_bridge.h"
#include "client/state/event_bus.h"
#include "common/logging.h"

namespace EQ {

// ============================================================================
// Spell Gems
// ============================================================================

bool SpellManager::memorizeSpell(uint32_t spell_id, uint8_t gem_slot)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return false;
    }

    const SpellData* spell = m_spell_db.getSpell(spell_id);
    if (!spell) {
        LOG_WARN(MOD_SPELL, "Cannot memorize unknown spell {}", spell_id);
        return false;
    }

    // Check if spell is scribed
    if (!hasSpellScribed(spell_id)) {
        LOG_WARN(MOD_SPELL, "Cannot memorize spell '{}' - not in spellbook", spell->name);
        return false;
    }

    // Check level requirement
    PlayerClass pc = static_cast<PlayerClass>(m_eq->GetClass());
    if (!spell->canClassUse(pc, m_eq->GetLevel())) {
        LOG_WARN(MOD_SPELL, "Cannot memorize spell '{}' - level too low", spell->name);
        return false;
    }

    // Check if already casting or memorizing
    if (m_is_casting || m_is_memorizing) {
        return false;
    }

    // If slot has a spell, forget it first
    if (m_spell_gems[gem_slot] != SPELL_UNKNOWN) {
        forgetSpell(gem_slot);
    }

    // Start memorization
    m_is_memorizing = true;
    m_memorize_slot = gem_slot;
    m_memorize_spell_id = spell_id;
    m_memorize_start_time = std::chrono::steady_clock::now();
    m_memorize_duration_ms = calculateMemorizeTime(spell_id);
    m_gem_states[gem_slot] = GemState::MemorizeProgress;
    publishGemChanged(gem_slot);

    // Send memorize packet to server
    sendMemorizePacket(spell_id, gem_slot, true);

    LOG_INFO(MOD_SPELL, "Starting memorization of '{}' in gem {} ({}ms)",
        spell->name, gem_slot + 1, m_memorize_duration_ms);

    // D20e1: Signal memorize start via bridge
    if (m_bridge) {
        eqt::state::SpellMemorizeVisualStartedData d;
        d.spellName = spell->name;
        d.durationMs = m_memorize_duration_ms;
        m_bridge->pushEvent(eqt::state::GameEvent(
            eqt::state::GameEventType::SpellMemorizeVisualStarted, std::move(d)));
    }

    return true;
}

bool SpellManager::forgetSpell(uint8_t gem_slot)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return false;
    }

    if (m_spell_gems[gem_slot] == SPELL_UNKNOWN) {
        return false;  // Already empty
    }

    // Can't forget while casting
    if (m_is_casting && m_current_gem_slot == gem_slot) {
        return false;
    }

    uint32_t spell_id = m_spell_gems[gem_slot];
    m_spell_gems[gem_slot] = SPELL_UNKNOWN;
    m_gem_states[gem_slot] = GemState::Empty;
    publishGemChanged(gem_slot);

    // Send forget packet to server
    sendMemorizePacket(spell_id, gem_slot, false);

    LOG_DEBUG(MOD_SPELL, "Forgot spell from gem {}", gem_slot + 1);

    return true;
}

bool SpellManager::swapGems(uint8_t slot1, uint8_t slot2)
{
    if (slot1 >= MAX_SPELL_GEMS || slot2 >= MAX_SPELL_GEMS) {
        return false;
    }

    if (slot1 == slot2) {
        return false;
    }

    // Can't swap while casting from either slot
    if (m_is_casting && (m_current_gem_slot == slot1 || m_current_gem_slot == slot2)) {
        return false;
    }

    // Can't swap if either is on cooldown or memorizing
    if (m_gem_states[slot1] == GemState::Refresh ||
        m_gem_states[slot1] == GemState::MemorizeProgress ||
        m_gem_states[slot2] == GemState::Refresh ||
        m_gem_states[slot2] == GemState::MemorizeProgress) {
        return false;
    }

    // Swap
    std::swap(m_spell_gems[slot1], m_spell_gems[slot2]);
    std::swap(m_gem_states[slot1], m_gem_states[slot2]);

    LOG_DEBUG(MOD_SPELL, "Swapped gems {} and {}", slot1 + 1, slot2 + 1);

    return true;
}

uint32_t SpellManager::getMemorizedSpell(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return SPELL_UNKNOWN;
    }
    return m_spell_gems[gem_slot];
}

GemState SpellManager::getGemState(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return GemState::Empty;
    }
    return m_gem_states[gem_slot];
}

float SpellManager::getGemCooldownProgress(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS || m_gem_states[gem_slot] != GemState::Refresh) {
        return 1.0f;  // Ready
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_gem_cooldown_start[gem_slot]).count();

    if (m_gem_cooldown_duration[gem_slot] == 0) {
        return 1.0f;
    }

    return std::min(1.0f, static_cast<float>(elapsed) / m_gem_cooldown_duration[gem_slot]);
}

uint32_t SpellManager::getGemCooldownRemaining(uint8_t gem_slot) const
{
    if (gem_slot >= MAX_SPELL_GEMS || m_gem_states[gem_slot] != GemState::Refresh) {
        return 0;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_gem_cooldown_start[gem_slot]).count();

    if (elapsed >= m_gem_cooldown_duration[gem_slot]) {
        return 0;
    }

    return m_gem_cooldown_duration[gem_slot] - static_cast<uint32_t>(elapsed);
}

float SpellManager::getMemorizeProgress(uint8_t gem_slot) const
{
    if (!m_is_memorizing || gem_slot != m_memorize_slot) {
        return 1.0f;  // Not memorizing or different slot
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_memorize_start_time).count();

    if (m_memorize_duration_ms == 0) {
        return 1.0f;
    }

    return std::min(1.0f, static_cast<float>(elapsed) / m_memorize_duration_ms);
}

uint32_t SpellManager::calculateMemorizeTime(uint32_t spell_id) const
{
    // Memorization time decreases with level
    uint8_t level = m_eq->GetLevel();

    if (level >= 50) return 500;   // Nearly instant at 50+
    if (level >= 40) return 1000;  // 1 second
    if (level >= 30) return 2000;  // 2 seconds
    if (level >= 20) return 4000;  // 4 seconds
    return 8000;                    // 8 seconds for low levels
}

uint8_t SpellManager::findGemSlotForSpell(uint32_t spell_id) const
{
    for (uint8_t i = 0; i < MAX_SPELL_GEMS; ++i) {
        if (m_spell_gems[i] == spell_id) {
            return i;
        }
    }
    return 0xFF;  // Not found
}

// ============================================================================
// Spellbook
// ============================================================================

bool SpellManager::scribeSpell(uint32_t spell_id, uint16_t book_slot)
{
    if (book_slot >= MAX_SPELLBOOK_SLOTS) {
        return false;
    }

    if (!m_spell_db.hasSpell(spell_id)) {
        return false;
    }

    m_spellbook[book_slot] = spell_id;
    return true;
}

bool SpellManager::hasSpellScribed(uint32_t spell_id) const
{
    for (uint16_t i = 0; i < MAX_SPELLBOOK_SLOTS; ++i) {
        if (m_spellbook[i] == spell_id) {
            return true;
        }
    }
    return false;
}

uint32_t SpellManager::getSpellbookSpell(uint16_t slot) const
{
    if (slot >= MAX_SPELLBOOK_SLOTS) {
        return SPELL_UNKNOWN;
    }
    return m_spellbook[slot];
}

int32_t SpellManager::findFreeSpellbookSlot() const
{
    for (uint16_t i = 0; i < MAX_SPELLBOOK_SLOTS; ++i) {
        if (m_spellbook[i] == SPELL_UNKNOWN || m_spellbook[i] == 0) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

std::vector<uint32_t> SpellManager::getScribedSpells() const
{
    std::vector<uint32_t> spells;
    for (uint16_t i = 0; i < MAX_SPELLBOOK_SLOTS; ++i) {
        if (m_spellbook[i] != SPELL_UNKNOWN) {
            spells.push_back(m_spellbook[i]);
        }
    }
    return spells;
}

void SpellManager::setSpellbook(const uint32_t* spells, size_t count)
{
    size_t max_count = std::min(count, static_cast<size_t>(MAX_SPELLBOOK_SLOTS));
    for (size_t i = 0; i < max_count; ++i) {
        m_spellbook[i] = spells[i];
    }

    LOG_DEBUG(MOD_SPELL, "Set {} spells in spellbook", max_count);
}

void SpellManager::setSpellGems(const uint32_t* gems, size_t count)
{
    size_t max_count = std::min(count, static_cast<size_t>(MAX_SPELL_GEMS));
    for (size_t i = 0; i < max_count; ++i) {
        m_spell_gems[i] = gems[i];
        m_gem_states[i] = (gems[i] != SPELL_UNKNOWN && gems[i] != 0)
            ? GemState::Ready : GemState::Empty;
    }

    LOG_DEBUG(MOD_SPELL, "Set {} spell gems", max_count);
}

void SpellManager::applyServerGemUpdate(uint32_t gem_slot, uint32_t spell_id,
                                        uint32_t scribing)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return;
    }
    const uint8_t slot = static_cast<uint8_t>(gem_slot);

    // Our OWN in-flight memorisation for this same slot and spell owns the
    // gem until updateMemorization() completes it -- the progress animation
    // is the whole point of MemorizeProgress. Leave it alone.
    if (m_is_memorizing && m_memorize_slot == slot && m_memorize_spell_id == spell_id) {
        LOG_DEBUG(MOD_SPELL, "Server gem update for gem {} matches our in-flight memorise; ignored", slot + 1);
        return;
    }

    if (scribing == 2) {
        // Un-memorise. Unconditional: the server has already emptied the
        // slot, and refusing (as forgetSpell() does while casting) would
        // leave the client holding a spell the server says is gone.
        if (m_spell_gems[slot] == SPELL_UNKNOWN) {
            return;
        }
        LOG_DEBUG(MOD_SPELL, "Server un-memorised spell {} from gem {}", m_spell_gems[slot], slot + 1);
        m_spell_gems[slot] = SPELL_UNKNOWN;
        m_gem_states[slot] = GemState::Empty;
        publishGemChanged(slot);
        return;
    }

    if (scribing == 1) {
        m_spell_gems[slot] = spell_id;
        m_gem_states[slot] = (spell_id != SPELL_UNKNOWN && spell_id != 0)
            ? GemState::Ready : GemState::Empty;
        const SpellData* spell = m_spell_db.getSpell(spell_id);
        LOG_INFO(MOD_SPELL, "Server memorised '{}' ({}) into gem {}",
            spell ? spell->name : "Unknown", spell_id, slot + 1);
        publishGemChanged(slot);
    }
}

void SpellManager::startGemCooldown(uint8_t gem_slot, uint32_t duration_ms)
{
    if (gem_slot >= MAX_SPELL_GEMS) {
        return;
    }

    // Minimum cooldown (global cooldown equivalent)
    if (duration_ms < 1500) {
        duration_ms = 1500;
    }

    m_gem_states[gem_slot] = GemState::Refresh;
    m_gem_cooldown_start[gem_slot] = std::chrono::steady_clock::now();
    m_gem_cooldown_duration[gem_slot] = duration_ms;
    publishGemChanged(gem_slot);

    LOG_DEBUG(MOD_SPELL, "Gem {} on cooldown for {}ms", gem_slot + 1, duration_ms);
}

} // namespace EQ
