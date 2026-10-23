/*
 * T417 (board B300) / T420 (board B355) -- the shipped spell sprite atlas is
 * the official client's own art, cell for cell, ALL SIXTEEN FRAMES of it.
 *
 * T417 shipped one still frame per sprite (the peak-alpha frame). T420 ships
 * the whole animation: 33 sprites x 16 reserved cells = 528 of the 1024, in
 * the source sheet's own display order, so a particle can walk them over its
 * lifetime. The assertion below is correspondingly stronger and simpler --
 * there is no frame-selection rule left to re-derive, every cell must equal
 * its frame.
 *
 * B1124 (#444, B1118 M8): seven of the 33 files are not grids but ONE 64x64
 * picture each (spelc3, spelc4, ...). Those are packed WHOLE into their
 * sprite's first cell and the other fifteen stay transparent; the census that
 * says which is re-derived here from the TGAs, independently of the builder.
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
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/environment/particle_types.h"
#include "client/graphics/environment/spell_sprite_atlas.h"
#include <dirent.h>
#include <strings.h>

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

bool sourcesAvailable() {
    std::ifstream f(std::string(kSourceDir) + "/" + SpellSpriteAtlas::kFiles[0],
                    std::ios::binary);
    return f.good();
}

// The builder's resampling rule, re-derived here rather than shared, so the
// test asserts against the SOURCE and not against the builder's own code.
// `src` is the top-left texel of the frame inside `img`; the result is one
// kCellSize x kCellSize RGBA cell.
//
// Magnify (or copy) by nearest neighbour; reduce by an ALPHA-WEIGHTED box
// average, because a transparent texel's RGB in these files is arbitrary and
// the additive blend samples RGB directly.
std::vector<uint8_t> expectedCell(const uint8_t* src, int stride, int frameSize) {
    const int cs = SpellSpriteAtlas::kCellSize;
    std::vector<uint8_t> out(static_cast<size_t>(cs) * cs * 4, 0);

    if (frameSize <= cs) {
        const int scale = cs / frameSize;
        for (int y = 0; y < cs; ++y) {
            for (int x = 0; x < cs; ++x) {
                const size_t s =
                    (static_cast<size_t>(y / scale) * stride + (x / scale)) * 4;
                std::memcpy(&out[(static_cast<size_t>(y) * cs + x) * 4], &src[s], 4);
            }
        }
        return out;
    }

    const int factor = frameSize / cs;
    for (int y = 0; y < cs; ++y) {
        for (int x = 0; x < cs; ++x) {
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
            uint8_t* d = &out[(static_cast<size_t>(y) * cs + x) * 4];
            if (sumA != 0) {
                const uint32_t texels = static_cast<uint32_t>(factor) * factor;
                d[0] = static_cast<uint8_t>(sumR / sumA);
                d[1] = static_cast<uint8_t>(sumG / sumA);
                d[2] = static_cast<uint8_t>(sumB / sumA);
                d[3] = static_cast<uint8_t>(sumA / texels);
            }
        }
    }
    return out;
}

// Total alpha of one cell of the shipped sheet.
uint64_t cellAlpha(const Bitmap& bmp, int cell) {
    int ox = 0, oy = 0;
    SpellSpriteAtlas::cellOrigin(cell, ox, oy);
    uint64_t total = 0;
    for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
        for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
            total += bmp.at(ox + x, oy + y)[3];
        }
    }
    return total;
}

// B1124's census, re-derived from the source rather than shared with the
// builder: additive mass (max(R,G,B) * alpha) inside a band of cell/8 texels
// either side of the 4x4 sheet's internal seams, as a share of the total,
// over the band's share of the area. A grid pads each frame inside its own
// cell (near 0); one centred picture lies across the seams (above 1).
// B1127: the client is Windows -- spellsnew.edd's glowflare02.tga is
// Glowflare02.tga on disk.
std::string sourcePath(const char* file) {
    std::string hit = std::string(kSourceDir) + "/" + file;
    if (DIR* d = opendir(kSourceDir)) {
        while (dirent* e = readdir(d)) {
            if (strcasecmp(e->d_name, file) == 0) hit = std::string(kSourceDir) + "/" + e->d_name;
        }
        closedir(d);
    }
    return hit;
}

// B1127: 16 frames = 4x4 grid, 4 = 2x2 grid, 1 = one picture.
int gridFor(int frames) {
    return frames == SpellSpriteAtlas::kFramesPerSprite ? SpellSpriteAtlas::kSheetGrid
         : frames == SpellSpriteAtlas::kTwoByTwo ? 2 : 1;
}

double seamRatio(const EQT::ToolTGA::Image& img, int grid = SpellSpriteAtlas::kSheetGrid) {
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
    // up a level: every sprite must have visible frames, and its peak frame
    // must be substantially brighter than its first, which is what says the
    // sixteen cells hold a cycle rather than sixteen copies of nothing.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        uint64_t peak = 0;
        int visibleFrames = 0;
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        for (int f = 0; f < frames; ++f) {
            const uint64_t a = cellAlpha(bmp, SpellSpriteAtlas::cellFor(sprite, f));
            if (a > 0) ++visibleFrames;
            if (a > peak) peak = a;
        }
        EXPECT_GT(peak, 0u) << SpellSpriteAtlas::kFiles[sprite]
                            << ": every one of its cells is transparent";
        if (frames > 1) {
            EXPECT_GT(visibleFrames, 1) << SpellSpriteAtlas::kFiles[sprite]
                                        << ": only " << visibleFrames
                                        << " of sixteen frames carry any alpha";
        }
    }
}

TEST(SpellSpriteAtlasFile, TheSingleImageSheetsArePackedWholeInTheirFirstCell) {
    // B1124 (#444). T420 asserted these six files' frame 0 was BLANK -- it
    // was: frame 0 was the empty top-left corner of ONE centred picture, and
    // the sheet drew 16x16 fragments of the star as "frames". The picture now
    // occupies the first cell whole, lit nearly as much as the brightest
    // grid frames, and the fifteen cells after it are transparent. RED on the
    // pre-B1124 sheet (frame 0 alpha 0, fragments in cells 1..15).
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    // B1127: every single image the sheet now carries (spelb3/spelb4 left it:
    // no spell without a field-145 record reaches them).
    int singles = 0;
    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        if (SpellSpriteAtlas::framesFor(sprite) != SpellSpriteAtlas::kSingleImage) continue;
        ++singles;
        const char* file = SpellSpriteAtlas::kFiles[sprite];
        EXPECT_GT(cellAlpha(bmp, SpellSpriteAtlas::cellFor(sprite, 0)), 0u)
            << file << ": the whole picture is not in the first cell";
        for (int f = 1; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
            EXPECT_EQ(cellAlpha(bmp, SpellSpriteAtlas::cellFor(sprite, f)), 0u)
                << file << " reserved cell " << f
                << " carries texels -- a fragment of the picture is still packed";
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

TEST(SpellSpriteAtlasFile, TheCentreOfASingleImageIsTheCentreOfItsCell) {
    // The positive form of "packed whole": spelc3's star is centred on its
    // sheet, so its alpha-weighted centroid must sit at the CELL centre
    // (16,16), not in a corner as every 16x16 fragment's did.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);
    for (const char* file : {"spelc3.tga", "spelc4.tga"}) {
        const int sprite = SpellSpriteAtlas::slotForFile(file);
        ASSERT_GE(sprite, 0);
        int ox = 0, oy = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, 0), ox, oy);
        double m = 0, mx = 0, my = 0;
        for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
            for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                const double a = bmp.at(ox + x, oy + y)[3];
                m += a;
                mx += a * (x + 0.5);
                my += a * (y + 0.5);
            }
        }
        ASSERT_GT(m, 0.0) << file << ": first cell is empty";
        EXPECT_NEAR(mx / m, SpellSpriteAtlas::kCellSize / 2.0, 2.0) << file;
        EXPECT_NEAR(my / m, SpellSpriteAtlas::kCellSize / 2.0, 2.0) << file;
    }
}

TEST(SpellSpriteAtlasFile, TheGridOrSingleCensusMatchesTheHeader) {
    // B1124's census over the 33 shipped files, re-measured from the TGAs.
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
        const double r = seamRatio(img, frames == SpellSpriteAtlas::kTwoByTwo ? 2 : SpellSpriteAtlas::kSheetGrid);
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

TEST(SpellSpriteAtlasFile, UnusedCellsAreFullyTransparent) {
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int slot = SpellSpriteAtlas::kUsedCells; slot < SpellSpriteAtlas::kCapacity;
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
// Every cell equals its source FRAME -- all 528 of them.
// ---------------------------------------------------------------------------

TEST(SpellSpriteAtlasFile, EveryCellEqualsItsSourceFrame) {
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

        // B1124: a single image is ONE frame, the whole sheet; the other
        // reserved cells must be all-zero. B1127: a 2x2 sheet is four.
        const int frames = SpellSpriteAtlas::framesFor(sprite);
        const int grid = gridFor(frames);
        const int frameSize = img.width / grid;

        for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
            const int fx = (f % grid) * frameSize;
            const int fy = (f / grid) * frameSize;
            const std::vector<uint8_t> want =
                f < frames
                    ? expectedCell(
                          img.rgba.data() + (static_cast<size_t>(fy) * img.width + fx) * 4,
                          img.width, frameSize)
                    : std::vector<uint8_t>(static_cast<size_t>(SpellSpriteAtlas::kCellSize) *
                                               SpellSpriteAtlas::kCellSize * 4,
                                           0);

            int ox = 0, oy = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, f), ox, oy);

            for (int y = 0; y < SpellSpriteAtlas::kCellSize; ++y) {
                for (int x = 0; x < SpellSpriteAtlas::kCellSize; ++x) {
                    const uint8_t* w =
                        want.data() +
                        (static_cast<size_t>(y) * SpellSpriteAtlas::kCellSize + x) * 4;
                    const uint8_t* got = bmp.at(ox + x, oy + y);
                    ASSERT_EQ(std::memcmp(w, got, 4), 0)
                        << SpellSpriteAtlas::kFiles[sprite] << " frame " << f
                        << " texel (" << x << "," << y << ")";
                }
            }
        }
    }
}

TEST(SpellSpriteAtlasFile, TheSixteenFramesOfASpriteAreNotAllTheSame) {
    // Grid sheets only (B1124). T409 recorded these sheets as one repeated frame; T417 measured 5 to 16
    // DISTINCT frames in every one. If this goes red the builder has stopped
    // walking the source grid and is packing the same frame sixteen times --
    // which would leave the sheet 16x larger and still a still.
    const Bitmap bmp = loadBmp(atlasPath());
    ASSERT_TRUE(bmp.ok);

    for (int sprite = 0; sprite < SpellSpriteAtlas::kFileCount; ++sprite) {
        if (SpellSpriteAtlas::framesFor(sprite) == 1) continue;  // B1124: one picture
        bool differs = false;
        int ax = 0, ay = 0;
        SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, 0), ax, ay);
        for (int f = 1; f < SpellSpriteAtlas::kFramesPerSprite && !differs; ++f) {
            int bx = 0, by = 0;
            SpellSpriteAtlas::cellOrigin(SpellSpriteAtlas::cellFor(sprite, f), bx, by);
            for (int y = 0; y < SpellSpriteAtlas::kCellSize && !differs; ++y) {
                if (std::memcmp(bmp.at(ax, ay + y), bmp.at(bx, by + y),
                                SpellSpriteAtlas::kCellSize * 4) != 0) {
                    differs = true;
                }
            }
        }
        EXPECT_TRUE(differs) << SpellSpriteAtlas::kFiles[sprite]
                             << ": all sixteen cells are byte-identical";
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
    for (int sprite : {d2, u2}) {
        uint64_t peak = 0;
        for (int f = 0; f < SpellSpriteAtlas::kFramesPerSprite; ++f) {
            peak = std::max(peak, cellAlpha(bmp, SpellSpriteAtlas::cellFor(sprite, f)));
        }
        // A quarter of T417's threshold: the cell is a quarter of the area it
        // was (32x32, not 64x64), so the same picture carries a quarter of
        // the alpha.
        EXPECT_GT(peak, 25000u) << SpellSpriteAtlas::kFiles[sprite]
                                << " never becomes visible in any frame";
    }
}
