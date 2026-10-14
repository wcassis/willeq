/*
 * The hotbar window -- one stock Titanium HotButtonWnd (B1025-H2, #345).
 *
 * A separate TU for the same reason combat_ability_panel.cpp is: the
 * renderer body the old renderHotbar() (static_panels.cpp) is replaced by.
 * Nothing here computes a rect -- the geometry is HotbarLayout, which quotes
 * EQUI_HotButtonWnd.xml (hotbar_layout.h), and the layout member
 * layout.hotbar is the window rect for this screen (ui_layout.h).
 *
 * THE WINDOW. The stock client cannot show Hotbars 2-4 (the oracle gives
 * HotButtonWnd2..4 zero size; H-O measured them invisible even forced on),
 * so this is ONE window, ten pages, ten buttons -- the model's own shape
 * (1x10x10). The oracle shows the window by default and offers no UI to
 * hide it (the stock ini files carry no [HotButtonWnd] Show=), so isOpen
 * defaults to true and is NOT one of the per-character preferences (only
 * user-changeable things are persisted, T432); the close box toggles the
 * in-session flag.
 *
 * SLOT ART, per kind (H-O's looks):
 *  - Item: the dragitem sheets' own icon when the slot carries a valid icon
 *    id (UIRenderer::drawItemIcon, ids 500..1723) -- the oracle's A_DragItem
 *    button decal, 40x40. A slot with no drawable icon falls back to its
 *    name, as every other panel does.
 *  - Spell: its name. Measured: the spell-icon texture the XML's SpellGem
 *    holder refers to is NOT in the shipped UI atlas (the pack carries no
 *    spell-icon entry; the gem bar's "icons" are the same text placeholder),
 *    so a spell slot cannot show real art today -- text is the only truthful
 *    thing it can draw. See refactor/B1025-H2/README.md (FOLLOW-UP: a
 *    spell-icon source) before reaching for a new atlas.
 *  - Skill / Emote / Macro / Combat: the slot's name text (emote/macro carry
 *    their own text; the model resolves skill names).
 *
 * The draw command list here is PINNED by tests/test_static_ui_frame_cost.cpp
 * (the B506 pins, re-taken for this window). The function name
 * renderHotbar() is the one the frame-order golden
 * tests/goldens/frame_render_order_g05.txt records, so it is kept.
 */

#pragma once

#include <irrlicht.h>

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

/**
 * The hotbar window's hit target, for input. None = the point is not over any
 * control of the window (not even the window's own frame -- the background is
 * not clickable, only the controls are). Slot carries the 0..9 slot index;
 * the rest are the three page-row controls.
 */
enum class HotbarHit {
    None = 0,
    Slot,
    PageLeft,
    PageRight,
    Close,
    // Over the window's frame (the 2x5 grid's gaps, the page row's dead
    // space): the point is consumed, nothing happens.
    Window,
};

/**
 * Hit-test the hotbar window. The window rect is layout.hotbar (the oracle's
 * rect for this screen); the controls are HotbarLayout's, so the renderer and
 * this test share one rect source -- the same rule T277 set for the buff bar.
 */
HotbarHit hitTestHotbar(const UILayout& layout, HotbarPanelState& state,
                        int x, int y);

/**
 * Render the hotbar window: frame, page row (left / label / right), the 2x5
 * grid of ten 40x40 slots with per-kind art and the cooldown overlay, and the
 * close box. No-op while the window is closed.
 */
void renderHotbar(UIRenderer& ui, const UILayout& layout,
                  const HotbarPanelState& state);

}  // namespace Graphics
}  // namespace EQT
