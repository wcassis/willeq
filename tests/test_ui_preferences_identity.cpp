/**
 * T467 (board B560) -- a character identity change must not hand the new
 * character the previous one's persisted UI state.
 *
 * MEASURED before the fix: FrameComponent::syncUiPreferences() started the load
 * for a new identity from collect() -- whatever the client held -- so every key
 * the new character's `UI_<char>_<server>.ini` did not state (all of them, when
 * it had no file) kept the PREVIOUS character's value: window open states, the
 * Options window and its three sliders, and both user-camera rigs. T464 had
 * already reset the camera MODE there. The rule (docs/decisions.md 2026-09-07,
 * T432/T368) is that an absent key keeps the DEFAULT.
 *
 * REACHABILITY, recorded rather than assumed: today one process logs in one
 * character (ApplicationConfig::character, passed once to the single EverQuest
 * built in Application::initEverQuestClient()), and /camp ends the process
 * (PlayerComponent::ZoneProcessLogoutReply() -> RequestQuit()). So these cases
 * drive the identity change the way Application would if a character switch
 * existed; they pin FrameComponent's contract, not a live path.
 *
 * The first identity of a run is deliberately NOT reset: before it the client
 * may already hold real values (the audio manager publishes its volumes into
 * the Options sliders on GameEventType::VolumesChanged), and T432's baseline
 * keeps them. The last case pins that.
 *
 * Same seam as tests/test_camera_zone_in_preference.cpp: the bare, never
 * Init()'d IrrlichtRenderer and a private WILLEQ_CACHE_DIR.
 */
#include <gtest/gtest.h>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// T486 (item 7): AThirdCharacterDoesNotInheritTheChaseEyeScale plants the
// cached Chase eye height, which only a built player model writes in
// production (chaseEyeAboveFeet()); there is no model on this bare renderer.
// The define must precede every client include: frame_component.h reaches the
// camera component's header first.
#define private public
#define protected public
#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/player/player_camera_component.h"
#undef private
#undef protected
#include "client/graphics/player/camera_rig.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_preferences.h"

namespace fs = std::filesystem;
using namespace EQT::Graphics;

namespace {

class UiPreferencesIdentityTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("willeq_t467_" + std::to_string(::getpid()) + "_" + std::to_string(counter_++));
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

    // Application::startLoadingThread() + a zone-graphics loader + the first
    // frame's sync, as tests/test_camera_zone_in_preference.cpp drives it.
    void ZoneIn(const std::string& character) {
        renderer_.setUiPreferencesIdentity(character, "srv");
        renderer_.placeCameraForZoneIn(0.0f, 0.0f, 0.0f, 0.0f);
        renderer_.Frame().syncUiPreferences();
    }

    // Everything the client holds that the file persists.
    UiPreferences Held() {
        const PlayerCameraComponent& camera = renderer_.PlayerCamera();
        return CollectUiPreferences(renderer_.Frame().uiPreferencesBinding(),
                                    static_cast<int>(camera.cameraMode()),
                                    *camera.cameraRigFor(CameraMode::User1),
                                    *camera.cameraRigFor(CameraMode::User2));
    }

    // Move every persisted field away from its default, through the same
    // members a click or a slash command writes.
    void ChangeEverything() {
        UiPreferencesBinding b = renderer_.Frame().uiPreferencesBinding();
        b.skills->isOpen = true;
        b.group->isOpen = false;
        b.pet->isOpen = false;
        b.buff->isOpen = false;
        b.combatAbility->isOpen = true;
        b.options->isOpen = true;
        b.options->musicVolume = 3;
        b.options->soundVolume = 4;
        b.options->clipPlane = 17;
        CameraRig user1 = kUser1DefaultRig;
        user1.azimuthDeg += 40.0f;
        user1.distance += 12.0f;
        CameraRig user2 = kUser2DefaultRig;
        user2.azimuthDeg -= 25.0f;
        user2.height += 4.0f;
        renderer_.PlayerCamera().setUserCameraRigs(user1, user2);
        renderer_.PlayerCamera().setCameraMode(CameraMode::User2);
    }

    static std::string Describe(const UiPreferences& p) { return SerializeUiPreferences(p); }

    IrrlichtRenderer renderer_;
    fs::path dir_;
    std::string saved_;
    static int counter_;
};
int UiPreferencesIdentityTest::counter_ = 0;

}  // namespace

// Pre-fix RED: Beta held Alpha's windows, sliders and rigs.
TEST_F(UiPreferencesIdentityTest, ASecondCharacterWithNoFileGetsTheDefaults) {
    ZoneIn("Alpha");
    ChangeEverything();
    const UiPreferences alpha = Held();
    ASSERT_NE(alpha, UiPreferences{}) << "the fixture changed nothing";
    ASSERT_FALSE(alpha.user1Camera == kUser1DefaultRig) << "the fixture's rig was clamped back to the default";
    ASSERT_FALSE(alpha.user2Camera == kUser2DefaultRig) << "the fixture's rig was clamped back to the default";
    renderer_.Frame().syncUiPreferences();  // Alpha's save-on-difference
    ASSERT_TRUE(fs::exists(IniFor("Alpha")));

    ZoneIn("Beta");
    EXPECT_EQ(Held(), UiPreferences{})
        << "Beta, with no file, holds:\n" << Describe(Held())
        << "\nexpected the defaults:\n" << Describe(UiPreferences{});

    renderer_.Frame().syncUiPreferences();
    EXPECT_FALSE(fs::exists(IniFor("Beta"))) << "a fresh second character wrote a file";
}

// Pre-fix RED: the keys Beta's file omits kept Alpha's values.
TEST_F(UiPreferencesIdentityTest, AKeyTheSecondCharactersFileOmitsKeepsItsDefault) {
    ZoneIn("Alpha");
    ChangeEverything();
    renderer_.Frame().syncUiPreferences();

    std::ofstream(IniFor("Beta")) << "[InventoryWindow]\nShow=1\n[Options]\nMusic=9\n";
    ZoneIn("Beta");

    UiPreferences expected;
    expected.inventoryWindow = true;
    expected.musicVolume = 9;
    EXPECT_EQ(Held(), expected) << "Beta holds:\n" << Describe(Held())
                                << "\nexpected:\n" << Describe(expected);
}

// The reset must not touch the first character's file: coming back loads it.
TEST_F(UiPreferencesIdentityTest, ReturningToTheFirstCharacterRestoresItsFile) {
    ZoneIn("Alpha");
    ChangeEverything();
    renderer_.Frame().syncUiPreferences();
    const UiPreferences alpha = Held();

    ZoneIn("Beta");
    ZoneIn("Alpha");
    EXPECT_EQ(Held(), alpha) << "Alpha holds:\n" << Describe(Held())
                             << "\nexpected:\n" << Describe(alpha);
}

// A re-zone is not an identity change: session changes survive it.
TEST_F(UiPreferencesIdentityTest, AZoneLoadForTheSameCharacterKeepsItsSessionState) {
    ZoneIn("Alpha");
    ChangeEverything();
    const UiPreferences alpha = Held();
    ZoneIn("Alpha");
    EXPECT_EQ(Held(), alpha) << Describe(Held());
}

// Unchanged by T467: the FIRST identity of a run starts from what the client
// already holds, so a volume the audio manager published before zone-in is
// not overwritten by a default when the character has no file.
TEST_F(UiPreferencesIdentityTest, TheFirstCharacterOfARunKeepsWhatTheClientAlreadyHolds) {
    UiPreferencesBinding b = renderer_.Frame().uiPreferencesBinding();
    b.options->musicVolume = 7;   // as the VolumesChanged arm writes it
    b.options->soundVolume = 11;

    ZoneIn("Alpha");
    EXPECT_EQ(Held().musicVolume, 7);
    EXPECT_EQ(Held().soundVolume, 11);
    renderer_.Frame().syncUiPreferences();
    EXPECT_FALSE(fs::exists(IniFor("Alpha"))) << "the first character wrote a file for a value it did not change";
}

// T486 (2026-09-14 audit, item 7): the Chase race scale is a per-character
// value too. chaseEyeAboveFeet_ caches the player's head-bone eye height
// (T474) and is refreshed only while the player model is STANDING with a
// head-derived eye; a new character's first frames -- and every frame until
// its model is built and standing -- placed Chase with the PREVIOUS
// character's eye. It now goes back to the fitted character's value with the
// rest of the T467 identity reset.
TEST_F(UiPreferencesIdentityTest, AThirdCharacterDoesNotInheritTheChaseEyeScale) {
    ZoneIn("Alpha");
    // What a built Human male standing in the world would have written.
    renderer_.PlayerCamera().chaseEyeAboveFeet_ = kChaseFitEyeAboveFeet * 1.5f;
    ASSERT_NE(renderer_.PlayerCamera().chaseEyeAboveFeet_, kChaseFitEyeAboveFeet);

    ZoneIn("Beta");
    EXPECT_FLOAT_EQ(renderer_.PlayerCamera().chaseEyeAboveFeet_, kChaseFitEyeAboveFeet)
        << "Beta's Chase camera is scaled by Alpha's eye height";
}

// Zoning as the same character keeps the measured eye (the reset is for a
// change of character, not of zone).
TEST_F(UiPreferencesIdentityTest, AZoneLoadForTheSameCharacterKeepsItsChaseEyeScale) {
    ZoneIn("Alpha");
    renderer_.PlayerCamera().chaseEyeAboveFeet_ = kChaseFitEyeAboveFeet * 1.5f;
    ZoneIn("Alpha");
    EXPECT_FLOAT_EQ(renderer_.PlayerCamera().chaseEyeAboveFeet_, kChaseFitEyeAboveFeet * 1.5f);
}
