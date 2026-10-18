#pragma once

// B888-CC3 (GitLab #204): the Explore mode's STATE MACHINE -- device-free,
// render-thread side, the same layering as char_select_screen.h. Explore is a
// SELECT-SCREEN mode (the card's correction of 2026-10-13): the 3D backdrop
// becomes a free camera and the account's characters can be selected by
// clicking their models. It is NOT character creation.
//
// The flow, MEASURED on the official client (refactor/B888-CC3/README.md +
// explore.tsv, the lane-5 B1048 runs oracle-a6 / oracle-b1; the backdrop's
// scene file -- the `clz` zone, shipped as clz.eqg, the client-internal world
// initialized before the char-select UI opens -- measured this run off the
// client's OWN load log, oracle-c1/client_dbg.txt, "World initialized: clz"):
//
//   Explore button    CSW_Explore_Button (the select window's, now LIVE -- no
//                     longer the greyed T363 control): opens Explore. The
//                     EXW_ExploreModeWindow overlay appears (the char info
//                     line top-left, the instructions lower-left); the left
//                     character LIST window hides; the account's characters
//                     stand in the 3D scene (the CC-3 backdrop).
//   A click on a      selects the character whose model was clicked. The
//   character model   ledger's A4/A5 could not pin which mesh the click
//                     landed on (fixed root coordinates may have been off the
//                     figure) and EXW_CharInfoLabel stayed on the
//                     login-selected character, so the SELECTION mechanism is
//                     observable but not pinned to a click (Left open). This
//                     unit models the OBSERVED half: the selection cursor
//                     moves to the clicked row and, on Enter, drives world
//                     entry. The 3D click-to-row mapping (a scene raycast) is
//                     the host's seam, not this machine.
//   Enter/Return      OP_EnterWorld for the LIST-SELECTED character --
//                     MEASURED to be the cursor, NOT the login target
//                     (oracle-b1: Testcaster, the login-selected row, entered,
//                     not the logged-in Testchar). The SAME body as the Enter
//                     World button (pressEnterWorld).
//   Esc               closes Explore: the list window returns, a single model
//                     stands on the central platform (the select preview), the
//                     overlay is gone. MEASURED (oracle-a6 A9).
//   WASD              the free camera. Forward moves it (oracle-a6 A6,
//                     full-frame diff 45.48); orbit/zoom were NOT captured on
//                     this rig (A7/A8). The camera's continuous motion is the
//                     host's scene's job (the backdrop scene's free camera);
//                     the state machine's ExploreCameraVerb is the
//                     device-free seam a test drives that motion through.
//
// ASSUMPTIONS AWAITING ORACLE (see the README's "Left open"):
//   * a click that lands on NO model (empty scene): does the selection change?
//     A4/A5's unchanged info line suggests not; a missed mesh cannot be
//     separated from that on this rig.
//   * whether Explore opens with nothing selected (assumed yes: the button is
//     lit on a fresh account, an inert camera over the showroom).
//   * the per-row 3D positions the account's characters stand at (the
//     pedestal + archway layout is the clz geometry's; the placement is
//     assumed, not measured).

#include "client/graphics/ui/char_select_screen.h"

namespace EQT {
namespace Graphics {

// One free-camera verb the backdrop scene applies each frame. Device-free:
// the scene owns the camera, the state machine owns WHICH verb (if any) is
// active. The verbs are the measured one (forward) plus the stock set the
// notes name (reverse/strafe/orbit/zoom -- the last two NOT captured on this
// rig, so a host may implement them as no-ops without breaking parity).
struct ExploreCameraVerb {
    float forward = 0.0f;    // +1 = forward (measured), -1 = reverse
    float strafe = 0.0f;     // +1 = right, -1 = left
    float orbitX = 0.0f;     // mouse-drag orbit, radians (unmeasured on this rig)
    float orbitY = 0.0f;
    float zoom = 0.0f;       // wheel, signed (unmeasured on this rig)
    bool active() const {
        return forward != 0.0f || strafe != 0.0f || orbitX != 0.0f || orbitY != 0.0f || zoom != 0.0f;
    }
};

/** CSW_Explore_Button: open the mode. Greyed and inert while the creation
 *  screen is open (it IS the whole screen) or the delete prompt is up (modal).
 *  Opening changes NO selection and sends NOTHING. */
void enterExplore(CharSelectScreenState& s);

/** Esc: close the mode. The selection is kept (the next Enter World button
 *  press, if the user goes back, uses it); nothing is sent. */
void exitExplore(CharSelectScreenState& s);

/** A click the scene's raycast mapped to row `n` (-1 = nothing hit): the
 *  selection cursor moves to the row. Inert while the screen is blocked or
 *  modal, or when Explore is not open. */
void exploreSelectRow(CharSelectScreenState& s, int n);

/** Enter/Return while Explore is open: the SAME body as the Enter World
 *  button (pressEnterWorld) -- the list-selected character enters (oracle-b1).
 *  Inert outside Explore (applyCharSelectKey's own Enter is the select
 *  screen's). */
CharSelectAction exploreEnterWorld(CharSelectScreenState& s);

/** The held-key verbs of the free camera. The MEASURED verb is forward on
 *  `d` (oracle-a6 A6: holding `d` 8 s moved the free camera, full-frame diff
 *  45.48). The OTHER keys' camera roles were NOT captured on this rig
 *  (README "Not verified"), so they are not mapped here: a verb for an
 *  unmeasured key would be an invented control, and the struct's extra
 *  fields stay zero until an oracle run measures them. The receiver's
 *  isKeyDown() is the frame's held set -- the scene asks each frame, not
 *  once per key event. Outside Explore: all-zero. Escape is NOT a camera
 *  verb -- exitExplore() is its only meaning here. */
ExploreCameraVerb exploreCameraVerb(const CharSelectScreenState& s, bool dHeld);

}  // namespace Graphics
}  // namespace EQT
