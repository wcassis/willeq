// B893: the ONE jump/fall model. Every jump-arc and fall-damage constant in
// the client lives in this one table, so the headless MovementComponent and
// the renderer cannot drift apart -- tests/test_jump_fall_arc.cpp fails if a
// second writer of an arc constant appears.
//
// PROVENANCE OF THE NUMBERS (refactor/B893/README.md):
//   The official-client arc (apex, duration) and the height->damage curve are
//   MEASURED off the lane-2 oracle VM (official Titanium client over the wire):
//     - apex: +3.05 world units above standing, from the mid-air OP_ClientUpdate
//       Z(t) (logs/b893_jump_wirecap.decoded.txt, the 11:29:32 sample);
//     - fall: the client COMPUTES the damage and sends OP_EnvDamage C->S (dmgtype
//       0xFC/252); the server reads ed->damage verbatim. The measured curve is
//       concave (5 damage at 28u, 91 at 68u) with a no-damage floor at ~18u; we
//       implement it as a linear ramp from that floor (see refactor/B893/README.md
//       for the full data and why linear is the model form).
//   The arc DURATION is still [ASSUMED]: the wire carries position at ~1 Hz, which
//   is too coarse to resolve a sub-second arc, so the 1.0 s is marked and owed.
//   Terminal velocity: nothing clamps to it (no terminal-velocity term in the
//   integrator) and the capture does not isolate it, so it stays [ASSUMED, OWED].
//
//   The damage curve is CLIENT-owned: eqemu reads ed->damage VERBATIM
//   (Handle_OP_EnvDamage, akk-stack code/zone/client_packet.cpp) -- it drops the
//   packet in liquid, applies ReduceFallDamage, and (for a GM) prints the would-be
//   damage. The server does no fall math.
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
    // [MEASURED] units above startZ at t = duration/2. The lane-2 oracle's
    // mid-air OP_ClientUpdate Z rose to -35.165 vs standing -38.2157 (the
    // +3.05 sample, the max of five mid-air captures; the wire is ~1 Hz so this
    // is the apex lower bound, logs/b893_jump_wirecap.decoded.txt).
    float peakHeight = 3.05f;
    // [ASSUMED, OWED] seconds, full arc (takeoff to landing). The wire carries
    // position at ~1 Hz, which cannot resolve a sub-second arc; the official
    // client's jump is clearly sub-second (a single mid-air sample per jump).
    // 1.0 s is the pre-B893 renderer's value, kept and marked owed.
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
    // [MEASURED] a fall shorter than this deals no damage. The lane-2 oracle's
    // 18.2-unit drop (goto z=-20 -> ground -38.2157) sent NO OP_EnvDamage; the
    // 23.2-unit drop (z=-15) sent 5. The floor is between 18.2 and 23.2, so 18.0
    // is the measured no-damage bound (logs/b893_fall_series.txt).
    float safeHeight = 18.0f;
    // [MEASURED, mid/high range] damage grows ~1.5 per unit above the floor.
    // The full measured curve is concave (5 at 28u, 91 at 68u; README for the
    // table); 1.5/unit is the least-squares slope of the clean 48-68u range
    // (30@48, 59@53, 81@58, 87@63, 91@68 -> slope ~1.5). The low end (28u/43u
    // both reported 5) is below this ramp's line and is treated as a client
    // rounding floor, not a model feature.
    float damagePerUnitAboveSafe = 1.5f;
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
