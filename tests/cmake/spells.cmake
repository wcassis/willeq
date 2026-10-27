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
TARGET_COMPILE_DEFINITIONS(test_spell_manager PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
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

# B1103 (GitLab #423): the spellbook's icon SOURCE. The shipped DB bound
# spells_us.txt field 143 (ai_pt_bonus, 0 on all 8,066 rows -- not an icon
# column in the Titanium export) to spells.spell_icon, which the spellbook
# (and the buff bar, and the shift-click spell display) read -- so every
# book slot drew cell 0 of the Spells family while the gem bar, fed from
# field 144 (NEW_ICON, gem_icon), was right. This test drives the real
# PublishFullStateSnapshot() with the real shipped DB: a published book
# entry carries the DB's icon id for its spell, two spells' entries
# DIFFER (no all-the-same), and the Spells-sheet cells differ. Device-free:
# no renderer, no pack, no display; self-skips without willeq.db (the
# test_spell_manager precedent). refactor/B1103/README.md has the full
# mechanism, the field-level proof and the negative (pre-fix) run.
ADD_EXECUTABLE(test_spellbook_icon_source
    test_spellbook_icon_source.cpp
)
TARGET_COMPILE_DEFINITIONS(test_spellbook_icon_source PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
TARGET_LINK_LIBRARIES(test_spellbook_icon_source GTest::gtest willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spellbook_icon_source PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spellbook_icon_source DISCOVERY_TIMEOUT 120)

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

# B892 (GitLab #208): the player's own levitation -- the SPA-57 predicate
# (the first reader of SpellEffect::Levitate in the tree), the full
# buff-apply/fade -> LevitationChanged chain over a live EverQuest with a
# test bridge, the ClampLevitationFeet ground decision (device-free, next to
# its sibling in test_ground_sentinels) and the levitate_applies scenario
# state machine. RED on the pre-fix tree: every name it uses was absent.
# The wire tests need the real spell DB (spells 261/970 carry SPA 57; 288
# must NOT) and degrade to GTEST_SKIP like test_spell_manager's DB half;
# the predicate, clamp and scenario tests run with no DB at all.
ADD_EXECUTABLE(test_levitation
    test_levitation.cpp
)
TARGET_COMPILE_DEFINITIONS(test_levitation PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
TARGET_LINK_LIBRARIES(test_levitation GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_levitation PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_levitation DISCOVERY_TIMEOUT 120)

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
    "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
TARGET_LINK_LIBRARIES(test_spell_interrupt_wire GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_interrupt_wire PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_interrupt_wire DISCOVERY_TIMEOUT 120)

# T482 (board B631): how a cast the server CONFIRMED ends -- the success tail
# (ManaChange keepcasting 0 + MemorizeSpell memSpellSpellbar) against a
# StopCasting() -- as real Titanium wire bytes through the zone dispatcher.
ADD_EXECUTABLE(test_spell_cast_end_wire
    test_spell_cast_end_wire.cpp
)
TARGET_COMPILE_DEFINITIONS(test_spell_cast_end_wire PRIVATE
    "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
TARGET_LINK_LIBRARIES(test_spell_cast_end_wire GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_cast_end_wire PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_cast_end_wire DISCOVERY_TIMEOUT 120)

# B1164 (#483) fix leg: the local player's own spawn size comes from the
# server's OP_ZoneEntry Spawn_Struct (the attach scale of its own spell slots).
ADD_EXECUTABLE(test_local_player_spawn_size
    test_local_player_spawn_size.cpp
)
TARGET_LINK_LIBRARIES(test_local_player_spawn_size GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_local_player_spawn_size PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_local_player_spawn_size DISCOVERY_TIMEOUT 120)

# T483 (board B762/B770): store the ninth Titanium gem, show and offer eight.
# The panel half draws on the EDT_NULL device, so the target needs graphics.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_ninth_spell_gem
        test_ninth_spell_gem.cpp
    )
    TARGET_LINK_LIBRARIES(test_ninth_spell_gem GTest::gtest_main willeq_core)
    TARGET_INCLUDE_DIRECTORIES(test_ninth_spell_gem PRIVATE ${IRRLICHT_INCLUDE_DIR})
    GTEST_DISCOVER_TESTS(test_ninth_spell_gem DISCOVERY_TIMEOUT 120)

    # B1076 (GitLab #396): the spellbook window's geometry, quoted from
    # EQUI_SpellBookWnd.xml. Pure layout math -- needs only the Irrlicht
    # headers for the rects, no EverQuest, no device.
    ADD_EXECUTABLE(test_spellbook_layout
        test_spellbook_layout.cpp
    )
    TARGET_LINK_LIBRARIES(test_spellbook_layout GTest::gtest_main)
    TARGET_INCLUDE_DIRECTORIES(test_spellbook_layout PRIVATE ${IRRLICHT_INCLUDE_DIR})
    GTEST_DISCOVER_TESTS(test_spellbook_layout DISCOVERY_TIMEOUT 120)
ENDIF()

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
TARGET_COMPILE_DEFINITIONS(test_buff_manager PRIVATE "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
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
    "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
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
    "WILLEQ_DB_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
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

# B1120 (#440): the landing VOLUME -- column, spark shell, pillar + dome,
# swirl + glints, star field, ball burst -- measured off the official client
# (refactor/B1120/README.md), plus the landing onset delay and the pool-size
# wiring.  Drives SimulationWorker::computeOnce() with the shipped presets, so
# it needs the Irrlicht headers when graphics are on.
ADD_EXECUTABLE(test_spell_landing_volume
    test_spell_landing_volume.cpp
)
TARGET_LINK_LIBRARIES(test_spell_landing_volume GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_landing_volume PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_landing_volume DISCOVERY_TIMEOUT 120)

# B1131 (#451): the Source (cast) stage per spells.eff record -- anchor class,
# shape, onset and hold from the B1130 survey. Config rows run everywhere; the
# real SpellVisualFX -> ParticleManager -> SimulationWorker seam needs graphics.
ADD_EXECUTABLE(test_spell_source_anchors
    test_spell_source_anchors.cpp
)
TARGET_LINK_LIBRARIES(test_spell_source_anchors GTest::gtest_main willeq_core)
# EQT_GRAPHICS=OFF (CI build-headless): willeq_core carries no graphics TU, so
# the config rows (SourceRecordRows) take the same pure-logic sources as
# test_spell_colour_and_overrides; the SpellVisualFX seam is #ifdef'd out.
IF(NOT EQT_GRAPHICS)
    TARGET_SOURCES(test_spell_source_anchors PRIVATE
        ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_effects_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_emitter_presets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_eff_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_edd_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_LINK_LIBRARIES(test_spell_source_anchors ${FMT_LIB} ${JSONCPP_LIBRARIES} dl)
ENDIF()
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_source_anchors PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_source_anchors DISCOVERY_TIMEOUT 120)

# B1132 (#452): the Target (landing) stage per spells.eff record -- anchor
# class and shape from the B1130 survey, and no landing on the caster of an
# other-target cast. Same layout as test_spell_source_anchors.
ADD_EXECUTABLE(test_spell_target_anchors
    test_spell_target_anchors.cpp
)
TARGET_LINK_LIBRARIES(test_spell_target_anchors GTest::gtest_main willeq_core)
IF(NOT EQT_GRAPHICS)
    TARGET_SOURCES(test_spell_target_anchors PRIVATE
        ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_effects_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_emitter_presets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_eff_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_edd_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_LINK_LIBRARIES(test_spell_target_anchors ${FMT_LIB} ${JSONCPP_LIBRARIES} dl)
ENDIF()
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_target_anchors PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_target_anchors DISCOVERY_TIMEOUT 120)

# B1144 (#464): SpellVisualFX draws a spellsnew.eff record slot by slot (level
# gate, anchor, attach point; no Target slot = no landing) on the shipped client
# files. The seam needs graphics + GLES2; the reader half is
# test_spells_new_eff_reader (database.cmake), which runs everywhere.
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_spell_slot_rules
        test_spell_slot_rules.cpp
    )
    TARGET_LINK_LIBRARIES(test_spell_slot_rules GTest::gtest_main willeq_core)
    IF(IRRLICHT_LIBRARY)
        TARGET_INCLUDE_DIRECTORIES(test_spell_slot_rules PRIVATE ${IRRLICHT_INCLUDE_DIR})
    ENDIF()
    GTEST_DISCOVER_TESTS(test_spell_slot_rules DISCOVERY_TIMEOUT 120)
ENDIF()

# B1163 (#482): device-free rules for the two B1152 missile follow-ups (the
# calibrated missile speed + the oracle's spell-859 gate). Pure logic -- no
# <irrlicht.h> -- so it builds and runs in both the graphics and headless trees.
ADD_EXECUTABLE(test_b1163_missile_rules
    test_b1163_missile_rules.cpp
)
TARGET_LINK_LIBRARIES(test_b1163_missile_rules GTest::gtest_main willeq_core)
GTEST_DISCOVER_TESTS(test_b1163_missile_rules DISCOVERY_TIMEOUT 120)

# B1137 (#457): the measured Source/Target row is chosen by the spell's
# spellsnew.eff record (field 145), not its spells.eff record (field 123).
# Config rows run everywhere; the SpellVisualFX key check needs graphics.
ADD_EXECUTABLE(test_spell_anim_rows
    test_spell_anim_rows.cpp
)
TARGET_LINK_LIBRARIES(test_spell_anim_rows GTest::gtest_main willeq_core)
IF(NOT EQT_GRAPHICS)
    TARGET_SOURCES(test_spell_anim_rows PRIVATE
        ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_effects_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/environment/spell_emitter_presets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_eff_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_eff_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_edd_reader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_LINK_LIBRARIES(test_spell_anim_rows ${FMT_LIB} ${JSONCPP_LIBRARIES} dl)
ENDIF()
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_anim_rows PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_anim_rows DISCOVERY_TIMEOUT 120)

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

# B1106 (#426, from B1104 #424 F2): a spell landing's command must not be
# DROPPED because the frame it rides on was slow. Same seam as
# test_caster_own_landing (real SpellVisualFX -> ParticleManager ->
# SimulationWorker). The card's acceptance test: enqueue a CreateSpellEffect,
# tick the worker with deltaTime 2.0 then 0.016, assert the effect exists and
# spawned. Fails pre-fix (the 2.0 s tick early-returns before the command
# loop); passes post-fix (the command is executed on the slow frame, physics
# stepped with a clamped delta).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b1106_slow_frame_commands
        test_b1106_slow_frame_commands.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b1106_slow_frame_commands PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b1106_slow_frame_commands GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_b1106_slow_frame_commands DISCOVERY_TIMEOUT 120)
ENDIF()

# B293 (#124): the entity-direction callback was installed by two callers and
# invoked by none. The fix adds the renderer's direction-resolution arm
# (SimBridgeComponent::applySimulationOutput, mirroring the position arm) and
# ParticleManager::resolveEntityDirection(). This drives the REAL renderer
# body (no device, no GL) with a counting direction callback and the worker's
# real directionRequestEntities output, asserting the callback fired and its
# result landed in the pending map. RED at base (no arm: callback never
# fired); GREEN after.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_b293_entity_direction_resolution
        test_b293_entity_direction_resolution.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_b293_entity_direction_resolution PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_b293_entity_direction_resolution GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_b293_entity_direction_resolution DISCOVERY_TIMEOUT 120)
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
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_edd_reader.cpp
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
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_eff_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spells_new_edd_reader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
)
TARGET_LINK_LIBRARIES(test_spell_colour_and_overrides GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES} dl)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_colour_and_overrides PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_colour_and_overrides DISCOVERY_TIMEOUT 120)

# B1146 (#466): the spellsnew.edd colour words -- decode, the client's alpha,
# the per-region preset and the particle path (SimulationWorker::computeOnce).
# The decode cases run everywhere; the particle cases need GLES2.
ADD_EXECUTABLE(test_spell_edd_colour
    test_spell_edd_colour.cpp
)
TARGET_LINK_LIBRARIES(test_spell_edd_colour GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_edd_colour PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_edd_colour DISCOVERY_TIMEOUT 120)

# B1150 (#470): the TEN spawn shapes the official client decodes from the
# spellsnew.edd record (+0xA8) -- the reader's shape words at their code
# offsets, the shipped census (0:297 1:202 2:13 3:125 4:275 5:808 6:5 7:9
# 8:18 9:366, sum 2118, GTEST_SKIP without the client files, the
# test_spells_new_eff_reader pattern) and the per-case surface invariants of
# computeEddShapePoint() (the pure switch in spell_spawn_shape.h: point,
# ring, stacked rings + count table, cylinder, disc, sphere, box, cone,
# torus, ring-random; plus the offsets, tilts, +0xB0 fallback and the
# +0x19C scale). Pure logic -- no renderer, no device, no graphics TUs
# (the reader is in willeq_core, the shape switch is header-only), so it
# registers in both the graphics and the build-headless trees.
ADD_EXECUTABLE(test_spell_spawn_shapes
    test_spell_spawn_shapes.cpp
)
TARGET_LINK_LIBRARIES(test_spell_spawn_shapes GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_spell_spawn_shapes PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_spell_spawn_shapes DISCOVERY_TIMEOUT 120)

# B1151 (#471): the record's motion / spin / frame / flag words
# (refactor/B1151/README.md). The reader decode + the pure rules (frame
# banding, the +0x198 draw rule, the spin angle, the +0x178 alignment dispatch)
# are graphics-independent, but the per-term sim tests drive the real
# SimulationWorker, so the whole target registers under EQT_GRAPHICS.
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_spell_edd_motion
        test_spell_edd_motion.cpp
    )
    TARGET_LINK_LIBRARIES(test_spell_edd_motion GTest::gtest_main willeq_core)
    IF(IRRLICHT_LIBRARY)
        TARGET_INCLUDE_DIRECTORIES(test_spell_edd_motion PRIVATE ${IRRLICHT_INCLUDE_DIR})
    ENDIF()
    GTEST_DISCOVER_TESTS(test_spell_edd_motion DISCOVERY_TIMEOUT 120)
ENDIF()
