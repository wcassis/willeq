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
    ${CMAKE_SOURCE_DIR}/src/client/audio/polyphase_resampler.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sfx_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
    ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
    ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
    ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
    # B977: audio_manager.cpp / zone_audio_manager.cpp open `au.*` owner labels
    # (eqt::memory::StartupScope), which -- unlike MemoryScope -- is not
    # header-only. Every AudioManager test therefore links the ledger TU, the
    # way test_audio_startup_peak always did.
    ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_b924.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_trace.cpp
    ${CMAKE_SOURCE_DIR}/src/client/core/gpu_buffer_census.cpp
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
    GTEST_DISCOVER_TESTS(test_xmi_decoder DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_eff_loader DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_zone_sound_emitters DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_day_night_audio DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_zone_music DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_sound_assets DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_sound_effects DISCOVERY_TIMEOUT 120)
ENDIF()

# B1015 (#336, supersedes the T412 B292 shape): the ONE decoded-PCM cache of
# SFX PCM must honour its ceiling. SoundBufferCache::entries_ is that cache
# (one cap); SfxManager holds no cache of its own and never evicts the sound
# being inserted. Drives the real SoundBufferCache and, for the pin cases, a
# real SfxManager + AudioMixer -- no device is opened.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sfx_cache_eviction
        test_sfx_cache_eviction.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_sfx_cache_eviction PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_sfx_cache_eviction ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_sfx_cache_eviction DISCOVERY_TIMEOUT 120)
ENDIF()

# B1018 (GitLab #339): the "audio.vendor_music is parsed but ignored" WARN
# must fire only when a config actually sets the key. Drives the exact
# production parse path, Application::loadConfigFile() (application_config
# .cpp's `audio_config.isMember("vendor_music")` arm, where the post-B1018
# WARN lives) with two minimal configs -- the key absent (0 warns) and
# present (exactly 1). Steady-state stdout capture (the T31 idiom from
# test_renderer_command_echo.cpp).
#
# Link shape: willeq_core ALREADY compiles application_config.cpp (it is in
# ALL_CORE_SOURCES), so this target compiles NOTHING from it -- it links the
# archive and pulls in only the objects it actually calls (loadConfigFile,
# the hotkey-manager objects its EQT_HAS_GRAPHICS block references, and
# performance_metrics / json_config / data_paths for the two timers, the
# JsonConfigFile::Load and ResolveDataPath). No direct source of that TU, or
# the archive's copy and a duplicate collide at link time.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_vendor_music_config_warn
        test_vendor_music_config_warn.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_vendor_music_config_warn PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_COMPILE_DEFINITIONS(test_vendor_music_config_warn PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_vendor_music_config_warn
        GTest::gtest_main willeq_core ${JSONCPP_LIBRARIES} ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_vendor_music_config_warn DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_spatial_audio DISCOVERY_TIMEOUT 120)
ENDIF()

# Test creature sounds (Phase 15: NPC/creature sound file mapping)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_creature_sounds
        test_creature_sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/creature_sounds.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_creature_sounds PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_creature_sounds GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_creature_sounds DISCOVERY_TIMEOUT 120)
ENDIF()

# Test weather and water audio (Phase 17: water entry/exit, rain, thunder, wind)
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_weather_audio
        test_weather_audio.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/weather_audio.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        # T399 (board B235): every routed loader calls eqt::ResolveDataPath().
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_weather_audio PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_weather_audio ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_weather_audio DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_rdp_input_handler DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_rdp_server DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_ui_sounds DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_ui_sound_events DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_door_sounds DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_player_sounds DISCOVERY_TIMEOUT 120)
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
    GTEST_DISCOVER_TESTS(test_combat_music DISCOVERY_TIMEOUT 120)

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
    GTEST_DISCOVER_TESTS(test_pfs_sound_load DISCOVERY_TIMEOUT 120)
ENDIF()

# T404 Leg A (board B250): the mixer's playback RATE. AudioMixer::render()
# never read MixChannel::sampleRate, so every asset played at the device rate
# and the shipped 11,025 Hz effects came out an octave high and half as long.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_audio_playback_rate
        test_audio_playback_rate.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_audio_playback_rate PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_audio_playback_rate ${AUDIO_COMMON_TEST_LIBS})
    GTEST_DISCOVER_TESTS(test_audio_playback_rate DISCOVERY_TIMEOUT 120)
ENDIF()

# T496 Leg B (board B817): the `audio` ledger category is the HEAD of the
# Orange Pi's shrink queue -- 47.6 MiB live at every lifecycle point, measured
# on the Pi itself (review/T496/pi/table_categories.tsv). Roughly half of it is
# a second, abandoned copy of synthus2.sf2 held by MusicPlayer's fallback
# MidiPlayer. This target links the ledger TU directly (as test_memory_ledger
# does) so the assertions can be in BYTES of the `audio` category rather than
# in a proxy. Skips itself when no EQ client SoundFont is present.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_music_player_soundfont_lifetime
        test_music_player_soundfont_lifetime.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/music_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/xmi_decoder.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_mixer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_buffer.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sfx_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/sound_assets.cpp
        ${CMAKE_SOURCE_DIR}/src/client/graphics/eq/pfs.cpp
        ${CMAKE_SOURCE_DIR}/src/client/willeq_database.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
        ${CMAKE_SOURCE_DIR}/third_party/sqlite3.c
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_b924.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/memory_ledger_trace.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/gpu_buffer_census.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_music_player_soundfont_lifetime PRIVATE
        WITH_AUDIO
        WILLEQ_EQ_CLIENT_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")
    TARGET_LINK_LIBRARIES(test_music_player_soundfont_lifetime
        GTest::gtest_main ${AUDIO_COMMON_TEST_LIBS} Threads::Threads)
    GTEST_DISCOVER_TESTS(test_music_player_soundfont_lifetime DISCOVERY_TIMEOUT 120)
ENDIF()

# B990 (issue #309): the custom orchestral SoundFont (synthus2.sf2) is loaded
# on demand by the first Velious/Luclin XMI (T504) and B977 measured it is
# NEVER released -- 24,162,944 B of float samples for the rest of the session
# (refactor/B977/audio_ledger_census.tsv). This target pins the release at the
# mechanism that ends a song (MusicPlayer::stop immediate + its fade
# completion): a stopped custom-font song must release the font, a following
# one reloads it (T504 unchanged), and a GM-font song never loads it. It
# constructs the real AudioManager (forceLoopback, no device needed) so the
# release is proven at AudioManager::stopMusic, not just on a bare MidiPlayer.
# Links the whole AUDIO_COMMON_TEST_SOURCES set for the same reason
# test_audio_startup_peak does. Skips itself when no EQ client SoundFont is
# present.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_soundfont_release
        test_soundfont_release.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_soundfont_release PRIVATE
        WITH_AUDIO
        WILLEQ_EQ_CLIENT_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")
    TARGET_LINK_LIBRARIES(test_soundfont_release
        GTest::gtest_main ${AUDIO_COMMON_TEST_LIBS} Threads::Threads)
    GTEST_DISCOVER_TESTS(test_soundfont_release DISCOVERY_TIMEOUT 120)
ENDIF()

# T496 Leg D (board B823): the `audio` category's PEAK at startup. Links the
# ledger TU directly so the assertion is in bytes of Category::Audio, and links
# the same AUDIO_COMMON_TEST_SOURCES the other AudioManager tests use.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_audio_startup_peak
        test_audio_startup_peak.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_audio_startup_peak PRIVATE
        WITH_AUDIO
        WILLEQ_EQ_CLIENT_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")
    TARGET_LINK_LIBRARIES(test_audio_startup_peak
        GTest::gtest_main ${AUDIO_COMMON_TEST_LIBS} Threads::Threads)
    # B845 (board B845): register the three cases by hand instead of
    # GTEST_DISCOVER_TESTS so SET_TESTS_PROPERTIES can name them at configure
    # time (discovery defers the add_test calls to build time). The RESOURCE_LOCK
    # is the second belt, not the fix: the cases are isolated from each other at
    # the root by the authoritative WILLEQ_CACHE_DIR (each process's read and
    # write are confined to its own per-process directory -- see the
    # ColdSoundIndex fixture and src/client/core/data_paths.cpp), so they can no
    # longer share the on-disk snd_index_cache.json. The lock serialises the
    # three against each other so even a regression that reintroduces
    # shared-cache state cannot flake the suite. It deliberately does NOT cover
    # the other audio tests, which use the shared cache by design and must stay
    # free to run in parallel. Renaming a case means adding it here too -- the
    # fixture's resolver ASSERT is what keeps a renamed case from measuring
    # nothing silently.
    FOREACH(_case IN ITEMS
            IndexingTheSoundArchivesDoesNotHoldThemAllAtOnce
            TheSteadyStateIsTheSoundFontsAndLittleElse
            ListFilenamesMatchesOpenPlusGetFilenames)
        ADD_TEST(NAME AudioStartupPeak.${_case}
            COMMAND test_audio_startup_peak
            --gtest_filter=AudioStartupPeak.${_case}
            --gtest_also_run_disabled_tests)
        SET_TESTS_PROPERTIES(AudioStartupPeak.${_case} PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            SKIP_REGULAR_EXPRESSION "\\[  SKIPPED \\]"
            RESOURCE_LOCK "audio_startup_peak_shared_cache")
    ENDFOREACH()
ENDIF()

# B977 (2026-10-01): audio bytes are charged to `audio` whichever thread asks.
# The custom SoundFont that T504 deferred to the first Velious/Luclin XMI, and
# every WAV decoded on first play, used to land in the CALLER's category -- on
# the client that is the game thread's `game_state` (B854 velketor: +24.8 MB of
# game_state, audio flat). The cases open a GameState scope, call the public
# entry points the game thread calls, and assert the bytes moved to `audio`.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_audio_ledger_attribution
        test_audio_ledger_attribution.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_audio_manager.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/zone_sound_emitter.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/eff_loader.cpp
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_audio_ledger_attribution PRIVATE
        WITH_AUDIO
        WILLEQ_EQ_CLIENT_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")
    TARGET_LINK_LIBRARIES(test_audio_ledger_attribution
        GTest::gtest_main ${AUDIO_COMMON_TEST_LIBS} Threads::Threads)
    GTEST_DISCOVER_TESTS(test_audio_ledger_attribution DISCOVERY_TIMEOUT 120)
ENDIF()

# B989 (#308): the decoded PCM of a played WAV is held ONCE. playSound() used
# to decode into bufferCache_ (au.sound_buffers) AND copy the same samples
# into SfxManager::cache_ (au.sfx_cache) -- up to 16 MiB of duplicated PCM,
# each cache capped separately. The cases play real archive sounds through the
# client's own paths and assert the owner rows agree (one holds the PCM, the
# other 0 for it), the `audio` category carries it once, and the preset's
# soundCache cap bounds the single cache with T412's LRU surviving.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_sfx_single_pcm_cache
        test_sfx_single_pcm_cache.cpp
        ${AUDIO_COMMON_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/src/client/core/data_paths.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_sfx_single_pcm_cache PRIVATE
        WITH_AUDIO
        WILLEQ_EQ_CLIENT_PATH="${CMAKE_SOURCE_DIR}/data/eq_client")
    TARGET_LINK_LIBRARIES(test_sfx_single_pcm_cache
        GTest::gtest_main ${AUDIO_COMMON_TEST_LIBS} Threads::Threads)
    GTEST_DISCOVER_TESTS(test_sfx_single_pcm_cache DISCOVERY_TIMEOUT 120)
ENDIF()

# B1069 (#389): the playback DEVICE rate -- audio.device_sample_rate /
# --audio-rate (default 48000, 0 = native), DeviceRateAdapter's frame counts
# (22050 mixer -> 48000 device, no drift over 10 s), and MiniaudioBackend on
# miniaudio's NULL backend (no audio hardware needed) with its init log line.
# Links willeq_core for Application::parseArguments/loadConfigFile and the
# audio TUs, the test_vendor_music_config_warn shape.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_audio_device_rate
        test_audio_device_rate.cpp
    )
    TARGET_INCLUDE_DIRECTORIES(test_audio_device_rate PRIVATE ${IRRLICHT_INCLUDE_DIR})
    TARGET_COMPILE_DEFINITIONS(test_audio_device_rate PRIVATE WITH_AUDIO)
    TARGET_LINK_LIBRARIES(test_audio_device_rate
        GTest::gtest_main willeq_core ${JSONCPP_LIBRARIES} ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_audio_device_rate DISCOVERY_TIMEOUT 120)
ENDIF()
