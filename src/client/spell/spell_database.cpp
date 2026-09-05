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
    // T355: previously left at its 0 default at every call site, so every
    // discipline looked free to the local endurance check that did not exist.
    spell.endurance_cost = info.enduranceCost;
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
    // T384 (B163): the two visual columns the builder now binds. Verbatim.
    spell.target_anim = info.targetAnim;
    spell.travel_type = static_cast<uint8_t>(info.travelType);
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

    // Reagents (T246): spells_us.txt marks an empty reagent slot with -1
    // (and still carries count 1 for it), so -1 lands as id 0 / count 0 and
    // SpellData::requiresReagents() (id != 0 && count > 0) reads cleanly.
    for (size_t ri = 0; ri < spell.reagent_id.size() && ri < info.reagentId.size(); ++ri) {
        int32_t rawId = info.reagentId[ri];
        int32_t rawCount = info.reagentCount[ri];
        spell.reagent_id[ri] = rawId > 0 ? static_cast<uint32_t>(rawId) : 0u;
        spell.reagent_count[ri] = (rawId > 0 && rawCount > 0) ? static_cast<uint32_t>(rawCount) : 0u;
        int32_t rawNoexpend = info.noexpendReagentId[ri];
        spell.noexpend_reagent_id[ri] = rawNoexpend > 0 ? static_cast<uint32_t>(rawNoexpend) : 0u;
    }

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

// T316: this used to forward to getSpellByName() -- an exact-match query --
// so /mem <partial name> found nothing unless the player typed the spell's
// full name. It now runs a real substring LIKE query (bound + escaped, see
// WilleqDatabase::findSpellsByNameLike) and routes every matched id back
// through getSpell() so the results come out of the same LRU cache as every
// other lookup rather than a second full-column SELECT.
std::vector<const SpellData*> SpellDatabase::findSpellsByName(const std::string& partial_name) const {
    std::vector<const SpellData*> results;
    if (!m_db || partial_name.empty()) return results;

    auto matches = m_db->findSpellsByNameLike(partial_name, kMaxNameSearchResults);
    results.reserve(matches.size());
    for (const auto& match : matches) {
        if (const SpellData* spell = getSpell(match.id)) {
            results.push_back(spell);
        }
    }
    return results;
}

size_t SpellDatabase::getSpellCount() const {
    if (m_db) return m_db->getSpellCount();
    return 0;
}

} // namespace EQ
