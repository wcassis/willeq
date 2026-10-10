# tests/cmake/world_geometry.cmake -- included from tests/CMakeLists.txt.
# Raycasting, height/collision maps, zone lines, pathfinding, EQ client geometry.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Test raycast mesh and map loading
ADD_EXECUTABLE(test_raycast
    test_raycast.cpp
    ${CMAKE_SOURCE_DIR}/src/client/raycast_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/client/hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_raycast GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_raycast DISCOVERY_TIMEOUT 120)

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
TARGET_COMPILE_DEFINITIONS(test_hc_map PRIVATE WILLEQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}")  # T504d ray-set goldens
GTEST_DISCOVER_TESTS(test_hc_map DISCOVERY_TIMEOUT 120)

# B941 (GitLab #259): the collision map and navmesh re-representation is
# bit-identical -- triangle lists, FindBestZ / CheckLOS and FindPath /
# FindRoute digests over real zones (skip when /mnt/projects/maps is absent)
# against the pre-B941 code, plus a synthetic V2 map that always runs.
SET(TEST_GAME_STATE_IDENTITY_SOURCES
    test_game_state_identity.cpp
    ${CMAKE_SOURCE_DIR}/src/client/hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/client/raycast_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
SET(TEST_GAME_STATE_IDENTITY_LIBS GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES})
IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    LIST(APPEND TEST_GAME_STATE_IDENTITY_SOURCES
        ${CMAKE_SOURCE_DIR}/src/client/pathfinder_nav_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/navmesh_load.cpp)
    LIST(APPEND TEST_GAME_STATE_IDENTITY_LIBS ${RECAST_LIBRARY} ${DETOUR_LIBRARY})
ENDIF()
ADD_EXECUTABLE(test_game_state_identity ${TEST_GAME_STATE_IDENTITY_SOURCES})
TARGET_LINK_LIBRARIES(test_game_state_identity ${TEST_GAME_STATE_IDENTITY_LIBS})
GTEST_DISCOVER_TESTS(test_game_state_identity DISCOVERY_TIMEOUT 120)

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
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
TARGET_COMPILE_DEFINITIONS(test_zone_lines PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
TARGET_LINK_LIBRARIES(test_zone_lines GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES} ${JSONCPP_LIBRARIES})
GTEST_DISCOVER_TESTS(test_zone_lines DISCOVERY_TIMEOUT 120)

# Test EQ client structs and enums
ADD_EXECUTABLE(test_eq_client
    test_eq_client.cpp
    ${CMAKE_SOURCE_DIR}/src/client/position.cpp
)
TARGET_LINK_LIBRARIES(test_eq_client GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_eq_client DISCOVERY_TIMEOUT 120)

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
        ${CMAKE_SOURCE_DIR}/src/client/navmesh_load.cpp
        ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
    )
    LIST(APPEND TEST_PATHFINDER_LIBS ${RECAST_LIBRARY} ${DETOUR_LIBRARY} ${ZLIB_LIBRARIES})
ENDIF()

ADD_EXECUTABLE(test_pathfinder ${TEST_PATHFINDER_SOURCES})
TARGET_LINK_LIBRARIES(test_pathfinder ${TEST_PATHFINDER_LIBS})
GTEST_DISCOVER_TESTS(test_pathfinder DISCOVERY_TIMEOUT 120)

# Spatial budget overrun (B813 follow-up): the software-occlusion occluder
# table is held once, flat (40 B per triangle + 8 B per region), and both zone
# loaders select occluders through the one shared body. Device-free: the
# culler TU plus a header-only template.
ADD_EXECUTABLE(test_region_occluders
    test_region_occluders.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/software_occlusion_culler.cpp
)
TARGET_COMPILE_DEFINITIONS(test_region_occluders PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
TARGET_LINK_LIBRARIES(test_region_occluders GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_region_occluders DISCOVERY_TIMEOUT 120)

# B914: the gate's benchmark releases the zone source the client releases at
# the end of a load (Step 13), through the one shared S3DZone function. Loads
# befallen.s3d (asset-gated, tests/asset_requirement.h) plus a source census.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zone_source_release
        test_zone_source_release.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_source_release PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    TARGET_INCLUDE_DIRECTORIES(test_zone_source_release PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zone_source_release GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_zone_source_release DISCOVERY_TIMEOUT 120)
ENDIF()

# B909 (GitLab #229): the zone's collision triangles are held once -- a pooled
# zone's set reads the draw pools in place, with bit-identical ray results.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_region_world_triangles
        test_region_world_triangles.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_region_world_triangles PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    TARGET_INCLUDE_DIRECTORIES(test_region_world_triangles PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_region_world_triangles GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_region_world_triangles DISCOVERY_TIMEOUT 120)
ENDIF()

# B909b (GitLab #229): placed objects' collision holds each distinct mesh once
# (a matrix + box per instance), with bit-identical ray results to the old
# per-instance world-space copy.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_object_collision_set
        test_object_collision_set.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_object_collision_set PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    TARGET_INCLUDE_DIRECTORIES(test_object_collision_set PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_object_collision_set GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_object_collision_set DISCOVERY_TIMEOUT 120)
ENDIF()

# B997 (GitLab #317): the per-material client-array batch of the PVS doors.
# Pins the batching decision (grouping, the opaque-only guard, the world
# transform bake + u16 index remap) so the residual draw count cannot silently
# regress. Runs headless on the EDT_NULL driver (lastBakedDraw() seam).
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_scene_graph_batch
        test_scene_graph_batch.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_scene_graph_batch PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_scene_graph_batch GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_scene_graph_batch DISCOVERY_TIMEOUT 120)
ENDIF()

# B936 (GitLab #254): placed objects hold one mesh per geometry, the
# vertex-animated (keyframe) ones included; one pose routine.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_placed_object_meshes
        test_placed_object_meshes.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_placed_object_meshes PRIVATE WILLEQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    TARGET_INCLUDE_DIRECTORIES(test_placed_object_meshes PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_placed_object_meshes GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_placed_object_meshes DISCOVERY_TIMEOUT 120)
ENDIF()

# B938 (GitLab #256): the compact per-placement PlacedObjectRecord that
# replaced the ~2.2 KB per-placement IMeshSceneNode. Header-only size/layout
# pin (168 B on x86-64) plus the structural invariants the refactor's
# reference accounting rests on. No device, no zone files.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_placed_object_record
        test_placed_object_record.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_placed_object_record PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_placed_object_record GTest::gtest_main willeq_core)
    GTEST_DISCOVER_TESTS(test_placed_object_record DISCOVERY_TIMEOUT 120)
ENDIF()
