/*
 * particle_atlas_builder — regenerates data/textures/particle_atlas.bmp.
 *
 * T400 (board B225). `generateParticleAtlas()` builds all sixteen tiles
 * ENTIRELY IN ALPHA -- every draw helper writes one constant RGB per tile
 * and varies only `pixelColor.setAlpha(...)`. The shipped file was written
 * by the then-retired generator (now the live `tools/generate_textures.cpp`,
 * un-retired by T403 / board B260), which used
 * Irrlicht's `writeImageToFile(img, "*.bmp")`; `CImageWriterBMP` emits 24
 * bpp and DROPS the alpha channel, so the atlas shipped as a 24-bit BMP3
 * whose every texel in a tile is that tile's flat RGB. The runtime's
 * `decodeBMP()` then substitutes alpha=255 and every particle in the game
 * draws as a fully opaque coloured square.
 *
 * This tool writes the same image through `tools/bmp32_writer.h` instead:
 * 32 bpp, BI_BITFIELDS, alpha preserved. Both readers that matter
 * (`ConstrainedTextureCache::decodeBMP()` and `tools/eqe1_builder.cpp`)
 * accept that layout -- it is the one `ui_atlas_builder` has always used.
 *
 * RGB stays exactly as the generator writes it, unchanged and
 * NON-premultiplied, because the consumers alpha-BLEND rather than add:
 * `ParticleManager::init()` sets `EMT_TRANSPARENT_ALPHA_CHANNEL`, which
 * `COpenGLES2Driver::applyMaterialRenderState()` maps to
 * `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`, and
 * `UnifiedParticleRenderer::renderBatch()` uses the same pair for its
 * non-additive batch. Straight alpha also happens to be what the constant
 * per-tile RGB needs: bilinear filtering and the mip chain interpolate RGB
 * across texels, so a premultiplied (black-toward-transparent) tile would
 * darken its own edges, while a constant-RGB tile is interpolation-proof.
 *
 * Usage:
 *   particle_atlas_builder [--output <dir>]     # default: data/textures
 *   # Creates: <dir>/particle_atlas.bmp (64x64, 4x4 tiles of 16x16)
 *
 * Deterministic: no RNG, no time, no GPU -- an EDT_NULL Irrlicht device is
 * created purely because `generateParticleAtlas()` takes an IVideoDriver*
 * to call `createImage()`.
 *
 * NOTE (T403, board B267): `generate_textures --only particle_atlas.bmp` now
 * does exactly what this tool does, for all 64 shipped BMPs rather than one.
 * This target is a strict subset and a duplicate-then-drift risk; it is kept
 * only because `test_particle_atlas_alpha` and T400's Review name it.
 */

#include "bmp32_writer.h"

#include "client/graphics/texture_generators.h"

#include <irrlicht.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    std::string outputDir = "data/textures";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printf("Usage: particle_atlas_builder [--output <dir>]\n");
            printf("  Writes <dir>/particle_atlas.bmp as a 32-bit BMP whose\n");
            printf("  alpha channel carries the sixteen tile shapes.\n");
            return 0;
        } else {
            fprintf(stderr, "particle_atlas_builder: unknown argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "particle_atlas_builder: failed to create EDT_NULL Irrlicht device\n");
        return 1;
    }

    irr::video::IVideoDriver* driver = device->getVideoDriver();
    if (!driver) {
        fprintf(stderr, "particle_atlas_builder: no video driver\n");
        device->drop();
        return 1;
    }

    irr::video::IImage* img = EQT::Graphics::generateParticleAtlas(driver);
    if (!img) {
        fprintf(stderr, "particle_atlas_builder: generateParticleAtlas() returned null\n");
        device->drop();
        return 1;
    }

    const irr::core::dimension2d<irr::u32> dim = img->getDimension();
    const int w = static_cast<int>(dim.Width);
    const int h = static_cast<int>(dim.Height);

    // Read the A8R8G8B8 image back out as RGBA8888 rows, top row first.
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            irr::video::SColor c = img->getPixel(x, y);
            size_t idx = (static_cast<size_t>(y) * w + x) * 4;
            rgba[idx + 0] = static_cast<uint8_t>(c.getRed());
            rgba[idx + 1] = static_cast<uint8_t>(c.getGreen());
            rgba[idx + 2] = static_cast<uint8_t>(c.getBlue());
            rgba[idx + 3] = static_cast<uint8_t>(c.getAlpha());
        }
    }
    img->drop();
    device->drop();

    const std::string path = outputDir + "/particle_atlas.bmp";
    if (!willeq_tools::writeBMP32(path, rgba.data(), w, h)) {
        fprintf(stderr, "particle_atlas_builder: failed to write %s\n", path.c_str());
        return 1;
    }

    printf("OK: %s (%dx%d, 32-bit BMP with alpha)\n", path.c_str(), w, h);
    return 0;
}
