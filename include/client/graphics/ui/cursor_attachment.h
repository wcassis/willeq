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
#include "client/hotbar/hotbar_slot_type.h"

namespace eqt { namespace state {
struct InventorySlotChangedData;
struct CursorCoinChangedData;
struct HotbarAssignPendingData;
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

    // B442b (#151 row 29): held COIN, fed by GameEventType::CursorCoinChanged
    // (the game thread's one cursor-coin publisher). EQT::COINTYPE_* and the
    // amount; amount 0 = no coin. Unlike an item's stack, a coin's count IS
    // drawn: the oracle puts it in a strip under the decal (measured,
    // CursorAttachmentLayout::coinCountRect). Also what a purse / bank coin
    // row reads to decide "put the held coin back" -- the render thread
    // keeps no shadow of its own (B442's coinHeld_ is gone).
    uint8_t coinType = 0;
    uint32_t coinAmount = 0;

    bool holdsCoin() const { return coinAmount > 0; }

    // B1025-H3 (#345): the HOT BUTTON the cursor carries while a hotbar
    // assignment is in flight -- a held spell gem, a combat-ability row, an
    // inventory item or the /hotbutton command's social button. It is LOCAL
    // render state: the render thread's hold timer sets it, the game
    // thread's HotbarAssignPending event (the /hotbutton command) sets it,
    // and it crosses back to the game thread only as HotbarAssignIntent when
    // it is placed. It is cleared locally (placement, world drop, being
    // replaced by an item pick-up) -- the game thread's model never knows it
    // exists, so there is no "cleared" event.
    bool hasHotbutton = false;
    eqt::HotButtonCarried hotbutton;

    void clearHotbutton() {
        hasHotbutton = false;
        hotbutton = eqt::HotButtonCarried{};
    }

    // B1025-H3 (#345): the hold timer's frame-local state, read and written
    // by tickHotbarHold (hotbar_assignment.h). `holdElapsedMs` accumulates
    // the pointer's time on its source; the holdSource* fields are what that
    // source is, so a move off it (or onto a different source) resets the
    // accumulator rather than crediting one source's time to another.
    float holdElapsedMs = 0.0f;
    uint8_t holdSourceKind = 0;
    uint32_t holdSourceId = 0;

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

/** B442b: the dragitem icon a held coin of `coinType` is drawn with. */
uint32_t cursorCoinIcon(uint8_t coinType);

/** B442b: the CursorCoinChanged bridge arm. */
void applyCursorCoinChanged(CursorAttachmentState& state,
                            const eqt::state::CursorCoinChangedData& data);

/**
 * Draw CA_Anim at the mouse: the held item's icon, or -- with no item held --
 * the held coin's decal and its count strip (B442b, row 29).
 */
void renderCursorAttachment(UIRenderer& ui, const UILayout& layout,
                            const CursorAttachmentState& state);

}  // namespace Graphics
}  // namespace EQT
