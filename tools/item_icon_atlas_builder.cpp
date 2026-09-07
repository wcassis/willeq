// tools/item_icon_atlas_builder.cpp -- packs the official client's own item
// icon art into data/textures/item_icons0.bmp and item_icons1.bmp.
//
// T430, board B376. Will ruled 2026-09-07: CRITICAL, and "the official
// uifiles/default/dragitem*.tga sheets are the source".
//
// INPUT. `<eq-path>/uifiles/default/dragitem1.tga` .. `dragitem34.tga`, the
// 34 sheets the oracle's own `EQUI_Animations.xml` lists under its single
// `A_DragItem` animation, in that order. Each is a 256x256 32-bit TGA holding
// a 6x6 grid of 40x40 cells.
//
// LAYOUT. `include/client/graphics/ui/item_icon_atlas.h` is the ONE source of
// truth -- the grid, the column-major cell order (`<Vertical>true</Vertical>`,
// proven against dragitem34's partial fill), the id base, and the packed
// atlas geometry all live there, and the client's `ItemIconAtlas` includes the
// same header. Read its banner before changing anything here.
//
// NO RESAMPLING. Cells are copied at their native 40x40. The oracle draws
// them at 40x40 (`<DecalSize><CX>40</CX><CY>40</CY></DecalSize>` on every
// A_DragItem button), so there is nothing to scale to and no arithmetic that
// could differ between optimisation levels -- the output is trivially
// bit-identical at -O0 and -O2 (board B268's rule).
//
// ALL-OR-NOTHING. A sheet that cannot be read, is not 256x256, or decodes
// fully transparent is a hard exit 1, never a silently degraded atlas (Will's
// standing EQE1 rule; `ui_atlas_builder` and `spell_sprite_atlas_builder` set
// the precedent). A cell that is legitimately empty is fine -- dragitem34 has
// 23 of them and they are the end of the id space, not a decode failure.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bmp32_writer.h"
#include "tga_reader.h"

#include "client/graphics/ui/item_icon_atlas.h"

namespace Layout = EQT::Graphics::ItemIconAtlasLayout;

namespace {

// One buffer per packed atlas, fully transparent so an unused cell
// contributes nothing however it is sampled.
std::vector<std::vector<uint8_t>> g_atlases;

void initAtlases() {
    g_atlases.assign(
        Layout::kAtlasCount,
        std::vector<uint8_t>(
            static_cast<size_t>(Layout::kAtlasEdge) * Layout::kAtlasEdge * 4, 0));
}

// Copy one 40x40 cell out of a decoded source sheet into its packed slot.
void blitCell(int index, const EQT::ToolTGA::Image& img, int srcX, int srcY) {
    const int atlas = Layout::atlasForIndex(index);
    int dstX = 0, dstY = 0;
    Layout::atlasCellOrigin(index, dstX, dstY);

    std::vector<uint8_t>& dst = g_atlases[static_cast<size_t>(atlas)];
    for (int row = 0; row < Layout::kCellSize; ++row) {
        const uint8_t* s =
            img.rgba.data() +
            (static_cast<size_t>(srcY + row) * img.width + srcX) * 4;
        uint8_t* d =
            dst.data() +
            (static_cast<size_t>(dstY + row) * Layout::kAtlasEdge + dstX) * 4;
        std::memcpy(d, s, static_cast<size_t>(Layout::kCellSize) * 4);
    }
}

bool packOneSheet(const std::string& uifilesDir, int sheet, int& populatedCells) {
    char nameBuf[24] = {};
    const std::string path =
        uifilesDir + "/" + Layout::sourceSheetName(sheet, nameBuf);

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }

    if (img.width != Layout::kSourceSheetEdge ||
        img.height != Layout::kSourceSheetEdge) {
        fprintf(stderr, "Error: %s is %dx%d, not the %dx%d EQUI_Animations.xml "
                        "declares for A_DragItem\n",
                path.c_str(), img.width, img.height, Layout::kSourceSheetEdge,
                Layout::kSourceSheetEdge);
        return false;
    }

    uint64_t sheetAlpha = 0;
    for (int k = 0; k < Layout::kCellsPerSheet; ++k) {
        const int index = sheet * Layout::kCellsPerSheet + k;
        int checkSheet = 0, srcX = 0, srcY = 0;
        Layout::sourceCell(index, checkSheet, srcX, srcY);
        if (checkSheet != sheet) {
            fprintf(stderr, "Error: layout header disagrees with the sheet walk "
                            "(index %d -> sheet %d, expected %d)\n",
                    index, checkSheet, sheet);
            return false;
        }

        // Per-cell alpha census: how many cells carry art. Reported, never
        // used to skip a cell -- an empty cell is packed as empty.
        uint64_t cellAlpha = 0;
        for (int row = 0; row < Layout::kCellSize; ++row) {
            const uint8_t* r =
                img.rgba.data() +
                (static_cast<size_t>(srcY + row) * img.width + srcX) * 4;
            for (int col = 0; col < Layout::kCellSize; ++col)
                cellAlpha += r[col * 4 + 3];
        }
        if (cellAlpha != 0) ++populatedCells;
        sheetAlpha += cellAlpha;

        blitCell(index, img, srcX, srcY);
    }

    // A sheet with no alpha anywhere is a decode failure wearing an icon
    // sheet's name, not a blank sheet: every one of the 34 carries at least
    // one icon (dragitem34, the emptiest, carries thirteen).
    if (sheetAlpha == 0) {
        fprintf(stderr, "Error: every cell of %s is fully transparent\n",
                path.c_str());
        return false;
    }
    return true;
}

void printUsage(const char* argv0) {
    printf("Usage: %s [--output <dir>] [--eq-path <dir>] [--uifiles-path <dir>]\n\n",
           argv0);
    printf("  --output, -o    Directory to write item_icons0.bmp/item_icons1.bmp into\n");
    printf("                  (default: data/textures)\n");
    printf("  --eq-path       EverQuest client directory; uifiles/default is read\n");
    printf("                  under it (default: /mnt/projects/EverQuestP1999)\n");
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

    int populatedCells = 0;
    for (int sheet = 0; sheet < Layout::kSheetCount; ++sheet) {
        if (!packOneSheet(uifilesPath, sheet, populatedCells)) {
            fprintf(stderr,
                    "Refusing to write a partial item icon atlas "
                    "(%d of %d sheets packed).\n",
                    sheet, Layout::kSheetCount);
            return 1;
        }
    }

    if (populatedCells != Layout::kPopulatedIconCount) {
        fprintf(stderr,
                "Error: %d cells carry art, but the layout header says %d. "
                "The source sheets changed; re-take the census before "
                "shipping.\n",
                populatedCells, Layout::kPopulatedIconCount);
        return 1;
    }

    for (int atlas = 0; atlas < Layout::kAtlasCount; ++atlas) {
        const std::string outPath =
            outputDir + "/" + Layout::atlasName(atlas) + ".bmp";
        if (!willeq_tools::writeBMP32(outPath,
                                      g_atlases[static_cast<size_t>(atlas)].data(),
                                      Layout::kAtlasEdge, Layout::kAtlasEdge)) {
            fprintf(stderr, "Error: failed to write %s\n", outPath.c_str());
            return 1;
        }
        printf("Wrote %s (%dx%d)\n", outPath.c_str(), Layout::kAtlasEdge,
               Layout::kAtlasEdge);
    }

    printf("Packed %d icons (ids %u..%u) from %d sheets in %s; %d carry art, "
           "%d cells are empty.\n",
           Layout::kIconCount, Layout::kFirstIconId,
           Layout::kFirstIconId + Layout::kIconCount - 1, Layout::kSheetCount,
           uifilesPath.c_str(), populatedCells,
           Layout::kIconCount - populatedCells);
    return 0;
}
