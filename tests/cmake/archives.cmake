# tests/cmake/archives.cmake -- included from tests/CMakeLists.txt.
# PFS archive + WLD loader tests.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

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
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_material.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_mesh.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_skeleton.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_light.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_fragments_bsp.cpp
        ${CMAKE_SOURCE_DIR}/src/client/zone_bsp.cpp
    )
    TARGET_LINK_LIBRARIES(test_wld GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_wld)
ENDIF()
