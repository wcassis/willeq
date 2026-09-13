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
// ORACLE-OWED (board B530): whether the official user cameras hold their offset
// relative to the player's heading (as here) or to the world; what
// eqclient.ini's Camera1-DirHeading / Camera1-Pitch units are; whether
// Camera1-Height is the look-at height or the camera's own; what Camera1-Zoom
// (90) is. The clamps and step sizes below are fable's own, not measurements.

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
// PLACEHOLDERS, NOT MEASUREMENTS (ORACLE-OWED, B530). Picked only so each mode
// is visibly distinct until Leg B solves them against the official client.
constexpr CameraRig kOverheadPlaceholderRig{0.0f, 70.0f, 80.0f, 3.0f};
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
