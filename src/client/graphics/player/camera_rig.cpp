// T461 -- the camera set's name table and the orbit rig's geometry.
// Device-free; see include/client/graphics/player/camera_mode.h and
// include/client/graphics/player/camera_rig.h for the evidence and the
// conventions.

#include "client/graphics/player/camera_mode.h"
#include "client/graphics/player/camera_rig.h"

#include <cctype>
#include <cmath>

namespace EQT {
namespace Graphics {

namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string NormalizeModeToken(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_' || c == '\t') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

struct ModeRow {
    CameraMode mode;
    const char* name;   // eqstr_us.txt, verbatim
    const char* shortForm;
};

// eqstr_us.txt 9500..9505, in id order -- which is also the F9 cycle order.
const ModeRow kModes[] = {
    {CameraMode::FirstPerson, "First Person Camera", "firstperson"},
    {CameraMode::Overhead, "Overhead Camera", "overhead"},
    {CameraMode::Chase, "Chase Camera", "chase"},
    {CameraMode::User1, "User Defined Camera 1", "user1"},
    {CameraMode::User2, "User Defined Camera 2", "user2"},
    {CameraMode::Tether, "Tether Camera", "tether"},
};

float Clamp(float v, float lo, float hi, bool& clamped) {
    if (v < lo) { clamped = true; return lo; }
    if (v > hi) { clamped = true; return hi; }
    return v;
}

}  // namespace

CameraMode nextCameraMode(CameraMode mode) {
    const int n = static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));
    for (int i = 0; i < n; ++i) {
        if (kModes[i].mode == mode) return kModes[(i + 1) % n].mode;
    }
    return kDefaultCameraMode;
}

const char* cameraModeName(CameraMode mode) {
    for (const auto& row : kModes) {
        if (row.mode == mode) return row.name;
    }
    return "Unknown";
}

bool parseCameraModeName(const std::string& text, CameraMode& out) {
    const std::string norm = NormalizeModeToken(text);
    if (norm.empty()) return false;
    if (norm == "fp") { out = CameraMode::FirstPerson; return true; }
    for (const auto& row : kModes) {
        if (norm == row.shortForm || norm == NormalizeModeToken(row.name)) {
            out = row.mode;
            return true;
        }
    }
    return false;
}

bool cameraModeFromIniValue(int value, CameraMode& out) {
    for (const auto& row : kModes) {
        if (static_cast<int>(row.mode) == value) {
            out = row.mode;
            return true;
        }
    }
    return false;
}

bool cameraModeFromLegacyIniMode(int legacy, CameraMode& out) {
    switch (legacy) {
        case 0: out = CameraMode::Chase; return true;        // Free: no official camera, no body
        case 1: out = CameraMode::Chase; return true;        // Follow == Chase, same geometry
        case 2: out = CameraMode::FirstPerson; return true;  // FirstPerson
        default: return false;
    }
}

bool isRigCameraMode(CameraMode mode) {
    return mode == CameraMode::Overhead || mode == CameraMode::User1 ||
           mode == CameraMode::User2 || mode == CameraMode::Tether;
}

bool isUserCameraMode(CameraMode mode) {
    return mode == CameraMode::User1 || mode == CameraMode::User2;
}

bool clampCameraRig(CameraRig& rig) {
    bool clamped = false;
    const CameraRig fallback{};
    if (!std::isfinite(rig.azimuthDeg)) { rig.azimuthDeg = fallback.azimuthDeg; clamped = true; }
    if (!std::isfinite(rig.pitchDeg)) { rig.pitchDeg = fallback.pitchDeg; clamped = true; }
    if (!std::isfinite(rig.distance)) { rig.distance = fallback.distance; clamped = true; }
    if (!std::isfinite(rig.height)) { rig.height = fallback.height; clamped = true; }

    rig.azimuthDeg = std::fmod(rig.azimuthDeg, 360.0f);
    if (rig.azimuthDeg < 0.0f) rig.azimuthDeg += 360.0f;
    if (rig.azimuthDeg >= 360.0f) rig.azimuthDeg -= 360.0f;  // fmod rounding at -0.0...

    rig.pitchDeg = Clamp(rig.pitchDeg, kCameraRigMinPitchDeg, kCameraRigMaxPitchDeg, clamped);
    rig.distance = Clamp(rig.distance, kCameraRigMinDistance, kCameraRigMaxDistance, clamped);
    rig.height = Clamp(rig.height, kCameraRigMinHeight, kCameraRigMaxHeight, clamped);
    return clamped;
}

CameraRigPose computeCameraRigPose(const CameraRig& rig, float playerX, float playerY,
                                   float feetZ, float headingEq512) {
    // The renderer's heading convention, the one CameraController::
    // setFollowPosition() uses: forward = (sin h, cos h), h = heading/512*2pi.
    const float headingRad = headingEq512 / 512.0f * 2.0f * kPi;
    const float angle = headingRad + rig.azimuthDeg * kPi / 180.0f;
    const float pitchRad = rig.pitchDeg * kPi / 180.0f;

    CameraRigPose pose;
    pose.targetX = playerX;
    pose.targetY = playerY;
    pose.targetZ = feetZ + rig.height;

    const float horizontal = rig.distance * std::cos(pitchRad);
    pose.camX = pose.targetX - std::sin(angle) * horizontal;
    pose.camY = pose.targetY - std::cos(angle) * horizontal;
    pose.camZ = pose.targetZ + rig.distance * std::sin(pitchRad);
    return pose;
}

}  // namespace Graphics
}  // namespace EQT
