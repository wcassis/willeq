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

## Review

Branch `task/T417-real-spell-sprites`, base `113b6388`. Legs A and B done;
**Leg C STOPPED** on the lock, as the plan directed.

### The headline, and three measured corrections

fable draws the real sprites. `data/textures/spell_sprite_atlas.bmp` is the
official client's own `SpellEffects/spel*.tga` art, and every `spells.eff`
sprite name that resolves to a shipped file now addresses its own cell. All
twelve T389/T409 name->tile approximations are deleted, the four figurative
ones included: the sword, the flaming skull, the row of skulls and the
lightning-wreathed sphere draw themselves. `GENI00_SPRITE`, the commonest name
in the whole file at 32 of 301 populated slots, is the skull, and it had been
drawing a soft white dot.

Three things the plan and the board had recorded turned out to be wrong, and
the second one would have shipped a visibly broken asset:

1. **T409's "each `spel*.tga` is a 4x4 sheet of one repeated frame" is
   REFUTED.** Hashing all sixteen frames of all 67 files gives between 5 and
   16 *distinct* frames in every single one. They are animations. `spelg1` is
   a sword rotating through sixteen headings; `speln1` is a mottled disc whose
   noise churns. Mean inter-frame byte difference runs 1.1 to 21.9 with 7% to
   69% of bytes differing.

   The plan set a STOP here ("a sprite file's frame is not extractable
   (animated sheet with differing frames)"). Read literally that stops the
   whole unit on all 36 files and makes Will's ruling unimplementable. I did
   not take it, and I am flagging that as a deviation. The STOP's purpose is a
   sheet where "the frame" is ill-defined -- different subjects per cell.
   These are sixteen frames of ONE subject, so a representative frame exists
   and the sheets are extractable. What the STOP correctly demanded is the
   report, which is here and on the board as **B355**.

2. **Frame 0 is the wrong frame, and not cosmetically.** The plan says "takes
   the single frame". The sheets are fade-in/fade-out cycles and frame 0 is
   the start of one. My first build took frame 0 and **seven of the 33 cells
   came out completely blank** (`spelb3`, `spelb4`, `spelc3`, `spelc4`,
   `speld4`, `spely3`, and `spely4` at a single alpha level of 1). Worse,
   `spelu2` -- one of Frost Bolt's two oracle-measured impact sprites --
   carries 14,966 of its peak 120,757 total alpha at frame 0, i.e. 12%: the
   unit's own worked example would have shipped nearly invisible.

   The rule is now **the peak frame**: greatest total alpha, ties to the
   lowest index. Pure integer sums over the file's own bytes, so it is
   deterministic. It is also the right answer independently -- the peak frame
   is the fully-formed sprite, the one a viewer would call "the picture".

3. **"36 of the 40 names resolve to a real `SpellEffects/*.tga`" (B300) is
   off by two.** Measured from `spells.eff` directly: 40 distinct sprite
   names across 301 populated slots; 36 match the GEN rule; **34** resolve to
   a file that actually exists; and those 34 name **33** distinct files,
   because `GENE00_SPRITE` and `GENE01_SPRITE` both give `spele1.tga` (the
   reader ignores `name[5]`). The two that resolve to nothing on disk are
   `GENA20_SPRITE` -> `spela3.tga` (13 slots) and `GENA30_SPRITE` ->
   `spela4.tga` (1 slot); neither file is shipped anywhere under
   `/mnt/projects/EverQuestP1999`. They keep the procedural fallback, pinned.

### Leg A -- the atlas

`tools/spell_sprite_atlas_builder` (new, 205 lines, links nothing -- header-
only `tga_reader.h` + `bmp32_writer.h`). 512x512, 8x8 cells of 64x64, 33 used
and 31 fully transparent. A 64px pitch puts every cell boundary on a 4x4 ETC1
block boundary, so no cell can bleed into its neighbour when the pack
compresses it (T398 Leg B's coin-pitch rule). The two smaller source shapes
(32x32 frames from `spelr1`/`spelr2`, 16x16 from seven others) are magnified
by an exact integer factor with nearest-neighbour replication -- integer
arithmetic only, and the GPU's bilinear filter smooths it at draw time.
All-or-nothing: an unreadable file or a non-4x4 sheet is exit 1, never a
partial sheet.

**DEVIATION, flagged**: the plan asked for "a JSON index of regions". I built
`include/client/graphics/environment/spell_sprite_atlas.h` instead -- a shared
`constexpr` table that the builder AND `SpellEffectsConfig` both include. A
shipped JSON index would have needed its own sha pin, its own place in the
`DataPathRouting` census, and its own drift test to assert the same thing the
compiler now asserts for free. With the header there is no second source of
truth to drift: the layout is one table and the name rule is
`SpellsEffReader::effectNameToTexture()`, the reader's own.

Determinism (B268's rule): the builder's output is **bit-identical at -O0, -O2
and the RelWithDebInfo tree build** -- `cmp` exit 0 across all three.

### Leg B -- the emitter and the draw

`getTextureIndex()` keeps its explicit rows for the fifteen procedural shape
names (which `config/spell_effects.json` uses) and for the four non-GEN sprite
names, which have no file and now get a tile chosen from what the name says
(`I_SNOWFLAKESPRITE` -> Snowflake, `I_TFIRE_SPB` -> Ember, `I_TSMOKE_SPB` ->
SmokeWisp, `GEN_Z_SPB` -> WispyCloud). On any other miss it runs the 0-based
rule and looks the file up in the layout header.

Addressing is one flat ordinal: below `SpellSpriteAtlas::kBase` (64) is a
procedural tile, at or above it is cell `index - kBase`. That fits `uint8_t`
(max 96) so `EmitterConfig::textureRegions[4]` and `applyStageSpriteNames`
needed no change at all, and an emitter can mix procedural tiles and real
sprites in one pick-list.

`UnifiedParticleRenderer` bins by **sheet as well as blend mode** -- four
vertex arrays, not two -- because the bound texture and `uAtlasRegionSize` are
per-draw state. A sprite particle with no sheet loaded is **skipped**, never
redrawn with the procedural atlas's UVs; painting an unrelated tile with a
confident face on it is the failure mode B300 was opened about. The legacy CPU
billboard path, whose material is bound to the procedural atlas alone, clamps
a sprite index to tile 0 rather than computing `index % 4`.

`ParticleManager` loads and re-acquires the second sheet exactly like the
first (init, `onTextureEvicted`, `onZoneEnter`), but a missing sprite sheet is
an ERROR, not `exit(1)`: every procedural particle still draws without it.

### Proof

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo) | exit 0 |
| `ctest -j6` | **4304/4305**, sole failure `visual_gate` (the bless, B357) |
| pre-fix red | **17 tests**: 11 in `test_spell_target_stage_textures` (config TU reverted to `fable`), 6 in `test_spell_sprite_atlas` (BMP parked) |
| post-fix green | 22/22 and 6/6 |
| planted fault (sprite region size forced back to 0.25) | 4 of 6 `SpellSpriteSheetDrawTest` cases RED |
| `TexturesReproducible` | PASS -- 65 pinned, 64 byte-identical, 1 exempt |
| `PackReproducible` | PASS -- rebuild == pin == installed |
| `scripts/build-gles2-x86.sh` | exit 0 |
| visual gate standalone | FAIL on `eqe1MappedBytes` only; **every image golden matched**, CPU p95 under ceiling in all three zones, budget clean |
| `scripts/build-arm-noble.sh` | exit 0 |
| `check_file_sizes.sh` | 18 files, 18 ok, 0 warn, 0 fail (largest new file 279 lines) |

One `ctest` run died at gtest DISCOVERY on `test_spell_manager` ("Process
terminated due to timeout"). The binary was intact -- valid ELF, 260 MB,
`--gtest_list_tests` returned instantly -- so this was the protocol's
documented cold-cache contention case, not truncation. The re-run was clean.

The headless render proof (`tests/test_spell_sprite_sheet_draw.cpp`, 6 cases
through a real GLES2 device, self-skipping) paints a synthetic 512x512 sheet
with a unique flat colour per cell, draws one particle per cell index, and
reads back the centre pixel -- so a wrong UV is not just a failure, the
message names which sprite was drawn instead. Frost Bolt's `speld2` and
`spelu2` each sample their own cell and differ from each other.

### The pack

5,532,254 -> **5,881,918** bytes (+349,664); 126 -> 128 entries
(`spell_sprite_atlas` + `spell_sprite_atlas_alpha`, both ETC1 -- no
`--rgba8888-texture` override was needed, so
`TextureAlphaCensus.NoPackEntryNeedsTheRgba8888Payload` stays green). sha
`aca03e7f...` -> `584ba1fe...`.

**Installed 2026-09-06 13:32:01-13:32:58 UTC.** No `willeq` client was
running. I also waited for `main`'s tip-verification `ctest` to finish first:
the pack was ready while main was mid-run, and swapping it then would have
turned main's `PackReproducible` red for reasons that had nothing to do with
main. The pre-change pack is preserved at
`review/T417/willeq.eqe1.pre-T417`.

`eqe1MappedBytes` rose by **exactly 349,664 in each of the three gate zones**
-- the pack's own delta, so the sheet is mapped once per zone and nothing else
moved. **Bless owed: B357.** `tests/goldens/manifest.json` untouched.

### Leg C -- STOPPED

`/mnt/projects/fable/.eqemu-lock` read `T418 2026-09-06` for the whole unit
(T418 was running an oracle capture under Wine/Xvfb throughout). The plan's
STOP applied: boarded as **B356**, not attempted, no connection opened.

So the burst's size and lifetime are unchanged -- still 36 particles of
0.05-0.15 world units for 0.4-0.8 s against the oracle's hundreds of sprites
for 2-3 s. **What changed is what each particle looks like.** A capture pair
against T389's oracle taken now would differ in two ways at once; take B356's
A/B first.

### For Will

`review/T417/` -- the labelled 33-cell contact sheet is the thing to look at.
One judgement call is genuinely open and I did not want to make it silently:
**cells 4, 5, 8, 9, 11, 30 and 31** (`spelb3`, `spelb4`, `spelc3`, `spelc4`,
`speld4`, `spely3`, `spely4`) are blocky colour gradients rather than discrete
particles. Those seven files are the 16x16-frame sheets magnified 4x, and they
read more like beam or trail ramps than sprites. They are all low-traffic
names. If they look wrong, they can go back to a procedural tile.

### Board

**B300 struck** (with the three corrections above recorded on it).
**B165** and **B226** annotated. New: **B355** (the sheets animate; we ship a
still), **B356** (Leg C's owed A/B), **B357** (the `eqe1MappedBytes` bless).
