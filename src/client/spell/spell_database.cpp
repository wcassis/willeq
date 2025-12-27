/*
 * WillEQ Spell Database Implementation
 *
 * Parses the spells_us.txt file format from the EQ Titanium client.
 * Fields are delimited by '^' characters.
 */

#include "client/spell/spell_database.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace EQ {

SpellDatabase::SpellDatabase() = default;
SpellDatabase::~SpellDatabase() = default;

bool SpellDatabase::loadFromFile(const std::string& filepath)
{
    m_spells.clear();
    m_name_to_id.clear();
    m_loaded = false;
    m_load_error.clear();
    m_max_spell_id = 0;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        m_load_error = "Failed to open file: " + filepath;
        return false;
    }

    std::string line;
    int line_number = 0;
    int spells_loaded = 0;
    int spells_skipped = 0;

    while (std::getline(file, line)) {
        line_number++;

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        // Skip comment lines (if any)
        if (line[0] == '#') {
            continue;
        }

        SpellData spell;
        if (parseSpellLine(line, spell)) {
            // Only add valid spells (ID > 0, has a name)
            if (spell.id != SPELL_UNKNOWN && spell.id > 0 && !spell.name.empty()) {
                m_spells[spell.id] = std::move(spell);
                spells_loaded++;

                if (spell.id > m_max_spell_id) {
                    m_max_spell_id = spell.id;
                }
            } else {
                spells_skipped++;
            }
        } else {
            // Parse error on this line - skip it
            spells_skipped++;
        }
    }

    file.close();

    if (spells_loaded == 0) {
        m_load_error = "No valid spells found in file";
        return false;
    }

    // Build lookup indices
    buildIndices();

    m_loaded = true;
    return true;
}

bool SpellDatabase::parseSpellLine(const std::string& line, SpellData& spell)
{
    // Split line by '^' delimiter
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;

    while (std::getline(ss, field, '^')) {
        fields.push_back(field);
    }

    // Need at least enough fields for basic spell data
    if (fields.size() < 100) {
        return false;
    }

    // ========================================================================
    // Parse identification fields
    // ========================================================================

    int32_t id_val;
    if (!parseNumericField(fields[SpellField::ID], id_val) || id_val < 0) {
        return false;
    }
    spell.id = static_cast<uint32_t>(id_val);

    spell.name = fields[SpellField::Name];
    if (spell.name.empty()) {
        return false;  // Spell must have a name
    }

    if (fields.size() > SpellField::PlayerTag) {
        spell.player_tag = fields[SpellField::PlayerTag];
    }

    if (fields.size() > SpellField::TeleportZone) {
        spell.teleport_zone = fields[SpellField::TeleportZone];
    }

    // ========================================================================
    // Parse messages
    // ========================================================================

    if (fields.size() > SpellField::CastOnYou) {
        spell.cast_on_you = fields[SpellField::CastOnYou];
    }
    if (fields.size() > SpellField::CastOnOther) {
        spell.cast_on_other = fields[SpellField::CastOnOther];
    }
    if (fields.size() > SpellField::SpellFades) {
        spell.spell_fades = fields[SpellField::SpellFades];
    }

    // ========================================================================
    // Parse range and positioning
    // ========================================================================

    float fval;
    if (fields.size() > SpellField::Range && parseFloatField(fields[SpellField::Range], fval)) {
        spell.range = fval;
    }
    if (fields.size() > SpellField::AoERange && parseFloatField(fields[SpellField::AoERange], fval)) {
        spell.aoe_range = fval;
    }
    if (fields.size() > SpellField::PushBack && parseFloatField(fields[SpellField::PushBack], fval)) {
        spell.push_back = fval;
    }
    if (fields.size() > SpellField::PushUp && parseFloatField(fields[SpellField::PushUp], fval)) {
        spell.push_up = fval;
    }

    // ========================================================================
    // Parse timing
    // ========================================================================

    int32_t ival;
    if (fields.size() > SpellField::CastTime && parseNumericField(fields[SpellField::CastTime], ival)) {
        spell.cast_time_ms = static_cast<uint32_t>(std::max(0, ival));
    }
    if (fields.size() > SpellField::RecoveryTime && parseNumericField(fields[SpellField::RecoveryTime], ival)) {
        spell.recovery_time_ms = static_cast<uint32_t>(std::max(0, ival));
    }
    if (fields.size() > SpellField::RecastTime && parseNumericField(fields[SpellField::RecastTime], ival)) {
        spell.recast_time_ms = static_cast<uint32_t>(std::max(0, ival));
    }
    if (fields.size() > SpellField::DurationFormula && parseNumericField(fields[SpellField::DurationFormula], ival)) {
        spell.duration_formula = static_cast<uint8_t>(std::max(0, ival));
    }
    if (fields.size() > SpellField::DurationCap && parseNumericField(fields[SpellField::DurationCap], ival)) {
        spell.duration_cap = static_cast<uint32_t>(std::max(0, ival));
    }
    if (fields.size() > SpellField::AoEDuration && parseNumericField(fields[SpellField::AoEDuration], ival)) {
        spell.aoe_duration = static_cast<uint32_t>(std::max(0, ival));
    }

    // ========================================================================
    // Parse costs
    // ========================================================================

    if (fields.size() > SpellField::ManaCost && parseNumericField(fields[SpellField::ManaCost], ival)) {
        spell.mana_cost = ival;
    }

    // ========================================================================
    // Parse effect slots (12 slots each for base, base2, max, formula, effect_id)
    // ========================================================================

    for (int i = 0; i < MAX_SPELL_EFFECTS; i++) {
        SpellEffectSlot& effect = spell.effects[i];

        // Effect ID
        int effect_field = SpellField::EffectId1 + i;
        if (fields.size() > static_cast<size_t>(effect_field)) {
            if (parseNumericField(fields[effect_field], ival)) {
                // 254 and 255 are unused/invalid
                if (ival >= 0 && ival < 254) {
                    effect.effect_id = static_cast<SpellEffect>(ival);
                } else {
                    effect.effect_id = SpellEffect::InvalidEffect;
                }
            }
        }

        // Base value
        int base_field = SpellField::BaseValue1 + i;
        if (fields.size() > static_cast<size_t>(base_field)) {
            parseNumericField(fields[base_field], effect.base_value);
        }

        // Base2 value
        int base2_field = SpellField::Base2Value1 + i;
        if (fields.size() > static_cast<size_t>(base2_field)) {
            parseNumericField(fields[base2_field], effect.base2_value);
        }

        // Max value
        int max_field = SpellField::MaxValue1 + i;
        if (fields.size() > static_cast<size_t>(max_field)) {
            parseNumericField(fields[max_field], effect.max_value);
        }

        // Formula
        int formula_field = SpellField::Formula1 + i;
        if (fields.size() > static_cast<size_t>(formula_field)) {
            parseNumericField(fields[formula_field], effect.formula);
        }
    }

    // ========================================================================
    // Parse reagents (4 slots)
    // ========================================================================

    for (int i = 0; i < MAX_SPELL_REAGENTS; i++) {
        int reagent_id_field = SpellField::ReagentId1 + i;
        if (fields.size() > static_cast<size_t>(reagent_id_field)) {
            if (parseNumericField(fields[reagent_id_field], ival) && ival > 0) {
                spell.reagent_id[i] = static_cast<uint32_t>(ival);
            }
        }

        int reagent_count_field = SpellField::ReagentCount1 + i;
        if (fields.size() > static_cast<size_t>(reagent_count_field)) {
            if (parseNumericField(fields[reagent_count_field], ival) && ival > 0) {
                spell.reagent_count[i] = static_cast<uint32_t>(ival);
            }
        }

        int no_expend_field = SpellField::NoExpendReagent1 + i;
        if (fields.size() > static_cast<size_t>(no_expend_field)) {
            if (parseNumericField(fields[no_expend_field], ival) && ival > 0) {
                spell.no_expend_reagent[i] = static_cast<uint32_t>(ival);
            }
        }
    }

    // ========================================================================
    // Parse classification
    // ========================================================================

    if (fields.size() > SpellField::GoodEffect && parseNumericField(fields[SpellField::GoodEffect], ival)) {
        // 0 = detrimental, 1 = beneficial, 2 = beneficial group only
        spell.is_beneficial = (ival > 0);
    }

    if (fields.size() > SpellField::Activated && parseNumericField(fields[SpellField::Activated], ival)) {
        spell.activated = (ival != 0);
    }

    if (fields.size() > SpellField::ResistType && parseNumericField(fields[SpellField::ResistType], ival)) {
        if (ival >= 0 && ival <= 9) {
            spell.resist_type = static_cast<ResistType>(ival);
        }
    }

    if (fields.size() > SpellField::TargetType && parseNumericField(fields[SpellField::TargetType], ival)) {
        spell.target_type = static_cast<SpellTargetType>(ival);
    }

    if (fields.size() > SpellField::BaseDifficulty && parseNumericField(fields[SpellField::BaseDifficulty], ival)) {
        spell.resist_diff = static_cast<int16_t>(ival);
    }

    if (fields.size() > SpellField::CastingSkill && parseNumericField(fields[SpellField::CastingSkill], ival)) {
        spell.casting_skill = static_cast<CastingSkill>(ival);
    }

    if (fields.size() > SpellField::ZoneType && parseNumericField(fields[SpellField::ZoneType], ival)) {
        spell.zone_type = static_cast<uint8_t>(ival);
    }

    if (fields.size() > SpellField::EnvironmentType && parseNumericField(fields[SpellField::EnvironmentType], ival)) {
        spell.environment_type = static_cast<uint8_t>(ival);
    }

    if (fields.size() > SpellField::TimeOfDay && parseNumericField(fields[SpellField::TimeOfDay], ival)) {
        spell.time_of_day = static_cast<uint8_t>(ival);
    }

    // ========================================================================
    // Parse class levels (16 classes)
    // ========================================================================

    for (int i = 0; i < NUM_CLASSES; i++) {
        int class_field = SpellField::ClassLevel1 + i;
        if (fields.size() > static_cast<size_t>(class_field)) {
            if (parseNumericField(fields[class_field], ival)) {
                // 255 or 254 means class can't use the spell
                if (ival > 0 && ival < 255) {
                    spell.class_levels[i] = static_cast<uint8_t>(ival);
                } else {
                    spell.class_levels[i] = 255;
                }
            }
        }
    }

    // ========================================================================
    // Parse animations and icons
    // ========================================================================

    if (fields.size() > SpellField::CastAnim && parseNumericField(fields[SpellField::CastAnim], ival)) {
        spell.cast_anim = static_cast<uint16_t>(std::max(0, ival));
    }

    if (fields.size() > SpellField::TargetAnim && parseNumericField(fields[SpellField::TargetAnim], ival)) {
        spell.target_anim = static_cast<uint16_t>(std::max(0, ival));
    }

    if (fields.size() > SpellField::SpellIcon && parseNumericField(fields[SpellField::SpellIcon], ival)) {
        spell.spell_icon = static_cast<uint16_t>(std::max(0, ival));
    }

    if (fields.size() > SpellField::NewIcon && parseNumericField(fields[SpellField::NewIcon], ival)) {
        spell.gem_icon = static_cast<uint16_t>(std::max(0, ival));
    }

    // ========================================================================
    // Parse stacking info
    // ========================================================================

    if (fields.size() > SpellField::SpellGroup && parseNumericField(fields[SpellField::SpellGroup], ival)) {
        spell.spell_group = static_cast<int16_t>(ival);
    }

    if (fields.size() > SpellField::SpellRank && parseNumericField(fields[SpellField::SpellRank], ival)) {
        spell.spell_rank = static_cast<int16_t>(ival);
    }

    // ========================================================================
    // Parse additional flags
    // ========================================================================

    if (fields.size() > SpellField::Dispellable && parseNumericField(fields[SpellField::Dispellable], ival)) {
        spell.is_dispellable = (ival != 0);
    }

    return true;
}

bool SpellDatabase::parseNumericField(const std::string& value, int32_t& result)
{
    if (value.empty()) {
        result = 0;
        return true;
    }

    try {
        result = std::stoi(value);
        return true;
    } catch (...) {
        result = 0;
        return false;
    }
}

bool SpellDatabase::parseFloatField(const std::string& value, float& result)
{
    if (value.empty()) {
        result = 0.0f;
        return true;
    }

    try {
        result = std::stof(value);
        return true;
    } catch (...) {
        result = 0.0f;
        return false;
    }
}

void SpellDatabase::buildIndices()
{
    m_name_to_id.clear();

    for (const auto& [id, spell] : m_spells) {
        std::string lower_name = toLower(spell.name);
        m_name_to_id[lower_name] = id;
    }
}

std::string SpellDatabase::toLower(const std::string& str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

const SpellData* SpellDatabase::getSpell(uint32_t spell_id) const
{
    auto it = m_spells.find(spell_id);
    if (it != m_spells.end()) {
        return &it->second;
    }
    return nullptr;
}

bool SpellDatabase::hasSpell(uint32_t spell_id) const
{
    return m_spells.find(spell_id) != m_spells.end();
}

const SpellData* SpellDatabase::getSpellByName(const std::string& name) const
{
    std::string lower_name = toLower(name);
    auto it = m_name_to_id.find(lower_name);
    if (it != m_name_to_id.end()) {
        return getSpell(it->second);
    }
    return nullptr;
}

std::vector<const SpellData*> SpellDatabase::findSpellsByName(const std::string& partial_name) const
{
    std::vector<const SpellData*> results;
    std::string lower_partial = toLower(partial_name);

    for (const auto& [id, spell] : m_spells) {
        std::string lower_name = toLower(spell.name);
        if (lower_name.find(lower_partial) != std::string::npos) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::getSpellsForClass(PlayerClass pc, uint8_t level) const
{
    std::vector<const SpellData*> results;

    for (const auto& [id, spell] : m_spells) {
        if (spell.canClassUse(pc, level)) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::getSpellsBySchool(SpellSchool school) const
{
    std::vector<const SpellData*> results;

    for (const auto& [id, spell] : m_spells) {
        if (spell.getSchool() == school) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::getSpellsByEffect(SpellEffect effect) const
{
    std::vector<const SpellData*> results;

    for (const auto& [id, spell] : m_spells) {
        if (spell.hasEffect(effect)) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::getSpellsByLevelRange(
    PlayerClass pc, uint8_t min_level, uint8_t max_level) const
{
    std::vector<const SpellData*> results;
    uint8_t class_idx = static_cast<uint8_t>(pc);

    if (class_idx == 0 || class_idx > NUM_CLASSES) {
        return results;
    }

    for (const auto& [id, spell] : m_spells) {
        uint8_t spell_level = spell.class_levels[class_idx - 1];
        if (spell_level != 255 && spell_level >= min_level && spell_level <= max_level) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::getBeneficialSpells() const
{
    std::vector<const SpellData*> results;

    for (const auto& [id, spell] : m_spells) {
        if (spell.is_beneficial) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::getDetrimentalSpells() const
{
    std::vector<const SpellData*> results;

    for (const auto& [id, spell] : m_spells) {
        if (!spell.is_beneficial) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<const SpellData*> SpellDatabase::filterSpells(
    std::function<bool(const SpellData&)> predicate) const
{
    std::vector<const SpellData*> results;

    for (const auto& [id, spell] : m_spells) {
        if (predicate(spell)) {
            results.push_back(&spell);
        }
    }

    return results;
}

std::vector<uint32_t> SpellDatabase::getAllSpellIds() const
{
    std::vector<uint32_t> ids;
    ids.reserve(m_spells.size());

    for (const auto& [id, spell] : m_spells) {
        ids.push_back(id);
    }

    return ids;
}

} // namespace EQ
