#pragma once

#ifdef WITH_AUDIO

#include <cstdint>
#include <string>

namespace EQT {
namespace Audio {

// Door material types that determine the sound played
enum class DoorType {
    Metal,      // Metal doors (e.g., iron gates)
    Stone,      // Stone doors (e.g., dungeon passages)
    Wood,       // Wooden doors (e.g., tavern doors)
    Secret,     // Secret/hidden doors
    Sliding     // Sliding stone doors
};

// Object types that produce sounds when activated
enum class ObjectType {
    Lever,          // Lever activation
    Drawbridge,     // Drawbridge mechanism (looping)
    Elevator,       // Elevator mechanism (looping)
    Portcullis,     // Portcullis mechanism (looping)
    SpearTrapDown,  // Spear trap extending
    SpearTrapUp,    // Spear trap retracting
    TrapDoor        // Trap door opening
};

// Sound IDs for door and object sounds (from SoundAssets.txt)
namespace DoorSoundId {
    // Wood doors
    constexpr uint32_t WOOD_DOOR_OPEN = 135;     // DoorWd_O.WAV
    constexpr uint32_t WOOD_DOOR_CLOSE = 136;    // DoorWd_C.WAV

    // Metal doors
    constexpr uint32_t METAL_DOOR_CLOSE = 175;   // DoorMt_C.WAV
    constexpr uint32_t METAL_DOOR_OPEN = 176;    // DoorMt_O.WAV

    // Secret doors
    constexpr uint32_t SECRET_DOOR = 177;        // DoorSecr.WAV

    // Stone doors
    constexpr uint32_t STONE_DOOR_CLOSE = 178;   // DoorSt_C.WAV
    constexpr uint32_t STONE_DOOR_OPEN = 179;    // DoorSt_O.WAV

    // Sliding doors
    constexpr uint32_t SLIDING_DOOR_CLOSE = 183; // SlDorStC.WAV
    constexpr uint32_t SLIDING_DOOR_OPEN = 184;  // SlDorStO.WAV
}

// Sound IDs for object sounds (from SoundAssets.txt)
namespace ObjectSoundId {
    constexpr uint32_t DRAWBRIDGE_LOOP = 173;    // Dbrdg_Lp.WAV
    constexpr uint32_t DRAWBRIDGE_STOP = 174;    // DbrdgStp.WAV
    constexpr uint32_t LEVER = 180;              // Lever.WAV
    constexpr uint32_t PORTCULLIS_LOOP = 181;    // PortC_Lp.WAV
    constexpr uint32_t PORTCULLIS_STOP = 182;    // PortcStp.WAV
    constexpr uint32_t ELEVATOR_LOOP = 185;      // ElevLoop.wav (or 186)
    constexpr uint32_t SPEAR_DOWN = 187;         // SpearDn.WAV
    constexpr uint32_t SPEAR_UP = 188;           // SpearUp.WAV
    constexpr uint32_t TRAP_DOOR = 189;          // TrapDoor.WAV
}

// Utility class for mapping door/object types to sound IDs
class DoorSounds {
public:
    // Get the sound ID for a door opening or closing
    // @param type The type of door material
    // @param opening True for opening sound, false for closing sound
    // @return The sound ID to play
    static uint32_t getDoorSound(DoorType type, bool opening);

    // Get the sound ID for an object activation
    // @param type The type of object
    // @return The sound ID to play
    static uint32_t getObjectSound(ObjectType type);

    // Get the filename for a door sound (for direct file access)
    // @param type The type of door material
    // @param opening True for opening sound, false for closing sound
    // @return The WAV filename (e.g., "DoorMt_O.WAV")
    static std::string getDoorSoundFilename(DoorType type, bool opening);

    // Get the filename for an object sound (for direct file access)
    // @param type The type of object
    // @return The WAV filename (e.g., "Lever.WAV")
    static std::string getObjectSoundFilename(ObjectType type);

    // Check if a door type has separate open/close sounds
    // (Secret doors use the same sound for both)
    static bool hasSeparateOpenCloseSound(DoorType type);
};

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
