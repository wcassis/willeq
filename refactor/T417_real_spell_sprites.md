# T417 — the real spell sprites draw (B300, Will 2026-09-06: "as close to the original as possible")

Class A, the slot that frees, no server for Legs A–B (the oracle frames
are in `review/T389/`, `review/T409/sprites/`). Read B300, B162, B165,
B225/B226, T389's and T409's §Reviews (the 0-based rule, the crops, the
name→tile map), T400/T411 (the atlas alpha path: 32-bit BMP, pack
companion, `POINT_SPRITE_FS` sampling unit 1), `ParticleAtlas`,
`SpellEffectsConfig::getTextureIndex()`, `EmitterConfig::textureRegions[]`,
the ETC1/pack builder (`eqe1_builder`, `scripts/build_willeq_pack.sh`),
and the sprite files `/mnt/projects/EverQuestP1999/SpellEffects/spel*.tga`
(each a 4x4 sheet of one repeated frame — T409).

## Leg A — a sprite atlas built from the official files

1. A build-time tool (`tools/spell_sprite_atlas_builder`, next to
   `ui_atlas_builder`) that reads every `spel*.tga` the 0-based reader can
   name (36 files), takes the single frame (T409 measured the 4x4 sheets
   are one repeated frame — verify per file, some may animate), and packs
   them into `data/textures/spell_sprite_atlas.bmp` (32-bit, alpha kept)
   + a JSON index of regions; `scripts/build_textures.sh` and
   `TexturesReproducible` learn the new file; the pack gets the entry via
   the companion route (T411) — `PackReproducible` and the sha pins move in
   the SAME commit (atomic install; copy the old pack under
   `review/T417/`; no ctest/gate running). Deterministic across build
   types (T403's rule — the input is files, so this is easy; pin it).
2. `ParticleAtlas` gains a second sheet (or the atlas grows past 16
   tiles — read what `textureRegions[]` and the point-sprite path can
   address; the STOP T380 set on "extending the atlas" is lifted by
   Will's ruling). `getTextureIndex()` maps a resolved sprite name to its
   region on the sprite sheet; the procedural tiles stay for names that
   do not resolve.
3. Tests, pre-fix failing: the 36 names resolve to sprite-sheet regions;
   a region's pixels equal the source frame's (alpha included); the two
   oracle-measured names (`speln1`, `spela2`) map to their own files, not
   a procedural tile.

## Leg B — the emitter draws the sprite the record names

Per stage (Source/Middle/Target), the emitter's texture region comes
from the record's sprite name (T389's `applyStageSpriteNames` extended
to every stage and every resolved name). Keep T389's tile fallbacks only
for the four non-GEN names. Headless render test (T411's harness): an
impact for Frost Bolt (54) samples the `speld2`/`spelu2` frames.

## Leg C — sizes and lifetimes (B226) — ONLY with the lock

The oracle's impact is "hundreds of sprites for 2–3 s" (T389); fable's
style is 36 particles for 0.1 s. Match by live A/B (count, size,
lifetime) for 54 and 93 against T389's oracle videos, one parameter at
a time, weather/heading pinned (T405's rig). If the lock is held when
you reach Leg C, STOP there: board Leg C's owed match (B355+) and finish.

## Proof

Build (all trees, background + exit file), `ctest -j6`, pre-fix/post-fix,
`TexturesReproducible` + `PackReproducible` green in every commit, visual
gate standalone + ARM (no golden should move — the gate casts nothing; a
moved image golden is a STOP), file sizes, `eqe1MappedBytes` delta
reported for a bless. Strike B300; annotate B165/B226.

STOP: a sprite file's frame is not extractable (animated sheet with
differing frames — report which and how many).
