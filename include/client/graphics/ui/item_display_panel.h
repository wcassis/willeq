/*
 * The item display window -- renderer-side state and draw (T421, board B298
 * item half).
 *
 * WHAT WAS BROKEN. `TitaniumItemParser` fills a hundred-odd `ItemInstance`
 * fields off the wire and fable drew none of them: there was no item display
 * window in the tree at all, and the inventory popup shows three characters
 * of an item's name in its slot. B298's item census -- `accuracy`,
 * `damageShield`, `dotShield`, `spellShield`, `strikethrough`, `stunResist`,
 * `shielding`, `enduranceRegen`, `hpRegen`, `manaRegen`, `delay`,
 * `recLevel`, `maxCharges`, `lore` and the rest -- is the list of stats this
 * window is the home for.
 *
 * Same static-panel idiom as T290's loot window, T393's purse and T415's
 * tradeskill panel: a plain state struct plus free functions, so a test
 * drives the REAL publisher's data struct through the REAL applier with no
 * Irrlicht device.
 *
 * WHERE THE TEXT COMES FROM. Not from here. The game thread composes it in
 * `eqt::inventory::BuildItemDisplayText()` and ships the finished lines on
 * the `ItemDisplayShown` event, because the official client composes the
 * same free text from its own shipped string table (eqstr_us.txt) and
 * EQUI_ItemDisplay.xml's whole body is one `<STMLbox>`. This file is the box.
 *
 * The geometry is NOT here either -- every rect comes from
 * include/client/graphics/ui/item_display_layout.h, which quotes the XML and
 * writes out the anchor arithmetic.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct ItemDisplayShownData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

struct ItemDisplayPanelState {
    bool isOpen = false;

    uint32_t itemId = 0;
    std::string name;                 // -> the Style_Titlebar row
    uint32_t iconId = 0;              // -> IDW_IconButton's decal
    std::vector<std::string> lines;   // -> IDW_ItemDescription

    // `Style_VScroll true` on the STMLbox: a long item overflows the box.
    // Measured from the TOP, the SkillsPopupState convention.
    int scrollOffset = 0;

    bool closeHovered = false;

    // B442b (#151 rows 24-26). The official client shows the display opened
    // by a RIGHT press only WHILE the button is held and keeps the one opened
    // by ALT+LEFT (row 26) until Escape (row 27). The request crosses to the
    // game thread and the contents come back a frame or two later, so a
    // release can come before them: `heldReleasedEarly` makes that late
    // ItemDisplayShown open nothing (row 24: a press+release leaves nothing).
    bool heldOnly = false;            // the open window goes with the right button
    bool heldRequestPending = false;  // a right-press request has not come back yet
    bool heldReleasedEarly = false;   // ...and the button is already up
    int heldReleaseAgeFrames = 0;     // a refused request never comes back: forget it

    /** Highest legal `scrollOffset`, the LootPanelState::maxScrollOffset shape. */
    int maxScrollOffset(const UILayout& layout) const;

    /** How many lines are actually drawn this frame. */
    int visibleLines(const UILayout& layout) const;
};

/**
 * The ItemDisplayShown bridge arm, as a free function (T290's idiom).
 * Opens the window and replaces its contents -- a second request while it is
 * up retargets it rather than stacking, which is what the official
 * single-instance ItemDisplayWindow does.
 */
void applyItemDisplayShown(ItemDisplayPanelState& state,
                           const eqt::state::ItemDisplayShownData& data);

/**
 * B442b: a display was just requested -- `held` for a RIGHT press (row 25),
 * false for ALT+LEFT (row 26), which also cancels any pending held request.
 */
void noteItemDisplayRequest(ItemDisplayPanelState& state, bool held);

/**
 * B442b: once per frame with the right button's state. A held display closes
 * when the button is up; a held request still in flight is marked so its
 * contents open nothing when they arrive.
 */
void updateHeldItemDisplay(ItemDisplayPanelState& state, bool rightButtonDown);

/** Frames a released held request is remembered before it is forgotten. */
constexpr int kHeldDisplayForgetFrames = 60;

/** Draw the window. Only when state.isOpen. */
void renderItemDisplayPopup(UIRenderer& ui, const UILayout& layout,
                            const ItemDisplayPanelState& state);

/**
 * Hit-test Style_Closebox. False on a miss and on a CLOSED window -- a
 * hidden panel must not eat clicks (T290's rule).
 */
bool hitTestItemDisplayCloseBox(const UILayout& layout,
                                const ItemDisplayPanelState& state,
                                int mouseX, int mouseY);

/** Is the point anywhere inside the open window? (Click consumption.) */
bool itemDisplayContains(const UILayout& layout,
                         const ItemDisplayPanelState& state,
                         int mouseX, int mouseY);

} // namespace Graphics
} // namespace EQT
