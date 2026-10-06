// G05 (refactor/G05_process_frame_render.md): the RENDER-PASS ORDER net for
// FrameComponent::processFrameRender().
//
// processFrameRender() was one 544-line body. G05 cut it into sixteen private
// passes, each a verbatim slice INCLUDING its measureSection() bracket, and
// left processFrameRender() as the ordered list of calls. The order is
// load-bearing in the strongest sense available in a renderer: depth and
// blending state, overlays that must paint on top of the geometry they
// annotate, the UI on top of that, and endScene()'s swap after all of it.
// Getting it wrong does not crash and does not fail a unit test -- it paints a
// different picture.
//
// WHY A TEXT PARSE AND NOT A RUNTIME OBSERVATION (G03 correction 9 -- check
// what the container preserves first): there is no container. Every pass is a
// direct call, and processFrameRender() cannot be run in a unit test at all --
// it needs a live Irrlicht device, a GL context, a scene manager, a loaded
// zone and a driver that can beginScene()/endScene(). The TU IS the order, so
// the test reads it.
//
// The golden was extracted from the PRE-G05 body (git show
// fable:src/client/graphics/frame/renderer_frame_render.cpp at c1917296),
// before a line of it was touched. Normalisation, mirrored exactly below:
// strip whitespace; drop blank lines, //-comment lines, preprocessor lines and
// brace-only lines.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string kRoot = WILLEQ_REPO_ROOT;
const std::string kTU = kRoot + "/src/client/graphics/frame/renderer_frame_render.cpp";
const std::string kGolden = kRoot + "/tests/goldens/frame_render_order_g05.txt";

// The golden's entry count. It is NOT a number to nudge until the suite goes
// green: the golden is a record of the PRE-G05 draw sequence PLUS the
// deliberate, reviewed edits listed here, each named by the POSITIONS it
// touched. A change is taught to the golden IN PLACE at those positions and
// this comment gains a line; it is NEVER regenerated from the tip, because a
// regenerated golden accepts whatever the tip says, which is the one property
// this golden exists to deny (T337's rule, batch-G correction 32).
//
//   * T335b: WorldLineBatch guard in the frustum block, entries 120-128.
//     Entry 127 changed text -- `ctx_.driver->setMaterial(debugMat);` became
//     `WorldLineBatch batch(ctx_, debugMat);` -- because the restore moved
//     into that guard's destructor. The count is UNCHANGED at 305: the guard
//     replaces the statement it supersedes rather than being added beside it,
//     and its destructor costs no statement at all. Entries 120-126 and 128
//     are byte-identical; nothing was reordered.
//   * T354 (B042): the zone-line debug border deleted, entry 279. That entry
//     was `ctx_.Overlays().drawZoneLineOverlay();`; the draw body could never
//     turn on (L03 had deleted setZoneLineDebug(), the sole writer of the
//     `inZoneLine_` gate) and it is a developer aid with no official-client
//     oracle, so the call, the body and the three members went together. The
//     count DROPS by one, 305 -> 304: a statement was removed and nothing
//     replaced it. Entries 0-278 are byte-identical; entries 280-304 became
//     279-303 with their text byte-identical; nothing was reordered.
//   * T415 (boards B321/B320): the tradeskill container window and the combat
//     abilities window added to renderStaticUI(), entries 276 and 277 --
//     inserted directly after entry 275 `renderLootPopup(...)`, which is
//     where the two draws go (the container window is the last centre popup;
//     the combat abilities window is a HUD panel that draws above them).
//     Plus the two panels' input hop, entries 257-259 -- the three lines of
//     `staticUIInput_->processPanelInput(...)`, inserted directly after entry
//     256, the last line of the existing processInput() call, and before
//     `pendingUIClick_ = false;` which both calls read.
//     The count RISES by five, 304 -> 309: five statements were added and
//     nothing was removed or replaced. Entries 0-256 are byte-identical;
//     everything after each insertion point shifts with its text
//     byte-identical; nothing was reordered.
//   * T423 (board B362): the scene-ambient recompute added as the FIRST render
//     pass, entry 3 -- `if (owner_.isSceneAmbientDirty())
//     owner_.updateTimeOfDay(ctx_.currentHour, ctx_.currentMinute);`, inserted
//     directly after entry 2 `return false;  // Signal loading screen was
//     shown` and before entry 3 (now 4), the first line of
//     runSceneProfileIfScheduled(). It goes there because the ambient it sets
//     is read by the same frame's drawAll() and zone-shader uniforms, so it
//     must precede beginScene(). The count RISES by one, 309 -> 310: one
//     statement was added and nothing was removed or replaced. Entries 0-2 are
//     byte-identical; entries 3-308 became 4-309 with their text
//     byte-identical; nothing was reordered.
//   * T421 (board B298, item half): the item display window. Its INPUT hop,
//     entries 260-262 -- the three lines of
//     `staticUIInput_->processItemDisplayInput(...)`, inserted directly after
//     entry 259 (the last line of T415's processPanelInput call) and before
//     `pendingUIClick_ = false;`, which all three input calls read. And its
//     DRAW, entry 285 -- `renderItemDisplayPopup(...)`, inserted directly
//     after entry 284 `renderCombatAbilityPanel(...)` and before
//     `renderXPBar(...)`, because the oracle's ItemDisplayWindow has its own
//     absolute placement (`RelativePosition false`, (0,80)) and overlaps
//     whatever is beneath it, so it is drawn last of the windows.
//     The count RISES by four, 309 -> 313: four statements were added and
//     nothing was removed or replaced. Entries 0-259 are byte-identical;
//     everything after each insertion point shifts with its text
//     byte-identical; nothing was reordered (verified by diffing the composed
//     list against the pre-edit golden -- the only hunks are the two
//     insertions).
//   * T425 (board B298, spell half): the spell display window -- which is
//     the SAME window, because the oracle has no second one (there is no
//     EQUI_SpellDisplay.xml in uifiles/default at all). So NO draw was added
//     and NO input entry point was added; the only change is entry 257, the
//     last argument line of the existing `staticUIInput_->processInput(...)`
//     call, which gained `ctx_.eventReceiver->isKeyDown(irr::KEY_SHIFT)`.
//     SHIFT had to be an argument to THAT call rather than its own entry
//     point (the T415/T421 shape) because it modifies two clicks the same
//     call already handles -- a gem click casts and a spellbook click
//     memorises -- so a separate later pass would fire after they had
//     already acted. The count is UNCHANGED at 314: one entry's TEXT
//     changed and nothing was added, removed or reordered.
//   * MERGE (T423 + T421, orchestrator): both learnings composed -- T423's
//     entry 3 plus T421's four insertions, 309 -> 314; the golden was rebuilt
//     from the branch list with T423's statement re-inserted at index 3.
//   * T434: the Options window. TWO pure insertions, no text change and no
//     reorder anywhere (verified by diffing the composed list against the
//     pre-edit golden -- the only hunks are these two).
//     - entries 264-268, the input hop: the `OptionsLayout::Setting
//       optionsChanged` declaration, the three lines of the
//       `staticUIInput_->processOptionsInput(...)` call and
//       `applyOptionsSetting(optionsChanged);`. It goes LAST of the four
//       input entry points because the oracle's OptionsWindow is
//       `RelativePosition false` at its own <Location> and sits on top of
//       even the item display window (the T421 rule, one level higher).
//       It is its own entry point rather than an eleventh parameter on
//       processInput(), the T415/T421 shape. The Setting out-parameter is
//       there because two of the three wired controls need a bridge intent
//       carrying a DELTA and the third needs a renderer call, so the click
//       is resolved here and acted on in FrameComponent::applyOptionsSetting().
//     - entry 291, the draw: `renderOptionsWindow(...)` directly after
//       entry 290 `renderItemDisplayPopup(...)`, mirroring the hit-test
//       order above it, and before `renderXPBar(...)`.
//     The count rises 314 -> 320: five statements plus one, nothing removed
//     and nothing replaced.
//   * T436 (board B429): the merchant window, the bank window and the cursor
//     attachment. FOUR pure insertions, no text change and no reorder
//     anywhere (verified by diffing the composed list against the pre-edit
//     golden -- the only hunks are these four).
//     - entries 262-266, the input hop: the two `cursorState_.mouseX/Y`
//       writes and the three lines of the
//       `staticUIInput_->processCommerceInput(...)` call, inserted directly
//       after entry 261 (the last line of T415's processPanelInput call) and
//       before T421's processItemDisplayInput. It goes THERE, and not last,
//       because the bank window draws above the centre popups but below the
//       item display and the Options window, and the merchant window IS a
//       centre popup -- hit-test order mirrors draw order. The two mouse
//       writes are in this pass rather than the draw pass because this is
//       the one place that already holds the event receiver.
//     - entry 295, `renderMerchantPopup(...)`, directly after entry 294
//       `renderTradeskillPopup(...)`: the merchant window is the last centre
//       popup (Style_Sizable true, 505px tall from its own <Location>, so it
//       is scaled onto centerPopup the way T415 scaled the tradeskill one).
//     - entry 297, `renderBankWindow(...)`, directly after entry 296
//       `renderCombatAbilityPanel(...)`: NOT a centre popup (Style_Sizable
//       false at 108x347), so it draws above them.
//     - entry 301, `renderCursorAttachment(...)`, directly after entry 300
//       `renderXPBar(...)` and LAST of everything. It is the pointer:
//       nothing may cover it, not even the Options window or the XP bar.
//     The count rises 320 -> 328: five statements plus one plus one plus
//     one, nothing removed and nothing replaced.
//   * T490: the region-box debug overlay reads the camera region's PVS
//     through the new decoded-bitset handle (BspRegion::visibleRegions, a
//     dense vector<bool> per region, was regions^2/8 bytes across the tree
//     and OOM-killed the Orange Pi on iceclad). FIVE entries changed text and
//     nothing moved: 130 `std::shared_ptr<...BspRegion> pvsRegion;` became
//     `EQT::Graphics::PvsBits pvsRegion;`, 133 gained `->pvs()`, and 138,
//     141, 142 read `.empty()` / `.size()` / `[regionIdx]` on that handle
//     instead of `->visibleRegions.*`. The count is UNCHANGED at 328.
//     Entry 132 also gained `&& ctx_.zoneBspTree->regions[...]`: the old code
//     held the region as a shared_ptr and tested it (`if (pvsRegion && ...)`
//     at 138), so a null slot in `regions` was safe; calling `->pvs()` at 133
//     dereferences it, and the guard moved into the condition rather than
//     becoming a statement of its own. Still 328.
//   * B898 (#214): the skill trainer window. TWO pure insertions, no text
//     change and no reorder anywhere.
//     - entries 266-268, the three lines of
//       `staticUIInput_->processTrainerInput(...)`, directly after entry 265
//       (the last line of T436's processCommerceInput call) and before
//       processItemDisplayInput: the trainer window draws above the bank and
//       below the item display / Options windows, and hit-test order
//       mirrors draw order.
//     - entry 300, `renderTrainerWindow(...)`, directly after entry 299
//       `renderBankWindow(...)`: Style_Sizable false at 580x280, not a
//       centre popup, so it draws above them and above the bank.
//     The count rises 328 -> 332: three statements plus one, nothing removed
//     and nothing replaced.
//   * B442 (#151): the click-to-cursor gesture. THREE pure insertions, no
//     text change and no reorder (verified by diffing the composed list
//     against the pre-edit golden -- the only hunks are these three).
//     - entries 254-264, at the head of the static-UI input hop, before
//       entry 265 `staticUIInput_->processInput(`: the right-click latch,
//       the two modifiers, the QuantityWnd hit test (it is above every
//       window, so a click it takes is withheld from the rest) and
//       `setFrameClickState(...)`.
//     - entry 289, `setFrameClickState(false, {}, &cursorState_);`, directly
//       after entry 288 `pendingUIClick_ = false;`: the latched state is
//       spent with the frame.
//     - entries 316-317, `renderQuantityWindow(...)` and its null guard,
//       directly after entry 315 `renderXPBar(...)` and before the cursor
//       attachment: above every window, below the pointer.
//     Applied after B898 (rebased onto it); B898's entries above are
//     shifted by B442's eleven-line head insertion, nothing else moved.
//     The count rises 332 -> 346: eleven plus one plus two.
//   * B895 (GitLab #211): the Tracking Window, applied after B898 and B442
//     (rebased onto them). TWO pure insertions, no text change and no
//     reorder anywhere.
//     - entry 280, `processTrackingInput(ctx_.bridge, ...);` (one line),
//       directly after entry 279, the last line of B898's
//       processTrainerInput call, and before T421's processItemDisplayInput:
//       the window draws above the trainer and the bank and below the item
//       display, and the hit-test order mirrors the draw order.
//     - entry 314, `renderTrackingWindow(...)`, directly after entry 313
//       `renderTrainerWindow(...)` (the oracle's TrackingWnd has its own
//       placement, so it is not a centre popup).
//     The count rises 346 -> 348, nothing removed and nothing replaced.
//   * B899 (#215 / #151 Leg B): the era container windows, applied after
//     B898, B442 and B895 (rebased onto them). TWO pure insertions, no text
//     change and no reorder anywhere.
//     - entries 281-283, the three lines of
//       `staticUIInput_->processContainerInput(...)`, directly after entry
//       280 (B895's processTrackingInput) and before T421's
//       processItemDisplayInput: the oracle's ContainerWindow is
//       `RelativePosition false` at its own <Location>, so it sits above the
//       centre popups, the bank, the trainer and the tracking window and
//       below the item display / Options.
//     - entry 318, `renderContainerWindows(...)`, directly after entry 317
//       `renderTrackingWindow(...)`, mirroring that hit-test order.
//     The count rises 348 -> 352: three statements plus one.
//   * B897 (GitLab #213): the Friends window. TWO pure insertions, no text
//     change and no reorder anywhere.
//     - entry 287, `processFriendsWindowInput();`, directly after the last
//       line of T421's processItemDisplayInput call and before T434's
//       Options hop: the Friends window is `RelativePosition false` at its
//       own placement, so it sits above the item display and below the
//       Options window, and hit-test order mirrors draw order. It is a
//       FrameComponent method rather than a StaticUIInput entry point
//       because an Add/Delete/Who click pushes an intent and a Contact click
//       opens the chat prefill, both of which only FrameComponent can do.
//     - entry 321, `renderFriendsWindow(...)`, directly after
//       `renderItemDisplayPopup(...)` and before `renderOptionsWindow(...)`.
//     The count rises 352 -> 354.
//   * B442b (#151 remainder): the drop click, the held item display and the
//     confirmation dialog. NINE text changes and THREE insertions, no
//     reorder (applied by editing the golden in place, never regenerated):
//     - entry 259 (new), `mods.alt = ...KEY_LMENU || ...KEY_RMENU;`, directly
//       after the ctrl modifier: ALT+LEFT is the kept item display (row 26).
//     - entry 260 (was 259) changed `processQuantityInput(` to
//       `processDialogInput(`: the confirmation dialog is hit-tested above
//       the QuantityWnd, which that call still asks second.
//     - entries 266, 270, 275, 278, 281, 282, 285, 290 (were 265, 269, 274,
//       277, 280, 281, 284, 289) gained `bool uiTook = ` / `uiTook |= `: the
//       entry points' existing return values are collected so a world click
//       is told from a window click (rows 31-33). Nothing else on those
//       lines changed.
//     - entry 294 (new), `processCursorGestureFrame(uiTook);`, directly after
//       `applyOptionsSetting(optionsChanged);` and before
//       `pendingUIClick_ = false;`, which it reads.
//     - entry 328 (new), `renderConfirmationDialog(...)`, directly after
//       `renderQuantityWindow(...)` inside the same guard: the topmost modal,
//       below only the pointer.
//     The count rises 354 -> 357.
//   * B896 (GitLab #212): the trade / give window, applied after B442b (rebased onto it). TWO pure insertions, no
//     text change and no reorder anywhere (the renumbering of the entries
//     after each is the only other hunk).
//     - entry 266, `processTradeWindowInput();`, directly after entry 265
//       (B442's `setFrameClickState(rightClick, ...)`) and before entry 267
//       `bool uiTook = staticUIInput_->processInput(`: the window is drawn above the centre
//       popups, the bank, the trainer, the tracking and container windows,
//       and the inventory is open beside it (review/UIGAP-ORACLE/212/
//       trade.tsv row 1), so it takes its click first and spends it. A
//       FrameComponent method (renderer_frame_trade.cpp), like B897's.
//     - entry 323, `renderTradeWindow(...)`, directly after entry 322
//       `renderContainerWindows(...)` and before the item display.
//     The count rises 357 -> 359.
//   * B834 (GitLab #202, UIGAP-small): the tip windows, applied after B896
//     (rebased onto it). TWO pure insertions, no text change and no reorder.
//     - entry 266, `if (processTipWindowInput()) pendingUIClick_ = false;`,
//       directly before B896's `processTradeWindowInput();`: the tip window
//       draws above every window but the QuantityWnd, so it is asked before
//       them and keeps the click it takes.
//     - entry 328, `renderTipWindow(...)`, directly after
//       `renderOptionsWindow(...)` and before `renderXPBar(...)`.
//     The count rises 359 -> 361.
const size_t kGoldenSize = 361u;

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot open " << path;
    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

std::string strip(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// The one normalisation, shared by the extractor and this test.
bool keep(const std::string& raw) {
    const std::string s = strip(raw);
    if (s.empty()) return false;
    if (s.rfind("//", 0) == 0) return false;
    if (s.rfind("#", 0) == 0) return false;
    if (s.find_first_not_of("{};") == std::string::npos) return false;
    return true;
}

// Every `bool|void FrameComponent::<name>(...)` defined at column 0 in the TU,
// mapped to its normalised body. A body ends at the first column-0 "}".
std::map<std::string, std::vector<std::string>> parseDefinitions() {
    const std::vector<std::string> lines = readLines(kTU);
    std::map<std::string, std::vector<std::string>> defs;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        size_t marker = std::string::npos;
        if (l.rfind("void FrameComponent::", 0) == 0) marker = 21;
        else if (l.rfind("bool FrameComponent::", 0) == 0) marker = 21;
        if (marker == std::string::npos) continue;
        size_t paren = l.find('(', marker);
        if (paren == std::string::npos) continue;
        const std::string name = l.substr(marker, paren - marker);
        std::vector<std::string> body;
        size_t j = i + 1;
        for (; j < lines.size(); ++j) {
            if (lines[j] == "}") break;
            if (keep(lines[j])) body.push_back(strip(lines[j]));
        }
        defs[name] = body;
        i = j;
    }
    return defs;
}

// A line that is exactly `name();` or `name(deltaTime);`.
bool asPassCall(const std::string& s, std::string* name) {
    if (s.size() < 4 || s.back() != ';') return false;
    size_t paren = s.find('(');
    if (paren == std::string::npos || paren == 0) return false;
    const std::string args = s.substr(paren + 1, s.size() - paren - 3);
    if (s[s.size() - 2] != ')') return false;
    if (!(args.empty() || args == "deltaTime")) return false;
    const std::string id = s.substr(0, paren);
    for (char c : id) {
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    *name = id;
    return true;
}

// Walk processFrameRender()'s body in order; a line that calls a function
// DEFINED IN THIS TU expands RECURSIVELY to that function's body (G04
// correction 20). Everything else is emitted as itself.
void compose(const std::string& fn,
             const std::map<std::string, std::vector<std::string>>& defs,
             std::vector<std::string>* out,
             std::vector<std::string>* calls,
             int depth) {
    ASSERT_LT(depth, 8) << "render-pass expansion recursed too deep at " << fn;
    auto it = defs.find(fn);
    ASSERT_NE(it, defs.end()) << "no definition for " << fn;
    for (const std::string& s : it->second) {
        std::string callee;
        if (asPassCall(s, &callee) && defs.count(callee)) {
            calls->push_back(callee);
            compose(callee, defs, out, calls, depth + 1);
        } else {
            out->push_back(s);
        }
    }
}

std::vector<std::string> goldenEntries() {
    std::vector<std::string> out;
    for (const std::string& l : readLines(kGolden)) {
        if (l.empty()) continue;
        size_t sp = l.find(' ');
        if (sp == std::string::npos) { out.push_back(""); continue; }
        out.push_back(l.substr(sp + 1));
    }
    return out;
}

// ---------------------------------------------------------------------------

// Case 1: the empty-file trap (G01 correction 5 / K11 ee) -- in its own case,
// so nothing below can pass vacuously against a truncated or missing golden.
TEST(FrameRenderOrder, GoldenIsPresentAndTheRightSize) {
    const std::vector<std::string> golden = goldenEntries();
    ASSERT_FALSE(golden.empty()) << "the render-order golden is empty or missing: " << kGolden;
    EXPECT_EQ(golden.size(), kGoldenSize)
        << "the render-order golden changed size. It is a record of the PRE-G05 draw "
           "sequence plus the reviewed edits named above kGoldenSize, and it is never "
           "REGENERATED from the tip -- a genuine change is taught to it IN PLACE at "
           "the positions it touches, and named there.";
}

// Case 2: every pass is reached exactly once, and no pass is orphaned.
TEST(FrameRenderOrder, EveryPassIsCalledExactlyOnceAndNoneIsOrphaned) {
    const auto defs = parseDefinitions();
    ASSERT_TRUE(defs.count("processFrameRender"));
    std::vector<std::string> composed, calls;
    compose("processFrameRender", defs, &composed, &calls, 0);

    for (size_t i = 0; i < calls.size(); ++i) {
        for (size_t j = i + 1; j < calls.size(); ++j) {
            EXPECT_NE(calls[i], calls[j]) << "render pass called twice: " << calls[i];
        }
    }
    EXPECT_EQ(calls.size(), 17u) << "expected seventeen render passes in processFrameRender()";
    for (const auto& kv : defs) {
        if (kv.first == "processFrameRender") continue;
        EXPECT_NE(std::find(calls.begin(), calls.end(), kv.first), calls.end())
            << kv.first << "() is defined in renderer_frame_render.cpp but never called from "
               "processFrameRender() -- the pass is dead and whatever it drew is gone";
    }
}

// Case 3: the anti-regrowth clause (G04 correction 16). processFrameRender()
// carries nothing but the loading-screen guard and the ordered list; work
// appended to the driver would sit outside every pass.
TEST(FrameRenderOrder, ProcessFrameRenderIsOnlyTheOrderedList) {
    const auto defs = parseDefinitions();
    ASSERT_TRUE(defs.count("processFrameRender"));
    const std::vector<std::string>& body = defs.at("processFrameRender");
    EXPECT_LT(body.size(), 40u) << "processFrameRender() has grown work of its own again";
    const std::vector<std::string> allowed{
        "if (ctx_.loadingScreenVisible) {",
        "ctx_.Overlays().drawLoadingScreen(loadingProgress_, loadingText_);",
        "return false;  // Signal loading screen was shown",
        "LOG_TRACE(MOD_GRAPHICS, \"POST-DRAW checkpoint 12: frame complete\");",
        "FlushThreadLog();",
        "return true;",
    };
    for (const std::string& s : body) {
        std::string callee;
        if (asPassCall(s, &callee) && defs.count(callee)) continue;
        EXPECT_NE(std::find(allowed.begin(), allowed.end(), s), allowed.end())
            << "processFrameRender() contains a statement that is not a render-pass call: " << s;
    }
}

// Case 4: THE order. Position by position against the pre-G05 golden.
TEST(FrameRenderOrder, ComposedDrawOrderMatchesThePreG05Golden) {
    const auto defs = parseDefinitions();
    std::vector<std::string> composed, calls;
    compose("processFrameRender", defs, &composed, &calls, 0);
    const std::vector<std::string> golden = goldenEntries();

    ASSERT_EQ(composed.size(), golden.size())
        << "draw statement count changed: " << golden.size() << " -> " << composed.size();
    for (size_t i = 0; i < composed.size(); ++i) {
        if (composed[i] == golden[i]) continue;
        std::ostringstream ctx;
        for (size_t k = (i >= 3 ? i - 3 : 0); k < std::min(composed.size(), i + 3); ++k) {
            ctx << "\n  [" << k << "] want: " << golden[k] << "\n        got : " << composed[k];
        }
        FAIL() << "draw order diverges at statement " << i << ctx.str();
    }
}

}  // namespace
