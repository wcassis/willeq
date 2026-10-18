// T434 -- the Options window.
//
// Before this unit the `O` key reached a bare `break;` in
// renderer_frame_input.cpp ("WindowManager removed -- options window not
// available") and a `PopupType` enumerator named Options had zero uses in the
// tree: a bound key advertising the one window that is the home of every
// user-changeable setting. T432's Review recorded `[OptionsWindow]` as the one
// section it could not persist because there was nothing to persist. T439
// DELETED that enumerator (board B434) -- see test_options_merchant_residue.
//
// Everything here is state + geometry + the ini format, so no Irrlicht device
// is needed -- the same shape as test_tradeskill_combat_panels.cpp.
//
// What is pinned:
//   * the window and every control sit at the ORACLE's offsets, quoted in
//     include/client/graphics/ui/options_layout.h from
//     /mnt/projects/EverQuestP1999/uifiles/default/EQUI_OptionsWindow.xml
//   * the seven tabs tile the strip and each one is reachable
//   * a control with NO backing setting is not hit-testable (the T363 rule)
//   * each of the three WIRED controls moves its setting
//   * the whole thing round-trips through T432's one persistence writer,
//     under the oracle's own `[Options]` key names

#include <gtest/gtest.h>

#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/audio/volume_steps.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_preferences.h"
#include "client/input/hotkey_manager.h"

#include <irrlicht.h>
#include <algorithm>
#include <set>
#include <string>

using namespace EQT::Graphics;
namespace OL = EQT::Graphics::OptionsLayout;

namespace {

UILayout layoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

UILayout layout800() { return layoutAt(800, 600); }

std::string rectStr(const irr::core::rect<irr::s32>& r) {
    return "{" + std::to_string(r.UpperLeftCorner.X) + "," +
           std::to_string(r.UpperLeftCorner.Y) + "," +
           std::to_string(r.LowerRightCorner.X) + "," +
           std::to_string(r.LowerRightCorner.Y) + "}";
}

// The index of a control by its XML `item` name -- so a test names the
// oracle's element, not a position in a table that may grow.
int controlIndex(const char* xmlItem) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        if (std::string(OL::kControls[i].xmlItem) == xmlItem) return i;
    }
    return -1;
}

irr::core::vector2di centre(const irr::core::rect<irr::s32>& r) {
    return irr::core::vector2di(
        (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
        (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2);
}

}  // namespace

// ===========================================================================
// The window, against the oracle's <Screen item="OptionsWindow">
// ===========================================================================

TEST(OptionsLayoutT434, TheWindowIsTheOraclesOwnSizeUnscaled) {
    // <Size><CX>400</CX><CY>452</CY></Size>. defaults.ini and a real player's
    // UI_Obian_project1999.ini both record 400x452 at EVERY resolution, which
    // is the measured reason this window is not scaled onto centerPopup the
    // way T415 scaled the tradeskill window.
    for (const auto wh : {std::pair<int, int>{800, 600},
                          std::pair<int, int>{1024, 768},
                          std::pair<int, int>{1280, 1024}}) {
        const auto l = layoutAt(wh.first, wh.second);
        const auto w = OL::windowRect(l);
        EXPECT_EQ(w.getWidth(), 400) << wh.first << "x" << wh.second;
        EXPECT_EQ(w.getHeight(), 452) << wh.first << "x" << wh.second;
    }
}

TEST(OptionsLayoutT434, TheWindowStaysWhollyOnScreen) {
    for (const auto wh : {std::pair<int, int>{640, 480},
                          std::pair<int, int>{800, 600},
                          std::pair<int, int>{1024, 768},
                          std::pair<int, int>{1280, 1024},
                          std::pair<int, int>{1920, 1080}}) {
        const auto l = layoutAt(wh.first, wh.second);
        const auto w = OL::windowRect(l);
        EXPECT_GE(w.UpperLeftCorner.X, 0) << rectStr(w);
        EXPECT_GE(w.UpperLeftCorner.Y, 0) << rectStr(w);
        EXPECT_LE(w.LowerRightCorner.X, l.screenW) << rectStr(w);
        EXPECT_LE(w.LowerRightCorner.Y, l.screenH) << rectStr(w);
    }
}

TEST(OptionsLayoutT434, TheBandsAddUpToTheOraclesHeight) {
    // The derivation the layout header writes out: 28 + 22 + 402 = 452, and
    // every <Page> is <Location>(0,22)</Location> <Size>387x402</Size>.
    EXPECT_EQ(OL::TITLE_H + OL::TAB_H + OL::PAGE_H, OL::ORACLE_WINDOW_H);
    EXPECT_EQ(OL::PAGE_W + 2 * OL::PAGE_X + 1, OL::ORACLE_WINDOW_W);

    const auto l = layout800();
    const auto win = OL::windowRect(l);
    const auto page = OL::pageRect(l);
    EXPECT_EQ(page.getWidth(), 387) << rectStr(page);
    EXPECT_EQ(page.getHeight(), 402) << rectStr(page);
    EXPECT_EQ(page.UpperLeftCorner.Y,
              win.UpperLeftCorner.Y + OL::TITLE_H + OL::TAB_H);
}

// ===========================================================================
// The tab strip -- OPTW_OptionsSubwindows' seven <Pages>
// ===========================================================================

TEST(OptionsLayoutT434, SevenTabsTileTheStripWithNoGapOrOverlap) {
    const auto l = layout800();
    const auto win = OL::windowRect(l);
    ASSERT_EQ(OL::TAB_COUNT, 7);
    for (int i = 0; i < OL::TAB_COUNT; ++i) {
        const auto r = OL::tabRect(l, i);
        EXPECT_EQ(r.getHeight(), OL::TAB_H) << i;
        EXPECT_EQ(r.UpperLeftCorner.Y, win.UpperLeftCorner.Y + OL::TITLE_H) << i;
        if (i == 0) {
            EXPECT_EQ(r.UpperLeftCorner.X, win.UpperLeftCorner.X);
        } else {
            EXPECT_EQ(r.UpperLeftCorner.X,
                      OL::tabRect(l, i - 1).LowerRightCorner.X) << i;
        }
    }
    EXPECT_EQ(OL::tabRect(l, OL::TAB_COUNT - 1).LowerRightCorner.X,
              win.LowerRightCorner.X);
}

TEST(OptionsLayoutT434, TheTabCaptionsAreTheOraclesTabText) {
    EXPECT_STREQ(OL::tabText(0), "General");
    EXPECT_STREQ(OL::tabText(1), "Display");
    EXPECT_STREQ(OL::tabText(2), "Mouse");
    EXPECT_STREQ(OL::tabText(3), "Keys");
    EXPECT_STREQ(OL::tabText(4), "Filters");
    EXPECT_STREQ(OL::tabText(5), "Colors");
    EXPECT_STREQ(OL::tabText(6), "Mail");
}

TEST(OptionsLayoutT434, EveryTabIsReachableByAClickOnItsHeader) {
    const auto l = layout800();
    StaticUIInput input;
    OptionsPanelState s;
    s.isOpen = true;
    for (int i = 0; i < OL::TAB_COUNT; ++i) {
        const auto p = centre(OL::tabRect(l, i));
        OL::Setting changed = OL::Setting::None;
        EXPECT_TRUE(input.processOptionsInput(p.X, p.Y, /*leftClicked=*/true, l,
                                              s, changed));
        EXPECT_EQ(s.activeTab, i) << "tab " << i;
        EXPECT_EQ(changed, OL::Setting::None);
    }
}

// ===========================================================================
// The controls, against the oracle's <Location>/<Size>
// ===========================================================================

TEST(OptionsLayoutT434, EveryTabHasControls) {
    int perTab[OL::TAB_COUNT] = {0};
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        perTab[static_cast<int>(OL::kControls[i].tab)]++;
    }
    for (int t = 0; t < OL::TAB_COUNT; ++t) {
        EXPECT_GT(perTab[t], 0) << "tab " << OL::tabText(t) << " is empty";
    }
}

TEST(OptionsLayoutT434, EveryControlIsInsideItsPage) {
    const auto l = layout800();
    const auto page = OL::pageRect(l);
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        const auto r = OL::controlRect(l, i);
        EXPECT_GE(r.UpperLeftCorner.X, page.UpperLeftCorner.X)
            << OL::kControls[i].xmlItem << " " << rectStr(r);
        EXPECT_GE(r.UpperLeftCorner.Y, page.UpperLeftCorner.Y)
            << OL::kControls[i].xmlItem << " " << rectStr(r);
        EXPECT_LE(r.LowerRightCorner.X, page.LowerRightCorner.X)
            << OL::kControls[i].xmlItem << " " << rectStr(r);
        EXPECT_LE(r.LowerRightCorner.Y, page.LowerRightCorner.Y)
            << OL::kControls[i].xmlItem << " " << rectStr(r);
    }
}

TEST(OptionsLayout, NoControlOutsideItsPage) {
    // B1053 (#373) close-out: the two resolutions the report was taken at,
    // for ALL tabs — the card's resolution pair.
    for (const auto wh : {std::pair<int, int>{1280, 720},
                          std::pair<int, int>{1024, 768}}) {
        const auto l = layoutAt(wh.first, wh.second);
        const auto page = OL::pageRect(l);
        const auto window = OL::windowRect(l);
        int perTab[OL::TAB_COUNT] = {0};
        for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
            const auto& control = OL::kControls[i];
            const auto r = OL::controlRect(l, i);
            EXPECT_GE(static_cast<int>(control.tab), 0) << control.xmlItem;
            EXPECT_LT(static_cast<int>(control.tab), OL::TAB_COUNT) << control.xmlItem;
            perTab[static_cast<int>(control.tab)]++;
            EXPECT_GT(r.getWidth(), 0) << control.xmlItem;
            EXPECT_GT(r.getHeight(), 0) << control.xmlItem;
            EXPECT_GE(r.UpperLeftCorner.X, page.UpperLeftCorner.X) << control.xmlItem;
            EXPECT_GE(r.UpperLeftCorner.Y, page.UpperLeftCorner.Y) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.X, page.LowerRightCorner.X) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.Y, page.LowerRightCorner.Y) << control.xmlItem;
            EXPECT_GE(r.UpperLeftCorner.X, window.UpperLeftCorner.X) << control.xmlItem;
            EXPECT_GE(r.UpperLeftCorner.Y, window.UpperLeftCorner.Y) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.X, window.LowerRightCorner.X) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.Y, window.LowerRightCorner.Y) << control.xmlItem;
        }
        for (int t = 0; t < OL::TAB_COUNT; ++t) {
            EXPECT_GT(perTab[t], 0) << "tab " << OL::tabText(t)
                                    << " has no control at " << wh.first << "x" << wh.second;
        }
    }
}

TEST(OptionsLayout, NoParticleSubPageControlOutsideItsNestedPage) {
    // B995's ODP_ParticlesTabs: a control with subPage >= 0 sits on one of the
    // nested box's three <Pages>, each 22px below the box origin. Its rect
    // must stay inside that sub-page and therefore inside the page and the
    // window — at both of the card's resolutions.
    constexpr irr::s32 kSubStripH = 22;  // the nested box's own tab strip
    for (const auto wh : {std::pair<int, int>{1280, 720},
                          std::pair<int, int>{1024, 768}}) {
        const auto l = layoutAt(wh.first, wh.second);
        const int box = controlIndex("ODP_ParticlesTabs");
        ASSERT_GE(box, 0);
        const auto boxRect = OL::controlRect(l, box);
        const auto page = OL::pageRect(l);
        const auto window = OL::windowRect(l);
        int perSub[3] = {0};
        for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
            const auto& control = OL::kControls[i];
            if (control.subPage < 0) continue;
            perSub[control.subPage]++;
            const irr::core::rect<irr::s32> subPage(
                boxRect.UpperLeftCorner.X,
                boxRect.UpperLeftCorner.Y + kSubStripH,
                boxRect.LowerRightCorner.X,
                boxRect.LowerRightCorner.Y);
            const auto r = OL::controlRect(l, i);
            EXPECT_GE(r.UpperLeftCorner.X, subPage.UpperLeftCorner.X) << control.xmlItem;
            EXPECT_GE(r.UpperLeftCorner.Y, subPage.UpperLeftCorner.Y) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.X, subPage.LowerRightCorner.X) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.Y, subPage.LowerRightCorner.Y) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.X, page.LowerRightCorner.X) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.Y, page.LowerRightCorner.Y) << control.xmlItem;
            EXPECT_LE(r.LowerRightCorner.Y, window.LowerRightCorner.Y) << control.xmlItem;
        }
        for (int s = 0; s < 3; ++s) {
            EXPECT_GT(perSub[s], 0) << "particle sub-page " << s << " is empty";
        }
    }
}

TEST(OptionsLayoutT434, ControlNamesAreUnique) {
    std::set<std::string> seen;
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        EXPECT_TRUE(seen.insert(OL::kControls[i].xmlItem).second)
            << "duplicate " << OL::kControls[i].xmlItem;
    }
}

TEST(OptionsLayoutT434, TheQuotedOffsetsAreTheOraclesOwn) {
    // A spot check per tab, each quoted from EQUI_OptionsWindow.xml. The
    // page-relative x/y/w/h in the table must be the XML's own numbers.
    struct Row { const char* item; irr::s32 x, y, w, h; };
    const Row rows[] = {
        // General: <Location>(10,15)</Location> <Size>150x20</Size>
        {"OGP_FastItemDestroyCheckbox", 10, 15, 150, 20},
        {"OGP_MusicVolumeSlider", 200, 70, 100, 16},
        {"OGP_SoundVolumeSlider", 200, 110, 100, 16},
        {"OGP_LoadSkinButton", 200, 345, 150, 20},
        {"OGP_AutoConsentGuildCheckbox", 10, 375, 150, 20},
        // Display
        {"ODP_PCNamesCheckbox", 10, 12, 150, 18},
        {"ODP_ClipPlaneSlider", 200, 129, 100, 16},
        {"ODP_ParticlesTabs", 190, 174, 190, 170},
        {"ODP_AdvancedButton", 190, 350, 150, 20},
        // Mouse
        {"OMP_MouseSensitivitySlider", 200, 30, 100, 16},
        // Keys: anchored -- Top=60, Left=5, Right=5, Bottom=5 in a 387x402
        // page => (5,60) 377x337.
        {"OKP_KeyboardAssignmentList", 5, 60, 377, 337},
        // Filters: Top=10, Left=40, Right=40, Bottom=40 => (40,10) 307x352.
        {"OFP_FilterList", 40, 10, 307, 352},
        // Colors: the change button anchors BOTH edges to the bottom --
        // Top=30 => y=372, Bottom=8 => y=394.
        {"OCP_UserColorChange", 126, 372, 159, 22},
        {"OFP_FilterComboBox", 60, 368, 267, 24},
        // Mail: a 16x16 checkbox with its caption in a separate label.
        {"OMP_MailForwardingCheckbox", 10, 80, 16, 16},
        {"OMP_AddressBookConfirmDeletionLabel", 30, 140, 347, 16},
    };
    for (const auto& row : rows) {
        const int i = controlIndex(row.item);
        ASSERT_GE(i, 0) << row.item << " is not in the table";
        const auto& c = OL::kControls[i];
        EXPECT_EQ(c.x, row.x) << row.item;
        EXPECT_EQ(c.y, row.y) << row.item;
        EXPECT_EQ(c.w, row.w) << row.item;
        EXPECT_EQ(c.h, row.h) << row.item;
    }
}

// ===========================================================================
// The T363 rule: a control with no setting is drawn and NOT hit-testable
// ===========================================================================

TEST(OptionsWindowT434, TheThreeT434SlidersStayWired) {
    // B430 wired more controls; the count is no longer pinned here -- the
    // classification table (test_options_wiring.cpp) is the census.
    EXPECT_EQ(OL::kControls[controlIndex("OGP_MusicVolumeSlider")].setting,
              OL::Setting::MusicVolume);
    EXPECT_EQ(OL::kControls[controlIndex("OGP_SoundVolumeSlider")].setting,
              OL::Setting::SoundVolume);
    EXPECT_EQ(OL::kControls[controlIndex("ODP_ClipPlaneSlider")].setting,
              OL::Setting::ClipPlane);
}

TEST(OptionsWindowT434, ADisabledControlIsNotHitTestable) {
    const auto l = layout800();
    OptionsPanelState s;
    s.isOpen = true;
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        const auto& c = OL::kControls[i];
        if (c.setting != OL::Setting::None) continue;
        s.activeTab = static_cast<int>(c.tab);
        // B995: a particle sub-page control is tested on its OWN sub-page --
        // the Spell, Env and Player pages share the oracle's rects.
        if (c.subPage >= 0) s.particlesTab = c.subPage;
        const auto p = centre(OL::controlRect(l, i));
        EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), -1)
            << c.xmlItem << " at " << rectStr(OL::controlRect(l, i));
    }
}

TEST(OptionsWindowT434, EachWiredControlRoundTripsThroughItsOwnHitTest) {
    const auto l = layout800();
    OptionsPanelState s;
    s.isOpen = true;
    for (const char* item : {"OGP_MusicVolumeSlider", "OGP_SoundVolumeSlider",
                             "ODP_ClipPlaneSlider"}) {
        const int i = controlIndex(item);
        ASSERT_GE(i, 0) << item;
        s.activeTab = static_cast<int>(OL::kControls[i].tab);
        const auto p = centre(OL::controlRect(l, i));
        EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), i) << item;
    }
}

TEST(OptionsWindowT434, AControlOnAnInactiveTabIsNotHitTestable) {
    const auto l = layout800();
    OptionsPanelState s;
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::Tab::General);
    const int clip = controlIndex("ODP_ClipPlaneSlider");
    ASSERT_GE(clip, 0);
    const auto p = centre(OL::controlRect(l, clip));
    EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), -1);
    s.activeTab = static_cast<int>(OL::Tab::Display);
    EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), clip);
}

TEST(OptionsWindowT434, AClosedWindowHitTestsNothingAndConsumesNothing) {
    const auto l = layout800();
    StaticUIInput input;
    OptionsPanelState s;  // isOpen defaults false -- UI_Obian_*.ini's Show=0
    ASSERT_FALSE(s.isOpen);
    const auto p = centre(OL::windowRect(l));
    EXPECT_EQ(hitTestOptionsTab(l, s, p.X, p.Y), -1);
    EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), -1);
    OL::Setting changed = OL::Setting::None;
    EXPECT_FALSE(input.processOptionsInput(p.X, p.Y, true, l, s, changed));
}

TEST(OptionsWindowT434, AnOpenWindowConsumesEveryClickInsideIt) {
    const auto l = layout800();
    StaticUIInput input;
    OptionsPanelState s;
    s.isOpen = true;
    // A greyed control: the click must be swallowed, not fall through to the
    // 3D scene, and must change nothing.
    const int i = controlIndex("OGP_FastItemDestroyCheckbox");
    ASSERT_GE(i, 0);
    const auto p = centre(OL::controlRect(l, i));
    OL::Setting changed = OL::Setting::None;
    EXPECT_TRUE(input.processOptionsInput(p.X, p.Y, true, l, s, changed));
    EXPECT_EQ(changed, OL::Setting::None);
    EXPECT_EQ(s.hoveredControl, -1);

    // Outside the window it consumes nothing.
    const auto win = OL::windowRect(l);
    EXPECT_FALSE(input.processOptionsInput(win.UpperLeftCorner.X - 3,
                                           win.UpperLeftCorner.Y - 3, true, l,
                                           s, changed));
}

// ===========================================================================
// The three wired sliders actually move their settings
// ===========================================================================

TEST(OptionsWindowT434, DraggingASliderMovesItsSettingAndNamesIt) {
    const auto l = layout800();
    StaticUIInput input;
    OptionsPanelState s;
    s.isOpen = true;

    struct Case { const char* item; OL::Setting setting; };
    const Case cases[] = {
        {"OGP_MusicVolumeSlider", OL::Setting::MusicVolume},
        {"OGP_SoundVolumeSlider", OL::Setting::SoundVolume},
        {"ODP_ClipPlaneSlider", OL::Setting::ClipPlane},
    };
    for (const auto& c : cases) {
        const int i = controlIndex(c.item);
        ASSERT_GE(i, 0) << c.item;
        s.activeTab = static_cast<int>(OL::kControls[i].tab);
        const auto r = OL::controlRect(l, i);

        // Far left = step 0, far right = step 20.
        OL::Setting changed = OL::Setting::None;
        input.processOptionsInput(r.UpperLeftCorner.X + 1,
                                  centre(r).Y, true, l, s, changed);
        EXPECT_EQ(changed, c.setting) << c.item;
        EXPECT_EQ(optionsSettingStep(s, c.setting), 0) << c.item;

        changed = OL::Setting::None;
        input.processOptionsInput(r.LowerRightCorner.X - 1, centre(r).Y, true,
                                  l, s, changed);
        EXPECT_EQ(changed, c.setting) << c.item;
        EXPECT_EQ(optionsSettingStep(s, c.setting), OL::SLIDER_STEPS) << c.item;

        // A click landing on the step it is already at reports NO change,
        // so the applier never sends a zero delta.
        changed = OL::Setting::None;
        input.processOptionsInput(r.LowerRightCorner.X - 1, centre(r).Y, true,
                                  l, s, changed);
        EXPECT_EQ(changed, OL::Setting::None) << c.item;
    }
}

TEST(OptionsWindowT434, TheKnobTracksTheStepAndTheClickRoundTrips) {
    const auto l = layout800();
    const int i = controlIndex("OGP_MusicVolumeSlider");
    ASSERT_GE(i, 0);
    for (int step = 0; step <= OL::SLIDER_STEPS; ++step) {
        const auto knob = OL::sliderKnobRect(l, i, step);
        EXPECT_EQ(OL::sliderStepAt(l, i, centre(knob).X), step)
            << "step " << step << " knob " << rectStr(knob);
        EXPECT_TRUE(OL::controlRect(l, i).isRectCollided(knob) ||
                    OL::controlRect(l, i).isPointInside(centre(knob)))
            << "step " << step;
    }
}

TEST(OptionsWindowT434, TheReadoutIsTheOraclesValueTimesFive) {
    // MEASURED: eqclient.ini holds Music=18 / SoundVolume=10 and that same
    // client displays 90 % / 50 %
    // (review/T377/oracle/T377_oracle_options_alt_o_1024x768.png).
    OptionsPanelState s;
    s.musicVolume = 18;
    s.soundVolume = 10;
    EXPECT_EQ(optionsSettingText(s, OL::Setting::MusicVolume), "90 %");
    EXPECT_EQ(optionsSettingText(s, OL::Setting::SoundVolume), "50 %");
    s.musicVolume = 1;  // the ini's Realism=1, shown as 5 %
    EXPECT_EQ(optionsSettingText(s, OL::Setting::MusicVolume), "5 %");
}

TEST(OptionsWindowT434, TheClipPlaneMappingCoversFablesRenderDistanceRange) {
    // fable's mapping, flagged in options_panel.h: step*50 + 50, so the
    // shipped renderDistance of 300 is exactly step 5 and the whole range
    // stays inside the 50..10000 band /renderdist enforces.
    // B606 (#189) deleted optionsClipPlaneStep() (zero callers): the panel
    // only ever stores the step, never derives one from a distance. The
    // inverse these cases used pins the mapping, so it is computed here.
    auto stepFromDistance = [](float distance) {
        const int step = static_cast<int>((distance - 50.0f) / 50.0f + 0.5f);
        return std::max(0, std::min(OL::SLIDER_STEPS, step));
    };
    EXPECT_FLOAT_EQ(optionsClipPlaneDistance(0), 50.0f);
    EXPECT_FLOAT_EQ(optionsClipPlaneDistance(5), 300.0f);
    EXPECT_FLOAT_EQ(optionsClipPlaneDistance(20), 1050.0f);
    EXPECT_EQ(stepFromDistance(300.0f), 5);
    for (int step = 0; step <= OL::SLIDER_STEPS; ++step) {
        EXPECT_EQ(stepFromDistance(optionsClipPlaneDistance(step)), step);
        EXPECT_GE(optionsClipPlaneDistance(step), 50.0f);
        EXPECT_LE(optionsClipPlaneDistance(step), 10000.0f);
    }
    // Out of range clamps rather than casting to something absurd.
    EXPECT_FLOAT_EQ(optionsClipPlaneDistance(-4), 50.0f);
    EXPECT_FLOAT_EQ(optionsClipPlaneDistance(99), 1050.0f);
    EXPECT_EQ(stepFromDistance(-1.0f), 0);
    EXPECT_EQ(stepFromDistance(99999.0f), 20);
}

TEST(OptionsWindowT434, VolumeStepsMapOntoTheZeroToOneFractionAudioTakes) {
    // B606 (#189) deleted the optionsVolumeFraction/optionsVolumeStep
    // wrappers (zero callers); the conversion is the audio-side function.
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(0), 0.0f);
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(20), 1.0f);
    EXPECT_FLOAT_EQ(eqt::audio::volumeFractionFromStep(14), 0.7f);   // config default
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(0.7f), 14);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(1.0f), 20);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(-1.0f), 0);
    EXPECT_EQ(eqt::audio::volumeStepFromFraction(5.0f), 20);
}

TEST(OptionsWindowT434, TheDefaultsAreFablesOwnShippedValuesOnTheOracleScale) {
    const OptionsPanelState s;
    // AudioManager/bridge_component defaults: music 0.7, effects 1.0.
    EXPECT_EQ(s.musicVolume, eqt::audio::volumeStepFromFraction(0.7f));
    EXPECT_EQ(s.soundVolume, eqt::audio::volumeStepFromFraction(1.0f));
    // config/display_settings.json's renderDistance: 300, which the
    // step*50 + 50 mapping places on step 5.
    EXPECT_EQ(s.clipPlane, 5);
    EXPECT_FLOAT_EQ(optionsClipPlaneDistance(s.clipPlane), 300.0f);
}

// ===========================================================================
// The key -- Alt+O, and `O` unbound
// ===========================================================================

TEST(OptionsWindowT434, TheKeyIsAltOAndBareOIsUnbound) {
    // MEASURED at the oracle's Options > Keys > UI page
    // (review/T377/oracle/T377_oracle_options_keys_UI_page2.png):
    // "Toggle options window   Alt O", Alternate column EMPTY.
    auto& mgr = eqt::input::HotkeyManager::instance();
    mgr.resetToDefaults();

    const auto withAlt = mgr.getAction(irr::KEY_KEY_O, /*ctrl=*/false,
                                       /*shift=*/false, /*alt=*/true,
                                       eqt::input::HotkeyMode::Player);
    ASSERT_TRUE(withAlt.has_value()) << "Alt+O is not bound";
    EXPECT_EQ(*withAlt, eqt::input::HotkeyAction::ToggleOptions);

    const auto bare = mgr.getAction(irr::KEY_KEY_O, false, false, false,
                                    eqt::input::HotkeyMode::Player);
    EXPECT_FALSE(bare.has_value())
        << "a bare O is still bound -- the oracle leaves it free";
}

// ===========================================================================
// Persistence, through T432's ONE writer
// ===========================================================================

TEST(OptionsPreferencesT434, TheWindowStateAndTheThreeSlidersRoundTrip) {
    UiPreferences p;
    p.optionsWindow = true;
    p.musicVolume = 18;
    p.soundVolume = 10;
    p.clipPlane = 20;

    const std::string text = SerializeUiPreferences(p);
    // The oracle's own section and key names.
    EXPECT_NE(text.find("[OptionsWindow]"), std::string::npos) << text;
    EXPECT_NE(text.find("[Options]"), std::string::npos) << text;
    EXPECT_NE(text.find("Music=18"), std::string::npos) << text;
    EXPECT_NE(text.find("SoundVolume=10"), std::string::npos) << text;
    EXPECT_NE(text.find("ClipPlane=20"), std::string::npos) << text;

    const UiPreferences back = ParseUiPreferences(text);
    EXPECT_TRUE(back.optionsWindow);
    EXPECT_EQ(back.musicVolume, 18);
    EXPECT_EQ(back.soundVolume, 10);
    EXPECT_EQ(back.clipPlane, 20);
    EXPECT_EQ(back, p);
}

TEST(OptionsPreferencesT434, AnAbsentKeyKeepsTheDefault) {
    // The official rule (T368): a default is stated by OMISSION. A file that
    // says nothing about the sliders must not zero them.
    const UiPreferences parsed = ParseUiPreferences("[GroupWindow]\nShow=0\n");
    const UiPreferences defaults;
    EXPECT_EQ(parsed.musicVolume, defaults.musicVolume);
    EXPECT_EQ(parsed.soundVolume, defaults.soundVolume);
    EXPECT_EQ(parsed.clipPlane, defaults.clipPlane);
    EXPECT_EQ(parsed.optionsWindow, defaults.optionsWindow);
}

TEST(OptionsPreferencesT434, AnOutOfRangeOrGarbageValueIsRefused) {
    const UiPreferences defaults;
    const UiPreferences bad = ParseUiPreferences(
        "[Options]\nMusic=99\nSoundVolume=-3\nClipPlane=banana\n");
    EXPECT_EQ(bad.musicVolume, defaults.musicVolume);
    EXPECT_EQ(bad.soundVolume, defaults.soundVolume);
    EXPECT_EQ(bad.clipPlane, defaults.clipPlane);
}

TEST(OptionsPreferencesT434, TheOfficialFilesOptionsWindowShowIsHonoured) {
    // Quoted from /mnt/projects/EverQuestP1999/UI_Obian_project1999.ini.
    const UiPreferences p = ParseUiPreferences(
        "[OptionsWindow]\r\n"
        "INIVersion=3\r\n"
        "XPos1280x1024=95\r\n"
        "Width1280x1024=400\r\n"
        "Height1280x1024=452\r\n"
        "Locked=false\r\n"
        "Show=0\r\n");
    EXPECT_FALSE(p.optionsWindow);
    const UiPreferences q = ParseUiPreferences("[OptionsWindow]\nShow=1\n");
    EXPECT_TRUE(q.optionsWindow);
}

TEST(OptionsPreferencesT434, CollectAndApplyCarryTheWholeOptionsState) {
    OptionsPanelState opt;
    UiPreferencesBinding b;
    b.options = &opt;

    opt.isOpen = true;
    opt.musicVolume = 3;
    opt.soundVolume = 7;
    opt.clipPlane = 11;
    const UiPreferences collected = CollectUiPreferences(b, /*cameraMode=*/1);
    EXPECT_TRUE(collected.optionsWindow);
    EXPECT_EQ(collected.musicVolume, 3);
    EXPECT_EQ(collected.soundVolume, 7);
    EXPECT_EQ(collected.clipPlane, 11);

    OptionsPanelState other;
    UiPreferencesBinding b2;
    b2.options = &other;
    ApplyUiPreferences(collected, b2);
    EXPECT_TRUE(other.isOpen);
    EXPECT_EQ(other.musicVolume, 3);
    EXPECT_EQ(other.soundVolume, 7);
    EXPECT_EQ(other.clipPlane, 11);

    // Apply clamps a value assembled in code, not only one read from a file.
    UiPreferences wild = collected;
    wild.musicVolume = 500;
    wild.clipPlane = -9;
    ApplyUiPreferences(wild, b2);
    EXPECT_EQ(other.musicVolume, 20);
    EXPECT_EQ(other.clipPlane, 0);
}

TEST(OptionsPreferencesT434, TheDefaultsMatchTheOptionsPanelStruct) {
    // The same invariant T432's DefaultsMatchThePanelStructs asserts, for the
    // four fields this unit added.
    const UiPreferences defaults;
    const OptionsPanelState panel;
    EXPECT_EQ(defaults.optionsWindow, panel.isOpen);
    EXPECT_EQ(defaults.musicVolume, panel.musicVolume);
    EXPECT_EQ(defaults.soundVolume, panel.soundVolume);
    EXPECT_EQ(defaults.clipPlane, panel.clipPlane);
}
