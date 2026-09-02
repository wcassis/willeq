// T264: the intent/handler reachability sweep, pinned so it never has to be
// repeated by hand.
//
// Patterns #12, #13 and #15 were each found one at a time by a unit that
// happened to probe the route live. This test re-runs the four mechanical
// sweeps of refactor/T264_intent_reachability_sweep.md against the source
// tree on every ctest, the same way T223's RendererCommandCatalogTest parses
// irrlicht_renderer.cpp:
//
//   1. every RendererIntent variant member has a consumer arm in
//      eq_bridge.cpp, and every member NOT on the documented blocked list
//      has at least one production producer (pushIntent(...) outside
//      eq_bridge.cpp);
//   2. every GameEventType enumerator has at least one publisher outside the
//      consumer TUs and at least one case/subscription inside them;
//   3. every IActionHandler pure virtual is reached through some
//      ActionDispatcher method that a production caller (command processor,
//      input bridge, application, ...) invokes -- directly or via another
//      dispatcher method -- except the documented blocked ones;
//   4. every EverQuest::Send* declared in eq.h has a caller outside its own
//      definition.
//
// The blocked lists are asserted EXACTLY in both directions: wiring one of
// them makes this test fail with "no longer producer-less: remove it from
// the list", and adding a new orphan fails with the orphan's name. Either
// way the list in the T264 Review and the list here cannot drift apart.
//
// Text-level parsing, deliberately. A compile-time check cannot see whether
// a producer EXISTS (that is the whole defect class), and the greps are the
// same ones the sweep used -- if the parsing is wrong, the failure message
// names the file:line so it can be judged rather than trusted.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "cannot open " << p;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

bool isCommentLine(const std::string& line) {
    std::string t = trim(line);
    return t.rfind("//", 0) == 0 || t.rfind("*", 0) == 0 || t.rfind("/*", 0) == 0;
}

// The code part of a line (everything before a // comment).
std::string codePart(const std::string& line) {
    size_t p = line.find("//");
    return p == std::string::npos ? line : line.substr(0, p);
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Whole-word occurrence of `tok` in `s`.
bool containsToken(const std::string& s, const std::string& tok) {
    size_t pos = 0;
    while ((pos = s.find(tok, pos)) != std::string::npos) {
        bool leftOk = pos == 0 || !isIdentChar(s[pos - 1]);
        size_t end = pos + tok.size();
        bool rightOk = end >= s.size() || !isIdentChar(s[end]);
        if (leftOk && rightOk) return true;
        pos = end;
    }
    return false;
}

struct SourceFile {
    fs::path path;                    // absolute
    std::string rel;                  // relative to repo root, '/' separated
    std::vector<std::string> lines;
};

std::vector<SourceFile> collect(const std::vector<std::string>& dirs,
                                const std::set<std::string>& exts) {
    std::vector<SourceFile> out;
    for (const auto& d : dirs) {
        fs::path base = kRoot / d;
        if (!fs::exists(base)) continue;
        for (auto& e : fs::recursive_directory_iterator(base)) {
            if (!e.is_regular_file()) continue;
            if (!exts.count(e.path().extension().string())) continue;
            SourceFile f;
            f.path = e.path();
            f.rel = fs::relative(e.path(), kRoot).generic_string();
            f.lines = splitLines(readFile(e.path()));
            out.push_back(std::move(f));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const SourceFile& a, const SourceFile& b) { return a.rel < b.rel; });
    return out;
}

std::string join(const std::set<std::string>& s) {
    std::string out;
    for (const auto& x : s) { if (!out.empty()) out += ", "; out += x; }
    return out.empty() ? "(none)" : out;
}

// Assert two sets are equal, naming what is only on each side.
void expectSetsEqual(const std::set<std::string>& actual,
                     const std::set<std::string>& expected,
                     const char* actualLabel, const char* expectedLabel) {
    std::set<std::string> onlyActual, onlyExpected;
    std::set_difference(actual.begin(), actual.end(), expected.begin(), expected.end(),
                        std::inserter(onlyActual, onlyActual.end()));
    std::set_difference(expected.begin(), expected.end(), actual.begin(), actual.end(),
                        std::inserter(onlyExpected, onlyExpected.end()));
    EXPECT_TRUE(onlyActual.empty())
        << "NEW " << actualLabel << " (not on the documented list): " << join(onlyActual)
        << "\n  -> either wire a producer/caller, or add it to the list in "
           "tests/test_reachability_sweep.cpp AND the T264 Review's blocked table";
    EXPECT_TRUE(onlyExpected.empty())
        << "no longer " << actualLabel << " but still listed as " << expectedLabel
        << ": " << join(onlyExpected)
        << "\n  -> remove it from the list here and from the T264 Review's blocked table";
}

// ===================================================================
// 1. RendererIntent variant
// ===================================================================

std::vector<std::string> variantMembers() {
    auto lines = splitLines(readFile(kRoot / "include/client/events/renderer_intents.h"));
    std::vector<std::string> names;
    bool in = false;
    for (const auto& l : lines) {
        if (l.find("using RendererIntent = std::variant<") != std::string::npos) { in = true; continue; }
        if (!in) continue;
        if (trim(l).rfind(">;", 0) == 0) break;
        if (isCommentLine(l)) continue;
        std::string t = trim(codePart(l));
        while (!t.empty() && (t.back() == ',' || t.back() == ' ')) t.pop_back();
        if (!t.empty()) names.push_back(t);
    }
    return names;
}

// Intents whose consumer arm exists but which NO production code produces.
// Each blocks a user-facing feature (T264 Review, "blocked features"); the
// intent is kept as the natural target for the producer that feature needs.
// T268 wired the loot window ACTIONS (take one, loot all, close) as the
// /loot family of commands -- CommandProcessor -> ActionDispatcher ->
// EqActionHandler, the same last hops the shift+click gesture uses; T283
// then reported the missing UI-gesture leg as a FEATURE, not a wiring job,
// and left LootItemIntent/LootAllIntent/CloseLootIntent listed here.
// T290 BUILT that feature and DELISTED all three: the restored loot popup
// (renderLootPopup + LootPanelState, ported from the LootWindow U07c2
// deleted) produces them from StaticUIInput::processInput() -- a row click,
// the Loot All button, the Close button -- and processKey()'s ESC arm.
// They land on the SAME consumer bodies the commands do (eq_bridge.cpp ->
// LootItemFromCorpse / CombatManager::LootAll / CloseLootWindow), so there
// is one body per action, not two. Pinned by tests/test_loot_panel.
// DestroyAllLootIntent was DELETED: Titanium has no destroy-loot packet, so
// its arm only closed the window -- a second name for CloseLootIntent.
// T273 wired ReadItemIntent as the /read command (CommandProcessor ->
// ActionDispatcher -> EqActionHandler -> EverQuest::RequestReadItem), so it
// is no longer on this list.
// T280 delisted TrainSkillIntent and CloseTrainerIntent: /train <skill|id>
// and /trainclose produce them (CommandProcessor -> ActionDispatcher ->
// EqActionHandler -> EverQuest::RequestTrainSkill/RequestCloseTrainer).
// There is still no trainer PANEL to click, so the command is the only
// producer -- said out loud rather than implied.
const std::set<std::string> kBlockedIntents = {
    // T272 delisted MemorizeSpellIntent (spellbook click produces it);
    // Delisted as each got a producer: ReadItemIntent (T273 /read),
    // ScribeSpellIntent (T276 /scribe), BuffCancelIntent (T277 buff-bar
    // click + /removebuff), TradeskillCombineIntent + TradeskillCloseIntent
    // (T278 /combine, /closecontainer).
    "SpellbookStateIntent",
    // T278 delisted TradeskillCombineIntent/TradeskillCloseIntent (/combine,
    // /closecontainer); T280 delisted TrainSkillIntent/CloseTrainerIntent
    // (/train, /trainclose).
};

TEST(ReachabilitySweep, EveryIntentHasAConsumerArm) {
    auto members = variantMembers();
    ASSERT_GT(members.size(), 20u) << "variant parse failed";
    std::string bridge = readFile(kRoot / "src/client/eq_bridge_intents.cpp");
    for (const auto& n : members) {
        EXPECT_NE(bridge.find("is_same_v<T, eqt::events::" + n + ">"), std::string::npos)
            << "RendererIntent member " << n << " has no arm in eq_bridge_intents.cpp ProcessBridgeIntents";
    }
}

TEST(ReachabilitySweep, ProducerlessIntentsAreExactlyTheBlockedList) {
    auto members = variantMembers();
    ASSERT_GT(members.size(), 20u);
    auto files = collect({"src", "include"}, {".cpp", ".h"});

    std::set<std::string> producerless;
    std::map<std::string, std::string> firstProducer;
    for (const auto& n : members) {
        bool found = false;
        for (const auto& f : files) {
            if (f.rel == "src/client/eq_bridge.cpp") continue;
            if (f.rel == "src/client/eq_bridge_intents.cpp") continue;
            if (f.rel == "include/client/events/renderer_intents.h") continue;
            for (size_t i = 0; i < f.lines.size() && !found; ++i) {
                std::string code = codePart(f.lines[i]);
                if (!containsToken(code, n)) continue;
                bool push = code.find("pushIntent(") != std::string::npos;
                bool decl = code.find(n + " intent") != std::string::npos;  // PlayerPositionChanged
                if (push || decl) {
                    found = true;
                    firstProducer[n] = f.rel + ":" + std::to_string(i + 1);
                }
            }
            if (found) break;
        }
        if (!found) producerless.insert(n);
    }
    expectSetsEqual(producerless, kBlockedIntents, "producer-less intents", "blocked");
    // Every blocked intent must still have its consumer arm (test 1 covers
    // members generally; this pins that a listed one was not deleted
    // without being delisted).
    std::string bridge = readFile(kRoot / "src/client/eq_bridge_intents.cpp");
    for (const auto& n : kBlockedIntents) {
        EXPECT_NE(bridge.find("is_same_v<T, eqt::events::" + n + ">"), std::string::npos)
            << "blocked intent " << n << " lost its consumer arm";
    }
}

// ===================================================================
// 2. GameEventType
// ===================================================================

std::vector<std::string> eventEnumerators() {
    auto lines = splitLines(readFile(kRoot / "include/client/state/event_bus.h"));
    std::vector<std::string> names;
    bool in = false;
    for (const auto& l : lines) {
        if (l.find("enum class GameEventType") != std::string::npos) { in = true; continue; }
        if (!in) continue;
        if (trim(l).rfind("};", 0) == 0) break;
        if (isCommentLine(l)) continue;
        std::string t = trim(codePart(l));
        if (t.empty()) continue;
        size_t c = t.find(',');
        if (c != std::string::npos) t = t.substr(0, c);
        t = trim(t);
        if (!t.empty()) names.push_back(t);
    }
    return names;
}

bool isEventConsumerTu(const std::string& rel) {
    return rel == "src/client/bridge/irrlicht_bridge.cpp" ||
           rel == "src/client/bridge/console_bridge.cpp" ||
           rel == "src/client/output/console_renderer.cpp" ||
           rel == "src/client/mode/automated_mode.cpp";
}

TEST(ReachabilitySweep, EveryEventIsPublishedAndConsumed) {
    auto names = eventEnumerators();
    ASSERT_GT(names.size(), 50u) << "enum parse failed";
    auto files = collect({"src", "include"}, {".cpp", ".h"});

    std::set<std::string> unpublished, unconsumed;
    for (const auto& n : names) {
        std::string tok = "GameEventType::" + n;
        bool published = false, consumed = false;
        for (const auto& f : files) {
            if (f.rel == "include/client/state/event_bus.h") continue;
            bool consumerTu = isEventConsumerTu(f.rel);
            for (const auto& raw : f.lines) {
                std::string code = codePart(raw);
                if (!containsToken(code, tok)) continue;
                if (consumerTu) {
                    if (code.find("case ") != std::string::npos ||
                        code.find("subscribe(") != std::string::npos) consumed = true;
                } else {
                    published = true;
                }
            }
        }
        if (!published) unpublished.insert(n);
        if (!consumed) unconsumed.insert(n);
    }
    EXPECT_TRUE(unpublished.empty())
        << "GameEventType enumerators nothing publishes: " << join(unpublished);
    EXPECT_TRUE(unconsumed.empty())
        << "GameEventType enumerators no bridge/console consumer handles: " << join(unconsumed);
}

// ===================================================================
// 3. IActionHandler -> ActionDispatcher -> production caller
// ===================================================================

std::set<std::string> handlerPureVirtuals() {
    auto lines = splitLines(readFile(kRoot / "include/client/action/action_handler.h"));
    std::set<std::string> out;
    bool in = false;
    std::regex pv(R"(virtual\s+[A-Za-z_:<>0-9]+\s+(\w+)\s*\(.*\)\s*(const\s*)?=\s*0;)");
    for (const auto& l : lines) {
        if (l.find("class IActionHandler") != std::string::npos) { in = true; continue; }
        if (!in) continue;
        if (trim(l).rfind("};", 0) == 0) break;
        std::smatch m;
        if (std::regex_search(l, m, pv)) out.insert(m[1]);
    }
    return out;
}

// Handler methods that are the real last hop of a feature nothing reaches
// (see the T264 Review). T268 wired lootItem/lootAll (/loot <item>, /lootall);
// the list is empty -- a new orphan fails the test with its name.
const std::set<std::string> kBlockedHandlerMethods = {};

TEST(ReachabilitySweep, UnreachableHandlerMethodsAreExactlyTheBlockedList) {
    auto virtuals = handlerPureVirtuals();
    ASSERT_GT(virtuals.size(), 100u) << "IActionHandler parse failed";

    // Parse the dispatcher into methods.
    auto lines = splitLines(readFile(kRoot / "src/client/action/action_dispatcher.cpp") +
                            readFile(kRoot / "src/client/action/action_dispatcher_social.cpp") +
                            readFile(kRoot / "src/client/action/action_dispatcher_guild_misc.cpp"));
    std::regex def(R"(^\S.*\bActionDispatcher::(\w+)\s*\()");
    std::regex handlerCall(R"(m_handler->(\w+)\s*\()");
    std::map<std::string, std::set<std::string>> callsHandler;   // method -> handler methods
    std::map<std::string, std::set<std::string>> callsMethod;    // method -> other dispatcher methods
    std::set<std::string> methods;
    std::string cur;
    for (const auto& raw : lines) {
        std::smatch m;
        if (std::regex_search(raw, m, def)) { cur = m[1]; methods.insert(cur); continue; }
        if (cur.empty()) continue;
        std::string code = codePart(raw);
        auto b = std::sregex_iterator(code.begin(), code.end(), handlerCall);
        for (auto it = b; it != std::sregex_iterator(); ++it) callsHandler[cur].insert((*it)[1]);
    }
    // Internal dispatcher->dispatcher calls (checkGMMode, pet wrappers, ...).
    cur.clear();
    for (const auto& raw : lines) {
        std::smatch m;
        if (std::regex_search(raw, m, def)) { cur = m[1]; continue; }
        if (cur.empty()) continue;
        std::string code = codePart(raw);
        if (code.find("m_handler->") != std::string::npos) continue;
        for (const auto& other : methods) {
            if (other == cur) continue;
            size_t p = code.find(other + "(");
            if (p == std::string::npos) continue;
            if (p > 0 && (isIdentChar(code[p - 1]) || code[p - 1] == '>' || code[p - 1] == '.')) continue;
            callsMethod[cur].insert(other);
        }
    }

    // External callers.
    auto files = collect({"src", "include"}, {".cpp", ".h"});
    std::regex ext(R"((?:m_dispatcher|dispatcher_|dispatcher|m_actionDispatcher|actionDispatcher_)(?:\.|->)(\w+)\s*\()");
    std::set<std::string> reachable;
    for (const auto& f : files) {
        if (f.rel == "src/client/action/action_dispatcher.cpp") continue;
        if (f.rel == "src/client/action/action_dispatcher_social.cpp") continue;
        if (f.rel == "src/client/action/action_dispatcher_guild_misc.cpp") continue;
        if (f.rel == "include/client/action/action_dispatcher.h") continue;
        if (f.rel == "include/client/action/action_types.h") continue;
        if (f.rel == "include/client/action/action_handler.h") continue;
        for (const auto& raw : f.lines) {
            std::string code = codePart(raw);
            auto b = std::sregex_iterator(code.begin(), code.end(), ext);
            for (auto it = b; it != std::sregex_iterator(); ++it) {
                if (methods.count((*it)[1])) reachable.insert((*it)[1]);
            }
        }
    }
    // Closure over internal calls.
    bool grew = true;
    while (grew) {
        grew = false;
        for (const auto& r : std::set<std::string>(reachable)) {
            for (const auto& o : callsMethod[r]) {
                if (reachable.insert(o).second) grew = true;
            }
        }
    }
    std::set<std::string> reachedHandler;
    for (const auto& r : reachable) {
        for (const auto& h : callsHandler[r]) reachedHandler.insert(h);
    }
    std::set<std::string> unreachable;
    for (const auto& v : virtuals) {
        if (!reachedHandler.count(v)) unreachable.insert(v);
    }
    expectSetsEqual(unreachable, kBlockedHandlerMethods,
                    "unreachable IActionHandler methods", "blocked");
}

// ===================================================================
// 4. EverQuest::Send* senders
// ===================================================================

TEST(ReachabilitySweep, EverySenderHasACaller) {
    auto lines = splitLines(readFile(kRoot / "include/client/eq.h"));
    std::regex decl(R"(^\s*(?:virtual\s+)?(?:static\s+)?[A-Za-z_:<>0-9]+\s+\*?(Send[A-Za-z0-9_]+)\s*\()");
    std::set<std::string> senders;
    for (const auto& l : lines) {
        if (isCommentLine(l)) continue;
        std::smatch m;
        if (std::regex_search(l, m, decl)) senders.insert(m[1]);
    }
    ASSERT_GT(senders.size(), 50u) << "eq.h Send* parse failed";

    auto files = collect({"src", "include"}, {".cpp", ".h"});
    std::set<std::string> uncalled;
    for (const auto& s : senders) {
        bool called = false;
        for (const auto& f : files) {
            if (f.rel == "include/client/eq.h") continue;
            for (const auto& raw : f.lines) {
                std::string code = codePart(raw);
                if (code.find("EverQuest::" + s + "(") != std::string::npos) continue;  // definition
                if (containsToken(code, s) && code.find(s + "(") != std::string::npos) { called = true; break; }
            }
            if (called) break;
        }
        if (!called) uncalled.insert(s);
    }
    EXPECT_TRUE(uncalled.empty()) << "EverQuest::Send* with no caller: " << join(uncalled);
}

}  // namespace
