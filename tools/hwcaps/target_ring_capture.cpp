/*
 * target_ring_capture -- B994 (#314): the 3D target ring, drawn by the
 * client's own TargetRing::build() + TargetRing::drawGeometry() into a real
 * headless GLES2 device (surfaceless EGL / llvmpipe), one PNG.
 *
 * The scene: a grey floor quad seen from a chase-like camera, and one ring
 * per con section of TargetIndicator.ini (Trivial .. Deadly) at a human
 * footprint radius (1.5) in a row, plus a larger target (footprint 6) behind
 * them -- so a reviewer sees every tint, the fade-in at the centre, the
 * fade-out at twice the footprint, and the size following the target. A
 * second PNG has the option OFF (TargetRing::shouldDraw false): the same
 * floor, no ring.
 *
 *   target_ring_capture [output_dir]
 *
 * Not a performance tool: one frame, captured.
 */

#include <irrlicht.h>

#include <cstdio>
#include <string>
#include <sys/stat.h>

#include "client/data_paths.h"
#include "client/graphics/gles2_shader_cache.h"
#include "client/graphics/overlays/target_ring.h"
#include "client/graphics/overlays/target_ring_draw.h"

using namespace EQT::Graphics;

namespace {

void drawFloor(irr::video::IVideoDriver* driver) {
    irr::video::S3DVertex v[4];
    const irr::video::SColor grey(255, 70, 72, 66);
    v[0] = irr::video::S3DVertex(-60, 0, -20, 0, 1, 0, grey, 0, 0);
    v[1] = irr::video::S3DVertex(60, 0, -20, 0, 1, 0, grey, 1, 0);
    v[2] = irr::video::S3DVertex(60, 0, 60, 0, 1, 0, grey, 1, 1);
    v[3] = irr::video::S3DVertex(-60, 0, 60, 0, 1, 0, grey, 0, 1);
    const irr::u16 idx[6] = {0, 1, 2, 0, 2, 3};
    irr::video::SMaterial m;
    m.Lighting = false;
    m.BackfaceCulling = false;
    driver->setTransform(irr::video::ETS_WORLD, irr::core::matrix4());
    driver->setMaterial(m);
    driver->drawVertexPrimitiveList(v, 4, idx, 2, irr::video::EVT_STANDARD,
                                    irr::scene::EPT_TRIANGLES, irr::video::EIT_16BIT);
}

bool capture(irr::IrrlichtDevice* device, bool optionOn, const std::string& path) {
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    irr::core::matrix4 view, proj;
    view.buildCameraLookAtMatrixLH(irr::core::vector3df(0, 14, -26), irr::core::vector3df(0, 0, 8),
                                   irr::core::vector3df(0, 1, 0));
    proj.buildProjectionMatrixPerspectiveFovLH(1.0f, 800.0f / 600.0f, 1.0f, 500.0f);

    TargetRing::Geometry geom;
    for (int pass = 0; pass < 3; ++pass) {  // prime + warm-up + the captured frame
        driver->beginScene(true, true, irr::video::SColor(255, 20, 24, 32));
        driver->setTransform(irr::video::ETS_VIEW, view);
        driver->setTransform(irr::video::ETS_PROJECTION, proj);
        drawFloor(driver);
        for (int s = 0; s < static_cast<int>(TargetRing::Section::Count); ++s) {
            if (!TargetRing::shouldDraw(optionOn, 1, true, true)) break;
            const float x = -20.0f + 8.0f * static_cast<float>(s);
            TargetRing::build(TargetRing::styleFor(static_cast<TargetRing::Section>(s)), x, 0.0f,
                              0.0f, 1.5f, geom);
            TargetRing::drawGeometry(driver, geom);
        }
        if (TargetRing::shouldDraw(optionOn, 2, true, true)) {
            TargetRing::build(TargetRing::styleFor(TargetRing::Section::Difficult), 0.0f, 0.0f,
                              25.0f, 6.0f, geom);
            TargetRing::drawGeometry(driver, geom);
        }
        driver->endScene();
    }
    irr::video::IImage* img = driver->createScreenShot();
    if (!img) return false;
    const bool ok = driver->writeImageToFile(img, path.c_str());
    img->drop();
    if (ok) printf("Wrote %s\n", path.c_str());
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : "target_ring_capture";
    mkdir(outDir.c_str(), 0755);
    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_OGLES2;
    params.WindowSize = irr::core::dimension2d<irr::u32>(800, 600);
    params.Fullscreen = true;
    eqt::ConfigureGles2ShaderCache();
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "ERROR: no EDT_OGLES2 device\n");
        return 1;
    }
    bool ok = capture(device, true, outDir + "/target_ring_on_800x600.png");
    ok = capture(device, false, outDir + "/target_ring_off_800x600.png") && ok;
    device->drop();
    return ok ? 0 : 1;
}
