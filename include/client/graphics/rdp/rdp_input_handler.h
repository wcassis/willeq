#ifndef EQT_GRAPHICS_RDP_INPUT_HANDLER_H
#define EQT_GRAPHICS_RDP_INPUT_HANDLER_H

#ifdef WITH_RDP

#include <irrlicht.h>
#include <freerdp/input.h>
#include <cstdint>

namespace EQT {
namespace Graphics {

/**
 * Translates RDP scancodes to Irrlicht key codes.
 *
 * RDP uses hardware scancodes (similar to Windows virtual key codes),
 * while Irrlicht uses its own EKEY_CODE enum. This function provides
 * the mapping between them.
 *
 * @param rdpScancode The RDP scancode (8-bit value + extended flag)
 * @param extended Whether the extended flag is set (e.g., for arrow keys)
 * @return The corresponding Irrlicht EKEY_CODE, or KEY_KEY_CODES_COUNT if unknown
 */
irr::EKEY_CODE rdpScancodeToIrrlicht(uint8_t rdpScancode, bool extended);

/**
 * Get the character representation of a key event.
 *
 * Used for text input - returns the ASCII/Unicode character for printable keys.
 *
 * @param rdpScancode The RDP scancode
 * @param shift Whether shift is held
 * @param capsLock Whether caps lock is active
 * @return The character, or 0 for non-printable keys
 */
wchar_t rdpScancodeToChar(uint8_t rdpScancode, bool shift, bool capsLock);

/**
 * Translate RDP mouse flags to Irrlicht mouse event type.
 *
 * @param rdpFlags The RDP pointer flags (PTR_FLAGS_*)
 * @return The corresponding Irrlicht mouse event type
 */
irr::EMOUSE_INPUT_EVENT rdpMouseFlagsToIrrlicht(uint16_t rdpFlags);

/**
 * Extract wheel delta from RDP mouse flags.
 *
 * RDP encodes wheel rotation in the lower 9 bits of the flags when
 * PTR_FLAGS_WHEEL is set.
 *
 * @param rdpFlags The RDP pointer flags
 * @return The wheel delta (positive = scroll up, negative = scroll down)
 */
float rdpGetWheelDelta(uint16_t rdpFlags);

} // namespace Graphics
} // namespace EQT

#endif // WITH_RDP

#endif // EQT_GRAPHICS_RDP_INPUT_HANDLER_H
