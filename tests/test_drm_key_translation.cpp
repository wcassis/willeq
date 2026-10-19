#include "docker/irrlicht-drm/drm_key_translation.h"
#include <gtest/gtest.h>

namespace {
struct KeyState {
    bool keys[KEY_CNT] = {};
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    irr::SEvent event = {};

    bool send(unsigned code, int value, irr::EKEY_CODE key) {
        input_event input = {};
        input.type = EV_KEY;
        input.code = code;
        input.value = value;
        return eqt_drm::translateKeyEvent(input, key, keys, KEY_CNT,
                                          shift, ctrl, alt, event);
    }
};
} // namespace

TEST(DRMKeyTranslation, PressRepeatRepeatRelease) {
    KeyState state;
    for (int value : {1, 2, 2, 0}) {
        ASSERT_TRUE(state.send(KEY_A, value, irr::KEY_KEY_A));
        EXPECT_EQ(state.event.EventType, irr::EET_KEY_INPUT_EVENT);
        EXPECT_EQ(state.event.KeyInput.Key, irr::KEY_KEY_A);
        EXPECT_EQ(state.event.KeyInput.PressedDown, value != 0);
        EXPECT_EQ(state.event.KeyInput.Char, value != 0 ? L'a' : 0);
        EXPECT_EQ(state.keys[KEY_A], value != 0);
    }
}

TEST(DRMKeyTranslation, ShiftAndControlPersistAcrossRepeat) {
    KeyState state;
    ASSERT_TRUE(state.send(KEY_LEFTSHIFT, 1, irr::KEY_LSHIFT));
    ASSERT_TRUE(state.send(KEY_LEFTCTRL, 1, irr::KEY_LCONTROL));
    ASSERT_TRUE(state.send(KEY_A, 2, irr::KEY_KEY_A));
    EXPECT_EQ(state.event.KeyInput.Char, L'A');
    EXPECT_TRUE(state.event.KeyInput.Shift);
    EXPECT_TRUE(state.event.KeyInput.Control);
    ASSERT_TRUE(state.send(KEY_LEFTSHIFT, 0, irr::KEY_LSHIFT));
    ASSERT_TRUE(state.send(KEY_A, 2, irr::KEY_KEY_A));
    EXPECT_EQ(state.event.KeyInput.Char, L'a');
    EXPECT_FALSE(state.event.KeyInput.Shift);
    EXPECT_TRUE(state.event.KeyInput.Control);
}

TEST(DRMKeyTranslation, RejectsNonKeyAndOutOfRangeValuesWithoutMutation) {
    KeyState state;
    state.event.EventType = irr::EET_USER_EVENT;
    input_event input = {};
    input.type = EV_REL;
    input.code = KEY_A;
    input.value = 1;
    EXPECT_FALSE(eqt_drm::translateKeyEvent(input, irr::KEY_KEY_A,
        state.keys, KEY_CNT, state.shift, state.ctrl, state.alt, state.event));
    input.type = EV_KEY;
    for (int value : {-1, 3, 255}) {
        KeyState invalidState;
        invalidState.event.EventType = irr::EET_USER_EVENT;
        input.value = value;
        EXPECT_FALSE(eqt_drm::translateKeyEvent(input, irr::KEY_KEY_A,
            invalidState.keys, KEY_CNT, invalidState.shift, invalidState.ctrl,
            invalidState.alt, invalidState.event)) << value;
        EXPECT_FALSE(invalidState.keys[KEY_A]) << value;
        EXPECT_FALSE(invalidState.shift) << value;
        EXPECT_FALSE(invalidState.ctrl) << value;
        EXPECT_FALSE(invalidState.alt) << value;
        EXPECT_EQ(invalidState.event.EventType, irr::EET_USER_EVENT) << value;
    }
}

// B1099 (GitLab #419): Ctrl+C is the quit chord the device turns into SIGINT
// (K_OFF keeps the VT from doing it). Driven through translateKeyEvent first,
// exactly as pollEvdev() does, so ctrlDown is the device's own state.
namespace {
bool quitChordAfter(KeyState& state, unsigned code, int value, irr::EKEY_CODE key) {
    input_event input = {};
    input.type = EV_KEY;
    input.code = code;
    input.value = value;
    state.send(code, value, key);
    return eqt_drm::isQuitChord(input, state.ctrl);
}
} // namespace

TEST(DRMQuitChord, CtrlCIsTheQuitChord) {
    KeyState state;
    EXPECT_FALSE(quitChordAfter(state, KEY_LEFTCTRL, 1, irr::KEY_LCONTROL));
    EXPECT_TRUE(quitChordAfter(state, KEY_C, 1, irr::KEY_KEY_C));
    EXPECT_FALSE(quitChordAfter(state, KEY_C, 0, irr::KEY_KEY_C)) << "the release is not a second chord";
    EXPECT_FALSE(quitChordAfter(state, KEY_LEFTCTRL, 0, irr::KEY_LCONTROL));
}

TEST(DRMQuitChord, RightControlCountsToo) {
    KeyState state;
    quitChordAfter(state, KEY_RIGHTCTRL, 1, irr::KEY_RCONTROL);
    EXPECT_TRUE(quitChordAfter(state, KEY_C, 1, irr::KEY_KEY_C));
}

TEST(DRMQuitChord, PlainCAndOtherCtrlChordsAreNot) {
    KeyState state;
    EXPECT_FALSE(quitChordAfter(state, KEY_C, 1, irr::KEY_KEY_C)) << "a plain c types a c";
    quitChordAfter(state, KEY_C, 0, irr::KEY_KEY_C);
    quitChordAfter(state, KEY_LEFTCTRL, 1, irr::KEY_LCONTROL);
    EXPECT_FALSE(quitChordAfter(state, KEY_V, 1, irr::KEY_KEY_V));
    EXPECT_FALSE(quitChordAfter(state, KEY_ESC, 1, irr::KEY_ESCAPE))
        << "ESC is the loading screen's quit (loading_screen_quit.h), not the device's";
}

TEST(DRMQuitChord, AutoRepeatDoesNotFireASecondSignal) {
    // A second SIGINT forces the exit (quit_signal.h); a held Ctrl+C must
    // stay ONE request.
    KeyState state;
    quitChordAfter(state, KEY_LEFTCTRL, 1, irr::KEY_LCONTROL);
    EXPECT_TRUE(quitChordAfter(state, KEY_C, 1, irr::KEY_KEY_C));
    EXPECT_FALSE(quitChordAfter(state, KEY_C, 2, irr::KEY_KEY_C));
    EXPECT_FALSE(quitChordAfter(state, KEY_C, 2, irr::KEY_KEY_C));
}
