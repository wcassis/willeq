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

## Review

Branch `task/T420-sprite-animation`, base `ee1743a1`. Media:
`/mnt/projects/fable/review/T420/` (README, `sheets/`, `oracle_frame_rate/`,
`willeq.eqe1.pre-T420`).

### 0. The plan's STOP did not fire, and it could not have

"The particle path cannot address per-particle frames without a shader
restructure" is false, and the answer was already in the tree. The atlas
offset is **not** computed in the shader: `UnifiedParticleRenderer::render()`
computes `atlasU`/`atlasV` on the CPU in its per-particle binning loop and
ships them as the `aTexCoord1` vertex attribute. Deriving the cell from the
particle's age is therefore three lines in a loop that already ran once per
particle per frame. `POINT_SPRITE_VS` and `POINT_SPRITE_FS` are BYTE-IDENTICAL
to the tip's; so is `SpellEffectsConfig::getTextureIndex()`.

### 1. The oracle's frame rate — measured, with the bound it actually supports

Item 1 asked for a per-spell table of cells per second read off T389's videos.
The honest answer is that **the capture cannot support one**, and the reason
is worth recording so the next unit does not spend a day on it:

| | measured |
|---|---|
| oracle video container rate | 10 fps (all seven `.mkv`) |
| **new RENDERS in that video, Whirling Wind window** | **~5 per second** (a new frame every 2nd–3rd video frame, pixel-exact comparison) |
| **new RENDERS, Weaken window** | **~1.2 per second** (renders at video frames 1911, 1920, 1929, 1937, 1945, 1952 only) |
| Nyquist limit on a readable sheet rate | 2.5 cells/s (Whirling Wind), 0.6 cells/s (Weaken) |

So T389's "Weaken: 20 video frames = 2.0 s" is **five renders**, not twenty.
The official client under Xvfb/software GL was not producing 10 fps.

| spell | sprite | what could be measured |
|---|---|---|
| Whirling Wind (253) | `spela2` | **order 1–3 cells/s**, low confidence. Method (c): `spela2`'s star rotates exactly −11.25°/cell, so the 8th angular Fourier harmonic of a background-subtracted star patch reads `cell mod 4`. Consistent with one 16-cell playthrough over the ~6 s effect (2.67 cells/s); cannot be separated from 1.33 cells/s. |
| Weaken (41) | `speln1` | **not measurable, zero information.** Template matching reaches NCC 0.98 (which re-confirms T389's `speln1` identification) but the best-vs-second margin is 0.002–0.087 while the sixteen cells are mutually 0.90-correlated at the rendered ~28 px size — the match is scoring the silhouette, not the cell. |
| Frost Bolt (54), Burst of Flame (93) | — | no clean isolated-sprite window in the captures |

Two hypotheses were tested explicitly rather than assumed:

* **"The whole effect runs on one clock" — REFUTED.** Render 504 of run7 holds
  six stars at phase class 1 and eight at class 3 *simultaneously*, at high
  strength; renders 509–519 are bimodal too. A single effect-wide clock puts
  every particle on the same cell. **Per-particle playback is what survived**,
  which is what this unit implements.
* **">= 10 cells/s, i.e. a cell per rendered frame" — REFUTED.** The modal
  class held class 2 across four consecutive renders spanning 0.5 s at
  *unequal* gaps (0.3/0.3/0.2 s). Invariance of `cell mod 4` across unequal
  gaps needs a rate below ~3 cells/s.

Full method, raw per-render orientation tables, the estimator null over 20,000
random background patches, and the contact sheets are in
`review/T420/oracle_frame_rate/`.

**What this means for fable.** With one playthrough per lifetime the rate is
`16 / lifetime`. `styles.impact` ships 0.4–0.8 s, i.e. **20–40 cells/s** —
about ten times the oracle bound. The knob is a LIFETIME, and it is the same
knob B226/B356 hold for the burst's size and duration, so retuning it here
would have made the next capture pair unattributable in three ways at once.
Boarded as **B370** and left alone. (Deviation from the plan's item 3, flagged:
the plan says "at the measured rate"; there is no measured rate to set, and
what was measured refutes the only reading under which a constant could be
set at all.)

### 2. The sheets, re-censused — two corrections to the record

A sha1 over all sixteen frames of all 33 files, plus an RMS distance matrix:

* T409's "one repeated frame" stays refuted. Byte-distinct counts are **8 to
  16**, not T417's "5 to 16"; exactly ONE file (`spelh1`) has frames 8..15
  byte-identical to 0..7.
* **But `speln1`, `spela2` and `spelg1` — byte-distinct at 16, 16 and 10 —
  have frame f within 0.0–0.3 of 255 of frame f+8.** Several sheets are
  effectively **8 phases played twice**. The shipped 16 cells reproduce that
  faithfully; dropping the repeats would retime the animation.
* **They are NOT uniformly fade-in cycles**, and the header no longer says
  they are. Six files have a COMPLETELY blank frame 0 (`spelb3`, `spelb4`,
  `spelc3`, `spelc4`, `speld4`, `spely3`; `spely4` is a seventh at 256 of
  11,418 alpha). FIVE **peak at frame 0** and decay from there (`spela2`,
  `spelb2`, `spelj2`, `spelu1`, `spely2`). The rest ramp to a peak in the
  middle. The census is in `review/T420/README.md`.

### 3. The layout, and why the cell shrank

| | T417 | T420 |
|---|---|---|
| sheet | 512x512 | **1024x1024** |
| cell | 64 px, 8x8 grid | **32 px, 32x32 grid** |
| cells used | 33 (one still each) | **528 = 33 sprites x 16 frames** |
| cell address | `sprite` | **`sprite * 16 + frame`** |

33 x 16 at a 64 px cell needs 2048x2048: 2.8 MB of ETC1-with-mips per pack
entry, 5.6 MB with the `_alpha` companion, on every zone's mapped budget and
on a 512 MB Orange Pi. At 32 px the whole thing fits in 1024x1024 for 1.4 MB.
A spell point sprite is clamped to 100 screen pixels by the vertex shader and
measures single digits to ~30 px in practice, so 32 px is still oversampled at
the size anything actually draws. 32 is a multiple of 4, so a cell edge is
still an ETC1 block edge and no cell can bleed into its neighbour.

Sixteen frames on a 32-column grid means a sprite occupies exactly half a row
and its animation reads left to right — `review/T420/sheets/` is that, one PNG
per sprite.

**Resampling**, all integer, no floating point, bit-identical at every
optimisation level (board B268's rule): 16 px frames magnified 2x nearest;
32 px copied; 64 px reduced 2:1 by an **alpha-weighted** box average. The
weighting is not decoration — a transparent texel's RGB in these files is
arbitrary (often black) and the additive blend samples RGB directly, so an
unweighted average darkens and fringes every sprite edge.

### 4. How a particle addresses its frame

The flat ordinal a particle carries still names a **SPRITE, never a cell**,
because `EmitterConfig::textureRegions[]` is a `uint8_t[4]` and 528 cells do
not fit in one. The cell is derived at draw time:

    sprite = textureIndex - kBase
    frame  = frameForNormalizedAge(age / maxLifetime)   // 0..15, clamped
    cell   = sprite * 16 + frame

so the sheet plays through **exactly once over the particle's lifetime**:
frame 0 at birth, frame 15 as it dies, clamped rather than wrapped (a particle
drawn past its lifetime holds the last frame — wrapping would make the six
blank-frame-0 sprites blink out at the end of every life). Three things chose
that rule: the oracle measurement refuted the alternative (§1); a fixed
cells-per-second clock would strand a short-lived particle in the opening
frames and never reach the peak, a visible regression against T417's
peak-frame still; and it needs no new state, because `UnifiedParticle` already
carries `age` and `maxLifetime`.

### 5. Tests — 14 red before, all green after

Pre-fix state produced by exporting the three production changes
(`tools/spell_sprite_atlas_builder.cpp`,
`src/client/graphics/environment/unified_particle_renderer.cpp`,
`data/textures/spell_sprite_atlas.bmp`) as a patch and `git apply -R`-ing it —
never `git stash`.

| test | pre-fix | post-fix |
|---|---|---|
| `SpellSpriteAtlasFile.IsShippedAt32BitWithTheDeclaredGeometry` | FAIL | pass |
| `SpellSpriteAtlasFile.EverySpriteHasVisibleFramesAndReachesAPeak` | FAIL | pass |
| `SpellSpriteAtlasFile.TheBlankFirstFramesAreShippedAsBlank` | FAIL | pass |
| `SpellSpriteAtlasFile.UnusedCellsAreFullyTransparent` | FAIL | pass |
| `SpellSpriteAtlasFile.EveryCellEqualsItsSourceFrame` (all 528 cells, re-derived from the TGAs) | FAIL | pass |
| `SpellSpriteAtlasFile.TheSixteenFramesOfASpriteAreNotAllTheSame` | FAIL | pass |
| `SpellSpriteAtlasFile.TheTwoOracleMeasuredSpritesAreTheirOwnFiles` | FAIL | pass |
| `SpellSpriteSheetDrawTest.FrostBoltImpactSamplesTheSpelD2Cell` | FAIL | pass |
| `SpellSpriteSheetDrawTest.FrostBoltImpactSamplesTheSpelU2Cell` | FAIL | pass |
| **`SpellSpriteSheetDrawTest.AParticleWalksItsSheetFromBirthToDeath`** | **FAIL** | **pass** |
| **`SpellSpriteSheetDrawTest.EveryFrameOfASpriteIsReachableByAge`** | **FAIL** | **pass** |
| **`SpellSpriteSheetDrawTest.AnOverrunParticleHoldsTheLastFrameRatherThanWrapping`** | **FAIL** | **pass** |
| `SpellSpriteSheetDrawTest.EveryShippedSpriteDrawsItsOwnPixels` | FAIL | pass |
| `SpellSpriteSheetDrawTest.TheSpriteSheetIsNotSampledAtTheAtlasRegionSize` | FAIL | pass |

The three bold ones are the plan's item-3 assertion, taken through a REAL
headless GLES2 point sprite (a synthetic 1024x1024 sheet, one unique colour
per cell, one particle drawn at a chosen normalized age over a magenta clear,
centre pixel read back — so a wrong cell is named, not merely detected).
`SpellSpriteAnimation.*` (4 new cases) states the same thing as pure
arithmetic and passes either way, being header-only.

Totals: `test_spell_sprite_atlas` 8/8, `test_spell_sprite_sheet_draw` 9/9,
`test_spell_target_stage_textures` 26/26.

### 6. The cascade, and the pack

One commit carries the BMP, both sha pins and the rebuilt pack.

| | before | after |
|---|---|---|
| `data/textures/spell_sprite_atlas.bmp` | 1,048,698 B | 4,194,426 B |
| its pin in `data/textures.sha256` | (T417's) | `2b2eeb77…` |
| `willeq.eqe1` | 5,881,918 B | **6,930,494 B (+1,048,576, exactly 1 MiB)** |
| entries | 128 | 128 (unchanged) |
| `data/willeq.eqe1.sha256` | `584ba1fe…` | `14a6bd45…` |

Install: **16:27:07 → 16:28:18 (71 s)** on 2026-09-06, atomic `mv` via
`scripts/build_willeq_pack.sh`, with **no `willeq`, `ctest` or gate process
running** (checked immediately before; slot2's T418 live client had exited).
The pre-change pack is preserved at `review/T420/willeq.eqe1.pre-T420`.

### 7. Verification

| step | result |
|---|---|
| `cmake --build build -j24` (RelWithDebInfo, GLES2 config) | **exit 0** |
| `ctest --test-dir build -j6` | **4,317 / 4,318**; the ONE failure is `visual_gate` (the owed bless, §8) |
| `scripts/build-gles2-x86.sh` | **exit 0** |
| visual gate, standalone | **FAIL — three `eqe1MappedBytes` only; EVERY IMAGE GOLDEN MATCHED** |
| `scripts/build-arm-noble.sh` | **exit 0** |
| `scripts/refactor/check_file_sizes.sh fable` | 22 files checked, **22 ok, 0 warn, 0 fail** |

### 8. The bless owed (board B371)

| zone | expected | got | delta |
|---|---|---|---|
| memory-coverage qeynos2 | 123,015,486 | 124,064,062 | +1,048,576 |
| memory-coverage gfaydark | 121,681,578 | 122,730,154 | +1,048,576 |
| memory-coverage befallen | 116,166,074 | 117,214,650 | +1,048,576 |

Nothing else in the gate moved — no image golden, no CPU ceiling, no other
budget. `tests/goldens/manifest.json` is untouched: an executor never
self-blesses.

### 9. Deviations and what is still owed

* **Deviation, flagged:** the plan's item 3 says the emitter advances the cell
  "at the measured rate". There is no measurable rate in T389's capture (§1),
  and what IS measurable refutes the effect-wide clock a constant rate would
  imply. The implemented rule is one playthrough per particle lifetime, whose
  rate is `16 / lifetime`; matching the oracle is a lifetime change, boarded as
  **B370** with B226/B356.
* **Still owed:** the live A/B against the oracle (**B356**). T418 held
  `/mnt/projects/fable/.eqemu-lock` (`T418 2026-09-06`) throughout; no
  connection to 127.0.0.1:5998 was opened.
* Board: **B355 STRUCK**. **B370** and **B371** are new. **B226** and **B356**
  annotated.
