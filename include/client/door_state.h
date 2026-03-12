#ifndef EQT_DOOR_STATE_H
#define EQT_DOOR_STATE_H

#include <cstdint>
#include <string>

namespace EQT {

// Game-state representation of a door. No renderer dependencies.
// "Doors" include static placeables, dynamic doors, elevators, triggered
// platforms, revolving doors, and sliding doors.
struct DoorState {
    uint8_t doorId = 0;
    std::string name;          // Model name (matches zone S3D object)
    float x = 0.0f;           // Position (EQ coordinates)
    float y = 0.0f;
    float z = 0.0f;
    float heading = 0.0f;     // Heading when closed (degrees)
    uint32_t incline = 0;     // Open rotation offset
    uint16_t size = 100;      // Scale (100 = normal)
    uint8_t opentype = 0;     // Behavior type
    bool isOpen = false;       // Current state (after invert logic)
    bool invertState = false;  // Spawn state inversion
    uint32_t doorParam = 0;   // Lock type / key item ID
};

} // namespace EQT

#endif // EQT_DOOR_STATE_H
