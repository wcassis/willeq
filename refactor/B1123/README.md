# B1123 (#443): spell FX overrides and population from the official client (B1118 M5)

Fifth fix card of the B1118 chain (B1119 size -> B1120 volume -> B1121 duration ->
B1122 source -> **B1123 overrides** -> B1124 atlas). Scope: the per-spell `spellOverrides`
populations and the landing coverage residue. Evidence: `/mnt/projects/fable/review/B1123/`
(index: its `README.md`).

## 0. Result

| card row (caster heights^2) | target | oracle | B1122 | **B1123 gate** | met? | bound (sec. 5) |
|---|---|---|---|---|---|---|
| 253 Source | >= 3.1 | 3.884 | 3.189 | **3.235** | yes (already at B1122) | - |
| 41 Landing | >= 3.3 | 4.154 | 3.336 (3.246 on the cal leg) | **3.684** | yes | - |
| 93 Landing | >= 4.7 | 5.847 | 2.877 | **2.833** | **no** (60 %) | pool, all of it |
| 253 Landing | >= 5.8 | 7.281 | 1.496 | **1.862** | **no** (32 %) | pool + size, none of it brightness |

- **Two of the four rows are met and two are not.** The two misses are bound by the 4096
  pool, and 253 also by the B1119 size band. A lane-5 population sweep measured the pool
  each target needs (sec. 5): **93 4.7 needs 8.0 k, 253 5.8 needs 11.5 k at the shipped
  sizes** (6.4 k at the oracle's ROI sprite size). The pool stays 4096 (Will's #440 call).
- **The T422 population ratios are gone.** 93's override is deleted (record 0's style
  now carries the pool bound). 41's x0.8657 is replaced by the same bound. 253's
  landing sprites are raised x1.12, to the top of B1119's band.
- **Non-regression holds.** Every B1119-B1122 row is inside its band or its
  capture-to-capture spread (sec. 4).
- **38's -3.7 % (B1121/B1122) is not the sustain.** A same-binary A/B shows no
  difference, and the spread between two identical configs is 3.8 % (sec. 4c).

## 1. What each override did against the styles (before this card)

| spell (record) | override keys | source | status now |
|---|---|---|---|
| 93 (0) | impact 1214.6/s, spellComplete 1986/s = style x1.1034 | **T422 blob-count ratio** (715:648, llvmpipe) | **deleted**; the style takes these rates as the pool bound |
| 38 (0) | landing sizes x0.74 of the style | B1119 (oracle p90 0.058 vs 93's 0.074) | kept |
| 41 (20) | dome + pillar shape (B1120), sizes x1.51 (B1119), sustain (B1121), castSpray cloud (B1122); rates 953.3/1557 = style x0.8657 | shape/size/duration/Source from the oracle; **rates = T422 ratio** | rates replaced by the pool bound (x1.232) |
| 54 / 656 (23) | swirl + glints (B1120), sizes (B1119, 54 only), sustain (B1121), orbital Source (B1122) | oracle | kept |
| 458 (33) | burst + spark field (B1120), sustain (B1121), Source column (B1122) | oracle | kept (landing 123 % of the oracle: not this card's) |
| 253 (35) | frame-filling BOX field 600/s each half (pool cap, B1120), 4.35 s (B1121), Source box 1950/s (B1122) | oracle shape, pool-capped rates | kept; **+ landing sizes = style x1.12** |

Why the 93 override is not needed: 93 and 38 are one spells.eff record (Source slots
spelc2/spelc4/spelc2, Target spelc3/spelc2/spelg1) and draw the same picture on the
oracle (B1120 sec. 2). The oracle's 93:38 coverage ratio, 1.50, matches their B1118 p90
sprite-area ratio, (0.074/0.058)^2 = 1.63. That difference is B1119's 38 size override,
not a population difference. Nothing measured gives 93 more sprites than 38.

## 2. The change (`config/spell_effects.json` only; no code)

Generator: `review/B1123/scripts/b1123_final.py` (from the B1122 config), formatted by
`json_fmt.py`.

- **The pool bound.** The largest live peak one fed landing may have is the pool minus
  one 253 field: 4096 - 2 x 600/s x 1.8 s = **1936**. This is the B1121 rule ("253 field
  + one record-0 landing <= 4096"), and B375's two-landing rule follows from it.
  - **Why the bound is the target.** The B1123 sweep (sec. 5) shows that every one of
    these oracle landings needs MORE than 1936 live. So "the oracle's population" and
    "the pool bound" give the same number until the pool grows.
- **Record-0 style** spellComplete 1800 -> **1986.0/s**, impact 1100 -> **1214.6/s**. This
  is 93's previous population, and its expected peak, 1927 live, sits 9 under the bound.
  93 is byte-identical in effect. 38 (the same record) gains +10.4 %, as does every
  unmeasured spell.
- **41:** spawnRate and sustainRate x1.232. Peak live goes 1562 -> **1925**. The dome and
  pillar shape, the sizes and the 4.15 s are unchanged. 41 then reads 3.68-3.74 against
  the oracle's 4.15: still under it, so the bound, not the oracle, sets the number.
- **253 landing sizes** = style x **1.12** (0.251-0.392 -> 0.493-0.737 impact, 0.298-0.493 ->
  0.596-0.878 spellComplete).
  - On the ROI instrument the oracle's 253 Landing sprite is 0.093 (B1120
    `measure_oracle/roi_oracle.txt`); fable's style read 0.054-0.057.
  - B1119's band for this row is B1118's whole-frame p90, 0.059 +-15 % = **<= 0.068**,
    and the card keeps B1119's bands satisfied.
  - The raise was calibrated on lane 5: x1.2 read 0.072 (`cal/run_c1`, out of band);
    x1.12 reads 0.065-0.068 on four legs (gate 0.067).
- **Untouched:** every shape, delay, duration, lifetime, Source key, and the pool (4096).

## 3. Gate (B1118 legs A/B/C, ThinkPad lane 5)

- **Build.** ThinkPad `~/src/willeq`, branch `b1123-fx-overrides`, WIP 320de3f8 = fable
  66435f90 plus this config (`review/B1123/fable_*/willeq.head`). The binary is B1122's
  Release build. `rsync -c` showed `src/ include/ tests/ scripts/ CMakeLists.txt` were
  identical to this worktree, and the card changes no code.
- **Capture.** x11grab 30.00 fps; max frame gap 34 ms; 0/59 stale grabs in every landing
  window (all 7). `--dump-frames-dir` was not used.
- **Frame time.**
  - From the first cast: every window has 60 frames (72 + 33 + 14 windows).
  - Window-mean Total Frame 0.77-1.22 ms; worst frame 2.2 ms; 0 `slow!`.
  - Leg A's 50.9 ms frame is the first window after zone-in, before any cast (as at
    B1122).
- **Pool.** `pool initialized (size=4096)` on every gate leg.
- **Window.** The legs ran 09:39-09:47 UTC. Calibration ran 08:36-10:18 UTC. apt-daily
  runs at 20:56 and apt-daily-upgrade at 06:58, so the legs were clear of both. Xorg was
  not restarted.
- **The 253 scene-brightness trap did not recur** in 15 legs (gate B plus 14 calibration
  legs; 253 tail lit <= 0.020 caster heights^2 from Act+7 to +10, against 1.64 / 1.85 in
  B1122's two; `review/B1123/measure_253_tail.txt`).
  - **What it was** (B1122 `fable_R` and `fable_B_scene_change`, measured here): a
    single-frame darkening of the SKY only. The mean change is 30 levels in rows
    0-200, 1 level in rows 200-500, and 0 below. It lands at 31.2 s after the
    `#set time 11 0` OP_TimeOfDay in **both** legs.
  - **What it is not.** The zone-in time-of-day (21:49 and 13:00 changed; 13:15, 10:44
    and 17:44 did not) and the weather (clear in all) do not separate the legs.
  - **Cause:** unexplained; it is a sky/time-of-day event, not an effect (FOLLOW-UP 5).
- **Tables:** `review/B1123/gate_table.tsv`, `gap_table.tsv`, `pool_fit.tsv`, `ab38.tsv`.
  **Strips:** `review/B1123/strips/strip_<id>.png`, ORACLE / before (fable tip, `cal/run_base`)
  / B1123 gate, at Act+0.5/1/2/3/4 s.

## 4. Before / after (caster heights)

| spell | landing cov B1122 -> **B1123** (oracle) | end B1122 -> B1123 (oracle) | rowtop | ROI p90 landing B1122 -> B1123 (B1119 band / oracle ROI) | hue |
|---|---|---|---|---|---|
| 93 | 2.877 -> **2.833** (5.847) | 4.924 -> 4.922 (4.867) | 2.57 = | 0.111 -> 0.115 (0.063-0.085 / 0.075) | 36 = |
| 38 | 1.706 -> **1.901** (3.889) | 4.844 -> 4.836 (4.850) | 2.57 = | 0.092 -> 0.093 (0.049-0.067 / 0.069) | 33 = |
| 54 | 1.569 -> 1.574 (2.461) | 5.159 -> 5.165 (5.103) | 1.54 -> 1.53 | 0.083 -> 0.087 (0.054-0.074 / 0.044) | 220 = |
| 656 | 1.256 -> 1.288 (2.493) | 5.174 -> 5.118 (5.004) | 1.53 -> 1.54 | 0.074 -> 0.073 (0.048-0.064 / 0.035) | 220 = |
| 41 | 3.336 -> **3.684** (4.154) | 6.332 -> 6.352 (6.224) | 2.57 = | 0.186 -> 0.163 (0.094-0.127 / 0.101) | 71 -> 70 |
| 253 | 1.496 -> **1.862** (7.281) | 6.493 -> 6.477 (6.522) | 2.57 = | 0.056 -> **0.067** (0.050-0.068 / 0.093) | 203 -> 202 |
| 458 | 1.548 -> 1.530 (1.242) | 5.010 -> 5.003 (4.942) | 1.78 -> 1.83 | 0.073 -> 0.072 (0.057-0.077 / 0.101) | white = |

- **Ends** are all within +0.13 s of the oracle (band +-0.4).
- **ROI p90.** The two rows inside B1119's band at B1122 (253, 458) are still inside it;
  253 is at the band top, by design. The rows that left the band at B1120 (merged blobs
  at the B1120 density) moved by -0.023..+0.004.
- **Landing onset.** `landing_onset_abs.py`'s ABS reads +0.02..+0.36 on the B1123 gate
  and +0.02..+0.39 on B1122's `fable_R`. Both are held-Source captures, where ABS returns
  the held cloud's minimum, not the landing (B1122 sec. 6b), so it is unchanged. The
  onset is pinned in code (`triggerDelay` 0.33, untouched;
  `TheLandingWaitsForTheOraclesOnset` passes).

### 4a. Source (B1122's rows; Source config byte-identical)

| spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
|---|---|---|---|---|---|---|---|
| coverage oracle / B1122 / **B1123** | 0.364 / 0.402 / **0.394** | 0.361 / 0.363 / **0.405** | 0.461 / 0.519 / **0.533** | 0.615 / 0.597 / **0.596** | 1.209 / 1.082 / **1.085** | 3.884 / 3.189 / **3.235** | 0.058 / 0.067 / **0.062** |
| extent oracle / B1122 / **B1123** | 2.92 / 2.62 / **2.18** | 2.24 / 2.31 / 2.32 | 1.60 / 1.52 / 1.52 | 1.45 / 1.52 / 1.51 | 2.68 / 2.43 / 2.71 | 3.70 / 3.98 / 3.89 | 0.75 / 0.77 / 0.74 |
| centroid oracle / B1122 / **B1123** | 0.89 / 0.87 / 0.80 | 0.95 / 0.91 / 0.94 | 0.82 / 0.71 / 0.70 | 0.61 / 0.69 / 0.69 | 0.87 / 0.92 / 0.94 | 1.11 / 1.07 / 1.04 | 0.53 / 0.64 / 0.58 |
| ROI p90 Source B1122 / B1123 | 0.127 / 0.129 | 0.131 / 0.132 | 0.063 / 0.065 | 0.066 / 0.063 | 0.173 / 0.177 | 0.176 / 0.173 | 0.053 / 0.051 |

- **93's Source extent** read 2.18, the low end of the same-config spread. The six
  calibration legs with this Source read 2.18-2.60; B1122's gate read 2.62. Nothing in
  the Source changed.
- **Source hue** is unchanged (`gate_table.tsv`).

### 4b. Act-0.1..+0.5 s hold (`gap_table.tsv`)

The B1122 hold is intact. 93 reads 0.40/0.41/0.40/0.38/0.38 over Act-0.1..+0.4 (oracle
0.41/0.43/0.41/0.42/0.35); 41, 54, 656 and 458 match B1122's `fable_R` within 0.05.
- **253** holds a flat 3.65 (its Source peak is 3.2-3.3).
- **Why the B1122 row is higher.** B1122's `fable_R` read 4.7 for 253. That leg's tail
  baseline was offset by the sky step (sec. 3), so its row reads high.

### 4c. 38's landing (the B1121 / B1122 "-3.7 %")

`ab38.tsv`: the same binary and the same day, cal tour.

| 38's landing config | legs | coverage |
|---|---|---|
| B1123 (with the B1121 sustain from 1.6 s) | gate, cal c1, cal f1 | 1.901, 1.836, 1.878 (mean 1.872) |
| B1123 without the sustain (B1120's 0.6 s landing, override only for this A/B) | n38a, n38b | 1.895, 1.827 (mean 1.861) |
| identical 38 config at pop x2, two legs | p2, ps2 | 2.834, 2.942 (3.8 % apart) |

- **The sustain does not lower 38's peak.** The two configs agree within 0.6 %.
- **The B1121 "drop" is the size of the capture spread.** Two identical configs read
  3.8 % apart. With n = 3-4 per side, -3.7 % is noise.
- **Not changed:** B1121's suggested move of `sustainFrom` to 1.8 s has no measured
  reason, so it was not made.
- **B1122 -> B1123:** 38's coverage rose +11 % (1.706 -> 1.901), from its +10.4 %
  population.

## 5. What bounds the misses (measured decomposition)

**The sweep** (`review/B1123/cal/`, 7-spell wizard leg, `scripts/b1123_gen.py`):
- every landing's `spawnRate` and `sustainRate` was multiplied by N, with the pool raised
  to 16384 or 32768 **in the calibration config only**;
- three size sets were swept: B1122's sizes, the shipped sizes, and the oracle's ROI
  sizes (253 x1.63, 93 x0.68: the ROI ratios oracle/fable);
- `scripts/pool_fit.py` interpolates in log2(N) (`pool_fit.tsv`). A `>` marks an
  extrapolation past the last measured N.

Coverage by N (caster heights^2):

| spell, sizes | N=1 | N=2 | N=4 | N=8 |
|---|---|---|---|---|
| 93, B1122 = shipped sizes | 2.95 (cal) / 2.83 (gate) | 4.47 / 4.63 | 6.14 / 5.98 | 7.59 / 7.47 |
| 93, oracle ROI size (x0.68) | 1.54 | 2.80 | 4.28 | - |
| 41, B1122 rates | 3.25 | 4.66 | 5.77 | 6.32 |
| 41, shipped (x1.232 = N 1.232 of B1122) | 3.68 | 4.95 | 5.99 | 6.37 |
| 253, B1122 sizes | 1.50 | 2.73 | 4.64 | 6.92 |
| 253, shipped sizes (x1.12) | 1.86 | 3.38 | 5.46 | 7.74 |
| 253, oracle ROI size (x1.63) | 3.49 | 5.67 | 7.89 | - |
| 253 Source (N = 1, 1.3, 1.6) | 3.34 | 3.92 (N 1.3) | 4.49 (N 1.6) | - |

Live sprites and the pool each target needs:

| target | sizes | N | live per landing | pool (rule) |
|---|---|---|---|---|
| 93 >= 4.7 | shipped | 2.07 | 4.0 k | **8.0 k** (two fed landings) |
| 93 = oracle 5.85 | shipped | 3.74 | 7.2 k | 14.4 k |
| 41 >= 3.3 | shipped | met | 1.9 k | 4096 (shipped) |
| 41 = oracle 4.15 | shipped | 1.29 | 2.5 k | 5.0 k |
| 253 >= 5.8 | shipped (x1.12) | 4.44 | 9.6 k | **11.5 k** (field + a record-0 landing) |
| 253 >= 5.8 | oracle ROI (x1.63) | 2.08 | 4.5 k | 6.4 k |
| 253 = oracle 7.28 | shipped | 6.97 | 15.0 k | 17.0 k |
| 253 Source >= 3.1 | shipped | met | 1.9 k | 4096 |
| 253 Source = oracle 3.88 | shipped | 1.28 | 2.5 k | 4.4 k |

B1121's "~8 k for the 93/38 plateau" agrees with 93's 8.0 k.

**Decomposition per miss.** Each miss is split into three possible causes:
- **(a) pool:** the population.
- **(b) size:** the per-sprite size against the oracle's art, on the ROI instrument
  (`roi_oracle.txt`).
- **(c) brightness and fragmentation:** B1124's. This is the atlas lit-area census,
  `atlas_lit_census.tsv` (`scripts/atlas_lit_census.py`): the fraction of each frame's
  cell whose additive RGB exceeds the mask's 80, averaged over the 16 frames, and for
  spelc3 / spelc4 also packed as one whole image.

- **93 Landing (gate 2.83, target 4.7): bound entirely by the pool.**
  - **(a) Pool.** It closes the whole gap at N 2.07 (8.0 k pool).
  - **(b) Size is a credit, not a bound.** Fable's record-0 sprite reads 0.115 on ROI
    against the oracle's 0.075. At the oracle's size, 93 falls to 1.54 and would need a
    pool of more than 18.8 k.
  - **(c) Brightness and fragments: about +1 %.** spelc3 and spelc4 as 16 fragments
    average lit 0.154-0.155 of the cell. Packed whole they would be 0.168-0.170. Each
    is 1/3 of a half's sprites, so the record-0 landing's isolated lit area would rise
    by about 1 % (rate-weighted). spelg1 (lit 0.12, impact slot 3) is the oracle's own
    art. B1124 is a look fix for 93 (the Act+0.5 flash), not a coverage lever.
- **253 Landing (gate 1.86, target 5.8): pool, then size; no brightness bound.**
  - **(a) Pool alone, at the shipped sizes,** closes it at N 4.44 (11.5 k).
  - **(b) Size.** The step from the shipped x1.12 to the oracle's ROI-matched x1.63 is
    worth +1.6 at N=1 (1.86 -> 3.49). That is 41 % of the gap; the remaining 59 % is the
    pool (N 2.08, 6.4 k).
    - **Linear law vs the ROI reading.** x1.63 is the linear-law size. On the ROI
      instrument it reads 0.130, above the oracle's 0.093, because of merged blobs at
      density. A ROI-exact size would sit between x1.12 and x1.63 (about x1.3 by
      interpolation).
    - **Why it is not shipped.** B1119's band stops it at x1.12 (sec. 2).
  - **(c) Brightness: 0.** spela2 is a real 16-frame sheet; every frame is lit over
    0.41-0.43 of the cell. No M8 fragment is involved.
  - **Not reproduced: the oracle's ribbons** (B1120 FOLLOW-UP 4). The oracle draws long
    curved bands of larger, brighter stars (`strips/strip_253.png`); fable draws a
    uniform field.
- **253 Source (met, 3.235):** 83 % of the oracle's 3.884; reaching 100 % needs 2.5 k
  live, a 4.4 k pool.
- **41 Landing (met, 3.684):** 89 % of the oracle; 100 % needs 2.5 k live, a 5.0 k pool.

**Memory if raised** (B1120 sec. 7: 120 B per sprite, plus the same again for the
render-buffer copy at full occupancy):

| pool | sprites | pool bytes | + render copy | vs 4096 |
|---|---|---|---|---|
| 4096 (shipped) | 4096 | 0.49 MB | 0.49 MB | - |
| 8 k | 8192 | 0.98 MB | 0.98 MB | +0.98 MB |
| 11.5 k | 11.5 k | 1.38 MB | 1.38 MB | +1.78 MB |

That is Will's call (#440).

## 6. Pi note (estimate, not measured)

- **Live counts.** 93's and 253's live counts are unchanged.
- **Additive fill:**
  - 38 and every unmeasured spell: +10.4 % sprites;
  - 41: +23 % sprites;
  - 253's field: x1.25 per-sprite area, at 2160 sprites.
- **Bounds unchanged.** No landing exceeds the B1121/B1122 pool bounds. CPU per sprite is
  unchanged.
- **Lane-1 fill measurement:** still owed (B1120 FOLLOW-UP 7).

## 7. Tests (same commit)

- **`tests/test_spell_colour_and_overrides.cpp`.**
  - The header gains a B1123 paragraph that retires the T422 ratios.
  - `BurstOfFlameIsTheRecordZeroStyle` (new; replaces
    `BurstOfFlameReachesItsOwnMeasuredPopulation`): 93 == the record-0 style in rates,
    sustain, size and window, and 93 == 38 in population.
  - `EveryFedLandingTakesThePoolBoundBesideAField` (new; replaces
    `WeakenReachesItsOwnMeasuredPopulation` and `TheTwoWideBurstsAreNoLongerTheSameSize`):
    the style, 93 and 41 peak at 98-100 % of 4096 minus 253's field.
  - `WhirlingWindSpritesAreTheStyleRaisedToTheB1119BandTop` (new; replaces
    `WhirlingWindSpritesComeFromTheB1119Style`): 253's four landing size keys = style
    x1.12 +-0.005.
  - `AnOverrideOnlyMovesWhatItNames`: now uses 38 (sizes only), since 93 has no override.
  - `ThePoolStillHoldsTwoOfTheLargestLanding`: the largest over every measured fed landing.
- **`tests/test_data_paths.cpp`:** the shipped impact style rate pin is 1214.6 (it was
  1100), and a stale "93 carries 530" sentence is fixed.
- **RED on the B1122 config** (`review/B1123/red_b1122_config.txt`, with B1122's
  `config/spell_effects.json` swapped in): the three new tests and
  `CwdElsewhere.TheProductionStringLoadsTheShippedConfigFromAnyDirectory` fail. The
  other 10 `SpellStyleOverrides` tests pass.
- **Unchanged and passing:** `test_spell_burst_oracle_match` and
  `test_spell_landing_volume`. The real worker's `OneFedLandingFitsHalfThePool` and
  `ASourceAndItsOwnLandingFitHalfThePool` (41 now at ~1925 + its Source) stay under 2048.
- **Local run** (Release, named targets, -j8):
  - `ctest -R "Spell|Sim|Particle|Landing|DataPath|ConfigKey|B1106|CasterOwn"`: 440 tests,
    100 % pass, 32 skipped (they need EQ client files).
  - `ctest -L hygiene`: 45/48. The same three `*Reproducible` tests fail: their generator
    tools are not built.
  - `CwdElsewhere.*` is not in that filter. It fails in a build tree outside the repo,
    which is an environment limit. The two config tests pass from a repo-shaped root
    (binary copied beside `config` and `data` symlinks; `review/B1123/README.md`).
- **Docs.** CLAUDE.md "The spell sprite atlas": "T422 population ratios" is struck; it
  now says every population is the pool's bound (B1123). It stays at 59,986 bytes (one
  narrative sentence was tightened). `check_doc_counts.py`: OK.

## 8. FOLLOW-UP (for the orchestrator; workers do not cut cards)

1. **Pool size (Will, #440).** 8.0 k reaches 93's card target and 11.5 k reaches 253's at
   the shipped sizes (sec. 5 table, +0.98 / +1.78 MB).
2. **B1119's 253 Landing band.**
   - The band is B1118's whole-frame p90 (0.059). The oracle on the shared ROI
     instrument reads 0.093.
   - Re-basing the band on the oracle ROI would allow ~x1.3. At the linear-law x1.63,
     253's 5.8 needs only a 6.4 k pool.
   - Decision owed: which instrument the size bands use (B1119 FOLLOW-UP 1/2).
3. **253 ribbons** (B1120 FOLLOW-UP 4): a phase-locked orbital emitter. This is the
   part of 253's shape that no population or size reproduces.
4. **B1124 (M8)** is a look fix for record 0 (the Act+0.5 flash), not a coverage lever:
   +1 % lit area by the census.
5. **The B1122 sky step.** A sky-only darkening 31.2 s after `#set time` in 2 of B1122's
   legs. It did not recur in 15 B1123 legs. A tour that waits >= 35 s after `#set time` before
   the first cast would avoid it. The sky/time-of-day cause is unexplained.
6. **458's landing** reads 123 % of the oracle on gate leg A and 69 % on the cal tour
   (0.85-0.89): the tour changes it by 1.8x. Not touched.

## Completion leg (Will's rulings 2026-10-22: pool option (b); sizes on the ROI instrument)

Evidence: `/mnt/projects/fable/review/B1123/leg2/` (index: its `README.md`). Gate: ThinkPad lane 5,
2026-10-22 13:10-14:00 UTC, tree = this commit (synced, `build-gles2` Release), pack sha 8199a8a2
mapped on every leg (`pack.sha256`, `pack.mapped`), atlas identical to B1124's (`tree.sha256`).

### C0. Result

| card row (caster heights^2) | target | oracle | B1124 (pool 4096) | **completion gate (pool 8192)** | met? |
|---|---|---|---|---|---|
| 253 Source | >= 3.1 | 3.884 | 3.271 | **3.375** | yes |
| 41 Landing | >= 3.3 | 4.154 | 3.700 | **4.062** (98 % of the oracle) | yes |
| 93 Landing | >= 4.7 | 5.847 | 2.921 | **4.762** | yes (cal tour 4.78 / 4.69) |
| 253 Landing | >= 5.8 | 7.281 | 1.865 | **4.032** | **no** (69 %): the ROI size band caps the sprite, the pool caps the count (C4) |

- **Three of four met.** 253 Landing more than doubled but misses: at 8192 the field takes half the pool
  (4096 live) and its sprite sits at the ROI band top (C4).
- **The pool is one preset key per platform** and every landing population is derived from it by one rule
  (C1, C2). Nothing in code, config or tests carries a second copy of 8192 or 4096.
- **Non-regression holds** except where noted in C5 (38 ROI reads 0.077 / 0.087 on two identical-config legs).

### C1. The pool: one preset key, per platform (Will 2026-10-22, PROVISIONAL until B1130 #450)

- `config/constrained_presets.json`, orangepi preset: `"spellParticlePool": {"sbc": 4096, "desktop": 8192}`.
  - **Platform = the build:** `sbc` on an `EQT_DRM` build (Orange Pi and the other SBC lanes, XU4 included),
    `desktop` otherwise (x86, macOS). `ConstrainedRendererConfig::spellParticlePlatformKey()`.
  - **Why not "the preset"?** Every build runs the orangepi preset unless told otherwise
    (`application.cpp` defaults to it, and `max`/`none` map to it), so a preset value alone would have given x86 4096.
  - A preset that does not name the key (`128x32x10`, a memory spec) takes the orangepi preset's.
- `loadJsonOverrides()` reads it (via `readSpellParticlePool()`, the same reader the tests use) into
  `ConstrainedRendererConfig::spellParticlePool`; `renderer_init.cpp` hands it to
  `SpellEffectsConfig::applyPool()` right after the particle manager loads the spell config.
- **`spell_effects.json` no longer carries a pool.** `global.maxParticles` is gone; if a file still has it the
  loader logs a WARN and ignores it. `getGlobal().maxParticles` is now the pool IN EFFECT (written only by
  `applyPool`), and the two worker wiring sites (B1120) are unchanged.
- **Log lines (x86, every gate leg):** `Preset 'orangepi': spellParticlePool=8192 (platform 'desktop')`,
  `spell particle pool 8192 -> a landing and its held Source peak at <= 4096 (style landing 1927 -> 4079; ...)`,
  `SimulationWorker: Particle pool initialized (size=8192)`.
- **Orange Pi:** `sbc` = 4096, unchanged; its populations come from the same rule (C2 table).

### C2. Populations: ONE rule, derived from the pool

**Rule** (`SpellEffectsConfig::applyPopulationRule`, model in `include/client/graphics/environment/spell_population_model.h`):

> every landing (impact + spellComplete; the style and every spell that overrides either) is scaled so that
> **the landing plus its own held Source peaks at half the pool**. A landing the oracle fills with FEWER
> sprites is a hand-set exception (`landingPopulation.exceptions.<id>.peakLive`, with its `why`): its landing
> peaks at min(that, the rule).

- **Why half the pool.** It is B1121/B1123's rule restated per pool: two overlapping fed landings fit, and so
  does 253's field beside one. Every bound-limited landing measured needs more than half the pool
  (B1123 sweep: 93 7.2 k, 253 15 k), so for them the bound IS the target.
- **Why the held Source.** B1122 holds the Source past OP_Action; the real-worker test
  `ASourceAndItsOwnLandingFitHalfThePool` failed at 4096 for 54/656 (2341 > 2048) under a landing-only rule.
  The larger of castGlow / castSpray counts (which one a spell draws is decided at cast time).
- **The JSON rates are the SCHEDULE** (peak vs sustain, impact vs spellComplete); the rule scales spawnRate and
  sustainRate together. A re-sized pool re-flows every population with no config edit.
- **Why one rule, not two config sets.** B1123's 4096 set is the rule's result to within 0-9 % for the
  bound-limited landings; two sets would drift and B1130 re-sizes the pool again.

| landing | how set | desktop 8192 peak live | sbc 4096 peak live | B1123 (4096) |
|---|---|---|---|---|
| record 0 (93, 38, every unmeasured spell) | rule | 4079 | 2024 | 1927 |
| 253 field | rule | 4096 | 2048 | 2160 |
| 41 | exception 2492: B1123 sweep, oracle 4.154 at the shipped sizes | 2492 | 2048 (rule binds) | 1925 |
| 54, 656 (record 23) | exception 3517: 54 reads 2.459 vs the oracle's 2.461 (cal `m_k1`); 656 shares the record's population | 3517 | 1704 (rule binds) | 1556 |
| 458 | exception 373 (C3) | 373 | 373 | 622 |

`review/B1123/leg2/scripts/rule.py <config> <pool>` is the Python twin (prints this table for any pool).

**Orange Pi deltas** (not measured on a Pi): record-0 +5 %, 41 +6 %, 54/656 +9 %, 253 field -5 %, 458 -40 %
(but its sprites are x1.3, so its fill is ~x1.0). The 4096 pool and its bytes are unchanged.

### C3. Sprite sizes on the ROI instrument

**Instrument:** `fx_sprite_roi.py` p90 (Landing Act+1.2..1.8, Source BC+0.3..Act-0.05, pooled), against
`review/B1120/measure_oracle/roi_oracle.txt`, band +-15 %. Read on the GATE capture (the oracle was read on
its own natural-density capture, so this is the like-for-like reading).

| row | B1118 whole-frame band (retired) | ROI band | B1124 gate | completion gate | thinned (separable) | action |
|---|---|---|---|---|---|---|
| 93 L | 0.063-0.085 | 0.064-0.086 | 0.098 | 0.094 | 0.089 | held (over) |
| 38 L | 0.049-0.067 | 0.059-0.079 | 0.092 | 0.077 / 0.087 | 0.061 | none (in on leg 1, over on leg 2; same config) |
| 54 L | 0.054-0.074 | 0.037-0.051 | 0.086 | 0.074 | 0.078 | held (over) |
| 656 L | 0.048-0.064 | 0.030-0.040 | 0.075 | 0.063 | 0.061 | held (over) |
| 41 L | 0.094-0.127 | 0.086-0.116 | 0.155 | 0.156 | 0.110 | held (over at density, in band separable) |
| **253 L** | 0.050-0.068 | 0.079-0.107 | 0.066 | **0.107** | 0.060 | **style x1.12 -> x1.3** |
| **458 L** | 0.057-0.077 | 0.086-0.116 | 0.076 | **0.110** (0.079 at the style size) | 0.060 | **style -> x1.3**, population 622 -> 373 |
| Sources (93 38 54 656 41 253 458) | - | 0.084 0.074 0.027 0.028 0.113 0.178 0.043 | 0.128 0.131 0.068 0.063 0.178 0.170 0.051 | 0.135 0.130 0.066 0.066 0.181 0.173 0.051 | 0.035 0.065 0.047 0.043 0.110 0.094 0.039 | held (253 in band) |

- **253.** On the gate capture x1.3 reads 0.107 (band top) and x1.45 reads 0.130 (cal `m_k2`); B1123's
  linear-law x1.63 would read ~0.137. So **x1.3 is the largest size the ROI band admits**, and the
  B1123 prediction ("6.4 k at the oracle's ROI size") does not hold: it used the linear law, not the ROI reading.
- **458** read 0.079 at the style size (short). x1.3 reads 0.110 (in band). The population moved to 0.6 x
  B1123's on the cal tour's evidence (`m_k2`: 0.71 of the coverage). On the gate tour its coverage did not move
  (1.559 -> 1.572, 127 % of the oracle): 458's coverage is tour-sensitive (B1123 FOLLOW-UP 6).
- **Over-band rows are HELD, not shrunk** (93/54/656/41 Landing, five Sources). Shrinking them to the band
  costs landing coverage the card's coverage rows need: 93 at the ROI-matched size needs a pool > 18.8 k for
  4.7 (B1123 `m_s1`/`m_ps*`). The thinned leg (`cal/m_t1`, every population x0.1) shows part of it is merged
  blobs at density (41 L 0.156 -> 0.110, in band), part is real size (54/656 L stay ~1.8x over even separable).
  **This trade is Will's** (FOLLOW-UP 1).
- **The instrument depends on density.** 253 reads 0.060 separable vs 0.107 at the gate density. The band is
  judged at the gate density above; a separable reading would have put 253 at x1.55.

### C4. What bounds 253's landing now

- **Count:** the rule gives the field half the pool (4096 live, N 1.90 of B1123's 2160).
- **Size:** x1.3 is the ROI band top (C3).
- At these, 253 reads 4.03 (gate) / 4.07 (cal `m_k1`); 5.8 needs either ~1.8x more sprites at x1.3
  (B1123's sweep adds ~2.1 per doubling: a ~15 k pool under the half-pool rule) or sprites the ROI band does not admit.
- The oracle's ribbons (B1120 FOLLOW-UP 4) remain unreproduced (`strips/strip_253.png`).

### C5. Non-regression (`gate_table.tsv`, `gap_table.tsv`)

- **Landing ends** within +0.14 s of the oracle on every spell (93 +0.14, 38 +0.01, 54 +0.02, 656 +0.13,
  41 +0.12, 253 +0.02, 458 +0.05; band +-0.4). **Rowtop** unchanged (2.57 / 1.54 / 1.80).
- **Landing coverage** rose toward the oracle on every pool-bound spell: 38 1.93 -> 3.32 (oracle 3.89),
  54 1.60 -> 2.48 (2.46), 656 1.27 -> 2.08 (2.49). 458 1.58 -> 1.57.
- **Hue:** landing 93 34 -> 41 (oracle 46), 38 32 -> 35 (47), 41 70 -> 68 (72), 253 202 -> 200, 54/656 within
  2-4; Source hues unchanged.
- **Source** (`source_all.py`): coverage 93 0.377, 38 0.386, 54 0.531, 656 0.609, 41 1.137, 253 3.375, 458 0.069
  (B1124 0.413 0.429 0.546 0.597 1.133 3.271 0.065); extent / centroid within the B1123 same-config spread
  (93 extent 2.27, inside B1123's 2.18-2.62).
- **Hold (Act-0.1..+0.5):** unchanged to Act+0.4; Act+0.5 rises earlier on 93/38/54 (the denser landing).
- **93/38 whole sprites (B1124):** atlas and pack identical (sha), unchanged.

### C6. Frame time (NVIDIA, `gate/frame_summary.txt`; windows of 60 frames from the first /cast)

| leg | B1124 window means / worst | completion | notes |
|---|---|---|---|
| A (38 54 656 458) | 0.96-1.15 / 2.2 ms | shipped: 1.11-1.46 / 2.4 ms, repeat 0.97-1.19 / 1.9 ms; 458 at the style size 0.91-1.15 / 1.9 ms | the 1.11-1.46 run is +0.27 ms from its FIRST cast (38, config unchanged) and did not repeat (`gate/repeat/`): run noise |
| B (253 93) | 0.78-1.00 / 1.4 ms | 0.86-1.19 / 1.5 ms | +0.19 ms at worst: the doubled field |
| C (41) | 0.88-1.01 / n/a | 0.82-0.97 / 1.1 ms | - |

0 `slow!` on every leg. Leg C's 53.8 ms whole-leg worst is the zone-in hitch before any cast (as at B1123).
**iGPU / XU4 estimate (not measured):** an XU4 is an `EQT_DRM` build, so it keeps the sbc pool (4096); the
desktop pool reaches x86 iGPUs and macOS only. There the worst landing fill grows x2.1 (93: 1927 -> 4079 sprites)
and x2.5 (253: x1.9 sprites at x1.35 area); on the NVIDIA box that is +0.2 ms, so an iGPU at ~5-10x lower
blend rate would see roughly +1-2 ms in a 253 field. Measure it before B1130 grows the pool further.

### C7. Memory (per 1024 particles, so a re-size is arithmetic)

`sizeof(UnifiedParticle)` = 120 B; free list `uint16_t` = 2 B; `SimulationOutput` is double-buffered
(`output_[2]`, each `renderBuffer` keeps its capacity); the GLES2 renderer stages 44 B per drawn sprite twice
(`packedVerts_` + its category vector).

| item | bytes per 1024 | when |
|---|---|---|
| pool + free list | 124,928 | allocated at init (fixed) |
| render copies (2 x 120 B) | 245,760 | at full occupancy |
| draw staging (2 x 44 B) | 90,112 | at full occupancy |
| **total** | **460,800 (450 KiB)** | |

- **x86 delta 4096 -> 8192:** +499,712 B fixed (pool), up to +1,843,200 B (1.76 MiB) at full occupancy
  (pool + render copies alone: +1,482,752 B).
- **Orange Pi:** pool unchanged (4096): 0 B fixed; occupancy changes by the C2 deltas only.
- **Hard ceiling:** the free list is `uint16_t`, so a pool above 65,536 needs a wider index (B1130 note).

### C8. Tests

- **New / re-banded (every pool number is read from the presets file; each pool-accounting case runs at
  BOTH platforms' pools):**
  - `tests/spell_pool.h` (helper): `Shipped(platform)`, `LoadAt(pool)`.
  - `SpellPoolRule.{EveryLandingPeaksAtTheRuleForThisPool, TheRuleKeepsEachLandingsSchedule,
    WhirlingWindIsASteadyFrameFillingField, ThePoolStillHoldsTwoOfTheLargestLanding}` x {desktop, sbc};
    `SpellPoolPlatforms.TheSbcPoolIsTheSmallerOne`; `SpellPoolExceptions.OnlyTheOracleLimitedLandingsAreExceptions`.
  - `SpellBurstPool.{OneLandingReachesTheOraclesPeakSpriteCount, TheConfiguredPoolHoldsTwoOverlappingLandings}`,
    `SpellBurstPoolSizing.TheOldHardcodedPoolClipsAndTheConfiguredOneDoesNot` x {desktop, sbc}.
  - `SpellLandingPool.{OneFedLandingFitsHalfThePool, ASourceAndItsOwnLandingFitHalfThePool,
    EverySourceCloudFitsBesideALanding}` x {desktop, sbc} (real worker).
  - `ConstrainedRendererConfigTest.{JsonOverride_SpellParticlePoolPicksThisBuildsPlatform,
    ShippedOrangePiPreset_CarriesThePlatformSpellParticlePool}`.
  - `WhirlingWindSpritesAreTheStyleRaisedToTheRoiBandTop` (x1.3) and the 458 x1.3 pin; the B1119 size block's
    comment table is now the ROI table (C3).
  - `test_config_key_reachability`: `landingPopulation.exceptions` is a data-keyed map.
- **RED on the pre-fix tree** (`review/B1123/leg2/tests/red_prefix_*.txt`): with fable a0fdc62d's
  `constrained_presets.json` (no key) every pool-accounting case fails (`ASSERT_GT(pool, 0)` in each fixture); with its
  `spell_effects.json` the ROI 253 pin fails.
- **Runs** (Release, named targets, -j8; `review/B1123/leg2/tests/`):
  `ctest -R "Spell|Sim|Particle|Landing|DataPath|ConfigKey|B1106|CasterOwn|Atlas|Sprite|Preset|Constrained"`
  579 tests, 578 pass, 32 of them skipped (EQ client files), 1 fails (below); `ctest -L hygiene` 48/48.
  `CwdElsewhere.ConstrainedPresetOverridesLoadFromAnyDirectory` fails in a build tree outside the repo and
  equally with B1124's binary; it passes from a repo-shaped root (environment limit, as B1123 noted).
- **CLAUDE.md:** "The spell sprite atlas" names the ROI instrument as the size authority and the pool key
  (8192 / 4096, PROVISIONAL, B1130); two narrative sentences were tightened to stay under 60,000 bytes.

### C9. FOLLOW-UP (orchestrator; workers do not cut cards)

1. **Over-band sizes (Will).** 93/54/656/41 Landing and 93/38/54/656/41/458 Source read over their ROI band at
   gate density. Shrinking costs coverage (93 4.7 would need > 18.8 k). Rule which wins, or whether the band is
   read on a thinned capture (where 38/41 are in band and 253/458 short).
2. **253 Landing 5.8** needs ~15 k under the half-pool rule at the ROI-band size, or the ribbon emitter
   (B1120 FOLLOW-UP 4). Feed into B1130's re-size.
3. **iGPU frame time** of the desktop pool is estimated, not measured (C6); measure it on an x86 iGPU before
   B1130 grows the pool.
4. **Rig defect fixed here:** the B1118-B1124 `class_run.sh` changed char 67's class BEFORE taking the lane
   lock. At 12:55Z, with B1130's oracle session holding lane 5, it set class 6 then 14 for ~13 s each before
   failing on the lock (class 12 restored). The leg2 rig changes the class only inside the lock; B1130's
   rig should be checked for the same order.
5. **458's coverage** is tour-sensitive (125 % on the gate tour at both populations, 73-103 % on the cal tour).
6. **uint16 free list** caps the pool at 65,536 (C7).

## Sizes-first leg (Will, #443 note 7118: "sizes match the oracle first, coverage follows from the pool")

Evidence: `/mnt/projects/fable/review/B1123/leg3/` (index: its `README.md`). Gate: ThinkPad lane 5, 2026-10-22
23:20-23:35 UTC, legs A/B/C as the completion leg. Tree = this commit, synced to `~/src/willeq`, `build-gles2`
Release target `willeq` (sha e295dcd3). Pack 8199a8a2 mapped on every leg, and every leg logged
`Particle pool initialized (size=8192)`. Capture: x11grab 30.00 fps, max frame gap 34 ms, 0 gaps > 50 ms on all three
legs (`gate/capture_fps.txt`). `--dump-frames-dir` was not used, and the first cast came >= 36 s after `#set time`.

### S0. Result: every size row is in its ROI band; coverage dropped

**Instrument and band:** `fx_sprite_roi.py` p90 (Landing Act+1.2..1.8, Source BC+0.3..Act-0.05, pooled), read DENSE on
the gate capture, against `review/B1120/measure_oracle/roi_oracle.txt`. The band is +-15 %, the same one the completion
leg used. **Every over-band row was SHRUNK. No row was held for coverage.**

| row | oracle ROI | band | old fable size (start mid) | old ROI (completion gate) | new fable size (start mid) | new ROI (gate) | in band |
|---|---|---|---|---|---|---|---|
| 93 L (record-0 style) | 0.075 | 0.064-0.086 | impact 0.287 / complete 0.353 | 0.094 | 0.198 / 0.244 (x0.69) | **0.074** | yes |
| 38 L | 0.069 | 0.059-0.079 | 0.2125 / 0.261 | 0.087 | 0.1785 / 0.219 (x0.84) | **0.073** | yes |
| 54 L | 0.044 | 0.037-0.051 | 0.3385 / 0.416 | 0.074 | 0.1525 / 0.187 (x0.45) | **0.046** | yes |
| 656 L | 0.035 | 0.030-0.040 | 0.287 / 0.353 (the style) | 0.063 | 0.115 / 0.141 (x0.40) | **0.033** | yes |
| 41 L | 0.101 | 0.086-0.116 | 0.4335 / 0.533 | 0.156 | 0.2645 / 0.325 (x0.61) | **0.102** | yes |
| 253 L | 0.093 | 0.079-0.107 | 0.373 / 0.459 | 0.107 | 0.351 / 0.4315 (x0.94) | **0.095** | yes |
| 458 L | 0.101 | 0.086-0.116 | 0.373 / 0.459 | 0.110 | 0.3615 / 0.4455 (x0.97) | **0.100** | yes |
| 93 S (castGlow style) | 0.084 | 0.071-0.097 | 1.03 | 0.135 | 0.659 (x0.64) | **0.078** | yes |
| 38 S (castGlow style) | 0.074 | 0.063-0.085 | 1.03 | 0.130 | 0.659 (x0.64) | **0.077** | yes |
| 54 S | 0.027 | 0.023-0.031 | 0.45 | 0.066 | 0.216 (x0.48) | **0.027** | yes |
| 656 S | 0.028 | 0.024-0.032 | 0.45 | 0.066 | 0.225 (x0.50) | **0.028** | yes |
| 41 S (castSpray, 41's override) | 0.113 | 0.096-0.130 | 1.356 | 0.181 | 1.0035 (x0.74) | **0.108** | yes |
| 253 S | 0.178 | 0.151-0.205 | 1.03 | 0.173 | 1.03 (unchanged, now pinned in 253's override) | **0.164** | yes |
| 458 S | 0.043 | 0.037-0.049 | 0.55 | 0.051 | 0.4895 (x0.89) | **0.046** | yes |

**14 of 14 rows are in band.** Calibration took two cal-tour legs (`cal/m_c1`, `cal/m_c2`); c2's sizes are the shipped
ones (`cfg/final.json` = `config/spell_effects.json`), generated by `scripts/b1123c_gen.py`.

Notes on the calibration and the size changes:

- **One row read out of band on the cal tour only.** 253 Source read 0.150 on cal c2, 0.001 under its band, while
  its size was unchanged from the completion leg. On the gate tour (leg B, druid) it reads 0.164, in band. The
  completion leg's cal/gate pair (0.157 / 0.173) shows the same tour offset.
- **The dense read responded to size on every row, so the thinned (separable) column was not needed.** In c1, at
  x0.71, 54/656 Landing barely moved (0.072 -> 0.066). In c2, at x0.45 / x0.40, they landed in band. The
  orchestrator's fallback (pin on a thinned capture) was not used; `scripts/thin.py` is in the rig, unrun.
- **What moved besides the rows.** Record 0 is 93's own measurement, so shrinking it moves 93, 38's Source, and
  every unmeasured spell (they resolve to the plain style). Every other measured row now carries its sizes
  explicitly, so the style shrink does not move it:
  - 656's Landing previously resolved to the style;
  - 253's Source sizes are kept, pinned in its override.
- **The castSpray STYLE is untouched.** 41 is its only measured spell, so 41's shrink sits in 41's override.

### S1. Coverage after the shrink (reported, not gated), caster heights^2

| spell | landing: oracle | completion gate (old sizes) | **sizes-first gate** | % of oracle | Source: oracle | completion | **sizes-first** |
|---|---|---|---|---|---|---|---|
| 93 | 5.847 | 4.762 | **3.006** | 51 % | 0.364 | 0.377 | **0.168** |
| 38 | 3.889 | 3.320 | **2.551** | 66 % | 0.361 | 0.386 | **0.151** |
| 54 | 2.461 | 2.479 | **0.981** | 40 % | 0.461 | 0.531 | **0.152** |
| 656 | 2.493 | 2.076 | **0.622** | 25 % | 0.615 | 0.609 | **0.193** |
| 41 | 4.154 | 4.062 | **2.249** | 54 % | 1.209 | 1.137 | **0.624** |
| 253 | 7.281 | 4.032 | **3.673** | 50 % | 3.884 | 3.375 | **3.317** |
| 458 | 1.242 | 1.572 | **1.496** | 120 % | 0.058 | 0.069 | **0.055** |

- **The card's coverage rows are no longer met:**
  - 93 Landing 3.01 (target >= 4.7);
  - 41 Landing 2.25 (target >= 3.3);
  - 253 Landing 3.67 (target >= 5.8);
  - 253 Source 3.32 (target >= 3.1) is still met.

  This is the sizes-first ruling's stated consequence: coverage at the right sprite size is now a population
  question, the pool's (S2), not a size lever.
- **Source coverage roughly halved** on the castGlow-style and record-23 Sources (93, 38, 54, 656) and on 41. The
  oracle's Source is denser than fable's population at the right size.
- **Unchanged (`gate_table.tsv`, `feed/bands_leg3_*.txt`):**
  - landing ends: within -0.13..+0.10 s of the oracle (band +-0.4);
  - rowtop: 2.57 / 1.51-1.52 / 2.49 for 41 / 1.81;
  - height-band profiles: 93/38 still fill 0-2.6 caster heights, 54/656/458 stay below 1.5;
  - landing hues: 93 35, 38 34, 54 221, 656 220, 41 72, 253 201;
  - frame time: window means 0.80-1.23 ms, worst 7.8 ms (zone-in), 0 `slow!` (`gate/frame_summary.txt`).
- **Landing onset.** `landing_onset_abs.py` ABS reads +0.03..+0.37 on the gate. These are held-Source captures, as
  in sec. 4, and `triggerDelay` is untouched.

### S2. The pool: unchanged (`spellParticlePool` {desktop 8192, sbc 4096}; `applyPool` and every exception untouched)

The B1130 survey (`review/B1130/README.md` "Tranche 2 results", `record_table.tsv`) measured the official client's
sprite demand over 41 spells.eff records:

- **Compact-sprite demand:** the maximum is **875 live sprites** (record 15). Tranche 1's maximum was 734 (record 25).
- **Source demand:** the maximum is **703** (record 19).
- **Uncountable records:** seven glare or merged landings (records 3, 26, 32, 22, 8, 19, 30) saturate the blob
  estimator, so their 2,100-8,048 "demand" is not a sprite count.

Every countable demand fits far below both pools. So the provisional key stays as it is: not raised, not lowered.

The population rule stays one rule with no second copy of a pool size. Each landing plus its held Source peaks at half
the pool, and the 41 / 54 / 656 / 458 exceptions are as they were. Their `why` strings still cite the coverage they read
at the completion leg's sizes, and that coverage is history now (S1).

### S3. Tests (same commit)

- **`tests/test_spell_burst_oracle_match.cpp`:** `SpriteSizeAndExpandSpeedAreTheMeasuredOnes` pins the record-0 style
  at its sizes-first values (impact 0.155/0.241/0.304/0.454, spellComplete 0.184/0.304/0.367/0.541).
- **`tests/test_spell_colour_and_overrides.cpp`:**
  - New: `B1123SizesAreTheOraclesRoi` pins every row's start-mid, as in the S0 table (+-0.0006). It replaces four tests:
    - `B1119LandingStylesAreTwoPointEightTimesT422`;
    - `B1119LandingOverridesFollowTheOracleTable`;
    - `B1119SourceSizesFollowTheOracleTable`;
    - `WhirlingWindSpritesAreTheStyleRaisedToTheRoiBandTop`.
  - The ROI comment table carries the oracle, cal and gate readings plus the coverage. Coverage is not asserted: it
    is a capture measurement.
  - `B1122SourceShapes…` now asserts 253's Source size differs from the shrunk style.
- **RED on fable 52b810e2's config** (`review/B1123/leg3/tests/red_52b810e2_config.txt`): three tests fail,
  `SpriteSizeAndExpandSpeedAreTheMeasuredOnes`, `B1123SizesAreTheOraclesRoi` and `B1122SourceShapesFollowTheSpellsEffRecord`.
- **Run** (Release, the two named targets): 14/14 and 24/24, 1 skipped (spells.eff).
- **CLAUDE.md:** "The spell sprite atlas" now says sizes match the oracle FIRST (coverage follows from the pool, #443
  note 7118). The pool sentence drops PROVISIONAL and cites B1130's demand.

### S4. FOLLOW-UP (orchestrator)

1. **Coverage is now pool-bound at the oracle's sprite size on every pool-bound spell.** Even so, nothing measured
   needs more than 8192 by B1130's compact count. The gap to the oracle's coverage (93 51 %, 54 40 %, 656 25 %, 41 54 %,
   253 50 %) is fable drawing FEWER sprites than the oracle per landing, at the right size. One option would be to
   re-fit the exceptions (54/656 3517 live, 41 2492), or the half-pool rule, against coverage at the new sizes. That
   is Will's population call and is not made here.
2. **Sources** read 40-50 % of the oracle's coverage at the right size (the same population question, Source rates).
3. **Calibrate against the gate tour.** The cal tour reads 253 Source about 0.015 lower than the gate tour, which is
   the tour offset noted in S0.
