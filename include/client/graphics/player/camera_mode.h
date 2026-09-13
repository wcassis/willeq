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
// ORACLE-OWED (board B530): that F9 is TOGGLECAM's default key and that it
// cycles in exactly this order, which mode a character logs in with, and the
// geometry of every fixed camera. No file records a default key (an
// eqclient.ini [KeyMaps] line exists only for a binding the user CHANGED;
// see T382's note in tests/test_hotkey_manager.cpp). F9 is Will's ruling.
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

// fable's default. Chase is the camera fable used to call "Follow", unchanged
// in geometry, so the visual gate's goldens (a default-camera render) do not
// move. Whether the official client logs in on Chase is ORACLE-OWED (B530).
constexpr CameraMode kDefaultCameraMode = CameraMode::Chase;

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
// body; it maps to the default Chase, which is what a Free session actually
// saw in motion (nothing), rounded to the nearest live camera. False for any
// other value.
bool cameraModeFromLegacyIniMode(int legacy, CameraMode& out);

// The four modes whose placement is a CameraRig (camera_rig.h): Overhead,
// User1, User2, Tether. First person has its own measured formula
// (repositionFirstPersonCamera) and Chase keeps CameraController's follow path.
bool isRigCameraMode(CameraMode mode);

// User Defined Camera 1 / 2 -- the two the official client lets the player
// adjust and persists (eqclient.ini [Options] Camera1-* / Camera2-*).
bool isUserCameraMode(CameraMode mode);

}  // namespace Graphics
}  // namespace EQT
