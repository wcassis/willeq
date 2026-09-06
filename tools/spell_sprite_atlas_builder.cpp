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
// FRAME EXTRACTION -- and a MEASURED correction to T409. Every `spel*.tga`
// is a 4x4 sheet. T409 recorded that the sixteen frames are one repeated
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
// SCALING. The source sheets are 256x256 (64x64 frames), 128x128 (32x32) and
// 64x64 (16x16). Cells are 32x32 (see the header for why the cell shrank when
// the frame count went up), so each frame is resampled by an exact integer
// factor:
//   * 16x16 -> 32x32: magnified 2x by NEAREST-neighbour replication.
//   * 32x32 -> 32x32: copied.
//   * 64x64 -> 32x32: reduced 2:1 by an ALPHA-WEIGHTED box average. Plain
//     RGB averaging is wrong here: a transparent texel's RGB in these files
//     is arbitrary (often black), and the additive blend samples RGB
//     directly, so an unweighted average darkens or fringes every sprite
//     edge. Weighting each texel by its own alpha averages only the colour
//     that is actually there.
// All of it is integer arithmetic on `uint32_t` -- no floating point, no
// signed overflow -- so the output is bit-identical at every optimisation
// level (board B268's rule; a shipped asset that depends on
// CMAKE_BUILD_TYPE makes `TexturesReproducible` a coin toss).
//
// ALL-OR-NOTHING. A file that cannot be read or is not a 4x4 sheet of the
// expected size is a hard exit 1, never a silently degraded sheet (Will's
// standing EQE1 rule, and `ui_atlas_builder`'s precedent).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bmp32_writer.h"
#include "tga_reader.h"

#include "client/graphics/environment/spell_sprite_atlas.h"

namespace Atlas = EQT::Graphics::Environment::SpellSpriteAtlas;

namespace {

// Fully transparent, so an unused cell contributes nothing however it is
// sampled.
std::vector<uint8_t> g_pixels(static_cast<size_t>(Atlas::kWidth) * Atlas::kHeight * 4, 0);

// Resample one source frame into one 32x32 cell. `frameSize` is the source
// frame's edge; `src` points at its top-left texel inside `img`, whose row
// stride is `stride` texels.
void blitCell(int cell, const uint8_t* src, int stride, int frameSize) {
    int originX = 0, originY = 0;
    Atlas::cellOrigin(cell, originX, originY);

    if (frameSize <= Atlas::kCellSize) {
        // Magnify (or copy) by an exact integer factor, nearest neighbour.
        const int scale = Atlas::kCellSize / frameSize;
        for (int y = 0; y < Atlas::kCellSize; ++y) {
            const int srcY = y / scale;
            for (int x = 0; x < Atlas::kCellSize; ++x) {
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
    const int factor = frameSize / Atlas::kCellSize;
    for (int y = 0; y < Atlas::kCellSize; ++y) {
        for (int x = 0; x < Atlas::kCellSize; ++x) {
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

bool packOneFile(const std::string& spellEffectsDir, int sprite) {
    const std::string path = spellEffectsDir + "/" + Atlas::kFiles[sprite];

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }

    if (img.width != img.height || img.width % Atlas::kSheetGrid != 0) {
        fprintf(stderr, "Error: %s is %dx%d, not a square 4x4 frame sheet\n",
                path.c_str(), img.width, img.height);
        return false;
    }

    const int frameSize = img.width / Atlas::kSheetGrid;
    const bool integerUp =
        frameSize > 0 && frameSize <= Atlas::kCellSize &&
        Atlas::kCellSize % frameSize == 0;
    const bool integerDown =
        frameSize > Atlas::kCellSize && frameSize % Atlas::kCellSize == 0;
    if (!integerUp && !integerDown) {
        fprintf(stderr,
                "Error: %s has %dx%d frames, which do not resample to a %d px cell "
                "by an integer factor\n",
                path.c_str(), frameSize, frameSize, Atlas::kCellSize);
        return false;
    }

    // Pack every frame in display order. `ToolTGA::load` always returns
    // top-down RGBA regardless of the file's own origin bit, so the 4x4 grid
    // is in display order whether the TGA is bottom-left (0x08) or top-left
    // (0x28) origin -- both appear among these files.
    uint64_t sheetAlpha = 0;
    for (int f = 0; f < Atlas::kFramesPerSprite; ++f) {
        const int fx = (f % Atlas::kSheetGrid) * frameSize;
        const int fy = (f / Atlas::kSheetGrid) * frameSize;
        const uint8_t* src =
            img.rgba.data() + (static_cast<size_t>(fy) * img.width + fx) * 4;

        for (int row = 0; row < frameSize; ++row) {
            const uint8_t* r = src + static_cast<size_t>(row) * img.width * 4;
            for (int col = 0; col < frameSize; ++col) sheetAlpha += r[col * 4 + 3];
        }

        blitCell(Atlas::cellFor(sprite, f), src, img.width, frameSize);
    }

    // A sheet with no alpha anywhere is a decode failure wearing a sprite's
    // name, not an animation. (Individual frames MAY be blank -- seven of
    // these files have a completely empty frame 0, which is the fade-in
    // starting from nothing.)
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

    printf("Wrote %s (%dx%d, %d sprites x %d frames = %d of %d cells, from %s)\n",
           outPath.c_str(), Atlas::kWidth, Atlas::kHeight, Atlas::kFileCount,
           Atlas::kFramesPerSprite, Atlas::kUsedCells, Atlas::kCapacity,
           spellEffectsDir.c_str());
    return 0;
}
