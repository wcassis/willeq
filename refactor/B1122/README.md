# B1122 (#442): spell cast (Source) stage from the official client (B1118 M4)

Fourth fix card of the B1118 chain (B1119 size -> B1120 volume -> B1121 duration ->
**B1122 source** -> B1123 overrides -> B1124 atlas). Scope: the Source (cast) stage, from
OP_BeginCast to OP_Action. Evidence: `/mnt/projects/fable/review/B1122/` (index: its
`README.md`).

## 0. Result

The Source is now a body-wide cloud, not a hand pinpoint. All seven spells reach >= 80 %
of the card's coverage target on the lane-5 gate:

| spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
|---|---|---|---|---|---|---|---|
| card target (B1118, caster heights^2) | 0.38 | 0.38 | 0.48 | 0.63 | 1.23 | 3.92 | 0.08 |
| B1121 fable | <= 0.076 | 0.068 | <= 0.065 | <= 0.056 | 0.059 | 0.060 | <= 0.034 |
| **B1122 fable (gate)** | **0.402** | **0.363** | **0.519** | **0.597** | **1.082** | **3.189** | **0.067** |
| % of target | 106 | 96 | 108 | 95 | 88 | **81** | **84** |

(`<=` = never above the B1118 threshold; that is the max.)

- **Shape.** Each spell's extent and centroid is within 0.3 caster heights of the
  oracle's own reading (sec. 5). The card's band (extent 1.8-3.9, centroid 0.7-1.2) is
  wider than the oracle itself for some spells. Measured with UI excluded, the oracle's
  54/656/458 Source is 1.45-1.60 / 0.75 wide, and 656/458 sit at centroid 0.61/0.53.
  Fable follows the oracle per spell, not the card's band.
- **Onset.** By the B1118 threshold rule, fable's Source starts at BC+0.27-0.55 s
  (oracle 0.41-0.49 with UI excluded). At a quarter of each peak, fable is BC+0.41-0.50
  and the oracle BC+0.47-0.62.
- **Stop.** The card said the Source stops at OP_Action. **The oracle's does not.** Its
  cloud holds its level through Act+0.4, when the landing takes over (sec. 6). On the
  coordinator's ruling ("copy the official client") the Source now holds past
  OP_Action. OP_Action RELEASES it: it keeps emitting for `releaseDelay` (0.33 s, the
  landing's own `triggerDelay`), and then its sprites finish their life. The gap is
  gone (sec. 6a):

  | Act+0.2 s, caster heights^2 | 93 | 38 | 54 | 656 | 41 | 458 |
  |---|---|---|---|---|---|---|
  | oracle | 0.41 | 0.29 | 0.52 | 0.57 | 1.27 | 0.09 |
  | fable, stopped at OP_Action (first gate) | 0.03 | 0.03 | 0.01 | 0.02 | 0.02 | 0.03 |
  | **fable, released (re-gate `fable_R`)** | **0.39** | **0.39** | **0.56** | **0.61** | **1.06** | **0.08** |
- **Misses, stated plainly:**
  - **253 is at 81 %** on the gate leg (86 % on the repeat leg). The oracle's ribbons are
    not reproduced (B1120 FOLLOW-UP 4). The pool caps the population (sec. 7).
  - **458 is at 84 % of the card's 0.08**, but that 0.08 includes ~0.03 of the oracle's
    own Casting Time window (sec. 2). Against the effect alone (0.058) fable is at 116 %.
  - **458 is white** (M6, spela3 missing). **54/656 are pale blue-violet** (hue 223-224,
    sat 0.30-0.33) against the oracle's violet (240-244 / 0.41), and they draw as a
    hard-edged cylinder (sec. 5). Colour is M6/M7's, not this card's.
  - **The fx_timing LANDING onset moved later** (0.41-0.52 -> 0.51-1.00) and 253 reads
    "none". This is the instrument, not the landing (sec. 6). On a gap-aware reading the
    landing onset, end and coverage are unchanged within capture noise.

## 1. Mechanism (B1118 M4)

`SpellVisualFX::createCastGlow()` used to create the Source on the two hand-bone
pseudo-entities, and it overwrote the style's geometry:
- `orbitalRadius = 0.4`;
- `positionOffset = 0`.

The config was `castGlow`: POINT spawn, ORBITAL, 24/s per hand (`castSpray`: POINT, 45/s,
sprayed along the facing). The result was 5-15 sprites at the hands, mostly behind the
spawned NPC (B1119 sec. 4).

## 2. The oracle's Source, per spell

The tool is `review/B1122/scripts/fx_source_shape.py`.
- **Mask:** `fx_timing.py`'s Source mask: rise > 80 over the median of BC-1.2..-0.2 s,
  inside the B1118 crop.
- **Per frame:** coverage, extent (x p2..p98), centroid height and rowtop, in caster
  heights.
- **UI excluded on the oracle.** The client's Casting Time window opens at BeginCast
  inside the crop (root 600,40,170,75), and the gem-bar tooltip sits at x < 200. Both
  were counted by B1118:
  - its 458 Source "extent 2.74, rowtop 2.56" was the Casting Time window plus chat
    text;
  - its 458 onset BC+0.42 was the window opening, not the effect.

  `source/masks/oracle_458.png` shows the mask.
- **Excluded on fable:** two swaying tree canopies (490,0,190,200 and 170,0,280,140).
- **Strips:** `review/B1122/strips/oracle_source_<id>.png` and `strip_source_<id>.png`, at
  BC+0.25/0.5/1.0/1.5 s and Act-0.1 s.

| spell | what the oracle draws (strip frames) | peak cov (UI excl.) | extent | centroid | onset |
|---|---|---|---|---|---|
| 93 (oracle_druid) | Orange spark motes in a loose cloud from the knees to above the head, drifting up, plus flares on the forearms. Sparse at f7499 (BC+0.49), filled by f7512 (BC+1.01), densest at f7522 (Act-0.09). | 0.364 | 2.92 | 0.89 | 0.489 |
| 38 (oracle_leg) | The same record and the same picture: f9697 (BC+0.51) to f9744 (Act-0.11). | 0.361 | 2.24 | 0.95 | 0.429 |
| 54 (oracle_leg) | Violet squiggles swirling in a column round the body, ~1.5 wide, from the feet to ~1.4. A few at f10639 (BC+0.48), dense at f10650 (Act-0.10). | 0.461 | 1.60 | 0.82 | 0.440 |
| 656 (oracle_leg) | The same record and the same column. Dense from f11598 (BC+1.02) to f11620 (Act-0.12). | 0.615 | 1.45 | 0.61 | 0.417 |
| 41 (oracle_enc) | Green-yellow balls in a dome ~2.7 wide and ~1.7 tall round the caster. They appear at f6384 (BC+0.49), fill by f6397 (BC+1.01), and hold to f6407 (Act-0.10). | 1.209 | 2.68 | 0.87 | 0.412 |
| 253 (oracle_druid) | Blue spela2 stars laid in long curved ribbons that cross the whole frame. Already wide at f6498 (BC+0.51); the frame is full by f6510 (BC+0.99). | 3.884 | 3.70 | 1.11 | 0.429 |
| 458 (oracle_leg) | Small cyan sparkles in a narrow column round the legs and waist, ~0.75 wide. They appear by f7636 (BC+1.02) and hold to f7698 (Act-0.10). | 0.058 | 0.75 | 0.53 | 1.019 |

Full series: `review/B1122/source/oracle/source_oracle_<id>.txt`.

## 3. The change

### Code (minimum)

The first three bullets are the body cloud. The last is the coordinator's ruling: hold
past OP_Action.

- **New style key `anchor`** (`spell_effects_config.{h,cpp}`: StyleSettings, parse,
  merge, like B1120's `triggerDelay`).
  - `"body"` sets `SpellEffectDef::bodyAnchored` in `SpellPresets::CastGlow` /
    `CastSpray`.
  - Without the key, the legacy hand path is unchanged. The built-in fallback styles of
    `setDefaults` have no anchor.
- **`createCastGlow()`** (`spell_visual_fx.cpp`). A body-anchored def gets ONE effect on
  the caster's own entity:
  - with the style's own geometry (no `orbitalRadius` / `positionOffset` override);
  - without the facing-direction spray (`useDynamicDir` false).

  In the first gate the emitter lifetime was the cast time and `removeCastGlow()` at
  OP_Action freed the cloud. After the ruling, OP_Action releases it instead (last
  bullet below).
- **`triggerDelay` on the Source.** `CastGlow` / `CastSpray` now call `applyTrigger()`.
  The Source becomes the sim's existing DELAYED arm; no sim change.
- **Release at OP_Action (new style key `releaseDelay`, parsed and merged like
  `anchor`; 0 = the old instant stop).**
  - `SpellVisualFX::releaseCastGlow(caster, spell)` replaces `removeCastGlow()` in the
    bridge's SpellCastVisualComplete arm. That is the one call; the B1105 arming
    (`CastGlowRoute`) and the landing trigger are untouched.
  - With a `releaseDelay` it sends `ParticleManager::releaseSpellEffectsForEntity()`.
    The worker's new `ReleaseSpellEffectsEntity` arm sets the matching effects'
    `maxDuration` to now + delay: the lifecycle phase then stops their emitters, frees
    nothing, and erases each effect after its last sprite dies.
  - Without a `releaseDelay` it is `removeCastGlow()`.
  - An interrupt still calls `removeCastGlow()` and frees at once. The oracle's
    interrupt was not measured.
  - `createCastGlow()` gives a released body cloud an unbounded emitter and
    `maxDuration` = cast time + delay. The emitter cannot stop by itself before
    OP_Action arrives; the cap applies only if OP_Action never comes.

### Config (`config/spell_effects.json`)

The shapes come from sec. 2, in caster heights, converted with B1120's scale:
- H = 6.5 world units per caster height;
- the entity anchor is A = 3.6 world units above the feet.

The generator is `review/B1122/scripts/b1122_gen.py`. Sizes, lifetimes and texture
regions are B1119's and are untouched; the sprite names still come from spells.eff
(Source -> cast glow).

| record (spells) | where | shape (caster heights) | rate /s |
|---|---|---|---|
| 0 (93, 38) and every unmeasured spell | styles `castGlow` + `castSpray`, `anchor: body`, `triggerDelay` 0.25, `releaseDelay` 0.33 | BOX +-1.3 wide x +-0.65 deep, 0.28-1.38 above the feet, rising 0.35 +-0.2 H/s | 80 |
| 23 (54, 656) | `castGlow` override | ORBITAL r = 0.75, w 3 rad/s, 0.25-1.35 high | 1050 |
| 20 (41, Alteration = castSpray) | `castSpray` override | BOX +-1.15 x +-1.15, 0.25-1.85 high, near-static | 330 |
| 35 (253) | `castGlow` override | BOX +-1.9 x +-1.0, 0.2-2.42 high, near-static (no ribbons) | 1950 |
| 33 (458) | `castGlow` override | BOX +-0.36 x +-0.36, 0.15-1.05 high, rising 0.1 H/s | 65 |

- **Rates** were calibrated on two wizard legs with all seven casts: `cal/run_v1`
  (150/900/200/1200/50) and `cal/run_v2` (80/1050/330/1800/65). The gate config is v2
  with 253 at 1950 and small centroid moves.
- **The delay.** A dense cloud crosses the B1118 threshold ~0.1-0.25 s after it starts,
  so 0.25 puts the onset at BC+0.3-0.55.

## 4. Gate (B1118 legs A/B/C, ThinkPad lane 5)

- **Build.** `~/src/willeq`, branch `b1122-fx-source`. The binary is d0597834 (the code);
  the legs ran e2410513, which is d0597834 plus the config (`fable_*/willeq.head`).
  `scripts/build-gles2-x86.sh` JOBS=12, `CMAKE_BUILD_TYPE=Release`. Both WIP commits
  are squashed into this commit.
- **Capture.** x11grab at 30.00 fps. The max frame gap over every whole video is 34 ms;
  0/59 stale grabs in every landing window. `--dump-frames-dir` was not used.
- **Frame time.**
  - Every `FRAME TIMING BREAKDOWN` window counts 60 frames (127 + 80 + 61 windows).
  - Window-mean Total Frame: median 0.85-0.98 ms.
  - Worst single frame during the casts: 5.8 ms. The 57.4 / 33.0 ms frames of legs B/C
    are the first window after zone-in, before any cast.
  - 0 `slow!` warnings.
- **Pool.** Every leg logged `pool initialized (size=4096)`.
- **Two legs were re-run, and the first captures are kept:**
  - `fable_A_xorg_restarted/`: unattended-upgrades re-executed systemd at 06:30:39 and
    Xorg :99 restarted. The client died with "X connection to :99 broken" after 38 and 54.
  - `fable_B_scene_change/`: a scene-wide change at 253's Act+8.46 s corrupted that
    cast's tail baseline. Its Source readings are valid and are used as the repeat
    sample: 93 0.468, 253 3.378.
- **Table:** `review/B1122/gate_table.tsv`.

- **Re-gate after the ruling: `fable_R/`.** This is the 7-spell wizard leg
  (`plans/fable_cal.txt`), as the coordinator asked. Binary b193d77f is this commit's
  code and config.
  - Capture: x11grab 30.00 fps, max gap 34 ms, 0/59 stale grabs.
  - Frame time: 173 windows, all 60 frames; window-mean 0.77-1.04 ms; worst frame
    5.7 ms; 0 slow-frame warnings.
  - Pool: 4096.
  - Source peaks (BC-referenced, so the release does not touch them):

    | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
    |---|---|---|---|---|---|---|---|
    | coverage | 0.373 | 0.393 | 0.523 | 0.602 | 1.203 | 3.178 | 0.069 |
    | extent | 2.40 | 2.43 | 1.52 | 1.51 | 2.54 | 3.95 | 0.83 |
    | centroid | 0.80 | 0.84 | 0.69 | 0.68 | 0.92 | 1.10 | 0.60 |

    That is 98 / 103 / 109 / 96 / 98 / 81 / 86 % of the card.

## 5. Source before/after (caster heights)

| spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
|---|---|---|---|---|---|---|---|
| coverage oracle (UI excl.) / **B1122** | 0.364 / **0.402** | 0.361 / **0.363** | 0.461 / **0.519** | 0.615 / **0.597** | 1.209 / **1.082** | 3.884 / **3.189** | 0.058 / **0.067** |
| extent oracle / **B1122** | 2.92 / **2.62** | 2.24 / **2.31** | 1.60 / **1.52** | 1.45 / **1.52** | 2.68 / **2.43** | 3.70 / **3.98** | 0.75 / **0.77** |
| centroid oracle / **B1122** | 0.89 / **0.87** | 0.95 / **0.91** | 0.82 / **0.71** | 0.61 / **0.69** | 0.87 / **0.92** | 1.11 / **1.07** | 0.53 / **0.64** |
| onset (threshold rule) oracle / **B1122** | 0.489 / **0.554** | 0.429 / **0.267** | 0.440 / **0.373** | 0.417 / **0.461** | 0.412 / 0.122* | 0.429 / 0.129* | 1.019 / **0.433** |
| onset at 25 % of peak, oracle / **B1122** | 0.529 / **0.454** | 0.469 / **0.500** | 0.520 / **0.407** | 0.577 / **0.427** | 0.572 / **0.422** | 0.469 / **0.429** | 0.619 / **0.500** |
| hue/sat oracle / B1121 / **B1122** | 36/.74 / 32/.78 / **29/.76** | 37/.69 / 32/.74 / **27/.76** | 244/.41 / 240/.14 / **224/.33** | 240/.42 / 281/.16 / **223/.30** | 76/.72 / 67/.53 / **74/.64** | 214/.70 / 203/.39 / **198/.51** | 199/.32 / white / **white** |

- `*` 41/253 are self-casts. Fable's colour-cycling target outline on the caster is
  above the threshold from BC+0 until the tour's `/cleartarget` (the B1118 BC+0.226
  blip). The cloud itself starts at BC+0.33-0.41 (`source/b1122/source_fable_*.txt`).
- **Record 23 (54/656) looks like a cylinder.** Every sprite rides one orbit radius, so
  the silhouette has hard vertical edges where the oracle's squiggles are ragged. The
  ORBITAL arm has no radius spread (sec. 9).
- **Hue.** 93/38 moved 32 -> 27-29 (away from the oracle's 36-37, small). 41 moved
  toward the oracle. 54/656 gained saturation (0.14-0.16 -> 0.30-0.33; the oracle is
  0.41) at hue 223. 253 gained saturation (0.39 -> 0.51; the oracle is 0.70). 458 stays
  white (M6). The sprite is spells.eff's; the mix is what changed.

## 6. Landing non-regression (B1121 gate -> B1122 gate)

The landing's config keys are byte-identical to B1121's (a JSON diff of every
`spellComplete` / `impact` style and override is empty).

| spell | coverage | end (Act+, s) | onset, gap-aware (Act+, s) | rowtop | ROI p90 | hue |
|---|---|---|---|---|---|---|
| 93 | 2.970 -> 2.877 (repeat leg 2.898) | 4.888 -> 4.924 | 0.388 -> 0.424 | 2.57 -> 2.57 | 0.124 -> 0.111 | 36 -> 36 |
| 38 | 1.714 -> 1.706 | 4.776 -> 4.844 | 0.443 -> 0.444 | 2.57 -> 2.57 | 0.090 -> 0.092 | 32 -> 33 |
| 54 | 1.591 -> 1.569 | 5.170 -> 5.159 | 0.436 -> 0.426 | 1.53 -> 1.54 | 0.078 -> 0.083 | 220 -> 220 |
| 656 | 1.256 -> 1.256 | 5.072 -> 5.174 | 0.405 -> 0.441 | 1.54 -> 1.53 | 0.075 -> 0.074 | 220 -> 220 |
| 41 | 3.329 -> 3.336 | 6.347 -> 6.332 | 0.414 -> 0.398 | 2.57 -> 2.57 | 0.206 -> 0.186 | 71 -> 71 |
| 253 | 1.571 -> 1.496 | 6.457 -> 6.493 | 0.457 -> 0.393 | 2.57 -> 2.57 | 0.055 -> 0.056 | 203 -> 203 |
| 458 | 1.608 -> 1.548 | 5.059 -> 5.010 | 0.292 -> 0.444 | 1.78 -> 1.78 | 0.068 -> 0.073 | 35 -> 35 (white) |

- **Coverage.** Two rise or hold; five fall by 0.5-4.8 %. 253 falls 4.8 %, 458 3.7 %
  and 93 3.1 % (2.4 % on the repeat leg). B1121 measured a ±2-3 % capture-to-capture
  spread, so 253 and 458 sit slightly outside it. No mechanism was found:
  - the landing config is identical;
  - in that (first) gate the Source was freed at OP_Action, 0.33 s before the landing's
    delayed trigger, so it held no pool slot and drew nothing then;
  - the pool never filled (Source <= 1924 + landing <= 2160 are never live together).

  Flagged, not explained.
- **38 re-read (B1121 sec. 6a).** 1.706 on this gate: inside B1121's four readings
  (1.626-1.714) and still below B1120's three (1.733-1.747). **The drop persists.** No
  B1122 change acts on 38's landing.
- **End and rowtop** are unchanged (end -0.05..+0.10 s). **ROI p90** moved -10 % for
  93/41, inside the merged-blob caveat of B1120 sec. 7. **Hue** is unchanged.
- **Onset (instrument note).** `fx_timing.py`'s LANDING onset is "the first frame above
  1.5 x the Source tail". With a Source as large as the oracle's, that rule now waits
  for the landing to outgrow the Source:
  - it reads 0.51-1.00 s instead of 0.41-0.52;
  - 253's landing never reaches 1.5 x its 5.0 caster heights^2 Source, so it reads
    "none".

  The gap-aware reading `scripts/landing_onset_abs.py` (the first frame above the
  threshold after the post-Action minimum) reproduces fx_timing's B1121 coverage and end
  exactly on the B1121 legs. On it the onset is unchanged (0.39-0.44).
- **Measured on the way: the oracle's Source does NOT stop at OP_Action** (lit series
  around Act, `measure/fable_<id>/lit80.tsv` vs `B1118/measure/oracle_<id>/lit80.tsv`;
  the full table is sec. 6a):

  | | Act-0.1 | Act+0.1 | Act+0.3 | Act+0.45 |
  |---|---|---|---|---|
  | 93 oracle | 0.41 | 0.43 | 0.42 | 0.57 (landing) |
  | 93 fable | 0.45 | 0.03 | 0.03 | 0.11 |
  | 41 oracle | 1.16 | 1.28 | 1.30 | 2.60 |
  | 41 fable | 1.07 | 0.01 | 0.02 | 0.15 |

  The oracle holds its cloud until the landing takes over (Act+0.4) and then jumps. The
  first gate (stop at OP_Action) dropped to the noise floor. The coordinator ruled to
  match the oracle (sec. 3, sec. 6a).

### 6a. After the ruling: the hold (`review/B1122/gap_table.tsv`, `scripts/gap_table.py`)

Coverage (caster heights^2) on the tail-baseline lit series:

| spell | capture | Act-0.1 | +0.1 | +0.2 | +0.3 | +0.4 | +0.5 |
|---|---|---|---|---|---|---|---|
| 93 | oracle | 0.41 | 0.43 | 0.41 | 0.42 | 0.35 | 0.59 |
| 93 | stop@Act | 0.41 | 0.03 | 0.03 | 0.03 | 0.06 | 0.24 |
| 93 | **released** | 0.37 | **0.39** | **0.39** | **0.42** | 0.45 | 0.60 |
| 38 | oracle / stop / **released** | 0.31 / 0.36 / 0.40 | 0.27 / 0.03 / **0.38** | 0.29 / 0.03 / **0.39** | 0.32 / 0.03 / **0.36** | 0.33 / 0.05 / 0.37 | 0.48 / 0.15 / 0.40 |
| 54 | oracle / stop / **released** | 0.46 / 0.47 / 0.49 | 0.52 / 0.01 / **0.55** | 0.52 / 0.01 / **0.56** | 0.50 / 0.01 / **0.57** | 0.48 / 0.01 / 0.61 | 1.97 / 0.23 / 0.71 |
| 656 | oracle / stop / **released** | 0.63 / 0.58 / 0.60 | 0.60 / 0.02 / **0.60** | 0.57 / 0.02 / **0.61** | 0.62 / 0.02 / **0.59** | 1.06 / 0.02 / 0.62 | 1.74 / 0.19 / 0.69 |
| 41 | oracle / stop / **released** | 1.16 / 1.07 / 1.13 | 1.28 / 0.01 / **1.06** | 1.27 / 0.02 / **1.06** | 1.30 / 0.02 / **1.07** | 1.32 / 0.07 / 1.09 | 2.70 / 0.33 / 1.03 |
| 253 | oracle / stop / released* | 4.28 / 3.57 / 4.71 | 4.36 / 0.01 / 4.75 | 4.44 / 0.03 / 4.77 | 4.43 / 0.05 / 4.75 | 4.30 / 0.06 / 4.69 | 4.17 / 0.11 / 4.29 |
| 458 | oracle / stop / **released** | 0.09 / 0.09 / 0.06 | 0.09 / 0.02 / **0.07** | 0.09 / 0.03 / **0.08** | 0.09 / 0.04 / **0.09** | 0.07 / 0.05 / 0.09 | 0.23 / 0.13 / 0.13 |

- `*` In `fable_R` (as in the first leg B) a scene-wide brightness change at 253's
  Act+8.4 s shifts that cast's tail baseline by ~1.6. The 253 row is offset by that;
  its shape (flat through the gap) still reads.
- **The hold matches.** Fable no longer drops to ~0.03; it stays at its pre-Action
  level, within ±0.1 of the oracle except 41 (1.06 vs 1.27-1.30, its Source peak being
  88-98 %).
- **What still differs:** the oracle's landing JUMPS at Act+0.4-0.5 (54: 0.48 -> 1.97;
  41: 1.32 -> 2.70), while fable's ramps (54 0.61 -> 0.71). That is the landing's
  emission profile (B1120/B1121), not the Source.

### 6b. Landing onset instrument (adopted, with its limit)

On the coordinator's instruction, `scripts/landing_onset_abs.py` replaces B1118's
"1.5 x the Source tail" rule as the landing-onset instrument, just as B1119 replaced the
whole-frame p90 with `fx_sprite_roi.py`. It prints two readings:
- **gap-aware** (`ABS`): the first frame above the threshold after the post-Action
  minimum.
- **rise** (`RISE`): the first frame above the Source tail plus the threshold.

**Neither separates the landing from a HELD Source by pixel count:**
- On a capture with a post-Action gap (fable B1121, fable stop@Act) `ABS` is exact. It
  reproduces fx_timing's B1121 coverage and end on every spell.
- On a held-Source capture (the oracle; `fable_R`) there is no gap, so `ABS` returns
  the gap minimum itself (oracle 0.00-0.39, fable_R 0.02-0.39).
- `RISE` reads the oracle's noise on its held cloud (93 0.107, 54 0.063, 253 0.202) and
  never fires when the landing is smaller than the Source (fable 253).

So the landing onset is pinned in code (`triggerDelay` 0.33,
`TheLandingWaitsForTheOraclesOnset`) and read on gap captures. A held-Source onset needs
a colour or region separation (sec. 9, item 1).

### 6c. Landing non-regression on `fable_R` (same tour as `cal/run_v1`)

Same 7-spell wizard tour; the landing config is identical to B1121's. The first column
is v1 (B1122 code, stop at OP_Action):

| spell | coverage v1 -> R | end v1 -> R (Act+, s) |
|---|---|---|
| 93 | 2.956 -> 2.909 | 4.859 -> 4.939 |
| 38 | 1.645 -> 1.686 | 4.804 -> 4.881 |
| 54 | 1.544 -> 1.586 | 5.128 -> 5.161 |
| 656 | 1.264 -> 1.270 | 5.081 -> 5.175 |
| 41 | 3.316 -> 3.319 | 6.369 -> 6.341 |
| 458 | 0.881 -> 0.931 | 4.893 -> 4.919 |
| 253 | 1.511 -> unreadable (tail shift above) | 6.5 (B1121) -> unreadable |

- **The landing reads the same, released or not.** Coverage is within -1.6 to +5.7 %,
  ends within +0.03..+0.09 s. The landing peaks at Act+1.7-2.1, after the released
  Source is dead (Act+0.33 + <= 1.2 s).
- 458's ~0.9 on this tour, against 1.55 on gate leg A, is the tour: B1121's calibration
  leg read 1.35-1.39 there too.
- 38 is 1.686 (B1121 sec. 6a's question). It still sits below B1120's 1.733-1.747.

## 7. Pool accounting (4096, not raised)

Live Source sprites at the end of each cast (OP_Action), from the real worker
(`EverySourceCloudFitsBesideALanding`):

| spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
|---|---|---|---|---|---|---|---|
| live at OP_Action | 84 | 82 | 787 | 1055 | 176 | 1924 | 64 |

- **The Source now overlaps its own landing.** It is released at OP_Action, still
  feeding until Act+0.33 (when the landing triggers), and its sprites die by Act+1.53.

  `ASourceAndItsOwnLandingFitHalfThePool` drives one whole cast per spell through the
  real worker and records the peak live count over the cast (three runs):

  | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
  |---|---|---|---|---|---|---|---|
  | Source + own landing, peak | 1898-1900 | 1720-1727 | 1882-1892 | 1877-1893 | 1514-1520 | 2168-2172 | 608-612 |
  | own landing alone (after Act+2) | 1840-1857 | 1676-1684 | 1511-1521 | 1504-1522 | 1514-1520 | 2168-2172 | 595-605 |

  - **No spell needs a taper.** The landing ramps up (<= 2500/s) more slowly than the
    released Source drains: it decays from Act+0.33 and is dead by Act+1.53, before any
    landing peaks (Act+1.7-2.1).
  - 54/656 gain ~375 sprites (Source tail + early landing), the most of any spell, and
    stay <= 1893.
  - 253's Source adds nothing at its field's peak.
  - The coordinator's "253 Source 1924 + 253 landing 2160" never coexists: by the time
    the field holds 2160 the cloud has been dead for > 0.4 s.
- **Worst case against the pool, with a third (previous) landing live:**
  - a 253 field (2160) + a 54/656 cast (<= 1893) = 4053 <= 4096;
  - a record-0 landing (1925) + a 253 cast = 4085 expected, which is B1121's figure,
    unchanged here. The sampled 253 field peak (2168-2172) puts it at 4093-4097: 0-1
    sprite over in the worst sample. That is B1121's tight bound, not a B1122 cost.
- 4096 stays.
- **The previous cast's landing.** A Source can overlap the previous cast's landing,
  which is <= 2048 (`OneFedLandingFitsHalfThePool`). Every Source is <= 2048, so
  Source + landing <= 4096. The worst case is 253 (1924) over a record-0 landing (1925):
  3849.
- **253's own landing** (a 2160-sprite field) cannot be live under a new 253 Source: the
  recast is 12 s and the landing ends at Act+6.5 s.
- **253 is pool-bound.** Its 1950/s gives 81-86 % of the coverage target. Reaching 100 %
  at B1119's sprite size needs ~2500 live sprites. The other route is the oracle's
  larger 253 Source sprite (ROI p90 0.178 vs fable's ~0.075, B1120 sec. 7). That is
  B1123's or Will's (#440).
- **Memory.** No new allocation: the pool is the B1120 4096.
- **Pi fill (estimate, not measured).** The 253 Source draws ~1900 sprites for ~1.2 s,
  like a 253 landing.

## 8. Tests (same commit)

- **`tests/test_spell_landing_volume.cpp`** (the real worker):
  - `TheSourceIsABodyWideCloudShapedLikeTheOracles`: bodyAnchored, plus the particle
    width and mean height per spell in the sec. 2 bands.
  - `TheSourceWaitsForTheOraclesOnset`: DELAYED, 0.2-0.35 s, nothing before it.
  - `EverySourceCloudFitsBesideALanding`: live count >= the gate's (85 % of the
    calibrated count) and <= half the pool.
  - `AStyleWithoutADelayStaysImmediate` now uses BuffAura (the cast glow has a delay).
- **`tests/test_caster_own_landing.cpp`** (SpellVisualFX -> ParticleManager -> worker):
  - `TheSourceIsOneBodyCloudThatAnInterruptFrees`: one effect on the caster, none on
    either hand id, a filled cloud after 1 s, and 0 live sprites one frame after
    `removeCastGlow()` (the interrupt).
  - `TheSourceHoldsPastTheActionUntilTheLandingTakesOver`. After `releaseCastGlow()`
    (OP_Action) plus the landing:
    - the cloud keeps >= 3/4 of its size at Act+0.3;
    - `releaseDelay` is 0.25-0.45 s;
    - no Source sprite younger than 0.5 s is alive at Act+2.3, i.e. emission stopped.
  - `ASourceAndItsOwnLandingFitHalfThePool` (in `test_spell_landing_volume`): the sec. 7
    table. The bound is half the pool, or the landing's own peak for 253.
- **`tests/test_spell_colour_and_overrides.cpp`:**
  - `B1122SourceShapesFollowTheSpellsEffRecord`: record 0 and unmeasured spells are the
    style, 54 == 656, 41/253/458 carry their own shape, and the B1119 sizes are
    untouched.
- **`tests/test_spell_burst_oracle_match.cpp`:**
  - `TheSourceCloudRatesAreTheCalibratedOnes`: rates, delay, and the 0.8-1.2 s particle
    life. Its comment names the measuring command.
- **RED on the stop-at-OP_Action behaviour** (B1122's first commit's config, without
  `releaseDelay`; `review/B1122/red_stop_at_action.txt`): two tests fail.
  - `TheSourceHoldsPastTheActionUntilTheLandingTakesOver`: "the cloud went dark at
    OP_Action".
  - `ASourceAndItsOwnLandingFitHalfThePool`: `releaseDelay` is 0.
- **RED before the fix:**
  - With B1121's config (`review/B1122/red_b1121_config.txt`), all six new config tests
    fail: the three Source tests in `test_spell_landing_volume`,
    `TheSourceCloudRatesAreTheCalibratedOnes`, `B1122SourceShapesFollowTheSpellsEffRecord`
    and `TheSourceIsOneBodyCloudThatStopsAtTheAction`.
  - With the final config and createCastGlow's body branch disabled
    (`red_b1121_code.txt`), `TheSourceIsOneBodyCloudThatStopsAtTheAction` fails: "the
    Source is still pinned to the right hand".
- **Local run** (Release, named targets, -j8):
  - `ctest -R "Spell|Sim|Particle|Landing|DataPath|ConfigKey|B1106|CasterOwn"`: 441
    tests, 100 % pass, 32 skipped. The skips need EQ client files, as on fable.
  - `ctest -L hygiene`: 45/48. The same three `*Reproducible` tests fail: their
    generator tools were not built.

## 9. FOLLOW-UP (for the orchestrator; workers do not cut cards)

1. **DONE on the coordinator's ruling** (sec. 3, 6a): the Source holds to the landing.
   Still open:
   - a landing-onset reading for held-Source captures (colour or region separation,
     sec. 6b);
   - the oracle's landing JUMP at Act+0.4-0.5 against fable's ramp (landing emission
     profile, B1120/B1121);
   - the oracle's interrupt behaviour (unmeasured; fable still frees at once).
2. **253 Source at 81-86 %**: pool-bound at B1119's sprite size. The oracle's 253
   Source sprite is ~2.4x larger on the ROI instrument (B1123 or a pool ruling, #440).
   The ribbons (phase-locked orbit) are B1120 FOLLOW-UP 4's.
3. **Record-23 cylinder.** The ORBITAL arm takes one `orbitalRadius`, so 54/656 draw a
   hard-edged cylinder. A radius spread (min..max) in the ORBITAL spawn would give the
   oracle's ragged swirl. That is new sim code, not done here.
4. **Colour.** 458 white (M6). 54/656 hue 223 vs 240-244 (M7, secondary list). 93/38
   Source hue 27-29 vs 36-37: the spelc2/spelc4 mix (M8 fragments).
5. **Landing coverage -3..-5 % on 93/253/458, and 38's B1121 drop**, persist with no
   mechanism. A repeat of all three legs on the same binary would size the noise.
6. **Instrument.** The B1118 Source numbers include the oracle's Casting Time window
   (0.02-0.03 caster heights^2, and 458's extent/onset). Future cards should read the
   oracle with `review/B1122/scripts/source_all.py`.
7. **Unmeasured spray spells** (Alteration/Abjuration other than 41) now get the record-0
   cloud at the castSpray sizes and 0.4-0.7 s life. That is the style rule, not a
   measurement.
8. **Lane 5 hygiene.** unattended-upgrades restarted Xorg :99 mid-leg (06:30:39 UTC).
   Disable it on the ThinkPad, or hold the lane while it runs.
