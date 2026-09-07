# tests/cmake/database.cmake -- included from tests/CMakeLists.txt.
# SQLite-backed database tests (strings, spells, spell effects, WilleqDatabase).
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Test StringDatabase loading and formatting
ADD_EXECUTABLE(test_string_database
    test_string_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/string_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/formatted_message.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_LINK_LIBRARIES(test_string_database GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_string_database DISCOVERY_TIMEOUT 120)

# Test spell database loading and querying
ADD_EXECUTABLE(test_spell_database
    test_spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_COMPILE_DEFINITIONS(test_spell_database PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spell_database GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_spell_database DISCOVERY_TIMEOUT 120)

# T70: spells.eff visual-effect-definition reader, indexed by
# SpellAffectIndex. Deliberately no graphics/Irrlicht dependency (the reader
# itself has none) -- links spell_database/willeq_database/sqlite3 the same
# lightweight way test_spell_database.cpp does, for the index-resolution
# tests that cross-reference real spells' spell_affect_index.
ADD_EXECUTABLE(test_spells_eff_reader
    test_spells_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_COMPILE_DEFINITIONS(test_spells_eff_reader PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spells_eff_reader GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_spells_eff_reader DISCOVERY_TIMEOUT 120)

# T72: pure spell_affect_index -> spells.eff record -> per-stage visual
# selection layer (spell_visual_selection.h/.cpp). No Irrlicht/rendering
# dependency -- links the same lightweight spell_database/willeq_database/
# sqlite3 set as test_spells_eff_reader.cpp for the DB-backed census tests.
ADD_EXECUTABLE(test_spell_visual_selection
    test_spell_visual_selection.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/spell_visual_selection.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_COMPILE_DEFINITIONS(test_spell_visual_selection PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spell_visual_selection GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_spell_visual_selection DISCOVERY_TIMEOUT 120)

# Test WilleqDatabase::open() schema-version validation (T56)
ADD_EXECUTABLE(test_willeq_database
    test_willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
# T265: pfs.cpp (+zlib) so the shipped-content test can open each race row's
# s3d_file and read the model names from its WLD string hash.
TARGET_LINK_LIBRARIES(test_willeq_database GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES} dl)
# T257: the concurrent-open tests fork readers against the REAL tracked
# file on its real filesystem (the race is NFS-specific and inter-process).
TARGET_COMPILE_DEFINITIONS(test_willeq_database PRIVATE
    WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
GTEST_DISCOVER_TESTS(test_willeq_database DISCOVERY_TIMEOUT 120)
