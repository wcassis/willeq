// T275: pixel-level tests for the ATLAS-backed UIRenderer path.
//
// Every UI acceptance capture before this unit (T54/T57/T60/T62, via
// tools/hwcaps/ui_inventory_capture) ran with `ui.init(driver, nullptr, ...)`
// -- the FALLBACK path, where drawRect() calls draw2DRectangle() directly.
// The atlas path, which is the one the real client always uses (the renderer
// loads ui_atlas out of the EQE1 packs at startup), had never had a single
// pixel asserted on it. T272 found the symptom live: nothing drawn by
// UIRenderer::drawRect() was visible in the spell-gem panel.
//
// These tests render into a real headless GLES2 device (surfaceless EGL /
// llvmpipe, the same backend the visual gate uses) with a real UIAtlas loaded
// out of willeq.eqe1, read the framebuffer back, and assert on the colour.
// They self-skip when the EQE1 asset or the GLES2 device is unavailable,
// following test_constrained_texture_cache.cpp's precedent.

#include <gtest/gtest.h>

#include "client/graphics/ui/ui_renderer.h"
#include "client/graphics/ui/ui_atlas.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/static_panels.h"
#include "client/graphics/ui/inventory_layout.h"  // T294: paperdollRect()
#include "client/graphics/text_batch.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"
#include "client/state/event_bus.h"   // T398: CurrencyChangedData

#include <irrlicht.h>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>

using namespace EQT::Graphics;

namespace {

const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

bool fileExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}

}  // namespace

class UIAtlasPixelTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    EQE1Loader loader_;
    std::unique_ptr<ConstrainedTextureCache> cache_;
    std::unique_ptr<UIAtlas> atlas_;
    TextBatch textBatch_;
    UILayout layout_;

    static constexpr irr::u32 kW = 800;
    static constexpr irr::u32 kH = 600;

    void SetUp() override {
        if (!fileExists(kWilleqEqe1)) {
            GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
        }
        if (!loader_.open(kWilleqEqe1)) {
            GTEST_SKIP() << "Failed to open " << kWilleqEqe1;
        }

        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;  // ui_inventory_capture's surfaceless-EGL idiom
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                            "unavailable in this environment) — skipping.";
        }
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
    }

    void TearDown() override {
        atlas_.reset();
        cache_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // Renders `draw` over an opaque black clear and returns the framebuffer.
    // Caller must drop() the image.
    //
    // The prime frame + the repeated content frame are load-bearing on this
    // surfaceless-EGL/llvmpipe backend, and so is the single font draw:
    //   - tools/hwcaps/ui_inventory_capture.cpp already documents that the
    //     FIRST content frame after device creation loses its immediate-mode
    //     draws from the screenshot;
    //   - T275 measured a second quirk on top of it -- a frame whose only
    //     content is 2D quads never reaches the framebuffer at all, while the
    //     identical quads DO appear as soon as one font draw joins them.
    // The live client always draws UI text, so keeping a text draw in the
    // frame reproduces the real client's frame shape rather than papering
    // over anything. Neither quirk is what this unit fixed; both are harness
    // facts, recorded so a future reader does not re-derive them.
    irr::video::IImage* renderFrame(UIRenderer& ui,
                                    const std::function<void(UIRenderer&)>& draw) {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();

        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            ui.beginFrame();
            draw(ui);
            if (ui.getTextBatch())
                ui.getTextBatch()->addText(".", 2, 2, irr::video::SColor(255, 255, 255, 255));
            ui.endFrame();
            driver_->endScene();
        }
        return driver_->createScreenShot();
    }

    irr::video::SColor renderAndSample(UIRenderer& ui, irr::s32 x, irr::s32 y,
                                       const std::function<void(UIRenderer&)>& draw) {
        irr::video::IImage* img = renderFrame(ui, draw);
        EXPECT_NE(img, nullptr);
        if (!img) return irr::video::SColor(0, 0, 0, 0);
        irr::video::SColor c = img->getPixel(static_cast<irr::u32>(x), static_cast<irr::u32>(y));
        img->drop();
        return c;
    }
};

// The core T272 finding: a solid rectangle drawn through the atlas path must
// actually appear, in the colour it was asked for.
//
// Before this unit it appeared in neither sense -- twice over. (1) The atlas
// texture was uploaded with GL_LINEAR_MIPMAP_LINEAR over a mip chain that
// stops at 4x4, i.e. mipmap-INCOMPLETE, so every atlas sample came back
// opaque black. (2) UIRenderer::endFrame() passed nullptr for draw2DImage's
// `colors` argument, discarding the per-quad tint.
TEST_F(UIAtlasPixelTest, DrawRectIsVisibleAndTintedThroughAtlasPath) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    const irr::video::SColor kGreen(255, 100, 200, 100);
    irr::video::SColor got = renderAndSample(ui, 400, 300, [&](UIRenderer& r) {
        r.drawRect(irr::core::rect<irr::s32>(300, 200, 500, 400), kGreen);
    });

    EXPECT_GT(static_cast<int>(got.getRed()) + got.getGreen() + got.getBlue(), 30)
        << "drawRect drew nothing visible through the atlas path (got "
        << (int)got.getRed() << "," << (int)got.getGreen() << "," << (int)got.getBlue() << ")";
    EXPECT_NEAR(static_cast<int>(got.getRed()), 100, 24);
    EXPECT_NEAR(static_cast<int>(got.getGreen()), 200, 24);
    EXPECT_NEAR(static_cast<int>(got.getBlue()), 100, 24);
}

// The fallback path (no atlas) has always worked; pin it so a change to the
// atlas path cannot silently regress it.
TEST_F(UIAtlasPixelTest, DrawRectIsVisibleAndTintedThroughFallbackPath) {
    UIRenderer ui;
    ui.init(driver_, nullptr, &textBatch_);

    const irr::video::SColor kGreen(255, 100, 200, 100);
    irr::video::SColor got = renderAndSample(ui, 400, 300, [&](UIRenderer& r) {
        r.drawRect(irr::core::rect<irr::s32>(300, 200, 500, 400), kGreen);
    });

    EXPECT_NEAR(static_cast<int>(got.getRed()), 100, 24);
    EXPECT_NEAR(static_cast<int>(got.getGreen()), 200, 24);
    EXPECT_NEAR(static_cast<int>(got.getBlue()), 100, 24);
}

// drawSprite() shares endFrame()'s flush, and an incomplete-mipmap atlas made
// every sprite black too -- the "black box with text on it" the whole UI
// rendered as. bar_hp is a bright green gradient.
TEST_F(UIAtlasPixelTest, DrawSpriteBarHPIsVisible) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    irr::video::SColor got = renderAndSample(ui, 400, 300, [&](UIRenderer& r) {
        r.drawSprite(irr::core::rect<irr::s32>(300, 200, 500, 400),
                     static_cast<uint8_t>(UISprite::BarHP));
    });

    EXPECT_GT(static_cast<int>(got.getGreen()), 80)
        << "bar_hp sprite drew nothing green (got "
        << (int)got.getRed() << "," << (int)got.getGreen() << "," << (int)got.getBlue() << ")";
    EXPECT_GT(static_cast<int>(got.getGreen()), static_cast<int>(got.getRed()) + 40);
}

// The exact panel T272 photographed: a Ready gem's state-coloured border.
// gemColor(1) is SColor(255, 100, 200, 100) and the border is the 1px top
// edge of the gem's 32px slot.
TEST_F(UIAtlasPixelTest, SpellGemReadyBorderIsGreen) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    SpellGemPanelState gems;
    gems.gems[0].spellId = 288;
    gems.gems[0].gemState = 1;  // Ready
    gems.gems[0].spellName = "Minor Shielding";

    const irr::s32 x = layout_.spellGems.UpperLeftCorner.X;
    const irr::s32 y = layout_.spellGems.UpperLeftCorner.Y;

    irr::video::SColor got = renderAndSample(ui, x + 16, y, [&](UIRenderer& r) {
        renderSpellGemPanel(r, layout_, gems);
    });

    EXPECT_NEAR(static_cast<int>(got.getRed()), 100, 24)
        << "gem Ready border (got " << (int)got.getRed() << "," << (int)got.getGreen()
        << "," << (int)got.getBlue() << ")";
    EXPECT_NEAR(static_cast<int>(got.getGreen()), 200, 24);
    EXPECT_NEAR(static_cast<int>(got.getBlue()), 100, 24);
}

// T272's memorize destination marker: a 2px gold (255,215,0) border on the
// picked gem. This is the marker that prompted the whole unit.
TEST_F(UIAtlasPixelTest, SpellGemMemorizeTargetMarkerIsGold) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    SpellGemPanelState gems;
    gems.memorizeTargetGem = 5;  // gem A6, the one T272 picked live

    const irr::s32 x = layout_.spellGems.UpperLeftCorner.X;
    const irr::s32 y = layout_.spellGems.UpperLeftCorner.Y +
                       5 * (UILayout::SLOT_SIZE + UILayout::MARGIN);

    irr::video::SColor got = renderAndSample(ui, x + 16, y, [&](UIRenderer& r) {
        renderSpellGemPanel(r, layout_, gems);
    });

    EXPECT_NEAR(static_cast<int>(got.getRed()), 255, 24)
        << "gold memorize marker (got " << (int)got.getRed() << "," << (int)got.getGreen()
        << "," << (int)got.getBlue() << ")";
    EXPECT_NEAR(static_cast<int>(got.getGreen()), 215, 24);
    EXPECT_LT(static_cast<int>(got.getBlue()), 40);
}

// A gem with no marker must NOT be gold -- otherwise the two tests above
// could both pass on a renderer that painted the whole panel gold.
TEST_F(UIAtlasPixelTest, SpellGemWithoutMarkerIsNotGold) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    SpellGemPanelState gems;
    gems.memorizeTargetGem = 5;

    const irr::s32 x = layout_.spellGems.UpperLeftCorner.X;
    const irr::s32 y = layout_.spellGems.UpperLeftCorner.Y +
                       2 * (UILayout::SLOT_SIZE + UILayout::MARGIN);  // gem A3

    irr::video::SColor got = renderAndSample(ui, x + 16, y, [&](UIRenderer& r) {
        renderSpellGemPanel(r, layout_, gems);
    });

    EXPECT_LT(static_cast<int>(got.getRed()), 200)
        << "unmarked gem A3 came out gold (got " << (int)got.getRed() << ","
        << (int)got.getGreen() << "," << (int)got.getBlue() << ")";
}

// ---------------------------------------------------------------------------
// T294: the inventory paperdoll (T60) vs the atlas flush.
//
// UIRenderer::drawTexture() used to draw IMMEDIATELY while every other
// UIRenderer call only ACCUMULATED into atlasQuads_. renderInventoryPopup()
// submits the popup background BEFORE the paperdoll blit, so on the atlas
// path -- the only path the real client runs -- the background was flushed in
// endFrame() ON TOP of the doll and the doll was invisible. Confirmed live
// before the fix (review/T294/before/01_inventory_open_atlas.png: the popup's
// centre is a solid slab).
//
// Every paperdoll capture in the tree until now was taken through the
// ATLAS-LESS fallback (ui.init(driver, nullptr, ...)), where drawTexture()
// and drawRect() are both immediate and the ordering is accidentally
// correct -- which is why nothing caught it.
//
// These tests stand a solid-magenta texture in for the RTT (the ordering is
// what is under test, not the character model) and assert it survives to the
// framebuffer at the exact rect renderInventoryPopup() blits it to.
class UIAtlasPaperdollTest : public UIAtlasPixelTest {
protected:
    irr::video::ITexture* makeSolidTexture(irr::video::SColor color, const char* name) {
        irr::video::IImage* img = driver_->createImage(
            irr::video::ECF_A8R8G8B8, irr::core::dimension2d<irr::u32>(304, 256));
        if (!img) return nullptr;
        img->fill(color);
        irr::video::ITexture* tex = driver_->addTexture(name, img);
        img->drop();
        return tex;
    }
};

TEST_F(UIAtlasPaperdollTest, PaperdollSurvivesTheAtlasFlush) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    const irr::video::SColor kMagenta(255, 255, 0, 255);
    irr::video::ITexture* doll = makeSolidTexture(kMagenta, "t294_doll_atlas");
    ASSERT_NE(doll, nullptr);

    InventoryPanelState inv;
    inv.activePopup = PopupType::Inventory;
    inv.paperdollTexture = doll;

    const irr::core::rect<irr::s32> r = InventoryLayout::paperdollRect(layout_);
    const irr::s32 cx = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
    const irr::s32 cy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;

    irr::video::SColor got = renderAndSample(ui, cx, cy, [&](UIRenderer& rr) {
        renderInventoryPopup(rr, layout_, inv);
    });

    // Before the fix this sampled the popup background instead: a near-black
    // slab (the panel_background atlas entry with its alpha companion
    // unbound, T279), i.e. red and blue both near zero.
    EXPECT_GT(static_cast<int>(got.getRed()), 150)
        << "paperdoll blit did not survive the atlas flush (got "
        << (int)got.getRed() << "," << (int)got.getGreen() << "," << (int)got.getBlue() << ")";
    EXPECT_GT(static_cast<int>(got.getBlue()), 150);
    EXPECT_LT(static_cast<int>(got.getGreen()), 100);
}

// The doll must not leak outside its own rect -- a "fix" that flushed the
// blit last, or stretched it over the popup, would pass the test above.
TEST_F(UIAtlasPaperdollTest, PaperdollStaysInsideItsRect) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    irr::video::ITexture* doll =
        makeSolidTexture(irr::video::SColor(255, 255, 0, 255), "t294_doll_bounds");
    ASSERT_NE(doll, nullptr);

    InventoryPanelState inv;
    inv.activePopup = PopupType::Inventory;
    inv.paperdollTexture = doll;

    const irr::core::rect<irr::s32> r = InventoryLayout::paperdollRect(layout_);
    // A point inside the popup but well left of the paperdoll rect.
    const irr::s32 x = r.UpperLeftCorner.X - 12;
    const irr::s32 y = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;
    ASSERT_GT(x, layout_.centerPopup.UpperLeftCorner.X);

    irr::video::SColor got = renderAndSample(ui, x, y, [&](UIRenderer& rr) {
        renderInventoryPopup(rr, layout_, inv);
    });

    EXPECT_FALSE(got.getRed() > 150 && got.getBlue() > 150 && got.getGreen() < 100)
        << "paperdoll bled outside paperdollRect() (got " << (int)got.getRed() << ","
        << (int)got.getGreen() << "," << (int)got.getBlue() << ")";
}

// The atlas-less fallback path was already correct; pin it so the ordered
// command list cannot regress the path the capture tool and the static-panel
// tests use.
TEST_F(UIAtlasPaperdollTest, PaperdollSurvivesOnTheFallbackPath) {
    UIRenderer ui;
    ui.init(driver_, nullptr, &textBatch_);

    irr::video::ITexture* doll =
        makeSolidTexture(irr::video::SColor(255, 255, 0, 255), "t294_doll_fallback");
    ASSERT_NE(doll, nullptr);

    InventoryPanelState inv;
    inv.activePopup = PopupType::Inventory;
    inv.paperdollTexture = doll;

    const irr::core::rect<irr::s32> r = InventoryLayout::paperdollRect(layout_);
    const irr::s32 cx = (r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2;
    const irr::s32 cy = (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2;

    irr::video::SColor got = renderAndSample(ui, cx, cy, [&](UIRenderer& rr) {
        renderInventoryPopup(rr, layout_, inv);
    });

    EXPECT_GT(static_cast<int>(got.getRed()), 150)
        << "paperdoll blit lost on the fallback path (got " << (int)got.getRed() << ","
        << (int)got.getGreen() << "," << (int)got.getBlue() << ")";
    EXPECT_GT(static_cast<int>(got.getBlue()), 150);
    EXPECT_LT(static_cast<int>(got.getGreen()), 100);
}

// ── The coin decals (T398, board B218) ──────────────────────────────────────
//
// T393 reproduced the oracle's four IW_Money decal BOXES
// (InventoryLayout::coinDecalRect()) but had nothing to put in them: fable's
// atlas was fully procedural (tools/ui_atlas_builder opened no file) and had
// no coin sprite, so the popup drew the denomination letter p/g/s/c instead.
//
// T398 gave ui_atlas_builder a --uifiles-path and lifted the oracle's own
// pixels: EQUI_Animations.xml puts A_PlatinumCoin / A_GoldCoin /
// A_SilverCoin / A_CopperCoin in ONE texture, window_pieces01.tga, as a
// contiguous 72x18 strip at (90,140). They land in row 4 of the atlas
// (y=224, 20 px pitch) and reach the client through willeq.eqe1.
//
// These assert on HUE, not on exact bytes: the atlas is ETC1-compressed with
// an alpha companion and the sprite is stretched, so the values move a
// little. The measured centre pixels of the four source cells are
// platinum (171,174,194), gold (255,240,12), silver (142,143,148),
// copper (60,21,0) -- gold is the only strongly chromatic one, copper is a
// dark red-brown, and the two greys differ mainly in blue.
//
// Pre-fix (the shipped pack without the coin entries) every one of these
// fails: UIAtlas::getSprite() returns the empty rect for an unmapped name,
// so the decal is a degenerate quad and the sample is the slot background.
class UIAtlasCoinDecalTest : public UIAtlasPixelTest {
protected:
    // Draws one coin sprite stretched over a large rect and samples its
    // centre -- the stretch maps the centre of the 18x18 cell onto the
    // sample point, so this reads the coin's own body, not its alpha edge.
    irr::video::SColor sampleCoin(UISprite sprite) {
        UIRenderer ui;
        ui.init(driver_, atlas_.get(), &textBatch_);
        return renderAndSample(ui, 400, 300, [&](UIRenderer& r) {
            r.drawSprite(irr::core::rect<irr::s32>(300, 200, 500, 400),
                         static_cast<uint8_t>(sprite));
        });
    }
};

TEST_F(UIAtlasCoinDecalTest, TheAtlasHasARectForEveryCoinDenomination) {
    const UISprite kCoins[4] = {UISprite::CoinPlatinum, UISprite::CoinGold,
                                UISprite::CoinSilver, UISprite::CoinCopper};
    for (int i = 0; i < 4; ++i) {
        const auto& r = atlas_->getSprite(kCoins[i]).srcRect;
        EXPECT_EQ(r.getWidth(), 18) << "coin " << i << " is not an 18x18 cell";
        EXPECT_EQ(r.getHeight(), 18) << "coin " << i;
        EXPECT_EQ(r.UpperLeftCorner.Y, 224) << "coin " << i << " is not in atlas row 4";
        EXPECT_EQ(r.UpperLeftCorner.X, i * 20)
            << "coin " << i << " is not at its 20 px pitch";
    }
}

TEST_F(UIAtlasCoinDecalTest, GoldIsYellow) {
    irr::video::SColor got = sampleCoin(UISprite::CoinGold);
    EXPECT_GT(static_cast<int>(got.getRed()), 120)
        << "gold coin sampled (" << (int)got.getRed() << "," << (int)got.getGreen()
        << "," << (int)got.getBlue() << ")";
    EXPECT_GT(static_cast<int>(got.getGreen()), 120);
    EXPECT_LT(static_cast<int>(got.getBlue()), 120)
        << "gold must be far more red+green than blue";
}

TEST_F(UIAtlasCoinDecalTest, CopperIsADarkRedBrown) {
    irr::video::SColor got = sampleCoin(UISprite::CoinCopper);
    EXPECT_GT(static_cast<int>(got.getRed()), static_cast<int>(got.getGreen()))
        << "copper coin sampled (" << (int)got.getRed() << "," << (int)got.getGreen()
        << "," << (int)got.getBlue() << ")";
    EXPECT_GE(static_cast<int>(got.getGreen()), static_cast<int>(got.getBlue()));
    EXPECT_GT(static_cast<int>(got.getRed()), 20) << "copper drew nothing at all";
}

// The two greys are near-neutral and bright; platinum carries the blue cast
// that separates it from silver in the oracle's art.
TEST_F(UIAtlasCoinDecalTest, PlatinumAndSilverAreBrightNeutralsAndPlatinumIsBluer) {
    irr::video::SColor plat = sampleCoin(UISprite::CoinPlatinum);
    irr::video::SColor silver = sampleCoin(UISprite::CoinSilver);

    EXPECT_GT(static_cast<int>(plat.getRed()), 100)
        << "platinum sampled (" << (int)plat.getRed() << "," << (int)plat.getGreen()
        << "," << (int)plat.getBlue() << ")";
    EXPECT_GT(static_cast<int>(silver.getRed()), 100)
        << "silver sampled (" << (int)silver.getRed() << "," << (int)silver.getGreen()
        << "," << (int)silver.getBlue() << ")";
    EXPECT_LT(std::abs(static_cast<int>(silver.getRed()) -
                       static_cast<int>(silver.getBlue())), 40)
        << "silver is a neutral grey";
    EXPECT_GT(static_cast<int>(plat.getBlue()), static_cast<int>(silver.getBlue()) - 20)
        << "platinum is the bluer of the two greys";
}

// The end-to-end pin: the pixel a PLAYER sees. Render the whole inventory
// popup and sample the centre of the gold row's decal box. Gold is the only
// unambiguous hue in the set, and the decal is drawn after the paperdoll
// blit (T393), so nothing can be on top of it.
TEST_F(UIAtlasCoinDecalTest, TheInventoryPopupPaintsTheGoldDecalGold) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    InventoryPanelState inv;
    inv.activePopup = PopupType::Inventory;
    eqt::state::CurrencyChangedData d;
    d.platinum = 8; d.gold = 18; d.silver = 8; d.copper = 16;
    applyCurrencyChanged(inv, d);

    const auto decal = InventoryLayout::coinDecalRect(layout_, InventoryLayout::COIN_GOLD);
    ASSERT_GT(decal.getWidth(), 4);
    ASSERT_GT(decal.getHeight(), 4);
    const irr::s32 cx = (decal.UpperLeftCorner.X + decal.LowerRightCorner.X) / 2;
    const irr::s32 cy = (decal.UpperLeftCorner.Y + decal.LowerRightCorner.Y) / 2;

    irr::video::SColor got = renderAndSample(ui, cx, cy, [&](UIRenderer& r) {
        renderInventoryPopup(r, layout_, inv);
    });

    // Pre-fix this sampled the slot background behind the letter: a very dark
    // near-neutral slab.
    EXPECT_GT(static_cast<int>(got.getRed()), 100)
        << "gold decal at (" << cx << "," << cy << ") sampled ("
        << (int)got.getRed() << "," << (int)got.getGreen() << ","
        << (int)got.getBlue() << ")";
    EXPECT_GT(static_cast<int>(got.getGreen()), 100);
    EXPECT_LT(static_cast<int>(got.getBlue()), static_cast<int>(got.getGreen()));
}

// ── The purse's row frames (T416, board B316) ───────────────────────────────
//
// T411 turned the atlas's real alpha on for the UI (77.1% of ui_atlas is not
// opaque). Every 32x32 slot in the client draws its border by stretching
// `slot_border_normal` -- a 1px ring inside a 32x32 tile -- onto a 32x32 rect,
// which is 1:1 and fine. The purse is the ONE place that sprite is drawn onto
// a rect SMALLER than the tile: a coin row is 18 px tall at 800x600, so the
// tile is minified, the sampler takes mip level 1, and the ring has averaged
// into its own transparent interior. Before T411 that was invisible -- the
// washed-out ring sat on a tile the UI painted fully opaque, so the row still
// read as a filled box. With the alpha live the frame collapsed to a 17/255
// step against the row's interior (measured T416,
// review/T416/crops/coinrows_zoom4x_before_after.png), and the row stopped
// reading as a box at all.
//
// The oracle's IW_Money buttons are framed plates
// (review/T401/oracle/T401_oracle_purse_coinrows_crop4x.png), so the fix
// draws the frame from the uniform 4x4 `white` sprite via
// UIRenderer::drawFrame() -- every mip of which is still white -- in the same
// {80,80,80} grey ui_atlas_builder gives the ring.
//
// Pre-fix both tests below fail: the top edge samples ~38 where the interior
// samples ~21.
class UIAtlasCoinRowFrameTest : public UIAtlasPixelTest {
protected:
    // Renders the whole inventory popup once and returns the framebuffer, so
    // the edge and the interior are sampled from the SAME frame -- a contrast
    // assertion across two frames would be comparing two renders.
    irr::video::IImage* renderPurse(UIRenderer& ui) {
        InventoryPanelState inv;
        inv.activePopup = PopupType::Inventory;
        eqt::state::CurrencyChangedData d;
        d.platinum = 8; d.gold = 18; d.silver = 8; d.copper = 16;
        applyCurrencyChanged(inv, d);
        return renderFrame(ui, [&](UIRenderer& r) {
            renderInventoryPopup(r, layout_, inv);
        });
    }
};

TEST_F(UIAtlasCoinRowFrameTest, EveryCoinRowPaintsAFullBrightFrame) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    irr::video::IImage* img = renderPurse(ui);
    ASSERT_NE(img, nullptr);

    for (int denom = 0; denom < InventoryLayout::COIN_COUNT; ++denom) {
        const auto row = InventoryLayout::coinRect(layout_, denom);
        ASSERT_GT(row.getWidth(), 8) << "denom " << denom;
        ASSERT_GT(row.getHeight(), 4) << "denom " << denom;

        const irr::s32 midX = (row.UpperLeftCorner.X + row.LowerRightCorner.X) / 2;
        const irr::s32 midY = (row.UpperLeftCorner.Y + row.LowerRightCorner.Y) / 2;

        // The four edge pixels drawFrame() paints.
        const irr::video::SColor top =
            img->getPixel(static_cast<irr::u32>(midX),
                          static_cast<irr::u32>(row.UpperLeftCorner.Y));
        const irr::video::SColor bottom =
            img->getPixel(static_cast<irr::u32>(midX),
                          static_cast<irr::u32>(row.LowerRightCorner.Y - 1));
        const irr::video::SColor left =
            img->getPixel(static_cast<irr::u32>(row.UpperLeftCorner.X),
                          static_cast<irr::u32>(midY));
        const irr::video::SColor right =
            img->getPixel(static_cast<irr::u32>(row.LowerRightCorner.X - 1),
                          static_cast<irr::u32>(midY));

        const char* kEdge[4] = {"top", "bottom", "left", "right"};
        const irr::video::SColor edges[4] = {top, bottom, left, right};
        for (int e = 0; e < 4; ++e) {
            EXPECT_GT(static_cast<int>(edges[e].getRed()), 60)
                << "coin row " << denom << " " << kEdge[e] << " edge sampled ("
                << (int)edges[e].getRed() << "," << (int)edges[e].getGreen() << ","
                << (int)edges[e].getBlue() << ") -- the frame is not being drawn "
                   "at full strength";
        }
    }
    img->drop();
}

TEST_F(UIAtlasCoinRowFrameTest, TheFrameStandsOutAgainstTheRowInterior) {
    UIRenderer ui;
    ui.init(driver_, atlas_.get(), &textBatch_);

    irr::video::IImage* img = renderPurse(ui);
    ASSERT_NE(img, nullptr);

    const auto row = InventoryLayout::coinRect(layout_, InventoryLayout::COIN_PLATINUM);
    const irr::s32 midX = (row.UpperLeftCorner.X + row.LowerRightCorner.X) / 2;

    const irr::video::SColor edge =
        img->getPixel(static_cast<irr::u32>(midX),
                      static_cast<irr::u32>(row.UpperLeftCorner.Y));
    // Two pixels in from the top edge, same column: the row's own interior.
    // The decal and its number are on the left half, so a mid-column sample
    // just below the frame reads the slot background and nothing else.
    const irr::video::SColor interior =
        img->getPixel(static_cast<irr::u32>(midX),
                      static_cast<irr::u32>(row.UpperLeftCorner.Y + 2));
    img->drop();

    EXPECT_GT(static_cast<int>(edge.getRed()) - static_cast<int>(interior.getRed()), 30)
        << "frame (" << (int)edge.getRed() << "," << (int)edge.getGreen() << ","
        << (int)edge.getBlue() << ") vs interior (" << (int)interior.getRed() << ","
        << (int)interior.getGreen() << "," << (int)interior.getBlue()
        << ") -- the row does not read as a box";
}
