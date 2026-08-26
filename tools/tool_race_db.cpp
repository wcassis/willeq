#include "tool_race_db.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "client/willeq_database.h"

bool openWilleqRaceDatabase() {
    static EQT::WilleqDatabase db;
    static bool opened = false;
    static bool tried = false;
    if (tried) return opened;
    tried = true;

    // Only attempt paths that exist -- WilleqDatabase::open() logs an ERROR
    // for a missing file, and the fallback chain would print one per miss.
    auto tryOpen = [&](const char* path) {
        if (!path || !*path || opened) return;
        std::ifstream f(path);
        if (!f.good()) return;
        opened = db.open(path);
    };
    tryOpen(std::getenv("WILLEQ_DB_PATH"));
    tryOpen("data/willeq.db");
#ifdef WILLEQ_DB_REAL_PATH
    tryOpen(WILLEQ_DB_REAL_PATH);
#endif

    if (opened) {
        EQT::WilleqDatabase::setGlobalInstance(&db);
    } else {
        fprintf(stderr,
                "WARNING: could not open willeq.db -- every race lookup will be "
                "'unknown' and no character model will load (set WILLEQ_DB_PATH "
                "or run from the repo root)\n");
    }
    return opened;
}
