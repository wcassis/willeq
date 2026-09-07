// door_motion.h -- the door-animation constants, MEASURED against the official
// Titanium client, and the lift cycle that plays them.
//
// T438 (board B421/B422/B423). Every number in this file used to be an
// invention: DoorManager carried `ANIM_SPEED = 2.0f` ("complete animation in
// ~0.5 seconds") and `SPIN_SPEED = 42.35f` ("~180 degrees per 4.25 seconds"),
// opentype-59 lifts had no translation code at all, and `door_param` was parsed
// off the wire and read by nothing. B421 was the blocker: T433 proved the
// official client's 3D door pick refuses every synthetic click the Xvfb rig can
// produce, so no door could be made to animate and nothing could be measured.
//
// T438 unblocked it with a temporary quest fixture calling eqemu's own
// `Doors::ForceOpen()` (/mnt/projects/akk-stack/code/zone/doors.cpp:658), which
// emits the SAME OP_MoveDoor the click path emits -- so what was recorded is
// the official CLIENT's animation, whatever triggered it. Method, videos and
// per-frame traces: review/T438/README.md.
//
// THE MEASUREMENTS (gfaydark FAYDOOR 30, FAYLEVATOR 69 and 77, steamfont
// WMBLADE 2; 30 fps x11grab video stepped frame by frame, plus a 4 Hz
// server-side trace of the player's own position):
//
//   * A hinged door's OPEN rotation takes 0.95 s and its CLOSE rotation 0.60 s
//     (five cycles each; spans 0.933-0.967 s and 0.567-0.633 s). fable's single
//     0.5 s was too fast in both directions and wrong to be one number.
//
//   * THE CLIENT CLOSES THE DOOR ITSELF. eqemu's `Doors::Process()`
//     (doors.cpp:138) only transmits a close for `open_type == 40` or
//     `trigger_type == 1`; for an ordinary door it flips its own state and sends
//     NOTHING. The official client still closed the door, 11.04 s after it
//     opened, five times out of five (11.00/11.07/11.10/11.03/11.00 s). fable
//     drives its doors purely from OP_MoveDoor, so before this every ordinary
//     door a player or an NPC opened stayed open for the rest of the session.
//
//   * A lift travels `door_param` units along z at a CONSTANT 8.67 units/s,
//     dwells 20.5 s at the far end and RETURNS BY ITSELF -- all from one
//     OP_MoveDoor. Two lifts with different door_param settle both numbers at
//     once: door 69 (param 68) went open-to-return in 28.33 s and door 77
//     (param 98) in 31.79 s, so 30 extra units cost 3.46 s => 8.67 units/s and
//     a dwell of 20.5 s that does not depend on the distance. Sending a second
//     MoveDoor mid-cycle changed nothing.
//
//   * The player standing on the platform IS CARRIED, exactly: with the rig
//     character parked on lift 69 the server-side trace shows its z climb
//     5.81 -> 10.93 -> 19.98 -> 29.30 -> 39.47 -> 49.87 -> 58.65 in step with
//     the platform (review/T438/logs/trace_lift69_ride.tsv).
//
//   * A spinner turns a FULL 360 degrees in 4.267 s = 84.4 deg/s. fable's
//     42.35 is exactly half of it: the same 4.25 s period read as a HALF turn.
//     The blade's horizontal centroid autocorrelates at -0.861 at lag 64 frames
//     and +0.822 at 128, which is what 180-degree symmetry looks like and is why
//     the half-turn reading was so easy to make.
#ifndef EQT_GRAPHICS_DOOR_MOTION_H
#define EQT_GRAPHICS_DOOR_MOTION_H

#include <cstdint>

namespace EQT {
namespace Graphics {

namespace DoorMotion {

// Hinged doors (opentypes 0/5/56 -- the ones fable rotates).
constexpr float kHingedOpenSeconds = 0.95f;
constexpr float kHingedCloseSeconds = 0.60f;
// From the START of the open rotation to the START of the close rotation.
constexpr float kHingedAutoCloseSeconds = 11.04f;

// Spinners (opentypes 100/105). MEASURED on a 105 (steamfont WMBLADE); no
// opentype-100 spinner was measured -- see the board item T438 opened for it.
constexpr float kSpinDegreesPerSecond = 84.4f;

// Lifts (opentype 59 with door_param > 0).
constexpr float kLiftUnitsPerSecond = 8.67f;
constexpr float kLiftDwellSeconds = 20.5f;

// How far a player's feet may sit above a lift's surface and still ride it.
constexpr float kLiftRideTolerance = 2.0f;

}  // namespace DoorMotion

// Opentype 59 is the lift type. door_param is the travel distance in units;
// the 21 era rows with param 0/-1 (grobb signs, acrylia torches, the arena
// tower) are decorative and must not translate -- T433's census, board B422.
bool DoorOpentypeIsLift(uint8_t opentype);
bool DoorTravels(uint8_t opentype, uint32_t doorParam);

// Seconds a lift of this door_param spends in ONE direction.
float LiftTravelSeconds(uint32_t doorParam);

// Would a player whose feet are at feetZ be carried by a platform whose
// walkable surface is at surfaceZ? The ground query resolves the surface a hair
// below the feet, and a rising platform gets ahead of the player between
// frames, so the test is a band rather than an equality.
bool PlayerRidesLift(float feetZ, float surfaceZ,
                     float tolerance = DoorMotion::kLiftRideTolerance);

// One lift's animation. Pure: no scene node, no device, no clock of its own.
//
// The measured behaviour is a whole CYCLE per OP_MoveDoor, not a move to a
// commanded end state: the platform leaves the end it is parked at, travels
// `travel` units, waits kLiftDwellSeconds and comes back. `offset()` is the z
// displacement from the door's SPAWN pose, and the parked-at-rest value is
// +travel, because that is where the oracle keeps it: at the gfaydark lift 69
// spawn point a `#goto` to z 5 and to z 12 both resolved to the forest floor at
// -0.09 while the door was idle, and only after a MoveDoor did the same spot
// resolve to the platform at 5.81.
enum class LiftPhase : uint8_t { Parked, Outbound, Dwell, Inbound };

class LiftCycle {
public:
    // Called once when the door is created. Parks the platform at the far end.
    void init(float travelUnits);

    // One OP_MoveDoor. Ignored while a cycle is already running -- measured:
    // the server's own close packet arrives 5 s into the lift's ride and the
    // official client does not react to it.
    void trigger();

    void advance(float deltaSeconds);

    float offset() const { return offset_; }
    float travel() const { return travel_; }
    bool moving() const { return phase_ == LiftPhase::Outbound || phase_ == LiftPhase::Inbound; }
    bool active() const { return phase_ != LiftPhase::Parked; }
    LiftPhase phase() const { return phase_; }

private:
    LiftPhase phase_ = LiftPhase::Parked;
    float travel_ = 0.0f;
    float offset_ = 0.0f;
    float timer_ = 0.0f;
};

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_DOOR_MOTION_H
