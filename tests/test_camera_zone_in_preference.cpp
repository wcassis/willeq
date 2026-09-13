/**
 * T464 (board B532) -- a zone load must not overwrite the player's saved
 * camera.
 *
 * MEASURED before the fix: both of Application's zone-graphics loaders
 * (application_zone_loading.cpp, step "7. Camera") called
 * setCameraMode(Chase) on EVERY zone load, while
 * FrameComponent::syncUiPreferences() reads the ini only once per
 * character/server identity. The first zone-in was right (the load runs on the
 * frame after the reset); every later zone put the player back in Chase, and
 * the next save-on-difference wrote `View=3` over the saved choice.
 *
 * The zone-in camera step is now one entry point,
 * IrrlichtRenderer::placeCameraForZoneIn(), which both Application loaders
 * call; these cases drive it exactly as Application does (identity, zone-in
 * camera, then the first frame's sync), on the bare never-Init()'d renderer
 * T461's camera tests use, with a private WILLEQ_CACHE_DIR.
 */
#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#include "client/graphics/ui/ui_preferences.h"

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

class CameraZoneInPreferenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("willeq_t464_" + std::to_string(::getpid()) + "_" + std::to_string(counter_++));
        fs::create_directories(dir_ / "ui");
        if (const char* prev = std::getenv("WILLEQ_CACHE_DIR")) saved_ = prev;
        ::setenv("WILLEQ_CACHE_DIR", dir_.c_str(), 1);
    }
    void TearDown() override {
        if (saved_.empty()) {
            ::unsetenv("WILLEQ_CACHE_DIR");
        } else {
            ::setenv("WILLEQ_CACHE_DIR", saved_.c_str(), 1);
        }
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    fs::path IniFor(const std::string& character) const {
        return dir_ / UiPreferencesCacheName(character, "srv");
    }
    void WriteIni(const std::string& character, const std::string& body) {
        std::ofstream(IniFor(character)) << body;
    }

    // Application::startLoadingThread() + one zone-graphics loader + the first
    // frame after the load (renderer_frame.cpp's syncUiPreferences() tick).
    void ZoneIn(const std::string& character, float x, float y, float z, float heading512) {
        renderer_.setUiPreferencesIdentity(character, "srv");
        renderer_.placeCameraForZoneIn(x, y, z, heading512);
        renderer_.Frame().syncUiPreferences();
    }

    IrrlichtRenderer renderer_;
    fs::path dir_;
    std::string saved_;
    static int counter_;
};
int CameraZoneInPreferenceTest::counter_ = 0;

}  // namespace

TEST_F(CameraZoneInPreferenceTest, ASavedUserCameraSurvivesASecondZoneLoad) {
    WriteIni("Tester", "[Camera]\nView=4\n");

    ZoneIn("Tester", 0.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_EQ(renderer_.getCameraMode(), CameraMode::User1) << "the first zone-in load";

    ZoneIn("Tester", 100.0f, -50.0f, 3.0f, 128.0f);
    EXPECT_EQ(renderer_.getCameraMode(), CameraMode::User1)
        << "the second zone load reset the player's saved camera";
    const std::string ini = ReadFile(IniFor("Tester"));
    EXPECT_NE(ini.find("View=4"), std::string::npos) << ini;
    EXPECT_EQ(ini.find("View=3"), std::string::npos) << "Chase was saved over the choice:\n" << ini;
}

TEST_F(CameraZoneInPreferenceTest, FirstPersonSurvivesASecondZoneLoad) {
    WriteIni("Tester", "[Camera]\nView=1\n");

    ZoneIn("Tester", 0.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_EQ(renderer_.getCameraMode(), CameraMode::FirstPerson);

    ZoneIn("Tester", 100.0f, -50.0f, 3.0f, 128.0f);
    EXPECT_EQ(renderer_.getCameraMode(), CameraMode::FirstPerson);
    EXPECT_NE(ReadFile(IniFor("Tester")).find("View=1"), std::string::npos);
}

// A choice made in-session (not from the file) must survive a zone too.
TEST_F(CameraZoneInPreferenceTest, AChoiceMadeThisSessionSurvivesAZoneLoad) {
    ZoneIn("Tester", 0.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_EQ(renderer_.getCameraMode(), CameraMode::Chase);
    renderer_.PlayerCamera().setCameraMode(CameraMode::User2);
    renderer_.Frame().syncUiPreferences();  // the save-on-difference
    ASSERT_NE(ReadFile(IniFor("Tester")).find("View=5"), std::string::npos);

    ZoneIn("Tester", 100.0f, -50.0f, 3.0f, 128.0f);
    EXPECT_EQ(renderer_.getCameraMode(), CameraMode::User2);
    EXPECT_NE(ReadFile(IniFor("Tester")).find("View=5"), std::string::npos);
}

// Unchanged by T464: a character with no saved view gets Chase (the login
// camera is ORACLE-OWED, B530) and writes no file -- including when a
// different character held another camera earlier in the same process.
TEST_F(CameraZoneInPreferenceTest, ACharacterWithNoSavedViewGetsChase) {
    WriteIni("First", "[Camera]\nView=4\n");
    ZoneIn("First", 0.0f, 0.0f, 0.0f, 0.0f);
    ASSERT_EQ(renderer_.getCameraMode(), CameraMode::User1);

    ZoneIn("Second", 0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(renderer_.getCameraMode(), CameraMode::Chase);
    renderer_.Frame().syncUiPreferences();
    EXPECT_FALSE(fs::exists(IniFor("Second"))) << "a fresh character wrote a file";
}

// Both Application loaders go through the one entry point, and no zone-load
// site names a camera mode.
TEST(CameraZoneInSourceTest, BothZoneLoadersUseTheOneEntryPointAndNameNoMode) {
    const std::string src =
        ReadFile(fs::path(WILLEQ_REPO_ROOT) / "src/client/application_zone_loading.cpp");
    ASSERT_FALSE(src.empty());
    size_t calls = 0;
    for (size_t p = src.find("placeCameraForZoneIn("); p != std::string::npos;
         p = src.find("placeCameraForZoneIn(", p + 1)) {
        ++calls;
    }
    EXPECT_EQ(calls, 2u) << "loadZoneGraphicsOnThread() and loadZoneGraphics()";
    EXPECT_EQ(src.find("setCameraMode("), std::string::npos)
        << "a zone load must not choose the player's camera";
}
