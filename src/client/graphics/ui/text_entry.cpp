/*
 * T421 (board B365) -- see text_entry.h. This body is
 * FrameComponent::processChatInput()'s inline key drain, moved out verbatim
 * so the tradeskill search boxes can use the same one.
 */

#include "client/graphics/ui/text_entry.h"

namespace EQT {
namespace Graphics {

void focusTextEntry(TextEntryState& state, const std::string& initial) {
    state.inputText = initial;
    state.cursorPos = static_cast<int>(initial.size());
    state.inputActive = true;
}

void clearTextEntry(TextEntryState& state) {
    state.inputText.clear();
    state.cursorPos = 0;
    state.inputActive = false;
}

TextEntryAction applyTextEntryKey(TextEntryState& state,
                                  irr::EKEY_CODE key,
                                  wchar_t character) {
    if (key == irr::KEY_RETURN) {
        return TextEntryAction::Submit;
    }
    if (key == irr::KEY_ESCAPE) {
        return TextEntryAction::Cancel;
    }
    if (key == irr::KEY_TAB) {
        return TextEntryAction::Complete;
    }
    if (key == irr::KEY_BACK) {
        if (state.cursorPos > 0 && !state.inputText.empty()) {
            state.inputText.erase(static_cast<size_t>(state.cursorPos) - 1, 1);
            state.cursorPos--;
            return TextEntryAction::Changed;
        }
        return TextEntryAction::None;
    }
    // Printable ASCII only -- the same filter the chat drain has always had.
    if (character >= 32 && character < 127) {
        state.inputText.insert(static_cast<size_t>(state.cursorPos), 1,
                               static_cast<char>(character));
        state.cursorPos++;
        return TextEntryAction::Changed;
    }
    return TextEntryAction::None;
}

} // namespace Graphics
} // namespace EQT
