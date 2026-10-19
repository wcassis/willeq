# CMake 4 rejects etc2comp's historical minimum. Patch only its fetched
# build metadata; never change the upstream source or global policy floor.
SET(cmake_file "${ETC2COMP_SOURCE_DIR}/CMakeLists.txt")
FILE(READ "${cmake_file}" contents)
STRING(REGEX REPLACE "cmake_minimum_required\\(VERSION [0-9]+(\\.[0-9]+)*\\)"
    "cmake_minimum_required(VERSION 3.5)" patched "${contents}")
IF(patched STREQUAL contents AND NOT contents MATCHES "cmake_minimum_required\\(VERSION 3\\.5\\)")
    MESSAGE(FATAL_ERROR "etc2comp minimum-version declaration not found")
ENDIF()
FILE(WRITE "${cmake_file}" "${patched}")
