# tests/cmake/spells.cmake -- included from tests/CMakeLists.txt.
# SpellManager / BuffManager coverage (T24 and follow-ons).
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T24: SpellManager coverage (gem state machine, cast validation matrix via
# direct validateCast()/checkMana()/checkRange()/adjustTargetForSpellType()
# calls with hand-built SpellData, cooldown/memorization timing via T17-style
# private-member backdating, interrupt + server packet handlers, callbacks).
# Reaches the live SpellManager through a bare EverQuest via
# GetSpellManager() -- Option A, no seam/interface extraction
# (refactor/T23_spell_combat_seam_scoping.md, refactor/T24_spell_manager.md).
# Needs the real SQLite spell DB (data/willeq.db) for a subset of tests;
# degrades cleanly via hasDb()/GTEST_SKIP when absent, same as
# test_spell_database.cpp. Split into two TUs (gem state machine + cast
# validation matrix here; timing/handlers/callbacks in
# test_spell_manager_timing.cpp) to keep each file under the plan's
# ~800-line-per-file guidance -- both link the same live SpellManager
# through the same seam, there is no behavioral split.
ADD_EXECUTABLE(test_spell_manager
    test_spell_manager.cpp
)
TARGET_COMPILE_DEFINITIONS(test_spell_manager PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spell_manager GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
# T163: T79's raised discovery timeout (60s) removed here and on five other
# targets (test_graphics, test_eq_action_handler_spell_pet,
# test_eq_action_handler_tradeskill, test_skill_possession_gate,
# test_skill_gate_bypasses). Its own comment said the raised timeout "doesn't
# remove the contention, it just gives it more slack" -- the PRE_TEST mode set
# at the top of this file removes the contention, so the slack is moot. The six
# were also picked by which target happened to fail on the day, not by
# measurement: test_spell_manager lists its tests in 0.05s on an idle host,
# while the two genuinely slowest binaries (test_zoning_graphics_integration
# and test_beneficial_spell_casting, 0.21s) were never in the set. All 112
# discovery calls in this file now behave identically.
GTEST_DISCOVER_TESTS(test_spell_manager)

# T314: spell targeting correctness -- group spells target the caster's GROUP
# (eqt::state::GroupState) intersected with the spell's AE radius, and the two
# hand-rolled body-type tables in src/client/spell/ are gone. Both files are
# headless (no DB, no graphics, no eqemu): the group tests replay
# OP_NewSpawn/OP_GroupUpdate into a live EverQuest, and the body-type tests
# call the one shared predicate plus both of its consumers. The guard test in
# test_spell_body_type_targeting scans the source tree, so it needs
# WILLEQ_REPO_ROOT (set globally at the top of this file).
ADD_EXECUTABLE(test_spell_group_targeting
    test_spell_group_targeting.cpp
)
TARGET_LINK_LIBRARIES(test_spell_group_targeting GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_group_targeting PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_group_targeting)

ADD_EXECUTABLE(test_spell_body_type_targeting
    test_spell_body_type_targeting.cpp
)
TARGET_LINK_LIBRARIES(test_spell_body_type_targeting GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_body_type_targeting PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_body_type_targeting)

ADD_EXECUTABLE(test_spell_manager_timing
    test_spell_manager_timing.cpp
)
TARGET_LINK_LIBRARIES(test_spell_manager_timing GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_manager_timing PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_manager_timing)

# T24: BuffManager coverage. BuffManager is constructible standalone from a
# SpellDatabase* (buff_manager.h:99) with no EverQuest at all -- the
# cleanest isolation in this batch, so this target links only the spell
# layer + willeq_database (sqlite3), not the full willeq_core.
ADD_EXECUTABLE(test_buff_manager
    test_buff_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/buff_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_COMPILE_DEFINITIONS(test_buff_manager PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_buff_manager GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_buff_manager)
