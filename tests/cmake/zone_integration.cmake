# tests/cmake/zone_integration.cmake -- included from tests/CMakeLists.txt.
# Zoning, session replay, position/heading conventions and the headless shutdown path.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

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
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_material.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_skeleton.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_light.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry_atlased.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/frustum_culler.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/software_occlusion_culler.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_upload_queue.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_eviction.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_compressed.cpp
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

# T332 (B049): the same zone-in z conversion WITH A RENDERER PRESENT -- the arm
# test_position_frame_roundtrip structurally cannot cover, because headless
# there is no renderer echo and the defect is invisible (T331 measured it live).
# Bare, never-Init()'d IrrlichtRenderer, so it needs graphics but no device and
# no GL context; same idiom as test_player_camera_component.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_in_z_conversion
        test_zone_in_z_conversion.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_in_z_conversion PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_in_z_conversion GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_in_z_conversion)
ENDIF()

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

# T322: pins the zone-transition connection lifecycle -- the graceful
# source-zone hand-off (OP_SaveOnZoneReq + OP_DeleteSpawn, server closes the
# session) that keeps eqemu from treating every zone change as a linkdead
# disconnect and disbanding the group, plus the OP_LogoutReply routing that
# must keep a real logout disconnecting. Headless: no network, no graphics.
ADD_EXECUTABLE(test_zone_out_handshake
    test_zone_out_handshake.cpp
)
TARGET_LINK_LIBRARIES(test_zone_out_handshake GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_out_handshake PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_out_handshake)

# T344 (BOARD B001): the split of MovementComponent::SetPositionState into a
# user-initiated setter (state + OP_SpawnAppearance) and a server-forced one
# (ApplyPositionState, state only), plus the deletion of the AT_DIE echo that
# T311 Leg A measured never appearing in either direction of a real
# official-client session. Asserts on the real outbound bytes via a
# DaybreakConnection over FakeUdpTransport -- SendSpawnAppearance bypasses
# PacketSender, so the T268 capture seam cannot see it. No network, no
# graphics, no eqemu.
ADD_EXECUTABLE(test_forced_pose_no_echo
    test_forced_pose_no_echo.cpp
)
TARGET_LINK_LIBRARIES(test_forced_pose_no_echo GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_forced_pose_no_echo PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_forced_pose_no_echo)
