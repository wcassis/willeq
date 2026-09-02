// T310 Leg A -- the /fpcam command's text half: name lookup, parsing,
// formatting. See include/client/graphics/first_person_tunables.h.
//
// WHY THIS IS ITS OWN TU AND NOT MORE OF irrlicht_renderer.cpp: that file was
// the ONE sanctioned home of the first-person camera formula
// (tests/test_first_person_camera_single_formula.cpp), it is already enormous,
// and none of this needs a camera. The /fpcam arm over there is a five-line
// dispatch into these functions plus the one thing that DOES need the live
// camera -- `dump`, the resolved pose.
//
// Nothing in this file may name kFirstPersonPitchDownDegrees,
// kFirstPersonHorizontalFovDegrees, firstPersonMaxDownPitchDegrees,
// firstPersonMinLookPitchDegrees, firstPersonVerticalFovRadians or
// computeFirstPersonCamera: the single-formula drift test counts the files
// under src/client/graphics that mention them and allows exactly one. The
// spec table reaches the defaults through fpTunableSpecs() instead.

#include "client/graphics/first_person_tunables.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace EQT {
namespace Graphics {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string Num(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f", static_cast<double>(v));
    return std::string(buf);
}

bool ParseFloat(const std::string& text, float& out) {
    const std::string t = Trim(text);
    if (t.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

std::vector<std::string> SplitWhitespace(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string NameList() {
    std::string out;
    const FpTunableSpec* specs = fpTunableSpecs();
    for (int i = 0; i < kFpTunableCount; ++i) {
        if (i) out += ", ";
        out += specs[i].name;
    }
    return out;
}

}  // namespace

int fpTunableIndexByName(const std::string& name) {
    const std::string key = Lower(Trim(name));
    if (key.empty()) return -1;
    const FpTunableSpec* specs = fpTunableSpecs();
    for (int i = 0; i < kFpTunableCount; ++i) {
        if (Lower(specs[i].name) == key) return i;
    }
    // Second pass: the constexpr's own spelling, so a reader who only knows
    // `kEyeForwardOffsetUnits` from the header comment can still drive it.
    for (int i = 0; i < kFpTunableCount; ++i) {
        if (Lower(specs[i].constantName) == key) return i;
    }
    return -1;
}

bool fpTunableSet(int index, float v) {
    if (index < 0 || index >= kFpTunableCount) return false;
    if (!std::isfinite(v)) return false;
    detail::fpStore().value[index].store(v, std::memory_order_relaxed);
    detail::fpStore().overridden[index].store(true, std::memory_order_relaxed);
    return true;
}

void fpTunableReset(int index) {
    if (index < 0 || index >= kFpTunableCount) return;
    detail::fpStore().value[index].store(fpTunableSpecs()[index].defaultValue,
                                         std::memory_order_relaxed);
    detail::fpStore().overridden[index].store(false, std::memory_order_relaxed);
}

void fpTunableResetAll() {
    for (int i = 0; i < kFpTunableCount; ++i) fpTunableReset(i);
}

bool fpTunableApplyAssignment(const std::string& assignment, std::string& error) {
    const std::string t = Trim(assignment);
    if (t.empty()) { error = "empty assignment"; return false; }

    std::string namePart, valuePart;
    const size_t eq = t.find('=');
    if (eq != std::string::npos) {
        namePart = t.substr(0, eq);
        valuePart = t.substr(eq + 1);
    } else {
        const auto tokens = SplitWhitespace(t);
        if (tokens.size() != 2) {
            error = "expected name=value (got '" + t + "')";
            return false;
        }
        namePart = tokens[0];
        valuePart = tokens[1];
    }

    const int idx = fpTunableIndexByName(namePart);
    if (idx < 0) {
        error = "unknown parameter '" + Trim(namePart) + "'; known: " + NameList();
        return false;
    }
    float v = 0.0f;
    if (!ParseFloat(valuePart, v)) {
        error = "'" + Trim(valuePart) + "' is not a finite number";
        return false;
    }
    fpTunableSet(idx, v);
    return true;
}

bool fpTunableApplyAssignmentList(const std::string& list, std::string& error) {
    std::string cur;
    bool ok = true;
    auto flush = [&]() {
        const std::string item = Trim(cur);
        cur.clear();
        if (item.empty()) return;
        std::string err;
        if (!fpTunableApplyAssignment(item, err)) {
            if (ok) error = err;
            ok = false;
        }
    };
    for (char c : list) {
        if (c == ',' || c == ';') flush();
        else cur += c;
    }
    flush();
    return ok;
}

std::vector<std::string> FormatFirstPersonPoseDump(const FirstPersonPoseSample& s) {
    std::vector<std::string> out;
    auto n = [](float v) { return Num(v); };
    const float feetEqZ = s.nodeOriginEqZ - s.collisionZOffset;

    out.push_back("[FPCAM-DUMP] mode=" + s.modeName +
                  " cameraNodePresent=" + (s.cameraNodePresent ? "1" : "0") +
                  " eyeFromHeadBone=" + (s.eyeFromHeadBone ? "1" : "0"));
    out.push_back("[FPCAM-DUMP] eyeIrrX=" + n(s.eyeIrrX) + " eyeIrrY=" + n(s.eyeIrrY) +
                  " eyeIrrZ=" + n(s.eyeIrrZ) + " units=eq_units frame=irrlicht_world");
    // Irrlicht Y is up; EQ Z is up. Same point, stated in both frames so a
    // reader never has to remember which one a dump is in.
    out.push_back("[FPCAM-DUMP] eyeEqX=" + n(s.eyeIrrX) + " eyeEqY=" + n(s.eyeIrrZ) +
                  " eyeEqZ=" + n(s.eyeIrrY) + " units=eq_units frame=eq_world");
    // feetEqZ is nodeOriginEqZ minus the player's own collisionZOffset --
    // reported READ-ONLY; T310 Leg B owns that number and is expected to
    // change it.
    out.push_back("[FPCAM-DUMP] nodeOriginEqZ=" + n(s.nodeOriginEqZ) +
                  " collisionZOffset=" + n(s.collisionZOffset) +
                  " feetEqZ=" + n(feetEqZ) + " units=eq_units frame=eq_world");
    out.push_back("[FPCAM-DUMP] eyeAboveNodeOrigin=" + n(s.eyeIrrY - s.nodeOriginEqZ) +
                  " eyeAboveGround=" + n(s.eyeIrrY - feetEqZ) +
                  " eyeOffsetIrrX=" + n(s.eyeOffsetIrrX) +
                  " eyeOffsetIrrY=" + n(s.eyeOffsetIrrY) +
                  " eyeOffsetIrrZ=" + n(s.eyeOffsetIrrZ) +
                  " units=eq_units frame=relative_to_player_node");
    out.push_back("[FPCAM-DUMP] headingEqUnits=" + n(s.headingEqUnits) +
                  " headingDeg=" + n(s.headingEqUnits / 512.0f * 360.0f) +
                  " units=eq_heading_0_512 frame=eq_world");
    out.push_back("[FPCAM-DUMP] lookPitchDeg=" + n(s.lookPitchDeg) +
                  " fixedPitchDownDeg=" + n(s.fixedPitchDownDeg) +
                  " totalPitchDeg=" + n(s.totalPitchDeg) +
                  " maxDownPitchDeg=" + n(s.maxDownPitchDeg) +
                  " maxUpPitchDeg=" + n(s.maxUpPitchDeg) +
                  " units=degrees frame=signed_from_horizon_positive_up");
    out.push_back("[FPCAM-DUMP] hfovDeg=" + n(s.hfovDeg) + " vfovDeg=" + n(s.vfovDeg) +
                  " aspect=" + n(s.aspect) + " nearPlane=" + n(s.nearPlane) +
                  " units=degrees_and_eq_units frame=camera");
    out.push_back("[FPCAM-DUMP] targetIrrX=" + n(s.targetIrrX) +
                  " targetIrrY=" + n(s.targetIrrY) + " targetIrrZ=" + n(s.targetIrrZ) +
                  " upX=" + n(s.upX) + " upY=" + n(s.upY) + " upZ=" + n(s.upZ) +
                  " units=eq_units frame=irrlicht_world");

    // The tunables that produced the pose above, on one line, so a sweep
    // driver can record the input and the output from a single dump.
    std::string tunables = "[FPCAM-DUMP] tunables";
    const FpTunableSpec* specs = fpTunableSpecs();
    for (int i = 0; i < kFpTunableCount; ++i) {
        tunables += std::string(" ") + specs[i].name + "=" + n(fpTunableValue(i));
    }
    out.push_back(tunables);
    return out;
}

// ------------------------------------------------------------------------
// The command's text half. `dump` is NOT handled here -- it needs the live
// camera and is answered in renderer_camera.cpp (J11).
// ------------------------------------------------------------------------
std::vector<std::string> FirstPersonTunablesCommandLines(const std::string& args) {
    std::vector<std::string> out;
    const FpTunableSpec* specs = fpTunableSpecs();
    const auto tokens = SplitWhitespace(args);
    const std::string sub = tokens.empty() ? std::string() : Lower(tokens[0]);

    auto describe = [&](int i) {
        return std::string("[FPCAM] ") + specs[i].name + " = " + Num(fpTunableValue(i)) +
               " default=" + Num(specs[i].defaultValue) +
               " overridden=" + (fpTunableIsOverridden(i) ? "yes" : "no") +
               " units=" + specs[i].units +
               " frame=" + specs[i].frame +
               " status=" + (specs[i].live ? "live" : "advisory");
    };

    if (sub.empty() || sub == "list" || sub == "show") {
        out.push_back("[FPCAM] " + std::to_string(kFpTunableCount) +
                      " first-person camera parameters. "
                      "/fpcam set <name> <value> | /fpcam reset [<name>] | "
                      "/fpcam dump | /fpcam help [<name>]");
        for (int i = 0; i < kFpTunableCount; ++i) out.push_back(describe(i));
        return out;
    }

    if (sub == "set") {
        if (tokens.size() < 2) {
            out.push_back("[FPCAM] error: usage /fpcam set <name> <value>; known: " + NameList());
            return out;
        }
        // Accept "set name value", "set name=value" and "set name = value".
        std::string assignment = tokens[1];
        for (size_t i = 2; i < tokens.size(); ++i) assignment += " " + tokens[i];
        // Collapse "name = value" to "name=value" so the shared parser sees one form.
        {
            const auto t2 = SplitWhitespace(assignment);
            if (t2.size() == 3 && t2[1] == "=") assignment = t2[0] + "=" + t2[2];
        }
        const int idx = fpTunableIndexByName(
            assignment.substr(0, assignment.find_first_of("= ")));
        const float before = idx >= 0 ? fpTunableValue(idx) : 0.0f;
        std::string err;
        if (!fpTunableApplyAssignment(assignment, err)) {
            out.push_back("[FPCAM] error: " + err);
            return out;
        }
        out.push_back(std::string("[FPCAM] set ") + specs[idx].name + " = " +
                      Num(fpTunableValue(idx)) + " (was " + Num(before) +
                      ", default " + Num(specs[idx].defaultValue) + ")" +
                      (specs[idx].live ? "" : " -- ADVISORY: this parameter has no "
                                              "runtime consumer and moves no pixels"));
        return out;
    }

    if (sub == "reset") {
        if (tokens.size() < 2) {
            fpTunableResetAll();
            out.push_back("[FPCAM] reset all " + std::to_string(kFpTunableCount) +
                          " parameters to their compiled defaults");
            for (int i = 0; i < kFpTunableCount; ++i) out.push_back(describe(i));
            return out;
        }
        const int idx = fpTunableIndexByName(tokens[1]);
        if (idx < 0) {
            out.push_back("[FPCAM] error: unknown parameter '" + tokens[1] +
                          "'; known: " + NameList());
            return out;
        }
        fpTunableReset(idx);
        out.push_back(std::string("[FPCAM] reset ") + specs[idx].name + " = " +
                      Num(fpTunableValue(idx)) + " (default)");
        return out;
    }

    if (sub == "help") {
        if (tokens.size() >= 2) {
            const int idx = fpTunableIndexByName(tokens[1]);
            if (idx < 0) {
                out.push_back("[FPCAM] error: unknown parameter '" + tokens[1] +
                              "'; known: " + NameList());
                return out;
            }
            out.push_back(std::string("[FPCAM] ") + specs[idx].name + " (defaults from " +
                          specs[idx].constantName + "): " + specs[idx].help);
            out.push_back(describe(idx));
            return out;
        }
        out.push_back("[FPCAM] /fpcam                 list every parameter, value, default, "
                      "units, frame and whether it is overridden");
        out.push_back("[FPCAM] /fpcam set <name> <v>  set one parameter (any finite value; "
                      "deliberately unclamped -- this is a measurement instrument)");
        out.push_back("[FPCAM] /fpcam reset [<name>]  restore the compiled default(s)");
        out.push_back("[FPCAM] /fpcam dump            the RESOLVED pose, one parseable "
                      "key=value line per group, units and frame stated");
        out.push_back("[FPCAM] /fpcam help [<name>]   this, or one parameter's rationale");
        out.push_back("[FPCAM] startup equivalent: --fpcam name=value[,name=value...] "
                      "(repeatable)");
        for (int i = 0; i < kFpTunableCount; ++i) {
            out.push_back(std::string("[FPCAM] ") + specs[i].name + ": " + specs[i].help);
        }
        return out;
    }

    out.push_back("[FPCAM] error: unknown subcommand '" + tokens[0] +
                  "'. Use: (none)|list, set, reset, dump, help.");
    return out;
}

}  // namespace Graphics
}  // namespace EQT
