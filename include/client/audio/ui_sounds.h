#pragma once

#ifdef WITH_AUDIO

#include "client/audio/sound_assets.h"   // SoundBank -- the measured id table

#include <string>
#include <cstdint>
#include <optional>

namespace EQT {
namespace Audio {

// UI sound types for various interface events
enum class UISoundType {
    // Player events
    LevelUp,            // Player levels up
    LevelDown,          // Player loses a level (death penalty)
    EndQuest,           // Quest completion

    // Bell sounds
    BoatBell,           // Ship/boat bell

    // Interaction sounds
    // T328 MEASURED (review/T328/README.md row 8): BuyItem.WAV is the generic
    // COIN-MOVE sound -- it fired when 1pp moved from a money slot to the
    // cursor with no merchant anywhere, and the same dialog for an ITEM stack
    // was silent. Buy and sell themselves were NOT measured (Leg A could not
    // open a merchant window under Xvfb).
    BuyItem,            // Coin moved onto the cursor (measured); buy (unmeasured)
    SellItem,           // Selling to vendor -- UNMEASURED, aliases BuyItem

    // Container sounds
    // T328 MEASURED: Chest_Op/Chest_Cl are NOT the bag sounds. A container
    // opened or closed from the inventory (and a tradeskill container) plays
    // ItemCloth. What Chest_* is actually for was not measured.
    ChestOpen,          // Opening a world chest (unmeasured)
    ChestClose,         // Closing a world chest (unmeasured)

    // T328 MEASURED (review/T328/README.md rows 1-6): the two container/item
    // sounds the official Titanium client actually plays.
    ItemCloth,          // Container open OR close (bag, and tradeskill container)
    WearCloth,          // An item dropped into an inventory / bag slot

    // Door sounds
    DoorWoodOpen,       // Wooden door opening
    DoorWoodClose,      // Wooden door closing
    DoorMetalOpen,      // Metal door opening
    DoorMetalClose,     // Metal door closing
    DoorStoneOpen,      // Stone door opening
    DoorStoneClose,     // Stone door closing
    DoorSecret,         // Secret door
    TrapDoor,           // Trap door

    // UI interaction sounds
    // T328 MEASURED: the official client PRELOADS Button_1.WAV and plays it
    // for no button tested (six distinct buttons, review/T328/README.md row
    // 12). Catalogued, deliberately NOT wired -- wiring it would invent a
    // sound the oracle does not make.
    ButtonClick,        // Button click -- preloaded, never played. Do not wire.

    // Notification sounds
    YouveGotMail,       // Mail notification

    // Effects sounds
    Teleport,           // Teleport effect

    // Player state sounds
    Drink,              // Drinking sound

    // Total count for iteration
    Count
};

// Utility class for UI sound mapping
class UISounds {
public:
    // Get the filename for a UI sound type
    // Returns the WAV filename (e.g., "LevelUp.WAV")
    static std::string getSoundFile(UISoundType type);

    // Get the sound ID from SoundAssets.txt for a UI sound type
    // Returns the ID if known, or empty optional if not in SoundAssets.txt
    static std::optional<uint32_t> getSoundId(UISoundType type);

    // Get a human-readable name for a UI sound type
    static std::string getTypeName(UISoundType type);

    // Check if a UI sound type has a valid sound file defined
    static bool isValid(UISoundType type);
};

// Sound IDs for UI sounds. Everything in the 98..149 bank is an ALIAS into
// SoundBank (sound_assets.h), which holds the ONE measured id table (T328 Leg
// A). Ids outside that range (the bell, metal/stone/secret doors, trapdoor)
// were NOT in the client's preload burst and are unverified.
namespace UISoundId {
    // Player events
    constexpr uint32_t LEVEL_UP = SoundBank::LEVELUP;        // 139 LevelUp.WAV
    constexpr uint32_t LEVEL_DOWN = SoundBank::LEVDN;        // 140 LevDn.WAV
    constexpr uint32_t END_QUEST = SoundBank::ENDQUEST;      // 141 EndQuest.WAV

    // Bell sounds
    constexpr uint32_t BOAT_BELL = 170;         // BoatBell.WAV

    // Interaction
    // MEASURED: the generic coin-move sound, not a "purchase" sound.
    constexpr uint32_t BUY_ITEM = SoundBank::BUYITEM;        // 138 BuyItem.WAV

    // Containers -- MEASURED: these two are NOT the bag sounds; ITEM_CLOTH is.
    constexpr uint32_t CHEST_CLOSE = SoundBank::CHEST_CL;    // 133 Chest_Cl.WAV
    constexpr uint32_t CHEST_OPEN = SoundBank::CHEST_OP;     // 134 Chest_Op.WAV

    // Container open/close and item placement -- the two ids T328 Leg B wires.
    constexpr uint32_t ITEM_CLOTH = SoundBank::ITEMCLTH;     // 145 itemclth.WAV
    constexpr uint32_t WEAR_CLOTH = SoundBank::WEARCLTH;     // 146 wearclth.WAV

    // Wood doors
    constexpr uint32_t DOOR_WOOD_OPEN = SoundBank::DOORWD_O;  // 135 DoorWd_O.WAV
    constexpr uint32_t DOOR_WOOD_CLOSE = SoundBank::DOORWD_C; // 136 DoorWd_C.WAV

    // Metal doors
    constexpr uint32_t DOOR_METAL_OPEN = 176;   // DoorMt_O.WAV
    constexpr uint32_t DOOR_METAL_CLOSE = 175;  // DoorMt_C.WAV

    // Stone doors
    constexpr uint32_t DOOR_STONE_OPEN = 179;   // DoorSt_O.WAV
    constexpr uint32_t DOOR_STONE_CLOSE = 178;  // DoorSt_C.WAV

    // Special doors
    constexpr uint32_t DOOR_SECRET = 177;       // DoorSecr.WAV
    constexpr uint32_t TRAP_DOOR = 189;         // TrapDoor.WAV

    // UI buttons -- MEASURED preloaded-but-never-played. Do not wire.
    constexpr uint32_t BUTTON_CLICK = SoundBank::BUTTON_1;   // 142 Button_1.WAV

    // Effects
    constexpr uint32_t TELEPORT = SoundBank::TELEPORT;       // 137 Teleport.WAV

    // Player state (T328's positive control: the oracle plays this on a drink)
    constexpr uint32_t DRINK = SoundBank::DRINK;             // 149 Drink.WAV

    // Note: YouveGotMail uses string key "YouveGotMail" not numeric ID
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
