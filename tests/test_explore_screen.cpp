// B888-CC3 (GitLab #204): the Explore mode (a SELECT-screen mode, not creation)
// -- the device-free state machine and the EXW_ExploreModeWindow overlay
// layout, checked against the stock EQUI_CharacterSelect.xml when present.
//
// The flow and its oracle citations:
//   include/client/graphics/ui/explore_screen.h   (the machine)
//   include/client/graphics/ui/explore_layout.h   (the EXW overlay rects)
//   refactor/B888-CC3/README.md + explore.tsv     (measured: oracle-a6/b1,
//     the backdrop scene file clz/clz.eqg from the client's own load log
//     oracle-c1/client_dbg.txt)

#include <gtest/gtest.h>

#include "client/graphics/eq/pfs.h"
#include "client/graphics/eq/s3d_loader.h"
#include "client/graphics/ui/char_select_layout.h"
#include "client/graphics/ui/char_select_screen.h"
#include "client/graphics/ui/explore_layout.h"
#include "client/graphics/ui/explore_screen.h"
#include "client/login_world/char_create_data.h"
#include "client/login_world/character_select.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace EQT::Graphics;
using namespace eqt::login_world;
using eqt::charcreate::CharCreateData;
namespace fs = std::filesystem;
using E = ExploreLayout;

namespace {

const fs::path kXml = "/mnt/projects/EverQuestP1999/uifiles/default/EQUI_CharacterSelect.xml";

// A three-character view (a small account -- the ledger's A-runs used two
// characters, the third keeps the row math non-trivial).
CharSelectScreenState StateWith(int n, int selected = -1) {
    CharSelectScreenState s;
    for (int i = 0; i < n; ++i) {
        CharacterSelectRow r;
        r.name = "Char" + std::to_string(i);
        s.rows.push_back(r);
    }
    s.selected = selected;
    return s;
}

std::string XmlBody(const std::string& xml, const std::string& name) {
    const size_t at = xml.find("item=\"" + name + "\"");
    if (at == std::string::npos) return std::string();
    const size_t next = xml.find("item=\"", at + 6);
    return xml.substr(at, next == std::string::npos ? std::string::npos : next - at);
}
int XmlTag(const std::string& body, const std::string& tag) {
    // Non-greedy: the value is the first </tag>, not the next sibling's close.
    const size_t at = body.find("<" + tag + ">");
    if (at == std::string::npos) return -99999;
    const size_t open = at + tag.size() + 2;
    const size_t close = body.find("</" + tag + ">", open);
    if (close == std::string::npos) return -99999;
    return std::atoi(body.c_str() + open);
}

}  // namespace

// ---------------------------------------------------------------------------
// The machine
// ---------------------------------------------------------------------------

TEST(ExploreMode, EnterOpensAndEscCloses) {
    auto s = StateWith(3, 0);
    EXPECT_FALSE(s.exploreOpen);
    enterExplore(s);
    EXPECT_TRUE(s.exploreOpen);
    // Opening changes no selection and posts nothing.
    EXPECT_EQ(s.selected, 0);
    exitExplore(s);
    EXPECT_FALSE(s.exploreOpen);
    // The selection survives the round trip.
    EXPECT_EQ(s.selected, 0);
}

TEST(ExploreMode, EnterInertWhenAlreadyClosed) {
    auto s = StateWith(3, 0);
    exitExplore(s);   // a no-op when already closed
    EXPECT_FALSE(s.exploreOpen);
}

TEST(ExploreMode, ClickSelectsRowAndKeepsCursorOnAMiss) {
    auto s = StateWith(3, 0);
    enterExplore(s);
    exploreSelectRow(s, 2);
    EXPECT_EQ(s.selected, 2);
    // A click that lands on no model (-1) keeps the cursor where it was.
    exploreSelectRow(s, -1);
    EXPECT_EQ(s.selected, 2);
    // A row past the end is a miss too.
    exploreSelectRow(s, 99);
    EXPECT_EQ(s.selected, 2);
}

TEST(ExploreMode, ClickInertOutsideExplore) {
    auto s = StateWith(3, 0);
    exploreSelectRow(s, 1);   // Explore not open: no change
    EXPECT_EQ(s.selected, 0);
}

TEST(ExploreMode, EnterWorldUsesTheListCursorNotTheLoginTarget) {
    // oracle-b1: the list-selected character entered (Testcaster), not the
    // login target. The cursor (selected) is what enters, the same body as
    // the Enter World button.
    auto s = StateWith(3, 2);
    enterExplore(s);
    exploreSelectRow(s, 1);
    auto a = exploreEnterWorld(s);
    EXPECT_EQ(a.kind, CharSelectAction::Kind::Post);
    EXPECT_EQ(a.request.kind, CharacterSelectRequest::Kind::EnterWorld);
    EXPECT_EQ(a.request.name, std::string("Char1"));
    // A second Enter is inert while the first is in flight (blocked).
    auto b = exploreEnterWorld(s);
    EXPECT_EQ(b.kind, CharSelectAction::Kind::None);
}

TEST(ExploreMode, EnterInertOutsideExplore) {
    auto s = StateWith(3, 1);
    auto a = exploreEnterWorld(s);   // Explore not open: the select screen's own Enter is a different path
    EXPECT_EQ(a.kind, CharSelectAction::Kind::None);
}

TEST(ExploreMode, ClickAndEnterBlockedWhileRequestInFlight) {
    auto s = StateWith(3, 0);
    enterExplore(s);
    exploreEnterWorld(s);   // posts, now blocked
    exploreSelectRow(s, 1);
    EXPECT_EQ(s.selected, 0);   // the in-flight request freezes the machine
}

TEST(ExploreMode, ExploreDoesNotOpenOverTheDeletePromptOrCreation) {
    {
        auto s = StateWith(3, 0);
        s.confirm.isOpen = true;   // the delete prompt is modal
        enterExplore(s);
        EXPECT_FALSE(s.exploreOpen);
        s.confirm.isOpen = false;
    }
    {
        auto s = StateWith(3, 0);
        s.create.isOpen = true;    // the creation screen IS the whole screen
        enterExplore(s);
        EXPECT_FALSE(s.exploreOpen);
        s.create.isOpen = false;
    }
}

TEST(ExploreMode, CameraVerbIsForwardOnDWhileOpen) {
    auto s = StateWith(3, 0);
    // Outside Explore the camera holds.
    ExploreCameraVerb out = exploreCameraVerb(s, true);
    EXPECT_FALSE(out.active());
    enterExplore(s);
    // Forward on `d` is the MEASURED verb (oracle-a6 A6: holding d moved the
    // camera, full-frame diff 45.48).
    ExploreCameraVerb fwd = exploreCameraVerb(s, true);
    EXPECT_NEAR(fwd.forward, 1.0f, 1e-6f);
    EXPECT_TRUE(fwd.active());
    // Releasing the key (the next frame) holds the camera.
    ExploreCameraVerb hold = exploreCameraVerb(s, false);
    EXPECT_FALSE(hold.active());
}

// ---------------------------------------------------------------------------
// The EXW_ExploreModeWindow overlay, against the stock XML
// ---------------------------------------------------------------------------

TEST(ExploreLayout, MatchesTheXml) {
    if (!fs::exists(kXml)) GTEST_SKIP() << "no oracle install at " << kXml;
    std::ifstream in(kXml, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string xml = ss.str();

    // EXW_ExploreModeWindow2 (the char-info plate) at (0,0) 400x25.
    {
        const std::string b = XmlBody(xml, "EXW_ExploreModeWindow2");
        ASSERT_FALSE(b.empty());
        EXPECT_EQ(E::kInfoWinW, XmlTag(b, "CX"));
        EXPECT_EQ(E::kInfoWinH, XmlTag(b, "CY"));
        // RelativePosition false -> absolute, at (0,0).
        EXPECT_EQ(0, XmlTag(b, "X"));
        EXPECT_EQ(0, XmlTag(b, "Y"));
        EXPECT_NE(std::string::npos, b.find("<RelativePosition>false"));
        EXPECT_NE(std::string::npos, b.find("EXW_CharInfoLabel"));
    }
    // EXW_CharInfoLabel (5,2) 390x15, AlignCenter (390 = WindowWidth - 2*inset).
    {
        const std::string b = XmlBody(xml, "EXW_CharInfoLabel");
        ASSERT_FALSE(b.empty());
        EXPECT_EQ(E::kInsetX, XmlTag(b, "X"));
        EXPECT_EQ(2, XmlTag(b, "Y"));
        EXPECT_EQ(E::kInsetX * 2, E::kInfoWinW - E::kLabelW);
        EXPECT_NE(std::string::npos, b.find("AlignCenter"));
    }
    // EXW_ExploreModeWindow3 (the instruction plate) at (0,420) 400x40.
    {
        const std::string b = XmlBody(xml, "EXW_ExploreModeWindow3");
        ASSERT_FALSE(b.empty());
        EXPECT_EQ(E::kInstrWinW, XmlTag(b, "CX"));
        EXPECT_EQ(E::kInstrWinH, XmlTag(b, "CY"));
        EXPECT_EQ(0, XmlTag(b, "X"));
        EXPECT_EQ(E::kInstructionsY, XmlTag(b, "Y"));
        EXPECT_NE(std::string::npos, b.find("EXW_InfoLabel1"));
        EXPECT_NE(std::string::npos, b.find("EXW_InfoLabel2"));
    }
    // The two instruction labels (5,2)/(5,18), AlignCenter, with their stock text.
    {
        const std::string b = XmlBody(xml, "EXW_InfoLabel1");
        ASSERT_FALSE(b.empty());
        EXPECT_EQ(E::kInsetX, XmlTag(b, "X"));
        EXPECT_EQ(2, XmlTag(b, "Y"));
        EXPECT_NE(std::string::npos, b.find("Click on a character to select that character."));
        EXPECT_NE(std::string::npos, b.find("<AlignCenter>true"));
    }
    {
        const std::string b = XmlBody(xml, "EXW_InfoLabel2");
        ASSERT_FALSE(b.empty());
        EXPECT_EQ(E::kInsetX, XmlTag(b, "X"));
        EXPECT_EQ(18, XmlTag(b, "Y"));
        EXPECT_NE(std::string::npos, b.find("Press Enter to enter the game, or press Esc to leave explore mode."));
    }

    // The derived screen rects.
    EXPECT_EQ(E::charInfoWindowRect(0, 0), irr::core::rect<irr::s32>(0, 0, 400, 25));
    EXPECT_EQ(E::charInfoLabelRect(0, 0), irr::core::rect<irr::s32>(5, 2, 395, 17));
    EXPECT_EQ(E::instructionsWindowRect(0, 0), irr::core::rect<irr::s32>(0, 420, 400, 460));
    EXPECT_EQ(E::infoLabel1Rect(0, 0), irr::core::rect<irr::s32>(5, 422, 395, 437));
    EXPECT_EQ(E::infoLabel2Rect(0, 0), irr::core::rect<irr::s32>(5, 438, 395, 453));
}

// =============== The backdrop: clz.eqg, headless ===============
// The decisive evidence from oracle-c1 (client_dbg.txt L276-279): the client
// initialises the `clz` world *before* starting the char select UI --
// `World initialized: clz` / `load done.` / `Starting char select.` -- and
// the zone ships as `clz.eqg`, a PFS archive. The archive's member census
// (measured below, 256 members) is the load-bearing finding for CC-3's 3D
// slice: it holds DDS textures + `obj_*/obp_*` .mod/.lit static models +
// `ter_clz.ter/.lit` + `clz.zon`, but **no `.wld`** -- while
// `S3DLoader::loadZone()` refuses an archive without a WLD (s3d_loader.cpp
// "No WLD file found in archive"). So fable's zone-load path (open PFS ->
// S3DLoader::loadZone -> combined geometry) is what opens the file, but it
// does NOT stand up clz's geometry today: the 3D slice must either teach the
// loader the `.ter` terrain format (or parse `ter_clz.ter` directly) or
// stand the static .mod/.lit models instead. This test is the net for the
// format fact itself, so the slice's first step is a documented choice
// against measured data, not a rediscovery.

namespace {
// The client-internal char-select world. On the dev checkout it ships at the
// EverQuest client root (the P1999 tree's top level); WILLEQ_CLZ overrides it
// (the oracle host keeps it at data/eq_client/clz.eqg).
std::string clzPath() {
    const char* env = std::getenv("WILLEQ_CLZ");
    if (env && *env) return env;
    const std::string candidates[] = {
        "/mnt/projects/EverQuestP1999/clz.eqg",
        "data/eq_client/clz.eqg",
    };
    for (const auto& c : candidates)
        if (std::filesystem::exists(c)) return c;
    return "";
}
}  // namespace

TEST(ExploreScreen, BackdropZoneLoadsHeadless) {
    const std::string path = clzPath();
    if (path.empty()) {
        GTEST_SKIP() << "clz.eqg not present on this host (WILLEQ_CLZ unset)";
    }

    // The raw PFS archive opens and carries the zone's contents -- the format
    // check the loader itself performs (PFS magic, no extension sniffing).
    // Measured census (P1999 terrain archive): 256 members = 68 DDS +
    // 29 .mod + 157 .lit static objects + ter_clz.ter/.lit + clz.zon.
    EQT::Graphics::PfsArchive pfs;
    ASSERT_TRUE(pfs.open(path)) << "clz.eqg failed to open as a PFS archive: " << path;
    const auto files = pfs.getFiles();
    EXPECT_EQ(files.size(), 256u) << "clz's member count is measured; a new file is a ruling";

    // The two facts the 3D slice rests on:
    //   1. it has NO .wld member, and S3DLoader::loadZone() requires one --
    //      so the production zone-load path does not stand up clz today;
    //   2. it does hold zone content (terrain + static objects) to stand up.
    int wld = 0, ter = 0, mod = 0, lit = 0, zon = 0;
    for (const auto& [n, e] : files) {
        auto ext = [&](const char* s) {
            return n.size() > 4 && n.substr(n.size() - 4) == s;
        };
        if (ext(".wld")) wld++;
        if (ext(".ter")) ter++;
        if (ext(".mod")) mod++;
        if (ext(".lit")) lit++;
        if (ext(".zon")) zon++;
    }
    EXPECT_EQ(wld, 0) << "a .wld in clz would change the 3D slice's format choice";
    EXPECT_EQ(ter, 1) << "exactly one terrain fragment (ter_clz): " << ter;
    EXPECT_EQ(mod, 29) << "the showroom's static .mod count is measured";
    EXPECT_EQ(lit, 157) << "the showroom's static .lit count is measured";
    EXPECT_EQ(zon, 1) << "clz.zon names the zone";

    // The production path, stated as a fact rather than a happy assumption:
    // the zone loader refuses the archive (no WLD) -- this IS the seam the
    // 3D slice has to close.
    EQT::Graphics::S3DLoader loader;
    ASSERT_FALSE(loader.loadZone(path)) << "loadZone should refuse clz until the WLD/ter seam is closed";
    EXPECT_NE(loader.getError().find("WLD"), std::string::npos)
        << "the refusal must be the WLD leg, not a corrupted archive: " << loader.getError();
}
