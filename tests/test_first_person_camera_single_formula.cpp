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
//   2. the two first-person tunables (kFirstPersonCameraStandoffUnits,
//      kFirstPersonPitchDownRad) are referenced from exactly one TU;
//   3. that one body actually applies BOTH of them, and applies the user's
//      mouse-look pitch (playerPitch_) as well -- a helper that quietly
//      stopped using a tunable would otherwise pass parts 1 and 2.

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

// Part 2: the two tunables live in, and are read by, exactly one TU.
TEST(FirstPersonCameraSingleFormula, TunablesAreReferencedFromOneTUOnly) {
    for (const char* tunable : {"kFirstPersonCameraStandoffUnits", "kFirstPersonPitchDownRad"}) {
        std::vector<std::string> files;
        for (const auto& path : graphicsSources()) {
            const auto lines = splitLines(readFile(path));
            for (const auto& line : lines) {
                if (stripLineComment(line).find(tunable) != std::string::npos) {
                    files.push_back(path.filename().string());
                    break;
                }
            }
        }
        ASSERT_EQ(files.size(), 1u)
            << tunable << " is referenced from " << files.size()
            << " translation units; it must be read only by " << kHelperTU << "'s "
            << kHelperName << "(). More than one reader means more than one formula.";
        EXPECT_EQ(files[0], kHelperTU) << tunable << " moved out of " << kHelperTU;
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

    EXPECT_NE(body.find("kFirstPersonCameraStandoffUnits"), std::string::npos)
        << kHelperName << "() no longer applies T76's camera standoff.";
    EXPECT_NE(body.find("kFirstPersonPitchDownRad"), std::string::npos)
        << kHelperName << "() no longer applies T75's down-pitch.";
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
    for (const char* name : {"updateFirstPersonWeapons", "getPlayerHeadBonePosition"}) {
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
            << name << "() was deleted by T295 as unreachable (zero callers). It is back at:"
            << joined
            << "\nIf it is genuinely needed now, it needs a caller AND this expectation updated.";
    }
}
