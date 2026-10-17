# tests/cmake/midi.cmake -- included from tests/CMakeLists.txt.
# TinySoundFont / MidiPlayer tests (T130, T131, T248).
# Moved verbatim from tests/CMakeLists.txt by J20 (no target, source or
# link changes). Included in the tests/ directory scope, so every variable
# set in the top-level preamble is in effect here.

# T130: regression test for the vendored TinySoundFont OOB read
# (third_party/TinySoundFont/tsf.h's "WILLEQ T130" clamp). Self-contained --
# includes tsf.h with TSF_IMPLEMENTATION directly (same pattern as
# midi_player.cpp), no audio backend/device needed, no WITH_AUDIO gate.
ADD_EXECUTABLE(test_tsf_oob_read
    test_tsf_oob_read.cpp
)
TARGET_LINK_LIBRARIES(test_tsf_oob_read GTest::gtest_main)
GTEST_DISCOVER_TESTS(test_tsf_oob_read DISCOVERY_TIMEOUT 120)

# T131: verifies tsf_set_max_voices + all-16-channels-precreated is applied
# on every MidiPlayer construction path, including the runtime reload
# (MidiPlayer::loadSoundFont). Links midi_player.cpp directly (needs
# WITH_AUDIO for the class to exist at all) plus its own required data (no
# AudioManager/AudioMixer/audio device needed -- MidiPlayer itself has no
# device dependency).
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_midi_player_prealloc
        test_midi_player_prealloc.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_midi_player_prealloc PRIVATE WITH_AUDIO
        "WILLEQ_TEST_SF2_PATH=WILLEQ_REPO_PATH(\"data/1mgm.sf2\")")
    TARGET_LINK_LIBRARIES(test_midi_player_prealloc GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_midi_player_prealloc DISCOVERY_TIMEOUT 120)
ENDIF()

# T248: MidiPlayer::stop() vs the audio render callback. Links MidiPlayer +
# AudioMixer only (the test owns the render thread -- no device, no
# miniaudio). This binary is also the ThreadSanitizer driver for the unit:
# build it in a -fsanitize=thread tree and run it directly.
IF(WITH_AUDIO)
    ADD_EXECUTABLE(test_midi_stop_race
        test_midi_stop_race.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/midi_player.cpp
        ${CMAKE_SOURCE_DIR}/src/client/audio/audio_mixer.cpp
    )
    TARGET_COMPILE_DEFINITIONS(test_midi_stop_race PRIVATE WITH_AUDIO
        "WILLEQ_TEST_SF2_PATH=WILLEQ_REPO_PATH(\"data/1mgm.sf2\")")
    TARGET_LINK_LIBRARIES(test_midi_stop_race GTest::gtest_main ${FMT_LIB})
    GTEST_DISCOVER_TESTS(test_midi_stop_race DISCOVERY_TIMEOUT 120)
ENDIF()
