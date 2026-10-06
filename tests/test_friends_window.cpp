// B897 (GitLab #213): the Friends window (friends + ignore lists).
//
// What is pinned here, all offline (no server, no device):
//   1. GEOMETRY against /mnt/projects/EverQuestP1999/uifiles/default/
//      EQUI_FriendsWnd.xml and defaults.ini [FriendsWindow] (skipped when the
//      oracle install is absent; the constants are asserted either way).
//   2. The MODEL: add/remove/toggle, case-insensitive matching with the typed
//      spelling kept, the 100 cap, too-long, self-ignore, the *NULL* marker.
//   3. The ORACLE STRINGS, byte for byte against eqstr_us.txt.
//   4. The COMMANDS (/friends, /ignore, /friendswho) and their negatives,
//      including that /friends is no longer an alias of the who query.
//   5. The INI shape (the oracle's own file parses; ours is tail-packed CRLF
//      with all 100 keys and keeps foreign sections) and the persistence tick.
//   6. The PANEL: both tabs' hit tests (Contact/Who exist only on Friends),
//      every click's action, scroll and selection clamps.
//   7. The GAME-THREAD path through a real EverQuest: an ignored sender's
//      channel messages are dropped and never become /reply's target, and a
//      window intent edits, prints, publishes and persists.
//   8. The wiring: Alt+F, [FriendsWindow] Show=, the bridge arm, /friendswindow.
//
// THE ORACLE LEDGER: review/UIGAP-ORACLE/213/friends.tsv (two official Titanium
// clients, 2026-10-05). Each case below names the rows it pins as "row N".
// Cases that read the capture's own files skip when the review dir is absent.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "client/eq_enums.h"
#include "client/events/renderer_intents.h"
#include "client/graphics/ui/friends_layout.h"
#include "client/graphics/ui/friends_panel.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_preferences.h"
#include "client/input/hotkey_manager.h"
#include "client/state/event_bus.h"
#include "client/state/social_lists.h"
#include "client/state/social_lists_io.h"
#include "common/packet_structs.h"
#include "zone_stream_test_utils.h"

namespace fs = std::filesystem;
using namespace EQT::Graphics;
using eqt::state::SocialEditOp;
using eqt::state::SocialEditResult;
using eqt::state::SocialList;
using eqt::state::SocialListsModel;
namespace SocialStr = eqt::state::SocialStr;

namespace {

const fs::path kOracle = "/mnt/projects/EverQuestP1999";
const fs::path kLedger = "/mnt/projects/fable/review/UIGAP-ORACLE/213";

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

UILayout LayoutAt(int w, int h) {
    UILayout l;
    l.computeLayout(w, h);
    return l;
}

// One <Location>/<Size> pair out of an `<X item = "name">` element.
struct XmlRect { int x = -1, y = -1, w = -1, h = -1; };
XmlRect XmlPiece(const std::string& xml, const std::string& item) {
    XmlRect r;
    const size_t at = xml.find("item = \"" + item + "\"");
    if (at == std::string::npos) return r;
    const size_t end = xml.find("</", xml.find('>', at));
    const std::string body = xml.substr(at, xml.find("\n\t</", at) - at);
    std::smatch m;
    if (std::regex_search(body, m, std::regex(R"(<X>(\d+)</X>\s*<Y>(\d+)</Y>)"))) {
        r.x = std::stoi(m[1]); r.y = std::stoi(m[2]);
    }
    if (std::regex_search(body, m, std::regex(R"(<CX>(\d+)</CX>\s*<CY>(\d+)</CY>)"))) {
        r.w = std::stoi(m[1]); r.h = std::stoi(m[2]);
    }
    (void)end;
    return r;
}

// A private cache dir, as tests/test_ui_preferences.cpp does.
class CacheDir {
public:
    CacheDir() {
        static int counter = 0;
        dir_ = fs::temp_directory_path() /
               ("willeq_b897_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        fs::create_directories(dir_);
        if (const char* prev = std::getenv("WILLEQ_CACHE_DIR")) saved_ = prev;
        ::setenv("WILLEQ_CACHE_DIR", dir_.c_str(), 1);
    }
    ~CacheDir() {
        if (saved_.empty()) ::unsetenv("WILLEQ_CACHE_DIR");
        else ::setenv("WILLEQ_CACHE_DIR", saved_.c_str(), 1);
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    fs::path file(const std::string& rel) const { return dir_ / rel; }
private:
    fs::path dir_;
    std::string saved_;
};

}  // namespace

// ===========================================================================
// 1. Geometry
// ===========================================================================

TEST(FriendsLayoutB897, ConstantsAreTheXmlsOwn) {
    using namespace FriendsLayout;
    EXPECT_EQ(WINDOW_W, 168);
    EXPECT_EQ(WINDOW_H, 288);
    EXPECT_EQ(TITLE_H, 24) << "288 - 264: derived, not picked";
    EXPECT_EQ(PAGE_H, 242);
    EXPECT_LE(DELETE_Y + DELETE_H, PAGE_H) << "the deepest piece must fit under a 22px strip";

    const fs::path xmlPath = kOracle / "uifiles/default/EQUI_FriendsWnd.xml";
    if (!fs::exists(xmlPath)) GTEST_SKIP() << "no oracle install at " << kOracle;
    const std::string xml = ReadFile(xmlPath);

    struct Row { const char* item; int x, y, w, h; };
    const Row rows[] = {
        {"FriendsWindow", XML_X, XML_Y, WINDOW_W, WINDOW_H},
        {"FW_Subwindows", TABBOX_X, TABBOX_Y, TABBOX_W, TABBOX_H},
        {"FW_FriendsList", LIST_X, LIST_Y, LIST_W, LIST_H},
        {"FW_IgnoreList", LIST_X, LIST_Y, LIST_W, LIST_H},
        {"FW_NameInput", INPUT_X, INPUT_Y, INPUT_W, INPUT_H},
        {"FW_IgnoreNameInput", INPUT_X, INPUT_Y, INPUT_W, INPUT_H},
        {"FW_AddButton", ADD_X, ADD_Y, ADD_W, ADD_H},
        {"FW_IgnoreAddButton", ADD_X, ADD_Y, ADD_W, ADD_H},
        {"FW_DeleteButton", DELETE_X, DELETE_Y, DELETE_W, DELETE_H},
        {"FW_IgnoreDeleteButton", DELETE_X, DELETE_Y, DELETE_W, DELETE_H},
        {"FW_ContactButton", CONTACT_X, CONTACT_Y, CONTACT_W, CONTACT_H},
        {"FW_WhoButton", WHO_X, WHO_Y, WHO_W, WHO_H},
    };
    for (const auto& r : rows) {
        const XmlRect x = XmlPiece(xml, r.item);
        EXPECT_EQ(x.x, r.x) << r.item;
        EXPECT_EQ(x.y, r.y) << r.item;
        EXPECT_EQ(x.w, r.w) << r.item;
        EXPECT_EQ(x.h, r.h) << r.item;
    }
    // The Ignored page has no Contact/Who in the file -- the negative.
    const size_t page = xml.find("item = \"FW_IgnorePage\"");
    const std::string ignorePage = xml.substr(page, xml.find("</Page>", page) - page);
    EXPECT_EQ(ignorePage.find("Contact"), std::string::npos);
    EXPECT_EQ(ignorePage.find("Who"), std::string::npos);
    EXPECT_NE(xml.find("<TabText>Friends</TabText>"), std::string::npos);
    EXPECT_NE(xml.find("<TabText>Ignored</TabText>"), std::string::npos);
    EXPECT_STREQ(tabText(0), "Friends");
    EXPECT_STREQ(tabText(1), "Ignored");
}

TEST(FriendsLayoutB897, DefaultPositionsAreDefaultsIni) {
    const fs::path ini = kOracle / "defaults.ini";
    if (!fs::exists(ini)) GTEST_SKIP() << "no defaults.ini";
    const std::string text = ReadFile(ini);
    const size_t at = text.find("[FriendsWindow]");
    ASSERT_NE(at, std::string::npos);
    const std::string sec = text.substr(at, text.find("\n[", at + 1) - at);
    for (const auto& d : FriendsLayout::kDefaultPositions) {
        const std::string res = std::to_string(d.screenW) + "x" + std::to_string(d.screenH);
        EXPECT_NE(sec.find("XPos" + res + "=" + std::to_string(d.x)), std::string::npos) << res;
        EXPECT_NE(sec.find("YPos" + res + "=" + std::to_string(d.y)), std::string::npos) << res;
    }
}

TEST(FriendsLayoutB897, PlacementPerResolutionAndClamped) {
    const auto r800 = FriendsLayout::windowRect(LayoutAt(800, 600));
    EXPECT_EQ(r800.UpperLeftCorner.X, 259);
    EXPECT_EQ(r800.UpperLeftCorner.Y, 125);
    EXPECT_EQ(r800.getWidth(), 168);
    EXPECT_EQ(r800.getHeight(), 288);
    const auto r640 = FriendsLayout::windowRect(LayoutAt(640, 480));
    EXPECT_EQ(r640.UpperLeftCorner.X, 196) << "no defaults.ini entry: the XML Location";
    EXPECT_EQ(r640.UpperLeftCorner.Y, 50);
    const auto tiny = FriendsLayout::windowRect(LayoutAt(200, 200));
    EXPECT_EQ(tiny.UpperLeftCorner.X, 32);
    EXPECT_EQ(tiny.UpperLeftCorner.Y, 0);
    // Pieces are page-relative under the 24px title and the 22px strip.
    const auto l = LayoutAt(800, 600);
    const auto list = FriendsLayout::listRect(l);
    EXPECT_EQ(list.UpperLeftCorner.X, 259 + 2 + 3);
    EXPECT_EQ(list.UpperLeftCorner.Y, 125 + 24 + 22 + 30);
    EXPECT_EQ(FriendsLayout::VISIBLE_ROWS, 10);
}

// ===========================================================================
// 2-3. Model and strings
// ===========================================================================

TEST(SocialListsB897, AddRemoveToggleCaseInsensitiveKeepsSpelling) {
    SocialListsModel m;
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Add, "Xenephon"), SocialEditResult::Added);
    EXPECT_TRUE(m.contains(SocialList::Friends, "xenephon"));
    EXPECT_EQ(m.friends()[0], "Xenephon") << "stored as typed (Obian_project1999.ini)";
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Add, "XENEPHON"),
              SocialEditResult::AlreadyPresent);
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Toggle, "xenephon"), SocialEditResult::Removed);
    EXPECT_TRUE(m.friends().empty());
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Remove, "nobody"), SocialEditResult::NotPresent);
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Add, "  "), SocialEditResult::Invalid);
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Add, "*NULL*"), SocialEditResult::Invalid);
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Add, std::string(64, 'a')),
              SocialEditResult::TooLong);
    EXPECT_EQ(m.edit(SocialList::Friends, SocialEditOp::Add, std::string(63, 'a')),
              SocialEditResult::Added);
    EXPECT_FALSE(m.isIgnored(std::string(63, 'a'))) << "the two lists are independent";
    EXPECT_FALSE(m.isIgnored(""));
}

TEST(SocialListsB897, CapIsOneHundredAndSelfCannotBeIgnored) {
    SocialListsModel m;
    for (int i = 0; i < eqt::state::kSocialListCapacity; ++i) {
        ASSERT_EQ(m.edit(SocialList::Ignored, SocialEditOp::Add, "p" + std::to_string(i)),
                  SocialEditResult::Added);
    }
    EXPECT_EQ(m.edit(SocialList::Ignored, SocialEditOp::Add, "one_more"), SocialEditResult::Full);
    EXPECT_EQ(m.ignored().size(), 100u);
    SocialListsModel n;
    EXPECT_EQ(n.edit(SocialList::Ignored, SocialEditOp::Toggle, "testchar", "Testchar"),
              SocialEditResult::Self);
    EXPECT_EQ(n.edit(SocialList::Friends, SocialEditOp::Toggle, "testchar", "Testchar"),
              SocialEditResult::Added) << "only /ignore refuses yourself";
}

TEST(SocialListsB897, RevisionMovesOnlyOnChange) {
    SocialListsModel m;
    const auto r0 = m.revision();
    m.edit(SocialList::Friends, SocialEditOp::Remove, "nobody");
    EXPECT_EQ(m.revision(), r0);
    m.edit(SocialList::Friends, SocialEditOp::Add, "Bob");
    EXPECT_GT(m.revision(), r0);
}

TEST(SocialListsB897, StringsMatchTheOracleFile) {
    const uint32_t ids[] = {12307, 12308, 12977, 12978, 12979, 12980, 12981, 12982,
                            12983, 12984, 13240, 13241, 13242, 13243, 13244};
    for (uint32_t id : ids) EXPECT_FALSE(eqt::state::SocialDefaultString(id).empty()) << id;
    EXPECT_TRUE(eqt::state::SocialDefaultString(5001).empty());

    const fs::path eqstr = kOracle / "eqstr_us.txt";
    if (!fs::exists(eqstr)) GTEST_SKIP() << "no eqstr_us.txt";
    std::istringstream in(ReadFile(eqstr));
    std::string line;
    int matched = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        uint32_t id = 0;
        try { id = static_cast<uint32_t>(std::stoul(line.substr(0, sp))); } catch (...) { continue; }
        for (uint32_t want : ids) {
            if (id == want) {
                EXPECT_EQ(eqt::state::SocialDefaultString(id), line.substr(sp + 1)) << id;
                ++matched;
            }
        }
    }
    EXPECT_EQ(matched, 15);
}

// ===========================================================================
// 4. Commands
// ===========================================================================

// Rows 1-5: toggle, list, no existence check, stored as typed.
TEST(SocialCommandsB897, FriendsToggleAndListMessages) {
    SocialListsModel m;
    auto none = eqt::state::RunFriendsCommand(m, "", "Me");
    ASSERT_EQ(none.lines.size(), 2u);
    EXPECT_EQ(none.lines[0], "You have no friends! Awww, how sad...");
    EXPECT_EQ(none.lines[1], "Maybe you should add one by typing: /friend <name>");

    auto add = eqt::state::RunFriendsCommand(m, "Bob extra words", "Me");
    ASSERT_EQ(add.lines.size(), 1u);
    EXPECT_EQ(add.lines[0], "Bob is now your friend.");
    EXPECT_TRUE(add.changed);
    eqt::state::RunFriendsCommand(m, "alice", "Me");

    // Row 2: heading, a 17-dash rule, the names, the count. Rows 1/11: sorted.
    auto list = eqt::state::RunFriendsCommand(m, "", "Me");
    ASSERT_EQ(list.lines.size(), 5u);
    EXPECT_EQ(list.lines[0], "List of Friends");
    EXPECT_EQ(list.lines[1], "-----------------");
    EXPECT_EQ(list.lines[2], "alice");
    EXPECT_EQ(list.lines[3], "Bob");
    EXPECT_EQ(list.lines[4], "You have 2 friend(s).");

    auto del = eqt::state::RunFriendsCommand(m, "BOB", "Me");
    ASSERT_EQ(del.lines.size(), 1u);
    EXPECT_EQ(del.lines[0], "Bob is no longer your friend.") << "the stored spelling";

    auto tooLong = eqt::state::RunFriendsCommand(m, std::string(70, 'z'), "Me");
    ASSERT_EQ(tooLong.lines.size(), 1u);
    EXPECT_EQ(tooLong.lines[0], "That is too long to be the name of a friend!");
    EXPECT_FALSE(tooLong.changed);

    for (int i = 0; m.friends().size() < 100; ++i) m.edit(SocialList::Friends, SocialEditOp::Add, "f" + std::to_string(i));
    auto full = eqt::state::RunFriendsCommand(m, "Zed", "Me");
    ASSERT_EQ(full.lines.size(), 1u);
    EXPECT_EQ(full.lines[0], "You may only have 100 friends in your list at once, please "
                             "delete one before adding another.");
}

// Rows 6-8, 10: add, list, self refused, toggle off. The cap (13244) is row
// 18 -- NOT captured; the 100 is the issue's default, pinned as such.
TEST(SocialCommandsB897, IgnoreToggleListSelfAndFull) {
    SocialListsModel m;
    auto empty = eqt::state::RunIgnoreCommand(m, "", "Me");
    ASSERT_EQ(empty.lines.size(), 1u);
    EXPECT_EQ(empty.lines[0], "NAMES OF THOSE CURRENTLY BEING IGNORED:");
    EXPECT_EQ(eqt::state::RunIgnoreCommand(m, "Troll", "Me").lines.at(0),
              "You have added Troll to your IGNORE list.");
    auto list = eqt::state::RunIgnoreCommand(m, "", "Me");
    ASSERT_EQ(list.lines.size(), 2u);
    EXPECT_EQ(list.lines[1], "Troll");
    EXPECT_EQ(eqt::state::RunIgnoreCommand(m, "troll", "Me").lines.at(0),
              "You have removed Troll from your IGNORE list.");
    EXPECT_EQ(eqt::state::RunIgnoreCommand(m, "me", "Me").lines.at(0),
              "That person cannot be ignored.");
    EXPECT_TRUE(m.ignored().empty());
    for (int i = 0; i < 100; ++i) m.edit(SocialList::Ignored, SocialEditOp::Add, "i" + std::to_string(i));
    EXPECT_EQ(eqt::state::RunIgnoreCommand(m, "Another", "Me").lines.at(0),
              "You have all 100 ignore slots full already.  You must remove some to add new ones.");
}

// Rows 1-5 replayed: the official client's own chat log for the same command
// sequence, line for line (timestamps stripped, other chatter skipped).
TEST(SocialCommandsB897, ReplaysTheOraclesChatLog) {
    const fs::path log = kLedger / "A.chat.txt";
    if (!fs::exists(log)) GTEST_SKIP() << "no UIGAP-ORACLE/213 capture";
    SocialListsModel m;
    std::vector<std::string> ours;
    auto run = [&](const eqt::state::SocialOutcome& o) {
        ours.insert(ours.end(), o.lines.begin(), o.lines.end());
    };
    run(eqt::state::RunFriendsCommand(m, "Testbuddy", "Testchar"));   // row 1
    run(eqt::state::RunFriendsCommand(m, "", "Testchar"));            // row 2
    run(eqt::state::RunFriendsCommand(m, "Nobody", "Testchar"));      // row 3
    run(eqt::state::RunFriendsCommand(m, "Testbuddy", "Testchar"));   // row 4
    run(eqt::state::RunFriendsCommand(m, "", "Testchar"));
    run(eqt::state::RunFriendsCommand(m, "testbuddy", "Testchar"));   // row 5
    run(eqt::state::RunFriendsCommand(m, "", "Testchar"));
    run(eqt::state::RunIgnoreCommand(m, "Testbuddy", "Testchar"));    // row 6
    run(eqt::state::RunIgnoreCommand(m, "", "Testchar"));             // row 7
    run(eqt::state::RunIgnoreCommand(m, "Testchar", "Testchar"));     // row 8
    run(eqt::state::RunIgnoreCommand(m, "Testbuddy", "Testchar"));    // row 10

    // The oracle's lines, in order, keeping only those our sequence could print.
    std::istringstream in(ReadFile(log));
    std::string line;
    std::vector<std::string> theirs;
    while (std::getline(in, line) && theirs.size() < ours.size()) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t close = line.find("] ");
        if (line.empty() || line[0] != '[' || close == std::string::npos) continue;
        const std::string text = line.substr(close + 2);
        if (std::find(ours.begin(), ours.end(), text) != ours.end()) theirs.push_back(text);
    }
    EXPECT_EQ(ours, theirs);
}

TEST(SocialCommandsB897, FriendsWhoUsesTheStoredListOrTheGivenOne) {
    SocialListsModel m;
    auto empty = eqt::state::RunFriendsWho(m, "");
    EXPECT_TRUE(empty.whoList.empty());
    ASSERT_EQ(empty.lines.size(), 1u);
    EXPECT_EQ(empty.lines[0], "You have no friends, but you can add some with: /friends <name>");
    m.edit(SocialList::Friends, SocialEditOp::Add, "Aaa");
    m.edit(SocialList::Friends, SocialEditOp::Add, "Bbb");
    EXPECT_EQ(eqt::state::RunFriendsWho(m, "").whoList, "Aaa,Bbb");
    EXPECT_EQ(eqt::state::RunFriendsWho(m, " Ccc,Ddd ").whoList, "Ccc,Ddd") << "verbatim, as T150";
}

TEST(SocialCommandsB897, WindowEditsAreAddOnlyAndRemoveOnly) {
    SocialListsModel m;
    auto a = eqt::state::ApplySocialEdit(m, SocialList::Friends, SocialEditOp::Add, "Bob", "Me");
    EXPECT_TRUE(a.changed);
    auto again = eqt::state::ApplySocialEdit(m, SocialList::Friends, SocialEditOp::Add, "bob", "Me");
    EXPECT_FALSE(again.changed) << "Add of a present name must not toggle it off";
    EXPECT_TRUE(again.lines.empty());
    EXPECT_TRUE(m.contains(SocialList::Friends, "Bob"));
    auto gone = eqt::state::ApplySocialEdit(m, SocialList::Friends, SocialEditOp::Remove, "Nobody", "Me");
    EXPECT_FALSE(gone.changed);
}

// ===========================================================================
// 5. The ini shape and the persistence tick
// ===========================================================================

TEST(SocialIniB897, TheOraclesOwnFileParses) {
    const fs::path ini = kOracle / "Obian_project1999.ini";
    if (!fs::exists(ini)) GTEST_SKIP() << "no oracle character file";
    std::vector<std::string> f, ig;
    eqt::state::ParseSocialListsIni(ReadFile(ini), f, ig);
    const std::vector<std::string> want = {"Xenephon", "bilblow", "aaodyn", "caricature",
                                           "khozmo", "shikaka", "bortill"};
    EXPECT_EQ(f, want);
    EXPECT_TRUE(ig.empty());
    std::vector<std::string> f2, ig2;
    eqt::state::ParseSocialListsIni(ReadFile(kOracle / "Logjam_project1999.ini"), f2, ig2);
    ASSERT_EQ(ig2.size(), 1u);
    EXPECT_EQ(ig2[0], "ilvvqwgwqvvqgll");
}

// Rows 11 + 17: after the window added Zzfriend and Zzignore, the official
// client's own file. Writing the same lists into it must reproduce it
// byte for byte (sorted, tail-packed, *NULL*, CRLF, other sections kept).
TEST(SocialIniB897, WriterReproducesTheOraclesFileExactly) {
    const fs::path ini = kLedger / "ini_after_window.ini";
    if (!fs::exists(ini)) GTEST_SKIP() << "no UIGAP-ORACLE/213 capture";
    const std::string theirs = ReadFile(ini);
    std::vector<std::string> f, ig;
    eqt::state::ParseSocialListsIni(theirs, f, ig);
    EXPECT_EQ(f, (std::vector<std::string>{"Nobody", "Zzfriend"}));
    EXPECT_EQ(ig, (std::vector<std::string>{"Zzignore"}));
    SocialListsModel m;
    m.edit(SocialList::Friends, SocialEditOp::Add, "Zzfriend");   // added in the
    m.edit(SocialList::Friends, SocialEditOp::Add, "Nobody");     // other order
    m.edit(SocialList::Ignored, SocialEditOp::Add, "Zzignore");
    std::string ours = eqt::state::MergeSocialListsIni(theirs, m);
    std::string want = theirs;
    while (!want.empty() && (want.back() == '\n' || want.back() == '\r')) want.pop_back();
    while (!ours.empty() && (ours.back() == '\n' || ours.back() == '\r')) ours.pop_back();
    EXPECT_EQ(ours, want);
}

TEST(SocialIniB897, WriteIsTailPackedCrlfAllKeysAndKeepsOtherSections) {
    SocialListsModel m;
    m.edit(SocialList::Friends, SocialEditOp::Add, "Alpha");
    m.edit(SocialList::Friends, SocialEditOp::Add, "beta");
    m.edit(SocialList::Ignored, SocialEditOp::Add, "Troll");
    const std::string existing =
        "[Friends]\r\nFriend0=Stale\r\n[HotButtons]\r\nPage1Button1=B0\r\n[Ignored]\r\nIgnored5=Old\r\n[Socials]\r\nPage1Button1Name=Hi\r\n";
    const std::string out = eqt::state::MergeSocialListsIni(existing, m);
    EXPECT_NE(out.find("[Friends]\r\nFriend0=*NULL*\r\n"), std::string::npos);
    EXPECT_NE(out.find("Friend97=*NULL*\r\nFriend98=Alpha\r\nFriend99=beta\r\n"), std::string::npos);
    EXPECT_NE(out.find("Ignored98=*NULL*\r\nIgnored99=Troll\r\n"), std::string::npos);
    EXPECT_EQ(out.find("Stale"), std::string::npos);
    EXPECT_EQ(out.find("Old"), std::string::npos);
    EXPECT_NE(out.find("[HotButtons]\r\nPage1Button1=B0\r\n"), std::string::npos);
    EXPECT_NE(out.find("[Socials]\r\nPage1Button1Name=Hi\r\n"), std::string::npos);
    size_t keys = 0;
    for (size_t p = out.find("\r\nFriend"); p != std::string::npos; p = out.find("\r\nFriend", p + 1)) ++keys;
    EXPECT_EQ(keys, 100u);

    std::vector<std::string> f, ig;
    eqt::state::ParseSocialListsIni(out, f, ig);
    EXPECT_EQ(f, (std::vector<std::string>{"Alpha", "beta"}));
    EXPECT_EQ(ig, (std::vector<std::string>{"Troll"}));
}

TEST(SocialIniB897, FileNameIsTheOraclesAndSanitised) {
    EXPECT_EQ(eqt::state::SocialListsFileName("Obian", "project1999"), "Obian_project1999.ini");
    EXPECT_EQ(eqt::state::SocialListsCacheName("../x", "a b"), "ui/___x_a_b.ini");
    EXPECT_NE(eqt::state::SocialListsFileName("A", "b"), UiPreferencesFileName("A", "b"))
        << "the lists live in <Char>_<server>.ini, not the UI_ file";
}

TEST(SocialIniB897, SyncLoadsSavesAndResetsOnIdentity) {
    CacheDir cache;
    SocialListsModel m;
    eqt::state::SocialListsSync sync;
    EXPECT_FALSE(sync.tick(m, "", "srv")) << "no character, nothing happens";
    EXPECT_TRUE(sync.tick(m, "Alice", "srv")) << "a new identity publishes";
    EXPECT_FALSE(sync.tick(m, "Alice", "srv"));
    EXPECT_FALSE(fs::exists(cache.file("ui/Alice_srv.ini"))) << "a fresh character writes nothing";

    m.edit(SocialList::Friends, SocialEditOp::Add, "Bob");
    EXPECT_TRUE(sync.tick(m, "Alice", "srv"));
    ASSERT_TRUE(fs::exists(cache.file("ui/Alice_srv.ini")));
    EXPECT_NE(ReadFile(cache.file("ui/Alice_srv.ini")).find("Friend99=Bob"), std::string::npos);

    EXPECT_TRUE(sync.tick(m, "Carol", "srv"));
    EXPECT_TRUE(m.friends().empty()) << "nothing carries over to another character";

    SocialListsModel fresh;
    eqt::state::SocialListsSync sync2;
    EXPECT_TRUE(sync2.tick(fresh, "Alice", "srv"));
    EXPECT_EQ(fresh.friends(), (std::vector<std::string>{"Bob"}));
}

// ===========================================================================
// 6. The panel
// ===========================================================================

namespace {
eqt::state::SocialListsChangedData Lists(int nFriends, int nIgnored) {
    eqt::state::SocialListsChangedData d;
    for (int i = 0; i < nFriends; ++i) d.friends.push_back("Friend" + std::to_string(i));
    for (int i = 0; i < nIgnored; ++i) d.ignored.push_back("Pest" + std::to_string(i));
    return d;
}
irr::core::vector2di Center(const irr::core::rect<irr::s32>& r) { return r.getCenter(); }
}  // namespace

TEST(FriendsPanelB897, HitTestsBothTabs) {
    const UILayout l = LayoutAt(800, 600);
    FriendsPanelState s;
    auto at = [&](const irr::core::rect<irr::s32>& r) {
        const auto c = Center(r);
        return hitTestFriendsWindow(l, s, c.X, c.Y).hit;
    };
    EXPECT_EQ(at(FriendsLayout::whoButtonRect(l)), FriendsHit::None) << "closed: nothing";
    s.isOpen = true;
    applySocialListsChanged(s, Lists(3, 2));
    EXPECT_EQ(at(FriendsLayout::closeBoxRect(l)), FriendsHit::Close);
    EXPECT_EQ(at(FriendsLayout::tabRect(l, 0)), FriendsHit::Tab0);
    EXPECT_EQ(at(FriendsLayout::tabRect(l, 1)), FriendsHit::Tab1);
    EXPECT_EQ(at(FriendsLayout::whoButtonRect(l)), FriendsHit::Who);
    EXPECT_EQ(at(FriendsLayout::contactButtonRect(l)), FriendsHit::Contact);
    EXPECT_EQ(at(FriendsLayout::addButtonRect(l)), FriendsHit::Add);
    EXPECT_EQ(at(FriendsLayout::deleteButtonRect(l)), FriendsHit::Delete);
    EXPECT_EQ(at(FriendsLayout::nameInputRect(l)), FriendsHit::NameInput);
    const auto row1 = Center(FriendsLayout::rowRect(l, 1));
    auto t = hitTestFriendsWindow(l, s, row1.X, row1.Y);
    EXPECT_EQ(t.hit, FriendsHit::Row);
    EXPECT_EQ(t.row, 1);
    EXPECT_EQ(at(FriendsLayout::rowRect(l, 5)), FriendsHit::Window) << "an empty row is not a row";
    EXPECT_EQ(hitTestFriendsWindow(l, s, 0, 0).hit, FriendsHit::None);

    s.activePage = 1;
    EXPECT_EQ(at(FriendsLayout::whoButtonRect(l)), FriendsHit::Window)
        << "the Ignored page declares no Who button";
    EXPECT_EQ(at(FriendsLayout::contactButtonRect(l)), FriendsHit::Window)
        << "the Ignored page declares no Contact button";
    EXPECT_EQ(at(FriendsLayout::addButtonRect(l)), FriendsHit::Add);
    EXPECT_EQ(at(FriendsLayout::deleteButtonRect(l)), FriendsHit::Delete);
}

TEST(FriendsPanelB897, ClicksResolveToActions) {
    FriendsPanelState s;
    s.isOpen = true;
    applySocialListsChanged(s, Lists(3, 2));

    // Row 12: Delete / Contact are disabled (inert) with no selection.
    EXPECT_EQ(applyFriendsClick(s, {FriendsHit::Delete, -1}).kind, FriendsAction::Kind::None);
    EXPECT_EQ(applyFriendsClick(s, {FriendsHit::Contact, -1}).kind, FriendsAction::Kind::None);

    applyFriendsClick(s, {FriendsHit::Row, 2});
    EXPECT_EQ(s.selected[0], 2);
    auto contact = applyFriendsClick(s, {FriendsHit::Contact, -1});   // row 15
    EXPECT_EQ(contact.kind, FriendsAction::Kind::Contact);
    EXPECT_EQ(contact.name, "Friend2");
    auto del = applyFriendsClick(s, {FriendsHit::Delete, -1});
    EXPECT_EQ(del.kind, FriendsAction::Kind::Remove);
    EXPECT_EQ(del.page, 0);
    EXPECT_EQ(del.name, "Friend2");
    // Row 13: Who with no selection -> the stored list (empty name).
    auto who = applyFriendsClick(s, {FriendsHit::Who, -1});
    EXPECT_EQ(who.kind, FriendsAction::Kind::Who);
    EXPECT_TRUE(who.name.empty());
    // Row 14: Who with a selected friend -> a who-all for that name.
    applyFriendsClick(s, {FriendsHit::Row, 0});
    auto whoOne = applyFriendsClick(s, {FriendsHit::Who, -1});
    EXPECT_EQ(whoOne.kind, FriendsAction::Kind::Who);
    EXPECT_EQ(whoOne.name, "Friend0");

    // Row 17: Add takes the Editbox's name; row 12: disabled while empty.
    EXPECT_EQ(applyFriendsClick(s, {FriendsHit::Add, -1}).kind, FriendsAction::Kind::None);
    applyFriendsClick(s, {FriendsHit::NameInput, -1});
    EXPECT_EQ(s.focusedEntry, 0);
    s.nameEntry[0].inputText = "  Newpal trailing";
    auto add = applyFriendsClick(s, {FriendsHit::Add, -1});
    EXPECT_EQ(add.kind, FriendsAction::Kind::Add);
    EXPECT_EQ(add.name, "Newpal");
    EXPECT_TRUE(s.nameEntry[0].inputText.empty());

    // The Ignored tab acts on its own list, and has no Who.
    applyFriendsClick(s, {FriendsHit::Tab1, -1});
    EXPECT_EQ(s.activePage, 1);
    EXPECT_EQ(s.focusedEntry, -1) << "a tab click takes the caret away";
    applyFriendsClick(s, {FriendsHit::Row, 1});
    auto delIg = applyFriendsClick(s, {FriendsHit::Delete, -1});
    EXPECT_EQ(delIg.page, 1);
    EXPECT_EQ(delIg.name, "Pest1");
    EXPECT_EQ(applyFriendsClick(s, {FriendsHit::Who, -1}).kind, FriendsAction::Kind::None);
    EXPECT_EQ(applyFriendsClick(s, {FriendsHit::Close, -1}).kind, FriendsAction::Kind::Close);
}

TEST(FriendsPanelB897, ScrollAndSelectionClamp) {
    FriendsPanelState s;
    s.isOpen = true;
    applySocialListsChanged(s, Lists(25, 0));
    EXPECT_EQ(s.maxScroll(0), 15);
    applyFriendsClick(s, {FriendsHit::ScrollDown, -1});
    EXPECT_EQ(s.scroll[0], 10);
    applyFriendsClick(s, {FriendsHit::ScrollDown, -1});
    EXPECT_EQ(s.scroll[0], 15);
    applyFriendsClick(s, {FriendsHit::Row, 24});
    EXPECT_EQ(s.selected[0], 24);
    applySocialListsChanged(s, Lists(12, 0));
    EXPECT_EQ(s.selected[0], -1) << "a selection past the new end is dropped";
    EXPECT_EQ(s.scroll[0], 2);
    applyFriendsClick(s, {FriendsHit::ScrollUp, -1});
    EXPECT_EQ(s.scroll[0], 0);
}

// ===========================================================================
// 7. The game thread, through a real EverQuest
// ===========================================================================

namespace {
std::vector<uint8_t> ChannelMessage(const std::string& sender, uint32_t channel,
                                    const std::string& message, const std::string& target = "") {
    return zonetest::BuildPayload(150 + message.size() + 1, [&](EQ::Net::DynamicPacket& p) {
        p.PutCString(2, target.c_str());
        p.PutCString(66, sender.c_str());
        p.PutUInt32(134, channel);
        p.PutCString(150, message.c_str());
    });
}
}  // namespace

TEST(SocialGameThreadB897, IgnoredSenderIsDroppedOnEveryChannelAndNeverBecomesReplyTarget) {
    EverQuest eq = zonetest::MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    eq.GetGameState().social().edit(SocialList::Ignored, SocialEditOp::Add, "Pest");

    // Row 9: tell, say, shout, ooc, auction and emote are hidden on A.
    const uint32_t channels[] = {CHAT_CHANNEL_TELL, CHAT_CHANNEL_SAY, CHAT_CHANNEL_SHOUT,
                                 CHAT_CHANNEL_OOC, CHAT_CHANNEL_AUCTION, CHAT_CHANNEL_EMOTE};
    for (uint32_t c : channels) {
        zonetest::ReplayZone(eq, HC_OP_ChannelMessage, ChannelMessage("pest", c, "spam", "Testchar"));
    }
    const std::string emote = "Pest waves at you.";
    zonetest::ReplayZone(eq, HC_OP_Emote,
                         zonetest::BuildPayload(6 + emote.size() + 1, [&](EQ::Net::DynamicPacket& p) {
                             p.PutUInt32(2, 0);
                             p.PutCString(6, emote.c_str());
                         }));
    int chats = 0;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::ChatMessage) ++chats;
    }
    EXPECT_EQ(chats, 0) << "every measured channel from an ignored sender is dropped (case-insensitive)";
    EXPECT_TRUE(eq.GetLastTellSender().empty()) << "an ignored tell is not /reply's target";

    zonetest::ReplayZone(eq, HC_OP_ChannelMessage,
                         ChannelMessage("Friendly", CHAT_CHANNEL_TELL, "hi", "Testchar"));
    chats = 0;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::ChatMessage) ++chats;
    }
    EXPECT_EQ(chats, 1) << "the negative: a sender not on the list still reaches chat";
    EXPECT_EQ(eq.GetLastTellSender(), "Friendly");

    // Group and guild were NOT measured (row 9 covers six channels), so they
    // are not filtered -- the documented default, owed to the oracle.
    zonetest::ReplayZone(eq, HC_OP_ChannelMessage,
                         ChannelMessage("Pest", CHAT_CHANNEL_GROUP, "grp", "Testchar"));
    chats = 0;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::ChatMessage) ++chats;
    }
    EXPECT_EQ(chats, 1);
    eq.setBridge(nullptr);
}

// Rows 11, 17: a window Add prints the command's line and persists at once.
TEST(SocialGameThreadB897, WindowIntentEditsPrintsPublishesAndPersists) {
    CacheDir cache;
    EverQuest eq = zonetest::MakeEq();
    zonetest::TestBridge bridge;
    eq.setBridge(&bridge);
    eq.SetLoadingPhase(LoadingPhase::COMPLETE);

    eq.GameTick(0.0f);   // identity bind: Testchar on "test" -- a SocialListsChanged
    bool published = false;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SocialListsChanged) published = true;
    }
    EXPECT_TRUE(published) << "the first tick publishes the (empty) lists to the window";

    bridge.pushIntent(eqt::events::SocialListIntent{0, eqt::events::SocialListOp::Add, "Buddy"});
    eq.GameTick(0.0f);
    std::vector<std::string> system;
    eqt::state::SocialListsChangedData lists;
    published = false;
    for (auto& ev : bridge.drainEvents()) {
        if (ev.type == eqt::state::GameEventType::SystemMessage) {
            system.push_back(std::get<eqt::state::ChatMessageData>(ev.data).message);
        } else if (ev.type == eqt::state::GameEventType::SocialListsChanged) {
            lists = std::get<eqt::state::SocialListsChangedData>(ev.data);
            published = true;
        }
    }
    ASSERT_TRUE(published);
    EXPECT_EQ(lists.friends, (std::vector<std::string>{"Buddy"}));
    EXPECT_NE(std::find(system.begin(), system.end(), "Buddy is now your friend."), system.end());
    ASSERT_TRUE(fs::exists(cache.file("ui/Testchar_test.ini")));
    EXPECT_NE(ReadFile(cache.file("ui/Testchar_test.ini")).find("Friend99=Buddy"), std::string::npos);

    // Remove through the same intent; then an unchanged tick publishes nothing.
    bridge.pushIntent(eqt::events::SocialListIntent{0, eqt::events::SocialListOp::Remove, "buddy"});
    eq.GameTick(0.0f);
    bridge.drainEvents();
    EXPECT_TRUE(eq.GetGameState().social().friends().empty());
    eq.GameTick(0.0f);
    for (auto& ev : bridge.drainEvents()) {
        EXPECT_NE(ev.type, eqt::state::GameEventType::SocialListsChanged);
    }
    eq.setBridge(nullptr);
}

// ===========================================================================
// 8. Wiring
// ===========================================================================

TEST(FriendsWiringB897, TheKeyIsAltF) {
    // MEASURED: review/T377/oracle/T377_oracle_options_keys_UI_page1.png,
    // "Toggle friend window   Alt F", Alternate column empty (eqstr 6599).
    auto& mgr = eqt::input::HotkeyManager::instance();
    mgr.resetToDefaults();
    const auto a = mgr.getAction(irr::KEY_KEY_F, false, false, true, eqt::input::HotkeyMode::Player);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(*a, eqt::input::HotkeyAction::ToggleFriends);
    const std::string json = ReadFile(fs::path(WILLEQ_REPO_ROOT) / "config/hotkeys.json");
    EXPECT_NE(json.find("\"ToggleFriends\": \"Alt+F\""), std::string::npos)
        << "the shipped hotkeys.json must carry it (T464's lesson)";
}

TEST(FriendsWiringB897, OpenStatePersistsAsFriendsWindowShow) {
    UiPreferences p;
    EXPECT_FALSE(p.friendsWindow);
    p.friendsWindow = true;
    const std::string text = SerializeUiPreferences(p);
    EXPECT_NE(text.find("[FriendsWindow]\nShow=1"), std::string::npos);
    EXPECT_TRUE(ParseUiPreferences(text).friendsWindow);
    FriendsPanelState s;
    UiPreferencesBinding b;
    b.friends = &s;
    ApplyUiPreferences(p, b);
    EXPECT_TRUE(s.isOpen);
    EXPECT_TRUE(CollectUiPreferences(b, p.cameraMode).friendsWindow);
}

TEST(FriendsWiringB897, SourceCensus) {
    const fs::path root = WILLEQ_REPO_ROOT;
    const std::string reg = ReadFile(root / "src/client/action/command_processor_registration.cpp");
    EXPECT_EQ(reg.find("\"friendswho\", {\"friends\"}"), std::string::npos)
        << "/friends must not alias the who query again";
    EXPECT_NE(reg.find("\"friends\", {\"friend\"}"), std::string::npos);
    EXPECT_NE(reg.find("\"ignore\", {}"), std::string::npos);
    const std::string bridge = ReadFile(root / "src/client/bridge/irrlicht_bridge.cpp");
    EXPECT_NE(bridge.find("case state::GameEventType::SocialListsChanged:"), std::string::npos);
    EXPECT_NE(bridge.find("applySocialListsChanged("), std::string::npos);
    const std::string slash = ReadFile(root / "src/client/graphics/slash/renderer_slash_commands.cpp");
    EXPECT_NE(slash.find("cmd == \"/friendswindow\""), std::string::npos);
    const std::string chat = ReadFile(root / "src/client/chat/chat_component.cpp");
    const size_t ignore = chat.find("social().isIgnored(sender)");
    const size_t reply = chat.find("m_last_tell_sender = sender;");
    ASSERT_NE(ignore, std::string::npos);
    EXPECT_LT(ignore, reply) << "the filter must run before /reply's bookkeeping";
}
