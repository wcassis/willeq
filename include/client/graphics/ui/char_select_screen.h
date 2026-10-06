#pragma once

// B998 (GitLab #318, #204 Legs S/M): the Character Select screen's STATE
// MACHINE -- device-free, render-thread side. Geometry: char_select_layout.h;
// draw and click resolution: char_select_panel.h; the thread seam and the
// rows' text: login_world/character_select.h; the host that runs it before a
// zone exists: char_select_host.h.
//
// The flow, MEASURED on the official client (review/UIGAP-ORACLE/318/, its
// README's rule numbers; earlier frames: eq-oracle/logs/shots/cc_13_delete_confirm.png,
// review/UIGAP-ORACLE/204/00_character_select.png):
//
//   Char<n> button    the list in OP_SendCharInfo order, eight rows (rule 2);
//                     an occupied row shows the NAME and selects it (yellow
//                     text), sending nothing; the selected character's
//                     "Name [level Class]" and zone line float above it
//                     (rule 3). An empty row reads eqstr 1947 "Create a New
//                     Character" and ONE click on it opens the Character
//                     Creation screen (B888's, hosted here; rule 5).
//   The selection     the FIRST row on entering the screen, after Back from
//                     creation and after a delete (rule 4).
//   Delete            acts on the selected row: a modal "Are you absolutely
//                     sure you want to delete Testmelee?" (eqstr 9065) with
//                     Yes / No; while it is up every button but Quit is
//                     greyed (rule 6, still 10). It CLOSES ITSELF after
//                     ~10 s (10.2 / 9.9 / 9.9 / 10.2 s) and a timed-out
//                     prompt deletes nothing (rule 7). Yes sends
//                     OP_DeleteCharacter, world answers with a new list
//                     (rule 8).
//   Enter World       OP_EnterWorld for the selected character (rule 9).
//   Quit              ends the run (not exercised on the oracle).
//   Creation          Back returns here; a successful create goes STRAIGHT
//                     INTO THE WORLD (review/UIGAP-ORACLE/204/14) -- the game
//                     thread does that half (LoginWorldComponent).
//
// ASSUMPTIONS AWAITING ORACLE:
//   * Enter World and Delete with nothing selected (an empty account) are
//     greyed and inert;
//   * the prompt: a click outside it, and Quit while it is up, do nothing;
//     it is drawn where B442b's dialog is (centred) -- the oracle's sits right
//     of centre (552..820 x 352..547 at 1024x768);
//   * a double click on a character (the rig's cc_17_dbl.png) is not wired;
//   * the name field: 15 characters, letters after the first lower-cased
//     (both MEASURED, review/UIGAP-ORACLE/204/10); Return does nothing.

#include "client/graphics/ui/char_create_screen.h"
#include "client/graphics/ui/confirmation_dialog.h"
#include "client/login_world/character_select.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace EQT {
namespace Graphics {

struct CharSelectScreenState {
    static constexpr float kDeleteConfirmSeconds = 10.0f;
    static constexpr size_t kNameMaxChars = 15;

    std::vector<eqt::login_world::CharacterSelectRow> rows;   // the newest list
    int selected = -1;                                         // index into rows
    std::shared_ptr<const eqt::login_world::EqstrTable> strings;

    ConfirmationDialogState confirm;   // the delete prompt (its own clock, kDeleteConfirmSeconds)
    std::string confirmName;

    CharCreateScreenState create;      // B888's two pages, hosted here

    // The request this screen is waiting on. A view whose requestsHandled has
    // caught up with `posted` and that is no longer busy is its answer.
    enum class Awaiting : uint8_t { None, EnterWorld, Delete, Create, GetName };
    Awaiting awaiting = Awaiting::None;
    uint32_t posted = 0;
};

/** What the host must do after an input. */
struct CharSelectAction {
    enum class Kind : uint8_t { None, Post, Quit };
    Kind kind = Kind::None;
    eqt::login_world::CharacterSelectRequest request;   // Post
};

/** Take a new view: the rows (selection kept by name), the strings, and the
 *  answer to the request in flight. */
void applyCharacterSelectView(CharSelectScreenState& s, const eqt::login_world::CharacterSelectView& v);

/** eqstr text from the view's table ("" when it is not there). */
std::string charSelectEqstr(const CharSelectScreenState& s, uint32_t id);

/** The row button `n` (0..7) shows, or -1 for "Create a New Character". */
int charSelectRowForButton(const CharSelectScreenState& s, int n);

/** True while input is ignored: a request is in flight, or the game is entering world. */
bool charSelectBlocked(const CharSelectScreenState& s);

/** CSW_Char<n+1>_Button: select a character, or open creation (`seed`: its random pick). */
CharSelectAction pressCharButton(CharSelectScreenState& s, int n,
                                 const eqt::charcreate::CharCreateData& data, uint32_t seed);
/** CSW_Enter_World_Button. */
CharSelectAction pressEnterWorld(CharSelectScreenState& s);
/** CSW_Delete_Button: open the prompt (nothing is sent yet). */
void pressDelete(CharSelectScreenState& s);
/** The prompt's Yes / No (and its timeout, and ESC, as No). */
CharSelectAction answerDeletePrompt(CharSelectScreenState& s, bool yes);
/** A creation-screen action, turned into the request it posts. */
CharSelectAction fromCharCreateAction(CharSelectScreenState& s, const CharCreateAction& a);

/** Per-frame time: the delete prompt's timeout. */
void tickCharSelect(CharSelectScreenState& s, float dt);

/** A key: the creation name field when it holds the caret, ESC closes the prompt. */
void applyCharSelectKey(CharSelectScreenState& s, irr::EKEY_CODE key, wchar_t ch);

}  // namespace Graphics
}  // namespace EQT
