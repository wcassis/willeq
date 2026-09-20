// B654: the X11-keysym -> Irrlicht EKEY_CODE decision, factored out of
// CIrrDeviceFB_X11.cpp into a pure, display-free header so the normal (non-
// graphics) test build can compile and pin it.
//
// WHY A HEADER: the map lives in a TU that is compiled into the patched
// libIrrlicht.a (docker/irrlicht-drm-x86/CIrrDeviceFB_X11.cpp ->
// third_party/irrlicht-gles2-x86/lib/libIrrlicht.a by scripts/build-gles2-
// x86.sh), which the test build never compiles. Putting the DECISION here (a
// header-only inline function) means BOTH the device TU and the display-less
// unit test (tests/test_x11_keysym_map.cpp) share the exact same map, so the
// test pins production behaviour instead of a copy that can drift.
//
// INCLUDES ARE DELIBERATELY MINIMAL: <X11/X.h> (for the KeySym typedef, which
// Xlib.h alone would also provide but at 4,000 lines), <X11/keysym.h> (the
// XK_* constants) and <Keycodes.h> (the EKEY_CODE enum) only -- never the full
// <irrlicht.h>, so the normal test build compiles this with no graphics
// driver, no EGL, no X11 connection. This is what "display-less" means: the
// function takes a KeySym and returns an EKEY_CODE; it touches no Display*.
//
// The X11 backend compiles this with CXXINCS="-I../../include" (Keycodes.h)
// and the system <X11/...> headers; the test build compiles it with
// ${IRRLICHT_INCLUDE_DIR} (Keycodes.h) and the same system headers. Both
// resolve <Keycodes.h> through their include path (this header is NOT
// co-located with Keycodes.h, so angle-bracket include, not quoted).

#ifndef WILLEQ_X11_KEYSYM_MAP_H_INCLUDED_
#define WILLEQ_X11_KEYSYM_MAP_H_INCLUDED_

#include <X11/X.h>      // KeySym (typedef XID)
#include <X11/keysym.h> // XK_* constants
#include <Keycodes.h>   // EKEY_CODE

namespace irr
{

// B654 (issue #56): a numpad DIGIT key is pressed as its NumLock-LEVEL-0
// (navigation) keysym under Xvfb's default keymap. pollX11Events() always
// looks up at index 0, so `XLookupKeysym(&ev, 0)` returns the NAVIGATION alias
// for a keypad digit regardless of NumLock (measured live on Xvfb: keypad-7 ->
// XK_KP_Home 0xff95, keypad-1 -> XK_KP_End 0xff9c, ...; the digit XK_KP_7 only
// appears at index 1). The evdev/DRM backend (CIrrDeviceFB.cpp keyMap_) maps
// KEY_KP0..KEY_KP9 to KEY_NUMPAD0..9 regardless of NumLock -- that is the
// reference behaviour. So BOTH the digit keysym (NumLock-aware keymaps) and the
// navigation-alias keysym (index-0 / NumLock-off) must resolve to the same
// numpad-digit EKEY_CODE, or every numpad digit is dropped and the Numpad
// camera bindings in config/hotkeys.json never fire under --egl-x11.
//
// The alias->digit correspondence below is the physical numpad layout, measured
// against Xvfb's default keymap (not assumed):
//   7->KP_Home 8->KP_Up 9->KP_Prior 4->KP_Left 5->KP_Begin
//   6->KP_Right 1->KP_End 2->KP_Down 3->KP_Next 0->KP_Insert
// (KP_Decimal is the same keysym at both indices -> KEY_DECIMAL, no alias.)
inline EKEY_CODE x11KeysymToIrrlicht(KeySym sym)
{
    switch (sym) {
        case XK_a: case XK_A: return KEY_KEY_A;
        case XK_b: case XK_B: return KEY_KEY_B;
        case XK_c: case XK_C: return KEY_KEY_C;
        case XK_d: case XK_D: return KEY_KEY_D;
        case XK_e: case XK_E: return KEY_KEY_E;
        case XK_f: case XK_F: return KEY_KEY_F;
        case XK_g: case XK_G: return KEY_KEY_G;
        case XK_h: case XK_H: return KEY_KEY_H;
        case XK_i: case XK_I: return KEY_KEY_I;
        case XK_j: case XK_J: return KEY_KEY_J;
        case XK_k: case XK_K: return KEY_KEY_K;
        case XK_l: case XK_L: return KEY_KEY_L;
        case XK_m: case XK_M: return KEY_KEY_M;
        case XK_n: case XK_N: return KEY_KEY_N;
        case XK_o: case XK_O: return KEY_KEY_O;
        case XK_p: case XK_P: return KEY_KEY_P;
        case XK_q: case XK_Q: return KEY_KEY_Q;
        case XK_r: case XK_R: return KEY_KEY_R;
        case XK_s: case XK_S: return KEY_KEY_S;
        case XK_t: case XK_T: return KEY_KEY_T;
        case XK_u: case XK_U: return KEY_KEY_U;
        case XK_v: case XK_V: return KEY_KEY_V;
        case XK_w: case XK_W: return KEY_KEY_W;
        case XK_x: case XK_X: return KEY_KEY_X;
        case XK_y: case XK_Y: return KEY_KEY_Y;
        case XK_z: case XK_Z: return KEY_KEY_Z;

        case XK_0: return KEY_KEY_0;
        case XK_1: return KEY_KEY_1;
        case XK_2: return KEY_KEY_2;
        case XK_3: return KEY_KEY_3;
        case XK_4: return KEY_KEY_4;
        case XK_5: return KEY_KEY_5;
        case XK_6: return KEY_KEY_6;
        case XK_7: return KEY_KEY_7;
        case XK_8: return KEY_KEY_8;
        case XK_9: return KEY_KEY_9;

        case XK_F1:  return KEY_F1;
        case XK_F2:  return KEY_F2;
        case XK_F3:  return KEY_F3;
        case XK_F4:  return KEY_F4;
        case XK_F5:  return KEY_F5;
        case XK_F6:  return KEY_F6;
        case XK_F7:  return KEY_F7;
        case XK_F8:  return KEY_F8;
        case XK_F9:  return KEY_F9;
        case XK_F10: return KEY_F10;
        case XK_F11: return KEY_F11;
        case XK_F12: return KEY_F12;

        case XK_Escape:      return KEY_ESCAPE;
        case XK_Tab:         return KEY_TAB;
        case XK_BackSpace:   return KEY_BACK;
        case XK_Return:      return KEY_RETURN;
        case XK_space:       return KEY_SPACE;
        case XK_Delete:      return KEY_DELETE;
        case XK_Insert:      return KEY_INSERT;
        case XK_Home:        return KEY_HOME;
        case XK_End:         return KEY_END;
        case XK_Prior:       return KEY_PRIOR;
        case XK_Next:        return KEY_NEXT;
        case XK_Caps_Lock:   return KEY_CAPITAL;
        case XK_Num_Lock:    return KEY_NUMLOCK;
        case XK_Scroll_Lock: return KEY_SCROLL;

        case XK_Up:    return KEY_UP;
        case XK_Down:  return KEY_DOWN;
        case XK_Left:  return KEY_LEFT;
        case XK_Right: return KEY_RIGHT;

        case XK_Shift_L:   return KEY_LSHIFT;
        case XK_Shift_R:   return KEY_RSHIFT;
        case XK_Control_L: return KEY_LCONTROL;
        case XK_Control_R: return KEY_RCONTROL;
        case XK_Alt_L:     return KEY_LMENU;
        case XK_Alt_R:     return KEY_RMENU;

        case XK_minus:         return KEY_MINUS;
        case XK_equal:         return KEY_PLUS;
        case XK_bracketleft:   return KEY_OEM_4;
        case XK_bracketright:  return KEY_OEM_6;
        case XK_semicolon:     return KEY_OEM_1;
        case XK_apostrophe:    return KEY_OEM_7;
        case XK_grave:         return KEY_OEM_3;
        case XK_backslash:     return KEY_OEM_5;
        case XK_comma:         return KEY_COMMA;
        case XK_period:        return KEY_PERIOD;
        case XK_slash:         return KEY_OEM_2;

        case XK_KP_0: return KEY_NUMPAD0;
        case XK_KP_1: return KEY_NUMPAD1;
        case XK_KP_2: return KEY_NUMPAD2;
        case XK_KP_3: return KEY_NUMPAD3;
        case XK_KP_4: return KEY_NUMPAD4;
        case XK_KP_5: return KEY_NUMPAD5;
        case XK_KP_6: return KEY_NUMPAD6;
        case XK_KP_7: return KEY_NUMPAD7;
        case XK_KP_8: return KEY_NUMPAD8;
        case XK_KP_9: return KEY_NUMPAD9;
        // B654 (issue #56): the NumLock-level-0 (navigation) aliases. This is
        // what XLookupKeysym(&ev, 0) actually returns for a numpad-digit key on
        // Xvfb's default keymap (in both NumLock states), so each must resolve
        // to the SAME numpad-digit code as its digit keysym, or the key is
        // dropped. See the measured alias->digit table in this file's header
        // comment and tests/test_x11_keysym_map.cpp for the regression net.
        case XK_KP_Home:    return KEY_NUMPAD7; // keypad-7
        case XK_KP_Up:      return KEY_NUMPAD8; // keypad-8
        case XK_KP_Prior:   return KEY_NUMPAD9; // keypad-9
        case XK_KP_Left:    return KEY_NUMPAD4; // keypad-4
        case XK_KP_Begin:   return KEY_NUMPAD5; // keypad-5
        case XK_KP_Right:   return KEY_NUMPAD6; // keypad-6
        case XK_KP_End:     return KEY_NUMPAD1; // keypad-1
        case XK_KP_Down:    return KEY_NUMPAD2; // keypad-2
        case XK_KP_Next:    return KEY_NUMPAD3; // keypad-3
        case XK_KP_Insert:  return KEY_NUMPAD0; // keypad-0
        case XK_KP_Decimal:  return KEY_DECIMAL;
        case XK_KP_Enter:    return KEY_RETURN;
        case XK_KP_Add:      return KEY_ADD;
        case XK_KP_Subtract: return KEY_SUBTRACT;
        case XK_KP_Multiply: return KEY_MULTIPLY;
        case XK_KP_Divide:   return KEY_DIVIDE;

        default: return (EKEY_CODE)0;
    }
}

} // end namespace irr

#endif // WILLEQ_X11_KEYSYM_MAP_H_INCLUDED_
