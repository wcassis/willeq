// Free chat-channel, appearance-change and result value types used across the
// action layer.
// Extracted from the action layer's original combined header by refactor unit
// J15 (its plan lives in refactor/). Move-only: nothing was edited.

#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <optional>
#include <vector>  // T149: recipesFavorite() takes a recipe-id list
#include <glm/glm.hpp>
#include "client/hotbar/hotbar_slot_type.h"  // T239: activateHotbarCommandSlot

namespace eqt {

// Forward declarations
namespace state {
class GameState;
}

namespace action {

/**
 * ChatChannel - Types of chat channels.
 */
enum class ChatChannel {
    Say,
    Shout,
    OOC,
    Auction,
    Tell,
    Group,
    Guild,
    Raid,
    Emote
};

/**
 * AppearanceChange - T153: the seven fields of a Titanium OP_FaceChange, each
 * optional.
 *
 * `/appearance` sets fields by name, but the wire struct is all-or-nothing:
 * eqemu's Handle_OP_FaceChange (zone/client_packet.cpp:6387) copies all seven
 * into the PlayerProfile and Save()s. So the unset fields have to be filled in
 * from the character's current appearance, and only the handler can see that
 * state -- the merge deliberately happens there, not here, and this type
 * carries "what the user asked to change" rather than "what goes on the wire".
 *
 * Note for the merge site: this client parses eyecolor1/eyecolor2 nowhere it
 * keeps them (EverQuest::ZoneProcessPlayerProfile, eq_zone_entry.cpp, drops
 * them into unused locals, and Entity has no eyecolor fields at all), so they cannot
 * be preserved and default to 0. The handler prints the full seven-field body
 * it is about to send so nothing is changed silently.
 */
struct AppearanceChange {
    std::optional<uint8_t> haircolor;
    std::optional<uint8_t> beardcolor;
    std::optional<uint8_t> eyecolor1;
    std::optional<uint8_t> eyecolor2;
    std::optional<uint8_t> hairstyle;
    std::optional<uint8_t> beard;
    std::optional<uint8_t> face;

    bool empty() const {
        return !haircolor && !beardcolor && !eyecolor1 && !eyecolor2 &&
               !hairstyle && !beard && !face;
    }
};

/**
 * ActionResult - Result of an action attempt.
 */
struct ActionResult {
    bool success = false;
    std::string message;  // Error or status message

    static ActionResult Success(const std::string& msg = "") {
        return {true, msg};
    }

    static ActionResult Failure(const std::string& msg) {
        return {false, msg};
    }
};

} // namespace action
} // namespace eqt
