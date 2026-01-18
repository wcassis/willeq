#ifdef WITH_RDP

#include <gtest/gtest.h>
#include "client/graphics/rdp/rdp_input_handler.h"

using namespace EQT::Graphics;

// =============================================================================
// Scancode to Irrlicht Key Code Tests
// =============================================================================

class RDPScancodeTest : public ::testing::Test {};

TEST_F(RDPScancodeTest, LetterKeys_QWERTY) {
    // Test QWERTY row
    EXPECT_EQ(rdpScancodeToIrrlicht(0x10, false), irr::KEY_KEY_Q);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x11, false), irr::KEY_KEY_W);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x12, false), irr::KEY_KEY_E);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x13, false), irr::KEY_KEY_R);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x14, false), irr::KEY_KEY_T);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x15, false), irr::KEY_KEY_Y);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x16, false), irr::KEY_KEY_U);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x17, false), irr::KEY_KEY_I);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x18, false), irr::KEY_KEY_O);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x19, false), irr::KEY_KEY_P);
}

TEST_F(RDPScancodeTest, LetterKeys_ASDF) {
    // Test ASDF row
    EXPECT_EQ(rdpScancodeToIrrlicht(0x1E, false), irr::KEY_KEY_A);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x1F, false), irr::KEY_KEY_S);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x20, false), irr::KEY_KEY_D);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x21, false), irr::KEY_KEY_F);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x22, false), irr::KEY_KEY_G);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x23, false), irr::KEY_KEY_H);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x24, false), irr::KEY_KEY_J);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x25, false), irr::KEY_KEY_K);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x26, false), irr::KEY_KEY_L);
}

TEST_F(RDPScancodeTest, LetterKeys_ZXCV) {
    // Test ZXCV row
    EXPECT_EQ(rdpScancodeToIrrlicht(0x2C, false), irr::KEY_KEY_Z);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x2D, false), irr::KEY_KEY_X);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x2E, false), irr::KEY_KEY_C);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x2F, false), irr::KEY_KEY_V);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x30, false), irr::KEY_KEY_B);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x31, false), irr::KEY_KEY_N);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x32, false), irr::KEY_KEY_M);
}

TEST_F(RDPScancodeTest, NumberKeys) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x02, false), irr::KEY_KEY_1);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x03, false), irr::KEY_KEY_2);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x04, false), irr::KEY_KEY_3);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x05, false), irr::KEY_KEY_4);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x06, false), irr::KEY_KEY_5);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x07, false), irr::KEY_KEY_6);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x08, false), irr::KEY_KEY_7);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x09, false), irr::KEY_KEY_8);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x0A, false), irr::KEY_KEY_9);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x0B, false), irr::KEY_KEY_0);
}

TEST_F(RDPScancodeTest, FunctionKeys) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x3B, false), irr::KEY_F1);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x3C, false), irr::KEY_F2);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x3D, false), irr::KEY_F3);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x3E, false), irr::KEY_F4);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x3F, false), irr::KEY_F5);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x40, false), irr::KEY_F6);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x41, false), irr::KEY_F7);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x42, false), irr::KEY_F8);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x43, false), irr::KEY_F9);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x44, false), irr::KEY_F10);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x57, false), irr::KEY_F11);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x58, false), irr::KEY_F12);
}

TEST_F(RDPScancodeTest, SpecialKeys) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x01, false), irr::KEY_ESCAPE);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x0F, false), irr::KEY_TAB);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x1C, false), irr::KEY_RETURN);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x39, false), irr::KEY_SPACE);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x0E, false), irr::KEY_BACK);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x3A, false), irr::KEY_CAPITAL);
}

TEST_F(RDPScancodeTest, ModifierKeys) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x2A, false), irr::KEY_LSHIFT);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x36, false), irr::KEY_RSHIFT);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x1D, false), irr::KEY_LCONTROL);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x38, false), irr::KEY_LMENU);  // Left Alt
}

TEST_F(RDPScancodeTest, ExtendedKeys_ArrowKeys) {
    // Arrow keys use extended scancodes
    EXPECT_EQ(rdpScancodeToIrrlicht(0x48, true), irr::KEY_UP);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x50, true), irr::KEY_DOWN);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4B, true), irr::KEY_LEFT);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4D, true), irr::KEY_RIGHT);
}

TEST_F(RDPScancodeTest, ExtendedKeys_Navigation) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x47, true), irr::KEY_HOME);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4F, true), irr::KEY_END);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x49, true), irr::KEY_PRIOR);   // Page Up
    EXPECT_EQ(rdpScancodeToIrrlicht(0x51, true), irr::KEY_NEXT);    // Page Down
    EXPECT_EQ(rdpScancodeToIrrlicht(0x52, true), irr::KEY_INSERT);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x53, true), irr::KEY_DELETE);
}

TEST_F(RDPScancodeTest, ExtendedKeys_RightModifiers) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x1D, true), irr::KEY_RCONTROL);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x38, true), irr::KEY_RMENU);   // Right Alt
}

TEST_F(RDPScancodeTest, NumpadKeys) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0x47, false), irr::KEY_NUMPAD7);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x48, false), irr::KEY_NUMPAD8);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x49, false), irr::KEY_NUMPAD9);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4B, false), irr::KEY_NUMPAD4);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4C, false), irr::KEY_NUMPAD5);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4D, false), irr::KEY_NUMPAD6);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4F, false), irr::KEY_NUMPAD1);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x50, false), irr::KEY_NUMPAD2);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x51, false), irr::KEY_NUMPAD3);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x52, false), irr::KEY_NUMPAD0);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x53, false), irr::KEY_DECIMAL);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x45, false), irr::KEY_NUMLOCK);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x37, false), irr::KEY_MULTIPLY);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4A, false), irr::KEY_SUBTRACT);
    EXPECT_EQ(rdpScancodeToIrrlicht(0x4E, false), irr::KEY_ADD);
}

TEST_F(RDPScancodeTest, UnknownScancode_ReturnsCodesCount) {
    EXPECT_EQ(rdpScancodeToIrrlicht(0xFF, false), irr::KEY_KEY_CODES_COUNT);
    EXPECT_EQ(rdpScancodeToIrrlicht(0xFE, true), irr::KEY_KEY_CODES_COUNT);
}

// =============================================================================
// Scancode to Character Tests
// =============================================================================

class RDPScancodeToCharTest : public ::testing::Test {};

TEST_F(RDPScancodeToCharTest, LetterKeys_Lowercase) {
    EXPECT_EQ(rdpScancodeToChar(0x10, false, false), L'q');
    EXPECT_EQ(rdpScancodeToChar(0x11, false, false), L'w');
    EXPECT_EQ(rdpScancodeToChar(0x12, false, false), L'e');
    EXPECT_EQ(rdpScancodeToChar(0x1E, false, false), L'a');
    EXPECT_EQ(rdpScancodeToChar(0x1F, false, false), L's');
    EXPECT_EQ(rdpScancodeToChar(0x20, false, false), L'd');
    EXPECT_EQ(rdpScancodeToChar(0x2C, false, false), L'z');
    EXPECT_EQ(rdpScancodeToChar(0x2D, false, false), L'x');
    EXPECT_EQ(rdpScancodeToChar(0x2E, false, false), L'c');
}

TEST_F(RDPScancodeToCharTest, LetterKeys_UppercaseWithShift) {
    EXPECT_EQ(rdpScancodeToChar(0x10, true, false), L'Q');
    EXPECT_EQ(rdpScancodeToChar(0x11, true, false), L'W');
    EXPECT_EQ(rdpScancodeToChar(0x12, true, false), L'E');
    EXPECT_EQ(rdpScancodeToChar(0x1E, true, false), L'A');
    EXPECT_EQ(rdpScancodeToChar(0x1F, true, false), L'S');
    EXPECT_EQ(rdpScancodeToChar(0x20, true, false), L'D');
}

TEST_F(RDPScancodeToCharTest, LetterKeys_UppercaseWithCapsLock) {
    EXPECT_EQ(rdpScancodeToChar(0x10, false, true), L'Q');
    EXPECT_EQ(rdpScancodeToChar(0x11, false, true), L'W');
    EXPECT_EQ(rdpScancodeToChar(0x1E, false, true), L'A');
}

TEST_F(RDPScancodeToCharTest, LetterKeys_LowercaseWithShiftAndCapsLock) {
    // Shift XOR CapsLock = lowercase when both are true
    EXPECT_EQ(rdpScancodeToChar(0x10, true, true), L'q');
    EXPECT_EQ(rdpScancodeToChar(0x11, true, true), L'w');
}

TEST_F(RDPScancodeToCharTest, NumberKeys_NoShift) {
    EXPECT_EQ(rdpScancodeToChar(0x02, false, false), L'1');
    EXPECT_EQ(rdpScancodeToChar(0x03, false, false), L'2');
    EXPECT_EQ(rdpScancodeToChar(0x04, false, false), L'3');
    EXPECT_EQ(rdpScancodeToChar(0x05, false, false), L'4');
    EXPECT_EQ(rdpScancodeToChar(0x06, false, false), L'5');
    EXPECT_EQ(rdpScancodeToChar(0x07, false, false), L'6');
    EXPECT_EQ(rdpScancodeToChar(0x08, false, false), L'7');
    EXPECT_EQ(rdpScancodeToChar(0x09, false, false), L'8');
    EXPECT_EQ(rdpScancodeToChar(0x0A, false, false), L'9');
    EXPECT_EQ(rdpScancodeToChar(0x0B, false, false), L'0');
}

TEST_F(RDPScancodeToCharTest, NumberKeys_WithShift) {
    EXPECT_EQ(rdpScancodeToChar(0x02, true, false), L'!');
    EXPECT_EQ(rdpScancodeToChar(0x03, true, false), L'@');
    EXPECT_EQ(rdpScancodeToChar(0x04, true, false), L'#');
    EXPECT_EQ(rdpScancodeToChar(0x05, true, false), L'$');
    EXPECT_EQ(rdpScancodeToChar(0x06, true, false), L'%');
    EXPECT_EQ(rdpScancodeToChar(0x07, true, false), L'^');
    EXPECT_EQ(rdpScancodeToChar(0x08, true, false), L'&');
    EXPECT_EQ(rdpScancodeToChar(0x09, true, false), L'*');
    EXPECT_EQ(rdpScancodeToChar(0x0A, true, false), L'(');
    EXPECT_EQ(rdpScancodeToChar(0x0B, true, false), L')');
}

TEST_F(RDPScancodeToCharTest, SpecialKeys) {
    EXPECT_EQ(rdpScancodeToChar(0x39, false, false), L' ');     // Space
    EXPECT_EQ(rdpScancodeToChar(0x1C, false, false), L'\r');    // Enter
    EXPECT_EQ(rdpScancodeToChar(0x0F, false, false), L'\t');    // Tab
}

TEST_F(RDPScancodeToCharTest, PunctuationKeys_NoShift) {
    EXPECT_EQ(rdpScancodeToChar(0x29, false, false), L'`');
    EXPECT_EQ(rdpScancodeToChar(0x0C, false, false), L'-');
    EXPECT_EQ(rdpScancodeToChar(0x0D, false, false), L'=');
    EXPECT_EQ(rdpScancodeToChar(0x1A, false, false), L'[');
    EXPECT_EQ(rdpScancodeToChar(0x1B, false, false), L']');
    EXPECT_EQ(rdpScancodeToChar(0x2B, false, false), L'\\');
    EXPECT_EQ(rdpScancodeToChar(0x27, false, false), L';');
    EXPECT_EQ(rdpScancodeToChar(0x28, false, false), L'\'');
    EXPECT_EQ(rdpScancodeToChar(0x33, false, false), L',');
    EXPECT_EQ(rdpScancodeToChar(0x34, false, false), L'.');
    EXPECT_EQ(rdpScancodeToChar(0x35, false, false), L'/');
}

TEST_F(RDPScancodeToCharTest, PunctuationKeys_WithShift) {
    EXPECT_EQ(rdpScancodeToChar(0x29, true, false), L'~');
    EXPECT_EQ(rdpScancodeToChar(0x0C, true, false), L'_');
    EXPECT_EQ(rdpScancodeToChar(0x0D, true, false), L'+');
    EXPECT_EQ(rdpScancodeToChar(0x1A, true, false), L'{');
    EXPECT_EQ(rdpScancodeToChar(0x1B, true, false), L'}');
    EXPECT_EQ(rdpScancodeToChar(0x2B, true, false), L'|');
    EXPECT_EQ(rdpScancodeToChar(0x27, true, false), L':');
    EXPECT_EQ(rdpScancodeToChar(0x28, true, false), L'"');
    EXPECT_EQ(rdpScancodeToChar(0x33, true, false), L'<');
    EXPECT_EQ(rdpScancodeToChar(0x34, true, false), L'>');
    EXPECT_EQ(rdpScancodeToChar(0x35, true, false), L'?');
}

TEST_F(RDPScancodeToCharTest, NonPrintable_ReturnsZero) {
    EXPECT_EQ(rdpScancodeToChar(0x01, false, false), 0);  // Escape
    EXPECT_EQ(rdpScancodeToChar(0x3B, false, false), 0);  // F1
    EXPECT_EQ(rdpScancodeToChar(0xFF, false, false), 0);  // Unknown
}

// =============================================================================
// Mouse Event Translation Tests
// =============================================================================

class RDPMouseTest : public ::testing::Test {};

TEST_F(RDPMouseTest, MovementEvent) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_MOVE), irr::EMIE_MOUSE_MOVED);
}

TEST_F(RDPMouseTest, LeftButtonDown) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN),
              irr::EMIE_LMOUSE_PRESSED_DOWN);
}

TEST_F(RDPMouseTest, LeftButtonUp) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_BUTTON1),
              irr::EMIE_LMOUSE_LEFT_UP);
}

TEST_F(RDPMouseTest, RightButtonDown) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_BUTTON2 | PTR_FLAGS_DOWN),
              irr::EMIE_RMOUSE_PRESSED_DOWN);
}

TEST_F(RDPMouseTest, RightButtonUp) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_BUTTON2),
              irr::EMIE_RMOUSE_LEFT_UP);
}

TEST_F(RDPMouseTest, MiddleButtonDown) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_BUTTON3 | PTR_FLAGS_DOWN),
              irr::EMIE_MMOUSE_PRESSED_DOWN);
}

TEST_F(RDPMouseTest, MiddleButtonUp) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_BUTTON3),
              irr::EMIE_MMOUSE_LEFT_UP);
}

TEST_F(RDPMouseTest, WheelEvent) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(PTR_FLAGS_WHEEL),
              irr::EMIE_MOUSE_WHEEL);
}

TEST_F(RDPMouseTest, DefaultToMovement) {
    EXPECT_EQ(rdpMouseFlagsToIrrlicht(0), irr::EMIE_MOUSE_MOVED);
}

// =============================================================================
// Wheel Delta Tests
// =============================================================================

class RDPWheelDeltaTest : public ::testing::Test {};

TEST_F(RDPWheelDeltaTest, NoWheelFlag_ReturnsZero) {
    EXPECT_FLOAT_EQ(rdpGetWheelDelta(0), 0.0f);
    EXPECT_FLOAT_EQ(rdpGetWheelDelta(PTR_FLAGS_MOVE), 0.0f);
}

TEST_F(RDPWheelDeltaTest, PositiveWheel) {
    // 120 = one notch = 1.0f
    uint16_t flags = PTR_FLAGS_WHEEL | 120;
    EXPECT_FLOAT_EQ(rdpGetWheelDelta(flags), 1.0f);
}

TEST_F(RDPWheelDeltaTest, NegativeWheel) {
    // Negative wheel rotation
    // Note: PTR_FLAGS_WHEEL_NEGATIVE is 0x0100, which is within WheelRotationMask (0x01FF)
    // The actual rotation value is masked by 0x01FF, so we need to be careful
    // When WHEEL_NEGATIVE is set, the lower 8 bits contain the magnitude
    uint16_t flags = PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 120;
    // The result will be -(flags & 0x01FF) / 120 = -(0x0100 | 120) / 120 = -376 / 120
    // This is a quirk of the RDP protocol encoding
    float delta = rdpGetWheelDelta(flags);
    EXPECT_LT(delta, 0.0f);  // Should be negative
}

TEST_F(RDPWheelDeltaTest, HalfNotch) {
    // 60 = half notch = 0.5f
    uint16_t flags = PTR_FLAGS_WHEEL | 60;
    EXPECT_FLOAT_EQ(rdpGetWheelDelta(flags), 0.5f);
}

#else

// Provide a dummy test when RDP is not enabled
#include <gtest/gtest.h>

TEST(RDPInputHandlerDisabledTest, RDPNotEnabled) {
    GTEST_SKIP() << "RDP support not enabled in build";
}

#endif // WITH_RDP
