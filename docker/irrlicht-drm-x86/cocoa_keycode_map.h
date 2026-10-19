// B1044 (#364): the macOS virtual-keycode -> Irrlicht EKEY_CODE decision for
// the Cocoa windowed device (CIrrDeviceFB_Cocoa.mm), the Darwin counterpart of
// x11_keysym_map.h. Pure and window-free (it includes only <Keycodes.h>), so a
// test can pin it without AppKit.
//
// The keycodes are the HIToolbox `kVK_*` virtual keycodes (physical key
// positions, layout-independent: kVK_ANSI_W is the key where US-QWERTY has W).
// The values below were copied MECHANICALLY from the macOS 15.7 SDK's
// Carbon.framework/Frameworks/HIToolbox.framework/Headers/Events.h on the M4
// mini (B1044 README); they are spelled as numbers so this header needs no
// Carbon include (Carbon's global `Rect`/`Point` names collide with nothing
// here today, but the device TU stays Cocoa-only).
//
// SEMANTICS match x11KeysymToIrrlicht(): letters/digits/F-keys by position,
// `=` -> KEY_PLUS (the X11 map's XK_equal choice), every numpad digit ->
// KEY_NUMPAD0..9 (the evdev reference behaviour; a Mac keypad has no NumLock
// navigation alias, so B654's alias problem does not arise), left/right
// Shift/Control/Option -> L/R SHIFT/CONTROL/MENU (Option is the Mac's Alt).
// Command and Fn map to 0 (X11's Super is unmapped too); the device handles
// Command-Q itself. The Mac "Help" key sits where a PC keyboard has Insert;
// the keypad "Clear" key sits where a PC keypad has NumLock.

#ifndef WILLEQ_COCOA_KEYCODE_MAP_H_INCLUDED_
#define WILLEQ_COCOA_KEYCODE_MAP_H_INCLUDED_

#include <Keycodes.h> // EKEY_CODE

namespace irr
{

inline EKEY_CODE cocoaKeycodeToIrrlicht(unsigned short keyCode)
{
    switch (keyCode) {
        case 0x00: return KEY_KEY_A;   // kVK_ANSI_A
        case 0x0B: return KEY_KEY_B;   // kVK_ANSI_B
        case 0x08: return KEY_KEY_C;   // kVK_ANSI_C
        case 0x02: return KEY_KEY_D;   // kVK_ANSI_D
        case 0x0E: return KEY_KEY_E;   // kVK_ANSI_E
        case 0x03: return KEY_KEY_F;   // kVK_ANSI_F
        case 0x05: return KEY_KEY_G;   // kVK_ANSI_G
        case 0x04: return KEY_KEY_H;   // kVK_ANSI_H
        case 0x22: return KEY_KEY_I;   // kVK_ANSI_I
        case 0x26: return KEY_KEY_J;   // kVK_ANSI_J
        case 0x28: return KEY_KEY_K;   // kVK_ANSI_K
        case 0x25: return KEY_KEY_L;   // kVK_ANSI_L
        case 0x2E: return KEY_KEY_M;   // kVK_ANSI_M
        case 0x2D: return KEY_KEY_N;   // kVK_ANSI_N
        case 0x1F: return KEY_KEY_O;   // kVK_ANSI_O
        case 0x23: return KEY_KEY_P;   // kVK_ANSI_P
        case 0x0C: return KEY_KEY_Q;   // kVK_ANSI_Q
        case 0x0F: return KEY_KEY_R;   // kVK_ANSI_R
        case 0x01: return KEY_KEY_S;   // kVK_ANSI_S
        case 0x11: return KEY_KEY_T;   // kVK_ANSI_T
        case 0x20: return KEY_KEY_U;   // kVK_ANSI_U
        case 0x09: return KEY_KEY_V;   // kVK_ANSI_V
        case 0x0D: return KEY_KEY_W;   // kVK_ANSI_W
        case 0x07: return KEY_KEY_X;   // kVK_ANSI_X
        case 0x10: return KEY_KEY_Y;   // kVK_ANSI_Y
        case 0x06: return KEY_KEY_Z;   // kVK_ANSI_Z

        case 0x1D: return KEY_KEY_0;   // kVK_ANSI_0
        case 0x12: return KEY_KEY_1;   // kVK_ANSI_1
        case 0x13: return KEY_KEY_2;   // kVK_ANSI_2
        case 0x14: return KEY_KEY_3;   // kVK_ANSI_3
        case 0x15: return KEY_KEY_4;   // kVK_ANSI_4
        case 0x17: return KEY_KEY_5;   // kVK_ANSI_5
        case 0x16: return KEY_KEY_6;   // kVK_ANSI_6
        case 0x1A: return KEY_KEY_7;   // kVK_ANSI_7
        case 0x1C: return KEY_KEY_8;   // kVK_ANSI_8
        case 0x19: return KEY_KEY_9;   // kVK_ANSI_9

        case 0x7A: return KEY_F1;      // kVK_F1
        case 0x78: return KEY_F2;      // kVK_F2
        case 0x63: return KEY_F3;      // kVK_F3
        case 0x76: return KEY_F4;      // kVK_F4
        case 0x60: return KEY_F5;      // kVK_F5
        case 0x61: return KEY_F6;      // kVK_F6
        case 0x62: return KEY_F7;      // kVK_F7
        case 0x64: return KEY_F8;      // kVK_F8
        case 0x65: return KEY_F9;      // kVK_F9
        case 0x6D: return KEY_F10;     // kVK_F10
        case 0x67: return KEY_F11;     // kVK_F11
        case 0x6F: return KEY_F12;     // kVK_F12

        case 0x35: return KEY_ESCAPE;  // kVK_Escape
        case 0x30: return KEY_TAB;     // kVK_Tab
        case 0x33: return KEY_BACK;    // kVK_Delete (the backspace key)
        case 0x24: return KEY_RETURN;  // kVK_Return
        case 0x31: return KEY_SPACE;   // kVK_Space
        case 0x75: return KEY_DELETE;  // kVK_ForwardDelete
        case 0x72: return KEY_INSERT;  // kVK_Help (PC Insert position)
        case 0x73: return KEY_HOME;    // kVK_Home
        case 0x77: return KEY_END;     // kVK_End
        case 0x74: return KEY_PRIOR;   // kVK_PageUp
        case 0x79: return KEY_NEXT;    // kVK_PageDown
        case 0x39: return KEY_CAPITAL; // kVK_CapsLock
        case 0x47: return KEY_NUMLOCK; // kVK_ANSI_KeypadClear (PC NumLock position)

        case 0x7E: return KEY_UP;      // kVK_UpArrow
        case 0x7D: return KEY_DOWN;    // kVK_DownArrow
        case 0x7B: return KEY_LEFT;    // kVK_LeftArrow
        case 0x7C: return KEY_RIGHT;   // kVK_RightArrow

        case 0x38: return KEY_LSHIFT;   // kVK_Shift
        case 0x3C: return KEY_RSHIFT;   // kVK_RightShift
        case 0x3B: return KEY_LCONTROL; // kVK_Control
        case 0x3E: return KEY_RCONTROL; // kVK_RightControl
        case 0x3A: return KEY_LMENU;    // kVK_Option
        case 0x3D: return KEY_RMENU;    // kVK_RightOption

        case 0x1B: return KEY_MINUS;   // kVK_ANSI_Minus
        case 0x18: return KEY_PLUS;    // kVK_ANSI_Equal (X11 map: XK_equal -> KEY_PLUS)
        case 0x21: return KEY_OEM_4;   // kVK_ANSI_LeftBracket
        case 0x1E: return KEY_OEM_6;   // kVK_ANSI_RightBracket
        case 0x29: return KEY_OEM_1;   // kVK_ANSI_Semicolon
        case 0x27: return KEY_OEM_7;   // kVK_ANSI_Quote
        case 0x32: return KEY_OEM_3;   // kVK_ANSI_Grave
        case 0x2A: return KEY_OEM_5;   // kVK_ANSI_Backslash
        case 0x2B: return KEY_COMMA;   // kVK_ANSI_Comma
        case 0x2F: return KEY_PERIOD;  // kVK_ANSI_Period
        case 0x2C: return KEY_OEM_2;   // kVK_ANSI_Slash

        case 0x52: return KEY_NUMPAD0; // kVK_ANSI_Keypad0
        case 0x53: return KEY_NUMPAD1; // kVK_ANSI_Keypad1
        case 0x54: return KEY_NUMPAD2; // kVK_ANSI_Keypad2
        case 0x55: return KEY_NUMPAD3; // kVK_ANSI_Keypad3
        case 0x56: return KEY_NUMPAD4; // kVK_ANSI_Keypad4
        case 0x57: return KEY_NUMPAD5; // kVK_ANSI_Keypad5
        case 0x58: return KEY_NUMPAD6; // kVK_ANSI_Keypad6
        case 0x59: return KEY_NUMPAD7; // kVK_ANSI_Keypad7
        case 0x5B: return KEY_NUMPAD8; // kVK_ANSI_Keypad8
        case 0x5C: return KEY_NUMPAD9; // kVK_ANSI_Keypad9
        case 0x41: return KEY_DECIMAL;  // kVK_ANSI_KeypadDecimal
        case 0x4C: return KEY_RETURN;   // kVK_ANSI_KeypadEnter
        case 0x45: return KEY_ADD;      // kVK_ANSI_KeypadPlus
        case 0x4E: return KEY_SUBTRACT; // kVK_ANSI_KeypadMinus
        case 0x43: return KEY_MULTIPLY; // kVK_ANSI_KeypadMultiply
        case 0x4B: return KEY_DIVIDE;   // kVK_ANSI_KeypadDivide

        default: return (EKEY_CODE)0;  // Command, Fn, F13+, media keys, JIS/ISO extras
    }
}

} // end namespace irr

#endif // WILLEQ_COCOA_KEYCODE_MAP_H_INCLUDED_
