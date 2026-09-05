# tests/cmake/audio.cmake -- included from tests/CMakeLists.txt.
# Audio subsystem tests (XMI, EFF, emitters, music, sound effects, RDP audio path).
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# Common audio source files used by audio tests that need AudioManager
SET(AUDIO_COMMON_TEST_SOURCES
    ${CMAKE_SOURCE_DIR}/src/client/audio/audio_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/audio_mixer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/audio_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sfx_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
)

# Common libs for audio tests (platform audio libs for miniaudio)
SET(AUDIO_COMMON_TEST_LIBS GTest::gtest_main ${JSONCPP_LIBRARIES} ${FMT_LIB} ZLIB::ZLIB)
IF(APPLE)
    FIND_LIBRARY(COREAUDIO_FRAMEWORK CoreAudio)
    FIND_LIBRARY(AUDIOUNIT_FRAMEWORK AudioUnit)
    FIND_LIBRARY(COREFOUNDATION_FRAMEWORK CoreFoundation)
    LIST(APPEND AUDIO_COMMON_TEST_LIBS ${COREAUDIO_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
ELSEIF(UNIX)
    LIST(APPEND AUDIO_COMMON_TEST_LIBS dl m)
ENDIF()

# Test XMI decoder (XMI to MIDI conversion)
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
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
    )
    TARGET_COMPILE_DEFINITIONS(test_eff_loader PRIVATE WITH_AUDIO WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_LINK_LIBRARIES(test_eff_loader GTest::gtest_main ${FMT_LIB} dl)
    GTEST_DISCOVER_TESTS(test_eff_loader)
ENDIF()

# Test zone sound emitters (positioned sound sources in zones)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_zone_sound_emitters
        test_zone_sound_emitters.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_sound_emitters PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_zone_sound_emitters ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_zone_sound_emitters)
ENDIF()

# Test day/night audio system (Phase 13: game time based sound transitions)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_day_night_audio
        test_day_night_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_day_night_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_day_night_audio ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_day_night_audio)
ENDIF()

# Test zone music system (zone music mapping and transitions)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_zone_music
        test_zone_music.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_zone_music PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_zone_music ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_zone_music)
ENDIF()

# Test sound assets (SoundAssets.txt parsing, WAV loading, AudioManager)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sound_assets
        test_sound_assets.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_sound_assets PRIVATE WITH_AUDIO
        WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_LINK_LIBRARIES(test_sound_assets ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sound_assets)
ENDIF()

# Test sound effects (sound effect playback, 3D positioning, volume controls)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sound_effects
        test_sound_effects.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_sound_effects PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_sound_effects ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sound_effects)
ENDIF()

# Test spatial audio (3D positional audio, distance attenuation, listener position)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_spatial_audio
        test_spatial_audio.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_spatial_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_spatial_audio ${AUDIO_COMMON_TEST_LIBS})
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
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_weather_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_weather_audio ${AUDIO_COMMON_TEST_LIBS})
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

# Test UI sounds (UI sound type mappings, sound IDs, filenames)
# These tests verify the UISounds utility class and UI sound mappings
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_ui_sounds
        test_ui_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/ui_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_ui_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_ui_sounds GTest::gtest_main ${FMT_LIB} ZLIB::ZLIB)
    GTEST_DISCOVER_TESTS(test_ui_sounds)
ENDIF()

# T328 Leg B: the UI sounds the official Titanium client was MEASURED to play,
# asserted at the game-side publisher through the PlaySoundIntent seam --
# container open/close (a real EverQuest replaying OP_ClickObjectAction) and
# item drop / coin-to-cursor (a bare InventoryManager on a RecordingBridge).
# Needs willeq_core for EverQuest + InventoryManager; no audio device, no EQ
# client data, no eqemu.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_ui_sound_events
        test_ui_sound_events.cpp
    )
    TARGET_LINK_LIBRARIES(test_ui_sound_events GTest::gtest_main willeq_core)
    IF(EQT_GRAPHICS AND IRRLICHT_LIBRARY)
        TARGET_INCLUDE_DIRECTORIES(test_ui_sound_events PRIVATE ${IRRLICHT_INCLUDE_DIR})
    ENDIF()
    GTEST_DISCOVER_TESTS(test_ui_sound_events)
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
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_combat_music PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_combat_music ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_combat_music)

    # Music loopback compare test (manual test for debugging RDP audio)
    ADD_EXECUTABLE(test_music_loopback_compare
        test_music_loopback_compare.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_music_loopback_compare PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_music_loopback_compare GTest::gtest ${AUDIO_COMMON_TEST_LIBS})
    # Note: Not using GTEST_DISCOVER_TESTS as this is a manual command-line tool
ENDIF()

# T392 (board B200): the snd*.pfs sound-load path, against the real client
# archives. No audio device (forceLoopback), no eqemu, no graphics.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_pfs_sound_load
        test_pfs_sound_load.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_pfs_sound_load PRIVATE WITH_AUDIO
        WILLEQ_DB_PATH="${CMAKE_SOURCE_DIR}/data/willeq.db")
    TARGET_LINK_LIBRARIES(test_pfs_sound_load ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_pfs_sound_load)
ENDIF()
