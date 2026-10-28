// B1167 (#486): the presets' WORLD orientation -- the headings and camera the
// B1130 / B1118 captures were taken at, so (a) a world-fixed EDD word
// (spellsnew.edd +0x12C, the world +X drift: EQGraphicsDX9 0x10062917
// `world x += s*D12C*dt`, applied by the sim at src/client/graphics/
// sim_particles.cpp updateEddMotion) points where it pointed on the capture's
// screen, and (b) the actors face where the capture's actors faced (WILL'S
// RULE, the orchestrator's 2026-10-27 ruling on issue #486: "the oracle is
// reproduced exactly, never approximated").
//
// The capture facts (refactor/B1167/README.md sec. 2 and the continuation
// sec. 7; decoded by refactor/B1167/scripts/oracle_poses.py from
// review/B1130/oracle/<leg>/capture.pcap and review/B1118/oracle_druid/
// capture.pcap):
// - every B1130 fixed_npc leg: the caster `#goto -67.6 434.4` lands at heading
//   0 (C->S OP_ClientUpdate heading word 0), the `#spawn TrollF` target stands
//   at the caster's pre-goto spot (-74, 428) with her heading then, 160;
// - every B1130 fixed_self leg: the caster at (-74, 428), heading 160;
// - every B1130 leg: `Camera1-DirHeading=192` (eqclient-before.ini, all 47
//   B1130 captures; F9 x3 = user camera 1, review/B1130/README.md "Plans");
// - the B1118 leg: the caster at (-74, 428) heading 160 for the WHOLE capture
//   (review/B1118/oracle_druid/capture.pcap: every C->S OP_ClientUpdate
//   heading 160), `#spawn TrollF` at (-64, 423.75) heading 160 (the capture's
//   OP_NewSpawn), and TWO cameras, both pinned: the oracle's user camera 1
//   (`Camera1-Distance=30`, `Camera1-DirHeading=192`, `Camera1-Height=5` in
//   review/B1118/oracle_druid/eqclient-before.ini) and the fable leg B's
//   chase camera, pinned by its own plan (`review/B1118/plans/fable_B.txt`:
//   `/camera chase` + `/camera set az 180 pitch 12 dist 32`). The b1118
//   preset's framing (228 px, feet row 605, 12 deg pitch) is the FABLE leg's
//   (refactor/B1139/README.md), so the preset's DirHeading is the chase
//   camera's 180 deg (256/512), not the oracle's 135 deg.
// Headings are EQ 512ths of a turn. The client's render rule for a heading is
// node rotation Y = -heading deg (src/client/graphics/entity_mesh_build.cpp,
// "EQ heading to Irrlicht Y rotation"); with the models' rest facing +X
// (kHarnessFaceOnRotY's note) an actor at heading h faces the direction at
// angle +h deg from +X toward +Z, and a camera at DirHeading d sits at angle
// h + d.
//
// The harness fits on the actors AT their capture headings (the ruling:
// b1130 fits on the back-turned caster -- her box 209 px / feet 598 is the
// oracle's back-turned box; the fit-frame rotation presetRotYDeg is the
// heading expressed against the fitted camera), then turns the rig -- camera,
// actor rotations and (for b1130) the solved target position -- about the
// caster's vertical axis by psi = h_caster + d - 90 deg, so the camera lands
// where the capture's did relative to the world axes. After the turn an
// actor's rotation is exactly rotYForHeading(its capture heading): the world
// frame sits where the capture's did, on screen as the capture saw it.
//
// Header-only and Irrlicht-free (tests/test_fx_harness.cpp, B1167CapturePose*).
#pragma once

#include <cmath>
#include <cstring>

namespace fxharness {

struct CapturePose {
    float casterHeading512;     // the caster's heading at the cast
    float targetHeading512;     // the target's (unused for a self leg)
    float cameraDirHeading512;  // the capture camera's DirHeading (the preset's own)
};

// fixed_npc legs (--camera b1130) and fixed_self legs (--camera b1130-self),
// from the 47 B1130 captures.
constexpr CapturePose kB1130NpcPose{0.0f, 160.0f, 192.0f};
constexpr CapturePose kB1130SelfPose{160.0f, 160.0f, 192.0f};
// --camera b1118: caster and target both heading 160 (review/B1118/
// oracle_druid/capture.pcap), the fable leg B's pinned chase camera (az 180 =
// 256/512; review/B1118/plans/fable_B.txt) -- the camera the preset's framing
// was fitted to.
constexpr CapturePose kB1118Pose{160.0f, 160.0f, 256.0f};

// The preset's capture pose (nullptr for a preset that has none).
inline const CapturePose* poseForPreset(const char* preset) {
    if (std::strcmp(preset, "b1130") == 0) return &kB1130NpcPose;
    if (std::strcmp(preset, "b1130-self") == 0) return &kB1130SelfPose;
    if (std::strcmp(preset, "b1118") == 0) return &kB1118Pose;
    return nullptr;
}

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

// The actor's rotation Y in the FITTED frame (before the rig turn), so that
// after the turn -- which subtracts psi -- the actor sits at
// rotYForHeading(actorHeading512): facing where the capture's actor faced.
// For the caster this is cameraDirHeading - 90 (b1130: 45 deg, three-quarters
// back; b1118: 90 deg, back to the chase camera).
inline float presetRotYDeg(const CapturePose& p, float actorHeading512) {
    return rigYawDeg(p) - headingDeg(actorHeading512);
}

// Turn a horizontal (x, z) offset by psi (deg, from +X toward +Z).
inline void turnXZ(float psiDeg, float& x, float& z) {
    const float a = psiDeg * 3.14159265358979f / 180.0f;
    const float c = std::cos(a), s = std::sin(a);
    const float nx = x * c - z * s, nz = x * s + z * c;
    x = nx;
    z = nz;
}

}  // namespace fxharness
