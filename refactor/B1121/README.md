# B1121 (#441): spell landing DURATION from the official client (B1118 M3)

Third fix card of the B1118 chain (B1119 size -> B1120 volume -> **B1121 duration** ->
B1122 source emitter -> B1123 overrides -> B1124 atlas). Scope: when the landing
(`spellComplete` + `impact`) ends. Evidence: `/mnt/projects/fable/review/B1121/`
(index: its `README.md`).

## 0. Result

All seven landings now end within +-0.13 s of the official client (gate, sec. 5); the
target was +-0.4 s.

| spell | 93 | 38 | 54 | 656 | 41 | 253 | 458 |
|---|---|---|---|---|---|---|---|
| oracle end (Act+, s) | 4.867 | 4.850 | 5.103 | 5.004 | 6.224 | 6.522 | 4.942 |
| B1118 fable | 2.319 | 2.294 | 2.219 | 2.234 | 2.246 | none | 2.196 |
| B1120 fable | 2.826 | 2.824 | 2.806 | 2.757 | 2.884 | 5.766 | 2.746 |
| **B1121 fable** | **4.888** | **4.776** | **5.170** | **5.072** | **6.347** | **6.457** | **5.059** |
| B1121 - oracle | +0.02 | -0.07 | +0.07 | +0.07 | +0.12 | -0.07 | +0.12 |

- **Mechanism matched: the oracle FEEDS its landing** (sec. 2). Fable now does too:
  a peak phase (B1120's emission, unchanged) and a sustain phase to the measured end.
  Particle lifetimes are untouched (1.6-2.0 s), so the sprite sheet still plays once
  per particle life at the same rate.
- **Peak coverage, height, onset and ROI size are B1120's for six of seven spells**
  (sec. 6): the peak phase is byte-identical to B1120's. **38 reads 1.5-6 % low**
  in four captures against three B1120-config captures (1.626-1.714 vs 1.733-1.747),
  with no mechanism found. It is flagged, not hidden (sec. 6a).
- **What is NOT matched (named, sec. 8):** the plateau is thin. Between Act+2.8 and the
  end, the oracle's 93 column holds 2.3-4.7 caster heights^2; fable's holds 0.4-0.7.
  The 4096 pool is the limit (sec. 4).

## 1. Mechanism (B1118 M3)

Before this card both landing styles were one rectangular emission: `spawnRate` over a
0.6 s `emitterLifetime`, particles living 1.6-2.0 s, starting at `triggerDelay` 0.33 s.
So every landing was gone by 0.33 + 0.6 + 2.0 = 2.93 s after OP_Action (measured
2.76-2.88 s, B1120). 253 was the exception (a 3.6 s field, ended at 5.77 s).

CLAUDE.md "The spell sprite atlas" quoted the 1.6-2.0 s band as the impact/spellComplete
lifetime, and `test_spell_burst_oracle_match.cpp` pinned `emitterLifetime + lifetimeMax`
to T422's 2.1-3.0 s. That band is a per-PARTICLE life (T422, llvmpipe era); the duration
B1118 measured at real frame rate is 4.85-6.52 s.

Two ways to lengthen it:
- **Longer particle life.** `SpellSpriteAtlas::frameForNormalizedAge()` plays the 16
  frames once per life, so life x2.5 slows the animation x2.5.
- **Longer emission (feed).** Sprites keep their life and animation; new ones keep
  being born.

## 2. The oracle: fed, not long-lived (frames)

`review/B1121/scripts/fx_bands.py` (from B1120's `fx_shape.py`, same mask, crop and
scale) splits the lit pixels of each frame into height bands above the caster's feet.
Output: `review/B1121/feed/bands_oracle_<id>.txt` (0.4 s step) and `_fine.txt`
(every frame).

A single cohort of long-lived sprites rising at 0.15-1.7 caster heights/s (the B1120
column) would leave the BASE band empty within ~1 s of its last birth; a fed column keeps
it lit. The oracle keeps it lit to the end:

| 93 (oracle_druid) | f7555 Act+1.23 | f7575 +2.03 | f7595 +2.83 | f7615 +3.63 | f7635 +4.43 | f7642 +4.71 |
|---|---|---|---|---|---|---|
| 0.0-0.5 H band | 1.221 | 1.309 | 1.030 | 0.947 | 0.806 | 0.058 |
| 2.0-2.6 H band | 0.005 | 0.854 | 0.664 | 0.289 | 0.014 | 0.001 |

- **38** (oracle_leg): the base band holds 0.70-1.09 from f9777 (Act+1.21) to f9857
  (Act+4.41).
- **41**: 0.89-1.36 from f6440 (+1.22) to f6520 (+4.42).
- **253**: 1.11-1.25 from f6553 (+1.20) to f6633 (+4.40).
- **The ends differ by record:**
  - 93/38 end abruptly. The base band falls 0.738 -> 0.058 between f7638 (Act+4.55)
    and f7642 (Act+4.71), and 0.585 -> 0.030 between f9861 (+4.57) and f9864 (+4.69).
    Whatever is alive at the end dies within ~0.16 s.
  - 41 and 253 fade over ~1.6-2.0 s. 253 goes 6.63 -> 0.19 from f6633 (+4.40) to
    f6683 (+6.40), the length of a 1.6-2.0 s particle life after the feed stops.
  - 54/656/458 step down at ~Act+3.4 (one component stops), then hold a lower
    plateau to ~Act+4.8.

**Conclusion: the official client feeds the landing.** Fable matches that with longer
emission and leaves the particle life alone. The 25 fps capture cannot resolve a
per-sprite animation rate (B1118 sec. 6); none is claimed.

## 3. The change

### Code: a two-phase emission

`EmitterConfig` gains three fields; `SpellEffectsConfig` parses and merges them as
style/override keys like every other:

| key | meaning | default |
|---|---|---|
| `spawnWindow` | `spawnRate` runs while emitter age < this | 0 = the whole `emitterLifetime` |
| `sustainRate` | per second, from `sustainFrom` to `emitterLifetime` | 0 |
| `sustainFrom` | emitter age the sustain starts | 0 |

- **Where:** `include/client/graphics/environment/unified_particle.h` (EmitterConfig),
  `spell_effects_config.{h,cpp}` (StyleSettings, parse, merge),
  `spell_emitter_presets.cpp` (`applyStyle`, density-scaled like `spawnRate`),
  `sim_particles.cpp` (the spawn-rate arm sums the two phases).
- **Backward compatible:** with the defaults the arm is the old one. Fire, weather and
  every non-landing style are untouched.

Why not just a longer window at a lower rate (calibration run v1, sec. 7)? At the
same live population a steady stream holds young, small sprites near the spawn point
instead of B1120's synchronised, grown cohort. The peak coverage fell 13-56 %
(93 2.96 -> 2.35, 458 1.33 -> 0.69). The peak phase keeps B1120's cohort exactly.

### Config (`config/spell_effects.json`)

- **Peak phase = B1120**: `spawnRate` unchanged, `spawnWindow` 0.6 (B1120's
  `emitterLifetime`).
- **Sustain** (both halves, split in the peak rates' ratio):
  `min(peak population / 1.8 s mean life, pool bound)`. The first term holds the peak's
  population (the oracle's 54/41/253/458 plateaus sit near their peaks); the second is
  sec. 4.

| record | spells | `emitterLifetime` | sustain /s (spellComplete + impact) | `sustainFrom` |
|---|---|---|---|---|
| style (record 0, and every spell without its own) | 93, 38 | 2.85 | 186.2 + 113.8 = 300 (pool-bound) | 1.6 (style) |
| 23 | 54, 656 | 3.0 | 499.8 + 333.2 = 833 | 1.6 |
| 20 | 41 | 4.15 | 519.1 + 317.9 = 837 | 1.6 |
| 33 | 458 | 2.9 | 166.5 + 166.5 = 333 | 1.6 |
| 35 | 253 | 4.35 (was 3.6) | none: already a continuous field (`spawnWindow` 0) | - |

- **Where E comes from.** The last lit frame falls ~1.7 s after the emission stops
  (the sustain thins below the B1118 visibility threshold before the last particle
  dies). So E = oracle end - 0.33 - ~1.7, calibrated on `cal/run_v1..v3` (sec. 7).
- **`sustainFrom` 1.6** is the latest start that leaves the peak untouched: the peak
  cohort starts dying at emitter age 1.6. An earlier start adds to the cohort's peak and
  breaks the pool bound (sec. 4).
- **Unmeasured spells** take the style, i.e. the record-0 duration (~4.9 s).

### Side effects (what moved besides the end)

- **Live population late in the landing:** a sustain plateau (sec. 4) where there was
  nothing.
- **Pool peaks** rose by 5-56 sprites (sec. 4).
- **Nothing else:** shapes, sizes, lifetimes, delay, colours and peak rates are
  byte-identical (`git diff` on `config/spell_effects.json` adds keys and changes only
  `emitterLifetime`).

## 4. Pool accounting (4096, two overlapping landings)

Expected live counts (the integral of rate x survival, lifetimes uniform 1.6-2.0 s;
`tests/spell_landing_population.h`, cross-checked numerically in
`review/B1121/cal/`):

| spell | B1120 peak | B1121 peak (emitter age) | sustain plateau | at window close |
|---|---|---|---|---|
| 93 | 1920 | 1925 (1.64 s) | 540 | 360 |
| 38 / style | 1740 | 1746 (1.64 s) | 540 | 360 |
| 54 / 656 | 1500 | 1555 (1.74 s) | 1499 | 1125 |
| 41 | 1506 | 1562 (1.74 s) | 1507 | 1507 |
| 458 | 600 | 622 (1.74 s) | 599 | 416 |
| 253 | 2160 | 2160 | 2160 | 2160 |

- **Two 93 landings:** 3850 <= 4096.
- **253 + 93:** 2160 + 1925 = 4085 <= 4096 (`WhirlingWindIsASteadyFrameFillingField`).
- **The record-0 sustain is pool-bound.** A constant feed at B1120's rate needs
  3200 x 1.8 = 5760 live per 93 landing. Holding the peak's population (1920 / 1.8 =
  1067/s) needs 1920 + 1067 x (overlap) > 2048 against the cohort. Only ~300/s fits
  after the cohort starts dying. Hence the thin plateau (sec. 8, FOLLOW-UP 1).
- **The real worker agrees:** `OneFedLandingFitsHalfThePool` drives 93 through
  `SimulationWorker` and checks a peak of 1500 to 2048.

## 5. Gate (B1118 legs A/B/C, ThinkPad lane 5)

- **Build.** `~/src/willeq`, branch `b1121-fx-duration` at WIP 9e4a6acb
  (`review/B1121/fable_*/willeq.head`), `scripts/build-gles2-x86.sh` JOBS=12,
  `CMAKE_BUILD_TYPE=Release`. The WIP commit was squashed into this commit; its
  `src/`, `include/`, `config/` and `tests/` trees are identical to this commit's.
- **Capture.** x11grab 30.00 fps on :99, max frame gap 34 ms, 0/59 stale grabs in every
  landing window (all 7). `--dump-frames-dir` not used.
- **Frame time.** Every `FRAME TIMING BREAKDOWN` window counts 60 frames (vsync,
  127 + 80 + 61 windows). Window-mean Total Frame 0.90-1.08 ms, max window 1.26 ms;
  worst single frame 6.1 ms (`[entity-build] frame_ms max`); 0 `slow!`.
- **Pool.** Every leg logged `Particle pool initialized (size=4096)`.
- **Window length.** The tour's per-cast spacing was checked: the next cast's
  BeginCast is >= 11 s after the previous OP_Action and the landing baseline is
  Act+8.5..10 s, so a 6.5 s landing ends inside the window and before the baseline. No
  plan change was needed.
- **Table:** `review/B1121/gate_table.tsv`. **Strips:**
  `review/B1121/strips/strip_<id>.png`, ORACLE / B1120 / B1121 at
  Act+0.5/1/2/3/4/5/6 s.

## 6. Non-regression (B1120 gate -> B1121 gate)

| spell | coverage | onset (oracle) | rowtop | extent | ROI p90 | hue |
|---|---|---|---|---|---|---|
| 93 | 2.961 -> 2.970 | 0.426 -> 0.455 (0.547) | 2.57 -> 2.57 | 2.84 -> 2.75 | 0.122 -> 0.124 | 35 -> 36 |
| 38 | 1.747 -> 1.714 | 0.491 -> 0.509 (0.530) | 2.57 -> 2.57 | 2.96 -> 3.06 | 0.093 -> 0.090 | 33 -> 32 |
| 54 | 1.548 -> 1.591 | 0.406 -> 0.436 (0.423) | 1.55 -> 1.53 | 3.57 -> 3.71 | 0.088 -> 0.078 | 220 -> 220 |
| 656 | 1.267 -> 1.256 | 0.391 -> 0.405 (0.404) | 1.53 -> 1.54 | 3.15 -> 2.89 | 0.079 -> 0.075 | 220 -> 220 |
| 41 | 3.313 -> 3.329 | 0.417 -> 0.414 (0.424) | 2.57 -> 2.57 | 4.17 -> 3.98 | 0.203 -> 0.206 | 71 -> 71 |
| 253 | 1.582 -> 1.571 | 0.466 -> 0.524 (0.802) | 2.57 -> 2.57 | 4.20 -> 4.24 | 0.057 -> 0.055 | 203 -> 203 |
| 458 | 1.556 -> 1.608 | 0.412 -> 0.426 (0.422) | 1.76 -> 1.78 | 2.30 -> 2.28 | 0.078 -> 0.068 | white 0.03 -> white 0.03 |

- **Coverage.** Four rise, three read 0.7-1.9 % lower: 38 (-1.9 %), 656 (-0.9 %) and
  253 (-0.7 %). For all three the configuration up to the coverage peak is
  byte-identical to B1120's: the sustain starts at emitter age 1.6 s (Act+1.93), and
  253's field changed only its `emitterLifetime` (3.6 -> 4.35), which acts after its
  peak. Sec. 6a measures the capture-to-capture spread: 656 and 253 sit inside it; 38
  does not.
- **Onset.** All seven are inside the oracle's 0.40-0.80 band; 656 was 0.013 s short
  in B1120 and is now in. Five moved toward the oracle's own value.
- **Height** (rowtop) is unchanged. **Extent** moved by -0.26..+0.14 caster heights
  (656 3.15 -> 2.89 is the largest).
- **ROI size** stays inside B1119/B1120's spread; the size keys are byte-identical
  (`B1119*` tests).
- **Hue** is unchanged.

### 6a. Repeat leg A (noise sample)

Leg A was captured twice more: once as a repeat (`fable_A_rep/`) and once as a CONTROL
running the B1120 config on the B1121 binary (`fable_A_ctl_b1120cfg/`). Peak coverage,
caster heights^2:

| spell | B1120 gate | control: B1120 config, B1121 binary | B1121 gate | B1121 repeat | B1121 cal v2 / v3 |
|---|---|---|---|---|---|
| 38 | 1.747 | 1.733 | 1.714 | 1.660 | 1.699 / 1.626 |
| 54 | 1.548 | 1.573 | 1.591 | 1.532 | 1.614 / 1.571 |
| 656 | 1.267 | 1.240 | 1.256 | 1.272 | 1.300 / 1.255 |
| 458 | 1.556 | 1.553 | 1.608 | 1.575 | 1.388 / 1.351 (cal leg) |

The control's ends are 2.73-2.81 s, so the new code with the old config reproduces
B1120 exactly. The ends in the repeat (38 4.90, 54 5.18, 656 5.11, 458 5.03) are in
band too.

54, 656 and 458 overlap B1120 in their capture-to-capture spread (+-2-3 %).

**38 does not, and this is flagged rather than explained away.** Its three B1120-config
readings are 1.733-1.747; its four B1121 readings are 1.626-1.714 (mean -3.7 %).
Nothing in the configuration differs before Act+1.93, when the sustain starts. The peak
is at Act+2.0. The sustain only ADDS sprites; the renderer blends additively with depth
writes off (`unified_particle_renderer.cpp:463-464`), so new sprites cannot hide old
ones. No mechanism was found. B1122's gate should re-read 38. If the drop persists,
move the record-0 `sustainFrom` later (1.6 -> 1.8 s keeps the end and the pool
bound).

## 7. Calibration (`review/B1121/cal/`, one wizard leg with all seven casts)

| run | config | ends (93 38 54 656 41 253 458) | peak coverage (same order) |
|---|---|---|---|
| v1 | one window, rate / 3, E 2.8/3.0/4.15/4.35/2.9 | 4.93 4.93 5.12 5.06 6.38 6.48 4.89 | 2.35 1.40 1.42 1.17 2.88 1.52 0.69 |
| v2 | peak + sustain, `sustainFrom` 1.4, style E 3.15 | 5.16 5.17 5.11 5.05 6.32 6.48 4.92 | 3.06 1.70 1.61 1.30 3.38 1.54 1.39 |
| v3 | as v2 with `sustainFrom` 1.6 (pool) | see `cal/m_v3/` | see `cal/m_v3/` |
| final | v3 with style E 2.85 | the gate (sec. 5) | the gate |

v1 found the coverage cost of a single long window (sec. 3). v2 found the record-0 E
(the end moved +0.3 s with the lower record-0 sustain). `sustainFrom` moved 1.4 -> 1.6
to keep one 253 field plus one 93 landing inside the pool.

## 8. Tests (same commit)

- **New helper `tests/spell_landing_population.h`:** the expected live count of a
  landing (rate schedule x survival, midpoint rule), and the peak phase's cohort.
- **`test_spell_burst_oracle_match.cpp`, re-banded:**
  - The emission-window tests now pin `spawnWindow` (T422's 0.5-0.8 s window is the
    peak phase).
  - `TheEffectLastsAsLongAsTheOracles` (2.1-3.0 s) is replaced by
    `EveryMeasuredLandingEndsWhereTheOraclesDoes`. It pins the sim's last death
    (`triggerDelay + emitterLifetime + lifetimeMax`) per spell to [oracle end, +0.8].
    The comment names the measuring command; the measured end is the capture's.
  - New: `TheLandingIsFedPastItsPeakWindow`.
  - The lifetime test keeps 1.4-2.4 s, reworded as a per-particle life.
    `SheetPlaybackRateFollowsFromTheMeasuredLifetime` is unchanged and passes: the
    animation still plays once per particle life.
  - The pool tests use the expected live peak.
  - The T422 DURATION paragraph is marked superseded.
- **`test_spell_colour_and_overrides.cpp`:**
  - 93/41 population ratios are on the peak cohort (unchanged values).
  - The pool tests use the live peak.
  - `WhirlingWindLastsAsLongAsTheOracles` moved from 5.6 +-0.3 (T422) to a last death
    in [6.52, 7.32].
- **`test_spell_landing_volume.cpp`:**
  - New `TheColumnIsFedAtTheFeetUntilItsWindowCloses` (real worker): young sprites at
    the feet just before the window closes, and nothing alive after the last death.
  - New `OneFedLandingFitsHalfThePool` (real worker, 93 both halves).
  - The ten B1120 shape/onset tests pass unchanged.
- **RED on the B1120 config** (run with B1120's `config/spell_effects.json` swapped
  in): `EveryMeasuredLandingEndsWhereTheOraclesDoes`, `TheLandingIsFedPastItsPeakWindow`,
  both `*EmitsOverAWindow*` (no `spawnWindow`) and `WhirlingWindLastsAsLongAsTheOracles`
  fail. `TheColumnIsFedAtTheFeetUntilItsWindowCloses` passes on it, because it reads
  the window the config declares; it guards the sim side (young sprites at the feet
  at the close, none after the last death), not the duration.
- **Local run** (Release, named targets): `ctest -R "Spell|Sim|Particle|Landing|DataPath|ConfigKey|B1106|CasterOwn"`
  102 tests, all pass; 3 skip as on fable (need EQ client files).
  `ctest -L hygiene` 45/48. The same three `*Reproducible` tests fail as at B1120:
  their generator tools were not built.

## 9. Docs

- **CLAUDE.md "The spell sprite atlas":** the 1.6-2.0 s duration sentence and its
  8-10 cells/s figure are replaced. The new text says the lifetime is per particle, the
  landing is fed, and its end is measured by `review/B1121/scripts/b1120_measure.sh`.
  The file shrank by 98 bytes (59,985 -> 59,887).
- **`scripts/refactor/check_doc_counts.py`:** a NAMED_COMMAND row keeps CLAUDE.md naming
  that command instead of a duration number.

## 10. Pi note (estimate, not measured)

- **The peak is unchanged.** The landing's peak sprite count and fill are B1120's.
- **Record 0 adds a 540-sprite plateau** for ~2 s per landing.
- **54/656/41/458 hold their peak population** (600-1500) for 1.4-2.6 s longer, so the
  Mali-400 blend cost of a landing lasts ~2.2x as long.
- **253 lasts 0.75 s longer.**
- **CPU:** the sim's per-particle cost scales with live count, which never exceeds
  B1120's peak by more than 56.

Lane-1 measurement owed (B1120 FOLLOW-UP 7).

## 11. FOLLOW-UP (for the orchestrator; workers do not cut cards)

1. **The plateau is pool-bound (Will's memory call).** The oracle's 93 column holds
   58-80 % of its peak coverage from Act+2.8 to 4.4; fable's holds ~15-25 %
   (`feed/bands_b1121_93.txt` vs `bands_oracle_93.txt`). Holding the peak's population
   for record 0 needs ~1067/s of sustain, i.e. ~3900 live per landing: a pool of ~8 k
   (B1120 FOLLOW-UP 1 asks the same for coverage).
2. **The end shape per record.** 93/38 end in ~0.16 s (a hard cut), but fable fades
   over ~1.7 s of particle life. 41/253 fade like fable. 54/656/458 step down at
   ~Act+3.4. A per-component end (a sim "effect kill" at `emitterLifetime`) and a
   component split for record 23/33 would match these. Not done: the end time matches
   without it, and a hard cut is new behaviour, not a duration.
3. **Per-record onset** (B1120 FOLLOW-UP 6) is still open: 253's oracle onset is 0.80,
   fable's 0.52.
4. **`SpellEffectsConfig::setDefaults`** still describes the pre-T422 one-shot disc
   (B1119 FOLLOW-UP 4); it has no sustain.
