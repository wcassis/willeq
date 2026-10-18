// tools/spellbook_backdrop_builder.cpp -- composes the official client's own
// spellbook book-ART (the four backdrop tiles + the Done "X") from
// uifiles/default into ONE shipped BMP, data/textures/spellbook_backdrop.bmp.
//
// B1076 (GitLab #396). See include/client/graphics/ui/spellbook_backdrop.h
// for the source/composition map and the reason the four oracle tiles are
// shipped as one composed image rather than four pack entries.
//
// NO RESAMPLING. Every pixel is copied from its TGA at its native size and
// place -- no arithmetic that could differ between optimisation levels --
// so the output is bit-identical at -O0 and -O2 (board B268's rule).
//
// ALL-OR-NOTHING. A source TGA that cannot be read, is not the size this
// header says, or whose tile decodes fully transparent is a hard exit 1,
// never a silently degraded backdrop (Will's standing EQE1 rule; the same
// shape as item_icon_atlas_builder and spell_icon_atlas_builder).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bmp32_writer.h"
#include "tga_reader.h"

#include "client/graphics/ui/spellbook_backdrop.h"

namespace Layout = EQT::Graphics::SpellBookBackdrop;

namespace {

// The composed canvas, fully transparent until every tile lands.
std::vector<uint8_t> g_canvas;

// Copies a srcW x srcH rectangle out of a decoded source TGA into the
// canvas at (dstX, dstY). Never clamps -- a crop that does not fit the
// source is a wrong header, not a silent truncation.
bool blitRect(const EQT::ToolTGA::Image& img, int srcX, int srcY, int srcW,
              int srcH, int dstX, int dstY, const std::string& what) {
    if (srcX < 0 || srcY < 0 || srcX + srcW > img.width ||
        srcY + srcH > img.height) {
        fprintf(stderr, "Error: %s crop (%d,%d %dx%d) does not fit a %dx%d "
                        "source TGA\n",
                what.c_str(), srcX, srcY, srcW, srcH, img.width, img.height);
        return false;
    }
    if (dstX < 0 || dstY < 0 || dstX + srcW > Layout::kCanvasW ||
        dstY + srcH > Layout::kCanvasH) {
        fprintf(stderr, "Error: %s tile placed (%d,%d %dx%d) falls outside "
                        "the %dx%d canvas\n",
                what.c_str(), dstX, dstY, srcW, srcH, Layout::kCanvasW,
                Layout::kCanvasH);
        return false;
    }
    for (int row = 0; row < srcH; ++row) {
        const uint8_t* s =
            img.rgba.data() +
            (static_cast<size_t>(srcY + row) * img.width + srcX) * 4;
        uint8_t* d =
            g_canvas.data() +
            (static_cast<size_t>(dstY + row) * Layout::kCanvasW + dstX) * 4;
        std::memcpy(d, s, static_cast<size_t>(srcW) * 4);
    }
    return true;
}

// Loads one source TGA, checks it is the size the header says, and copies
// its tile crop onto the canvas. A tile that decodes fully transparent is a
// decode failure, not an empty region -- the book art is opaque.
bool packTile(const std::string& uifilesDir, int index) {
    const Layout::Tile& t = Layout::kTiles[index];
    const std::string path = uifilesDir + "/" + t.source;

    EQT::ToolTGA::Image img;
    std::string error;
    if (!EQT::ToolTGA::load(path, img, error)) {
        fprintf(stderr, "Error: %s\n", error.c_str());
        return false;
    }
    if (img.width != Layout::sourceWidth(index) ||
        img.height != Layout::sourceHeight(index)) {
        fprintf(stderr,
                "Error: %s is %dx%d, not the %dx%d the source TGA carries\n",
                path.c_str(), img.width, img.height,
                Layout::sourceWidth(index), Layout::sourceHeight(index));
        return false;
    }

    uint64_t tileAlpha = 0;
    for (int row = 0; row < t.srcH; ++row) {
        const uint8_t* r =
            img.rgba.data() +
            (static_cast<size_t>(t.srcY + row) * img.width + t.srcX) * 4;
        for (int col = 0; col < t.srcW; ++col)
            tileAlpha += r[col * 4 + 3];
    }
    if (tileAlpha == 0) {
        fprintf(stderr,
                "Error: tile %d (%s) decodes fully transparent\n",
                index, t.source);
        return false;
    }

    return blitRect(img, t.srcX, t.srcY, t.srcW, t.srcH, t.dstX, t.dstY,
                    t.source);
}

void printUsage(const char* argv0) {
    printf(
        "Usage: %s [--output <dir>] [--eq-path <dir>] [--uifiles-path <dir>]\n\n",
        argv0);
    printf(
        "  --output, -o    Directory to write spellbook_backdrop.bmp into\n"
        "                  (default: data/textures)\n");
    printf(
        "  --eq-path       EverQuest client directory; uifiles/default is read\n"
        "                  under it (default: /mnt/projects/EverQuestP1999)\n");
    printf(
        "  --uifiles-path  The uifiles/default directory directly; overrides\n"
        "                  --eq-path when given\n");
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

    g_canvas.assign(
        static_cast<size_t>(Layout::kCanvasW) * Layout::kCanvasH * 4, 0);

    for (int index = 0; index < 4; ++index) {
        if (!packTile(uifilesPath, index)) {
            fprintf(stderr, "Refusing to write a partial spellbook backdrop "
                            "(%d of 4 tiles packed).\n", index);
            return 1;
        }
    }

    // The Done "X" on the same canvas, at the window's SBW_DoneButton place.
    {
        const std::string path = uifilesPath + "/" + Layout::kDoneSource;
        EQT::ToolTGA::Image img;
        std::string error;
        if (!EQT::ToolTGA::load(path, img, error)) {
            fprintf(stderr, "Error: %s\n", error.c_str());
            return 1;
        }
        if (!blitRect(img, Layout::kDoneSrcX, Layout::kDoneSrcY,
                      Layout::kDoneSize, Layout::kDoneSize, Layout::kDoneDstX,
                      Layout::kDoneDstY, Layout::kDoneSource)) {
            fprintf(stderr, "Refusing to write a partial spellbook backdrop "
                            "(the Done X did not land).\n");
            return 1;
        }
    }

    const std::string outPath = outputDir + "/" + Layout::kShippedFile;
    if (!willeq_tools::writeBMP32(outPath, g_canvas.data(), Layout::kCanvasW,
                                  Layout::kCanvasH)) {
        fprintf(stderr, "Error: failed to write %s\n", outPath.c_str());
        return 1;
    }
    printf("Wrote %s (%dx%d)\n", outPath.c_str(), Layout::kCanvasW,
           Layout::kCanvasH);
    printf("Composed 4 backdrop tiles + the Done X from %s.\n",
           uifilesPath.c_str());
    return 0;
}
