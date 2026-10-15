// B998 (GitLab #318) -- the Character Select screen's draw and click
// resolution. Its own TU (M00). Geometry is
// include/client/graphics/ui/char_select_layout.h; this file computes no rect.

#include "client/graphics/ui/char_select_panel.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/char_select_layout.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

using L = CharSelectLayout;
using Rect = irr::core::rect<irr::s32>;

namespace {

constexpr uint32_t kEqstrCreateNewCharacter = 1947;   // "Create a New Character"

const irr::video::SColor kText(255, 230, 230, 230);
// Measured on the oracle frames: the selected name and the two lines above
// the model are yellow; an unavailable button's text is grey (cc_13: "Return
// Home").
const irr::video::SColor kSelected(255, 235, 235, 60);
const irr::video::SColor kGreyed(255, 120, 120, 128);
const irr::video::SColor kFrame(255, 120, 112, 90);

bool inside(const Rect& r, int x, int y) { return r.isPointInside(irr::core::vector2di(x, y)); }

enum class ButtonState { Normal, Selected, Greyed };

void drawButton(UIRenderer& ui, TextBatch* tb, const Rect& r, const std::string& label, ButtonState st) {
    ui.drawRect(r, st == ButtonState::Selected ? irr::video::SColor(220, 20, 20, 30)
                                               : irr::video::SColor(210, 48, 48, 60));
    ui.drawFrame(r, st == ButtonState::Selected ? irr::video::SColor(255, 30, 30, 30) : kFrame);
    if (tb && !label.empty()) {
        tb->addTextCentered(label, r,
                            st == ButtonState::Selected ? kSelected
                            : st == ButtonState::Greyed ? kGreyed : kText);
    }
}

bool hasSelection(const CharSelectScreenState& s) {
    return s.selected >= 0 && s.selected < static_cast<int>(s.rows.size());
}

}  // namespace

EqstrResolver charSelectResolver(const CharSelectScreenState& s) {
    return [&s](uint32_t id) { return charSelectEqstr(s, id); };
}

void renderCharSelectScreen(UIRenderer& ui, const UILayout& layout, const CharSelectScreenState& s) {
    const int sw = layout.screenW, sh = layout.screenH;
    if (s.create.isOpen) {
        renderCharCreateScreen(ui, sw, sh, s.create, charSelectResolver(s));
        return;
    }
    TextBatch* tb = ui.getTextBatch();

    const Rect win = L::windowRect(sw, sh);
    ui.drawPanel(win);
    ui.drawFrame(win, kFrame);
    const Rect title = L::titlePiecesRect(sw, sh);
    const Rect titleBox(title.UpperLeftCorner.X + 16, title.UpperLeftCorner.Y + 6,
                        title.LowerRightCorner.X - 16, title.LowerRightCorner.Y - 6);
    ui.drawRect(titleBox, irr::video::SColor(230, 8, 8, 12));
    ui.drawFrame(titleBox, kFrame);
    if (tb) tb->addTextCentered("Character Select", L::titleLabelRect(sw, sh), kText);

    const bool sel = hasSelection(s);
    // While the delete prompt is up every button but Quit is greyed
    // (MEASURED, review/UIGAP-ORACLE/318/charselect.10_*).
    const bool modal = s.confirm.isOpen;
    const ButtonState live = modal ? ButtonState::Greyed : ButtonState::Normal;
    drawButton(ui, tb, L::exploreRect(sw, sh), "Explore", ButtonState::Greyed);
    drawButton(ui, tb, L::deleteRect(sw, sh), "Delete", sel ? live : ButtonState::Greyed);
    drawButton(ui, tb, L::rotateRect(sw, sh), "Rotate", ButtonState::Greyed);

    std::string create = charSelectEqstr(s, kEqstrCreateNewCharacter);
    if (create.empty()) create = "Create a New Character";   // eqstr 1947 verbatim, should the table miss it
    for (int n = 0; n < L::kCharButtons; ++n) {
        const int row = charSelectRowForButton(s, n);
        if (row >= 0) {
            drawButton(ui, tb, L::charButtonRect(sw, sh, n), s.rows[static_cast<size_t>(row)].name,
                       modal ? ButtonState::Greyed : row == s.selected ? ButtonState::Selected : ButtonState::Normal);
        } else {
            drawButton(ui, tb, L::charButtonRect(sw, sh, n), create, live);
        }
    }
    drawButton(ui, tb, L::quitRect(sw, sh), "Quit", ButtonState::Normal);
    drawButton(ui, tb, L::enterWorldRect(sw, sh), "Enter World", sel ? live : ButtonState::Greyed);

    if (sel && tb) {
        const auto& r = s.rows[static_cast<size_t>(s.selected)];
        tb->addTextCentered(r.infoLine, L::infoLineRect(sw, sh), kSelected);
        tb->addTextCentered(r.zoneLine, L::zoneLineRect(sw, sh), kSelected);
    }

    // B992: the prompt overlaps the window, so it is its own layer -- the
    // window's button text must not draw over the dialog's plate.
    if (s.confirm.isOpen) renderConfirmationDialog(ui.layer(), layout, s.confirm);
}

CharSelectClickTarget hitTestCharSelect(int sw, int sh, const CharSelectScreenState& s, int x, int y) {
    CharSelectClickTarget t;
    if (s.create.isOpen || s.confirm.isOpen) return t;
    if (charSelectBlocked(s)) {
        t.hit = CharSelectHit::Blocked;
        return t;
    }
    const bool sel = hasSelection(s);
    for (int n = 0; n < L::kCharButtons; ++n) {
        if (inside(L::charButtonRect(sw, sh, n), x, y)) {
            t.hit = CharSelectHit::CharButton;
            t.index = n;
            return t;
        }
    }
    // Greyed controls are not hit-testable (T363): Explore and Rotate
    // always, Delete and Enter World with nothing selected.
    if (sel && inside(L::deleteRect(sw, sh), x, y)) { t.hit = CharSelectHit::Delete; return t; }
    if (sel && inside(L::enterWorldRect(sw, sh), x, y)) { t.hit = CharSelectHit::EnterWorld; return t; }
    if (inside(L::quitRect(sw, sh), x, y)) { t.hit = CharSelectHit::Quit; return t; }
    return t;
}

CharSelectAction applyCharSelectClick(CharSelectScreenState& s, const UILayout& layout, int x, int y,
                                      bool rightButton, const eqt::charcreate::CharCreateData& data,
                                      uint32_t seed, bool useOldModels) {
    CharSelectAction none;
    const int sw = layout.screenW, sh = layout.screenH;
    if (s.confirm.isOpen) {
        if (rightButton) return none;
        switch (processConfirmationClick(layout, s.confirm, x, y, true)) {
        case ConfirmationClick::Yes: return answerDeletePrompt(s, true);
        case ConfirmationClick::No:
        case ConfirmationClick::Ok: return answerDeletePrompt(s, false);
        case ConfirmationClick::Consumed:
        case ConfirmationClick::None: return none;   // modal: a click elsewhere does nothing
        }
        return none;
    }
    if (s.create.isOpen) {
        if (charSelectBlocked(s) && s.awaiting != CharSelectScreenState::Awaiting::GetName) return none;
        const CharCreateClickTarget t = hitTestCharCreate(sw, sh, s.create, x, y);
        return fromCharCreateAction(s, applyCharCreateClick(s.create, t, rightButton));
    }
    if (rightButton) return none;
    const CharSelectClickTarget t = hitTestCharSelect(sw, sh, s, x, y);
    switch (t.hit) {
    case CharSelectHit::CharButton:
        return pressCharButton(s, t.index, data, seed, useOldModels);
    case CharSelectHit::EnterWorld: return pressEnterWorld(s);
    case CharSelectHit::Delete: pressDelete(s); return none;
    case CharSelectHit::Quit: {
        CharSelectAction q;
        q.kind = CharSelectAction::Kind::Quit;
        return q;
    }
    case CharSelectHit::Blocked:
    case CharSelectHit::None: return none;
    }
    return none;
}

}  // namespace Graphics
}  // namespace EQT
