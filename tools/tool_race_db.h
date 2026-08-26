#pragma once

// T270: race codes, scales and archive names are served exclusively from
// WilleqDatabase -- race_codes.cpp's pre-JSON hardcoded switch is deleted
// because its numbering contradicted the audited race_models rows. A tool
// that renders entities therefore has to open the shipped db exactly like
// Application::run()'s EverQuest::OpenDatabase("data/willeq.db") does;
// without it every race resolves to "unknown" and loads nothing.
//
// Opens (in order) $WILLEQ_DB_PATH, ./data/willeq.db, and the configure-time
// WILLEQ_DB_REAL_PATH, installs the first that opens as the global instance,
// and warns on stderr if none did. The database is owned by a function-local
// static, so it lives for the process lifetime.
// Returns true if a database was installed.
bool openWilleqRaceDatabase();
