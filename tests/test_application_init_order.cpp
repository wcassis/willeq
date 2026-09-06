// G04 (refactor/G04_application_initialize.md) -- the ORDER net for
// Application::initialize().
//
// Why a text parse and not a runtime observation (Batch G template
// correction 9: check what the container preserves first): there is no
// container here at all. Application::initialize() brings up the whole
// client -- config validation, the EverQuest client, the spell system, the
// action layer, the game mode, the input bridge, graphics, the console
// bridge, the login connection -- and the ONLY record of the order is the
// translation unit itself. Running it is not an option for a unit test
// either: it opens a database, stats the EQ client directory, installs
// signal handlers, creates a renderer and connects to a login server.
// So the TU IS the order, and this file reads it.
//
// The order is load-bearing in at least these ways, each of which a swap
// would break silently rather than loudly:
//   * everything from initSpellSystem() onward dereferences m_eqClient,
//     which initEverQuestClient() creates;
//   * initActionLayer(), initGameMode() and initInputBridge() all take
//     m_eqClient->GetGameState(), and the input bridge needs the dispatcher
//     and the command processor the action layer made;
//   * initRendererAndGraphics() hands m_renderer and m_bridge to
//     EverQuest::InitGraphics(), and the constrained texture cache it
//     attaches is built by initConstrainedRendererConfig();
//   * initConsoleBridge() attaches a headless bridge only when the graphics
//     step did NOT (`!m_graphicsInitialized && !m_bridge`), so moving it
//     ahead of graphics would attach a console bridge to every run;
//   * initLoginConnection() is deliberately LAST -- see its comment: DRM/EGL
//     bring-up takes 4-5 s on an Orange Pi and the login server drops a
//     connection that nothing pumps during that window.
//
// The golden was extracted mechanically from the PRE-G04 source
//     git show fable:src/client/application.cpp        (at e00e17bb)
// -> tests/goldens/application_init_order_g04.txt (316 entries), before a
// line of it was touched. Composition here: walk initialize()'s body in
// order; a line `if (!initX(config)) return false;` naming a function
// defined in this TU expands (recursively) to that function's body minus
// its trailing `return true;`; every other line is emitted as itself.
// Normalisation on both sides: strip whitespace, drop blank lines,
// //-comments, preprocessor lines and brace-only lines.
//
// A swapped step call, a dropped step call, a statement moved across a cut
// and a statement added or deleted all fail here. A genuine change to
// startup order is meant to require a matching edit to the golden -- that
// edit is the review moment.

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

#ifndef WILLEQ_REPO_ROOT
#define WILLEQ_REPO_ROOT "."
#endif

std::string GoldenPath() {
    return std::string(WILLEQ_REPO_ROOT) +
           "/tests/goldens/application_init_order_g04.txt";
}

std::string SourcePath() {
    return std::string(WILLEQ_REPO_ROOT) + "/src/client/application.cpp";
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << "cannot open: " << path;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

std::string Strip(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// "<ordinal> <normalised source line>" per line, '#' comments skipped.
std::vector<std::string> LoadGolden() {
    std::vector<std::string> entries;
    for (const auto& line : ReadLines(GoldenPath())) {
        if (line.empty() || line[0] == '#') continue;
        auto sp = line.find(' ');
        if (sp == std::string::npos) continue;
        entries.push_back(line.substr(sp + 1));
    }
    return entries;
}

// The normalisation, applied identically to the golden's source and here.
// Returns false for a line that carries no statement.
bool Normalise(const std::string& raw, std::string* out) {
    std::string s = Strip(raw);
    if (s.empty()) return false;
    if (s.rfind("//", 0) == 0) return false;
    if (s[0] == '#') return false;
    if (s == "{" || s == "}" || s == "};") return false;
    *out = s;
    return true;
}

// `bool Application::<name>(` at column 0 .. the matching `}` at column 0.
std::map<std::string, std::vector<std::string>> ParseFunctions() {
    const auto lines = ReadLines(SourcePath());
    const std::string kPrefix = "bool Application::";
    std::map<std::string, std::vector<std::string>> fns;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind(kPrefix, 0) != 0) continue;
        auto paren = lines[i].find('(');
        if (paren == std::string::npos) continue;
        const std::string name =
            lines[i].substr(kPrefix.size(), paren - kPrefix.size());
        size_t j = i + 1;
        while (j < lines.size() && lines[j] != "}") ++j;
        if (j >= lines.size()) continue;
        fns[name] = std::vector<std::string>(lines.begin() + i + 1,
                                             lines.begin() + j);
        i = j;
    }
    return fns;
}

// `if (!<name>(config)) return false;` -> <name>, else "".
std::string StepCallee(const std::string& stripped) {
    const std::string a = "if (!";
    const std::string b = "(config)) return false;";
    if (stripped.rfind(a, 0) != 0) return "";
    if (stripped.size() <= a.size() + b.size()) return "";
    if (stripped.compare(stripped.size() - b.size(), b.size(), b) != 0) return "";
    return stripped.substr(a.size(), stripped.size() - a.size() - b.size());
}

struct Composed {
    std::vector<std::string> order;
    std::vector<std::string> steps;   // step names, in call order (nested included)
    std::string error;
};

void Expand(const std::map<std::string, std::vector<std::string>>& fns,
            const std::string& name, bool isStep, int depth, Composed* c) {
    if (depth > 8) { c->error = "step call nesting deeper than 8 at " + name; return; }
    auto it = fns.find(name);
    if (it == fns.end()) { c->error = name + "() is not defined in this TU"; return; }
    std::vector<std::string> body = it->second;
    if (isStep && !body.empty() && Strip(body.back()) == "return true;") {
        body.pop_back();
    }
    for (const auto& raw : body) {
        const std::string s = Strip(raw);
        const std::string callee = StepCallee(s);
        if (!callee.empty() && fns.count(callee)) {
            c->steps.push_back(callee);
            Expand(fns, callee, true, depth + 1, c);
            if (!c->error.empty()) return;
            continue;
        }
        std::string n;
        if (Normalise(raw, &n)) c->order.push_back(n);
    }
}

Composed Compose() {
    Composed c;
    Expand(ParseFunctions(), "initialize", false, 0, &c);
    return c;
}

std::string Join(const std::vector<std::string>& v, size_t from, size_t count) {
    std::string out;
    for (size_t i = from; i < v.size() && i < from + count; ++i) {
        if (!out.empty()) out += "\n            ";
        out += v[i];
    }
    return out;
}

} // namespace

// Correction 5 (Batch G template / K11 ee): everything below is vacuous
// against a missing or truncated golden, so the golden is checked first, in
// its own case.
TEST(ApplicationInitOrderTest, GoldenIsPresentAndTheRightSize) {
    const auto golden = LoadGolden();
    ASSERT_FALSE(golden.empty())
        << "golden is empty or unreadable: " << GoldenPath();
    // kGoldenSize: 316 at G04 (the pre-G04 body's normalised line count), then
    // -2 by T347 (B051), which DELETED entries 163 `bool customMemorySpec =
    // false;` and 166 `customMemorySpec = true;` -- a write-only flag whose
    // enclosing if/else calls loadJsonOverrides() identically in both arms,
    // then +1 by T357 (B057), which ADDED entry 256
    // `m_renderer->setCommandCompletionProvider(...)` right after
    // `m_renderer->setBridge(...)` -- Application is the only holder of both
    // command registries, so it is the only object that can supply the chat
    // box's Tab-completion vocabulary, then -1 by T402 (B246), which DELETED
    // entry 46 `checkFile(... "config/race_models.json" ...)` -- a startup
    // requirement for a file no runtime loader opens.
    // Correction 32: an order golden learns a reviewed edit in place, at its
    // positions, named here; it is never regenerated from the tip.
    EXPECT_EQ(golden.size(), 314u)
        << "316 statement lines at G04, -2 for T347's write-only "
           "customMemorySpec, +1 for T357's completion-provider wiring, "
           "-1 for T402's S03 race_models.json check (B246); "
           "the golden has " << golden.size();
    for (const auto& e : golden) EXPECT_FALSE(e.empty());
}

TEST(ApplicationInitOrderTest, EveryStepIsCalledExactlyOnceAndNoneIsOrphaned) {
    const auto fns = ParseFunctions();
    ASSERT_EQ(fns.count("initialize"), 1u)
        << "Application::initialize() not found in " << SourcePath();

    const auto c = Compose();
    ASSERT_TRUE(c.error.empty()) << c.error;
    EXPECT_FALSE(c.steps.empty()) << "initialize() calls no init step at all";

    std::set<std::string> seen;
    for (const auto& s : c.steps) {
        EXPECT_TRUE(seen.insert(s).second) << s << "() is called twice";
    }
    // Every init*() defined in this TU is reached -- an orphaned step would
    // silently drop a whole subsystem's bring-up.
    for (const auto& kv : fns) {
        if (kv.first.rfind("init", 0) != 0) continue;
        if (kv.first == "initialize") continue;
        EXPECT_EQ(seen.count(kv.first), 1u)
            << kv.first << "() is defined but never called from initialize()";
    }
}

TEST(ApplicationInitOrderTest, InitializeIsOnlyTheOrderedList) {
    const auto fns = ParseFunctions();
    auto it = fns.find("initialize");
    ASSERT_NE(it, fns.end());
    // initialize() may hold the config copy, the two bookend log lines, the
    // step calls and `return true;` -- nothing else. Anything more has crept
    // back into the driver and is outside every step the golden pins.
    size_t statements = 0;
    for (const auto& raw : it->second) {
        std::string n;
        if (!Normalise(raw, &n)) continue;
        ++statements;
        const bool allowed =
            !StepCallee(n).empty() ||
            n == "m_config = config;" ||
            n == "return true;" ||
            n.rfind("LOG_INFO(MOD_MAIN,", 0) == 0;
        EXPECT_TRUE(allowed)
            << "initialize() carries a statement that is not a step call: " << n;
    }
    EXPECT_LT(statements, 40u)
        << "initialize() has grown back to " << statements << " statements";
}

TEST(ApplicationInitOrderTest, ComposedStartupOrderMatchesThePreG04Golden) {
    const auto golden = LoadGolden();
    ASSERT_FALSE(golden.empty());

    const auto c = Compose();
    ASSERT_TRUE(c.error.empty()) << c.error;

    ASSERT_EQ(c.order.size(), golden.size())
        << "startup statement count changed: " << golden.size() << " -> "
        << c.order.size()
        << " -- a step call was dropped, duplicated, or a statement was "
           "added to or removed from a step";

    for (size_t i = 0; i < golden.size(); ++i) {
        ASSERT_EQ(c.order[i], golden[i])
            << "startup order diverges at statement " << (i + 1) << ":\n"
            << "  golden: " << Join(golden, i, 4) << "\n"
            << "  actual: " << Join(c.order, i, 4) << "\n"
            << "Order is load-bearing -- see the header comment in this file.";
    }
}
