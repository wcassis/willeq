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
#include <type_traits>

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
const std::string kHelperTU = "renderer_camera.cpp";  // J11: was irrlicht_renderer.cpp
// L05 (Batch L): the TU kept its basename and moved into player/. Every
// comparison below is on the BASENAME, so only this one path join changed.
const fs::path kHelperTUPath = kGraphicsDir / "player" / kHelperTU;
const std::string kHelperName = "repositionFirstPersonCamera";
// L05 (Batch L, correction 21): a text-matching guard must learn the component
// spelling or it silently stops guarding. The formula's body moved from
// class IrrlichtRenderer to class PlayerCameraComponent, and the mechanism's
// core substitution rewrote camera_ to ctx_.camera. The INVARIANT is unchanged
// -- exactly one first-person camera formula, applying both tunables and the
// user's mouse-look pitch -- so the assertions accept either spelling and
// require EXACTLY ONE of each pair to be present. Accepting both spellings at
// once would let a body that mixes them through.
const std::string kOwnerSpellings[2] = {"IrrlichtRenderer", "PlayerCameraComponent"};
const std::string kCameraSpellings[2] = {"camera_->", "ctx_.camera->"};

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
            // L05: match the component spelling too. A re-duplicated formula
            // written as ctx_.camera->setPosition(...) inside a Batch L
            // component was invisible to this detector before.
            if (code.find("camera_->setPosition(") != std::string::npos ||
                code.find("camera_->setTarget(") != std::string::npos ||
                code.find("ctx_.camera->setPosition(") != std::string::npos ||
                code.find("ctx_.camera->setTarget(") != std::string::npos) {
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
    const auto lines = splitLines(readFile(kHelperTUPath));

    // Find the helper body by brace depth from its definition line. L05: the
    // body may be owned by IrrlichtRenderer or by PlayerCameraComponent, but
    // EXACTLY ONE definition must exist -- two would be the duplication this
    // whole test exists to stop.
    size_t start = lines.size();
    int definitionsFound = 0;
    for (const auto& owner : kOwnerSpellings) {
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].find("void " + owner + "::" + kHelperName + "()") != std::string::npos) {
                if (definitionsFound == 0) start = i;
                ++definitionsFound;
            }
        }
    }
    ASSERT_EQ(definitionsFound, 1)
        << kHelperName << "() must be defined exactly once in " << kHelperTU
        << ", on IrrlichtRenderer or on PlayerCameraComponent; found "
        << definitionsFound << ".";
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
    // L05: the camera is reached as camera_-> on IrrlichtRenderer and as
    // ctx_.camera-> from a component. Exactly one spelling per call -- a body
    // that mixes them is reaching the camera two ways and is a defect.
    // (Note "ctx_.camera->setPosition(" contains "camera_->setPosition(" is
    // FALSE -- ctx_.camera has no trailing underscore -- so these counts are
    // genuinely disjoint.)
    for (const char* call : {"setPosition(", "setTarget("}) {
        int spellingsPresent = 0;
        for (const auto& spelling : kCameraSpellings) {
            if (body.find(spelling + std::string(call)) != std::string::npos) ++spellingsPresent;
        }
        EXPECT_EQ(spellingsPresent, 1)
            << kHelperName << "() must drive the camera with exactly one spelling of "
            << call << " (camera_-> or ctx_.camera->); found " << spellingsPresent << ".";
    }
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
// T301 REMOVED the vertical FOV from the clamp and from
// computeFirstPersonCamera(); it is now only the projection's own number, so
// no test here needs it any more.
}

namespace {
// THE TOTAL DOWN-PITCH, RECOVERED UNAMBIGUOUSLY.
//
// Every earlier test here measured pitch as atan2(dy, |horizontal|). That is
// only valid while the pitch stays inside +/-90: past the nadir the horizontal
// component points BEHIND the heading, |horizontal| throws the sign away, and
// the angle folds back about -90 -- so -92.65 reads as -87.35 and a clamp
// comparison silently inverts. T301 Leg A's measured clamp is 94.9, past the
// nadir, so the fold is now reachable in normal play.
//
// Projecting onto the heading keeps the sign and gives the true angle in
// (-180, 180].
float signedTotalPitchDegrees(const EQT::Graphics::FirstPersonCameraPlacement& p,
                              float headingUnits) {
    constexpr float kPiLocal = 3.14159265358979323846f;
    const float headingRad = headingUnits / 512.0f * 2.0f * kPiLocal;
    const float fwdX = std::sin(headingRad);
    const float fwdZ = std::cos(headingRad);
    const float dx = p.targetX - p.posX;
    const float dy = p.targetY - p.posY;
    const float dz = p.targetZ - p.posZ;
    const float alongHeading = dx * fwdX + dz * fwdZ;   // signed, not a magnitude
    return std::atan2(dy, alongHeading) * 180.0f / kPiLocal;
}
}  // namespace

TEST(FirstPersonCameraFormula, ForwardOffsetIsInRangeAndHasNoLateralComponent) {
    // SUPERSEDES T300's "NoStandoffInAnyDirection".
    //
    // T301 Leg A measured +0.9 +/- 0.25 units FORWARD of the model's vertical
    // axis; T298 measured 0.00 +/- 0.4. *** THE TWO MEASUREMENTS DISAGREE AT
    // ABOUT 1.9 SIGMA and the rotation-parallax test that would settle it was
    // never run. *** Leg A's instruction was to treat it as the range 0 to
    // 1.0 and pick the value that keeps the player's face out of frame, and
    // T301 Leg B therefore took 1.0, the far end.
    //
    // T309 set it to 0.0. That rationale is obsolete: the face is kept out of
    // frame by the measured DRAW RULE (first_person_body_draw.h), not by
    // geometry, and a large offset now actively contradicts T307's filmed
    // frames -- the body fills the view during a swing at the clamp, and the
    // held weapon's grip sits only +0.678 forward of the axis, so an eye at
    // +0.83 puts the weapon BEHIND the camera during the swing the official
    // client draws it in. The full argument is on the constant itself.
    //
    // What this test guards is the two things neither measurement disputes:
    // the offset must stay inside Leg A's 0..1.0 range, and it must be purely
    // along the look ray with NO lateral component (T298's lateral
    // 0.00 +/- 0.05; Leg A did not contradict it). T76's 0.3 BACKWARD standoff
    // is wrong by sign and stays excluded by the range floor.
    using EQT::Graphics::kEyeForwardOffsetUnits;
    constexpr float kPiLocal = 3.14159265358979323846f;

    EXPECT_GE(kEyeForwardOffsetUnits, 0.0f) << "Leg A's range floor";
    EXPECT_LE(kEyeForwardOffsetUnits, 1.0f) << "Leg A's range ceiling";

    for (float heading : {0.0f, 64.0f, 128.0f, 190.0f, 300.0f, 511.0f}) {
        const auto p = computeFirstPersonCamera(
            /*playerX*/ 21.9086f, /*playerY*/ -77.9232f, /*playerZ*/ 3.0f,
            /*eyeAboveNodeOrigin*/ 3.0f, /*eyeAdjust*/ 0.0f,
            heading, /*lookPitchDegrees*/ 0.0f);

        const float headingRad = heading / 512.0f * 2.0f * kPiLocal;
        const float fwdX = std::sin(headingRad);
        const float fwdZ = std::cos(headingRad);

        const float dx = p.posX - 21.9086f;
        const float dz = p.posZ - (-77.9232f);

        // Entirely along the heading...
        EXPECT_NEAR(dx * fwdX + dz * fwdZ, kEyeForwardOffsetUnits, 1e-3f)
            << "heading " << heading << ": forward component";
        // ...and nothing across it.
        EXPECT_NEAR(dx * fwdZ - dz * fwdX, 0.0f, 1e-3f)
            << "heading " << heading << ": lateral component must stay zero";
    }
}

TEST(FirstPersonCameraFormula, EyeSitsAtTheNodeOriginPlusEyeHeightNotPlusBoth) {
    // The frame trap this unit exists to close: playerZ_ is the player NODE
    // ORIGIN (== the server's Z, the model CENTRE -- the Human male mesh spans
    // Y=[-3.05,+3.65] about it), and eyeAboveNodeOrigin is measured in that
    // same frame. 3.65 above the origin IS the official 6.75 above the feet;
    // adding 6.75 here would put the eye 3.05 units above the crown.
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, /*playerZ*/ 3.0f,
                                            /*eye*/ 3.6527f, 0.0f, 0.0f, 0.0f);
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
                                            kTargetDistance);
    // heading 0 -> forward is +Z in Irrlicht space.
    const float horizontal = std::sqrt((p.targetX - p.posX) * (p.targetX - p.posX) + (p.targetZ - p.posZ) * (p.targetZ - p.posZ));
    const float pitchDeg = std::atan2(p.targetY - p.posY, horizontal) / kDeg;
    EXPECT_NEAR(pitchDeg, -3.65f, 0.15f)
        << "the fixed down-bias must match T298's measured 3.65 +/- 0.15 degrees";
}

TEST(FirstPersonCameraFormula, MouseLookPitchAddsToTheFixedBias) {
    for (float look : {-30.0f, -5.0f, 0.0f, 5.0f, 30.0f}) {
        const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                0.0f, look, 100.0f);
        const float horizontal = std::sqrt((p.targetX - p.posX) * (p.targetX - p.posX) + (p.targetZ - p.posZ) * (p.targetZ - p.posZ));
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

TEST(FirstPersonCameraFormula, DownPitchClampIsTheMeasuredAngleAndReachesPastTheNadir) {
    // T301 Leg A MEASURED the official max down-pitch at 94.9 +/- 0.5 degrees
    // below horizontal, and verified it is genuinely clamped there (4 further
    // seconds of input changed 0.08% of pixels). Two things are refuted at
    // once: T300's 55-75 bracket, and its `90 - vFOV/2` derivation.
    //
    // *** THE HEADLINE IS THAT IT IS PAST 90. *** A clamp that reaches beyond
    // straight down cannot be what hides the player's body, which is why this
    // test asserts the inequality explicitly rather than just a value: if
    // someone "fixes" this back under 90 they have reintroduced the refuted
    // theory, and this is where that stops.
    using EQT::Graphics::firstPersonMaxDownPitchDegrees;
    using EQT::Graphics::kFirstPersonMaxDownPitchDegrees;

    EXPECT_FLOAT_EQ(firstPersonMaxDownPitchDegrees(), kFirstPersonMaxDownPitchDegrees);
    EXPECT_NEAR(kFirstPersonMaxDownPitchDegrees, 94.9f, 0.5f)
        << "Leg A's measured value, with its own error bar";
    EXPECT_GT(kFirstPersonMaxDownPitchDegrees, 90.0f)
        << "THE CLAMP MUST REACH PAST THE NADIR. Leg A measured it there, and "
           "its control run (an identical model teleported onto the actor's own "
           "coordinates fills 17.8% of the frame at this clamp) proves the "
           "frustum CONTAINS the body. The clamp is not the hiding mechanism; "
           "pulling it back under 90 does not make it one.";

    // THE REGRESSION GUARD against the FOV derivation: a measured constant
    // does not track the lens. 90 - vFOV/2 would have moved by 10 degrees
    // between these two aspects.
    const float wide = EQT::Graphics::firstPersonVerticalFovRadians(1.0f);
    const float narrow = EQT::Graphics::firstPersonVerticalFovRadians(16.0f / 9.0f);
    EXPECT_NE(wide, narrow) << "the two aspects must differ for this to prove anything";
    EXPECT_FLOAT_EQ(firstPersonMaxDownPitchDegrees(), kFirstPersonMaxDownPitchDegrees);

    // And the signature carries no FOV, so it cannot be re-coupled by accident.
    static_assert(std::is_same<decltype(firstPersonMaxDownPitchDegrees()), float>::value,
                  "firstPersonMaxDownPitchDegrees() takes no arguments by design");
}

TEST(FirstPersonCameraFormula, UpVectorStaysDefinedThroughAndPastTheNadir) {
    // WITHOUT THIS THE MEASURED CLAMP IS UNUSABLE. A look-at camera on
    // Irrlicht's default (0,1,0) up vector degenerates as the look ray nears
    // straight down -- ray parallel to up, view basis undefined -- and flips
    // as it crosses. Leg A's 94.9 crosses.
    constexpr float kDegLocal = 3.14159265358979323846f / 180.0f;
    for (float heading : {0.0f, 64.0f, 128.0f, 190.0f, 300.0f, 511.0f}) {
        for (float look : {80.0f, 10.0f, 0.0f, -45.0f, -86.35f, -91.25f, -1000.0f}) {
            const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 3.0f, 0.0f,
                                                    heading, look, 100.0f);
            const float upLen = std::sqrt(p.upX * p.upX + p.upY * p.upY + p.upZ * p.upZ);
            EXPECT_NEAR(upLen, 1.0f, 1e-4f)
                << "heading " << heading << " look " << look;

            // Up must never be parallel to the look ray, at any pitch -- that
            // is the degeneracy itself.
            const float fx = p.targetX - p.posX;
            const float fy = p.targetY - p.posY;
            const float fz = p.targetZ - p.posZ;
            const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
            ASSERT_GT(flen, 0.0f);
            const float dot = (fx * p.upX + fy * p.upY + fz * p.upZ) / flen;
            EXPECT_NEAR(dot, 0.0f, 1e-3f)
                << "up is not perpendicular to forward at heading " << heading
                << " look " << look;
        }
    }

    // Level pitch must reproduce the ordinary world up exactly, or every
    // level-view golden moves for no reason.
    const auto level = computeFirstPersonCamera(
        0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f,
        EQT::Graphics::kFirstPersonPitchDownDegrees, 100.0f);
    EXPECT_NEAR(level.upX, 0.0f, 1e-6f);
    EXPECT_NEAR(level.upY, 1.0f, 1e-6f);
    EXPECT_NEAR(level.upZ, 0.0f, 1e-6f);

    // At the clamp the view has gone OVER the top: up tilts below horizontal.
    // This is the observable signature of a past-nadir clamp, and it is what
    // would silently vanish if the clamp were pulled back under 90.
    const auto clamped = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 3.0f, 0.0f,
                                                  0.0f, -1000.0f, 100.0f);
    EXPECT_LT(clamped.upY, 0.0f)
        << "at a past-nadir clamp the up vector must have tipped below "
           "horizontal; upY = " << clamped.upY;
    const float clampedPitch =
        std::atan2(clamped.targetY - clamped.posY,
                   std::sqrt((clamped.targetX - clamped.posX) *
                                 (clamped.targetX - clamped.posX) +
                             (clamped.targetZ - clamped.posZ) *
                                 (clamped.targetZ - clamped.posZ))) /
        kDegLocal;
    // Past the nadir the horizontal component has flipped to point BEHIND the
    // heading, so the measured pitch reads as its reflection about -90.
    EXPECT_NEAR(clampedPitch,
                -(180.0f - EQT::Graphics::kFirstPersonMaxDownPitchDegrees), 0.05f);
}

TEST(FirstPersonCameraFormula, DownPitchClampIsIndependentOfModelSize) {
    // The clamp takes NO size term -- its only input is a measured constant --
    // so a 5x body cannot move it, and neither can a Troll.
    //
    // NOTE WHAT THIS TEST NO LONGER CLAIMS. Until T301 its failure message
    // read "the clamp is the ONLY thing hiding the player's body". Leg A
    // refuted that: the clamp sits at 94.9 degrees, PAST the nadir, and Leg A's
    // control (an identical model teleported onto the actor's own coordinates)
    // fills 17.8% of the frame at exactly that clamp. The frustum contains the
    // body; the client does not draw it. What is asserted here is only that
    // the clamp is honoured at the point of use and is size-independent.
    using EQT::Graphics::firstPersonMaxDownPitchDegrees;
    const float clamp = firstPersonMaxDownPitchDegrees();
    for (int i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(firstPersonMaxDownPitchDegrees(), clamp);
    }
    for (float look : {-10.0f, -60.0f, -89.0f, -120.0f, -1000.0f}) {
        const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                0.0f, look, 100.0f);
        const float pitchDeg = signedTotalPitchDegrees(p, 0.0f);
        EXPECT_GE(pitchDeg, -clamp - 0.01f)
            << "look pitch " << look << " produced a total down-pitch of "
            << pitchDeg << ", past the clamp -- a caller must not be able to "
                           "get around the limit applied at the point of use.";
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
    const float minLook = firstPersonMinLookPitchDegrees();
    EXPECT_NEAR(minLook - EQT::Graphics::kFirstPersonPitchDownDegrees,
                -firstPersonMaxDownPitchDegrees(), 1e-3f);

    // Feeding the accumulator's floor into the formula lands exactly on the
    // clamp, not short of it and not past it.
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            0.0f, minLook, 100.0f);
    EXPECT_NEAR(signedTotalPitchDegrees(p, 0.0f),
                -firstPersonMaxDownPitchDegrees(), 0.01f);
}

TEST(FirstPersonCameraFormula, UpSideIsNotPartOfTheHiding) {
    // Leg A measured only the down side. The up side needs no hiding clamp:
    // the body hangs BELOW the eye, so looking up moves it further out of
    // frame. The +/-89 that remains is a gimbal guard for the look-at target,
    // and it is kept.
    EXPECT_FLOAT_EQ(EQT::Graphics::kFirstPersonMaxUpPitchDegrees, 89.0f);
    const auto p = computeFirstPersonCamera(0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            0.0f, 1000.0f, 100.0f);
    const float horizontal = std::sqrt((p.targetX - p.posX) * (p.targetX - p.posX) + (p.targetZ - p.posZ) * (p.targetZ - p.posZ));
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
