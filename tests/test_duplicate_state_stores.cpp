// T286: the duplicate-store sweep, pinned so it never has to be repeated by
// hand.
//
// T280 found EverQuest::m_practice_points duplicating
// PlayerState::m_practicePoints; T283 deleted it; T271/T276 found `m_class`
// and `m_level` being read where GetClass()/GetLevel() was meant. Those were
// three instances of ONE defect class: EverQuest kept its own copy of a value
// that a state module also holds, both copies were written by hand at every
// wire site, and a site that updated only one of them left the other stale
// with nothing to catch it.
//
// T286 swept all of them. This test re-runs the sweep against the source tree
// on every ctest, the same way T264's test_reachability_sweep parses eq.h and
// the src tree: it collects EverQuest's data members from include/client/eq.h,
// collects the members of every state module under include/client/state/, and
// asserts the overlap is EXACTLY the documented exception list below.
//
// Asserted in BOTH directions:
//   - a NEW duplicate member fails with its name (the regression this pins);
//   - deleting one of the documented exceptions ALSO fails, with "no longer
//     duplicated: remove it from kKnownRemaining", so this list and the
//     T286 Review's table cannot drift apart.
//
// Text-level parsing, deliberately: a compile-time check cannot see that two
// separately-declared members hold the same value -- that is the whole defect
// class. The failure message names the member and its counterpart so it can
// be judged rather than trusted.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
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

// The duplicates T286 deliberately did NOT delete, each with the reason.
// Anything here is a REMAINING duplicate: it must still exist on both sides.
//   key   = EverQuest member
//   value = {state counterpart, why it survived T286}
const std::map<std::string, std::pair<std::string, std::string>> kKnownRemaining = {
    {"m_x",       {"PlayerState::m_x",
                   "position: 19 direct writes in the movement integrator, "
                   "grouped x/y/z setters on the state side -- own unit"}},
    {"m_y",       {"PlayerState::m_y",       "see m_x"}},
    {"m_z",       {"PlayerState::m_z",       "see m_x"}},
    {"m_heading", {"PlayerState::m_heading", "see m_x"}},
    {"m_my_spawn_id", {"PlayerState::m_spawnId",
                   "138 references, and the spawn-id write order relative to "
                   "the pet/player-entity creation paths is load-bearing -- "
                   "own unit, with the position cluster"}},
    {"m_character", {"PlayerState::m_name",
                   "NOT the same value: m_character is the login-time "
                   "character name, fixed at construction and used by every "
                   "packet sender; PlayerState::m_name is the SERVER-reported "
                   "name from PlayerProfile ('name[0] ? name : m_character'), "
                   "which does not exist until the profile arrives"}},
    {"m_current_zone_name", {"WorldState::m_zoneName",
                   "WorldState::setZoneName fires ZoneChanged, which "
                   "ConsoleRenderer subscribes to; CleanupZone clears the "
                   "EverQuest copy, so converging them changes what a "
                   "console session prints -- own unit"}},
    {"m_current_zone_id", {"WorldState::m_zoneId", "see m_current_zone_name"}},
};

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
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// A member declaration line: <type> m_name[ = init];  (no parentheses, so
// member FUNCTIONS and constructor-style initializers are not collected).
const std::regex kMemberDecl(
    R"(^[ \t]*(?:mutable[ \t]+)?[A-Za-z_][A-Za-z_0-9:<>,*&\[\] \t]*[ \t*&](m_[A-Za-z_0-9]+)[ \t]*(?:=[^;]*)?;)");

std::set<std::string> membersOf(const fs::path& header) {
    std::set<std::string> out;
    for (const auto& line : splitLines(readFile(header))) {
        // skip comments outright -- prose mentions member names constantly
        const auto firstNonWs = line.find_first_not_of(" \t");
        if (firstNonWs != std::string::npos &&
            line.compare(firstNonWs, 2, "//") == 0) {
            continue;
        }
        if (line.find('(') != std::string::npos) continue;
        std::smatch m;
        if (std::regex_search(line, m, kMemberDecl)) {
            out.insert(m[1].str());
        }
    }
    return out;
}

// Normalise a member name so EverQuest's snake_case and the state layer's
// camelCase land on the same key: drop the leading "m_", drop underscores,
// lowercase the rest.  m_cur_hp -> "curhp"; m_curHP -> "curhp".
std::string normalize(const std::string& member) {
    std::string s = member.substr(2);  // strip "m_"
    std::string out;
    for (char c : s) {
        if (c == '_') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// The state modules whose members are the canonical stores for player-facing
// values. EntityManager/InventoryState/DoorStateManager hold collections
// EverQuest also indexes for other reasons and are swept separately.
const std::vector<std::string> kStateHeaders = {
    "player_state.h", "world_state.h", "combat_state.h",
    "group_state.h", "pet_state.h",
};

// EverQuest members whose NAME collides with a state member's after
// normalisation but which hold a different thing. Each is a false positive of
// the name-based match, not a duplicate.
const std::map<std::string, std::string> kNotDuplicates = {
    {"m_zone_connection", "the DaybreakConnection shared_ptr, not "
                          "WorldState::m_zoneConnected"},
};

}  // namespace

TEST(DuplicateStateStores, EverQuestKeepsNoCopyOfAStateModuleMember) {
    const std::set<std::string> eqMembers = membersOf(kRoot / "include/client/eq.h");
    ASSERT_GT(eqMembers.size(), 100u)
        << "eq.h member parse collapsed -- the regex, not the code, is wrong";

    // normalised state member -> "Class::m_member"
    std::map<std::string, std::string> stateByKey;
    for (const auto& h : kStateHeaders) {
        const fs::path p = kRoot / "include/client/state" / h;
        std::string cls = h.substr(0, h.size() - 2);  // strip ".h"
        // player_state -> PlayerState
        std::string pretty;
        bool up = true;
        for (char c : cls) {
            if (c == '_') { up = true; continue; }
            pretty += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            up = false;
        }
        for (const auto& m : membersOf(p)) {
            if (m == "m_eventBus") continue;
            stateByKey.emplace(normalize(m), pretty + "::" + m);
        }
    }
    ASSERT_GT(stateByKey.size(), 50u)
        << "state header member parse collapsed -- the regex, not the code, is wrong";

    std::vector<std::string> found;   // EverQuest members that duplicate a state member
    for (const auto& m : eqMembers) {
        if (kNotDuplicates.count(m)) continue;
        auto it = stateByKey.find(normalize(m));
        if (it != stateByKey.end()) {
            found.push_back(m + " <-> " + it->second);
        }
    }

    // 1. No NEW duplicate.
    std::vector<std::string> unexpected;
    for (const auto& f : found) {
        const std::string member = f.substr(0, f.find(' '));
        if (!kKnownRemaining.count(member)) unexpected.push_back(f);
    }
    EXPECT_TRUE(unexpected.empty())
        << "New duplicate state store(s) in EverQuest -- the T286 defect class "
           "is back.\nWrite the value into m_game_state.<module>() at the "
           "source and read it back from there; do not add a second copy:\n  "
        << [&] {
               std::string s;
               for (const auto& u : unexpected) s += u + "\n  ";
               return s;
           }();

    // 2. The documented exceptions are still real -- otherwise this list and
    //    the T286 Review's table have drifted. Checked by NAME on both sides,
    //    because four of the eight are spelled differently across the two
    //    classes (m_my_spawn_id / m_spawnId, m_current_zone_name / m_zoneName,
    //    ...) and so are invisible to the normalised-name sweep above. That
    //    is this test's one blind spot, stated out loud: a NEW duplicate
    //    introduced under a DIFFERENT name is not detected. Same-name
    //    duplicates -- every one T280/T283/T286 actually found -- are.
    for (const auto& [member, why] : kKnownRemaining) {
        EXPECT_TRUE(eqMembers.count(member))
            << member << " is gone from eq.h: remove it from kKnownRemaining "
            << "(and from the T286 Review's deferred table). It was kept "
            << "because: " << why.second;

        const std::string counterpart = why.first;      // "PlayerState::m_x"
        const size_t sep = counterpart.rfind("::");
        ASSERT_NE(sep, std::string::npos) << counterpart;
        const std::string stateMember = counterpart.substr(sep + 2);
        const auto it = stateByKey.find(normalize(stateMember));
        EXPECT_TRUE(it != stateByKey.end() && it->second == counterpart)
            << counterpart << " no longer exists: " << member
            << " is no longer duplicated, so remove it from kKnownRemaining "
            << "(and from the T286 Review's deferred table).";
    }
}
