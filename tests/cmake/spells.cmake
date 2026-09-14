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
GTEST_DISCOVER_TESTS(test_spell_manager DISCOVERY_TIMEOUT 120)

# T314: the two hand-rolled body-type tables in src/client/spell/ are gone --
# creature-restricted spells go through one shared predicate. Headless (no DB,
# no graphics, no eqemu): the tests replay OP_NewSpawn into a live EverQuest
# and call the predicate and its live consumer, EQ::SpellManager. The guard
# test in it scans the source tree, so it needs WILLEQ_REPO_ROOT (set globally
# at the top of this file).
#
# T324 removed test_spell_group_targeting.cpp along with the class it pinned
# (the unreached spell target-resolution layer) -- see the tombstone in
# src/client/eq.cpp. Do not re-add the target.
ADD_EXECUTABLE(test_spell_body_type_targeting
    test_spell_body_type_targeting.cpp
)
TARGET_LINK_LIBRARIES(test_spell_body_type_targeting GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_body_type_targeting PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_body_type_targeting DISCOVERY_TIMEOUT 120)

ADD_EXECUTABLE(test_spell_manager_timing
    test_spell_manager_timing.cpp
)
TARGET_LINK_LIBRARIES(test_spell_manager_timing GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_manager_timing PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_manager_timing DISCOVERY_TIMEOUT 120)

# T469 (board B604): the server's cast interrupt as real Titanium wire bytes
# (InterruptCast_Struct, ManaChange_Struct) through the zone dispatcher into a
# headless EverQuest mid-cast.
ADD_EXECUTABLE(test_spell_interrupt_wire
    test_spell_interrupt_wire.cpp
)
TARGET_COMPILE_DEFINITIONS(test_spell_interrupt_wire PRIVATE
    WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spell_interrupt_wire GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_interrupt_wire PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_interrupt_wire DISCOVERY_TIMEOUT 120)

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
GTEST_DISCOVER_TESTS(test_buff_manager DISCOVERY_TIMEOUT 120)

# T348 (board B055): the buff-LANDING producer. eqemu never sends a Titanium
# client an "applied" OP_Buff, so ZoneProcessBuff()'s apply branch -- the only
# production caller of setPlayerBuff(), and therefore of T339's vision apply
# hook -- is dead on the wire. The landing arrives as the second OP_Action of
# the pair (effect_flag == 4); these tests replay the packets T345 measured
# live (review/T345/logs/live_session_vision.log) through a real EverQuest.
# Owns its main() so it can open data/willeq.db once for the binary, hence
# GTest::gtest rather than gtest_main.
ADD_EXECUTABLE(test_buff_landing_producer
    test_buff_landing_producer.cpp
)
TARGET_COMPILE_DEFINITIONS(test_buff_landing_producer PRIVATE
    WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_buff_landing_producer GTest::gtest willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_buff_landing_producer PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_buff_landing_producer DISCOVERY_TIMEOUT 120)

# T395 Leg A (board B204): the spell impact visual, wire to particle. The
# producer half needs a real EverQuest + dispatch (willeq_core); the worker
# half drives SimulationWorker::computeOnce() directly, so it needs the
# Irrlicht headers when graphics are on.
ADD_EXECUTABLE(test_spell_impact_visual
    test_spell_impact_visual.cpp
)
TARGET_COMPILE_DEFINITIONS(test_spell_impact_visual PRIVATE
    WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spell_impact_visual GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_impact_visual PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_impact_visual DISCOVERY_TIMEOUT 120)

# T422 (board B226/B356/B370): the impact and spellComplete burst parameters
# pinned against the official client measured live at 30 fps in T422.  Reads
# the shipped config through the same preset functions production calls, and
# drives SimulationWorker::computeOnce() to prove the pool size is
# load-bearing, so it needs the Irrlicht headers when graphics are on.
ADD_EXECUTABLE(test_spell_burst_oracle_match
    test_spell_burst_oracle_match.cpp
)
TARGET_LINK_LIBRARIES(test_spell_burst_oracle_match GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_burst_oracle_match PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_burst_oracle_match DISCOVERY_TIMEOUT 120)

# T395 Leg B (board B205): eqemu say-links in server chat text. The helper
# plus the two consumers that print server-authored lines (OP_SpecialMesg and
# OP_ChannelMessage).
ADD_EXECUTABLE(test_say_link_text
    test_say_link_text.cpp
)
TARGET_LINK_LIBRARIES(test_say_link_text GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_say_link_text PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_say_link_text DISCOVERY_TIMEOUT 120)

# T397 Leg B (board B220): removal of spell particle effects by entity. The
# command under test is drained from a real ParticleManager and fed to a real
# SimulationWorker, so both halves of the producer/consumer field contract are
# in the test.
ADD_EXECUTABLE(test_spell_effect_removal
    test_spell_effect_removal.cpp
)
TARGET_LINK_LIBRARIES(test_spell_effect_removal GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_effect_removal PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_effect_removal DISCOVERY_TIMEOUT 120)

# T426 Leg C (board B400): the caster's own landing must survive the SECOND
# OP_Action eqemu sends it for its own cast. Real SpellVisualFX (EDT_NULL
# device) -> real ParticleManager -> real SimulationWorker, so the removal
# under test is the one production issues.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_caster_own_landing
        test_caster_own_landing.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_caster_own_landing PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_caster_own_landing GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_caster_own_landing DISCOVERY_TIMEOUT 120)
ENDIF()

# T389 Leg B (board B162): the spells.eff sprite name -> particle-atlas tile
# mapping measured against the official client, and the routing of a resolved
# Target stage into an impact effect's EmitterConfig::textureRegions[].
# Pure logic -- two Environment TUs, no renderer, no Irrlicht device.
# T417 (board B300) rewrote the mapping half: the twelve tile approximations
# are gone and every resolving GEN name addresses its own cell on the spell
# sprite sheet. spells_eff_reader.cpp joins the sources because the config now
# resolves through the reader's one implementation of the 0-based rule.
ADD_EXECUTABLE(test_spell_target_stage_textures
    test_spell_target_stage_textures.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_effects_config.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_emitter_presets.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
)
TARGET_LINK_LIBRARIES(test_spell_target_stage_textures GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES} dl)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_target_stage_textures PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_target_stage_textures DISCOVERY_TIMEOUT 120)

# T441 (boards B398, B397): where the official client's per-spell colour comes
# from (spells_us.txt field 123 -> spells.eff record -> the stage's sprite
# names -> spel*.tga art, with NO colour field anywhere in the chain), and the
# per-spell style overrides T422 measured. Reads the SHIPPED spell sprite
# sheet's pixels directly and the real spells.eff when the client data is
# mounted; otherwise it rests on the measured stage literals. Same source set
# as test_spell_target_stage_textures -- pure logic, no renderer, no device.
ADD_EXECUTABLE(test_spell_colour_and_overrides
    test_spell_colour_and_overrides.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_effects_config.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_emitter_presets.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
)
TARGET_LINK_LIBRARIES(test_spell_colour_and_overrides GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES} dl)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_colour_and_overrides PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_colour_and_overrides DISCOVERY_TIMEOUT 120)
