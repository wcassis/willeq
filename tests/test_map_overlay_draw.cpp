// T335 (refactor/T335_debug_overlay_siblings.md, B054): OverlaysComponent::
// drawMapOverlay() -- the Ctrl+M collision-mesh overlay -- carried B048's
// defect verbatim. It runs in the same post-drawAll() debug-overlay pass as
// drawNavmeshOverlay(), calls setMaterial() with a 3D material and then
// draw3DLine() in world space, and re-set neither ETS_VIEW/ETS_PROJECTION nor
// the 2D-safe render state afterwards. So it drew nothing AND wiped the 2D UI
// for the rest of the frame.
//
// Same harness and same frame shape as tests/test_navmesh_overlay_draw.cpp --
// a real headless GLES2 device (surfaceless EGL / llvmpipe) with a framebuffer
// readback, because the defect is driver state and a mock cannot hold it:
//
//   beginScene -> a 2D draw -> drawMapOverlay() -> the 2D UI -> endScene
//
// The leading 2D draw is load-bearing: it leaves COpenGLES2Driver latched in
// Is2DMode with an ORTHO projection, and setOrthoProjection() early-returns
// while that latch is set. See client/graphics/world_line_batch.h for the
// measurement this pins.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"

#include <irrlicht.h>
#include <memory>

using namespace EQT::Graphics;

namespace {

const irr::video::SColor kMapGreen(255, 0, 255, 0);
const irr::video::SColor kUiRed(255, 255, 0, 0);

}  // namespace

class MapOverlayDrawTest : public ::testing::Test {
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

        // Identity captured transforms, so a world-space vertex IS its
        // clip-space position and the expected pixels below are arithmetic.
        renderer_->Context().captured3DView.makeIdentity();
        renderer_->Context().captured3DProj.makeIdentity();
        renderer_->Frame().have3DTransforms() = true;

        renderer_->Overlays().showMapOverlay() = true;
        // Terrain triangle (isPlaceable = false): the branch that draws under
        // the identity world transform, so the rotation/mirror levers are out
        // of the picture.
        renderer_->Overlays().seedMapOverlayTriangleForTest(
            irr::core::vector3df(-0.8f, -0.8f, 0.0f),
            irr::core::vector3df(0.8f, -0.8f, 0.0f),
            irr::core::vector3df(0.0f, 0.8f, 0.0f),
            kMapGreen, false);
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
            renderer_->Overlays().drawMapOverlay();
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
TEST_F(MapOverlayDrawTest, MapTrianglesReachTheFramebuffer) {
    if (!device_) GTEST_SKIP();
    irr::video::IImage* img = renderFrame();
    ASSERT_NE(img, nullptr);

    struct Probe { const char* name; irr::s32 x, y; };
    const Probe probes[] = {
        {"bottom edge", 400, 540},
        {"left edge",   240, 300},
        {"right edge",  560, 300},
    };
    for (const auto& p : probes) {
        irr::video::SColor c = brightestNear(img, p.x, p.y, 3);
        EXPECT_GT((int)c.getGreen(), 100)
            << p.name << " of the map overlay triangle drew nothing at (" << p.x << ","
            << p.y << ") -- got " << (int)c.getRed() << "," << (int)c.getGreen()
            << "," << (int)c.getBlue();
        EXPECT_LT((int)c.getRed(), 100) << p.name << ": not the map overlay colour";
    }
    img->drop();
}

// Symptom 2, "the 2D UI vanishes".
TEST_F(MapOverlayDrawTest, TheTwoDUIDrawnAfterTheOverlayStillAppears) {
    if (!device_) GTEST_SKIP();
    irr::video::IImage* img = renderFrame();
    ASSERT_NE(img, nullptr);

    irr::video::SColor c = img->getPixel((irr::u32)uiSample().X, (irr::u32)uiSample().Y);
    EXPECT_GT((int)c.getRed(), 200)
        << "the 2D UI drawn after drawMapOverlay() was wiped -- got "
        << (int)c.getRed() << "," << (int)c.getGreen() << "," << (int)c.getBlue();
    EXPECT_LT((int)c.getGreen(), 60);
    EXPECT_LT((int)c.getBlue(), 60);
    img->drop();
}

// The state contract behind both symptoms.
TEST_F(MapOverlayDrawTest, TransformStateIsRestoredAfterTheLineBatch) {
    if (!device_) GTEST_SKIP();

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();

    const irr::core::matrix4 viewBefore = driver_->getTransform(irr::video::ETS_VIEW);
    const irr::core::matrix4 projBefore = driver_->getTransform(irr::video::ETS_PROJECTION);
    const irr::core::matrix4 worldBefore = driver_->getTransform(irr::video::ETS_WORLD);

    renderer_->Overlays().drawMapOverlay();

    EXPECT_EQ(driver_->getTransform(irr::video::ETS_VIEW), viewBefore)
        << "drawMapOverlay() left the view transform changed";
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_PROJECTION), projBefore)
        << "drawMapOverlay() left the projection transform changed";
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_WORLD), worldBefore)
        << "drawMapOverlay() left the world transform changed";

    driver_->endScene();
}

// The early return is unchanged: an empty triangle list draws nothing and
// touches no driver state at all.
TEST_F(MapOverlayDrawTest, AnEmptyTriangleListIsStillANoOp) {
    if (!device_) GTEST_SKIP();
    IrrlichtRenderer bare;
    bare.Context().driver = driver_;
    bare.Overlays().showMapOverlay() = true;
    ASSERT_EQ(bare.Overlays().mapOverlayTriangleCount(), 0u);

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();
    const irr::core::matrix4 projBefore = driver_->getTransform(irr::video::ETS_PROJECTION);
    bare.Overlays().drawMapOverlay();
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_PROJECTION), projBefore);
    driver_->draw2DRectangle(kUiRed, uiRect());
    driver_->endScene();

    irr::video::IImage* img = driver_->createScreenShot();
    ASSERT_NE(img, nullptr);
    img->drop();
}
