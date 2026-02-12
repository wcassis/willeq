# Tests CMakeLists.txt

# Include Google Test
INCLUDE(GoogleTest)

# Common include directories for tests
INCLUDE_DIRECTORIES(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Helper variable for fmt library
IF(fmt_FOUND)
    SET(FMT_LIB fmt::fmt)
ELSE()
    SET(FMT_LIB ${FMT_LIBRARY})
ENDIF()

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

# Test timer
ADD_EXECUTABLE(test_timer
    test_timer.cpp
    ${CMAKE_SOURCE_DIR}/src/common/event/timer.cpp
)
TARGET_LINK_LIBRARIES(test_timer GTest::gtest_main ${LIBUV_LIBRARY})
GTEST_DISCOVER_TESTS(test_timer)

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

# Integration test - network protocol
ADD_EXECUTABLE(test_integration_network
    test_integration_network.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/packet.cpp
    ${CMAKE_SOURCE_DIR}/src/common/net/crc32.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_integration_network GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_integration_network)

# Test Titanium opcodes are correctly hard-coded
ADD_EXECUTABLE(test_titanium_opcodes
    test_titanium_opcodes.cpp
)
TARGET_LINK_LIBRARIES(test_titanium_opcodes GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_titanium_opcodes)

# Test combat enums and structures
ADD_EXECUTABLE(test_combat
    test_combat.cpp
)
TARGET_LINK_LIBRARIES(test_combat GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_combat)

# Test raycast mesh and map loading
ADD_EXECUTABLE(test_raycast
    test_raycast.cpp
    ${CMAKE_SOURCE_DIR}/src/client/raycast_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/client/hc_map.cpp
    ${CMAKE_SOURCE_DIR}/src/common/util/compression.cpp
)
TARGET_LINK_LIBRARIES(test_raycast GTest::gtest_main ${FMT_LIB} ${ZLIB_LIBRARIES})
GTEST_DISCOVER_TESTS(test_raycast)

# Test EQ client structs and enums
ADD_EXECUTABLE(test_eq_client
    test_eq_client.cpp
    ${CMAKE_SOURCE_DIR}/src/client/position.cpp
)
TARGET_LINK_LIBRARIES(test_eq_client GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_eq_client)

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
GTEST_DISCOVER_TESTS(test_pathfinder)

# Test packet structures (Titanium-specific)
ADD_EXECUTABLE(test_packet_structs
    test_packet_structs.cpp
)
TARGET_LINK_LIBRARIES(test_packet_structs GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_packet_structs)

# Test FormattedMessage parsing (link extraction)
ADD_EXECUTABLE(test_formatted_message
    test_formatted_message.cpp
    ${CMAKE_SOURCE_DIR}/src/client/formatted_message.cpp
)
TARGET_LINK_LIBRARIES(test_formatted_message GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_formatted_message)

# Test frustum culler (pure math, no external dependencies)
ADD_EXECUTABLE(test_frustum_culler
    test_frustum_culler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/frustum_culler.cpp
)
TARGET_LINK_LIBRARIES(test_frustum_culler GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_frustum_culler)

# Test StringDatabase loading and formatting
ADD_EXECUTABLE(test_string_database
    test_string_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/string_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/formatted_message.cpp
)
TARGET_LINK_LIBRARIES(test_string_database GTest::gtest_main ${FMT_LIB})
GTEST_DISCOVER_TESTS(test_string_database)

# Test spell database loading and querying
ADD_EXECUTABLE(test_spell_database
    test_spell_database.cpp
    ${CMAKE_SOURCE_DIR}/src/client/spell/spell_database.cpp
)
TARGET_LINK_LIBRARIES(test_spell_database GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_spell_database)

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
        test_wld.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
    )
    TARGET_LINK_LIBRARIES(test_wld GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_wld)
ENDIF()

# Test constrained renderer configuration (presets, resolution calculation, clamping)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_constrained_renderer
        test_constrained_renderer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
    )
    TARGET_LINK_LIBRARIES(test_constrained_renderer GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_constrained_renderer)
ENDIF()

# Test graphics components (PFS, DDS decoder, etc.)
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_graphics
        test_graphics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_graphics PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_graphics GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_graphics)

    # Test HotkeyManager (requires Irrlicht for key codes)
    ADD_EXECUTABLE(test_hotkey_manager
        test_hotkey_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/input/hotkey_manager.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_hotkey_manager PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_hotkey_manager GTest::gtest_main ${FMT_LIB} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_hotkey_manager)
ENDIF()

# Test asset loading (equipment, race models, textures, animations, lights)
# These tests verify EQ assets load without errors
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_asset_loading
        test_asset_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_codes.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/race_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/model_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/mesh_building.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_creation.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/variant_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/geometry_combiner.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_model_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/equipment_textures.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animation_mapping.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/skeletal_animator.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/animated_mesh_scene_node.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_asset_loading PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_asset_loading GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY} ${JSONCPP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_asset_loading)
ENDIF()

# Test door loading (unit tests + graphics tests)
# Unit tests run without display, graphics tests need DISPLAY=:99
IF(EQT_GRAPHICS)
    ADD_EXECUTABLE(test_door_loading
        test_door_loading.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/wld_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/s3d_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/zone_geometry.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/dds_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/door_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/common/performance_metrics.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_renderer_config.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/constrained_texture_cache.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_door_loading PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_door_loading GTest::gtest_main ${ZLIB_LIBRARIES} ${FMT_LIB} ${IRRLICHT_LIBRARY})
    GTEST_DISCOVER_TESTS(test_door_loading)
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
# Note: Not using GTEST_DISCOVER_TESTS because this requires a running server

# Integration test: Zoning with Graphics (connects to real EQEmu server with graphics enabled)
# This test is NOT auto-discovered because it requires a running server and X display.
# Run manually with: DISPLAY=:99 ./bin/test_zoning_graphics_integration
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_zoning_graphics_integration
        test_zoning_graphics_integration.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zoning_graphics_integration PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_zoning_graphics_integration GTest::gtest_main willeq_core)
    # Note: Not using GTEST_DISCOVER_TESTS because this requires a running server and display
ENDIF()

# Integration test: Inventory Window Model View (connects to real EQEmu server with graphics enabled)
# This test verifies the inventory window paperdoll character model loads, has textures, and animates.
# Run manually with: DISPLAY=:99 ./bin/test_inventory_model_view
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_inventory_model_view
        test_inventory_model_view.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_inventory_model_view PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_inventory_model_view GTest::gtest_main willeq_core)
    # Note: Not using GTEST_DISCOVER_TESTS because this requires a running server and display
ENDIF()

# Integration test: Beneficial Spell Casting (connects to real EQEmu server with graphics enabled)
# This test verifies beneficial spell casting works correctly - casting buffs on self and verifying
# they land properly. Tests casting multiple spells in series and buff refresh behavior.
# Run manually with: DISPLAY=:99 ./bin/test_beneficial_spell_casting
IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
    ADD_EXECUTABLE(test_beneficial_spell_casting
        test_beneficial_spell_casting.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_beneficial_spell_casting PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(test_beneficial_spell_casting GTest::gtest_main willeq_core)
    # Note: Not using GTEST_DISCOVER_TESTS because this requires a running server and display
ENDIF()

# Test XMI decoder (XMI to MIDI conversion)
# These tests verify XMI file decoding without requiring FluidSynth
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_xmi_decoder
        test_xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_xmi_decoder PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_xmi_decoder GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_xmi_decoder)
ENDIF()

# Test EFF file loader (zone sound emitter configuration)
# These tests verify _sounds.eff and _sndbnk.eff parsing
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_eff_loader
        test_eff_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_eff_loader PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_eff_loader GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_eff_loader)
ENDIF()

# Test zone sound emitters (positioned sound sources in zones)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_zone_sound_emitters
        test_zone_sound_emitters.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_sound_emitters PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_sound_emitters PRIVATE WITH_AUDIO)
    SET(ZONE_EMITTER_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_zone_sound_emitters PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND ZONE_EMITTER_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_zone_sound_emitters ${ZONE_EMITTER_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_zone_sound_emitters)
ENDIF()

# Test day/night audio system (Phase 13: game time based sound transitions)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_day_night_audio
        test_day_night_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_day_night_audio PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_day_night_audio PRIVATE WITH_AUDIO)
    SET(DAY_NIGHT_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_day_night_audio PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND DAY_NIGHT_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_day_night_audio ${DAY_NIGHT_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_day_night_audio)
ENDIF()

# Test zone music system (zone music mapping and transitions)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_zone_music
        test_zone_music.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_zone_music PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_music PRIVATE WITH_AUDIO)
    SET(ZONE_MUSIC_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_zone_music PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND ZONE_MUSIC_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_zone_music ${ZONE_MUSIC_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_zone_music)
ENDIF()

# Test sound assets (SoundAssets.txt parsing, WAV loading, AudioManager)
# These tests require OpenAL and libsndfile
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sound_assets
        test_sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_sound_assets PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_sound_assets PRIVATE WITH_AUDIO)
    SET(SOUND_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_sound_assets PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND SOUND_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_sound_assets ${SOUND_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sound_assets)
ENDIF()

# Test sound effects (sound effect playback, 3D positioning, volume controls)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sound_effects
        test_sound_effects.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_sound_effects PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_sound_effects PRIVATE WITH_AUDIO)
    SET(SOUND_EFFECTS_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_sound_effects PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND SOUND_EFFECTS_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_sound_effects ${SOUND_EFFECTS_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sound_effects)
ENDIF()

# Test spatial audio (3D positional audio, distance attenuation, listener position)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_spatial_audio
        test_spatial_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_spatial_audio PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_spatial_audio PRIVATE WITH_AUDIO)
    SET(SPATIAL_AUDIO_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_spatial_audio PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND SPATIAL_AUDIO_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_spatial_audio ${SPATIAL_AUDIO_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_spatial_audio)
ENDIF()

# Test creature sounds (Phase 15: NPC/creature sound file mapping)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_creature_sounds
        test_creature_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/creature_sounds.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_creature_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_creature_sounds GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_creature_sounds)
ENDIF()

# Test weather and water audio (Phase 17: water entry/exit, rain, thunder, wind)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_weather_audio
        test_weather_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/weather_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/water_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_weather_audio PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_weather_audio PRIVATE WITH_AUDIO)
    SET(WEATHER_AUDIO_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_weather_audio PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND WEATHER_AUDIO_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_weather_audio ${WEATHER_AUDIO_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_weather_audio)
ENDIF()

# Test RDP input handler (scancode translation)
# These tests verify RDP scancode to Irrlicht key code translation
IF(WITH_RDP)
    ADD_EXECUTABLE(test_rdp_input_handler
        test_rdp_input_handler.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_input_handler.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_rdp_input_handler PRIVATE
        ${IRRLICHT_INCLUDE_DIR}
        ${FREERDP_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_rdp_input_handler PRIVATE WITH_RDP)
    TARGET_LINK_LIBRARIES(test_rdp_input_handler GTest::gtest_main ${FREERDP_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_rdp_input_handler)
ENDIF()

# Test RDP server (initialization, lifecycle, callbacks)
# These tests verify RDP server functionality without actual network connections
IF(WITH_RDP)
    ADD_EXECUTABLE(test_rdp_server
        test_rdp_server.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_server.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_peer_context.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/rdp/rdp_input_handler.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_rdp_server PRIVATE
        ${IRRLICHT_INCLUDE_DIR}
        ${FREERDP_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_rdp_server PRIVATE WITH_RDP)
    TARGET_LINK_LIBRARIES(test_rdp_server GTest::gtest_main ${FREERDP_LIBRARIES} freerdp-server3 ${FMT_LIB} ${OPENSSL_LIBRARIES})
    GTEST_DISCOVER_TESTS(test_rdp_server)
ENDIF()

# Standalone FluidSynth MIDI streaming test
# This test can be run independently to test MIDI playback via FluidSynth->PulseAudio
# Run: PULSE_SINK=sink_name ./bin/test_fluidsynth_midi /path/to/soundfont.sf2 /path/to/file.xmi
IF(WITH_AUDIO AND FLUIDSYNTH_FOUND)
    ADD_EXECUTABLE(test_fluidsynth_midi
        test_fluidsynth_midi.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_fluidsynth_midi PRIVATE
        ${OPENAL_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_fluidsynth_midi PRIVATE WITH_AUDIO WITH_FLUIDSYNTH)
    TARGET_LINK_LIBRARIES(test_fluidsynth_midi ${FLUIDSYNTH_LIBRARIES} ${FMT_LIB})
    # Not using GTEST_DISCOVER_TESTS - this is a standalone interactive test
ENDIF()

# Test UI sounds (UI sound type mappings, sound IDs, filenames)
# These tests verify the UISounds utility class and UI sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_ui_sounds
        test_ui_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/ui_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_ui_sounds PRIVATE
        ${OPENAL_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_ui_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_ui_sounds GTest::gtest_main ${FMT_LIB} ZLIB::ZLIB)
    GTEST_DISCOVER_TESTS(test_ui_sounds)
ENDIF()

# Test door and object sounds (door/lever/trap sound type mappings, sound IDs, filenames)
# These tests verify the DoorSounds utility class and door/object sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_door_sounds
        test_door_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/door_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_door_sounds PRIVATE
        ${OPENAL_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_door_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_door_sounds GTest::gtest_main ${FMT_LIB} ZLIB::ZLIB)
    GTEST_DISCOVER_TESTS(test_door_sounds)
ENDIF()

# Test player character sounds (death, drown, jump, gethit, gasp)
# These tests verify the PlayerSounds utility class and race/gender sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_player_sounds
        test_player_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/player_sounds.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_player_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_player_sounds GTest::gtest_main)
    GTEST_DISCOVER_TESTS(test_player_sounds)
ENDIF()

# Test combat music stingers (Phase 19: combat music cues during fights)
# These tests verify the CombatMusicManager class and combat stinger logic
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_combat_music
        test_combat_music.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/combat_music.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_combat_music PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_combat_music PRIVATE WITH_AUDIO)
    SET(COMBAT_MUSIC_TEST_LIBS GTest::gtest_main ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${FMT_LIB})
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_combat_music PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND COMBAT_MUSIC_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_combat_music ${COMBAT_MUSIC_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_combat_music)

    # Music loopback compare test (manual test for debugging RDP audio)
    ADD_EXECUTABLE(test_music_loopback_compare
        test_music_loopback_compare.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_music_loopback_compare PRIVATE
        ${OPENAL_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
    )
    TARGET_COMPILE_DEFINITIONS(test_music_loopback_compare PRIVATE WITH_AUDIO)
    SET(LOOPBACK_COMPARE_TEST_LIBS GTest::gtest ${OPENAL_LIBRARIES} ${SNDFILE_LIBRARIES} ${FMT_LIB})
    IF(FLUIDSYNTH_FOUND)
        TARGET_COMPILE_DEFINITIONS(test_music_loopback_compare PRIVATE WITH_FLUIDSYNTH)
        LIST(APPEND LOOPBACK_COMPARE_TEST_LIBS ${FLUIDSYNTH_LIBRARIES})
    ENDIF()
    TARGET_LINK_LIBRARIES(test_music_loopback_compare ${LOOPBACK_COMPARE_TEST_LIBS})
    # Note: Not using GTEST_DISCOVER_TESTS as this is a manual command-line tool
ENDIF()
