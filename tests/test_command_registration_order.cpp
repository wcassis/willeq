// G03 (refactor/G03_register_builtin_commands.md) -- the ORDER net for
// CommandProcessor::registerBuiltinCommands().
//
// Why a text parse and not a runtime enumeration: the registry's storage is a
// std::map<std::string, RegisteredCommand>, so getAllCommands() hands back
// names in ALPHABETICAL order and cannot witness registration order at all.
// What registration order actually decides is shadowing --
// CommandProcessor::registerCommand() does
//     m_commands[lowerName] = ...;  m_aliases[alias] = lowerName;
// so the LAST registration of a name or of an alias wins (J04; T097/T098
// audited the live shadowing bugs this produced). Before G03 that order was
// one 768-line function; after G03 it is (a) the order of the
// registerXCommands() calls inside registerBuiltinCommands() and (b) the order
// of the registrations inside each family. This file pins the composition of
// the two against a golden extracted mechanically from the pre-G03 source:
//     git show fable:src/client/action/command_processor_registration.cpp
// -> tests/goldens/command_registration_order_g03.txt (142 entries).
//
// A swap of two adjacent registrations ACROSS a family cut, a reordering of
// the calls in registerBuiltinCommands(), a registration moved between
// families, and an added or deleted command all fail here. Adding a genuinely
// new command is meant to require a line in the golden -- that edit is the
// review moment.

#include <gtest/gtest.h>

#include <fstream>
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
           "/tests/goldens/command_registration_order_g03.txt";
}

std::string SourcePath() {
    return std::string(WILLEQ_REPO_ROOT) +
           "/src/client/action/command_processor_registration.cpp";
}

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path);
    EXPECT_TRUE(in.is_open()) << "cannot open: " << path;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

// "<ordinal> <name>" per line, '#' comments skipped.
std::vector<std::string> LoadGolden() {
    std::vector<std::string> names;
    for (const auto& line : ReadLines(GoldenPath())) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string ordinal, name;
        ls >> ordinal >> name;
        if (name.empty()) continue;
        names.push_back(name);
    }
    return names;
}

// The first double-quoted token on a line, or "" if there is none.
std::string FirstQuoted(const std::string& line) {
    auto a = line.find('"');
    if (a == std::string::npos) return "";
    auto b = line.find('"', a + 1);
    if (b == std::string::npos) return "";
    return line.substr(a + 1, b - a - 1);
}

// A member function definition at namespace scope in this TU.
struct Function {
    std::string name;
    std::vector<std::string> registered;  // registerCommand({ "<name>", ... })
    std::vector<std::string> calls;       // bare `    someCall();` lines
};

// Parses `void CommandProcessor::<name>() {` ... `}` at column 0. The TU is
// written in exactly that shape and check_file_sizes.sh keeps it that way.
std::vector<Function> ParseSource() {
    const auto lines = ReadLines(SourcePath());
    const std::string kPrefix = "void CommandProcessor::";
    std::vector<Function> fns;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        if (l.rfind(kPrefix, 0) != 0) continue;
        auto paren = l.find("()");
        if (paren == std::string::npos) continue;
        Function fn;
        fn.name = l.substr(kPrefix.size(), paren - kPrefix.size());
        for (size_t j = i + 1; j < lines.size(); ++j) {
            if (lines[j] == "}") { i = j; break; }
            const std::string& b = lines[j];
            if (b == "    registerCommand({" && j + 1 < lines.size()) {
                fn.registered.push_back(FirstQuoted(lines[j + 1]));
                continue;
            }
            // `    identifier();` and nothing else on the line.
            if (b.size() > 7 && b.rfind("    ", 0) == 0 && b[4] != ' ' &&
                b.compare(b.size() - 3, 3, "();") == 0 &&
                b.find(' ', 4) == std::string::npos) {
                fn.calls.push_back(b.substr(4, b.size() - 7));
            }
        }
        fns.push_back(fn);
    }
    return fns;
}

const Function* Find(const std::vector<Function>& fns, const std::string& name) {
    for (const auto& f : fns) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

// The effective registration order: registerBuiltinCommands()'s calls, in
// call order, each expanded to its family's registrations in slice order.
std::vector<std::string> EffectiveOrder(const std::vector<Function>& fns,
                                        std::string* error) {
    std::vector<std::string> order;
    const Function* top = Find(fns, "registerBuiltinCommands");
    if (!top) { *error = "registerBuiltinCommands() not found in the TU"; return order; }
    if (!top->registered.empty()) {
        *error = "registerBuiltinCommands() still registers commands directly "
                 "(first: " + top->registered.front() + ")";
        return order;
    }
    for (const auto& callee : top->calls) {
        const Function* fam = Find(fns, callee);
        if (!fam) { *error = "registerBuiltinCommands() calls " + callee +
                             "(), which is not defined in this TU"; return order; }
        for (const auto& n : fam->registered) order.push_back(n);
    }
    return order;
}

std::string Join(const std::vector<std::string>& v, size_t from, size_t count) {
    std::string out;
    for (size_t i = from; i < v.size() && i < from + count; ++i) {
        if (!out.empty()) out += ", ";
        out += v[i];
    }
    return out;
}

} // namespace

// Correction 5 (Batch G template / K11 ee): every assertion below is vacuous
// against a missing or truncated golden, so the golden is checked first, in
// its own case.
TEST(CommandRegistrationOrderTest, GoldenIsPresentAndTheRightSize) {
    const auto golden = LoadGolden();
    ASSERT_FALSE(golden.empty()) << "golden is empty or unreadable: " << GoldenPath();
    // kGoldenSize: 142 at G03 (the pre-G03 source's count), then
    // +1 by T347 (B012), which INSERTED entry 022 `cleartarget` after 021
    // `target` and renumbered 022-142 to 023-143. Correction 32: an order
    // golden learns a reviewed edit in place, at its positions, named here.
    // +4 by B442 (#151): 078-081 `pickup` `putdown` `cursor` `coin` after
    // 077 `scribe`, 078-143 renumbered to 082-147.
    // +5 by B895 (GitLab #211): registerTrackingCommands() APPENDED entries
    // 148-152 (trackfilter, tracksort, trackplayers, stoptracking, tracklist).
    // +1 by B899 (#215): entry 065 `bag` INSERTED after 064 `closecontainer`,
    // 065-152 renumbered to 066-153.
    EXPECT_EQ(golden.size(), 153u)
        << "142 commands at G03, +1 for T347's /cleartarget, +4 for B442's cursor "
           "commands, +5 for B895's tracking family, +1 for B899's /bag; the golden has "
        << golden.size();
    for (const auto& n : golden) {
        EXPECT_FALSE(n.empty());
    }
}

TEST(CommandRegistrationOrderTest, EveryFamilyIsCalledExactlyOnceAndNothingElse) {
    const auto fns = ParseSource();
    const Function* top = Find(fns, "registerBuiltinCommands");
    ASSERT_NE(top, nullptr);
    EXPECT_FALSE(top->calls.empty()) << "registerBuiltinCommands() calls nothing";

    std::set<std::string> seen;
    for (const auto& c : top->calls) {
        EXPECT_TRUE(seen.insert(c).second) << c << "() is called twice";
        EXPECT_NE(Find(fns, c), nullptr) << c << "() is not defined in this TU";
    }
    // Every register*Commands() defined here is reached from the top -- an
    // orphaned family would silently drop its whole vocabulary.
    for (const auto& f : fns) {
        if (f.name == "registerBuiltinCommands") continue;
        if (f.name.rfind("register", 0) != 0) continue;
        EXPECT_EQ(seen.count(f.name), 1u)
            << f.name << "() is defined but never called by registerBuiltinCommands()";
    }
}

TEST(CommandRegistrationOrderTest, RegistrationOrderMatchesThePreG03Golden) {
    const auto golden = LoadGolden();
    ASSERT_FALSE(golden.empty());

    std::string error;
    const auto actual = EffectiveOrder(ParseSource(), &error);
    ASSERT_TRUE(error.empty()) << error;

    ASSERT_EQ(actual.size(), golden.size())
        << "registration count changed: " << golden.size() << " -> " << actual.size();

    for (size_t i = 0; i < golden.size(); ++i) {
        ASSERT_EQ(actual[i], golden[i])
            << "registration order diverges at position " << (i + 1) << ":\n"
            << "  golden: " << Join(golden, i, 4) << "\n"
            << "  actual: " << Join(actual, i, 4) << "\n"
            << "Order is load-bearing (J04): the last registration of a name or "
               "alias shadows the earlier ones.";
    }
}

// The families partition the golden: no registration is dropped on the floor
// between two cuts, and none is registered twice.
TEST(CommandRegistrationOrderTest, TheFamiliesPartitionTheGolden) {
    const auto fns = ParseSource();
    const Function* top = Find(fns, "registerBuiltinCommands");
    ASSERT_NE(top, nullptr);

    size_t total = 0;
    for (const auto& f : fns) {
        if (f.name == "registerBuiltinCommands") continue;
        total += f.registered.size();
    }
    EXPECT_EQ(total, LoadGolden().size())
        << "the per-family registrations do not sum to the golden -- a "
           "registration is outside every family, or in two of them";
}
