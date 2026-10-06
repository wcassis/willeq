// B888 (GitLab #204) -- the Character Creation screen's draw and click
// resolution. Its own TU (M00). Geometry is
// include/client/graphics/ui/char_create_layout.h; this file computes no
// control rect (the message box is the one fable rect, flagged in the header).

#include "client/graphics/ui/char_create_panel.h"

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace EQT {
namespace Graphics {

using namespace eqt::charcreate;
using L = CharCreateLayout;
using Rect = irr::core::rect<irr::s32>;

namespace {

const irr::video::SColor kText(255, 230, 230, 230);
// Measured on the oracle frames: a selected label is yellow, a greyed one grey,
// a primary stat green (cc_06_stats.png).
const irr::video::SColor kSelected(255, 235, 235, 60);
const irr::video::SColor kGreyed(255, 120, 120, 128);
const irr::video::SColor kPrimary(255, 40, 230, 40);
const irr::video::SColor kFrame(255, 120, 112, 90);

bool inside(const Rect& r, int x, int y) { return r.isPointInside(irr::core::vector2di(x, y)); }

enum class ButtonState { Normal, Selected, Greyed };

void drawButton(UIRenderer& ui, TextBatch* tb, const Rect& r, const std::string& label,
                ButtonState st) {
    ui.drawRect(r, st == ButtonState::Selected ? irr::video::SColor(220, 20, 20, 30)
                                               : irr::video::SColor(210, 48, 48, 60));
    ui.drawFrame(r, st == ButtonState::Selected ? irr::video::SColor(255, 30, 30, 30) : kFrame);
    if (tb && !label.empty()) {
        tb->addTextCentered(label, r,
                            st == ButtonState::Selected ? kSelected
                            : st == ButtonState::Greyed ? kGreyed : kText);
    }
}

void drawLabelBox(UIRenderer& ui, TextBatch* tb, const Rect& r, const char* label) {
    ui.drawRect(r, irr::video::SColor(220, 16, 18, 28));
    ui.drawFrame(r, kFrame);
    if (tb) tb->addTextCentered(label, r, kText);
}

void drawScreen(UIRenderer& ui, const Rect& r) {
    ui.drawPanel(r);
    ui.drawFrame(r, kFrame);
}

std::string resolve(const EqstrResolver& eqstr, uint32_t id) {
    std::string text = eqstr ? eqstr(id) : std::string();
    if (text.empty()) text = "eqstr " + std::to_string(id);
    return charCreatePlainText(text);
}

// Greedy word wrap of `text` (with '\n' breaks) into `r`.
void drawWrapped(TextBatch* tb, const Rect& r, const std::string& text, irr::video::SColor color) {
    if (!tb) return;
    const int lineH = std::max<int>(12, tb->getLineHeight());
    const int maxW = r.getWidth();
    int y = r.UpperLeftCorner.Y;
    std::istringstream paragraphs(text);
    std::string para;
    while (std::getline(paragraphs, para)) {
        std::istringstream words(para);
        std::string word, line;
        auto flush = [&]() {
            if (y + lineH <= r.LowerRightCorner.Y) tb->addText(line, r.UpperLeftCorner.X, y, color);
            y += lineH;
            line.clear();
        };
        bool any = false;
        while (words >> word) {
            any = true;
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && tb->getTextWidth(cand) > maxW) {
                flush();
                line = word;
            } else {
                line = cand;
            }
        }
        if (!line.empty()) flush();
        else if (!any) y += lineH;   // a blank line (<BR><BR>)
    }
}

const EraEntry* entryAt(const std::vector<EraEntry>& v, int i) {
    return (i >= 0 && i < static_cast<int>(v.size())) ? &v[static_cast<size_t>(i)] : nullptr;
}

void drawHelpPane(UIRenderer& ui, TextBatch* tb, const Rect& pane, const CharCreateScreenState& s,
                  const EqstrResolver& eqstr) {
    drawScreen(ui, pane);
    const Rect text = L::helpTextRect(pane);
    ui.drawRect(text, irr::video::SColor(230, 12, 14, 22));
    std::string body;
    if (!s.helpTitle.empty()) body = s.helpTitle;
    if (s.helpEqstr) body += (body.empty() ? "" : "\n\n") + resolve(eqstr, s.helpEqstr);
    drawWrapped(tb, Rect(text.UpperLeftCorner.X + 3, text.UpperLeftCorner.Y + 2,
                         text.LowerRightCorner.X - 3, text.LowerRightCorner.Y - 2),
                body, kText);
}

void drawPage0(UIRenderer& ui, TextBatch* tb, int sw, int sh, const CharCreateScreenState& s) {
    drawScreen(ui, L::screen0aRect(sw, sh));
    drawLabelBox(ui, tb, L::characterTitleRect(sw, sh), "Character");
    drawButton(ui, tb, L::faceButtonRect(sw, sh), "Set Face", ButtonState::Greyed);
    drawButton(ui, tb, L::maleButtonRect(sw, sh), "Male",
               s.gender == Gender::Male ? ButtonState::Selected : ButtonState::Normal);
    drawButton(ui, tb, L::femaleButtonRect(sw, sh), "Female",
               s.gender == Gender::Female ? ButtonState::Selected : ButtonState::Normal);
    for (int i = 0; i < static_cast<int>(s.races.size()) && i < L::kRaceSlots; ++i) {
        const auto& r = s.races[static_cast<size_t>(i)];
        drawButton(ui, tb, L::raceSlotRect(sw, sh, i), r.label,
                   r.id == s.race ? ButtonState::Selected : ButtonState::Normal);
    }
    for (int i = 0; i < static_cast<int>(s.classes.size()) && i < L::kClassSlots; ++i) {
        const auto& c = s.classes[static_cast<size_t>(i)];
        const ButtonState st = c.id == s.cls ? ButtonState::Selected
                               : isValidRaceClass(s.race, c.id) ? ButtonState::Normal
                                                                : ButtonState::Greyed;
        drawButton(ui, tb, L::classSlotRect(sw, sh, i), c.label, st);
    }

    drawScreen(ui, L::screen0bRect(sw, sh));
    drawLabelBox(ui, tb, L::abilitiesTitleRect(sw, sh), "Abilities");
    drawButton(ui, tb, L::pointsLeftButtonRect(sw, sh), "POINTS LEFT", ButtonState::Normal);
    const Rect pd = L::pointsLeftDisplayRect(sw, sh);
    ui.drawRect(pd, irr::video::SColor(230, 10, 10, 14));
    ui.drawFrame(pd, kFrame);
    if (tb) tb->addTextCentered(std::to_string(s.alloc.pointsLeft()), pd, kText);
    for (int n = 0; n < L::kStatRows; ++n) {
        drawButton(ui, tb, L::statButtonRect(sw, sh, n), statLabel(n), ButtonState::Normal);
        const Rect d = L::statDisplayRect(sw, sh, n);
        ui.drawRect(d, irr::video::SColor(230, 10, 10, 14));
        ui.drawFrame(d, kFrame);
        if (tb) {
            tb->addTextCentered(std::to_string(s.alloc.value(n)), d,
                                isPrimaryStat(s.cls, n) ? kPrimary : kText);
        }
        drawButton(ui, tb, L::statUpRect(sw, sh, n), "+", ButtonState::Normal);
        drawButton(ui, tb, L::statDownRect(sw, sh, n), "-", ButtonState::Normal);
    }
}

void drawPage1(UIRenderer& ui, TextBatch* tb, int sw, int sh, const CharCreateScreenState& s) {
    drawScreen(ui, L::screen1aRect(sw, sh));
    drawLabelBox(ui, tb, L::nameLabelRect(sw, sh), "Name");
    const Rect in = L::nameInputRect(sw, sh);
    ui.drawRect(in, irr::video::SColor(230, 10, 10, 14));
    ui.drawFrame(in, s.nameEntry.inputActive ? irr::video::SColor(255, 150, 160, 200) : kFrame);
    if (tb) tb->addText(s.nameEntry.inputText, in.UpperLeftCorner.X + 3, in.UpperLeftCorner.Y + 4, kText);
    drawButton(ui, tb, L::getNameButtonRect(sw, sh), "Get Name", ButtonState::Normal);

    drawScreen(ui, L::screen1bRect(sw, sh));
    drawLabelBox(ui, tb, L::deityTitleRect(sw, sh), "Deity");
    const auto deities = s.offeredDeities();
    for (int i = 0; i < static_cast<int>(s.deities.size()) && i < L::kDeitySlots; ++i) {
        const auto& d = s.deities[static_cast<size_t>(i)];
        const bool offered = std::find(deities.begin(), deities.end(), d.id) != deities.end();
        drawButton(ui, tb, L::deitySlotRect(sw, sh, i), d.label,
                   d.id == s.deity ? ButtonState::Selected
                   : offered       ? ButtonState::Normal : ButtonState::Greyed);
    }

    drawScreen(ui, L::screen1cRect(sw, sh));
    drawLabelBox(ui, tb, L::cityTitleRect(sw, sh), "City");
    const auto cities = s.offeredCities();
    for (int i = 0; i < static_cast<int>(s.cities.size()) && i < L::kCitySlots; ++i) {
        const auto& c = s.cities[static_cast<size_t>(i)];
        const bool offered = std::find(cities.begin(), cities.end(), c.id) != cities.end();
        drawButton(ui, tb, L::citySlotRect(sw, sh, i), c.label,
                   c.id == s.city ? ButtonState::Selected
                   : offered      ? ButtonState::Normal : ButtonState::Greyed);
    }
}

}  // namespace

Rect charCreateMessageRect(int sw, int sh) {
    // fable's rect: the oracle's message dialog (create_Oracledwarf/
    // 04_deitypage.png, ~270x195) belongs to a generic window not in
    // EQUI_CharacterCreate.xml. Centred.
    constexpr int kW = 270, kH = 120;
    return L::make((sw - kW) / 2, (sh - kH) / 2, kW, kH);
}

Rect charCreateMessageOkRect(int sw, int sh) {
    const Rect m = charCreateMessageRect(sw, sh);
    return L::make(m.getCenter().X - 31, m.LowerRightCorner.Y - 28, 62, 20);
}

std::string charCreatePlainText(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        // eqstr 3334 & co. carry U+2019 (UTF-8 E2 80 99) for the apostrophe;
        // the static UI's bitmap font has no glyph for it.
        if (in.compare(i, 3, "\xE2\x80\x99") == 0) {
            out.push_back('\'');
            i += 3;
            continue;
        }
        if (in[i] == '<') {
            const size_t close = in.find('>', i);
            if (close == std::string::npos) { out += in.substr(i); break; }
            std::string tag = in.substr(i + 1, close - i - 1);
            for (auto& ch : tag) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (tag == "BR") out.push_back('\n');
            i = close + 1;
            continue;
        }
        out.push_back(in[i]);
        ++i;
    }
    return out;
}

void renderCharCreateScreen(UIRenderer& ui, int sw, int sh, const CharCreateScreenState& s,
                            const EqstrResolver& eqstr) {
    if (!s.isOpen) return;
    TextBatch* tb = ui.getTextBatch();

    drawScreen(ui, L::titleRect(sw, sh));
    if (tb) tb->addTextCentered("Character Creation", L::titleLabelRect(sw, sh), kText);
    drawButton(ui, tb, L::helpButtonRect(sw, sh), "Help", ButtonState::Normal);
    drawButton(ui, tb, L::backButtonRect(sw, sh), "Back", ButtonState::Normal);
    drawButton(ui, tb, L::nextButtonRect(sw, sh), "Next", ButtonState::Normal);

    if (s.page == 0) {
        drawPage0(ui, tb, sw, sh, s);
        drawHelpPane(ui, tb, L::screen0cRect(sw, sh), s, eqstr);
    } else {
        drawPage1(ui, tb, sw, sh, s);
        drawHelpPane(ui, tb, L::screen1dRect(sw, sh), s, eqstr);
    }

    if (s.messageEqstr || !s.messageText.empty()) {
        ui.beginLayer();   // B992: the modal is above the panes' text (B998 hosts it live)
        const Rect m = charCreateMessageRect(sw, sh);
        drawScreen(ui, m);
        const std::string text = s.messageEqstr ? resolve(eqstr, s.messageEqstr) : s.messageText;
        drawWrapped(tb, Rect(m.UpperLeftCorner.X + 8, m.UpperLeftCorner.Y + 8,
                             m.LowerRightCorner.X - 8, m.LowerRightCorner.Y - 32),
                    text, kText);
        if (!s.submitting) drawButton(ui, tb, charCreateMessageOkRect(sw, sh), "Ok", ButtonState::Normal);
    }
}

CharCreateClickTarget hitTestCharCreate(int sw, int sh, const CharCreateScreenState& s, int x, int y) {
    CharCreateClickTarget t;
    if (!s.isOpen) return t;
    if (s.submitting) { t.hit = CharCreateHit::Blocked; return t; }
    if (s.messageEqstr || !s.messageText.empty()) {
        t.hit = inside(charCreateMessageOkRect(sw, sh), x, y) ? CharCreateHit::MessageOk
                                                              : CharCreateHit::Blocked;
        return t;
    }
    if (inside(L::backButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::Back; return t; }
    if (inside(L::nextButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::Next; return t; }
    if (inside(L::helpButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::Help; return t; }

    if (s.page == 0) {
        if (inside(L::maleButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::Male; return t; }
        if (inside(L::femaleButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::Female; return t; }
        for (int i = 0; i < static_cast<int>(s.races.size()) && i < L::kRaceSlots; ++i) {
            if (inside(L::raceSlotRect(sw, sh, i), x, y)) {
                t.hit = CharCreateHit::Race; t.index = i; t.id = s.races[static_cast<size_t>(i)].id;
                return t;
            }
        }
        for (int i = 0; i < static_cast<int>(s.classes.size()) && i < L::kClassSlots; ++i) {
            if (inside(L::classSlotRect(sw, sh, i), x, y)) {
                const uint32_t id = s.classes[static_cast<size_t>(i)].id;
                if (!isValidRaceClass(s.race, id)) return t;   // greyed: not hit-testable
                t.hit = CharCreateHit::Class; t.index = i; t.id = id;
                return t;
            }
        }
        if (inside(L::pointsLeftButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::PointsLeft; return t; }
        for (int n = 0; n < L::kStatRows; ++n) {
            if (inside(L::statUpRect(sw, sh, n), x, y)) { t.hit = CharCreateHit::StatUp; t.index = n; return t; }
            if (inside(L::statDownRect(sw, sh, n), x, y)) { t.hit = CharCreateHit::StatDown; t.index = n; return t; }
            if (inside(L::statButtonRect(sw, sh, n), x, y)) { t.hit = CharCreateHit::StatName; t.index = n; return t; }
        }
        return t;
    }

    if (inside(L::nameInputRect(sw, sh), x, y)) { t.hit = CharCreateHit::NameInput; return t; }
    if (inside(L::getNameButtonRect(sw, sh), x, y)) { t.hit = CharCreateHit::GetName; return t; }
    for (int i = 0; i < static_cast<int>(s.deities.size()) && i < L::kDeitySlots; ++i) {
        if (inside(L::deitySlotRect(sw, sh, i), x, y)) {
            const uint32_t id = s.deities[static_cast<size_t>(i)].id;
            if (!s.deityOffered(id)) return t;
            t.hit = CharCreateHit::Deity; t.index = i; t.id = id;
            return t;
        }
    }
    for (int i = 0; i < static_cast<int>(s.cities.size()) && i < L::kCitySlots; ++i) {
        if (inside(L::citySlotRect(sw, sh, i), x, y)) {
            const uint32_t id = s.cities[static_cast<size_t>(i)].id;
            if (!s.cityOffered(id)) return t;
            t.hit = CharCreateHit::City; t.index = i; t.id = id;
            return t;
        }
    }
    return t;
}

CharCreateAction applyCharCreateClick(CharCreateScreenState& s, const CharCreateClickTarget& t,
                                      bool rightButton) {
    CharCreateAction none;
    switch (t.hit) {
    case CharCreateHit::Back: return pressBack(s);
    case CharCreateHit::Next: return pressNext(s);
    case CharCreateHit::Help:
        // eqstr 5874 (page 0) / 5875 (page 1): the CharacterCreationHelp text.
        s.helpEqstr = s.page == 0 ? 5874u : 5875u;
        s.helpTitle.clear();
        return none;
    case CharCreateHit::Male: selectGender(s, Gender::Male); return none;
    case CharCreateHit::Female: selectGender(s, Gender::Female); return none;
    case CharCreateHit::Race: selectRace(s, t.id); return none;
    case CharCreateHit::Class: selectClass(s, t.id); return none;
    case CharCreateHit::StatName:
        if (rightButton) statMax(s, t.index); else showStatHelp(s, t.index);
        return none;
    case CharCreateHit::StatUp: statUp(s, t.index); return none;
    case CharCreateHit::StatDown: statDown(s, t.index); return none;
    case CharCreateHit::PointsLeft: resetPoints(s); return none;
    case CharCreateHit::Deity: selectDeity(s, t.id); return none;
    case CharCreateHit::City: selectCity(s, t.id); return none;
    case CharCreateHit::NameInput: focusTextEntry(s.nameEntry, s.nameEntry.inputText); return none;
    case CharCreateHit::GetName: return pressGetName(s);
    case CharCreateHit::MessageOk: dismissMessage(s); return none;
    case CharCreateHit::Blocked:
    case CharCreateHit::None: return none;
    }
    return none;
}

}  // namespace Graphics
}  // namespace EQT
