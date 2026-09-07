// db_builder.h — shared seam for the willeq_db_builder tool (T447)
//
// The builder was one 1,444-line TU riding the 1,500-line ceiling. It is now
// four files, split by the KIND of input each loader reads:
//
//   tools/willeq_db_builder.cpp          helpers, metadata table, main()
//   tools/willeq_db/db_loaders_text.cpp  the EQ client's *.txt files
//   tools/willeq_db/db_loaders_json.cpp  the WillEQ/config *.json files
//
// Every loader keeps its name and its signature: `int load*(sqlite3*, const
// std::string& filepath)`, returning the row count it inserted (0 when the
// input is missing, which is a WARNING and not fatal — see main()).
//
// The four helpers below were `static` in the single TU and are now shared;
// `readLine()` in particular is NOT optional (T392/board B200: every Titanium
// *.txt file is CRLF-terminated, and std::getline() leaves the '\r' in the
// last field). Their definitions live in tools/willeq_db_builder.cpp.

#pragma once

#include <istream>
#include <string>
#include <vector>

#include "sqlite3.h"

// ---------------------------------------------------------------------------
// Helpers (defined in tools/willeq_db_builder.cpp)
// ---------------------------------------------------------------------------

std::string trim(const std::string& s);
bool readLine(std::istream& in, std::string& line);
std::vector<std::string> split(const std::string& s, char delim);
bool execSQL(sqlite3* db, const char* sql);

// ---------------------------------------------------------------------------
// Loaders over the EQ client's *.txt files (db_loaders_text.cpp)
// ---------------------------------------------------------------------------

int loadSpells(sqlite3* db, const std::string& filepath);        // spells_us.txt
int loadEqStrings(sqlite3* db, const std::string& filepath);     // eqstr_us.txt
int loadDbStrings(sqlite3* db, const std::string& filepath);     // dbstr_us.txt
int loadSoundAssets(sqlite3* db, const std::string& filepath);   // SoundAssets.txt
int loadAnimationSounds(sqlite3* db, const std::string& filepath); // AnimationSounds.txt
int loadMusicIndex(sqlite3* db, const std::string& filepath);    // mp3index.txt
int loadLoginStrings(sqlite3* db, const std::string& filepath);  // eqlsstr_us.txt

// ---------------------------------------------------------------------------
// Loaders over the WillEQ/config *.json files (db_loaders_json.cpp)
// ---------------------------------------------------------------------------

int loadItemModels(sqlite3* db, const std::string& filepath);      // item_models.json
int loadEquipmentModels(sqlite3* db, const std::string& filepath); // equipment_models.json
int loadRaceModels(sqlite3* db, const std::string& filepath);      // race_models.json
