# T409 — the paperdoll at the oracle's proportion (B219); the 0-based sprite reader (B162)

Class C-ruled + B, no server. Read B219 (the oracle: `IW_CharacterView`
85x168 at (166,57) in 428x460), T393's §Review (`inventory_layout.h`,
`paperdollRect()`, `coinRect()`), and for Leg B: B162 (settled: the digit
is 0-based, `GEN<L><d>0_SPRITE` → `spel<l><d+1>.tga`), T389's §Review,
`spells_eff_reader.cpp` `effectNameToTexture()`, T389's name→tile
mapping in `spell_emitter_presets.cpp`.

## Leg A — B219

`paperdollRect()` scales the oracle's character-view rect into the popup
the same way `coinRect()` scales the coins (T393's integer mapping from
428x460). The doll render (T294 ordering) draws into the smaller rect;
the coins no longer overlap it. Test, pre-fix failing: the paperdoll
rect and the four coin rects are disjoint at all five resolutions; the
rect equals the scaled oracle rect. Static-panel seam. Strike B219; the
live screenshot pair is OWED to the next lock unit.

## Leg B — B162

`effectNameToTexture()` adopts the 0-based rule and drops the `'0'`
exclusion: 36 of 40 names resolve (272 of 301 slots). The mapping from
resolved texture name → atlas tile (T389's `applyStageSpriteNames`)
gains the newly resolved names by the same tile-vs-crop comparison T389
used (its sprites/ crops; the atlas now carries alpha — T400 — so shape
matters). Tests, pre-fix failing: `GENN00_SPRITE` → `speln1`,
`GENA10_SPRITE` → `spela2`, the resolved-slot census = 272. Strike B162.

## Proof

Build, `ctest -j6`, pre-fix/post-fix, visual gate standalone + ARM (no
golden should move — the gate opens no inventory and casts nothing),
file sizes.

STOP: none expected.
