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
 * Caps-lock (and friends) tracking for the RDP key stream (T317).
 *
 * WHY THIS IS NOT JUST ANOTHER MODIFIER
 *
 *   `IrrlichtRenderer::handleRDPKeyboard` reads shift by asking the Irrlicht
 *   event receiver whether KEY_LSHIFT/KEY_RSHIFT are *down* -- correct for a
 *   held modifier, useless for a lock. Caps lock is a TOGGLE: it flips on a
 *   key-down and stays flipped after the key comes back up, so there is no
 *   "is it down" question to ask. It has to be remembered.
 *
 *   Two inputs feed it, and both matter:
 *     - The key stream itself. RDP scancode 0x3A, key-down (KBD_FLAGS_RELEASE
 *       clear). Auto-repeat sends repeated downs while the key is held, so a
 *       down is only a toggle if the key was not already down.
 *     - The client's own lock state, which RDP sends in its synchronize /
 *       KBD_SYNC message (`rdpInput::SynchronizeEvent`). This is the ONLY way
 *       to learn that caps lock was already on when the session connected --
 *       the key stream cannot tell us about a press that happened before we
 *       were listening. FreeRDP's KBD_SYNC_CAPS_LOCK is 0x00000004
 *       (freerdp/input.h: SCROLL 0x1, NUM 0x2, CAPS 0x4, KANA 0x8).
 */
class RDPLockKeyState {
public:
    /** RDP scancode of the caps-lock key. */
    static constexpr uint8_t kCapsLockScancode = 0x3A;
    /** KBD_FLAGS_RELEASE, as handleRDPKeyboard reads it. */
    static constexpr uint16_t kFlagRelease = 0x8000;
    /** FreeRDP KBD_SYNC_CAPS_LOCK (freerdp/input.h). NOT 0x2 -- that is
     *  KBD_SYNC_NUM_LOCK. */
    static constexpr uint32_t kSyncCapsLock = 0x00000004;

    /**
     * Feed one key event from the RDP stream. Ignores everything but caps
     * lock, ignores releases, and ignores auto-repeat downs.
     */
    void applyKeyEvent(uint16_t flags, uint8_t scancode);

    /** Feed the flags from an RDP synchronize (KBD_SYNC) message. */
    void applySynchronize(uint32_t syncFlags);

    bool capsLock() const { return capsLock_; }

private:
    bool capsLock_ = false;
    bool capsKeyDown_ = false;
};

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
