/*
 * T436 (board B429) -- the cursor attachment.
 *
 * Its own TU (M00). The geometry is in
 * include/client/graphics/ui/cursor_attachment_layout.h, which quotes
 * EQUI_CursorAttachment.xml; this file computes no rect.
 */

#include "client/graphics/ui/cursor_attachment.h"

#include "client/graphics/ui/cursor_attachment_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"

#include <algorithm>
#include <string>

namespace EQT {
namespace Graphics {

bool applyCursorSlotChanged(CursorAttachmentState& state,
                            const eqt::state::InventorySlotChangedData& data) {
    // SERVER_CURSOR_SLOT (33) as well as CURSOR_SLOT (30): the wire uses one
    // and the client model the other, and InventoryManager::getItem() treats
    // them as the same place.
    if (data.slotId != eqt::inventory::CURSOR_SLOT &&
        data.slotId != eqt::inventory::SERVER_CURSOR_SLOT) {
        return false;
    }
    state.hasItem = data.hasItem;
    state.itemId = data.hasItem ? data.itemId : 0;
    state.itemName = data.hasItem ? data.itemName : std::string();
    state.iconId = data.hasItem ? data.iconId : 0;
    state.quantity = data.hasItem ? static_cast<int32_t>(data.quantity) : 0;
    return true;
}

void applyCursorCoinChanged(CursorAttachmentState& state,
                            const eqt::state::CursorCoinChangedData& data) {
    state.coinType = data.amount > 0 ? data.coinType : 0;
    state.coinAmount = data.amount;
}

// B442b (#151 row 29): the coin a held pile is drawn with. MEASURED for
// platinum: the oracle's cursor coin in run1.G33a matches dragitem cell 644
// pixel for pixel (MSE 195 at a 1px offset; the next-best cell scores 7,546).
// 645/646/647 are the three cells under it in dragitem5.tga's first column --
// gold, silver and copper coin art, in that order -- an INFERENCE from the
// sheet, owed an oracle grab of each denomination on the cursor.
uint32_t cursorCoinIcon(uint8_t coinType) {
    constexpr uint32_t kPlatinumIcon = 644;
    return kPlatinumIcon + static_cast<uint32_t>(3 - std::min<uint8_t>(coinType, 3));
}

void renderCursorAttachment(UIRenderer& ui, const UILayout& layout,
                            const CursorAttachmentState& state) {
    if (!state.hasItem && state.holdsCoin()) {
        const auto icon =
            CursorAttachmentLayout::iconRect(layout, state.mouseX, state.mouseY);
        const auto strip =
            CursorAttachmentLayout::coinCountRect(layout, state.mouseX, state.mouseY);
        ui.drawItemIcon(icon, cursorCoinIcon(state.coinType));
        ui.drawRect(strip, irr::video::SColor(255, 64, 64, 64));
        if (auto* tb = ui.getTextBatch()) {
            tb->addTextCentered(std::to_string(state.coinAmount), strip,
                                irr::video::SColor(255, 230, 230, 230));
        }
        return;
    }
    if (!state.hasItem) return;

    // Style_Transparent true, Style_Border false: the oracle draws NO frame
    // and NO background here, only the decal. So neither does fable -- an
    // opaque plate under the pointer would be a fable invention.
    const auto icon =
        CursorAttachmentLayout::iconRect(layout, state.mouseX, state.mouseY);
    if (ui.drawItemIcon(icon, state.iconId)) return;

    // The no-art fallback every icon site keeps since T430: a client with no
    // pack still shows SOMETHING is held rather than nothing at all.
    auto* tb = ui.getTextBatch();
    if (!tb) return;
    tb->addTextCentered(
        state.itemName.substr(0, std::min<size_t>(3, state.itemName.size())),
        icon, irr::video::SColor(255, 235, 235, 235));
}

}  // namespace Graphics
}  // namespace EQT
