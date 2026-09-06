/*
 * TextEntryState -- THE text-entry mechanism of fable's static UI (T421,
 * board B365).
 *
 * WHAT WAS BROKEN. T419 drew the tradeskill window's `COMBW_MinTrivialEdit`,
 * `COMBW_MaxTrivialEdit`, `COMBW_SearchTextEdit` and `COMBW_SearchButton`
 * because leaving a hole where the oracle's chrome belongs is the defect
 * B335 complained about -- but all four were INERT, and the reason it gave
 * was structural: "fable's static UI has no text-entry widget outside the
 * chat box". That was true. `FrameComponent::processChatInput()` was the
 * only keyboard consumer in the renderer, and its caret, its backspace and
 * its printable-character filter were written inline against
 * `ChatPanelState`'s own three members.
 *
 * THIS IS THAT WIDGET, and there is exactly one of it. `ChatPanelState`
 * DERIVES from `TextEntryState` -- the three members keep the names chat has
 * always used (`inputText`, `cursorPos`, `inputActive`), so every existing
 * chat call site and every existing chat test is untouched -- and the
 * tradeskill window's three editboxes are three more `TextEntryState`s.
 * `applyTextEntryKey()` is the one body that turns a key into an edit, for
 * both.
 *
 * The behaviour is deliberately IDENTICAL to what processChatInput() did
 * inline before this unit, key for key: Return submits, Escape cancels, Tab
 * asks the caller for completion, Backspace deletes before the caret, and a
 * printable ASCII character inserts at the caret. Nothing was added while
 * moving it (caret arrows included), because "the chat entry is unchanged"
 * is a condition of this unit and a shared widget that quietly changes one
 * of its two users is the opposite of sharing.
 */

#pragma once

#include <irrlicht.h>

#include <cstdint>
#include <string>

namespace EQT {
namespace Graphics {

/**
 * One editable text field. The member NAMES are chat's, because
 * ChatPanelState derives from this and its call sites predate it.
 */
struct TextEntryState {
    std::string inputText;
    int cursorPos = 0;
    bool inputActive = false;
};

/** What a key did, for the caller to act on. */
enum class TextEntryAction : uint8_t {
    None = 0,     // the key is not this widget's
    Changed,      // the text or the caret moved
    Submit,       // Return
    Cancel,       // Escape
    Complete      // Tab -- the caller supplies the vocabulary
};

/** Focus a field, seeded with `initial`, caret at its end. */
void focusTextEntry(TextEntryState& state, const std::string& initial);

/** Clear and unfocus. */
void clearTextEntry(TextEntryState& state);

/**
 * Apply one key event. `character` is Irrlicht's SKeyInput::Char.
 *
 * Submit and Cancel do NOT themselves clear the field -- what happens to the
 * text on either is the caller's policy (chat clears on both; the search box
 * keeps what was typed on Submit so the box still echoes the live query).
 */
TextEntryAction applyTextEntryKey(TextEntryState& state,
                                  irr::EKEY_CODE key,
                                  wchar_t character);

} // namespace Graphics
} // namespace EQT
