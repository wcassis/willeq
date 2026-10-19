/*
 * B834 (#202) -- the tip windows. See tip_window.h for the oracle and the
 * flagged assumptions; the geometry is tip_layout.h.
 */

#include "client/graphics/ui/tip_window.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "client/graphics/text_batch.h"
#include "client/graphics/ui/ui_renderer.h"

namespace EQT {
namespace Graphics {

namespace {

const irr::video::SColor kText(255, 230, 230, 230);
const irr::video::SColor kTitle(255, 220, 220, 230);
const irr::video::SColor kFrame(255, 110, 110, 130);

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Collapse runs of whitespace to one space (HTML's own rule).
std::string collapse(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

// Tags out, <br> to a newline, the few entities the files use decoded.
std::string stripMarkup(const std::string& html) {
    std::string out;
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] == '<') {
            const size_t end = html.find('>', i);
            if (end == std::string::npos) break;
            const std::string tag = lower(trim(html.substr(i + 1, end - i - 1)));
            if (tag == "br" || tag == "br/" || tag == "br /" || tag == "p" || tag == "/p") out += '\n';
            i = end + 1;
            continue;
        }
        if (html.compare(i, 6, "&nbsp;") == 0) { out += ' '; i += 6; continue; }
        if (html.compare(i, 5, "&amp;") == 0) { out += '&'; i += 5; continue; }
        if (html.compare(i, 4, "&lt;") == 0) { out += '<'; i += 4; continue; }
        if (html.compare(i, 4, "&gt;") == 0) { out += '>'; i += 4; continue; }
        out += (html[i] == '\n' || html[i] == '\r') ? ' ' : html[i];
        ++i;
    }
    return out;
}

std::vector<std::string> paragraphsOf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) out.push_back(collapse(trim(line)));
    // Drop leading/trailing blank paragraphs.
    while (!out.empty() && out.front().empty()) out.erase(out.begin());
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return r.isPointInside(irr::core::vector2di(x, y));
}

void drawCheckbox(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& box,
                  const irr::core::rect<irr::s32>& label, bool checked, const char* text) {
    ui.drawRect(box, irr::video::SColor(220, 20, 20, 26));
    ui.drawFrame(box, kFrame);
    if (checked) {
        irr::core::rect<irr::s32> in(box.UpperLeftCorner.X + 3, box.UpperLeftCorner.Y + 3,
                                     box.LowerRightCorner.X - 3, box.LowerRightCorner.Y - 3);
        ui.drawRect(in, irr::video::SColor(255, 220, 220, 220));
    }
    if (tb) tb->addText(text, label.UpperLeftCorner.X, label.UpperLeftCorner.Y, kText);
}

void drawButton(UIRenderer& ui, TextBatch* tb, const irr::core::rect<irr::s32>& r,
                const char* text, bool hovered) {
    ui.drawRect(r, hovered ? irr::video::SColor(220, 120, 60, 60)
                           : irr::video::SColor(200, 56, 62, 84));
    ui.drawFrame(r, kFrame);
    if (tb) tb->addTextCentered(text, r, kText);
}

}  // namespace

// eqgame.exe's table, in its own order, for the triggers fable has.
const std::vector<TipCatalogEntry>& tipCatalog() {
    static const std::vector<TipCatalogEntry> kCatalog = {
        {"Inventory", "ctxinventory.html"},
        {"Spellbook", "ctxspellbook.html"},
        {"OptKeys", "ctxkeyboardoptions.html"},
        {"Friends", "ctxfriends.html"},
        {"SkillsWindow", "ctxskills.html"},
        {"Merchant", "ctxmerchant.html"},
        {"Bank", "ctxbank.html"},
        {"Trainer", "ctxtrainer.html"},
        {"Loot", "ctxlooting.html"},
        {"WorldContainer", "ctxcontainer.html"},
        {"Quantity", "ctxquantity.html"},
        // B896's windows: both pop in the oracle run (README rows Trade, Give).
        {"GiveNPC", "ctxgive.html"},
        {"Trade", "ctxtrade.html"},
    };
    return kCatalog;
}

const char* tipFileForKey(const std::string& key) {
    for (const auto& e : tipCatalog()) {
        if (key == e.key) return e.file;
    }
    return nullptr;
}

TipDocument parseTipHtml(const std::string& html) {
    TipDocument doc;
    const std::string low = lower(html);
    const size_t t0 = low.find("<title>");
    const size_t t1 = low.find("</title>");
    if (t0 != std::string::npos && t1 != std::string::npos && t1 > t0) {
        doc.title = collapse(trim(html.substr(t0 + 7, t1 - t0 - 7)));
    }
    size_t body = low.find("<body");
    if (body == std::string::npos) {
        body = 0;
    } else {
        const size_t gt = low.find('>', body);
        body = gt == std::string::npos ? html.size() : gt + 1;
    }
    size_t bodyEnd = low.find("</body>", body);
    if (bodyEnd == std::string::npos) bodyEnd = html.size();
    doc.paragraphs = paragraphsOf(stripMarkup(html.substr(body, bodyEnd - body)));
    return doc;
}

std::vector<TipDocument> parseTipOfTheDay(const std::string& text) {
    std::vector<TipDocument> out;
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> block;
    auto flush = [&]() {
        std::vector<std::string> lines;
        for (const auto& l : block) {
            const std::string s = collapse(trim(stripMarkup(l)));
            if (!s.empty()) lines.push_back(s);
        }
        block.clear();
        if (lines.empty()) return;
        TipDocument d;
        d.title = "Tip of the Day";
        d.paragraphs.push_back(lines[0]);  // the <c> heading, e.g. "Gameplay Tip"
        d.paragraphs.push_back("");
        std::string body;
        for (size_t i = 1; i < lines.size(); ++i) {
            if (!body.empty()) body += ' ';
            body += lines[i];
        }
        if (!body.empty()) d.paragraphs.push_back(body);
        out.push_back(std::move(d));
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (trim(line) == "###") {
            flush();
            continue;
        }
        block.push_back(line);
    }
    flush();
    return out;
}

int TipWindowState::maxScroll(const UILayout& layout) const {
    return std::max(0, static_cast<int>(lines.size()) - TipLayout::visibleLines(layout));
}

bool shouldPopTip(const TipPreferences& prefs, const TipWindowState& state,
                  const std::string& key) {
    if (!prefs.showTips) return false;
    if (prefs.off.count(key)) return false;
    if (state.shownThisSession.count(key)) return false;
    return tipFileForKey(key) != nullptr;
}

bool shouldPopTipOfTheDay(const TipPreferences& prefs, const TipWindowState& state) {
    return prefs.showTotd && !state.totdShownThisSession;
}

int tipCharsPerLine(const UILayout& layout) {
    return std::max(10, (TipLayout::displayRect(layout).getWidth() - 8) / 6);
}

void layoutTipLines(TipWindowState& state, const TipDocument& doc, int charsPerLine) {
    state.lines.clear();
    for (const std::string& para : doc.paragraphs) {
        if (para.empty()) {
            state.lines.emplace_back();
            continue;
        }
        std::istringstream words(para);
        std::string word, line;
        while (words >> word) {
            if (!line.empty() && static_cast<int>(line.size() + 1 + word.size()) > charsPerLine) {
                state.lines.push_back(line);
                line.clear();
            }
            if (!line.empty()) line += ' ';
            line += word;
        }
        if (!line.empty()) state.lines.push_back(line);
    }
    state.scroll = 0;
}

void openContextTip(TipWindowState& state, const std::string& key, const TipDocument& doc,
                    const UILayout& layout) {
    state.isOpen = true;
    state.isTipOfTheDay = false;
    state.key = key;
    state.title = doc.title.empty() ? key + " Tip window" : doc.title;
    state.noShowThis = false;
    state.noShowAll = false;
    layoutTipLines(state, doc, tipCharsPerLine(layout));
    state.shownThisSession.insert(key);
}

void openTipOfTheDay(TipWindowState& state, const std::vector<TipDocument>& entries,
                     const UILayout& layout) {
    if (entries.empty()) return;
    const TipDocument& doc =
        entries[static_cast<size_t>(state.totdIndex) % entries.size()];
    state.totdIndex = static_cast<int>((static_cast<size_t>(state.totdIndex) + 1) % entries.size());
    state.isOpen = true;
    state.isTipOfTheDay = true;
    state.key.clear();
    state.title = "Tip of the Day";  // EQUI_TipWnd.xml <Text>
    state.noShowThis = false;
    if (!state.totdShownThisSession) state.noShowAll = false;
    layoutTipLines(state, doc, tipCharsPerLine(layout));
    state.totdShownThisSession = true;
}

void closeTipWindow(TipWindowState& state, TipPreferences& prefs) {
    if (!state.isOpen) return;
    if (state.isTipOfTheDay) {
        if (state.noShowAll) prefs.showTotd = false;   // ASSUMPTION, tip_window.h
    } else {
        if (state.noShowThis && !state.key.empty()) prefs.off.insert(state.key);
        if (state.noShowAll) prefs.showTips = false;
    }
    state.isOpen = false;
    state.closeHovered = false;
    state.nextHovered = false;
}

TipHit hitTestTipWindow(const UILayout& layout, const TipWindowState& state, int mx, int my) {
    if (!state.isOpen) return TipHit::None;
    if (!inside(TipLayout::windowRect(layout), mx, my)) return TipHit::None;
    if (inside(TipLayout::closeBoxRect(layout), mx, my)) return TipHit::Close;
    if (inside(TipLayout::closeButtonRect(layout), mx, my)) return TipHit::Close;
    if (state.isTipOfTheDay) {
        if (inside(TipLayout::nextTipRect(layout), mx, my)) return TipHit::NextTip;
    } else {
        if (inside(TipLayout::noShowThisBoxRect(layout), mx, my) ||
            inside(TipLayout::noShowThisLabelRect(layout), mx, my)) {
            return TipHit::NoShowThis;
        }
    }
    if (inside(TipLayout::noShowBoxRect(layout), mx, my) ||
        inside(TipLayout::noShowLabelRect(layout), mx, my)) {
        return TipHit::NoShowAll;
    }
    return TipHit::Body;
}

bool applyTipClick(TipWindowState& state, TipPreferences& prefs, TipHit hit,
                   const std::vector<TipDocument>& totdEntries, const UILayout& layout) {
    switch (hit) {
        case TipHit::None:
            return false;
        case TipHit::Body:
            return true;
        case TipHit::Close:
            closeTipWindow(state, prefs);
            return true;
        case TipHit::NextTip:
            openTipOfTheDay(state, totdEntries, layout);
            return true;
        case TipHit::NoShowThis:
            state.noShowThis = !state.noShowThis;
            return true;
        case TipHit::NoShowAll:
            state.noShowAll = !state.noShowAll;
            return true;
    }
    return false;
}

bool scrollTipWindow(TipWindowState& state, const UILayout& layout, int mx, int my, float wheel) {
    if (!state.isOpen || wheel == 0.0f) return false;
    if (!inside(TipLayout::displayRect(layout), mx, my)) return false;
    // Wheel +1 is away from the user: towards the top, like the chat panel.
    const int lines = static_cast<int>(-3.0f * wheel);
    state.scroll = std::max(0, std::min(state.maxScroll(layout), state.scroll + lines));
    return true;
}

void renderTipWindow(UIRenderer& ui, const UILayout& layout, const TipWindowState& state) {
    if (!state.isOpen) return;
    auto* tb = ui.getTextBatch();
    ui.drawPanelFor(TipLayout::windowRect(layout), UIWindow::Tip);

    const auto title = TipLayout::titleRect(layout);
    ui.drawRect(title, irr::video::SColor(200, 26, 26, 34));
    if (tb) tb->addTextCentered(state.title, title, kTitle);
    const auto box = TipLayout::closeBoxRect(layout);
    ui.drawRect(box, state.closeHovered ? irr::video::SColor(220, 120, 60, 60)
                                        : irr::video::SColor(200, 60, 60, 72));
    if (tb) tb->addTextCentered("X", box, kText);

    // TIP_Display (WDT_Inner, VScroll).
    const auto disp = TipLayout::displayRect(layout);
    ui.drawRect(disp, irr::video::SColor(220, 14, 14, 22));
    ui.drawFrame(disp, kFrame);
    if (tb) {
        const int visible = TipLayout::visibleLines(layout);
        for (int i = 0; i < visible; ++i) {
            const int idx = state.scroll + i;
            if (idx >= static_cast<int>(state.lines.size())) break;
            tb->addText(state.lines[static_cast<size_t>(idx)], disp.UpperLeftCorner.X + 4,
                        disp.UpperLeftCorner.Y + 3 + i * TipLayout::LINE_H, kText);
        }
    }

    if (state.isTipOfTheDay) {
        drawButton(ui, tb, TipLayout::nextTipRect(layout), "Show Another Tip", state.nextHovered);
    } else {
        // TIP_History: the measured grab reads the tip's own title.
        const auto hist = TipLayout::historyRect(layout);
        ui.drawRect(hist, irr::video::SColor(220, 14, 14, 22));
        ui.drawFrame(hist, kFrame);
        if (tb) tb->addText(state.title, hist.UpperLeftCorner.X + 4, hist.UpperLeftCorner.Y + 6, kText);
        drawCheckbox(ui, tb, TipLayout::noShowThisBoxRect(layout),
                     TipLayout::noShowThisLabelRect(layout), state.noShowThis,
                     "Don't show this tip again");
    }
    drawCheckbox(ui, tb, TipLayout::noShowBoxRect(layout), TipLayout::noShowLabelRect(layout),
                 state.noShowAll, "Don't show any tips");
    drawButton(ui, tb, TipLayout::closeButtonRect(layout), "Close", state.closeHovered);
}

}  // namespace Graphics
}  // namespace EQT
