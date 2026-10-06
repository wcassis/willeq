/*
 * The colour picker window -- EQUI_ColorPickerWnd.xml, opened by the Options
 * window's "Change Selected Color" (B993, #313).
 *
 * GEOMETRY IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_ColorPickerWnd.xml:
 *
 *   <Screen item="ColorPickerWnd">  RelativePosition false,
 *     <Location>(260,140)</Location> <Size>140x190</Size>,
 *     <Text>Choose a color</Text>, Style_Titlebar + Style_Closebox,
 *     Style_Sizable false
 *   CPW_TC0Button .. CPW_TC14Button   20x20 swatches, TextColor = the swatch
 *                                     colour (NormalDecal A_WhitePixel at
 *                                     (2,2) 16x16, tinted)
 *   CPW_Red/Green/BlueSliderInput     (5, 70/90/110) 35x20 Editboxes
 *   CPW_Red/Green/BlueSlider          (40, 70/90/110) 85x20 Sliders
 *   CPW_TC15Button                    (5,135) 40x30, the current colour
 *   CPW_Accept_Button                 (50,135) 75x30 "Accept"
 *
 * Drawn UNSCALED at the oracle's own absolute (260,140) -- the window is not
 * sizable and every rect below is the XML's. Children are offset by fable's
 * own title band (kTitleH, 18: the bank and container windows' value) --
 * the XML's last child ends at y 165, so 165 + 18 = 183 fits the 190.
 *
 * BEHAVIOUR. ASSUMPTIONS AWAITING ORACLE (none of it is in the XML):
 *   P1. a swatch click sets all three channels to the swatch's colour;
 *   P2. a slider click sets that channel to the clicked position, 0..255
 *       (the oracle's slider range is not in the file; [TextColors] is 0..255);
 *   P3. Accept writes the colour into the row and closes the window; the
 *       close box closes it with no change;
 *   P4. the three Editboxes show the channel values and take no typing here
 *       (fable's Editboxes are read-only displays in this window).
 *
 * Device-free except for the draw (UIRenderer).
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>

#include "client/chat/chat_text_colors.h"
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

class UIRenderer;

namespace ColorPickerLayout {

constexpr irr::s32 kWindowX = 260;  // <Location>
constexpr irr::s32 kWindowY = 140;
constexpr irr::s32 kWindowW = 140;  // <Size>
constexpr irr::s32 kWindowH = 190;
constexpr irr::s32 kTitleH = 18;    // fable's title band (flagged above)

constexpr int kSwatchCount = 15;    // CPW_TC0Button .. CPW_TC14Button

/** CPW_TC<i>Button's <TextColor>, verbatim. */
eqt::ui::UserColor swatchColor(int i);

/** CPW_TC<i>Button's rect, absolute. */
irr::core::rect<irr::s32> swatchRect(const UILayout& layout, int i);

/** channel 0 R, 1 G, 2 B: the slider (CPW_<C>Slider) and its Editbox. */
irr::core::rect<irr::s32> sliderRect(const UILayout& layout, int channel);
irr::core::rect<irr::s32> editRect(const UILayout& layout, int channel);

irr::core::rect<irr::s32> previewRect(const UILayout& layout);  // CPW_TC15Button
irr::core::rect<irr::s32> acceptRect(const UILayout& layout);   // CPW_Accept_Button
irr::core::rect<irr::s32> windowRect(const UILayout& layout);
irr::core::rect<irr::s32> closeBoxRect(const UILayout& layout);

/** The 0..255 value a click at `mouseX` on slider `channel` selects. */
int sliderValueAt(const UILayout& layout, int channel, int mouseX);

}  // namespace ColorPickerLayout

struct ColorPickerState {
    bool isOpen = false;
    int row = 0;                  // the [TextColors] row being edited, 1-based
    eqt::ui::UserColor working;   // the colour the sliders show
};

/** Open on row `n` (1-based) of `table`, starting from its current colour. */
void openColorPicker(ColorPickerState& picker, const eqt::ui::ChatTextColors& table, int n);

/** Is the point inside the open picker? */
bool colorPickerContains(const UILayout& layout, const ColorPickerState& picker, int x, int y);

/**
 * A left click inside the open picker. Returns true when Accept wrote the
 * working colour into `table` (the row changed); the window then closes.
 */
bool clickColorPicker(const UILayout& layout, ColorPickerState& picker,
                      eqt::ui::ChatTextColors& table, int x, int y);

void renderColorPicker(UIRenderer& ui, const UILayout& layout, const ColorPickerState& picker);

}  // namespace Graphics
}  // namespace EQT
