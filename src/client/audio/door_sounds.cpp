#ifdef WITH_AUDIO

#include "client/audio/door_sounds.h"

namespace EQT {
namespace Audio {

uint32_t DoorSounds::getDoorSound(DoorType type, bool opening) {
    switch (type) {
        case DoorType::Metal:
            return opening ? DoorSoundId::METAL_DOOR_OPEN : DoorSoundId::METAL_DOOR_CLOSE;

        case DoorType::Stone:
            return opening ? DoorSoundId::STONE_DOOR_OPEN : DoorSoundId::STONE_DOOR_CLOSE;

        case DoorType::Wood:
            return opening ? DoorSoundId::WOOD_DOOR_OPEN : DoorSoundId::WOOD_DOOR_CLOSE;

        case DoorType::Secret:
            // Secret doors use the same sound for open and close
            return DoorSoundId::SECRET_DOOR;

        case DoorType::Sliding:
            return opening ? DoorSoundId::SLIDING_DOOR_OPEN : DoorSoundId::SLIDING_DOOR_CLOSE;

        default:
            // Default to metal door sound
            return opening ? DoorSoundId::METAL_DOOR_OPEN : DoorSoundId::METAL_DOOR_CLOSE;
    }
}

uint32_t DoorSounds::getObjectSound(ObjectType type) {
    switch (type) {
        case ObjectType::Lever:
            return ObjectSoundId::LEVER;

        case ObjectType::Drawbridge:
            return ObjectSoundId::DRAWBRIDGE_LOOP;

        case ObjectType::Elevator:
            return ObjectSoundId::ELEVATOR_LOOP;

        case ObjectType::Portcullis:
            return ObjectSoundId::PORTCULLIS_LOOP;

        case ObjectType::SpearTrapDown:
            return ObjectSoundId::SPEAR_DOWN;

        case ObjectType::SpearTrapUp:
            return ObjectSoundId::SPEAR_UP;

        case ObjectType::TrapDoor:
            return ObjectSoundId::TRAP_DOOR;

        default:
            // Default to lever sound
            return ObjectSoundId::LEVER;
    }
}

std::string DoorSounds::getDoorSoundFilename(DoorType type, bool opening) {
    switch (type) {
        case DoorType::Metal:
            return opening ? "DoorMt_O.WAV" : "DoorMt_C.WAV";

        case DoorType::Stone:
            return opening ? "DoorSt_O.WAV" : "DoorSt_C.WAV";

        case DoorType::Wood:
            return opening ? "DoorWd_O.WAV" : "DoorWd_C.WAV";

        case DoorType::Secret:
            return "DoorSecr.WAV";

        case DoorType::Sliding:
            return opening ? "SlDorStO.WAV" : "SlDorStC.WAV";

        default:
            return opening ? "DoorMt_O.WAV" : "DoorMt_C.WAV";
    }
}

std::string DoorSounds::getObjectSoundFilename(ObjectType type) {
    switch (type) {
        case ObjectType::Lever:
            return "Lever.WAV";

        case ObjectType::Drawbridge:
            return "Dbrdg_Lp.WAV";

        case ObjectType::Elevator:
            return "ElevLoop.wav";

        case ObjectType::Portcullis:
            return "PortC_Lp.WAV";

        case ObjectType::SpearTrapDown:
            return "SpearDn.WAV";

        case ObjectType::SpearTrapUp:
            return "SpearUp.WAV";

        case ObjectType::TrapDoor:
            return "TrapDoor.WAV";

        default:
            return "Lever.WAV";
    }
}

bool DoorSounds::hasSeparateOpenCloseSound(DoorType type) {
    // Secret doors use the same sound for both open and close
    return type != DoorType::Secret;
}

} // namespace Audio
} // namespace EQT

#endif // WITH_AUDIO
