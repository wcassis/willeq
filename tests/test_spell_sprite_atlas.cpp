/*
 * T417 (board B300) -- the shipped spell sprite atlas is the official
 * client's own art, cell for cell.
 *
 * Will ruled 2026-09-06: draw the REAL sprites, "as close to the original as
 * possible". This asserts the shipped BMP against its inputs directly --
 * `data/textures/spell_sprite_atlas.bmp` versus
 * `SpellEffects/spel*.tga` -- so a builder change that silently altered a
 * cell, or a hand-edited BMP, is a failure rather than a subtly wrong sprite
 * nobody notices.
 *
 * Reads files only: the BMP and the TGAs. No device, no GPU, no renderer.
 * The TGA decode is the same header-only `tools/tga_reader.h` the builder
 * uses, which is the point -- the test re-derives the expected pixels from
 * the source rather than from a golden nobody can re-check.
 *
 * If the EQ client tree is not mounted the sprite cases SKIP (the shipped
 * BMP's own structure is still asserted), matching how every other test that
 * needs `/mnt/projects/EverQuestP1999` behaves.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include "tga_reader.h"

using namespace EQT::Graphics::Environment;

namespace {

const char* kSourceDir = "/mnt/projects/EverQuestP1999/SpellEffects";

std::string atlasPath() {
    return std::string(WILLEQ_SOURCE_DIR) + "/data/textures/spell_sprite_atlas.bmp";
}

// A decoded 32-bit BMP: top-down RGBA, whatever the file's own row order.
struct Bitmap {
    int width = 0;
    int height = 0;
    int bitCount = 0;
    std::vector<uint8_t> rgba;
    bool ok = false;

    const uint8_t* at(int x, int y) const {
        return rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
    }
};

uint32_t rd32(const std::vector<uint8_t>& d, size_t off) {
    uint32_t v = 0;
    std::memcpy(&v, d.data() + off, 4);
    return v;
}

Bitmap loadBmp(const std::string& path) {
    Bitmap bmp;
    std::ifstream f(path, std::ios::binary);
    if (!f) return bmp;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return bmp;

    const uint32_t dataOffset = rd32(d, 10);
    const int32_t w = static_cast<int32_t>(rd32(d, 18));
    const int32_t h = static_cast<int32_t>(rd32(d, 22));
    uint16_t bits = 0;
    std::memcpy(&bits, d.data() + 28, 2);

    bmp.width = w;
    bmp.height = h < 0 ? -h : h;
    bmp.bitCount = bits;
    if (bits != 32 || w <= 0 || bmp.height <= 0) return bmp;

    const bool topDown = h < 0;
    const size_t need = dataOffset + static_cast<size_t>(w) * bmp.height * 4;
    if (d.size() < need) return bmp;

    bmp.rgba.resize(static_cast<size_t>(w) * bmp.height * 4);
    for (int y = 0; y < bmp.height; ++y) {
        const int srcRow = topDown ? y : (bmp.height - 1 - y);
        const uint8_t* src = d.data() + dataOffset +
                             static_cast<size_t>(srcRow) * w * 4;
        uint8_t* dst = bmp.rgba.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            // BMP stores B,G,R,A; the atlas is compared as RGBA.
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    bmp.ok = true;
    return bmp;
}

bool sourcesAvailable() {
    std::ifstream f(std::string(kSourceDir) + "/" + SpellSpriteAtlas::kFiles[0],
                    std::ios::binary);
    return f.good();
}

// The builder's rule, re-derived here rather than shared: the frame with the
// greatest total alpha, ties broken by the lowest index.
int peakFrame(const EQT::ToolTGA::Image& img, int frameSize) {
    int best = 0;
    uint64_t bestAlpha = 0;
    for (int f = 0; f < 16; ++f) {
        const int fx = (f % 4) * frameSize;
        const int fy = (f / 4) * frameSize;
        uint64_t total = 0;
        for (int row = 0; row < frameSize; ++row) {
            const uint8_t* src =
                img.rgba.data() + (static_cast<size_t>(fy + row) * img.width + fx) * 4;
            for (int col = 0; col < frameSize; ++col) total += src[col * 4 + 3];
        }
        if (total > bestAlpha) {
            bestAlpha = total;
            best = f;
        }
    }
    return best;
}

}  // namespace

// ---------------------------------------------------------------------------
// The shipped file's own structure.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasFile, IsShippedAt32BitWithTheDeclaredGeometry) {
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok) << "cannot decode " << atlasPath()
                        << " (bitCount=" << bmp.bitCount << ")";
    EXPECT_EQ(bmp.bitCount, 32);
    EXPECT_EQ(bmp.width, SpellSpriteAtlas::kWidth);
    EXPECT_EQ(bmp.height, SpellSpriteAtlas::kHeight);
}

TEST(SpellSpriteAtlasFile, EveryDeclaredCellCarriesPixels) {
    // The defect this catches is precise and MEASURED: T417's first build
    // took frame 0 of each sheet, and seven cells came out completely blank
    // because these are fade-in animations whose first frame is empty.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int slot = 0; slot < SpellSpriteAtlas::kFileCount; ++slot) {
        int ox = 0, oy = 0;
        SpellSpriteAtlas::cellOrigin(slot, ox, oy);
        int visible = 0;
        for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
            for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                if (bmp.at(ox + x, oy + y)[3] != 0) ++visible;
            }
        }
        EXPECT_GT(visible, 0) << "cell " << slot << " (" << SpellSpriteAtlas::kFiles[slot]
                              << ") is fully transparent";
    }
}

TEST(SpellSpriteAtlasFile, UnusedCellsAreFullyTransparent) {
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int slot = SpellSpriteAtlas::kFileCount; slot < SpellSpriteAtlas::kCapacity;
         ++slot) {
        int ox = 0, oy = 0;
        SpellSpriteAtlas::cellOrigin(slot, ox, oy);
        for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
            for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                ASSERT_EQ(bmp.at(ox + x, oy + y)[3], 0)
                    << "unused cell " << slot << " has a texel at (" << x << "," << y << ")";
            }
        }
    }
}

TEST(SpellSpriteAtlasFile, TheSheetCarriesARealAlphaChannel) {
    // The B225 failure mode -- a sheet shipped with alpha 255 everywhere,
    // which draws every sprite as an opaque square.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    bool levels[256] = {false};
    int distinct = 0;
    for (size_t i = 3; i < bmp.rgba.size(); i += 4) {
        if (!levels[bmp.rgba[i]]) {
            levels[bmp.rgba[i]] = true;
            ++distinct;
        }
    }
    EXPECT_GT(distinct, 16) << "only " << distinct << " alpha levels in the whole sheet";
}

// ---------------------------------------------------------------------------
// Every cell equals the source sprite's peak frame, alpha included.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasFile, EveryCellEqualsItsSourceFrame) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kSourceDir;
    }

    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int slot = 0; slot < SpellSpriteAtlas::kFileCount; ++slot) {
        const std::string src =
            std::string(kSourceDir) + "/" + SpellSpriteAtlas::kFiles[slot];

        EQT::ToolTGA::Image img;
        std::string error;
        ASSERT_TRUE(EQT::ToolTGA::load(src, img, error)) << error;
        ASSERT_EQ(img.width, img.height) << src;
        ASSERT_EQ(img.width % 4, 0) << src;

        const int frameSize = img.width / 4;
        ASSERT_EQ(SpellSpriteAtlas::kCellSize % frameSize, 0) << src;
        const int scale = SpellSpriteAtlas::kCellSize / frameSize;

        const int frame = peakFrame(img, frameSize);
        std::vector<uint8_t> expected;
        ASSERT_TRUE(img.crop((frame % 4) * frameSize, (frame / 4) * frameSize,
                             frameSize, frameSize, expected));

        int ox = 0, oy = 0;
        SpellSpriteAtlas::cellOrigin(slot, ox, oy);

        for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
            for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                const uint8_t* want =
                    expected.data() +
                    (static_cast<size_t>(y / scale) * frameSize + (x / scale)) * 4;
                const uint8_t* got = bmp.at(ox + x, oy + y);
                ASSERT_EQ(std::memcmp(want, got, 4), 0)
                    << SpellSpriteAtlas::kFiles[slot] << " cell " << slot
                    << " texel (" << x << "," << y << ") frame " << frame;
            }
        }
    }
}

TEST(SpellSpriteAtlasFile, TheTwoOracleMeasuredSpritesAreTheirOwnFiles) {
    // Frost Bolt (54) record 23's Target stage is GEND10 + GENU10 x2, i.e.
    // speld2.tga and spelu2.tga -- the two names T389 measured against the
    // official client. They must occupy their own distinct cells.
    const int d2 = SpellSpriteAtlas::slotForFile("speld2.tga");
    const int u2 = SpellSpriteAtlas::slotForFile("spelu2.tga");
    ASSERT_GE(d2, 0);
    ASSERT_GE(u2, 0);
    EXPECT_NE(d2, u2);

    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kSourceDir;
    }

    // ...and neither cell may be blank, which is what the frame-0 rule gave
    // spelu2 (12% of its peak alpha).
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);
    for (int slot : {d2, u2}) {
        int ox = 0, oy = 0;
        SpellSpriteAtlas::cellOrigin(slot, ox, oy);
        uint64_t alpha = 0;
        for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
            for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                alpha += bmp.at(ox + x, oy + y)[3];
            }
        }
        EXPECT_GT(alpha, 100000u) << "cell " << slot << " is nearly invisible";
    }
}
