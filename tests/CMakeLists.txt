# Tests CMakeLists.txt

# Include Google Test
INCLUDE(GoogleTest)

# T163 (2026-08-21): move gtest test discovery OUT of the parallel build.
#
# The default DISCOVERY_MODE is POST_BUILD: CMake attaches a post-build
# command to every test target that runs the freshly linked binary with
# --gtest_list_tests *as part of the build*. With the 112
# GTEST_DISCOVER_TESTS() calls below and `cmake --build build -j24`, that is
# up to 112 process launches competing with 24 compile/link jobs -- and the
# GoogleTest module's discovery timeout defaults to 5 SECONDS. Three units in
# three days (T159, T161, T162) plus T79's 3x before them saw the first
# from-scratch -j24 build die inside GoogleTestAddTests.cmake with no compiler
# error; T162 captured the verbatim reason: "Error running test executable ...
# test_login_world_seam ... Process terminated due to timeout". An immediate
# rebuild was always clean, because by then nothing else was competing.
#
# The aggravating factor is that this checkout lives on an NFS mount: each
# test binary is 70-80 MB, and discovery exec's it immediately after the
# linker wrote it, while up to 24 other links are saturating the same mount.
# Measured on an idle host, the slowest --gtest_list_tests of all 118 test
# binaries is 0.21s; sampled continuously through a full from-scratch -j24
# build it peaked at 1.07s. Neither is near 5s, which is why the failure is
# intermittent rather than constant -- it needs a cold read-back of a
# just-linked binary through a loaded NFS mount on top of the CPU contention.
#
# PRE_TEST moves discovery to ctest time, where it runs serially and with no
# compile/link load at all, so the contention is removed rather than merely
# given more slack (which is all T79's per-target raised timeouts did -- see
# the removal note further down for why those never covered the right targets
# anyway). One variable applies to every GTEST_DISCOVER_TESTS() call in this
# directory, so there is nothing to remember at the 112 call sites.
#
# Version floor: DISCOVERY_MODE and this variable were both added in CMake
# 3.18, NOT 3.20 as T79's comment stated -- that mistake is what made T79
# reject this fix. The oldest CMake that has to configure this project is
# 3.18.4 (debian:bullseye, docker/Dockerfile.arm-cross; every other path is
# ubuntu:24.04 / 3.28). So no build path is left behind and no version guard
# is needed. CMAKE_MINIMUM_REQUIRED stays at 3.14 deliberately: raising it
# would flip ~15 CMake policies to NEW across a 1700-line CMakeLists for no
# reason connected to this fix. On a hypothetical pre-3.18 CMake this
# variable is simply ignored and discovery falls back to POST_BUILD, which is
# today's behaviour -- degraded, never broken. The warning below makes that
# visible instead of silent.
#
# What PRE_TEST does NOT change: gtest_discover_tests() still sets each
# test's WORKING_DIRECTORY to CMAKE_CURRENT_BINARY_DIR (<build>/tests) in
# both modes, so the T36 note directly below still holds, and `ctest -R
# <name>` still selects individual tests.
IF(CMAKE_VERSION VERSION_LESS 3.18)
    MESSAGE(WARNING
        "CMake ${CMAKE_VERSION} predates DISCOVERY_MODE (3.18+); gtest test "
        "discovery falls back to POST_BUILD and the -j24 discovery-timeout "
        "failure T163 fixed can recur. Use CMake 3.18 or newer.")
ENDIF()
SET(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

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

# T130: regression test for the vendored TinySoundFont OOB read
# (third_party/TinySoundFont/tsf.h's "WILLEQ T130" clamp). Self-contained --
# includes tsf.h with TSF_IMPLEMENTATION directly (same pattern as
# midi_player.cpp), no audio backend/device needed, no WITH_AUDIO gate.
ADD_EXECUTABLE(test_tsf_oob_read
    test_tsf_oob_read.cpp
)
TARGET_LINK_LIBRARIES(test_tsf_oob_read GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_tsf_oob_read)

# T131: verifies tsf_set_max_voices + all-16-channels-precreated is applied
# on every MidiPlayer construction path, including the runtime reload
# (MidiPlayer::loadSoundFont). Links midi_player.cpp directly (needs
# WITH_AUDIO for the class to exist at all) plus its own required data (no
# AudioManager/AudioMixer/audio device needed -- MidiPlayer itself has no
# device dependency).
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_midi_player_prealloc
        test_midi_player_prealloc.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_midi_player_prealloc PRIVATE WITH_AUDIO
        WILLEQ_TEST_SF2_PATH="${CMAKE_SOURCE_DIR}/data/1mgm.sf2")
    TARGET_LINK_LIBRARIES(test_midi_player_prealloc GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_midi_player_prealloc)
ENDIF()

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
    # T173: EntityManager ranks a living entity above its own corpse and
    # asks the ONE shared predicate; the TU is dependency-free by design.
    ${CMAKE_SOURCE_DIR}/src/client/corpse_predicate.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
)
TARGET_LINK_LIBRARIES(test_state_modules GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_state_modules)

# T02: state module unit tests, part 2 (GroupState, PetState, WorldState,
# CombatState, GameState; TradeskillState deleted T232). Split from test_state_modules
# purely to stay under the project's file-size check
# (scripts/refactor/check_file_sizes.sh warns above 1500 lines).
# game_state.cpp also pulls in door_state_manager.cpp (client/state/game_state.h
# dependency). No graphics, no Irrlicht.
ADD_EXECUTABLE(test_state_modules_secondary
    test_state_modules_secondary.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/entity_manager.cpp
    # T173: EntityManager ranks a living entity above its own corpse and
    # asks the ONE shared predicate; the TU is dependency-free by design.
    ${CMAKE_SOURCE_DIR}/src/client/corpse_predicate.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/group_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/pet_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/trade_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/world_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/combat_state.cpp
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
    # T173: EntityManager ranks a living entity above its own corpse and
    # asks the ONE shared predicate; the TU is dependency-free by design.
    ${CMAKE_SOURCE_DIR}/src/client/corpse_predicate.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/player_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/spell_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/inventory_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/group_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/pet_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/trade_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/world_state.cpp
    ${CMAKE_SOURCE_DIR}/src/client/state/combat_state.cpp
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
#
# T165: the cases are split across five .cpp files (core mechanics, then one
# per command family) sharing command_processor_test_fixture.h. They are all
# sources of THIS ONE target on purpose -- the 3000-line ceiling is a
# per-file rule, so splitting the file satisfies it without adding four more
# 80MB binaries and four more ctest discovery passes (T163's NFS cost).
ADD_EXECUTABLE(test_command_processor
    test_command_processor.cpp
    test_command_processor_migration.cpp
    test_command_processor_combat_guild.cpp
    test_command_processor_world.cpp
    test_command_processor_senders.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
# T140: cmdMove/cmdMoveTo/cmdFollow use fmt::format directly (T88's
# dual-frame /move echo, and the original /moveto and /follow ack wording),
# so this target now needs fmt linked -- previously it didn't use fmt at all.
TARGET_LINK_LIBRARIES(test_command_processor GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_command_processor)

# T153: /appearance parse + validation. Same dependency set as
# test_command_processor (real ActionDispatcher + CommandProcessor driven
# through the recording IActionHandler fake), in its own target because
# test_command_processor.cpp was past check_file_sizes.sh's 3000-line FAIL
# ceiling when T153 landed. See refactor/T153_consider_corpse_and_face.md.
# T165 split that file, so the ceiling is no longer the reason this target
# exists -- it stays separate only because nothing is gained by merging it.
ADD_EXECUTABLE(test_appearance_command
    test_appearance_command.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_appearance_command GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_appearance_command)

# T143: registry-aware /help. Merges registry 1's CommandRegistry (game,
# graphics/ui) and registry 2's CommandProcessor (action) catalogs -- needs
# both, plus the same ACTION_LAYER_STATE_SOURCES dependency set as
# test_command_processor.cpp, but no Irrlicht, no EverQuest, no Application.
ADD_EXECUTABLE(test_merged_help
    test_merged_help.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/action_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/client/action/command_processor.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/ui/command_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/client/help/merged_help.cpp
    ${CMAKE_SOURCE_DIR}/src/client/help/renderer_command_catalog.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
# T223: the registry-3 drift guard parses the renderer's if/else chain source
# text and asserts its `cmd == "/x"` literal set equals the static catalog.
TARGET_COMPILE_DEFINITIONS(test_merged_help PRIVATE
    RENDERER_COMMAND_SOURCE="${CMAKE_SOURCE_DIR}/src/client/graphics/irrlicht_renderer.cpp")
TARGET_LINK_LIBRARIES(test_merged_help GTest::gtest_main fmt::fmt)
GTEST_DISCOVER_TESTS(test_merged_help)

# T223: Application::dispatchToActionOrRenderer()'s registry-3 stage on a
# bare (never initialize()d) Application -- m_graphicsInitialized is false,
# exactly as in a --no-graphics run, so a known renderer command must be
# REFUSED (outcome=failed with the "requires graphics mode" message), never
# queued, and an unknown name must be unrecognized. Links willeq_core the
# same way test_video_capture_flags does.
ADD_EXECUTABLE(test_headless_command_refusal
    test_headless_command_refusal.cpp
)
TARGET_LINK_LIBRARIES(test_headless_command_refusal GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_headless_command_refusal PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_headless_command_refusal)

# T223 (T198): every configured EQE1 global archive must exist as a client
# .s3d (asset-gated skip), and the built-in fallback copy of that list must
# not carry the phantom gequip7.
ADD_EXECUTABLE(test_eqe1_archive_config
    test_eqe1_archive_config.cpp
)
TARGET_COMPILE_DEFINITIONS(test_eqe1_archive_config PRIVATE
    EQE1_ARCHIVES_JSON="${CMAKE_SOURCE_DIR}/config/eqe1_archives.json"
    RENDERER_EFFECTS_SOURCE="${CMAKE_SOURCE_DIR}/src/client/graphics/renderer_effects.cpp")
TARGET_LINK_LIBRARIES(test_eqe1_archive_config GTest::gtest_main ${JSONCPP_LIBRARIES})
GTEST_DISCOVER_TESTS(test_eqe1_archive_config)

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
    # T174: the bridge now reads HotbarModel to route keyboard hotbar presses
    # by slot content type. hotbar_model.cpp is dependency-light (event_bus +
    # an inline GameStateBridge::pushEvent, both already in the state set).
    ${CMAKE_SOURCE_DIR}/src/client/hotbar/hotbar_model.cpp
    ${ACTION_LAYER_STATE_SOURCES}
)
TARGET_LINK_LIBRARIES(test_input_action_bridge GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})  # T174: hotbar_model.cpp reads/writes jsoncpp
GTEST_DISCOVER_TESTS(test_input_action_bridge)

# T40/T227: GameStateSyncBridge tests -- since T227 the decorator projects
# nothing (one GameState, written at the source), so this covers only the
# forwarding hop: single-stage applyEvent() and the T220 stage/forward pair.
# Driven through a local recording fake standing in for
# IrrlichtBridge/ConsoleBridge, so no graphics or state deps needed.
ADD_EXECUTABLE(test_game_state_sync_bridge
    test_game_state_sync_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/client/bridge/game_state_sync_bridge.cpp
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
# T102 already removed assist/shield/duel's cases (migrated to
# action::CommandProcessor), leaving only /bandage on this seam. T135
# MIGRATES bandage too and deletes its eq_commands.cpp registration --
# test_eq_commands_assist.cpp is REMOVED (not repointed), same shape as
# T102's test_eq_commands_attack.cpp removal below; equivalent coverage now
# lives in test_command_processor.cpp's T135 section (FakeActionHandler-
# based, matching every other migrated family). See
# refactor/T135_migrate_utility_family.md.

# T136: vendor/vendorlist/buy/sell coverage -- test_eq_commands_vendor.cpp
# (T48, the old game-registry ProcessChatInput seam) DELETED and replaced by
# test_eq_action_handler_vendor.cpp: same MakeEq()/TestBridge/ReplayZone()-
# driven vendor-open setup (HC_OP_ShopRequest, HC_OP_ItemPacket) and
# bridge.drainIntents()/drainEvents() assertions, but calling the new real
# entry points directly (EverQuest::RequestVendorToggle/RequestVendorBuy/
# RequestVendorSell, EqActionHandler::vendorList()) since eq_commands.cpp's
# RegisterVendorCommands() no longer exists. Argument-parsing cases moved to
# test_command_processor.cpp's T136 section (RecordingActionHandler, no wire
# replay needed). See refactor/T136_migrate_merchant_family.md.
# test_eq_action_handler_raid_guards.cpp (T183): the raid command guards --
# not-in-a-raid, not-the-leader, not-a-member, bad loot type -- against a real
# EverQuest whose raid state is established through the production packet path
# (ReplayZone + HC_OP_RaidUpdate), never by poking a setter. Argument arity
# lives in action::CommandProcessor and is covered in test_command_processor.
# See refactor/T183_gate_raid_remove.md.
ADD_EXECUTABLE(test_eq_action_handler_raid_guards
    test_eq_action_handler_raid_guards.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_raid_guards GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_raid_guards PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_raid_guards)

ADD_EXECUTABLE(test_eq_action_handler_vendor
    test_eq_action_handler_vendor.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_vendor GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_vendor PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_vendor)

# T50: /bank, /train coverage (eq_commands_spells.cpp RegisterSpellCommands())
# -- same seam test_eq_commands_vendor.cpp (T48) used to use, before T136
# deleted it; see test_eq_action_handler_vendor.cpp above for the retargeted
# replacement. /bank asserts the pushed
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

# T140: final 16 of the game-registry migration programme (equip excluded).
# Real-EqActionHandler/EverQuest coverage for showLocation/warp/
# showOrSetZoning/campNow/debugLevel/perfReport/timeOfDay -- the net-new
# surface this unit added. sit/stand/move/moveto/follow/stopfollow REUSE
# pre-existing IActionHandler methods with no new state to exercise here
# (covered at the CommandProcessor layer, test_command_processor.cpp);
# bank/train stay in test_eq_commands_bank_train.cpp above (retargeted from
# ProcessChatInput to the new EverQuest::RequestBankToggle()/
# RequestTraining() entry points in this same unit). See
# refactor/T140_migrate_final_seventeen.md.
ADD_EXECUTABLE(test_eq_action_handler_movement_utility
    test_eq_action_handler_movement_utility.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_movement_utility GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_movement_utility PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_movement_utility)

# T66: /equip coverage (eq_commands.cpp RegisterEquipmentCommands()) -- same
# seam test_eq_commands_vendor.cpp (T48) used to use (see
# test_eq_action_handler_vendor.cpp above) and test_eq_commands_bank_train.cpp
# still uses. T136 (Merchant family migration) deliberately did NOT migrate
# /equip -- it stays registered in eq_commands.cpp/m_command_registry, so
# this file is unaffected; see refactor/T136_migrate_merchant_family.md.
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
# T144: also links world_object.cpp so the OP_GroundSpawn captured-bytes tests
# can drive the real parse (eqt::ParseGroundSpawn), not just sizeof().
ADD_EXECUTABLE(test_packet_structs
    test_packet_structs.cpp
    ${CMAKE_SOURCE_DIR}/src/client/world_object.cpp
)
TARGET_LINK_LIBRARIES(test_packet_structs GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_packet_structs)

# T145: wire-layout (offsetof) + sender wire-shape tests
ADD_EXECUTABLE(test_wire_layout_t145
    test_wire_layout_t145.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
)
TARGET_LINK_LIBRARIES(test_wire_layout_t145 GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_wire_layout_t145)

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
    GTEST_DISCOVER_TESTS(test_graphics)

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
# T164 (2026-08-21): registered with ctest ONLY under -DEQT_LIVE_TESTS=ON, and
# labelled "live" so it can never join the default suite. Discovery is safe --
# each main() only parses args and prints before RUN_ALL_TESTS(), so
# --gtest_list_tests returns without touching a server (verified: 4 / 7 / 8
# tests listed in <1s with no eqemu running). These binaries are still BUILT
# unconditionally; before T164 they were built on every unit and run by
# nothing.
#   cmake -S . -B build -DEQT_LIVE_TESTS=ON
#   ctest --test-dir build -L live
IF(EQT_LIVE_TESTS)
    GTEST_DISCOVER_TESTS(test_zoning_integration PROPERTIES LABELS "live")
ENDIF()

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

# T240: castSpell()/castSpellOnTarget()/memorizeSpell() against the real
# EverQuest + EQ::SpellManager + a real DaybreakConnection over
# FakeUdpTransport (T234 WireHarness idiom), gems loaded through the same
# setSpellGems() call the PlayerProfile handler makes, outbound
# OP_CastSpell / OP_MemorizeSpell bytes asserted. Replaces T93's
# test_eq_action_handler_transforms (its two tests observed the
# CombatManager gem map/IsCasting() that T240 deleted; the 1-based gem
# conversion they pinned is pinned here on the real store). DB-gated cases
# skip without data/willeq.db. No network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_cast
    test_eq_action_handler_cast.cpp
)
TARGET_COMPILE_DEFINITIONS(test_eq_action_handler_cast PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_eq_action_handler_cast GTest::gtest willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_cast PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_cast)

# T234: the five transforms T93 listed but could not reach (targetSelf,
# targetGroupMember, hailTarget, setMovementMode, setPositionState) pinned
# against the real EverQuest: entities/pet/group via replayed NewSpawn +
# GroupUpdate packets, and a real DaybreakConnection over FakeUdpTransport
# installed as m_zone_connection so the outbound TargetMouse/ChannelMessage
# bytes can be asserted. No network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_transforms_real
    test_eq_action_handler_transforms_real.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_transforms_real GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_transforms_real PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_transforms_real)

# T167: PlayerProfile appearance parse + the /appearance merge. Pins the seven
# PlayerProfile offsets (verified against eqemu's titanium_structs.h) and the
# merge that keeps an unnamed field from being reset to zero on the server.
# Same headless EverQuest + EqActionHandler idiom as the transforms tests
# above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_profile_appearance
    test_profile_appearance.cpp
)
TARGET_LINK_LIBRARIES(test_profile_appearance GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_profile_appearance PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_profile_appearance)

# T217: group-accept inviter name (the T99 aliasing bug) and the OP_GMHideMe
# sender body (the T148 misroute). Headless EverQuest + TestBridge + the T88
# send-guard bypass; the hideme body is read through a T167-style seam.
ADD_EXECUTABLE(test_group_accept_gm_hideme
    test_group_accept_gm_hideme.cpp
)
TARGET_LINK_LIBRARIES(test_group_accept_gm_hideme GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_group_accept_gm_hideme PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_group_accept_gm_hideme)

# T229: the two remaining T148 GM-sender defects -- SendGMSummon's zeroed
# coordinates (eqemu READS them) and ZoneProcessGMFind's int32 decode of a
# float reply. Pins both structs against eqemu's field order/types.
ADD_EXECUTABLE(test_gm_summon_find
    test_gm_summon_find.cpp
)
TARGET_LINK_LIBRARIES(test_gm_summon_find GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_gm_summon_find PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_gm_summon_find)

# T169: OP_RaidUpdate decoding -- the action codes against eqemu's
# common/raid.h and the per-action body shapes (actions 8, 10, 14 and 30 do
# NOT have `parameter` where the general body does). Links willeq_core for
# EQT::DecodeRaidUpdate + EQ::Net::Packet; no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_raid_update_decode
    test_raid_update_decode.cpp
)
TARGET_LINK_LIBRARIES(test_raid_update_decode GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_raid_update_decode PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_raid_update_decode)

# T177: OP_Bind_Wound reply outcome mapping. Pure function, no network/graphics;
# links only the single TU under test.
ADD_EXECUTABLE(test_bind_wound_outcome
    test_bind_wound_outcome.cpp
    ${CMAKE_SOURCE_DIR}/src/client/bind_wound_outcome.cpp
)
TARGET_LINK_LIBRARIES(test_bind_wound_outcome GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_bind_wound_outcome)

# T178: OP_WhoAllResponse %1 count substitution. Pure function, no deps.
ADD_EXECUTABLE(test_who_format
    test_who_format.cpp
    ${CMAKE_SOURCE_DIR}/src/client/who_format.cpp
)
TARGET_LINK_LIBRARIES(test_who_format GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_who_format)

# T224: OP_WhoAllResponse entry parsing/formatting (pure, who_response.cpp)
# plus the handler end-to-end through the ReplayZonePacket seam, observed as
# SystemMessage bridge events (same idiom as test_zone_chat.cpp).
ADD_EXECUTABLE(test_who_response
    test_who_response.cpp
)
TARGET_LINK_LIBRARIES(test_who_response GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_who_response PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_who_response)

# T170: OP_InspectAnswer generation -- the field inversion between the request
# and the answer (TargetID is the requester in an answer, the inspected player
# in a request) and the sender itself through a real headless EverQuest.
ADD_EXECUTABLE(test_inspect_answer
    test_inspect_answer.cpp
)
TARGET_LINK_LIBRARIES(test_inspect_answer GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_inspect_answer PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_inspect_answer)

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
GTEST_DISCOVER_TESTS(test_eq_action_handler_spell_pet)

# T221 (Option A stage 2): every READ-model domain the GameStateSyncBridge
# projection was the sole writer of (pet, spells, entity position/appearance,
# combat target/auto-attack, inventory occupancy) is now written at the source
# (EverQuest::m_game_state) by the owning handler. Each case runs with and
# without a bridge attached (the no-bridge leg proves the writes precede the
# bridge guards). Headless EverQuest + ReplayZonePacket seam, same idiom as
# test_combat_manager / test_eq_action_handler_spell_pet (DB-dependent cast
# case follows the hasDb() + GTEST_SKIP precedent). Own main() registers the
# global willeq.db, so gtest_main is NOT linked.
ADD_EXECUTABLE(test_game_state_source_writes
    test_game_state_source_writes.cpp
)
TARGET_COMPILE_DEFINITIONS(test_game_state_source_writes PRIVATE WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
TARGET_LINK_LIBRARIES(test_game_state_source_writes GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_game_state_source_writes PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_game_state_source_writes)

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
    # T164 (2026-08-21): registered with ctest ONLY under -DEQT_LIVE_TESTS=ON, and
    # labelled "live" so it can never join the default suite. Discovery is safe --
    # each main() only parses args and prints before RUN_ALL_TESTS(), so
    # --gtest_list_tests returns without touching a server (verified: 4 / 7 / 8
    # tests listed in <1s with no eqemu running). These binaries are still BUILT
    # unconditionally; before T164 they were built on every unit and run by
    # nothing.
    #   cmake -S . -B build -DEQT_LIVE_TESTS=ON
    #   ctest --test-dir build -L live
    IF(EQT_LIVE_TESTS)
        GTEST_DISCOVER_TESTS(test_zoning_graphics_integration PROPERTIES LABELS "live")
    ENDIF()
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

# T156: /useskill + the hotbar/skills-popup intent producers. Same graphics
# gate as test_static_ui_panels above (it drives StaticUIInput), and no device
# is created — see refactor/T156_skill_reachability.md. Deliberately a NEW
# file: test_command_processor.cpp was past the 3000-line FAIL ceiling when
# T156 landed (T165 has since split it).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_skill_reachability
        test_skill_reachability.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_skill_reachability PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_skill_reachability GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_skill_reachability)
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

# T126: the dominant UAF holder named by this unit's write-time
# return-address probe -- EntityVisual::primaryEquipNode/secondaryEquipNode
# (weapon/shield meshes) were never scanned by EntityMeshTextureGuard despite
# T123's holder-inventory table claiming "equipment" was covered, AND
# EquipmentModelLoader's own persistent meshCache_ was never registered as a
# TextureEvictionListener at all. See refactor/T126_uaf_attribution.md.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_t126_equip_texture_guard
        test_t126_equip_texture_guard.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_t126_equip_texture_guard PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_t126_equip_texture_guard GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_t126_equip_texture_guard)
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

# T233: registry-3 (renderer) command replies must echo to the console log
# as well as the chat panel. Same bare-IrrlichtRenderer idiom as
# test_irrlicht_camera_command above, plus the T31 stdout-capture idiom
# from test_logging.cpp to observe the logging drain thread.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_renderer_command_echo
        test_renderer_command_echo.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_renderer_command_echo PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_renderer_command_echo GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_renderer_command_echo)
ENDIF()

# T235: spell-gem hotkeys reach the game thread -- drives the real
# RendererEventReceiver + IrrlichtRenderer::processChatInput() (bare, no
# device, friend access) and asserts GraphicsInputHandler still sees the
# request. No device needed -- same reasoning as test_renderer_command_echo.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_spell_gem_key_race
        test_spell_gem_key_race.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_spell_gem_key_race PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_spell_gem_key_race GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_spell_gem_key_race)
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
    # T164 (2026-08-21): registered with ctest ONLY under -DEQT_LIVE_TESTS=ON, and
    # labelled "live" so it can never join the default suite. Discovery is safe --
    # each main() only parses args and prints before RUN_ALL_TESTS(), so
    # --gtest_list_tests returns without touching a server (verified: 4 / 7 / 8
    # tests listed in <1s with no eqemu running). These binaries are still BUILT
    # unconditionally; before T164 they were built on every unit and run by
    # nothing.
    #   cmake -S . -B build -DEQT_LIVE_TESTS=ON
    #   ctest --test-dir build -L live
    IF(EQT_LIVE_TESTS)
        GTEST_DISCOVER_TESTS(test_beneficial_spell_casting PROPERTIES LABELS "live")
    ENDIF()
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

# T168: the zero-caller-sender gate. Fails if any EverQuest::Send*/Request*
# method has no production call site -- the condition that held for 28 of 97
# senders when the arc opened, and that roughly one in two turned out to be
# broken on the wire. Pure source scan: no build artefact, no server, no
# display, so it runs in every configuration. See
# refactor/T168_sender_triage.md and the script's own docstring for what it
# deliberately does NOT catch.
ADD_TEST(NAME check_sender_reachability
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/check_sender_reachability.py)

# T175: the stale-binary gate. Fails if any executable in build/bin is produced
# by no current CMake target -- the condition four deleted-source test binaries
# held on 2026-08-14, still running and still passing a week after their targets
# and sources were removed (cmake never prunes bin/). Asks the CMake file API
# (codemodel-v2) which artifacts the configured targets claim under bin/, and
# flags any file that is not one. Pure: no server, no display, no network. It
# inspects the build tree it is run against, so it SKIPs (125) when there is no
# configured tree -- the CMAKE_BINARY_DIR here IS such a tree at ctest time. See
# refactor/T175_build_tree_hygiene.md and the script's docstring.
ADD_TEST(NAME check_build_tree_orphans
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/ci/check_build_tree_orphans.py
            ${CMAKE_BINARY_DIR})
SET_TESTS_PROPERTIES(check_build_tree_orphans PROPERTIES SKIP_RETURN_CODE 125)

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

# T147: captured-bytes harness (tests/captured_packets.h) -- pins packet structs
# to real observed wire bytes rather than to sizeof(). See the header's docstring.
ADD_EXECUTABLE(test_captured_packets test_captured_packets.cpp)
TARGET_LINK_LIBRARIES(test_captured_packets GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_captured_packets)

# T149: real-EverQuest/EqActionHandler coverage for the four tradeskill recipe
# commands -- specifically that object_type is resolved from the OPEN
# container (T144's WorldObject::object_type + the drop_id
# ZoneProcessClickObjectAction records) and never supplied by the caller.
# Drives T144's real captured OP_GroundSpawn bytes through the production
# replay seam. Same headless EverQuest + EqActionHandler idiom as
# test_eq_action_handler_trade above -- no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_eq_action_handler_tradeskill
    test_eq_action_handler_tradeskill.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_tradeskill GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_tradeskill PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_tradeskill)

# T230: keyboard hotbar Item route below the action layer --
# EqActionHandler::findItemSlotByItemId (the id->slot hop) and
# EqActionHandler::useItem -> EverQuest::UseInventoryItem (the one click-
# effect sender, shared with the mouse path). Same headless EverQuest +
# RecordingBridge idiom as test_eq_action_handler_tradeskill above.
ADD_EXECUTABLE(test_eq_action_handler_hotbar_item
    test_eq_action_handler_hotbar_item.cpp
)
TARGET_LINK_LIBRARIES(test_eq_action_handler_hotbar_item GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_eq_action_handler_hotbar_item PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_eq_action_handler_hotbar_item)

# T152: the two skill-opcode bodies this client had to build from scratch
# (OP_Disarm, OP_PickPocket), pinned field-by-field at named offsets against
# eqemu's Disarm_Struct / PickPocket_Struct. Header-only -- no willeq_core, no
# network, no graphics. NOTE the authority is eqemu SOURCE, not captured wire
# bytes; see the file's docstring and T152's Review.
ADD_EXECUTABLE(test_skill_packet_bodies test_skill_packet_bodies.cpp)
TARGET_LINK_LIBRARIES(test_skill_packet_bodies GTest::gtest_main)
TARGET_INCLUDE_DIRECTORIES(test_skill_packet_bodies PRIVATE ${CMAKE_SOURCE_DIR}/include)
GTEST_DISCOVER_TESTS(test_skill_packet_bodies)

# T159: the skill possession gate and the real per-class cap table behind it.
# Loads the SHIPPED config/skill_caps.json by path (WILLEQ_REPO_ROOT) -- the
# gate's data must be the file the client ships, not a fixture. Same headless
# EverQuest seam as test_combat_manager.cpp (MakeEq() + live CombatManager /
# SkillManager + TestBridge); no network, no graphics device, no eqemu.
# New file: test_command_processor.cpp is past the 3000-line FAIL ceiling.
ADD_EXECUTABLE(test_skill_possession_gate
    test_skill_possession_gate.cpp
)
TARGET_LINK_LIBRARIES(test_skill_possession_gate GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_skill_possession_gate PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_skill_possession_gate)

# T162: the commands that emitted a skill opcode without passing T159's gate
ADD_EXECUTABLE(test_skill_gate_bypasses
    test_skill_gate_bypasses.cpp
)
TARGET_LINK_LIBRARIES(test_skill_gate_bypasses GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_skill_gate_bypasses PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_skill_gate_bypasses)
