// item_icon_atlas.h -- the layout of data/textures/item_icons{0,1}.bmp, the
// item-icon sheets lifted from the official client's own dragitem*.tga art.
//
// T430 (board B376; Will 2026-09-07: "CRITICAL. The official
// uifiles/default/dragitem*.tga sheets are the source."). Every panel that
// shows an item has carried the server's icon id since S2 put it on
// InventorySlotChangedData, and not one of them had art to draw for it: the
// inventory popup painted the first three characters of the item's NAME in a
// 32x32 slot, the tradeskill grid did the same, and T421's item display
// window painted the icon NUMBER inside IDW_IconButton's frame rather than a
// wrong picture. This header is the art's address book.
//
// ONE SOURCE OF TRUTH, the SpellSpriteAtlas precedent (T417). This header is
// included by `tools/item_icon_atlas_builder` (which writes the BMPs) and by
// the client's `ItemIconAtlas` runtime loader, so the sheets and the client's
// addressing of them cannot drift. `tests/test_item_icon_atlas.cpp` pins the
// builder's output against it.
//
// ── THE SOURCE SHEETS ───────────────────────────────────────────────────────
// `<uifiles>/default/dragitem1.tga` .. `dragitem34.tga`, 34 files, each
// 256x256 32-bit TGA. The census is not assumed; it is QUOTED from the
// oracle's own `EQUI_Animations.xml`, whose single `A_DragItem` animation
// lists exactly those 34 textures in that order and states the grid:
//
//     <Ui2DAnimation item="A_DragItem">
//         <Cycle>false</Cycle>
//         <Grid>true</Grid>
//         <Vertical>true</Vertical>
//         <CellHeight>40</CellHeight>
//         <CellWidth>40</CellWidth>
//         <Frames>
//             <Texture>dragitem1.tga</Texture>
//             <Location><X>0</X><Y>0</Y></Location>
//             <Size><CX>256</CX><CY>256</CY></Size>
//             ...
//         </Frames>
//         ...  (34 <Frames> blocks, dragitem1.tga .. dragitem34.tga)
//     </Ui2DAnimation>
//
// So: 40x40 cells on a 256x256 texture is 6 columns x 6 rows = 36 cells per
// sheet (the last 16 px of each axis are unused), 34 x 36 = 1224 icons, and
// the id space runs 500 .. 1723.
//
// ── COLUMN-MAJOR, AND HOW THAT WAS PROVEN ──────────────────────────────────
// `<Vertical>true</Vertical>` says the grid advances DOWN a column before
// moving to the next column, i.e. the cell for index k inside a sheet is
// column k/6, row k%6 -- NOT the row-major (k/6 = row) reading that the
// usual "(id-500)/36, (id-500)%36" folklore implies.
//
// That is not taken on the tag's word. dragitem34, the LAST sheet, is only
// partly filled, and a partly filled sheet shows its own fill order. A
// per-cell alpha census of it reads:
//
//     [1, 1, 1, 0, 0, 0]
//     [1, 1, 0, 0, 0, 0]
//     [1, 1, 0, 0, 0, 0]
//     [1, 1, 0, 0, 0, 0]
//     [1, 1, 0, 0, 0, 0]
//     [1, 1, 0, 0, 0, 0]
//
// -- column 0 full, column 1 full, then one cell at the top of column 2.
// Thirteen icons, filled COLUMN BY COLUMN. A row-major numbering would have
// produced two full rows and a third row of one. It is the only partly filled
// sheet, so it is the only one that could answer the question, and it answers
// it unambiguously.
//
// THE CENSUS IS 1200, NOT 1201, AND THE MISSING ONE IS NOT AT THE END.
// dragitem21 has a fully transparent cell in the MIDDLE of its grid --
// column 1, row 4, i.e. column-major index 10, flat index 730, icon id
// **1230**. Every other cell of every other sheet below 34 carries art. So
// 33 x 36 - 1 + 13 = 1200. A hole in the middle of the id space is exactly
// why `indexForIconId()` does not treat "populated" as a bound: id 1230 maps
// to a real, addressable, empty cell and draws nothing, which is the same
// thing the official client does with it.
//
// ── THE PACKED ATLASES, AND WHY TWO ────────────────────────────────────────
// The icons are re-packed rather than shipped as 34 whole sheets: a source
// sheet wastes 16 px on each axis, and 34 pack entries would be 34 texture
// binds. Cells stay at their NATIVE 40x40 -- the oracle draws them at 40x40
// (`<DecalSize><CX>40</CX><CY>40</CY></DecalSize>` on every A_DragItem
// button), so any rescale here would be a fidelity loss chosen for nothing.
//
// 1024x1024 at a 40 px pitch holds 25 x 25 = 625 cells, so 1224 icons need
// TWO atlases (1250 cells, 26 spare). The alternatives were both worse: one
// 2048x2048 sheet costs 2601 cells' worth of ETC1 (4x the texels of one
// 1024) for the same 1224 icons, and a 1024x2048 sheet is the same bytes as
// two 1024x1024 with none of the eviction granularity.
//
// 40 and 1024 are both multiples of 4, so every cell boundary in the base
// level is an ETC1 4x4 block boundary and no icon can bleed into its
// neighbour when `eqe1_builder` compresses the sheet (T398 Leg B's
// coin-pitch rule).
//
// COST. Each atlas is an ordinary pack entry: ETC1 with mips plus an ETC1
// `_alpha` companion (T411). Item icons are cut-outs on transparency, so the
// companion is load-bearing -- these are not a `--rgba8888-texture` case
// (board B287's rule; the UI samples them through UI2D_FS, which reads unit
// 1). The pack and every zone's `eqe1MappedBytes` grow by the two entries;
// the measured figures are in the T430 Review.

#pragma once

#include <cstdint>

namespace EQT {
namespace Graphics {
namespace ItemIconAtlasLayout {

// ── The source sheets (quoted from EQUI_Animations.xml, see above) ─────────
constexpr int kSheetCount = 34;
constexpr int kSheetGrid = 6;                            // 256 / 40, both axes
constexpr int kCellsPerSheet = kSheetGrid * kSheetGrid;  // 36
constexpr int kSourceSheetEdge = 256;

// The first item icon id. Icon 0 means "no icon" everywhere in the wire
// format; the art starts at 500.
constexpr uint32_t kFirstIconId = 500;
constexpr int kIconCount = kSheetCount * kCellsPerSheet;  // 1224

// Icons that actually carry art. dragitem34 is filled only 13 cells deep and
// dragitem21 has one transparent cell in its middle (see the census above).
// This is a fact about the ART, not a bound the lookup enforces -- the empty
// cells are addressable and resolve to nothing drawn.
constexpr int kPopulatedIconCount = 33 * kCellsPerSheet - 1 + 13;  // 1200

// The one hole below the end of the id space, named so a future census
// change is a deliberate edit rather than a surprise.
constexpr uint32_t kEmptyIconIdInsideRange = 1230;  // dragitem21, col 1 row 4

// ── The packed atlases ────────────────────────────────────────────────────
constexpr int kCellSize = 40;
constexpr int kAtlasEdge = 1024;
constexpr int kAtlasColumns = kAtlasEdge / kCellSize;              // 25
constexpr int kAtlasRows = kAtlasEdge / kCellSize;                 // 25
constexpr int kCellsPerAtlas = kAtlasColumns * kAtlasRows;         // 625
constexpr int kAtlasCount =
    (kIconCount + kCellsPerAtlas - 1) / kCellsPerAtlas;            // 2

static_assert(kAtlasCount * kCellsPerAtlas >= kIconCount,
              "the item icon atlases do not hold every icon");
static_assert(kCellSize % 4 == 0 && kAtlasEdge % 4 == 0,
              "a cell boundary must be an ETC1 4x4 block boundary");

// Pack entry / BMP basename for atlas `atlas`. The BMP is
// `data/textures/<name>.bmp` and the pack entry is `<name>`, exactly the
// route every other shipped BMP takes.
inline const char* atlasName(int atlas) {
    switch (atlas) {
        case 0: return "item_icons0";
        case 1: return "item_icons1";
        default: return "";
    }
}

// Source file basename for sheet `sheet` (0-based), i.e. dragitem1.tga for
// sheet 0. The builder joins this to `<eq-path>/uifiles/default/`.
// `buffer` must hold at least 20 chars; returns it.
inline const char* sourceSheetName(int sheet, char* buffer) {
    const int n = sheet + 1;
    int i = 0;
    const char* prefix = "dragitem";
    while (*prefix) buffer[i++] = *prefix++;
    if (n >= 10) buffer[i++] = static_cast<char>('0' + n / 10);
    buffer[i++] = static_cast<char>('0' + n % 10);
    const char* suffix = ".tga";
    while (*suffix) buffer[i++] = *suffix++;
    buffer[i] = '\0';
    return buffer;
}

// ── The mapping ───────────────────────────────────────────────────────────

// Flat 0-based icon index for a server icon id, or -1 if this atlas set does
// not carry it (which includes id 0, "no icon").
inline int indexForIconId(uint32_t iconId) {
    if (iconId < kFirstIconId) return -1;
    const uint32_t index = iconId - kFirstIconId;
    if (index >= static_cast<uint32_t>(kIconCount)) return -1;
    return static_cast<int>(index);
}

// Where icon `index` lives in the SOURCE art: which dragitem sheet, and the
// top-left pixel of its cell inside that 256x256 sheet. Column-major --
// `<Vertical>true</Vertical>`, proven by dragitem34's fill.
inline void sourceCell(int index, int& sheet, int& x, int& y) {
    sheet = index / kCellsPerSheet;
    const int k = index % kCellsPerSheet;
    x = (k / kSheetGrid) * kCellSize;
    y = (k % kSheetGrid) * kCellSize;
}

// Which packed atlas holds icon `index`.
inline int atlasForIndex(int index) { return index / kCellsPerAtlas; }

// Top-left pixel of icon `index` inside its packed atlas. The packing is
// row-major within the atlas -- there is no oracle convention to honour here,
// the source order is what matters and it is preserved -- so a contact sheet
// of atlas 0 reads icons 500.. left to right, top to bottom.
inline void atlasCellOrigin(int index, int& x, int& y) {
    const int cell = index % kCellsPerAtlas;
    x = (cell % kAtlasColumns) * kCellSize;
    y = (cell / kAtlasColumns) * kCellSize;
}

}  // namespace ItemIconAtlasLayout
}  // namespace Graphics
}  // namespace EQT
