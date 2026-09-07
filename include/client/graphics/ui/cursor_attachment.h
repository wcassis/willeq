/*
 * The cursor attachment -- the item held on the cursor, drawn at the mouse
 * (T436, board B429).
 *
 * WHY IT MATTERS. This is what makes a pick-up or a drop VISIBLE at all: with
 * no attachment the item vanishes from its slot and reappears somewhere else,
 * and everything in between is invisible. fable has had a cursor MODEL since
 * the beginning -- `InventoryManager::cursorQueue_`, reached as
 * `getItem(eqt::inventory::CURSOR_SLOT)` (slot 30) -- and has never drawn it.
 *
 * WHAT FEEDS IT. The same enriched `InventorySlotChanged` event every other
 * slot uses, for slot id 30. Until T436 the bridge's arm covered 0..29 and
 * the WORLD container range and dropped everything else on the floor, so the
 * cursor slot's event -- carrying the icon id B429 is about -- crossed the
 * bridge and went nowhere. `CursorItemChangedData` is NOT the feed: T413
 * (board B296) reduced it to a single `hasCursorItem` bool and pinned that
 * with a `sizeof` test, and it carries no icon. It stays what it is.
 *
 * The geometry is in include/client/graphics/ui/cursor_attachment_layout.h,
 * which quotes EQUI_CursorAttachment.xml.
 */

#pragma once

#include <cstdint>
#include <string>

#include "client/graphics/ui/ui_layout.h"

namespace eqt { namespace state {
struct InventorySlotChangedData;
} }

namespace EQT {
namespace Graphics {

class UIRenderer;

struct CursorAttachmentState {
    bool hasItem = false;
    uint32_t itemId = 0;
    std::string itemName;
    uint32_t iconId = 0;     // -> CA_Anim's 40x40 decal
    int32_t quantity = 0;    // held, NOT drawn -- the oracle has no count label

    // Where the pointer was on the frame this was last drawn. Written once
    // per frame by FrameComponent::renderStaticUI() from the event receiver,
    // so the draw needs no device of its own.
    int mouseX = 0;
    int mouseY = 0;
};

/**
 * The cursor arm of the InventorySlotChanged bridge case. Returns false and
 * changes nothing for any slot that is not the cursor, so the caller can hand
 * it every event the way applyTradeskillSlotChanged() is handed every event.
 */
bool applyCursorSlotChanged(CursorAttachmentState& state,
                            const eqt::state::InventorySlotChangedData& data);

/** Draw CA_Anim at the mouse. Only when state.hasItem. */
void renderCursorAttachment(UIRenderer& ui, const UILayout& layout,
                            const CursorAttachmentState& state);

}  // namespace Graphics
}  // namespace EQT
