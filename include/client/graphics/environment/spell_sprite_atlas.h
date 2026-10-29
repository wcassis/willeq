// spell_sprite_atlas.h -- the layout of data/textures/spell_sprite_atlas.bmp.
//
// T417 (board B300, Will's ruling 2026-09-06: "draw the REAL sprites --
// effects as close to the original as possible"). The 16-tile procedural
// particle atlas cannot represent a sword, a flaming skull or a row of
// skulls, and `GENI00_SPRITE` -- the skull -- is the single commonest name in
// `spells.eff` (32 of its 301 populated slots). So the official client's own
// sprite art now ships as a SECOND particle sheet.
//
// T420 (board B355) made that sheet ANIMATE: a 256x256 or 128x128
// `spel*.tga` is a 4x4 sheet of 8 to 16 distinct frames -- `spelg1` is a
// rotating sword, `speln1` a mottled disc whose noise churns -- and a particle
// walks them over its own lifetime.
//
// B1124 (#444, B1118 M8): NOT EVERY FILE IS A GRID. The 64x64 files are ONE
// picture each -- `spelc3` a yellow-white star, `spelc4` an orange vertical
// flare -- and T420 cut them into sixteen 16x16 "frames" that were really
// fragments (its "six sheets with a BLANK frame 0" were exactly six of these:
// frame 0 was the empty top-left corner of one centred picture). The census
// (`review/B1124/sheet_census.tsv`, `refactor/B1124/README.md`) splits the 67
// files cleanly: every 64x64 one is a single image, every 128/256 one a grid.
// (B1127 grew the sheet to 60 files, 18 of them single images and two 2x2
// grids; `kSheetFrames` below says
// which, and the builder re-measures it and refuses to pack a file whose
// declaration its own census contradicts.
//
// ONE SOURCE OF TRUTH. This header is included by both
// `tools/spell_sprite_atlas_builder` (which writes the BMP) and
// `SpellEffectsConfig` (which maps a resolved sprite name to a sprite
// ordinal), so the atlas layout and the client's addressing of it cannot
// drift apart. A JSON index alongside the BMP -- what the T417 plan asked for
// -- would have needed its own sha pin and its own drift test to say the same
// thing; a shared constexpr table says it at compile time and
// `SpellSpriteAtlasLayout` pins the builder's output against it.
//
// THE INDEX SPACE, AND WHY IT IS TWO LEVELS.
// `UnifiedParticle::textureIndex` and `EmitterConfig::textureRegions[]` carry
// a single flat ordinal, and `textureRegions[]` is a `uint8_t[4]` -- 960 cells
// do not fit in it. So the ordinal names a SPRITE, never a cell:
//
//   textureIndex <  kBase   ->  procedural 4x4 particle atlas, tile as-is
//   textureIndex >= kBase   ->  this sheet, sprite (textureIndex - kBase)
//
// The CELL is derived at draw time from the sprite and the particle's age
// (`cellFor()` + `frameForNormalizedAge()`), which is what makes the sprite
// animate without widening a single struct field. 60 sprites keeps the
// ordinal at 123 or below, comfortably inside a `uint8_t`.
//
// FRAME EXTRACTION. All sixteen frames of every GRID sheet are packed, in the
// sheet's own display order (frame f at grid column f%4, row f/4 after
// top-down conversion). B1188 (#507) moved the cell layout to a GRID OF 4x4
// CELL BLOCKS (one 16-cell block, 128x128 px, per sprite, tiling the sheet
// as an 8x8 grid of blocks) so that a SINGLE-IMAGE sheet can be stored NATIVE
// at its own resolution inside its own block -- a 64x64 picture in the
// top-left 2x2 sub-block, the 256x256 one 2:1 (128x128) in the whole block --
// instead of box-reduced into a single 32x32 cell. The 2:1 box into one cell
// kept a QUARTER of the picture's additive mass (cell alpha = the mean of
// four texels, drawn at the same size the client draws its native texture),
// which is the b1130 landing pixel gap, measured 0.53x. Every sprite keeps
// the same sixteen-cell reservation, so no ordinal moves. T409's "one repeated frame" is refuted; T420 re-took
// the census by sha1 over all sixteen frames of all 33 files and found 8 to 16
// DISTINCT frames each (true of the 26 grids; the seven single images'
// "frames" differ only because they are different parts of one picture),
// with exactly ONE file (`spelh1`) whose frames 8..15 repeat 0..7. The duplicates that do occur are the animation holding, so
// they are packed as they are rather than deduplicated: dropping them would
// silently retime the animation, and a variable per-sprite frame count would
// buy 16 cells of the 1024 at the cost of a second lookup table.
//
// GEOMETRY, AND WHY THE CELL SHRANK. 33 sprites x 16 frames was 528 cells
// (B1127: 60 x 16 = 960 of the 1024).
// At T417's 64 px cell that needs a 2048x2048 sheet: 2.8 MB of ETC1 with mips
// per pack entry, 5.6 MB with the alpha companion, on every zone's mapped
// budget and on a 512 MB Orange Pi. A 32 px cell fits all of them in 1024x1024 at
// a quarter of that. The sprites are drawn as point sprites whose size is
// clamped to 100 screen pixels and which measure single digits to ~30 px in
// practice, so 32 px is still oversampled at the size anything actually
// draws. 32 is a multiple of 4, so a cell boundary is still an ETC1 block
// boundary and no cell can bleed into its neighbour when the pack compresses
// the sheet (T398 Leg B's coin-pitch rule).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EQT {
namespace Graphics {
namespace Environment {
namespace SpellSpriteAtlas {

// Geometry. 32x32 cells on a 32 px pitch, 32 x 32 of them.
constexpr int kCellSize = 32;
constexpr int kColumns = 32;
constexpr int kRows = 32;
constexpr int kWidth = kCellSize * kColumns;   // 1024
constexpr int kHeight = kCellSize * kRows;     // 1024
constexpr int kCapacity = kColumns * kRows;    // 1024

// A GRID source sheet is 4x4, so a grid sprite is sixteen frames, and every
// sprite -- grid or single image -- reserves that many cells.
constexpr int kSheetGrid = 4;
constexpr int kFramesPerSprite = kSheetGrid * kSheetGrid;  // 16

// First flat ordinal that addresses this sheet. Chosen above
// ParticleAtlas::TileCount with room to spare, so growing the procedural
// atlas later does not renumber the sprites.
constexpr uint8_t kBase = 64;

// The sprite files, in the order they occupy sprite ordinals 0..kFileCount-1,
// lower-case (the client is Windows: `spellsnew.edd` writes `Glowflare02.tga`
// as `glowflare02.tga`), sorted -- keep it sorted.
//
// B1127 (#447): THE ORACLE DRAWS spellsnew.eff, NOT spells.eff. A spell's
// art is the textures spellsnew.edd names for the emitters of its
// spellsnew.eff record (spells_us.txt field 145), proven on the oracle by
// same-record / different-field-145 A/B casts (`review/B1127/README.md`).
// The sheet therefore carries (a) every non-DDS texture those records name
// for a player spell, and (b) the spells.eff 0-based files of the records
// that only a spell WITHOUT a field-145 record still reaches. The DDS
// textures (NPC and discipline records) are not packed yet and fall back
// to the procedural circle (boarded, `refactor/B1127/README.md`).
constexpr const char* kFiles[] = {
    "flare_blsp50a.png", "flare_whsp50a.png", "gena20.bmp", "gena30.bmp",
    "genab1.png", "genb20.bmp", "glowflare02.tga", "gob.png",
    "spela1.tga", "spela2.tga", "spelaa.png", "spelab.tga",
    "spelan.png", "spelb2.tga", "spelba.png", "spelbb.tga",
    "spelc2.tga", "spelc3.tga", "spelc4.tga", "spelcb.png",
    "spelcc.png", "spelce.png", "spelcf.png", "spelcm.png",
    "speld2.tga", "speld4.tga", "speldc.png", "speldd.tga",
    "spele1.tga", "spelg1.tga", "speli1.tga", "spelj1.tga",
    "spelj2.tga", "spelja.png", "speljb.png", "spelk2.tga",
    "spelkb.png", "spell1.tga", "spella.png", "spelma.png",
    "speln1.tga", "spelna.png", "speloa.png", "spelpa.png",
    "spelqa.png", "spelr1.tga", "spelr2.tga", "spelra.png",
    "spelu2.tga", "spelua.png", "spelub.png", "spelw1.tga",
    "spelx1.tga", "spely2.tga", "spely3.tga", "spely4.tga",
    "spelyb.png", "spelyc.png", "spelyd.png", "spelye.png",
};

constexpr int kFileCount = static_cast<int>(sizeof(kFiles) / sizeof(kFiles[0]));

// B1124: how many frames each file in kFiles carries -- 16 for a 4x4 grid,
// 1 for a single image (packed whole, held for the particle's whole life).
// Parallel to kFiles. Measured, not assumed: `review/B1124/scripts/
// sheet_census.py` and the builder's own `seamRatio` census (additive mass on
// the 4x4 seams over their area share: grids <= 0.30 here, single images
// >= 1.33) agree with the eye on all 33 (B1124; B1127's additions are checked the
// same way, a 2x2 sheet on its 2x2 seams). A wrong entry is an exit 1 from the
// builder, not a silently mis-cut sheet.
// B1127: a 128x128 spellsnew.edd sheet with a frame count of 4 is a 2x2 GRID
// of 64 px frames (flare_blsp50a, flare_whsp50a); spellsnew.edd's own frame
// field (block +0x130: 16 / 4 / 1) agrees with the file size on every
// texture it names (`review/B1127/scripts/edd_census.py`).
constexpr int kSingleImage = 1;
constexpr int kTwoByTwo = 4;
constexpr uint8_t kSheetFrames[] = {
    kTwoByTwo, kTwoByTwo, kSingleImage, kSingleImage,       // flare_blsp50a flare_whsp50a gena20 gena30
    kSingleImage, kSingleImage, kSingleImage, 16,           // genab1 genb20 glowflare02 gob
    16, 16, 16, 16,                                         // spela1 spela2 spelaa spelab
    16, 16, 16, 16,                                         // spelan spelb2 spelba spelbb
    16, kSingleImage, kSingleImage, 16,                     // spelc2 spelc3 spelc4 spelcb
    kSingleImage, kSingleImage, kSingleImage, 16,           // spelcc spelce spelcf spelcm
    16, kSingleImage, kSingleImage, kSingleImage,           // speld2 speld4 speldc speldd
    16, 16, 16, 16,                                         // spele1 spelg1 speli1 spelj1
    16, 16, 16, 16,                                         // spelj2 spelja speljb spelk2
    16, 16, 16, 16,                                         // spelkb spell1 spella spelma
    16, 16, 16, 16,                                         // speln1 spelna speloa spelpa
    16, 16, 16, 16,                                         // spelqa spelr1 spelr2 spelra
    16, 16, 16, 16,                                         // spelu2 spelua spelub spelw1
    16, 16, kSingleImage, kSingleImage,                     // spelx1 spely2 spely3 spely4
    16, kSingleImage, kSingleImage, kSingleImage,           // spelyb spelyc spelyd spelye
};
static_assert(sizeof(kSheetFrames) / sizeof(kSheetFrames[0]) == kFileCount,
              "kSheetFrames must name a frame count for every file in kFiles");

// B1188 (#507): the STORED frame edge in pixels for each file -- how wide one
// frame of the sprite is when it sits in the sheet:
//   32  a grid frame (a 64 px frame box-reduces 2:1, a 32 px frame copies;
//       grids stay 32 px because sixteen native 64 px frames would need 64
//       cells a sprite and the 1024-cell sheet holds 16 for 60 sprites);
//   64  a single 64x64 image, stored 1:1 in a 2x2 sub-block of cells, and the
//       2x2 sheets' 64 px frames, stored 1:1 in 2x2 sub-blocks;
//   128 the 256x256 single image (glowflare02), 2:1 in the whole 128x128
//       block (the client draws it at 256; 1:1 needs 64 cells, four blocks).
// Measured, not assumed: /home/user/kb-evidence/kb-b1188-landing-pixels/
// scripts/file_sizes2.py and tga_sizes.py over the shipped client tree
// (every single image is 64x64 except glowflare02.tga at 256x256; both 2x2
// sheets are 128x128 of 64 px frames; spely2.tga and spelyb.png are 256x256
// 16-FRAME GRIDS, stored 32 like every grid). The builder re-checks every
// file's real size against this table and refuses (exit 1) a mismatch, as it
// already does for kSheetFrames. Parallel to kFiles.
constexpr int kStoredFramePx[] = {
    64, 64, 64,  64,  // flare_blsp50a flare_whsp50a gena20 gena30
    64, 64, 128, 32,  // genab1 genb20 glowflare02 gob
    32, 32, 32,  32,  // spela1 spela2 spelaa spelab
    32, 32, 32,  32,  // spelan spelb2 spelba spelbb
    32, 64, 64,  32,  // spelc2 spelc3 spelc4 spelcb
    64, 64, 64,  32,  // spelcc spelce spelcf spelcm
    32, 64, 64,  64,  // speld2 speld4 speldc speldd
    32, 32, 32,  32,  // spele1 spelg1 speli1 spelj1
    32, 32, 32,  32,  // spelj2 spelja speljb spelk2
    32, 32, 32,  32,  // spelkb spell1 spella spelma
    32, 32, 32,  32,  // speln1 spelna speloa spelpa
    32, 32, 32,  32,  // spelqa spelr1 spelr2 spelra
    32, 32, 32,  32,  // spelu2 spelua spelub spelw1
    32, 32, 64,  64,  // spelx1 spely2 spely3 spely4
    32, 64, 64,  64,  // spelyb spelyc spelyd spelye
};
static_assert(sizeof(kStoredFramePx) / sizeof(kStoredFramePx[0]) == kFileCount,
              "kStoredFramePx must name a stored frame size for every file");

// Frames sprite `sprite` carries: 16 for a 4x4 grid, 4 for a 2x2 grid, 1 for
// a single image. Out of range is 1, which draws the slot's first cell and nothing else.
inline int framesFor(int sprite) {
    if (sprite < 0 || sprite >= kFileCount) return 1;
    return kSheetFrames[sprite];
}

// B1188: THE CELL LAYOUT IS AN 8x8 GRID OF 4x4 CELL BLOCKS. Each sprite owns
// one 16-cell block (128x128 px); the 60 sprites tile 60 of the 64 blocks
// (block column sprite%8, row sprite/8), the last four blocks unused. A
// frame's cells are its stored-frame sub-block inside the block (kStored-
// FramePx / kCellSize cells on a side), so a sprite's animation reads left to
// right, top to bottom, exactly like its source sheet does.
constexpr int kBlockCols = 8;
constexpr int kBlockCells = kSheetGrid * kSheetGrid;  // 16 cells = 128x128 px
static_assert(kColumns % kSheetGrid == 0, "a sprite block must sit whole in a row");
static_assert(kBlockCols * kSheetGrid == kColumns, "the blocks must tile the sheet exactly");
static_assert(kRows % kSheetGrid == 0 &&
                  (kRows / kSheetGrid) * kBlockCols >= kFileCount,
              "the blocks must tile the sheet exactly (rows)");

// Cells RESERVED: one 4x4 block per sprite (60 x 16 = 960 of the 1024; the
// reserved cells of a native single image past its sub-block stay
// transparent).
constexpr int kUsedCells = kFileCount * kBlockCells;  // 960

static_assert(kUsedCells <= kCapacity,
              "spell sprite atlas has more frames than cells");
static_assert(kBase + kFileCount <= 256,
              "a sprite ordinal must fit in the uint8_t textureRegions[]");

// Sprite ordinal for a sprite file basename (any case), or -1 if this sheet
// does not carry it. Linear over 60 entries, called once per name at
// config-build time -- not a hot path.
inline int slotForFile(const char* file) {
    if (!file || !*file) return -1;
    for (int i = 0; i < kFileCount; ++i) {
        const char* a = kFiles[i];
        const char* b = file;
        while (*a && *b) {
            const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b - 'A' + 'a') : *b;
            if (*a != cb) break;
            ++a;
            ++b;
        }
        if (!*a && !*b) return i;  // case-insensitive: the client is Windows
    }
    return -1;
}

// The top-left cell of block SLOT (0..63) in the 8x8 block grid, with NO
// sprite-range clamping: `blockBase()` folds an out-of-range sprite ordinal to
// 0 for the runtime lookup, but the sheet's geometry (the unused-blocks test,
// the non-overlap test) needs the raw tiling of all 64 block slots, including
// the four no sprite occupies.
inline int blockSlotCell(int slot) {
    const int col = slot < 0 ? 0 : slot % kBlockCols;
    const int row = slot < 0 ? 0 : slot / kBlockCols;
    return col * kSheetGrid + kColumns * kSheetGrid * row;
}

// The top-left CELL of sprite `sprite`'s 4x4 block (B1188). The 60 sprites
// tile an 8x8 grid of blocks: block column sprite%kBlockCols, row
// sprite/kBlockCols, four cells on a side. The pitch between blocks in BOTH
// directions is the block's own size in cells: horizontally kSheetGrid cells
// (4 cells = 128 px), vertically kSheetGrid CELL-ROWS (kColumns * kSheetGrid
// cell INDICES -- a block is kSheetGrid cell-rows tall). B1188's first cut
// used a vertical pitch of one cell-row (kColumns indices), so every block
// row slid up over the one above and its box-reduced frames overwrote the
// lower halves of the sprites' native 64 px frames -- a silent atlas
// corruption no per-sprite check caught, because each sprite's own blits
// were internally consistent. The non-overlap test in
// test_spell_sprite_atlas.cpp pins this.
inline int blockBase(int sprite) {
    if (sprite < 0 || sprite >= kFileCount) return 0;
    return blockSlotCell(sprite);
}

// The stored frame edge in pixels (kStoredFramePx; kCellSize out of range).
inline int storedFramePx(int sprite) {
    if (sprite < 0 || sprite >= kFileCount) return kCellSize;
    return kStoredFramePx[sprite];
}

// How many CELLS one side of the stored frame spans (1, 2 or 4): the draw's
// UV region for the sprite, in cells.
inline int storedSpan(int sprite) { return storedFramePx(sprite) / kCellSize; }

// The cell holding frame `frame` of sprite `sprite`: the frame's stored-frame
// sub-block inside the sprite's block. A 4x4 grid of 32 px frames fills the
// block row-major (frame f at column f%4, row f/4 -- the source sheet's own
// display order, as before B1188); a 2x2 sheet of 64 px frames fills it with
// four 2x2 sub-blocks; a single image holds its sub-block (top-left of the
// block) for its whole life.
inline int cellFor(int sprite, int frame) {
    const int span = storedSpan(sprite);
    const int frames = framesFor(sprite);
    const int cols = frames == kFramesPerSprite ? kSheetGrid
                   : frames == kTwoByTwo ? 2 : 1;
    const int f = frame < 0 ? 0 : frame >= frames ? frames - 1 : frame;
    return blockBase(sprite) + (f % cols) * span + kColumns * (f / cols) * span;
}

// Top-left pixel of a CELL (not of a sprite).
inline void cellOrigin(int cell, int& x, int& y) {
    x = (cell % kColumns) * kCellSize;
    y = (cell / kColumns) * kCellSize;
}

// WHICH FRAME A PARTICLE SHOWS AT A GIVEN POINT IN ITS LIFE.
//
// The sheet plays through exactly ONCE over the particle's lifetime: frame 0
// at birth, the last frame as it dies. Three things chose that rule, and the
// alternatives were measured rather than argued.
//
//  1. T420 MEASURED the official client's own playback (T389's oracle videos,
//     the analysis in `review/T420/oracle_frame_rate/`) and REFUTED an
//     effect-wide clock: one render of Whirling Wind holds six sprites on one
//     animation phase and eight on another SIMULTANEOUSLY, which a single
//     clock over the whole effect cannot produce. Per-particle playback is
//     what survived. (The rate itself could not be pinned: the oracle capture
//     turns out to hold only ~5 new renders per second during that effect and
//     ~1.2 during Weaken, so Nyquist stops at 2.5 cells/s. The bound that
//     holds is BELOW ~3 cells/s, consistent with one 16-cell playthrough over
//     the ~6 s effect.)
//  2. A fixed cells-per-second clock would strand a short-lived particle in
//     the opening frames and never reach the peak -- a visible regression
//     against T417, whose single shipped cell was the PEAK frame. Tying the
//     cycle to the life guarantees every particle still passes through its
//     peak, at any lifetime, so the animation can be added without waiting on
//     the lifetime tuning that boards B226/B356 own.
//  3. It needs no new state: `UnifiedParticle` already carries `age` and
//     `maxLifetime`.
//
// Do NOT justify this by calling the sheets fade-in cycles -- T420's census of
// all 33 files (`review/T420/README.md`) says otherwise, and the record
// should not repeat T409's mistake. Five PEAK at frame 0 and decay from
// there; the rest ramp to a peak somewhere in the middle. (T420's "six files
// with a COMPLETELY BLANK frame 0 and a seventh all but blank" were the seven
// single images, B1124 -- the blank "frame" was a picture's empty corner.)
// What the grids have in common is only that they are sixteen-frame
// animations of one subject, which is all this rule needs.
//
// `normalizedAge` is `UnifiedParticle::getNormalizedAge()`, i.e. age /
// maxLifetime. Out-of-range input is clamped rather than folded, so a
// particle that overruns its lifetime holds the last frame instead of
// wrapping back to a possibly-empty one.
//
// `frames` is the sheet's frame count (`framesFor()`); a single image (1)
// shows frame 0 for its whole life -- no animation, no fragment stepping.
inline int frameForNormalizedAge(float normalizedAge, int frames = kFramesPerSprite) {
    if (frames <= 1) return 0;
    if (!(normalizedAge > 0.0f)) return 0;  // also catches NaN
    const int frame = static_cast<int>(normalizedAge * frames);
    return frame >= frames ? frames - 1 : frame;
}

// The cell a particle of sprite `sprite` shows at `normalizedAge`: what the
// renderer samples. B1124: honours the sheet's own frame count.
inline int cellForAge(int sprite, float normalizedAge) {
    return cellFor(sprite, frameForNormalizedAge(normalizedAge, framesFor(sprite)));
}

}  // namespace SpellSpriteAtlas
}  // namespace Environment
}  // namespace Graphics
}  // namespace EQT
