CMAKE_MINIMUM_REQUIRED(VERSION 3.14)

PROJECT(willeq VERSION 1.0.0 LANGUAGES CXX C)

# C++17 required
SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build options
OPTION(EQT_DEBUG "Enable debug logging" ON)
OPTION(EQT_BUILD_TESTS "Build tests" ON)
OPTION(EQT_GRAPHICS "Enable Irrlicht graphics rendering" ON)
OPTION(EQT_DRM "Use DRM/KMS/GBM/EGL instead of X11 (for ARM boards without X11)" OFF)
OPTION(EQT_GLES2 "Use OpenGL ES 2.0 backend instead of desktop GL (for DRM+Mali/Lima)" OFF)
# T164 (2026-08-21): register the live-only integration tests with ctest under
# the "live" label. OFF by default ON PURPOSE -- these need a running eqemu
# server, a display and EQ client files, so they must never join the default
# suite (whose total every unit compares against as its baseline). The three
# binaries are still BUILT unconditionally, which is what keeps them compiling;
# this option only controls whether ctest knows about them.
#   cmake -S . -B build -DEQT_LIVE_TESTS=ON
#   ctest --test-dir build -L live
OPTION(EQT_LIVE_TESTS "Register the live/integration tests (need a server + display) with ctest" OFF)
# H01: path to a locally-built patched Irrlicht (headers/ + lib/libIrrlicht.a),
# e.g. third_party/irrlicht-gles2-x86/ produced by scripts/build-gles2-x86.sh.
# When set, this overrides the system Irrlicht FIND_PATH/FIND_LIBRARY below.
# Leave empty (default) for the normal desktop-GL build and the ARM Docker flow.
SET(EQT_GLES2_IRRLICHT_DIR "" CACHE PATH "Path to a locally-built patched Irrlicht for x86 GLES2 dev builds")

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
FIND_LIBRARY(RECAST_LIBRARY NAMES Recast)
FIND_PATH(DETOUR_INCLUDE_DIR DetourNavMesh.h PATH_SUFFIXES recastnavigation)
FIND_LIBRARY(DETOUR_LIBRARY NAMES Detour)

# Find Irrlicht (required for graphics rendering, when EQT_GRAPHICS=ON).
#
# P30 (Phase 3, desktop_gl_elimination_plan.md): the system-Irrlicht
# FIND_PATH/FIND_LIBRARY fallback (searching for an apt-installed
# libirrlicht-dev, i.e. desktop GL / X11-GLX) was REMOVED — that backend no
# longer exists in this project. EQT_GLES2_IRRLICHT_DIR (a locally-built
# patched GLES2 Irrlicht, see scripts/build-gles2-x86.sh) is now the only
# supported way to provide Irrlicht for a from-scratch configure.
#
# ARM cross-compiles (docker/Dockerfile.arm-noble, scripts/build-arm-noble.sh)
# don't set EQT_GLES2_IRRLICHT_DIR at all — they pass -DIRRLICHT_INCLUDE_DIR /
# -DIRRLICHT_LIBRARY directly as CMake cache variables pointing at the
# ARM sysroot's own patched Irrlicht (installed via `cmake --install` earlier
# in the Dockerfile). Both mechanisms funnel into the same IRRLICHT_INCLUDE_DIR
# / IRRLICHT_LIBRARY variables checked below, so the ARM path is unaffected by
# this removal.
IF(EQT_GRAPHICS)
    IF(EQT_GLES2_IRRLICHT_DIR)
        # H01: use the locally-built patched Irrlicht (x86 GLES2 dev builds)
        # instead of searching the system for one.
        # Absolutize first: CMake's EXISTS is only defined for absolute
        # paths, so the documented relative form
        # (-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86) would
        # otherwise fail the check below even when the library is present.
        GET_FILENAME_COMPONENT(EQT_GLES2_IRRLICHT_DIR_ABS
            "${EQT_GLES2_IRRLICHT_DIR}" ABSOLUTE
            BASE_DIR "${CMAKE_SOURCE_DIR}")
        # Second trap: CMake itself absolutizes an untyped -D value against
        # the *invocation* cwd when the cache entry is typed PATH — so a
        # relative value passed while running `cmake -S` from elsewhere
        # arrives here already absolute-but-wrong. If the resolved location
        # lacks the library but the in-tree default has it, prefer the
        # in-tree default (with a notice).
        IF(NOT EXISTS "${EQT_GLES2_IRRLICHT_DIR_ABS}/lib/libIrrlicht.a"
           AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/irrlicht-gles2-x86/lib/libIrrlicht.a")
            MESSAGE(WARNING "EQT_GLES2_IRRLICHT_DIR resolved to "
                "'${EQT_GLES2_IRRLICHT_DIR_ABS}' (no libIrrlicht.a there); "
                "using the in-tree default third_party/irrlicht-gles2-x86 "
                "instead. If you intended an alternate Irrlicht tree, pass "
                "an absolute path to it.")
            SET(EQT_GLES2_IRRLICHT_DIR_ABS
                "${CMAKE_SOURCE_DIR}/third_party/irrlicht-gles2-x86")
        ENDIF()
        SET(IRRLICHT_INCLUDE_DIR "${EQT_GLES2_IRRLICHT_DIR_ABS}/include")
        SET(IRRLICHT_LIBRARY "${EQT_GLES2_IRRLICHT_DIR_ABS}/lib/libIrrlicht.a")
    ENDIF()
    IF(IRRLICHT_INCLUDE_DIR AND IRRLICHT_LIBRARY AND EXISTS "${IRRLICHT_INCLUDE_DIR}/irrlicht.h" AND EXISTS "${IRRLICHT_LIBRARY}")
        ADD_DEFINITIONS(-DEQT_HAS_GRAPHICS)
        MESSAGE(STATUS "Irrlicht graphics support: ENABLED (${IRRLICHT_LIBRARY})")
        IF(NOT (EQT_DRM OR EQT_GLES2))
            MESSAGE(FATAL_ERROR
                "EQT_GRAPHICS=ON with a patched Irrlicht requires -DEQT_GLES2=ON "
                "(x86 GLES2 dev builds) or -DEQT_DRM=ON (ARM DRM/KMS builds) — "
                "desktop GL (X11/GLX) support was removed (P30, "
                "refactor/desktop_gl_elimination_plan.md). Configure with "
                "-DEQT_GLES2=ON.")
        ENDIF()
    ELSE()
        MESSAGE(FATAL_ERROR
            "EQT_GRAPHICS=ON requires a locally-built patched GLES2 Irrlicht — "
            "desktop-GL Irrlicht (system libirrlicht-dev / X11-GLX) support was "
            "removed (P30, refactor/desktop_gl_elimination_plan.md). "
            "For x86 dev builds: run 'bash scripts/build-gles2-x86.sh "
            "--irrlicht-only' once, then configure with "
            "-DEQT_GLES2_IRRLICHT_DIR=third_party/irrlicht-gles2-x86 "
            "-DEQT_GLES2=ON (see CLAUDE.md 'Build Commands'). For ARM "
            "cross-compiles, use scripts/build-arm-noble.sh, which supplies "
            "IRRLICHT_INCLUDE_DIR/IRRLICHT_LIBRARY directly. To build without "
            "graphics at all, configure with -DEQT_GRAPHICS=OFF.")
    ENDIF()
ENDIF()

# Audio support (uses header-only libraries: miniaudio, TinySoundFont, dr_wav)
# No external audio dependencies required
OPTION(WITH_AUDIO "Enable audio support" ON)
IF(WITH_AUDIO)
    ADD_DEFINITIONS(-DWITH_AUDIO)
    MESSAGE(STATUS "Audio support: ENABLED (miniaudio + TinySoundFont + dr_wav)")
ELSE()
    MESSAGE(STATUS "Audio support: DISABLED (not requested)")
ENDIF()

# Find FreeRDP (optional, for native RDP streaming - requires graphics)
# RDP is an additional display option alongside Xvfb+x11vnc
IF(EQT_GRAPHICS)
    IF(PkgConfig_FOUND)
        # Try FreeRDP 3.x first, then fall back to 2.x
        PKG_CHECK_MODULES(FREERDP freerdp3 winpr3)
        IF(NOT FREERDP_FOUND)
            PKG_CHECK_MODULES(FREERDP freerdp2 winpr2)
        ENDIF()
    ENDIF()
    IF(FREERDP_FOUND)
        OPTION(WITH_RDP "Enable native RDP support" ON)
    ELSE()
        OPTION(WITH_RDP "Enable native RDP support" OFF)
    ENDIF()
    IF(WITH_RDP)
        IF(FREERDP_FOUND)
            ADD_DEFINITIONS(-DWITH_RDP)
            MESSAGE(STATUS "Native RDP support: ENABLED")
        ELSE()
            MESSAGE(STATUS "Native RDP support: DISABLED (FreeRDP not found)")
            SET(WITH_RDP OFF)
        ENDIF()
    ELSE()
        MESSAGE(STATUS "Native RDP support: DISABLED (not requested)")
    ENDIF()
ELSE()
    SET(WITH_RDP OFF)
ENDIF()

# Include directories
INCLUDE_DIRECTORIES(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/minimp3
    ${CMAKE_SOURCE_DIR}/third_party/TinySoundFont
    ${CMAKE_SOURCE_DIR}/third_party/dr_libs
    ${CMAKE_SOURCE_DIR}/third_party/miniaudio
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
ENDIF()
# T118: EQT_HAS_NAVMESH must gate on the LIBRARY being found (matching the
# CLIENT_CORE_SOURCES condition below that actually compiles
# pathfinder_nav_mesh.cpp), not merely the headers. Headers are
# architecture-independent and can be present system-wide (e.g.
# /usr/include/recastnavigation) even when no matching-arch .so exists —
# gating on headers alone defined the macro with no implementation linked
# in, an undefined-reference link failure discovered building an i386 tree
# where librecast-dev has no i386 package. This was previously silent only
# because every environment that had the headers also had the amd64 .so.
IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    ADD_DEFINITIONS(-DEQT_HAS_NAVMESH)
ENDIF()

IF(EQT_GRAPHICS AND IRRLICHT_INCLUDE_DIR)
    INCLUDE_DIRECTORIES(${IRRLICHT_INCLUDE_DIR})
ENDIF()

IF(WITH_RDP AND FREERDP_FOUND)
    INCLUDE_DIRECTORIES(${FREERDP_INCLUDE_DIRS})
    LINK_DIRECTORIES(${FREERDP_LIBRARY_DIRS})
ENDIF()

# Audio include directories are already in the main INCLUDE_DIRECTORIES
# (third_party/miniaudio, third_party/TinySoundFont, third_party/dr_libs)

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
    src/common/net/daybreak_connection_decode.cpp
    src/common/net/daybreak_connection_send.cpp
    src/common/net/crc32.cpp
    src/common/net/posix_udp_transport.cpp
    src/common/net/session_recorder.cpp
    src/common/performance_metrics.cpp
)

# Client sources (core - without main.cpp, for library)
SET(CLIENT_CORE_SOURCES
    src/client/core/everquest.cpp
    src/client/zone/zone_dispatch.cpp
    src/client/zone/zone_handshake.cpp
    src/client/zone/zone_entry.cpp
    # T326: the chat-server TU was deleted here -- it held 79 include lines and
    # one 241-line comment block, no code. Recover with: git show bcbf2ddc:src/client/eq_ucs.cpp
    # K15 (B005): src/client/ucs.cpp (413 lines, UCSConnection) was deleted from
    # this list too -- zero callers tree-wide, and T297 measured cross-zone ooc,
    # auction, tell and guild all working without it. Recover with:
    # git show 7c125b28:src/client/ucs.cpp and 7c125b28:include/client/ucs.h
    src/client/bridge/bridge_component.cpp
    src/client/bridge/bridge_component_intents.cpp
    src/client/bridge/bridge_component_ui_audio.cpp
    src/client/entities/entities_component.cpp
    src/client/entities/entities_component_appearance.cpp
    src/client/entities/entities_component_updates.cpp
    src/client/entities/entities_component_world.cpp
    src/client/commands/commands_component.cpp
    src/client/commands/commands_movement.cpp
    src/client/commands/commands_spells.cpp
    src/client/commands/commands_train.cpp
    src/client/core/eq_strings.cpp
    src/client/login_world/login_world_component.cpp
    src/client/guild/guild_component.cpp
    src/client/gm/gm_component.cpp
    src/client/merchant/merchant.cpp
    src/client/merchant/merchant_trade.cpp
    src/client/merchant/merchant_vendor_bank.cpp
    src/client/core/eq_player_info.cpp
    src/client/raid_update.cpp
    src/client/inspect_answer.cpp
    src/client/group/group_component.cpp
    src/client/zone/zone_component.cpp
    src/client/movement/movement_component.cpp
    src/client/player/player_component.cpp
    src/client/player/player_misc.cpp
    src/client/player/player_state.cpp
    src/client/player/player_buffs.cpp
    src/client/player/player_info.cpp
    src/client/player/profile_appearance.cpp
    src/client/movement/movement_update.cpp
    src/client/movement/movement_state.cpp
    src/client/chat/chat_component.cpp
    src/client/core/eq_chat.cpp
    # K13 (Batch K): the three context services.
    src/client/chat_output.cpp
    src/client/player_facade.cpp
    src/client/packet_sender.cpp
    src/client/commands/combat_actions.cpp
    src/client/combat.cpp
    src/client/combat_rounds.cpp
    src/client/combat_hunting.cpp
    src/client/corpse_predicate.cpp
    src/client/bind_wound_outcome.cpp
    src/client/who_format.cpp
    src/client/who_response.cpp
    src/client/trade_manager.cpp
    src/client/door_state_manager.cpp
    src/client/position.cpp
    src/client/world_object.cpp
    src/client/formatted_message.cpp
    src/client/string_database.cpp
    src/client/willeq_database.cpp
    third_party/sqlite3.c
    src/client/pathfinder_interface.cpp
    src/client/pathfinder_null.cpp
    src/client/raycast_mesh.cpp
    src/client/hc_map.cpp
    src/client/zone_bsp.cpp
    src/client/zone_lines.cpp
    # P28: PfsArchive/WldLoader have no renderer dependency (zone_bsp.h: "no
    # graphics dependencies") and are used unconditionally by base game logic
    # regardless of EQT_GRAPHICS — audio_manager.cpp scans sound archives via
    # PfsArchive, and zone_component.cpp's LoadZoneSources() parses the zone WLD for
    # the BSP tree (water detection / zone lines). Both were mis-listed under
    # GRAPHICS_SOURCES.
    src/client/graphics/eq/pfs.cpp
    src/client/graphics/eq/wld_loader.cpp
    src/client/graphics/eq/wld_fragments_material.cpp
    src/client/graphics/eq/wld_fragments_mesh.cpp
    src/client/graphics/eq/wld_fragments_actor.cpp
    src/client/graphics/eq/wld_fragments_skeleton.cpp
    src/client/graphics/eq/wld_fragments_light.cpp
    src/client/graphics/eq/wld_fragments_bsp.cpp
    # P28: same misclassification — inventory manager, hotbar model, chat
    # buffer, and chat command registry are plain data models with no
    # renderer dependency (declassified in eq.h; see the P28 unit).
    src/client/inventory/inventory_constants.cpp
    src/client/inventory/inventory_manager.cpp
    # J01: inventory_manager.cpp split move-only into four TUs
    src/client/inventory/inventory_item_parser.cpp
    src/client/inventory/inventory_cursor.cpp
    src/client/inventory/inventory_bags.cpp
    src/client/inventory/inventory_packets.cpp
    src/client/hotbar/hotbar_model.cpp
    src/client/chat/chat_message_buffer.cpp
    src/client/graphics/ui/command_registry.cpp
    # T310 Leg A: the /fpcam tunable store's name lookup, parsing and
    # formatting. No renderer dependency at all (it drives constants, not a
    # camera), and Application::initialize() applies --fpcam in every build,
    # graphics or not -- so it belongs here rather than in GRAPHICS_SOURCES.
    src/client/graphics/first_person_tunables.cpp
    # State management sources
    src/client/state/event_bus.cpp
    src/client/state/player_state.cpp
    src/client/state/entity_manager.cpp
    src/client/state/world_state.cpp
    src/client/state/combat_state.cpp
    src/client/state/group_state.cpp

    src/client/state/pet_state.cpp
    src/client/state/trade_state.cpp
    src/client/state/inventory_state.cpp
    src/client/state/spell_state.cpp
    src/client/state/game_state.cpp
    # Input handling sources
    src/client/input/null_input_handler.cpp
    src/client/input/console_input_handler.cpp
    # Output/rendering sources
    src/client/output/output_renderer.cpp
    src/client/output/null_renderer.cpp
    src/client/output/console_renderer.cpp
    src/client/output/graphical_renderer.cpp
    # Mode controller sources
    src/client/mode/game_mode.cpp
    src/client/mode/automated_mode.cpp
    src/client/mode/headless_mode.cpp
    src/client/mode/graphical_mode.cpp
    # Bridge sources
    src/client/bridge/console_bridge.cpp
    src/client/bridge/game_state_sync_bridge.cpp
    # Action system sources
    src/client/action/action_dispatcher.cpp
    src/client/action/action_dispatcher_social.cpp
    src/client/action/action_dispatcher_guild_misc.cpp
    src/client/action/input_action_bridge.cpp
    src/client/action/command_processor.cpp
    src/client/action/command_processor_bank.cpp
    src/client/action/command_processor_train.cpp
    src/client/action/command_processor_loot.cpp  # T268
    src/client/action/command_processor_registration.cpp  # J04
    src/client/action/command_processor_social.cpp  # J04
    src/client/action/command_processor_world.cpp  # J04
    src/client/action/command_processor_spells_gm.cpp  # J04
    # Help system sources (T143: merges EverQuest::m_command_registry +
    # action::CommandProcessor catalogs for a registry-aware /help)
    src/client/help/merged_help.cpp
    # T223: registry 3's static catalog (graphics-independent, so headless
    # builds can refuse renderer commands by name)
    src/client/help/renderer_command_catalog.cpp
    # Application layer sources
    src/client/application.cpp
    src/client/application_commands.cpp
    src/client/application_frame.cpp
    src/client/application_zone_loading.cpp
    src/client/application_config.cpp
    src/client/action/eq_action_handler.cpp
    src/client/action/eq_action_handler_spells_trade.cpp
    src/client/action/eq_action_handler_extended.cpp
    # Spell system sources
    src/client/spell/spell_database.cpp
    src/client/spell/spell_manager.cpp
    src/client/spell/spell_body_type_target.cpp
    src/client/spell/spell_cast_checks.cpp
    src/client/spell/cast_refusal.cpp
    src/client/spell/buff_manager.cpp
    src/client/spell/buff_cancel_request.cpp
    src/client/spell/spell_animations.cpp
    src/client/spell/spells_eff_reader.cpp
    # Skill system sources
    src/client/skill/behind_target.cpp
    src/client/skill/skill_cap_table.cpp
    src/client/skill/skill_manager.cpp
)

# Navmesh pathfinding (optional)
IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
    LIST(APPEND CLIENT_CORE_SOURCES src/client/pathfinder_nav_mesh.cpp)
ENDIF()

# Waypoint pathfinding (optional, requires Boost)
IF(Boost_FOUND)
    LIST(APPEND CLIENT_CORE_SOURCES src/client/pathfinder_waypoint.cpp)
    ADD_DEFINITIONS(-DEQT_HAS_WAYPOINT)
ENDIF()

# Graphics sources (optional, requires Irrlicht)
SET(GRAPHICS_SOURCES)
IF(EQT_GRAPHICS)
    SET(GRAPHICS_SOURCES
        # Graphics input handling
        src/client/input/graphics_input_handler.cpp
        # P28: unconditionally includes <irrlicht.h> — was mis-listed in
        # CLIENT_CORE_SOURCES (always compiled), which made it impossible to
        # build EQT_GRAPHICS=OFF at all regardless of eq.h's own guards.
        src/client/input/hotkey_manager.cpp
        src/client/bridge/irrlicht_bridge.cpp
        # EQ file loaders (pfs.cpp/wld_loader.cpp moved to CLIENT_CORE_SOURCES — P28, see there)
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/sky_loader.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/eq/equipment_model_loader.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/model_loading_chr.cpp
        src/client/graphics/eq/model_loading_search.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/variant_loading_animation.cpp
        src/client/graphics/eq/variant_loading_preload.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/client/graphics/eq/animated_mesh_eye.cpp
        src/client/graphics/first_person_eye.cpp
        src/client/graphics/irrlicht_renderer.cpp
        src/client/graphics/renderer_event_receiver.cpp  # J11
        src/client/graphics/renderer_init.cpp  # J11
        src/client/graphics/renderer_slash_commands.cpp  # J11
        src/client/graphics/renderer_hud_env.cpp  # J11
        src/client/graphics/renderer_camera.cpp  # J11
        # T166: the loadZoneSequential() step sequence, split out of one
        # 2996-line TU. Driver + geometry steps here, the rest alongside.
        src/client/graphics/renderer_zone_load.cpp
        src/client/graphics/renderer_zone_load_objects.cpp
        src/client/graphics/renderer_zone_load_entities.cpp
        src/client/graphics/renderer_zone_load_env.cpp
        src/client/graphics/renderer_entity_load_gate.cpp
        src/client/graphics/renderer_zone_lifecycle.cpp
        src/client/graphics/renderer_diagnostics.cpp
        src/client/graphics/renderer_debug.cpp
        src/client/graphics/renderer_overlays.cpp
        src/client/graphics/renderer_player_physics.cpp
        src/client/graphics/renderer_player_collision.cpp
        src/client/graphics/renderer_zone_draw.cpp
        src/client/graphics/renderer_zone_merge.cpp
        src/client/graphics/renderer_zone_stream.cpp
        src/client/graphics/renderer_effects.cpp
        src/client/graphics/renderer_sim_bridge.cpp
        src/client/graphics/renderer_frame.cpp
        src/client/graphics/renderer_frame_input.cpp
        src/client/graphics/renderer_frame_render.cpp
        src/client/graphics/renderer_gpu_upload.cpp
        src/client/graphics/renderer_entities.cpp
        src/client/graphics/zone_shader.cpp
        src/client/graphics/portal_system.cpp
        src/client/graphics/frustum_culler.cpp
        src/client/graphics/software_occlusion_culler.cpp
        src/client/graphics/camera_controller.cpp
        src/client/graphics/entity_renderer.cpp
        src/client/graphics/entity_mesh_build.cpp
        src/client/graphics/entity_build_step.cpp
        src/client/graphics/entity_updates.cpp
        src/client/graphics/entity_interpolation.cpp
        src/client/graphics/entity_lifecycle.cpp
        src/client/graphics/entity_castbars.cpp
        src/client/graphics/entity_combat_anim.cpp
        src/client/graphics/entity_first_person_draw.cpp
        src/client/graphics/entity_size.cpp
        src/client/graphics/entity_spatial.cpp
        src/client/graphics/entity_equipment.cpp
        src/client/graphics/background_thread_pool.cpp
        src/client/graphics/entity_prep_worker.cpp
        src/client/graphics/simulation_worker.cpp
        src/client/graphics/sim_visibility.cpp
        src/client/graphics/sim_animation.cpp
        src/client/graphics/sim_weather.cpp
        src/client/graphics/sim_particles.cpp
        src/client/graphics/sim_entity_sync.cpp
        src/client/graphics/sim_boids.cpp
        src/client/graphics/loading_thread.cpp
        src/client/graphics/door_manager.cpp
        src/client/graphics/animated_texture_manager.cpp
        src/client/graphics/sky_config.cpp
        src/client/graphics/sky_renderer.cpp
        src/client/graphics/sky_renderer_celestial.cpp
        src/client/graphics/sky_renderer_colors.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/frame_budget_governor.cpp
        src/client/graphics/texture_atlas.cpp
        src/client/graphics/eqe1_loader.cpp
    src/client/graphics/eqe1_texture_index.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/graphics_archive_index.cpp
        src/client/graphics/load_mem_audit.cpp
        # Inventory UI sources (inventory_constants/inventory_manager,
        # hotbar_model, chat_message_buffer, command_registry moved to
        # CLIENT_CORE_SOURCES — P28, see there: no renderer dependency, used
        # unconditionally by base game logic)
        src/client/graphics/ui/ui_atlas.cpp
        src/client/graphics/ui/static_ui_input.cpp
        src/client/graphics/ui/ui_renderer.cpp
        src/client/graphics/ui/static_panels.cpp
        src/client/graphics/ui/loot_panel.cpp
        src/client/graphics/ui/character_model_view.cpp
        src/client/graphics/spell_visual_fx.cpp
        src/client/graphics/spell_visual_fx_nodes.cpp
        src/client/graphics/spell_visual_selection.cpp
        src/client/graphics/text_batch.cpp
        # Detail system (grass, plants, debris)
        src/client/graphics/detail/detail_manager.cpp
        src/client/graphics/detail/detail_zone_configs.cpp
        src/client/graphics/detail/detail_chunk.cpp
        src/client/graphics/detail/detail_config_loader.cpp
        src/client/graphics/detail/detail_texture_atlas.cpp
        src/client/graphics/detail/detail_texture_atlas_flora.cpp
        src/client/graphics/detail/detail_texture_atlas_grass.cpp
        src/client/graphics/detail/seasonal_controller.cpp
        src/client/graphics/detail/foliage_disturbance.cpp
        src/client/graphics/detail/footprint_manager.cpp
        # Tree wind animation system (P32: animated_tree_manager.{h,cpp}
        # deleted — its live consumers now use tree_wind_controller/
        # tree_identifier directly, see irrlicht_renderer.h)
        src/client/graphics/tree_wind_config.cpp
        src/client/graphics/tree_identifier.cpp
        src/client/graphics/tree_wind_controller.cpp
        src/client/graphics/weather_system.cpp
        src/client/graphics/weather_effects_controller.cpp
        src/client/graphics/weather_config_loader.cpp
        src/client/graphics/weather_quality_preset.cpp
        # Environmental particle system
        src/client/graphics/environment/particle_emitter.cpp
        src/client/graphics/environment/particle_manager.cpp
        src/client/graphics/environment/environment_config.cpp
        src/client/graphics/environment/zone_biome.cpp
        # Particle emitters
        src/client/graphics/environment/emitters/dust_mote_emitter.cpp
        src/client/graphics/environment/emitters/pollen_emitter.cpp
        src/client/graphics/environment/emitters/firefly_emitter.cpp
        src/client/graphics/environment/emitters/mist_emitter.cpp
        src/client/graphics/environment/emitters/sand_dust_emitter.cpp
        src/client/graphics/environment/emitters/ember_emitter.cpp
        src/client/graphics/environment/emitters/smoke_emitter.cpp
        # Unified particle system (point sprite fire/weather effects)
        src/client/graphics/environment/unified_particle_renderer.cpp
        src/client/graphics/environment/fire_emitter_config.cpp
        src/client/graphics/environment/weather_emitter_config.cpp
        src/client/graphics/environment/spell_emitter_presets.cpp
        src/client/graphics/environment/spell_effects_config.cpp
        # Storm cloud layer
        src/client/graphics/environment/storm_cloud_layer.cpp
        # Screen-space rain overlay
        src/client/graphics/environment/rain_overlay.cpp
        # Screen-space snow overlay
        src/client/graphics/environment/snow_overlay.cpp
        # Boids (ambient creatures) system
        src/client/graphics/environment/flock_controller.cpp
        src/client/graphics/environment/boids_manager.cpp
        # Tumbleweed (rolling objects) system
        src/client/graphics/environment/tumbleweed_manager.cpp
    )
    # GLES2 EGL image sharing helper (DRM + desktop GL only, for ETC1 via DMA-BUF blit)
    # Not needed in GLES2 mode (native ETC1 support via glCompressedTexImage2D)
    IF(EQT_DRM AND NOT EQT_GLES2)
        LIST(APPEND GRAPHICS_SOURCES src/client/graphics/gles2_egl_helper.cpp)
    ENDIF()
    # GPU upload thread (GLES2 only — shared EGL context for async texture/VBO uploads)
    IF(EQT_GLES2)
        LIST(APPEND GRAPHICS_SOURCES src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
ENDIF()

# RDP server sources (optional, requires FreeRDP)
SET(RDP_SOURCES)
IF(WITH_RDP)
    SET(RDP_SOURCES
        src/client/graphics/rdp/rdp_server.cpp
        src/client/graphics/rdp/rdp_peer_context.cpp
        src/client/graphics/rdp/rdp_input_handler.cpp
        src/client/graphics/rdp/rdp_component.cpp
    )
ENDIF()

# Audio sources (optional, uses header-only libraries)
SET(AUDIO_SOURCES)
IF(WITH_AUDIO)
    SET(AUDIO_SOURCES
        src/client/audio/audio_manager.cpp
        src/client/audio/sound_buffer.cpp
        src/client/audio/music_player.cpp
        src/client/audio/sound_assets.cpp
        src/client/audio/xmi_decoder.cpp
        src/client/audio/eff_loader.cpp
        src/client/audio/zone_sound_emitter.cpp
        src/client/audio/zone_audio_manager.cpp
        src/client/audio/door_sounds.cpp
        src/client/audio/combat_music.cpp
        src/client/audio/audio_mixer.cpp
        src/client/audio/audio_backend.cpp
        src/client/audio/midi_player.cpp
        src/client/audio/sfx_manager.cpp
    )
ENDIF()

# All core sources (for library - excludes main.cpp)
SET(ALL_CORE_SOURCES
    ${COMMON_SOURCES}
    ${CLIENT_CORE_SOURCES}
    ${PATHFINDING_SOURCES}
    ${MAP_SOURCES}
    ${GRAPHICS_SOURCES}
    ${RDP_SOURCES}
    ${AUDIO_SOURCES}
)

# Create a static library for the core client code (used by both executable and tests)
ADD_LIBRARY(willeq_core STATIC ${ALL_CORE_SOURCES})
# TextureAtlas requires GL - only available in main library, not standalone tools
TARGET_COMPILE_DEFINITIONS(willeq_core PRIVATE EQT_HAS_TEXTURE_ATLAS)
# SQLite for WilleqDatabase
TARGET_INCLUDE_DIRECTORIES(willeq_core PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
TARGET_COMPILE_DEFINITIONS(willeq_core PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)

# Create the main executable
ADD_EXECUTABLE(willeq src/client/main.cpp)

# Link libraries
SET(LINK_LIBS
    ${OPENSSL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    Threads::Threads
    dl  # Required by SQLite
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
        # DRM/KMS/GBM/EGL (no X11) - for ARM boards with Lima/Panfrost, and for
        # H01's x86 GLES2 dev builds (EQT_DRM=OFF, EQT_GLES2=ON — CIrrDeviceFB
        # still needs the drm/gbm symbols even though it falls back to a
        # surfaceless EGL context at runtime when no DRM display is available).
        # P30: this is now the only non-APPLE path — the mandatory-Irrlicht
        # gate above guarantees EQT_DRM OR EQT_GLES2 whenever we get here; the
        # desktop X11/Xxf86vm/GL fallback branch was removed
        # (refactor/desktop_gl_elimination_plan.md §5/§6 Phase 3).
        IF(EQT_DRM)
            ADD_DEFINITIONS(-DEQT_HAS_DRM)
        ENDIF()
        # P32 (Phase 4, docs/decisions.md 2026-07-28 addendum): the
        # EQT_GLES2=OFF "Desktop GL mode via Lima" ELSE() branch (ARM board,
        # EQT_DRM=ON EQT_GLES2=OFF) was retired — GLES2 is the only backend
        # now, on ARM same as x86. scripts/build-arm-noble.sh no longer
        # offers a --no-gles2 flag to reach this configuration.
        IF(EQT_GLES2)
            # GLES2 mode: link GLESv2 only (no desktop GL)
            ADD_DEFINITIONS(-DEQT_HAS_GLES2)
            LIST(APPEND LINK_LIBS EGL GLESv2 drm gbm)
            # P25: X11 for the x86-only windowed EGL backend (--egl-x11).
            # ARM (EQT_DRM=ON) never links X11 - its CIrrDeviceFB.cpp is the
            # untouched shared DRM-only device with no X11 code at all.
            IF(NOT EQT_DRM)
                LIST(APPEND LINK_LIBS X11)
            ENDIF()
        ENDIF()
        # drm_fourcc.h is in a libdrm subdirectory — add include path
        FIND_PATH(LIBDRM_INCLUDE_DIR drm.h PATH_SUFFIXES libdrm drm)
        IF(LIBDRM_INCLUDE_DIR)
            INCLUDE_DIRECTORIES(${LIBDRM_INCLUDE_DIR})
        ENDIF()
    ENDIF()
ENDIF()

IF(WITH_RDP AND FREERDP_FOUND)
    LIST(APPEND LINK_LIBS ${FREERDP_LIBRARIES})
    # Add FreeRDP server library for RDPSND audio streaming
    LIST(APPEND LINK_LIBS freerdp-server3)
ENDIF()

IF(WITH_AUDIO)
    # miniaudio requires platform audio libraries
    IF(APPLE)
        FIND_LIBRARY(COREAUDIO_FRAMEWORK CoreAudio)
        FIND_LIBRARY(AUDIOUNIT_FRAMEWORK AudioUnit)
        FIND_LIBRARY(COREFOUNDATION_FRAMEWORK CoreFoundation)
        LIST(APPEND LINK_LIBS ${COREAUDIO_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
    ELSEIF(ANDROID)
        LIST(APPEND LINK_LIBS OpenSLES)
    ELSEIF(UNIX)
        LIST(APPEND LINK_LIBS dl m)
    ENDIF()
    # Windows: WASAPI linked automatically by miniaudio, no extra libs needed
ENDIF()

# Link libraries to the core library
TARGET_LINK_LIBRARIES(willeq_core ${LINK_LIBS})

# willeq executable links to the core library (which brings in all dependencies)
TARGET_LINK_LIBRARIES(willeq willeq_core ${LINK_LIBS})

# P50 evidence tool: exercises the two applyPaletteMask plumbing gaps closed
# by this unit (refactor/P50_mask_plumbing_gaps.md) directly against
# production RaceModelLoader/ConstrainedTextureCache code — no zone load
# needed. Links willeq_core (same as willeq itself) since it needs the full
# graphics stack.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(p50_mask_plumbing_probe tools/hwcaps/p50_mask_plumbing_probe.cpp)
    TARGET_LINK_LIBRARIES(p50_mask_plumbing_probe willeq_core ${LINK_LIBS})
ENDIF()

# T57: visual-acceptance capture harness for the inventory popup. No
# existing tool captures the static UI (--dump-png-prefix belongs to
# gles2_zone_benchmark, which renders zones, never a popup) — this is the
# smallest one that does, against a real headless GLES2 device (same
# surfaceless-EGL/llvmpipe backend as the visual gate). Links willeq_core
# for the same reason as p50_mask_plumbing_probe above.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(ui_inventory_capture tools/hwcaps/ui_inventory_capture.cpp)
    TARGET_LINK_LIBRARIES(ui_inventory_capture willeq_core ${LINK_LIBS})
ENDIF()

# P23: SimulationWorker input/output-buffer race stress harness. Manual
# tool (not ctest-registered — it's a bounded soak, not a fast deterministic
# unit test); see refactor/P23_particle_race_stability.md Review for the
# TSAN/ASAN invocation used to gather race evidence. Links against
# willeq_core directly since it needs SimulationWorker plus its full compute
# dependency set (portal_system, wld_loader, hc_map, weather_system, ...),
# which is simplest to pull in as the already-built core library rather than
# re-enumerating every TU.
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(particle_race_stress tools/particle_race_stress.cpp)
    TARGET_INCLUDE_DIRECTORIES(particle_race_stress PRIVATE ${CMAKE_SOURCE_DIR}/include)
    TARGET_LINK_LIBRARIES(particle_race_stress willeq_core ${LINK_LIBS})
ENDIF()

# Model Viewer tool (for debugging character models)
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(model_viewer
        tools/model_viewer.cpp
        tools/tool_race_db.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/eq/equipment_model_loader.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/model_loading_chr.cpp
        src/client/graphics/eq/model_loading_search.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/variant_loading_animation.cpp
        src/client/graphics/eq/variant_loading_preload.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/client/graphics/eq/animated_mesh_eye.cpp
        src/client/graphics/first_person_eye.cpp
        src/client/graphics/graphics_archive_index.cpp
        src/common/performance_metrics.cpp
        # SQLite database (required by spell_database, race_codes, equipment_model_loader)
        src/client/willeq_database.cpp
        third_party/sqlite3.c
        # Spell test mode dependencies
        src/client/spell/spell_database.cpp
        src/client/spell/spells_eff_reader.cpp
        src/client/graphics/spell_visual_fx.cpp
        src/client/graphics/spell_visual_fx_nodes.cpp
        src/client/graphics/spell_visual_selection.cpp
        # Particle system (required for GLES2 spell effects via ParticleManager)
        src/client/graphics/environment/particle_manager.cpp
        src/client/graphics/environment/particle_emitter.cpp
        src/client/graphics/environment/unified_particle_renderer.cpp
        src/client/graphics/environment/fire_emitter_config.cpp
        src/client/graphics/environment/weather_emitter_config.cpp
        src/client/graphics/environment/spell_emitter_presets.cpp
        src/client/graphics/environment/spell_effects_config.cpp
        src/client/graphics/environment/environment_config.cpp
        src/client/graphics/environment/zone_biome.cpp
        src/client/graphics/environment/emitters/dust_mote_emitter.cpp
        src/client/graphics/environment/emitters/pollen_emitter.cpp
        src/client/graphics/environment/emitters/firefly_emitter.cpp
        src/client/graphics/environment/emitters/mist_emitter.cpp
        src/client/graphics/environment/emitters/sand_dust_emitter.cpp
        src/client/graphics/environment/emitters/ember_emitter.cpp
        src/client/graphics/environment/emitters/smoke_emitter.cpp
    )
    # Background thread pool (needed by constrained_texture_cache)
    TARGET_SOURCES(model_viewer PRIVATE src/client/graphics/background_thread_pool.cpp)
    # GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(model_viewer PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(model_viewer PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
        ${IRRLICHT_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIR}
    )
    TARGET_COMPILE_DEFINITIONS(model_viewer PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
    # T270: race lookups are db-only; give the tool the repo's db path as a
    # fallback for runs whose CWD is not the repo root (tools/tool_race_db.cpp).
    TARGET_COMPILE_DEFINITIONS(model_viewer PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
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
        # P30: this is now the only non-APPLE path (desktop X11/Xxf86vm/GL
        # ELSE() removed — refactor/desktop_gl_elimination_plan.md §6 Phase 3;
        # the mandatory-Irrlicht gate guarantees EQT_DRM OR EQT_GLES2 here).
        # P26: EQT_DRM OR EQT_GLES2 (not EQT_DRM alone) — the x86 GLES2 dev
        # tree (EQT_DRM=OFF, EQT_GLES2=ON) was falling into the (now-removed)
        # desktop X11/GL branch and failing to link against the static
        # GLES2-patched Irrlicht archive (missing eglGetError/gbm symbols).
        IF(EQT_GLES2)
            TARGET_LINK_LIBRARIES(model_viewer
                ${IRRLICHT_LIBRARY}
                ${ZLIB_LIBRARIES}
                ${JSONCPP_LIBRARIES}
                fmt::fmt
                EGL GLESv2 drm gbm
            )
            # P27: x86 GLES2 dev tree (EQT_DRM=OFF) needs X11 too — the P25
            # windowed-EGL backend in CIrrDeviceFB.cpp references X11 symbols
            # unconditionally (used only at runtime behind --egl-x11, but the
            # static archive still needs the symbols resolved at link time).
            # Same parity gap as the willeq_core LINK_LIBS block above
            # (line ~538) — P26 copied the EGL/GLESv2/drm/gbm set to this
            # target but missed X11, discovered by P27 while verifying the
            # GLES2 tree's full ctest baseline.
            IF(NOT EQT_DRM)
                TARGET_LINK_LIBRARIES(model_viewer X11)
            ENDIF()
        ELSE()
            TARGET_LINK_LIBRARIES(model_viewer
                ${IRRLICHT_LIBRARY}
                ${ZLIB_LIBRARIES}
                ${JSONCPP_LIBRARIES}
                fmt::fmt
                EGL GL drm gbm
            )
        ENDIF()
    ENDIF()
ENDIF()

# generate_textures tool RETIRED per Will's decision (2026-07-28, P26):
# offline procedural texture generator, no found consumer (§3 tools table,
# refactor/desktop_gl_elimination_plan.md). Source moved to
# tools/retired/generate_textures.cpp; git history has the buildable version.

# S3D Dump tool (for analyzing S3D archive contents)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(s3d_dump
        tools/s3d_dump.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/background_thread_pool.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(s3d_dump PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(s3d_dump PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(s3d_dump
        ${ZLIB_LIBRARIES}
        ${IRRLICHT_LIBRARY}
        ${JSONCPP_LIBRARIES}
        fmt::fmt
    )
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(s3d_dump EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment above (same P25/P26 parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(s3d_dump X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — the
    # mandatory-Irrlicht gate guarantees EQT_DRM OR EQT_GLES2 whenever
    # IRRLICHT_LIBRARY is set (refactor/desktop_gl_elimination_plan.md §6
    # Phase 3).

    # uv_analysis tool RETIRED per Will's decision (2026-07-28, P26): no live
    # usage found beyond a prior bug report (§3 tools table,
    # refactor/desktop_gl_elimination_plan.md). Source moved to
    # tools/retired/uv_analysis.cpp; git history has the buildable version.

    # Entity Composite Texture Builder (offline composite sheet generation for single-draw entity rendering)
    ADD_EXECUTABLE(entity_composite_builder
        tools/entity_composite_builder.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/background_thread_pool.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(entity_composite_builder PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(entity_composite_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(entity_composite_builder
        ${ZLIB_LIBRARIES}
        ${IRRLICHT_LIBRARY}
        ${JSONCPP_LIBRARIES}
        fmt::fmt
    )
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(entity_composite_builder EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment above (same P25/P26 parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(entity_composite_builder X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — see the
    # s3d_dump comment above (same mandatory-Irrlicht gate).

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

    # PFS Verify tool (for verifying PFS/S3D archive parsing with JSON output)
    ADD_EXECUTABLE(pfs_verify
        tools/pfs_verify.cpp
        src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(pfs_verify PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(pfs_verify
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    # WLD Dump tool (for analyzing WLD file contents)
    ADD_EXECUTABLE(wld_dump
        tools/wld_dump.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
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
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
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

    ADD_EXECUTABLE(zone_region_classifier
        tools/zone_region_classifier.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/portal_system.cpp
        src/client/graphics/portal_geometry_clipper.cpp
        src/client/hc_map.cpp
        src/client/raycast_mesh.cpp
        src/common/util/compression.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(zone_region_classifier PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIR}
    )
    TARGET_LINK_LIBRARIES(zone_region_classifier
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    ADD_EXECUTABLE(region_poly_audit
        tools/region_poly_audit.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(region_poly_audit PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(region_poly_audit
        ${ZLIB_LIBRARIES}
        fmt::fmt
    )

    # analyze_hum_model, test_animation, extract_zone_lines,
    # bsp_coordinate_finder tools RETIRED per Will's decision (2026-07-28,
    # P26): no live usage found beyond a prior bug report (§3 tools table,
    # refactor/desktop_gl_elimination_plan.md). Sources moved to
    # tools/retired/; git history has the buildable versions.

    # Surface Map Generator tool (for pre-computing surface type maps)
    ADD_EXECUTABLE(generate_surface_map
        tools/generate_surface_map.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/background_thread_pool.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(generate_surface_map PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(generate_surface_map PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(generate_surface_map
        ${ZLIB_LIBRARIES}
        ${IRRLICHT_LIBRARY}
        ${JSONCPP_LIBRARIES}
        fmt::fmt
    )
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(generate_surface_map EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment above (same P25/P26 parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(generate_surface_map X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — see the
    # s3d_dump comment above (same mandatory-Irrlicht gate).

    # Surface Map Query tool (for debugging surface maps)
    ADD_EXECUTABLE(query_surface_map
        tools/query_surface_map.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(query_surface_map PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    # Zone Line Editor tool (2D editor for marking zone line boundaries)
    ADD_EXECUTABLE(zone_line_editor
        tools/zone_line_editor.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/load_mem_audit.cpp
        src/client/graphics/background_thread_pool.cpp
        src/client/graphics/portal_system.cpp
        src/client/graphics/portal_geometry_clipper.cpp
        src/client/hc_map.cpp
        src/client/raycast_mesh.cpp
        src/common/util/compression.cpp
    )
    # P26: GPU upload thread (GLES2 only — needed by constrained_texture_cache)
    IF(EQT_GLES2)
        TARGET_SOURCES(zone_line_editor PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(zone_line_editor PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
        ${IRRLICHT_INCLUDE_DIR}
        ${JSONCPP_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIR}
    )
    TARGET_LINK_LIBRARIES(zone_line_editor
        ${ZLIB_LIBRARIES}
        ${IRRLICHT_LIBRARY}
        ${JSONCPP_LIBRARIES}
        fmt::fmt
    )
    IF(EQT_DRM OR EQT_GLES2)
        # P26: x86 GLES2 dev tree (EQT_DRM=OFF, EQT_GLES2=ON) needs these too —
        # CIrrDeviceFB/gpu_upload_thread reference drm/gbm/EGL symbols the
        # static GLES2-patched Irrlicht archive doesn't bring in transitively.
        TARGET_LINK_LIBRARIES(zone_line_editor EGL GLESv2 drm gbm)
        # P27: x86 GLES2 dev tree (EQT_DRM=OFF) also needs X11 — see the
        # model_viewer comment above (same P25/P26 parity gap).
        IF(NOT EQT_DRM)
            TARGET_LINK_LIBRARIES(zone_line_editor X11)
        ENDIF()
    ENDIF()
    # P30: desktop X11/Xxf86vm/GL ELSEIF(NOT APPLE) branch removed — see the
    # s3d_dump comment above (same mandatory-Irrlicht gate).
ENDIF()

# Zone Atlas Builder tool (offline ETC1 texture atlas generation)
IF(EQT_GRAPHICS)
    INCLUDE(FetchContent)
    FetchContent_Declare(
        etc2comp
        GIT_REPOSITORY https://github.com/google/etc2comp.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(etc2comp)

    # WillEQ Database Builder tool (offline SQLite database generation)
    ADD_EXECUTABLE(willeq_db_builder
        tools/willeq_db_builder.cpp
        third_party/sqlite3.c
    )
    TARGET_INCLUDE_DIRECTORIES(willeq_db_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/include
    )
    TARGET_LINK_LIBRARIES(willeq_db_builder dl pthread)
    TARGET_COMPILE_DEFINITIONS(willeq_db_builder PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION
    )

    # Equipment Index Builder tool (offline equipment model index from *equip.s3d)
    # T207: name -> owning-archive index for an EQE1 tree. Separate target from
    # equipment_index_builder on purpose -- different question, different output,
    # different consumer (see the tool's header comment).
    ADD_EXECUTABLE(eqe1_texture_index_builder
        tools/eqe1_texture_index_builder.cpp
        src/client/graphics/eqe1_loader.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(eqe1_texture_index_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    IF(fmt_FOUND)
        TARGET_LINK_LIBRARIES(eqe1_texture_index_builder ${ZLIB_LIBRARIES} fmt::fmt)
    ELSE()
        TARGET_LINK_LIBRARIES(eqe1_texture_index_builder ${ZLIB_LIBRARIES} ${FMT_LIBRARY})
    ENDIF()

    ADD_EXECUTABLE(equipment_index_builder
        tools/equipment_index_builder.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(equipment_index_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    IF(fmt_FOUND)
        TARGET_LINK_LIBRARIES(equipment_index_builder
            ${ZLIB_LIBRARIES}
            fmt::fmt
        )
    ELSE()
        TARGET_LINK_LIBRARIES(equipment_index_builder
            ${ZLIB_LIBRARIES}
            ${FMT_LIBRARY}
        )
    ENDIF()

    # S3D Index Builder tool (offline asset index generation)
    ADD_EXECUTABLE(s3d_index_builder
        tools/s3d_index_builder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(s3d_index_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${ZLIB_INCLUDE_DIRS}
    )
    TARGET_LINK_LIBRARIES(s3d_index_builder
        ${ZLIB_LIBRARIES}
    )

    ADD_EXECUTABLE(zone_atlas_builder
        tools/zone_atlas_builder.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(zone_atlas_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
        ${etc2comp_SOURCE_DIR}/EtcLib/Etc
        ${etc2comp_SOURCE_DIR}/EtcLib/EtcCodec
    )
    IF(fmt_FOUND)
        TARGET_LINK_LIBRARIES(zone_atlas_builder
            ${ZLIB_LIBRARIES}
            fmt::fmt
            EtcLib
        )
    ELSE()
        TARGET_LINK_LIBRARIES(zone_atlas_builder
            ${ZLIB_LIBRARIES}
            ${FMT_LIBRARY}
            EtcLib
        )
    ENDIF()

    ADD_EXECUTABLE(eqe1_builder
        tools/eqe1_builder.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(eqe1_builder PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
        ${etc2comp_SOURCE_DIR}/EtcLib/Etc
        ${etc2comp_SOURCE_DIR}/EtcLib/EtcCodec
    )
    IF(fmt_FOUND)
        TARGET_LINK_LIBRARIES(eqe1_builder
            ${ZLIB_LIBRARIES}
            fmt::fmt
            EtcLib
        )
    ELSE()
        TARGET_LINK_LIBRARIES(eqe1_builder
            ${ZLIB_LIBRARIES}
            ${FMT_LIBRARY}
            EtcLib
        )
    ENDIF()

    # Add ETC1 support to entity_composite_builder (optional --eqe1 flag)
    TARGET_INCLUDE_DIRECTORIES(entity_composite_builder PRIVATE
        ${etc2comp_SOURCE_DIR}/EtcLib/Etc
        ${etc2comp_SOURCE_DIR}/EtcLib/EtcCodec
    )
    TARGET_COMPILE_DEFINITIONS(entity_composite_builder PRIVATE EQT_HAS_ETC2COMP)
    TARGET_LINK_LIBRARIES(entity_composite_builder EtcLib)

    ADD_EXECUTABLE(verify_eqe1
        tools/verify_eqe1.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(verify_eqe1 PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
ENDIF()

# U02: UI atlas builder tool
ADD_EXECUTABLE(ui_atlas_builder
    tools/ui_atlas_builder.cpp
)

# Atlas UV audit tool (check which textures can be atlased)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(atlas_uv_audit
        tools/atlas_uv_audit.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/zone_bsp.cpp
        src/client/graphics/eq/dds_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(atlas_uv_audit PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${ZLIB_INCLUDE_DIRS}
    )
    IF(fmt_FOUND)
        TARGET_LINK_LIBRARIES(atlas_uv_audit ${ZLIB_LIBRARIES} fmt::fmt)
    ELSE()
        TARGET_LINK_LIBRARIES(atlas_uv_audit ${ZLIB_LIBRARIES} ${FMT_LIBRARY})
    ENDIF()
ENDIF()

# Hardware capability test tools
# These are lightweight GPU probes (no Irrlicht dependency) for testing
# hardware features on constrained devices like Orange Pi.
ADD_EXECUTABLE(gpu_texture_formats tools/hwcaps/gpu_texture_formats.cpp)
TARGET_INCLUDE_DIRECTORIES(gpu_texture_formats PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gpu_texture_formats PRIVATE EQT_HAS_DRM)
    FIND_PATH(LIBDRM_INCLUDE_DIR drm.h PATH_SUFFIXES libdrm)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gpu_texture_formats PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gpu_texture_formats EGL GL drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gpu_texture_formats EGL GL)
ENDIF()

ADD_EXECUTABLE(gles2_etc1_benchmark tools/hwcaps/gles2_etc1_benchmark.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_etc1_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_etc1_benchmark PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_etc1_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_etc1_benchmark EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_etc1_benchmark EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_etc1_alpha_readback_test
    tools/hwcaps/gles2_etc1_alpha_readback_test.cpp
    src/client/graphics/eqe1_loader.cpp
)
TARGET_INCLUDE_DIRECTORIES(gles2_etc1_alpha_readback_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_etc1_alpha_readback_test PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_etc1_alpha_readback_test PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_etc1_alpha_readback_test EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_etc1_alpha_readback_test EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_shader_perpixel_benchmark tools/hwcaps/gles2_shader_perpixel_benchmark.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_shader_perpixel_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_shader_perpixel_benchmark PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_shader_perpixel_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_shader_perpixel_benchmark EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_shader_perpixel_benchmark EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_program_switch_benchmark tools/hwcaps/gles2_program_switch_benchmark.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_program_switch_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_program_switch_benchmark PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_program_switch_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_program_switch_benchmark EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_program_switch_benchmark EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_fog_volume_benchmark tools/hwcaps/gles2_fog_volume_benchmark.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_fog_volume_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_fog_volume_benchmark PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_fog_volume_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_fog_volume_benchmark EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_fog_volume_benchmark EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_icosphere_anim_benchmark tools/hwcaps/gles2_icosphere_anim_benchmark.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_icosphere_anim_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_icosphere_anim_benchmark PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_icosphere_anim_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_icosphere_anim_benchmark EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_icosphere_anim_benchmark EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_texture_read_benchmark tools/hwcaps/gles2_texture_read_benchmark.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_texture_read_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_texture_read_benchmark PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_texture_read_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_texture_read_benchmark EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_texture_read_benchmark EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_fog_visual_compare tools/hwcaps/gles2_fog_visual_compare.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_fog_visual_compare PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_fog_visual_compare PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_fog_visual_compare PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_fog_visual_compare EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_fog_visual_compare EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_derivatives_test tools/hwcaps/gles2_derivatives_test.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_derivatives_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_derivatives_test PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_derivatives_test PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_derivatives_test EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_derivatives_test EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(gles2_npot_test tools/hwcaps/gles2_npot_test.cpp)
TARGET_INCLUDE_DIRECTORIES(gles2_npot_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(gles2_npot_test PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_npot_test PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_npot_test EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(gles2_npot_test EGL GLESv2)
ENDIF()

ADD_EXECUTABLE(egl_image_sharing_test tools/hwcaps/egl_image_sharing_test.cpp)
TARGET_INCLUDE_DIRECTORIES(egl_image_sharing_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(egl_image_sharing_test PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(egl_image_sharing_test PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(egl_image_sharing_test EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(egl_image_sharing_test EGL GLESv2)
ENDIF()

# Zone benchmark — offline zone rendering with real S3D/WLD pipeline
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(gles2_zone_benchmark
        tools/hwcaps/gles2_zone_benchmark.cpp
        tools/hwcaps/zone_bench_memory.cpp
        tools/hwcaps/zone_bench_loader.cpp
        tools/hwcaps/zone_bench_camera.cpp
        tools/hwcaps/zone_bench_entity_viewer.cpp
        tools/hwcaps/zone_bench_metrics.cpp
        tools/hwcaps/zone_bench_histogram.cpp
        tools/hwcaps/zone_bench_merge_probe.cpp
        tools/hwcaps/zone_bench_merge.cpp
        tools/tool_race_db.cpp
        # Zone loading pipeline (same sources as zone_line_editor + extras)
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/eqe1_texture_index.cpp
        src/client/graphics/load_mem_audit.cpp
        src/client/graphics/background_thread_pool.cpp
        src/client/graphics/portal_system.cpp
        src/client/graphics/portal_geometry_clipper.cpp
        src/client/graphics/frustum_culler.cpp
        src/client/hc_map.cpp
        src/client/raycast_mesh.cpp
        src/client/graphics/zone_shader.cpp
        src/client/graphics/texture_atlas.cpp
        src/client/graphics/software_occlusion_culler.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/model_loading_chr.cpp
        src/client/graphics/eq/model_loading_search.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/variant_loading_animation.cpp
        src/client/graphics/eq/variant_loading_preload.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/client/graphics/eq/animated_mesh_eye.cpp
        src/client/graphics/first_person_eye.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/graphics_archive_index.cpp
        src/common/performance_metrics.cpp
        src/client/willeq_database.cpp
        third_party/sqlite3.c
        src/client/zone_bsp.cpp
        src/common/util/compression.cpp
    )
    # GPU upload thread (GLES2 only — needed by texture_atlas.cpp)
    IF(EQT_GLES2)
        TARGET_SOURCES(gles2_zone_benchmark PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    # Optionally add navmesh source
    IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
        TARGET_SOURCES(gles2_zone_benchmark PRIVATE src/client/pathfinder_nav_mesh.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(gles2_zone_benchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
        ${ZLIB_INCLUDE_DIRS}
        ${IRRLICHT_INCLUDE_DIR}
        ${JSONCPP_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIR}
    )
    TARGET_COMPILE_DEFINITIONS(gles2_zone_benchmark PRIVATE EQT_HAS_TEXTURE_ATLAS SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
    # T270: race lookups are db-only; give the tool the repo's db path as a
    # fallback for runs whose CWD is not the repo root.
    TARGET_COMPILE_DEFINITIONS(gles2_zone_benchmark PRIVATE
        WILLEQ_DB_REAL_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    # T284: same fallback for the EQE1 name->pack index, which the composite
    # path now resolves through instead of scanning the sheet directory.
    TARGET_COMPILE_DEFINITIONS(gles2_zone_benchmark PRIVATE
        WILLEQ_EQE1_INDEX_REAL_PATH="${CMAKE_SOURCE_DIR}/data/eqe1_texture_index.json")
    TARGET_INCLUDE_DIRECTORIES(gles2_zone_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/tools)
    IF(EQT_DRM)
        TARGET_COMPILE_DEFINITIONS(gles2_zone_benchmark PRIVATE EQT_HAS_DRM)
    ENDIF()
    IF((EQT_DRM OR EQT_GLES2) AND LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(gles2_zone_benchmark PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    IF(EQT_GLES2)
        TARGET_COMPILE_DEFINITIONS(gles2_zone_benchmark PRIVATE EQT_HAS_GLES2)
        # P38: gl_call_counters.h (driver-side glBindTexture/glTexParameteri
        # counters, disabled by default) -- header-only, shared with the
        # patched Irrlicht copy at docker/irrlicht-drm/COpenGLES2Driver.cpp.
        TARGET_INCLUDE_DIRECTORIES(gles2_zone_benchmark PRIVATE
            ${CMAKE_SOURCE_DIR}/docker/irrlicht-drm)
    ENDIF()
    SET(ZONE_BENCH_LIBS
        ${ZLIB_LIBRARIES}
        ${IRRLICHT_LIBRARY}
        ${JSONCPP_LIBRARIES}
        fmt::fmt
    )
    IF(EQT_DRM OR EQT_GLES2)
        # H01: x86 GLES2 dev builds have EQT_DRM=OFF, EQT_GLES2=ON — CIrrDeviceFB
        # still references drm/gbm symbols even with the surfaceless fallback.
        LIST(APPEND ZONE_BENCH_LIBS EGL GLESv2 drm gbm)
        # P25: x86-only CIrrDeviceFB.cpp always references X11 symbols now
        # (the windowed EGL backend is compiled in even though it's an
        # opt-in runtime path) - link X11 whenever EQT_GLES2 without
        # EQT_DRM, same condition as the main willeq target.
        IF(EQT_GLES2 AND NOT EQT_DRM)
            LIST(APPEND ZONE_BENCH_LIBS X11)
        ENDIF()
    ENDIF()
    # P30: desktop-GL-only ELSE() (plain "GL" for texture_atlas.cpp
    # glGenTextures etc.) removed — the mandatory-Irrlicht gate guarantees
    # EQT_DRM OR EQT_GLES2 whenever IRRLICHT_LIBRARY is set
    # (refactor/desktop_gl_elimination_plan.md §5/§6 Phase 3).
    IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
        LIST(APPEND ZONE_BENCH_LIBS ${RECAST_LIBRARY} ${DETOUR_LIBRARY})
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_zone_benchmark ${ZONE_BENCH_LIBS})

    # Shader diagnostic tool — renders test modules through all debug shader modes
    ADD_EXECUTABLE(gles2_shader_diagnostic
        tools/hwcaps/gles2_shader_diagnostic.cpp
        tools/hwcaps/zone_bench_loader.cpp
        tools/hwcaps/zone_bench_camera.cpp
        tools/hwcaps/zone_bench_metrics.cpp
        tools/hwcaps/zone_bench_histogram.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/eqe1_texture_index.cpp
        src/client/graphics/load_mem_audit.cpp
        src/client/graphics/background_thread_pool.cpp
        src/client/graphics/portal_system.cpp
        src/client/graphics/portal_geometry_clipper.cpp
        src/client/graphics/frustum_culler.cpp
        src/client/hc_map.cpp
        src/client/raycast_mesh.cpp
        src/client/graphics/zone_shader.cpp
        src/client/graphics/texture_atlas.cpp
        src/client/graphics/software_occlusion_culler.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/model_loading_chr.cpp
        src/client/graphics/eq/model_loading_search.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/variant_loading_animation.cpp
        src/client/graphics/eq/variant_loading_preload.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/client/graphics/eq/animated_mesh_eye.cpp
        src/client/graphics/first_person_eye.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/graphics_archive_index.cpp
        src/common/performance_metrics.cpp
        src/client/willeq_database.cpp
        third_party/sqlite3.c
        src/client/zone_bsp.cpp
        src/common/util/compression.cpp
    )
    IF(EQT_GLES2)
        TARGET_SOURCES(gles2_shader_diagnostic PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
        TARGET_SOURCES(gles2_shader_diagnostic PRIVATE src/client/pathfinder_nav_mesh.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(gles2_shader_diagnostic PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
        ${ZLIB_INCLUDE_DIRS}
        ${IRRLICHT_INCLUDE_DIR}
        ${JSONCPP_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIR}
    )
    TARGET_COMPILE_DEFINITIONS(gles2_shader_diagnostic PRIVATE EQT_HAS_TEXTURE_ATLAS SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
    IF(EQT_DRM)
        TARGET_COMPILE_DEFINITIONS(gles2_shader_diagnostic PRIVATE EQT_HAS_DRM)
        IF(LIBDRM_INCLUDE_DIR)
            TARGET_INCLUDE_DIRECTORIES(gles2_shader_diagnostic PRIVATE ${LIBDRM_INCLUDE_DIR})
        ENDIF()
    ENDIF()
    IF(EQT_GLES2)
        TARGET_COMPILE_DEFINITIONS(gles2_shader_diagnostic PRIVATE EQT_HAS_GLES2)
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_shader_diagnostic ${ZONE_BENCH_LIBS})
    # T284: shares zone_bench_loader.cpp's index-driven composite open.
    TARGET_COMPILE_DEFINITIONS(gles2_shader_diagnostic PRIVATE
        WILLEQ_EQE1_INDEX_REAL_PATH="${CMAKE_SOURCE_DIR}/data/eqe1_texture_index.json")

    # Composite entity audit tool — renders all race/gender/variant combos as PNGs
    ADD_EXECUTABLE(gles2_composite_audit
        tools/hwcaps/gles2_composite_audit.cpp
        tools/hwcaps/zone_bench_loader.cpp
        tools/hwcaps/zone_bench_camera.cpp
        tools/hwcaps/zone_bench_metrics.cpp
        tools/hwcaps/zone_bench_histogram.cpp
        src/client/graphics/eq/pfs.cpp
        src/client/graphics/eq/wld_loader.cpp
        src/client/graphics/eq/wld_fragments_material.cpp
        src/client/graphics/eq/wld_fragments_mesh.cpp
        src/client/graphics/eq/wld_fragments_actor.cpp
        src/client/graphics/eq/wld_fragments_skeleton.cpp
        src/client/graphics/eq/wld_fragments_light.cpp
        src/client/graphics/eq/wld_fragments_bsp.cpp
        src/client/graphics/eq/s3d_loader.cpp
        src/client/graphics/eq/zone_geometry.cpp
        src/client/graphics/eq/zone_geometry_atlased.cpp
        src/client/graphics/eq/dds_decoder.cpp
        src/client/graphics/constrained_renderer_config.cpp
        src/client/graphics/constrained_texture_cache.cpp
        src/client/graphics/constrained_texture_cache_upload_queue.cpp
        src/client/graphics/constrained_texture_cache_eviction.cpp
        src/client/graphics/constrained_texture_cache_compressed.cpp
        src/client/graphics/texture_upload_ledger.cpp
        src/client/graphics/eqe1_loader.cpp
        src/client/graphics/eqe1_texture_index.cpp
        src/client/graphics/load_mem_audit.cpp
        src/client/graphics/background_thread_pool.cpp
        src/client/graphics/portal_system.cpp
        src/client/graphics/portal_geometry_clipper.cpp
        src/client/graphics/frustum_culler.cpp
        src/client/hc_map.cpp
        src/client/raycast_mesh.cpp
        src/client/graphics/zone_shader.cpp
        src/client/graphics/texture_atlas.cpp
        src/client/graphics/software_occlusion_culler.cpp
        src/client/graphics/eq/race_model_loader.cpp
        src/client/graphics/eq/model_loading.cpp
        src/client/graphics/eq/model_loading_chr.cpp
        src/client/graphics/eq/model_loading_search.cpp
        src/client/graphics/eq/mesh_building.cpp
        src/client/graphics/eq/variant_loading.cpp
        src/client/graphics/eq/variant_loading_animation.cpp
        src/client/graphics/eq/variant_loading_preload.cpp
        src/client/graphics/eq/geometry_combiner.cpp
        src/client/graphics/eq/race_codes.cpp
        src/client/graphics/eq/animation_mapping.cpp
        src/client/graphics/eq/animated_mesh_creation.cpp
        src/client/graphics/eq/animated_mesh_scene_node.cpp
        src/client/graphics/eq/animated_mesh_eye.cpp
        src/client/graphics/first_person_eye.cpp
        src/client/graphics/eq/skeletal_animator.cpp
        src/client/graphics/eq/equipment_textures.cpp
        src/client/graphics/graphics_archive_index.cpp
        src/common/performance_metrics.cpp
        src/client/willeq_database.cpp
        third_party/sqlite3.c
        src/client/zone_bsp.cpp
        src/common/util/compression.cpp
    )
    IF(EQT_GLES2)
        TARGET_SOURCES(gles2_composite_audit PRIVATE src/client/graphics/gpu_upload_thread.cpp)
    ENDIF()
    IF(RECAST_LIBRARY AND DETOUR_LIBRARY)
        TARGET_SOURCES(gles2_composite_audit PRIVATE src/client/pathfinder_nav_mesh.cpp)
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(gles2_composite_audit PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/third_party
        ${ZLIB_INCLUDE_DIRS}
        ${IRRLICHT_INCLUDE_DIR}
        ${JSONCPP_INCLUDE_DIRS}
        ${GLM_INCLUDE_DIR}
    )
    TARGET_COMPILE_DEFINITIONS(gles2_composite_audit PRIVATE EQT_HAS_TEXTURE_ATLAS SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
    IF(EQT_DRM)
        TARGET_COMPILE_DEFINITIONS(gles2_composite_audit PRIVATE EQT_HAS_DRM)
        IF(LIBDRM_INCLUDE_DIR)
            TARGET_INCLUDE_DIRECTORIES(gles2_composite_audit PRIVATE ${LIBDRM_INCLUDE_DIR})
        ENDIF()
    ENDIF()
    IF(EQT_GLES2)
        TARGET_COMPILE_DEFINITIONS(gles2_composite_audit PRIVATE EQT_HAS_GLES2)
    ENDIF()
    TARGET_LINK_LIBRARIES(gles2_composite_audit ${ZONE_BENCH_LIBS})
    # T284: shares zone_bench_loader.cpp's index-driven composite open.
    TARGET_COMPILE_DEFINITIONS(gles2_composite_audit PRIVATE
        WILLEQ_EQE1_INDEX_REAL_PATH="${CMAKE_SOURCE_DIR}/data/eqe1_texture_index.json")
ENDIF()

ADD_EXECUTABLE(test_gl_points tools/test_gl_points.cpp)
TARGET_INCLUDE_DIRECTORIES(test_gl_points PRIVATE ${CMAKE_SOURCE_DIR}/include)
IF(EQT_DRM)
    TARGET_COMPILE_DEFINITIONS(test_gl_points PRIVATE EQT_HAS_DRM)
    IF(LIBDRM_INCLUDE_DIR)
        TARGET_INCLUDE_DIRECTORIES(test_gl_points PRIVATE ${LIBDRM_INCLUDE_DIR})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_gl_points EGL GLESv2 drm gbm)
ELSE()
    TARGET_LINK_LIBRARIES(test_gl_points EGL GLESv2)
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
    IF(EQT_DRM AND EQT_GLES2)
        MESSAGE(STATUS "Display backend: DRM/KMS/GBM/EGL + OpenGL ES 2.0 (no X11, no desktop GL)")
    ELSEIF(EQT_DRM)
        MESSAGE(STATUS "Display backend: DRM/KMS/GBM/EGL + desktop GL 2.1 (no X11)")
    ELSEIF(EQT_GLES2)
        # P30 cosmetic fix (roadmap §1): this branch (EQT_DRM=OFF,
        # EQT_GLES2=ON — the x86 GLES2 dev tree) used to fall into the bare
        # ELSE() below and misreport "X11/GLX", even though the actual
        # LINK_LIBS logic (EQT_GLES2 branch above) links EGL/GLESv2/drm/gbm
        # (+X11 only for the optional windowed --egl-x11 backend). Was never
        # a functional defect, log-message only.
        MESSAGE(STATUS "Display backend: EGL + OpenGL ES 2.0 (surfaceless llvmpipe, or windowed via --egl-x11; no desktop GL)")
    ELSE()
        # P30: desktop GL (X11/GLX) is no longer a reachable configuration —
        # the mandatory-Irrlicht gate (above) requires EQT_DRM or EQT_GLES2
        # whenever EQT_GRAPHICS=ON. This branch is dead but left as a
        # defensive default rather than deleted outright.
        MESSAGE(STATUS "Display backend: UNKNOWN (unreachable — EQT_GRAPHICS=ON requires EQT_DRM or EQT_GLES2)")
    ENDIF()
ELSE()
    MESSAGE(STATUS "Irrlicht: NOT FOUND (graphics disabled)")
ENDIF()
IF(WITH_RDP)
    MESSAGE(STATUS "FreeRDP: ${FREERDP_LIBRARIES} (native RDP enabled)")
ELSEIF(EQT_GRAPHICS AND FREERDP_FOUND)
    MESSAGE(STATUS "FreeRDP: available but disabled (use -DWITH_RDP=ON to enable)")
ELSEIF(EQT_GRAPHICS)
    MESSAGE(STATUS "FreeRDP: NOT FOUND (native RDP disabled)")
ELSE()
    MESSAGE(STATUS "FreeRDP: NOT AVAILABLE (requires graphics)")
ENDIF()
IF(WITH_AUDIO)
    MESSAGE(STATUS "Audio: enabled (miniaudio + TinySoundFont + dr_wav)")
ELSE()
    MESSAGE(STATUS "Audio: disabled (use -DWITH_AUDIO=ON to enable)")
ENDIF()
MESSAGE(STATUS "")
