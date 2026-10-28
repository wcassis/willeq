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
        "GLES2_ZONE_BENCHMARK_BIN=WILLEQ_BIN_PATH_OF(\"$<TARGET_FILE_NAME:gles2_zone_benchmark>\")")
    TARGET_LINK_LIBRARIES(test_entity_viewer_regression GTest::gtest_main)
    ADD_DEPENDENCIES(test_entity_viewer_regression gles2_zone_benchmark)
    GTEST_DISCOVER_TESTS(test_entity_viewer_regression DISCOVERY_TIMEOUT 120)

    # B997 (GitLab #317) step 2: the DETERMINISTIC door pixel A/B for the
    # per-material scene-graph batch (EQT_SG_BATCH) -- the gate for shipping
    # it ON. Same shellout-via-popen() pattern as
    # test_entity_viewer_regression above: two tool runs (one leg per
    # process, EQT_SG_BATCH=0 then =1) through --sg-batch-ab, then a
    # byte-comparison of the two PNGs (tests/test_sg_batch_door_pixel_ab.cpp
    # and tools/hwcaps/zone_bench_door_batch_ab.h for the design).
    # Asset-gated the same way (EQ_CLIENT_PATH / data/eq_client symlink,
    # else skip) + the binary-missing guard. The door fixtures are committed
    # under refactor/B997/data/ (the eqemu `doors` table rows, see the
    # README's "How the doors were sourced").
    ADD_EXECUTABLE(test_sg_batch_door_pixel_ab
        test_sg_batch_door_pixel_ab.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_sg_batch_door_pixel_ab PRIVATE
        "GLES2_ZONE_BENCHMARK_BIN=WILLEQ_BIN_PATH_OF(\"$<TARGET_FILE_NAME:gles2_zone_benchmark>\")"
        "WILLEQ_SRC=WILLEQ_REPO_PATH(\"\")")
    TARGET_LINK_LIBRARIES(test_sg_batch_door_pixel_ab GTest::gtest_main)
    ADD_DEPENDENCIES(test_sg_batch_door_pixel_ab gles2_zone_benchmark)
    GTEST_DISCOVER_TESTS(test_sg_batch_door_pixel_ab DISCOVERY_TIMEOUT 120)
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
    "WILLEQ_BIN_PATH=WILLEQ_BIN_PATH_OF(\"$<TARGET_FILE_NAME:willeq>\")")
TARGET_LINK_LIBRARIES(test_headless_shutdown GTest::gtest_main)
ADD_DEPENDENCIES(test_headless_shutdown willeq)
GTEST_DISCOVER_TESTS(test_headless_shutdown DISCOVERY_TIMEOUT 120)

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
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger.cpp  # T504c: s3d_loader.cpp reads eqt::memory::stats()
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_b924.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_trace.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/gpu_buffer_census.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_objects.cpp
        # B734: loadObjects() poses skinned objects through T476's resolver.
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_actor_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_characters.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader_skinning.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager_create.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager_rebuild.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_collision.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_motion.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager_collision.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager_animation.cpp
        # B997 (GitLab #317): door_manager.cpp drives the scene-graph door batch.
        ${CMAKE_SOURCE_DIR}/src/client/graphics/scene_graph_batch.cpp
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/frustum_culler.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/software_occlusion_culler.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_upload_queue.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_eviction.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_compressed.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache_residency.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/archive_texture_source.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/texture_upload_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eqe1_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/background_thread_pool.cpp
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
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
    GTEST_DISCOVER_TESTS(test_door_loading DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_zoning_integration PROPERTIES LABELS "live" DISCOVERY_TIMEOUT 120)
ENDIF()

# B925 (issue #244/#258): the repeatable `live` zone-link check. Drives the
# shipped willeq binary in the `--scenario zone_link_hold` lane against a real
# eqemu stack and takes the process exit code as the verdict (0 = held the
# link for the hold, 2 = dropped and never recovered, 3 = never zoned in).
# GTEST_SKIPs (not red) when no stack answers the config's login port.
ADD_EXECUTABLE(test_zone_link_hold
    test_zone_link_hold.cpp
)
TARGET_LINK_LIBRARIES(test_zone_link_hold GTest::gtest_main ${JSONCPP_LIBRARIES})
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_link_hold PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
IF(EQT_LIVE_TESTS)
    GTEST_DISCOVER_TESTS(test_zone_link_hold PROPERTIES LABELS "live" DISCOVERY_TIMEOUT 120)
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
GTEST_DISCOVER_TESTS(test_session_replay DISCOVERY_TIMEOUT 120)

# T04: zone connection-flow sequencing tests (CheckZoneRequestPhaseComplete
# gating, dispatcher robustness, observable post-handshake state). Same
# replay-seam idiom as test_session_replay above -- no network, no graphics,
# no live connection -- so safe to auto-discover.
# T446: split by functional group (the handshake gating + dispatcher
# robustness, the full-state snapshot, and the identity/region fields a zone-in
# resolves). ONE target, three sources (T165's test_command_processor
# precedent), so every case keeps its suite and its name and ctest sees exactly
# the same 35 tests. Shared packet builders + the RecordingBridge fake:
# tests/zone_connection_flow_test_fixture.h.
ADD_EXECUTABLE(test_zone_connection_flow
    test_zone_connection_flow_handshake.cpp
    test_zone_connection_flow_snapshot.cpp
    test_zone_connection_flow_identity.cpp
)
TARGET_LINK_LIBRARIES(test_zone_connection_flow GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_connection_flow PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_connection_flow DISCOVERY_TIMEOUT 120)

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
GTEST_DISCOVER_TESTS(test_zone_position_convention DISCOVERY_TIMEOUT 120)

# T426 Leg A (board B399): the self OP_ClientUpdate edge -- a server-forced
# relocation of our own character -- must reach the camera in the client's
# internal heading convention while the entity row keeps the raw server one.
# Same replay seam as test_zone_position_convention above.
ADD_EXECUTABLE(test_self_relocation_heading
    test_self_relocation_heading.cpp
)
TARGET_LINK_LIBRARIES(test_self_relocation_heading GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_self_relocation_heading PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_self_relocation_heading DISCOVERY_TIMEOUT 120)

# T460 (board B521): PlayerMovedData::heading on the renderer bridge is the EQ
# 0..512 unit for every publisher (UpdateMovement and /warp sent degrees).
ADD_EXECUTABLE(test_player_moved_heading_units
    test_player_moved_heading_units.cpp
)
TARGET_LINK_LIBRARIES(test_player_moved_heading_units GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_player_moved_heading_units PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
TARGET_COMPILE_DEFINITIONS(test_player_moved_heading_units PRIVATE
    "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")")
GTEST_DISCOVER_TESTS(test_player_moved_heading_units DISCOVERY_TIMEOUT 120)

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
GTEST_DISCOVER_TESTS(test_position_frame_roundtrip DISCOVERY_TIMEOUT 120)

# B1172 (#491) fix leg: our own size is ONE number -- the client's resolved
# size (size word, or the race/gender default written back) -- in the entity
# record (actor scale) and PlayerState (wire-z offset), from OP_PlayerProfile
# and the server's OP_ZoneEntry Spawn_Struct.
ADD_EXECUTABLE(test_self_actor_size
    test_self_actor_size.cpp
)
TARGET_LINK_LIBRARIES(test_self_actor_size GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_self_actor_size PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_self_actor_size DISCOVERY_TIMEOUT 120)

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
    GTEST_DISCOVER_TESTS(test_zone_in_z_conversion DISCOVERY_TIMEOUT 120)
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
GTEST_DISCOVER_TESTS(test_setheading_conversion DISCOVERY_TIMEOUT 120)

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
GTEST_DISCOVER_TESTS(test_zone_out_handshake DISCOVERY_TIMEOUT 120)

# B1005 (GitLab #324): /camp -> Character Select, /camp desktop -> exit. The
# camp state machine (timing, cancels, destinations), the eqstr lines against
# the shipped table, the packet bytes and the Character Select re-entry on the
# server's OP_LogoutReply through the real EverQuest + CharacterSelectService,
# and the camp_to_charselect / camp_desktop scenarios over a fake world.
ADD_EXECUTABLE(test_camp
    test_camp.cpp
)
TARGET_LINK_LIBRARIES(test_camp GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_camp PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
TARGET_COMPILE_DEFINITIONS(test_camp PRIVATE
    "WILLEQ_SOURCE_DIR=WILLEQ_REPO_PATH(\"\")"
    "WILLEQ_DB_REAL_PATH=WILLEQ_REPO_PATH(\"data/willeq.db\")")
GTEST_DISCOVER_TESTS(test_camp DISCOVERY_TIMEOUT 120)

# B1072 (GitLab #392): the zone-IN connect bound -- ZoneConnectWatchdog on a
# test clock, and a headless EverQuest whose ZoneServerInfo names a dead port:
# timeout -> one retry of the same host:port -> a fresh world login at
# character select (never a hang at 20 %, never an exit).
ADD_EXECUTABLE(test_zone_connect_timeout
    test_zone_connect_timeout.cpp
)
TARGET_LINK_LIBRARIES(test_zone_connect_timeout GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_connect_timeout PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_connect_timeout DISCOVERY_TIMEOUT 120)

# B1099 (GitLab #419): the PRE-ZONE connect bound -- LoginConnectWatchdog on a
# test clock, and a headless EverQuest whose login host is a dead port:
# timeout -> the session torn down -> "Could not connect to login server" on
# the loading screen -> the could-not-connect record (exit 3).
ADD_EXECUTABLE(test_login_connect_timeout
    test_login_connect_timeout.cpp
)
TARGET_LINK_LIBRARIES(test_login_connect_timeout GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_login_connect_timeout PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_login_connect_timeout DISCOVERY_TIMEOUT 120)

# B1081 (GitLab #401): no character-select flash on a zone change.
# IsZoningReconnect() over the real client (fresh login / zone-change
# reconnect / camp return / B1072 give-up), the Activations() count over the
# real CharacterSelectService, and the zone_change_no_flash scenario over a
# fake world.
ADD_EXECUTABLE(test_zone_change_no_flash
    test_zone_change_no_flash.cpp
)
TARGET_LINK_LIBRARIES(test_zone_change_no_flash GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_change_no_flash PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_change_no_flash DISCOVERY_TIMEOUT 120)

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
GTEST_DISCOVER_TESTS(test_forced_pose_no_echo DISCOVERY_TIMEOUT 120)

# T374 Leg B (BOARD B137): the zone opcode table names its keys with HC_OP_*
# enumerators, never bare hex. A source reader over zone_dispatch.cpp plus two
# runtime checks; no network, no graphics, no eqemu.
ADD_EXECUTABLE(test_zone_opcode_literals
    test_zone_opcode_literals.cpp
)
TARGET_LINK_LIBRARIES(test_zone_opcode_literals GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_zone_opcode_literals PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_zone_opcode_literals DISCOVERY_TIMEOUT 120)

# T346 (BOARD B001 remainder): the self-death sequence matches the official
# client. Replays the T311 Leg A capture's OP_Death + OP_ZonePlayerToBind
# bytes verbatim and compares fable's outbound opcode sequence to a golden
# extracted from the capture's opcode timeline
# (scripts/refactor/t311_death_golden.py). Same DaybreakConnection-over-
# FakeUdpTransport seam as test_forced_pose_no_echo: SendSpawnAppearance and
# PacketSender::SafeQueueZone both bottom out there, so it is the one place
# that sees the whole sequence in order. No network, no graphics, no eqemu.
ADD_EXECUTABLE(test_self_death_sequence
    test_self_death_sequence.cpp
)
TARGET_LINK_LIBRARIES(test_self_death_sequence GTest::gtest_main willeq_core)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    TARGET_INCLUDE_DIRECTORIES(test_self_death_sequence PRIVATE ${IRRLICHT_INCLUDE_DIR})
ENDIF()
GTEST_DISCOVER_TESTS(test_self_death_sequence DISCOVERY_TIMEOUT 120)

# B917 (issue #236) + B918 (issue #237): a server-directed zone change that
# lands DURING a zone load, and the exit code of a run that dies on a loader
# FATAL. Drives eqt::ZoneLoadHandoff (include/client/zone_load_handoff.h) in
# the order Application does, with a real loading thread that opens the zone
# archive through the real S3DLoader. The destination-load case is asset-gated
# (data/eq_client, or EQ_CLIENT_PATH; a FAIL under WILLEQ_REQUIRE_ASSETS=1);
# the loader-failure and exit-code cases need nothing. No server, no device.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_load_handoff
        test_zone_load_handoff.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_load_handoff PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_load_handoff GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_load_handoff DISCOVERY_TIMEOUT 120)
ENDIF()
