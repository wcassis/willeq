/*
 * B1077 (GitLab #397) -- the shipped spell icon sheets are the official
 * client's own gemicons/Spells/Spell_Icons art, cell for cell, and the id ->
 * cell mapping is the one EQUI_Animations.xml declares (row-major,
 * zero-based).
 *
 * Will's 2026-10-15 play session: "Spell icons were missing from the
 * spellbook. Spell gems on the spell gem hotbar are also missing." Before
 * this unit fable had no spell-icon art at all: the ids were plumbed
 * (SpellGemState::iconId <- spell->gem_icon, SpellbookDisplayEntry::iconId /
 * buffs <- spell->spell_icon) and the gem bar painted the first four letters
 * of the spell name, the buff bar a flat 20x20 rect, and the hotbar comment
 * said so outright.
 *
 * WHAT THIS PINS, and why each case exists:
 *
 *  1. The CENSUS. 3 gemicon sheets x 63 cells (7x9 of 36x28), 7 spell sheets
 *     x 36 (6x6 of 40x40) and 1 buff sheet of 36 cells, 23 of which carry
 *     art (the empties are the trailing run ids 23..35). The gem and spell
 *     families are FULL; the buff sheet is the one part-filled family. Both
 *     numbers are read back off the source files, not asserted from the
 *     header alone, so a change to the mounted client tree is a failure
 *     rather than a silently different atlas.
 *
 *  2. ROW-MAJOR, ZERO-BASED. All three families are
 *     `<Vertical>false</Vertical>` and the salvage code's arithmetic
 *     (iconId / 36, iconId % 36) is zero-based. The test asserts it three
 *     ways: (a) the shipped atlas cell for hand-read ids (gem 24 = Glimpse,
 *     25 = Rejuvenation, 26 = Firefist, 35 = Summon Corpse, 51 = Firestrike
 *     -- the ones whose art was vision-verified against the spells,
 *     2026-10-16) equals the ROW-major source cell; (b) the column-major
 *     reading of a gem id lands on a DIFFERENT cell than the shipped one;
 *     (c) the part-filled buff sheet's FILL alone proves the order: id 24 is
 *     an EMPTY cell row-major (row 4 col 0) but a POPULATED cell
 *     column-major (row 0 col 4), so a column-major builder is a red test
 *     even before its 189 wrong pictures reach a screen.
 *
 *  3. EVERY CELL. Every cell of the three shipped family BMPs equals its
 *     source cell (the gem art at its native 36x28 in the 40x40 cell, the
 *     rest transparent), and the holder BMP equals the CW_SpellGemHolder
 *     piece of classic_pieces01.tga at (41,123) 36x28 -- the holder
 *     EQUI_CastSpellWnd.xml names and locally defines (the B1077 fix leg's
 *     CW-vs-A ruling, docs/decisions.md), not the 39x32 A_SpellGemHolder of
 *     window_pieces02.tga.
 *
 * Reads files only: the four BMPs and the 11 source TGAs. No device, no
 * GPU, no renderer. The TGA decode is the same header-only
 * `tools/tga_reader.h` the builder uses, so the expected pixels are
 * re-derived from the source rather than from a golden nobody can re-check.
 * If the EQ client tree is not mounted the source cases SKIP, matching every
 * other test that needs /mnt/projects/EverQuestP1999.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "client/graphics/ui/spell_icon_atlas.h"
#include "tga_reader.h"

namespace Layout = EQT::Graphics::SpellIconAtlasLayout;
using Layout::SpellIconSheet;

namespace {

const char* kUifilesDir = "/mnt/projects/EverQuestP1999/uifiles/default";

std::string atlasPath(SpellIconSheet sheet) {
    return std::string(WILLEQ_SOURCE_DIR) + "/data/textures/" +
           Layout::atlasName(sheet) + ".bmp";
}

std::string holderPath() {
    return std::string(WILLEQ_SOURCE_DIR) + "/data/textures/" +
           Layout::holderName() + ".bmp";
}

// A decoded 32-bit BMP: top-down RGBA, whatever the file's own row order.
// Same reader test_item_icon_atlas.cpp uses.
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
    std::ifstream f(std::string(kUifilesDir) + "/gemicons01.tga",
                    std::ios::binary);
    if (!f.good()) return false;
    f.clear();
    std::ifstream g(std::string(kUifilesDir) + "/classic_pieces01.tga",
                    std::ios::binary);
    return g.good();
}

// Total alpha over one SOURCE cell of a decoded source sheet.
uint64_t cellAlpha(const EQT::ToolTGA::Image& img, int x, int y, int cw,
                   int ch) {
    uint64_t sum = 0;
    for (int row = 0; row < ch; ++row) {
        const uint8_t* r =
            img.rgba.data() +
            (static_cast<size_t>(y + row) * img.width + x) * 4;
        for (int col = 0; col < cw; ++col) sum += r[col * 4 + 3];
    }
    return sum;
}

// Load every source sheet of a family. `missing` names the sheet that
// failed.
bool loadFamily(SpellIconSheet sheet,
                std::vector<EQT::ToolTGA::Image>& sheets, std::string& error) {
    const int count =
        sheet == SpellIconSheet::Gems ? Layout::kGemSheetCount
        : sheet == SpellIconSheet::Spells ? Layout::kSpellSheetCount
                                          : Layout::kBuffSheetCount;
    sheets.assign(count, EQT::ToolTGA::Image());
    for (int s = 0; s < count; ++s) {
        char name[24] = {};
        const std::string path =
            std::string(kUifilesDir) + "/" +
            Layout::sourceSheetName(sheet, s, name);
        if (!EQT::ToolTGA::load(path, sheets[static_cast<size_t>(s)], error))
            return false;
    }
    return true;
}

// The source pixels a family id must appear as in its packed atlas cell.
void expectedCell(SpellIconSheet sheet, const std::vector<EQT::ToolTGA::Image>&
                      sheets, uint32_t id, std::vector<uint8_t>& out) {
    int cw = 0, ch = 0;
    Layout::cellSize(sheet, cw, ch);
    out.assign(static_cast<size_t>(cw) * ch * 4, 0);
    int sh = 0, x = 0, y = 0;
    Layout::sourceCell(sheet, id, sh, x, y);
    const EQT::ToolTGA::Image& img = sheets[static_cast<size_t>(sh)];
    for (int row = 0; row < ch; ++row) {
        const uint8_t* s = img.rgba.data() +
                           (static_cast<size_t>(y + row) * img.width + x) * 4;
        std::memcpy(out.data() + static_cast<size_t>(row) * cw * 4, s,
                    static_cast<size_t>(cw) * 4);
    }
}

}  // namespace

// ── 1. The layout itself ──────────────────────────────────────────────────

TEST(SpellIconAtlasLayout, MatchesTheAnimationXmlDeclaration) {
    // A_SpellGems (EQUI_Animations.xml line 5823): 36x28 cells, 3 sheets.
    EXPECT_EQ(Layout::kGemCellW, 36);
    EXPECT_EQ(Layout::kGemCellH, 28);
    EXPECT_EQ(Layout::kGemCols, Layout::kSourceSheetEdge / 36);  // 7
    EXPECT_EQ(Layout::kGemRows, Layout::kSourceSheetEdge / 28);  // 9
    EXPECT_EQ(Layout::kGemCellsPerSheet, 63);
    EXPECT_EQ(Layout::kGemIconCount, 189);
    EXPECT_EQ(Layout::kGemSheetCount, 3);
    // A_SpellIcons (line 5666): 40x40 cells, 7 sheets.
    EXPECT_EQ(Layout::kSpellCellsPerSheet, 36);
    EXPECT_EQ(Layout::kSpellIconCount, 252);
    EXPECT_EQ(Layout::kSpellSheetCount, 7);
    // BuffIcons (line 5643): 40x40 cells, one sheet.
    EXPECT_EQ(Layout::kBuffIconCount, 36);
    EXPECT_EQ(Layout::kBuffSheetCount, 1);
    // The packed geometry: one 1024x1024 atlas per family, 40 px pitch.
    EXPECT_EQ(Layout::kCellSize, 40);
    EXPECT_EQ(Layout::kAtlasEdge, 1024);
    EXPECT_EQ(Layout::kCellsPerAtlas, 625);
    // The holder piece (CW_SpellGemHolder, EQUI_CastSpellWnd.xml line 5:
    // classic_pieces01.tga (41,123) 36x28 -- the holder the gem bar's own
    // window names and defines).
    EXPECT_EQ(Layout::kHolderW, 36);
    EXPECT_EQ(Layout::kHolderH, 28);
    EXPECT_EQ(Layout::kHolderSrcX, 41);
    EXPECT_EQ(Layout::kHolderSrcY, 123);
    EXPECT_STREQ(Layout::atlasName(SpellIconSheet::Gems), "spell_icons_gems");
    EXPECT_STREQ(Layout::atlasName(SpellIconSheet::Spells),
                 "spell_icons_spells");
    EXPECT_STREQ(Layout::atlasName(SpellIconSheet::Buff), "spell_icons_buff");
    EXPECT_STREQ(Layout::holderName(), "spell_icon_holder");
}

TEST(SpellIconAtlasLayout, IdsMapZeroBasedAndRejectTheUncovered) {
    // Zero-based (the salvage code's arithmetic): id 0 is the first cell.
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Gems, 0), 0);
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Gems, 24), 24);
    // -1 (the export's sentinel, 112 rows) and ids past the family's end
    // (gem 189+ outruns the 189 cells) carry no art.
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Gems, 2147483647u), -1);
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Gems, 189), -1);
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Spells, 252), -1);
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Buff, 36), -1);
    EXPECT_EQ(Layout::indexForId(SpellIconSheet::Buff, 35), 35);
}

TEST(SpellIconAtlasLayout, SourceCellIsRowMajorZeroBased) {
    // Row-major: the cell for index k is row k/cols, col k%cols. The gem
    // family is 7 wide, the 40x40 families 6.
    int sh = 0, x = 0, y = 0;
    Layout::sourceCell(SpellIconSheet::Gems, 0, sh, x, y);
    EXPECT_EQ(sh, 0); EXPECT_EQ(x, 0); EXPECT_EQ(y, 0);
    Layout::sourceCell(SpellIconSheet::Gems, 24, sh, x, y);
    EXPECT_EQ(sh, 0);
    EXPECT_EQ(x, (24 % 7) * 36);      // col 3
    EXPECT_EQ(y, (24 / 7) * 28);      // row 3
    Layout::sourceCell(SpellIconSheet::Gems, 63, sh, x, y);
    EXPECT_EQ(sh, 1); EXPECT_EQ(x, 0); EXPECT_EQ(y, 0);  // sheet 2, cell 0
    Layout::sourceCell(SpellIconSheet::Gems, 188, sh, x, y);
    EXPECT_EQ(sh, 2);
    EXPECT_EQ(x, ((188 % 63) % 7) * 36);
    EXPECT_EQ(y, ((188 % 63) / 7) * 28);
    Layout::sourceCell(SpellIconSheet::Spells, 24, sh, x, y);
    EXPECT_EQ(sh, 0);
    EXPECT_EQ(x, (24 % 6) * 40);      // col 0
    EXPECT_EQ(y, (24 / 6) * 40);      // row 4
    Layout::sourceCell(SpellIconSheet::Buff, 35, sh, x, y);
    EXPECT_EQ(sh, 0);
    EXPECT_EQ(x, 5 * 40);
    EXPECT_EQ(y, 5 * 40);
}

TEST(SpellIconAtlasLayout, OneAtlasHoldsEveryFamily) {
    EXPECT_GE(Layout::kCellsPerAtlas, Layout::kGemIconCount);
    EXPECT_GE(Layout::kCellsPerAtlas, Layout::kSpellIconCount);
    EXPECT_GE(Layout::kCellsPerAtlas, Layout::kBuffIconCount);
    int x = 0, y = 0;
    Layout::atlasCellOrigin(SpellIconSheet::Gems, 0, x, y);
    EXPECT_EQ(x, 0); EXPECT_EQ(y, 0);
    Layout::atlasCellOrigin(SpellIconSheet::Gems, 24, x, y);
    EXPECT_EQ(x, (24 % 25) * 40);
    EXPECT_EQ(y, (24 / 25) * 40);
    Layout::atlasCellOrigin(SpellIconSheet::Spells, 251, x, y);
    EXPECT_EQ(x, (251 % 25) * 40);
    EXPECT_EQ(y, (251 / 25) * 40);
    EXPECT_LE(x + Layout::kCellSize, Layout::kAtlasEdge);
    EXPECT_LE(y + Layout::kCellSize, Layout::kAtlasEdge);
}

// ── 2. The shipped BMPs ───────────────────────────────────────────────────

TEST(SpellIconAtlas, ShippedSheetsHaveTheDeclaredGeometry) {
    for (int s = 0; s < static_cast<int>(SpellIconSheet::Count); ++s) {
        const Bitmap bmp = loadBmp(atlasPath(static_cast<SpellIconSheet>(s)));
        ASSERT_TRUE(bmp.ok)
            << atlasPath(static_cast<SpellIconSheet>(s))
            << " is missing or not a 32-bit BMP";
        EXPECT_EQ(bmp.width, Layout::kAtlasEdge);
        EXPECT_EQ(bmp.height, Layout::kAtlasEdge);
        EXPECT_EQ(bmp.bitCount, 32)
            << "spell icons are cut-outs on transparency; a 24-bit sheet has "
               "thrown the alpha away";
    }
    const Bitmap holder = loadBmp(holderPath());
    ASSERT_TRUE(holder.ok) << holderPath() << " is missing or not a 32-bit BMP";
    EXPECT_EQ(holder.width, Layout::kCellSize);
    EXPECT_EQ(holder.height, Layout::kCellSize);
    EXPECT_EQ(holder.bitCount, 32);
}

// ── 3. The census, re-read from the source TGAs ───────────────────────────

TEST(SpellIconAtlas, CensusMatchesTheHeader) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    const struct {
        SpellIconSheet sheet;
        int expected;
    } families[] = {
        {SpellIconSheet::Gems, Layout::kGemPopulated},
        {SpellIconSheet::Spells, Layout::kSpellPopulated},
        {SpellIconSheet::Buff, Layout::kBuffPopulated},
    };
    for (const auto& fam : families) {
        std::vector<EQT::ToolTGA::Image> sheets;
        std::string error;
        ASSERT_TRUE(loadFamily(fam.sheet, sheets, error)) << error;
        int cw = 0, ch = 0;
        Layout::cellSize(fam.sheet, cw, ch);
        int populated = 0;
        const int perSheet = Layout::cellsPerSheet(fam.sheet);
        const int cols = (fam.sheet == SpellIconSheet::Gems) ? Layout::kGemCols
                                                             : Layout::kSourceSheetEdge / 40;
        for (size_t s = 0; s < sheets.size(); ++s) {
            for (int k = 0; k < perSheet; ++k) {
                const int id = static_cast<int>(s) * perSheet + k;
                const int x = (k % cols) * cw;
                const int y = (k / cols) * ch;
                if (cellAlpha(sheets[s], x, y, cw, ch) != 0) ++populated;
            }
        }
        EXPECT_EQ(populated, fam.expected)
            << Layout::atlasName(fam.sheet) << ": a source sheet changed";
    }
}

// ── 4. The buff sheet is the one part-filled family, and its fill proves
//    the row-major order where the two full families cannot ─────────────────

TEST(SpellIconAtlas, BuffFillProvesRowMajor) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    std::vector<EQT::ToolTGA::Image> sheets;
    std::string error;
    ASSERT_TRUE(loadFamily(SpellIconSheet::Buff, sheets, error)) << error;
    const Bitmap bmp = loadBmp(atlasPath(SpellIconSheet::Buff));
    ASSERT_TRUE(bmp.ok);

    // The sheet is 6x6. Id 24: ROW-major (row 4, col 0) is an EMPTY cell;
    // the COLUMN-major reading of the same id (row 0, col 4) is POPULATED.
    // So the fill alone separates the two orders -- a column-major builder
    // would put the populated top-row art at id 24 and an empty cell where
    // row-major puts id 4.
    const uint32_t id24 = 24;
    const uint32_t id4 = 4;
    // Row-major physical cells: id24 -> (row 4, col 0), id4 -> (row 0, col 4).
    int x24 = 0, y24 = 0;
    {
        int sh = 0;
        Layout::sourceCell(SpellIconSheet::Buff, id24, sh, x24, y24);
    }
    int x4 = 0, y4 = 0;
    {
        int sh = 0;
        Layout::sourceCell(SpellIconSheet::Buff, id4, sh, x4, y4);
    }
    // The row-major mapping is the one the header encodes.
    EXPECT_EQ(y24 / 40, 4);
    EXPECT_EQ(x24 / 40, 0);
    EXPECT_EQ(y4 / 40, 0);
    EXPECT_EQ(x4 / 40, 4);

    // Source: the row-major id-24 cell is empty, id-4 cell is populated.
    EXPECT_EQ(cellAlpha(sheets[0], x24, y24, 40, 40), 0u)
        << "row-major id 24 (row 4 col 0) must be the empty cell";
    EXPECT_GT(cellAlpha(sheets[0], x4, y4, 40, 40), 0u)
        << "row-major id 4 (row 0 col 4) must carry art";

    // The SHIPPED atlas agrees: id 24's 40x40 slot is fully transparent,
    // id 4's carries art. A column-major builder would have swapped these.
    int cx = 0, cy = 0;
    Layout::atlasCellOrigin(SpellIconSheet::Buff, id24, cx, cy);
    bool id24Opaque = false;
    Layout::atlasCellOrigin(SpellIconSheet::Buff, id4, cx, cy);
    bool id4Opaque = false;
    for (int r = 0; r < Layout::kCellSize; ++r)
        for (int c = 0; c < Layout::kCellSize; ++c)
            if (bmp.at(cx + c, cy + r)[3] != 0) id4Opaque = true;
    Layout::atlasCellOrigin(SpellIconSheet::Buff, id24, cx, cy);
    for (int r = 0; r < Layout::kCellSize; ++r)
        for (int c = 0; c < Layout::kCellSize; ++c)
            if (bmp.at(cx + c, cy + r)[3] != 0) id24Opaque = true;
    EXPECT_FALSE(id24Opaque)
        << "the shipped buff cell for id 24 is not the empty one";
    EXPECT_TRUE(id4Opaque)
        << "the shipped buff cell for id 4 does not carry art";
}

// ── 5. Every cell of the shipped atlases equals its source cell ───────────

TEST(SpellIconAtlas, EveryCellEqualsItsSourceCell) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    const struct {
        SpellIconSheet sheet;
        int count;
    } families[] = {
        {SpellIconSheet::Gems, Layout::kGemIconCount},
        {SpellIconSheet::Spells, Layout::kSpellIconCount},
        {SpellIconSheet::Buff, Layout::kBuffIconCount},
    };
    for (const auto& fam : families) {
        std::vector<EQT::ToolTGA::Image> sheets;
        std::string error;
        ASSERT_TRUE(loadFamily(fam.sheet, sheets, error)) << error;
        const Bitmap bmp = loadBmp(atlasPath(fam.sheet));
        ASSERT_TRUE(bmp.ok) << atlasPath(fam.sheet);

        for (int id = 0; id < fam.count; ++id) {
            std::vector<uint8_t> expected;
            expectedCell(fam.sheet, sheets, static_cast<uint32_t>(id),
                         expected);
            int cx = 0, cy = 0;
            Layout::atlasCellOrigin(fam.sheet, static_cast<uint32_t>(id), cx,
                                    cy);
            int cw = 0, ch = 0;
            Layout::cellSize(fam.sheet, cw, ch);
            for (int row = 0; row < Layout::kCellSize; ++row) {
                for (int col = 0; col < Layout::kCellSize; ++col) {
                    const uint8_t* got = bmp.at(cx + col, cy + row);
                    const uint8_t* want =
                        (row < ch && col < cw)
                            ? expected.data() +
                                  (static_cast<size_t>(row) * cw + col) * 4
                            : nullptr;
                    for (int c = 0; c < 4; ++c) {
                        EXPECT_EQ(got[c], want ? want[c] : 0)
                            << "family " << static_cast<int>(fam.sheet)
                            << " id=" << id << " offset ("
                            << col << "," << row << ") channel " << c;
                    }
                }
            }
        }
    }
}

// The hand-read cells: the ids whose art was vision-verified against their
// spells (gem 24 Glimpse, 25 Rejuvenation, 26 Firefist, 35 Summon Corpse,
// 51 Firestrike, 2026-10-16). These pin the row-major, zero-based mapping
// against the ART itself: a column-major reading of the same ids would land
// elsewhere and this test would fail.
TEST(SpellIconAtlas, HandReadIdsResolveToTheOracleCells) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    std::vector<EQT::ToolTGA::Image> gems;
    std::string error;
    ASSERT_TRUE(loadFamily(SpellIconSheet::Gems, gems, error)) << error;
    const Bitmap bmp = loadBmp(atlasPath(SpellIconSheet::Gems));
    ASSERT_TRUE(bmp.ok);

    const struct {
        uint32_t id;
        int sheet;
        int col;   // 0-based, within the 7-wide sheet
        int row;   // 0-based, within the 9-deep sheet
    } hand[] = {
        {24, 0, 3, 3},   // Glimpse -> gemicons01 row 3 col 3
        {25, 0, 4, 3},   // Rejuvenation -> row 3 col 4
        {26, 0, 5, 3},   // Firefist -> row 3 col 5
        {35, 0, 0, 5},   // Summon Corpse/Food -> row 5 col 0
        {51, 0, 2, 7},   // Firestrike -> row 7 col 2
    };
    for (const auto& h : hand) {
        int sh = 0, x = 0, y = 0;
        Layout::sourceCell(SpellIconSheet::Gems, h.id, sh, x, y);
        EXPECT_EQ(sh, h.sheet) << "id " << h.id << " sheet";
        EXPECT_EQ(x, h.col * Layout::kGemCellW) << "id " << h.id << " col";
        EXPECT_EQ(y, h.row * Layout::kGemCellH) << "id " << h.id << " row";

        // The cell carries art (an empty one would be a wrong mapping too).
        EXPECT_GT(cellAlpha(gems[static_cast<size_t>(sh)], x, y,
                            Layout::kGemCellW, Layout::kGemCellH),
                  0u)
            << "id " << h.id << " maps to an empty cell";
    }
}

// The negative half: for ids where row-major and column-major disagree, the
// shipped cell is the ROW-major one and NOT the column-major one.
TEST(SpellIconAtlas, ColumnMajorReadingWouldBeWrong) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    std::vector<EQT::ToolTGA::Image> gems;
    std::string error;
    ASSERT_TRUE(loadFamily(SpellIconSheet::Gems, gems, error)) << error;
    const Bitmap bmp = loadBmp(atlasPath(SpellIconSheet::Gems));
    ASSERT_TRUE(bmp.ok);

    // id 15: row-major (row 2, col 1); column-major of the SAME id is
    // (col 15/9 = 1, row 15%9 = 6), i.e. row-major cell 6*7+1 = 43 --
    // a different cell, and the two carry different art (verified against
    // the source, 2026-10-16). (id 7 happens to resolve to the same
    // physical cell under both orderings and would make this test
    // vacuous, so it must not be used.)
    const uint32_t id = 15;
    std::vector<uint8_t> rowMajor, colMajor;
    {
        int sh = 0, x = 0, y = 0;
        Layout::sourceCell(SpellIconSheet::Gems, id, sh, x, y);
        expectedCell(SpellIconSheet::Gems, gems, id, rowMajor);
        (void)sh; (void)x; (void)y;
    }
    {
        // Column-major of the same id: col id/rows, row id%rows. Its
        // ROW-major global index is row*cols + col.
        const int cols = Layout::kGemCols, rows = Layout::kGemRows;
        const int col = id / rows, row = id % rows;
        const uint32_t colMajorId = static_cast<uint32_t>(row * cols + col);
        expectedCell(SpellIconSheet::Gems, gems, colMajorId, colMajor);
    }
    int cx = 0, cy = 0;
    Layout::atlasCellOrigin(SpellIconSheet::Gems, id, cx, cy);
    bool differ = false;
    for (size_t i = 0; i < rowMajor.size() && i < colMajor.size(); ++i) {
        if (rowMajor[i] != colMajor[i]) { differ = true; break; }
    }
    if (!differ) {
        GTEST_SKIP() << "row-major and column-major cells of id 15 happen to "
                        "be pixel-identical; the negative half is vacuous";
    }
    for (int row = 0; row < Layout::kGemCellH; ++row) {
        for (int col = 0; col < Layout::kGemCellW; ++col) {
            const uint8_t* got = bmp.at(cx + col, cy + row);
            const uint8_t* want =
                rowMajor.data() + (static_cast<size_t>(row) *
                                             Layout::kGemCellW + col) * 4;
            for (int c = 0; c < 4; ++c) {
                EXPECT_EQ(got[c], want[c])
                    << "id 15: the shipped cell is not the row-major cell";
            }
        }
    }
}

// The holder piece, re-read from classic_pieces01.tga.
TEST(SpellIconAtlas, HolderEqualsTheCwWindowPiecesCut) {
    if (!sourcesAvailable()) {
        GTEST_SKIP() << "EQ client tree not mounted at " << kUifilesDir;
    }
    EQT::ToolTGA::Image img;
    std::string error;
    ASSERT_TRUE(EQT::ToolTGA::load(std::string(kUifilesDir) +
                                       "/classic_pieces01.tga",
                                   img, error))
        << error;
    ASSERT_GE(img.width, Layout::kHolderSrcX + Layout::kHolderW);
    ASSERT_GE(img.height, Layout::kHolderSrcY + Layout::kHolderH);

    const Bitmap bmp = loadBmp(holderPath());
    ASSERT_TRUE(bmp.ok);
    for (int row = 0; row < Layout::kCellSize; ++row) {
        for (int col = 0; col < Layout::kCellSize; ++col) {
            const uint8_t* got = bmp.at(col, row);
            const uint8_t* want =
                (row < Layout::kHolderH && col < Layout::kHolderW)
                    ? img.rgba.data() +
                          (static_cast<size_t>(Layout::kHolderSrcY + row) *
                                   img.width +
                           Layout::kHolderSrcX + col) *
                              4
                    : nullptr;
            for (int c = 0; c < 4; ++c) {
                EXPECT_EQ(got[c], want ? want[c] : 0)
                    << "holder offset (" << col << "," << row << ")";
            }
        }
    }
}

