// B888-CC3 (GitLab #204) -- the Explore mode's state machine. Its own TU (M00).
// The flow and its oracle citations: include/client/graphics/ui/explore_screen.h.

#include "client/graphics/ui/explore_screen.h"

#include <algorithm>

namespace EQT {
namespace Graphics {

namespace {

// The mode is a pure SELECT-screen thing: it cannot be opened over the
// creation screen (which IS the whole screen) or under the delete prompt
// (which is modal).
bool canBeExplore(const CharSelectScreenState& s) {
    return !s.create.isOpen && !s.confirm.isOpen;
}

}  // namespace

void enterExplore(CharSelectScreenState& s) {
    if (charSelectBlocked(s) || !canBeExplore(s)) return;
    s.exploreOpen = true;
}

void exitExplore(CharSelectScreenState& s) {
    // Esc closes it (MEASURED, oracle-a6 A9); a request in flight or a modal
    // prompt also ends the mode (the screen cannot host both). The selection
    // is kept.
    s.exploreOpen = false;
}

void exploreSelectRow(CharSelectScreenState& s, int n) {
    if (!s.exploreOpen || charSelectBlocked(s) || !canBeExplore(s)) return;
    if (n < 0 || n >= static_cast<int>(s.rows.size())) return;   // a miss: keep the cursor
    s.selected = n;
}

CharSelectAction exploreEnterWorld(CharSelectScreenState& s) {
    if (!s.exploreOpen) return {};
    return pressEnterWorld(s);   // the list-selected character enters (oracle-b1)
}

ExploreCameraVerb exploreCameraVerb(const CharSelectScreenState& s, bool dHeld) {
    ExploreCameraVerb v;
    if (!s.exploreOpen || charSelectBlocked(s)) return v;   // camera holds
    // The ONLY measured verb: holding `d` moves the free camera forward
    // (oracle-a6 A6, diff 45.48). The other camera keys' roles were not
    // captured on this rig (README "Not verified"), so they are left
    // UNMAPPED rather than guessed -- the struct's strafe/orbit/zoom fields
    // are the seam a measured gesture fills in.
    if (dHeld) v.forward = 1.0f;
    return v;
}

}  // namespace Graphics
}  // namespace EQT
