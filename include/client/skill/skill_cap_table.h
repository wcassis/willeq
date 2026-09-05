/*
 * WillEQ Skill Cap Table (T159)
 *
 * The client-side answer to one question: "can this class EVER have this
 * skill?" It is backed by config/skill_caps.json, which the orchestrator
 * exported from the live server's `peq.skill_caps` (58,359 raw rows, 16
 * classes, 76 skills, levels 1-105) collapsed to one row per
 * (class_id, skill_id) carrying `first_level` and `max_cap`.
 *
 * WHY POSSESSION AND NOT LEVEL (this is the whole design; see T158's Review):
 *   eqemu's per-skill handlers do not agree with each other about HasSkill.
 *   Handle_OP_Taunt consults it, so a Rogue -- who has no skill_caps row for
 *   Taunt at ANY level -- can never taunt and the server silently bins the
 *   packet. Handle_OP_ApplyPoison does NOT consult it; it checks the item and
 *   GetClass() == Rogue, so a level-10 Rogue with Apply Poison at 0 applies
 *   poison successfully even though the rogue cap for that skill only starts
 *   at level 18. A client gate keyed on "cap at my current level > 0" would
 *   therefore refuse a send that demonstrably works.
 *
 *   So the gate is PRESENCE of the (class, skill) pair, never the level
 *   threshold. `first_level` is loaded and exposed for information only --
 *   nothing in the activation path reads it.
 *
 * `max_cap` is the LIFETIME cap for the class, not the cap at the player's
 * current level: the per-level curve was collapsed out of the export and
 * cannot be recovered from this file.
 *
 * FAILURE MODE: if the file is missing or unparseable the table stays
 * `!isLoaded()` and every caller must fall back to the pre-T159 permissive
 * behaviour (allow everything). A data regression must never brick the skill
 * system by refusing every skill.
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace EQ {

class SkillCapTable {
public:
    // One entry per (class_id, skill_id) present in the export.
    struct Entry {
        uint8_t  first_level = 1;   // MIN(level) with cap>0. INFORMATIONAL ONLY.
        uint32_t max_cap = 0;       // MAX(cap) over all levels; lifetime, not per-level.
    };

    // Path shipped with the client, in the style of config/race_models.json
    // and config/spell_effects.json. T399 (board B235): repo-relative, NOT
    // working-directory-relative -- SkillCapTable::load() resolves it through
    // eqt::ResolveDataPath().
    static constexpr const char* kDefaultPath = "config/skill_caps.json";

    static SkillCapTable& instance();

    // Load (or reload) the table. Logs loudly and returns false on any
    // failure, leaving isLoaded() false so callers stay permissive.
    bool load(const std::string& path = kDefaultPath);

    // Load once per process from kDefaultPath. Repeat calls are no-ops once a
    // load has been ATTEMPTED, successful or not -- a broken file must not be
    // re-parsed (and re-logged) on every level-up.
    void ensureLoaded();

    bool isLoaded() const { return m_loaded; }
    const std::string& getPath() const { return m_path; }
    size_t entryCount() const { return m_entries.size(); }

    // THE PREDICATE. True if the export carries a row for this pair at any
    // level. When the table failed to load this returns true for every id in
    // range -- the permissive fallback.
    bool classCanEverHaveSkill(uint8_t class_id, uint8_t skill_id) const;

    // nullptr when the pair is absent (or the table is not loaded).
    const Entry* getEntry(uint8_t class_id, uint8_t skill_id) const;

private:
    SkillCapTable() = default;

    static uint32_t key(uint8_t class_id, uint8_t skill_id) {
        return (static_cast<uint32_t>(class_id) << 8) | skill_id;
    }

    std::unordered_map<uint32_t, Entry> m_entries;
    std::string m_path;
    bool m_loaded = false;
    bool m_load_attempted = false;
};

} // namespace EQ
