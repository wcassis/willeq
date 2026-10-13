#pragma once

// Include evdev before Irrlicht: their KEY_* names overlap. The device has
// already saved the overlapping evdev codes as LK_* before including this.
#include <linux/input.h>
#undef KEY_BACK
#undef KEY_TAB
#undef KEY_CANCEL
#undef KEY_CLEAR
#undef KEY_MENU
#undef KEY_PAUSE
#undef KEY_SPACE
#undef KEY_NEXT
#undef KEY_HOME
#undef KEY_END
#undef KEY_LEFT
#undef KEY_UP
#undef KEY_RIGHT
#undef KEY_DOWN
#undef KEY_SELECT
#undef KEY_PRINT
#undef KEY_INSERT
#undef KEY_DELETE
#undef KEY_HELP
#undef KEY_SLEEP
#undef KEY_HANGUEL
#undef KEY_HANJA
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#undef KEY_F12
#undef KEY_F13
#undef KEY_F14
#undef KEY_F15
#undef KEY_F16
#undef KEY_F17
#undef KEY_F18
#undef KEY_F19
#undef KEY_F20
#undef KEY_F21
#undef KEY_F22
#undef KEY_F23
#undef KEY_F24
#undef KEY_NUMLOCK
#undef KEY_MINUS
#undef KEY_COMMA
#undef KEY_PLAY
#undef KEY_ZOOM
#include <irrlicht.h>

namespace eqt_drm {

// Accept only EV_KEY values 0 (release), 1 (press), and 2 (repeat).
// A repeat is another key-down event; held-key state and modifiers stay down.
inline bool translateKeyEvent(const input_event& input, irr::EKEY_CODE key,
                              bool* keyIsDown, unsigned keyCount,
                              bool& shiftDown, bool& ctrlDown, bool& altDown,
                              irr::SEvent& event)
{
    if (input.type != EV_KEY || input.value < 0 || input.value > 2 ||
        input.code >= keyCount)
        return false;
    const bool down = input.value != 0;
    keyIsDown[input.code] = down;
    if (input.code == KEY_LEFTSHIFT || input.code == KEY_RIGHTSHIFT)
        shiftDown = down;
    else if (input.code == KEY_LEFTCTRL || input.code == KEY_RIGHTCTRL)
        ctrlDown = down;
    else if (input.code == KEY_LEFTALT || input.code == KEY_RIGHTALT)
        altDown = down;

    event.EventType = irr::EET_KEY_INPUT_EVENT;
    event.KeyInput.Key = key;
    event.KeyInput.PressedDown = down;
    event.KeyInput.Shift = shiftDown && input.code != KEY_EQUAL;
    event.KeyInput.Control = ctrlDown;
    event.KeyInput.Char = 0;
    if (!down)
        return true;

    if (key >= irr::KEY_KEY_A && key <= irr::KEY_KEY_Z) {
        event.KeyInput.Char = static_cast<wchar_t>((shiftDown ? 'A' : 'a') + key - irr::KEY_KEY_A);
    } else if (key >= irr::KEY_KEY_0 && key <= irr::KEY_KEY_9) {
        const char shifted[] = ")!@#$%^&*(";
        const int index = key - irr::KEY_KEY_0;
        event.KeyInput.Char = shiftDown ? shifted[index] : static_cast<wchar_t>('0' + index);
    } else {
        switch (key) {
            case irr::KEY_SPACE: event.KeyInput.Char = L' '; break;
            case irr::KEY_RETURN: event.KeyInput.Char = L'\r'; break;
            case irr::KEY_TAB: event.KeyInput.Char = L'\t'; break;
            case irr::KEY_BACK: event.KeyInput.Char = L'\b'; break;
            case irr::KEY_MINUS: event.KeyInput.Char = shiftDown ? L'_' : L'-'; break;
            case irr::KEY_PLUS: event.KeyInput.Char = shiftDown ? L'+' : L'='; break;
            case irr::KEY_COMMA: event.KeyInput.Char = shiftDown ? L'<' : L','; break;
            case irr::KEY_PERIOD: event.KeyInput.Char = shiftDown ? L'>' : L'.'; break;
            case irr::KEY_OEM_2: event.KeyInput.Char = shiftDown ? L'?' : L'/'; break;
            case irr::KEY_OEM_1: event.KeyInput.Char = shiftDown ? L':' : L';'; break;
            case irr::KEY_OEM_7: event.KeyInput.Char = shiftDown ? L'"' : L'\''; break;
            case irr::KEY_OEM_4: event.KeyInput.Char = shiftDown ? L'{' : L'['; break;
            case irr::KEY_OEM_6: event.KeyInput.Char = shiftDown ? L'}' : L']'; break;
            case irr::KEY_OEM_5: event.KeyInput.Char = shiftDown ? L'|' : L'\\'; break;
            case irr::KEY_OEM_3: event.KeyInput.Char = shiftDown ? L'~' : L'`'; break;
            default: break;
        }
    }
    return true;
}

} // namespace eqt_drm
