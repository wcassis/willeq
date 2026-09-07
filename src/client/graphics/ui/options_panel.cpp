/*
 * The Options window's draw and hit tests (T434).
 *
 * See include/client/graphics/ui/options_panel.h for what is wired and why,
 * and include/client/graphics/ui/options_layout.h for the oracle XML every
 * rect is quoted from. This TU computes no rect: every one of them comes out
 * of OptionsLayout, which is what keeps the draw and the two hit tests on one
 * geometry source (T57's rule).
 */

#include "client/graphics/ui/options_panel.h"

#include <algorithm>
#include <string>

#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"

namespace EQT {
namespace Graphics {

namespace {

using OptionsLayout::Kind;
using OptionsLayout::Setting;

// The oracle paints an unchecked checkbox's caption white and a checked
// one's yellow (review/T377/oracle/T377_oracle_options_alt_o_1024x768.png:
// "Receive Guild Invites" and "Allow Context Menus" are yellow, "Autosplit"
// and "Anonymous" are white). fable has no state behind any checkbox on this
// window, so every one of them is drawn in the DISABLED grey below rather
// than picked a colour for.
const irr::video::SColor kEnabledText(255, 230, 230, 210);
const irr::video::SColor kDisabledText(255, 122, 122, 122);
const irr::video::SColor kTitleText(255, 255, 215, 0);
const irr::video::SColor kTabActiveText(255, 255, 255, 0);    // TabTextActiveColor
const irr::video::SColor kTabInactiveText(255, 255, 255, 255);// TabTextColor
const irr::video::SColor kFrame(255, 60, 60, 70);
const irr::video::SColor kDisabledFrame(255, 48, 48, 54);
const irr::video::SColor kSliderTrack(200, 20, 20, 25);
const irr::video::SColor kKnobEnabled(255, 200, 180, 90);
const irr::video::SColor kKnobDisabled(255, 90, 90, 96);
const irr::video::SColor kHover(60, 255, 255, 255);
const irr::video::SColor kTabActiveFill(90, 255, 255, 255);

bool enabled(const OptionsLayout::Control& c) { return c.setting != Setting::None; }

}  // namespace

bool optionsWindowIsVisible(const OptionsPanelState& state) {
    return state.isOpen;
}

int optionsSettingStep(const OptionsPanelState& state, Setting setting) {
    switch (setting) {
        case Setting::MusicVolume: return state.musicVolume;
        case Setting::SoundVolume: return state.soundVolume;
        case Setting::ClipPlane:   return state.clipPlane;
        case Setting::None:        break;
    }
    return 0;
}

void setOptionsSettingStep(OptionsPanelState& state, Setting setting, int step) {
    step = std::max(0, std::min(OptionsLayout::SLIDER_STEPS, step));
    switch (setting) {
        case Setting::MusicVolume: state.musicVolume = step; break;
        case Setting::SoundVolume: state.soundVolume = step; break;
        case Setting::ClipPlane:   state.clipPlane = step; break;
        case Setting::None:        break;
    }
}

std::string optionsSettingText(const OptionsPanelState& state, Setting setting) {
    if (setting == Setting::None) return "0 %";
    // The oracle's own readout: the stored 0..20 integer times five, as a
    // percentage. See the measurement in options_panel.h.
    return std::to_string(optionsSettingStep(state, setting) * 5) + " %";
}

float optionsClipPlaneDistance(int step) {
    step = std::max(0, std::min(OptionsLayout::SLIDER_STEPS, step));
    return 50.0f + static_cast<float>(step) * 50.0f;
}

int optionsClipPlaneStep(float distance) {
    const int step = static_cast<int>((distance - 50.0f) / 50.0f + 0.5f);
    return std::max(0, std::min(OptionsLayout::SLIDER_STEPS, step));
}

float optionsVolumeFraction(int step) {
    step = std::max(0, std::min(OptionsLayout::SLIDER_STEPS, step));
    return static_cast<float>(step) / static_cast<float>(OptionsLayout::SLIDER_STEPS);
}

int optionsVolumeStep(float fraction) {
    const int step = static_cast<int>(
        fraction * static_cast<float>(OptionsLayout::SLIDER_STEPS) + 0.5f);
    return std::max(0, std::min(OptionsLayout::SLIDER_STEPS, step));
}

void renderOptionsWindow(UIRenderer& ui, const UILayout& layout,
                         const OptionsPanelState& state) {
    if (!optionsWindowIsVisible(state)) return;

    auto* tb = ui.getTextBatch();
    const auto win = OptionsLayout::windowRect(layout);

    ui.drawPanel(win);

    // <Text>Options</Text>, in fable's title band.
    if (tb) {
        tb->addText("Options", win.UpperLeftCorner.X + 8,
                    win.UpperLeftCorner.Y + 6, kTitleText);
    }

    // OPTW_OptionsSubwindows' tab strip. TabTextActiveColor is 255/255/0 and
    // TabTextColor 255/255/255 in the XML; both are honoured.
    for (int i = 0; i < OptionsLayout::TAB_COUNT; ++i) {
        const auto r = OptionsLayout::tabRect(layout, i);
        if (i == state.activeTab) ui.drawRect(r, kTabActiveFill);
        else if (i == state.hoveredTab) ui.drawRect(r, kHover);
        ui.drawFrame(r, kFrame);
        if (tb) {
            tb->addTextCentered(OptionsLayout::tabText(i), r,
                                i == state.activeTab ? kTabActiveText
                                                     : kTabInactiveText);
        }
    }

    // The active <Page>.
    const auto page = OptionsLayout::pageRect(layout);
    ui.drawFrame(page, kFrame);

    for (int i = 0; i < OptionsLayout::CONTROL_COUNT; ++i) {
        const auto& c = OptionsLayout::kControls[i];
        if (static_cast<int>(c.tab) != state.activeTab) continue;

        const auto r = OptionsLayout::controlRect(layout, i);
        const bool on = enabled(c);
        const irr::video::SColor text = on ? kEnabledText : kDisabledText;
        const irr::video::SColor frame = on ? kFrame : kDisabledFrame;

        switch (c.kind) {
            case Kind::Label:
                if (tb) tb->addText(c.text, r.UpperLeftCorner.X,
                                    r.UpperLeftCorner.Y, text);
                break;

            case Kind::ValueLabel:
                if (tb) {
                    // A wired slider prints its live value; a disabled one
                    // prints the oracle's own shipped placeholder text.
                    const std::string s = on ? optionsSettingText(state, c.setting)
                                             : std::string(c.text);
                    tb->addText(s, r.UpperLeftCorner.X, r.UpperLeftCorner.Y, text);
                }
                break;

            case Kind::Checkbox: {
                // The oracle's checkbox is a Style_Checkbox button whose box
                // sits at the left of its rect with the caption beside it.
                const irr::core::rect<irr::s32> box(
                    r.UpperLeftCorner.X, r.UpperLeftCorner.Y + 2,
                    r.UpperLeftCorner.X + 12, r.UpperLeftCorner.Y + 14);
                ui.drawFrame(box, frame);
                if (tb && c.text[0] != '\0') {
                    tb->addText(c.text, box.LowerRightCorner.X + 4,
                                r.UpperLeftCorner.Y + 2, text);
                }
                break;
            }

            case Kind::Slider: {
                ui.drawRect(r, kSliderTrack);
                ui.drawFrame(r, frame);
                const int step = optionsSettingStep(state, c.setting);
                ui.drawRect(OptionsLayout::sliderKnobRect(layout, i, step),
                            on ? kKnobEnabled : kKnobDisabled);
                if (on && state.hoveredControl == i) ui.drawRect(r, kHover);
                break;
            }

            case Kind::Combobox:
            case Kind::Editbox:
            case Kind::Button:
            case Kind::Listbox:
            case Kind::TabBox:
                ui.drawFrame(r, frame);
                if (!tb || c.text[0] == '\0') break;
                if (c.kind == Kind::Button) {
                    tb->addTextCentered(c.text, r, text);
                } else {
                    // Listbox column headers and the nested tab box's own
                    // sub-tab captions are pipe-separated in the table; they
                    // are drawn as one header row rather than invented into
                    // columns fable has no rows for.
                    std::string header(c.text);
                    std::replace(header.begin(), header.end(), '|', ' ');
                    tb->addText(header, r.UpperLeftCorner.X + 3,
                                r.UpperLeftCorner.Y + 2, text);
                }
                break;
        }
    }
}

int hitTestOptionsTab(const UILayout& layout, const OptionsPanelState& state,
                      int mouseX, int mouseY) {
    if (!optionsWindowIsVisible(state)) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    for (int i = 0; i < OptionsLayout::TAB_COUNT; ++i) {
        if (OptionsLayout::tabRect(layout, i).isPointInside(p)) return i;
    }
    return -1;
}

int hitTestOptionsControl(const UILayout& layout, const OptionsPanelState& state,
                          int mouseX, int mouseY) {
    if (!optionsWindowIsVisible(state)) return -1;
    const irr::core::vector2di p(mouseX, mouseY);
    for (int i = 0; i < OptionsLayout::CONTROL_COUNT; ++i) {
        const auto& c = OptionsLayout::kControls[i];
        if (static_cast<int>(c.tab) != state.activeTab) continue;
        // A control with no setting behind it is drawn and never hit --
        // the T363 rule, made mechanical.
        if (!enabled(c)) continue;
        if (OptionsLayout::controlRect(layout, i).isPointInside(p)) return i;
    }
    return -1;
}

bool optionsWindowContains(const UILayout& layout,
                           const OptionsPanelState& state, int mouseX,
                           int mouseY) {
    if (!optionsWindowIsVisible(state)) return false;
    return OptionsLayout::windowRect(layout).isPointInside(
        irr::core::vector2di(mouseX, mouseY));
}

}  // namespace Graphics
}  // namespace EQT
