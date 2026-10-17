/*
 * B1025-H3 (GitLab #345) -- the deferred hot-button source action.
 *
 * THE PROBLEM. fable fires a UI source's action on the PRESS frame (the
 * receiver latches wasLeftButtonClicked and the press frame acts on it). The
 * oracle's gesture is PRESS-AND-HOLD: the clone appears a bounded fraction of
 * a second AFTER mouse-down (H-O, refactor/B1025-H0/README.md: a 3.286 s hold
 * on an occupied hotbar slot cloned shortly after mouse-down; a 0.117 s press
 * released and cast with no clone). If the source's normal action fired on
 * the press, a hold would ACTIVATE on the press and clone a frame later -- the
 * opposite of the oracle.
 *
 * THE MECHANISM. A press on a holdable hotbar slot stores its action (the
 * slot's activate) HERE instead of pushing it, remembering the source it began
 * on. Each later frame the gesture tick hands it the CURRENT hover; it keeps
 * the action pending while the button is still down over the source, fires it
 * when the button releases over the source (the short-press activate), and
 * cancels it when the pointer leaves the source. The frame that crosses the
 * hold threshold clones the source onto the cursor and cancels this pending
 * action (the press was the clone's, not a click).
 *
 * A production caller runs the gesture (cursor bound, holder armed); a test
 * caller bypasses it (a test's press-and-release in one call fires the slot's
 * action on the press, exactly as the pre-H3 tests expect).
 */

#pragma once

#include <functional>

#include "client/bridge/game_state_bridge.h"
#include "client/graphics/ui/hotbar_assignment.h"

namespace EQT {
namespace Graphics {

/** One pending source action, held from a press for the hold timer. */
class HotButtonHolder {
public:
    HotButtonHolder() = default;

    bool hasPending() const { return has_; }
    /** The source the pending action began on. */
    HotbarHoldSource pendingSource() const { return source_; }

    /**
     * Store a press's action (the button is still down). `source` is what the
     * press is on; `push` builds the intent (the slot's activate) and hands
     * it to the bridge. Replaces any stale pending.
     */
    void begin(const HotbarHoldSource& source,
               std::function<void(eqt::bridge::GameStateBridge&)> push) {
        has_ = true;
        source_ = source;
        push_ = std::move(push);
    }

    /** Drop the pending action without firing it (a move-off or a clone). */
    void cancel() { has_ = false; }

    /**
     * One frame: `source` the pointer is over NOW, `buttonDown` the
     * left-button state NOW. The action was stored with the button down, so:
     * still on its source with the button down keeps it pending; the button
     * releasing over its source fires it (the short-press activate); the
     * pointer leaving its source cancels it. Returns true when it fired.
     */
    bool step(const HotbarHoldSource& source, bool buttonDown,
              eqt::bridge::GameStateBridge& bridge) {
        if (!has_) return false;
        const bool onSource = source.kind != HotbarHoldSource::Kind::None &&
                              source.kind == source_.kind &&
                              source.id == source_.id;
        if (!onSource) {  // moved off (or released elsewhere)
            has_ = false;
            return false;
        }
        if (buttonDown) return false;  // still holding
        push_(bridge);
        has_ = false;
        return true;
    }

private:
    bool has_ = false;
    HotbarHoldSource source_;
    std::function<void(eqt::bridge::GameStateBridge&)> push_;
};

}  // namespace Graphics
}  // namespace EQT
