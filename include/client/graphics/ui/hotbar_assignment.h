/*
 * B1025-H3 (GitLab #345) -- the hotbar assignment gesture, render-thread side.
 *
 * THE GESTURE IS PRESS-AND-HOLD, THEN CLICK. The official client clones a hot
 * button onto the cursor a bounded fraction of a second after the press
 * (H-O oracle, refactor/B1025-H0/README.md "Measured facts": a 0.117 s gem
 * press released without a clone and cast; a 3.074 s hold cloned by ~1.1 s;
 * the inventory onset is bounded at about 0.72-1.02 s from adjacent 10 fps
 * frames). There is NO drag-and-drop anywhere in fable, and this file adds
 * none: a hold that crosses the threshold with the pointer still on its
 * source puts a HOT BUTTON on the cursor, the source's normal action does not
 * fire, and a SEPARATE click on a hotbar slot places it (HotbarAssignIntent).
 *
 * THE DEFERRAL. fable fires a source's action on the PRESS frame, but the
 * clone appears a frame or more later, so a naive press would cast on the
 * press and clone a frame after -- the opposite of the oracle. So the
 * gesture is split: a press on a holdable source stores its action in a
 * HotButtonHolder (hotbutton_holder.h) instead of pushing it; the next frame
 * the hold timer ticks; crossing the threshold clones AND cancels the
 * pending action; a release over the source fires it (the short-press cast).
 * `stepHotbarGesture` (below) is that one-frame advance, pure and testable.
 *
 * THE STATE IS LOCAL RENDER THREAD. The hot button lives in
 * CursorAttachmentState (cursor_attachment.h), fed by this TU's timer and by
 * the HotbarAssignPending bridge event (the /hotbutton command, eqstr
 * 13278-13283). It crosses to the game thread only as HotbarAssignIntent.
 *
 * THE THRESHOLD. H-O's measured onsets (refactor/B1025-H0/README.md, the
 * oracle's ledger rows) are all 10-fps frame-bounded: a 0.117 s gem press
 * released with no clone, and the gem clone visible by ~1.1 s; the ability
 * and the occupied-slot clones both ~1.0 s; the inventory clone ~0.72-1.02 s.
 * kHotbarHoldThresholdMs = 300 sits INSIDE those bounds (past the 0.117 s
 * short press, well before the 0.72 s earliest measured onset). It is the
 * middle of the bound, not a claimed millisecond -- the oracle's exact value
 * is still open (H-O "Not captured").
 */

#pragma once

#include <functional>

#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/hotbar_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/combat_ability_layout.h"
#include "client/graphics/ui/combat_ability_panel.h"

namespace eqt { namespace bridge { class GameStateBridge; } }

namespace EQT {
namespace Graphics {

class HotButtonHolder;  // hotbutton_holder.h (its own TU, after this one)

/** The hold-to-pickup onset, in milliseconds. See the file header. */
constexpr float kHotbarHoldThresholdMs = 300.0f;

/**
 * What a pointer press is sitting ON, for the hold timer. Filled by the UI
 * press arms (the hit-test they already did), read by the frame's gesture
 * tick. `kind` None = not over a hot-button source (the timer runs down and
 * nothing is created).
 *
 * `id` is the holder's KEY (what the press was on); the `clone*` fields are
 * what the clone carries (for a gem, the SPELL at the gem, not the gem index;
 * for a slot, the slot's own button).
 */
struct HotbarHoldSource {
    enum class Kind : uint8_t { None = 0, Gem, Ability, Item, Slot };
    Kind kind = Kind::None;
    uint32_t id = 0;                 // Gem: gem index; Ability: skill id; Slot: slot index
    eqt::HotbarSlotType cloneType = eqt::HotbarSlotType::Empty;  // what the clone is
    uint32_t cloneId = 0;            // Spell/skill/item id (or the slot's id for a pickup)
    int page = 0;                    // Slot: the hotbar page
    int slot = -1;                   // Slot: the slot index (a pickup clears it)
    std::string name;                // display name (also the emote text for a command)
    uint32_t iconId = 0;
};

inline HotbarHoldSource::Kind holdKind(uint8_t raw) {
    return static_cast<HotbarHoldSource::Kind>(raw);
}
inline uint8_t rawHoldKind(HotbarHoldSource::Kind k) {
    return static_cast<uint8_t>(k);
}

/** The HotbarAssignPending bridge arm: the /hotbutton command's button. */
void applyHotbarAssignPending(CursorAttachmentState& state,
                              const eqt::state::HotbarAssignPendingData& data);

/**
 * The cursor's hot button, drawn at the mouse -- CA_SpellGem's own box, the
 * piece B440 recorded as having no fable producer; this is its producer. An
 * item button reuses its dragitem decal, a spell button the gem name text
 * (the gem bar draws no gem icon texture -- the hotbar's spell slots do the
 * same), and an emote button the command text.
 */
void renderHotbarCursorAttachment(UIRenderer& ui, const UILayout& layout,
                                  const CursorAttachmentState& state);

/**
 * Advance the hold timer by one frame. `heldDown` is the left-button state,
 * `source` what the pointer is over, `deltaTime` the frame's seconds.
 * Crossing the threshold WITH the pointer still on its source clones: the
 * cursor's hot button is set (a held item is displaced -- the oracle's pickup
 * replaces the cursor's contents) and `holdElapsedMs` resets. A press that
 * ends, or the pointer leaving its source, runs the timer back to zero.
 * Returns true on the frame the clone is created.
 */
bool tickHotbarHold(CursorAttachmentState& cursor, bool heldDown,
                    const HotbarHoldSource& source, float deltaTime);

/**
 * The hot-button placement's displacement. H-O "hotbar-swap": placing over
 * an OCCUPIED slot replaces it and leaves the displaced button on the cursor
 * (the only thing on it) -- replacement-with-cursor, not a persisted two-slot
 * swap. The assign intent already carried the destination and the NEW button
 * to the model; this hop keeps the window's own cache and the cursor in step
 * with the result, so neither disagrees with the model for the frames before
 * its HotbarSlotAssigned republish lands: the slot's cache shows `newButton`
 * and the cursor carries `oldButton` (an empty slot's old is nothing -- the
 * clone is simply consumed). `hotbar` and `oldButton` are the slot's state
 * BEFORE the placement (captured by the caller, which does not yet know it);
 * `newButton` is the clone that was on the cursor.
 */
void applyHotbarPlacementDisplacement(HotbarPanelState& hotbar, int slot,
                                      const HotbarSlotState& oldButton,
                                      const eqt::HotButtonCarried& newButton);

/**
 * One frame of the gesture, after tickHotbarHold has run: advance the
 * deferred-action holder. `cursor` is the gesture's home; `holder` carries
 * the press's pending action; `source` the current hover; `buttonDown` the
 * left-button state.
 *
 * A clone on the cursor (the hold crossing the threshold, or /hotbutton)
 * CANCELs the pending action: the press was the clone's, not a click. When
 * that clone was the PICKUP of an occupied slot, its source is cleared too
 * (H-O "hotbar-pickup": "source INI key removed") -- the HotbarClearIntent
 * pushed here is the first production caller of the clear path. A release
 * does NOT clear the clone (H-O "spell-hold": "remained attached to cursor
 * on release"); it is cleared only by a placement (the slot-click arm) or a
 * world click (H-O "hotbar-worlddrop").
 *
 * Otherwise one frame of the holder: a release over the source FIRES the
 * action (the short-press cast); a move off with the button down cancels it.
 *
 * Pure (no device, no layout) -- the frame loop calls it once per frame and
 * the tests drive the whole gesture through it.
 */
void stepHotbarGesture(CursorAttachmentState& cursor, HotButtonHolder& holder,
                       const HotbarHoldSource& source, bool buttonDown,
                       eqt::bridge::GameStateBridge& bridge);

}  // namespace Graphics
}  // namespace EQT
