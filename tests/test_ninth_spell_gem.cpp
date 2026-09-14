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
#include "client/graphics/ui/ui_renderer.h"
#undef private
#undef protected

#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/ui_layout.h"
#include "spell_wire_payloads.h"
#include "zone_stream_test_utils.h"

#include <irrlicht.h>

#include <iterator>
#include <set>
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
    EXPECT_FALSE(mgr->swapGems(0, 8));
    EXPECT_EQ(mgr->getMemorizedSpell(8), kFirstSpell + 8) << "a refused offer must not clear it";
}

TEST(NinthSpellGem, ThePanelHoldsEightGems) {
    EQT::Graphics::SpellGemPanelState panel;
    EXPECT_EQ(std::size(panel.gems), 8u);
}

namespace {

// renderSpellGemPanel() on the null driver, no atlas: every sprite and rect
// is queued as a solid quad (UIRenderer::drawSprite fallback), so the queue
// is the emitted geometry. A gem slot is SLOT_SIZE square and sits at the
// panel's own x.
void ExpectEightOnScreenSlots(irr::u32 w, irr::u32 h) {
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

    constexpr irr::s32 S = EQT::Graphics::UILayout::SLOT_SIZE;
    std::set<irr::s32> slotRows;
    for (const auto& q : ui.atlasQuads_) {
        const auto& r = q.destRect;
        if (r.getWidth() == S && r.getHeight() == S &&
            r.UpperLeftCorner.X == layout.spellGems.UpperLeftCorner.X) {
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
