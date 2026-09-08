// db_loaders_json.cpp — willeq_db_builder loaders over the WillEQ/config
// *.json data files (T447; bodies moved verbatim from
// tools/willeq_db_builder.cpp).
//
//   data/item_models.json      -> item_models
//   config/race_models.json    -> race_models
//
// T452 (board B468): a fourth loader, loadS3dIndex() (s3d_index.json ->
// s3d_assets), was deleted here. Nothing ever passed willeq_db_builder
// --s3d-index, so the table was never in the shipped database, and its only
// reachable consumer had been querying a table that did not exist.
//
// T454 (board B474) and T456 (board B476): loadEquipmentModels() is GONE, and
// with it both tables it wrote. equipment_textures (2,623 rows naming which of
// the 17 *equip*.s3d archives holds each held-item texture) went in T454;
// equipment_models (1,000 rows: model_id -> archive / wld / actor / textures)
// goes here. Neither was ever read: WilleqDatabase is the only sqlite consumer
// in the tree and names neither table in any SQL.
//
// The B468 check was made deliberately for BOTH, and both routes are HEALTHY
// rather than broken -- this was redundancy, not a dead route:
// EquipmentModelLoader::buildEquipmentIndex()
// (src/client/graphics/eq/equipment_model_loader.cpp:180) indexes those same
// 17 archives LIVE at zone load, filling equipmentModelIndex_ AND
// textureIndex_, and getModelRef() (:972) is what
// EntityPrepWorker::prepEquipmentModels()
// (src/client/graphics/entity_prep_worker.cpp:406) asks, exactly as
// getTextureRef() is what the mesh build asks for the textures.
//
// data/equipment_models.json and tools/equipment_index_builder are left in the
// tree: dropping them is a separate question from dropping the table, and it
// is boarded rather than swept along.
//
// The parsers here are hand-rolled string scans, not a JSON library: the
// builder links only sqlite3 and must stay dependency-free.

#include "willeq_db/db_builder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Item models loader (item_models.json)
// ============================================================================

int loadItemModels(sqlite3* db, const std::string& filepath) {
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

int loadRaceModels(sqlite3* db, const std::string& filepath) {
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
            invisible INTEGER DEFAULT 0,
            color INTEGER DEFAULT 0
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO race_models (race_id, code, name, female_code, "
        "male_code, neuter_code, s3d_file, fallback_code, animation_source, "
        "animation_source_s3d, scale, gender_variants, invisible, color) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
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
        // T304 Leg 2: "#AARRGGBB" placeholder-cube tint -> packed ARGB.
        // These values are INVENTED (Will's ruling, 2026-08-28); see the
        // file's _notes. A row without one gets 0, which the client reads
        // as "no colour recorded" and answers with its generic grey.
        {
            std::string colorStr = extractStr("color");
            unsigned long packed = 0;
            if (!colorStr.empty() && colorStr[0] == '#') {
                packed = std::strtoul(colorStr.c_str() + 1, nullptr, 16);
            }
            sqlite3_bind_int64(stmt, 14, static_cast<sqlite3_int64>(packed));
        }

        sqlite3_step(stmt);
        count++;

        pos = objEnd + 1;
    }

    sqlite3_finalize(stmt);
    execSQL(db, "COMMIT");
    return count;
}
