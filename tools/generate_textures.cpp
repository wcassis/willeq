/*
 * generate_textures -- the build recipe for every BMP under data/textures/.
 *
 * T403 (board B260). P26 retired this tool on the rationale "no found
 * consumer of its output anywhere in the tree or docs". That rationale was
 * wrong: T398 established that the 64 BMPs it writes are 64 of the 67
 * entries in the shared `willeq.eqe1` pack, read on every zone load, and
 * T400 showed the cost -- `src/client/graphics/texture_generators.cpp`
 * compiled in NO target for a year while its (alpha-less) output kept
 * shipping, so nobody could run the code that proved the shipped particle
 * atlas wrong (board B225).
 *
 * This is the un-retired tool, restored as a live CMake target. It writes
 * all 64 files, and `scripts/build_textures.sh --check` (the
 * `TexturesReproducible` ctest test) regenerates into a temp directory and
 * compares sha256 against the checked-in tree, so `data/textures/` is now
 * DERIVED, not hand-carried.
 *
 * Differences from the retired version, all deliberate:
 *
 *  1. EDT_NULL, not EDT_SOFTWARE. Nothing here rasterises; every generator
 *     takes an IVideoDriver* only to call createImage(). EDT_NULL is the
 *     device tools/particle_atlas_builder already uses and is the only one
 *     guaranteed to come up headless on both the GLES2-patched x86 build
 *     and the ARM cross-build.
 *  2. detail_atlas goes through DetailTextureAtlas::createAtlasImage()
 *     rather than a createAtlas() -> createImage(texture) round trip. An
 *     EDT_NULL driver's textures are dummies that cannot be read back; the
 *     retired tool only got away with it because it asked for a software
 *     driver.
 *  3. Per-file output format. `writeImageToFile()` goes through Irrlicht's
 *     CImageWriterBMP, which emits 24 bpp and DROPS alpha with no
 *     diagnostic -- that is exactly how B225 happened. Files whose shipped
 *     form is 32-bit-with-alpha are written through tools/bmp32_writer.h
 *     instead. The `format` column of kTextures below is per file and is
 *     the format of the SHIPPED asset, so a regeneration is byte-comparable
 *     against the tree.
 *
 * `--report` prints the census (dimensions, output format, and the alpha
 * histogram of the generated image before it is written) without writing
 * anything. A file whose generator draws real alpha but whose shipped form
 * is 24-bit is a B225 sibling: the report is how you find one.
 *
 * Deterministic: no clock, no unseeded RNG. Every generator either has no
 * randomness or takes an explicit seed (std::mt19937 with a literal).
 *
 * Usage:
 *   generate_textures [--output <dir>] [--report] [--only <name>]
 */

#include "bmp32_writer.h"

#include "client/graphics/texture_generators.h"
#include "client/graphics/detail/detail_texture_atlas.h"

#include <irrlicht.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

using irr::video::IImage;
using irr::video::IVideoDriver;

namespace {

enum class BmpFormat {
    Bmp24,  // Irrlicht CImageWriterBMP: 24 bpp, BI_RGB, bottom-up. Alpha is LOST.
    Bmp32,  // tools/bmp32_writer.h: 32 bpp, BI_BITFIELDS, top-down. Alpha kept.
};

struct TextureRecipe {
    // std::string, not const char*: the generated names below are built in a
    // loop, and a vector<string> reallocating would dangle every c_str().
    std::string filename;
    BmpFormat format;
    std::function<IImage*(IVideoDriver*)> make;
};

// ---------------------------------------------------------------------------
// The storm cloud set: 4 Perlin source frames, each pair interpolated in 8
// steps, giving storm_cloud_blend_0 .. storm_cloud_blend_31. The sources are
// built once and shared -- generating them per output would repeat 128
// 256x256 Perlin passes.
// ---------------------------------------------------------------------------
constexpr int kCloudSize = 256;
constexpr int kCloudFrames = 4;
constexpr int kCloudOctaves = 4;
constexpr float kCloudPersistence = 0.5f;
constexpr int kBlendStepsPerPair = 8;

std::vector<IImage*> g_cloudSources;

void buildCloudSources(IVideoDriver* driver) {
    if (!g_cloudSources.empty()) return;
    for (int i = 0; i < kCloudFrames; ++i) {
        const int seed = 12345 + i * 7919;
        g_cloudSources.push_back(EQT::Graphics::generateCloudFrame(
            driver, seed, kCloudSize, kCloudOctaves, kCloudPersistence));
    }
}

void dropCloudSources() {
    for (IImage* img : g_cloudSources) {
        if (img) img->drop();
    }
    g_cloudSources.clear();
}

IImage* makeCloudBlend(IVideoDriver* driver, int frameIndex) {
    buildCloudSources(driver);
    const int pair = frameIndex / kBlendStepsPerPair;
    const int step = frameIndex % kBlendStepsPerPair;
    const int nextPair = (pair + 1) % kCloudFrames;
    const float t = static_cast<float>(step) / static_cast<float>(kBlendStepsPerPair);
    return EQT::Graphics::generateBlendedCloudFrame(
        driver, g_cloudSources[pair], g_cloudSources[nextPair], t);
}

IImage* makeDetailAtlas(IVideoDriver* driver) {
    EQT::Graphics::Detail::DetailTextureAtlas atlas;
    return atlas.createAtlasImage(driver);
}

// ---------------------------------------------------------------------------
// The manifest. Order is the retired tool's order; the pack does its own
// path sort, so this order is cosmetic.
// ---------------------------------------------------------------------------
std::vector<TextureRecipe> buildManifest() {
    std::vector<TextureRecipe> m;

    // T400 (board B225): the ONLY 32-bit shipped file. Its sixteen tiles are
    // drawn entirely in alpha over a constant per-tile RGB, so a 24-bit write
    // erases the asset.
    m.push_back({"particle_atlas.bmp", BmpFormat::Bmp32,
                 [](IVideoDriver* d) { return EQT::Graphics::generateParticleAtlas(d); }});
    m.push_back({"creature_atlas.bmp", BmpFormat::Bmp24,
                 [](IVideoDriver* d) { return EQT::Graphics::generateCreatureAtlas(d); }});
    m.push_back({"tumbleweed.bmp", BmpFormat::Bmp24,
                 [](IVideoDriver* d) { return EQT::Graphics::generateTumbleweedTexture(d); }});

    // T406 (board B266): the 32 cloud frames are a CONSTANT RGB with the whole
    // shape in alpha (generateCloudFrame() writes SColor(alpha, r, g, b) with
    // r/g/b fixed), and StormCloudLayer draws the dome with
    // EMT_TRANSPARENT_ALPHA_CHANNEL -- glBlendFunc(GL_SRC_ALPHA,
    // GL_ONE_MINUS_SRC_ALPHA) through SOLID3D_FS, one sampler. A 24-bit write
    // makes every frame a solid grey dome.
    for (int i = 0; i < kCloudFrames * kBlendStepsPerPair; ++i) {
        m.push_back({"storm_cloud_blend_" + std::to_string(i) + ".bmp", BmpFormat::Bmp32,
                     [i](IVideoDriver* d) { return makeCloudBlend(d, i); }});
    }

    m.push_back({"spell_particle_fallback.bmp", BmpFormat::Bmp24,
                 [](IVideoDriver* d) { return EQT::Graphics::generateSpellParticleFallback(d); }});
    m.push_back({"software_cursor.bmp", BmpFormat::Bmp24,
                 [](IVideoDriver* d) { return EQT::Graphics::generateSoftwareCursor(d); }});
    m.push_back({"detail_atlas.bmp", BmpFormat::Bmp24, &makeDetailAtlas});

    // Rain: ten intensities plus three fixed layers. numStreaks/seed/length
    // arguments are the retired tool's, verbatim -- they are the recipe.
    // T406: the twelve RainOverlay reads (and the twelve SnowOverlay ones
    // below) go through driver->draw2DImage(..., useAlphaChannelOfTexture=true),
    // i.e. GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA with the single-sampler UI2D
    // program. The streaks and flakes ARE the alpha, so those twelve+twelve are
    // Bmp32; the two files nothing loads stay Bmp24.
    for (int intensity = 1; intensity <= 10; ++intensity) {
        char buf[64];
        snprintf(buf, sizeof(buf), "rain_intensity_%02d.bmp", intensity);
        const int numStreaks = 20 + (intensity - 1) * (100 - 20) / 9;
        const int seed = 100 + intensity;
        m.push_back({buf, BmpFormat::Bmp32,
                     [numStreaks, seed](IVideoDriver* d) {
                         return EQT::Graphics::generateRainTexture(d, numStreaks, seed, 30, 70);
                     }});
    }
    m.push_back({"rain_layer_mid.bmp", BmpFormat::Bmp32, [](IVideoDriver* d) {
                     return EQT::Graphics::generateRainTexture(d, 40, 200, 20, 50); }});
    m.push_back({"rain_layer_far.bmp", BmpFormat::Bmp32, [](IVideoDriver* d) {
                     return EQT::Graphics::generateRainTexture(d, 35, 201, 10, 35); }});
    // rain_streaks has NO consumer -- RainOverlay names rain_intensity_01..10
    // and rain_layer_mid/far and nothing else -- so it stays 24-bit rather than
    // paying for an RGBA8888 pack entry nothing samples (T406, board B266).
    m.push_back({"rain_streaks.bmp", BmpFormat::Bmp24, [](IVideoDriver* d) {
                     return EQT::Graphics::generateRainTexture(d, 50, 42, 10, 40); }});

    // Snow: ten intensities plus three fixed layers.
    for (int intensity = 1; intensity <= 10; ++intensity) {
        char buf[64];
        snprintf(buf, sizeof(buf), "snow_intensity_%02d.bmp", intensity);
        const int density = 50 + (intensity - 1) * 60;
        const float minSz = 1.0f + (intensity - 1) * 0.1f;
        const float maxSz = 3.0f + (intensity - 1) * 0.2f;
        const int alpha = 140 + intensity * 8;
        m.push_back({buf, BmpFormat::Bmp32,
                     [density, minSz, maxSz, alpha](IVideoDriver* d) {
                         return EQT::Graphics::generateSnowTexture(d, density, 42 + density,
                                                                  minSz, maxSz, alpha);
                     }});
    }
    m.push_back({"snow_layer_mid.bmp", BmpFormat::Bmp32, [](IVideoDriver* d) {
                     return EQT::Graphics::generateSnowTexture(d, 200, 242, 1.5f, 3.5f, 160); }});
    m.push_back({"snow_layer_far.bmp", BmpFormat::Bmp32, [](IVideoDriver* d) {
                     return EQT::Graphics::generateSnowTexture(d, 150, 292, 0.8f, 2.0f, 120); }});
    // snow_flakes has NO consumer, exactly like rain_streaks above.
    m.push_back({"snow_flakes.bmp", BmpFormat::Bmp24, [](IVideoDriver* d) {
                     return EQT::Graphics::generateSnowTexture(d, 300, 342, 1.0f, 3.0f, 180); }});

    return m;
}

bool writeBmp24(IVideoDriver* driver, IImage* img, const std::string& path) {
    return driver->writeImageToFile(img, path.c_str());
}

bool writeBmp32From(IImage* img, const std::string& path) {
    const irr::core::dimension2d<irr::u32> dim = img->getDimension();
    const int w = static_cast<int>(dim.Width);
    const int h = static_cast<int>(dim.Height);
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const irr::video::SColor c = img->getPixel(x, y);
            const size_t idx = (static_cast<size_t>(y) * w + x) * 4;
            rgba[idx + 0] = static_cast<uint8_t>(c.getRed());
            rgba[idx + 1] = static_cast<uint8_t>(c.getGreen());
            rgba[idx + 2] = static_cast<uint8_t>(c.getBlue());
            rgba[idx + 3] = static_cast<uint8_t>(c.getAlpha());
        }
    }
    return willeq_tools::writeBMP32(path, rgba.data(), w, h);
}

void reportAlpha(IImage* img, const char* name, BmpFormat fmt) {
    const irr::core::dimension2d<irr::u32> dim = img->getDimension();
    std::set<int> levels;
    int minA = 255, maxA = 0;
    for (irr::u32 y = 0; y < dim.Height; ++y) {
        for (irr::u32 x = 0; x < dim.Width; ++x) {
            const int a = static_cast<int>(img->getPixel(x, y).getAlpha());
            if (levels.size() < 64) levels.insert(a);
            if (a < minA) minA = a;
            if (a > maxA) maxA = a;
        }
    }
    printf("%-32s %4ux%-4u %-5s alphaLevels=%2zu%s min=%3d max=%3d%s\n",
           name, dim.Width, dim.Height,
           fmt == BmpFormat::Bmp32 ? "BMP32" : "BMP24",
           levels.size(), levels.size() >= 64 ? "+" : " ", minA, maxA,
           (fmt == BmpFormat::Bmp24 && levels.size() > 1)
               ? "   <-- ALPHA DROPPED BY THE 24-BIT WRITE"
               : "");
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string outputDir = "data/textures";
    bool report = false;
    std::string only;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printf("Usage: generate_textures [--output <dir>] [--report] [--only <name>]\n");
            printf("  --output <dir>  where to write the BMPs (default: data/textures)\n");
            printf("  --report        print the census (dims, format, alpha histogram);\n");
            printf("                  writes nothing\n");
            printf("  --only <name>   restrict to one filename, e.g. tumbleweed.bmp\n");
            return 0;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--report") {
            report = true;
        } else if (arg == "--only" && i + 1 < argc) {
            only = argv[++i];
        } else {
            fprintf(stderr, "generate_textures: unknown argument '%s'\n", arg.c_str());
            return 2;
        }
    }

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    if (!device) {
        fprintf(stderr, "generate_textures: failed to create EDT_NULL Irrlicht device\n");
        return 1;
    }
    IVideoDriver* driver = device->getVideoDriver();
    if (!driver) {
        fprintf(stderr, "generate_textures: no video driver\n");
        device->drop();
        return 1;
    }

    if (!report) {
        mkdir(outputDir.c_str(), 0755);
    }

    const std::vector<TextureRecipe> manifest = buildManifest();
    int written = 0;
    int failed = 0;

    for (const TextureRecipe& r : manifest) {
        if (!only.empty() && only != r.filename) continue;
        IImage* img = r.make(driver);
        if (!img) {
            fprintf(stderr, "  FAIL: %s (generator returned null)\n", r.filename.c_str());
            ++failed;
            continue;
        }
        if (report) {
            reportAlpha(img, r.filename.c_str(), r.format);
            img->drop();
            continue;
        }
        const std::string path = outputDir + "/" + r.filename;
        const bool ok = (r.format == BmpFormat::Bmp32) ? writeBmp32From(img, path)
                                                       : writeBmp24(driver, img, path);
        img->drop();
        if (ok) {
            ++written;
        } else {
            fprintf(stderr, "  FAIL: %s (write failed)\n", path.c_str());
            ++failed;
        }
    }

    dropCloudSources();
    device->drop();

    if (!report) {
        printf("generate_textures: %d written, %d failed (%s)\n", written, failed,
               outputDir.c_str());
    }
    return failed > 0 ? 1 : 0;
}
