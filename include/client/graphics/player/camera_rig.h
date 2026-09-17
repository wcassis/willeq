#pragma once

// T461 -- the orbit rig: azimuth, pitch, distance and look-at height around
// the player, and the ONE function that turns it into a camera pose.
//
// Device-free (no Irrlicht). PlayerCameraComponent owns one rig per rig mode
// (camera_mode.h: Overhead, Chase since T474, User1, User2, Tether),
// CameraController applies the BSP collision, and tests/test_camera_modes.cpp
// pins the geometry with no client at all.
//
// UNITS AND CONVENTIONS -- the `/camera` help text states the same four:
//   azimuthDeg  degrees around the look-at point, in the mode's AZIMUTH FRAME
//               (camera_mode.h cameraAzimuthFrame(); T471, board B580):
//                 PlayerHeading (Overhead): relative to the player's heading.
//                   0 = directly behind the player; 180 = in front, looking back.
//                 World (User Defined 1/2, Tether): a world direction -- the
//                   numbers a heading-0 player would read. 0 puts the camera on
//                   the -Y side of the look-at point looking +Y (EQ client
//                   frame), 90 on the -X side looking +X.
//               Either way a positive azimuth turns the camera in the same
//               sense as a growing EQ heading.
//   pitchDeg    degrees; + puts the camera ABOVE the look-at point, looking
//               down. Clamped to [-89, 89].
//   distance    EQ world units from the look-at point to the camera.
//   height      EQ world units of the look-at point above the player's FEET
//               (node origin minus the player's collisionZOffset -- the same
//               feet first_person_tunables.cpp's [FPCAM-DUMP] reports).
//
// "Behind" is the renderer's own: the forward vector is (sin h, cos h) in EQ
// client (x, y) with h = heading / 512 * 2pi, the vector the pre-T474 follow
// camera (CameraController::setFollowPosition(), deleted by T474) put the
// Chase camera behind. Since T474 the Chase camera IS an azimuth-0 rig.
//
// MEASURED ON THE OFFICIAL CLIENT (T466, review/T466/README.md):
//   - First Person, Overhead and Chase turn with the player; User Defined 1/2
//     and Tether hold a WORLD direction (h160 vs h288 edge 0.084 / 0.120 /
//     0.193, re-grab noise), and an entry AFTER the turn gives the same view.
//     T471 applied it: computeCameraRigPose() takes the mode's
//     CameraAzimuthFrame, and a World azimuth ignores the heading (B580).
//   - The official client does not read eqclient.ini's Camera1-*/Camera2-*
//     keys at launch (it rewrites them at login) and does not persist camera
//     changes, so those keys cannot give the units of DirHeading / Pitch /
//     Height / Zoom (board B585). They stay ORACLE-OWED through the fit only.
// The clamps and step sizes below are fable's own, not measurements.

#include "client/graphics/player/camera_mode.h"  // T471: CameraAzimuthFrame

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
constexpr float kCameraRigMaxDistance = 300.0f;   // the pre-T474 follow camera's MAX_FOLLOW_DISTANCE
constexpr float kCameraRigMinPitchDeg = -89.0f;
constexpr float kCameraRigMaxPitchDeg = 89.0f;
constexpr float kCameraRigMinHeight = -100.0f;
constexpr float kCameraRigMaxHeight = 100.0f;

// One zoom notch (a +/- key press or one wheel notch), EQ units. The same 2.0
// the +/- keys have always moved the Chase camera by.
constexpr float kCameraZoomStepUnits = 2.0f;
// One press of a Pitch or Tether-slide key, degrees. The KEYS are measured
// (T466, Options > Keys > Camera; bound by T471); the step is fable's own --
// T466 could not measure it (board B653).
constexpr float kCameraOrbitKeyStepDeg = 5.0f;
// One press of Raise / Lower camera (T471): EQ units of the Tether camera's
// look-at height. fable's own; what the official key moves and by how much is
// ORACLE-OWED (T466 scored Raise x10 in User 1 at re-grab noise, 0.022 --
// board B653).
constexpr float kCameraRaiseKeyStepUnits = 1.0f;

// User Defined Camera 1 / 2 defaults. Distance and Height are quoted from
// /mnt/projects/EverQuestP1999/eqclient.ini [Options]
//   Camera1-Distance=30  Camera1-Height=5   Camera1-Pitch=0
//   Camera2-Distance=82  Camera2-Height=18  Camera2-Pitch=0
// (the same values in eqclient-obian.ini, eqclient-seth.ini,
// eqclient-bilblow.ini, p1999-obian.ini and p1999-obian2.ini). The azimuth is
// a PLACEHOLDER 0: the files say Camera1-DirHeading=192 / Camera2-DirHeading=277
// but not in what unit or from what zero (ORACLE-OWED, B530), and the official
// client does not read those keys at all (B585). Since T471 it is a WORLD
// azimuth (B580); which world direction the official cameras start at is
// unmeasured (board B651).
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
// Chase Camera: MEASURED (T466), APPLIED by T474 (board B650). Same sweep, same
// pose and metric as Overhead above, scored on the official client's own Chase
// frame (fable/sessions/fit2_* and fit4_*, scores_vs_oracle_chase_wallcrop.tsv).
// Minimum edge 0.7379 at az 0, pitch 6, dist 4.0, height 7.0, reproduced to four
// decimals in two sessions; bracketed on every axis:
//   dist 3.5 -> 0.767, dist 4.5 -> 0.839     (pitch 6, height 7)
//   height 6.5 -> 0.779, height 7.5 -> 0.922 (pitch 6, dist 4)
//   pitch 3 -> 0.937, pitch 9 -> 0.880       (dist 4, height 7)
// Read it as dist 4.0 +/- 0.3, height 7.0 +/- 0.3, pitch 6 +/- 1.5. Azimuth 0:
// Chase turns with the player (h160 vs h288 edge 0.680). Before T474 Chase was
// CameraController::setFollowPosition(): 20 behind, 10 above the node origin,
// looking 10 AHEAD of the player -- 16.02 farther back than this, 5.70 higher,
// 1.60 degrees more pitch (T471's computation). The camera and its look-at are
// the rig's; the scale below is applied first.
//
// NOT SETTLED (T478, board B721): the fit depends on the scoring crop, and no
// rig is the minimum at both headings on either crop. On T474's wall crop
// (400x600+0+100, which leaves the player out) h160 minimises at p6 / d4 /
// h6.75 and h288 at p7.5 / d4 / h7.5; on a player-centred crop
// (424x668+300+100) h160 at p6 / d2.5 / h6.5 and h288 at p4.5 / d2 / h6.5;
// the official figure's head is about 1.6x the width of fable's at this rig,
// and the same rig scores up to 0.033 apart between two sessions. The
// distance agrees at 4.0 on the wall crop only. This rig stays until a metric
// that uses the whole frame without being dominated by the model's shading
// exists (B721). Whether the official Chase zooms at all is board B680
// (narrowed T478: the wheel does not zoom it).
constexpr CameraRig kChaseRig{0.0f, 6.0f, 4.0f, 7.0f};
// The character the Chase fit was taken on: T466's Human male Warrior, whose eye
// the official client puts 6.023 +/- 0.08 above its own feet (T431, solved on the
// same oracle at the same qeynos2 spot; fable's head-bone eye for that model
// reproduces it, review/T431/README.md section 0).
constexpr float kChaseFitEyeAboveFeet = 6.023f;

// T474: THE CHASE FIT'S RACE SCALING. T466 fitted ONE character, so the only
// per-race input fable has is the per-race eye it already derives from each
// model's head bone (first_person_eye.h, T301/T431). The rig is scaled
// UNIFORMLY about the feet by eyeAboveFeet / kChaseFitEyeAboveFeet -- distance
// and look-at height both, azimuth and pitch untouched -- so a character is
// framed the same way whatever its size, and the fitted character is placed
// exactly at the fit. That the official client scales Chase this way (or at
// all) is NOT measured; every race but a Human male is unmeasured (board B681).
// A non-finite or non-positive eye leaves the rig unscaled.
CameraRig scaleChaseRigForEye(const CameraRig& rig, float eyeAboveFeet);
// Tether Camera: STILL A PLACEHOLDER (ORACLE-OWED, B530). T466's coarse sweep
// did not converge: best 0.749 at az 300, pitch 5, dist 20 on the horizon crop,
// on the edge of both the pitch and the distance ranges. The official Tether is
// world-anchored and follows a moving player; since T471 the rig holds its
// azimuth in the world frame (B580) and follows the player's position, but
// these four numbers are still placeholders (B530, B651).
constexpr CameraRig kTetherPlaceholderRig{0.0f, 10.0f, 30.0f, 5.0f};

// Clamp pitch, distance and height to the ranges above and wrap the azimuth to
// [0, 360). Returns true when a CLAMP moved a value (wrapping is not a clamp),
// so `/camera set` can say so. A non-finite value is replaced by the default
// rig's value and counts as clamped.
bool clampCameraRig(CameraRig& rig);

// The pose for `rig` around a player at (playerX, playerY) with feet at feetZ
// and EQ heading headingEq512 (0..512). Pure; the caller applies collision.
// `frame` is the mode's cameraAzimuthFrame(): a World azimuth ignores the
// heading (T471, B580).
CameraRigPose computeCameraRigPose(const CameraRig& rig, float playerX, float playerY,
                                   float feetZ, float headingEq512,
                                   CameraAzimuthFrame frame = CameraAzimuthFrame::PlayerHeading);

}  // namespace Graphics
}  // namespace EQT
