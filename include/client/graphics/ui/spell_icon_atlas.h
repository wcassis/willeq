// spell_icon_atlas.h -- the layout of data/textures/spell_icons{gems,spells,
// buff}.bmp + spell_icon_holder.bmp, the spell-icon art lifted from the
// official client's own gemicons*.tga / spells*.tga / Spell_Icons.tga sheets
// and the CW_SpellGemHolder piece of classic_pieces01.tga (the holder
// EQUI_CastSpellWnd.xml itself names and locally defines).
//
// B1077 (GitLab #397; Will 2026-10-15 play session: "Spell icons were missing
// from the spellbook. Spell gems on the spell gem hotbar are also missing.").
// fable has NEVER shipped spell-icon art: the ids were plumbed end to end
// (`SpellGemState::iconId` <- `spell->gem_icon`, `SpellbookDisplayEntry::iconId`
// / buffs <- `spell->spell_icon`) and nothing could draw for them. This header
// is the art's address book, in the ItemIconAtlas pattern (T430): ONE header
// included by `tools/spell_icon_atlas_builder` (which writes the BMPs) and by
// the client's `SpellIconSheets` runtime loader, so the sheets and the client's
// addressing of them cannot drift. `tests/test_spell_icon_atlas.cpp` pins the
// builder's output against it.
//
// ── THE THREE ID SPACES, SETTLED ────────────────────────────────────────────
// spells_us.txt carries TWO icon columns (tools/willeq_db/db_loaders_text.cpp
// :171-172 binds them; spell_data.h names them):
//
//     field 143  SpellIcon    -> spells.spell_icon
//     field 144  NewIcon      -> spells.gem_icon
//
// Measured 2026-10-16 over the mounted Titanium export (8,066 spell rows,
// data/willeq.db): spell_icon is 0 on EVERY row; gem_icon is -1 on 112 rows
// (resolving to nothing), in-sheet on 7,826 rows, and 128 rows carry one of
// 11 distinct ids that outrun the gem family (189, 190, 191, 193, 194, 195,
// 202, 204, 208, 211, 215). Consequence, and it is the honest reading of the
// data, not a defect: in a Titanium session the SPELLBOOK and the BUFF window
// resolve to cell 0 of their families and draw it, while the GEM BAR is the
// one family whose ids actually carry distinct art. The spell and buff id
// spaces are still shipped and addressable -- they are the oracle's own, and
// a build whose export does populate field 143 resolves against them with no
// code change.
//
// ONE ANIMATION PER FAMILY, quoted from the oracle's own
// uifiles/default/EQUI_Animations.xml (2026-10-16):
//
//     <Ui2DAnimation item="A_SpellGems">    (line 5823)
//         <Vertical>false</Vertical> <CellWidth>36</CellWidth> <CellHeight>28</CellHeight>
//         <Texture>gemicons01.tga</Texture> ... gemicons02, gemicons03
//     <Ui2DAnimation item="A_SpellIcons">   (line 5666)
//         <Vertical>false</Vertical> <CellWidth>40</CellWidth> <CellHeight>40</CellHeight>
//         <Texture>Spells01.tga</Texture> ... Spells07
//     <Ui2DAnimation item="BuffIcons">      (line 5643)
//         <Vertical>false</Vertical> <CellWidth>40</CellWidth> <CellHeight>40</CellHeight>
//         <Texture>Spell_Icons.tga</Texture>
//
// All three families are `<Vertical>false</Vertical>` -- ROW-MAJOR within a
// sheet: the cell for index k is row k/cols, col k%cols. A_DragItem is the one
// column-major family (proven by dragitem34's fill; item_icon_atlas.h); these
// three are NOT. The gem and spell families are full (re-measured 2026-10-16
// by test_spell_icon_atlas.cpp: gemicons 3x63/63, Spells 7x36/36), so their
// order rests on the hand-read cells: gem 24 (Glimpse, an eye slit), gem 25
// (Rejuvenation, a green orb), gem 26 (Firefist, fire) and gem 51 (Firestrike,
// a fire bolt) all land on their spells' art only under the row-major,
// zero-based mapping (below); a column-major reading of the same four ids
// would land on four unrelated cells. The Buff family is the one part-filled
// sheet (23/36) and its fill itself proves the order: id 24 is an EMPTY cell
// row-major (row 4, col 0) but a POPULATED cell column-major (row 0, col 4 --
// the fifth across the top row).
//
// THE INDEXING CONVENTION is the salvage source's own. The pre-static-UI tree
// (`0f25cb57^`, item_icon_loader.cpp getSpellIcon, salvaged for this card)
// read the spell sheets exactly:
//
//     sheetNumber = iconId / 36 + 1;   // sheet 1 = spells01
//     localIndex  = iconId % 36;
//     row = localIndex / 6;  col = localIndex % 6;
//
// ZERO-BASED: id 0 is the first cell of the first sheet. The same arithmetic
// applies to the gem family (its cells are 36x28, seven wide, but ids still
// advance left-to-right, top-to-bottom). -1 and any id at or above the family's
// count carry no art.
//
// ── WHICH FAMILY EACH WINDOW USES ───────────────────────────────────────────
//     GEM BAR     gem_icon (field 144) indexes A_SpellGems (gemicons*.tga).
//                 EQUI_CastSpellWnd.xml's CSPW_SpellN pieces are <SpellGem>
//                 with <Holder>CW_SpellGemHolder</Holder>, and the file
//                 DEFINES it locally (line 5): classic_pieces01.tga at
//                 (41,123), 36x28 -- a socket frame 1:1 with the gem cell.
//                 That is the holder fable ships (below); the A_SpellGemHolder
//                 piece of window_pieces02.tga (39x32, oversized) is the
//                 other clients' holder (CursorAttachment, HotButtonWnd).
//                 Field 144's population (e.g. Glimpse=24, Summon Food=35,
//                 Firestrike=51) lands on populated gemicons cells; 128 spell
//                 rows in this export carry one of 11 distinct ids at or
//                 above 189 -- OUTSIDE the 3-sheet family (189 cells) -- and
//                 they resolve to nothing: drawing nothing is the answer, not
//                 an invented icon.
//     SPELLBOOK   spell_icon (field 143) indexes A_SpellIcons (Spells*.tga) --
//                 the family the salvage loader indexed, and the only one
//                 field 143 can name (0 on every Titanium row -> cell 0 of
//                 Spells01). Note: the oracle's EQUI_SpellBookWnd/EQUI_BuffWindow
//                 buttons declare <NormalDecal>BuffIcons</NormalDecal>; fable's
//                 plumbing hands the spellbook spell_icon, so it draws through
//                 the Spells family by its own id space. Each id space resolves
//                 against its own family only; a BuffIcons id is never mixed
//                 into a spell_icon and vice versa.
//     BUFF BAR    EQUI_BuffWindow.xml's BW_BuffN_Button pieces carry
//                 <NormalDecal>BuffIcons</NormalDecal> -- the BuffIcons
//                 animation (Spell_Icons.tga, one 6x6 sheet, 23 of 36 cells
//                 populated; the empties are ids 23..35, a trailing run).
//                 The buff's id column is field 143 (spell_icon),
//                 0 everywhere in this export, so every buff draws cell 0 of
//                 Spell_Icons (row 0 col 0, a sword icon in the sheet) until an
//                 export populates the column.
//
// ── THE PACKED ATLASES, AND WHY 40x40 CELLS FOR THE GEM ICONS ─────────────
// The gemicons cells are 36x28; everything else here is 40x40. One 1024x1024
// atlas per family at a 40 px pitch (25 x 25 = 625 cells) holds every family
// with room to spare (189 / 252 / 36 cells) and keeps EVERY cell boundary an
// ETC1 4x4 block boundary (40 and 1024 are both multiples of 4, T398's
// coin-pitch rule) and a power-of-two sheet -- which is what the NPOT warning
// the salvage audit names was about: the pre-static-UI loader cut each icon
// into its own 36x28/40x40 ITexture and the driver's getOptimalSize rounded
// non-power-of-two sizes. The gemicons art is copied into its 40x40 cell at a
// (0,0) origin -- the extra 4 px of pitch on the right and 12 px below are
// transparent, so the oracle's 36x28 gem blit (CSPW_SpellN's cell size,
// spell_gem_layout.h) samples exactly the art and nothing else, and the
// oracle's 40x40 buff/spellbook decal samples a native 40x40 cell 1:1.
//
// COST. Four more ordinary pack entries, ETC1 with mips plus an ETC1
// `_alpha` companion each (T411): spell_icons_gems, spell_icons_spells,
// spell_icons_buff (32-bit cut-outs on transparency, sampled through UI2D_FS
// on unit 1) and spell_icon_holder (the CW_SpellGemHolder piece, 36x28, lifted
// from classic_pieces01.tga (41,123) the way ui_atlas_builder lifts the coin
// strip from window_pieces01.tga).

#pragma once

#include <cstdint>

namespace EQT {
namespace Graphics {
namespace SpellIconAtlasLayout {

// ── The source sheets (quoted from EQUI_Animations.xml, see above) ─────────
constexpr int kGemSheetCount = 3;        // gemicons01..03.tga
constexpr int kGemCols = 7;              // 256 / 36, whole cells
constexpr int kGemRows = 9;              // 256 / 28, whole cells
constexpr int kGemCellsPerSheet = kGemCols * kGemRows;  // 63
constexpr int kGemCellW = 36;
constexpr int kGemCellH = 28;
constexpr int kGemIconCount = kGemSheetCount * kGemCellsPerSheet;  // 189

constexpr int kSpellSheetCount = 7;      // Spells01..07.tga
constexpr int kSpellCellsPerSheet = 36;  // 256 / 40 = 6, both axes
constexpr int kSpellIconCount = kSpellSheetCount * kSpellCellsPerSheet;  // 252

constexpr int kBuffSheetCount = 1;       // Spell_Icons.tga
constexpr int kBuffCellsPerSheet = 36;
constexpr int kBuffIconCount = kBuffCellsPerSheet;  // 36

constexpr int kSourceSheetEdge = 256;

// Zero-based, see the indexing-convention comment above: id 0 is the first
// cell; -1 (112 rows of gem_icon in this export) and ids at or above the
// family's count carry no art. indexForId() is the only bounds check, and
// drawSpellIcon()'s false-return is the caller's "no art" signal.

// Cells that actually carry art (re-measured 2026-10-16 over the mounted
// sheets; pinned by test_spell_icon_atlas.cpp against the source TGAs):
// gemicons 3x63, Spells 7x36 -- every cell. The Buff sheet is the ONE
// part-filled family: 23 of its 36 cells carry art, and the empties are ids
// 23..35 -- row 3 col 5 plus the whole of rows 4 and 5 -- a trailing run,
// not scattered. (Id 14, the diagonal cell row 2 col 2, carries art: alpha
// sum 77,520, re-measured 2026-10-17.) A part-filled sheet is exactly the
// census that distinguishes a cell ORDER: id 24 is an EMPTY cell row-major
// (row 4, col 0) but a POPULATED cell column-major (row 0, col 4 -- the
// fifth across the top row), so test_spell_icon_atlas.cpp pins the buff
// family's ordering from the fill, where the two full families cannot.
// (Id 14 does not work as the order-proving cell: on a square 6x6 grid it
// is on the diagonal, the SAME physical cell under both orders.) A fact
// about the ART, not a bound the lookup enforces: an empty cell is still
// addressable and resolves to nothing drawn, the ItemIconAtlas rule.
constexpr int kGemPopulated = 189;
constexpr int kSpellPopulated = 252;
constexpr int kBuffPopulated = 23;

// The id space's own population, from the export, for the record (re-measured
// 2026-10-17 over data/willeq.db): gem_icon is -1 on 112 rows, in-sheet on
// 7,826, and 128 rows carry one of 11 distinct ids (189, 190, 191, 193, 194,
// 195, 202, 204, 208, 211, 215) that outrun the gem family's 189 cells.

// ── The packed atlases ─────────────────────────────────────────────────────
constexpr int kCellSize = 40;
constexpr int kAtlasEdge = 1024;
constexpr int kAtlasColumns = kAtlasEdge / kCellSize;  // 25
constexpr int kAtlasRows = kAtlasEdge / kCellSize;     // 25
constexpr int kCellsPerAtlas = kAtlasColumns * kAtlasRows;  // 625

static_assert(kCellsPerAtlas >= kGemIconCount &&
                  kCellsPerAtlas >= kSpellIconCount &&
                  kCellsPerAtlas >= kBuffIconCount,
              "one atlas per family must hold every cell of the family");
static_assert(kCellSize % 4 == 0 && kAtlasEdge % 4 == 0,
              "a cell boundary must be an ETC1 4x4 block boundary");
static_assert(kCellSize >= kGemCellW && kCellSize >= kGemCellH,
              "the gem art must fit inside its 40x40 cell");

// The CW_SpellGemHolder piece: classic_pieces01.tga at (41,123), 36x28 --
// the holder EQUI_CastSpellWnd.xml names AND locally defines (all eight
// CSPW_SpellN carry <Holder>CW_SpellGemHolder</Holder>). 36x28 is 1:1 with
// the gem cell. The alternative, A_SpellGemHolder (window_pieces02.tga
// (208,216), 39x32, EQUI_Animations.xml), is 3 px wider / 4 px taller than
// the cell and is the piece the oracle's CursorAttachment and HotButtonWnd
// use; the gem bar ships the CW piece (docs/decisions.md, B1077 fix leg).
// One ordinary pack entry beside the three families.
constexpr int kHolderW = 36;
constexpr int kHolderH = 28;
constexpr int kHolderSrcX = 41;
constexpr int kHolderSrcY = 123;

// The three id spaces, so a caller cannot index one family with an id meant
// for another.
enum class SpellIconSheet : int {
    Gems = 0,    // A_SpellGems,  gemicons*.tga    (gem_icon,  field 144)
    Spells = 1,  // A_SpellIcons, Spells*.tga      (spell_icon, field 143)
    Buff = 2,    // BuffIcons,    Spell_Icons.tga  (BuffIcons decal)
    Count
};

// Pack entry / BMP basenames, in SpellIconSheet order. The BMP is
// `data/textures/<name>.bmp`, the pack entry `<name>`.
inline const char* atlasName(SpellIconSheet sheet) {
    switch (sheet) {
        case SpellIconSheet::Gems:   return "spell_icons_gems";
        case SpellIconSheet::Spells: return "spell_icons_spells";
        case SpellIconSheet::Buff:   return "spell_icons_buff";
        default: return "";
    }
}

inline const char* holderName() { return "spell_icon_holder"; }

// The cell count of a family.
inline int iconCount(SpellIconSheet sheet) {
    switch (sheet) {
        case SpellIconSheet::Gems:   return kGemIconCount;
        case SpellIconSheet::Spells: return kSpellIconCount;
        case SpellIconSheet::Buff:   return kBuffIconCount;
        default: return 0;
    }
}

inline int cellsPerSheet(SpellIconSheet sheet) {
    switch (sheet) {
        case SpellIconSheet::Gems:   return kGemCellsPerSheet;
        case SpellIconSheet::Spells: return kSpellCellsPerSheet;
        case SpellIconSheet::Buff:   return kBuffCellsPerSheet;
        default: return 0;
    }
}

// The native SOURCE cell size of a family (the gem cell is 36x28).
inline void cellSize(SpellIconSheet sheet, int& w, int& h) {
    if (sheet == SpellIconSheet::Gems) { w = kGemCellW; h = kGemCellH; }
    else { w = kCellSize; h = kCellSize; }
}

// Flat 0-based cell index for an id, or -1 when the family does not carry it
// (negative ids, or id >= the family's count).
inline int indexForId(SpellIconSheet sheet, uint32_t id) {
    if (static_cast<int>(id) >= iconCount(sheet)) return -1;
    return static_cast<int>(id);
}

// Where an id lives in the SOURCE art: which sheet (0-based) and the
// top-left pixel of its cell inside that 256x256 sheet. ROW-MAJOR,
// zero-based -- `<Vertical>false</Vertical>` plus the salvage code's
// arithmetic (above).
inline void sourceCell(SpellIconSheet sheet, uint32_t id,
                       int& sheetOut, int& x, int& y) {
    const int idx = indexForId(sheet, id);
    const int perSheet = cellsPerSheet(sheet);
    sheetOut = idx / perSheet;
    const int k = idx % perSheet;
    int cw = 0, ch = 0;
    cellSize(sheet, cw, ch);
    const int cols = (sheet == SpellIconSheet::Gems) ? kGemCols
                                                     : kSourceSheetEdge / 40;
    x = (k % cols) * cw;
    y = (k / cols) * ch;
}

// Top-left pixel of an id inside its packed atlas. The packing is
// row-major within the atlas -- there is no oracle convention to honour
// here, the source order is what matters and it is preserved -- so a
// contact sheet of the gem atlas reads ids 0.. left to right, top to bottom.
inline void atlasCellOrigin(SpellIconSheet sheet, uint32_t id, int& x, int& y) {
    const int idx = indexForId(sheet, id);
    const int cell = idx % kCellsPerAtlas;
    x = (cell % kAtlasColumns) * kCellSize;
    y = (cell / kAtlasColumns) * kCellSize;
}

// Source file basename for sheet `sheetIndex` (0-based) of a family. The
// client's own files are numbered 1-BASED (gemicons01..03.tga,
// Spells01..07.tga per EQUI_Animations.xml), so the printed digit is
// sheetIndex + 1.
//
// CASE: the oracle XML writes the spell sheets "Spells01..07.tga" (capital
// S) -- correct on the Windows machine the oracle ran on, which is
// case-INSENSITIVE. The mounted Linux client tree carries them lowercase
// (spells01..07.tga), and Linux IS case-sensitive, so the name this returns
// is the on-disk, lowercase one for the spell family. gemicons and
// Spell_Icons are lowercase/identical in both. A builder that reads the
// Linux mount must use this; a Windows-side tool would use the XML's case.
// `buffer` must hold at least 16 chars; returns it.
inline const char* sourceSheetName(SpellIconSheet sheet, int sheetIndex,
                                   char* buffer) {
    if (sheet == SpellIconSheet::Buff) {
        const char* name = "Spell_Icons.tga";
        for (int i = 0; name[i]; ++i) buffer[i] = name[i];
        buffer[15] = '\0';
        return buffer;
    }
    if (sheetIndex < 0) sheetIndex = 0;
    // NOTE the lowercase "spells0" prefix: the mounted Linux tree's on-disk
    // case (the oracle XML's "Spells0" is the Windows case-insensitive form).
    const char* prefix =
        (sheet == SpellIconSheet::Gems) ? "gemicons0" : "spells0";
    int i = 0;
    while (*prefix) buffer[i++] = *prefix++;
    buffer[i++] = static_cast<char>('0' + (sheetIndex + 1) % 10);
    const char* suffix = ".tga";
    while (*suffix) buffer[i++] = *suffix++;
    buffer[i] = '\0';
    return buffer;
}

}  // namespace SpellIconAtlasLayout
}  // namespace Graphics
}  // namespace EQT
