// B919 (#238): the `game_state` owner-row census, as a test.
//
// The ledger's owner rows (WILLEQ_LEDGER_OWNERS=game_state, B839's mechanism
// re-pointed at the category) name the bytes a category holds. B941 named the
// four game-thread zone loads; B919 adds the per-spawn state (gs.entities),
// the two bridge event buffers (gs.event_queue, gs.event_stage), the doors
// (gs.doors) and the ground objects (gs.world_objects), plus the dispatch
// catch-all (gs.dispatch). This file is the ratchet that keeps every
// `game_state` scoping site carrying an owner: it re-takes the census on
// every ctest run by reading the sources, and it FAILS on
//
//   * a MemoryScope that opens Category::GameState without a StartupScope
//     label opened AFTER it in the same scope (a label opened BEFORE the
//     MemoryScope is worse than none: the MemoryScope's constructor clears
//     the sub-owner, so the label's bytes silently fold into "misc"), and
//   * the game thread's default (setThreadCategory(GameState) in
//     application.cpp's gameThreadLoop) without the default's owner named in
//     a comment on the same or the next line (the whole thread's unlabelled
//     bytes land there, so the default must have a name the tables can
//     quote: the B941 README calls it "misc"; B919 names it gs.thread in the
//     README's table, and this test pins that the name is written down).
//
// Source reader, no link deps on purpose: the sites it guards end in network
// handlers and cannot be exercised in-process (same shape as
// test_data_path_routing.cpp, T399). The behavioural half -- that a labelled
// allocation really lands on its row -- is test_memory_ledger.cpp's
// StartupScope tests.
//
// The scanner is deliberately simple: it strips comments (string literals
// intact, as in test_data_path_routing.cpp) and tracks brace depth. For each
// site line it finds the innermost open brace block (by matching braces
// forward) and requires the owner requirement somewhere in that block.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const std::string kRoot = std::string(WILLEQ_REPO_ROOT) + "/";

std::string ReadFile(const std::string& relative) {
    std::ifstream f(kRoot + relative, std::ios::binary);
    if (!f.is_open()) return std::string();
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Replace the body of every // and /* */ comment with spaces, leaving line
// structure and string literals intact (copied from
// test_data_path_routing.cpp: a site quoted in a comment is not a site).
std::string StripComments(const std::string& src) {
    std::string out = src;
    enum { kCode, kLine, kBlock, kString, kChar } state = kCode;
    for (size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        const char n = (i + 1 < out.size()) ? out[i + 1] : '\0';
        switch (state) {
            case kCode:
                if (c == '/' && n == '/') { state = kLine; out[i] = out[i + 1] = ' '; ++i; }
                else if (c == '/' && n == '*') { state = kBlock; out[i] = out[i + 1] = ' '; ++i; }
                else if (c == '"') state = kString;
                else if (c == '\'') state = kChar;
                break;
            case kLine:
                if (c == '\n') state = kCode; else out[i] = ' ';
                break;
            case kBlock:
                if (c == '*' && n == '/') { out[i] = out[i + 1] = ' '; ++i; state = kCode; }
                else if (c != '\n') out[i] = ' ';
                break;
            case kString:
                if (c == '\\') ++i;
                else if (c == '"') state = kCode;
                break;
            case kChar:
                if (c == '\\') ++i;
                else if (c == '\'') state = kCode;
                break;
        }
    }
    return out;
}

// Split into lines (the scanner works on line starts and brace counts).
std::vector<std::string> Lines(const std::string& src) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : src) {
        if (c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// The brace depth at the START of each line (braces before the first
// non-space content count; a line's own opening brace belongs to its
// block). The file starts at depth 0.
std::vector<long> DepthAtLineStart(const std::vector<std::string>& lines) {
    std::vector<long> depth(lines.size(), 0);
    long d = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        depth[i] = d;
        for (char c : lines[i]) {
            if (c == '{') ++d;
            else if (c == '}') --d;
        }
    }
    return depth;
}

// Every line index that opens a site of `pattern`.
std::vector<size_t> FindSites(const std::vector<std::string>& lines, const std::string& pattern) {
    std::vector<size_t> out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(pattern) != std::string::npos) out.push_back(i);
    }
    return out;
}

// True if `needle` appears from line `from` to the end of the block the site
// at `from` sits in. If the site line OPENS a block (a function-definition
// site), that block is the one searched; otherwise the block the site is
// already inside. Walks the braces character by character: the block ends
// where the depth drops below the floor (a well-formed file always ends at
// the function's closing brace; an unbalanced file ends at EOF, and the
// check then just fails).
bool InBlockFrom(const std::vector<std::string>& lines, const std::vector<long>& depth,
                 size_t from, const std::string& needle) {
    long floor = depth[from];
    long d = floor;
    for (char c : lines[from]) {
        if (c == '{') ++d;
        else if (c == '}') --d;
    }
    if (d > floor) floor = d;  // the site's own line opened the block
    for (size_t i = from; i < lines.size(); ++i) {
        for (char c : lines[i]) {
            if (c == '{') ++d;
            else if (c == '}') {
                --d;
                if (d < floor) return false;  // left the block
            }
        }
        if (d >= floor && lines[i].find(needle) != std::string::npos) return true;
    }
    return false;
}

struct SiteCheck {
    const char* file;          // repo-relative
    const char* pattern;       // the scoping site
    const char* requirement;   // what must follow in the same function
    const char* why;           // failure message
};

// ---------------------------------------------------------------------------
// The census: every `game_state` scoping site, and the owner it must carry.
// ---------------------------------------------------------------------------
const SiteCheck kSites[] = {
    // T491's dispatch scope: the zone-packet handlers' game_state bytes. The
    // catch-all row gs.dispatch must open AFTER the MemoryScope (a label
    // before it is cleared by the MemoryScope's constructor).
    {"src/client/zone/zone_dispatch.cpp", "MemoryScope gsScope(eqt::memory::Category::GameState)",
     "eqt::memory::StartupScope owner(\"gs.dispatch\")",
     "DispatchZonePacket's GameState scope must carry the gs.dispatch row"},
    // The per-spawn state (B919): ZoneSpawns into ctx_.entities + the
    // state::EntityManager sync.
    {"src/client/entities/entities_component.cpp", "EntitiesComponent::ZoneProcessZoneSpawns",
     "eqt::memory::StartupScope owner(\"gs.entities\")",
     "ZoneProcessZoneSpawns must carry the gs.entities row"},
    // The door spawn table (B919).
    {"src/client/entities/entities_component.cpp", "EntitiesComponent::ZoneProcessSpawnDoor",
     "eqt::memory::StartupScope owner(\"gs.doors\")",
     "ZoneProcessSpawnDoor must carry the gs.doors row"},
    // Door state updates (B919).
    {"src/client/entities/entities_component_world.cpp", "EntitiesComponent::ZoneProcessMoveDoor",
     "eqt::memory::StartupScope owner(\"gs.doors\")",
     "ZoneProcessMoveDoor must carry the gs.doors row"},
    // Ground-spawn world objects (B919).
    {"src/client/entities/entities_component_updates.cpp", "EntitiesComponent::ZoneProcessGroundSpawn",
     "eqt::memory::StartupScope owner(\"gs.world_objects\")",
     "ZoneProcessGroundSpawn must carry the gs.world_objects row"},
    // The base event queue's storage (B919), in the header: every event's
    // push is one row. The site is the function signature (the label opens
    // BEFORE the push, so the push line itself would scan the wrong way).
    {"include/client/bridge/game_state_bridge.h", "void pushEvent(state::GameEvent event)",
     "owner.emplace(\"gs.event_queue\")",
     "GameStateBridge::pushEvent must carry the gs.event_queue row"},
    // The game->render staging FIFO (B919).
    {"src/client/bridge/game_state_sync_bridge.cpp", "GameStateSyncBridge::stagePendingEvents",
     "eqt::memory::StartupScope owner(\"gs.event_stage\")",
     "stagePendingEvents must carry the gs.event_stage row"},
};

}  // namespace

TEST(GameStateOwners, EveryGameStateScopeSiteCarriesAnOwner) {
    for (const auto& site : kSites) {
        const std::string code = StripComments(ReadFile(site.file));
        ASSERT_FALSE(code.empty()) << "missing source " << site.file;
        const auto lines = Lines(code);
        const auto depth = DepthAtLineStart(lines);
        const auto sites = FindSites(lines, site.pattern);
        ASSERT_FALSE(sites.empty()) << "site '" << site.pattern << "' not found in " << site.file;
        for (size_t i : sites) {
            EXPECT_TRUE(InBlockFrom(lines, depth, i, site.requirement))
                << site.why << " -- " << site.file << ":" << i + 1 << " opens a game_state scope "
                << "whose owner '" << site.requirement << "' is missing in the same function. A "
               "bare scope (or a label before the MemoryScope) reports its bytes as \"misc\".";
        }
    }
}

// The game thread's DEFAULT category is game_state (application.cpp's
// gameThreadLoop). Its unlabelled bytes are the tables' residual, so the
// default must be NAMED in a comment the tables can quote: B919 calls it
// gs.thread. Pin the name, not the exact wording.
TEST(GameStateOwners, TheGameThreadDefaultIsNamed) {
    const std::string code = StripComments(ReadFile("src/client/application.cpp"));
    ASSERT_FALSE(code.empty());
    const auto lines = Lines(code);
    const auto sites = FindSites(lines, "setThreadCategory(eqt::memory::Category::GameState)");
    ASSERT_FALSE(sites.empty()) << "gameThreadLoop no longer sets the GameState default -- "
                                << "the tables' residual name must be updated";
    for (size_t i : sites) {
        // The comment above the site is stripped (StripComments), so the name
        // requirement is checked against the RAW source.
        const auto rawLines = Lines(ReadFile("src/client/application.cpp"));
        std::string rawWindow;
        for (size_t j = (i > 8) ? i - 8 : 0; j <= i && j < rawLines.size(); ++j) {
            rawWindow += rawLines[j] + "\n";
        }
        EXPECT_NE(rawWindow.find("gs.thread"), std::string::npos)
            << "the game thread's default game_state category must name its owner "
               "(gs.thread) in a comment within 8 lines of the setThreadCategory site: "
            << "src/client/application.cpp:" << i + 1;
    }
}

// The four B941 zone loads keep their rows (this card adds owners, it does
// not re-name what B941/B951 proved): pin them here so the B919 README's
// table is not the only place they exist.
TEST(GameStateOwners, TheB941ZoneLoadOwnersAreStillThere) {
    const std::string code = StripComments(ReadFile("src/client/zone/zone_entry.cpp"));
    ASSERT_FALSE(code.empty());
    for (const char* owner : {"\"gs.navmesh\"", "\"gs.collision_map\"",
                              "\"gs.zone_lines\"", "\"gs.zone_sources\""}) {
        EXPECT_NE(code.find(owner), std::string::npos)
            << "the B941/B951 row " << owner << " is gone from zone_entry.cpp";
    }
}
