// B994 (#314) -- the 3D target ring (Options > Display "Show 3D Target Ring").
//
// What is pinned:
//   * the ring's numbers are TargetIndicator.ini's (four circles of 64 points,
//     the con tints, the fade distances, FloorOffset) -- compared against the
//     shipped file when it is present;
//   * the geometry: four concentric circles centred on the target's feet,
//     alpha 0 / full / full / 0, sized by the target's footprint (R1), lifted
//     by FloorOffset, and it FOLLOWS the target;
//   * the draw decision: option off -> no ring; no target -> no ring; a target
//     whose node is hidden -> no ring;
//   * the Options row: WIRED, persisted under its key, default off, and
//     `/option targetring` sets it;
//   * real pixels: a headless GLES2 frame through TargetRing::drawGeometry()
//     -- tinted pixels where the ring is, none with the option off.

#include <gtest/gtest.h>

#include "client/con_tier.h"
#include "client/graphics/overlays/target_ring.h"
#include "client/graphics/overlays/target_ring_draw.h"
#include "client/graphics/ui/options_classification.h"
#include "client/graphics/ui/options_command.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_preferences.h"

#include <irrlicht.h>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace EQT::Graphics;
namespace OL = EQT::Graphics::OptionsLayout;
namespace TR = EQT::Graphics::TargetRing;

namespace {

int controlIndex(const char* xmlItem) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        if (std::string(OL::kControls[i].xmlItem) == xmlItem) return i;
    }
    return -1;
}

// TargetIndicator.ini -> section -> key -> value.
std::map<std::string, std::map<std::string, std::string>> readIni(const std::string& path) {
    std::map<std::string, std::map<std::string, std::string>> out;
    std::ifstream in(path);
    std::string line, section;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\t' || line.back() == ' '))
            line.pop_back();
        if (line.size() > 2 && line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        out[section][line.substr(0, eq)] = line.substr(eq + 1);
    }
    return out;
}

}  // namespace

TEST(TargetRing, TheNumbersAreTargetIndicatorIni) {
    const char* kSections[] = {"Trivial", "VeryEasy", "Easy", "FairMatch", "Difficult", "Deadly"};
    const auto ini = readIni("/mnt/projects/EverQuestP1999/uifiles/default/TargetIndicator.ini");
    if (ini.empty()) GTEST_SKIP() << "the oracle's TargetIndicator.ini is not on this host";
    EXPECT_EQ(std::stoi(ini.at("TargetIndicator").at("PointCount")), TR::kPointCount);
    EXPECT_EQ(std::stoi(ini.at("TargetIndicator").at("Additive")), 1);
    for (int s = 0; s < static_cast<int>(TR::Section::Count); ++s) {
        const auto& sec = ini.at(kSections[s]);
        const TR::Style& st = TR::styleFor(static_cast<TR::Section>(s));
        EXPECT_EQ(st.red, std::stoi(sec.at("Red"))) << kSections[s];
        EXPECT_EQ(st.green, std::stoi(sec.at("Green"))) << kSections[s];
        EXPECT_EQ(st.blue, std::stoi(sec.at("Blue"))) << kSections[s];
        EXPECT_EQ(st.alpha, std::stoi(sec.at("Alpha"))) << kSections[s];
        EXPECT_FLOAT_EQ(st.opaqueStart, std::stof(sec.at("OpaqueStart"))) << kSections[s];
        EXPECT_FLOAT_EQ(st.opaqueEnd, std::stof(sec.at("OpaqueEnd"))) << kSections[s];
        EXPECT_FLOAT_EQ(st.fadeStart, std::stof(sec.at("FadeStart"))) << kSections[s];
        EXPECT_FLOAT_EQ(st.fadeEnd, std::stof(sec.at("FadeEnd"))) << kSections[s];
        EXPECT_FLOAT_EQ(st.floorOffset, std::stof(sec.at("FloorOffset"))) << kSections[s];
    }
}

TEST(TargetRing, ConTiersPickTheirSection) {
    EXPECT_EQ(TR::sectionForCon(eqt::ConTier::Green), TR::Section::Trivial);
    EXPECT_EQ(TR::sectionForCon(eqt::ConTier::LightBlue), TR::Section::VeryEasy);
    EXPECT_EQ(TR::sectionForCon(eqt::ConTier::White), TR::Section::FairMatch);
    EXPECT_EQ(TR::sectionForCon(eqt::ConTier::Yellow), TR::Section::Difficult);
    EXPECT_EQ(TR::sectionForCon(eqt::ConTier::Red), TR::Section::Deadly);
}

TEST(TargetRing, FourConcentricCirclesAtTheFeetThatFollowTheTarget) {
    const TR::Style& st = TR::styleFor(TR::Section::Deadly);
    TR::Geometry g;
    TR::build(st, 100.0f, 7.0f, -40.0f, 3.0f, g);
    ASSERT_EQ(g.vertices.size(), static_cast<size_t>(TR::kCircles * TR::kPointCount));
    ASSERT_EQ(g.indices.size(), static_cast<size_t>(3 * TR::kPointCount * 6));
    const float k = TR::unitScale(3.0f);
    const float radii[4] = {st.opaqueStart * k, st.opaqueEnd * k, st.fadeStart * k, st.fadeEnd * k};
    for (int c = 0; c < TR::kCircles; ++c) {
        for (int i = 0; i < TR::kPointCount; ++i) {
            const auto& v = g.vertices[c * TR::kPointCount + i];
            EXPECT_NEAR(std::hypot(v.Pos.X - 100.0f, v.Pos.Z + 40.0f), radii[c], 1e-3f);
            EXPECT_FLOAT_EQ(v.Pos.Y, 7.0f + st.floorOffset);
        }
    }
    // R1: FadeStart is the footprint edge, FadeEnd twice it.
    EXPECT_NEAR(radii[2], 3.0f, 1e-5f);
    EXPECT_NEAR(radii[3], 6.0f, 1e-5f);
    // The edges are dark (alpha 0), the two middle circles carry the tint.
    // (The vertex colour is stored B<->R swapped for the GLES2 driver's raw
    // byte read -- target_ring.cpp -- so DRAWN red is the SColor blue slot.)
    EXPECT_EQ(g.vertices[0].Color.getBlue(), 0u);
    EXPECT_EQ(g.vertices[3 * TR::kPointCount].Color.getBlue(), 0u);
    EXPECT_GT(g.vertices[TR::kPointCount].Color.getBlue(), 50u);
    EXPECT_EQ(g.vertices[TR::kPointCount].Color.getGreen(), 0u) << "Deadly is red";
    EXPECT_EQ(g.vertices[TR::kPointCount].Color.getRed(), 0u) << "Deadly is red";

    // The target moves: the ring is rebuilt about the new feet.
    TR::build(st, -5.0f, 0.0f, 9.0f, 3.0f, g);
    float cx = 0, cz = 0;
    for (const auto& v : g.vertices) { cx += v.Pos.X; cz += v.Pos.Z; }
    EXPECT_NEAR(cx / g.vertices.size(), -5.0f, 1e-3f);
    EXPECT_NEAR(cz / g.vertices.size(), 9.0f, 1e-3f);
    EXPECT_LT(TR::geometryBytes(g), 16u * 1024u) << "the ui ledger row's whole cost";
}

TEST(TargetRing, OnlyAnOptionAndAVisibleTargetDrawIt) {
    EXPECT_TRUE(TR::shouldDraw(true, 12, true, true));
    EXPECT_FALSE(TR::shouldDraw(false, 12, true, true)) << "option off";
    EXPECT_FALSE(TR::shouldDraw(true, 0, true, true)) << "no target";
    EXPECT_FALSE(TR::shouldDraw(true, 12, false, true)) << "target not rendered";
    EXPECT_FALSE(TR::shouldDraw(true, 12, true, false)) << "target node hidden";
}

TEST(TargetRing, TheCheckboxIsWiredPersistedAndDefaultsOff) {
    const int i = controlIndex("ODP_ShowTargetRingCheckbox");
    ASSERT_GE(i, 0);
    EXPECT_EQ(OL::kControls[i].setting, OL::Setting::TargetRing);
    EXPECT_EQ(OL::findControlClass("ODP_ShowTargetRingCheckbox")->disposition, OL::Disposition::Wired);

    OptionsPanelState s;
    EXPECT_EQ(optionsSettingStep(s, OL::Setting::TargetRing), 0) << "R5: default off";
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::Tab::Display);
    UILayout layout;
    layout.computeLayout(800, 600);
    const auto r = OL::controlRect(layout, i);
    StaticUIInput input;
    OL::Setting changed = OL::Setting::None;
    input.processOptionsInput((r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
                              (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2, true, layout, s, changed);
    EXPECT_EQ(changed, OL::Setting::TargetRing);
    EXPECT_EQ(optionsSettingStep(s, OL::Setting::TargetRing), 1);

    UiPreferencesBinding b;
    b.options = &s;
    const std::string text = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(text.find("ShowTargetRing=1\n"), std::string::npos);
    OptionsPanelState back;
    UiPreferencesBinding b2;
    b2.options = &back;
    ApplyUiPreferences(ParseUiPreferences(text), b2);
    EXPECT_EQ(optionsSettingStep(back, OL::Setting::TargetRing), 1);

    auto res = RunOptionsCommand(back, "/option", "targetring off");
    ASSERT_EQ(res.changed.size(), 1u);
    EXPECT_EQ(optionsSettingStep(back, OL::Setting::TargetRing), 0);
}

// ---------------------------------------------------------------------------
// Pixels: the client's own draw into a headless GLES2 frame.
// ---------------------------------------------------------------------------

namespace {

int ringPixels(irr::video::IVideoDriver* driver, bool optionOn, uint8_t& maxRed, uint8_t& maxGreen) {
    irr::core::matrix4 view, proj;
    view.buildCameraLookAtMatrixLH(irr::core::vector3df(0, 10, -10), irr::core::vector3df(0, 0, 0),
                                   irr::core::vector3df(0, 1, 0));
    proj.buildProjectionMatrixPerspectiveFovLH(1.0f, 4.0f / 3.0f, 1.0f, 200.0f);
    TR::Geometry g;
    for (int pass = 0; pass < 3; ++pass) {
        driver->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver->setTransform(irr::video::ETS_VIEW, view);
        driver->setTransform(irr::video::ETS_PROJECTION, proj);
        if (TR::shouldDraw(optionOn, 7, true, true)) {
            TR::build(TR::styleFor(TR::Section::Deadly), 0.0f, 0.0f, 0.0f, 3.0f, g);
            TR::drawGeometry(driver, g);
        }
        driver->endScene();
    }
    irr::video::IImage* img = driver->createScreenShot();
    if (!img) return -1;
    int n = 0;
    maxRed = maxGreen = 0;
    const auto d = img->getDimension();
    for (irr::u32 y = 0; y < d.Height; ++y) {
        for (irr::u32 x = 0; x < d.Width; ++x) {
            const auto p = img->getPixel(x, y);
            if (p.getRed() > 0 || p.getGreen() > 0 || p.getBlue() > 0) ++n;
            maxRed = std::max<uint8_t>(maxRed, static_cast<uint8_t>(p.getRed()));
            maxGreen = std::max<uint8_t>(maxGreen, static_cast<uint8_t>(p.getGreen()));
        }
    }
    img->drop();
    return n;
}

}  // namespace

TEST(TargetRingPixels, TheOptionTogglesARedRingOnTheFloor) {
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(320, 240);
    params.Fullscreen = true;
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) GTEST_SKIP() << "no EDT_OGLES2 device";
    uint8_t r = 0, g = 0;
    // This pixel test is also the net for the B<->R vertex-colour swap.
    const int on = ringPixels(device->getVideoDriver(), true, r, g);
    EXPECT_GT(on, 500) << "the ring covers the floor under the target";
    EXPECT_GT(r, 40) << "Deadly's tint";
    EXPECT_EQ(g, 0) << "and only red";
    const int off = ringPixels(device->getVideoDriver(), false, r, g);
    EXPECT_EQ(off, 0) << "option off: nothing drawn";
    device->drop();
}
