CMAKE_MINIMUM_REQUIRED(VERSION 3.14)

PROJECT(willeq VERSION 1.0.0 LANGUAGES CXX C)

# C++17 required
SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
OPTION(EQT_DEBUG "Enable debug logging" ON)
OPTION(EQT_BUILD_TESTS "Build tests" ON)
OPTION(EQT_GRAPHICS "Enable Irrlicht graphics rendering" ON)

# Output directories
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
SET(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
SET(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Debug definition
IF(EQT_DEBUG)
    ADD_DEFINITIONS(-DEQT_DEBUG)
ENDIF()

# Find required packages
FIND_PACKAGE(Threads REQUIRED)
FIND_PACKAGE(OpenSSL REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)

# Find libuv
FIND_PATH(LIBUV_INCLUDE_DIR uv.h)
FIND_LIBRARY(LIBUV_LIBRARY NAMES uv libuv)
IF(NOT LIBUV_INCLUDE_DIR OR NOT LIBUV_LIBRARY)
    MESSAGE(FATAL_ERROR "libuv not found")
ENDIF()

# Find fmt
FIND_PACKAGE(fmt QUIET)
IF(NOT fmt_FOUND)
    FIND_PATH(FMT_INCLUDE_DIR fmt/format.h)
    FIND_LIBRARY(FMT_LIBRARY NAMES fmt fmtd)
    IF(NOT FMT_INCLUDE_DIR OR NOT FMT_LIBRARY)
        MESSAGE(FATAL_ERROR "fmt not found")
    ENDIF()
ENDIF()

# Find GLM (header-only)
FIND_PATH(GLM_INCLUDE_DIR glm/glm.hpp)
IF(NOT GLM_INCLUDE_DIR)
    MESSAGE(FATAL_ERROR "GLM not found")
ENDIF()

# Find jsoncpp
FIND_PACKAGE(PkgConfig)
IF(PkgConfig_FOUND)
    PKG_CHECK_MODULES(JSONCPP jsoncpp)
ENDIF()
IF(NOT JSONCPP_FOUND)
    FIND_PATH(JSONCPP_INCLUDE_DIRS json/json.h PATH_SUFFIXES jsoncpp)
    FIND_LIBRARY(JSONCPP_LIBRARIES NAMES jsoncpp)
ENDIF()

# Find Boost (for pathfinding)
FIND_PACKAGE(Boost 1.70 COMPONENTS graph)

# Find Recast/Detour (optional, for navmesh pathfinding)
FIND_PATH(RECAST_INCLUDE_DIR Recast.h PATH_SUFFIXES recastnavigation)
FIND_LIBRARY(RECAST_LIBRARY NAMES Recast PATHS /usr/lib/x86_64-linux-gnu)
FIND_PATH(DETOUR_INCLUDE_DIR DetourNavMesh.h PATH_SUFFIXES recastnavigation)
FIND_LIBRARY(DETOUR_LIBRARY NAMES Detour PATHS /usr/lib/x86_64-linux-gnu)

# Find Irrlicht (optional, for graphics rendering)
IF(EQT_GRAPHICS)
    FIND_PATH(IRRLICHT_INCLUDE_DIR irrlicht.h PATH_SUFFIXES irrlicht)
    FIND_LIBRARY(IRRLICHT_LIBRARY NAMES Irrlicht)
    IF(IRRLICHT_INCLUDE_DIR AND IRRLICHT_LIBRARY)
        ADD_DEFINITIONS(-DEQT_HAS_GRAPHICS)
        MESSAGE(STATUS "Irrlicht graphics support: ENABLED")
    ELSE()
        MESSAGE(STATUS "Irrlicht graphics support: DISABLED (Irrlicht not found)")
        SET(EQT_GRAPHICS OFF)
    ENDIF()
ENDIF()

# Include directories
INCLUDE_DIRECTORIES(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBUV_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
)

IF(fmt_FOUND)
    INCLUDE_DIRECTORIES(${fmt_INCLUDE_DIRS})
ELSE()
    INCLUDE_DIRECTORIES(${FMT_INCLUDE_DIR})
ENDIF()

IF(JSONCPP_INCLUDE_DIRS)
    INCLUDE_DIRECTORIES(${JSONCPP_INCLUDE_DIRS})
ENDIF()

IF(Boost_FOUND)
    INCLUDE_DIRECTORIES(${Boost_INCLUDE_DIRS})
ENDIF()

IF(RECAST_INCLUDE_DIR AND DETOUR_INCLUDE_DIR)
    INCLUDE_DIRECTORIES(${RECAST_INCLUDE_DIR} ${DETOUR_INCLUDE_DIR})
    ADD_DEFINITIONS(-DEQT_HAS_NAVMESH)
ENDIF()

IF(EQT_GRAPHICS AND IRRLICHT_INCLUDE_DIR)
    INCLUDE_DIRECTORIES(${IRRLICHT_INCLUDE_DIR})
ENDIF()

# Common library sources
SET(COMMON_SOURCES
    src/common/util/types.cpp
    src/common/util/strings.cpp
    src/common/util/random.cpp
    src/common/util/data_verification.cpp
    src/common/util/compression.cpp
    src/common/util/file.cpp
    src/common/util/json_config.cpp
    src/common/net/packet.cpp
    src/common/net/daybreak_connection.cpp
    src/common/net/crc32.cpp
    src/common/event/timer.cpp
    src/common/performance_metrics.cpp
)

# Client sources
SET(CLIENT_SOURCES
    src/client/eq.cpp
    src/client/main.cpp
    src/client/combat.cpp
    src/client/trade_manager.cpp
    src/client/ucs.cpp
    src/client/position.cpp
    src/client/formatted_message.cpp
    src/client/pathfinder_interface.cpp
    src/client/pathfinder_null.cpp
    src/client/raycast_mesh.cpp
    src/client/hc_map.cpp
    src/client/zone_lines.cpp
    # Spell system sources
    src/client/spell/spell_database.cpp
    src/client/spell/spell_manager.cpp
    src/client/spell/buff_manager.cpp
    src/client/spell/spell_effects.cpp
    src/client/spell/spell_type_processor.cpp
    src/client/spell/spell_animations.cpp
    # Skill system sources
    src/client/skill/skill_manager.cpp
)

# Navmesh pathfinding (optional)
IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    LIST(APPEND CLIENT_SOURCES src/client/pathfinder_nav_mesh.cpp)
ENDIF()

# Waypoint pathfinding (optional, requires Boost)
IF(Boost_FOUND)
    LIST(APPEND CLIENT_SOURCES src/client/pathfinder_waypoint.cpp)
    ADD_DEFINITIONS(-DEQT_HAS_WAYPOINT)
ENDIF()

# Graphics sources (optional, requires Irrlicht)
SET(GRAPHICS_SOURCES)
IF(EQT_GRAPHICS)
    SET(GRAPHICS_SOURCES
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/eq/equipment_model_loader.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/client/graphics/irrlicht_renderer.cpp
        src/client/graphics/camera_controller.cpp
        src/client/graphics/entity_renderer.cpp
        src/client/graphics/door_manager.cpp
        src/client/graphics/animated_texture_manager.cpp
        # Inventory UI sources
        src/client/graphics/ui/inventory_constants.cpp
        src/client/graphics/ui/inventory_manager.cpp
        src/client/graphics/ui/window_base.cpp
        src/client/graphics/ui/item_slot.cpp
        src/client/graphics/ui/item_tooltip.cpp
        src/client/graphics/ui/item_icon_loader.cpp
        src/client/graphics/ui/inventory_window.cpp
        src/client/graphics/ui/bag_window.cpp
        src/client/graphics/ui/loot_window.cpp
        src/client/graphics/ui/vendor_window.cpp
        src/client/graphics/ui/trade_window.cpp
        src/client/graphics/ui/trade_request_dialog.cpp
        src/client/graphics/ui/money_input_dialog.cpp
        src/client/graphics/ui/window_manager.cpp
        src/client/graphics/ui/chat_message_buffer.cpp
        src/client/graphics/ui/chat_input_field.cpp
        src/client/graphics/ui/chat_window.cpp
        src/client/graphics/ui/command_registry.cpp
        src/client/graphics/ui/command_autocomplete.cpp
        src/client/graphics/ui/spell_book_window.cpp
        src/client/graphics/ui/spell_gem_panel.cpp
        src/client/graphics/ui/buff_window.cpp
        src/client/graphics/ui/buff_tooltip.cpp
        src/client/graphics/ui/skill_tooltip.cpp
        src/client/graphics/ui/group_window.cpp
        src/client/graphics/ui/hotbar_window.cpp
        src/client/graphics/ui/hotbar_cursor.cpp
        src/client/graphics/ui/skills_window.cpp
        src/client/graphics/ui/note_window.cpp
        src/client/graphics/ui/player_status_window.cpp
        src/client/graphics/ui/casting_bar.cpp
        src/client/graphics/ui/character_model_view.cpp
        src/client/graphics/ui/ui_settings.cpp
        src/client/graphics/spell_visual_fx.cpp
    )
ENDIF()

# All sources
SET(ALL_SOURCES
    ${COMMON_SOURCES}
    ${CLIENT_SOURCES}
    ${PATHFINDING_SOURCES}
    ${MAP_SOURCES}
    ${GRAPHICS_SOURCES}
)

# Create the executable
ADD_EXECUTABLE(willeq ${ALL_SOURCES})

# Link libraries
SET(LINK_LIBS
    ${LIBUV_LIBRARY}
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    Threads::Threads
)

IF(fmt_FOUND)
    LIST(APPEND LINK_LIBS fmt::fmt)
ELSE()
    LIST(APPEND LINK_LIBS ${FMT_LIBRARY})
ENDIF()

IF(JSONCPP_LIBRARIES)
    LIST(APPEND LINK_LIBS ${JSONCPP_LIBRARIES})
ENDIF()

IF(Boost_FOUND)
    LIST(APPEND LINK_LIBS ${Boost_LIBRARIES})
ENDIF()

IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    LIST(APPEND LINK_LIBS ${RECAST_LIBRARY} ${DETOUR_LIBRARY})
ENDIF()

IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    LIST(APPEND LINK_LIBS ${IRRLICHT_LIBRARY})
    IF(APPLE)
        # macOS frameworks
        FIND_LIBRARY(COCOA_FRAMEWORK Cocoa)
        FIND_LIBRARY(OPENGL_FRAMEWORK OpenGL)
        FIND_LIBRARY(IOKIT_FRAMEWORK IOKit)
        FIND_LIBRARY(CARBON_FRAMEWORK Carbon)
        LIST(APPEND LINK_LIBS ${COCOA_FRAMEWORK} ${OPENGL_FRAMEWORK} ${IOKIT_FRAMEWORK} ${CARBON_FRAMEWORK})
    ELSE()
        # Linux/X11
        LIST(APPEND LINK_LIBS X11 Xxf86vm GL)
    ENDIF()
ENDIF()

TARGET_LINK_LIBRARIES(willeq ${LINK_LIBS})

# Model Viewer tool (for debugging character models)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(model_viewer
        tools/model_viewer.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/eq/equipment_model_loader.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/common/performance_metrics.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(model_viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${IRRLICHT_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIRS}
    )
    IF(APPLE)
        TARGET_LINK_LIBRARIES(model_viewer
            ${IRRLICHT_LIBRARY}
            ${ZLIB_LIBRARIES}
            ${JSONCPP_LIBRARIES}
            fmt::fmt
            ${COCOA_FRAMEWORK}
            ${OPENGL_FRAMEWORK}
            ${IOKIT_FRAMEWORK}
            ${CARBON_FRAMEWORK}
        )
    ELSE()
        TARGET_LINK_LIBRARIES(model_viewer
            ${IRRLICHT_LIBRARY}
            ${ZLIB_LIBRARIES}
            ${JSONCPP_LIBRARIES}
            fmt::fmt
            X11
            Xxf86vm
            GL
        )
    ENDIF()
ENDIF()

# S3D Dump tool (for analyzing S3D archive contents)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(s3d_dump
        tools/s3d_dump.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(s3d_dump PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(s3d_dump
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    # S3D Extract tool (for extracting files from S3D archives)
    ADD_EXECUTABLE(s3d_extract
        tools/s3d_extract.cpp
        src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(s3d_extract PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(s3d_extract
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    # WLD Dump tool (for analyzing WLD file contents)
    ADD_EXECUTABLE(wld_dump
        tools/wld_dump.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(wld_dump PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(wld_dump
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    # BSP Region Finder tool (for analyzing zone line regions)
    ADD_EXECUTABLE(bsp_region_finder
        tools/bsp_region_finder.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(bsp_region_finder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(bsp_region_finder
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    ADD_EXECUTABLE(analyze_hum_model
        tools/analyze_hum_model.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(analyze_hum_model PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(analyze_hum_model
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    ADD_EXECUTABLE(test_animation
        tools/test_animation.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_animation PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(test_animation
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )
ENDIF()

# Install
INSTALL(TARGETS willeq RUNTIME DESTINATION bin)

# Tests
IF(EQT_BUILD_TESTS)
    ENABLE_TESTING()

    # Find or fetch Google Test
    FIND_PACKAGE(GTest QUIET)
    IF(NOT GTest_FOUND)
        INCLUDE(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
        )
        FetchContent_MakeAvailable(googletest)
    ENDIF()

    # Test sources
    ADD_SUBDIRECTORY(tests)
ENDIF()

# Print configuration summary
MESSAGE(STATUS "")
MESSAGE(STATUS "=== WillEQ Configuration ===")
MESSAGE(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "Debug logging: ${EQT_DEBUG}")
MESSAGE(STATUS "Build tests: ${EQT_BUILD_TESTS}")
MESSAGE(STATUS "libuv: ${LIBUV_LIBRARY}")
MESSAGE(STATUS "OpenSSL: ${OPENSSL_LIBRARIES}")
MESSAGE(STATUS "zlib: ${ZLIB_LIBRARIES}")
IF(fmt_FOUND)
    MESSAGE(STATUS "fmt: found via package")
ELSE()
    MESSAGE(STATUS "fmt: ${FMT_LIBRARY}")
ENDIF()
MESSAGE(STATUS "GLM: ${GLM_INCLUDE_DIR}")
IF(JSONCPP_LIBRARIES)
    MESSAGE(STATUS "jsoncpp: ${JSONCPP_LIBRARIES}")
ELSE()
    MESSAGE(STATUS "jsoncpp: NOT FOUND")
ENDIF()
IF(Boost_FOUND)
    MESSAGE(STATUS "Boost: ${Boost_VERSION}")
ELSE()
    MESSAGE(STATUS "Boost: NOT FOUND (waypoint pathfinding disabled)")
ENDIF()
IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    MESSAGE(STATUS "Recast/Detour: ${RECAST_LIBRARY}, ${DETOUR_LIBRARY}")
ELSE()
    MESSAGE(STATUS "Recast/Detour: NOT FOUND (navmesh pathfinding disabled)")
ENDIF()
IF(EQT_GRAPHICS)
    MESSAGE(STATUS "Irrlicht: ${IRRLICHT_LIBRARY}")
ELSE()
    MESSAGE(STATUS "Irrlicht: NOT FOUND (graphics disabled)")
ENDIF()
MESSAGE(STATUS "")
