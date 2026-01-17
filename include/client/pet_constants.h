/*
 * WillEQ Pet Constants
 *
 * Defines pet command IDs and UI button IDs for the pet system.
 * Based on EQEmu zone/common.h pet constants.
 */

#ifndef EQT_PET_CONSTANTS_H
#define EQT_PET_CONSTANTS_H

#include <cstdint>

namespace EQT {

/*
 * Pet Command IDs - TITANIUM CLIENT VALUES
 *
 * These are the command values sent in OP_PetCommands packets for the
 * Titanium client. The server's titanium.cpp decoder translates these
 * to internal server values.
 *
 * IMPORTANT: These values are Titanium-specific and differ from the
 * server's internal PetCommand enum (in emu_constants.h).
 */
enum PetCommand : uint32_t {
    // Titanium client command values (from titanium.cpp DECODE(OP_PetCommands))
    PET_BACKOFF         = 1,    // /pet back off - Stop attacking, return to owner
    PET_GETLOST         = 2,    // /pet get lost - Dismiss pet
    PET_ASYOUWERE       = 3,    // /pet as you were - Return to previous state (mapped to follow)
    PET_HEALTHREPORT    = 4,    // /pet health - Report pet health status
    PET_GUARDHERE       = 5,    // /pet guard - Guard current location
    PET_GUARDME         = 6,    // /pet guard me - Guard owner (mapped to follow)
    PET_ATTACK          = 7,    // /pet attack - Attack current target
    PET_FOLLOWME        = 8,    // /pet follow - Follow owner
    PET_SITDOWN         = 9,    // /pet sit on - Force sit down
    PET_STANDUP         = 10,   // /pet sit off - Force stand up
    PET_TAUNT           = 11,   // /pet taunt - Toggle taunt mode
    PET_HOLD            = 12,   // /pet hold - Toggle hold mode
    PET_TAUNT_ON        = 13,   // /pet taunt on
    PET_TAUNT_OFF       = 14,   // /pet taunt off
    // 15 = target command (doesn't send packet)
    PET_LEADER          = 16,   // /pet leader - Show who the pet follows
    PET_FEIGN           = 17,   // /pet feign - Feign death (if pet has ability)
    PET_SPELLHOLD       = 18,   // /pet no cast - Toggle spell casting
    PET_FOCUS           = 19,   // /pet focus - Toggle focus on target
    PET_HOLD_ON         = 20,   // /pet hold on
    PET_HOLD_OFF        = 21,   // /pet hold off

    // Convenience alias
    PET_SIT             = PET_SITDOWN,  // /pet sit - Sit down
};

/*
 * Pet Button IDs
 *
 * These are the button IDs sent in OP_PetCommandState packets from the server.
 * They indicate which UI button's state should be updated (pressed/unpressed).
 * Toggle commands (follow, guard, sit, taunt, hold, etc.) use these to reflect
 * the current pet state in the UI.
 */
enum PetButton : uint32_t {
    PET_BUTTON_SIT       = 0,   // Sit button state
    PET_BUTTON_STOP      = 1,   // Stop button state
    PET_BUTTON_REGROUP   = 2,   // Regroup button state
    PET_BUTTON_FOLLOW    = 3,   // Follow button state
    PET_BUTTON_GUARD     = 4,   // Guard button state
    PET_BUTTON_TAUNT     = 5,   // Taunt button state
    PET_BUTTON_HOLD      = 6,   // Hold button state
    PET_BUTTON_GHOLD     = 7,   // GHold (group hold) button state
    PET_BUTTON_FOCUS     = 8,   // Focus button state
    PET_BUTTON_SPELLHOLD = 9,   // SpellHold (no cast) button state
    PET_BUTTON_COUNT     = 10   // Total number of pet buttons
};

/*
 * Helper function to get a human-readable name for a pet command
 */
inline const char* GetPetCommandName(PetCommand cmd) {
    switch (cmd) {
        case PET_BACKOFF:       return "back off";
        case PET_GETLOST:       return "get lost";
        case PET_ASYOUWERE:     return "as you were";
        case PET_HEALTHREPORT:  return "health";
        case PET_GUARDHERE:     return "guard";
        case PET_GUARDME:       return "guard me";
        case PET_ATTACK:        return "attack";
        case PET_FOLLOWME:      return "follow";
        case PET_SITDOWN:       return "sit";
        case PET_STANDUP:       return "stand";
        case PET_TAUNT:         return "taunt";
        case PET_HOLD:          return "hold";
        case PET_TAUNT_ON:      return "taunt on";
        case PET_TAUNT_OFF:     return "taunt off";
        case PET_LEADER:        return "leader";
        case PET_FEIGN:         return "feign";
        case PET_SPELLHOLD:     return "no cast";
        case PET_FOCUS:         return "focus";
        case PET_HOLD_ON:       return "hold on";
        case PET_HOLD_OFF:      return "hold off";
        default:                return "unknown";
    }
}

/*
 * Helper function to get a human-readable name for a pet button
 */
inline const char* GetPetButtonName(PetButton btn) {
    switch (btn) {
        case PET_BUTTON_SIT:       return "Sit";
        case PET_BUTTON_STOP:      return "Stop";
        case PET_BUTTON_REGROUP:   return "Regroup";
        case PET_BUTTON_FOLLOW:    return "Follow";
        case PET_BUTTON_GUARD:     return "Guard";
        case PET_BUTTON_TAUNT:     return "Taunt";
        case PET_BUTTON_HOLD:      return "Hold";
        case PET_BUTTON_GHOLD:     return "GHold";
        case PET_BUTTON_FOCUS:     return "Focus";
        case PET_BUTTON_SPELLHOLD: return "SpellHold";
        default:                   return "Unknown";
    }
}

} // namespace EQT

#endif // EQT_PET_CONSTANTS_H
