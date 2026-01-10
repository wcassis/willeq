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
 * Pet Command IDs
 *
 * These are the command values sent in OP_PetCommands packets.
 * The client sends these when issuing pet commands via /pet slash commands
 * or clicking pet window buttons.
 */
enum PetCommand : uint32_t {
    PET_HEALTHREPORT    = 0,    // /pet health - Report pet health status
    PET_LEADER          = 1,    // /pet leader - Show who the pet follows
    PET_ATTACK          = 2,    // /pet attack - Attack current target
    PET_QATTACK         = 3,    // /pet qattack - Queue attack on player's target
    PET_FOLLOWME        = 4,    // /pet follow - Follow owner
    PET_GUARDHERE       = 5,    // /pet guard - Guard current location
    PET_SIT             = 6,    // /pet sit - Toggle sit
    PET_SITDOWN         = 7,    // /pet sit on - Force sit down
    PET_STANDUP         = 8,    // /pet sit off - Force stand up
    PET_STOP            = 9,    // /pet stop - Stop movement (not implemented on most servers)
    PET_STOP_ON         = 10,   // /pet stop on
    PET_STOP_OFF        = 11,   // /pet stop off
    PET_TAUNT           = 12,   // /pet taunt - Toggle taunt mode
    PET_TAUNT_ON        = 13,   // /pet taunt on
    PET_TAUNT_OFF       = 14,   // /pet taunt off
    PET_HOLD            = 15,   // /pet hold - Won't add to hate list unless attacking
    PET_HOLD_ON         = 16,   // /pet hold on
    PET_HOLD_OFF        = 17,   // /pet hold off
    PET_GHOLD           = 18,   // /pet ghold - Never adds to hate list
    PET_GHOLD_ON        = 19,   // /pet ghold on
    PET_GHOLD_OFF       = 20,   // /pet ghold off
    PET_SPELLHOLD       = 21,   // /pet no cast - Don't cast spells
    PET_SPELLHOLD_ON    = 22,   // /pet spellhold on
    PET_SPELLHOLD_OFF   = 23,   // /pet spellhold off
    PET_FOCUS           = 24,   // /pet focus - Focus on target
    PET_FOCUS_ON        = 25,   // /pet focus on
    PET_FOCUS_OFF       = 26,   // /pet focus off
    PET_FEIGN           = 27,   // /pet feign - Feign death (if pet has ability)
    PET_BACKOFF         = 28,   // /pet back off - Stop attacking, return to owner
    PET_GETLOST         = 29,   // /pet get lost - Dismiss pet
    PET_GUARDME         = 30,   // Same as follow (older clients)
    PET_REGROUP         = 31,   // /pet regroup - Stop attack, return to guard position
    PET_REGROUP_ON      = 32,   // /pet regroup on
    PET_REGROUP_OFF     = 33,   // /pet regroup off
    PET_MAXCOMMANDS     = 34    // Total number of pet commands
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
        case PET_HEALTHREPORT:  return "health";
        case PET_LEADER:        return "leader";
        case PET_ATTACK:        return "attack";
        case PET_QATTACK:       return "qattack";
        case PET_FOLLOWME:      return "follow";
        case PET_GUARDHERE:     return "guard";
        case PET_SIT:           return "sit";
        case PET_SITDOWN:       return "sit on";
        case PET_STANDUP:       return "sit off";
        case PET_STOP:          return "stop";
        case PET_STOP_ON:       return "stop on";
        case PET_STOP_OFF:      return "stop off";
        case PET_TAUNT:         return "taunt";
        case PET_TAUNT_ON:      return "taunt on";
        case PET_TAUNT_OFF:     return "taunt off";
        case PET_HOLD:          return "hold";
        case PET_HOLD_ON:       return "hold on";
        case PET_HOLD_OFF:      return "hold off";
        case PET_GHOLD:         return "ghold";
        case PET_GHOLD_ON:      return "ghold on";
        case PET_GHOLD_OFF:     return "ghold off";
        case PET_SPELLHOLD:     return "no cast";
        case PET_SPELLHOLD_ON:  return "spellhold on";
        case PET_SPELLHOLD_OFF: return "spellhold off";
        case PET_FOCUS:         return "focus";
        case PET_FOCUS_ON:      return "focus on";
        case PET_FOCUS_OFF:     return "focus off";
        case PET_FEIGN:         return "feign";
        case PET_BACKOFF:       return "back off";
        case PET_GETLOST:       return "get lost";
        case PET_GUARDME:       return "guardme";
        case PET_REGROUP:       return "regroup";
        case PET_REGROUP_ON:    return "regroup on";
        case PET_REGROUP_OFF:   return "regroup off";
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
