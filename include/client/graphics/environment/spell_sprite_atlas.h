// spell_sprite_atlas.h -- the layout of data/textures/spell_sprite_atlas.bmp.
//
// T417 (board B300, Will's ruling 2026-09-06: "draw the REAL sprites --
// effects as close to the original as possible"). The 16-tile procedural
// particle atlas cannot represent a sword, a flaming skull or a row of
// skulls, and `GENI00_SPRITE` -- the skull -- is the single commonest name in
// `spells.eff` (32 of its 301 populated slots). So the official client's own
// sprite art now ships as a SECOND particle sheet.
//
// ONE SOURCE OF TRUTH. This header is included by both
// `tools/spell_sprite_atlas_builder` (which writes the BMP) and
// `SpellEffectsConfig` (which maps a resolved sprite name to a cell), so the
// atlas layout and the client's addressing of it cannot drift apart. A JSON
// index alongside the BMP -- what the T417 plan asked for -- would have needed
// its own sha pin and its own drift test to say the same thing; a shared
// constexpr table says it at compile time and `SpellSpriteAtlasLayout` pins
// the builder's output against it.
//
// THE CELL INDEX SPACE. `UnifiedParticle::textureIndex` and
// `EmitterConfig::textureRegions[]` carry a single flat ordinal. Values below
// `kBase` address the 4x4 procedural atlas (`ParticleAtlas::*`, tiles 0..15);
// values from `kBase` up address this sheet, cell `index - kBase`. Nothing
// else changes shape, so an emitter can mix procedural tiles and real sprites
// in one `textureRegions[]` pick-list.
//
// FRAME EXTRACTION. Every `spel*.tga` is a 4x4 sheet, and T417 MEASURED that
// the sixteen frames are NOT identical -- T409's "one repeated frame" is
// refuted; every sheet animates (spelg1 is a sword rotating through sixteen
// headings). The atlas takes frame 0, the top-left frame as displayed, which
// is a faithful single-frame stand-in because every sheet animates ONE
// subject. Animating the sheet is boarded separately (B355).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace EQT {
namespace Graphics {
namespace Environment {
namespace SpellSpriteAtlas {

// Geometry. 64x64 cells on a 64 px pitch: a cell boundary is also a 4x4 ETC1
// block boundary, so no cell can bleed into its neighbour when the pack
// compresses the sheet (T398 Leg B's coin-pitch rule).
constexpr int kCellSize = 64;
constexpr int kColumns = 8;
constexpr int kRows = 8;
constexpr int kWidth = kCellSize * kColumns;   // 512
constexpr int kHeight = kCellSize * kRows;     // 512
constexpr int kCapacity = kColumns * kRows;    // 64

// First flat ordinal that addresses this sheet. Chosen above
// ParticleAtlas::TileCount with room to spare, so growing the procedural
// atlas later does not renumber the sprites.
constexpr uint8_t kBase = 64;

// The sprite files, in the order they occupy cells 0..kFileCount-1
// (row-major from the top-left). Sorted by basename, which is also the order
// the builder's directory scan produces -- keep it sorted.
//
// These are exactly the distinct files the 0-based rule
// (`SpellsEffReader::effectNameToTexture`) reaches from a name that appears in
// `spells.eff` AND that exists under `SpellEffects/`. Two resolving names
// name files that are simply not shipped -- `GENA20_SPRITE` -> `spela3.tga`
// and `GENA30_SPRITE` -> `spela4.tga` -- and they keep the procedural
// fallback.
constexpr const char* kFiles[] = {
    "spela1.tga", "spela2.tga", "spelb1.tga", "spelb2.tga", "spelb3.tga",
    "spelb4.tga", "spelc1.tga", "spelc2.tga", "spelc3.tga", "spelc4.tga",
    "speld2.tga", "speld4.tga", "spele1.tga", "spelg1.tga", "spelh1.tga",
    "speli1.tga", "spelj1.tga", "spelj2.tga", "spelk2.tga", "spell1.tga",
    "spelm1.tga", "speln1.tga", "spelo1.tga", "spelp1.tga", "spelr1.tga",
    "spelr2.tga", "spelu1.tga", "spelu2.tga", "spelv1.tga", "spely2.tga",
    "spely3.tga", "spely4.tga", "spelz1.tga",
};

constexpr int kFileCount = static_cast<int>(sizeof(kFiles) / sizeof(kFiles[0]));

static_assert(kFileCount <= kCapacity,
              "spell sprite atlas has more files than cells");

// Cell index for a resolved `spel*.tga` basename, or -1 if this sheet does
// not carry it. Linear over 33 entries, called once per name at config-build
// time -- not a hot path.
inline int slotForFile(const char* file) {
    if (!file || !*file) return -1;
    for (int i = 0; i < kFileCount; ++i) {
        if (std::strcmp(kFiles[i], file) == 0) return i;
    }
    return -1;
}

// Top-left pixel of a cell.
inline void cellOrigin(int slot, int& x, int& y) {
    x = (slot % kColumns) * kCellSize;
    y = (slot / kColumns) * kCellSize;
}

}  // namespace SpellSpriteAtlas
}  // namespace Environment
}  // namespace Graphics
}  // namespace EQT
