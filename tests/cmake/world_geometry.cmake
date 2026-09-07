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
GTEST_DISCOVER_TESTS(test_hc_map DISCOVERY_TIMEOUT 120)

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
        ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
    )
    LIST(APPEND TEST_PATHFINDER_LIBS ${RECAST_LIBRARY} ${DETOUR_LIBRARY} ${ZLIB_LIBRARIES})
ENDIF()

ADD_EXECUTABLE(test_pathfinder ${TEST_PATHFINDER_SOURCES})
TARGET_LINK_LIBRARIES(test_pathfinder ${TEST_PATHFINDER_LIBS})
GTEST_DISCOVER_TESTS(test_pathfinder DISCOVERY_TIMEOUT 120)
