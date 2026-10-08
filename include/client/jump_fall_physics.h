// B893: the ONE jump/fall model. Every jump-arc and fall-damage constant in
// the client lives in this one table, so the headless MovementComponent and
// the renderer cannot drift apart -- tests/test_jump_fall_arc.cpp fails if a
// second writer of an arc constant appears.
//
// PROVENANCE OF THE NUMBERS (refactor/B893/README.md):
//   The official-client arc (peak, duration, gravity, terminal speed) and the
//   height->damage table are [ASSUMED] placeholders in this revision. The
//   lane-2 oracle capture (three standing jumps + a +60-unit damaging fall)
//   is the measurement that replaces them; until it lands, every value here
//   is marked and the README says exactly what it owes.
//
//   The damage TABLE SHAPE and the packet layout are not assumptions:
//   eqemu reads ed->damage VERBATIM (Handle_OP_EnvDamage, akk-stack
//   code/zone/client_packet.cpp:6308) -- it drops the packet in liquid,
//   applies ReduceFallDamage, and (for a GM) prints the would-be damage. So
//   the client owns the entire height->damage curve; the table below is the
//   client's, and Leg C replaces its values with the measured sweep.
//
// The arc is a fixed parabola (the official client jumps a fixed height for a
// fixed time; the B893 spec). Both integrators evaluate the SAME curve:
//   z(t) = startZ + peak * 4t(1-t),  t in [0, duration]
// The headless integrator advances it by wall clock (MovementComponent::
// UpdateJump) and owns the wire Z; the renderer integrates it per frame with
// the same peak/duration for its on-screen arc (PlayerCameraComponent).

#ifndef EQT_JUMP_FALL_PHYSICS_H
#define EQT_JUMP_FALL_PHYSICS_H

#include <cstdint>
#include <cmath>

namespace eqt {
namespace jumpfall {

// --- The arc: one set of constants, one curve ---

struct Arc {
    // [ASSUMED] units above startZ at t = duration/2. Leg C: measured.
    float peakHeight = 10.0f;
    // [ASSUMED] seconds, full arc (takeoff to landing). Leg C: measured.
    float durationS = 1.0f;
    // [ASSUMED, OWED] the maximum fall speed. The lane-2 capture is what
    // measures it (the steepest slope of the fall's Z(t)). Nothing clamps to
    // it today: the integrator has no terminal-velocity term at all (the
    // pre-B893 renderer had none either), so this is the table's slot for the
    // measured value, named owed in refactor/B893/README.md.
    float terminalSpeed = 100.0f;

    // The fixed jump curve. tSeconds is clamped to [0, durationS]; at the
    // endpoints the height is 0, the peak is peakHeight at durationS/2.
    float jumpHeightAt(float tSeconds) const {
        if (tSeconds <= 0.0f || tSeconds >= durationS) {
            return 0.0f;
        }
        const float t = tSeconds / durationS;
        return peakHeight * 4.0f * t * (1.0f - t);
    }

    // Has the arc elapsed?
    bool elapsed(float tSeconds) const { return tSeconds >= durationS; }

    // The two per-frame constants the renderer's velocity integrator needs,
    // DERIVED from the single (peakHeight, durationS) pair so they cannot
    // drift from it: for z(t) = 4P·(t/D)(1 - t/D), dz/dt|_{t=0} = 4P/D and the
    // (downward) acceleration is 8P/D^2. At P=10, D=1 these are exactly 40 and
    // 80 -- which is why the two old models disagreed: the headless side ran
    // the 25-unit/1000 ms closed form while the renderer ran a 10-unit arc
    // from its own hardcoded 40/80.
    float initialVelocity() const { return 4.0f * peakHeight / durationS; }
    float gravity() const { return 8.0f * peakHeight / (durationS * durationS); }
};

inline const Arc& arc() {
    static const Arc kArc;
    return kArc;
}

// --- The fall: damage from fall height ---

struct Fall {
    // [ASSUMED] a fall shorter than this deals no damage (a single step down
    // the renderer's collisionStepHeight*2 is ~2.0).
    float safeHeight = 4.0f;
    // [ASSUMED] linear: damage grows with the distance above the threshold.
    float damagePerUnitAboveSafe = 3.0f;
};

inline const Fall& fall() {
    static const Fall kFall;
    return kFall;
}

// The client's height->damage table: 0 until the safe height, then linear.
// Returns 0 for heights at or below safeHeight.
inline uint32_t damageForFallHeight(float fallHeight) {
    if (fallHeight <= fall().safeHeight) {
        return 0;
    }
    const float excess = fallHeight - fall().safeHeight;
    const double d = excess * fall().damagePerUnitAboveSafe;
    return static_cast<uint32_t>(d + 0.5);  // round to nearest
}

// --- The fall: takeoff tracking ---
//
// The renderer integrates jump/fall gravity only while `isJumping` is set
// (Space), so a damaging fall is a jump that lands lower than its takeoff --
// jumping off a ledge. The pure logic the FrameComponent uses to detect that
// transition (it straddles updatePlayerMovement, which the movement-order
// golden polices and so must not grow fall code):
//
//   begin(z)  -- the player became airborne (the isJumping rising edge).
//                Record z, the takeoff height, as the reference.
//   end(z)    -- the player landed (the falling edge). Return the drop
//                height (takeoffZ - z, clamped at 0) and clear the flag.
//
// The damage is NOT this struct's: the game thread converts height to damage
// with damageForFallHeight() (0 at/below the safe height -> no send). A
// jump that lands back at its takeoff returns 0 and deals nothing.
struct FallTracker {
    float takeoffZ = 0.0f;
    bool airborne = false;

    void begin(float z) {
        takeoffZ = z;
        airborne = true;
    }

    void clear() { airborne = false; }

    float end(float groundZ) {
        if (!airborne) {
            return 0.0f;
        }
        airborne = false;
        const float drop = takeoffZ - groundZ;
        return drop > 0.0f ? drop : 0.0f;
    }
};

// --- The wire packet ---

// dmgtype values: eqemu EQ::constants::EnvironmentalDamage
// (code/common/emu_constants.h:308) -- 250=Lava, 251=Drowning, 252=Falling,
// 253=Trap.
constexpr uint8_t kEnvDamageFalling = 252;

// Encode one EnvDamage2_Struct (include/common/packets/travel_misc.h:132, 31
// bytes, C->S) into `out`. The layout is byte-exact to eqemu:
//   +0  uint32 id          (0 = self)
//   +4  uint16 unknown4    (0)
//   +6  uint32 damage
//   +10 uint8  unknown10[12] (0)
//   +22 uint8  dmgtype
//   +23 uint8  unknown2[4] (0)
//   +27 uint16 constant    (0xFFFF, per the struct comment and the pcap)
//   +29 uint16 unknown29   (0)
inline void encodeEnvDamage(unsigned char* out, uint32_t damage, uint8_t dmgtype) {
    for (int i = 0; i < 31; ++i) out[i] = 0;
    out[0] = 0;  // id (uint32, self)
    out[4] = 0;  // unknown4 (uint16)
    out[6] = static_cast<unsigned char>(damage & 0xFF);
    out[7] = static_cast<unsigned char>((damage >> 8) & 0xFF);
    out[8] = static_cast<unsigned char>((damage >> 16) & 0xFF);
    out[9] = static_cast<unsigned char>((damage >> 24) & 0xFF);
    out[22] = dmgtype;
    out[27] = 0xFF;  // constant, little-endian 0xFFFF
    out[28] = 0xFF;
}

}  // namespace jumpfall
}  // namespace eqt

#endif  // EQT_JUMP_FALL_PHYSICS_H
