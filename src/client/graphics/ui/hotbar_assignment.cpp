// B1025-H3 (GitLab #345) -- the hotbar assignment gesture, render-thread side.
// Its own TU (M00); the declarations and the H-O citations live in
// hotbar_assignment.h.

#include "client/graphics/ui/hotbar_assignment.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/cursor_attachment_layout.h"
#include "client/graphics/ui/hotbutton_holder.h"  // B1025-H3: the deferred action's full type
#include "client/graphics/ui/ui_renderer.h"
#include "client/bridge/game_state_bridge.h"
#include "client/hotbar/hotbar_slot_type.h"

#include <string>

namespace EQT {
namespace Graphics {

void applyHotbarAssignPending(CursorAttachmentState& state,
                              const eqt::state::HotbarAssignPendingData& data) {
    // The /hotbutton command is the game thread's one hot-button producer:
    // a social button (eqstr 13278-13283) that lands on the cursor and waits
    // for a click on a hotbar slot. Replaces whatever the cursor was
    // carrying -- the command's own button is the newest thing on the cursor.
    state.hasHotbutton = true;
    state.hotbutton = eqt::HotButtonCarried{data.type, data.id, data.text,
                                             data.iconId};
}

void renderHotbarCursorAttachment(UIRenderer& ui, const UILayout& layout,
                                  const CursorAttachmentState& state) {
    if (!state.hasHotbutton) return;
    // CA_SpellGem's box: EQUI_CursorAttachment.xml's (5,5) 40x32 piece.
    // cursor_attachment_layout.h's ICON_* are the shared 40x40 CA_Anim box at
    // the same (5,5) origin -- the one rect that is NOT a second copy,
    // because the two pieces sit at identical coordinates in the XML.
    const auto icon =
        CursorAttachmentLayout::iconRect(layout, state.mouseX, state.mouseY);
    const auto& btn = state.hotbutton;
    if (btn.iconId != 0 && btn.type == eqt::HotbarSlotType::Item) {
        // The item button reuses the held-item decal path (T430's reuse,
        // not a second copy of the art).
        if (ui.drawItemIcon(icon, btn.iconId)) return;
    }
    // Spell and emote buttons draw their name text: the gem bar draws no gem
    // icon texture (renderSpellGemPanel's convention -- name text only), so
    // the button carries the same.
    auto* tb = ui.getTextBatch();
    if (!tb) return;
    std::string label = btn.text.empty() ? std::string() : btn.text;
    if (label.size() > 10) label = label.substr(0, 10);
    if (label.empty() && btn.iconId != 0) {
        tb->addTextCentered(std::to_string(btn.iconId), icon,
                            irr::video::SColor(255, 235, 235, 235));
        return;
    }
    if (label.empty()) return;  // nothing to say, nothing to draw
    tb->addTextCentered(label, icon, irr::video::SColor(255, 235, 235, 235));
}

bool tickHotbarHold(CursorAttachmentState& cursor, bool heldDown,
                    const HotbarHoldSource& source, float deltaTime) {
    if (!heldDown) {
        cursor.holdElapsedMs = 0.0f;
        return false;
    }
    if (cursor.hasHotbutton) {
        // The button is already cloned; the press is the button's, not the
        // source's, so the timer is done.
        cursor.holdElapsedMs = 0.0f;
        return false;
    }
    if (source.kind == HotbarHoldSource::Kind::None) {
        cursor.holdElapsedMs = 0.0f;
        return false;
    }
    // A retarget to a REAL source (a move from one pressable onto another)
    // restarts the accumulator: time on one source is not credited to another
    // (the press frame is spent in transit, not on the destination). Two
    // GEMS are different sources even though their kind agrees -- so a
    // retarget is a (kind, id) change coming from a kind that is not `None`.
    // The press itself (None -> source) is NOT a retarget: the press frame
    // counts, because H-O's onsets are measured from the press. A release
    // before the threshold fires the deferred action on its own release
    // frame (the holder handles that).
    const uint8_t newKind = rawHoldKind(source.kind);
    const bool retargeting =
        (cursor.holdSourceKind != newKind || cursor.holdSourceId != source.id) &&
        cursor.holdSourceKind != rawHoldKind(HotbarHoldSource::Kind::None);
    cursor.holdSourceKind = newKind;
    cursor.holdSourceId = source.id;
    if (retargeting) {
        cursor.holdElapsedMs = 0.0f;
        return false;
    }
    cursor.holdElapsedMs += deltaTime * 1000.0f;
    if (cursor.holdElapsedMs >= kHotbarHoldThresholdMs) {
        // THE GESTURE: the clone appears. It carries the source's kind and id
        // (for a gem, the spell at the gem; for a slot, the slot's own
        // button). A held item is displaced -- the oracle's pickup replaces
        // the cursor's contents; the item returns to its slot when the press
        // resolves (the hot button and the item do not share a cursor).
        cursor.hasHotbutton = true;
        cursor.hotbutton = eqt::HotButtonCarried{
            source.cloneType == eqt::HotbarSlotType::Empty
                ? (source.kind == HotbarHoldSource::Kind::Gem
                       ? eqt::HotbarSlotType::Spell
                       : eqt::HotbarSlotType::Combat)
                : source.cloneType,
            source.cloneId, source.name, source.iconId};
        cursor.hasItem = false;
        cursor.itemId = 0;
        cursor.itemName.clear();
        cursor.iconId = 0;
        cursor.quantity = 0;
        cursor.holdElapsedMs = 0.0f;
        return true;
    }
    return false;
}

void applyHotbarPlacementDisplacement(HotbarPanelState& hotbar, int slot,
                                      const HotbarSlotState& oldButton,
                                      const eqt::HotButtonCarried& newButton) {
    if (slot < 0 || slot >= HotbarPanelState::SLOT_COUNT) return;
    // The slot's display now shows the NEW button (the model's republish
    // follows; the name the model assigns there is the display name the cache
    // must show).
    auto& s = hotbar.slots[slot];
    s.type = newButton.type;
    s.id = newButton.id;
    s.emoteText = newButton.text;
    s.iconId = newButton.iconId;
    s.name = newButton.text.empty() ? oldButton.name
                                    : newButton.text.substr(0, 10);
    s.cooldownEndTime = std::chrono::steady_clock::time_point{};
    s.cooldownDurationMs = 0;
}

void stepHotbarGesture(CursorAttachmentState& cursor, HotButtonHolder& holder,
                       const HotbarHoldSource& source, bool buttonDown,
                       eqt::bridge::GameStateBridge& bridge) {
    // A clone is on the cursor: the press was the clone's, not a click, so
    // whatever the press deferred is dropped. When that clone was the
    // PICKUP of an occupied slot (a press that began on a slot), the slot's
    // own content is gone with the button -- H-O "hotbar-pickup": "source
    // INI key removed". The press's pending action (the slot's activate) is
    // the press itself; a clone appearing cancels it AND clears the source.
    if (cursor.hasHotbutton) {
        if (holder.hasPending()) {
            const auto& ps = holder.pendingSource();
            if (ps.kind == HotbarHoldSource::Kind::Slot) {
                bridge.pushIntent(eqt::events::HotbarClearIntent{
                    ps.page, ps.slot});
            }
            holder.cancel();
        }
        return;
    }
    // One frame of the deferred action: a release over the source fires it
    // (the short-press cast); a move off with the button down cancels it.
    holder.step(source, buttonDown, bridge);
}

}  // namespace Graphics
}  // namespace EQT
