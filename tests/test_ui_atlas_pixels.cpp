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
#include "client/graphics/text_batch.h"
#include "client/graphics/constrained_texture_cache.h"
#include "client/graphics/constrained_renderer_config.h"
#include "client/graphics/eqe1_loader.h"

#include <irrlicht.h>
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
