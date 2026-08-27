// T295: pin the first-person camera to exactly ONE formula.
//
// History this test exists to stop repeating: the FP camera formula has been
// duplicated and re-duplicated across four sites. T75 de-duplicated the
// server-position path (irrlicht_renderer.cpp) and its Review claimed it had
// removed "the last duplicate" -- it had not. A FOURTH copy survived in
// renderer_player_physics.cpp's updatePlayerMovement(), the per-frame LOCAL
// movement path, applying NEITHER T76's kFirstPersonCameraStandoffUnits NOR
// T75's kFirstPersonPitchDownRad. Because that path runs whenever the player
// moves or turns, both shipped fixes were silently discarded in exactly the
// case ("while moving") anyone would test them in.
//
// A compile-time check cannot see this: every copy compiles fine. So, like
// T264's reachability sweep and T223's command catalog, this parses the
// source and fails with a file:line the reader can judge.
//
// The invariant, in three parts:
//   1. IrrlichtRenderer::repositionFirstPersonCamera() is the ONLY function
//      in the renderer that drives camera_ under a CameraMode::FirstPerson
//      condition;
//   2. the first-person tunables are DEFINED in exactly one place
//      (include/client/graphics/first_person_camera.h) and read by exactly
//      one .cpp;
//   3. that one body actually applies the shared formula and the user's
//      mouse-look pitch (playerPitch_) -- a helper that quietly stopped
//      using a tunable would otherwise pass parts 1 and 2;
//   4. T300: the formula's numbers are what they claim to be, checked
//      NUMERICALLY against the official-client measurement (T298 A3), not
//      just textually.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;
const fs::path kGraphicsDir = kRoot / "src" / "client" / "graphics";
// The one sanctioned home of the formula.
const std::string kHelperTU = "irrlicht_renderer.cpp";
const std::string kHelperName = "repositionFirstPersonCamera";

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur += c; }
    }
    out.push_back(cur);
    return out;
}

// Every .cpp/.h under src/client/graphics, recursively (ui/ included --
// character_model_view.cpp drives its own paperdoll camera, which is a
// different camera object entirely and must not be confused for this one).
std::vector<fs::path> graphicsSources() {
    std::vector<fs::path> out;
    for (const auto& e : fs::recursive_directory_iterator(kGraphicsDir)) {
        if (!e.is_regular_file()) continue;
        auto ext = e.path().extension().string();
        if (ext == ".cpp" || ext == ".h") out.push_back(e.path());
    }
    EXPECT_FALSE(out.empty()) << "no graphics sources found under " << kGraphicsDir;
    return out;
}

// Strip // comments so a file that merely DESCRIBES the old formula (this
// unit left such comments behind on purpose) is not counted as a copy.
std::string stripLineComment(const std::string& line) {
    auto pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

}  // namespace

// Part 1: only the helper's TU may touch camera_ inside a first-person branch.
//
// Detection is deliberately coarse: any source file that mentions
// CameraMode::FirstPerson AND writes the camera (camera_->setPosition /
// camera_->setTarget) is a candidate copy. The only file allowed to be both
// is the helper's own TU. renderer_player_physics.cpp used to be the second
// such file; it now calls repositionFirstPersonCamera() instead.
TEST(FirstPersonCameraSingleFormula, OnlyTheHelperTUDrivesTheCameraInFirstPerson) {
    std::vector<std::string> offenders;
    for (const auto& path : graphicsSources()) {
        const std::string text = readFile(path);
        bool mentionsFirstPerson = false;
        bool writesCamera = false;
        int firstWriteLine = 0;
        const auto lines = splitLines(text);
        for (size_t i = 0; i < lines.size(); ++i) {
            const std::string code = stripLineComment(lines[i]);
            if (code.find("CameraMode::FirstPerson") != std::string::npos) {
                mentionsFirstPerson = true;
            }
            if (code.find("camera_->setPosition(") != std::string::npos ||
                code.find("camera_->setTarget(") != std::string::npos) {
                if (!writesCamera) firstWriteLine = static_cast<int>(i) + 1;
                writesCamera = true;
            }
        }
        if (mentionsFirstPerson && writesCamera && path.filename().string() != kHelperTU) {
            offenders.push_back(path.string() + ":" + std::to_string(firstWriteLine));
        }
    }

    std::string joined;
    for (const auto& o : offenders) joined += "\n    " + o;
    EXPECT_TRUE(offenders.empty())
        << "A second first-person camera formula has appeared. These files both branch on\n"
           "CameraMode::FirstPerson and write camera_ directly; only " << kHelperTU << "'s\n"
        << kHelperName << "() may do that. Call the helper instead of re-deriving the\n"
           "camera -- T75 and T76's fixes were lost for months to exactly this:" << joined;
}

// Part 2: the tunables are read by exactly one .cpp (the header that DEFINES
// them is expected to mention them and is excluded).
TEST(FirstPersonCameraSingleFormula, TunablesAreReferencedFromOneTUOnly) {
    const std::string kTunableHome = "first_person_camera.h";
    // T300: the DOWN-PITCH FLOOR is deliberately read by a second TU. The
    // clamp is applied both where the mouse-look value is produced
    // (renderer_player_physics.cpp -- the live path, so the stored value never
    // leaves range and the control never develops a dead zone) and where the
    // camera consumes it (the formula itself, which is the guarantee). That is
    // one limit applied twice, not two formulas; every other tunable stays
    // single-reader.
    const std::string kFloorSecondReader = "renderer_player_physics.cpp";
    for (const char* tunable : {"kFirstPersonPitchDownDegrees",
                                "kFirstPersonHorizontalFovDegrees",
                                "firstPersonMaxDownPitchDegrees",
                                "firstPersonMinLookPitchDegrees",
                                "firstPersonVerticalFovRadians",
                                "computeFirstPersonCamera"}) {
        std::vector<std::string> files;
        for (const auto& path : graphicsSources()) {
            if (path.filename().string() == kTunableHome) continue;
            const auto lines = splitLines(readFile(path));
            for (const auto& line : lines) {
                if (stripLineComment(line).find(tunable) != std::string::npos) {
                    files.push_back(path.filename().string());
                    break;
                }
            }
        }
        // At most one reader. Zero is legitimate for a constant consumed
        // only inside its own header (kFirstPersonHorizontalFovDegrees is
        // read by firstPersonVerticalFovRadians(), which irrlicht_renderer.cpp
        // calls); two or more means two formulas.
        if (std::string(tunable) == "firstPersonMinLookPitchDegrees") {
            for (const auto& f : files) {
                EXPECT_TRUE(f == kHelperTU || f == kFloorSecondReader)
                    << tunable << " is read by " << f << "; only " << kHelperTU << " and "
                    << kFloorSecondReader << " may apply the down-pitch floor.";
            }
            ASSERT_LE(files.size(), 2u) << tunable << " is read by " << files.size()
                << " translation units; at most the camera formula's TU and the pitch "
                   "accumulator's TU may.";
            continue;
        }
        ASSERT_LE(files.size(), 1u)
            << tunable << " is referenced from " << files.size()
            << " translation units under " << kGraphicsDir << "; only " << kHelperTU
            << "'s " << kHelperName << "() may read it. More than one reader means "
               "more than one formula.";
        if (!files.empty()) {
            EXPECT_EQ(files[0], kHelperTU) << tunable << " moved out of " << kHelperTU;
        }
    }
}

// Part 3: the single body still applies both tunables and the mouse-look
// pitch. Parts 1 and 2 would still pass if the helper stopped using them.
TEST(FirstPersonCameraSingleFormula, TheOneBodyAppliesBothTunablesAndMouseLookPitch) {
    const auto lines = splitLines(readFile(kGraphicsDir / kHelperTU));

    // Find the helper body by brace depth from its definition line.
    size_t start = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find("void IrrlichtRenderer::" + kHelperName + "()") != std::string::npos) {
            start = i;
            break;
        }
    }
    ASSERT_LT(start, lines.size()) << kHelperName << "() definition not found in " << kHelperTU;

    std::string body;
    int depth = 0;
    bool opened = false;
    for (size_t i = start; i < lines.size(); ++i) {
        const std::string code = stripLineComment(lines[i]);
        for (char c : code) {
            if (c == '{') { depth++; opened = true; }
            else if (c == '}') { depth--; }
        }
        body += code + "\n";
        if (opened && depth == 0) break;
    }
    ASSERT_TRUE(opened) << "could not delimit " << kHelperName << "()'s body";

    EXPECT_NE(body.find("computeFirstPersonCamera("), std::string::npos)
        << kHelperName << "() no longer calls the shared placement formula in\n"
           "include/client/graphics/first_person_camera.h. Re-deriving the camera here is\n"
           "how T75's and T76's fixes were lost for months.";
    EXPECT_NE(body.find("applyCameraProjectionForMode("), std::string::npos)
        << kHelperName << "() no longer applies the first-person projection. The NEAR CLIP\n"
           "PLANE is the only thing that hides the player's body in first person (T300);\n"
           "without it the player's own model fills the frame.";
    EXPECT_EQ(body.find("kFirstPersonCameraStandoffUnits"), std::string::npos)
        << "T76's backward standoff is back. T298 measured the official client's forward\n"
           "offset at 0.00 +/- 0.4; the standoff existed to clear in-world arm geometry\n"
           "that T300 stopped drawing as a separate pass.";
    EXPECT_NE(body.find("playerPitch_"), std::string::npos)
        << kHelperName << "() no longer applies the user's mouse-look pitch. Before T295 the\n"
           "only code that read playerPitch_ was the duplicate formula in\n"
           "renderer_player_physics.cpp; dropping it here silently disables vertical mouse-look.";
    EXPECT_NE(body.find("camera_->setPosition("), std::string::npos);
    EXPECT_NE(body.find("camera_->setTarget("), std::string::npos);
}

// Guard the deletions this unit made: reachability pattern #9's last two
// members. Both had zero callers; if a declaration comes back without a
// caller, it is the same defect again.
TEST(FirstPersonCameraSingleFormula, DeadFirstPersonHelpersStayDeleted) {
    for (const char* name : {"updateFirstPersonWeapons", "getPlayerHeadBonePosition",
                             // T300: the retired viewmodel pass and T74's
                             // arm-bone geometry filter. First person draws
                             // the ORDINARY player model and hides it with a
                             // near clip plane (T299 measured that mechanism
                             // in two client versions). A filter or a second
                             // pass coming back is the same wrong mechanism
                             // returning, not a refinement.
                             "renderFirstPersonViewmodel", "ViewmodelParts",
                             "setDeferredToViewmodelPass", "computeViewmodelPlacement",
                             "buildArmBoneIndices", "applyFirstPersonIndexFilter",
                             "hasValidFirstPersonGeometry", "getArmBoneIndices"}) {
        std::vector<std::string> hits;
        for (const auto& path : graphicsSources()) {
            const auto lines = splitLines(readFile(path));
            for (size_t i = 0; i < lines.size(); ++i) {
                if (stripLineComment(lines[i]).find(name) != std::string::npos) {
                    hits.push_back(path.string() + ":" + std::to_string(i + 1));
                }
            }
        }
        std::string joined;
        for (const auto& h : hits) joined += "\n    " + h;
        EXPECT_TRUE(hits.empty())
            << name << "() was deleted (T295 unreachable sweep, or T300's retirement of the\n"
               "viewmodel pass and the arm-bone filter). It is back at:"
            << joined
            << "\nIf it is genuinely needed now, it needs a caller AND this expectation updated.";
    }
}

// ---------------------------------------------------------------------------
// Part 4 (T300): the numbers, checked numerically against the measurement.
//
// The source-parsing tests above prove there is ONE formula. These prove that
// the one formula is the RIGHT one, by calling it. Reference: T298 Leg A's
// measurement of the official Titanium client (ring-ladder + marker-pair fits,
// 2000-sample Monte Carlo error bars) --
//   forward 0.00 +/- 0.4, lateral 0.00 +/- 0.05, eye 6.75 +/- 0.20 above the
//   server z, pitch 3.65 +/- 0.15 deg DOWN, hFOV 62.8 +/- 0.3 deg.
//
// WHAT THESE CANNOT DO: they do not rasterize. They pin the camera's numbers.
// Whether the body is actually clipped and a swing actually visible is the
// live video gate's job (T300 Leg C), and nothing headless can substitute.
// ---------------------------------------------------------------------------

#include "client/graphics/first_person_camera.h"

using EQT::Graphics::computeFirstPersonCamera;
using EQT::Graphics::firstPersonVerticalFovRadians;

namespace {
constexpr float kDeg = 3.14159265358979323846f / 180.0f;
// The camera's vertical FOV at 4:3 -- the clamp is derived from it, so every
// call has to pass the same one production uses.
const float kVFov43 = EQT::Graphics::firstPersonVerticalFovRadians(4.0f / 3.0f);
}

TEST(FirstPersonCameraFormula, NoStandoffInAnyDirection) {
    // T76 shipped a 0.3-unit BACKWARD standoff. T298 measured 0.00 +/- 0.4
    // forward and 0.00 +/- 0.05 lateral: the camera is at the player's own XY.
    for (float heading : {0.0f, 64.0f, 128.0f, 190.0f, 300.0f, 511.0f}) {
        const auto p = computeFirstPersonCamera(
            /*playerX*/ 21.9086f, /*playerY*/ -77.9232f, /*playerZ*/ 3.0f,
            /*eyeAboveNodeOrigin*/ 3.6527f, /*eyeAdjust*/ 0.0f,
            heading, /*lookPitchDegrees*/ 0.0f, kVFov43);
        EXPECT_FLOAT_EQ(p.posX, 21.9086f) << "heading " << heading;
        EXPECT_FLOAT_EQ(p.posZ, -77.9232f) << "heading " << heading;
    }
}

TEST(FirstPersonCameraFormula, EyeSitsAtTheNodeOriginPlusEyeHeightNotPlusBoth) {
    // The frame trap this unit exists to close: playerZ_ is the player NODE
    // ORIGIN (== the server's Z, the model CENTRE -- the Human male mesh spans
    // Y=[-3.05,+3.65] about it), and eyeAboveNodeOrigin is measured in that
    // same frame. 3.65 above the origin IS the official 6.75 above the feet;
    // adding 6.75 here would put the eye 3.05 units above the crown.
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, /*playerZ*/ 3.0f,
                                            /*eye*/ 3.6527f, 0.0f, 0.0f, 0.0f, kVFov43);
    EXPECT_NEAR(p.posY, 6.6527f, 1e-4f);
    // The eye is 6.70 above the mesh's own feet (origin - 3.05), which is the
    // official 6.75 +/- 0.20 measurement.
    const float feetZ = 3.0f - 3.05f;
    EXPECT_NEAR(p.posY - feetZ, 6.75f, 0.20f);
}

TEST(FirstPersonCameraFormula, FixedPitchIsTheMeasured3Point65DegreesDown) {
    // T75 shipped 12 degrees down (chosen to tip the frustum onto in-world
    // arm geometry that is no longer drawn as a separate pass).
    const float kTargetDistance = 100.0f;
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            /*heading*/ 0.0f, /*lookPitch*/ 0.0f,
                                            kVFov43, kTargetDistance);
    // heading 0 -> forward is +Z in Irrlicht space.
    const float horizontal = std::sqrt(p.targetX * p.targetX + p.targetZ * p.targetZ);
    const float pitchDeg = std::atan2(p.targetY - p.posY, horizontal) / kDeg;
    EXPECT_NEAR(pitchDeg, -3.65f, 0.15f)
        << "the fixed down-bias must match T298's measured 3.65 +/- 0.15 degrees";
}

TEST(FirstPersonCameraFormula, MouseLookPitchAddsToTheFixedBias) {
    for (float look : {-30.0f, -5.0f, 0.0f, 5.0f, 30.0f}) {
        const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                0.0f, look, kVFov43, 100.0f);
        const float horizontal = std::sqrt(p.targetX * p.targetX + p.targetZ * p.targetZ);
        const float pitchDeg = std::atan2(p.targetY - p.posY, horizontal) / kDeg;
        EXPECT_NEAR(pitchDeg, look - 3.65f, 0.01f) << "mouse-look pitch " << look;
    }
}

TEST(FirstPersonCameraFormula, HorizontalFovMatchesTheOfficialMeasurement) {
    // fable never called setFOV(), so the camera ran at Irrlicht's default
    // PI/2.5 vertical == ~88 degrees horizontal at 4:3. The official client
    // measured 62.8 +/- 0.3 horizontal (49.3 vertical at 4:3).
    const float fovY43 = firstPersonVerticalFovRadians(4.0f / 3.0f);
    EXPECT_NEAR(fovY43 / kDeg, 49.3f, 0.3f);
    // Round-trip: whatever the aspect, the HORIZONTAL angle stays 62.8.
    for (float aspect : {4.0f / 3.0f, 16.0f / 9.0f, 1.0f}) {
        const float fovY = firstPersonVerticalFovRadians(aspect);
        const float hDeg = 2.0f * std::atan(std::tan(fovY * 0.5f) * aspect) / kDeg;
        EXPECT_NEAR(hDeg, 62.8f, 0.3f) << "aspect " << aspect;
    }
    // A zero/garbage aspect must not produce a NaN projection.
    EXPECT_GT(firstPersonVerticalFovRadians(0.0f), 0.0f);
}

TEST(FirstPersonCameraFormula, DownPitchClampIsDerivedFromTheFov) {
    // T300 post-Leg-A: the hiding mechanism is a clamped down-pitch, NOT a
    // near plane. Leg A measured the official client clipping nothing, and its
    // size sweep killed both the fixed and the size-derived plane at once (5x
    // body, zero player geometry, ground 34 units below still drawn).
    //
    // The clamp is 90 degrees minus half the vertical FOV -- the angle at which
    // the frustum's lower edge reaches straight down. 90 - vFOV/2 is an
    // INFERENCE consistent with Leg A's measurement (roughly 65 degrees,
    // bracket 55-75), not itself a measured angle; a dedicated measurement of
    // the clamp is the outstanding follow-up.
    using EQT::Graphics::firstPersonMaxDownPitchDegrees;
    using EQT::Graphics::firstPersonVerticalFovRadians;

    const float clamp43 = firstPersonMaxDownPitchDegrees(kVFov43);
    EXPECT_NEAR(clamp43, 90.0f - (kVFov43 / kDeg) * 0.5f, 1e-3f);
    // Inside Leg A's measured bracket.
    EXPECT_GT(clamp43, 55.0f);
    EXPECT_LT(clamp43, 75.0f);
    // And consistent with Leg A's own vFOV figure (49.1 at 1024x768).
    EXPECT_NEAR(clamp43, 65.45f, 0.5f);

    // Tracks the lens: a wider vertical FOV tightens the clamp, because the
    // frustum's lower edge reaches the nadir sooner.
    EXPECT_LT(firstPersonMaxDownPitchDegrees(60.0f * kDeg),
              firstPersonMaxDownPitchDegrees(40.0f * kDeg));
    // At every aspect ratio the relationship holds exactly.
    for (float aspect : {4.0f / 3.0f, 16.0f / 9.0f, 1.0f}) {
        const float v = firstPersonVerticalFovRadians(aspect);
        EXPECT_NEAR(firstPersonMaxDownPitchDegrees(v) + (v / kDeg) * 0.5f, 90.0f, 1e-3f)
            << "aspect " << aspect << ": clamp + half-FOV must be exactly the nadir";
    }
}

TEST(FirstPersonCameraFormula, DownPitchClampIsIndependentOfModelSize) {
    // THE property that makes this mechanism right where a near plane was
    // wrong, and Will's actual requirement: the hiding must be immune to
    // race size and to shrink/grow. The clamp takes NO size term -- its only
    // input is the FOV -- so a 5x body cannot move it. Asserted here rather
    // than argued: the same lens gives the same clamp, whatever the model.
    using EQT::Graphics::firstPersonMaxDownPitchDegrees;
    const float clamp = firstPersonMaxDownPitchDegrees(kVFov43);
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(firstPersonMaxDownPitchDegrees(kVFov43), clamp);
    }
    // The camera formula honours it no matter what pitch is fed in, which is
    // the guarantee at the point of use.
    for (float look : {-10.0f, -60.0f, -89.0f, -1000.0f}) {
        const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                0.0f, look, kVFov43, 100.0f);
        const float horizontal = std::sqrt(p.targetX * p.targetX + p.targetZ * p.targetZ);
        const float pitchDeg = std::atan2(p.targetY - p.posY, horizontal) / kDeg;
        EXPECT_GE(pitchDeg, -clamp - 0.01f)
            << "look pitch " << look << " produced a total down-pitch of " << pitchDeg
            << ", past the clamp. The clamp is the ONLY thing hiding the player's body; a "
               "caller must not be able to get around it.";
    }
}

TEST(FirstPersonCameraFormula, StoredMouseLookFloorMatchesTheTotalClamp) {
    // The accumulator (renderer_player_physics.cpp) clamps the STORED
    // mouse-look value; the camera formula clamps the TOTAL, which includes
    // the fixed kFirstPersonPitchDownDegrees bias. They must be the same limit
    // expressed in the two frames, or the control develops a dead zone that
    // has to be dragged back through before the view moves.
    using EQT::Graphics::firstPersonMinLookPitchDegrees;
    using EQT::Graphics::firstPersonMaxDownPitchDegrees;
    const float minLook = firstPersonMinLookPitchDegrees(kVFov43);
    EXPECT_NEAR(minLook - EQT::Graphics::kFirstPersonPitchDownDegrees,
                -firstPersonMaxDownPitchDegrees(kVFov43), 1e-3f);

    // Feeding the accumulator's floor into the formula lands exactly on the
    // clamp, not short of it and not past it.
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            0.0f, minLook, kVFov43, 100.0f);
    const float horizontal = std::sqrt(p.targetX * p.targetX + p.targetZ * p.targetZ);
    const float pitchDeg = std::atan2(p.targetY - p.posY, horizontal) / kDeg;
    EXPECT_NEAR(pitchDeg, -firstPersonMaxDownPitchDegrees(kVFov43), 0.01f);
}

TEST(FirstPersonCameraFormula, UpSideIsNotPartOfTheHiding) {
    // Leg A measured only the down side. The up side needs no hiding clamp:
    // the body hangs BELOW the eye, so looking up moves it further out of
    // frame. The +/-89 that remains is a gimbal guard for the look-at target,
    // and it is kept.
    EXPECT_FLOAT_EQ(EQT::Graphics::kFirstPersonMaxUpPitchDegrees, 89.0f);
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            0.0f, 1000.0f, kVFov43, 100.0f);
    const float horizontal = std::sqrt(p.targetX * p.targetX + p.targetZ * p.targetZ);
    const float pitchDeg = std::atan2(p.targetY - p.posY, horizontal) / kDeg;
    EXPECT_NEAR(pitchDeg, 89.0f, 0.01f);
    EXPECT_GT(horizontal, 0.0f) << "a fully-up look must not degenerate the look-at target";
}

TEST(FirstPersonCameraFormula, FirstPersonClipsNoDifferentlyFromThirdPerson) {
    // Leg A: the official client clips nothing -- world and NPC geometry draws
    // at point-blank range, so its near plane is under 1 unit. The near-plane
    // hiding that briefly lived here is refuted; first person must use the
    // world's plane like every other mode.
    EXPECT_FLOAT_EQ(EQT::Graphics::kWorldNearPlaneUnits, 1.0f);
}
