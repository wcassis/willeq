# T420 — the spell sprites animate as the official client animates them (B355, Will: "as close to the original as possible")

Class A, slot1, no server for the implementation (T389's oracle videos
are the reference; a live pair is owed to the next lock unit). Read
B355, T417's §Review (every `spel*.tga` is a 4x4 sheet of 5–16 DISTINCT
frames — fade-in cycles; T417 ships the peak-alpha frame as a still;
`spell_sprite_atlas.h` layout; the builder), `review/T417/sheets/`, and
T389's per-spell frame counts (e.g. Frost Bolt 33 frames at 10 fps, Burst
of Flame 23).

1. Measure the oracle's frame rate per sprite from T389's videos: how
   many video frames a sheet frame persists (the sheet has 16 cells; the
   effect lasts N frames → cells per second). Table per measured spell.
2. Pack ALL 16 frames per sprite into the atlas (512x512 holds 64 cells
   of 64x64: 33 sprites × 16 frames needs 528 cells → grow to 1024x1024 or
   pack per-sprite strips; say the layout and the pack delta) — the
   builder emits the layout header; `TexturesReproducible`/`PackReproducible`
   cascade in ONE commit (atomic pack install; no ctest/gate/willeq
   running; copy the old pack under `review/T420/`).
3. The emitter advances the cell per particle over its lifetime at the
   measured rate (`EmitterConfig::textureRegions[]` already exists as an
   array — read whether the particle shader can index frames per
   particle age; if not, the smallest change that does). Headless test
   (T411's harness): a particle at age 0 samples cell 0, at half life the
   middle cell, at end the last; pre-fix failing.
4. Strike B355; report the `eqe1MappedBytes` delta for a bless.

## Proof

Build (all trees, background), `ctest -j6`, pre-fix/post-fix, visual
gate standalone + ARM (no image golden should move), file sizes.

STOP: the particle path cannot address per-particle frames without a
shader restructure (report the shape and the cost).
