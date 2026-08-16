# Tests CMakeLists.txt

# Include Google Test
INCLUDE(GoogleTest)

# T36: repo root as a compile-time define, for EVERY test target in this
# directory and below.
#
# gtest_discover_tests() sets each test's ctest WORKING_DIRECTORY to the
# executable's own binary dir (<build>/tests), NOT the repo root. Any test
# that resolved a data path relatively therefore looked for it under
# <build>/tests/ and silently GTEST_SKIP()'d when it wasn't there -- which
# is every freshly created build/ dir, since build/ is gitignored. That hid
# the golden session replay, the primary regression oracle for every
# source-touching unit, behind a "Skipped" line that reads exactly like the
# legitimate "no capture recorded" skip.
#
# The pattern itself already existed (test_zone_lines.cpp's RepoPath(),
# added with its own per-target define and the comment "to stay independent
# of ctest's working directory") -- it just was never generalised. This
# makes it available everywhere so the next test that needs a repo file
# inherits it instead of rediscovering the trap.
ADD_COMPILE_DEFINITIONS(WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Common include directories for tests
INCLUDE_DIRECTORIES(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

# Default EQ client path (resolved at configure time, overridable via EQ_CLIENT_PATH env var)
ADD_COMPILE_DEFINITIONS(EQ_CLIENT_DATA_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")

# P39: path to the real shipped constrained-presets JSON, so tests can load
# it directly (not a synthetic copy) and assert against production values.
ADD_COMPILE_DEFINITIONS(WILLEQ_CONSTRAINED_PRESETS_JSON="${CMAKE_SOURCE_DIR}/config/constrained_presets.json")

# SQLite flags for WilleqDatabase (needed by any target pulling in willeq_database.cpp/sqlite3.c)
ADD_COMPILE_DEFINITIONS(SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

# Helper variable for fmt library
IF(fmt_FOUND)
    SET(FMT_LIB fmt::fmt)
ELSE()
    SET(FMT_LIB ${FMT_LIBRARY})
ENDIF()

# Test utility functions - strings needs fmt
ADD_EXECUTABLE(test_strings
    test_strings.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/strings.cpp
)
TARGET_LINK_LIBRARIES(test_strings GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_strings)

# Test compression
ADD_EXECUTABLE(test_compression
    test_compression.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_compression GTest::gtest_main ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_compression)

# Test CRC32
ADD_EXECUTABLE(test_crc32
    test_crc32.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
)
TARGET_LINK_LIBRARIES(test_crc32 GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_crc32)

# P49: DRM mode-selection decision logic (docker/irrlicht-drm/
# drm_mode_select.h) -- pure, header-only, no libdrm/gbm/EGL/Irrlicht
# dependency, so it builds and runs on x86 even though the real DRM device
# code (CIrrDeviceFB.cpp) only compiles for ARM. See
# refactor/P49_drm_fail_loud.md.
ADD_EXECUTABLE(test_drm_mode_select
    test_drm_mode_select.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_drm_mode_select PRIVATE ${CMAKE_SOURCE_DIR})
TARGET_LINK_LIBRARIES(test_drm_mode_select GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_drm_mode_select)

# Test packet serialization - packet.cpp uses fmt
ADD_EXECUTABLE(test_packet
    test_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
)
TARGET_LINK_LIBRARIES(test_packet GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_packet)

# Test position utilities
ADD_EXECUTABLE(test_position
    test_position.cpp
    ${CMAKE_SOURCE_DIR}/src/client/position.cpp
)
TARGET_LINK_LIBRARIES(test_position GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_position)

# Test data verification
ADD_EXECUTABLE(test_data_verification
    test_data_verification.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/data_verification.cpp
)
TARGET_LINK_LIBRARIES(test_data_verification GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_data_verification)

# Test UDP transport
ADD_EXECUTABLE(test_udp_transport
    test_udp_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/posix_udp_transport.cpp
)
TARGET_LINK_LIBRARIES(test_udp_transport GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_udp_transport)

# Test random
ADD_EXECUTABLE(test_random
    test_random.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/random.cpp
)
TARGET_LINK_LIBRARIES(test_random GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_random)

# Test logging infrastructure
ADD_EXECUTABLE(test_logging
    test_logging.cpp
)
TARGET_LINK_LIBRARIES(test_logging GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_logging)

# Integration test - network protocol
ADD_EXECUTABLE(test_integration_network
    test_integration_network.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_integration_network GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_integration_network)

# Protocol-level pcap replay tests (Batch R / R03).
# All of these SKIP cleanly when no capture is present: WILLEQ_PCAP env var,
# else data/captures/session.pcap (see docs/plans/session_replay_oracle.md).
# No capture ships in the repo, so on a stock checkout every test below
# reports SKIPPED rather than PASSED or FAILED.

# Test the pcap file reading/parsing utility itself (header-only, no project deps)
ADD_EXECUTABLE(test_pcap_extractor
    test_pcap_extractor.cpp
)
TARGET_LINK_LIBRARIES(test_pcap_extractor GTest::gtest)
GTEST_DISCOVER_TESTS(test_pcap_extractor)

# Test the standalone DaybreakTestHarness (replicates protocol decode logic
# for exploration/debugging without touching production code); needs zlib
# for the harness's own inflate calls.
ADD_EXECUTABLE(test_daybreak_harness
    test_daybreak_harness.cpp
)
TARGET_LINK_LIBRARIES(test_daybreak_harness GTest::gtest ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_daybreak_harness)

# Step-by-step verbose pcap decode verification (uses the standalone harness)
ADD_EXECUTABLE(test_pcap_step_through
    test_pcap_step_through.cpp
)
TARGET_LINK_LIBRARIES(test_pcap_step_through GTest::gtest ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_pcap_step_through)

# Test the REAL production DaybreakConnection::ProcessPacket() against a
# pcap capture (private-member access via #define private public).
ADD_EXECUTABLE(test_daybreak_connection_pcap
    test_daybreak_connection_pcap.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/daybreak_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/posix_udp_transport.cpp
)
TARGET_LINK_LIBRARIES(test_daybreak_connection_pcap GTest::gtest ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_daybreak_connection_pcap)

# T17: DaybreakConnection/DaybreakConnectionManager state-machine tests
# driven through a FakeUdpTransport (tests/fake_udp_transport.h) — session
# lifecycle, sequencing, fragmentation (both directions), ack/retransmit,
# CRC, compression, combined packets. No real sockets, no wall-clock sleeps.
ADD_EXECUTABLE(test_daybreak_state_machine
    test_daybreak_state_machine.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/daybreak_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
)
TARGET_LINK_LIBRARIES(test_daybreak_state_machine GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_daybreak_state_machine)

# Test Titanium opcodes are correctly hard-coded
ADD_EXECUTABLE(test_titanium_opcodes
    test_titanium_opcodes.cpp
)
TARGET_LINK_LIBRARIES(test_titanium_opcodes GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_titanium_opcodes)

# T02: EventBus unit tests (src/client/state/event_bus.cpp). No graphics,
# no Irrlicht -- links only the EventBus TU itself.
ADD_EXECUTABLE(test_event_bus
    test_event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
)
TARGET_LINK_LIBRARIES(test_event_bus GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_event_bus)

# T02: state module unit tests, part 1 (EntityManager, PlayerState,
# SpellState, InventoryState -- the priority-1 modules). No graphics, no
# Irrlicht.
ADD_EXECUTABLE(test_state_modules
    test_state_modules.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/entity_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
)
TARGET_LINK_LIBRARIES(test_state_modules GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_state_modules)

# T02: state module unit tests, part 2 (GroupState, PetState, WorldState,
# CombatState, TradeskillState, GameState). Split from test_state_modules
# purely to stay under the project's file-size check
# (scripts/refactor/check_file_sizes.sh warns above 1500 lines).
# game_state.cpp also pulls in door_state_manager.cpp (client/state/game_state.h
# dependency). No graphics, no Irrlicht.
ADD_EXECUTABLE(test_state_modules_secondary
    test_state_modules_secondary.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/entity_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/group_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/pet_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/trade_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/world_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/combat_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/tradeskill_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/game_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/door_state_manager.cpp
)
TARGET_LINK_LIBRARIES(test_state_modules_secondary GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_state_modules_secondary)

# T12: ActionDispatcher routing + guard behavior tests. Same state-module
# dependency set as test_state_modules_secondary, plus action_dispatcher.cpp
# itself. No graphics, no Irrlicht, no EverQuest -- the action layer's only
# includes are game_state.h, output_renderer.h, logging.h and stdlib.
SET(ACTION_LAYER_STATE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/entity_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/group_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/pet_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/trade_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/world_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/combat_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/tradeskill_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/game_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/door_state_manager.cpp
)

ADD_EXECUTABLE(test_action_dispatcher
    test_action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_action_dispatcher GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_action_dispatcher)

# T12: CommandProcessor tests -- registration, completion, category
# consistency, argument parsing, case sensitivity. Needs ActionDispatcher
# (real, driven through a recording IActionHandler fake) plus the same
# state-module set. IOutputRenderer is a pure interface satisfied entirely
# by a local test fake -- output_renderer.cpp / console_renderer.cpp /
# graphical_renderer.cpp (which pulls in Irrlicht) are NOT linked.
ADD_EXECUTABLE(test_command_processor
    test_command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_command_processor GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_command_processor)

# T15: InputActionBridge tests -- the T12 follow-on. Driven through a
# scriptable IInputHandler fake (action_test_fakes.h); observes both the
# ActionCallback and the recording handler behind a real ActionDispatcher.
# CommandProcessor is linked for the console-forwarding path. Same rule as
# the other two: no Irrlicht, no EverQuest.
ADD_EXECUTABLE(test_input_action_bridge
    test_input_action_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/input_action_bridge.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_input_action_bridge GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_input_action_bridge)

# T40: GameStateSyncBridge tests -- the decorator that feeds
# Application::m_gameState's PetState from bridge events instead of the
# deleted syncPetState() poll. Driven through a local RecordingBridge fake
# standing in for IrrlichtBridge/ConsoleBridge, so no graphics deps needed.
ADD_EXECUTABLE(test_game_state_sync_bridge
    test_game_state_sync_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/client/bridge/game_state_sync_bridge.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_game_state_sync_bridge GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_game_state_sync_bridge)

# T47: bridge-pointer teardown hygiene (EverQuest::m_bridge + the four
# managers'). Needs a real EverQuest (for setBridge()/GetHotbarModel()), so
# links willeq_core the same way test_spell_manager does.
ADD_EXECUTABLE(test_bridge_teardown
    test_bridge_teardown.cpp
)
TARGET_LINK_LIBRARIES(test_bridge_teardown GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_bridge_teardown PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_bridge_teardown)

# Test combat enums and structures
ADD_EXECUTABLE(test_combat
    test_combat.cpp
)
TARGET_LINK_LIBRARIES(test_combat GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_combat)

# T23: CombatManager behavior coverage (targeting, auto-attack/fire,
# cycling, consider, state machine, config round-trips, the loot-path
# blocking-sleep BUG_ pin). Reaches the live CombatManager through a bare
# EverQuest via GetCombatManager() -- Option A, no seam/interface
# extraction (refactor/T23_spell_combat_seam_scoping.md). Same replay-seam
# idiom as test_zone_entities.cpp (T18); uses willeq_core for the same
# reason.
ADD_EXECUTABLE(test_combat_manager
    test_combat_manager.cpp
)
TARGET_LINK_LIBRARIES(test_combat_manager GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_combat_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_combat_manager)

# T28: coverage for the four eq_commands.cpp readers repointed from the
# never-populated state-layer CombatState target to the authoritative
# CombatManager (refactor/T28_state_layer_readers.md Part 2). Same seam as
# test_combat_manager.cpp (bare EverQuest via MakeEq(), live CombatManager
# via GetCombatManager()) plus ProcessChatInput() -- the same public entry
# point production chat input uses -- to reach the command-registry lambda
# handlers in eq_commands.cpp, and the bridge's SystemMessage event (same
# idiom as test_zone_chat.cpp) to observe AddChatSystemMessage() output.
ADD_EXECUTABLE(test_eq_commands_assist
    test_eq_commands_assist.cpp
)
TARGET_LINK_LIBRARIES(test_eq_commands_assist GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_commands_assist PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_commands_assist)

# T48: /vendorlist, /buy, /sell coverage (eq_commands.cpp
# RegisterVendorCommands()) -- same MakeEq()/TestBridge/ProcessChatInput seam
# as test_eq_commands_assist.cpp above, plus ReplayZone()-driven vendor-open
# setup (HC_OP_ShopRequest, HC_OP_ItemPacket) and bridge.drainIntents() to
# assert the pushed VendorBuyIntent/VendorSellIntent fields directly.
ADD_EXECUTABLE(test_eq_commands_vendor
    test_eq_commands_vendor.cpp
)
TARGET_LINK_LIBRARIES(test_eq_commands_vendor GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_commands_vendor PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_commands_vendor)

# T50: /bank, /train coverage (eq_commands_spells.cpp RegisterSpellCommands())
# -- same seam as test_eq_commands_vendor.cpp above. /bank asserts the pushed
# BankerInteractIntent's fields (bridge.drainIntents()); /train's new
# distance/class pre-validation is a direct check, not an intent, so those
# cases assert the printed SystemMessage text itself, per the plan's bar.
ADD_EXECUTABLE(test_eq_commands_bank_train
    test_eq_commands_bank_train.cpp
)
TARGET_LINK_LIBRARIES(test_eq_commands_bank_train GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_commands_bank_train PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_commands_bank_train)

# T66: /equip coverage (eq_commands.cpp RegisterEquipmentCommands()) -- same
# seam as test_eq_commands_vendor.cpp/test_eq_commands_bank_train.cpp above.
# Asserts the pushed MoveItemIntent's fields directly (bridge.drainIntents())
# -- the SAME intent the inventory UI's drag/drop gesture pushes -- plus
# argument parsing and slot-validation refusal messages. The live leg
# (SendMoveItem's outgoing packet, and whether a real server actually equips
# a real item) is explicitly outstanding -- sibling T65 holds the eqemu lock
# this round; see refactor/T66_camera_and_equip_reachability.md's Review.
ADD_EXECUTABLE(test_eq_commands_equip
    test_eq_commands_equip.cpp
)
TARGET_LINK_LIBRARIES(test_eq_commands_equip GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_commands_equip PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_commands_equip)

# T64: parseArguments()-only coverage for the video-capture evidence tool's
# CLI flags (--dump-frames-dir/--dump-every/--dump-max-frames). Static
# method, no renderer/GL context needed -- see the file header comment for
# what is and isn't covered here.
ADD_EXECUTABLE(test_video_capture_flags
    test_video_capture_flags.cpp
)
TARGET_LINK_LIBRARIES(test_video_capture_flags GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_video_capture_flags PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_video_capture_flags)

# T51 added coverage here for the then-consolidated attack path
# (eq_commands_movement.cpp's registry-1 attack/stopattack/aa). T102
# MIGRATES attack/stopattack/aa/target (plus assist/shield/duel from
# test_eq_commands_assist.cpp) to action::CommandProcessor and deletes the
# game-registry registrations this file's ProcessChatInput()-seam tests
# depended on -- test_eq_commands_attack.cpp is REMOVED (not repointed);
# equivalent coverage now lives in test_command_processor.cpp's T102
# section (FakeActionHandler-based, matching every other migrated family).
# See refactor/T102_migrate_combat_social.md.

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
# T79 (2026-08-12): raised from the GoogleTest module's 5s default.
# `test_spell_manager` links willeq_core (a heavy TU set); its --gtest_list_tests
# discovery run failed 3x today under `-j24` parallel build load with
# "GoogleTestAddTests.cmake: Error running test executable" -- a
# discovery-time timeout, not a compile error, and each time clean on
# retry. Raising the timeout (vs. moving to PRE_TEST discovery mode) keeps
# discovery at build/post-build time and is compatible with this project's
# CMAKE_MINIMUM_REQUIRED(3.14) -- DISCOVERY_MODE PRE_TEST is a 3.20+
# keyword. Trade-off: this doesn't remove the contention (discovery still
# runs concurrently with -j24 compilation), it just gives it more slack;
# a sufficiently loaded host could still exceed 60s.
GTEST_DISCOVER_TESTS(test_spell_manager DISCOVERY_TIMEOUT 60)

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

# Test raycast mesh and map loading
ADD_EXECUTABLE(test_raycast
    test_raycast.cpp
    ${CMAKE_SOURCE_DIR}/src/client/raycast_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/client/hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_raycast GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_raycast)

# T19: hc_map.cpp (HCMap V1/V2 binary map format) unit tests. Real-fixture
# tests read maps from /mnt/projects/akk-stack/server/maps/base/ (READ-ONLY
# external eqemu install), overridable via WILLEQ_MAP_FIXTURES env var, and
# GTEST_SKIP cleanly when absent. Synthetic malformed-input tests need no
# fixtures at all -- always run. Links hc_map.cpp + raycast_mesh.cpp +
# compression.cpp directly (same set as test_raycast, minus willeq_core) so
# it stays independent of graphics/Irrlicht.
ADD_EXECUTABLE(test_hc_map
    test_hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/client/hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/client/raycast_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_hc_map GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_hc_map)

# T19: zone_lines.cpp (EQT::ZoneLines JSON parser, both the zone-specific
# data/zone_lines/*.json format and the legacy data/zone_lines.json format)
# unit tests. Real fixtures are checked-in repo data (no external
# dependency, no env var needed). zone_lines.cpp calls into HCMap
# (expandZoneLinesToGeometry), so links the same hc_map/raycast_mesh/
# compression set as test_hc_map above, plus jsoncpp.
ADD_EXECUTABLE(test_zone_lines
    test_zone_lines.cpp
    ${CMAKE_SOURCE_DIR}/src/client/zone_lines.cpp
    ${CMAKE_SOURCE_DIR}/src/client/hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/client/raycast_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_COMPILE_DEFINITIONS(test_zone_lines PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
TARGET_LINK_LIBRARIES(test_zone_lines GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES} ${JSONCPP_LIBRARIES})
GTEST_DISCOVER_TESTS(test_zone_lines)

# Test EQ client structs and enums
ADD_EXECUTABLE(test_eq_client
    test_eq_client.cpp
    ${CMAKE_SOURCE_DIR}/src/client/position.cpp
)
TARGET_LINK_LIBRARIES(test_eq_client GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_eq_client)

# Test pathfinder interface and null implementation
SET(TEST_PATHFINDER_SOURCES
    test_pathfinder.cpp
    ${CMAKE_SOURCE_DIR}/src/client/pathfinder_interface.cpp
    ${CMAKE_SOURCE_DIR}/src/client/pathfinder_null.cpp
)
SET(TEST_PATHFINDER_LIBS GTest::gtest_main ${FMT_LIB})

IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    LIST(APPEND TEST_PATHFINDER_SOURCES
        ${CMAKE_SOURCE_DIR}/src/client/pathfinder_nav_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
    )
    LIST(APPEND TEST_PATHFINDER_LIBS ${RECAST_LIBRARY} ${DETOUR_LIBRARY} ${ZLIB_LIBRARIES})
ENDIF()

ADD_EXECUTABLE(test_pathfinder ${TEST_PATHFINDER_SOURCES})
TARGET_LINK_LIBRARIES(test_pathfinder ${TEST_PATHFINDER_LIBS})
GTEST_DISCOVER_TESTS(test_pathfinder)

# Test packet structures (Titanium-specific)
ADD_EXECUTABLE(test_packet_structs
    test_packet_structs.cpp
)
TARGET_LINK_LIBRARIES(test_packet_structs GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_packet_structs)

# Test SessionRecorder (R01: session log writer for replay)
ADD_EXECUTABLE(test_session_recorder
    test_session_recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/session_recorder.cpp
)
TARGET_LINK_LIBRARIES(test_session_recorder GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_session_recorder)

# Test FormattedMessage parsing (link extraction)
ADD_EXECUTABLE(test_formatted_message
    test_formatted_message.cpp
    ${CMAKE_SOURCE_DIR}/src/client/formatted_message.cpp
)
TARGET_LINK_LIBRARIES(test_formatted_message GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_formatted_message)

# Test frustum culler (pure math + logging for diagnostics)
ADD_EXECUTABLE(test_frustum_culler
    test_frustum_culler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/frustum_culler.cpp
)
TARGET_LINK_LIBRARIES(test_frustum_culler GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_frustum_culler)

# Test SIMD math optimizations (pure math + logging for diagnostics)
ADD_EXECUTABLE(test_simd_math
    test_simd_math.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/frustum_culler.cpp
)
TARGET_LINK_LIBRARIES(test_simd_math GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_simd_math)

# Test BackgroundWorkQueue abstraction (pool-backed mode needs background_thread_pool.cpp)
ADD_EXECUTABLE(test_background_work_queue
    test_background_work_queue.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
)
TARGET_LINK_LIBRARIES(test_background_work_queue GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_background_work_queue)

# Test BackgroundThreadPool (shared priority-ordered thread pool)
ADD_EXECUTABLE(test_background_thread_pool
    test_background_thread_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
)
TARGET_LINK_LIBRARIES(test_background_thread_pool GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_background_thread_pool)

# Test WorkPriorityKey unified priority system (header-only, no external dependencies)
ADD_EXECUTABLE(test_work_priority
    test_work_priority.cpp
)
TARGET_LINK_LIBRARIES(test_work_priority GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_work_priority)

# Test StringDatabase loading and formatting
ADD_EXECUTABLE(test_string_database
    test_string_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/string_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/formatted_message.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_LINK_LIBRARIES(test_string_database GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_string_database)

# Test spell database loading and querying
ADD_EXECUTABLE(test_spell_database
    test_spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_COMPILE_DEFINITIONS(test_spell_database PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_spell_database GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_spell_database)

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
GTEST_DISCOVER_TESTS(test_spells_eff_reader)

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
GTEST_DISCOVER_TESTS(test_spell_visual_selection)

# Test WilleqDatabase::open() schema-version validation (T56)
ADD_EXECUTABLE(test_willeq_database
    test_willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)
TARGET_LINK_LIBRARIES(test_willeq_database GTest::gtest_main ${FMT_LIB} dl)
GTEST_DISCOVER_TESTS(test_willeq_database)

# Test PFS/S3D archive parsing
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_pfs_archive
        test_pfs_archive.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_LINK_LIBRARIES(test_pfs_archive GTest::gtest_main ${ZLIB_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_pfs_archive)
ENDIF()

# Test WLD file parsing (Phase 2: Header and String Table)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_wld
        test_wld_header.cpp
        test_wld_fragments_mesh.cpp
        test_wld_fragments_skeleton.cpp
        test_wld_fragments_light.cpp
        test_wld_fragments_bsp.cpp
        test_wld_conversions.cpp
        test_wld_texture_loading.cpp
        test_wld_zones.cpp
        test_wld_zones_comprehensive.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
    )
    TARGET_LINK_LIBRARIES(test_wld GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_wld)
ENDIF()

# Test constrained renderer configuration (presets, resolution calculation, clamping)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_constrained_renderer
        test_constrained_renderer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_LINK_LIBRARIES(test_constrained_renderer GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_constrained_renderer)
ENDIF()

# Test graphics components (PFS, DDS decoder, etc.)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_graphics
        test_graphics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/graphics_archive_index.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(test_graphics PRIVATE ${CMAKE_SOURCE_DIR}/src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(test_graphics PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_graphics GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES} dl)
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(test_graphics EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment in the top-level CMakeLists.txt (same P25/P26
        # parity gap: CIrrDeviceFB.cpp's windowed-EGL backend references X11
        # symbols the static archive needs resolved at link time).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(test_graphics X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — the
    # mandatory-Irrlicht gate in the top-level CMakeLists.txt guarantees
    # EQT_DRM OR EQT_GLES2 whenever IRRLICHT_LIBRARY is set
    # (refactor/desktop_gl_elimination_plan.md §6 Phase 3).
    # T79 (2026-08-12): same discovery-time timeout under -j24 parallel
    # build load as test_spell_manager above (test_graphics links Irrlicht +
    # willeq_core, another heavy binary) -- see the comment on
    # GTEST_DISCOVER_TESTS(test_spell_manager ...) for the mechanism and
    # trade-off.
    GTEST_DISCOVER_TESTS(test_graphics DISCOVERY_TIMEOUT 60)

    # Test HotkeyManager (requires Irrlicht for key codes)
    ADD_EXECUTABLE(test_hotkey_manager
        test_hotkey_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/input/hotkey_manager.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotkey_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotkey_manager GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_hotkey_manager)
ENDIF()

# Test asset loading (equipment, race models, textures, animations, lights)
# These tests verify EQ assets load without errors
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_asset_loading
        test_asset_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/graphics_archive_index.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(test_asset_loading PRIVATE ${CMAKE_SOURCE_DIR}/src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(test_asset_loading PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_asset_loading GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES} dl)
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(test_asset_loading EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment in the top-level CMakeLists.txt (same P25/P26
        # parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(test_asset_loading X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — see the
    # test_graphics comment above (same mandatory-Irrlicht gate).
    GTEST_DISCOVER_TESTS(test_asset_loading)
ENDIF()

# P47: entity-viewer regression pin. Deliberately lightweight -- shells out
# to the already-built gles2_zone_benchmark's --entity-viewer mode (see
# tools/hwcaps/zone_bench_entity_viewer.h/.cpp) and parses its
# ENTITY_VIEWER_RESULT stdout lines, rather than re-linking the whole
# graphics/zone-loading pipeline into yet another test binary. Only needs
# GTest + <filesystem>/popen. Asset-gated exactly like test_asset_loading
# (EQ_CLIENT_PATH env var, else data/eq_client symlink, else skip) -- plus a
# second skip guard for the sibling binary not existing (e.g. EQT_GRAPHICS
# off, or the target simply hasn't been rebuilt yet in this tree).
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_entity_viewer_regression
        test_entity_viewer_regression.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_entity_viewer_regression PRIVATE
        GLES2_ZONE_BENCHMARK_BIN="$<TARGET_FILE:gles2_zone_benchmark>")
    TARGET_LINK_LIBRARIES(test_entity_viewer_regression GTest::gtest_main)
    ADD_DEPENDENCIES(test_entity_viewer_regression gles2_zone_benchmark)
    GTEST_DISCOVER_TESTS(test_entity_viewer_regression)
ENDIF()

# T30: exit-matrix pin for "headless + piped stdin + no server" -- the case
# P36's "all clean" exit matrix never covered (see
# refactor/T30_headless_shutdown_fix.md). Deliberately lightweight, same
# shellout-via-popen() pattern as test_entity_viewer_regression above:
# spawns the actual willeq binary (unconditionally built regardless of
# EQT_GRAPHICS -- unlike the entity viewer, this needs no graphics assets)
# and asserts on its stdout/exit code instead of re-linking Application
# into another test binary.
ADD_EXECUTABLE(test_headless_shutdown
    test_headless_shutdown.cpp
)
TARGET_COMPILE_DEFINITIONS(test_headless_shutdown PRIVATE
    WILLEQ_BIN_PATH="$<TARGET_FILE:willeq>")
TARGET_LINK_LIBRARIES(test_headless_shutdown GTest::gtest_main)
ADD_DEPENDENCIES(test_headless_shutdown willeq)
GTEST_DISCOVER_TESTS(test_headless_shutdown)

# Test door loading (unit tests + graphics tests)
# Unit tests run without display, graphics tests need DISPLAY=:99
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_door_loading
        test_door_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/frustum_culler.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/software_occlusion_culler.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(test_door_loading PRIVATE ${CMAKE_SOURCE_DIR}/src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(test_door_loading PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_door_loading GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES})
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(test_door_loading EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment in the top-level CMakeLists.txt (same P25/P26
        # parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(test_door_loading X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — see the
    # test_graphics comment above (same mandatory-Irrlicht gate).
    GTEST_DISCOVER_TESTS(test_door_loading)
ENDIF()

# Integration test: Zoning (connects to real EQEmu server)
# This test is NOT auto-discovered because it requires a running server.
# Run manually with: ./bin/test_zoning_integration
ADD_EXECUTABLE(test_zoning_integration
    test_zoning_integration.cpp
)
TARGET_LINK_LIBRARIES(test_zoning_integration GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zoning_integration PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
# Note: Not using GTEST_DISCOVER_TESTS because this requires a running server

# Batch R (R02): headless session replay harness / game-state regression
# oracle. Synthetic mode always runs (no network, no EQ client files); golden
# mode activates only when data/captures/session.sleq exists and GTEST_SKIPs
# otherwise, so this is safe to auto-discover.
ADD_EXECUTABLE(test_session_replay
    test_session_replay.cpp
)
TARGET_LINK_LIBRARIES(test_session_replay GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_session_replay PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_session_replay)

# T04: zone connection-flow sequencing tests (CheckZoneRequestPhaseComplete
# gating, dispatcher robustness, observable post-handshake state). Same
# replay-seam idiom as test_session_replay above -- no network, no graphics,
# no live connection -- so safe to auto-discover.
ADD_EXECUTABLE(test_zone_connection_flow
    test_zone_connection_flow.cpp
)
TARGET_LINK_LIBRARIES(test_zone_connection_flow GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_connection_flow PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_connection_flow)

# T52: pins the x/y coordinate convention at the three m_pending_zone_x/y/z
# sites (two producers in eq_zone.cpp, one consumer in eq_login_world.cpp).
# Same replay-seam idiom as test_zone_connection_flow above -- no network,
# no graphics, no live connection -- so safe to auto-discover.
ADD_EXECUTABLE(test_zone_position_convention
    test_zone_position_convention.cpp
)
TARGET_LINK_LIBRARIES(test_zone_position_convention GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_position_convention PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_position_convention)

# T88: headless position-frame round-trip regression tests (inbound
# server-frame decode -> outbound client-frame encode -> server-frame
# re-encode is identity; Move()/MoveToWithPath() operates in client frame).
# Same replay-seam idiom as test_zone_position_convention above -- no
# network, no graphics, no live connection -- so safe to auto-discover.
ADD_EXECUTABLE(test_position_frame_roundtrip
    test_position_frame_roundtrip.cpp
)
TARGET_LINK_LIBRARIES(test_position_frame_roundtrip GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_position_frame_roundtrip PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_position_frame_roundtrip)

# T90: EqActionHandler::setHeading() double-conversion regression tests
# (degrees -> EQ 512-units conversion applied on top of a value that was
# then stored raw by EverQuest::SetHeading(), which every other site reads
# as plain degrees). Same headless EverQuest + EqActionHandler idiom as
# T88's test_position_frame_roundtrip above -- no network, no graphics.
ADD_EXECUTABLE(test_setheading_conversion
    test_setheading_conversion.cpp
)
TARGET_LINK_LIBRARIES(test_setheading_conversion GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_setheading_conversion PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_setheading_conversion)

# T93: fake-substitution sweep. Regression tests for TRANSFORMS in
# EqActionHandler that RecordingActionHandler (tests/action_test_fakes.h)
# cannot catch -- the 1-indexed-to-0-indexed gem slot conversion shared by
# castSpell()/castSpellOnTarget()/memorizeSpell(). targetSelf()'s self/pet
# toggle logic was investigated and deferred (no test seam for
# EverQuest::m_entities -- see this file's own comment and refactor/
# deferred.md). Same headless EverQuest + EqActionHandler idiom as T88/T90
# above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_transforms
    test_eq_action_handler_transforms.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_transforms GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_transforms PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_transforms)

# T95: regression tests for the previously-unvalidated int->enum casts in
# EqActionHandler::setMovementMode()/setPositionState()/sendPetCommand() --
# T93's deferred finding. MovementMode/PositionState are plain enums with no
# fixed underlying type, so an out-of-range static_cast into them was
# undefined behaviour, not merely a wrong result. Same headless EverQuest +
# EqActionHandler idiom as T88/T90/T93 above -- no network, no graphics, no
# eqemu.
ADD_EXECUTABLE(test_eq_action_handler_enum_guards
    test_eq_action_handler_enum_guards.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_enum_guards GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_enum_guards PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_enum_guards)

# T103: regression tests proving EqActionHandler's trade methods
# (requestTrade/acceptTrade/cancelTrade/declineTrade) read precondition/
# display state from state::TradeState (the projection T103 added) rather
# than reaching into TradeManager directly. Same headless EverQuest +
# EqActionHandler idiom as T95 above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_trade
    test_eq_action_handler_trade.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_trade GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_trade PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_trade)

# T101: real-EqActionHandler coverage for the SpellManager-backed methods
# the Spells+Pet migration added/repointed (beginCastFromGem(),
# interruptCast() [repointed from CombatManager], forgetSpell() [repointed
# from a stub], memorizeSpellByName()). Same headless EverQuest +
# EqActionHandler idiom as T88/T90/T93/T95 above -- no network, no
# graphics, no eqemu. DB-dependent cases follow test_spell_manager.cpp's
# hasDb() + GTEST_SKIP precedent.
ADD_EXECUTABLE(test_eq_action_handler_spell_pet
    test_eq_action_handler_spell_pet.cpp
)
TARGET_COMPILE_DEFINITIONS(test_eq_action_handler_spell_pet PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_eq_action_handler_spell_pet GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_spell_pet PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
# T79 precedent (test_spell_manager, above): links willeq_core (a heavy TU
# set); its --gtest_list_tests discovery run can exceed the GoogleTest
# module's 5s default under -j24 parallel build load.
GTEST_DISCOVER_TESTS(test_eq_action_handler_spell_pet DISCOVERY_TIMEOUT 60)

# T05: login/world replay seam smoke test (ReplayLoginPacket/ReplayWorldPacket
# -> DispatchLoginPacket/DispatchWorldPacket). Same replay-seam idiom as
# test_zone_connection_flow above -- no network, no graphics, no live
# connection -- so safe to auto-discover.
ADD_EXECUTABLE(test_login_world_seam
    test_login_world_seam.cpp
)
TARGET_LINK_LIBRARIES(test_login_world_seam GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_world_seam PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_world_seam)

# T18: entity-domain zone-stream tests (eq_entities.cpp) -- real-capture
# full-stream replay + synthetic Spawn_Struct decode and malformed-packet
# guards. Same replay-seam idiom as test_zone_connection_flow.
ADD_EXECUTABLE(test_zone_entities
    test_zone_entities.cpp
)
TARGET_LINK_LIBRARIES(test_zone_entities GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_entities PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_entities)

# T18: chat-domain zone-stream tests (eq_chat.cpp) -- ChannelMessage channel
# mapping, /reply tracking, size guards, and real-capture chat replay,
# observed through GameStateBridge events.
ADD_EXECUTABLE(test_zone_chat
    test_zone_chat.cpp
)
TARGET_LINK_LIBRARIES(test_zone_chat GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_chat PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_chat)

# T06: login/world flow coverage (real-capture replay, synthetic sequencing,
# and robustness cases) -- the thing the T05 seam above was built for. Same
# replay-seam idiom -- no network, no graphics, no live connection -- so safe
# to auto-discover. Real-capture replay self-skips when data/captures/session.sleq
# is absent.
ADD_EXECUTABLE(test_login_world_flow
    test_login_world_flow.cpp
)
TARGET_LINK_LIBRARIES(test_login_world_flow GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_world_flow PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_world_flow)

# Integration test: Zoning with Graphics (connects to real EQEmu server with graphics enabled)
# This test is NOT auto-discovered because it requires a running server and X display.
# Run manually with: DISPLAY=:99 ./bin/test_zoning_graphics_integration
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zoning_graphics_integration
        test_zoning_graphics_integration.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zoning_graphics_integration PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zoning_graphics_integration GTest::gtest_main willeq_core)
    # Note: Not using GTEST_DISCOVER_TESTS because this requires a running server and display
ENDIF()

# Headless character-model loading/animation suite (RaceModelLoader +
# EQAnimatedMeshSceneNode). Originally an EQEmu-server + DISPLAY + paperdoll-
# UI integration test (InventoryModelViewTest); T54 (2026-08-10) deleted that
# fixture — its six TEST_F cases were unconditionally GTEST_SKIP()'d
# ("WindowManager has been deleted"), not environment-gated, because the
# paperdoll UI they exercised (WindowManager/InventoryWindow/
# CharacterModelView) was itself deleted in 0f25cb57. The salvageable
# assertions (textures applied, loads for race/gender, animation coherence)
# now live in RaceModelAnimationHeadlessTest (added P12B, extended T54),
# which needs neither a server nor a display — EDT_NULL device only. Safe
# to auto-discover.
#
# Filename kept as-is for this unit (renaming would churn CMake/git history
# for no test value) — T54's Review recommends test_race_model_loading.cpp
# as a truer name for a future rename.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_inventory_model_view
        test_inventory_model_view.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_inventory_model_view PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_inventory_model_view GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_inventory_model_view)
ENDIF()

# T54: first tests for the static-panel UI (StaticUIInput hit-testing,
# processKey popup dismissal, and an EDT_NULL renderInventoryPopup smoke
# test). Pure hit-testing needs no device at all; the optional rendering
# tail uses EDT_NULL like test_door_loading's DoorManagerGraphicsTest.
# Same gate as test_inventory_model_view (needs Irrlicht + the graphics UI
# sources in willeq_core) — safe to auto-discover, nothing here needs a
# server or DISPLAY.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_static_ui_panels
        test_static_ui_panels.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_static_ui_panels PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_static_ui_panels GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_static_ui_panels)
ENDIF()

# T60: the shared bone-lookup fallback chain (bone_attachment.h) — pure
# string logic over a synthetic skeleton, no device/Irrlicht driver needed
# (just the irr::core math types, header-only via willeq_core's include
# path). Nothing tested this before convergence.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_bone_attachment
        test_bone_attachment.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_bone_attachment PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_bone_attachment GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_bone_attachment)
ENDIF()

# T92: ConstrainedTextureCache coverage — the P50 masked-variant query-method
# gap (hasAlpha/getTexture/getAlphaTexture/isETC1Texture by-name overloads
# now take applyPaletteMask) and the Part 1 empty-input-vs-decode-failure
# message distinction at getOrLoad(). EDT_NULL for the bulk of it (no
# window/GPU/DISPLAY); one fixture (ConstrainedTextureCacheETC1Test) opens a
# real EDT_OGLES2 surfaceless device + the real qeynos2_obj.eqe1 archive
# (same asset test_eqe1_loader.cpp depends on) to exercise the ETC1-only
# fields (isETC1/alphaTexture), which only the real upload path can set —
# self-skips if either is unavailable.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_constrained_texture_cache
        test_constrained_texture_cache.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_constrained_texture_cache PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_constrained_texture_cache GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_constrained_texture_cache)
ENDIF()

# T123: ObjectNodeTextureGuard regression coverage — a detached object node's
# node-level Materials copy (what CMeshSceneNode::render() actually reads,
# ReadOnlyMaterials=false default) must not go dangling after
# ConstrainedTextureCache::clear() (the real zone-unload path) destroys its
# texture. See refactor/T123_texture_holder_audit.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_object_node_texture_guard
        test_object_node_texture_guard.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_object_node_texture_guard PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_object_node_texture_guard GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_object_node_texture_guard)
ENDIF()

# T124: shared eviction-swap primitive coverage (swapTextureInMaterial/
# swapTextureInMesh/swapTextureInNodeMaterials/swapTextureInMeshSceneNode)
# plus DoorTextureGuard (doors_ sibling of T123's ObjectNodeTextureGuard)
# and the singleton-subsystem holders (SpellVisualFX::swapEvictedTexture,
# BoidsManager::swapEvictedTexture). See
# refactor/T124_remaining_texture_holders.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_t124_texture_holder_guards
        test_t124_texture_holder_guards.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_t124_texture_holder_guards PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_t124_texture_holder_guards GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_t124_texture_holder_guards)
ENDIF()

# T66: /camera coverage (irrlicht_renderer.cpp processSlashCommand()) --
# genuinely headless end-to-end: exercises the real processSlashCommand() ->
# setCameraMode()/cycleCameraMode()/getCameraModeString() chain via a
# default-constructed, never-Init()'d IrrlichtRenderer (no Irrlicht device
# needed at all -- see the file header comment for why that's safe). Same
# "minimal fake bridge" idiom test_static_ui_panels.cpp (T54) uses to
# capture the command's chat() replies.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_irrlicht_camera_command
        test_irrlicht_camera_command.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_irrlicht_camera_command PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_irrlicht_camera_command GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_irrlicht_camera_command)
ENDIF()

# T111: coverage for IrrlichtRenderer::isEntityBlockingProgressiveLoad(), the
# pure predicate extracted from checkProgressiveLoadingComplete() to fix the
# deferred-entity-pickup bug T109 shipped inert around. No device needed --
# same reasoning as test_irrlicht_camera_command above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_progressive_load_rearm
        test_progressive_load_rearm.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_progressive_load_rearm PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_progressive_load_rearm GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_progressive_load_rearm)
ENDIF()

# T116: coverage for the SimulationOutput dangling-pointer class made
# impossible by construction -- applySimulationOutput()/
# bindFrameSimulationBuffers() split by lifetime contract. FRIEND_TEST-based
# access to private members, same "default-constructed, never-Init()'d
# IrrlichtRenderer, no device needed" idiom as test_irrlicht_camera_command
# above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_simulation_output_lifetime
        test_simulation_output_lifetime.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_simulation_output_lifetime PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_simulation_output_lifetime GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_simulation_output_lifetime)
ENDIF()

# Integration test: Beneficial Spell Casting (connects to real EQEmu server with graphics enabled)
# This test verifies beneficial spell casting works correctly - casting buffs on self and verifying
# they land properly. Tests casting multiple spells in series and buff refresh behavior.
# Run manually with: DISPLAY=:99 ./bin/test_beneficial_spell_casting
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_beneficial_spell_casting
        test_beneficial_spell_casting.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_beneficial_spell_casting PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_beneficial_spell_casting GTest::gtest_main willeq_core)
    # Note: Not using GTEST_DISCOVER_TESTS because this requires a running server and display
ENDIF()

# Common audio source files used by audio tests that need AudioManager
SET(AUDIO_COMMON_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/audio_mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/audio_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sfx_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)

# Common libs for audio tests (platform audio libs for miniaudio)
SET(AUDIO_COMMON_TEST_LIBS GTest::gtest_main ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
IF(APPLE)
    FIND_LIBRARY(COREAUDIO_FRAMEWORK CoreAudio)
    FIND_LIBRARY(AUDIOUNIT_FRAMEWORK AudioUnit)
    FIND_LIBRARY(COREFOUNDATION_FRAMEWORK CoreFoundation)
    LIST(APPEND AUDIO_COMMON_TEST_LIBS ${COREAUDIO_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
ELSEIF(UNIX)
    LIST(APPEND AUDIO_COMMON_TEST_LIBS dl m)
ENDIF()

# Test XMI decoder (XMI to MIDI conversion)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_xmi_decoder
        test_xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_xmi_decoder PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_xmi_decoder GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_xmi_decoder)
ENDIF()

# Test EFF file loader (zone sound emitter configuration)
# These tests verify _sounds.eff and _sndbnk.eff parsing
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_eff_loader
        test_eff_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
    )
    TARGET_COMPILE_DEFINITIONS(test_eff_loader PRIVATE WITH_AUDIO WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_LINK_LIBRARIES(test_eff_loader GTest::gtest_main ${FMT_LIB} dl)
    GTEST_DISCOVER_TESTS(test_eff_loader)
ENDIF()

# Test zone sound emitters (positioned sound sources in zones)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_zone_sound_emitters
        test_zone_sound_emitters.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_sound_emitters PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_zone_sound_emitters ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_zone_sound_emitters)
ENDIF()

# Test day/night audio system (Phase 13: game time based sound transitions)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_day_night_audio
        test_day_night_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_day_night_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_day_night_audio ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_day_night_audio)
ENDIF()

# Test zone music system (zone music mapping and transitions)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_zone_music
        test_zone_music.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_music PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_zone_music ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_zone_music)
ENDIF()

# Test sound assets (SoundAssets.txt parsing, WAV loading, AudioManager)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sound_assets
        test_sound_assets.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_sound_assets PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_sound_assets ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sound_assets)
ENDIF()

# Test sound effects (sound effect playback, 3D positioning, volume controls)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sound_effects
        test_sound_effects.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_sound_effects PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_sound_effects ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sound_effects)
ENDIF()

# Test spatial audio (3D positional audio, distance attenuation, listener position)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_spatial_audio
        test_spatial_audio.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_spatial_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_spatial_audio ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_spatial_audio)
ENDIF()

# Test creature sounds (Phase 15: NPC/creature sound file mapping)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_creature_sounds
        test_creature_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/creature_sounds.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_creature_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_creature_sounds GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_creature_sounds)
ENDIF()

# Test weather and water audio (Phase 17: water entry/exit, rain, thunder, wind)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_weather_audio
        test_weather_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/weather_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/water_sounds.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_weather_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_weather_audio ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_weather_audio)
ENDIF()

# Test RDP input handler (scancode translation)
# These tests verify RDP scancode to Irrlicht key code translation
IF(WITH_RDP)
    ADD_EXECUTABLE(test_rdp_input_handler
        test_rdp_input_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_input_handler.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_rdp_input_handler PRIVATE
        ${IRRLICHT_INCLUDE_DIR}
        ${FREERDP_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_rdp_input_handler PRIVATE WITH_RDP)
    TARGET_LINK_LIBRARIES(test_rdp_input_handler GTest::gtest_main ${FREERDP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_rdp_input_handler)
ENDIF()

# Test RDP server (initialization, lifecycle, callbacks)
# These tests verify RDP server functionality without actual network connections
IF(WITH_RDP)
    ADD_EXECUTABLE(test_rdp_server
        test_rdp_server.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_server.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_peer_context.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_input_handler.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_rdp_server PRIVATE
        ${IRRLICHT_INCLUDE_DIR}
        ${FREERDP_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_rdp_server PRIVATE WITH_RDP)
    TARGET_LINK_LIBRARIES(test_rdp_server GTest::gtest_main ${FREERDP_LIBRARIES} freerdp-server3 ${FMT_LIB} ${OPENSSL_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_rdp_server)
ENDIF()

# Test UI sounds (UI sound type mappings, sound IDs, filenames)
# These tests verify the UISounds utility class and UI sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_ui_sounds
        test_ui_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/ui_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_ui_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_ui_sounds GTest::gtest_main ${FMT_LIB} ZLIB::ZLIB)
    GTEST_DISCOVER_TESTS(test_ui_sounds)
ENDIF()

# Test door and object sounds (door/lever/trap sound type mappings, sound IDs, filenames)
# These tests verify the DoorSounds utility class and door/object sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_door_sounds
        test_door_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/door_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_door_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_door_sounds GTest::gtest_main ${FMT_LIB} ZLIB::ZLIB)
    GTEST_DISCOVER_TESTS(test_door_sounds)
ENDIF()

# Test player character sounds (death, drown, jump, gethit, gasp)
# These tests verify the PlayerSounds utility class and race/gender sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_player_sounds
        test_player_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/player_sounds.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_player_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_player_sounds GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_player_sounds)
ENDIF()

# Test combat music stingers (Phase 19: combat music cues during fights)
# These tests verify the CombatMusicManager class and combat stinger logic
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_combat_music
        test_combat_music.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/combat_music.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_combat_music PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_combat_music ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_combat_music)

    # Music loopback compare test (manual test for debugging RDP audio)
    ADD_EXECUTABLE(test_music_loopback_compare
        test_music_loopback_compare.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
    )
    TARGET_COMPILE_DEFINITIONS(test_music_loopback_compare PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_music_loopback_compare GTest::gtest ${AUDIO_COMMON_TEST_LIBS})
    # Note: Not using GTEST_DISCOVER_TESTS as this is a manual command-line tool
ENDIF()

# Test EQE1 flat pack loader (standalone, no GTest — uses real .eqe1 files)
ADD_EXECUTABLE(test_eqe1_loader
    test_eqe1_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_eqe1_loader PRIVATE ${CMAKE_SOURCE_DIR}/include)
ADD_TEST(NAME test_eqe1_loader COMMAND test_eqe1_loader)

# H02: golden-frame/metric/memory CI gate on top of H01's x86 GLES2 build
# (build-gles2/, gitignored, produced by scripts/build-gles2-x86.sh). Not a
# GTest binary — it's a script that drives gles2_zone_benchmark directly.
# Registered unconditionally (not gated on an EQT_GLES2 CMake option, since
# build-gles2/ is a separate, independently-configured build tree from this
# one) — the script itself detects a missing benchmark binary and exits 125,
# which SKIP_RETURN_CODE below turns into a clean ctest SKIP rather than a
# failure, so the default `build/` + ctest suite stays fully independent of
# whether build-gles2/ has ever been built in this worktree.
ADD_TEST(NAME visual_gate COMMAND ${CMAKE_SOURCE_DIR}/scripts/ci/visual_gate.sh)
SET_TESTS_PROPERTIES(visual_gate PROPERTIES SKIP_RETURN_CODE 125)

# T108: regression test -- totalEstimate must not credit eqe1MappedBytes
# (address-space/mapped size, not RSS) as if it were resident. Drives the
# already-built gles2_zone_benchmark (same tool + same computeMemorySnapshot()
# the memory gate uses -- tools/hwcaps/zone_bench_memory.cpp) with real EQE1
# archive data. SKIPs (125, same convention as visual_gate above) if the
# benchmark wasn't built (EQT_GRAPHICS off) or no EQE1 archive data is present
# on this machine -- see the script's own docstring.
IF(TARGET gles2_zone_benchmark)
    ADD_TEST(NAME test_eqe1_totalestimate_accounting
        COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/test_eqe1_totalestimate_accounting.py
                $<TARGET_FILE:gles2_zone_benchmark>)
    SET_TESTS_PROPERTIES(test_eqe1_totalestimate_accounting PROPERTIES SKIP_RETURN_CODE 125)
ENDIF()
