// T483 (board B762, decision B770 option (a)) -- the ninth Titanium spell gem.
//
// Titanium carries NINE memorised spells on the wire:
// common/patches/titanium_limits.h:308-328 (CastingSlot Gem1..Gem9,
// MaxGems = 9), and titanium_structs.h:854 PlayerProfile mem_spells[9] at
// offset 4360. eqemu gates CASTING from slot 8 on AA 1071 Mnemonic Retention
// (zone/client_packet.cpp:4387-4392), and the oracle client's loaded skin
// (uifiles/default/EQUI_CastSpellWnd.xml:152-158) parks CSPW_Spell8 at
// (-5000,-5000) with no size, so the official client SHOWS eight gems.
//
// The ruling: store all nine, show and offer eight. So this file pins both
// halves. The numbers here are literals (8, 9) on purpose, not the client's
// constants, so a constant that drifts is a red test rather than a tautology.

#include <gtest/gtest.h>

#define private public
#define protected public
#include "client/eq.h"
#include "client/spell/spell_manager.h"
#include "client/graphics/ui/spell_gem_layout.h"  // B1077: the oracle's cell size
#include "client/graphics/ui/ui_renderer.h"
#undef private
#undef protected

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/buff_layout.h"  // B1077: the buff buttons
#include "spell_wire_payloads.h"
#include "zone_stream_test_utils.h"

#include <irrlicht.h>

#include <cctype>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace zonetest;
using namespace spellwire;

namespace {

constexpr uint16_t kSelfId = 460;
constexpr size_t kMemSpellsOffset = 4360;  // titanium_structs.h:854
constexpr uint32_t kFirstSpell = 1000;

// A zero-filled PlayerProfile (the 2-byte opcode, then the 19,588-byte
// struct) with mem_spells[0..8] = 1000..1008.
std::vector<uint8_t> NineSpellProfile() {
    return BuildPayload(2 + 19588, [](EQ::Net::DynamicPacket& p) {
        for (uint32_t i = 0; i < 9; ++i) {
            p.PutUInt32(2 + kMemSpellsOffset + i * 4, kFirstSpell + i);
        }
        p.PutUInt32(2 + 14384, kSelfId);
    });
}

// EverQuest is not copyable, so the seams are set in place.
void PrepGemEq(EverQuest& eq) {
    eq.Net().Test_SetBypassSendGuardForSeam(true);
    eq.Test_SetMySpawnIdForSeam(kSelfId);
}

}  // namespace

// ---------------------------------------------------------------------------
// Storage: nine.
// ---------------------------------------------------------------------------

TEST(NinthSpellGem, PlayerProfileKeepsTheNinthMemorisedSpell) {
    EverQuest eq = MakeEq();
    PrepGemEq(eq);
    auto* mgr = eq.GetSpellManager();
    ASSERT_NE(mgr, nullptr);

    ReplayZone(eq, HC_OP_PlayerProfile, NineSpellProfile());

    for (uint8_t g = 0; g < 8; ++g) {
        EXPECT_EQ(mgr->getMemorizedSpell(g), kFirstSpell + g) << "gem " << int(g);
    }
    EXPECT_EQ(mgr->getMemorizedSpell(8), kFirstSpell + 8)
        << "mem_spells[8] was dropped at the PlayerProfile read";
    EXPECT_EQ(mgr->getGemState(8), EQ::GemState::Ready);
}

TEST(NinthSpellGem, MemorizeSpellForSlotEightLandsInState) {
    EverQuest eq = MakeEq();
    PrepGemEq(eq);
    auto* mgr = eq.GetSpellManager();
    ASSERT_NE(mgr, nullptr);

    constexpr uint32_t kSpell = 2345;
    ReplayZone(eq, HC_OP_MemorizeSpell, MemorizeSpellPayload(8, kSpell, 1));

    EXPECT_EQ(mgr->getMemorizedSpell(8), kSpell)
        << "OP_MemorizeSpell for slot 8 was dropped at the zone dispatcher";
    EXPECT_EQ(mgr->getGemState(8), EQ::GemState::Ready);
    EXPECT_EQ(eq.GetGameState().spells().getGemSpellId(8), kSpell)
        << "the game-state gem table must carry the ninth gem too";

    // And the server's un-memorise empties it again.
    ReplayZone(eq, HC_OP_MemorizeSpell, MemorizeSpellPayload(8, kSpell, 2));
    EXPECT_EQ(mgr->getMemorizedSpell(8), EQ::SPELL_UNKNOWN);
    EXPECT_EQ(eq.GetGameState().spells().getGemSpellId(8), 0xFFFFFFFFu);
}

// ---------------------------------------------------------------------------
// Offer: eight. The official client offers no ninth gem.
// ---------------------------------------------------------------------------

TEST(NinthSpellGem, TheNinthGemIsNeverCastFromTheClient) {
    EverQuest eq = MakeEq();
    PrepGemEq(eq);
    auto* mgr = eq.GetSpellManager();
    ASSERT_NE(mgr, nullptr);

    ReplayZone(eq, HC_OP_PlayerProfile, NineSpellProfile());

    EXPECT_EQ(mgr->beginCastFromGem(8), EQ::CastResult::InvalidSpell);
    EXPECT_FALSE(mgr->isCasting());
    EXPECT_FALSE(mgr->memorizeSpell(kFirstSpell, 8));
    EXPECT_FALSE(mgr->forgetSpell(8));
    EXPECT_EQ(mgr->getMemorizedSpell(8), kFirstSpell + 8) << "a refused offer must not clear it";
}

TEST(NinthSpellGem, ThePanelHoldsEightGems) {
    EQT::Graphics::SpellGemPanelState panel;
    EXPECT_EQ(std::size(panel.gems), 8u);
}

namespace {

// renderSpellGemPanel() on the null driver, no atlas: every sprite and rect
// is queued as a solid quad (UIRenderer::drawSprite fallback), so the queue
// is the emitted geometry. B1077: a gem cell is the oracle's own 36x28
// (spell_gem_layout.h) at the window's X=3, not a SLOT_SIZE square at the
// panel's own x.
void ExpectEightOnScreenSlots(irr::u32 w, irr::u32 h) {
    using namespace EQT::Graphics::SpellGemLayout;
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(w, h);
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) GTEST_SKIP() << "Failed to create EDT_NULL Irrlicht device";

    EQT::Graphics::UILayout layout;
    layout.computeLayout(static_cast<irr::s32>(w), static_cast<irr::s32>(h));

    EQT::Graphics::UIRenderer ui;
    ui.init(device->getVideoDriver(), nullptr, nullptr);
    ui.beginFrame();
    EQT::Graphics::SpellGemPanelState panel;
    EQT::Graphics::renderSpellGemPanel(ui, layout, panel);

    const irr::s32 cellX = layout.spellGems.UpperLeftCorner.X + kGemX;
    std::set<irr::s32> slotRows;
    for (const auto& q : ui.atlasQuads_) {
        const auto& r = q.destRect;
        if (r.getWidth() == kGemW && r.getHeight() == kGemH &&
            r.UpperLeftCorner.X == cellX) {
            slotRows.insert(r.UpperLeftCorner.Y);
            EXPECT_GE(r.UpperLeftCorner.Y, 0) << w << "x" << h;
            EXPECT_LE(r.LowerRightCorner.X, static_cast<irr::s32>(w)) << w << "x" << h;
            EXPECT_LE(r.LowerRightCorner.Y, layout.hotbar.UpperLeftCorner.Y) << w << "x" << h;
            EXPECT_TRUE(layout.spellGems.isRectCollided(r)) << w << "x" << h;
        }
    }
    EXPECT_EQ(slotRows.size(), 8u) << w << "x" << h;
    device->drop();
}

}  // namespace

TEST(NinthSpellGem, PanelEmitsExactlyEightOnScreenSlotsAt800x600) {
    ExpectEightOnScreenSlots(800, 600);
}

TEST(NinthSpellGem, PanelEmitsExactlyEightOnScreenSlotsAt1280x720) {
    ExpectEightOnScreenSlots(1280, 720);
}

// ── B1077 (GitLab #397): the window geometry, re-read from the oracle XML ──
//
// The layout headers (spell_gem_layout.h, buff_layout.h) quote the official
// client's UI xml. These cases re-read the xml and compare, so a skin that
// changes (or a quote that slips) is a red test, and the panel's drawn rects
// are proven to be the xml's rects.

namespace {

const char* kUifilesDir = "/mnt/projects/EverQuestP1999/uifiles/default";

std::string readXml(const std::string& name) {
    std::ifstream f(std::string(kUifilesDir) + "/" + name);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

// The (Location, Size) rect of one named item inside a UI xml. The FIRST
// item="NAME" match is the element's own opening tag (the <Pieces> references
// that name it inside CastSpellWnd come *before* the element), so the tag
// after the probe must actually open the element -- and the search window
// ends at that element's closing tag, so a later sibling's tags can never
// bleed in.
bool xmlItemRect(const std::string& xml, const std::string& item,
                 int& x, int& y, int& w, int& h) {
    const std::string probe = "item=\"" + item + "\"";
    const size_t i = xml.find(probe);
    if (i == std::string::npos) return false;
    const size_t openA = xml.rfind('<', i - 1);
    if (openA == std::string::npos) return false;
    const size_t sp = xml.find_first_of(" />", openA + 1);
    const std::string tag = (sp == std::string::npos)
        ? xml.substr(openA + 1, i - openA - 1)
        : xml.substr(openA + 1, sp - openA - 1);
    const std::string closeTag = "</" + tag + ">";
    const size_t end = xml.find(closeTag, i + probe.size());
    if (end == std::string::npos) return false;
    const std::string seg = xml.substr(openA, end - openA);
    auto grab = [&](const std::string& after, const std::string& t) -> int {
        const size_t a = seg.find(after);
        if (a == std::string::npos) return -1;
        const size_t b = seg.find("<" + t + ">", a);
        if (b == std::string::npos) return -1;
        const size_t c = seg.find("</" + t + ">", b);
        if (c == std::string::npos) return -1;
        std::string v = seg.substr(b + t.size() + 2,
                                   c - (b + t.size() + 2));
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front())))
            v.erase(v.begin());
        while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
            v.pop_back();
        return v.empty() ? -1 : std::stoi(v);
    };
    x = grab("<Location>", "X");
    y = grab("<Location>", "Y");
    w = grab("<Size>", "CX");
    h = grab("<Size>", "CY");
    return x >= -10000 && y >= -10000 && w > 0 && h > 0;
}

// The first <tag> value after a marker string, or -1.
int firstTagAfter(const std::string& seg, const std::string& marker,
                  const std::string& tag) {
    const size_t a = seg.find(marker);
    if (a == std::string::npos) return -1;
    const size_t b = seg.find("<" + tag + ">", a);
    if (b == std::string::npos) return -1;
    const size_t c = seg.find("</" + tag + ">", b);
    if (c == std::string::npos) return -1;
    std::string v = seg.substr(b + tag.size() + 2,
                               c - (b + tag.size() + 2));
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front())))
        v.erase(v.begin());
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back())))
        v.pop_back();
    return v.empty() ? -1 : std::stoi(v);
}

}  // namespace

TEST(SpellGemLayout, CastSpellWndQuotedFromTheOracleXml) {
    const std::string xml = readXml("EQUI_CastSpellWnd.xml");
    if (xml.empty()) GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;

    using namespace EQT::Graphics::SpellGemLayout;
    // The window: 48x290, Style_Sizable false.
    int x = 0, y = 0, w = 0, h = 0;
    ASSERT_TRUE(xmlItemRect(xml, "CastSpellWnd", x, y, w, h));
    EXPECT_EQ(w, kWindowW) << "CastSpellWnd CX";
    EXPECT_EQ(h, kWindowH) << "CastSpellWnd CY";

    // The eight shown gems: CSPW_Spell0..7, 36x28 at (3, 10+30i) -- and the
    // same rect the panel draws (gemRect).
    for (int i = 0; i < kGemCount; ++i) {
        ASSERT_TRUE(xmlItemRect(xml, "CSPW_Spell" + std::to_string(i),
                                x, y, w, h))
            << "CSPW_Spell" << i;
        EXPECT_EQ(w, kGemW) << "CSPW_Spell" << i << " CX";
        EXPECT_EQ(h, kGemH) << "CSPW_Spell" << i << " CY";
        EXPECT_EQ(x, kGemX) << "CSPW_Spell" << i << " X";
        EXPECT_EQ(y, kGemFirstY + i * kGemPitchY) << "CSPW_Spell" << i << " Y";
        const auto r = gemRect(i);
        EXPECT_EQ(r.UpperLeftCorner.X, x);
        EXPECT_EQ(r.UpperLeftCorner.Y, y);
        EXPECT_EQ(r.getWidth(), kGemW);
        EXPECT_EQ(r.getHeight(), kGemH);
    }

    // CSPW_Spell8 is the one parked at -5000 with no size (T483: shown
    // nowhere). It has no <Size> at all, so only its Location is quoted.
    {
        const std::string probe = "item=\"CSPW_Spell8\"";
        const size_t i = xml.find(probe);
        ASSERT_NE(i, std::string::npos);
        const size_t openA = xml.rfind('<', i - 1);
        const size_t sp = xml.find_first_of(" />", openA + 1);
        const std::string tag = xml.substr(openA + 1, sp - openA - 1);
        const size_t end = xml.find("</" + tag + ">", i + probe.size());
        const std::string seg = xml.substr(openA, end - openA);
        EXPECT_EQ(firstTagAfter(seg, "Location", "X"), -5000)
            << "CSPW_Spell8 X must stay off-screen";
    }

    // CSPW_SpellBook: 22x22 at (10,252) -- the book button.
    ASSERT_TRUE(xmlItemRect(xml, "CSPW_SpellBook", x, y, w, h));
    EXPECT_EQ(w, kBookW);
    EXPECT_EQ(h, kBookH);
    EXPECT_EQ(x, kBookX);
    EXPECT_EQ(y, kBookY);
    const auto b = bookRect();
    EXPECT_EQ(b.UpperLeftCorner.X, x);
    EXPECT_EQ(b.UpperLeftCorner.Y, y);
}

TEST(BuffWindowLayout, BuffButtonsQuotedFromTheOracleXml) {
    const std::string xml = readXml("EQUI_BuffWindow.xml");
    if (xml.empty()) GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;

    using namespace EQT::Graphics::BuffLayout;
    // The fifteen shown buttons: 34x34, columns 3/48/93, rows 3/49/95/141/187.
    for (int i = 0; i < kShownCount; ++i) {
        int x = 0, y = 0, w = 0, h = 0;
        ASSERT_TRUE(xmlItemRect(xml, "BW_Buff" + std::to_string(i) + "_Button",
                                x, y, w, h))
            << "BW_Buff" << i << "_Button";
        EXPECT_EQ(w, kButtonSize) << "BW_Buff" << i << " CX";
        EXPECT_EQ(h, kButtonSize) << "BW_Buff" << i << " CY";
        EXPECT_EQ(x, kColumnX[i % kCols]) << "BW_Buff" << i << " X";
        EXPECT_EQ(y, kRowY[i / kCols]) << "BW_Buff" << i << " Y";
    }

    // The decal: 30x30 at the button's (2,2) corner.
    const size_t i = xml.find("BW_Buff0_Button");
    ASSERT_NE(i, std::string::npos);
    const std::string seg = xml.substr(i, 1200);
    EXPECT_EQ(firstTagAfter(seg, "DecalOffset", "X"), kDecalOffset)
        << "DecalOffset X";
    EXPECT_EQ(firstTagAfter(seg, "DecalSize", "CX"), kDecalSize)
        << "DecalSize CX";
}
