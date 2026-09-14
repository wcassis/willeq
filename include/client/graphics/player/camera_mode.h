#pragma once

// T461 -- THE CAMERA SET, matched to the official client's own vocabulary.
//
// Device-free on purpose (no Irrlicht, no renderer): ui_preferences.cpp needs
// the ini mapping and tests/test_camera_modes.cpp needs all of it without a
// client. renderer_types.h includes this header, so the historical spelling
// EQT::Graphics::IrrlichtRenderer::CameraMode still resolves.
//
// WHAT THE CLIENT FILES SETTLE (every row quoted, file + line, in
// refactor/T461_camera_modes_orbit_f9.md's Review):
//
//   /mnt/projects/EverQuestP1999/eqstr_us.txt
//     9500 First Person Camera
//     9501 Overhead Camera
//     9502 Chase Camera
//     9503 User Defined Camera 1
//     9504 User Defined Camera 2
//     9505 Tether Camera
//   eqgame.exe's KEYMAPPING_ action-name table carries the same six as
//   FIRST_PERSON_CAMERA .. TETHER_CAMERA, plus TOGGLECAM, whose eqstr label is
//   6536 "Toggle camera view".
//
// So the NAMES and the SET are the files' own, and the enumerator values run
// from 1 in the eqstr id order (9500 -> 1 ... 9505 -> 6).
//
// MEASURED ON THE OFFICIAL CLIENT (T466, review/T466/README.md), no longer
// oracle-owed: F9 is TOGGLECAM's key and it cycles in exactly this order
// (review/T466/logs/oracle_F9_successor_matrix.tsv); a character logs in on
// FIRST PERSON (kDefaultCameraMode below); and which cameras turn with the
// player (cameraAzimuthFrame() below). The geometry of Tether and the user
// cameras is still ORACLE-OWED (board B530).
//
// THERE IS NO "Free" MODE ANY MORE. It was never an official camera, and it
// had no body: CameraController::update() had no caller, so Free froze the
// view (T319). An old `[Camera] Mode=0` is mapped explicitly, see
// cameraModeFromLegacyIniMode().

#include <string>

namespace EQT {
namespace Graphics {

enum class CameraMode : int {
    FirstPerson = 1,  // eqstr 9500 "First Person Camera"   FIRST_PERSON_CAMERA
    Overhead = 2,     // eqstr 9501 "Overhead Camera"       OVERHEAD_CAMERA
    Chase = 3,        // eqstr 9502 "Chase Camera"          CHASE_CAMERA
    User1 = 4,        // eqstr 9503 "User Defined Camera 1" USER1_CAMERA
    User2 = 5,        // eqstr 9504 "User Defined Camera 2" USER2_CAMERA
    Tether = 6,       // eqstr 9505 "Tether Camera"         TETHER_CAMERA
};

// The camera a character with no saved `[Camera] View=` logs in with -- the
// OFFICIAL client's login camera. T471: MEASURED by T466, which scored the
// official client's untouched first frame after entering world against each
// direct camera: First Person 0.232, every other camera >= 0.897
// (review/T466/logs/oracle_login_camera_scores.tsv). Before T471 this was
// Chase, fable's old follow camera, pending that measurement (B530). A saved
// View= still wins (FrameComponent::syncUiPreferences). The visual gate does
// not read it: gles2_zone_benchmark renders through its own fixed camera.
constexpr CameraMode kDefaultCameraMode = CameraMode::FirstPerson;

// T471 (board B580): the frame a rig camera's azimuth is held in. MEASURED by
// T466: the character at heading 160 and then at 288 (+90 degrees), each
// camera entered afresh after the turn, h160 vs h288 edge score -- First
// Person 0.837, Overhead 0.689, Chase 0.680 (turned with the player); User 1
// 0.084, User 2 0.120, Tether 0.193 (a re-grab scores 0.03-0.11: the same
// WORLD view). An entry after the turn gave the same view, so the world
// direction is not captured from the heading at entry; it is held.
enum class CameraAzimuthFrame {
    PlayerHeading,  // the azimuth is added to the player's heading (Overhead)
    World,          // the azimuth IS the world direction (User 1, User 2, Tether)
};

// World for User1, User2 and Tether; PlayerHeading for every other mode.
CameraAzimuthFrame cameraAzimuthFrame(CameraMode mode);

// "world" / "player_heading" -- the token `/camera get` prints as az_frame=.
const char* cameraAzimuthFrameName(CameraAzimuthFrame frame);

// The next mode in the F9 cycle: 1 -> 2 -> ... -> 6 -> 1 (eqstr id order).
CameraMode nextCameraMode(CameraMode mode);

// The official display name, verbatim from eqstr_us.txt (9500..9505).
const char* cameraModeName(CameraMode mode);

// Case-insensitive; spaces, '-' and '_' ignored. Accepts every
// cameraModeName() output (so /camera's set-then-read round-trips) plus one
// short form each: firstperson/fp, overhead, chase, user1, user2, tether.
bool parseCameraModeName(const std::string& text, CameraMode& out);

// `[Camera] View=` -- the enumerator value, 1..6. False for anything else.
bool cameraModeFromIniValue(int value, CameraMode& out);

// `[Camera] Mode=` as T432 wrote it before T461: 0 Free, 1 Follow,
// 2 FirstPerson. Mapped EXPLICITLY, never by cast -- 1 and 2 mean different
// modes in the two numberings. Free (0) has no official counterpart and no
// body; it maps to Chase (the camera a pre-T461 file's default was), which is
// what a Free session actually saw in motion (nothing), rounded to the nearest
// live camera. T471 moved kDefaultCameraMode to First Person; these two legacy
// values stay Chase, because that is what the file they come from meant.
// False for any other value.
bool cameraModeFromLegacyIniMode(int legacy, CameraMode& out);

// The five modes whose placement is a CameraRig (camera_rig.h): Overhead,
// Chase, User1, User2, Tether. First person has its own measured formula
// (repositionFirstPersonCamera). Chase joined in T474 (board B650), when its
// geometry became T466's oracle fit and CameraController's follow path was
// deleted.
bool isRigCameraMode(CameraMode mode);

// User Defined Camera 1 / 2 -- the two the official client lets the player
// adjust and persists (eqclient.ini [Options] Camera1-* / Camera2-*).
bool isUserCameraMode(CameraMode mode);

}  // namespace Graphics
}  // namespace EQT
