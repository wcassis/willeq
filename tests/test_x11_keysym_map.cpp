// B654 (issue #56): the --egl-x11 device drops every numpad digit key.
//
// ROOT CAUSE (measured live on Xvfb :99, origin/fable): CIrrDeviceFB_X11.cpp's
// pollX11Events() calls XLookupKeysym(&xev.xkey, 0) at index 0. On Xvfb's
// default keymap a keypad digit key's index-0 (NumLock-level-0) entry is its
// NAVIGATION alias, and XLookupKeysym returns that regardless of NumLock:
//
//     keypad-7 keycode 79 -> XK_KP_Home  (0xff95)   [digit XK_KP_7 is at idx 1]
//     keypad-1 keycode 87 -> XK_KP_End   (0xff9c)
//     ... and so on for all ten digits (see the measured table below).
//
// The old map only had cases for the DIGIT keysyms (XK_KP_0..9), so the
// navigation alias fell to `default -> (EKEY_CODE)0` and every numpad digit was
// dropped: the Numpad camera bindings in config/hotkeys.json (Numpad7 ->
// CameraZoomIn, Numpad1 -> CameraZoomOut, Alt+Numpad8/2/6/4/9/3 -> orbit/pitch/
// raise/lower) never fired under --egl-x11. The evdev/DRM backend maps
// KEY_KP0..KEY_KP9 to KEY_NUMPAD0..9 regardless of NumLock -- that is the
// reference behaviour this test pins.
//
// THIS TEST IS DISPLAY-LESS. It compiles the pure header
// docker/irrlicht-drm-x86/x11_keysym_map.h (the same header the X11 device TU
// #includes, so there is no copy that can drift) and pins the map's decision
// directly -- no Display*, no X11 connection, no graphics driver. It runs in
// the normal test build (hygiene -> build -> test) and is what makes a
// regression to the map a RED test, not a silent dropped key.
//
// The RED->GREEN story: with the pre-fix map (digits only) the ten
// navigation-alias cases below FAIL (they resolve to 0); the digit cases pass.
// The fix adds the ten navigation-alias cases to the shared map; then all pass.

#include <gtest/gtest.h>

#include "x11_keysym_map.h"

using namespace irr; // EKEY_CODE and the KEY_* / x11KeysymToIrrlicht symbols

namespace {

// The physical numpad layout's navigation-alias -> digit correspondence,
// measured against Xvfb's default keymap (XLookupKeysym(&ev, 0) at index 0):
//   top row    7  8  9     -> KP_Home  KP_Up     KP_Prior
//   middle row 4  5  6     -> KP_Left  KP_Begin  KP_Right
//   bottom row 1  2  3     -> KP_End   KP_Down   KP_Next
//   0          0          -> KP_Insert
//
// These are the keysyms pollX11Events() ACTUALLY receives for a numpad-digit
// press (index-0 lookup), so they must resolve to the same EKEY_CODE as the
// digit keysym -- otherwise the key is dropped.
struct NumpadAlias {
    KeySym alias;   // navigation alias (NumLock-level-0 / index-0)
    KeySym digit;   // the digit keysym (NumLock-aware keymaps / index-1)
    EKEY_CODE code; // the EKEY_CODE both must produce
};

const NumpadAlias kNumpadAliases[] = {
    {XK_KP_Home,    XK_KP_7, KEY_NUMPAD7},
    {XK_KP_Up,      XK_KP_8, KEY_NUMPAD8},
    {XK_KP_Prior,   XK_KP_9, KEY_NUMPAD9},
    {XK_KP_Left,    XK_KP_4, KEY_NUMPAD4},
    {XK_KP_Begin,   XK_KP_5, KEY_NUMPAD5},
    {XK_KP_Right,   XK_KP_6, KEY_NUMPAD6},
    {XK_KP_End,     XK_KP_1, KEY_NUMPAD1},
    {XK_KP_Down,    XK_KP_2, KEY_NUMPAD2},
    {XK_KP_Next,    XK_KP_3, KEY_NUMPAD3},
    {XK_KP_Insert,  XK_KP_0, KEY_NUMPAD0},
};

} // namespace

// The digit keysyms must resolve to the numpad-digit EKEY_CODE. These passed
// before the fix too (the map always had them); they are pinned so a
// "cleanup" that removes the digit cases is also a red test.
TEST(X11KeysymMap, NumpadDigitsResolveToNumpadCodes)
{
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_0), KEY_NUMPAD0);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_1), KEY_NUMPAD1);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_2), KEY_NUMPAD2);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_3), KEY_NUMPAD3);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_4), KEY_NUMPAD4);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_5), KEY_NUMPAD5);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_6), KEY_NUMPAD6);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_7), KEY_NUMPAD7);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_8), KEY_NUMPAD8);
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_9), KEY_NUMPAD9);
}

// B654 THE REGRESSION: the navigation alias that XLookupKeysym(&ev, 0) returns
// for a numpad-digit key (in BOTH NumLock states under Xvfb's default keymap)
// must resolve to the SAME EKEY_CODE as the digit keysym. Before the fix these
// all returned (EKEY_CODE)0 -- the dropped key -- and this test was RED.
TEST(X11KeysymMap, NumpadNavigationAliasesResolveToNumpadCodes)
{
    for (const NumpadAlias& n : kNumpadAliases) {
        EXPECT_EQ(x11KeysymToIrrlicht(n.alias), n.code)
            << "navigation alias " << n.alias << " ("
            << n.alias << ") for digit " << n.digit
            << " must resolve to " << n.code
            << " -- the index-0 keysym pollX11Events() receives, not 0.";
        // Sanity: the alias and its digit must agree.
        EXPECT_EQ(x11KeysymToIrrlicht(n.alias), x11KeysymToIrrlicht(n.digit))
            << "alias " << n.alias << " and digit " << n.digit
            << " must map to the same EKEY_CODE.";
    }
}

// KP_Decimal is the same keysym at both indices (no NumLock alias), so it must
// keep mapping to KEY_DECIMAL. Pinned so the fix does not accidentally swallow
// it into the numpad-0 bucket.
TEST(X11KeysymMap, NumpadDecimalUnchanged)
{
    EXPECT_EQ(x11KeysymToIrrlicht(XK_KP_Decimal), KEY_DECIMAL);
}

// Negative: an unhandled keysym still resolves to 0 (the map's documented
// default), so a numpad-alias mapping does NOT leak into unrelated keys.
TEST(X11KeysymMap, UnhandledKeysymStillZero)
{
    // XK_KP_Multiply/Add/etc are handled; use a genuinely unmapped keysym.
    EXPECT_EQ(x11KeysymToIrrlicht(0x00), (EKEY_CODE)0);
}
