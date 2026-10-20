// B992 (GitLab #312): static-UI text respects panel z-order.
//
// Before B992 UIRenderer::endFrame() drew every window's quads and then every
// window's text, so a window drawn on top of another showed the lower one's
// labels and counts through its own panel: the purse counts through the
// confirmation dialog and the QuantityWnd (review/B442b), a lower bag's label
// and stack counts through the bag opened on top of it (review/B899, which
// worked around it by drawing only the topmost container window).
//
// Each window is now its own layer (UIRenderer::beginLayer(), `layer()`), and
// endFrame() flushes quads-then-text per GROUP of adjacent layers, splitting
// only where a window's quads meet text queued before it.
//
// The pixel assertion: inside the upper window's rect, a frame with the
// lower window under it may differ from the upper window drawn alone by no
// more than what the upper window's own panel lets through. B1002 (#322):
// the panel fill is now per-window -- the atlas `white` sprite
// tinted at windowPanelAlpha(w), 255 for every fable window (the official
// client's measured blend alpha for its Style_Transparent windows is 255, so
// all 29 fable windows draw fully opaque, see window_flags.h). With 255 the
// lower window's text is ABSENT in the region the upper covers: it reaches 0,
// and kMaxThrough is then only the headroom for the upper window's OWN text
// anti-aliasing (a glyph-edge pixel blends against black alone, against the
// lower content stacked; the measured maxima are 85 and 26, bound 96). Before
// B1002 the shared panel_background was baked at alpha 180
// (tools/ui_atlas_builder.cpp) and let 75/255 through (kMaxThrough 76);
// that uniform 180 is superseded, and the pre-B992 render fails these
// assertions (a 180-alpha panel dims but does not hide).
//
// The draw-call account (TheLayeredFlushCost...) is the measurement the
// approach was chosen by; its numbers are pinned.
//
// Same harness as test_static_ui_frame_cost.cpp: a real headless GLES2 device
// (surfaceless EGL / llvmpipe) and the real UIAtlas out of willeq.eqe1; it
// self-skips without them.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/window_flags.h"  // B1002: translucentPanelAlphaOverride
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/hotbar_panel.h"  // B1025-H2: renderHotbar's home
#include "client/graphics/ui/tradeskill_panel.h"
#include "client/graphics/ui/merchant_panel.h"
#include "client/graphics/ui/combat_ability_panel.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/item_display_panel.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/cursor_attachment.h"
#include "client/graphics/ui/container_panel.h"
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/inventory_layout.h"  // B1002: windowRect for the test
#include "client/graphics/ui/quantity_window.h"
#include "client/graphics/ui/confirmation_dialog.h"
#include "client/graphics/ui/confirmation_layout.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/chat/chat_message_buffer.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace EQT::Graphics;
namespace inv = eqt::inventory;

namespace {

const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

// B1002 (#322): the panel is now drawn at its window's own alpha, 255 for
// every fable window (window_flags.h). The lower window's TEXT is what the
// assertion must show to be ABSENT: at a 255-alpha panel it reaches 0, while
// B992's 180-alpha panel_background let it through at 75/255. The only
// residual difference between "upper alone" and "lower+upper" is the upper
// window's OWN text anti-aliasing: a glyph edge pixel with partial alpha
// blends against black in the alone frame and against the (dimmed) lower
// content in the stacked frame. The measured maxima are 85 (confirmation
// dialog) and 26 (bags), so the bound covers text AA with margin and still
// fails on any lower-text bleed (the pre-B992 render measures 217-238).
// This is the re-based assertion the issue asks for: lower text absent, not
// "dimmed to the panel's own passes" (B992's 76).
constexpr int kMaxThrough = 96;

// One pixel's max per-channel difference (the diffInside body, unrolled).
int diffInsidePixel(irr::video::IImage* a, irr::video::IImage* b, irr::s32 x,
                    irr::s32 y) {
    const irr::video::SColor ca = a->getPixel(x, y);
    const irr::video::SColor cb = b->getPixel(x, y);
    return std::max({std::abs(int(ca.getRed()) - int(cb.getRed())),
                     std::abs(int(ca.getGreen()) - int(cb.getGreen())),
                     std::abs(int(ca.getBlue()) - int(cb.getBlue()))});
}

}  // namespace

class UiPanelZOrderTest : public ::testing::Test {
protected:
    static constexpr irr::u32 kW = 800;
    static constexpr irr::u32 kH = 600;
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch textBatch_;
    UILayout layout_;
    UIRenderer ui_;

    PlayerStatsData stats_;
    TargetInfoData target_;
    eqt::ui::ChatMessageBuffer chatBuffer_{500};
    ChatPanelState chat_;
    HotbarPanelState hotbar_;
    SpellGemPanelState gems_;
    BuffBarState buffs_;
    CastingBarState casting_;
    GroupPanelState group_;
    PetPanelState pet_;
    InventoryPanelState inv_;
    SpellbookPopupState spellbook_;
    SkillsPopupState skills_;
    LootPanelState loot_;
    TradeskillPanelState tradeskill_;
    MerchantPanelState merchant_;
    CombatAbilityPanelState combat_;
    BankPanelState bank_;
    ItemDisplayPanelState itemDisplay_;
    OptionsPanelState options_;
    XPBarState xp_;
    CursorAttachmentState cursor_;
    ContainerWindowsState containers_;
    QuantityWindowState quantity_;
    ConfirmationDialogState confirm_;

    void SetUp() override {
        std::ifstream probe(kWilleqEqe1);
        if (!probe.good()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
        if (!loader_.open(kWilleqEqe1)) GTEST_SKIP() << "Failed to open " << kWilleqEqe1;

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device (surfaceless EGL unavailable)";
        driver_ = device_->getVideoDriver();
        textBatch_.init(device_->getGUIEnvironment());
        layout_.computeLayout(static_cast<irr::s32>(kW), static_cast<irr::s32>(kH));

        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
        atlas_ = std::make_unique<UIAtlas>();
        if (!atlas_->load(driver_, cache_.get())) {
            GTEST_SKIP() << "UIAtlas::load() failed against willeq.eqe1";
        }
        ui_.init(driver_, atlas_.get(), &textBatch_);

        // test_static_ui_frame_cost.cpp's session.
        stats_.name = "Testcaster";
        stats_.level = 50;
        stats_.curHP = stats_.maxHP = 1200;
        stats_.curMana = stats_.maxMana = 2400;
        stats_.curEndurance = stats_.maxEndurance = 800;
        gems_.gems[0].spellId = 288;
        gems_.gems[0].gemState = 1;
        gems_.gems[0].spellName = "Minor Shielding";
        hotbar_.slots[0].type = eqt::HotbarSlotType::Spell;
        hotbar_.slots[0].name = "Minor Shielding";
        xp_.progress = 0.4f;
        chat_.messageBuffer = &chatBuffer_;
        for (int i = 0; i < 120; ++i) {
            chatBuffer_.addSystemMessage("a Qeynos guard hits a gnoll pup for " +
                                         std::to_string(10 + i % 17) + " points of damage.");
        }
        chatBuffer_.processPending();
        for (int i : {1, 2, 13, 14, 17}) {
            inv_.equipSlots[i].hasItem = true;
            inv_.equipSlots[i].itemName = "Robe";
            inv_.equipSlots[i].iconId = 600 + i;
        }
        for (int i = 0; i < 4; ++i) {
            inv_.generalSlots[i].hasItem = true;
            inv_.generalSlots[i].itemName = "Bag";
            inv_.generalSlots[i].iconId = 700 + i;
            inv_.generalSlots[i].quantity = (i == 3) ? 20 : 1;
        }
        eqt::state::CurrencyChangedData coins;
        coins.platinum = 8; coins.gold = 18; coins.silver = 8; coins.copper = 16;
        applyCurrencyChanged(inv_, coins);
    }

    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    // ui_panel_capture's B899 containers: a Backpack, a Sewing Kit, a Forge,
    // opened in that order (the Forge on top), with stack counts in cells.
    void openBag(ContainerWindowsState& s, bool world, int16_t slot, const char* name,
                 uint8_t type, uint8_t count, uint32_t icon) {
        eqt::state::TradeskillContainerOpenedEvent e;
        e.isWorldObject = world;
        e.objectId = world ? 4242u : 0u;
        e.inventorySlot = slot;
        e.containerName = name;
        e.containerType = type;
        e.slotCount = count;
        e.iconId = icon;
        applyContainerOpened(s, e);
    }
    void fillCell(ContainerWindowsState& s, int16_t slotId, uint32_t icon, uint32_t qty) {
        eqt::state::InventorySlotChangedData d;
        d.slotId = slotId;
        d.hasItem = true;
        d.itemId = 3000u + static_cast<uint32_t>(slotId);
        d.itemName = "Item";
        d.iconId = icon;
        d.quantity = qty;
        applyContainerSlotChanged(s, d);
    }
    void openBackpack(ContainerWindowsState& s) {
        openBag(s, false, inv::GENERAL_BEGIN, "Backpack", inv::CONTAINER_BACKPACK, 8, 565);
        fillCell(s, inv::calcBagSlotId(inv::GENERAL_BEGIN, 0), 537, 4);
        fillCell(s, inv::calcBagSlotId(inv::GENERAL_BEGIN, 6), 772, 12);
    }
    void openSewingKit(ContainerWindowsState& s) {
        openBag(s, false, inv::GENERAL_BEGIN + 1, "Sewing Kit", inv::CONTAINER_TAILORING, 10, 598);
        fillCell(s, inv::calcBagSlotId(inv::GENERAL_BEGIN + 1, 0), 1093, 2);
    }
    void openForge(ContainerWindowsState& s) {
        openBag(s, true, inv::SLOT_INVALID, "Forge", inv::CONTAINER_BLACKSMITHING, 10, 0);
        fillCell(s, inv::WORLD_BEGIN + 0, 700, 1);
    }

    // renderStaticUI()'s draw list, statement for statement, with each window
    // its own layer (`ui_.layer()`), minus the input pass, the paperdoll and
    // the windows this fixture has no state for (trainer, tracking, trade,
    // friends, tip).
    std::vector<std::function<void()>> staticUIWindows() {
        return {
            [&] { renderPlayerStatus(ui_.layer(), layout_, stats_); },
            [&] { renderTargetInfo(ui_.layer(), layout_, target_); },
            [&] { renderChatPanel(ui_.layer(), layout_, chat_); },
            [&] { renderHotbar(ui_.layer(), layout_, hotbar_); },
            [&] { renderSpellGemPanel(ui_.layer(), layout_, gems_); },
            [&] { renderBuffBar(ui_.layer(), layout_, buffs_); },
            [&] { renderCastingBar(ui_.layer(), layout_, casting_); },
            [&] { renderGroupPanel(ui_.layer(), layout_, group_); },
            [&] { renderPetPanel(ui_.layer(), layout_, pet_); },
            [&] { renderInventoryPopup(ui_.layer(), layout_, inv_); },
            [&] { renderSpellbookPopup(ui_.layer(), layout_, spellbook_); },
            [&] { renderSkillsPopup(ui_.layer(), layout_, skills_); },
            [&] { renderLootPopup(ui_.layer(), layout_, loot_); },
            [&] { renderTradeskillPopup(ui_.layer(), layout_, tradeskill_); },
            [&] { renderMerchantPopup(ui_.layer(), layout_, merchant_, cursor_); },
            [&] { renderCombatAbilityPanel(ui_.layer(), layout_, combat_); },
            [&] { renderBankWindow(ui_.layer(), layout_, bank_); },
            [&] { renderContainerWindows(ui_.layer(), layout_, containers_); },
            [&] { renderItemDisplayPopup(ui_.layer(), layout_, itemDisplay_); },
            [&] { renderOptionsWindow(ui_.layer(), layout_, options_); },
            [&] { renderXPBar(ui_.layer(), layout_, xp_); },
            [&] { renderQuantityWindow(ui_.layer(), layout_, quantity_); },
            [&] { renderConfirmationDialog(ui_.layer(), layout_, confirm_); },
            [&] { renderCursorAttachment(ui_.layer(), layout_, cursor_); },
        };
    }
    void submitStaticUI() {
        for (const auto& w : staticUIWindows()) w();
    }

    // The rejected alternative, measured: flush every window on its own
    // (quads, then text, per window). Driver draws summed over the frame.
    size_t perWindowFlushDraws() {
        size_t draws = 0;
        for (int frame = 0; frame < 2; ++frame) {
            draws = 0;
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            for (const auto& w : staticUIWindows()) {
                ui_.beginFrame();
                w();  // the stacked container windows split inside this one
                ui_.endFrame();
                const UIRenderer::FlushStats s = ui_.takeFlushStats();
                draws += s.driverDraws + s.textDraws;
            }
            driver_->endScene();
        }
        return draws;
    }

    // One content frame after a prime frame, with a repeat (the documented
    // surfaceless-EGL harness facts). Returns the framebuffer (caller drops).
    irr::video::IImage* renderFrames(const std::function<void()>& submit,
                                     UIRenderer::FlushStats* stats = nullptr) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();
        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui_.beginFrame();
            submit();
            ui_.endFrame();
            const UIRenderer::FlushStats s = ui_.takeFlushStats();
            if (stats) *stats = s;
            driver_->endScene();
        }
        return driver_->createScreenShot();
    }

    // Inside `r`: the largest per-channel difference between the two frames,
    // and how many pixels differ by more than kMaxThrough.
    static void diffInside(irr::video::IImage* a, irr::video::IImage* b,
                           const irr::core::rect<irr::s32>& r, int* maxDiff, size_t* over) {
        *maxDiff = 0;
        *over = 0;
        for (irr::s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y) {
            for (irr::s32 x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x) {
                const irr::video::SColor ca = a->getPixel(x, y);
                const irr::video::SColor cb = b->getPixel(x, y);
                const int d = std::max({std::abs(int(ca.getRed()) - int(cb.getRed())),
                                        std::abs(int(ca.getGreen()) - int(cb.getGreen())),
                                        std::abs(int(ca.getBlue()) - int(cb.getBlue()))});
                *maxDiff = std::max(*maxDiff, d);
                if (d > kMaxThrough) ++*over;
            }
        }
    }

    // The assertion, and its negative control with the pre-B992 flush.
    void expectLowerTextCovered(const char* what, const std::function<void()>& lowerThenUpper,
                                const std::function<void()>& upperAlone,
                                const irr::core::rect<irr::s32>& upperRect) {
        irr::video::IImage* alone = renderFrames(upperAlone);
        irr::video::IImage* stacked = renderFrames(lowerThenUpper);
        ui_.setLayeringEnabled(false);
        irr::video::IImage* preB992 = renderFrames(lowerThenUpper);
        ui_.setLayeringEnabled(true);
        ASSERT_NE(alone, nullptr);
        ASSERT_NE(stacked, nullptr);
        ASSERT_NE(preB992, nullptr);

        int maxDiff = 0;
        size_t over = 0;
        diffInside(preB992, alone, upperRect, &maxDiff, &over);
        EXPECT_GT(over, 20U) << what << ": the pre-B992 flush must show the lower window's text "
                                "through the upper one (max diff " << maxDiff
                             << ") -- otherwise this case cannot detect the bleed";
        diffInside(stacked, alone, upperRect, &maxDiff, &over);
        EXPECT_EQ(over, 0u) << what << ": " << over << " pixels inside the upper window differ "
                               "by more than its translucent panel lets through (max "
                            << maxDiff << ") -- the lower window's text is drawn over it";
        EXPECT_LE(maxDiff, kMaxThrough) << what;
        alone->drop();
        stacked->drop();
        preB992->drop();
    }

    // B1002 (#322): the translucent-over-opaque case. The measured blend alpha
    // is 255 (window_flags.h), so with the measured value a translucent window
    // still fully hides the lower one; to exercise the per-window BLEND path at
    // a genuinely translucent value, translucentPanelAlphaOverride drives a
    // translucent window to that value and this measures it from pixels.
    //
    // In the overlap of the upper window's own content with the lower one,
    // stacked ~= blend*upper + (1-blend)*lower, so (stacked - upper) ~=
    // (1-blend)*(lower - upper): at a lower TEXT pixel (bright over dark)
    // that is (1-blend)*(lower - dark) ~= (1-blend)*lower -- the lower text
    // PRESENT at the blend. maxDiff measures exactly that. Returns
    // {maxDiff, pixels exceeding blendTol}; the caller names the window.
    struct BlendResult { int maxDiff = 0; size_t over = 0; };
    BlendResult measureBlend(irr::video::IImage* a, irr::video::IImage* b,
                             const irr::core::rect<irr::s32>& region, int blendTol) {
        BlendResult r;
        for (irr::s32 y = region.UpperLeftCorner.Y; y < region.LowerRightCorner.Y; ++y) {
            for (irr::s32 x = region.UpperLeftCorner.X; x < region.LowerRightCorner.X; ++x) {
                const int d = diffInsidePixel(a, b, x, y);
                r.maxDiff = std::max(r.maxDiff, d);
                if (d > blendTol) ++r.over;
            }
        }
        return r;
    }
};

// review/B899: the Backpack's label "Backpack" and its "4" / "12" counts under
// the Forge, which shares its rect.
TEST_F(UiPanelZOrderTest, ALowerBagsTextIsCoveredByTheBagOpenedOnTopOfIt) {
    ContainerWindowsState both;
    openBackpack(both);
    openForge(both);
    ContainerWindowsState forgeOnly;
    openForge(forgeOnly);
    expectLowerTextCovered(
        "backpack under forge",
        [&] { renderContainerWindows(ui_.layer(), layout_, both); },
        [&] { renderContainerWindows(ui_.layer(), layout_, forgeOnly); },
        ContainerLayout::windowRect(layout_));
}

// review/B442b: the purse counts through the confirmation dialog.
TEST_F(UiPanelZOrderTest, ThePurseCountsAreCoveredByTheConfirmationDialog) {
    inv_.activePopup = PopupType::Inventory;
    confirm_.open(1, "ARE YOU SURE you wish to drop your Rusty Long Sword on the ground?");
    // B1075 (#395): the layout table moved the inventory to the right half of
    // the side-by-side pair, out from under the centred dialog at 800x600.
    // B1078 (GitLab #398): the inventory's window anchors at that slot's LEFT
    // (428x460 at one uniform factor), so it only reaches the centred dialog
    // when the slot sits left enough: window {280,48,595,429} carries the
    // coin column (x 519..551) under the dialog's right part (263..537).
    // This case is about TEXT LAYERING, not placement.
    UILayout stacked = layout_;
    stacked.inventoryPopup = {280, 125, 680, 475};
    expectLowerTextCovered(
        "inventory under confirmation",
        [&] {
            renderInventoryPopup(ui_.layer(), stacked, inv_);
            renderConfirmationDialog(ui_.layer(), stacked, confirm_);
        },
        [&] { renderConfirmationDialog(ui_.layer(), stacked, confirm_); },
        ConfirmationLayout::windowRect(stacked));
}

// All three of ui_panel_capture's containers open, the Forge newest: the two
// covered windows' text (the "Backpack" / "Sewing Kit" labels, the "4", "12"
// and "2" counts) under it. (B442b also named the QuantityWnd over the
// inventory; at 800x600 the two do not overlap -- review/B992.)
TEST_F(UiPanelZOrderTest, TwoCoveredBagsTextIsCoveredByTheNewestBag) {
    ContainerWindowsState three;
    openBackpack(three);
    openSewingKit(three);
    openForge(three);
    ContainerWindowsState forgeOnly;
    openForge(forgeOnly);
    expectLowerTextCovered(
        "backpack and sewing kit under forge",
        [&] { renderContainerWindows(ui_.layer(), layout_, three); },
        [&] { renderContainerWindows(ui_.layer(), layout_, forgeOnly); },
        ContainerLayout::windowRect(layout_));
}

// The overlap of two rects, or an empty rect when they do not meet.
irr::core::rect<irr::s32> overlapRect(const irr::core::rect<irr::s32>& a,
                                      const irr::core::rect<irr::s32>& b) {
    const irr::s32 x0 = std::max(a.UpperLeftCorner.X, b.UpperLeftCorner.X);
    const irr::s32 y0 = std::max(a.UpperLeftCorner.Y, b.UpperLeftCorner.Y);
    const irr::s32 x1 = std::min(a.LowerRightCorner.X, b.LowerRightCorner.X);
    const irr::s32 y1 = std::min(a.LowerRightCorner.Y, b.LowerRightCorner.Y);
    return x0 < x1 && y0 < y1 ? irr::core::rect<irr::s32>(x0, y0, x1, y1)
                              : irr::core::rect<irr::s32>(0, 0, 0, 0);
}

// B1002 (#322): the translucent-over-opaque case, named in the issue's
// re-basing of B992's pixel test. The TRANSLUCENT window is the buff bar
// (EQUI_BuffWindow, one of the 34); the OPAQUE one under it is a bag
// (EQUI_Container, one of the 86). The buff bar's own panel is a uniform
// fill, so its content (the buff slots) is identical in the upper-alone and
// upper-over-lower frames, and inside the overlap only the bag's content
// bleeding through the buff panel can differ. Two legs:
//
// 1. the MEASURED blend alpha (255, window_flags.h): the translucent window
//    hides the lower window's content exactly like an opaque one -- ABSENT
//    in the region it covers (the issue's re-based assertion: the lower
//    window's text is absent, not dimmed to the panel's own passes).
// 2. a GENUINELY translucent blend (180, through translucentPanelAlpha
//    Override): the lower window's content is PRESENT at the measured blend.
//    The panel lets (255-180)/255 = 30% of what is under it through, so a
//    bright lower pixel (the bag's slot art, ~255) over the dark panel
//    (15) is 0.30*240 = 72 above the opaque frame; the tolerance band
//    50..150 brackets 30% with headroom for the upper content's own
//    translucency.
//
// The negative control: the bag alone must show bright pixels inside the
// overlap (its slot art / contents), or the geometry cannot detect the bleed
// and the case fails loudly.
TEST_F(UiPanelZOrderTest, TranslucentBuffBarOverTheOpaqueBag) {
    ContainerWindowsState bags;
    openBackpack(bags);
    openForge(bags);
    // Buff slots make the buff bar's own content non-empty (and identical in
    // both compared frames).
    buffs_.buffs[0].spellId = 288;
    buffs_.buffs[1].spellId = 289;

    const irr::core::rect<irr::s32> bag = ContainerLayout::windowRect(layout_);
    UILayout moved = layout_;
    // Position the 300x40 buff bar over the bag's FIRST slot row (bag-space
    // y 84-124, ContainerLayout::SLOT_Y[0]=84) so the overlap carries the bag's
    // bright slot art. The bag is 100 wide; the overlap is that width.
    moved.buffBar =
        overlapRect(bag,
                    moved.clampOnScreen(
                        bag.UpperLeftCorner.X, bag.UpperLeftCorner.Y + 84,
                        300, 40));
    const irr::core::rect<irr::s32> region = overlapRect(moved.buffBar, bag);
    ASSERT_GT(region.getArea(), 0)
        << "the relocated buff bar must overlap the bag";

    const auto upperAlone = [&] { renderBuffBar(ui_.layer(), moved, buffs_); };
    const auto buffOverBag = [&] {
        renderContainerWindows(ui_.layer(), layout_, bags);
        renderBuffBar(ui_.layer(), moved, buffs_);
    };

    // Negative control: the bag's content is visible in the overlap.
    irr::video::IImage* lower = renderFrames(
        [&] { renderContainerWindows(ui_.layer(), layout_, bags); });
    ASSERT_NE(lower, nullptr);
    int bright = 0;
    for (irr::s32 y = region.UpperLeftCorner.Y; y < region.LowerRightCorner.Y; ++y)
        for (irr::s32 x = region.UpperLeftCorner.X; x < region.LowerRightCorner.X; ++x) {
            const irr::video::SColor p = lower->getPixel(x, y);
            bright += (int(p.getRed() + p.getGreen() + p.getBlue()) / 3 > 96);
        }
    lower->drop();
    std::printf("B1002 buff-over-bag: region %dx%d, bag pixels over 96: %d\n",
                region.getWidth(), region.getHeight(), bright);
    ASSERT_GT(bright, 20)
        << "the bag must show content inside the overlap (only "
        << bright << " bright pixels) -- the case cannot detect the bleed";

    // The upper-alone frame: both compared frames show the buff bar's own
    // content, so inside the overlap only the BLEED of the bag's content
    // through the buff panel can differ between them.
    irr::video::IImage* alone = renderFrames(upperAlone);
    ASSERT_NE(alone, nullptr);

    // Leg 1: the measured blend alpha (no override = kTranslucentPanelAlpha
    // = 255): the lower window's content is ABSENT in the region it covers.
    irr::video::IImage* opaque = renderFrames(buffOverBag);
    ASSERT_NE(opaque, nullptr);
    {
        int maxDiff = 0;
        size_t over = 0;
        diffInside(alone, opaque, region, &maxDiff, &over);
        std::printf("B1002 measured (%d) translucent-over-opaque: maxDiff=%d "
                    "over(>%d)=%zu\n",
                    int(kTranslucentPanelAlpha), maxDiff, kMaxThrough, over);
        EXPECT_EQ(over, 0u)
            << "at the measured blend alpha the translucent window fully hides "
               "the lower window's content in the region it covers (max diff "
            << maxDiff << ")";
        // The residual diff is the UPPER window's OWN content: its buff
        // decal (an ETC1 alpha-companion sprite, not baked 255) blends the
        // backdrop under its translucent pixels -- the bag's dark slot
        // background (alone: black). No lower TEXT/ART crosses the panel:
        // `over` above is 0.
        EXPECT_LE(maxDiff, 80)
            << "a 255-alpha panel hides the lower window; the only residual "
               "is the upper window's own (partly transparent) content (max "
               "diff "
            << maxDiff << ")";
    }

    // Leg 2: a genuinely translucent 180: the lower window's content is
    // PRESENT at the measured blend (30% through). A bright lower pixel
    // (bag slot art, ~255) over the dark panel (15) is 0.30*240 = 72 above
    // the opaque frame; the tolerance band 50..150 brackets that with
    // headroom for the upper content's own translucency on top.
    translucentPanelAlphaOverride = 180;
    irr::video::IImage* blended = renderFrames(buffOverBag);
    translucentPanelAlphaOverride = 0;
    ASSERT_NE(blended, nullptr);
    const BlendResult r = measureBlend(alone, blended, region, 50);
    std::printf("B1002 translucent 180 over opaque: maxDiff=%d over(>50)=%zu\n",
                r.maxDiff, r.over);
    EXPECT_GE(r.maxDiff, 50)
        << "a 180-alpha translucent panel lets ~30% of the lower content "
           "through; the blend must be visible (max diff "
        << r.maxDiff << ")";
    EXPECT_LE(r.maxDiff, 150)
        << "a 180-alpha panel lets ~30% through, never ~60% or more "
           "(max diff "
        << r.maxDiff << ") -- the panel is missing or the blend is wrong";
    EXPECT_GE(r.over, 20u)
        << "the lower window's content must be PRESENT at the blend across "
           "the overlap region (only "
        << r.over << " pixels over the 50 tolerance)";
    alone->drop();
    opaque->drop();
    blended->drop();
}

// Layering must not move a pixel where nothing overlaps: the HUD alone, every
// panel its own layer, paints exactly what the one-layer flush paints.
TEST_F(UiPanelZOrderTest, LayeringMovesNoPixelWhereNoWindowCoversText) {
    irr::video::IImage* layered = renderFrames([&] { submitStaticUI(); });
    ui_.setLayeringEnabled(false);
    irr::video::IImage* single = renderFrames([&] { submitStaticUI(); });
    ui_.setLayeringEnabled(true);
    ASSERT_NE(layered, nullptr);
    ASSERT_NE(single, nullptr);
    int maxDiff = 0;
    size_t over = 0;
    diffInside(layered, single, {0, 0, static_cast<irr::s32>(kW), static_cast<irr::s32>(kH)},
               &maxDiff, &over);
    EXPECT_EQ(maxDiff, 0) << "the HUD-only frame changed under layering";
    layered->drop();
    single->drop();
}

// ===========================================================================
// The cost. B975 batched the name tags into two draws for a reason: on the
// Orange Pi a draw call is expensive. Each scenario is flushed with layering
// off (the pre-B992 flush: one group) and on, and the driver draw calls
// (quads + text) are printed and pinned. A layered frame may cost more only
// by the groups a covering window really opens.
// ===========================================================================
TEST_F(UiPanelZOrderTest, TheLayeredFlushCostsDrawsOnlyWhereAWindowCoversText) {
    // Pins: driver draw calls (quads + text) of the pre-B992 flush and of the
    // layered one, the layers and the flush groups. Draw COUNTS do not depend
    // on the GPU; they are what the Orange Pi issues for the same frame.
    struct Pins { size_t preDraws, draws, layers, groups; };
    struct Row {
        const char* name;
        std::function<void()> setup;
        Pins atlas;     // --eqe1-path given: the UI atlas
        Pins fallback;  // no atlas: Will's Orange Pi recipe (T462, board B540)
    };
    auto reset = [&] {
        inv_.activePopup = PopupType::None;
        containers_ = ContainerWindowsState{};
        quantity_ = QuantityWindowState{};
        confirm_ = ConfirmationDialogState{};
        cursor_ = CursorAttachmentState{};
    };
    auto threeBags = [&] {
        openBackpack(containers_);
        openSewingKit(containers_);
        openForge(containers_);
    };
    const std::vector<Row> rows = {
        // B1025-H2: the fallback (no-atlas, Will's Pi recipe) pins re-taken
        // for the stock HotButtonWnd -- its page row and ten per-slot frames
        // are solid draws there. B1102 removed the close box, lowering every
        // fallback draw pin by one; the atlas draw pins are unchanged because
        // the remaining quads still share its texture runs.
        // B1077 adds the always-on gem bar to every fixture frame: +4 draws
        // on the atlas path, +3 on fallback; the B1078 band-scaled inventory
        // keeps its one-group flush and bag-covering group counts below.
        // Re-taken on the merged 800x600 layout (chat=120).
        // B1002 (#322): the atlas pins are UNCHANGED (a panel fill is one
        // textured atlas quad regardless of its tint alpha). The FALLBACK
        // pins drop: drawPanelFor fills the body at alpha 255 (the measured
        // blend alpha; all 29 fable windows are opaque) instead of the shared
        // panel_background baked at 180, so the body and the four border
        // lines are now ONE opaque solid run at the flush instead of a
        // 180-blend run plus an opaque run. B1102 removed the hotbar close
        // box (one fewer fallback draw per window); the atlas draw pins are
        // unchanged because the remaining quads still share its texture runs.
        {"hud", [] {}, {8, 8, 6, 1}, {55, 55, 6, 1}},
        // B1078 (GitLab #398): the inventory window moved to the band-anchored
        // 428x460 (one uniform factor), so its own flush is one group now
        // (was two) and every bag-covering scenario lost 1 group.
        {"inventory", [&] { inv_.activePopup = PopupType::Inventory; }, {35, 35, 7, 1}, {75, 75, 7, 1}},
        // What B899's workaround drew for three open bags: the topmost only.
        // The Forge (350,100) also covers inventory text, so even one bag is a
        // real split (the shipped bleed was there too).
        {"inv+forge", [&] {
             inv_.activePopup = PopupType::Inventory;
             openForge(containers_);
        }, {38, 42, 8, 2}, {94, 96, 8, 2}},
        {"inv+3 bags", [&] {
             inv_.activePopup = PopupType::Inventory;
             threeBags();
         }, {51, 63, 10, 4}, {111, 117, 10, 4}},
        {"full", [&] {
             inv_.activePopup = PopupType::Inventory;
             threeBags();
             quantity_.openForStack(25, "Arrow", 20);
             confirm_.open(1, "ARE YOU SURE you wish to drop your Rusty Long Sword on the ground?");
             cursor_.hasItem = true;
             cursor_.itemName = "Rusty Long Sword";
             cursor_.iconId = 1001;
             cursor_.mouseX = 560;
             cursor_.mouseY = 420;
         }, {52, 64, 13, 4}, {127, 136, 13, 5}},
    };

    for (bool fallback : {false, true}) {
    ui_.init(driver_, fallback ? nullptr : atlas_.get(), &textBatch_);
    std::printf("B992 DRAW CALLS (x86 llvmpipe GLES2, 800x600, %s path, chat=120)\n",
                fallback ? "fallback (no atlas)" : "atlas");
    std::printf("%-12s %6s %6s | %9s %9s %9s | %9s %9s %9s %6s %6s | %9s\n", "scenario", "cmds",
                "texts", "pre quad", "pre text", "pre all", "quad", "text", "all", "layers",
                "groups", "perWindow");
    for (const Row& row : rows) {
        const Pins& pin = fallback ? row.fallback : row.atlas;
        reset();
        row.setup();
        UIRenderer::FlushStats pre, post;
        ui_.setLayeringEnabled(false);
        irr::video::IImage* a = renderFrames([&] { submitStaticUI(); }, &pre);
        ui_.setLayeringEnabled(true);
        irr::video::IImage* b = renderFrames([&] { submitStaticUI(); }, &post);
        if (a) a->drop();
        if (b) b->drop();
        const size_t perWindow = perWindowFlushDraws();
        const size_t preAll = pre.driverDraws + pre.textDraws;
        const size_t all = post.driverDraws + post.textDraws;
        std::printf("%-12s %6zu %6zu | %9zu %9zu %9zu | %9zu %9zu %9zu %6zu %6zu | %9zu\n",
                    row.name, post.commands, post.textEntries, pre.driverDraws, pre.textDraws,
                    preAll, post.driverDraws, post.textDraws, all, post.layers,
                    post.layerGroups, perWindow);
        EXPECT_LE(all, perWindow) << row.name << ": merging must never cost more than "
                                                 "flushing every window alone";
        EXPECT_EQ(pre.commands, post.commands) << row.name;
        EXPECT_EQ(pre.textEntries, post.textEntries) << row.name;
        EXPECT_EQ(pre.layerGroups, 1u) << row.name << ": layering off is one group";
        EXPECT_EQ(preAll, pin.preDraws) << row.name;
        EXPECT_EQ(all, pin.draws) << row.name;
        EXPECT_EQ(post.layers, pin.layers) << row.name;
        EXPECT_EQ(post.layerGroups, pin.groups) << row.name;
    }
    }
}
