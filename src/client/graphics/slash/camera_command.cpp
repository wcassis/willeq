// T461 -- `/camera` parsing. See include/client/graphics/slash/camera_command.h.

#include "client/graphics/slash/camera_command.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace EQT {
namespace Graphics {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> Tokens(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string t;
    while (in >> t) out.push_back(t);
    return out;
}

// The whole token must be a finite number: "5x", "", "nan" and "inf" are not.
bool ParseFiniteFloat(const std::string& text, float& out) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || !std::isfinite(v)) {
        return false;
    }
    out = v;
    return true;
}

CameraCommand Error(const std::string& what) {
    CameraCommand c;
    c.kind = CameraCommand::Kind::Error;
    c.error = what + " -- '/camera help' lists the forms and their units.";
    return c;
}

CameraCommand ParseEdit(const std::vector<std::string>& tokens, CameraCommand::Kind kind) {
    const std::string verb = Lower(tokens[0]);
    if (tokens.size() < 3) {
        return Error("/camera " + verb + " needs at least one '<az|pitch|dist|height> <number>' pair");
    }
    if ((tokens.size() - 1) % 2 != 0) {
        return Error("/camera " + verb + ": '" + tokens.back() + "' has no value");
    }
    CameraCommand c;
    c.kind = kind;
    for (size_t i = 1; i + 1 < tokens.size(); i += 2) {
        const std::string key = Lower(tokens[i]);
        float value = 0.0f;
        if (!ParseFiniteFloat(tokens[i + 1], value)) {
            return Error("/camera " + verb + ": '" + tokens[i + 1] + "' is not a finite number (for " + key + ")");
        }
        bool* has = nullptr;
        float* slot = nullptr;
        if (key == "az") { has = &c.edit.hasAzimuth; slot = &c.edit.azimuthDeg; }
        else if (key == "pitch") { has = &c.edit.hasPitch; slot = &c.edit.pitchDeg; }
        else if (key == "dist") { has = &c.edit.hasDistance; slot = &c.edit.distance; }
        else if (key == "height") { has = &c.edit.hasHeight; slot = &c.edit.height; }
        else {
            return Error("/camera " + verb + ": unknown value '" + tokens[i] + "' (az, pitch, dist, height)");
        }
        if (*has) {
            return Error("/camera " + verb + ": " + key + " given twice");
        }
        *has = true;
        *slot = value;
    }
    return c;
}

}  // namespace

CameraCommand ParseCameraCommand(const std::string& args) {
    const std::vector<std::string> tokens = Tokens(args);
    CameraCommand c;
    if (tokens.empty()) {
        c.kind = CameraCommand::Kind::Report;
        return c;
    }
    const std::string verb = Lower(tokens[0]);
    if (verb == "cycle" || verb == "get" || verb == "help") {
        if (tokens.size() != 1) {
            return Error("/camera " + verb + " takes no arguments");
        }
        c.kind = verb == "cycle" ? CameraCommand::Kind::Cycle
               : verb == "get"   ? CameraCommand::Kind::Get
                                 : CameraCommand::Kind::Help;
        return c;
    }
    if (verb == "set") return ParseEdit(tokens, CameraCommand::Kind::Set);
    if (verb == "nudge") return ParseEdit(tokens, CameraCommand::Kind::Nudge);

    CameraMode mode;
    if (parseCameraModeName(args, mode)) {
        c.kind = CameraCommand::Kind::SetMode;
        c.mode = mode;
        return c;
    }
    return Error("Unknown camera mode or subcommand '" + args + "'");
}

std::vector<std::string> CameraCommandUsageLines() {
    return {
        "/camera -- report the camera mode",
        "/camera cycle -- next mode, the same as F9 (First Person, Overhead, Chase, User Defined 1, User Defined 2, Tether)",
        "/camera <mode> -- firstperson (fp), overhead, chase, user1, user2, tether, or the full official name",
        "/camera set az <deg> pitch <deg> dist <units> height <units> -- any subset, any order; edits the active user camera, or switches to User Defined Camera 1",
        "/camera nudge az <deg> pitch <deg> dist <units> height <units> -- adds to the current values",
        "/camera get -- prints the mode, the four values and the resulting camera position and look-at point",
        "Units: az = degrees around the player from its heading (0 = behind, 180 = in front); "
        "pitch = degrees, + = camera above the look-at point (clamped -89..89); "
        "dist = EQ world units from the look-at point (1..300); "
        "height = EQ world units of the look-at point above the player's feet (-100..100)",
    };
}

}  // namespace Graphics
}  // namespace EQT
