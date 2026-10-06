// B834 (GitLab #202, UIGAP-small) -- the tip windows.
//
// Oracle (tip_window.h has the detail): the trigger table observed in
// review/UIGAP-ORACLE/202/README.md; the texts are the client's own
// help/tips/ctx*.html and TOTD.txt; the ini keys are eqgame.exe's own
// ctx-file -> key table; the switches land in UI_<char>_<server>.ini [Tips]
// (real files: ShowTips=0, ShowTOTD=0, Inventory=0, Trainer=0, ...).
//
// Pinned: parsing, the catalog against the exe's pairs, the pop rules and
// the checkboxes, the [Tips] round trip, and -- on a device-less renderer --
// a window's rising edge popping its tip once, and ESC closing the tip FIRST
// and then the inventory (the measured order: tip, bag, inventory).

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "client/graphics/frame/frame_component.h"
#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/ui/tip_window.h"
#include "client/graphics/ui/ui_preferences.h"

using namespace EQT::Graphics;
namespace fs = std::filesystem;

namespace {

const char* kBankHtml =
    "<HEAD>\n<TITLE>Bank Tip window</TITLE>\n</HEAD>\n<BODY LINK=\"#14AO14\">\n"
    "<c \"#DCF000\">Bank Tips</c>\n<br><br>\n"
    "- <c \"#66CCFF\">Click on the \"Change\" button</c> to convert all your coin.\n<br>\n"
    "</BODY>";

const char* kTotd =
    "###\n<c \"#00CC00\">\nGameplay Tip\n</c>\nFire Beetle eyes make excellent light sources!\n"
    "###\n<c \"#00CC00\">\nSafety Tip\n</c>\nNever share your password.\n";

UILayout Layout() {
    UILayout l;
    l.computeLayout(1024, 768);
    return l;
}

}  // namespace

TEST(TipParse, CtxHtmlTitleAndParagraphs) {
    const TipDocument d = parseTipHtml(kBankHtml);
    EXPECT_EQ(d.title, "Bank Tip window");
    ASSERT_GE(d.paragraphs.size(), 3u);
    EXPECT_EQ(d.paragraphs[0], "Bank Tips");
    EXPECT_EQ(d.paragraphs[1], "");
    EXPECT_EQ(d.paragraphs[2], "- Click on the \"Change\" button to convert all your coin.");
}

TEST(TipParse, TipOfTheDayEntriesAndHeadings) {
    const auto e = parseTipOfTheDay(kTotd);
    ASSERT_EQ(e.size(), 2u);
    EXPECT_EQ(e[0].title, "Tip of the Day");
    EXPECT_EQ(e[0].paragraphs[0], "Gameplay Tip");  // the measured grab's heading
    EXPECT_EQ(e[0].paragraphs[2], "Fire Beetle eyes make excellent light sources!");
    EXPECT_EQ(e[1].paragraphs[0], "Safety Tip");
}

// The pairs quoted from `strings eqgame.exe` (the exe's own table).
TEST(TipCatalog, KeysAreTheExecutablesOwn) {
    const std::pair<const char*, const char*> exe[] = {
        {"Inventory", "ctxinventory.html"}, {"Spellbook", "ctxspellbook.html"},
        {"OptKeys", "ctxkeyboardoptions.html"}, {"Friends", "ctxfriends.html"},
        {"SkillsWindow", "ctxskills.html"}, {"Merchant", "ctxmerchant.html"},
        {"Bank", "ctxbank.html"}, {"Trainer", "ctxtrainer.html"}, {"Loot", "ctxlooting.html"},
        {"WorldContainer", "ctxcontainer.html"}, {"Quantity", "ctxquantity.html"},
    };
    ASSERT_EQ(tipCatalog().size(), std::size(exe));
    for (const auto& [key, file] : exe) {
        ASSERT_NE(tipFileForKey(key), nullptr) << key;
        EXPECT_STREQ(tipFileForKey(key), file) << key;
    }
}

// Where the client's files are present, every catalogued file parses to the
// caption the oracle grabs read.
TEST(TipCatalog, TheClientsOwnFilesParse) {
    const fs::path dir = "/mnt/projects/EverQuestP1999/help/tips";
    if (!fs::exists(dir)) GTEST_SKIP() << "no EQ client files here";
    for (const auto& e : tipCatalog()) {
        std::ifstream in(dir / e.file);
        ASSERT_TRUE(in.good()) << e.file;
        std::stringstream ss;
        ss << in.rdbuf();
        const TipDocument d = parseTipHtml(ss.str());
        EXPECT_NE(d.title.find("Tip window"), std::string::npos) << e.file << ": " << d.title;
        EXPECT_FALSE(d.paragraphs.empty()) << e.file;
    }
    std::ifstream totd(dir / "TOTD.txt");
    std::stringstream ss;
    ss << totd.rdbuf();
    EXPECT_GT(parseTipOfTheDay(ss.str()).size(), 50u);
}

TEST(TipRules, OncePerSessionAndTheCheckboxes) {
    const UILayout layout = Layout();
    TipPreferences prefs;
    TipWindowState s;
    EXPECT_TRUE(shouldPopTip(prefs, s, "Bank"));
    EXPECT_FALSE(shouldPopTip(prefs, s, "NotATip"));
    openContextTip(s, "Bank", parseTipHtml(kBankHtml), layout);
    EXPECT_TRUE(s.isOpen);
    EXPECT_EQ(s.title, "Bank Tip window");
    EXPECT_FALSE(shouldPopTip(prefs, s, "Bank")) << "once per session";

    // "Don't show this tip again" -> [Tips] Bank=0, applied on Close.
    EXPECT_EQ(hitTestTipWindow(layout, s, TipLayout::noShowThisBoxRect(layout).getCenter().X,
                               TipLayout::noShowThisBoxRect(layout).getCenter().Y),
              TipHit::NoShowThis);
    applyTipClick(s, prefs, TipHit::NoShowThis, {}, layout);
    EXPECT_TRUE(prefs.off.empty()) << "not until Close";
    applyTipClick(s, prefs, TipHit::Close, {}, layout);
    EXPECT_FALSE(s.isOpen);
    EXPECT_EQ(prefs.off.count("Bank"), 1u);

    // "Don't show any tips" -> ShowTips=0: nothing pops any more.
    TipWindowState fresh;
    openContextTip(fresh, "Inventory", parseTipHtml(kBankHtml), layout);
    applyTipClick(fresh, prefs, TipHit::NoShowAll, {}, layout);
    applyTipClick(fresh, prefs, TipHit::Close, {}, layout);
    EXPECT_FALSE(prefs.showTips);
    TipWindowState another;
    EXPECT_FALSE(shouldPopTip(prefs, another, "Merchant"));
}

TEST(TipRules, TipOfTheDayShowAnotherAndItsCheckbox) {
    const UILayout layout = Layout();
    const auto entries = parseTipOfTheDay(kTotd);
    TipPreferences prefs;
    TipWindowState s;
    ASSERT_TRUE(shouldPopTipOfTheDay(prefs, s));
    openTipOfTheDay(s, entries, layout);
    EXPECT_TRUE(s.isTipOfTheDay);
    EXPECT_EQ(s.lines.front(), "Gameplay Tip");
    EXPECT_FALSE(shouldPopTipOfTheDay(prefs, s));
    // The TOTD has no "this tip" box (measured); Show Another Tip advances.
    const auto next = TipLayout::nextTipRect(layout).getCenter();
    EXPECT_EQ(hitTestTipWindow(layout, s, next.X, next.Y), TipHit::NextTip);
    applyTipClick(s, prefs, TipHit::NextTip, entries, layout);
    EXPECT_EQ(s.lines.front(), "Safety Tip");
    applyTipClick(s, prefs, TipHit::NoShowAll, entries, layout);
    applyTipClick(s, prefs, TipHit::Close, entries, layout);
    EXPECT_FALSE(prefs.showTotd);
    EXPECT_TRUE(prefs.showTips);
}

TEST(TipLayoutOracle, MeasuredPlacementAt1024x768) {
    const auto w = TipLayout::windowRect(Layout());
    EXPECT_EQ(w.UpperLeftCorner.X, 664);
    EXPECT_EQ(w.UpperLeftCorner.Y, 234);
    EXPECT_EQ(w.getWidth(), 360);
    EXPECT_EQ(w.getHeight(), 300);
}

// [Tips] round-trips in the official shape, and a real player file's
// section parses (UI_Laddington_project1999.ini: ShowTOTD=0 ShowTips=0 Trainer=0).
TEST(TipPreferencesIni, TheOfficialSectionRoundTrips) {
    const UiPreferences p = ParseUiPreferences("[Tips]\r\nShowTOTD=0\r\nShowTips=0\r\nTrainer=0\r\n");
    EXPECT_FALSE(p.tips.showTotd);
    EXPECT_FALSE(p.tips.showTips);
    EXPECT_EQ(p.tips.off, (std::set<std::string>{"Trainer"}));
    const std::string text = SerializeUiPreferences(p);
    EXPECT_NE(text.find("[Tips]\nShowTips=0\nShowTOTD=0\nTrainer=0\n"), std::string::npos) << text;
    EXPECT_TRUE(ParseUiPreferences(text) == p);
    EXPECT_TRUE(ParseUiPreferences("") .tips == TipPreferences{}) << "absent = shown";
}

// --- the frame: rising edges and ESC ---------------------------------------

class TipFrameTest : public ::testing::Test {
protected:
    IrrlichtRenderer renderer;
    fs::path dir;
    FrameComponent& F() { return renderer.Frame(); }

    void SetUp() override {
        dir = fs::path(::testing::TempDir()) / "b834_client";
        fs::create_directories(dir / "help" / "tips");
        std::ofstream(dir / "help" / "tips" / "ctxinventory.html")
            << "<TITLE>Inventory Tip window</TITLE><BODY>Inventory Tips<br>- equip</BODY>";
        std::ofstream(dir / "help" / "tips" / "TOTD.txt") << kTotd;
        renderer.Context().config.eqClientPath = dir.string();
        renderer.Context().zoneReady = true;
        renderer.Context().loadingScreenVisible = false;
        F().uiPrefsLoaded_ = true;
        F().uiLayout_.computeLayout(1024, 768);
    }
};

TEST_F(TipFrameTest, TheTipOfTheDayPopsOnceAfterZoneIn) {
    F().updateTipWindows();
    EXPECT_TRUE(F().tipState_.isOpen);
    EXPECT_TRUE(F().tipState_.isTipOfTheDay);
    closeTipWindow(F().tipState_, F().tipPrefs_);
    F().updateTipWindows();
    EXPECT_FALSE(F().tipState_.isOpen);
}

TEST_F(TipFrameTest, OpeningTheInventoryPopsItsTipOnceAndEscClosesTheTipFirst) {
    F().tipState_.totdShownThisSession = true;  // isolate the context tip
    F().observeEscWindows();
    F().updateTipWindows();
    EXPECT_FALSE(F().tipState_.isOpen);

    F().inventoryState_.activePopup = PopupType::Inventory;
    F().updateTipWindows();
    F().observeEscWindows();
    ASSERT_TRUE(F().tipState_.isOpen);
    EXPECT_EQ(F().tipState_.title, "Inventory Tip window");

    // The measured order: the tip closes first, then the inventory.
    EXPECT_TRUE(F().handleEscapeKey());
    EXPECT_FALSE(F().tipState_.isOpen);
    EXPECT_EQ(F().inventoryState_.activePopup, PopupType::Inventory);

    // Closing and re-opening the inventory does not pop it again this session.
    F().inventoryState_.activePopup = PopupType::None;
    F().updateTipWindows();
    F().inventoryState_.activePopup = PopupType::Inventory;
    F().updateTipWindows();
    EXPECT_FALSE(F().tipState_.isOpen);
}

TEST_F(TipFrameTest, ASuppressedTipDoesNotPop) {
    F().tipState_.totdShownThisSession = true;
    F().tipPrefs_.off.insert("Inventory");
    F().updateTipWindows();
    F().inventoryState_.activePopup = PopupType::Inventory;
    F().updateTipWindows();
    EXPECT_FALSE(F().tipState_.isOpen);
}
