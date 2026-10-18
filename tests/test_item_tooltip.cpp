// B1004 (GitLab #323): item-name tooltips.
//
// The rule is the official client's, MEASURED on the local oracle lane:
// review/UIGAP-ORACLE/323/tooltips.tsv (rows cited per case). Holding ALT with
// the pointer over a bag or the bank labels EVERY occupied slot of that window
// with the item's name; the inventory's own slots never get labels; with the
// bank open, Alt over the inventory shows the BANK's labels; a hold, not a
// toggle; name only, no stack count. The hover tooltip is the client's own
// documented rule (help/tips/ctxbank.html, row 36), on the static-tooltip
// dwell and placement measured on the same rig (row 35), and is in by Will's
// 2026-10-06 ruling.
//
// The logic cases drive the REAL panel appliers (bank, container) and the real
// layout headers through updateItemTooltips() with no device. The pixel cases
// draw through a real headless GLES2 device and the real UIAtlas
// (ui_panel_capture's harness) and self-skip without them.

#include <gtest/gtest.h>

#include "client/graphics/ui/item_tooltip.h"
#include "client/graphics/ui/bank_layout.h"
#include "client/graphics/ui/bank_panel.h"
#include "client/graphics/ui/container_layout.h"
#include "client/graphics/ui/container_panel.h"
#include "client/graphics/ui/inventory_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/trade_layout.h"
#include "client/graphics/ui/trade_panel.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/text_batch.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/inventory/inventory_constants.h"
#include "client/state/event_bus.h"

#include <irrlicht.h>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

using namespace EQT::Graphics;
namespace inv = eqt::inventory;

namespace {

constexpr irr::s32 kLineH = 14;

// 7 px a character: the device-free stand-in for TextBatch::getTextWidth().
irr::s32 fixedWidth(const std::string& s, const void*) {
    return static_cast<irr::s32>(s.size()) * 7;
}

irr::core::vector2di centre(const irr::core::rect<irr::s32>& r) {
    return {(r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
            (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2};
}

bool inside(const irr::core::rect<irr::s32>& r, int x, int y) {
    return x >= r.UpperLeftCorner.X && x < r.LowerRightCorner.X &&
           y >= r.UpperLeftCorner.Y && y < r.LowerRightCorner.Y;
}

class ItemTooltipTest : public ::testing::Test {
protected:
    UILayout layout_;
    InventoryPanelState inv_;
    BankPanelState bank_;
    ContainerWindowsState bags_;
    TradePanelState trade_;
    ItemTooltipState state_;

    void SetUp() override { layout_.computeLayout(800, 600); }

    ItemTooltipScene scene(int x, int y, bool alt) const {
        ItemTooltipScene s;
        s.layout = &layout_;
        s.inventory = &inv_;
        s.bank = &bank_;
        s.containers = &bags_;
        s.trade = &trade_;
        s.mouseX = x;
        s.mouseY = y;
        s.altHeld = alt;
        return s;
    }
    ItemTooltipFrame frame(int x, int y, bool alt, uint64_t nowMs) {
        return updateItemTooltips(state_, scene(x, y, alt), nowMs, &fixedWidth, nullptr, kLineH);
    }

    void openInventory() {
        inv_.activePopup = PopupType::Inventory;
        // General slot 0 (flat 22): a Backpack; general 1: 20 Arrows; general 2 empty.
        inv_.generalSlots[0].hasItem = true;
        inv_.generalSlots[0].itemName = "Backpack";
        inv_.generalSlots[1].hasItem = true;
        inv_.generalSlots[1].itemName = "Arrow";
        inv_.generalSlots[1].quantity = 20;
    }
    // The ledger's bag: Rusty Dagger in cell 0, Skinning Knife in cell 1,
    // 3 Low Quality Wolf Skin in cell 3 (rows 16-19), through the real appliers.
    void openBackpack() {
        eqt::state::TradeskillContainerOpenedEvent e;
        e.isWorldObject = false;
        e.objectId = 0;
        e.inventorySlot = inv::GENERAL_BEGIN;
        e.containerName = "Backpack";
        e.containerType = inv::CONTAINER_BACKPACK;
        e.slotCount = 8;
        e.iconId = 565;
        applyContainerOpened(bags_, e);
        cell(0, "Rusty Dagger", 1);
        cell(1, "Skinning Knife", 1);
        cell(3, "Low Quality Wolf Skin", 3);
    }
    void cell(int i, const char* name, uint32_t qty) {
        eqt::state::InventorySlotChangedData d;
        d.slotId = inv::calcBagSlotId(inv::GENERAL_BEGIN, i);
        d.hasItem = true;
        d.itemId = 1000u + static_cast<uint32_t>(i);
        d.itemName = name;
        d.iconId = 600;
        d.quantity = qty;
        applyContainerSlotChanged(bags_, d);
    }
    // The ledger's bank: 5 Bottles in slot 0, 8 Pods of Water in slot 1 (row 28).
    void openBank() {
        eqt::state::WindowOpenedData w;
        w.npcId = 7;
        w.npcName = "Kendall Copperhold";
        w.sellRate = 1.0f;
        applyBankWindowOpened(bank_, w);
        bankCell(0, "Bottle", 5);
        bankCell(1, "Pod of Water", 8);
    }
    void bankCell(int i, const char* name, uint32_t qty) {
        eqt::state::InventorySlotChangedData d;
        d.slotId = static_cast<int16_t>(inv::BANK_BEGIN + i);
        d.hasItem = true;
        d.itemId = 2000u + static_cast<uint32_t>(i);
        d.itemName = name;
        d.iconId = 700;
        d.quantity = qty;
        ASSERT_TRUE(applyBankSlotChanged(bank_, d));
    }
    // A point inside the inventory popup, on general slot `g`, that no bag or
    // bank window covers (the bank is centred over the popup).
    irr::core::vector2di inventoryPoint(int g) const {
        const auto r = InventoryLayout::slotRect(layout_, InventoryLayout::EQUIP_COUNT + g);
        return centre(r);
    }
    irr::core::vector2di bagBodyPoint() const {
        // Row 15/16's pointer: the window's own label area, on no slot.
        return centre(ContainerLayout::labelRect(layout_));
    }
};

}  // namespace

// ---- hover tooltip ----------------------------------------------------------

TEST_F(ItemTooltipTest, HoverShowsTheNameAfterTheMeasuredDwell) {
    openInventory();
    const auto p = inventoryPoint(1);   // 20 Arrows
    EXPECT_FALSE(frame(p.X, p.Y, false, 10000).hoverShown);
    EXPECT_FALSE(frame(p.X, p.Y, false, 10000 + ItemTooltip::kHoverDelayMs - 1).hoverShown);
    const ItemTooltipFrame f = frame(p.X, p.Y, false, 10000 + ItemTooltip::kHoverDelayMs);
    ASSERT_TRUE(f.hoverShown);
    // Name only, never the stack count (rows 17, 28).
    EXPECT_EQ(f.hover.text, "Arrow");
    EXPECT_TRUE(f.altLabels.empty());
}

TEST_F(ItemTooltipTest, HoverBoxBottomSitsOnThePointerAndStaysOnScreen) {
    openInventory();
    const auto p = inventoryPoint(0);
    frame(p.X, p.Y, false, 0);
    const ItemTooltipFrame f = frame(p.X, p.Y, false, 5000);
    ASSERT_TRUE(f.hoverShown);
    // Row 35: the static tooltip's bottom edge is the pointer's y.
    EXPECT_EQ(f.hover.rect.LowerRightCorner.Y, p.Y);
    EXPECT_EQ(f.hover.rect.UpperLeftCorner.X, p.X);
    EXPECT_EQ(f.hover.rect.getWidth(), fixedWidth("Backpack", nullptr) + 2 * ItemTooltip::kPadX);
    EXPECT_EQ(f.hover.rect.getHeight(), kLineH + 2 * ItemTooltip::kPadY);
    // Clamped to the screen at the right edge (every measured static tooltip was).
    const auto r = hoverTooltipRect(795, 300, 120, kLineH, 800, 600);
    EXPECT_EQ(r.LowerRightCorner.X, 800);
    EXPECT_EQ(r.LowerRightCorner.Y, 300);
    const auto top = hoverTooltipRect(100, 5, 50, kLineH, 800, 600);
    EXPECT_EQ(top.UpperLeftCorner.Y, 0);
}

TEST_F(ItemTooltipTest, MovingToAnotherSlotRestartsTheDwell) {
    openInventory();
    const auto a = inventoryPoint(0);
    const auto b = inventoryPoint(1);
    frame(a.X, a.Y, false, 0);
    EXPECT_TRUE(frame(a.X, a.Y, false, 700).hoverShown);
    EXPECT_FALSE(frame(b.X, b.Y, false, 710).hoverShown);
    EXPECT_FALSE(frame(b.X, b.Y, false, 1300).hoverShown);
    EXPECT_TRUE(frame(b.X, b.Y, false, 1310).hoverShown);
}

TEST_F(ItemTooltipTest, NegativeEmptySlotNeverShowsATooltip) {
    openInventory();
    const auto p = inventoryPoint(2);   // empty
    frame(p.X, p.Y, false, 0);
    const ItemTooltipFrame f = frame(p.X, p.Y, false, 5000);
    EXPECT_FALSE(f.hoverShown);
    EXPECT_TRUE(f.altLabels.empty());
}

TEST_F(ItemTooltipTest, NegativeClosedInventoryAndWindowsAboveShowNothing) {
    openInventory();
    const auto p = inventoryPoint(0);
    inv_.activePopup = PopupType::None;
    frame(p.X, p.Y, false, 0);
    EXPECT_FALSE(frame(p.X, p.Y, false, 5000).hoverShown);
    // The merchant shares the centre popup and shows no tooltip (row 32).
    inv_.activePopup = PopupType::Vendor;
    EXPECT_FALSE(frame(p.X, p.Y, false, 9000).hoverShown);
    // A window drawn above every item window (item display, Options, ...).
    inv_.activePopup = PopupType::Inventory;
    ItemTooltipScene s = scene(p.X, p.Y, false);
    s.blockedAbove = true;
    updateItemTooltips(state_, s, 10000, &fixedWidth, nullptr, kLineH);
    EXPECT_FALSE(updateItemTooltips(state_, s, 20000, &fixedWidth, nullptr, kLineH).hoverShown);
}

TEST_F(ItemTooltipTest, HoverWorksInEveryItemWindow) {
    openBackpack();
    // Bag cell 0. (B1075: the bank and the trade window sit beside the
    // inventory now, over the bags' oracle (350,100) at 800x600, so each
    // opens just before its own check.)
    auto p = centre(ContainerLayout::slotRect(layout_, 0));
    frame(p.X, p.Y, false, 0);
    ItemTooltipFrame f = frame(p.X, p.Y, false, 1000);
    ASSERT_TRUE(f.hoverShown);
    EXPECT_EQ(f.hover.text, "Rusty Dagger");
    // Bank slot 1 (fable centres the bank, under the bags' shared rect, so
    // the bag is closed for this one).
    bags_ = ContainerWindowsState{};
    openBank();
    p = centre(BankLayout::slotRect(layout_, 1));
    frame(p.X, p.Y, false, 2000);
    f = frame(p.X, p.Y, false, 3000);
    ASSERT_TRUE(f.hoverShown);
    EXPECT_EQ(f.hover.text, "Pod of Water");
    // Our own trade slot 0 (the trade window draws above the bag and bank).
    trade_.isOpen = true;
    trade_.own[0].hasItem = true;
    trade_.own[0].itemName = "Bottle";
    const TooltipTarget t = resolveTooltipTarget(scene(0, 0, false));
    EXPECT_EQ(t.window, TooltipWindow::None);
    const auto tw = TradeLayout::windowRect(layout_);  // B1075: PairLeft
    for (int y = tw.UpperLeftCorner.Y; y < tw.LowerRightCorner.Y; y += 2) {
        for (int x = tw.UpperLeftCorner.X; x < tw.LowerRightCorner.X; x += 2) {
            const TradeHit h = hitTestTradeWindow(layout_, trade_, x, y);
            if (h.part == TradePart::OwnSlot && h.index == 0) {
                frame(x, y, false, 4000);
                f = frame(x, y, false, 5000);
                ASSERT_TRUE(f.hoverShown);
                EXPECT_EQ(f.hover.text, "Bottle");
                return;
            }
        }
    }
    FAIL() << "no point on trade slot 0";
}

// ---- the Alt rule -----------------------------------------------------------

TEST_F(ItemTooltipTest, AltOverABagLabelsEveryOccupiedSlotAtOnce) {
    openBackpack();
    const auto p = bagBodyPoint();
    const ItemTooltipFrame f = frame(p.X, p.Y, true, 100);
    ASSERT_EQ(f.altLabels.size(), 3u);   // rows 16-17: all three, the pointer on none
    EXPECT_FALSE(f.hoverShown);
    EXPECT_EQ(f.altLabels[0].text, "Rusty Dagger");
    EXPECT_EQ(f.altLabels[1].text, "Skinning Knife");
    EXPECT_EQ(f.altLabels[2].text, "Low Quality Wolf Skin");   // no "3"
    // Rows 19-21: left column at the slot's (+35,+1), right column (+35,+21).
    const auto s0 = ContainerLayout::slotRect(layout_, 0);
    const auto s1 = ContainerLayout::slotRect(layout_, 1);
    const auto s3 = ContainerLayout::slotRect(layout_, 3);
    EXPECT_EQ(f.altLabels[0].rect.UpperLeftCorner,
              irr::core::vector2di(s0.UpperLeftCorner.X + 35, s0.UpperLeftCorner.Y + 1));
    EXPECT_EQ(f.altLabels[1].rect.UpperLeftCorner,
              irr::core::vector2di(s1.UpperLeftCorner.X + 35, s1.UpperLeftCorner.Y + 21));
    EXPECT_EQ(f.altLabels[2].rect.UpperLeftCorner,
              irr::core::vector2di(s3.UpperLeftCorner.X + 35, s3.UpperLeftCorner.Y + 21));
    // The staggered labels of one row do not overlap (the reason for it).
    EXPECT_FALSE(f.altLabels[0].rect.isRectCollided(f.altLabels[1].rect));
}

TEST_F(ItemTooltipTest, AltIsAHoldNotAToggle) {
    openBackpack();
    const auto p = bagBodyPoint();
    EXPECT_EQ(frame(p.X, p.Y, true, 0).altLabels.size(), 3u);
    // Release: gone on the next frame (row 14).
    EXPECT_TRUE(frame(p.X, p.Y, false, 16).altLabels.empty());
    // A tap leaves nothing behind (row 12).
    frame(p.X, p.Y, true, 32);
    EXPECT_TRUE(frame(p.X, p.Y, false, 48).altLabels.empty());
    EXPECT_TRUE(frame(p.X, p.Y, false, 5000).altLabels.empty());
}

TEST_F(ItemTooltipTest, AltLabelsFollowThePointerOutOfAndBackIntoTheBag) {
    openBackpack();
    openInventory();
    const auto bag = bagBodyPoint();
    // Find an inventory slot the bag window does not cover.
    irr::core::vector2di invPt(-1, -1);
    for (int g = 0; g < InventoryLayout::GENERAL_COUNT && invPt.X < 0; ++g) {
        const auto c = inventoryPoint(g);
        if (!inside(ContainerLayout::windowRect(layout_), c.X, c.Y)) invPt = c;
    }
    ASSERT_GE(invPt.X, 0);
    EXPECT_EQ(frame(bag.X, bag.Y, true, 0).altLabels.size(), 3u);
    // Rows 18, 23: onto the inventory -> none (its slots never get labels).
    EXPECT_TRUE(frame(invPt.X, invPt.Y, true, 16).altLabels.empty());
    // Row 24: out to the world -> none.
    EXPECT_TRUE(frame(790, 590, true, 32).altLabels.empty());
    // Back over the bag -> back (row 23).
    EXPECT_EQ(frame(bag.X, bag.Y, true, 48).altLabels.size(), 3u);
}

TEST_F(ItemTooltipTest, NegativeAltWithNoPanelUnderThePointer) {
    openBackpack();
    openInventory();
    const ItemTooltipFrame f = frame(790, 590, true, 100);   // rows 9, 18, 31
    EXPECT_TRUE(f.altLabels.empty());
    EXPECT_FALSE(f.hoverShown);
}

TEST_F(ItemTooltipTest, NegativeAltOverTheInventoryLabelsNothing) {
    openInventory();
    const auto p = inventoryPoint(0);
    frame(p.X, p.Y, true, 0);
    const ItemTooltipFrame f = frame(p.X, p.Y, true, 5000);   // rows 10-11
    EXPECT_TRUE(f.altLabels.empty());
}

TEST_F(ItemTooltipTest, AltOverTheBankLabelsEveryBankItem) {
    openBank();
    // Row 28: the pointer on the bank's title, on no slot.
    const auto t = centre(BankLayout::bankerNameRect(layout_));
    const ItemTooltipFrame f = frame(t.X, t.Y, true, 100);
    ASSERT_EQ(f.altLabels.size(), 2u);
    EXPECT_EQ(f.altLabels[0].text, "Bottle");        // 5 of them: name only
    EXPECT_EQ(f.altLabels[1].text, "Pod of Water");
    const auto s0 = BankLayout::slotRect(layout_, 0);
    EXPECT_EQ(f.altLabels[0].rect.UpperLeftCorner,
              irr::core::vector2di(s0.UpperLeftCorner.X + 35, s0.UpperLeftCorner.Y + 1));
    // An item in the right column is staggered like the bag's.
    bankCell(5, "Fishing Pole", 1);
    const ItemTooltipFrame g = frame(t.X, t.Y, true, 200);
    ASSERT_EQ(g.altLabels.size(), 3u);
    const auto s5 = BankLayout::slotRect(layout_, 5);
    EXPECT_EQ(g.altLabels[2].rect.UpperLeftCorner,
              irr::core::vector2di(s5.UpperLeftCorner.X + 35, s5.UpperLeftCorner.Y + 21));
}

TEST_F(ItemTooltipTest, AltOverTheInventoryWithTheBankOpenShowsTheBankLabels) {
    openInventory();
    openBank();
    // Row 30 (4/4 trials): the pointer on the inventory, outside the bank.
    irr::core::vector2di p(-1, -1);
    for (int i = 0; i < InventoryLayout::TOTAL_SLOTS && p.X < 0; ++i) {
        const auto c = centre(InventoryLayout::slotRect(layout_, i));
        if (!bankContains(layout_, bank_, c.X, c.Y)) p = c;
    }
    ASSERT_GE(p.X, 0);
    ASSERT_EQ(resolveTooltipTarget(scene(p.X, p.Y, true)).window, TooltipWindow::Inventory);
    const ItemTooltipFrame f = frame(p.X, p.Y, true, 100);
    ASSERT_EQ(f.altLabels.size(), 2u);
    EXPECT_EQ(f.altLabels[0].text, "Bottle");
}

TEST_F(ItemTooltipTest, NegativeAltOverTheTradeWindowLabelsNothing) {
    trade_.isOpen = true;
    trade_.own[0].hasItem = true;
    trade_.own[0].itemName = "Bottle";
    const auto w = TradeLayout::windowRect(layout_);
    const auto p = centre(w);
    ASSERT_EQ(resolveTooltipTarget(scene(p.X, p.Y, true)).window, TooltipWindow::Trade);
    EXPECT_TRUE(frame(p.X, p.Y, true, 100).altLabels.empty());   // row 34: not measured
}

TEST_F(ItemTooltipTest, LabelsStayOnScreen) {
    const irr::core::rect<irr::s32> slot(780, 580, 820, 620);
    const auto r = itemLabelRect(slot, 1, 100, kLineH, 800, 600);
    EXPECT_LE(r.LowerRightCorner.X, 800);
    EXPECT_LE(r.LowerRightCorner.Y, 600);
    EXPECT_GE(r.UpperLeftCorner.X, 0);
}

// ---- pixels (ui_panel_capture's harness) ------------------------------------

namespace {
const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";
}

class ItemTooltipPixelTest : public ItemTooltipTest {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch text_;
    UIRenderer ui_;

    void SetUp() override {
        ItemTooltipTest::SetUp();
        std::ifstream probe(kWilleqEqe1);
        if (!probe.good()) GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
        if (!loader_.open(kWilleqEqe1)) GTEST_SKIP() << "Failed to open " << kWilleqEqe1;
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) GTEST_SKIP() << "no EDT_OGLES2 device (surfaceless EGL unavailable)";
        driver_ = device_->getVideoDriver();
        text_.init(device_->getGUIEnvironment());
        auto config = ConstrainedRendererConfig::fromPreset(ConstrainedRenderingPreset::TNT);
        cache_ = std::make_unique<ConstrainedTextureCache>(config);
        cache_->setDriver(driver_);
        cache_->addEQE1Loader(&loader_);
        atlas_ = std::make_unique<UIAtlas>();
        if (!atlas_->load(driver_, cache_.get())) GTEST_SKIP() << "UIAtlas::load() failed";
        ui_.init(driver_, atlas_.get(), &text_);
    }
    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) { device_->drop(); device_ = nullptr; }
    }

    static irr::s32 realWidth(const std::string& s, const void* ctx) {
        return static_cast<const TextBatch*>(ctx)->getTextWidth(s);
    }

    // renderStaticUI()'s tail: the bag window, then the tooltip layer.
    irr::video::IImage* draw(const ItemTooltipFrame& f) {
        for (int frame = 0; frame < 3; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui_.beginFrame();
            renderContainerWindows(ui_.layer(), layout_, bags_);
            renderItemTooltips(ui_.layer(), f);
            ui_.endFrame();
            driver_->endScene();
        }
        return driver_->createScreenShot();
    }

    // Pixels inside `r` within `tol` of the navy box colour.
    static size_t navyPixels(irr::video::IImage* img, const irr::core::rect<irr::s32>& r,
                             int tol = 24) {
        const irr::video::SColor want = ItemTooltip::boxColor();
        size_t n = 0;
        for (irr::s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y) {
            for (irr::s32 x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x) {
                const irr::video::SColor c = img->getPixel(x, y);
                if (std::abs(int(c.getRed()) - int(want.getRed())) <= tol &&
                    std::abs(int(c.getGreen()) - int(want.getGreen())) <= tol &&
                    std::abs(int(c.getBlue()) - int(want.getBlue())) <= tol) {
                    ++n;
                }
            }
        }
        return n;
    }
    // Bright (text) pixels inside `r`.
    static size_t whitePixels(irr::video::IImage* img, const irr::core::rect<irr::s32>& r) {
        size_t n = 0;
        for (irr::s32 y = r.UpperLeftCorner.Y; y < r.LowerRightCorner.Y; ++y) {
            for (irr::s32 x = r.UpperLeftCorner.X; x < r.LowerRightCorner.X; ++x) {
                const irr::video::SColor c = img->getPixel(x, y);
                if (c.getRed() > 200 && c.getGreen() > 200 && c.getBlue() > 200) ++n;
            }
        }
        return n;
    }
};

TEST_F(ItemTooltipPixelTest, AltLabelsArePaintedOverTheBagAndGoneWithoutAlt) {
    openBackpack();
    const auto p = bagBodyPoint();
    ItemTooltipState st;
    const ItemTooltipFrame on =
        updateItemTooltips(st, scene(p.X, p.Y, true), 100, &realWidth, &text_, text_.getLineHeight());
    ASSERT_EQ(on.altLabels.size(), 3u);
    const ItemTooltipFrame off =
        updateItemTooltips(st, scene(p.X, p.Y, false), 116, &realWidth, &text_, text_.getLineHeight());
    ASSERT_TRUE(off.altLabels.empty());

    irr::video::IImage* withAlt = draw(on);
    irr::video::IImage* without = draw(off);
    ASSERT_NE(withAlt, nullptr);
    ASSERT_NE(without, nullptr);
    for (const TooltipBox& b : on.altLabels) {
        const size_t area = static_cast<size_t>(b.rect.getArea());
        // The box: mostly navy, with the name's white glyphs on it.
        EXPECT_GT(navyPixels(withAlt, b.rect), area / 2) << b.text;
        EXPECT_GT(whitePixels(withAlt, b.rect), 10u) << b.text;
        EXPECT_LT(navyPixels(without, b.rect), area / 10) << b.text;
    }
    withAlt->drop();
    without->drop();
}

TEST_F(ItemTooltipPixelTest, HoverTooltipIsPaintedAboveTheBag) {
    openBackpack();
    const auto p = centre(ContainerLayout::slotRect(layout_, 1));
    ItemTooltipState st;
    updateItemTooltips(st, scene(p.X, p.Y, false), 0, &realWidth, &text_, text_.getLineHeight());
    const ItemTooltipFrame f = updateItemTooltips(st, scene(p.X, p.Y, false), 700, &realWidth,
                                                  &text_, text_.getLineHeight());
    ASSERT_TRUE(f.hoverShown);
    EXPECT_EQ(f.hover.text, "Skinning Knife");
    irr::video::IImage* img = draw(f);
    ASSERT_NE(img, nullptr);
    const size_t area = static_cast<size_t>(f.hover.rect.getArea());
    EXPECT_GT(navyPixels(img, f.hover.rect), area / 2);
    EXPECT_GT(whitePixels(img, f.hover.rect), 10u);
    img->drop();
}
