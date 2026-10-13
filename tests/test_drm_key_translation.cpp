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

TEST(DRMKeyTranslation, RejectsNonKeyAndUnknownValue) {
    KeyState state;
    input_event input = {};
    input.type = EV_REL;
    input.code = KEY_A;
    input.value = 1;
    EXPECT_FALSE(eqt_drm::translateKeyEvent(input, irr::KEY_KEY_A,
        state.keys, KEY_CNT, state.shift, state.ctrl, state.alt, state.event));
    input.type = EV_KEY;
    input.value = 3;
    EXPECT_FALSE(eqt_drm::translateKeyEvent(input, irr::KEY_KEY_A,
        state.keys, KEY_CNT, state.shift, state.ctrl, state.alt, state.event));
}
