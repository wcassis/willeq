#ifdef WITH_AUDIO

#include "client/audio/ui_sounds.h"

namespace EQT {
namespace Audio {

std::string UISounds::getSoundFile(UISoundType type) {
    switch (type) {
        // Player events
        case UISoundType::LevelUp:
            return "LevelUp.WAV";
        case UISoundType::LevelDown:
            return "LevDn.WAV";
        case UISoundType::EndQuest:
            return "EndQuest.WAV";

        // Bell sounds
        case UISoundType::BoatBell:
            return "BoatBell.WAV";

        // Interaction sounds
        case UISoundType::BuyItem:
            return "BuyItem.WAV";
        case UISoundType::SellItem:
            return "BuyItem.WAV";  // Reuses buy sound

        // Container sounds
        case UISoundType::ChestOpen:
            return "Chest_Op.WAV";
        case UISoundType::ChestClose:
            return "Chest_Cl.WAV";

        // Wood door sounds
        case UISoundType::DoorWoodOpen:
            return "DoorWd_O.WAV";
        case UISoundType::DoorWoodClose:
            return "DoorWd_C.WAV";

        // Metal door sounds
        case UISoundType::DoorMetalOpen:
            return "DoorMt_O.WAV";
        case UISoundType::DoorMetalClose:
            return "DoorMt_C.WAV";

        // Stone door sounds
        case UISoundType::DoorStoneOpen:
            return "DoorSt_O.WAV";
        case UISoundType::DoorStoneClose:
            return "DoorSt_C.WAV";

        // Special doors
        case UISoundType::DoorSecret:
            return "DoorSecr.WAV";
        case UISoundType::TrapDoor:
            return "TrapDoor.WAV";

        // UI interaction
        case UISoundType::ButtonClick:
            return "Button_1.WAV";

        // Notification
        case UISoundType::YouveGotMail:
            return "mail1.wav";

        // Effects
        case UISoundType::Teleport:
            return "Teleport.WAV";

        // Player state
        case UISoundType::Drink:
            return "Drink.WAV";

        case UISoundType::Count:
        default:
            return "";
    }
}

std::optional<uint32_t> UISounds::getSoundId(UISoundType type) {
    switch (type) {
        // Player events
        case UISoundType::LevelUp:
            return UISoundId::LEVEL_UP;
        case UISoundType::LevelDown:
            return UISoundId::LEVEL_DOWN;
        case UISoundType::EndQuest:
            return UISoundId::END_QUEST;

        // Bell sounds
        case UISoundType::BoatBell:
            return UISoundId::BOAT_BELL;

        // Interaction sounds
        case UISoundType::BuyItem:
            return UISoundId::BUY_ITEM;
        case UISoundType::SellItem:
            return UISoundId::BUY_ITEM;  // Reuses buy sound

        // Container sounds
        case UISoundType::ChestOpen:
            return UISoundId::CHEST_OPEN;
        case UISoundType::ChestClose:
            return UISoundId::CHEST_CLOSE;

        // Wood door sounds
        case UISoundType::DoorWoodOpen:
            return UISoundId::DOOR_WOOD_OPEN;
        case UISoundType::DoorWoodClose:
            return UISoundId::DOOR_WOOD_CLOSE;

        // Metal door sounds
        case UISoundType::DoorMetalOpen:
            return UISoundId::DOOR_METAL_OPEN;
        case UISoundType::DoorMetalClose:
            return UISoundId::DOOR_METAL_CLOSE;

        // Stone door sounds
        case UISoundType::DoorStoneOpen:
            return UISoundId::DOOR_STONE_OPEN;
        case UISoundType::DoorStoneClose:
            return UISoundId::DOOR_STONE_CLOSE;

        // Special doors
        case UISoundType::DoorSecret:
            return UISoundId::DOOR_SECRET;
        case UISoundType::TrapDoor:
            return UISoundId::TRAP_DOOR;

        // UI interaction
        case UISoundType::ButtonClick:
            return UISoundId::BUTTON_CLICK;

        // Notification
        case UISoundType::YouveGotMail:
            // YouveGotMail uses string key, not numeric ID
            return std::nullopt;

        // Effects
        case UISoundType::Teleport:
            return UISoundId::TELEPORT;

        // Player state
        case UISoundType::Drink:
            return UISoundId::DRINK;

        case UISoundType::Count:
        default:
            return std::nullopt;
    }
}

std::string UISounds::getTypeName(UISoundType type) {
    switch (type) {
        case UISoundType::LevelUp:
            return "LevelUp";
        case UISoundType::LevelDown:
            return "LevelDown";
        case UISoundType::EndQuest:
            return "EndQuest";
        case UISoundType::BoatBell:
            return "BoatBell";
        case UISoundType::BuyItem:
            return "BuyItem";
        case UISoundType::SellItem:
            return "SellItem";
        case UISoundType::ChestOpen:
            return "ChestOpen";
        case UISoundType::ChestClose:
            return "ChestClose";
        case UISoundType::DoorWoodOpen:
            return "DoorWoodOpen";
        case UISoundType::DoorWoodClose:
            return "DoorWoodClose";
        case UISoundType::DoorMetalOpen:
            return "DoorMetalOpen";
        case UISoundType::DoorMetalClose:
            return "DoorMetalClose";
        case UISoundType::DoorStoneOpen:
            return "DoorStoneOpen";
        case UISoundType::DoorStoneClose:
            return "DoorStoneClose";
        case UISoundType::DoorSecret:
            return "DoorSecret";
        case UISoundType::TrapDoor:
            return "TrapDoor";
        case UISoundType::ButtonClick:
            return "ButtonClick";
        case UISoundType::YouveGotMail:
            return "YouveGotMail";
        case UISoundType::Teleport:
            return "Teleport";
        case UISoundType::Drink:
            return "Drink";
        case UISoundType::Count:
        default:
            return "Unknown";
    }
}

bool UISounds::isValid(UISoundType type) {
    if (type == UISoundType::Count) {
        return false;
    }
    return !getSoundFile(type).empty();
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
