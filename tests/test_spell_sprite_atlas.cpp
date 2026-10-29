/*
 * T417 (board B300) / T420 (board B355) / B1124 (#444) / B1127 /
 * B1188 (#507) -- the shipped spell sprite atlas is the official client's
 * own art, frame for frame.
 *
 * T417 shipped one still frame per sprite (the peak-alpha frame). T420 ships
 * the whole animation. B1124 found seven files that are ONE 64x64 picture,
 * not a grid. B1188 (#507) is the pixel-side fix: the client draws its
 * 64x64 textures NATIVE, while fable's 2:1 box into one 32x32 cell
 * quartered their alpha and lit area (peak alpha 255 -> 107 on record 80's
 * pillar, measured 0.53x of the oracle on the b1130 landing gate). The sheet
 * is now an 8x8 grid of 4x4 CELL BLOCKS, one block per sprite, and every
 * picture the block can hold natively is stored 1:1 -- a 64x64 single image
 * in a 2x2 sub-block, a 2x2 sheet's 64 px frames in 2x2 sub-blocks,
 * glowflare02 2:1 in the whole block -- so the assertions below check
 * every frame against its STORED frame region, byte for byte, and assert the
 * 1:1 rule wherever the block allows it.
 *
 * Will ruled 2026-09-06: draw the REAL sprites, "as close to the original as
 * possible". This asserts the shipped BMP against its inputs directly --
 * `data/textures/spell_sprite_atlas.bmp` versus `SpellEffects/spel*.tga` --
 * so a builder change that silently altered a frame, or a hand-edited BMP,
 * is a failure rather than a subtly wrong sprite nobody notices.
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

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>
#include <strings.h>

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_sprite_atlas.h"

#include "sprite_image_reader.h"
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

// B1127: the client is Windows -- spellsnew.edd's glowflare02.tga is
// Glowflare02.tga on disk. Resolve the case-insensitively.
std::string sourcePath(const char* file) {
    DIR* d = opendir(kSourceDir);
    if (!d) return std::string(kSourceDir) + "/" + file;
    std::string hit = std::string(kSourceDir) + "/" + file;
    while (dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, file) == 0) hit = std::string(kSourceDir) + "/" + e->d_name;
    }
    closedir(d);
    return hit;
}

bool sourcesAvailable() {
    std::ifstream f(sourcePath(SpellSpriteAtlas::kFiles[0]), std::ios::binary);
    return f.good();
}

// The builder's resampling rule, re-derived here rather than shared, so the
// test asserts against the SOURCE and not against the builder's own code.
// `src` points at the frame's first texel inside `img` (row stride `stride`
// texels); the result is one storedPx x storedPx RGBA stored frame.
//
// Magnify (or copy) by nearest neighbour; reduce by an ALPHA-WEIGHTED box
// average, because a transparent texel's RGB in these files is arbitrary and
// the additive blend samples RGB directly (B1188's comment, same rule).
std::vector<uint8_t> expectedFrame(const uint8_t* src, int stride,
                                   int frameSize, int storedPx) {
    std::vector<uint8_t> out(static_cast<size_t>(storedPx) * storedPx * 4, 0);

    if (frameSize <= storedPx) {
        const int scale = storedPx / frameSize;
        for (int y = 0; y < storedPx; ++y) {
            for (int x = 0; x < storedPx; ++x) {
                const size_t s =
                    (static_cast<size_t>(y / scale) * stride + (x / scale)) * 4;
                std::memcpy(&out[(static_cast<size_t>(y) * storedPx + x) * 4], &src[s], 4);
            }
        }
        return out;
    }

    const int factor = frameSize / storedPx;
    for (int y = 0; y < storedPx; ++y) {
        for (int x = 0; x < storedPx; ++x) {
            uint32_t sumA = 0, sumR = 0, sumG = 0, sumB = 0;
            for (int by = 0; by < factor; ++by) {
                for (int bx = 0; bx < factor; ++bx) {
                    const size_t s =
                        (static_cast<size_t>(y * factor + by) * stride +
                         (x * factor + bx)) * 4;
                    const uint32_t a = src[s + 3];
                    sumA += a;
                    sumR += static_cast<uint32_t>(src[s + 0]) * a;
                    sumG += static_cast<uint32_t>(src[s + 1]) * a;
                    sumB += static_cast<uint32_t>(src[s + 2]) * a;
                }
            }
            uint8_t* dst = &out[(static_cast<size_t>(y) * storedPx + x) * 4];
            if (sumA != 0) {
                const uint32_t texels = static_cast<uint32_t>(factor) * factor;
                dst[0] = static_cast<uint8_t>(sumR / sumA);
                dst[1] = static_cast<uint8_t>(sumG / sumA);
                dst[2] = static_cast<uint8_t>(sumB / sumA);
                dst[3] = static_cast<uint8_t>(sumA / texels);
            }
        }
    }
    return out;
}

// B1124's census, re-derived from the source rather than shared with the
// builder: additive mass (max(R,G,B) * alpha) inside a band of cell/8 texels
// either side of the sheet's internal seams, as a share of the total, over
// the band's share of the area. A grid pads each frame inside its own cell
// (near 0); one centred picture lies across the seams (above 1).
double seamRatio(const EQT::ToolTGA::Image& img, int grid) {
    const int cell = img.width / grid;
    const int band = std::max(1, cell / 8);
    auto inBand = [&](int v) {
        for (int k = 1; k < grid; ++k) {
            if (v >= k * cell - band && v < k * cell + band) return true;
        }
        return false;
    };
    double total = 0, seam = 0, seamArea = 0;
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const uint8_t* t = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
            const double w = std::max({t[0], t[1], t[2]}) * static_cast<double>(t[3]);
            total += w;
            if (inBand(x) || inBand(y)) {
                seam += w;
                seamArea += 1;
            }
        }
    }
    if (total <= 0) return 0.0;
    return (seam / total) / (seamArea / (static_cast<double>(img.width) * img.height));
}

// B1188: a sprite's frame geometry inside its 4x4 CELL block. Frames tile the
// block row-major in the source sheet's own display order; a frame occupies
// storedSpan cells per side. `cols` is the frame columns per block row.
int frameColsFor(int frames) {
    if (frames == SpellSpriteAtlas::kFramesPerSprite) return SpellSpriteAtlas::kSheetGrid;
    if (frames == SpellSpriteAtlas::kTwoByTwo) return 2;
    return 1;
}
int frameGridFor(int frames) {
    if (frames == SpellSpriteAtlas::kFramesPerSprite) return SpellSpriteAtlas::kSheetGrid;
    if (frames == SpellSpriteAtlas::kTwoByTwo) return 2;
    return 1;
}

// Total alpha of one stored frame of the shipped sheet.
uint64_t frameAlpha(const Bitmap& bmp, int sprite, int frame) {
    int ox = 0, oy = 0;
    SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, frame), ox, oy);
    const int px = SpellSpriteAtlas::storedFramePx(sprite);
    uint64_t total = 0;
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) total += bmp.at(ox + x, oy + y)[3];
    return total;
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

TEST(SpellSpriteAtlasFile, EverySpriteHasVisibleFramesAndReachesAPeak) {
    // T417's failure mode was a sheet of STILLS where seven were blank,
    // because it took frame 0 and these are fade-ins. With the whole
    // animation shipped a blank frame 0 is CORRECT -- so the assertion moves
    // up a level: every sprite must have visible frames, and if it is an
    // animation more than one of its frames must carry alpha.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        uint64_t peak = 0;
        int visibleFrames = 0;
        for (int f = 0; f < frames; ++f) {
            const uint64_t a = frameAlpha(bmp, sprite, f);
            if (a > 0) ++visibleFrames;
            if (a > peak) peak = a;
        }
        EXPECT_GT(peak, 0u) << SpellSpriteAtlas::kFiles[sprite]
                            << ": every one of its frames is transparent";
        if (frames > 1) {
            EXPECT_GT(visibleFrames, 1) << SpellSpriteAtlas::kFiles[sprite]
                                        << ": only " << visibleFrames << " of " << frames
                                        << " frames carry any alpha";
        }
    }
}

TEST(SpellSpriteAtlasFile, TheSingleImageSheetsArePackedWholeInTheirFirstFrame) {
    // B1124 (#444). T420 asserted these files' frame 0 was BLANK -- it was:
    // frame 0 was the empty top-left corner of ONE centred picture, and the
    // sheet drew 16x16 fragments of the star as "frames". B1188: the picture
    // now occupies its stored-frame region whole (a 64x64 picture in its 2x2
    // sub-block, byte-identical to the source -- pinned by
    // NativePicturesAreStoredOneToOneNotBoxed), and the RESERVED cells of its
    // block -- the block cells no frame owns -- are transparent.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    int singles = 0;
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        if (SpellSpriteAtlas::framesFor(sprite) != SpellSpriteAtlas::kSingleImage) continue;
        ++singles;
        const char* file = SpellSpriteAtlas::kFiles[sprite];
        EXPECT_GT(frameAlpha(bmp, sprite, 0), 0u)
            << file << ": the whole picture is not in its stored frame";
        // Every block cell not owned by a frame is fully transparent.
        int bx = 0, by = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::blockBase(sprite), bx, by);
        const int span = SpellSpriteAtlas::storedSpan(sprite);
        for (int dy = 0; dy < SpellSpriteAtlas::kSheetGrid; ++dy) {
            for (int dx = 0; dx < SpellSpriteAtlas::kSheetGrid; ++dx) {
                if (dx < span && dy < span) continue;  // the one frame, top-left
                uint64_t a = 0;
                for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y)
                    for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x)
                        a += bmp.at(bx + dx * SpellSpriteAtlas::kCellSize + x,
                                    by + dy * SpellSpriteAtlas::kCellSize + y)[3];
                EXPECT_EQ(a, 0u) << file << " reserved block cell (" << dx << ","
                                 << dy << ") carries texels -- a fragment of the "
                                    "picture is still packed";
            }
        }
    }
    EXPECT_EQ(singles, 18);
    for (const char* file : {"spelc3.tga", "spelc4.tga", "speld4.tga", "spely3.tga",
                             "spely4.tga", "gena20.bmp", "genb20.bmp", "spelcc.png"}) {
        const int sprite = SpellSpriteAtlas::slotForFile(file);
        ASSERT_GE(sprite, 0) << file;
        EXPECT_EQ(SpellSpriteAtlas::framesFor(sprite), SpellSpriteAtlas::kSingleImage) << file;
    }
}

TEST(SpellSpriteAtlasFile, TheCentreOfASingleImageIsTheCentreOfItsStoredFrame) {
    // The positive form of "packed whole": spelc3's star is centred on its
    // sheet, so its alpha-weighted centroid must sit at the STORED-FRAME
    // centre (32,32 of the 64x64 sub-block), not in a corner as every 16x16
    // fragment's did.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);
    for (const char* file : {"spelc3.tga", "spelc4.tga"}) {
        const int sprite = SpellSpriteAtlas::slotForFile(file);
        ASSERT_GE(sprite, 0);
        int ox = 0, oy = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, 0), ox, oy);
        const int px = SpellSpriteAtlas::storedFramePx(sprite);
        double m = 0, mx = 0, my = 0;
        for (int y = 0; y < px; ++y) {
            for (int x = 0; x < px; ++x) {
                const double a = bmp.at(ox + x, oy + y)[3];
                m += a;
                mx += a * (x + 0.5);
                my += a * (y + 0.5);
            }
        }
        ASSERT_GT(m, 0.0) << file << ": stored frame is empty";
        EXPECT_NEAR(mx / m, px / 2.0, 2.0) << file;
        EXPECT_NEAR(my / m, px / 2.0, 2.0) << file;
    }
}

TEST(SpellSpriteAtlasFile, TheGridOrSingleCensusMatchesTheHeader) {
    // B1124's census over the shipped files, re-measured from the TGAs.
    // The measured gap is wide (grids <= 0.30, single images >= 1.33), so the
    // 1.0 split is not a tuned threshold. Also pins the dimension rule the
    // census found across all 67 spel*.tga: every 64x64 file is one picture.
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kSourceDir;
    }
    int singles = 0;
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const std::string src = sourcePath(SpellSpriteAtlas::kFiles[sprite]);
        EQT::ToolTGA::Image img;
        std::string error;
        ASSERT_TRUE(EQT::ToolSprite::load(src, img, error)) << error;
        // B1127: a declared 2x2 sheet is measured on its own 2x2 seams.
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const double r = seamRatio(img, frames == SpellSpriteAtlas::kTwoByTwo ? 2
                                                                             : SpellSpriteAtlas::kSheetGrid);
        const bool single = r >= 1.0;
        if (single) ++singles;
        EXPECT_EQ(frames == SpellSpriteAtlas::kSingleImage, single)
            << SpellSpriteAtlas::kFiles[sprite] << " " << img.width << "x" << img.height
            << " seam ratio " << r;
        // Every 64x64 file is one picture (B1124's census, true of B1127's too).
        if (img.width == 64) EXPECT_TRUE(single) << SpellSpriteAtlas::kFiles[sprite];
        // B1127: two ring sheets whose rings touch their cell edges read in the
        // gap (spelqa 0.39, spelw1 0.58); classified BY EYE as 4x4 grids
        // (review/B1127 atlas contact sheet) and spellsnew.edd's frame field
        // says 16 for both.
        const std::string f = SpellSpriteAtlas::kFiles[sprite];
        if (f == "spelqa.png" || f == "spelw1.tga") continue;
        EXPECT_TRUE(r <= 0.35 || r >= 1.2)
            << SpellSpriteAtlas::kFiles[sprite] << " seam ratio " << r
            << " fell into the census gap -- classify it by eye before shipping";
    }
    EXPECT_EQ(singles, 18);
}

TEST(SpellSpriteAtlasFile, TheUnusedBlocksAreFullyTransparent) {
    // B1188: the 60 sprites fill 60 of the 64 blocks; blocks 60..63 (the
    // sheet's last row of blocks) are unused and must be all-zero.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int slot = SpellSpriteAtlas::kFileCount; slot < 64; ++slot) {
        int bx = 0, by = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::blockBase(slot), bx, by);
        for (int y = 0; y < 4 * SpellSpriteAtlas::kCellSize; ++y) {
            for (int x = 0; x < 4 * SpellSpriteAtlas::kCellSize; ++x) {
                ASSERT_EQ(bmp.at(bx + x, by + y)[3], 0)
                    << "unused block " << slot << " has a texel at (" << x << ","
                    << y << ")";
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
// Every BLOCK equals its source frames -- the 128x128 region, byte for byte.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasFile, EveryBlockEqualsItsSourceFrames) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kSourceDir;
    }

    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const std::string src = sourcePath(SpellSpriteAtlas::kFiles[sprite]);

        EQT::ToolTGA::Image img;
        std::string error;
        ASSERT_TRUE(EQT::ToolSprite::load(src, img, error)) << error;
        ASSERT_EQ(img.width, img.height) << src;
        ASSERT_EQ(img.width % SpellSpriteAtlas::kSheetGrid, 0) << src;

        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const int grid = frameGridFor(frames);
        const int cols = frameColsFor(frames);
        const int frameSize = img.width / grid;
        const int storedPx = SpellSpriteAtlas::storedFramePx(sprite);
        const int span = SpellSpriteAtlas::storedSpan(sprite);

        int bx = 0, by = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::blockBase(sprite), bx, by);

        for (int f = 0; f < frames; ++f) {
            const int fx = (f % cols) * frameSize;
            const int fy = (f / cols) * frameSize;
            const std::vector<uint8_t> frame = expectedFrame(
                img.rgba.data() + (static_cast<size_t>(fy) * img.width + fx) * 4,
                img.width, frameSize, storedPx);
            // The frame's stored region, through the header's own cellFor.
            int ox = 0, oy = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, f), ox, oy);
            for (int y = 0; y < storedPx; ++y) {
                for (int x = 0; x < storedPx; ++x) {
                    const uint8_t* w = frame.data() +
                                       (static_cast<size_t>(y) * storedPx + x) * 4;
                    const uint8_t* got = bmp.at(ox + x, oy + y);
                    ASSERT_EQ(std::memcmp(w, got, 4), 0)
                        << SpellSpriteAtlas::kFiles[sprite] << " frame " << f
                        << " texel (" << x << "," << y << ")";
                }
            }
        }
        // The block cells no frame owns are transparent.
        const int frameRows = (frames + cols - 1) / cols;
        for (int dy = 0; dy < SpellSpriteAtlas::kSheetGrid; ++dy) {
            for (int dx = 0; dx < SpellSpriteAtlas::kSheetGrid; ++dx) {
                const int frameCol = dx / span;
                const int frameRow = dy / span;
                if (frameCol < cols && frameRow < frameRows) continue;
                uint64_t a = 0;
                for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y)
                    for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x)
                        a += bmp.at(bx + dx * SpellSpriteAtlas::kCellSize + x,
                                    by + dy * SpellSpriteAtlas::kCellSize + y)[3];
                EXPECT_EQ(a, 0u) << SpellSpriteAtlas::kFiles[sprite]
                                 << " reserved block cell (" << dx << "," << dy << ")";
            }
        }
    }
}

TEST(SpellSpriteAtlasFile, TheFramesOfASpriteAreNotAllTheSame) {
    // Grid sheets only (B1124). T409 recorded these sheets as one repeated frame; T417 measured 5 to 16
    // DISTINCT frames in every one. If this goes red the builder has stopped
    // walking the source grid and is packing the same frame over and over --
    // which would leave the sheet the same size and still a still.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        if (frames == 1) continue;  // B1124: one picture
        bool differs = false;
        int ax = 0, ay = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, 0), ax, ay);
        const int px = SpellSpriteAtlas::storedFramePx(sprite);
        for (int f = 1; f < frames && !differs; ++f) {
            int bx = 0, by = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, f), bx, by);
            for (int y = 0; y < px && !differs; ++y) {
                if (std::memcmp(bmp.at(ax, ay + y), bmp.at(bx, by + y), px * 4) != 0) {
                    differs = true;
                }
            }
        }
        EXPECT_TRUE(differs) << SpellSpriteAtlas::kFiles[sprite]
                             << ": all " << frames << " frames are byte-identical";
    }
}

// ---------------------------------------------------------------------------
// B1188 (#507): the client's rule -- a picture the block can hold NATIVE is
// stored 1:1, byte for byte. This is the fix: record 80's pillar (spelcf,
// w = h = 6.0) drew a 2:1 box (peak alpha 255 -> 107, lit80 area /5) and the
// b1130 landing gate measured 0.53x of the oracle's pixels on 38 of 40 rows.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasFile, NativePicturesAreStoredOneToOneNotBoxed) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kSourceDir;
    }
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    int checked = 0;
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        const std::string src = sourcePath(SpellSpriteAtlas::kFiles[sprite]);
        EQT::ToolTGA::Image img;
        std::string error;
        ASSERT_TRUE(EQT::ToolSprite::load(src, img, error)) << error;

        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const int grid = frameGridFor(frames);
        const int cols = frameColsFor(frames);
        const int frameSize = img.width / grid;
        const int storedPx = SpellSpriteAtlas::storedFramePx(sprite);

        // The 1:1 rule holds exactly where frameSize == storedPx: a single
        // 64x64 picture (2x2 sub-block), a 2x2 sheet's 64 px frames. The
        // documented losses are the 4x4 grids (64 -> 32) and glowflare02
        // (256 -> 128, the block 2:1).
        if (frameSize != storedPx) continue;
        ++checked;

        int bx = 0, by = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::blockBase(sprite), bx, by);
        for (int f = 0; f < frames; ++f) {
            const int fx = (f % cols) * frameSize;
            const int fy = (f / cols) * frameSize;
            for (int y = 0; y < frameSize; ++y) {
                for (int x = 0; x < frameSize; ++x) {
                    const uint8_t* w =
                        img.rgba.data() + (static_cast<size_t>(fy + y) * img.width + fx + x) * 4;
                    const uint8_t* got = bmp.at(bx + (f % cols) * frameSize + x,
                                                by + (f / cols) * frameSize + y);
                    ASSERT_EQ(std::memcmp(w, got, 4), 0)
                        << SpellSpriteAtlas::kFiles[sprite] << " frame " << f << " ("
                        << x << "," << y << "): the native " << frameSize
                        << "px frame must be stored 1:1 (got " << (int)got[0] << ","
                        << (int)got[1] << "," << (int)got[2] << "," << (int)got[3]
                        << ", want " << (int)w[0] << "," << (int)w[1] << "," << (int)w[2]
                        << "," << (int)w[3] << ")";
                }
            }
        }
    }
    // 16 single 64x64 pictures + the two 2x2 sheets (their four 64 px
    // frames). spellw02 (64x32x16, 32 px cells) is grid-stored, not native.
    EXPECT_EQ(checked, 18);
}

TEST(SpellSpriteAtlasFile, TheTwoOracleMeasuredSpritesAreTheirOwnFiles) {
    // Frost Bolt (54) record 23's Target stage is GEND10 + GENU10 x2, i.e.
    // speld2.tga and spelu2.tga -- the two names T389 measured against
    // the official client. They must occupy their own distinct frames.
    const int d2 = SpellSpriteAtlas::slotForFile("speld2.tga");
    const int u2 = SpellSpriteAtlas::slotForFile("spelu2.tga");
    ASSERT_GE(d2, 0);
    ASSERT_GE(u2, 0);
    EXPECT_NE(d2, u2);

    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kSourceDir;
    }

    // ...and neither frame may be blank, which is what the frame 10 frame
    // rule gave spelu2 (12% of its peak alpha).
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);
    for (int sprite : {d2, u2}) {
        uint64_t peak = 0;
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        for (int f = 0; f < frames; ++f) {
            peak = std::max(peak, frameAlpha(bmp, sprite, f));
        }
        // A quarter of T417's threshold: the frame is a quarter of the area
        // it was (32x32, not 64x64), so the same picture carries a quarter
        // of the alpha.
        EXPECT_GT(peak, 25000u) << SpellSpriteAtlas::kFiles[sprite]
                                << " never becomes visible in any frame";
    }
}
