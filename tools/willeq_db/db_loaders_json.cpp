// db_loaders_json.cpp — willeq_db_builder loaders over the WillEQ/config
// *.json data files (T447; bodies moved verbatim from
// tools/willeq_db_builder.cpp).
//
//   data/item_models.json      -> item_models
//   data/equipment_models.json -> equipment_models (+ textures)
//   config/race_models.json    -> race_models
//
// T452 (board B468): a fourth loader, loadS3dIndex() (s3d_index.json ->
// s3d_assets), was deleted here. Nothing ever passed willeq_db_builder
// --s3d-index, so the table was never in the shipped database, and its only
// reachable consumer had been querying a table that did not exist.
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
// Equipment models loader (data/equipment_models.json)
// ============================================================================

int loadEquipmentModels(sqlite3* db, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        fprintf(stderr, "WARNING: cannot open %s\n", filepath.c_str());
        return 0;
    }

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS equipment_models (
            model_id INTEGER PRIMARY KEY,
            archive TEXT NOT NULL,
            wld TEXT NOT NULL,
            actor TEXT NOT NULL,
            textures TEXT
        )
    )");

    execSQL(db, R"(
        CREATE TABLE IF NOT EXISTS equipment_textures (
            name TEXT PRIMARY KEY,
            archive TEXT NOT NULL,
            entry TEXT NOT NULL
        )
    )");

    execSQL(db, "BEGIN TRANSACTION");

    sqlite3_stmt* modelStmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO equipment_models (model_id, archive, wld, actor, textures) VALUES (?,?,?,?,?)",
        -1, &modelStmt, nullptr);

    sqlite3_stmt* texStmt;
    sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO equipment_textures (name, archive, entry) VALUES (?,?,?)",
        -1, &texStmt, nullptr);

    // Parse JSON — format: {"models": {"id": {...}, ...}, "textures": {"name": {...}, ...}}
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    int modelCount = 0;
    int texCount = 0;

    // Helper: find matching closing brace/bracket from opening position
    auto findMatchingClose = [&](size_t openPos, char open, char close) -> size_t {
        int depth = 1;
        size_t p = openPos + 1;
        bool inString = false;
        while (p < content.size() && depth > 0) {
            char c = content[p];
            if (c == '"' && (p == 0 || content[p-1] != '\\')) inString = !inString;
            if (!inString) {
                if (c == open) depth++;
                else if (c == close) depth--;
            }
            p++;
        }
        return p;  // one past matching close
    };

    // Helper: extract string value from a small JSON object string
    auto extractStr = [](const std::string& obj, const std::string& key) -> std::string {
        size_t kp = obj.find("\"" + key + "\"");
        if (kp == std::string::npos) return "";
        kp = obj.find('"', kp + key.size() + 2);
        if (kp == std::string::npos) return "";
        kp++;
        size_t ep = obj.find('"', kp);
        if (ep == std::string::npos) return "";
        return obj.substr(kp, ep - kp);
    };

    // Find "models" section and its brace-delimited extent
    size_t modelsKey = content.find("\"models\"");
    if (modelsKey == std::string::npos) {
        fprintf(stderr, "WARNING: no 'models' section in %s\n", filepath.c_str());
        sqlite3_finalize(modelStmt);
        sqlite3_finalize(texStmt);
        execSQL(db, "COMMIT");
        return 0;
    }

    size_t modelsOpen = content.find('{', modelsKey + 8);
    size_t modelsClose = findMatchingClose(modelsOpen, '{', '}');

    // Parse each model entry within the models section
    size_t pos = modelsOpen + 1;
    while (pos < modelsClose - 1) {
        // Find next quoted key (model ID)
        pos = content.find('"', pos);
        if (pos == std::string::npos || pos >= modelsClose - 1) break;
        pos++;
        size_t keyEnd = content.find('"', pos);
        if (keyEnd == std::string::npos) break;
        std::string modelIdStr = content.substr(pos, keyEnd - pos);
        pos = keyEnd + 1;

        // Must be all digits
        bool isModelId = !modelIdStr.empty();
        for (char c : modelIdStr) {
            if (!isdigit(c)) { isModelId = false; break; }
        }
        if (!isModelId) continue;
        int modelId = std::atoi(modelIdStr.c_str());
        if (modelId <= 0) continue;

        // Find and extract the model's object
        size_t braceStart = content.find('{', pos);
        if (braceStart == std::string::npos || braceStart >= modelsClose) break;
        size_t braceEnd = findMatchingClose(braceStart, '{', '}');
        std::string obj = content.substr(braceStart, braceEnd - braceStart);

        std::string archive = extractStr(obj, "archive");
        std::string wld = extractStr(obj, "wld");
        std::string actor = extractStr(obj, "actor");

        // Extract textures array as comma-separated string
        std::string texList;
        size_t texArrStart = obj.find("[");
        size_t texArrEnd = obj.find("]");
        if (texArrStart != std::string::npos && texArrEnd != std::string::npos) {
            std::string arr = obj.substr(texArrStart + 1, texArrEnd - texArrStart - 1);
            size_t tp = 0;
            while ((tp = arr.find('"', tp)) != std::string::npos) {
                tp++;
                size_t te = arr.find('"', tp);
                if (te == std::string::npos) break;
                if (!texList.empty()) texList += ",";
                texList += arr.substr(tp, te - tp);
                tp = te + 1;
            }
        }

        if (!archive.empty() && !wld.empty() && !actor.empty()) {
            sqlite3_reset(modelStmt);
            sqlite3_bind_int(modelStmt, 1, modelId);
            sqlite3_bind_text(modelStmt, 2, archive.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(modelStmt, 3, wld.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(modelStmt, 4, actor.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(modelStmt, 5, texList.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(modelStmt);
            modelCount++;
        }

        pos = braceEnd;
    }

    // Find "textures" section — search for the top-level key after models section
    size_t texKey = content.find("\"textures\"", modelsClose);
    if (texKey != std::string::npos) {
        size_t texOpen = content.find('{', texKey + 10);
        size_t texClose = findMatchingClose(texOpen, '{', '}');

        pos = texOpen + 1;
        while (pos < texClose - 1) {
            pos = content.find('"', pos);
            if (pos == std::string::npos || pos >= texClose - 1) break;
            pos++;
            size_t keyEnd = content.find('"', pos);
            if (keyEnd == std::string::npos) break;
            std::string texName = content.substr(pos, keyEnd - pos);
            pos = keyEnd + 1;

            // Must have a file extension
            if (texName.find('.') == std::string::npos) continue;

            size_t braceStart = content.find('{', pos);
            if (braceStart == std::string::npos || braceStart >= texClose) break;
            size_t braceEnd = findMatchingClose(braceStart, '{', '}');
            std::string obj = content.substr(braceStart, braceEnd - braceStart);

            std::string archive = extractStr(obj, "archive");
            std::string entry = extractStr(obj, "entry");

            if (!archive.empty() && !entry.empty()) {
                sqlite3_reset(texStmt);
                sqlite3_bind_text(texStmt, 1, texName.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(texStmt, 2, archive.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(texStmt, 3, entry.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(texStmt);
                texCount++;
            }

            pos = braceEnd;
        }
    }

    sqlite3_finalize(modelStmt);
    sqlite3_finalize(texStmt);
    execSQL(db, "COMMIT");
    return modelCount + texCount;
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
