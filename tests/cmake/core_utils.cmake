# tests/cmake/core_utils.cmake -- included from tests/CMakeLists.txt.
# Core utility + primitive tests (strings, compression, CRC32, packet, position, random, logging, DRM mode select).
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Test utility functions - strings needs fmt
ADD_EXECUTABLE(test_strings
    test_strings.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/strings.cpp
)
TARGET_LINK_LIBRARIES(test_strings GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_strings DISCOVERY_TIMEOUT 120)

# Test compression
ADD_EXECUTABLE(test_compression
    test_compression.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_compression GTest::gtest_main ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_compression DISCOVERY_TIMEOUT 120)

# Test CRC32
ADD_EXECUTABLE(test_crc32
    test_crc32.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
)
TARGET_LINK_LIBRARIES(test_crc32 GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_crc32 DISCOVERY_TIMEOUT 120)

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
GTEST_DISCOVER_TESTS(test_drm_mode_select DISCOVERY_TIMEOUT 120)

# Test packet serialization - packet.cpp uses fmt
ADD_EXECUTABLE(test_packet
    test_packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
)
TARGET_LINK_LIBRARIES(test_packet GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_packet DISCOVERY_TIMEOUT 120)

# Test position utilities
ADD_EXECUTABLE(test_position
    test_position.cpp
    ${CMAKE_SOURCE_DIR}/src/client/position.cpp
)
TARGET_LINK_LIBRARIES(test_position GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_position DISCOVERY_TIMEOUT 120)

# Test data verification
ADD_EXECUTABLE(test_data_verification
    test_data_verification.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/data_verification.cpp
)
TARGET_LINK_LIBRARIES(test_data_verification GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_data_verification DISCOVERY_TIMEOUT 120)

# Test UDP transport
ADD_EXECUTABLE(test_udp_transport
    test_udp_transport.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/posix_udp_transport.cpp
)
TARGET_LINK_LIBRARIES(test_udp_transport GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_udp_transport DISCOVERY_TIMEOUT 120)

# Test random
ADD_EXECUTABLE(test_random
    test_random.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/random.cpp
)
TARGET_LINK_LIBRARIES(test_random GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_random DISCOVERY_TIMEOUT 120)

# T410 (board B138/B139): the scenario lane's state machines.
# Links the scenario TUs directly -- NOT willeq_core -- because the whole
# point of IScenarioWorld is that a scenario needs no client, no network and
# no database to be tested. If this target ever needs willeq_core, the
# interface has grown a dependency it should not have.
ADD_EXECUTABLE(test_scenario_state_machines
    test_scenario_state_machines.cpp
    ${CMAKE_SOURCE_DIR}/src/client/scenario/scenario_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/client/scenario/scenario_runner.cpp
    ${CMAKE_SOURCE_DIR}/src/client/scenario/scenario_zone_in.cpp
    ${CMAKE_SOURCE_DIR}/src/client/scenario/scenario_two_client_survive.cpp
    ${CMAKE_SOURCE_DIR}/src/client/scenario/scenario_cast_lands.cpp
    ${CMAKE_SOURCE_DIR}/src/client/scenario/scenario_equip_observed.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_scenario_state_machines PRIVATE ${CMAKE_SOURCE_DIR}/include)
TARGET_LINK_LIBRARIES(test_scenario_state_machines GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_scenario_state_machines DISCOVERY_TIMEOUT 120)

# Test logging infrastructure
ADD_EXECUTABLE(test_logging
    test_logging.cpp
)
TARGET_LINK_LIBRARIES(test_logging GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_logging DISCOVERY_TIMEOUT 120)

# G04: the startup-ORDER net for Application::initialize().
# Pure text parse of src/client/application.cpp (WILLEQ_REPO_ROOT, set
# globally in tests/CMakeLists.txt) against
# tests/goldens/application_init_order_g04.txt -- initialize() brings up the
# whole client and cannot be run in a unit test, so the TU is the only record
# of the order, and the order is load-bearing (client before every holder of
# its GameState; renderer/bridge before InitGraphics; console bridge only if
# graphics did not attach one; login connection last). No link deps.
ADD_EXECUTABLE(test_application_init_order
    test_application_init_order.cpp
)
TARGET_LINK_LIBRARIES(test_application_init_order GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_application_init_order DISCOVERY_TIMEOUT 120)

# G05: the render-PASS-ORDER net for FrameComponent::processFrameRender().
# Pure text parse of src/client/graphics/frame/renderer_frame_render.cpp
# (WILLEQ_REPO_ROOT, set globally in tests/CMakeLists.txt) against
# tests/goldens/frame_render_order_g05.txt -- processFrameRender() needs a live
# device, a GL context and a loaded zone, so it cannot be run in a unit test and
# the TU is the only record of the draw order. That order is load-bearing
# (depth/blending state, overlays on top of the geometry they annotate, UI on
# top of that, the swap last). No link deps, no graphics guard: it reads the
# file as text.
ADD_EXECUTABLE(test_frame_render_order
    test_frame_render_order.cpp
)
TARGET_LINK_LIBRARIES(test_frame_render_order GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_frame_render_order DISCOVERY_TIMEOUT 120)

# T457 (board B482): the LIFECYCLE-ORDER net for the inventory paperdoll across
# a zone change. Same idiom as G05 above -- a pure text parse, of
# src/client/graphics/zone_lifecycle/renderer_zone_lifecycle.cpp,
# src/client/graphics/overlays/overlays_hud_env.cpp and
# src/client/graphics/ui/character_model_view.cpp -- because unloadZone()
# cannot be run in a unit test (live device, GL context, loaded zone,
# simulation and entity-prep workers) and the order is the fix: the paperdoll's
# model is released BEFORE RaceModelLoader's mesh caches and the constrained
# texture cache destroy what its materials name. The doll lives in its own
# scene manager, so ConstrainedTextureCache::clearTextureReferences()'s scan
# from the MAIN root cannot reach it. No link deps, no graphics guard.
ADD_EXECUTABLE(test_paperdoll_zone_unload_order
    test_paperdoll_zone_unload_order.cpp
)
TARGET_LINK_LIBRARIES(test_paperdoll_zone_unload_order GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_paperdoll_zone_unload_order DISCOVERY_TIMEOUT 120)

# G06: the same idiom for the player's per-frame movement order. A pure text
# parse of src/client/graphics/player/renderer_player_physics.cpp against
# tests/goldens/movement_order_g06.txt (extracted from the PRE-G06 body), plus
# include/client/graphics/player/player_camera_component.h for the
# MovementFrame field list. updatePlayerMovement() cannot be driven in a unit
# test -- it needs a live device, an event receiver, zone collision geometry, an
# entity renderer and a bridge -- so the TU is the record of the order. No link
# deps, no graphics guard.
ADD_EXECUTABLE(test_movement_order
    test_movement_order.cpp
)
TARGET_LINK_LIBRARIES(test_movement_order GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_movement_order DISCOVERY_TIMEOUT 120)

# T461: the official camera set, the orbit rig's geometry and /camera's
# grammar -- device-free, so it compiles the two TUs directly and needs no
# graphics guard and no willeq_core.
ADD_EXECUTABLE(test_camera_modes
    test_camera_modes.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/player/camera_rig.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/slash/camera_command.cpp
)
TARGET_INCLUDE_DIRECTORIES(test_camera_modes PRIVATE ${CMAKE_SOURCE_DIR}/include)
TARGET_LINK_LIBRARIES(test_camera_modes GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_camera_modes DISCOVERY_TIMEOUT 120)

# T397 Leg A (board B221): eqt::ResolveDataPath() plus the consumer that made
# it necessary -- SpellEffectsConfig::load("config/spell_effects.json"), whose
# CWD-relative literal silently produced a DIFFERENT spell effect from any
# working directory but the repo root. The consumer half needs the Irrlicht
# headers when graphics are on (spell_particle_types.h reaches EmitterConfig);
# the resolver half is guard-free.
ADD_EXECUTABLE(test_data_paths
    test_data_paths.cpp
)
TARGET_LINK_LIBRARIES(test_data_paths GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_data_paths PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_data_paths DISCOVERY_TIMEOUT 120)

# T399 (board B235): the census as a test. A source reader -- it opens the
# client sources at WILLEQ_REPO_ROOT and fails on any shipped-data literal that
# does not go through eqt::ResolveDataPath(). No link deps on purpose: most of
# the routed sites end in std::exit(1) on a miss and cannot be run in-process.
ADD_EXECUTABLE(test_data_path_routing
    test_data_path_routing.cpp
)
TARGET_LINK_LIBRARIES(test_data_path_routing GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_data_path_routing DISCOVERY_TIMEOUT 120)

# T477 (board B571): the /tmp census. A source reader over tests/
# (WILLEQ_REPO_ROOT, set globally in tests/CMakeLists.txt) that fails on a temp
# path with no per-process component -- the name every slot and every
# concurrent ctest case would share. No link deps on purpose.
ADD_EXECUTABLE(test_tmp_path_census
    test_tmp_path_census.cpp
)
TARGET_LINK_LIBRARIES(test_tmp_path_census GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_tmp_path_census DISCOVERY_TIMEOUT 120)

# T414 Leg A (board B297): the KEY-level companion to
# DataPathRouting.EveryShippedConfigFileIsNamedByCode. It walks every key in
# every shipped config/*.json and fails on one no source names, and it pins the
# display_settings.json wiring (eight of its keys had a live consumer and no
# parse line). Links the extracted parser TU so the wiring cases need no
# renderer, no device and no GL context.
ADD_EXECUTABLE(test_config_key_reachability
    test_config_key_reachability.cpp
)
TARGET_LINK_LIBRARIES(test_config_key_reachability GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_config_key_reachability PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_config_key_reachability DISCOVERY_TIMEOUT 120)

# T491 (2026-09-14): the allocator-level memory ledger
# (include/client/core/memory_ledger.h). Links the ledger TU directly -- it
# REPLACES malloc/free/operator new for this whole binary, gtest included --
# and exercises the hooks through the public allocation entry points.
ADD_EXECUTABLE(test_memory_ledger
    test_memory_ledger.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger.cpp
)
TARGET_LINK_LIBRARIES(test_memory_ledger GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES} Threads::Threads)
# B839 (issue #94): the startup sub-owner acceptance case SPAWNS the real
# client (headless, TEST-NET-1, same idiom as test_headless_shutdown) and
# reads its own [memledger-startup] table, so it needs the binary's path and
# a build-order dependency on it.
IF(TARGET willeq)
    TARGET_COMPILE_DEFINITIONS(test_memory_ledger PRIVATE
        WILLEQ_BIN_PATH="$<TARGET_FILE:willeq>")
    ADD_DEPENDENCIES(test_memory_ledger willeq)
ENDIF()
GTEST_DISCOVER_TESTS(test_memory_ledger DISCOVERY_TIMEOUT 120)
