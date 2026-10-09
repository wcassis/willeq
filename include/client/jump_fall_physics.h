// B893: the ONE jump/fall model. Every jump-arc and fall-damage constant in
// the client lives in this one table, so the headless MovementComponent and
// the renderer cannot drift apart -- tests/test_jump_fall_arc.cpp fails if a
// second writer of an arc constant appears.
//
// PROVENANCE OF THE NUMBERS (refactor/B893/README.md):
//   The official-client arc (apex, duration) and the height->damage curve are
//   taken off the lane-2 oracle VM (official Titanium client over the wire):
//     - apex: [MEASURED LOWER BOUND] +3.05 world units above standing. Five
//       jumps gave ONE mid-air OP_ClientUpdate each (four at an identical
//       +2.509, one at +3.051; evidence/b893_jump_wirecap_1129.log, decoded in
//       evidence/oracle_measurements.md). A 1-3 Hz wire samples a sub-second
//       arc once, at no particular phase, so the true apex is AT OR ABOVE 3.05
//       and is OWED from the committed video (B1011 review);
//     - fall: the client COMPUTES the damage and sends OP_EnvDamage C->S (dmgtype
//       0xFC/252); the server reads ed->damage verbatim. The table below IS the
//       measured points (no-damage floor at ~18u, 5 from 23u to 33u, then a
//       concave rise to 91 at 68u), linearly interpolated between them -- the
//       oracle's own numbers at every measured height, no formula assumed
//       (B1011 replaced B893's 1.5/unit linear ramp, which gave 23 where the
//       oracle gave 5). Measured on ONE character (level 60 GM, 2255 HP);
//       whether the curve depends on level or HP is UNTESTED and owed.
//   The arc DURATION is still [ASSUMED]: the wire cannot resolve a sub-second
//   arc, so the 1.0 s is marked and owed.
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
    // [MEASURED LOWER BOUND, apex OWED] units above startZ at t = duration/2.
    // The lane-2 oracle's mid-air OP_ClientUpdate Z rose to -35.1651 vs
    // standing -38.2157 (+3.051, the max of five single mid-air samples; the
    // other four were an identical +2.509). The wire samples a sub-second arc
    // once per jump at no particular phase, so the official client's apex is
    // at or above this value -- not equal to it. The committed capture
    // evidence/b893_jump_fall.mp4 is the measurement that resolves it (B1011).
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
    // is the measured no-damage bound (evidence/oracle_measurements.md).
    float safeHeight = 18.0f;
};

inline const Fall& fall() {
    static const Fall kFall;
    return kFall;
}

// [MEASURED] the oracle's height -> damage points, lane 2, 2026-10-08
// (evidence/oracle_measurements.md: #goto z, then the OP_EnvDamage the official
// client sent; fall = goto_z - standing -38.2157). Every point is a wire
// reading from ONE character (Testchar, level 60 GM, 2255 HP). The 48.2u point
// (71, above the 53.2u point's 59) is the one #goto-jitter reading the series
// could not reproduce and is left out; everything else is in. B1011: this
// table replaced B893's 1.5-per-unit linear ramp, which disagreed with the
// oracle by up to 4.6x in the 23-33u band (model 8..23, oracle 5).
struct FallDamagePoint {
    float height;
    uint32_t damage;
};
inline constexpr FallDamagePoint kFallDamageTable[] = {
    {18.2f, 0},   // no OP_EnvDamage sent
    {23.2f, 5},
    {28.2f, 5},
    {33.2f, 5},
    {43.2f, 30},
    {53.2f, 59},
    {58.2f, 81},
    {63.2f, 87},
    {68.2f, 91},
};
inline constexpr int kFallDamageTableSize =
    static_cast<int>(sizeof(kFallDamageTable) / sizeof(kFallDamageTable[0]));

// The client's height->damage curve: 0 at or below the safe height, the
// oracle's value at every measured height, linear interpolation between
// neighbouring points, and -- above the last measured point -- the last
// segment's slope continued ([ASSUMED]: nothing above 68.2u was captured).
// Rounded to nearest, like the oracle's integer damage.
inline uint32_t damageForFallHeight(float fallHeight) {
    if (fallHeight <= fall().safeHeight) {
        return 0;
    }
    const FallDamagePoint* t = kFallDamageTable;
    const int n = kFallDamageTableSize;
    // Below the first measured point: interpolate from (safeHeight, 0).
    float x0 = fall().safeHeight, y0 = 0.0f;
    float x1 = t[0].height, y1 = static_cast<float>(t[0].damage);
    for (int i = 0; i < n; ++i) {
        if (fallHeight <= t[i].height) {
            x1 = t[i].height;
            y1 = static_cast<float>(t[i].damage);
            if (i > 0) {
                x0 = t[i - 1].height;
                y0 = static_cast<float>(t[i - 1].damage);
            }
            const float d = y0 + (y1 - y0) * (fallHeight - x0) / (x1 - x0);
            return static_cast<uint32_t>(d + 0.5f);
        }
    }
    // Above the table: extrapolate along the last segment.
    x0 = t[n - 2].height;
    y0 = static_cast<float>(t[n - 2].damage);
    x1 = t[n - 1].height;
    y1 = static_cast<float>(t[n - 1].damage);
    const float d = y1 + (y1 - y0) * (fallHeight - x1) / (x1 - x0);
    return static_cast<uint32_t>(d + 0.5f);
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
