// T333 (refactor/T333_navmesh_overlay_draw.md, B048): the Ctrl+N navmesh
// overlay drew nothing AND wiped the entire 2D UI.
//
// Measured live in review/T329/ (two captures from one session, plus the
// client's own log proving the triangle list held 112 triangles). Both
// symptoms have one cause, and it is a driver-state one, so these tests use
// the SAME harness as tests/test_ui_atlas_pixels.cpp -- a real headless GLES2
// device (surfaceless EGL / llvmpipe) with a framebuffer readback -- rather
// than a mock. They need no EQE1 asset and no server.
//
// The frame shape below is the live frame's shape, minimised:
//
//   beginScene -> a 2D draw -> drawNavmeshOverlay() -> the 2D UI -> endScene
//
// The leading 2D draw is load-bearing, and it is what the real client always
// has by this point in the frame (billboards / GUI nodes / the target
// selection box, drawn during or just after smgr->drawAll() -- see the
// comment in OverlaysComponent::drawTargetOutline). It leaves COpenGLES2Driver
// latched in Is2DMode with an ORTHO projection in Matrices[ETS_PROJECTION],
// and setOrthoProjection() early-returns while that latch is set. So:
//
//   * drawing world-space lines without re-setting ETS_VIEW/ETS_PROJECTION
//     multiplies world coordinates by the ortho matrix -- the lines land far
//     outside the screen box, which is "nothing draws"; and
//   * setMaterial() re-enables back-face culling and the depth test, and the
//     next 2D draw cannot undo it because setOrthoProjection() (the only
//     caller of setCull(false)/setDepthTest(false)) early-returns -- which is
//     "the 2D UI vanishes".
//
// Nothing here asserts on how the fix is spelled; it asserts on the two
// user-visible facts and on the driver state being handed back unchanged.

#include <gtest/gtest.h>

#include "client/graphics/irrlicht_renderer.h"

#include <irrlicht.h>
#include <memory>

using namespace EQT::Graphics;

namespace {

const irr::video::SColor kNavmeshGreen(255, 0, 255, 0);
const irr::video::SColor kUiRed(255, 255, 0, 0);

}  // namespace

class NavmeshOverlayDrawTest : public ::testing::Test {
protected:
    irr::IrrlichtDevice* device_ = nullptr;
    irr::video::IVideoDriver* driver_ = nullptr;
    std::unique_ptr<IrrlichtRenderer> renderer_;

    static constexpr irr::u32 kW = 800;
    static constexpr irr::u32 kH = 600;

    // The UI stand-in: a plain opaque rectangle in the top-right corner,
    // clear of every navmesh edge sampled below.
    static irr::core::rect<irr::s32> uiRect() {
        return irr::core::rect<irr::s32>(600, 20, 780, 90);
    }
    static irr::core::position2di uiSample() { return irr::core::position2di(690, 55); }

    void SetUp() override {
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

        renderer_ = std::make_unique<IrrlichtRenderer>();
        renderer_->Context().driver = driver_;

        // The captured 3D transforms the frame pass hands the overlays. Both
        // identity, so a world-space vertex IS its clip-space position and the
        // expected screen pixels below are plain arithmetic rather than a
        // camera solve.
        renderer_->Context().captured3DView.makeIdentity();
        renderer_->Context().captured3DProj.makeIdentity();
        renderer_->Frame().have3DTransforms() = true;

        renderer_->Overlays().showNavmeshOverlay() = true;
        // One triangle spanning most of the viewport, so all three of its
        // edges are far from the screen border and from the UI rectangle.
        renderer_->Overlays().seedNavmeshOverlayTriangleForTest(
            irr::core::vector3df(-0.8f, -0.8f, 0.0f),
            irr::core::vector3df(0.8f, -0.8f, 0.0f),
            irr::core::vector3df(0.0f, 0.8f, 0.0f),
            kNavmeshGreen);
    }

    void TearDown() override {
        renderer_.reset();
        if (device_) {
            device_->drop();
            device_ = nullptr;
        }
    }

    // The live frame, minimised. `probe` runs between the leading 2D draw and
    // drawNavmeshOverlay()/the UI draw so a test can read driver state at
    // exactly that point.
    //
    // The prime frame and the repeated content frame are the harness facts
    // test_ui_atlas_pixels.cpp already documents for this backend (the first
    // content frame after device creation loses its immediate-mode draws), as
    // is keeping a font draw in the frame. Here the font draw doubles as the
    // leading 2D draw the defect needs.
    irr::video::IImage* renderFrame() {
        driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
        driver_->endScene();

        for (int frame = 0; frame < 2; ++frame) {
            driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
            draw2DLeadIn();
            renderer_->Overlays().drawNavmeshOverlay();
            driver_->draw2DRectangle(kUiRed, uiRect());
            driver_->endScene();
        }
        return driver_->createScreenShot();
    }

    // Stands in for everything 2D the real client has already drawn by the
    // time the debug-overlay pass runs.
    void draw2DLeadIn() {
        irr::gui::IGUIFont* font = device_->getGUIEnvironment()->getBuiltInFont();
        if (font) {
            font->draw(L".", irr::core::rect<irr::s32>(2, 2, 20, 20),
                       irr::video::SColor(255, 255, 255, 255));
        }
        driver_->draw2DRectangle(irr::video::SColor(255, 40, 40, 40),
                                 irr::core::rect<irr::s32>(2, 2, 8, 8));
    }

    // Rasterised lines are thin and land where they land; sample a small box
    // and keep the strongest hit rather than betting on one pixel.
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

// Symptom 1, the "nothing draws" half. World-space navmesh edges must reach
// the framebuffer. Before the fix drawNavmeshOverlay() left ETS_VIEW /
// ETS_PROJECTION as whatever the preceding 2D draw set (an ortho matrix in
// pixel units), so the triangle collapsed into the top-left corner and no
// green pixel existed anywhere near these three edge midpoints.
TEST_F(NavmeshOverlayDrawTest, NavmeshTrianglesReachTheFramebuffer) {
    if (!device_) GTEST_SKIP();
    irr::video::IImage* img = renderFrame();
    ASSERT_NE(img, nullptr);

    // The three edges of the seeded triangle, one sample each -- this is the
    // "3 lines per triangle" assertion, made on pixels.
    struct Probe { const char* name; irr::s32 x, y; };
    const Probe probes[] = {
        {"bottom edge", 400, 540},
        {"left edge",   240, 300},
        {"right edge",  560, 300},
    };
    for (const auto& p : probes) {
        irr::video::SColor c = brightestNear(img, p.x, p.y, 3);
        EXPECT_GT((int)c.getGreen(), 100)
            << p.name << " of the navmesh triangle drew nothing at (" << p.x << ","
            << p.y << ") -- got " << (int)c.getRed() << "," << (int)c.getGreen()
            << "," << (int)c.getBlue();
        EXPECT_LT((int)c.getRed(), 100) << p.name << ": not the navmesh colour";
    }
    img->drop();
}

// Symptom 2, the "the 2D UI vanishes" half, and the one B048 was actually
// reported for. The UI rectangle is drawn AFTER drawNavmeshOverlay() in the
// same frame, exactly as the static UI pass runs after the debug-overlay pass.
TEST_F(NavmeshOverlayDrawTest, TheTwoDUIDrawnAfterTheOverlayStillAppears) {
    if (!device_) GTEST_SKIP();
    irr::video::IImage* img = renderFrame();
    ASSERT_NE(img, nullptr);

    irr::video::SColor c = img->getPixel((irr::u32)uiSample().X, (irr::u32)uiSample().Y);
    EXPECT_GT((int)c.getRed(), 200)
        << "the 2D UI drawn after drawNavmeshOverlay() was wiped -- got "
        << (int)c.getRed() << "," << (int)c.getGreen() << "," << (int)c.getBlue();
    EXPECT_LT((int)c.getGreen(), 60);
    EXPECT_LT((int)c.getBlue(), 60);
    img->drop();
}

// The state contract behind both symptoms: the overlay borrows the driver's
// transforms for one line batch and must hand them back exactly as it found
// them, so the 2D pass that follows is unaffected.
TEST_F(NavmeshOverlayDrawTest, TransformStateIsRestoredAfterTheLineBatch) {
    if (!device_) GTEST_SKIP();

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();

    const irr::core::matrix4 viewBefore = driver_->getTransform(irr::video::ETS_VIEW);
    const irr::core::matrix4 projBefore = driver_->getTransform(irr::video::ETS_PROJECTION);
    const irr::core::matrix4 worldBefore = driver_->getTransform(irr::video::ETS_WORLD);

    renderer_->Overlays().drawNavmeshOverlay();

    EXPECT_EQ(driver_->getTransform(irr::video::ETS_VIEW), viewBefore)
        << "drawNavmeshOverlay() left the view transform changed";
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_PROJECTION), projBefore)
        << "drawNavmeshOverlay() left the projection transform changed";
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_WORLD), worldBefore)
        << "drawNavmeshOverlay() left the world transform changed";

    driver_->endScene();
}

// The early return is unchanged: an empty triangle list draws nothing and
// touches no driver state at all.
TEST_F(NavmeshOverlayDrawTest, AnEmptyTriangleListIsStillANoOp) {
    if (!device_) GTEST_SKIP();
    IrrlichtRenderer bare;
    bare.Context().driver = driver_;
    bare.Overlays().showNavmeshOverlay() = true;
    ASSERT_EQ(bare.Overlays().navmeshOverlayTriangleCount(), 0u);

    driver_->beginScene(true, true, irr::video::SColor(255, 0, 0, 0));
    draw2DLeadIn();
    const irr::core::matrix4 projBefore = driver_->getTransform(irr::video::ETS_PROJECTION);
    bare.Overlays().drawNavmeshOverlay();
    EXPECT_EQ(driver_->getTransform(irr::video::ETS_PROJECTION), projBefore);
    driver_->draw2DRectangle(kUiRed, uiRect());
    driver_->endScene();

    irr::video::IImage* img = driver_->createScreenShot();
    ASSERT_NE(img, nullptr);
    img->drop();
}
