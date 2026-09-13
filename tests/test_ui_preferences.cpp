/*
 * T432 (board B275): per-character persistence of what the user can change.
 *
 * Before this unit the client persisted NOTHING a player changed -- close the
 * group window, switch to first person, restart, and both were back to their
 * compiled-in defaults. The cases below are the ones the plan names: a toggle
 * survives a camp and a re-login, a fresh character starts from the
 * defaults.ini rule, and the file lands in the cache directory rather than in
 * the checkout.
 *
 * All of it runs with no device, no renderer and no server: Collect/Apply are
 * free functions over the panel structs, and the disk round trip is pointed
 * at a private WILLEQ_CACHE_DIR.
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "client/data_paths.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_preferences.h"

using EQT::Graphics::ApplyUiPreferences;
using EQT::Graphics::BuffBarState;
using EQT::Graphics::CollectUiPreferences;
using EQT::Graphics::CombatAbilityPanelState;
using EQT::Graphics::GroupPanelState;
using EQT::Graphics::InventoryPanelState;
using EQT::Graphics::LoadUiPreferences;
using EQT::Graphics::ParseUiPreferences;
using EQT::Graphics::PetPanelState;
using EQT::Graphics::PopupType;
using EQT::Graphics::SaveUiPreferences;
using EQT::Graphics::SerializeUiPreferences;
using EQT::Graphics::SkillsPopupState;
using EQT::Graphics::SpellbookPopupState;
using EQT::Graphics::UiPreferences;
using EQT::Graphics::UiPreferencesBinding;
using EQT::Graphics::UiPreferencesCacheName;
using EQT::Graphics::UiPreferencesFileName;

namespace {

// The renderer's panel members, standing in for FrameComponent's. The binding
// this builds is field-for-field the one FrameComponent::uiPreferencesBinding()
// builds -- which is the point of the binding existing at all.
struct FakeRendererPanels {
    InventoryPanelState inventory;
    SpellbookPopupState spellbook;
    SkillsPopupState skills;
    GroupPanelState group;
    PetPanelState pet;
    BuffBarState buff;
    CombatAbilityPanelState combatAbility;

    UiPreferencesBinding binding() {
        UiPreferencesBinding b;
        b.inventory = &inventory;
        b.spellbook = &spellbook;
        b.skills = &skills;
        b.group = &group;
        b.pet = &pet;
        b.buff = &buff;
        b.combatAbility = &combatAbility;
        return b;
    }
};

// A private cache directory for the round-trip cases, so they neither read
// nor write the developer's real one and never touch the checkout.
class UiPreferencesDiskTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("willeq_t432_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter_++));
        std::filesystem::create_directories(dir_);
        if (const char* prev = std::getenv("WILLEQ_CACHE_DIR")) savedCacheDir_ = prev;
        ::setenv("WILLEQ_CACHE_DIR", dir_.c_str(), 1);
    }
    void TearDown() override {
        if (savedCacheDir_.empty()) {
            ::unsetenv("WILLEQ_CACHE_DIR");
        } else {
            ::setenv("WILLEQ_CACHE_DIR", savedCacheDir_.c_str(), 1);
        }
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    std::filesystem::path dir_;
    std::string savedCacheDir_;
    static int counter_;
};
int UiPreferencesDiskTest::counter_ = 0;

}  // namespace

// ---------------------------------------------------------------------------
// The file's shape, against the official client's own
// ---------------------------------------------------------------------------

// The oracle's filename is UI_<Char>_<server>.ini -- e.g.
// /mnt/projects/EverQuestP1999/UI_Obian_project1999.ini. Ours is the same.
TEST(UiPreferencesTest, FileNameMatchesTheOfficialShape) {
    EXPECT_EQ(UiPreferencesFileName("Obian", "project1999"),
              "UI_Obian_project1999.ini");
    EXPECT_EQ(UiPreferencesCacheName("Obian", "project1999"),
              "ui/UI_Obian_project1999.ini");
}

// A character name arrives from a server packet and a server name from user
// config: neither may walk out of the cache directory.
TEST(UiPreferencesTest, FileNameCannotEscapeTheCacheDirectory) {
    EXPECT_EQ(UiPreferencesFileName("../../etc/passwd", "p99"),
              "UI_______etc_passwd_p99.ini");
    EXPECT_EQ(UiPreferencesFileName("", ""), "UI_unknown_unknown.ini");
}

// Section and key names are the official client's, so a fable file and an
// official one describe the same windows with the same words.
TEST(UiPreferencesTest, SerializesTheOfficialSectionAndKeyNames) {
    UiPreferences p;
    p.groupWindow = false;
    p.cameraMode = 2;   // T461: CameraMode::Overhead
    const std::string text = SerializeUiPreferences(p);

    EXPECT_NE(text.find("[GroupWindow]\nShow=0\n"), std::string::npos) << text;
    EXPECT_NE(text.find("[PetInfoWindow]\nShow=1\n"), std::string::npos) << text;
    EXPECT_NE(text.find("[BuffWindow]\nShow=1\n"), std::string::npos) << text;
    EXPECT_NE(text.find("[InventoryWindow]\nShow=0\n"), std::string::npos) << text;
    EXPECT_NE(text.find("[SpellBookWnd]\nShow=0\n"), std::string::npos) << text;
    EXPECT_NE(text.find("[SkillsWindow]\nShow=0\n"), std::string::npos) << text;
    EXPECT_NE(text.find("[CombatAbilityWnd]\nShow=0\n"), std::string::npos) << text;
    // T461: `View=` replaced `Mode=`, whose numbering collides with it.
    EXPECT_NE(text.find("[Camera]\nView=2\n"), std::string::npos) << text;
    EXPECT_EQ(text.find("Mode="), std::string::npos) << text;
}

TEST(UiPreferencesTest, RoundTripsThroughItsOwnText) {
    UiPreferences p;
    p.inventoryWindow = true;
    p.spellBookWnd = false;
    p.skillsWindow = false;
    p.groupWindow = false;
    p.petInfoWindow = false;
    p.buffWindow = false;
    p.combatAbilityWnd = true;
    p.cameraMode = 5;   // T461: User Defined Camera 2
    p.user1Camera = EQT::Graphics::CameraRig{12.5f, -20.0f, 44.0f, 7.25f};
    p.user2Camera = EQT::Graphics::CameraRig{300.0f, 45.5f, 150.0f, -3.0f};
    EXPECT_EQ(ParseUiPreferences(SerializeUiPreferences(p)), p);
}

// T368's measured rule: defaults.ini states a default by OMISSION -- it writes
// Show=0 only under a window that ships closed. So an absent key must leave
// the default alone, not read as false.
TEST(UiPreferencesTest, AnAbsentShowKeyKeepsTheDefault) {
    const UiPreferences parsed = ParseUiPreferences("[GroupWindow]\n");
    EXPECT_TRUE(parsed.groupWindow);
    EXPECT_TRUE(parsed.petInfoWindow);
    EXPECT_TRUE(parsed.buffWindow);
    EXPECT_FALSE(parsed.inventoryWindow);
    EXPECT_EQ(parsed.cameraMode, 3);  // T461: Chase, kDefaultCameraMode
}

// An official file carries dozens of position/size/tint keys and CRLF line
// endings. Reading one must pick up its Show= keys and ignore the rest.
TEST(UiPreferencesTest, ReadsAnOfficialFileShapeWithoutChoking) {
    const std::string official =
        "[Main]\r\n"
        "UISkin=Default\r\n"
        "[GroupWindow]\r\n"
        "INIVersion=1\r\n"
        "XPos1280x1024=850\r\n"
        "BGTint.red=255\r\n"
        "Fades=true\r\n"
        "Show=1\r\n"
        "[InventoryWindow]\r\n"
        "Locked=false\r\n"
        "Show=0\r\n"
        "[PetInfoWindow]\r\n"
        "Show=0\r\n"
        "[SomeWindowFableDoesNotHave]\r\n"
        "Show=0\r\n";
    const UiPreferences parsed = ParseUiPreferences(official);
    EXPECT_TRUE(parsed.groupWindow);
    EXPECT_FALSE(parsed.inventoryWindow);
    EXPECT_FALSE(parsed.petInfoWindow);
    EXPECT_TRUE(parsed.buffWindow);  // no section at all -> default
}

// T461: `View=` is the enumerator, 1..6, and anything else keeps the default
// -- a hand-edited value must never become an out-of-range CameraMode cast.
TEST(UiPreferencesTest, CameraViewAcceptsOnlyTheSixOfficialCameras) {
    for (int v = 1; v <= 6; ++v) {
        EXPECT_EQ(ParseUiPreferences("[Camera]\nView=" + std::to_string(v) + "\n").cameraMode, v);
    }
    EXPECT_EQ(ParseUiPreferences("[Camera]\nView=0\n").cameraMode, 3);
    EXPECT_EQ(ParseUiPreferences("[Camera]\nView=7\n").cameraMode, 3);
    EXPECT_EQ(ParseUiPreferences("[Camera]\nView=banana\n").cameraMode, 3);
}

// T461: the MIGRATION. A file T432's writer produced says `Mode=` with
// 0 Free, 1 Follow, 2 FirstPerson. Those numbers mean different cameras in
// the new numbering, so each is mapped explicitly -- never cast.
TEST(UiPreferencesTest, ALegacyCameraModeIsMappedOntoTheOfficialSet) {
    EXPECT_EQ(ParseUiPreferences("[Camera]\nMode=0\n").cameraMode, 3)   // Free -> Chase
        << "Free has no official camera and no body";
    EXPECT_EQ(ParseUiPreferences("[Camera]\nMode=1\n").cameraMode, 3);  // Follow -> Chase
    EXPECT_EQ(ParseUiPreferences("[Camera]\nMode=2\n").cameraMode, 1);  // FirstPerson
    EXPECT_EQ(ParseUiPreferences("[Camera]\nMode=7\n").cameraMode, 3);
    EXPECT_EQ(ParseUiPreferences("[Camera]\nMode=banana\n").cameraMode, 3);
}

// If a file somehow carries both keys, the new one wins, in either order.
TEST(UiPreferencesTest, ViewWinsOverALegacyModeWhereverEitherAppears) {
    EXPECT_EQ(ParseUiPreferences("[Camera]\nView=5\nMode=2\n").cameraMode, 5);
    EXPECT_EQ(ParseUiPreferences("[Camera]\nMode=2\nView=5\n").cameraMode, 5);
}

// T461: the two user cameras are persisted (the official client persists its
// own: eqclient.ini Camera1-* / Camera2-*); unparseable or out-of-range values
// cannot produce a NaN or out-of-range camera.
TEST(UiPreferencesTest, UserCamerasParseAndAreRangeChecked) {
    const UiPreferences p = ParseUiPreferences(
        "[Camera]\n"
        "User1Azimuth=90\nUser1Pitch=500\nUser1Distance=nan\nUser1Height=banana\n"
        "User2Azimuth=-90\nUser2Distance=0.5\n");
    EXPECT_FLOAT_EQ(p.user1Camera.azimuthDeg, 90.0f);
    EXPECT_FLOAT_EQ(p.user1Camera.pitchDeg, EQT::Graphics::kCameraRigMaxPitchDeg);
    EXPECT_FLOAT_EQ(p.user1Camera.distance, EQT::Graphics::kUser1DefaultRig.distance);
    EXPECT_FLOAT_EQ(p.user1Camera.height, EQT::Graphics::kUser1DefaultRig.height);
    EXPECT_FLOAT_EQ(p.user2Camera.azimuthDeg, 270.0f);
    EXPECT_FLOAT_EQ(p.user2Camera.distance, EQT::Graphics::kCameraRigMinDistance);
    EXPECT_FLOAT_EQ(p.user2Camera.height, EQT::Graphics::kUser2DefaultRig.height);
}

// ---------------------------------------------------------------------------
// Collect / Apply against the renderer's own panel state
// ---------------------------------------------------------------------------

// The compiled-in defaults and the panel structs' own defaults have to agree,
// or a fresh character's first save would rewrite state it never touched.
TEST(UiPreferencesTest, DefaultsMatchThePanelStructs) {
    FakeRendererPanels panels;
    // 3 == CameraMode::Chase, PlayerCameraComponent's own default (T461), and
    // the user cameras default to PlayerCameraComponent's own rigs.
    EXPECT_EQ(CollectUiPreferences(panels.binding(), 3), UiPreferences{});
    EXPECT_EQ(UiPreferences{}.cameraMode, static_cast<int>(EQT::Graphics::kDefaultCameraMode));
    EXPECT_EQ(UiPreferences{}.user1Camera, EQT::Graphics::kUser1DefaultRig);
    EXPECT_EQ(UiPreferences{}.user2Camera, EQT::Graphics::kUser2DefaultRig);
}

TEST(UiPreferencesTest, CollectReadsEveryPersistedWindow) {
    FakeRendererPanels panels;
    panels.inventory.activePopup = PopupType::Inventory;
    panels.spellbook.isOpen = true;
    panels.skills.isOpen = true;
    panels.group.isOpen = false;
    panels.pet.isOpen = false;
    panels.buff.isOpen = false;
    panels.combatAbility.isOpen = true;

    const UiPreferences p = CollectUiPreferences(panels.binding(), 2);
    EXPECT_TRUE(p.inventoryWindow);
    EXPECT_TRUE(p.spellBookWnd);
    EXPECT_TRUE(p.skillsWindow);
    EXPECT_FALSE(p.groupWindow);
    EXPECT_FALSE(p.petInfoWindow);
    EXPECT_FALSE(p.buffWindow);
    EXPECT_TRUE(p.combatAbilityWnd);
    EXPECT_EQ(p.cameraMode, 2);
}

TEST(UiPreferencesTest, ApplyWritesEveryPersistedWindow) {
    UiPreferences p;
    p.inventoryWindow = true;
    p.groupWindow = false;
    p.petInfoWindow = false;
    p.buffWindow = false;
    p.combatAbilityWnd = true;

    FakeRendererPanels panels;
    ApplyUiPreferences(p, panels.binding());
    EXPECT_EQ(panels.inventory.activePopup, PopupType::Inventory);
    EXPECT_FALSE(panels.group.isOpen);
    EXPECT_FALSE(panels.pet.isOpen);
    EXPECT_FALSE(panels.buff.isOpen);
    EXPECT_TRUE(panels.combatAbility.isOpen);
}

// The three centre popups are mutually exclusive live; a hand-edited file
// naming two of them open must not paint both.
TEST(UiPreferencesTest, ApplyKeepsTheCentrePopupsMutuallyExclusive) {
    UiPreferences p;
    p.inventoryWindow = true;
    p.spellBookWnd = true;
    p.skillsWindow = true;

    FakeRendererPanels panels;
    ApplyUiPreferences(p, panels.binding());
    EXPECT_TRUE(panels.skills.isOpen);
    EXPECT_FALSE(panels.spellbook.isOpen);
    EXPECT_NE(panels.inventory.activePopup, PopupType::Inventory);
}

// A vendor or bank window open at apply time is not this file's business.
TEST(UiPreferencesTest, ApplyDoesNotClobberAnUnrelatedPopup) {
    FakeRendererPanels panels;
    panels.inventory.activePopup = PopupType::Vendor;
    ApplyUiPreferences(UiPreferences{}, panels.binding());
    EXPECT_EQ(panels.inventory.activePopup, PopupType::Vendor);
}

// A partial binding is legal: nothing crashes on a null member.
TEST(UiPreferencesTest, ANullBindingMemberIsSkipped) {
    UiPreferencesBinding empty;
    UiPreferences p;
    p.groupWindow = false;
    EXPECT_NO_FATAL_FAILURE(ApplyUiPreferences(p, empty));
    EXPECT_EQ(CollectUiPreferences(empty, 3), UiPreferences{});
}

// ---------------------------------------------------------------------------
// The disk round trip -- the plan's three cases
// ---------------------------------------------------------------------------

// Case 1: toggle a window, camp, log in -> the state holds. Two panel sets
// stand in for the two sessions; the only thing they share is the file.
TEST_F(UiPreferencesDiskTest, AToggleSurvivesACampAndARelogin) {
    FakeRendererPanels session1;
    session1.group.isOpen = false;            // the player closes the group window
    session1.inventory.activePopup = PopupType::Inventory;
    ASSERT_TRUE(SaveUiPreferences("Testchar", "willeq",
                                  CollectUiPreferences(session1.binding(), 2)));

    FakeRendererPanels session2;
    ASSERT_TRUE(session2.group.isOpen);       // a fresh session starts open
    UiPreferences loaded = CollectUiPreferences(session2.binding(), 1);
    ASSERT_TRUE(LoadUiPreferences("Testchar", "willeq", loaded));
    ApplyUiPreferences(loaded, session2.binding());

    EXPECT_FALSE(session2.group.isOpen);
    EXPECT_EQ(session2.inventory.activePopup, PopupType::Inventory);
    EXPECT_EQ(loaded.cameraMode, 2);
}

// Case 2: a fresh character has no file and keeps the defaults -- and a miss
// is not an error.
TEST_F(UiPreferencesDiskTest, AFreshCharacterHasNoFileAndKeepsTheDefaults) {
    UiPreferences p;
    EXPECT_FALSE(LoadUiPreferences("Nobody", "willeq", p));
    EXPECT_EQ(p, UiPreferences{});
    EXPECT_FALSE(std::filesystem::exists(dir_ / "ui" / "UI_Nobody_willeq.ini"));
}

// Case 3: the file lands in the cache directory (T402's rule), not in the
// repo -- and the `ui/` subdirectory is created for it.
TEST_F(UiPreferencesDiskTest, TheFileLandsInTheCacheDirectory) {
    ASSERT_TRUE(SaveUiPreferences("Testchar", "willeq", UiPreferences{}));

    const std::filesystem::path expected = dir_ / "ui" / "UI_Testchar_willeq.ini";
    ASSERT_TRUE(std::filesystem::exists(expected)) << expected;
    EXPECT_EQ(eqt::ResolveCachePathForRead("ui/UI_Testchar_willeq.ini"),
              expected.string());

    std::ifstream in(expected);
    std::ostringstream buf;
    buf << in.rdbuf();
    EXPECT_NE(buf.str().find("[GroupWindow]"), std::string::npos);
}

// Each character on each server gets its own file, exactly as the oracle's
// directory of UI_*_*.ini files shows.
TEST_F(UiPreferencesDiskTest, EachCharacterServerPairGetsItsOwnFile) {
    UiPreferences a;
    a.groupWindow = false;
    UiPreferences b;
    b.petInfoWindow = false;
    ASSERT_TRUE(SaveUiPreferences("Alpha", "willeq", a));
    ASSERT_TRUE(SaveUiPreferences("Beta", "willeq", b));
    ASSERT_TRUE(SaveUiPreferences("Alpha", "other", UiPreferences{}));

    UiPreferences readA;
    UiPreferences readB;
    UiPreferences readOther;
    ASSERT_TRUE(LoadUiPreferences("Alpha", "willeq", readA));
    ASSERT_TRUE(LoadUiPreferences("Beta", "willeq", readB));
    ASSERT_TRUE(LoadUiPreferences("Alpha", "other", readOther));
    EXPECT_FALSE(readA.groupWindow);
    EXPECT_TRUE(readB.groupWindow);
    EXPECT_FALSE(readB.petInfoWindow);
    EXPECT_TRUE(readOther.groupWindow);
}

// A save must replace the previous file wholesale, never append to it.
TEST_F(UiPreferencesDiskTest, ASecondSaveReplacesTheFile) {
    UiPreferences first;
    first.buffWindow = false;
    ASSERT_TRUE(SaveUiPreferences("Testchar", "willeq", first));
    ASSERT_TRUE(SaveUiPreferences("Testchar", "willeq", UiPreferences{}));

    UiPreferences read;
    ASSERT_TRUE(LoadUiPreferences("Testchar", "willeq", read));
    EXPECT_TRUE(read.buffWindow);
    EXPECT_EQ(read, UiPreferences{});
}

// T461: a file written before T461 is still readable, means what it meant,
// and is rewritten in the new shape on the next save.
TEST_F(UiPreferencesDiskTest, APreT461FileMigratesOnLoadAndIsRewrittenOnSave) {
    std::filesystem::create_directories(dir_ / "ui");
    {
        std::ofstream out(dir_ / "ui" / "UI_Oldchar_willeq.ini");
        out << "[GroupWindow]\nShow=0\n[Camera]\nMode=2\n";   // T432's writer, first person
    }
    UiPreferences loaded;
    ASSERT_TRUE(LoadUiPreferences("Oldchar", "willeq", loaded));
    EXPECT_EQ(loaded.cameraMode, static_cast<int>(EQT::Graphics::CameraMode::FirstPerson));
    EXPECT_FALSE(loaded.groupWindow);
    EXPECT_EQ(loaded.user1Camera, EQT::Graphics::kUser1DefaultRig);

    loaded.user1Camera.distance = 55.0f;
    ASSERT_TRUE(SaveUiPreferences("Oldchar", "willeq", loaded));
    std::ifstream in(dir_ / "ui" / "UI_Oldchar_willeq.ini");
    std::ostringstream buf;
    buf << in.rdbuf();
    EXPECT_NE(buf.str().find("[Camera]\nView=1\n"), std::string::npos) << buf.str();
    EXPECT_NE(buf.str().find("User1Distance=55.000000\n"), std::string::npos) << buf.str();
    EXPECT_EQ(buf.str().find("Mode="), std::string::npos) << buf.str();

    UiPreferences reread;
    ASSERT_TRUE(LoadUiPreferences("Oldchar", "willeq", reread));
    EXPECT_EQ(reread, loaded);
}
