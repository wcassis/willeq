# tests/cmake/data_formats.cmake -- included from tests/CMakeLists.txt.
# Wire structs, session recorder, formatted messages, frustum/SIMD math, background work queues.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

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
