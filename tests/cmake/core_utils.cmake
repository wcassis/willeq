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
