// B1167 (#486): the b1130 rig's WORLD orientation -- the headings the B1130
// captures were taken at, so a world-fixed EDD word (spellsnew.edd +0x12C, the
// world +X drift: EQGraphicsDX9 0x10062917 `world x += s*D12C*dt`, applied by
// the sim at src/client/graphics/sim_particles.cpp updateEddMotion) points
// where it pointed on the oracle's screen, and the caster/target face the
// camera the way the oracle's did (the +0x60 emitters orient to the attach
// point, i.e. to the actor's facing).
//
// Before B1167 the b1130 presets built the scene in an arbitrary world frame:
// caster at the origin facing +Z, the camera on +Z looking back at her
// (kHarnessFaceOnRotY), the target solved at its screen anchor. Every screen
// fact held, but the world's +X axis sat 90 deg off-screen-left, and the
// caster faced the camera where the oracle's turned her back to it.
//
// The capture facts (refactor/B1167/README.md sec. 2; decoded by
// refactor/B1167/scripts/oracle_poses.py from review/B1130/oracle/<leg>/capture.pcap):
// - every fixed_npc leg: the caster `#goto -67.6 434.4` lands at heading 0
//   (C->S OP_ClientUpdate heading word 0), the `#spawn TrollF` target stands at
//   the caster's pre-goto spot (-74, 428) with her heading then, 160;
// - every fixed_self leg: the caster at (-74, 428), heading 160;
// - every leg: `Camera1-DirHeading=192` (eqclient-before.ini, all 47 B1130
//   captures), the user camera's direction from the player relative to the
//   player's heading (F9 x3 = user camera 1, review/B1130/README.md "Plans").
// Headings are EQ 512ths of a turn. The client's render rule for a heading is
// node rotation Y = -heading deg (src/client/graphics/entity_mesh_build.cpp,
// "EQ heading to Irrlicht Y rotation"); with the models' rest facing +X
// (kHarnessFaceOnRotY's note) an actor at heading h faces the direction at angle
// +h deg from +X toward +Z, and a camera at DirHeading d sits at angle h + d.
//
// The harness keeps its fit (camera on +Z, i.e. at angle 90 deg from the
// caster) and then turns the WHOLE rig -- camera, caster, target -- about the
// caster's vertical axis by psi = h_caster + d - 90 deg, so the rig's camera
// lands where the capture's did relative to the world axes. A rigid turn of
// the rig changes no screen fact the fit solved.
//
// Header-only and Irrlicht-free (tests/test_fx_harness.cpp, B1167CapturePose*).
#pragma once

#include <cmath>

namespace fxharness {

struct CapturePose {
    float casterHeading512;     // the caster's heading at the cast
    float targetHeading512;     // the target's (unused for a self leg)
    float cameraDirHeading512;  // Camera1-DirHeading
};

// fixed_npc legs (--camera b1130) and fixed_self legs (--camera b1130-self).
constexpr CapturePose kB1130NpcPose{0.0f, 160.0f, 192.0f};
constexpr CapturePose kB1130SelfPose{160.0f, 160.0f, 192.0f};

// The direction the fitted harness camera sits at, seen from the caster
// (angle from +X toward +Z, deg): +Z (kCasterFacing).
constexpr float kHarnessCameraAngleDeg = 90.0f;

inline float headingDeg(float h512) { return h512 * 360.0f / 512.0f; }

// The client's node rotation Y for a heading (entity_mesh_build.cpp).
inline float rotYForHeading(float h512) { return -headingDeg(h512); }

// The rig turn psi (deg, from +X toward +Z) that puts the fitted camera at the
// capture's camera direction.
inline float rigYawDeg(const CapturePose& p) {
    return headingDeg(p.casterHeading512) + headingDeg(p.cameraDirHeading512) - kHarnessCameraAngleDeg;
}

// The node rotation Y an actor needs BEFORE the turn (in the fit's frame) so
// that, turned by psi, it faces its capture heading. Facing angle = -rotY.
// Not applied yet: with the caster at heading 0 the fit's camera comes to
// 30.28 units and the human target renders 245 px, outside the bless-only
// standing-height ratchet (refactor/B1167/README.md RULING REQUEST); the
// harness keeps both actors face-on (kHarnessFaceOnRotY) through the turn.
inline float preTurnRotY(float h512, float psiDeg) { return rotYForHeading(h512) + psiDeg; }

// Turn a horizontal (x, z) offset by psi (deg, from +X toward +Z).
inline void turnXZ(float psiDeg, float& x, float& z) {
    const float a = psiDeg * 3.14159265358979f / 180.0f;
    const float c = std::cos(a), s = std::sin(a);
    const float nx = x * c - z * s, nz = x * s + z * c;
    x = nx;
    z = nz;
}

}  // namespace fxharness
