/*
 * T430 (board B376) -- the shipped item icon sheets are the official client's
 * own dragitem art, cell for cell, and the id -> cell mapping is the one the
 * oracle's EQUI_Animations.xml declares.
 *
 * Will ruled 2026-09-07: CRITICAL, "the official uifiles/default/dragitem*.tga
 * sheets are the source". Before this unit fable had no item-icon art at all:
 * the inventory popup painted the first three characters of an item's NAME in
 * a 32x32 slot, the tradeskill grid did the same, and the item display window
 * painted the icon NUMBER inside IDW_IconButton's frame.
 *
 * WHAT THIS PINS, and why each case exists:
 *
 *  1. The CENSUS. 34 sheets x 36 cells = 1224 icons over ids 500..1723, and
 *     1200 of those cells carry art -- dragitem34 stops after thirteen AND
 *     dragitem21 has a transparent cell in the MIDDLE of its grid (icon id
 *     1230). Both numbers are read back off the source files, not asserted
 *     from the header alone, so a change to the mounted client tree is a
 *     failure rather than a silently different atlas.
 *
 *  2. COLUMN-MAJOR. `<Vertical>true</Vertical>` in the A_DragItem animation
 *     means a sheet fills DOWN a column before moving right, so the cell for
 *     index k is column k/6, row k%6 -- the OPPOSITE of the row-major reading
 *     the usual "(id-500)/36, (id-500)%36" folklore implies. The test asserts
 *     it two ways: dragitem34's partial fill is column-shaped (twelve cells in
 *     two full columns plus one), and the shipped atlas cell for a specific
 *     id equals the COLUMN-major source cell and NOT the row-major one. The
 *     second half is what makes a "cleanup" back to row-major fail here
 *     instead of shipping 1224 wrong pictures.
 *
 *  3. EVERY CELL. All 1224 cells of the shipped BMPs equal their source cell,
 *     so a builder change that altered one icon is a failure rather than a
 *     subtly wrong picture nobody notices.
 *
 * Reads files only: the two BMPs and the 34 TGAs. No device, no GPU, no
 * renderer. The TGA decode is the same header-only `tools/tga_reader.h` the
 * builder uses, so the expected pixels are re-derived from the source rather
 * than from a golden nobody can re-check. If the EQ client tree is not
 * mounted the source cases SKIP, matching every other test that needs
 * /mnt/projects/EverQuestP1999.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/ui/item_icon_atlas.h"
#include "client/graphics/ui/ui_renderer.h"
#include "tga_reader.h"

namespace Layout = EQT::Graphics::ItemIconAtlasLayout;

namespace {

const char* kUifilesDir = "/mnt/projects/EverQuestP1999/uifiles/default";

std::string atlasPath(int atlas) {
    return std::string(WILLEQ_SOURCE_DIR) + "/data/textures/" +
           Layout::atlasName(atlas) + ".bmp";
}

std::string sheetPath(int sheet) {
    char buf[24] = {};
    return std::string(kUifilesDir) + "/" + Layout::sourceSheetName(sheet, buf);
}

// A decoded 32-bit BMP: top-down RGBA, whatever the file's own row order.
// Same reader test_spell_sprite_atlas.cpp uses.
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
            // BMP stores B,G,R,A; compared as RGBA.
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
    std::ifstream f(sheetPath(0), std::ios::binary);
    return f.good();
}

// Total alpha over a 40x40 cell of a decoded source sheet.
uint64_t cellAlpha(const EQT::ToolTGA::Image& img, int x, int y) {
    uint64_t sum = 0;
    for (int row = 0; row < Layout::kCellSize; ++row) {
        const uint8_t* r =
            img.rgba.data() +
            (static_cast<size_t>(y + row) * img.width + x) * 4;
        for (int col = 0; col < Layout::kCellSize; ++col) sum += r[col * 4 + 3];
    }
    return sum;
}

}  // namespace

// ── 1. The layout itself ──────────────────────────────────────────────────

TEST(ItemIconAtlasLayout, MatchesTheAnimationXmlDeclaration) {
    // Quoted from EQUI_Animations.xml's single <Ui2DAnimation item="A_DragItem">:
    // 34 <Frames> blocks (dragitem1.tga..dragitem34.tga), each <Size> 256x256,
    // <CellWidth>40</CellWidth> <CellHeight>40</CellHeight>.
    EXPECT_EQ(Layout::kSheetCount, 34);
    EXPECT_EQ(Layout::kCellSize, 40);
    EXPECT_EQ(Layout::kSourceSheetEdge, 256);
    EXPECT_EQ(Layout::kSheetGrid, 6) << "256 / 40 is 6 whole cells per axis";
    EXPECT_EQ(Layout::kCellsPerSheet, 36);
    EXPECT_EQ(Layout::kIconCount, 1224);
    EXPECT_EQ(Layout::kFirstIconId, 500u);
}

TEST(ItemIconAtlasLayout, IdMapsOnlyInsideTheShippedRange) {
    EXPECT_EQ(Layout::indexForIconId(0), -1) << "0 is 'no icon' on the wire";
    EXPECT_EQ(Layout::indexForIconId(499), -1);
    EXPECT_EQ(Layout::indexForIconId(500), 0);
    EXPECT_EQ(Layout::indexForIconId(1723), Layout::kIconCount - 1);
    EXPECT_EQ(Layout::indexForIconId(1724), -1);
}

// The mapping rule, spelled out on a handful of ids so the column-major
// arithmetic is readable rather than inferred from the loop below.
TEST(ItemIconAtlasLayout, SourceCellIsColumnMajorWithinASheet) {
    int sheet = -1, x = -1, y = -1;

    Layout::sourceCell(Layout::indexForIconId(500), sheet, x, y);
    EXPECT_EQ(sheet, 0);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);

    // Index 1 is the cell BELOW index 0, not the one to its right.
    Layout::sourceCell(Layout::indexForIconId(501), sheet, x, y);
    EXPECT_EQ(sheet, 0);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 40);

    // Index 6 starts the second COLUMN.
    Layout::sourceCell(Layout::indexForIconId(506), sheet, x, y);
    EXPECT_EQ(sheet, 0);
    EXPECT_EQ(x, 40);
    EXPECT_EQ(y, 0);

    // Index 36 is the first cell of the second SHEET.
    Layout::sourceCell(Layout::indexForIconId(536), sheet, x, y);
    EXPECT_EQ(sheet, 1);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
}

TEST(ItemIconAtlasLayout, TwoAtlasesHoldEveryIconWithNoOverlap) {
    ASSERT_EQ(Layout::kAtlasCount, 2);
    ASSERT_EQ(Layout::kCellsPerAtlas, 625);

    std::vector<std::vector<char>> used(
        Layout::kAtlasCount,
        std::vector<char>(static_cast<size_t>(Layout::kAtlasEdge) *
                              Layout::kAtlasEdge / (Layout::kCellSize * Layout::kCellSize),
                          0));

    for (int i = 0; i < Layout::kIconCount; ++i) {
        const int atlas = Layout::atlasForIndex(i);
        ASSERT_GE(atlas, 0);
        ASSERT_LT(atlas, Layout::kAtlasCount);
        int x = 0, y = 0;
        Layout::atlasCellOrigin(i, x, y);
        ASSERT_LE(x + Layout::kCellSize, Layout::kAtlasEdge) << "index " << i;
        ASSERT_LE(y + Layout::kCellSize, Layout::kAtlasEdge) << "index " << i;
        const size_t cell = static_cast<size_t>(y / Layout::kCellSize) *
                                Layout::kAtlasColumns +
                            static_cast<size_t>(x / Layout::kCellSize);
        ASSERT_LT(cell, used[static_cast<size_t>(atlas)].size());
        EXPECT_EQ(used[static_cast<size_t>(atlas)][cell], 0)
            << "two icons share atlas " << atlas << " cell " << cell;
        used[static_cast<size_t>(atlas)][cell] = 1;
    }
}

// ── 2. The shipped BMPs ───────────────────────────────────────────────────

TEST(ItemIconAtlas, ShippedSheetsHaveTheDeclaredGeometry) {
    for (int a = 0; a < Layout::kAtlasCount; ++a) {
        const Bitmap bmp = loadBmp(atlasPath(a));
        ASSERT_TRUE(bmp.ok) << atlasPath(a) << " is missing or not a 32-bit BMP";
        EXPECT_EQ(bmp.width, Layout::kAtlasEdge);
        EXPECT_EQ(bmp.height, Layout::kAtlasEdge);
        EXPECT_EQ(bmp.bitCount, 32)
            << "item icons are cut-outs on transparency; a 24-bit sheet has "
               "thrown the alpha away";
    }
}

// ── 3. Against the oracle's own art ───────────────────────────────────────

TEST(ItemIconAtlas, Dragitem34FillsColumnByColumn) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }

    // The LAST sheet is the only partly-filled one, so it is the only sheet
    // that can show its own fill order. If icons were numbered row-major it
    // would read as two full ROWS plus one; it reads as two full COLUMNS
    // plus one, which is <Vertical>true</Vertical>.
    EQT::ToolTGA::Image img;
    std::string error;
    ASSERT_TRUE(EQT::ToolTGA::load(sheetPath(33), img, error)) << error;

    int populated = 0;
    for (int col = 0; col < Layout::kSheetGrid; ++col) {
        for (int row = 0; row < Layout::kSheetGrid; ++row) {
            const bool hasArt =
                cellAlpha(img, col * Layout::kCellSize, row * Layout::kCellSize) > 0;
            const int columnMajorIndex = col * Layout::kSheetGrid + row;
            // Column-major numbering makes the populated cells exactly the
            // first thirteen indices and the empty ones exactly the rest.
            EXPECT_EQ(hasArt, columnMajorIndex < 13)
                << "dragitem34 col=" << col << " row=" << row
                << " column-major index " << columnMajorIndex;
            if (hasArt) ++populated;
        }
    }
    EXPECT_EQ(populated, 13);
}

TEST(ItemIconAtlas, EveryCellEqualsItsSourceCell) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }

    std::vector<Bitmap> atlases;
    for (int a = 0; a < Layout::kAtlasCount; ++a) {
        atlases.push_back(loadBmp(atlasPath(a)));
        ASSERT_TRUE(atlases.back().ok) << atlasPath(a);
    }

    int populated = 0;
    for (int sheet = 0; sheet < Layout::kSheetCount; ++sheet) {
        EQT::ToolTGA::Image img;
        std::string error;
        ASSERT_TRUE(EQT::ToolTGA::load(sheetPath(sheet), img, error)) << error;
        ASSERT_EQ(img.width, Layout::kSourceSheetEdge);
        ASSERT_EQ(img.height, Layout::kSourceSheetEdge);

        for (int k = 0; k < Layout::kCellsPerSheet; ++k) {
            const int index = sheet * Layout::kCellsPerSheet + k;
            int checkSheet = 0, sx = 0, sy = 0;
            Layout::sourceCell(index, checkSheet, sx, sy);
            ASSERT_EQ(checkSheet, sheet);
            if (cellAlpha(img, sx, sy) > 0) ++populated;

            const Bitmap& bmp = atlases[static_cast<size_t>(
                Layout::atlasForIndex(index))];
            int dx = 0, dy = 0;
            Layout::atlasCellOrigin(index, dx, dy);

            for (int row = 0; row < Layout::kCellSize; ++row) {
                const uint8_t* src =
                    img.rgba.data() +
                    (static_cast<size_t>(sy + row) * img.width + sx) * 4;
                const uint8_t* dst = bmp.at(dx, dy + row);
                ASSERT_EQ(std::memcmp(src, dst,
                                      static_cast<size_t>(Layout::kCellSize) * 4),
                          0)
                    << "icon id " << (Layout::kFirstIconId + index)
                    << " row " << row << " differs from its source cell";
            }
        }
    }

    EXPECT_EQ(populated, Layout::kPopulatedIconCount)
        << "the number of icons carrying art changed; re-take the census";
    EXPECT_EQ(populated, 1200);
}

// The negative half of the column-major proof. A row-major reading of the
// same id lands on a DIFFERENT source cell for most ids, and the shipped
// atlas must not match it.
TEST(ItemIconAtlas, RowMajorReadingWouldBeWrong) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }

    EQT::ToolTGA::Image img;
    std::string error;
    ASSERT_TRUE(EQT::ToolTGA::load(sheetPath(0), img, error)) << error;

    // Icon 506 is index 6: column 1 row 0 (column-major) or column 0 row 1
    // (row-major). Those two cells hold different art in dragitem1.
    const int index = Layout::indexForIconId(506);
    ASSERT_EQ(index, 6);

    const Bitmap bmp = loadBmp(atlasPath(0));
    ASSERT_TRUE(bmp.ok);
    int dx = 0, dy = 0;
    Layout::atlasCellOrigin(index, dx, dy);

    const uint8_t* shipped = bmp.at(dx, dy);
    const uint8_t* columnMajor =
        img.rgba.data() + (static_cast<size_t>(0) * img.width + 40) * 4;
    const uint8_t* rowMajor =
        img.rgba.data() + (static_cast<size_t>(40) * img.width + 0) * 4;

    const size_t rowBytes = static_cast<size_t>(Layout::kCellSize) * 4;
    EXPECT_EQ(std::memcmp(shipped, columnMajor, rowBytes), 0)
        << "icon 506 is not the column-major cell";
    EXPECT_NE(std::memcmp(columnMajor, rowMajor, rowBytes), 0)
        << "dragitem1's (1,0) and (0,1) cells are identical, so this id "
           "cannot discriminate the two orders -- pick another";
}

// The hole in the middle of the id space. It is NOT the end of the range, so
// a lookup that treated "populated" as a bound would be wrong for every id
// above it.
TEST(ItemIconAtlas, TheEmptyCellInsideTheRangeStillMaps) {
    const int index = Layout::indexForIconId(Layout::kEmptyIconIdInsideRange);
    ASSERT_GE(index, 0) << "id 1230 must still map -- it is an addressable cell";
    EXPECT_LT(index, Layout::kIconCount);

    int sheet = 0, x = 0, y = 0;
    Layout::sourceCell(index, sheet, x, y);
    EXPECT_EQ(sheet, 20) << "dragitem21";
    EXPECT_EQ(x, 40) << "column 1";
    EXPECT_EQ(y, 160) << "row 4";

    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    EQT::ToolTGA::Image img;
    std::string error;
    ASSERT_TRUE(EQT::ToolTGA::load(sheetPath(20), img, error)) << error;
    EXPECT_EQ(cellAlpha(img, x, y), 0u)
        << "dragitem21's (1,4) cell is no longer empty; re-take the census";
}

// B1078 (GitLab #398): the dragitem decal is a SQUARE (40x40, the oracle's
// own DecalSize). A cell that is not square must never stretch it, so
// drawItemIcon blits into UIRenderer::squareIn(rect) -- the centred square
// of side min(w,h) -- and every call site funnels through that one helper.
// This pins the helper's own arithmetic: a square cell comes back unchanged
// (every current call site is a square, so the drawing is pixel-identical to
// pre-B1078), and a non-square cell comes back as a centred square of the
// short side's size, never the long side and never the cell's own ratio.
TEST(ItemIconAtlasSquareIn, ACellThatIsAlreadySquareIsUntouched) {
    const auto cell = irr::core::rect<irr::s32>(10, 20, 50, 60);
    EXPECT_EQ(EQT::Graphics::UIRenderer::squareIn(cell), cell);
}

TEST(ItemIconAtlasSquareIn, ACellThatIsNotSquareBecomesACentredSquare) {
    // The pre-B1078 merchant row: 25x14 -- the decal was blitted stretched.
    const auto wide = irr::core::rect<irr::s32>(100, 50, 125, 64);
    const auto q = EQT::Graphics::UIRenderer::squareIn(wide);
    EXPECT_EQ(q.getWidth(), 14) << "side is the short side";
    EXPECT_EQ(q.getHeight(), 14);
    // Centred: the cell's centre (112, 57) stays the square's centre.
    EXPECT_EQ(q.getCenter().X, 112);
    EXPECT_EQ(q.getCenter().Y, 57);
    EXPECT_EQ(q, irr::core::rect<irr::s32>(105, 50, 119, 64));
}

TEST(ItemIconAtlasSquareIn, APortraitCellSquaresOnTheShortSideToo) {
    const auto tall = irr::core::rect<irr::s32>(0, 0, 20, 60);
    const auto q = EQT::Graphics::UIRenderer::squareIn(tall);
    EXPECT_EQ(q, irr::core::rect<irr::s32>(0, 20, 20, 40));
}

TEST(ItemIconAtlasSquareIn, AnEmptyCellYieldsAnEmptySquare) {
    EXPECT_EQ(EQT::Graphics::UIRenderer::squareIn(
                  irr::core::rect<irr::s32>(5, 5, 5, 5)),
              irr::core::rect<irr::s32>(5, 5, 5, 5));
}
