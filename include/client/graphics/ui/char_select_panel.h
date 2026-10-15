#pragma once

// B998 (GitLab #318): the Character Select screen's DRAW and CLICK
// resolution. Geometry: char_select_layout.h (quotes EQUI_CharacterSelect.xml;
// this file computes no rect). State: char_select_screen.h.
//
// What is drawn, per the oracle frames (eq-oracle/logs/shots/cc_13_delete_confirm.png,
// review/UIGAP-ORACLE/204/00_character_select.png): the window and its title,
// Explore / Delete / Rotate, the eight Char buttons (a name, or eqstr 1947),
// Quit and Enter World; the selected character's name is YELLOW, and its
// "Name [level Class]" + zone lines float above in yellow. The delete prompt
// is B442b's ConfirmationDialog (EQUI_ConfirmationDialog.xml) -- the oracle's
// own Yes/No box is not in EQUI_CharacterSelect.xml.
// While the creation screen is open it is the whole screen (B888's
// renderCharCreateScreen / hitTestCharCreate / applyCharCreateClick).
// NOT DRAWN: the 3D scene and the character model (OUT of this unit, as in
// B888), so Explore and Rotate are greyed; the filigree title art (the
// A_*TitlePiece animations are not in fable's atlas -- a framed box stands in).

#include "client/graphics/ui/char_create_panel.h"
#include "client/graphics/ui/char_select_screen.h"
#include "client/graphics/ui/ui_layout.h"

#include <cstdint>

namespace EQT {
namespace Graphics {

class UIRenderer;

enum class CharSelectHit : uint8_t {
    None = 0,
    CharButton,    // index = button (0..7)
    EnterWorld,
    Delete,
    Quit,
    Blocked,       // a request in flight: every click is swallowed
};

struct CharSelectClickTarget {
    CharSelectHit hit = CharSelectHit::None;
    int index = -1;
};

/** The screen's eqstr resolver (the view's pre-resolved table). */
EqstrResolver charSelectResolver(const CharSelectScreenState& s);

void renderCharSelectScreen(UIRenderer& ui, const UILayout& layout, const CharSelectScreenState& s);

/** The select window's own controls (not the prompt, not the creation screen). */
CharSelectClickTarget hitTestCharSelect(int sw, int sh, const CharSelectScreenState& s, int x, int y);

/**
 * One mouse click anywhere: the delete prompt first (it is modal), then the
 * creation screen when it is open, then the select window. `seed` is the
 * random pick creation opens on; `useOldModels` is the model era the creation
 * screen's FacePick rows gate on (finding 1: classic models read the
 * visible_classic column, Luclin models the Luclin column).
 */
CharSelectAction applyCharSelectClick(CharSelectScreenState& s, const UILayout& layout, int x, int y,
                                      bool rightButton, const eqt::charcreate::CharCreateData& data,
                                      uint32_t seed, bool useOldModels = true);

}  // namespace Graphics
}  // namespace EQT
