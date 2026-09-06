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
// FRAME EXTRACTION -- and a MEASURED correction to T409. Every `spel*.tga` is
// a 4x4 sheet. T409 recorded that the sixteen frames are one repeated frame;
// they are NOT. T417 hashed all sixteen frames of all 67 files and found 5 to
// 16 DISTINCT frames in every one: these are animations. `spelg1` is a sword
// rotating through sixteen headings; `speln1` is a mottled disc whose noise
// churns.
//
// Which frame, then? NOT frame 0. The sheets are fade-in/fade-out cycles and
// frame 0 is the START of one: SEVEN of the 33 files have a COMPLETELY BLANK
// frame 0 (`spelb3`, `spelb4`, `spelc3`, `spelc4`, `speld4`, `spely3`, and
// `spely4` at one alpha level of 1), and `spelu2` -- the Frost Bolt impact --
// carries 14,966 of its peak 120,757 alpha there, i.e. 12%. A frame-0 atlas
// ships seven empty cells and a nearly invisible Frost Bolt.
//
// The rule is therefore THE PEAK FRAME: the frame with the greatest total
// alpha, ties broken by the lowest index. That is the fully-formed sprite,
// the one a viewer would call "the picture", and it is a pure integer sum
// over the file's own bytes -- deterministic at every optimisation level.
// A static cell is still a deliberate approximation of a sixteen-frame
// animation; animating it is boarded as B355.
//
// SCALING. The source sheets are 256x256 (64x64 frames), 128x128 (32x32) and
// 64x64 (16x16). Cells are 64x64, so the two smaller shapes are magnified by
// an exact integer factor with NEAREST-neighbour replication: exact integer
// arithmetic, bit-identical at every optimisation level (board B268's rule),
// and the GPU's bilinear filter smooths the result at draw time anyway.
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

constexpr int kSheetGrid = 4;  // every spel*.tga is a 4x4 frame sheet

// Fully transparent, so an unused cell contributes nothing however it is
// sampled.
std::vector<uint8_t> g_pixels(static_cast<size_t>(Atlas::kWidth) * Atlas::kHeight * 4, 0);

void blitCell(int slot, const std::vector<uint8_t>& frame, int frameSize) {
    int originX = 0, originY = 0;
    Atlas::cellOrigin(slot, originX, originY);

    const int scale = Atlas::kCellSize / frameSize;
    for (int y = 0; y < Atlas::kCellSize; ++y) {
        const int srcY = y / scale;
        for (int x = 0; x < Atlas::kCellSize; ++x) {
            const int srcX = x / scale;
            const size_t src = (static_cast<size_t>(srcY) * frameSize + srcX) * 4;
            const size_t dst =
                (static_cast<size_t>(originY + y) * Atlas::kWidth + (originX + x)) * 4;
            std::memcpy(&g_pixels[dst], &frame[src], 4);
        }
    }
}

bool packOneFile(const std::string& spellEffectsDir, int slot) {
    const std::string path = spellEffectsDir + "/" + Atlas::kFiles[slot];

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }

    if (img.width != img.height || img.width % kSheetGrid != 0) {
        fprintf(stderr, "Error: %s is %dx%d, not a square 4x4 frame sheet\n",
                path.c_str(), img.width, img.height);
        return false;
    }

    const int frameSize = img.width / kSheetGrid;
    if (frameSize <= 0 || Atlas::kCellSize % frameSize != 0 ||
        frameSize > Atlas::kCellSize) {
        fprintf(stderr,
                "Error: %s has %dx%d frames, which do not scale to a %d px cell "
                "by an integer factor\n",
                path.c_str(), frameSize, frameSize, Atlas::kCellSize);
        return false;
    }

    // Pick the peak frame. `ToolTGA::load` always returns top-down RGBA
    // regardless of the file's own origin bit, so the grid is in display
    // order whether the TGA is bottom-left (0x08) or top-left (0x28) origin
    // -- both appear among these files.
    int bestFrame = 0;
    uint64_t bestAlpha = 0;
    for (int f = 0; f < kSheetGrid * kSheetGrid; ++f) {
        const int fx = (f % kSheetGrid) * frameSize;
        const int fy = (f / kSheetGrid) * frameSize;
        uint64_t total = 0;
        for (int row = 0; row < frameSize; ++row) {
            const uint8_t* src =
                img.rgba.data() + (static_cast<size_t>(fy + row) * img.width + fx) * 4;
            for (int col = 0; col < frameSize; ++col) {
                total += src[col * 4 + 3];
            }
        }
        if (total > bestAlpha) {  // strict >: ties keep the lowest index
            bestAlpha = total;
            bestFrame = f;
        }
    }

    if (bestAlpha == 0) {
        fprintf(stderr, "Error: every frame of %s is fully transparent\n",
                path.c_str());
        return false;
    }

    std::vector<uint8_t> frame;
    if (!img.crop((bestFrame % kSheetGrid) * frameSize,
                  (bestFrame / kSheetGrid) * frameSize, frameSize, frameSize,
                  frame)) {
        fprintf(stderr, "Error: cannot crop frame %d out of %s\n", bestFrame,
                path.c_str());
        return false;
    }

    blitCell(slot, frame, frameSize);
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

    for (int slot = 0; slot < Atlas::kFileCount; ++slot) {
        if (!packOneFile(spellEffectsDir, slot)) {
            fprintf(stderr,
                    "Refusing to write a partial spell sprite atlas "
                    "(%d of %d cells packed).\n",
                    slot, Atlas::kFileCount);
            return 1;
        }
    }

    const std::string outPath = outputDir + "/spell_sprite_atlas.bmp";
    if (!willeq_tools::writeBMP32(outPath, g_pixels.data(), Atlas::kWidth,
                                  Atlas::kHeight)) {
        fprintf(stderr, "Error: failed to write %s\n", outPath.c_str());
        return 1;
    }

    printf("Wrote %s (%dx%d, %d of %d cells from %s)\n", outPath.c_str(),
           Atlas::kWidth, Atlas::kHeight, Atlas::kFileCount, Atlas::kCapacity,
           spellEffectsDir.c_str());
    return 0;
}
