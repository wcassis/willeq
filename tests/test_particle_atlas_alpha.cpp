// T400 (board B225): the particle atlas must carry its alpha.
//
// `generateParticleAtlas()` builds all sixteen tiles ENTIRELY IN ALPHA --
// every draw helper writes one constant RGB per tile and varies only
// `pixelColor.setAlpha(...)`. Two things used to throw that away:
//
//   1. The shipped `data/textures/particle_atlas.bmp` was written through
//      Irrlicht's `writeImageToFile(img, "*.bmp")`, whose `CImageWriterBMP`
//      emits 24 bpp and drops alpha. `ConstrainedTextureCache::decodeBMP()`
//      then substitutes alpha=255 for every texel.
//   2. In `willeq.eqe1` the entry was ETC1 (RGB-only) with its alpha in a
//      separate `particle_atlas_alpha` companion. Both particle draw paths
//      sample a SINGLE texture and read `texColor.a` from it
//      (ParticleManager's EMT_TRANSPARENT_ALPHA_CHANNEL material ->
//      SOLID3D_FS; UnifiedParticleRenderer's point-sprite shader has one
//      sampler), so the companion was never read and alpha was 1.0
//      everywhere.
//
// Either one alone makes every particle in the game a flat, fully opaque
// coloured square, so both are asserted here.

#include <gtest/gtest.h>

#include "client/graphics/environment/particle_manager.h"
#include "client/graphics/eqe1_loader.h"
#include "client/graphics/eqe1_format.h"
#include "client/graphics/texture_generators.h"

#include <irrlicht.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

#ifndef WILLEQ_SOURCE_DIR
#define WILLEQ_SOURCE_DIR "."
#endif
const char* kAtlasPath = WILLEQ_SOURCE_DIR "/data/textures/particle_atlas.bmp";
const char* kWilleqEqe1 = "/mnt/projects/EverQuest_ETC1/willeq.eqe1";

constexpr int kTileSize = 16;
constexpr int kAtlasColumns = 4;
constexpr int kAtlasRows = 4;

// Tile 15 (column 3, row 3) is the one the generator never draws -- it stays
// at the fill value, fully transparent. Every OTHER tile is a shape.
constexpr int kUndrawnTile = 15;

struct BmpHeader {
    uint32_t dataOffset = 0;
    uint32_t dibHeaderSize = 0;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t bitsPerPixel = 0;
    uint32_t compression = 0;
};

std::vector<char> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::streamsize n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> data(static_cast<size_t>(n));
    if (!f.read(data.data(), n)) return {};
    return data;
}

bool parseBmpHeader(const std::vector<char>& data, BmpHeader& out) {
    if (data.size() < 54) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
    if (p[0] != 'B' || p[1] != 'M') return false;
    std::memcpy(&out.dataOffset, p + 10, 4);
    std::memcpy(&out.dibHeaderSize, p + 14, 4);
    std::memcpy(&out.width, p + 18, 4);
    std::memcpy(&out.height, p + 22, 4);
    std::memcpy(&out.bitsPerPixel, p + 28, 2);
    std::memcpy(&out.compression, p + 30, 4);
    return true;
}

// Decode a 24- or 32-bpp BMP to RGBA8888, top row first. Deliberately the
// same arithmetic `ConstrainedTextureCache::decodeBMP()` uses (including its
// alpha=255 substitution for 24 bpp), so what this test reads is what the
// runtime reads.
bool decodeBmpToRgba(const std::vector<char>& data, std::vector<uint8_t>& rgba,
                     int& width, int& height) {
    BmpHeader h;
    if (!parseBmpHeader(data, h)) return false;
    if (h.width <= 0 || h.height == 0) return false;
    if (h.bitsPerPixel != 24 && h.bitsPerPixel != 32) return false;

    bool topDown = h.height < 0;
    int bmpHeight = topDown ? -h.height : h.height;
    width = h.width;
    height = bmpHeight;

    int bytesPerPixel = h.bitsPerPixel / 8;
    int rowSize = ((width * bytesPerPixel + 3) / 4) * 4;
    if (h.dataOffset + static_cast<size_t>(rowSize) * height > data.size()) return false;

    rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint8_t* pixelData = reinterpret_cast<const uint8_t*>(data.data()) + h.dataOffset;

    for (int y = 0; y < height; ++y) {
        int srcY = topDown ? y : (height - 1 - y);
        const uint8_t* row = pixelData + static_cast<size_t>(srcY) * rowSize;
        for (int x = 0; x < width; ++x) {
            size_t dst = (static_cast<size_t>(y) * width + x) * 4;
            if (h.bitsPerPixel == 24) {
                rgba[dst + 0] = row[x * 3 + 2];
                rgba[dst + 1] = row[x * 3 + 1];
                rgba[dst + 2] = row[x * 3 + 0];
                rgba[dst + 3] = 255;
            } else {
                rgba[dst + 0] = row[x * 4 + 2];
                rgba[dst + 1] = row[x * 4 + 1];
                rgba[dst + 2] = row[x * 4 + 0];
                rgba[dst + 3] = row[x * 4 + 3];
            }
        }
    }
    return true;
}

}  // namespace

// ── The shipped file ────────────────────────────────────────────────────────

TEST(ParticleAtlasAlpha, ShippedFileIs32BitAndCarriesAnAlphaChannel) {
    std::vector<char> data = readFile(kAtlasPath);
    ASSERT_FALSE(data.empty())
        << "cannot read " << kAtlasPath;

    BmpHeader h;
    ASSERT_TRUE(parseBmpHeader(data, h)) << "not a BMP: " << kAtlasPath;

    EXPECT_EQ(h.bitsPerPixel, 32)
        << "the particle atlas is " << h.bitsPerPixel << " bpp, so it has no alpha "
           "channel at all -- every tile decodes to alpha=255 and every particle in "
           "the game draws as a flat opaque square (board B225). Regenerate it with "
           "`particle_atlas_builder`, which writes through tools/bmp32_writer.h.";
    EXPECT_EQ(h.width, kTileSize * kAtlasColumns);
    EXPECT_EQ(std::abs(h.height), kTileSize * kAtlasRows);
}

TEST(ParticleAtlasAlpha, EveryDrawnTileHasANonTrivialAlphaHistogram) {
    std::vector<char> data = readFile(kAtlasPath);
    ASSERT_FALSE(data.empty()) << "cannot read " << kAtlasPath;

    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    ASSERT_TRUE(decodeBmpToRgba(data, rgba, w, h)) << "failed to decode " << kAtlasPath;
    ASSERT_EQ(w, kTileSize * kAtlasColumns);
    ASSERT_EQ(h, kTileSize * kAtlasRows);

    for (int tile = 0; tile < kAtlasColumns * kAtlasRows; ++tile) {
        int col = tile % kAtlasColumns;
        int row = tile / kAtlasColumns;
        std::set<int> levels;
        int maxAlpha = 0;
        int minAlpha = 255;
        for (int y = 0; y < kTileSize; ++y) {
            for (int x = 0; x < kTileSize; ++x) {
                int px = col * kTileSize + x;
                int py = row * kTileSize + y;
                int a = rgba[(static_cast<size_t>(py) * w + px) * 4 + 3];
                levels.insert(a);
                maxAlpha = std::max(maxAlpha, a);
                minAlpha = std::min(minAlpha, a);
            }
        }

        if (tile == kUndrawnTile) {
            // Pinned deliberately: the generator draws fifteen shapes and
            // leaves tile 15 at the fill value. If a sixteenth shape is ever
            // added, this is the line that says so.
            EXPECT_EQ(maxAlpha, 0) << "tile " << tile
                << " is expected to be the one undrawn (fully transparent) tile";
            continue;
        }

        EXPECT_GT(levels.size(), 3u) << "tile " << tile
            << " has only " << levels.size() << " distinct alpha level(s) -- its shape "
               "was lost (board B225)";
        EXPECT_EQ(minAlpha, 0) << "tile " << tile
            << " never reaches alpha 0, so it has no transparent surround and draws "
               "as a square";
        EXPECT_GT(maxAlpha, 64) << "tile " << tile << " is never substantially opaque";
    }
}

TEST(ParticleAtlasAlpha, ShippedFileMatchesTheGeneratorPixelForPixel) {
    std::vector<char> data = readFile(kAtlasPath);
    ASSERT_FALSE(data.empty()) << "cannot read " << kAtlasPath;

    std::vector<uint8_t> fileRgba;
    int w = 0, h = 0;
    ASSERT_TRUE(decodeBmpToRgba(data, fileRgba, w, h));

    irr::SIrrlichtCreationParameters params;
    params.DriverType = irr::video::EDT_NULL;
    params.WindowSize = irr::core::dimension2d<irr::u32>(64, 64);
    irr::IrrlichtDevice* device = irr::createDeviceEx(params);
    ASSERT_NE(device, nullptr) << "failed to create EDT_NULL Irrlicht device";
    irr::video::IVideoDriver* driver = device->getVideoDriver();
    ASSERT_NE(driver, nullptr);

    irr::video::IImage* img = EQT::Graphics::generateParticleAtlas(driver);
    ASSERT_NE(img, nullptr);
    ASSERT_EQ(static_cast<int>(img->getDimension().Width), w);
    ASSERT_EQ(static_cast<int>(img->getDimension().Height), h);

    int alphaMismatches = 0;
    int rgbMismatches = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            irr::video::SColor c = img->getPixel(x, y);
            size_t idx = (static_cast<size_t>(y) * w + x) * 4;
            if (fileRgba[idx + 3] != c.getAlpha()) ++alphaMismatches;
            if (fileRgba[idx + 0] != c.getRed() ||
                fileRgba[idx + 1] != c.getGreen() ||
                fileRgba[idx + 2] != c.getBlue()) ++rgbMismatches;
        }
    }
    img->drop();
    device->drop();

    EXPECT_EQ(alphaMismatches, 0)
        << alphaMismatches << " of " << (w * h) << " texels disagree with "
           "generateParticleAtlas() in ALPHA -- the shipped file is not what the "
           "generator produces (board B225)";
    EXPECT_EQ(rgbMismatches, 0)
        << rgbMismatches << " of " << (w * h) << " texels disagree with "
           "generateParticleAtlas() in RGB";
}

// ── The material ────────────────────────────────────────────────────────────

TEST(ParticleAtlasAlpha, ParticleMaterialIsAlphaBlendNotAdditive) {
    irr::video::SMaterial material;
    EQT::Graphics::Environment::ParticleManager::configureParticleMaterial(material, nullptr);

    // EMT_TRANSPARENT_ALPHA_CHANNEL is what
    // COpenGLES2Driver::applyMaterialRenderState() maps to
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) and SOLID3D_FS, which
    // reads its alpha from texture unit 0 alone. If this ever becomes
    // EMT_TRANSPARENT_ADD_COLOR (GL_ONE, GL_ONE) the atlas's RGB would have
    // to be premultiplied instead, and the file the tests above pin would be
    // wrong -- so the two are asserted together on purpose.
    EXPECT_EQ(material.MaterialType, irr::video::EMT_TRANSPARENT_ALPHA_CHANNEL);
    EXPECT_NE(material.MaterialType, irr::video::EMT_TRANSPARENT_ADD_COLOR);
    EXPECT_FALSE(material.ZWriteEnable);
    EXPECT_FALSE(material.Lighting);
    EXPECT_FALSE(material.BackfaceCulling);
    EXPECT_FALSE(material.FogEnable);
    // The material never binds a second texture: there is no shader on this
    // path that would sample an ETC1 `_alpha` companion.
    EXPECT_EQ(material.getTexture(1), nullptr);
}

// ── The pack ────────────────────────────────────────────────────────────────

TEST(ParticleAtlasAlpha, PackEntryCarriesAlphaInBand) {
    std::ifstream probe(kWilleqEqe1, std::ios::binary);
    if (!probe.good()) {
        GTEST_SKIP() << "willeq.eqe1 not present at " << kWilleqEqe1;
    }
    probe.close();

    EQT::Graphics::EQE1Loader loader;
    ASSERT_TRUE(loader.open(kWilleqEqe1)) << "failed to open " << kWilleqEqe1;

    const EQT::Graphics::EQE1Loader::TextureEntry* entry = loader.lookup("particle_atlas");
    ASSERT_NE(entry, nullptr) << "willeq.eqe1 has no particle_atlas entry";

    EXPECT_TRUE(entry->hasAlpha) << "the packed particle atlas reports no alpha";
    EXPECT_EQ(entry->payloadFormat, EQT::Graphics::EQE1_PAYLOAD_RGBA8888)
        << "the packed particle atlas is payload format "
        << static_cast<int>(entry->payloadFormat)
        << ". Both particle draw paths sample ONE texture and read texColor.a from "
           "it, so an ETC1 payload gives alpha=1.0 everywhere however good the "
           "`particle_atlas_alpha` companion is (board B225). Rebuild the pack with "
           "scripts/build_willeq_pack.sh, which passes "
           "--rgba8888-texture particle_atlas.";

    // With alpha in-band there is no companion to sample, and nothing on the
    // particle path would sample one.
    EXPECT_EQ(entry->alphaData, nullptr)
        << "an RGBA8888 entry must not also carry an ETC1 alpha companion";
    EXPECT_EQ(loader.lookup("particle_atlas_alpha"), nullptr)
        << "an RGBA8888 entry must not also carry an ETC1 alpha companion";
}
