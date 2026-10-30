// tools/spell_sprite_atlas_builder.cpp -- packs the official client's own
// spell sprite art into data/textures/spell_sprite_atlas.bmp.
//
// T417, board B300. Will ruled 2026-09-06: draw the REAL sprites, "as close to
// the original as possible". The 16-tile procedural particle atlas cannot
// represent a sword (spelg1), a flaming skull (speli1) or a row of skulls
// (spelk2), and the skull is the commonest sprite name in the whole of
// `spells.eff` -- so the sprites the game's own data names now ship as pixels.
//
// INPUT. `<eq-path>/SpellEffects/spel*.tga`, the 33 files listed in
// `include/client/graphics/environment/spell_sprite_atlas.h`. That header is
// the ONE source of truth for the layout; this tool and `SpellEffectsConfig`
// both include it, so the sheet and the client's addressing of it cannot
// drift.
//
// GRID OR SINGLE IMAGE (B1124, #444, B1118 M8). The 256x256 and 128x128
// files are 4x4 frame grids; the 64x64 ones are ONE picture each (spelc3 a
// yellow-white star, spelc4 an orange flare), which T420 cut into sixteen
// 16x16 fragments and played as an "animation". The header's `kSheetFrames`
// declares which is which (16 or 1); this tool RE-MEASURES it with
// `seamRatio()` -- additive mass on the 4x4 seams over their share of the
// area: a grid pads every frame inside its own cell (<= 0.30 on the 33
// shipped files), one centred picture puts its body across the seams
// (>= 1.33) -- and refuses (exit 1) when the two disagree. A single image is
// packed WHOLE into its sprite's first cell; its other fifteen reserved
// cells stay transparent.
//
// FRAME EXTRACTION -- and a MEASURED correction to T409. Every GRID
// `spel*.tga` is a 4x4 sheet. T409 recorded that the sixteen frames are one repeated
// frame; they are NOT. T417 hashed all sixteen frames of all 67 files and
// found 5 to 16 DISTINCT frames in every one: these are animations. `spelg1`
// is a sword rotating through sixteen headings; `speln1` is a mottled disc
// whose noise churns.
//
// T417 shipped ONE frame of each -- the peak-alpha frame, the fully-formed
// sprite -- because `UnifiedParticle` had nowhere to put a frame index.
// T420 (board B355) ships ALL SIXTEEN, in the sheet's own display order, and
// the client walks them over a particle's lifetime. The peak-frame rule is
// gone with the still it produced; nothing is selected and nothing is
// dropped, so the packing is trivially faithful and there is no rule left to
// get wrong. Duplicate frames within a sheet are packed as duplicates: they
// are the animation holding, and dropping them would retime it.
//
// SCALING. The grid sheets are 256x256 (64x64 frames) and 128x128 (32x32 or
// 64x64); a single image is 64x64 (glowflare02 at 256x256). Cells are 32x32
// and the sheet is an 8x8 grid of 4x4 CELL BLOCKS, one block (128x128 px)
// per sprite (see the header), so each frame is resampled by an exact
// integer factor into its stored frame (kStoredFramePx, derived and
// re-checked here):
//   * 16x16 -> 32x32: magnified 2x by NEAREST-neighbour replication.
//   * 32x32 -> 32x32: copied.
//   * 64x64 -> 64x64: COPIED 1:1 (B1188: the client draws these native
//     64x64 textures; a 2:1 box into one cell quartered their alpha and lit
//     area and was the b1130 landing pixel gap, measured 0.53x).
//   * 64x64 -> 32x32: reduced 2:1 by an ALPHA-WEIGHTED box average (grids,
//     where sixteen native frames would not fit their block).
//   * 256x256 -> 128x128: reduced 2:1 (glowflare02, the whole block).
// Plain
// RGB averaging is wrong for a reduction: a transparent texel's RGB in
// these files is arbitrary (often black), and the additive blend samples RGB
// directly, so an unweighted average darkens or fringes every sprite
// edge. Weighting each texel by its own alpha averages only the colour
// that is actually there.
// All of it is integer arithmetic on `uint32_t` -- no floating point, no
// signed overflow -- so the output is bit-identical at every optimisation
// level (board B268's rule; a shipped asset that depends on
// CMAKE_BUILD_TYPE makes `TexturesReproducible` a coin toss).
//
// ALL-OR-NOTHING. A file that cannot be read, is not a square sheet of the
// expected size, or contradicts its kSheetFrames declaration is a hard exit 1, never a silently degraded sheet (Will's
// standing EQE1 rule, and `ui_atlas_builder`'s precedent).

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <strings.h>

#include "bmp32_writer.h"
#include "sprite_image_reader.h"

#include "client/graphics/environment/spell_sprite_atlas.h"

namespace Atlas = EQT::Graphics::Environment::SpellSpriteAtlas;

namespace {

// Fully transparent, so an unused cell contributes nothing however it is
// sampled.
std::vector<uint8_t> g_pixels(static_cast<size_t>(Atlas::kWidth) * Atlas::kHeight * 4, 0);

// Resample one source frame into its STORED frame region: a `storedPx` x
// `storedPx` square starting at the top-left pixel of cell `cell` (B1188:
// single images keep their native 64x64 in a 2x2 sub-block; glowflare02
// keeps 128x128 in the whole block; grids box-reduce to their 32x32 cell).
// `frameSize` is the source frame's edge; `src` points at its top-left texel
// inside `img`, whose row stride is `stride` texels.
void blitFrame(int cell, const uint8_t* src, int stride, int frameSize, int storedPx) {
    int originX = 0, originY = 0;
    Atlas::cellOrigin(cell, originX, originY);

    if (frameSize <= storedPx) {
        // Magnify (or copy) by an exact integer factor, nearest neighbour.
        const int scale = storedPx / frameSize;
        for (int y = 0; y < storedPx; ++y) {
            const int srcY = y / scale;
            for (int x = 0; x < storedPx; ++x) {
                const int srcX = x / scale;
                const size_t s = (static_cast<size_t>(srcY) * stride + srcX) * 4;
                const size_t d =
                    (static_cast<size_t>(originY + y) * Atlas::kWidth + (originX + x)) * 4;
                std::memcpy(&g_pixels[d], &src[s], 4);
            }
        }
        return;
    }

    // Reduce by an exact integer factor with an alpha-weighted box average.
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
            const uint32_t texels = static_cast<uint32_t>(factor) * factor;
            const size_t d =
                (static_cast<size_t>(originY + y) * Atlas::kWidth + (originX + x)) * 4;
            if (sumA == 0) {
                g_pixels[d + 0] = 0;
                g_pixels[d + 1] = 0;
                g_pixels[d + 2] = 0;
                g_pixels[d + 3] = 0;
            } else {
                g_pixels[d + 0] = static_cast<uint8_t>(sumR / sumA);
                g_pixels[d + 1] = static_cast<uint8_t>(sumG / sumA);
                g_pixels[d + 2] = static_cast<uint8_t>(sumB / sumA);
                g_pixels[d + 3] = static_cast<uint8_t>(sumA / texels);
            }
        }
    }
}

// B1124: is this sheet ONE picture rather than a 4x4 grid? Additive mass
// (max(R,G,B) * alpha -- what an additive blend paints) inside a band of
// `cell/8` texels either side of the three internal grid lines in each
// direction, as a share of the sheet's total mass, over the band's share of
// the sheet's area. 1.0 is a uniform texture; a grid's frames are each padded
// inside their own cell, so it reads near 0; one centred picture's body
// crosses the seams, so it reads above 1. Integer arithmetic only (B268).
bool measuresAsSingleImage(const EQT::ToolTGA::Image& img, int grid) {
    const int cell = img.width / grid;
    const int band = cell / 8 > 0 ? cell / 8 : 1;
    auto inBand = [&](int v) {
        for (int k = 1; k < grid; ++k) {
            const int seam = k * cell;
            if (v >= seam - band && v < seam + band) return true;
        }
        return false;
    };
    uint64_t total = 0, seamMass = 0, seamArea = 0;
    for (int y = 0; y < img.height; ++y) {
        const bool rowBand = inBand(y);
        for (int x = 0; x < img.width; ++x) {
            const uint8_t* t = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
            uint32_t m = t[0];
            if (t[1] > m) m = t[1];
            if (t[2] > m) m = t[2];
            const uint64_t w = static_cast<uint64_t>(m) * t[3];
            total += w;
            if (rowBand || inBand(x)) {
                seamMass += w;
                ++seamArea;
            }
        }
    }
    const uint64_t area = static_cast<uint64_t>(img.width) * img.height;
    // seamMass/total >= seamArea/area, cross-multiplied.
    return total > 0 && seamMass * area >= total * seamArea;
}

// B1127: the client is Windows, so `spellsnew.edd`'s `glowflare02.tga` is
// `Glowflare02.tga` on disk. Find the file case-insensitively; two matches
// would be ambiguous and are refused.
bool findCaseInsensitive(const std::string& dir, const char* name, std::string& path) {
    DIR* d = opendir(dir.c_str());
    if (!d) return false;
    int hits = 0;
    while (dirent* e = readdir(d)) {
        if (strcasecmp(e->d_name, name) == 0) {
            path = dir + "/" + e->d_name;
            ++hits;
        }
    }
    closedir(d);
    return hits == 1;
}

bool packOneFile(const std::string& spellEffectsDir, int sprite) {
    std::string path;
    if (!findCaseInsensitive(spellEffectsDir, Atlas::kFiles[sprite], path)) {
        fprintf(stderr, "Error: %s/%s not found (or not unique, ignoring case)\n",
                spellEffectsDir.c_str(), Atlas::kFiles[sprite]);
        return false;
    }

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolSprite::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }

    // B1127: 16 frames = a 4x4 grid, 4 = a 2x2 grid, 1 = one picture.
    const int frames = Atlas::framesFor(sprite);
    const int grid = frames == Atlas::kFramesPerSprite ? Atlas::kSheetGrid
                   : frames == Atlas::kTwoByTwo ? 2 : 1;
    if (frames != Atlas::kSingleImage && frames != Atlas::kTwoByTwo &&
        frames != Atlas::kFramesPerSprite) {
        fprintf(stderr, "Error: %s declares %d frames; a sheet is 1, 4 or %d\n",
                path.c_str(), frames, Atlas::kFramesPerSprite);
        return false;
    }
    if (img.width != img.height || img.width % Atlas::kSheetGrid != 0) {
        fprintf(stderr, "Error: %s is %dx%d, not a square frame sheet\n",
                path.c_str(), img.width, img.height);
        return false;
    }

    // B1124: the header's declaration and this file's own pixels must agree.
    // A declared grid must NOT measure as one picture on its own seams; a
    // declared single image must measure as one picture on the 4x4 seams.
    const bool single = measuresAsSingleImage(img, grid == 1 ? Atlas::kSheetGrid : grid);
    if (single != (frames == Atlas::kSingleImage)) {
        fprintf(stderr,
                "Error: %s measures as a %s but spell_sprite_atlas.h kSheetFrames "
                "declares %d frame(s)\n",
                path.c_str(), single ? "SINGLE IMAGE" : "GRID", frames);
        return false;
    }

    const int frameSize = img.width / grid;

    // B1188 (#507): the STORED frame size is derived from the sheet's kind --
    // one block is 4x4 cells (128 px), and
    //   a 4x4 grid fills it with sixteen frames of ONE cell each (32 px);
    //   a 2x2 sheet fills it with four frames of a 2x2 sub-block each (64 px);
    //   a single image fills the largest sub-block the frame fits in at its
    //   own resolution (64 px -> 2x2, 256 px -> the whole block 2:1) --
    // so the client's native texture pixels are kept 1:1 wherever they fit,
    // and the header's kStoredFramePx must agree with the derivation.
    const int cap = frames == Atlas::kFramesPerSprite ? Atlas::kCellSize
                   : frames == Atlas::kTwoByTwo ? 2 * Atlas::kCellSize
                   : 4 * Atlas::kCellSize;
    const int storedPx = std::min(frameSize, cap);
    const int declared = Atlas::kStoredFramePx[sprite];
    if (declared != storedPx) {
        fprintf(stderr,
                "Error: %s is a %s of %d px frames; the stored frame must be "
                "%d px, but spell_sprite_atlas.h kStoredFramePx declares %d px\n",
                path.c_str(), frames == Atlas::kSingleImage ? "SINGLE IMAGE"
                    : frames == Atlas::kTwoByTwo ? "2X2 SHEET" : "4X4 GRID",
                frameSize, storedPx, declared);
        return false;
    }
    if (!(storedPx % frameSize == 0 || frameSize % storedPx == 0)) {
        fprintf(stderr,
                "Error: %s has %d px frames that do not resample to the %d px "
                "stored frame by an integer factor\n",
                path.c_str(), frameSize, storedPx);
        return false;
    }

    // Pack every frame in display order. `ToolTGA::load` always returns
    // top-down RGBA regardless of the file's own origin bit, so the 4x4 grid
    // is in display order whether the TGA is bottom-left (0x08) or top-left
    // (0x28) origin -- both appear among these files.
    uint64_t sheetAlpha = 0;
    for (int f = 0; f < frames; ++f) {
        const int fx = (f % grid) * frameSize;
        const int fy = (f / grid) * frameSize;
        const uint8_t* src =
            img.rgba.data() + (static_cast<size_t>(fy) * img.width + fx) * 4;

        for (int row = 0; row < frameSize; ++row) {
            const uint8_t* r = src + static_cast<size_t>(row) * img.width * 4;
            for (int col = 0; col < frameSize; ++col) sheetAlpha += r[col * 4 + 3];
        }

        blitFrame(Atlas::cellFor(sprite, f), src, img.width, frameSize, storedPx);
    }

    // A sheet with no alpha anywhere is a decode failure wearing a sprite's
    // name, not an animation. (Individual grid frames MAY be blank.)
    if (sheetAlpha == 0) {
        fprintf(stderr, "Error: every frame of %s is fully transparent\n",
                path.c_str());
        return false;
    }
    return true;
}

void printUsage(const char* argv0) {
    printf("Usage: %s [--output <dir>] [--eq-path <dir>]\n\n", argv0);
    printf("  --output, -o   Directory to write spell_sprite_atlas.bmp into\n");
    printf("                 (default: data/textures)\n");
    printf("  --eq-path      EverQuest client directory holding SpellEffects/\n");
    printf("                 (default: /mnt/projects/EverQuestP1999)\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string outputDir = "data/textures";
    std::string eqPath = "/mnt/projects/EverQuestP1999";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--eq-path" && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    const std::string spellEffectsDir = eqPath + "/SpellEffects";

    for (int sprite = 0; sprite < Atlas::kFileCount; ++sprite) {
        if (!packOneFile(spellEffectsDir, sprite)) {
            fprintf(stderr,
                    "Refusing to write a partial spell sprite atlas "
                    "(%d of %d sprites packed).\n",
                    sprite, Atlas::kFileCount);
            return 1;
        }
    }

    const std::string outPath = outputDir + "/spell_sprite_atlas.bmp";
    if (!willeq_tools::writeBMP32(outPath, g_pixels.data(), Atlas::kWidth,
                                  Atlas::kHeight)) {
        fprintf(stderr, "Error: failed to write %s\n", outPath.c_str());
        return 1;
    }

    int singles = 0, drawn = 0;
    for (int sprite = 0; sprite < Atlas::kFileCount; ++sprite) {
        drawn += Atlas::framesFor(sprite);
        if (Atlas::framesFor(sprite) == Atlas::kSingleImage) ++singles;
    }
    printf("Wrote %s (%dx%d, %d sprites: %d grids + %d single images = %d "
           "drawn cells in %d reserved of %d, from %s)\n",
           outPath.c_str(), Atlas::kWidth, Atlas::kHeight, Atlas::kFileCount,
           Atlas::kFileCount - singles, singles, drawn, Atlas::kUsedCells,
           Atlas::kCapacity, spellEffectsDir.c_str());
    return 0;
}
