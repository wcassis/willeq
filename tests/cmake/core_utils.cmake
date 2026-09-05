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
GTEST_DISCOVER_TESTS(test_application_init_order)

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
GTEST_DISCOVER_TESTS(test_frame_render_order)

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
GTEST_DISCOVER_TESTS(test_movement_order)

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
GTEST_DISCOVER_TESTS(test_data_paths)

# T399 (board B235): the census as a test. A source reader -- it opens the
# client sources at WILLEQ_REPO_ROOT and fails on any shipped-data literal that
# does not go through eqt::ResolveDataPath(). No link deps on purpose: most of
# the routed sites end in std::exit(1) on a miss and cannot be run in-process.
ADD_EXECUTABLE(test_data_path_routing
    test_data_path_routing.cpp
)
TARGET_LINK_LIBRARIES(test_data_path_routing GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_data_path_routing)
