/*
 * WillEQ Buff Manager Implementation
 */

#include <client/spell/buff_manager.h>
#include <client/spell/spell_database.h>
#include <common/logging.h>
#include <algorithm>
#include <iostream>
#include <fmt/format.h>

namespace EQ {

BuffManager::BuffManager(SpellDatabase* spell_db)
    : m_spell_db(spell_db)
{
    m_player_buffs.reserve(MAX_PLAYER_BUFFS);
}

BuffManager::~BuffManager() = default;

// ============================================================================
// Update
// ============================================================================

void BuffManager::update(float delta_time)
{
    // Accumulate time for second-based countdown
    m_tick_accumulator += delta_time;

    // Decrement buff timers every second
    while (m_tick_accumulator >= 1.0f) {
        m_tick_accumulator -= 1.0f;

        // Decrement player buff timers
        for (auto& buff : m_player_buffs) {
            if (!buff.isPermanent() && buff.remaining_seconds > 0) {
                buff.remaining_seconds--;
                LOG_TRACE(MOD_SPELL, "Decremented buff {} slot {} to {} seconds",
                    buff.spell_id, buff.slot, buff.remaining_seconds);
            }
        }

        // Decrement pet buff timers
        for (auto& buff : m_pet_buffs) {
            if (!buff.isPermanent() && buff.remaining_seconds > 0) {
                buff.remaining_seconds--;
            }
        }

        // Decrement entity buff timers
        for (auto& [entity_id, buffs] : m_entity_buffs) {
            for (auto& buff : buffs) {
                if (!buff.isPermanent() && buff.remaining_seconds > 0) {
                    buff.remaining_seconds--;
                }
            }
        }
    }

    // Check for expired buffs
    checkBuffExpiration(m_player_buffs, 0);  // 0 = player
    checkBuffExpiration(m_pet_buffs, m_pet_buffs_owner_id);  // Pet buffs
    for (auto& [entity_id, buffs] : m_entity_buffs) {
        checkBuffExpiration(buffs, entity_id);
    }
}

void BuffManager::checkBuffExpiration(std::vector<ActiveBuff>& buffs, uint16_t entity_id)
{
    // Find and remove expired buffs
    auto it = buffs.begin();
    while (it != buffs.end()) {
        if (it->isExpired()) {
            uint32_t spell_id = it->spell_id;

            // Notify callback
            if (m_on_buff_fade) {
                m_on_buff_fade(entity_id, spell_id);
            }

            it = buffs.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Player Buffs
// ============================================================================

void BuffManager::setPlayerBuffs(const EQT::SpellBuff_Struct* buffs, size_t count)
{
    m_player_buffs.clear();

    if (!buffs || count == 0) {
        return;
    }

    for (size_t i = 0; i < count && i < MAX_PLAYER_BUFFS; ++i) {
        const auto& buff = buffs[i];

        // Skip empty slots
        if (buff.spellid == 0 || buff.spellid == SPELL_UNKNOWN ||
            buff.effect_type == static_cast<uint8_t>(BuffEffectType::None)) {
            continue;
        }

        ActiveBuff active;
        active.spell_id = buff.spellid;
        active.caster_id = static_cast<uint16_t>(buff.player_id);
        active.caster_level = buff.level;
        // Convert ticks to seconds; -1 or very large = permanent
        if (buff.duration < 0 || buff.duration == static_cast<int32_t>(0xFFFFFFFF)) {
            active.remaining_seconds = 0xFFFFFFFF;
        } else {
            active.remaining_seconds = static_cast<uint32_t>(buff.duration) * BUFF_TICK_SECONDS;
        }
        active.counters = buff.counters;
        active.effect_type = static_cast<BuffEffectType>(buff.effect_type);
        active.slot = static_cast<int8_t>(i);

        m_player_buffs.push_back(active);
    }
}

bool BuffManager::hasPlayerBuff(uint32_t spell_id) const
{
    for (const auto& buff : m_player_buffs) {
        if (buff.spell_id == spell_id) {
            return true;
        }
    }
    return false;
}

const ActiveBuff* BuffManager::getPlayerBuff(uint32_t spell_id) const
{
    for (const auto& buff : m_player_buffs) {
        if (buff.spell_id == spell_id) {
            return &buff;
        }
    }
    return nullptr;
}

const ActiveBuff* BuffManager::getPlayerBuffBySlot(uint8_t slot) const
{
    for (const auto& buff : m_player_buffs) {
        if (buff.slot == static_cast<int8_t>(slot)) {
            return &buff;
        }
    }
    return nullptr;
}

void BuffManager::setPlayerBuff(uint8_t slot, const EQT::SpellBuff_Struct& buff)
{
    // Remove existing buff in this slot
    m_player_buffs.erase(
        std::remove_if(m_player_buffs.begin(), m_player_buffs.end(),
            [slot](const ActiveBuff& b) { return b.slot == static_cast<int8_t>(slot); }),
        m_player_buffs.end()
    );

    // Skip if empty buff
    if (buff.spellid == 0 || buff.spellid == SPELL_UNKNOWN ||
        buff.effect_type == static_cast<uint8_t>(BuffEffectType::None)) {
        return;
    }

    ActiveBuff active;
    active.spell_id = buff.spellid;
    active.caster_id = static_cast<uint16_t>(buff.player_id);
    active.caster_level = buff.level;
    // Convert ticks to seconds; -1 or very large = permanent
    if (buff.duration < 0 || buff.duration == static_cast<int32_t>(0xFFFFFFFF)) {
        active.remaining_seconds = 0xFFFFFFFF;
    } else {
        active.remaining_seconds = static_cast<uint32_t>(buff.duration) * BUFF_TICK_SECONDS;
    }
    active.counters = buff.counters;
    active.effect_type = static_cast<BuffEffectType>(buff.effect_type);
    active.slot = static_cast<int8_t>(slot);

    std::cout << fmt::format("[BuffManager] Server buff update: spell {} slot {} duration_ticks={} -> remaining_seconds={}",
        active.spell_id, slot, buff.duration, active.remaining_seconds) << std::endl;

    m_player_buffs.push_back(active);

    // Notify callback
    if (m_on_buff_apply) {
        m_on_buff_apply(0, active.spell_id);  // 0 = player
    }
}

void BuffManager::removePlayerBuffBySlot(uint8_t slot)
{
    auto it = std::find_if(m_player_buffs.begin(), m_player_buffs.end(),
        [slot](const ActiveBuff& b) { return b.slot == static_cast<int8_t>(slot); });

    if (it != m_player_buffs.end()) {
        uint32_t spell_id = it->spell_id;
        m_player_buffs.erase(it);

        // Notify callback
        if (m_on_buff_fade) {
            m_on_buff_fade(0, spell_id);  // 0 = player
        }
    }
}

// ============================================================================
// Pet Buffs
// ============================================================================

void BuffManager::setPetBuffs(uint16_t petId, const EQT::PetBuff_Struct& buffs)
{
    m_pet_buffs.clear();
    m_pet_buffs_owner_id = petId;

    for (size_t i = 0; i < EQT::PET_BUFF_COUNT && i < buffs.buffcount; ++i) {
        uint32_t spell_id = buffs.spellid[i];

        // Skip empty slots (0xFFFFFFFF or 0 = empty)
        if (spell_id == 0 || spell_id == 0xFFFFFFFF || spell_id == SPELL_UNKNOWN) {
            continue;
        }

        ActiveBuff active;
        active.spell_id = spell_id;
        active.caster_id = 0;  // Not provided in PetBuff_Struct
        active.caster_level = 0;  // Not provided in PetBuff_Struct

        // Convert ticks to seconds; -1 or very large = permanent
        int32_t ticks = buffs.ticsremaining[i];
        if (ticks < 0 || ticks == static_cast<int32_t>(0xFFFFFFFF)) {
            active.remaining_seconds = 0xFFFFFFFF;  // Permanent
        } else {
            active.remaining_seconds = static_cast<uint32_t>(ticks) * BUFF_TICK_SECONDS;
        }

        active.counters = 0;
        active.slot = static_cast<int8_t>(i);

        // Determine effect type from spell data
        if (m_spell_db) {
            const SpellData* spell = m_spell_db->getSpell(spell_id);
            if (spell) {
                active.effect_type = spell->is_beneficial ? BuffEffectType::Buff : BuffEffectType::Inverse;
            } else {
                active.effect_type = BuffEffectType::Buff;  // Default to buff if spell not found
            }
        } else {
            active.effect_type = BuffEffectType::Buff;
        }

        m_pet_buffs.push_back(active);
    }

    LOG_DEBUG(MOD_SPELL, "Set {} pet buffs for pet {}", m_pet_buffs.size(), petId);
}

void BuffManager::clearPetBuffs()
{
    m_pet_buffs.clear();
    m_pet_buffs_owner_id = 0;
    LOG_DEBUG(MOD_SPELL, "Cleared pet buffs");
}

// ============================================================================
// Entity Buffs
// ============================================================================

void BuffManager::setEntityBuff(uint16_t entity_id, uint8_t slot, const EQT::SpellBuff_Struct& buff)
{
    auto& buffs = m_entity_buffs[entity_id];

    // Remove existing buff in this slot
    buffs.erase(
        std::remove_if(buffs.begin(), buffs.end(),
            [slot](const ActiveBuff& b) { return b.slot == static_cast<int8_t>(slot); }),
        buffs.end()
    );

    // Skip if empty buff
    if (buff.spellid == 0 || buff.spellid == SPELL_UNKNOWN ||
        buff.effect_type == static_cast<uint8_t>(BuffEffectType::None)) {
        return;
    }

    ActiveBuff active;
    active.spell_id = buff.spellid;
    active.caster_id = static_cast<uint16_t>(buff.player_id);
    active.caster_level = buff.level;
    // Convert ticks to seconds; -1 or very large = permanent
    if (buff.duration < 0 || buff.duration == static_cast<int32_t>(0xFFFFFFFF)) {
        active.remaining_seconds = 0xFFFFFFFF;
    } else {
        active.remaining_seconds = static_cast<uint32_t>(buff.duration) * BUFF_TICK_SECONDS;
    }
    active.counters = buff.counters;
    active.effect_type = static_cast<BuffEffectType>(buff.effect_type);
    active.slot = static_cast<int8_t>(slot);

    buffs.push_back(active);

    // Notify callback
    if (m_on_buff_apply) {
        m_on_buff_apply(entity_id, active.spell_id);
    }
}

void BuffManager::clearEntityBuffs(uint16_t entity_id)
{
    m_entity_buffs.erase(entity_id);
}

const std::vector<ActiveBuff>* BuffManager::getEntityBuffs(uint16_t entity_id) const
{
    auto it = m_entity_buffs.find(entity_id);
    if (it != m_entity_buffs.end()) {
        return &it->second;
    }
    return nullptr;
}

bool BuffManager::hasEntityBuff(uint16_t entity_id, uint32_t spell_id) const
{
    auto it = m_entity_buffs.find(entity_id);
    if (it == m_entity_buffs.end()) {
        return false;
    }

    for (const auto& buff : it->second) {
        if (buff.spell_id == spell_id) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Buff Application
// ============================================================================

void BuffManager::applyBuff(uint16_t target_id, uint32_t spell_id, uint16_t caster_id,
                            uint8_t caster_level, uint32_t duration_ticks)
{
    // Determine which buff list to use
    std::vector<ActiveBuff>* buffs = nullptr;
    if (target_id == 0) {
        buffs = &m_player_buffs;
    } else {
        buffs = &m_entity_buffs[target_id];
    }

    // Check stacking
    if (!checkStacking(*buffs, spell_id)) {
        return;  // Cannot stack, buff blocked
    }

    // Remove existing buff of same spell
    buffs->erase(
        std::remove_if(buffs->begin(), buffs->end(),
            [spell_id](const ActiveBuff& b) { return b.spell_id == spell_id; }),
        buffs->end()
    );

    // Find a free slot
    int8_t slot = findFreeSlot(*buffs);
    if (slot < 0) {
        return;  // No free slots
    }

    // Create the new buff
    ActiveBuff active;
    active.spell_id = spell_id;
    active.caster_id = caster_id;
    active.caster_level = caster_level;
    // Convert ticks to seconds; 0xFFFFFFFF = permanent
    if (duration_ticks == 0xFFFFFFFF) {
        active.remaining_seconds = 0xFFFFFFFF;
    } else {
        active.remaining_seconds = duration_ticks * BUFF_TICK_SECONDS;
    }
    active.counters = 0;
    active.slot = slot;

    // Determine effect type from spell data
    if (m_spell_db) {
        const SpellData* spell = m_spell_db->getSpell(spell_id);
        if (spell) {
            active.effect_type = spell->is_beneficial ? BuffEffectType::Buff : BuffEffectType::Inverse;
        }
    }

    buffs->push_back(active);

    // Notify callback
    if (m_on_buff_apply) {
        m_on_buff_apply(target_id, spell_id);
    }
}

void BuffManager::removeBuff(uint16_t target_id, uint32_t spell_id)
{
    std::vector<ActiveBuff>* buffs = nullptr;
    if (target_id == 0) {
        buffs = &m_player_buffs;
    } else {
        auto it = m_entity_buffs.find(target_id);
        if (it == m_entity_buffs.end()) {
            return;
        }
        buffs = &it->second;
    }

    auto it = std::find_if(buffs->begin(), buffs->end(),
        [spell_id](const ActiveBuff& b) { return b.spell_id == spell_id; });

    if (it != buffs->end()) {
        // Notify callback
        if (m_on_buff_fade) {
            m_on_buff_fade(target_id, spell_id);
        }

        buffs->erase(it);
    }
}

void BuffManager::removeBuffBySlot(uint16_t target_id, uint8_t slot)
{
    std::vector<ActiveBuff>* buffs = nullptr;
    if (target_id == 0) {
        buffs = &m_player_buffs;
    } else {
        auto it = m_entity_buffs.find(target_id);
        if (it == m_entity_buffs.end()) {
            return;
        }
        buffs = &it->second;
    }

    auto it = std::find_if(buffs->begin(), buffs->end(),
        [slot](const ActiveBuff& b) { return b.slot == static_cast<int8_t>(slot); });

    if (it != buffs->end()) {
        // Notify callback
        if (m_on_buff_fade) {
            m_on_buff_fade(target_id, it->spell_id);
        }

        buffs->erase(it);
    }
}

// ============================================================================
// Stat Modifications
// ============================================================================

int32_t BuffManager::getBuffedStatMod(uint16_t entity_id, SpellEffect stat) const
{
    const std::vector<ActiveBuff>* buffs = nullptr;
    if (entity_id == 0) {
        buffs = &m_player_buffs;
    } else {
        buffs = getEntityBuffs(entity_id);
    }

    if (!buffs) {
        return 0;
    }

    int32_t total = 0;
    for (const auto& buff : *buffs) {
        total += getStatModFromBuff(buff, stat);
    }
    return total;
}

int32_t BuffManager::getPlayerStatMod(SpellEffect stat) const
{
    return getBuffedStatMod(0, stat);
}

int32_t BuffManager::getStatModFromBuff(const ActiveBuff& buff, SpellEffect stat) const
{
    if (!m_spell_db) {
        return 0;
    }

    const SpellData* spell = m_spell_db->getSpell(buff.spell_id);
    if (!spell) {
        return 0;
    }

    int32_t total = 0;
    for (const auto& effect : spell->effects) {
        if (effect.effect_id == stat) {
            total += effect.base_value;
        }
    }
    return total;
}

// ============================================================================
// Spell Classification
// ============================================================================

bool BuffManager::isBeneficial(uint32_t spell_id) const
{
    if (!m_spell_db) {
        return false;
    }

    const SpellData* spell = m_spell_db->getSpell(spell_id);
    return spell && spell->is_beneficial;
}

bool BuffManager::isDetrimental(uint32_t spell_id) const
{
    if (!m_spell_db) {
        return false;
    }

    const SpellData* spell = m_spell_db->getSpell(spell_id);
    return spell && !spell->is_beneficial;
}

// ============================================================================
// Private Helpers
// ============================================================================

bool BuffManager::checkStacking(const std::vector<ActiveBuff>& existing, uint32_t new_spell_id) const
{
    if (!m_spell_db) {
        return true;  // Allow if we can't check
    }

    const SpellData* new_spell = m_spell_db->getSpell(new_spell_id);
    if (!new_spell) {
        return true;  // Allow unknown spells
    }

    // Check against each existing buff
    for (const auto& buff : existing) {
        const SpellData* existing_spell = m_spell_db->getSpell(buff.spell_id);
        if (!existing_spell) {
            continue;
        }

        // Same spell always stacks (overwrites)
        if (buff.spell_id == new_spell_id) {
            continue;
        }

        // Check spell group stacking
        if (new_spell->spell_group != 0 &&
            new_spell->spell_group == existing_spell->spell_group) {
            // Same spell group - higher rank overwrites
            if (new_spell->spell_rank <= existing_spell->spell_rank) {
                return false;  // Blocked by higher/equal rank
            }
        }

        // Check effect stacking (simplified - full EQ stacking is complex)
        // For now, allow most stacking and let server handle conflicts
    }

    return true;
}

int8_t BuffManager::findFreeSlot(const std::vector<ActiveBuff>& buffs) const
{
    // Find the lowest unused slot number
    std::array<bool, MAX_PLAYER_BUFFS> used = {};

    for (const auto& buff : buffs) {
        if (buff.slot >= 0 && static_cast<size_t>(buff.slot) < MAX_PLAYER_BUFFS) {
            used[static_cast<size_t>(buff.slot)] = true;
        }
    }

    for (size_t i = 0; i < MAX_PLAYER_BUFFS; ++i) {
        if (!used[i]) {
            return static_cast<int8_t>(i);
        }
    }

    return -1;  // No free slot
}

} // namespace EQ
