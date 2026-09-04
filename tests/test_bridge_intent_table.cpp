// G02 (refactor/G02_process_bridge_intents.md): the intent-dispatch coverage
// net for BridgeComponent::ProcessBridgeIntents.
//
// G02 turned the pump's 36 `if constexpr (std::is_same_v<T, ...>)` arms into
// 36 named private handlers and left the pump as pure dispatch: a std::visit
// over an overload set with ONE canonical line per RendererIntent
// alternative,
//
//     [this](const eqt::events::X& i) { onX(i); },
//
// There is NO golden file here on purpose -- the variant IS the golden. The
// alternative list is read out of renderer_intents.h as text, and case 1
// cross-checks that text parse against std::variant_size_v of the real type,
// so an unreadable or mis-parsed header cannot let anything below pass
// vacuously (the K11 "readFile" trap / Batch G correction 5).
//
// The overload set has no generic fallback lambda, so a NEW alternative with
// no handler is already a compile error. What this file adds is everything
// the compiler cannot see: that each arm calls ITS OWN handler and not a
// neighbour's (a copy-paste that still compiles), that each handler is
// declared in the header exactly once, that no handler exists which nothing
// dispatches, and that the pump stayed pure dispatch.
//
// tests/test_reachability_sweep.cpp's EveryIntentHasAConsumerArm holds the
// same canonical line from the reachability side; keep the two spellings in
// step.

#include <gtest/gtest.h>

#include "client/events/renderer_intents.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#ifndef WILLEQ_REPO_ROOT
#error "WILLEQ_REPO_ROOT must be defined (tests/CMakeLists.txt)"
#endif

namespace fs = std::filesystem;

namespace {

const fs::path kRoot = WILLEQ_REPO_ROOT;
const char* kIntentsTu = "src/client/bridge/bridge_component_intents.cpp";
const char* kBridgeHeader = "include/client/bridge/bridge_component.h";
const char* kIntentsHeader = "include/client/events/renderer_intents.h";

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

// The alternatives of RendererIntent, in the variant's own order, read as
// text out of the header that declares it.
std::vector<std::string> variantMembers() {
    auto lines = splitLines(readFile(kRoot / kIntentsHeader));
    std::vector<std::string> names;
    bool in = false;
    for (const auto& l : lines) {
        if (l.find("using RendererIntent = std::variant<") != std::string::npos) { in = true; continue; }
        if (!in) continue;
        std::string t = trim(l);
        if (t.rfind(">;", 0) == 0) break;
        if (t.rfind("//", 0) == 0) continue;
        size_t c = t.find("//");
        if (c != std::string::npos) t = trim(t.substr(0, c));
        while (!t.empty() && (t.back() == ',' || t.back() == ' ')) t.pop_back();
        if (!t.empty()) names.push_back(t);
    }
    return names;
}

std::string dispatchLine(const std::string& n) {
    return "[this](const eqt::events::" + n + "& i) { on" + n + "(i); },";
}

std::string declaration(const std::string& n) {
    return "void on" + n + "(const eqt::events::" + n + "& i);";
}

std::string definition(const std::string& n) {
    return "void eqt::bridge::BridgeComponent::on" + n + "(const eqt::events::" + n + "& i) {";
}

size_t countLinesEqualTo(const std::vector<std::string>& lines, const std::string& want) {
    size_t n = 0;
    for (const auto& l : lines) if (trim(l) == want) ++n;
    return n;
}

// The body of ProcessBridgeIntents, exclusive of its signature and its
// closing brace.
std::vector<std::string> pumpBody() {
    auto lines = splitLines(readFile(kRoot / kIntentsTu));
    const std::string sig = "void eqt::bridge::BridgeComponent::ProcessBridgeIntents() {";
    size_t s = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) if (trim(lines[i]) == sig) { s = i; break; }
    EXPECT_LT(s, lines.size()) << "ProcessBridgeIntents not found in " << kIntentsTu;
    if (s == lines.size()) return {};
    size_t e = s + 1;
    while (e < lines.size() && lines[e] != "}") ++e;
    EXPECT_LT(e, lines.size()) << "ProcessBridgeIntents has no closing brace at column 0";
    return std::vector<std::string>(lines.begin() + s + 1, lines.begin() + std::min(e, lines.size()));
}

}  // namespace

// 1. THE ANCHOR. The text parse of renderer_intents.h must agree with the
//    real type, so nothing below can pass against an empty or mis-parsed
//    file. No golden: std::variant_size_v IS the golden.
TEST(BridgeIntentTable, VariantParseAgreesWithTheVariantItself) {
    auto members = variantMembers();
    EXPECT_EQ(members.size(), std::variant_size_v<eqt::events::RendererIntent>)
        << "the text parse of " << kIntentsHeader << " disagrees with std::variant_size_v"
           "<eqt::events::RendererIntent>; every other case in this file is vacuous until"
           " this one passes";
    EXPECT_GT(members.size(), 20u) << "variant parse failed";
    std::set<std::string> uniq(members.begin(), members.end());
    EXPECT_EQ(uniq.size(), members.size()) << "duplicate alternative name in RendererIntent";
}

// 2. Every alternative is dispatched exactly once, by the canonical line, to
//    ITS OWN handler. Exactly-once is what catches a duplicated arm; the
//    handler name being inside the matched line is what catches an arm that
//    compiles but calls a neighbour's handler.
TEST(BridgeIntentTable, EveryIntentHasExactlyOneCanonicalDispatchLine) {
    auto lines = splitLines(readFile(kRoot / kIntentsTu));
    for (const auto& n : variantMembers()) {
        EXPECT_EQ(countLinesEqualTo(lines, dispatchLine(n)), 1u)
            << "RendererIntent alternative " << n
            << " must have exactly one dispatch line in " << kIntentsTu
            << "\n  -> expected: " << dispatchLine(n);
    }
}

// 3. Every handler is declared once in the component header.
TEST(BridgeIntentTable, EveryIntentHasExactlyOneHandlerDeclaration) {
    auto lines = splitLines(readFile(kRoot / kBridgeHeader));
    for (const auto& n : variantMembers()) {
        EXPECT_EQ(countLinesEqualTo(lines, declaration(n)), 1u)
            << "missing/duplicated handler declaration in " << kBridgeHeader
            << "\n  -> expected: " << declaration(n);
    }
}

// 4. ... and defined once in the intents TU.
TEST(BridgeIntentTable, EveryIntentHasExactlyOneHandlerDefinition) {
    auto lines = splitLines(readFile(kRoot / kIntentsTu));
    for (const auto& n : variantMembers()) {
        EXPECT_EQ(countLinesEqualTo(lines, definition(n)), 1u)
            << "missing/duplicated handler definition in " << kIntentsTu
            << "\n  -> expected: " << definition(n);
    }
}

// 5. The other direction: no on*() intent handler exists that no alternative
//    names. This is what catches an invented handler left behind by a
//    deleted intent -- the T264 unreachable-code class the sweep exists for.
TEST(BridgeIntentTable, NoIntentHandlerWithoutAnAlternative) {
    auto members = variantMembers();
    std::set<std::string> expected;
    for (const auto& n : members) expected.insert("on" + n);

    const std::string prefix = "void eqt::bridge::BridgeComponent::on";
    std::set<std::string> found;
    for (const auto& raw : splitLines(readFile(kRoot / kIntentsTu))) {
        std::string l = trim(raw);
        if (l.rfind(prefix, 0) != 0) continue;
        size_t p = l.find('(', prefix.size());
        if (p == std::string::npos) continue;
        found.insert(l.substr(prefix.size() - 2, p - (prefix.size() - 2)));
    }
    EXPECT_EQ(found, expected)
        << "the on*() handlers defined in " << kIntentsTu
        << " are not exactly the RendererIntent alternatives (found "
        << found.size() << ", expected " << expected.size() << ")";
}

// 6. The pump stayed PURE DISPATCH. G02's whole point: no arm body may creep
//    back into the visitor, and the old is_same_v chain may not come back.
TEST(BridgeIntentTable, ProcessBridgeIntentsIsPureDispatch) {
    auto body = pumpBody();
    ASSERT_FALSE(body.empty());
    EXPECT_LT(body.size(), 60u)
        << "ProcessBridgeIntents is " << body.size()
        << " lines; it must stay a dispatch table (G02)";

    size_t arms = 0;
    for (const auto& raw : body) {
        std::string l = trim(raw);
        EXPECT_EQ(l.find("is_same_v"), std::string::npos)
            << "the is_same_v arm chain is back in ProcessBridgeIntents: " << l;
        if (l.rfind("[this](const eqt::events::", 0) == 0) ++arms;
    }
    EXPECT_EQ(arms, std::variant_size_v<eqt::events::RendererIntent>)
        << "the visitor's arm count does not match the variant";
}
