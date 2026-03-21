/*
 * WillEQ Spell Database
 *
 * Provides spell lookups backed by WilleqDatabase (SQLite).
 * Maintains an LRU cache of recently-queried SpellData objects to provide
 * stable pointers for callers that expect const SpellData*.
 */

#ifndef EQT_SPELL_DATABASE_H
#define EQT_SPELL_DATABASE_H

#include "spell_data.h"
#include "spell_constants.h"
#include <unordered_map>
#include <list>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <atomic>

namespace EQT { class WilleqDatabase; }

namespace EQ {

class SpellDatabase {
public:
    SpellDatabase();
    ~SpellDatabase();

    // ========================================================================
    // Initialization
    // ========================================================================

    // Initialize from WilleqDatabase global instance (SQLite-backed, preferred).
    // Replaces loadFromFile() — the database must already be open.
    bool initFromDatabase();

    // Legacy: Load spells from EQ client spells_us.txt file
    // DEPRECATED — use initFromDatabase() instead
    bool loadFromFile(const std::string& filepath);

    // Check if database is initialized
    bool isLoaded() const { return m_loaded; }

    // Get load error message
    const std::string& getLoadError() const { return m_load_error; }

    // ========================================================================
    // Lookup by ID
    // ========================================================================

    // Get spell by ID (returns nullptr if not found)
    const SpellData* getSpell(uint32_t spell_id) const;

    // Check if a spell ID exists
    bool hasSpell(uint32_t spell_id) const;

    // ========================================================================
    // Lookup by Name
    // ========================================================================

    // Get spell by exact name (case-insensitive)
    const SpellData* getSpellByName(const std::string& name) const;

    // Find spells matching a partial name (case-insensitive)
    std::vector<const SpellData*> findSpellsByName(const std::string& partial_name) const;

    // ========================================================================
    // Filtering
    // ========================================================================

    // Get all spells usable by a class at a given level
    std::vector<const SpellData*> getSpellsForClass(PlayerClass pc, uint8_t level) const;

    // Get all spells of a specific school
    std::vector<const SpellData*> getSpellsBySchool(SpellSchool school) const;

    // Get all spells with a specific effect type
    std::vector<const SpellData*> getSpellsByEffect(SpellEffect effect) const;

    // Get all spells within a level range for a class
    std::vector<const SpellData*> getSpellsByLevelRange(
        PlayerClass pc, uint8_t min_level, uint8_t max_level) const;

    // Get all beneficial spells
    std::vector<const SpellData*> getBeneficialSpells() const;

    // Get all detrimental spells
    std::vector<const SpellData*> getDetrimentalSpells() const;

    // Custom filter using a predicate
    std::vector<const SpellData*> filterSpells(
        std::function<bool(const SpellData&)> predicate) const;

    // ========================================================================
    // Statistics
    // ========================================================================

    // Get total number of spells
    size_t getSpellCount() const;

    // Get all spell IDs
    std::vector<uint32_t> getAllSpellIds() const;

    // Get the highest spell ID
    uint32_t getMaxSpellId() const { return m_max_spell_id; }

    // Cache stats for /pmem
    size_t getCacheSize() const { return m_cache.size(); }
    size_t getCacheCapacity() const { return m_cache_capacity; }
    uint64_t getCacheHits() const { return m_cache_hits.load(std::memory_order_relaxed); }
    uint64_t getCacheMisses() const { return m_cache_misses.load(std::memory_order_relaxed); }

private:
    // Populate a SpellData from a WilleqDatabase SpellInfo query result
    SpellData* queryAndCache(uint32_t spell_id) const;

    // Cache management — evict LRU entry if at capacity
    void evictIfNeeded() const;

    // Convert name to lowercase for case-insensitive lookup
    static std::string toLower(const std::string& str);

    // Legacy file parsing (kept for loadFromFile compatibility)
    bool parseSpellLine(const std::string& line, SpellData& spell);
    bool parseNumericField(const std::string& value, int32_t& result);
    bool parseFloatField(const std::string& value, float& result);
    void buildIndices();

    // SQLite database (non-owning pointer)
    EQT::WilleqDatabase* m_db = nullptr;

    // LRU cache: spell_id -> SpellData (mutable because getSpell is const)
    mutable std::unordered_map<uint32_t, SpellData> m_cache;
    mutable std::list<uint32_t> m_lru;  // Front = most recent
    mutable std::unordered_map<uint32_t, std::list<uint32_t>::iterator> m_lru_map;
    size_t m_cache_capacity = 256;  // Max cached spells

    // Legacy storage (used only if loadFromFile is called)
    std::unordered_map<uint32_t, SpellData> m_spells;
    std::unordered_map<std::string, uint32_t> m_name_to_id;

    // Stats
    mutable std::atomic<uint64_t> m_cache_hits{0};
    mutable std::atomic<uint64_t> m_cache_misses{0};

    // State
    bool m_loaded = false;
    bool m_using_db = false;  // true = SQLite, false = legacy file
    std::string m_load_error;
    mutable uint32_t m_max_spell_id = 0;
};

} // namespace EQ

#endif // EQT_SPELL_DATABASE_H
