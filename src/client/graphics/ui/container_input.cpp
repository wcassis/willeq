/*
 * B899 (GitLab #215 / #151 Leg B) -- input for the container windows.
 *
 * StaticUIInput members, in their own TU so static_ui_input.cpp (shared with
 * six sibling units) grows by nothing. Geometry and the hit test live in
 * container_panel.cpp; this file only turns a hit into an intent.
 */

#include "client/graphics/ui/static_ui_input.h"

#include "client/bridge/game_state_bridge.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/container_panel.h"

namespace EQT {
namespace Graphics {

namespace {

// The one close every window kind uses. A world container's close reaches
// the server (OP_ClickObjectAction open=0); a carried bag's is answered on
// the game thread. Either way the window stays up until the game publishes
// TradeskillContainerClosed -- T290's rule.
eqt::events::TradeskillCloseIntent closeIntentFor(const ContainerWindow& w) {
    eqt::events::TradeskillCloseIntent intent{w.isWorldObject ? w.objectId : 0u};
    intent.inventorySlot = w.isWorldObject ? eqt::inventory::SLOT_INVALID
                                           : w.inventorySlot;
    return intent;
}

}  // namespace

bool StaticUIInput::processContainerInput(int mouseX, int mouseY, bool leftClicked,
                                          const UILayout& layout,
                                          ContainerWindowsState& containers) {
    for (auto& w : containers.windows) {
        w.hoveredSlot = -1;
        w.combineHovered = w.doneHovered = w.closeHovered = false;
    }

    const ContainerHit hit = hitTestContainer(layout, containers, mouseX, mouseY);
    if (hit.window < 0) return false;
    ContainerWindow& w = containers.windows[hit.window];

    switch (hit.part) {
        case ContainerPart::Slot:
            w.hoveredSlot = hit.slot;
            // B442 CursorGesture call site -- the ONE place a container cell
            // click goes. The same shared slotClick() the inventory, bank and
            // loot arms use: left = pick up / place / swap, right = item
            // display. The slot is the cell's Titanium wire slot -- a bag
            // sub-slot 251 + (general-22)*10 + n, or a world slot 4000+n
            // (UIGAP-ORACLE 151 gestures.tsv rows 22-23, 215 combine.tsv
            // rows 3 and 17).
            if (leftClicked || rightClicked_) {
                const auto& cell = w.slots[hit.slot];
                eqt::inventory::SlotSnapshot snap;
                snap.occupied = cell.hasItem;
                snap.quantity = cell.quantity;
                slotClick(containerSlotId(w, hit.slot), leftClicked, snap, cell.itemName,
                          cell.iconId);   // B442b: the icon a merchant selection shows
            }
            break;
        case ContainerPart::Combine:
            w.combineHovered = true;
            if (leftClicked && bridge_) {
                // The same intent, and the same container_slot, `/combine`
                // and `/combine <slot>` push (T278).
                bridge_->pushIntent(
                    eqt::events::TradeskillCombineIntent{containerCombineSlot(w)});
            }
            break;
        case ContainerPart::Done:
            w.doneHovered = true;
            if (leftClicked && bridge_) bridge_->pushIntent(closeIntentFor(w));
            break;
        case ContainerPart::CloseBox:
            w.closeHovered = true;
            if (leftClicked && bridge_) bridge_->pushIntent(closeIntentFor(w));
            break;
        case ContainerPart::Body:
        case ContainerPart::None:
            break;
    }
    // The whole window consumes the position, so a click on it does not fall
    // through to the 3D scene.
    return true;
}

bool StaticUIInput::processContainerEscape(ContainerWindowsState& containers, int onlyIndex) {
    // B460: the ESC window stack names the window it chose; -1 = the topmost.
    const int idx = onlyIndex < 0 ? topmostContainerWindow(containers) : onlyIndex;
    if (idx < 0 || idx >= ContainerWindowsState::MAX_WINDOWS || !containers.windows[idx].isOpen) {
        return false;
    }
    if (bridge_) bridge_->pushIntent(closeIntentFor(containers.windows[idx]));
    return true;
}

}  // namespace Graphics
}  // namespace EQT
