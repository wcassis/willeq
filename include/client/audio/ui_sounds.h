#pragma once

#ifdef WITH_AUDIO

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
    BuyItem,            // Purchasing from vendor
    SellItem,           // Selling to vendor (uses BuyItem if no separate)

    // Container sounds
    ChestOpen,          // Opening a chest/container
    ChestClose,         // Closing a chest/container

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
    ButtonClick,        // Button click (UI buttons)

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

// Sound IDs for UI sounds (from SoundAssets.txt)
// These extend the existing SoundId namespace in sound_assets.h
namespace UISoundId {
    // Player events
    constexpr uint32_t LEVEL_UP = 139;          // LevelUp.WAV
    constexpr uint32_t LEVEL_DOWN = 140;        // LevDn.WAV
    constexpr uint32_t END_QUEST = 141;         // EndQuest.WAV

    // Bell sounds
    constexpr uint32_t BOAT_BELL = 170;         // BoatBell.WAV

    // Interaction
    constexpr uint32_t BUY_ITEM = 138;          // BuyItem.WAV

    // Containers
    constexpr uint32_t CHEST_CLOSE = 133;       // Chest_Cl.WAV
    constexpr uint32_t CHEST_OPEN = 134;        // Chest_Op.WAV

    // Wood doors
    constexpr uint32_t DOOR_WOOD_OPEN = 135;    // DoorWd_O.WAV
    constexpr uint32_t DOOR_WOOD_CLOSE = 136;   // DoorWd_C.WAV

    // Metal doors
    constexpr uint32_t DOOR_METAL_OPEN = 176;   // DoorMt_O.WAV
    constexpr uint32_t DOOR_METAL_CLOSE = 175;  // DoorMt_C.WAV

    // Stone doors
    constexpr uint32_t DOOR_STONE_OPEN = 179;   // DoorSt_O.WAV
    constexpr uint32_t DOOR_STONE_CLOSE = 178;  // DoorSt_C.WAV

    // Special doors
    constexpr uint32_t DOOR_SECRET = 177;       // DoorSecr.WAV
    constexpr uint32_t TRAP_DOOR = 189;         // TrapDoor.WAV

    // UI buttons
    constexpr uint32_t BUTTON_CLICK = 142;      // Button_1.WAV

    // Effects
    constexpr uint32_t TELEPORT = 137;          // Teleport.WAV

    // Player state
    constexpr uint32_t DRINK = 149;             // Drink.WAV

    // Note: YouveGotMail uses string key "YouveGotMail" not numeric ID
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
