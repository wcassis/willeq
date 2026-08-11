/*
 * WillEQ Spell Database Implementation
 *
 * On-demand SQLite queries via WilleqDatabase with LRU cache.
 */

#include "client/spell/spell_database.h"
#include "client/willeq_database.h"
#include "common/logging.h"

namespace EQ {

SpellDatabase::SpellDatabase() = default;
SpellDatabase::~SpellDatabase() = default;

// ============================================================================
// Initialization
// ============================================================================

bool SpellDatabase::initFromDatabase() {
    auto* db = EQT::WilleqDatabase::globalInstance();
    if (!db || !db->isOpen()) {
        m_load_error = "WilleqDatabase global instance not available";
        return false;
    }

    m_db = db;
    m_loaded = true;
    m_load_error.clear();
    m_max_spell_id = 0;

    uint32_t count = db->getSpellCount();
    LOG_INFO(MOD_SPELL, "SpellDatabase: initialized from SQLite ({} spells, cache capacity {})",
             count, m_cache_capacity);

    return true;
}

// ============================================================================
// LRU cache
// ============================================================================

void SpellDatabase::evictIfNeeded() const {
    while (m_cache.size() >= m_cache_capacity && !m_lru.empty()) {
        uint32_t evictId = m_lru.back();
        m_lru.pop_back();
        m_lru_map.erase(evictId);
        m_cache.erase(evictId);
    }
}

SpellData* SpellDatabase::queryAndCache(uint32_t spell_id) const {
    if (!m_db) return nullptr;

    EQT::SpellInfo info = m_db->getSpell(spell_id);
    if (!info.found) return nullptr;

    evictIfNeeded();

    SpellData spell;
    spell.id = info.id;
    spell.name = std::move(info.name);
    spell.player_tag = std::move(info.playerTag);
    spell.teleport_zone = std::move(info.teleportZone);
    spell.cast_on_you = std::move(info.castOnYou);
    spell.cast_on_other = std::move(info.castOnOther);
    spell.spell_fades = std::move(info.spellFades);
    spell.cast_time_ms = info.castTimeMs;
    spell.recovery_time_ms = info.recoveryTimeMs;
    spell.recast_time_ms = info.recastTimeMs;
    spell.duration_formula = info.durationFormula;
    spell.duration_cap = info.durationCap;
    spell.mana_cost = info.manaCost;
    spell.target_type = static_cast<SpellTargetType>(info.targetType);
    spell.range = info.range;
    spell.aoe_range = info.aoeRange;
    spell.resist_type = static_cast<ResistType>(info.resistType);
    spell.resist_diff = info.resistDiff;
    spell.casting_skill = static_cast<CastingSkill>(info.castingSkill);
    spell.is_beneficial = info.isBeneficial;
    spell.spell_icon = info.spellIcon;
    spell.gem_icon = info.gemIcon;
    spell.cast_anim = info.castAnim;
    spell.zone_type = info.zoneType;
    for (size_t ci = 0; ci < spell.class_levels.size() && ci < info.classLevels.size(); ++ci) {
        spell.class_levels[ci] = info.classLevels[ci];
    }

    // Effect slots: EQT::SpellInfo carries the raw spells_us.txt fields,
    // markers included — interpretation happens here, at the consumer
    // boundary, so every predicate in spell_data.h (getEffectCount,
    // hasEffect, isDamageSpell, ...) sees an already-normalised array and
    // never has to re-learn the marker itself.
    //
    // effect id 254 == "slot unused" (not SPA 254): SpellEffect::UnusedEffect
    // is literally defined as 254 (spell_constants.h), so a raw 254 cast
    // straight to the enum already lands on UnusedEffect and SpellEffectSlot
    // ::isValid() correctly reports false for it. The explicit branch below
    // documents that intent rather than relying on the numeric coincidence.
    constexpr int16_t kEffectUnusedMarker = 254;
    for (size_t si = 0; si < spell.effects.size() && si < info.effectId.size(); ++si) {
        int16_t rawEffectId = info.effectId[si];
        spell.effects[si].effect_id = (rawEffectId == kEffectUnusedMarker)
            ? SpellEffect::UnusedEffect
            : static_cast<SpellEffect>(rawEffectId);
        spell.effects[si].base_value = info.baseValue[si];
        spell.effects[si].base2_value = info.base2Value[si];
        spell.effects[si].max_value = info.maxValue[si];
        // formula marker 100 == "unused"; SpellEffectSlot's own default is
        // also 100, so a verbatim copy is already the correct no-scaling
        // value for an unused slot — no separate branch needed.
        spell.effects[si].formula = info.formula[si];
    }
    spell.spell_affect_index = info.spellAffectIndex;

    auto [it, _] = m_cache.emplace(spell_id, std::move(spell));

    m_lru.push_front(spell_id);
    m_lru_map[spell_id] = m_lru.begin();

    if (spell_id > m_max_spell_id) m_max_spell_id = spell_id;

    return &it->second;
}

// ============================================================================
// Lookups
// ============================================================================

const SpellData* SpellDatabase::getSpell(uint32_t spell_id) const {
    // Check cache
    auto it = m_cache.find(spell_id);
    if (it != m_cache.end()) {
        m_cache_hits.fetch_add(1, std::memory_order_relaxed);
        // Move to front of LRU
        auto lruIt = m_lru_map.find(spell_id);
        if (lruIt != m_lru_map.end()) {
            m_lru.splice(m_lru.begin(), m_lru, lruIt->second);
        }
        return &it->second;
    }

    // Cache miss — query SQLite
    m_cache_misses.fetch_add(1, std::memory_order_relaxed);
    return queryAndCache(spell_id);
}

bool SpellDatabase::hasSpell(uint32_t spell_id) const {
    return getSpell(spell_id) != nullptr;
}

const SpellData* SpellDatabase::getSpellByName(const std::string& name) const {
    if (!m_db) return nullptr;

    EQT::SpellInfo info = m_db->getSpellByName(name);
    if (info.found) {
        return getSpell(info.id);  // Route through cache
    }
    return nullptr;
}

std::vector<const SpellData*> SpellDatabase::findSpellsByName(const std::string& partial_name) const {
    std::vector<const SpellData*> results;
    if (!m_db) return results;

    // Use SQL LIKE query for partial matching
    // For now, search by exact prefix via getSpellByName fallback
    // TODO: Add a LIKE query to WilleqDatabase for partial name search
    auto* spell = getSpellByName(partial_name);
    if (spell) {
        results.push_back(spell);
    }
    return results;
}

size_t SpellDatabase::getSpellCount() const {
    if (m_db) return m_db->getSpellCount();
    return 0;
}

} // namespace EQ
