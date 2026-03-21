/*
 * WillEQ Spell Database
 *
 * On-demand spell lookups backed by WilleqDatabase (SQLite).
 * LRU cache provides stable const SpellData* pointers for callers.
 */

#ifndef EQT_SPELL_DATABASE_H
#define EQT_SPELL_DATABASE_H

#include "spell_data.h"
#include "spell_constants.h"
#include <unordered_map>
#include <list>
#include <vector>
#include <string>
#include <functional>
#include <atomic>

namespace EQT { class WilleqDatabase; }

namespace EQ {

class SpellDatabase {
public:
    SpellDatabase();
    ~SpellDatabase();

    // Initialize from WilleqDatabase global instance
    bool initFromDatabase();

    bool isLoaded() const { return m_loaded; }
    const std::string& getLoadError() const { return m_load_error; }

    // ── Lookup by ID ──
    const SpellData* getSpell(uint32_t spell_id) const;
    bool hasSpell(uint32_t spell_id) const;

    // ── Lookup by Name ──
    const SpellData* getSpellByName(const std::string& name) const;
    std::vector<const SpellData*> findSpellsByName(const std::string& partial_name) const;

    // ── Statistics ──
    size_t getSpellCount() const;
    uint32_t getMaxSpellId() const { return m_max_spell_id; }

    // Cache stats for /pmem
    size_t getCacheSize() const { return m_cache.size(); }
    size_t getCacheCapacity() const { return m_cache_capacity; }
    uint64_t getCacheHits() const { return m_cache_hits.load(std::memory_order_relaxed); }
    uint64_t getCacheMisses() const { return m_cache_misses.load(std::memory_order_relaxed); }

private:
    // Query SQLite and insert into LRU cache, return pointer
    SpellData* queryAndCache(uint32_t spell_id) const;

    // Evict oldest cache entry if at capacity
    void evictIfNeeded() const;

    // SQLite database (non-owning, from global instance)
    EQT::WilleqDatabase* m_db = nullptr;

    // LRU cache (mutable because getSpell is const)
    mutable std::unordered_map<uint32_t, SpellData> m_cache;
    mutable std::list<uint32_t> m_lru;
    mutable std::unordered_map<uint32_t, std::list<uint32_t>::iterator> m_lru_map;
    size_t m_cache_capacity = 256;

    // Stats
    mutable std::atomic<uint64_t> m_cache_hits{0};
    mutable std::atomic<uint64_t> m_cache_misses{0};

    // State
    bool m_loaded = false;
    std::string m_load_error;
    mutable uint32_t m_max_spell_id = 0;
};

} // namespace EQ

#endif // EQT_SPELL_DATABASE_H
