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
