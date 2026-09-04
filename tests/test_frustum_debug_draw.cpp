// T335b (refactor/T335_debug_overlay_siblings.md, B054): the `frustumDebugDraw_`
// block in FrameComponent::renderDebugOverlays() -- the Ctrl+V culling debug
// wireframe -- was the last of B054's three siblings still carrying B048's
// defect. It runs in the same post-drawAll() debug-overlay pass as the navmesh,
// map and collision overlays, called setMaterial() with a 3D material and then
// draw3DLine() in world space, and re-set neither ETS_VIEW/ETS_PROJECTION nor
// the 2D-safe render state afterwards. So it drew nothing AND wiped the 2D UI
// for the rest of the frame.
//
// Same harness and same frame shape as tests/test_navmesh_overlay_draw.cpp --
// a real headless GLES2 device (surfaceless EGL / llvmpipe) with a framebuffer
// readback, because the defect is driver state:
//
//   beginScene -> a 2D draw -> renderDebugOverlays() -> the 2D UI -> endScene
//
// WHY THE WHOLE PASS AND NOT ONE FUNCTION: the block is not a function. Every
// statement of renderDebugOverlays() is pinned by
// tests/goldens/frame_render_order_g05.txt, so it cannot be factored out. The
// test drives the pass with every OTHER overlay in it left at its default OFF
// (collisionDebug, showMapOverlay, showNavmeshOverlay, stencilDebugDraw), so
// the only geometry that can reach the framebuffer is the frustum wireframe.
//
// See client/graphics/world_line_batch.h for the measurement this pins.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"
#include "client/graphics/frustum_culler.h"

#include <irrlicht.h>
#include <memory>

using namespace EQT::Graphics;

namespace {

const irr::video::SColor kUiRed(255, 255, 0, 0);

}  // namespace

class FrustumDebugDrawTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<IrrlichtRenderer> renderer_;

    static constexpr irr::u32 kW = 800;
    static constexpr irr::u32 kH = 600;

    static irr::core::rect<irr::s32> uiRect() {
        return irr::core::rect<irr::s32>(600, 20, 780, 90);
    }
    static irr::core::position2di uiSample() { return irr::core::position2di(690, 55); }

    void SetUp() override {
        irr::SIrrlichtCreationParameters params;
        params.DriverType = irr::video::EDT_OGLES2;
        params.WindowSize = irr::core::dimension2d<irr::u32>(kW, kH);
        params.Fullscreen = true;
        device_ = irr::createDeviceEx(params);
        if (!device_) {
            GTEST_SKIP() << "Failed to create EDT_OGLES2 device (surfaceless EGL "
                            "unavailable in this environment) — skipping.";
        }
        driver_ = device_->getVideoDriver();

        renderer_ = std::make_unique<IrrlichtRenderer>();
        renderer_->Context().driver = driver_;
        renderer_->Context().captured3DView.makeIdentity();
        renderer_->Context().captured3DProj.makeIdentity();
        renderer_->Frame().have3DTransforms() = true;

        // A default-constructed FrustumCuller holds six zeroed planes, so
        // testAABB() reports every box inside -- the box below is therefore
        // GREEN ("rendered"), the branch a live Ctrl+V session shows most of.
        // The player sits at the origin and renderDistance is wide, so the PVS
        // and distance branches are out of the picture too.
        renderer_->Context().frustumCuller = std::make_unique<FrustumCuller>();
        renderer_->Context().renderDistance = 10000.0f;

        // One region bbox in EQ (Z-up) coordinates. The block maps
        // EQ(x,y,z) -> Irr(x,z,y), and with identity captured transforms an
        // Irrlicht vertex IS its clip-space position, so the two drawn faces
        // land as one screen-space square with edges at x=80/720, y=60/540.
        renderer_->Frame().seedFrustumDebugBoxForTest(
            0, irr::core::aabbox3df(-0.8f, -0.8f, -0.8f, 0.8f, 0.8f, 0.8f));
        renderer_->Frame().frustumDebugDraw() = true;
    }

    void TearDown() override {
        renderer_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    irr::video::IImage* renderFrame() {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();

        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            draw2DLeadIn();
            renderer_->Frame().renderDebugOverlaysForTest(0.0f);
            driver_->draw2DRectangle(kUiRed, uiRect());
            driver_->endScene();
        }
        return driver_->createScreenShot();
    }

    void draw2DLeadIn() {
        irr::gui::IGUIFont* font = device_->getGUIEnvironment()->getBuiltInFont();
        if (font) {
            font->draw(L".", irr::core::rect<irr::s32>(2, 2, 20, 20),
                       irr::video::SColor(255, 255, 255, 255));
        }
        driver_->draw2DRectangle(irr::video::SColor(255, 40, 40, 40),
                                 irr::core::rect<irr::s32>(2, 2, 8, 8));
    }

    static irr::video::SColor brightestNear(irr::video::IImage* img,
                                            irr::s32 x, irr::s32 y, irr::s32 r) {
        irr::video::SColor best(255, 0, 0, 0);
        int bestSum = -1;
        for (irr::s32 dy = -r; dy <= r; ++dy) {
            for (irr::s32 dx = -r; dx <= r; ++dx) {
                irr::s32 px = x + dx, py = y + dy;
                if (px < 0 || py < 0 || px >= (irr::s32)kW || py >= (irr::s32)kH) continue;
                irr::video::SColor c = img->getPixel((irr::u32)px, (irr::u32)py);
                int sum = c.getRed() + c.getGreen() + c.getBlue();
                if (sum > bestSum) { bestSum = sum; best = c; }
            }
        }
        return best;
    }
};

// Symptom 1, "nothing draws".
TEST_F(FrustumDebugDrawTest, RegionBoundingBoxEdgesReachTheFramebuffer) {
    if (!device_) GTEST_SKIP();
    irr::video::IImage* img = renderFrame();
    ASSERT_NE(img, nullptr);

    struct Probe { const char* name; irr::s32 x, y; };
    const Probe probes[] = {
        {"bottom edge", 400, 540},
        {"left edge",    80, 300},
        {"right edge",  720, 300},
    };
    for (const auto& p : probes) {
        irr::video::SColor c = brightestNear(img, p.x, p.y, 3);
        EXPECT_GT((int)c.getGreen(), 100)
            << p.name << " of the frustum debug box drew nothing at (" << p.x << ","
            << p.y << ") -- got " << (int)c.getRed() << "," << (int)c.getGreen()
            << "," << (int)c.getBlue();
        EXPECT_LT((int)c.getRed(), 100) << p.name << ": not the 'rendered' green";
    }
    img->drop();
}

// Symptom 2, "the 2D UI vanishes".
TEST_F(FrustumDebugDrawTest, TheTwoDUIDrawnAfterTheOverlayStillAppears) {
    if (!device_) GTEST_SKIP();
    irr::video::IImage* img = renderFrame();
    ASSERT_NE(img, nullptr);

    irr::video::SColor c = img->getPixel((irr::u32)uiSample().X, (irr::u32)uiSample().Y);
    EXPECT_GT((int)c.getRed(), 200)
        << "the 2D UI drawn after the frustum debug block was wiped -- got "
        << (int)c.getRed() << "," << (int)c.getGreen() << "," << (int)c.getBlue();
    EXPECT_LT((int)c.getGreen(), 60);
    EXPECT_LT((int)c.getBlue(), 60);
    img->drop();
}

// The state contract behind both symptoms.
TEST_F(FrustumDebugDrawTest, TransformStateIsRestoredAfterTheLineBatch) {
    if (!device_) GTEST_SKIP();

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();

    const irr::core::matrix4 viewBefore = driver_->getTransform(irr::video::ETS_VIEW);
    const irr::core::matrix4 projBefore = driver_->getTransform(irr::video::ETS_PROJECTION);
    const irr::core::matrix4 worldBefore = driver_->getTransform(irr::video::ETS_WORLD);

    renderer_->Frame().renderDebugOverlaysForTest(0.0f);

    EXPECT_EQ(driver_->getTransform(irr::video::ETS_VIEW), viewBefore)
        << "the frustum debug block left the view transform changed";
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_PROJECTION), projBefore)
        << "the frustum debug block left the projection transform changed";
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_WORLD), worldBefore)
        << "the frustum debug block left the world transform changed";

    driver_->endScene();
}

// The guards are unchanged: with the toggle off, or with no region boxes, the
// pass touches no driver state at all.
TEST_F(FrustumDebugDrawTest, TheToggleAndTheEmptyBoxListAreStillNoOps) {
    if (!device_) GTEST_SKIP();
    ASSERT_EQ(renderer_->Frame().frustumDebugBoxCount(), 1u);

    // (a) toggle off, box present.
    renderer_->Frame().frustumDebugDraw() = false;
    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();
    const irr::core::matrix4 projBefore = driver_->getTransform(irr::video::ETS_PROJECTION);
    renderer_->Frame().renderDebugOverlaysForTest(0.0f);
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_PROJECTION), projBefore);
    driver_->endScene();

    // (b) toggle on, no boxes -- the for-loop body never runs.
    IrrlichtRenderer bare;
    bare.Context().driver = driver_;
    bare.Context().frustumCuller = std::make_unique<FrustumCuller>();
    bare.Frame().frustumDebugDraw() = true;
    ASSERT_EQ(bare.Frame().frustumDebugBoxCount(), 0u);

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();
    bare.Frame().renderDebugOverlaysForTest(0.0f);
    driver_->draw2DRectangle(kUiRed, uiRect());
    driver_->endScene();

    irr::video::IImage* img = driver_->createScreenShot();
    ASSERT_NE(img, nullptr);
    // No LINE drew -- but the material setup sits BEFORE the loop, so the block
    // still installed a 3D material for an empty box list. Pre-fix that alone
    // wiped the UI (this case failed too); the batch has to cover it.
    irr::video::SColor c = img->getPixel((irr::u32)uiSample().X, (irr::u32)uiSample().Y);
    EXPECT_GT((int)c.getRed(), 200);
    img->drop();
}
