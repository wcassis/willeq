/*
 * The colour picker window (B993) -- see color_picker.h for the oracle XML
 * every rect is quoted from and the flagged behaviour assumptions.
 */

#include "client/graphics/ui/color_picker.h"

#include <algorithm>
#include <string>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

namespace ColorPickerLayout {

namespace {

struct Swatch {
    irr::s32 x, y;
    eqt::ui::UserColor color;
};

// CPW_TC0Button .. CPW_TC14Button: <Location> and <TextColor>, verbatim.
const Swatch kSwatches[kSwatchCount] = {
    {5, 5, {240, 0, 0}},     {30, 5, {0, 240, 0}},    {55, 5, {0, 0, 240}},
    {80, 5, {240, 240, 0}},  {105, 5, {240, 240, 240}},
    {5, 25, {127, 0, 0}},    {30, 25, {0, 127, 0}},   {55, 25, {0, 0, 127}},
    {80, 25, {127, 127, 0}}, {105, 25, {127, 127, 127}},
    {5, 45, {0, 240, 240}},  {30, 45, {0, 127, 127}}, {55, 45, {240, 127, 0}},
    {80, 45, {127, 64, 0}},  {105, 45, {0, 0, 0}},
};

constexpr irr::s32 kSwatchSize = 20;
constexpr irr::s32 kRowY[3] = {70, 90, 110};  // Red, Green, Blue

irr::core::rect<irr::s32> child(const UILayout& layout, irr::s32 x, irr::s32 y, irr::s32 w,
                                irr::s32 h) {
    const auto win = windowRect(layout);
    const irr::s32 ox = win.UpperLeftCorner.X + x;
    const irr::s32 oy = win.UpperLeftCorner.Y + kTitleH + y;
    return irr::core::rect<irr::s32>(ox, oy, ox + w, oy + h);
}

}  // namespace

eqt::ui::UserColor swatchColor(int i) {
    if (i < 0 || i >= kSwatchCount) return eqt::ui::UserColor{};
    return kSwatches[i].color;
}

irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    // The oracle's absolute <Location>, pulled back on screen if the
    // resolution is too small to hold it.
    irr::s32 x = kWindowX, y = kWindowY;
    if (x + kWindowW > layout.screenW) x = std::max(0, layout.screenW - kWindowW);
    if (y + kWindowH > layout.screenH) y = std::max(0, layout.screenH - kWindowH);
    return irr::core::rect<irr::s32>(x, y, x + kWindowW, y + kWindowH);
}

irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout) {
    const auto win = windowRect(layout);
    return irr::core::rect<irr::s32>(win.LowerRightCorner.X - 15, win.UpperLeftCorner.Y + 3,
                                     win.LowerRightCorner.X - 3, win.UpperLeftCorner.Y + 15);
}

irr::core::rect<irr::s32> swatchRect(const UILayout& layout, int i) {
    if (i < 0 || i >= kSwatchCount) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return child(layout, kSwatches[i].x, kSwatches[i].y, kSwatchSize, kSwatchSize);
}

irr::core::rect<irr::s32> sliderRect(const UILayout& layout, int channel) {
    if (channel < 0 || channel > 2) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return child(layout, 40, kRowY[channel], 85, 20);
}

irr::core::rect<irr::s32> editRect(const UILayout& layout, int channel) {
    if (channel < 0 || channel > 2) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    return child(layout, 5, kRowY[channel], 35, 20);
}

irr::core::rect<irr::s32> previewRect(const UILayout& layout) { return child(layout, 5, 135, 40, 30); }
irr::core::rect<irr::s32> acceptRect(const UILayout& layout) { return child(layout, 50, 135, 75, 30); }

int sliderValueAt(const UILayout& layout, int channel, int mouseX) {
    const auto r = sliderRect(layout, channel);
    const int w = r.getWidth() - 1;
    if (w <= 0) return 0;
    const int rel = std::max(0, std::min(w, mouseX - r.UpperLeftCorner.X));
    return (rel * 255 + w / 2) / w;
}

}  // namespace ColorPickerLayout

namespace {

uint8_t& channelRef(eqt::ui::UserColor& c, int channel) {
    return channel == 0 ? c.r : (channel == 1 ? c.g : c.b);
}

irr::video::SColor toSColor(const eqt::ui::UserColor& c) {
    return irr::video::SColor(255, c.r, c.g, c.b);
}

}  // namespace

void openColorPicker(ColorPickerState& picker, const eqt::ui::ChatTextColors& table, int n) {
    if (n < 1 || n > eqt::ui::kUserColorCount) return;
    picker.isOpen = true;
    picker.row = n;
    picker.working = table[n - 1];
}

bool colorPickerContains(const UILayout& layout, const ColorPickerState& picker, int x, int y) {
    return picker.isOpen &&
           ColorPickerLayout::windowRect(layout).isPointInside(irr::core::vector2di(x, y));
}

bool clickColorPicker(const UILayout& layout, ColorPickerState& picker,
                      eqt::ui::ChatTextColors& table, int x, int y) {
    using namespace ColorPickerLayout;
    if (!colorPickerContains(layout, picker, x, y)) return false;
    const irr::core::vector2di p(x, y);
    if (closeBoxRect(layout).isPointInside(p)) {  // P3: no change
        picker.isOpen = false;
        return false;
    }
    for (int i = 0; i < kSwatchCount; ++i) {      // P1
        if (swatchRect(layout, i).isPointInside(p)) {
            picker.working = swatchColor(i);
            return false;
        }
    }
    for (int ch = 0; ch < 3; ++ch) {              // P2
        if (sliderRect(layout, ch).isPointInside(p)) {
            channelRef(picker.working, ch) = static_cast<uint8_t>(sliderValueAt(layout, ch, x));
            return false;
        }
    }
    if (acceptRect(layout).isPointInside(p)) {    // P3
        picker.isOpen = false;
        if (picker.row < 1 || picker.row > eqt::ui::kUserColorCount) return false;
        eqt::ui::UserColor& slot = table[picker.row - 1];
        if (slot == picker.working) return false;
        slot = picker.working;
        return true;
    }
    return false;
}

void renderColorPicker(UIRenderer& ui, const UILayout& layout, const ColorPickerState& picker) {
    using namespace ColorPickerLayout;
    if (!picker.isOpen) return;
    const irr::video::SColor kFrame(255, 60, 60, 70);
    const irr::video::SColor kText(255, 255, 255, 255);
    const irr::video::SColor kTrack(200, 20, 20, 25);
    const irr::video::SColor kKnob(255, 200, 180, 90);
    auto* tb = ui.getTextBatch();

    const auto win = windowRect(layout);
    ui.drawPanel(win);
    if (tb) {
        // <Text>Choose a color</Text>, in fable's title band.
        tb->addText("Choose a color", win.UpperLeftCorner.X + 6, win.UpperLeftCorner.Y + 3,
                    irr::video::SColor(255, 255, 215, 0));
    }
    const auto close = closeBoxRect(layout);
    ui.drawFrame(close, kFrame);
    if (tb) tb->addTextCentered("x", close, kText);

    for (int i = 0; i < kSwatchCount; ++i) {
        const auto r = swatchRect(layout, i);
        ui.drawFrame(r, kFrame);
        // The A_WhitePixel decal at <DecalOffset>(2,2) <DecalSize>16x16.
        ui.drawRect(irr::core::rect<irr::s32>(r.UpperLeftCorner.X + 2, r.UpperLeftCorner.Y + 2,
                                              r.UpperLeftCorner.X + 18, r.UpperLeftCorner.Y + 18),
                    toSColor(swatchColor(i)));
    }
    const uint8_t values[3] = {picker.working.r, picker.working.g, picker.working.b};
    const irr::video::SColor editColors[3] = {  // the Editboxes' own <TextColor>
        irr::video::SColor(255, 200, 0, 0), irr::video::SColor(255, 0, 200, 0),
        irr::video::SColor(255, 0, 0, 200)};
    for (int ch = 0; ch < 3; ++ch) {
        const auto e = editRect(layout, ch);
        ui.drawFrame(e, kFrame);
        if (tb) tb->addText(std::to_string(values[ch]), e.UpperLeftCorner.X + 3,
                            e.UpperLeftCorner.Y + 3, editColors[ch]);
        const auto s = sliderRect(layout, ch);
        ui.drawRect(s, kTrack);
        ui.drawFrame(s, kFrame);
        const irr::s32 travel = s.getWidth() - 8;
        const irr::s32 kx = s.UpperLeftCorner.X + (travel * values[ch]) / 255;
        ui.drawRect(irr::core::rect<irr::s32>(kx, s.UpperLeftCorner.Y, kx + 8, s.LowerRightCorner.Y),
                    kKnob);
    }
    const auto prev = previewRect(layout);
    ui.drawFrame(prev, kFrame);
    // CPW_TC15Button's decal: <DecalOffset>(2,2) <DecalSize>36x26.
    ui.drawRect(irr::core::rect<irr::s32>(prev.UpperLeftCorner.X + 2, prev.UpperLeftCorner.Y + 2,
                                          prev.UpperLeftCorner.X + 38, prev.UpperLeftCorner.Y + 28),
                toSColor(picker.working));
    const auto acc = acceptRect(layout);
    ui.drawFrame(acc, kFrame);
    if (tb) tb->addTextCentered("Accept", acc, kText);
}

}  // namespace Graphics
}  // namespace EQT
