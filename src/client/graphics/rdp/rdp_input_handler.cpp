#ifdef WITH_RDP

#include "client/graphics/rdp/rdp_input_handler.h"

#include <freerdp/scancode.h>

namespace EQT {
namespace Graphics {

irr::EKEY_CODE rdpScancodeToIrrlicht(uint8_t rdpScancode, bool extended) {
    // Extended keys (arrow keys, numpad enter, etc.)
    if (extended) {
        switch (rdpScancode) {
            case 0x48: return irr::KEY_UP;
            case 0x50: return irr::KEY_DOWN;
            case 0x4B: return irr::KEY_LEFT;
            case 0x4D: return irr::KEY_RIGHT;
            case 0x47: return irr::KEY_HOME;
            case 0x4F: return irr::KEY_END;
            case 0x49: return irr::KEY_PRIOR;    // Page Up
            case 0x51: return irr::KEY_NEXT;     // Page Down
            case 0x52: return irr::KEY_INSERT;
            case 0x53: return irr::KEY_DELETE;
            case 0x1C: return irr::KEY_RETURN;   // Numpad Enter
            case 0x1D: return irr::KEY_RCONTROL;
            case 0x38: return irr::KEY_RMENU;    // Right Alt
            case 0x35: return irr::KEY_DIVIDE;   // Numpad /
            default: break;
        }
    }

    // Standard scancodes
    switch (rdpScancode) {
        // Row 1: Escape and function keys
        case 0x01: return irr::KEY_ESCAPE;
        case 0x3B: return irr::KEY_F1;
        case 0x3C: return irr::KEY_F2;
        case 0x3D: return irr::KEY_F3;
        case 0x3E: return irr::KEY_F4;
        case 0x3F: return irr::KEY_F5;
        case 0x40: return irr::KEY_F6;
        case 0x41: return irr::KEY_F7;
        case 0x42: return irr::KEY_F8;
        case 0x43: return irr::KEY_F9;
        case 0x44: return irr::KEY_F10;
        case 0x57: return irr::KEY_F11;
        case 0x58: return irr::KEY_F12;

        // Row 2: Number row
        case 0x29: return irr::KEY_OEM_3;       // ` ~
        case 0x02: return irr::KEY_KEY_1;
        case 0x03: return irr::KEY_KEY_2;
        case 0x04: return irr::KEY_KEY_3;
        case 0x05: return irr::KEY_KEY_4;
        case 0x06: return irr::KEY_KEY_5;
        case 0x07: return irr::KEY_KEY_6;
        case 0x08: return irr::KEY_KEY_7;
        case 0x09: return irr::KEY_KEY_8;
        case 0x0A: return irr::KEY_KEY_9;
        case 0x0B: return irr::KEY_KEY_0;
        case 0x0C: return irr::KEY_MINUS;
        case 0x0D: return irr::KEY_PLUS;
        case 0x0E: return irr::KEY_BACK;        // Backspace

        // Row 3: QWERTY row
        case 0x0F: return irr::KEY_TAB;
        case 0x10: return irr::KEY_KEY_Q;
        case 0x11: return irr::KEY_KEY_W;
        case 0x12: return irr::KEY_KEY_E;
        case 0x13: return irr::KEY_KEY_R;
        case 0x14: return irr::KEY_KEY_T;
        case 0x15: return irr::KEY_KEY_Y;
        case 0x16: return irr::KEY_KEY_U;
        case 0x17: return irr::KEY_KEY_I;
        case 0x18: return irr::KEY_KEY_O;
        case 0x19: return irr::KEY_KEY_P;
        case 0x1A: return irr::KEY_OEM_4;       // [ {
        case 0x1B: return irr::KEY_OEM_6;       // ] }
        case 0x2B: return irr::KEY_OEM_5;       // \ |

        // Row 4: ASDF row
        case 0x3A: return irr::KEY_CAPITAL;     // Caps Lock
        case 0x1E: return irr::KEY_KEY_A;
        case 0x1F: return irr::KEY_KEY_S;
        case 0x20: return irr::KEY_KEY_D;
        case 0x21: return irr::KEY_KEY_F;
        case 0x22: return irr::KEY_KEY_G;
        case 0x23: return irr::KEY_KEY_H;
        case 0x24: return irr::KEY_KEY_J;
        case 0x25: return irr::KEY_KEY_K;
        case 0x26: return irr::KEY_KEY_L;
        case 0x27: return irr::KEY_OEM_1;       // ; :
        case 0x28: return irr::KEY_OEM_7;       // ' "
        case 0x1C: return irr::KEY_RETURN;      // Enter

        // Row 5: ZXCV row
        case 0x2A: return irr::KEY_LSHIFT;
        case 0x2C: return irr::KEY_KEY_Z;
        case 0x2D: return irr::KEY_KEY_X;
        case 0x2E: return irr::KEY_KEY_C;
        case 0x2F: return irr::KEY_KEY_V;
        case 0x30: return irr::KEY_KEY_B;
        case 0x31: return irr::KEY_KEY_N;
        case 0x32: return irr::KEY_KEY_M;
        case 0x33: return irr::KEY_COMMA;
        case 0x34: return irr::KEY_PERIOD;
        case 0x35: return irr::KEY_OEM_2;       // / ?
        case 0x36: return irr::KEY_RSHIFT;

        // Row 6: Bottom row
        case 0x1D: return irr::KEY_LCONTROL;
        case 0x38: return irr::KEY_LMENU;       // Left Alt
        case 0x39: return irr::KEY_SPACE;

        // Numpad
        case 0x45: return irr::KEY_NUMLOCK;
        case 0x47: return irr::KEY_NUMPAD7;
        case 0x48: return irr::KEY_NUMPAD8;
        case 0x49: return irr::KEY_NUMPAD9;
        case 0x4A: return irr::KEY_SUBTRACT;
        case 0x4B: return irr::KEY_NUMPAD4;
        case 0x4C: return irr::KEY_NUMPAD5;
        case 0x4D: return irr::KEY_NUMPAD6;
        case 0x4E: return irr::KEY_ADD;
        case 0x4F: return irr::KEY_NUMPAD1;
        case 0x50: return irr::KEY_NUMPAD2;
        case 0x51: return irr::KEY_NUMPAD3;
        case 0x52: return irr::KEY_NUMPAD0;
        case 0x53: return irr::KEY_DECIMAL;
        case 0x37: return irr::KEY_MULTIPLY;

        // Special keys
        case 0x46: return irr::KEY_SCROLL;      // Scroll Lock
        case 0x54: return irr::KEY_PRINT;       // Print Screen (SysRq)
        case 0xC5: return irr::KEY_PAUSE;

        default:
            return irr::KEY_KEY_CODES_COUNT;    // Unknown key
    }
}

wchar_t rdpScancodeToChar(uint8_t rdpScancode, bool shift, bool capsLock) {
    // Determine if we should use uppercase
    bool upper = shift ^ capsLock;

    // Letter keys
    if (rdpScancode >= 0x10 && rdpScancode <= 0x19) {
        // Q W E R T Y U I O P
        static const char qwertyRow[] = "qwertyuiop";
        char c = qwertyRow[rdpScancode - 0x10];
        return upper ? (c - 32) : c;
    }
    if (rdpScancode >= 0x1E && rdpScancode <= 0x26) {
        // A S D F G H J K L
        static const char asdfRow[] = "asdfghjkl";
        char c = asdfRow[rdpScancode - 0x1E];
        return upper ? (c - 32) : c;
    }
    if (rdpScancode >= 0x2C && rdpScancode <= 0x32) {
        // Z X C V B N M
        static const char zxcvRow[] = "zxcvbnm";
        char c = zxcvRow[rdpScancode - 0x2C];
        return upper ? (c - 32) : c;
    }

    // Number row (with shift for symbols)
    if (rdpScancode >= 0x02 && rdpScancode <= 0x0B) {
        static const char numbers[] = "1234567890";
        static const char symbols[] = "!@#$%^&*()";
        return shift ? symbols[rdpScancode - 0x02] : numbers[rdpScancode - 0x02];
    }

    // Special characters
    switch (rdpScancode) {
        case 0x39: return ' ';                           // Space
        case 0x1C: return '\r';                          // Enter
        case 0x0F: return '\t';                          // Tab
        case 0x29: return shift ? '~' : '`';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x2B: return shift ? '|' : '\\';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        default: return 0;  // Non-printable
    }
}

irr::EMOUSE_INPUT_EVENT rdpMouseFlagsToIrrlicht(uint16_t rdpFlags) {
    // Check for movement first
    if (rdpFlags & PTR_FLAGS_MOVE) {
        return irr::EMIE_MOUSE_MOVED;
    }

    // Left button
    if (rdpFlags & PTR_FLAGS_BUTTON1) {
        return (rdpFlags & PTR_FLAGS_DOWN) ?
            irr::EMIE_LMOUSE_PRESSED_DOWN : irr::EMIE_LMOUSE_LEFT_UP;
    }

    // Right button
    if (rdpFlags & PTR_FLAGS_BUTTON2) {
        return (rdpFlags & PTR_FLAGS_DOWN) ?
            irr::EMIE_RMOUSE_PRESSED_DOWN : irr::EMIE_RMOUSE_LEFT_UP;
    }

    // Middle button
    if (rdpFlags & PTR_FLAGS_BUTTON3) {
        return (rdpFlags & PTR_FLAGS_DOWN) ?
            irr::EMIE_MMOUSE_PRESSED_DOWN : irr::EMIE_MMOUSE_LEFT_UP;
    }

    // Mouse wheel
    if (rdpFlags & PTR_FLAGS_WHEEL) {
        return irr::EMIE_MOUSE_WHEEL;
    }

    // Default to movement
    return irr::EMIE_MOUSE_MOVED;
}

float rdpGetWheelDelta(uint16_t rdpFlags) {
    if (!(rdpFlags & PTR_FLAGS_WHEEL)) {
        return 0.0f;
    }

    // Extract wheel rotation from lower 9 bits
    int16_t rotation = static_cast<int16_t>(rdpFlags & WheelRotationMask);

    // Check for negative rotation
    if (rdpFlags & PTR_FLAGS_WHEEL_NEGATIVE) {
        rotation = -rotation;
    }

    // Normalize to -1.0 to 1.0 range (120 = one notch)
    return static_cast<float>(rotation) / 120.0f;
}

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP
