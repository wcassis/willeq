# tests/cmake/network.cmake -- included from tests/CMakeLists.txt.
# Network protocol tests: integration harness, pcap replay, DaybreakConnection, Titanium opcodes.
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

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

# T321: CRC validation driven by REAL captured wire bytes (login/world/zone
# session responses + a covering sample of each stream) from T311 Leg A's
# 73,506-packet pcap. Pins that ValidateCRC() accepts every recorded packet,
# rejects corrupted/truncated ones, and that skip_crc_validation (which
# src/client/eq.cpp sets on every zone connect) accepts corruption.
ADD_EXECUTABLE(test_daybreak_crc_capture
    test_daybreak_crc_capture.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/daybreak_connection.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
)
TARGET_COMPILE_DEFINITIONS(test_daybreak_crc_capture PRIVATE
    T321_CRC_CAPTURE="${CMAKE_SOURCE_DIR}/tests/data/t321_crc_capture.txt")
TARGET_LINK_LIBRARIES(test_daybreak_crc_capture GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_daybreak_crc_capture)

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
