#pragma once
/*
 * B834 (GitLab #202, UIGAP-small) -- the tip windows: the Tip of the Day at
 * zone-in and the context tips that pop the first time a window opens.
 *
 * ORACLE, all from the client's own files and the UIGAP-ORACLE grabs:
 *   - the trigger -> tip pairs observed (review/UIGAP-ORACLE/202/README.md):
 *     zone-in -> Tip of the Day; first open of Inventory, Quantity, Bank,
 *     Merchant, a world container, Trainer, Friends; /consider; trade; give.
 *     B834 (T499's sweep) adds Looting (corpse loot), Keyboard Options
 *     (Options > Keys), Sitting (/camp), Death and Consider;
 *   - each tip's TEXT and TITLE are help/tips/ctx<name>.html (<TITLE> is the
 *     window caption, e.g. "Bank Tip window"); the Tip of the Day's entries
 *     are help/tips/TOTD.txt, separated by "###", each headed by a <c> line
 *     ("Gameplay Tip") -- the measured grab shows exactly that heading;
 *   - each tip's INI KEY is quoted from eqgame.exe's own table, which pairs
 *     every ctx file with the key the client writes (`strings eqgame.exe`:
 *     "ctxinventory.html" "Inventory", "ctxbank.html" "Bank",
 *     "ctxcontainer.html" "WorldContainer", "ctxgive.html" "GiveNPC", ...);
 *   - the keys land in UI_<char>_<server>.ini `[Tips]`: real player files
 *     carry `ShowTips=0`, `ShowTOTD=0`, `Inventory=0`, `Trainer=0`, `Sit=0`,
 *     `Spellbook=0`, `OptDisplay=0`, `AAWindow=0` (27 files under
 *     /mnt/projects/EverQuestP1999). So "Don't show this tip again" writes
 *     `<Key>=0` and "Don't show any tips" writes `ShowTips=0`.
 *   - MEASURED (UIGAP-live2, review/UIGAP-ORACLE/202/README.md): `ShowTips=0`
 *     suppresses the context tips (Inventory, Quantity) but NOT the Tip of
 *     the Day, which still pops at zone-in with its "Don't show any tips" box
 *     unticked -- `ShowTOTD` alone gates it (shouldPopTipOfTheDay();
 *     TipRules.ShowTipsZeroSuppressesContextTipsButNotTheTipOfTheDay).
 *
 * ASSUMPTIONS AWAITING ORACLE (flagged, not silent):
 *   - a context tip pops once per SESSION per character (the oracle grabs are
 *     all first opens in a fresh client; re-appearance after relog is
 *     unmeasured);
 *   - the Tip of the Day's one checkbox writes `ShowTOTD=0` (that window has
 *     no "this tip" box; both keys are in the real files);
 *   - which Tip of the Day entry shows first (fable: a rotating index);
 *   - the body's <c "#rrggbb"> colours are dropped (plain text).
 *
 * Wired triggers are the ones fable has a window for: Inventory, Quantity,
 * Bank, Merchant, WorldContainer, Trainer, Friends, Loot, Spellbook,
 * SkillsWindow, OptKeys, Trade, GiveNPC (B896's window), plus the Tip of the
 * Day. Consider/Sit/Death have no fable window or hook in this unit and are
 * not popped; B442b's confirmation dialog is not in the oracle table.
 */

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "client/graphics/ui/tip_layout.h"
#include "client/graphics/ui/tip_preferences.h"
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {

class UIRenderer;

/** One row of eqgame.exe's ctx-file -> ini-key table. */
struct TipCatalogEntry {
    const char* key;   // the [Tips] key, e.g. "Inventory"
    const char* file;  // help/tips/<file>, e.g. "ctxinventory.html"
};

/** The rows fable can trigger (subset of the exe's table, in its order). */
const std::vector<TipCatalogEntry>& tipCatalog();
/** The file for a key, or nullptr. */
const char* tipFileForKey(const std::string& key);

/** A parsed tip: the caption and the body as display lines (unwrapped). */
struct TipDocument {
    std::string title;
    std::vector<std::string> paragraphs;
};

/** help/tips/ctx*.html: <TITLE> is the caption; <br> breaks; tags dropped. */
TipDocument parseTipHtml(const std::string& html);
/** help/tips/TOTD.txt: entries split on "###"; the <c> line is the heading. */
std::vector<TipDocument> parseTipOfTheDay(const std::string& text);


struct TipWindowState {
    bool isOpen = false;
    bool isTipOfTheDay = false;
    std::string key;                 // the ctx key, or "" for the TOTD
    std::string title;               // the caption (and the History text)
    std::vector<std::string> lines;  // wrapped to the display's width
    int scroll = 0;                  // first visible line
    int totdIndex = 0;               // next Tip of the Day entry

    // The two checkboxes, as shown; applied to the preferences on Close.
    bool noShowThis = false;
    bool noShowAll = false;

    // Hover, for the draw only.
    bool closeHovered = false;
    bool nextHovered = false;

    // Context tips already popped this session (per character).
    std::set<std::string> shownThisSession;
    bool totdShownThisSession = false;

    int maxScroll(const UILayout& layout) const;
};

/** Should `key`'s context tip pop now? */
bool shouldPopTip(const TipPreferences& prefs, const TipWindowState& state,
                  const std::string& key);
/** Should the Tip of the Day pop now? */
bool shouldPopTipOfTheDay(const TipPreferences& prefs, const TipWindowState& state);

/** Wrap `doc` into `state.lines` with `charsPerLine` columns. */
void layoutTipLines(TipWindowState& state, const TipDocument& doc, int charsPerLine);
/** Columns of fable's UI font the display holds (an estimate: 6 px a glyph). */
int tipCharsPerLine(const UILayout& layout);

/** Open `doc` as the context tip `key` (marks it shown this session). */
void openContextTip(TipWindowState& state, const std::string& key, const TipDocument& doc,
                    const UILayout& layout);
/** Open (or advance) the Tip of the Day from `entries`. */
void openTipOfTheDay(TipWindowState& state, const std::vector<TipDocument>& entries,
                     const UILayout& layout);

/** Close the window, folding the checkboxes into the preferences. */
void closeTipWindow(TipWindowState& state, TipPreferences& prefs);

enum class TipHit : uint8_t { None = 0, Body, Close, NextTip, NoShowThis, NoShowAll };
TipHit hitTestTipWindow(const UILayout& layout, const TipWindowState& state, int mx, int my);

/**
 * Apply one click. Returns true when the window consumed it. NextTip advances
 * the Tip of the Day (needs the entries); Close and the close box close.
 */
bool applyTipClick(TipWindowState& state, TipPreferences& prefs, TipHit hit,
                   const std::vector<TipDocument>& totdEntries, const UILayout& layout);

/** Mouse wheel over the display: scroll. True when consumed. */
bool scrollTipWindow(TipWindowState& state, const UILayout& layout, int mx, int my, float wheel);

void renderTipWindow(UIRenderer& ui, const UILayout& layout, const TipWindowState& state);

}  // namespace Graphics
}  // namespace EQT
