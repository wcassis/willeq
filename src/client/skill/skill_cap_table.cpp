/*
 * WillEQ Skill Cap Table implementation (T159)
 *
 * See include/client/skill/skill_cap_table.h for why this gates on presence
 * rather than on the level threshold.
 */

#include "client/skill/skill_cap_table.h"
#include "common/logging.h"
#include "common/packet_structs.h"

#include <json/json.h>
#include <cstdlib>
#include <fstream>

namespace EQ {

SkillCapTable& SkillCapTable::instance() {
    static SkillCapTable table;
    return table;
}

void SkillCapTable::ensureLoaded() {
    if (m_load_attempted) {
        return;
    }
    load(kDefaultPath);
}

bool SkillCapTable::load(const std::string& path) {
    m_load_attempted = true;
    m_loaded = false;
    m_entries.clear();
    m_path = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        // Loud on purpose: this is the "data regression" case. The skill
        // system stays permissive (pre-T159 behaviour) rather than refusing
        // every skill, but nobody should have to guess why the gate is off.
        LOG_ERROR(MOD_MAIN,
                  "SkillCapTable: cannot open '{}' -- skill possession gate DISABLED, "
                  "falling back to permissive behaviour (every class may use every skill)",
                  path);
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!Json::parseFromStream(builder, file, &root, &errors)) {
        LOG_ERROR(MOD_MAIN,
                  "SkillCapTable: '{}' is not parseable ({}) -- skill possession gate "
                  "DISABLED, falling back to permissive behaviour",
                  path, errors);
        return false;
    }

    const Json::Value& caps = root["caps"];
    if (!caps.isObject() || caps.empty()) {
        LOG_ERROR(MOD_MAIN,
                  "SkillCapTable: '{}' has no usable \"caps\" object -- skill possession "
                  "gate DISABLED, falling back to permissive behaviour",
                  path);
        return false;
    }

    for (const auto& class_key : caps.getMemberNames()) {
        const long class_id = std::strtol(class_key.c_str(), nullptr, 10);
        if (class_id <= 0 || class_id > 255) {
            LOG_WARN(MOD_MAIN, "SkillCapTable: skipping out-of-range class id '{}'", class_key);
            continue;
        }

        const Json::Value& skills = caps[class_key];
        if (!skills.isObject()) {
            LOG_WARN(MOD_MAIN, "SkillCapTable: class '{}' is not an object, skipped", class_key);
            continue;
        }

        for (const auto& skill_key : skills.getMemberNames()) {
            const long skill_id = std::strtol(skill_key.c_str(), nullptr, 10);
            if (skill_id < 0 || skill_id >= static_cast<long>(EQT::MAX_PP_SKILL)) {
                // Not fatal: the export can legitimately carry skill ids this
                // client's profile array has no room for.
                continue;
            }

            const Json::Value& pair = skills[skill_key];
            if (!pair.isArray() || pair.size() != 2) {
                LOG_WARN(MOD_MAIN, "SkillCapTable: class {} skill {} is not [first_level, max_cap], skipped",
                         class_key, skill_key);
                continue;
            }

            Entry entry;
            entry.first_level = static_cast<uint8_t>(pair[0].asUInt());
            entry.max_cap = pair[1].asUInt();
            m_entries[key(static_cast<uint8_t>(class_id), static_cast<uint8_t>(skill_id))] = entry;
        }
    }

    if (m_entries.empty()) {
        LOG_ERROR(MOD_MAIN,
                  "SkillCapTable: '{}' yielded zero usable rows -- skill possession gate "
                  "DISABLED, falling back to permissive behaviour",
                  path);
        return false;
    }

    m_loaded = true;
    LOG_INFO(MOD_MAIN, "SkillCapTable: loaded {} (class, skill) rows from '{}'",
             m_entries.size(), path);
    return true;
}

const SkillCapTable::Entry* SkillCapTable::getEntry(uint8_t class_id, uint8_t skill_id) const {
    if (!m_loaded) {
        return nullptr;
    }
    auto it = m_entries.find(key(class_id, skill_id));
    return (it == m_entries.end()) ? nullptr : &it->second;
}

bool SkillCapTable::classCanEverHaveSkill(uint8_t class_id, uint8_t skill_id) const {
    if (!m_loaded) {
        // Permissive fallback -- see the header's FAILURE MODE note.
        return skill_id < EQT::MAX_PP_SKILL;
    }
    return m_entries.find(key(class_id, skill_id)) != m_entries.end();
}

} // namespace EQ
