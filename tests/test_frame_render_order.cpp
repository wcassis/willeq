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
const size_t kGoldenSize = 310u;

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
