// willeq_db_builder — Build SQLite database from EQ client data files
//
// Reads EQ client text files (spells, strings, etc.) and WillEQ JSON data files,
// writes a single SQLite database for runtime queries. This is the final stage
// in the data pipeline: source files remain editable, database is rebuilt on change.
//
// Usage:
//   ./willeq_db_builder --eq-path /path/to/EQ --data-path /path/to/willeq/data \
//       --config-path /path/to/willeq/config --output data/willeq.db
//
// Tables:
//   spells          - Spell data from spells_us.txt
//   eqstrings       - String lookups from eqstr_us.txt
//   dbstrings       - DB strings from dbstr_us.txt (category + sub_id)
//   item_models     - Item ID to model ID mapping
//   race_models     - Race ID to model code + S3D file
//   zone_points     - Zone line teleport destinations
//   s3d_assets      - S3D archive asset index (if s3d_index.json present)
//   sound_assets    - Sound file references from SoundAssets.txt

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sqlite3.h"

// ============================================================================
// Helpers
// ============================================================================

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (std::getline(stream, token, delim))
        tokens.push_back(token);
    return tokens;
}

static bool execSQL(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\nStatement: %s\n", err, sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

// ============================================================================
// Spell loader (spells_us.txt)
// ============================================================================

static int loadSpells(sqlite3* db, const std::string& filepath) {
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
            zone_type INTEGER
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO spells (id, name, player_tag, teleport_zone, "
        "cast_on_you, cast_on_other, spell_fades, cast_time_ms, recovery_time_ms, "
        "recast_time_ms, duration_formula, duration_cap, mana_cost, target_type, "
        "range_val, aoe_range, resist_type, resist_diff, casting_skill, "
        "is_beneficial, spell_icon, gem_icon, cast_anim, zone_type) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
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
        sqlite3_bind_int(stmt, 21, safeInt(143));                           // SpellIcon
        sqlite3_bind_int(stmt, 22, safeInt(144));                           // NewIcon (gem_icon)
        sqlite3_bind_int(stmt, 23, safeInt(120));                           // CastAnim
        sqlite3_bind_int(stmt, 24, safeInt(101));                           // ZoneType

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

static int loadEqStrings(sqlite3* db, const std::string& filepath) {
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
    while (std::getline(file, line)) {
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

static int loadDbStrings(sqlite3* db, const std::string& filepath) {
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
    while (std::getline(file, line)) {
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
// Item models loader (item_models.json)
// ============================================================================

static int loadItemModels(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS item_models (
            item_id INTEGER PRIMARY KEY,
            model_id INTEGER NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO item_models (item_id, model_id) VALUES (?,?)",
        -1, &stmt, nullptr);

    // Simple JSON parser for {"key": value} format
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    int count = 0;
    size_t pos = 0;
    while ((pos = content.find('"', pos)) != std::string::npos) {
        pos++;
        size_t end = content.find('"', pos);
        if (end == std::string::npos) break;
        std::string key = content.substr(pos, end - pos);
        pos = end + 1;

        // Skip to colon then value
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) break;
        pos = colon + 1;

        // Read integer value
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        int value = 0;
        bool hasDigit = false;
        while (pos < content.size() && isdigit(content[pos])) {
            value = value * 10 + (content[pos] - '0');
            pos++;
            hasDigit = true;
        }

        if (!hasDigit) continue;

        int itemId = std::atoi(key.c_str());
        if (itemId <= 0) continue;

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, itemId);
        sqlite3_bind_int(stmt, 2, value);
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// Race models loader (config/race_models.json)
// ============================================================================

static int loadRaceModels(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS race_models (
            race_id INTEGER PRIMARY KEY,
            code TEXT NOT NULL,
            name TEXT,
            female_code TEXT,
            male_code TEXT,
            neuter_code TEXT,
            s3d_file TEXT,
            fallback_code TEXT,
            animation_source TEXT,
            animation_source_s3d TEXT,
            scale REAL DEFAULT 1.0,
            gender_variants INTEGER DEFAULT 0,
            invisible INTEGER DEFAULT 0
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO race_models (race_id, code, name, female_code, "
        "male_code, neuter_code, s3d_file, fallback_code, animation_source, "
        "animation_source_s3d, scale, gender_variants, invisible) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, nullptr);

    // Parse JSON
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Find "races" object — simple parser for our known format
    size_t racesPos = content.find("\"races\"");
    if (racesPos == std::string::npos) {
        sqlite3_finalize(stmt);
        execSQL(db, "COMMIT");
        return 0;
    }

    int count = 0;
    // Find each race entry: "N": { ... }
    size_t pos = content.find('{', racesPos + 7);  // Opening brace of races object
    if (pos == std::string::npos) { sqlite3_finalize(stmt); execSQL(db, "COMMIT"); return 0; }
    pos++;

    while (pos < content.size()) {
        // Find race ID string
        size_t q1 = content.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) break;
        std::string raceIdStr = content.substr(q1 + 1, q2 - q1 - 1);

        // Skip metadata keys
        if (raceIdStr[0] == '_') { pos = q2 + 1; continue; }

        int raceId = 0;
        try { raceId = std::stoi(raceIdStr); } catch (...) { pos = q2 + 1; continue; }

        // Find the object for this race
        size_t objStart = content.find('{', q2);
        if (objStart == std::string::npos) break;
        size_t objEnd = content.find('}', objStart);
        if (objEnd == std::string::npos) break;
        std::string obj = content.substr(objStart, objEnd - objStart + 1);

        auto extractStr = [&](const char* key) -> std::string {
            std::string search = std::string("\"") + key + "\"";
            size_t kp = obj.find(search);
            if (kp == std::string::npos) return "";
            size_t vs = obj.find('"', kp + search.size() + 1);
            if (vs == std::string::npos) return "";
            size_t ve = obj.find('"', vs + 1);
            if (ve == std::string::npos) return "";
            return obj.substr(vs + 1, ve - vs - 1);
        };
        auto extractFloat = [&](const char* key, float def) -> float {
            std::string search = std::string("\"") + key + "\"";
            size_t kp = obj.find(search);
            if (kp == std::string::npos) return def;
            size_t colon = obj.find(':', kp);
            if (colon == std::string::npos) return def;
            return static_cast<float>(std::atof(obj.c_str() + colon + 1));
        };
        auto extractBool = [&](const char* key) -> int {
            std::string search = std::string("\"") + key + "\"";
            size_t kp = obj.find(search);
            if (kp == std::string::npos) return 0;
            size_t colon = obj.find(':', kp);
            if (colon == std::string::npos) return 0;
            size_t vs = obj.find_first_not_of(" \t", colon + 1);
            return (vs < obj.size() && obj[vs] == 't') ? 1 : 0;
        };

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, raceId);
        sqlite3_bind_text(stmt, 2, extractStr("code").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, extractStr("name").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, extractStr("female_code").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, extractStr("male_code").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, extractStr("neuter_code").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, extractStr("s3d_file").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, extractStr("fallback_code").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 9, extractStr("animation_source").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 10, extractStr("animation_source_s3d").c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 11, extractFloat("scale", 1.0f));
        sqlite3_bind_int(stmt, 12, extractBool("gender_variants"));
        sqlite3_bind_int(stmt, 13, extractBool("invisible"));

        sqlite3_step(stmt);
        count++;

        pos = objEnd + 1;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// Sound assets loader (SoundAssets.txt)
// ============================================================================

static int loadSoundAssets(sqlite3* db, const std::string& filepath) {
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
    while (std::getline(file, line)) {
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

static int loadAnimationSounds(sqlite3* db, const std::string& filepath) {
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
    while (std::getline(file, line)) {
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
// Race data loader (RaceData.txt)
// ============================================================================

static int loadRaceData(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    // RaceData.txt: race_id ^ gender ^ unknown ^ unknown ^ anim_ids...
    // Store as race_id -> full raw field data for flexibility
    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS race_data (
            race_id INTEGER NOT NULL,
            gender INTEGER NOT NULL,
            raw_fields TEXT NOT NULL,
            PRIMARY KEY (race_id, gender)
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO race_data (race_id, gender, raw_fields) VALUES (?,?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto fields = split(line, '^');
        if (fields.size() < 4) continue;

        int raceId = std::atoi(fields[0].c_str());
        int gender = std::atoi(fields[1].c_str());

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, raceId);
        sqlite3_bind_int(stmt, 2, gender);
        sqlite3_bind_text(stmt, 3, line.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        count++;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}

// ============================================================================
// Music index loader (mp3index.txt)
// ============================================================================

static int loadMusicIndex(sqlite3* db, const std::string& filepath) {
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
    while (std::getline(file, line)) {
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
// Login server strings loader (eqlsstr_us.txt)
// ============================================================================

static int loadLoginStrings(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS login_strings (
            id INTEGER PRIMARY KEY,
            text TEXT NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO login_strings (id, text) VALUES (?,?)",
        -1, &stmt, nullptr);

    std::string line;
    int count = 0;
    // First line is header (EQST0002), skip it
    bool headerSkipped = false;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (!headerSkipped && line.find("EQST") != std::string::npos) {
            headerSkipped = true;
            continue;
        }

        size_t space = line.find(' ');
        if (space == std::string::npos) continue;

        int id = std::atoi(line.substr(0, space).c_str());
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
// Zone character model lists loader (zone_chr.txt files)
// ============================================================================

static int loadZoneCharacters(sqlite3* db, const std::string& eqPath) {
    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS zone_characters (
            zone_name TEXT NOT NULL,
            model_code TEXT NOT NULL,
            source_file TEXT,
            PRIMARY KEY (zone_name, model_code)
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO zone_characters (zone_name, model_code, source_file) VALUES (?,?,?)",
        -1, &stmt, nullptr);

    // Find all *_chr.txt files
    std::string cmd = "ls " + eqPath + "/*_chr.txt 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        sqlite3_finalize(stmt);
        execSQL(db, "COMMIT");
        return 0;
    }

    int totalCount = 0;
    char pathBuf[1024];
    while (fgets(pathBuf, sizeof(pathBuf), pipe)) {
        std::string fullPath(pathBuf);
        while (!fullPath.empty() && (fullPath.back() == '\n' || fullPath.back() == '\r'))
            fullPath.pop_back();

        // Extract zone name from filename: /path/to/qeynos2_chr.txt -> qeynos2
        std::string filename = fullPath;
        size_t lastSlash = filename.rfind('/');
        if (lastSlash != std::string::npos) filename = filename.substr(lastSlash + 1);
        size_t chrPos = filename.find("_chr.txt");
        if (chrPos == std::string::npos) continue;
        std::string zoneName = filename.substr(0, chrPos);

        std::ifstream file(fullPath);
        if (!file.is_open()) continue;

        std::string line;
        // First line is count, skip it
        std::getline(file, line);

        while (std::getline(file, line)) {
            line = trim(line);
            if (line.empty()) continue;

            // Format: model_code,source_file (comma separated)
            auto parts = split(line, ',');
            std::string modelCode = parts[0];
            std::string sourceFile = (parts.size() > 1) ? parts[1] : "";

            // Lowercase
            std::transform(modelCode.begin(), modelCode.end(), modelCode.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            std::transform(sourceFile.begin(), sourceFile.end(), sourceFile.begin(),
                           [](unsigned char c) { return std::tolower(c); });

            sqlite3_reset(stmt);
            sqlite3_bind_text(stmt, 1, zoneName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, modelCode.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, sourceFile.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            totalCount++;
        }
    }

    pclose(pipe);
    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    execSQL(db, "CREATE INDEX IF NOT EXISTS idx_zone_chars_zone ON zone_characters(zone_name)");
    return totalCount;
}

// ============================================================================
// S3D asset index loader (s3d_index.json)
// ============================================================================

static int loadS3dIndex(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s (run s3d_index_builder --scan first)\n",
                filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS s3d_assets (
            name TEXT NOT NULL,
            archive TEXT NOT NULL,
            block_offset INTEGER NOT NULL,
            compressed_size INTEGER NOT NULL,
            uncompressed_size INTEGER NOT NULL,
            PRIMARY KEY (name, archive)
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO s3d_assets (name, archive, block_offset, "
        "compressed_size, uncompressed_size) VALUES (?,?,?,?,?)",
        -1, &stmt, nullptr);

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    // Parse the JSON — find "archives" section, iterate entries
    int count = 0;
    std::string currentArchive;

    size_t archivesPos = content.find("\"archives\"");
    if (archivesPos == std::string::npos) {
        sqlite3_finalize(stmt);
        execSQL(db, "COMMIT");
        return 0;
    }

    size_t pos = archivesPos;

    // Simple state machine — find archive name strings followed by arrays of entries
    while (pos < content.size()) {
        // Find next quoted string
        size_t q1 = content.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = content.find('"', q1 + 1);
        if (q2 == std::string::npos) break;

        std::string key = content.substr(q1 + 1, q2 - q1 - 1);
        pos = q2 + 1;

        // Check if followed by ':'
        size_t colon = content.find_first_not_of(" \t\r\n", pos);
        if (colon == std::string::npos) break;

        if (content[colon] == ':') {
            pos = colon + 1;
            size_t bracket = content.find_first_not_of(" \t\r\n", pos);
            if (bracket != std::string::npos && content[bracket] == '[') {
                // This is an archive with entries
                currentArchive = key;
                pos = bracket + 1;
                continue;
            }
        }

        // Check if this is a field inside an entry object
        if (key == "name" || key == "offset" || key == "compressed" || key == "uncompressed") {
            // We need to parse the full entry object — back up and find the { start
            // Instead, use a simpler approach: find entry objects by pattern
            continue;
        }

        if (key.find(".s3d") != std::string::npos) {
            currentArchive = key;
        }
    }

    // Second pass: use regex-like pattern matching for entries
    // Pattern: {"name": "xxx", "offset": N, "compressed": N, "uncompressed": N}
    pos = archivesPos;
    while (pos < content.size()) {
        // Find archive name (*.s3d followed by :[)
        size_t archStart = content.find(".s3d\"", pos);
        if (archStart == std::string::npos) break;

        // Find start of archive name
        size_t nameStart = content.rfind('"', archStart - 1);
        if (nameStart == std::string::npos) break;
        currentArchive = content.substr(nameStart + 1, archStart + 4 - nameStart - 1);

        // Find the array
        size_t arrayStart = content.find('[', archStart);
        if (arrayStart == std::string::npos) break;
        size_t arrayEnd = content.find(']', arrayStart);
        if (arrayEnd == std::string::npos) break;

        // Parse entries within this array
        size_t epos = arrayStart;
        while (epos < arrayEnd) {
            size_t objStart = content.find('{', epos);
            if (objStart == std::string::npos || objStart > arrayEnd) break;
            size_t objEnd = content.find('}', objStart);
            if (objEnd == std::string::npos) break;

            std::string obj = content.substr(objStart, objEnd - objStart + 1);

            // Extract fields
            std::string name;
            uint32_t offset = 0, compressed = 0, uncompressed = 0;

            auto extractStr = [&](const char* field) -> std::string {
                size_t fpos = obj.find(field);
                if (fpos == std::string::npos) return "";
                size_t vs = obj.find('"', fpos + strlen(field));
                if (vs == std::string::npos) return "";
                size_t ve = obj.find('"', vs + 1);
                if (ve == std::string::npos) return "";
                return obj.substr(vs + 1, ve - vs - 1);
            };
            auto extractInt = [&](const char* field) -> uint32_t {
                size_t fpos = obj.find(field);
                if (fpos == std::string::npos) return 0;
                size_t vs = fpos + strlen(field);
                while (vs < obj.size() && !isdigit(obj[vs])) vs++;
                uint32_t val = 0;
                while (vs < obj.size() && isdigit(obj[vs]))
                    val = val * 10 + (obj[vs++] - '0');
                return val;
            };

            name = extractStr("\"name\":");
            offset = extractInt("\"offset\":");
            compressed = extractInt("\"compressed\":");
            uncompressed = extractInt("\"uncompressed\":");

            if (!name.empty()) {
                sqlite3_reset(stmt);
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, currentArchive.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 3, offset);
                sqlite3_bind_int(stmt, 4, compressed);
                sqlite3_bind_int(stmt, 5, uncompressed);
                sqlite3_step(stmt);
                count++;
            }

            epos = objEnd + 1;
        }

        pos = arrayEnd + 1;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    execSQL(db, "CREATE INDEX IF NOT EXISTS idx_s3d_name ON s3d_assets(name)");
    execSQL(db, "CREATE INDEX IF NOT EXISTS idx_s3d_archive ON s3d_assets(archive)");
    return count;
}

// ============================================================================
// Metadata table
// ============================================================================

static void writeMetadata(sqlite3* db, int totalEntries) {
    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS metadata (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    )");

    auto setMeta = [&](const char* key, const std::string& value) {
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO metadata (key, value) VALUES (?,?)",
            -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };

    setMeta("version", "1");
    setMeta("build_date", __DATE__ " " __TIME__);
    setMeta("total_entries", std::to_string(totalEntries));
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    std::string eqPath;
    std::string dataPath = "data";
    std::string configPath = "config";
    std::string outputPath = "data/willeq.db";
    std::string s3dIndexPath;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--eq-path") == 0 && i + 1 < argc) eqPath = argv[++i];
        else if (strcmp(argv[i], "--data-path") == 0 && i + 1 < argc) dataPath = argv[++i];
        else if (strcmp(argv[i], "--config-path") == 0 && i + 1 < argc) configPath = argv[++i];
        else if (strcmp(argv[i], "--s3d-index") == 0 && i + 1 < argc) s3dIndexPath = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) outputPath = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) outputPath = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s --eq-path /path/to/EQ [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --eq-path PATH      EQ client directory (required)\n");
            printf("  --data-path PATH    WillEQ data directory (default: data)\n");
            printf("  --config-path PATH  WillEQ config directory (default: config)\n");
            printf("  --s3d-index PATH    s3d_index.json path (optional)\n");
            printf("  -o, --output PATH   Output database path (default: data/willeq.db)\n");
            return 0;
        }
    }

    if (eqPath.empty()) {
        fprintf(stderr, "ERROR: --eq-path is required\n");
        return 1;
    }

    printf("=== WillEQ Database Builder ===\n");
    printf("EQ path: %s\n", eqPath.c_str());
    printf("Output: %s\n\n", outputPath.c_str());

    auto totalStart = std::chrono::steady_clock::now();

    // Remove existing db
    remove(outputPath.c_str());

    // Open database
    sqlite3* db;
    if (sqlite3_open(outputPath.c_str(), &db) != SQLITE_OK) {
        fprintf(stderr, "ERROR: cannot create database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Performance pragmas
    execSQL(db, "PRAGMA journal_mode=WAL");
    execSQL(db, "PRAGMA synchronous=NORMAL");
    execSQL(db, "PRAGMA page_size=4096");

    int totalEntries = 0;

    // ── Spells ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadSpells(db, eqPath + "/spells_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  spells:       %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── EQ Strings ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadEqStrings(db, eqPath + "/eqstr_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  eqstrings:    %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── DB Strings ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadDbStrings(db, eqPath + "/dbstr_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  dbstrings:    %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Item Models ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadItemModels(db, dataPath + "/item_models.json");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  item_models:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Race Models ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadRaceModels(db, configPath + "/race_models.json");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  race_models:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Sound Assets ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadSoundAssets(db, eqPath + "/SoundAssets.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  sound_assets: %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Animation Sounds ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadAnimationSounds(db, eqPath + "/AnimationSounds.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  anim_sounds:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Race Data ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadRaceData(db, eqPath + "/RaceData.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  race_data:    %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Music Index ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadMusicIndex(db, eqPath + "/mp3index.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  music_files:  %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Login Strings ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadLoginStrings(db, eqPath + "/eqlsstr_us.txt");
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  login_strings:%6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Zone Character Models ──
    {
        auto start = std::chrono::steady_clock::now();
        int n = loadZoneCharacters(db, eqPath);
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  zone_chars:   %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── S3D Asset Index ──
    if (!s3dIndexPath.empty()) {
        auto start = std::chrono::steady_clock::now();
        int n = loadS3dIndex(db, s3dIndexPath);
        auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        printf("  s3d_assets:   %6d entries (%.0f ms)\n", n, elapsed);
        totalEntries += n;
    }

    // ── Metadata ──
    writeMetadata(db, totalEntries);

    // Final optimization
    execSQL(db, "ANALYZE");

    sqlite3_close(db);

    auto totalElapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - totalStart).count();

    // Report file size
    std::ifstream check(outputPath, std::ios::binary | std::ios::ate);
    float sizeMB = check ? check.tellg() / (1024.0f * 1024.0f) : 0;

    printf("\nDatabase built: %s (%.2f MB, %d entries, %.0f ms)\n",
           outputPath.c_str(), sizeMB, totalEntries, totalElapsed);

    return 0;
}
