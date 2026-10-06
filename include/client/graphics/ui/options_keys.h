/*
 * The Options window's Keys page (B433): the category combobox, the
 * Command / Keypress / Alternate list over HotkeyManager, and rebinding.
 *
 * Bindings are GLOBAL (config/hotkeys.json + the user copy in the cache dir,
 * hotkey_manager.h), never per character.
 *
 * CATEGORIES. The oracle's Keys page shows a "Select category:" combobox;
 * three of its categories are MEASURED in review/T377/oracle/ ("UI",
 * "Commands", movement). The others here are fable's own grouping of fable's
 * HotkeyAction set (flagged), and "Debug" holds fable's developer toggles,
 * which have no oracle counterpart. Every HotkeyAction is in exactly one
 * category and no category outgrows the list's visible rows, so the list
 * never needs a scrollbar (OptionsKeys.* pins both).
 *
 * The Command column text is fable's (the action name split into words); the
 * oracle's wording differs ("Toggle inventory window") and is only partly
 * measured, so it is not imitated.
 */

#pragma once

#include <string>
#include <vector>

#include "client/input/hotkey_manager.h"

namespace EQT {
namespace Graphics {

struct OptionsPanelState;
class UIRenderer;
struct UILayout;

namespace OptionsKeys {

constexpr int kCategoryCount = 8;

const char* categoryName(int category);

/** The actions category `category` lists, in display order. */
const std::vector<eqt::input::HotkeyAction>& actionsIn(int category);

/** "ToggleInventory" -> "Toggle inventory". */
std::string commandText(eqt::input::HotkeyAction action);

/** A slot's text: the binding ("Ctrl+B"), or the oracle's "-------". */
std::string bindingText(eqt::input::HotkeyAction action, int slot);

/** The column a click at `mouseX` in the list lands in: 0 Command, 1 Keypress, 2 Alternate. */
int columnAt(int listLeft, int listWidth, int mouseX);

/**
 * Finish a capture: bind the captured action's slot to `key` + modifiers and
 * save the user copy. Returns false (and keeps capturing) for a bare modifier
 * key; ESC cancels without binding. Either way a finished capture clears
 * `state.keysCaptureAction`.
 */
bool finishKeyCapture(OptionsPanelState& state, irr::EKEY_CODE key, bool ctrl,
                      bool shift, bool alt, bool save = true);

}  // namespace OptionsKeys
}  // namespace Graphics
}  // namespace EQT
