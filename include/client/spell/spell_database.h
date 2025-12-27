/*
 * WillEQ Spell Database
 *
 * Loads and manages spell definitions from the EQ client spells_us.txt file.
 */

#ifndef EQT_SPELL_DATABASE_H
#define EQT_SPELL_DATABASE_H

#include "spell_data.h"
#include "spell_constants.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace EQ {

class SpellDatabase {
public:
    SpellDatabase();
    ~SpellDatabase();

    // ========================================================================
    // Loading
    // ========================================================================

    // Load spells from EQ client spells_us.txt file
    // Returns true if loading succeeded, false on error
    bool loadFromFile(const std::string& filepath);

    // Check if database is loaded
    bool isLoaded() const { return m_loaded; }

    // Get load error message (if loadFromFile returned false)
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

    // Get total number of loaded spells
    size_t getSpellCount() const { return m_spells.size(); }

    // Get all spell IDs
    std::vector<uint32_t> getAllSpellIds() const;

    // Get the highest spell ID
    uint32_t getMaxSpellId() const { return m_max_spell_id; }

private:
    // Parse a single line from spells_us.txt
    bool parseSpellLine(const std::string& line, SpellData& spell);

    // Parse individual fields
    bool parseNumericField(const std::string& value, int32_t& result);
    bool parseFloatField(const std::string& value, float& result);

    // Build lookup indices after loading
    void buildIndices();

    // Convert name to lowercase for case-insensitive lookup
    static std::string toLower(const std::string& str);

    // Storage
    std::unordered_map<uint32_t, SpellData> m_spells;

    // Indices for faster lookups
    std::unordered_map<std::string, uint32_t> m_name_to_id;  // lowercase name -> id

    // State
    bool m_loaded = false;
    std::string m_load_error;
    uint32_t m_max_spell_id = 0;
};

} // namespace EQ

#endif // EQT_SPELL_DATABASE_H
