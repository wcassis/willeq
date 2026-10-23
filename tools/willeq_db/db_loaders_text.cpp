// db_loaders_text.cpp — willeq_db_builder loaders over the EQ client's *.txt
// data files (T447; bodies moved verbatim from tools/willeq_db_builder.cpp).
//
//   spells_us.txt   -> spells
//   eqstr_us.txt    -> eqstrings
//   dbstr_us.txt    -> dbstrings
//   SoundAssets.txt -> sound_assets
//   AnimationSounds.txt -> animation_sounds
//   mp3index.txt    -> music_index

#include "willeq_db/db_builder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "client/spell/spell_constants.h"

// ============================================================================
// Spell loader (spells_us.txt)
// ============================================================================

int loadSpells(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS spells (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            player_tag TEXT,
            teleport_zone TEXT,
            cast_on_you TEXT,
            cast_on_other TEXT,
            spell_fades TEXT,
            cast_time_ms INTEGER,
            recovery_time_ms INTEGER,
            recast_time_ms INTEGER,
            duration_formula INTEGER,
            duration_cap INTEGER,
            mana_cost INTEGER,
            target_type INTEGER,
            range_val REAL,
            aoe_range REAL,
            resist_type INTEGER,
            resist_diff INTEGER,
            casting_skill INTEGER,
            is_beneficial INTEGER,
            spell_icon INTEGER,
            gem_icon INTEGER,
            cast_anim INTEGER,
            zone_type INTEGER,
            class_levels BLOB,
            effect_id_1 INTEGER, effect_id_2 INTEGER, effect_id_3 INTEGER,
            effect_id_4 INTEGER, effect_id_5 INTEGER, effect_id_6 INTEGER,
            effect_id_7 INTEGER, effect_id_8 INTEGER, effect_id_9 INTEGER,
            effect_id_10 INTEGER, effect_id_11 INTEGER, effect_id_12 INTEGER,
            base_value_1 INTEGER, base_value_2 INTEGER, base_value_3 INTEGER,
            base_value_4 INTEGER, base_value_5 INTEGER, base_value_6 INTEGER,
            base_value_7 INTEGER, base_value_8 INTEGER, base_value_9 INTEGER,
            base_value_10 INTEGER, base_value_11 INTEGER, base_value_12 INTEGER,
            base2_value_1 INTEGER, base2_value_2 INTEGER, base2_value_3 INTEGER,
            base2_value_4 INTEGER, base2_value_5 INTEGER, base2_value_6 INTEGER,
            base2_value_7 INTEGER, base2_value_8 INTEGER, base2_value_9 INTEGER,
            base2_value_10 INTEGER, base2_value_11 INTEGER, base2_value_12 INTEGER,
            max_value_1 INTEGER, max_value_2 INTEGER, max_value_3 INTEGER,
            max_value_4 INTEGER, max_value_5 INTEGER, max_value_6 INTEGER,
            max_value_7 INTEGER, max_value_8 INTEGER, max_value_9 INTEGER,
            max_value_10 INTEGER, max_value_11 INTEGER, max_value_12 INTEGER,
            formula_1 INTEGER, formula_2 INTEGER, formula_3 INTEGER,
            formula_4 INTEGER, formula_5 INTEGER, formula_6 INTEGER,
            formula_7 INTEGER, formula_8 INTEGER, formula_9 INTEGER,
            formula_10 INTEGER, formula_11 INTEGER, formula_12 INTEGER,
            spell_affect_index INTEGER,
            reagent_id_1 INTEGER, reagent_id_2 INTEGER, reagent_id_3 INTEGER, reagent_id_4 INTEGER,
            reagent_count_1 INTEGER, reagent_count_2 INTEGER, reagent_count_3 INTEGER, reagent_count_4 INTEGER,
            noexpend_reagent_1 INTEGER, noexpend_reagent_2 INTEGER, noexpend_reagent_3 INTEGER, noexpend_reagent_4 INTEGER,
            endurance_cost INTEGER,
            target_anim INTEGER,
            travel_type INTEGER,
            spell_anim INTEGER
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO spells (id, name, player_tag, teleport_zone, "
        "cast_on_you, cast_on_other, spell_fades, cast_time_ms, recovery_time_ms, "
        "recast_time_ms, duration_formula, duration_cap, mana_cost, target_type, "
        "range_val, aoe_range, resist_type, resist_diff, casting_skill, "
        "is_beneficial, spell_icon, gem_icon, cast_anim, zone_type, class_levels, "
        "effect_id_1, effect_id_2, effect_id_3, effect_id_4, effect_id_5, effect_id_6, "
        "effect_id_7, effect_id_8, effect_id_9, effect_id_10, effect_id_11, effect_id_12, "
        "base_value_1, base_value_2, base_value_3, base_value_4, base_value_5, base_value_6, "
        "base_value_7, base_value_8, base_value_9, base_value_10, base_value_11, base_value_12, "
        "base2_value_1, base2_value_2, base2_value_3, base2_value_4, base2_value_5, base2_value_6, "
        "base2_value_7, base2_value_8, base2_value_9, base2_value_10, base2_value_11, base2_value_12, "
        "max_value_1, max_value_2, max_value_3, max_value_4, max_value_5, max_value_6, "
        "max_value_7, max_value_8, max_value_9, max_value_10, max_value_11, max_value_12, "
        "formula_1, formula_2, formula_3, formula_4, formula_5, formula_6, "
        "formula_7, formula_8, formula_9, formula_10, formula_11, formula_12, "
        "spell_affect_index, "
        "reagent_id_1, reagent_id_2, reagent_id_3, reagent_id_4, "
        "reagent_count_1, reagent_count_2, reagent_count_3, reagent_count_4, "
        "noexpend_reagent_1, noexpend_reagent_2, noexpend_reagent_3, noexpend_reagent_4, "
        "endurance_cost, target_anim, travel_type, spell_anim) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,"
        "?,?,?,?,?,?,?,?,?,?,?,?,"
        "?,?,?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (readLine(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto fields = split(line, '^');
        if (fields.size() < 200) continue;  // spells_us.txt has 200+ fields per line

        int id = std::atoi(fields[0].c_str());
        if (id <= 0) continue;

        // Field indices from spell_constants.h SpellField namespace
        // These are the authoritative field positions for Titanium spells_us.txt
        auto safeStr = [&](int idx) -> const char* {
            return (idx < static_cast<int>(fields.size())) ? fields[idx].c_str() : "";
        };
        auto safeInt = [&](int idx) -> int {
            return (idx < static_cast<int>(fields.size())) ? std::atoi(fields[idx].c_str()) : 0;
        };
        auto safeDbl = [&](int idx) -> double {
            return (idx < static_cast<int>(fields.size())) ? std::atof(fields[idx].c_str()) : 0.0;
        };

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_bind_text(stmt, 2, safeStr(1), -1, SQLITE_TRANSIENT);     // Name
        sqlite3_bind_text(stmt, 3, safeStr(2), -1, SQLITE_TRANSIENT);     // PlayerTag
        sqlite3_bind_text(stmt, 4, safeStr(3), -1, SQLITE_TRANSIENT);     // TeleportZone
        sqlite3_bind_text(stmt, 5, safeStr(4), -1, SQLITE_TRANSIENT);     // CastOnYou
        sqlite3_bind_text(stmt, 6, safeStr(5), -1, SQLITE_TRANSIENT);     // CastOnOther
        sqlite3_bind_text(stmt, 7, safeStr(7), -1, SQLITE_TRANSIENT);     // SpellFades
        sqlite3_bind_int(stmt, 8, safeInt(13));                             // CastTime
        sqlite3_bind_int(stmt, 9, safeInt(14));                             // RecoveryTime
        sqlite3_bind_int(stmt, 10, safeInt(15));                            // RecastTime
        sqlite3_bind_int(stmt, 11, safeInt(16));                            // DurationFormula
        sqlite3_bind_int(stmt, 12, safeInt(17));                            // DurationCap
        sqlite3_bind_int(stmt, 13, safeInt(19));                            // ManaCost
        sqlite3_bind_int(stmt, 14, safeInt(98));                            // TargetType
        sqlite3_bind_double(stmt, 15, safeDbl(9));                          // Range
        sqlite3_bind_double(stmt, 16, safeDbl(10));                         // AoERange
        sqlite3_bind_int(stmt, 17, safeInt(85));                            // ResistType
        sqlite3_bind_int(stmt, 18, safeInt(99));                            // BaseDifficulty (resist diff)
        sqlite3_bind_int(stmt, 19, safeInt(100));                           // CastingSkill
        sqlite3_bind_int(stmt, 20, safeInt(83));                            // GoodEffect (is_beneficial)
        // B1103 (GitLab #423): the spellbook's icon column is the client's
        // NEW_ICON -- field 144, eqemu spdat.h:1662: "Spell icon used by the
        // client in uifiles/default/spells??.tga, both for spell gems & buff
        // window". The pre-fix bind read field 143, which spdat.h:1661 calls
        // ai_pt_bonus, "always set to 0, client doesn't save this": 0 on all
        // 8,066 rows of the Titanium export, so every spellbook slot (and
        // every buff, and every shift-click spell display) addressed cell 0
        // of its family while the gem bar -- fed from field 144 -- drew
        // per-spell art. Field 144 now feeds BOTH columns; the gem reader
        // (gem_icon) is untouched, and the duplication is deliberate until a
        // ruling drops one (refactor/B1103/README.md).
        sqlite3_bind_int(stmt, 21, safeInt(144));                           // NewIcon (spell_icon) -- B1103 rebind, was 143
        sqlite3_bind_int(stmt, 22, safeInt(144));                           // NewIcon (gem_icon)
        sqlite3_bind_int(stmt, 23, safeInt(120));                           // CastAnim
        sqlite3_bind_int(stmt, 24, safeInt(101));                           // ZoneType

        // Class minimum levels (16 classes, fields 104-119, one per class in
        // PlayerClass enum order: Warrior..Berserker). 255 = class cannot use
        // the spell; must round-trip exactly (no clamping/sign conversion).
        uint8_t classLevels[EQ::NUM_CLASSES];
        for (int c = 0; c < EQ::NUM_CLASSES; ++c) {
            int v = safeInt(EQ::SpellField::ClassLevel1 + c);
            classLevels[c] = static_cast<uint8_t>(v < 0 ? 255 : (v > 255 ? 255 : v));
        }
        sqlite3_bind_blob(stmt, 25, classLevels, sizeof(classLevels), SQLITE_TRANSIENT);

        // Effect slots (12 per spell): fields are read and stored exactly as
        // they appear in spells_us.txt, markers included (effect id 254 =
        // slot unused, formula 100 = unused). Normalising markers is the
        // consumer's job (SpellDatabase::queryAndCache) — doing it here too
        // is how the two-places-disagree drift this project keeps finding
        // gets started. See spell_constants.h SpellField for field indices.
        int bindIdx = 26;
        for (int slot = 0; slot < EQ::MAX_SPELL_EFFECTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::EffectId1 + slot));
        }
        for (int slot = 0; slot < EQ::MAX_SPELL_EFFECTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::BaseValue1 + slot));
        }
        for (int slot = 0; slot < EQ::MAX_SPELL_EFFECTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::Base2Value1 + slot));
        }
        for (int slot = 0; slot < EQ::MAX_SPELL_EFFECTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::MaxValue1 + slot));
        }
        for (int slot = 0; slot < EQ::MAX_SPELL_EFFECTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::Formula1 + slot));
        }
        sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::SpellAffectIndex));

        // Reagents (T246): fields 58-61 (item id, -1 = none), 62-65 (count),
        // 66-69 (no-expend reagent item id, -1 = none). Stored verbatim,
        // -1 markers included; SpellDatabase::queryAndCache normalises.
        for (int slot = 0; slot < EQ::MAX_SPELL_REAGENTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::ReagentId1 + slot));
        }
        for (int slot = 0; slot < EQ::MAX_SPELL_REAGENTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::ReagentCount1 + slot));
        }
        for (int slot = 0; slot < EQ::MAX_SPELL_REAGENTS; ++slot) {
            sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::NoExpendReagent1 + slot));
        }

        // T355: endurance cost, spells_us.txt field 166 (= eqemu
        // spells_new.EndurCost -- that table is a positional import of this
        // same file). Verbatim: 0 means the spell costs no endurance.
        sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::EnduranceCost));

        // T384: the two remaining per-spell VISUAL fields, spells_us.txt
        // fields 121 and 122 (= eqemu spells_new.TargetAnim / .TravelType at
        // the same ordinals -- Columns()[121] and [122] in
        // /mnt/projects/akk-stack/code/common/repositories/base/base_spells_new_repository.h;
        // CastingAnim at [120] is the cast_anim already bound
        // above). Verbatim, like every other spell column: target_anim is
        // the impact animation id (non-zero for 3,608 of 8,066 spells) and
        // travel_type == 3 marks the 85 spells with a projectile travel
        // stage. Board B163.
        sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::TargetAnim));
        sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::TravelType));
        // B1127 (#447): field 145, the spellsnew.eff record the official
        // client draws (eqemu spells_new.spellanim). Verbatim.
        sqlite3_bind_int(stmt, bindIdx++, safeInt(EQ::SpellField::SpellAnim));

        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    execSQL(db, "CREATE INDEX IF NOT EXISTS idx_spells_name ON spells(name)");
    return count;
}

// ============================================================================
// String loader (eqstr_us.txt)
// ============================================================================

int loadEqStrings(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS eqstrings (
            id INTEGER PRIMARY KEY,
            text TEXT NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO eqstrings (id, text) VALUES (?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (readLine(file, line)) {
        if (line.empty()) continue;

        // Format: "id text_content" (space separated, first token is ID)
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;

        int id = std::atoi(line.substr(0, space).c_str());
        if (id <= 0) continue;

        std::string text = line.substr(space + 1);

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_bind_text(stmt, 2, text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// DB String loader (dbstr_us.txt)
// ============================================================================

int loadDbStrings(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS dbstrings (
            category INTEGER NOT NULL,
            sub_id INTEGER NOT NULL,
            text TEXT NOT NULL,
            PRIMARY KEY (category, sub_id)
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO dbstrings (category, sub_id, text) VALUES (?,?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (readLine(file, line)) {
        if (line.empty()) continue;

        auto fields = split(line, '^');
        if (fields.size() < 3) continue;

        int category = std::atoi(fields[0].c_str());
        int sub_id = std::atoi(fields[1].c_str());
        const std::string& text = fields[2];

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, category);
        sqlite3_bind_int(stmt, 2, sub_id);
        sqlite3_bind_text(stmt, 3, text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// Sound assets loader (SoundAssets.txt)
// ============================================================================

int loadSoundAssets(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS sound_assets (
            id INTEGER PRIMARY KEY,
            filename TEXT NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO sound_assets (id, filename) VALUES (?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (readLine(file, line)) {
        if (line.empty()) continue;
        auto fields = split(line, '^');
        if (fields.size() < 2) continue;

        int id = std::atoi(fields[0].c_str());
        const std::string& filename = fields[1];

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_bind_text(stmt, 2, filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// Animation sounds loader (AnimationSounds.txt)
// ============================================================================

int loadAnimationSounds(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS animation_sounds (
            animation_name TEXT PRIMARY KEY,
            sound_id INTEGER NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO animation_sounds (animation_name, sound_id) VALUES (?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (readLine(file, line)) {
        if (line.empty()) continue;
        auto fields = split(line, '^');
        if (fields.size() < 2) continue;

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, fields[0].c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, std::atoi(fields[1].c_str()));
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// Race data loader (RaceData.txt) -- DELETED (T395, board B216)
// ============================================================================
//
// loadRaceData() built a `race_data` table of 475 (race_id, gender,
// raw_fields) rows into every willeq.db, where raw_fields was the whole
// RaceData.txt line stored verbatim. Nothing ever read it: a grep for
// `race_data` and `getRaceData` across src/ include/ tests/ tools/
// benchmarks/ docker/ scripts/ found only this file, and because the line was
// never parsed there was no consumer to preserve either. Removed with the
// table; WilleqDatabase::open() validates metadata.version and never a table
// list, so this was content-only and kWilleqDbExpectedVersion did not move.
// Pinned by WilleqDatabaseShippedContentTest.RaceDataTableIsGone. Do not
// rebuild it without a named reader.

// ============================================================================
// Music index loader (mp3index.txt)
// ============================================================================

int loadMusicIndex(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS music_files (
            id INTEGER PRIMARY KEY,
            filename TEXT NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO music_files (id, filename) VALUES (?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (readLine(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, count);
        sqlite3_bind_text(stmt, 2, line.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// B606 (#189): the login_strings loader (eqlsstr_us.txt -> login_strings) is
// deleted. The table's only client reader, WilleqDatabase::getLoginString(),
// had zero production callers.
