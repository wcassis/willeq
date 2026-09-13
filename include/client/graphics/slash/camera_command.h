#pragma once

// T461 -- `/camera`'s grammar, parsed device-free so every form (and every bad
// input) is testable with no renderer. SlashCommandsComponent hands the
// argument string to PlayerCameraComponent::runCameraCommand(), which calls
// ParseCameraCommand() and executes the result.
//
//   /camera                      report the current mode
//   /camera cycle                the next mode (what F9 does)
//   /camera <mode>               first person | overhead | chase | user1 |
//                                user2 | tether (or any official name)
//   /camera set   az <deg> pitch <deg> dist <u> height <u>   any subset, any order
//   /camera nudge az <deg> pitch <deg> dist <u> height <u>   adds to the values
//   /camera get                  mode, the four values, the resulting pose
//   /camera help                 the usage lines
//
// Units: see include/client/graphics/player/camera_rig.h.

#include "client/graphics/player/camera_mode.h"

#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

struct CameraRigEdit {
    bool hasAzimuth = false;
    bool hasPitch = false;
    bool hasDistance = false;
    bool hasHeight = false;
    float azimuthDeg = 0.0f;
    float pitchDeg = 0.0f;
    float distance = 0.0f;
    float height = 0.0f;
};

struct CameraCommand {
    enum class Kind { Report, Cycle, SetMode, Set, Nudge, Get, Help, Error };
    Kind kind = Kind::Report;
    CameraMode mode = kDefaultCameraMode;   // Kind::SetMode
    CameraRigEdit edit;                     // Kind::Set / Kind::Nudge
    std::string error;                      // Kind::Error, human-readable
};

CameraCommand ParseCameraCommand(const std::string& args);

// The help text, one chat line each, units stated.
std::vector<std::string> CameraCommandUsageLines();

}  // namespace Graphics
}  // namespace EQT
