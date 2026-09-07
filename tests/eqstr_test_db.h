// T429 (board B390) -- the shipped eqstr table, for the tests that drive a
// display composer.
//
// The composers stopped carrying their format strings as C++ literals: they
// fetch each one from `data/willeq.db`'s `eqstrings` table through
// `EQT::StringDatabase`. So a composer test needs the real shipped db open,
// exactly as `tests/test_who_response.cpp` opens it for race names. Opened
// once per test binary; `WILLEQ_DB_REAL_PATH` is the CMake-supplied path to
// the checkout's own copy.

#pragma once

#include "client/string_database.h"
#include "client/willeq_database.h"

#include <fstream>
#include <string>

namespace eqt_test {

/**
 * The shipped eqstr table. `isLoaded()` is false when the db is missing,
 * which every caller reports rather than silently passing.
 */
inline EQT::StringDatabase& ShippedStrings() {
    static EQT::WilleqDatabase db;
    static EQT::StringDatabase strings;
    static const bool once = [] {
        const std::string path = WILLEQ_DB_REAL_PATH;
        std::ifstream probe(path);
        if (!probe.good()) {
            return false;
        }
        if (!db.open(path)) {
            return false;
        }
        EQT::WilleqDatabase::setGlobalInstance(&db);
        return strings.initFromDatabase();
    }();
    (void)once;
    return strings;
}

}  // namespace eqt_test
