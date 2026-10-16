// B998 (GitLab #318) -- the Character Select screen's state machine. The flow
// and its oracle citations: include/client/graphics/ui/char_select_screen.h.

#include "client/graphics/ui/char_select_screen.h"

#include "client/graphics/ui/char_select_layout.h"

#include <algorithm>
#include <cctype>

namespace EQT {
namespace Graphics {

using eqt::login_world::CharacterSelectRequest;
using eqt::login_world::CharacterSelectResult;
using eqt::login_world::CharacterSelectView;

namespace {

constexpr uint32_t kEqstrDeletePrompt = 9065;   // "Are you absolutely sure you want to delete %1?" (MEASURED, 318/10)
constexpr uint8_t kDeletePurpose = 1;           // the dialog's echo token

CharSelectAction post(CharSelectScreenState& s, CharSelectScreenState::Awaiting what,
                      const CharacterSelectRequest& req) {
    s.awaiting = what;
    ++s.posted;
    CharSelectAction a;
    a.kind = CharSelectAction::Kind::Post;
    a.request = req;
    return a;
}

std::string substitute(std::string text, const std::string& arg) {
    const auto at = text.find("%1");
    if (at != std::string::npos) text.replace(at, 2, arg);
    return text;
}

}  // namespace

std::string charSelectEqstr(const CharSelectScreenState& s, uint32_t id) {
    if (!s.strings) return std::string();
    const auto it = s.strings->find(id);
    return it != s.strings->end() ? it->second : std::string();
}

int charSelectRowForButton(const CharSelectScreenState& s, int n) {
    if (n < 0 || n >= CharSelectLayout::kCharButtons) return -1;
    return n < static_cast<int>(s.rows.size()) ? n : -1;
}

bool charSelectBlocked(const CharSelectScreenState& s) {
    return s.awaiting != CharSelectScreenState::Awaiting::None;
}

void applyCharacterSelectView(CharSelectScreenState& s, const CharacterSelectView& v) {
    const std::string keep = (s.selected >= 0 && s.selected < static_cast<int>(s.rows.size()))
                                 ? s.rows[static_cast<size_t>(s.selected)].name
                                 : std::string();
    s.rows = v.rows;
    if (v.strings) s.strings = v.strings;
    s.selected = -1;
    for (size_t i = 0; i < s.rows.size(); ++i) {
        if (!keep.empty() && s.rows[i].name == keep) s.selected = static_cast<int>(i);
    }
    // At open, and when the selected character is gone (deleted): the first
    // one (MEASURED, review/UIGAP-ORACLE/318 rule 4, stills 00 and 16).
    if (s.selected < 0 && !s.rows.empty()) s.selected = 0;

    // The answer to the request in flight.
    if (s.awaiting == CharSelectScreenState::Awaiting::None || v.requestsHandled < s.posted || v.busy) return;
    const auto was = s.awaiting;
    s.awaiting = CharSelectScreenState::Awaiting::None;
    switch (was) {
    case CharSelectScreenState::Awaiting::Create:
        if (v.lastResult == CharacterSelectResult::Created) {
            // World entry follows on the game thread; keep the page up and
            // still until the screen is taken down.
            s.awaiting = CharSelectScreenState::Awaiting::EnterWorld;
        } else {
            applySubmitRefused(s.create, v.lastMessage);
        }
        break;
    case CharSelectScreenState::Awaiting::GetName:
        applyRandomName(s.create, v.randomName);
        if (s.create.nameEntry.inputText.size() > CharSelectScreenState::kNameMaxChars) {
            s.create.nameEntry.inputText.resize(CharSelectScreenState::kNameMaxChars);
            s.create.nameEntry.cursorPos = static_cast<int>(CharSelectScreenState::kNameMaxChars);
        }
        break;
    case CharSelectScreenState::Awaiting::EnterWorld:
        // Still listed and handled: the game refused it (logged there). The
        // screen goes away on its own when OP_EnterWorld really went out.
        break;
    case CharSelectScreenState::Awaiting::Delete:
    case CharSelectScreenState::Awaiting::None:
        break;
    }
}

CharSelectAction pressCharButton(CharSelectScreenState& s, int n,
                                 const eqt::charcreate::CharCreateData& data, uint32_t seed,
                                 bool useOldModels) {
    CharSelectAction none;
    if (charSelectBlocked(s) || s.confirm.isOpen || s.create.isOpen) return none;
    if (n < 0 || n >= CharSelectLayout::kCharButtons) return none;
    const int row = charSelectRowForButton(s, n);
    if (row >= 0) {
        s.selected = row;
        return none;
    }
    openCharCreate(s.create, data, seed, useOldModels);
    return none;
}

CharSelectAction pressEnterWorld(CharSelectScreenState& s) {
    CharSelectAction none;
    if (charSelectBlocked(s) || s.confirm.isOpen || s.create.isOpen) return none;
    if (s.selected < 0 || s.selected >= static_cast<int>(s.rows.size())) return none;   // greyed
    CharacterSelectRequest r;
    r.kind = CharacterSelectRequest::Kind::EnterWorld;
    r.name = s.rows[static_cast<size_t>(s.selected)].name;
    return post(s, CharSelectScreenState::Awaiting::EnterWorld, r);
}

void moveSelection(CharSelectScreenState& s, int delta) {
    // B1082 (GitLab #402), MEASURED (refactor/B1082/README.md): Up/Down move the
    // highlight one row and WRAP at the ends of the occupied list. The official
    // client cycles its highlight through the whole 8-button list -- occupied
    // rows and the empty "Create a New Character" slots -- and it sends NOTHING
    // to the server for any of it (the selection is client-side until
    // Enter/Return). This state model tracks only the occupied rows (s.rows):
    // an empty slot has no name to enter with, so a step that would land in the
    // empty range wraps to the next occupied row. The observable result is the
    // same -- the highlight returns to an occupied row, the 3D model is present
    // again (plan C: Down from the last occupied row steps around to the first
    // occupied row with the model still up), and EnterWorld targets an
    // occupied character.
    if (charSelectBlocked(s) || s.confirm.isOpen || s.create.isOpen) return;
    if (s.rows.empty() || delta == 0) return;
    if (s.selected < 0 || s.selected >= static_cast<int>(s.rows.size())) return;
    const int n = static_cast<int>(s.rows.size());
    s.selected = ((s.selected + delta) % n + n) % n;
}

void pressDelete(CharSelectScreenState& s) {
    if (charSelectBlocked(s) || s.confirm.isOpen || s.create.isOpen) return;
    if (s.selected < 0 || s.selected >= static_cast<int>(s.rows.size())) return;   // greyed
    s.confirmName = s.rows[static_cast<size_t>(s.selected)].name;
    std::string text = charSelectEqstr(s, kEqstrDeletePrompt);
    if (text.empty()) text = "eqstr " + std::to_string(kEqstrDeletePrompt) + " %1";
    s.confirm.open(kDeletePurpose, substitute(text, s.confirmName),
                   ConfirmationDialogState::Buttons::YesNo,
                   CharSelectScreenState::kDeleteConfirmSeconds);
}

CharSelectAction answerDeletePrompt(CharSelectScreenState& s, bool yes) {
    CharSelectAction none;
    if (!s.confirm.isOpen) return none;
    const std::string name = s.confirmName;
    s.confirm.close();
    s.confirmName.clear();
    if (!yes || name.empty()) return none;
    CharacterSelectRequest r;
    r.kind = CharacterSelectRequest::Kind::Delete;
    r.name = name;
    return post(s, CharSelectScreenState::Awaiting::Delete, r);
}

CharSelectAction fromCharCreateAction(CharSelectScreenState& s, const CharCreateAction& a) {
    CharSelectAction none;
    switch (a.kind) {
    case CharCreateAction::Kind::Submit: {
        CharacterSelectRequest r;
        r.kind = CharacterSelectRequest::Kind::Create;
        r.create = a.request;
        r.name = a.request.name;
        return post(s, CharSelectScreenState::Awaiting::Create, r);
    }
    case CharCreateAction::Kind::GetName: {
        CharacterSelectRequest r;
        r.kind = CharacterSelectRequest::Kind::GetName;
        r.race = a.race;
        r.gender = a.gender;
        return post(s, CharSelectScreenState::Awaiting::GetName, r);
    }
    case CharCreateAction::Kind::BackToCharacterSelect:
        // The page closed itself; the first row is selected again
        // (MEASURED, review/UIGAP-ORACLE/318 rule 4, still 04).
        s.selected = s.rows.empty() ? -1 : 0;
        return none;
    case CharCreateAction::Kind::None:
        return none;
    }
    return none;
}

void tickCharSelect(CharSelectScreenState& s, float dt) {
    // The one confirmation clock (confirmation_dialog.h), shared with the
    // in-world drop / destroy prompts.
    if (tickConfirmationDialog(s.confirm, dt)) answerDeletePrompt(s, false);
}

CharSelectAction applyCharSelectKey(CharSelectScreenState& s, irr::EKEY_CODE key, wchar_t ch) {
    if (s.confirm.isOpen) {
        // B998: ESC = No. MEASURED (B1082, refactor/B1082/README.md): the
        // official prompt does NOT answer Yes/No on Enter/Return or navigate
        // while up -- it answers No on ESC and closes on its own ~10 s timeout
        // (and on a Yes/No click). Nothing else is consumed here.
        if (key == irr::KEY_ESCAPE) answerDeletePrompt(s, false);
        return {};
    }
    if (s.create.isOpen) {
        if (s.create.page != 1 || !s.create.nameEntry.inputActive || s.create.submitting) return {};
        const std::string before = s.create.nameEntry.inputText;
        const TextEntryAction act = applyTextEntryKey(s.create.nameEntry, key, ch);
        if (act != TextEntryAction::Changed) return {};
        std::string& t = s.create.nameEntry.inputText;
        if (t.size() > CharSelectScreenState::kNameMaxChars) {
            t = before;   // the field is full: the key is refused
            s.create.nameEntry.cursorPos = static_cast<int>(std::min(t.size(), static_cast<size_t>(
                                               std::max(0, s.create.nameEntry.cursorPos))));
            return {};
        }
        // MEASURED (review/UIGAP-ORACLE/204/10): typed letters after the first are lower-cased.
        for (size_t i = 1; i < t.size(); ++i) t[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
        return {};
    }
    // B1082 (GitLab #402): the select screen's keyboard. MEASURED on the
    // official client (refactor/B1082/README.md):
    //   Down / Up   move the selection one occupied row, wrapping at the ends
    //               (never into the empty "Create a New Character" slots).
    //               Nothing is sent to the server for any of it.
    //   Enter/Return send OP_EnterWorld for the selected row -- the SAME body
    //               as the Enter World button (pressEnterWorld), greyed and
    //               inert when nothing is selected.
    //   Everything else (letters, Backspace, Delete, Escape, Tab, ...) is
    //               inert on this screen; the host keeps spending Enter/Escape
    //               so none of it replays in-zone.
    if (key == irr::KEY_DOWN) { moveSelection(s, +1); return {}; }
    if (key == irr::KEY_UP) { moveSelection(s, -1); return {}; }
    if (key == irr::KEY_RETURN) {   // Return (the numpad Enter maps here too)
        return pressEnterWorld(s);
    }
    return {};
}

}  // namespace Graphics
}  // namespace EQT
