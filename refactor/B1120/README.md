# B1120 (#440): spell landing VOLUME and SHAPE (B1118 M2)

Second fix card of the B1118 chain (B1119 size -> **B1120 volume** -> B1121 duration ->
B1122 source emitter -> B1123 overrides -> B1124 atlas). Scope: the landing stage's
shape, velocity, gravity and extent. Evidence: `/mnt/projects/fable/review/B1120/`
(index: its `README.md`).

## 0. Result

- **The landing is now a volume, not a waist-high disc.** Every spell's peak landing
  reaches the oracle's shape class: a fire column at 2.57 caster heights (93, 38;
  crop-limited, like the oracle's), a pillar and a settling dome (41), a swirl and flat
  glints (54, 656), a frame-wide star field (253), and a ball burst (458). The height
  target (>= 2 caster heights for 93/38/41) **is met** for all three.
- **The coverage target is met for 1 of 7 spells, and 41 sits on the line.**

  | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
  |---|---|---|---|---|---|---|---|
  | % of the oracle's peak coverage | 51 | 45 | 63 | 51 | **79.8** | 22 | **125** |

  41 read 80.4 % on the last calibration run (`cal/run_v3`) and 79.8 % on the gate.
  The shortfall has three named causes (sec. 6). Fixing them is outside this card:
  - the 4096-particle pool,
  - B1119's sprite sizes,
  - the M8 sprite fragments.
- **Two forced side effects outside the shape keys** (sec. 4):
  - **Pool wiring.** `global.maxParticles` never reached the worker in a live run, so
    every lane-5 log said `pool initialized (size=1024)`. It is now wired. That costs
    +368,640 bytes of pool on every host (sec. 7).
  - **Onset delay.** A `triggerDelay` holds the landing onset in the oracle's band.
    Without it, the volume emitters cross the visibility threshold at Act+0.09-0.27 s;
    the oracle's onset is Act+0.40-0.80 s.

  As a consequence of the delay, every landing ENDS ~0.33 s later (Act+2.75-2.88 s,
  B1119 2.35-2.53). That moves toward the oracle's 4.85-6.52 s. Duration itself is
  still B1121's.

## 1. Mechanism (B1118 sec. 5 M2)

Before this card, `config/spell_effects.json` defined both landing styles as follows:

- **`spellComplete`:** a RING r=0.5 spawn with BURST velocity 3.0 +-1 horizontal and
  1.5 +-0.5 vertical, gravity -2. It is anchored at the caster.
- **`impact`:** a POINT spawn with RADIAL_EXPAND 4.0 horizontal and 1.0 vertical, gravity
  -2. It is anchored at the target, +1 world unit.
- **Spawn and integration:** `SimulationWorker::spawnSpellParticle()` and the LINEAR /
  BURST / RADIAL_EXPAND arms of the physics phase (`src/client/graphics/sim_particles.cpp`).

Under those numbers, a particle rises at most 0.56 world units above the caster anchor.
On lane 5 that is <= 0.64 caster heights above the feet (sec. 3). The gate tool confirms
it on the B1119 capture: the lit region's top row at the coverage peak is 0.79-0.91
caster heights for every spell (`shape/b1119/`).

**No new motion type was needed.** The existing machinery expresses every oracle shape:
- BOX / SPHERE spawn shapes
- LINEAR / BURST / RADIAL_EXPAND / ORBITAL motion
- `positionOffset`
- drag and gravity

Two things were missing. Neither is a motion type (sec. 4):
- a delayed trigger in the data;
- the pool wiring.

## 2. The oracle, frame by frame (B1118 videos, B1118 instrument)

`review/B1120/scripts/fx_shape.py` reads the oracle videos with the same parameters as
`fx_timing.py`'s LANDING census:
- the same mask: rise > 80 over the tail baseline;
- the same crop: 150,60,900,540 at 210 px per caster height, feet at (640,598).

Per frame it also reports four shape fields:
- **extent:** x p2..p98;
- **top:** p2 of y above the feet;
- **centroid** height;
- **rowtop:** the highest row lit over >= 0.1 caster width.

Its peak coverages reproduce B1118's exactly:
5.847 / 3.889 / 2.461 / 2.493 / 4.154 / 7.281 / 1.242.

Full series: `review/B1120/shape/oracle/`. The frames read for each spell
(Act = OP_Action):

- **93 Burst of Flame (oracle_druid):**
  - **Flash.** A white flash at the feet at f7537-7540 (Act+0.5-0.6). That is M8's.
  - **Spark shell.** The shell expands from ~1.7 caster heights wide at f7545 (Act+0.8)
    to 2.6 at f7552 (+1.1) and 3.3 at f7560 (+1.4). That is ~1.5 caster heights/s
    radial, slowing down, which gives **BURST 1.6/s with drag 0.5**.
  - **Column.** The column grows from the ground. Its centroid goes from 0.58 (Act+0.63)
    to 1.15 (Act+2.03). Rowtop reaches the crop top, 2.56, at f7570 (Act+1.83). That
    is >= 1.5 caster heights/s at the top and a ground-anchored base, which gives
    **a column with a uniform vertical speed of 0.15-1.7/s from the feet**.
  - **Width.** Once the sparks are gone, the column's width is 1.74-1.86 at
    f7610-7630 (Act+3.4-4.2). That gives **half-width 0.85**.
  - **Peak.** f7573 (Act+1.95): extent 3.40, centroid 1.14, 55 % of the lit pixels above
    one caster height.
- **38 Lightning Bolt (oracle_leg):** the same record and the same picture. Peak f9800
  (Act+2.13): extent 2.81, rowtop 2.46.
- **41 Weaken (oracle_enc):**
  - **Dome.** Balls fill >= 3.9 caster heights of width by f6425 (Act+0.62); the crop is
    the limit. That is ~3 caster heights/s outward, which gives **RADIAL_EXPAND 3.0/s,
    drag 1.4, so it stops at ~2.1**.
  - **Settling.** The centroid falls from 0.83-0.93 (Act+0.6-2.6) to 0.58-0.66
    (Act+3.8-5.4). The share of lit pixels above one caster height falls from 0.34-0.40
    to 0.10-0.19. The balls settle, which gives **gravity -0.6/s²**.
  - **Pillar.** A yellow pillar ~0.7 wide and ~2 tall at f6440-6480. That gives **a
    column of half-width 0.35 at 0.2-1.6/s**.
  - **Peak.** f6448 (Act+1.54).
- **54 Frost Bolt / 656 Shock of Ice (oracle_leg):**
  - **Glints.** The glints' extent goes from 1.50 at f10658 (Act+0.22) to 3.99 at f10668
    (Act+0.62). That is ~3.1 caster heights/s for the half-extent, at knee height
    (centroid 0.65). It gives **RADIAL_EXPAND 6.0/s, drag 2.5 (stops at ~2.4), at 0.4
    high**.
  - **Swirl.** A swirl of spelu1 squiggles stays round the body: ~1.6-2 wide, feet to
    ~1.6 high, throughout Act+0.5-3.0. That gives **ORBITAL r = 0.85 over 0.15-1.45
    high**.
  - **Peaks.** f10670 (Act+0.70) and f11639 (Act+0.64). Rowtop 1.68 / 2.34.
- **253 Whirling Wind (oracle_druid):** stars fill the crop from Act+0.2 to Act+5:
  - extent 3.8-4.1 (crop-limited);
  - centroid 1.2;
  - 60 % of the lit pixels above one caster height;
  - rowtop 2.56.

  That gives **a BOX field of +-2.3 x 0.1-2.6**. The curved ribbons are not reproduced
  (sec. 8).
- **458 Fire Spiral (oracle_leg):**
  - Balls burst up and out at f7711-7735 (Act+0.4-1.4).
  - From Act+0.6 the cloud holds extent 2.4-2.8, centroid 0.8-1.0 and rowtop 2.0-2.2.

  That gives **BURST 0.6/s horizontal, 1.0/s up, gravity -1.2**, plus **a spark field of
  +-0.8 at 0.9 high**.

All values above are in caster heights.

## 3. Scale calibration (lane 5, chase camera az 180 pitch 12 dist 32)

The JSON is in world units. The oracle was measured in caster heights.
`review/B1120/scripts/gen_config.py` writes every landing number in caster heights and
converts them with two constants:

- **H = 6.5 world units per caster height.**
  - Derivation: the projection `px = h * (screenH/2) / (d * tan(vfov/2))`, with hfov
    62.8° at 1280x800 and d ≈ 32, gives 1048 px per world unit. Divided into the
    caster's 235 px, that is H ≈ 7.2.
  - It was set to 6.5 from the B1119 disc. Its outermost RADIAL_EXPAND particle (4.5
    units/s, ~1.4 s) spans 1.97 caster heights.
  - Checked on `cal/run_v2`, 54. The ORBITAL swirl of radius 0.85 H, plus a sprite,
    should render 1.78 caster heights across. The video shows 430 px = 1.83 (3 %).
- **A = 3.6 world units: the entity anchor above the feet.**
  - The B1119 disc topped out at 0.8 caster heights with a maximum particle height of
    anchor + 1.56 units, which puts the anchor at ~0.55 H.
  - Checked on the v1/v2 strips: the column's base sits on the feet tick at Act+0.5.

`tests/test_spell_landing_volume.cpp` uses the same two constants.

## 4. The change

### `config/spell_effects.json`

- **Styles** (the record-0 shape, the commonest oracle landing; every spell without an
  override gets it):
  - `spellComplete` = **grounded fire column**:
    - BOX +-0.85 H spawn at the feet;
    - LINEAR, vertical speed uniform in 0.15-1.7 H/s;
    - no gravity;
    - 1800/s.
  - `impact` = **spark shell**:
    - SPHERE 0.3 H spawn at 0.9 H;
    - BURST 1.6 +-0.5 H/s horizontal and 0.5 +-0.7 H/s vertical;
    - drag 0.5;
    - 1100/s.
    - The dead `expandSpeed` key was removed; BURST never read it.
  - Both styles get `"triggerDelay": 0.33`.
- **Overrides**, all derived in sec. 2:
  - 41: dome (spellComplete) + pillar (impact).
  - 54/656: swirl + glints.
  - 458: burst + spark field.
  - 253: star field on both halves.
  - 93's population override keeps T422's ratio to the style (x1.1034); so does 41's
    (x0.8657).
  - 253's T422 trickle (7.4/9.3 per s) is replaced by 600/s each, which is capped by the
    pool (sec. 6).
  - **Sizes and lifetimes are untouched:** B1119's size keys are byte-identical, and the
    1.6-2.0 s particle lifetime and 0.6 s emitter window are as before.

### Code

- **`triggerDelay`** (`spell_effects_config.{h,cpp}`, `spell_emitter_presets.cpp`):
  - A new optional style key. It is parsed, merged per spell, and turns a landing
    preset's IMMEDIATE trigger into the sim's existing `SpellTrigger::DELAYED` arm.
  - The sim needed no change: the DELAYED arm was already there.
  - Only `spellComplete` and `impact` read it.
  - The value comes from the measurements. The oracle's onsets are Act+0.40-0.80 (B1118
    sec. 4). A volume emitter crosses the threshold 0.09-0.27 s after it starts
    (`cal/run_v1`). The gap is ~0.33 s.
- **Pool wiring** (`src/client/graphics/zone_load/renderer_zone_load_env.cpp`):
  - The worker sizes its pool once, from the first input that says
    `unifiedRendererInitialized`. That input is the zone-load one, which never set
    `poolSize`, so the struct default 1024 won.
  - T422/B375's configured 4096 was therefore inert in every live run: B1118, B1119 and
    `cal/run_v1` all logged size=1024.
  - Without this fix the volume is clipped. At the same config, `cal/run_v1` (pool 1024)
    read 93 at 1.88; `cal/run_v2` (pool 4096) read 2.46.

## 5. Before / after (gate: B1118 legs A/B/C on lane 5)

The gate binary was built Release on the ThinkPad from WIP commit 0f486390
(`review/B1120/fable_*/willeq.head`). That WIP commit was squashed into this commit.
Its `src/`, `include/`, `config/` and `tests/` trees are identical to this commit's
(`git diff 0f486390 <this commit> -- src include config tests` is empty); only docs
were added after it. The client ran from the checkout, so it used the
committed config. The full table is `review/B1120/gate_table.tsv`.

| spell | oracle cov | B1118 | B1119 | **B1120** | % of target | rowtop oracle / B1119 / **B1120** | extent oracle / B1119 / **B1120** |
|---|---|---|---|---|---|---|---|
| 93 | 5.85 | 0.160 | 0.774 | **2.961** | 51 | 2.56 / 0.79 / **2.57** | 3.40 / 1.97 / **2.84** |
| 38 | 3.89 | 0.169 | 0.524 | **1.747** | 45 | 2.46 / 0.79 / **2.57** | 2.81 / 1.86 / **2.96** |
| 54 | 2.46 | 0.117 | 0.633 | **1.548** | 63 | 1.68 / 0.80 / **1.55** | 3.94 / 1.83 / **3.57** |
| 656 | 2.49 | 0.116 | 0.507 | **1.267** | 51 | 2.34 / 0.79 / **1.53** | 4.13 / 1.78 / **3.15** |
| 41 | 4.15 | 0.128 | 1.115 | **3.313** | 79.8 | 2.56 / 0.80 / **2.57** | 3.86 / 2.14 / **4.17** |
| 253 | 7.28 | <= 0.033 | <= 0.063 | **1.582** | 22 | 2.56 / 0.91 / **2.57** | 3.99 / 1.58 / **4.20** |
| 458 | 1.24 | 0.258 | 0.914 | **1.556** | 125 | 2.17 / 0.81 / **1.76** | 2.60 / 1.95 / **2.30** |

How to read the table:
- **Units.** Coverage is caster heights²; rowtop and extent are caster heights.
- **Oracle values are lower bounds.** The oracle's crop is 4.28 x 2.57 caster heights;
  fable's is 4.5 x 2.77. A rowtop of 2.56-2.57 means the crop top.
- **Strips.** `review/B1120/strips/strip_<id>.png`: the oracle over B1119 over B1120, in
  the same caster-height box at Act+0.5/1/1.5/2/3 s.

**Calibration path** (`review/B1120/cal/`, one wizard leg with all seven casts per run):

| run | config | % of target |
|---|---|---|
| v1 | first shapes, pool 1024 | 93 32, 41 62, 253 11 |
| v2 | pool 4096 + delay | 93 42, 41 79, 54 54 |
| v3 | wider column, denser swirl | 93 51, 41 80.4, 54 63 |
| gate | v3 with 253 capped | see the table above |

## 6. Why five spells stop short (named, not tuned away)

1. **The pool.**
   - `TheConfiguredPoolHoldsTwoOverlappingLandings` (B375) bounds one landing at 2048
     sprites. The record-0 landing is 1920.
   - 253's field (2 x 600/s x 1.8 s = 2160 alive) plus one record-0 landing fills the
     4096.
   - Coverage still rises with population. 93 went 1.88 -> 2.46 -> 2.96 as the
     population and the spread rose.
2. **Sprite size is B1119's.**
   - The oracle's 93/38 column and 253's ribbons are drawn with art far larger than the
     separable sparks that B1119 sized from.
   - On the shared ROI instrument (sec. 7), the oracle's 253 and 458 Landing sprites are
     1.6x and 1.4x fable's: 0.093 / 0.101 against 0.057 / 0.078.
   - With a fixed per-sprite area, 253's 7.28 caster heights² needs about 3x the pool.
3. **M8 fragments.** A third of record 0's `spellComplete` sprites are spelc4: a 64x64
   single image cut into 16 fragments, most of them dim. They contribute little lit
   area. That is B1124.

## 7. Non-regression

- **Sprite size.**
  - The size keys are byte-identical to B1119's, and `B1119*` tests pin them.
  - The ROI reading (`fx_sprite_roi.py`, Landing Act+1.2-1.8) **rose**:

    | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
    |---|---|---|---|---|---|---|---|
    | B1120 | 0.122 | 0.093 | 0.088 | 0.079 | 0.203 | 0.057 | 0.078 |
    | B1119 | 0.093 | 0.073 | 0.055 | 0.051 | 0.099 | 0.052 | 0.074 |

  - The cause is density, not size. At this population the round-blob filter keeps
    merged pairs and triples. 41's dome reads 0.203, against 0.178-0.200 on every
    calibration run. **This reading no longer measures a single sprite at the B1120
    density.** It needs a thinned calibration leg, as B1119's v3/v4 had.
  - Source ROI is unchanged: 0.032-0.206 against B1119's 0.032-0.208. The Source stage
    was not touched.
- **Oracle ROI p90** (B1119 FOLLOW-UP 2): the same tool on the B1118 oracle videos.
  - The ROI is the same caster-height box in the oracle crop: 199,781,265,540 at 210 px.
  - The PNGs were regenerated by `scripts/oracle_regen.sh` into
    `review/B1120/measure_oracle/`; the output is `measure_oracle/roi_oracle.txt`.
  - Source is pooled over BC+0.3..Act-0.05; Landing over Act+1.2..1.8.

  | spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
  |---|---|---|---|---|---|---|---|
  | oracle ROI p90, Landing | 0.075 | 0.069 | 0.044 | 0.035 | 0.101 | 0.093 | 0.101 |
  | oracle ROI p90, Source | 0.084 | 0.074 | 0.027 | 0.028 | 0.113 | 0.178 | 0.043 |
  | B1118 whole-frame p90, Landing | 0.074 | 0.058 | 0.064 | 0.056 | 0.110 | 0.059 | 0.067 |

  On the shared instrument, B1119's landing sizes sit as follows:

  | spell | B1119 ROI | oracle ROI | position |
  |---|---|---|---|
  | 41 | 0.099 | 0.101 | in band |
  | 38 | 0.073 | 0.069 | in band |
  | 93 | 0.093 | 0.075 | over |
  | 54 | 0.055 | 0.044 | over |
  | 656 | 0.051 | 0.035 | over |
  | 253 | 0.052 | 0.093 | under |
  | 458 | 0.074 | 0.101 | under |

- **Onset (Act+, s).** Six of the seven land inside the oracle's 0.40-0.80 band; 656
  falls 0.013 s short. Against B1119, five moved closer to the oracle and two moved away:

  | spell | oracle | B1119 | **B1120** | vs B1119 |
  |---|---|---|---|---|
  | 93 | 0.547 | 0.545 | **0.426** | away (still in band) |
  | 38 | 0.530 | 0.606 | **0.491** | closer |
  | 54 | 0.423 | 0.340 | **0.406** | closer |
  | 656 | 0.404 | 0.366 | **0.391** | closer |
  | 41 | 0.424 | 0.359 | **0.417** | closer |
  | 253 | 0.802 | none above threshold | **0.466** | newly measurable |
  | 458 | 0.422 | 0.254 | **0.412** | closer |

  93 moved away from the oracle (0.545 -> 0.426). It is still inside the band.
- **Hue (Landing, fx_blobs).**
  - **Toward the oracle:**
    - 41: 64 -> 71 (oracle 72).
    - 54/656: 213 -> 220, sat 0.24-0.27 -> 0.30 (oracle 243 / 0.47).
  - **Away from the oracle, small:**
    - 93: 39 -> 35 (oracle 46).
    - 38: 36 -> 33 (oracle 47).
    - 253: 208 -> 203 (oracle 213).
  - **458:** still white, sat 0.03 (M6).

  A colour is its sprite (T441). The shift is in the mix of sprites: more spelc2/spelc4
  in the column, more stars in the field. The sprite family is M7/M8's.
- **Frame time.**
  - Every `FRAME TIMING BREAKDOWN` line in all three legs (127 + 80 + 61) counts 60
    frames per 1.0 s window: 16.7 ms vsync.
  - The window-mean CPU frame is <= 1.26 ms. The worst single frame is 6.0 ms
    (`[entity-build] frame_ms max`).
  - There are 0 `slow!` warnings.
- **Capture.** x11grab ran at 30.00 fps with a max gap of 34 ms and 0/59 stale grabs in
  every landing window. `--dump-frames-dir` was not used.
- **Memory.** The pool wiring allocates the configured 4096 x 120 B (`sizeof(
  UnifiedParticle)`, measured) = 491,520 B instead of 122,880 B. That is **+368,640 B**
  on every host, plus the per-frame render-buffer copy, which grows with the live count:
  up to 491,520 B. This is Will's memory-first rule's business. `global.maxParticles` is
  the knob (FOLLOW-UP 1).
- **Pi fill (estimate, not measured).**
  - **Chase camera, 1280x800 (clip.w ≈ 32).** A record-0 landing peaks at 1920 sprites
    of mean ~12.5 px. That is ≈ 0.24 M px blended, against B1119's ≈ 0.08 M. At 800x600
    it is ≈ 0.13 M px, 0.28 of a screen.
  - **253's field.** 2160 sprites, ≈ 0.27 M px at 1280x800.
  - **First person (clip.w ≈ 5).** Sprites grow ~6.4x, to ~80 px. That is ≈ 10 M px per
    landing, ~20 screens at 800x600.
  - That is a real Mali-400 cost and must be measured on lane 1 (B1119 FOLLOW-UP 5).

## 8. Tests (same commit)

- **New: `tests/test_spell_landing_volume.cpp`** (`test_spell_landing_volume`, 10 tests).
  - **What it drives.** The real `SimulationWorker` with the shipped presets, under the
    T395 one-frame position protocol. It asserts in caster heights.
  - **Shape tests:**
    - `TheStyleLandingIsAColumnAtLeastTwoCasterHeightsTall`
    - `BurstOfFlameAndLightningBoltDrawTheColumn`
    - `TheStyleImpactIsASparkShellWiderThanTheColumn`
    - `WeakenIsAPillarAndASettlingDome`
    - `FrostBoltIsASwirlAndFlatGlints`
    - `WhirlingWindFillsTheFrame`
    - `FireSpiralIsABurstThatFallsBack`
  - **Onset tests:** `TheLandingWaitsForTheOraclesOnset`, `AStyleWithoutADelayStaysImmediate`.
  - **Pool test:** `SpellParticlePoolWiring.EveryInitialisedInputCarriesTheConfiguredPool`,
    a source census.
  - **RED on the B1119 config.** Eight of the ten fail on it; the pool census fails on
    the pre-fix TU.
- **Re-banded in `test_spell_burst_oracle_match.cpp`:**
  - `OneLandingReachesTheOraclesPeakSpriteCount` now runs from >= 715 (T422's blob
    count, a lower bound) to <= half the pool.
  - `SpriteSizeAndExpandSpeedAreTheMeasuredOnes` checks that the impact is a BURST at
    1.6 H/s. The T422 EXTENT text is marked superseded.
- **Re-banded in `test_spell_colour_and_overrides.cpp`:**
  - 93/41 populations are pinned as T422's RATIO to the style.
  - `WhirlingWindIsASteadyFrameFillingField` replaces the 25-36 trickle; it checks the
    field plus one landing against the pool.
  - The unmeasured-spell list is now 285/1/2. 656 has a measured shape now.
- **Delay-aware updates:**
  - `test_spell_impact_visual.cpp`: the two origin tests run through the first spawn
    frame past the delay, with a distance bound of 8 units for the volume.
  - `test_b1106_slow_frame_commands.cpp`: the zero-delta test pumps frames through the
    delay.
  - `test_data_paths.cpp`: the shipped impact style rate is now 1100.
- **Local run** (Release tree, named targets), all PASS:
  - test_spell_landing_volume
  - test_spell_burst_oracle_match
  - test_spell_colour_and_overrides
  - test_spell_impact_visual
  - test_caster_own_landing
  - test_spell_effect_removal
  - test_b1106_slow_frame_commands
  - test_spell_visual_vocabulary
  - test_spell_target_stage_textures
  - test_config_key_reachability
  - test_data_paths
  - `ctest -L hygiene`: 45/48 pass. The three `*Reproducible` tests need
    `generate_textures` / pack / db tools that were not built; this card does not touch
    those assets.

## 9. FOLLOW-UP (for the orchestrator; workers do not cut cards)

1. **Pool size (Will's call, memory first).**
   - Reaching >= 80 % for 93/38/656/253 at B1119's sprite sizes needs roughly 2-3x the
     population, which means a pool of ~8-12 k (+0.5-1 MB).
   - The other option is the larger art the oracle uses for columns and ribbons (B1124
     atlas / M8, B1123 overrides).
   - The 4096 pool is now live and costs +368,640 B against what actually ran before.
2. **ROI sprite-size reading at B1120 density.** `fx_sprite_roi.py` merges blobs at
   these populations (sec. 7). Re-read sprite size on a thinned leg, as B1119's v3/v4 did.
3. **Oracle-vs-fable size on one instrument** (the sec. 7 table). B1119's landing sizes
   are over for 93/54/656 and under for 253/458. That is B1119/B1123's to re-tune.
4. **253 ribbons.** The oracle's long curved ribbons need a phase-locked orbital emitter:
   a particle's phase must be the emitter's age times ω, not random. Coverage also lacks
   ~3x population (item 1).
5. **93/38 white flash** at Act+0.5-0.9 (f7537-7549) is M8's (spelc3 drawn whole).
6. **Onset.** 93 moved from 0.545 to 0.426 (still in the oracle band). A per-record
   delay would match 93/38 (0.53-0.55) and 253 (0.80) individually. That belongs with
   B1121 (timing).
7. **Pi fill cost** (sec. 7 estimate) on lane 1, chase and first person.
8. **Built-in fallback styles** (`SpellEffectsConfig::setDefaults`) still describe the
   pre-T422 one-shot disc (B1119 FOLLOW-UP 4).
