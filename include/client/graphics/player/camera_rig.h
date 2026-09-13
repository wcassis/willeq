#pragma once

// T461 -- the orbit rig: azimuth, pitch, distance and look-at height around
// the player, and the ONE function that turns it into a camera pose.
//
// Device-free (no Irrlicht). PlayerCameraComponent owns one rig per rig mode
// (camera_mode.h: Overhead, User1, User2, Tether), CameraController applies
// the BSP collision, and tests/test_camera_modes.cpp pins the geometry with no
// client at all.
//
// UNITS AND CONVENTIONS -- the `/camera` help text states the same four:
//   azimuthDeg  degrees around the player, RELATIVE TO THE PLAYER'S HEADING.
//               0 = directly behind the player; 180 = in front, looking back.
//               A positive azimuth turns the camera in the same sense as a
//               growing EQ heading.
//   pitchDeg    degrees; + puts the camera ABOVE the look-at point, looking
//               down. Clamped to [-89, 89].
//   distance    EQ world units from the look-at point to the camera.
//   height      EQ world units of the look-at point above the player's FEET
//               (node origin minus the player's collisionZOffset -- the same
//               feet first_person_tunables.cpp's [FPCAM-DUMP] reports).
//
// "Behind" is the renderer's own: the forward vector is (sin h, cos h) in EQ
// client (x, y) with h = heading / 512 * 2pi, exactly the vector
// CameraController::setFollowPosition() puts the Chase camera behind. So an
// azimuth-0 rig and the Chase camera look the same way.
//
// MEASURED ON THE OFFICIAL CLIENT (T466, review/T466/README.md):
//   - First Person, Overhead and Chase turn with the player; User Defined 1/2
//     and Tether hold a WORLD direction (h160 vs h288 edge 0.084 / 0.120 /
//     0.193, re-grab noise). This rig's azimuth is heading-relative, so a
//     user/tether azimuth here is right at one heading only -- board B580.
//   - The official client does not read eqclient.ini's Camera1-*/Camera2-*
//     keys at launch (it rewrites them at login) and does not persist camera
//     changes, so those keys cannot give the units of DirHeading / Pitch /
//     Height / Zoom (board B585). They stay ORACLE-OWED through the fit only.
// The clamps and step sizes below are fable's own, not measurements.

namespace EQT {
namespace Graphics {

struct CameraRig {
    float azimuthDeg = 0.0f;
    float pitchDeg = 0.0f;
    float distance = 30.0f;
    float height = 5.0f;

    bool operator==(const CameraRig& o) const {
        return azimuthDeg == o.azimuthDeg && pitchDeg == o.pitchDeg &&
               distance == o.distance && height == o.height;
    }
    bool operator!=(const CameraRig& o) const { return !(*this == o); }
};

// A resolved pose, in EQ CLIENT coordinates (Z up; the renderer swaps y/z when
// it hands this to Irrlicht).
struct CameraRigPose {
    float camX = 0.0f, camY = 0.0f, camZ = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
};

constexpr float kCameraRigMinDistance = 1.0f;
constexpr float kCameraRigMaxDistance = 300.0f;   // CameraController's MAX_FOLLOW_DISTANCE
constexpr float kCameraRigMinPitchDeg = -89.0f;
constexpr float kCameraRigMaxPitchDeg = 89.0f;
constexpr float kCameraRigMinHeight = -100.0f;
constexpr float kCameraRigMaxHeight = 100.0f;

// One zoom notch (a +/- key press or one wheel notch), EQ units. The same 2.0
// the +/- keys have always moved the Chase camera by.
constexpr float kCameraZoomStepUnits = 2.0f;
// One press of an orbit/pitch key, degrees. fable's own; ORACLE-OWED.
constexpr float kCameraOrbitKeyStepDeg = 5.0f;

// User Defined Camera 1 / 2 defaults. Distance and Height are quoted from
// /mnt/projects/EverQuestP1999/eqclient.ini [Options]
//   Camera1-Distance=30  Camera1-Height=5   Camera1-Pitch=0
//   Camera2-Distance=82  Camera2-Height=18  Camera2-Pitch=0
// (the same values in eqclient-obian.ini, eqclient-seth.ini,
// eqclient-bilblow.ini, p1999-obian.ini and p1999-obian2.ini). The azimuth is
// a PLACEHOLDER 0: the files say Camera1-DirHeading=192 / Camera2-DirHeading=277
// but not in what unit or from what zero (ORACLE-OWED, B530).
constexpr CameraRig kUser1DefaultRig{0.0f, 0.0f, 30.0f, 5.0f};
constexpr CameraRig kUser2DefaultRig{0.0f, 0.0f, 82.0f, 18.0f};
// Overhead Camera: MEASURED (T466). fable swept this rig with `/camera set` at
// qeynos2 (-74, 428, 3.751) heading 160, 1024x768, against the official
// client's own Overhead frame at the same pose (T310's gradient metric, crop
// root 1022x420+0+40; fable/sessions/fit1..fit4 in review/T466/). Minimum edge
// 0.774 at pitch 83, dist 45, height 3; bracketed on every axis:
//   pitch 80 -> 0.798, pitch 86 -> 0.788   (dist 45, height 3)
//   dist 40  -> 0.803, dist 50  -> 0.794   (pitch 83, height 3)
//   height 0 -> 0.798                      (pitch 83, dist 45)
// The bowl is shallow (+0.01..0.03 per step), so read it as pitch 83 +/- 3,
// dist 45 +/- 5, height 3 +/- 3. Azimuth 0: Overhead turns with the player.
constexpr CameraRig kOverheadRig{0.0f, 83.0f, 45.0f, 3.0f};
// Tether Camera: STILL A PLACEHOLDER (ORACLE-OWED, B530). T466's coarse sweep
// did not converge: best 0.749 at az 300, pitch 5, dist 20 on the horizon crop,
// on the edge of both the pitch and the distance ranges, and the official Tether
// is world-anchored and follows a moving player (B580), which this rig cannot
// express.
constexpr CameraRig kTetherPlaceholderRig{0.0f, 10.0f, 30.0f, 5.0f};

// Clamp pitch, distance and height to the ranges above and wrap the azimuth to
// [0, 360). Returns true when a CLAMP moved a value (wrapping is not a clamp),
// so `/camera set` can say so. A non-finite value is replaced by the default
// rig's value and counts as clamped.
bool clampCameraRig(CameraRig& rig);

// The pose for `rig` around a player at (playerX, playerY) with feet at feetZ
// and EQ heading headingEq512 (0..512). Pure; the caller applies collision.
CameraRigPose computeCameraRigPose(const CameraRig& rig, float playerX, float playerY,
                                   float feetZ, float headingEq512);

}  // namespace Graphics
}  // namespace EQT
