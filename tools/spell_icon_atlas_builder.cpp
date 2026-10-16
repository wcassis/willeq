// tools/spell_icon_atlas_builder.cpp -- packs the official client's own
// spell-icon art into data/textures/spell_icons{gems,spells,buff}.bmp and
// the A_SpellGemHolder piece into spell_icon_holder.bmp.
//
// B1077 (GitLab #397). Will's 2026-10-15 play session: "Spell icons were
// missing from the spellbook. Spell gems on the spell gem hotbar are also
// missing." fable has never shipped spell-icon art; this is the T430 pattern
// (item_icon_atlas_builder) applied to the three spell-icon families.
//
// INPUT. `<uifiles>/default/gemicons0{1,2,3}.tga` (A_SpellGems, 7x9 grid of
// 36x28 cells), `Spells0{1..7}.tga` (A_SpellIcons, 6x6 of 40x40) and
// `Spell_Icons.tga` (BuffIcons, 6x6 of 40x40), plus the A_SpellGemHolder
// piece of `window_pieces02.tga` at (208,216) 39x32.
//
// LAYOUT. `include/client/graphics/ui/spell_icon_atlas.h` is the ONE source
// of truth -- the grids, the row-major cell order (all three families are
// `<Vertical>false</Vertical>`), the zero-based id convention (the salvage
// code's own arithmetic), the packed atlas geometry and the holder rect all
// live there, and the client's `SpellIconSheets` includes the same header.
//
// NO RESAMPLING. Spell and buff cells are copied at their native 40x40 and
// the gem art at its native 36x28 into a (0,0) origin of its 40x40 cell; the
// extra pitch is transparent, so nothing is scaled and the output is
// bit-identical at -O0 and -O2 (board B268's rule).
//
// ALL-OR-NOTHING. A sheet that cannot be read, is not 256x256, or decodes
// fully transparent is a hard exit 1, never a silently degraded atlas
// (Will's standing EQE1 rule). Every cell is packed whether or not it carries
// art; the per-cell census is reported and checked against the header's
// populated count, so a source-tree change is a failure rather than a
// silently different atlas.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bmp32_writer.h"
#include "tga_reader.h"

#include "client/graphics/ui/spell_icon_atlas.h"

namespace Layout = EQT::Graphics::SpellIconAtlasLayout;
using Layout::SpellIconSheet;

namespace {

// One buffer per packed family atlas, fully transparent so an unused cell
// contributes nothing however it is sampled.
std::vector<std::vector<uint8_t>> g_atlases;

// The holder piece: 40x40 canvas, the 39x32 art at (0,0), the rest empty so
// it packs like every other 40x40 cell of the sheet.
std::vector<uint8_t> g_holder;

void initAtlases() {
    g_atlases.assign(static_cast<size_t>(SpellIconSheet::Count),
                     std::vector<uint8_t>(
                         static_cast<size_t>(Layout::kAtlasEdge) *
                             Layout::kAtlasEdge * 4,
                         0));
    g_holder.assign(static_cast<size_t>(Layout::kCellSize) *
                        Layout::kCellSize * 4,
                    0);
}

// Copy one SOURCE cell (native size, from the family) out of a decoded
// source sheet into its packed slot, at the slot's (0,0) origin.
void blitCell(SpellIconSheet sheet, int index, const EQT::ToolTGA::Image& img,
              int srcX, int srcY) {
    int cw = 0, ch = 0;
    Layout::cellSize(sheet, cw, ch);
    int dstX = 0, dstY = 0;
    Layout::atlasCellOrigin(sheet, static_cast<uint32_t>(index), dstX, dstY);

    std::vector<uint8_t>& dst =
        g_atlases[static_cast<size_t>(static_cast<int>(sheet))];
    for (int row = 0; row < ch; ++row) {
        const uint8_t* s =
            img.rgba.data() +
            (static_cast<size_t>(srcY + row) * img.width + srcX) * 4;
        uint8_t* d =
            dst.data() +
            (static_cast<size_t>(dstY + row) * Layout::kAtlasEdge + dstX) * 4;
        std::memcpy(d, s, static_cast<size_t>(cw) * 4);
    }
}

bool packOneSheet(SpellIconSheet sheet, const std::string& uifilesDir,
                  int sheetIndex, int& populatedCells) {
    char nameBuf[24] = {};
    const std::string path =
        uifilesDir + "/" + Layout::sourceSheetName(sheet, sheetIndex, nameBuf);

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }

    if (img.width != Layout::kSourceSheetEdge ||
        img.height != Layout::kSourceSheetEdge) {
        fprintf(stderr, "Error: %s is %dx%d, not the %dx%d\n"
                        "EQUI_Animations.xml declares for this family\n",
                path.c_str(), img.width, img.height,
                Layout::kSourceSheetEdge, Layout::kSourceSheetEdge);
        return false;
    }

    int cw = 0, ch = 0;
    Layout::cellSize(sheet, cw, ch);
    const int cols = (sheet == SpellIconSheet::Gems) ? Layout::kGemCols
                                                     : Layout::kSourceSheetEdge / 40;
    const int rows = Layout::kSourceSheetEdge / ch;
    const int perSheet = cols * rows;

    uint64_t sheetAlpha = 0;
    for (int k = 0; k < perSheet; ++k) {
        const int index = sheetIndex * perSheet + k;
        int checkSheet = 0, srcX = 0, srcY = 0;
        Layout::sourceCell(sheet, static_cast<uint32_t>(index), checkSheet,
                           srcX, srcY);
        if (checkSheet != sheetIndex) {
            fprintf(stderr,
                    "Error: layout header disagrees with the sheet walk "
                    "(index %d -> sheet %d, expected %d)\n",
                    index, checkSheet, sheetIndex);
            return false;
        }

        // Per-cell alpha census: how many cells carry art. Reported, never
        // used to skip a cell -- an empty cell is packed as empty.
        uint64_t cellAlpha = 0;
        for (int row = 0; row < ch; ++row) {
            const uint8_t* r =
                img.rgba.data() +
                (static_cast<size_t>(srcY + row) * img.width + srcX) * 4;
            for (int col = 0; col < cw; ++col)
                cellAlpha += r[col * 4 + 3];
        }
        if (cellAlpha != 0) ++populatedCells;
        sheetAlpha += cellAlpha;

        blitCell(sheet, index, img, srcX, srcY);
    }

    // A sheet with no alpha anywhere is a decode failure wearing an icon
    // sheet's name, not a blank sheet: every one of the three families
    // carries art in every cell of every sheet (the header's census).
    if (sheetAlpha == 0) {
        fprintf(stderr, "Error: every cell of %s is fully transparent\n",
                path.c_str());
        return false;
    }
    return true;
}

bool packHolder(const std::string& uifilesDir) {
    const std::string path = uifilesDir + "/window_pieces02.tga";

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }
    if (img.width < Layout::kHolderSrcX + Layout::kHolderW ||
        img.height < Layout::kHolderSrcY + Layout::kHolderH) {
        fprintf(stderr,
                "Error: %s is %dx%d, too small to hold the %dx%d holder "
                "piece at (%d,%d)\n",
                path.c_str(), img.width, img.height, Layout::kHolderW,
                Layout::kHolderH, Layout::kHolderSrcX, Layout::kHolderSrcY);
        return false;
    }

    for (int row = 0; row < Layout::kHolderH; ++row) {
        const uint8_t* s =
            img.rgba.data() +
            (static_cast<size_t>(Layout::kHolderSrcY + row) * img.width +
             Layout::kHolderSrcX) *
                4;
        uint8_t* d =
            g_holder.data() + (static_cast<size_t>(row) * Layout::kCellSize) * 4;
        std::memcpy(d, s, static_cast<size_t>(Layout::kHolderW) * 4);
    }

    // The piece must carry art; an all-transparent cut is a wrong rectangle.
    uint64_t alpha = 0;
    for (int row = 0; row < Layout::kHolderH; ++row)
        for (int col = 0; col < Layout::kHolderW; ++col)
            alpha += g_holder[(static_cast<size_t>(row) * Layout::kCellSize +
                               col) *
                              4 + 3];
    if (alpha == 0) {
        fprintf(stderr,
                "Error: the A_SpellGemHolder piece at (%d,%d) is fully "
                "transparent\n",
                Layout::kHolderSrcX, Layout::kHolderSrcY);
        return false;
    }
    return true;
}

int populatedCount(SpellIconSheet sheet) {
    switch (sheet) {
        case SpellIconSheet::Gems:   return Layout::kGemPopulated;
        case SpellIconSheet::Spells: return Layout::kSpellPopulated;
        case SpellIconSheet::Buff:   return Layout::kBuffPopulated;
        default: return 0;
    }
}

void printUsage(const char* argv0) {
    printf(
        "Usage: %s [--output <dir>] [--eq-path <dir>] [--uifiles-path <dir>]\n\n",
        argv0);
    printf(
        "  --output, -o    Directory to write spell_icons{gems,spells,buff}.bmp "
        "+ spell_icon_holder.bmp into\n");
    printf("                  (default: data/textures)\n");
    printf(
        "  --eq-path       EverQuest client directory; uifiles/default is read\n");
    printf(
        "                  under it (default: /mnt/projects/EverQuestP1999)\n");
    printf("  --uifiles-path  The uifiles/default directory directly; overrides\n");
    printf("                  --eq-path when given\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string outputDir = "data/textures";
    std::string eqPath = "/mnt/projects/EverQuestP1999";
    std::string uifilesPath;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (arg == "--eq-path" && i + 1 < argc) {
            eqPath = argv[++i];
        } else if (arg == "--uifiles-path" && i + 1 < argc) {
            uifilesPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            printUsage(argv[0]);
            return 1;
        }
    }

    if (uifilesPath.empty()) uifilesPath = eqPath + "/uifiles/default";

    initAtlases();

    const struct {
        SpellIconSheet sheet;
        int sheetCount;
    } families[] = {
        {SpellIconSheet::Gems, Layout::kGemSheetCount},
        {SpellIconSheet::Spells, Layout::kSpellSheetCount},
        {SpellIconSheet::Buff, Layout::kBuffSheetCount},
    };

    int populated[static_cast<int>(SpellIconSheet::Count)] = {};
    for (const auto& fam : families) {
        for (int sheet = 0; sheet < fam.sheetCount; ++sheet) {
            if (!packOneSheet(fam.sheet, uifilesPath, sheet,
                              populated[static_cast<int>(fam.sheet)])) {
                fprintf(stderr,
                        "Refusing to write a partial spell icon atlas "
                        "(%s sheet %d of %d failed).\n",
                        Layout::atlasName(fam.sheet), sheet, fam.sheetCount);
                return 1;
            }
        }
    }
    if (!packHolder(uifilesPath)) {
        fprintf(stderr, "Refusing to write a partial spell icon atlas "
                        "(the A_SpellGemHolder piece failed).\n");
        return 1;
    }

    for (const auto& fam : families) {
        if (populated[static_cast<int>(fam.sheet)] !=
            populatedCount(fam.sheet)) {
            fprintf(stderr,
                    "Error: %s -- %d cells carry art, but the layout header "
                    "says %d. The source sheets changed; re-take the census "
                    "before shipping.\n",
                    Layout::atlasName(fam.sheet),
                    populated[static_cast<int>(fam.sheet)],
                    populatedCount(fam.sheet));
            return 1;
        }
    }

    for (const auto& fam : families) {
        const std::string outPath =
            outputDir + "/" + Layout::atlasName(fam.sheet) + ".bmp";
        std::vector<uint8_t>& atlas =
            g_atlases[static_cast<size_t>(static_cast<int>(fam.sheet))];
        if (!willeq_tools::writeBMP32(
                outPath, atlas.data(), Layout::kAtlasEdge, Layout::kAtlasEdge)) {
            fprintf(stderr, "Error: failed to write %s\n", outPath.c_str());
            return 1;
        }
        printf("Wrote %s (%dx%d, %d cells, %d carry art)\n",
               outPath.c_str(), Layout::kAtlasEdge, Layout::kAtlasEdge,
               Layout::iconCount(fam.sheet),
               populated[static_cast<int>(fam.sheet)]);
    }

    const std::string holderPath =
        outputDir + "/" + Layout::holderName() + ".bmp";
    if (!willeq_tools::writeBMP32(holderPath, g_holder.data(),
                                  Layout::kCellSize, Layout::kCellSize)) {
        fprintf(stderr, "Error: failed to write %s\n", holderPath.c_str());
        return 1;
    }
    printf("Wrote %s (%dx%d, the %dx%d A_SpellGemHolder piece)\n",
           holderPath.c_str(), Layout::kCellSize, Layout::kCellSize,
           Layout::kHolderW, Layout::kHolderH);

    return 0;
}
