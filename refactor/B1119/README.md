# B1119 (#439): spell sprite SIZE (B1118 M1)

First fix card of the B1118 chain (B1119 size -> B1120 volume -> B1121 duration ->
B1122 source emitter -> B1123 overrides -> B1124 atlas). Scope: sprite size only.
Evidence: `/mnt/projects/fable/review/B1119/` (index: its `README.md`).

## 0. Result

- **The sizes changed.** Every spell sprite is now 2.1-4.2x larger at the Landing and
  4.4-22.6x larger at the Source than at fable d022f1db. Each size was set by a live
  measurement, not a desk estimate.
- **The B1118 gate number did NOT land for 13 of 14 rows, and size alone cannot make it
  land.** On the full-population tour, fable's sprites overlap: the landing disc merges into
  one blob, and the source glow merges at the hand bones. The ~100 small background blobs
  per frame then set the whole-frame p90 that `fx_blobs.py` reports. That number is no
  longer a sprite size; it is a population and volume number. Measured proof: raising the
  landing sizes 3.5x **lowered** that p90 (calibration v2, §3).
- **The sprite-separable reading is in band for 9 of 14 rows on the gate capture** (§4).
  It uses the same mask, restricted to the caster ROI, round blobs only, and pooled over the
  sample window. The rest:
  - 93 and 41 Source: hidden behind the spawned NPC / merged into one spray stream.
  - 253 Source: merged at the hand.
  - 93 and 38 Landing: about 10 % over the band.

  The B1118 p90 rows will only read sprite size once B1120 (volume) and B1122 (body-wide
  source emitter) spread the sprites the way the oracle's are spread.
- **Coverage and duration moved, towards the oracle.** That is a side effect of larger
  sprites, not a fix of M2/M3:
  - Landing coverage went from 0.12-0.26 to 0.51-1.12 model². The oracle is 1.2-7.3.
  - Landing duration went from 1.57-1.90 s to 1.87-2.17 s. The oracle is 4.3-5.8 s.
  - Source is now above threshold for 38/41/253.

  Both are still far from the oracle. They remain B1120/B1121's.

## 1. Mechanism (B1118 §5 M1)

- Sizes come only from `config/spell_effects.json`, never from spells.eff:
  - `castGlow`/`castSpray` (Source)
  - `spellComplete`/`impact` (Landing)
  - per-spell `spellOverrides`
- A point sprite is `size x screenH / clip.w` px, clamped to 1-100
  (`src/client/graphics/environment/unified_particle_renderer.cpp:58-69`).
  - The caster's own height in px scales with the same `screenH / clip.w`.
  - So a sprite's diameter **in caster heights is independent of resolution and camera
    distance**, except where the clamp binds.

**Why the T422 sizes were low.**
- T422 probed sizes on a capture that held 4.4-5.8 renders/s (llvmpipe era). It matched a
  *median* sprite diameter in px against an oracle frame at a different scale.
- B1118 re-measured both clients at real frame rate, at matched scale (caster heights). It
  found fable's p90 2-4x small at the Landing and 2-6x small at the Source.

## 2. The change (`config/spell_effects.json`)

Factors are relative to the sizes at d022f1db. All four size keys are scaled together.

| what | before (start -> end) | after | factor |
|---|---|---|---|
| `spellComplete` (style) | 0.095-0.157 -> 0.19-0.28 | 0.266-0.44 -> 0.532-0.784 | x2.8 |
| `impact` (style) | 0.08-0.125 -> 0.157-0.235 | 0.224-0.35 -> 0.44-0.658 | x2.8 |
| `castGlow` (style) | 0.08-0.12 -> 0.04-0.06 | 0.824-1.236 -> 0.412-0.618 | x10.3 |
| `castSpray` (style) | 0.04-0.08 -> 0.02-0.04 | 0.904-1.808 -> 0.452-0.904 | x22.6 |
| override 38 landing | (style) | style x0.74 | x2.07 |
| override 54 landing | (style) | style x1.18 | x3.3 |
| override 41 landing | (style; spawnRate kept) | style x1.51 | x4.23 |
| override 54/656 castGlow | (style) | 0.36-0.54 -> 0.18-0.27 | x4.5 |
| override 458 castGlow | (style) | 0.44-0.66 -> 0.22-0.33 | x5.5 |
| override 253 landing | T422 sizes (style x1.673) | **size keys removed**, so it resolves to the style | style x2.8 = T422 override x1.67 |

**Why per-spell overrides exist.** Every size override is forced by B1118's oracle table:
- Landing: 38 (0.058) and 93 (0.074) are the same record-0 sprites, yet need sizes ~20 %
  apart.
- Source: 656 (0.031) and 253 (0.075) share `castGlow`.
- Landing: 41 (0.110) is 1.5x every other spell.

**The 93/41/253 overrides:**
- 93/41 keep their T422 populations (B1123's to re-measure).
- 253 keeps its population and duration. Its size override is retired, because the
  oracle's 0.059 lands on the new style.

The calibration law (measured, §3):
- An isolated sprite's lit p90 diameter is linear in `size`.
- Landing: ~0.028 caster heights per x1 of the old sizes (0.019-0.031 per texture).
- castGlow: ~0.0071 per x1. 253 spela2 stars and 458 SoftCircles agree within 6 %.
- castSpray is derived from castGlow by the sample-time size ratio. It is not separately
  measurable, because its sprites stream into one blob.

## 3. Calibration (ThinkPad lane 5, real GL at 60 fps, x11grab 30 fps)

All of these are `review/B1119/cal/`, one wizard leg with all 7 casts.

| run | config | finding |
|---|---|---|
| v1 | landing x1.87, castGlow x4 (54/656 x1.65, 458 x2.7), castSpray x5.8, 41 x4.1, 253 x2.7 of T422 | B1118 p90 rose only 1.1-1.6x for 1.87x size |
| v2 | landing x3.5, castGlow x12, castSpray x15 ... | B1118 p90 **fell** (93 Landing 0.043 -> 0.024): the disc became one 39 000 px blob |
| v3 | v1 sizes, emitters thinned to separable (calibration only) | ROI sprite p90 linear in size |
| v4 | candidate sizes, thinned | confirms the linear law; final factors interpolated from v3/v4 |

`scripts/fx_sprite_roi.py` is the separable reading. It uses the B1118 mask (rise > 80, 8-connected, ≥ 2 px) with four restrictions:
- **ROI:** only the caster ROI (±1.6 model around the caster, below the far NPC, left of
  the tip window).
- **Shape:** only round blobs (fill ≥ 0.25 of the bbox, aspect ≤ 2.2), which drops
  model-animation edges.
- **Landing baseline:** the Landing is measured against `fx_timing.py`'s tail baseline.
- **Pooling:** the result is pooled over Source BC+0.3..Act-0.05 / Landing Act+1.2..1.8.

## 4. Before / after (caster heights; oracle = B1118, band ±15 %)

Reading notes:
- "B1118 p90" is the gate metric (`fx_blobs.py`, one frame, whole crop). "ROI p90" is the
  separable reading on the same gate capture.
- B1118 fable is c3275a16. B1119 is this branch built Release on the ThinkPad. Its gate
  capture is the B1118 A/B/C legs (wizard / druid / enchanter).

| spell | stage | oracle | band | fable B1118 | B1119 B1118-p90 | in band | B1119 ROI p90 | in band |
|---|---|---|---|---|---|---|---|---|
| 93 | Landing | 0.074 | 0.063-0.085 | 0.039 | 0.043 | no | 0.093 | no (+9 % over) |
| 93 | Source | 0.070 | 0.060-0.081 | 0.019 | 0.028 | no | 0.040 | no (hands hidden by the spawned NPC) |
| 38 | Landing | 0.058 | 0.049-0.067 | 0.035 | 0.054 | **yes** | 0.073 | no (+9 % over) |
| 38 | Source | 0.077 | 0.065-0.089 | 0.018 | 0.025 | no | 0.067 | **yes** |
| 54 | Landing | 0.064 | 0.054-0.074 | 0.030 | 0.035 | no | 0.055 | **yes** |
| 54 | Source | 0.032 | 0.027-0.037 | 0.019 | 0.021 | no | 0.032 | **yes** |
| 656 | Landing | 0.056 | 0.048-0.064 | 0.026 | 0.034 | no | 0.051 | **yes** |
| 656 | Source | 0.031 | 0.026-0.036 | 0.019 | 0.022 | no | 0.035 | **yes** |
| 41 | Landing | 0.110 | 0.094-0.127 | 0.027 | **0.022** (merged) | no | 0.099 | **yes** |
| 41 | Source | 0.104 | 0.088-0.120 | 0.018 | 0.021 | no | 0.208 | no (spray merges into one stream) |
| 253 | Landing | 0.059 | 0.050-0.068 | 0.022 | 0.032 | no | 0.052 | **yes** |
| 253 | Source | 0.075 | 0.064-0.086 | 0.017 | 0.023 | no | 0.172 | no (24/s at r=0.4 merge at the hand) |
| 458 | Landing | 0.067 | 0.057-0.077 | 0.040 | 0.046 | no | 0.074 | **yes** |
| 458 | Source | 0.038 | 0.032-0.044 | 0.014 | 0.024 | no | 0.041 | **yes** |

## 5. The other B1118 rows (gate capture vs B1118 fable)

Full rows: `review/B1119/gate_table.tsv`.

- **Landing coverage (model²).** Up on every spell, towards the oracle:

  | spell | 93 | 38 | 54 | 656 | 41 | 458 | 253 |
  |---|---|---|---|---|---|---|---|
  | B1118 fable | 0.160 | 0.169 | 0.117 | 0.116 | 0.128 | 0.258 | ≤ 0.033 |
  | B1119 | 0.774 | 0.524 | 0.633 | 0.507 | 1.115 | 0.914 | ≤ 0.063 (still below threshold; population is M5) |

- **Landing end and duration.** Ends moved to Act+2.35-2.53 s (were 2.20-2.32). Durations
  are 1.87-2.17 s (were 1.57-1.90). Longer, towards the oracle's 4.3-5.8 s.
- **Landing onset.** Oracle +0.40-0.80 s.
  - Five moved closer to the oracle:
    - 93 0.419 -> 0.545 (oracle 0.547)
    - 38 0.427 -> 0.606 (0.530)
    - 54 0.619 -> 0.340 (0.423)
    - 656 0.601 -> 0.366 (0.404)
    - 41 0.680 -> 0.359 (0.424)
  - **458 moved away: 0.296 -> 0.254 (oracle 0.422), 42 ms ≈ 1 frame earlier.** This is a
    regression, and it is recorded here. A larger sprite crosses the threshold one frame
    sooner. The onset itself is the emitter start, which B1122/B1121 own.
- **Landing colour (hue/sat).**
  - Towards the oracle:
    - 93 26 -> 39 (oracle 46)
    - 38 32 -> 36 (47)
    - 41 140/0.20 -> 64/0.73 (72/0.72)
    - 253 207 -> 208 (213)
  - **Away from the oracle (small):**
    - 54/656 217 -> 213 (oracle 243; sprite family is M7)
    - 458 sat 0.13 -> 0.04 (oracle 0.51; white SoftCircle is M6). Larger additive white
      sprites wash out further.
- **Source.** 38/41/253 are now above threshold. Their onset is BC+0.135-0.175 s, against
  the oracle's BC+0.20-0.45. Before, there was no measurable Source onset, so this is a
  newly visible divergence, not a regression of a matched row. B1122 (FX-SOURCE) already
  carries the oracle onset target. Source hues now read the sprite (93 37, 38 35, 41 66,
  253 200; oracle 36/37/76/214), not the background.

## 6. Clamp and point size

- **1280x800, B1118 chase camera (clip.w ≈ 32).** The largest size (castSpray start
  1.808) draws at ~45 px. Landing sizes draw at ≤ ~20 px. The 100 px clamp does not bind.
  `test_spell_colour_and_overrides.cpp` pins this for clip.w ≥ 20.
- **When the clamp binds.** It binds for castSpray/castGlow when clip.w < 14.5 / 9.9, which
  is first person or a camera closer than ~15 units. Hand-bone sprites are then held at
  100 px. Both clients' Source clouds are body-sized at that distance, so the clamp is not
  a target concern here.
- **800x600.** The px are 0.75x and the caster-height ratio is unchanged, so the same
  sizes hold.

**Orange Pi note (not measured on a Pi):**
- Additive fill per landing sprite grows with size². That is x7.8 for the landing styles,
  x17.9 for 41, and x106 / x511 per sprite for castGlow / castSpray, but those are only
  ~50 sprites at the hands.
- At the chase camera on 800x600, a 648-sprite landing is ~50 k blended px (≈ 0.1 screen).
- At a first-person distance (clip.w ≈ 5) the same landing is ~1.8 M px (≈ 4 screens),
  and the hand glow hits the 100 px clamp (~0.5 M px).
- That is a real Mali-400 fill cost. Measure it on lane 1 before B1120 adds volume.

## 7. Tests (same commit)

- `test_spell_burst_oracle_match`: `SpriteSizeAndExpandSpeedAreTheMeasuredOnes` re-pinned to
  the B1119 impact and spellComplete sizes. The T422 SIZE narrative is marked superseded.
- `test_spell_colour_and_overrides`:
  - `WhirlingWindSpritesAreTheLargerMeasuredOnes` became
    `WhirlingWindSpritesComeFromTheB1119Style`.
  - `AnUnmeasuredSpellStillResolvesToTheStyle` uses 656/285/1 (54 is now measured).
  - New: `B1119LandingStylesAreTwoPointEightTimesT422`,
    `B1119LandingOverridesFollowTheOracleTable`, `B1119SourceSizesFollowTheOracleTable`,
    `B1119SizesDoNotHitThePointClampAtTheChaseCamera`.

## 8. FOLLOW-UP (for the orchestrator; workers do not cut cards)

1. **The B1118 p90 gate is not a size metric on a dense effect.** Re-read the size rows
   with `fx_sprite_roi.py`, or with the oracle measured the same way, after B1120/B1122
   spread the sprites. Until then the B1118 p90 rows of every chain card will not be
   meaningful for fable.
2. **Oracle-side ROI p90.** The oracle targets are whole-crop single-frame p90s. Re-reading
   them with the ROI method (regen `review/B1118/measure/oracle_*` via `fx_regen.py`) would
   make the target and the fable reading the same instrument.
3. **93/38 Source** cannot be read in this tour: fable's hand sprites sit behind the
   `#spawn`ed NPC. A tour that spawns the NPC off-axis would read it.
4. **The hardcoded fallback styles** in `spell_effects_config.cpp` (`setDefaults`, castSpray
   ... groundCircle) still carry pre-T422 sizes. They are only used when the JSON is
   missing, but they are stale.
5. **Orange Pi fill cost** of the new sizes (§6) on lane 1, first-person and chase.
6. **458 landing onset** is 1 frame earlier, and 54/656/458 hue/sat moved slightly (§5).
   These are owned by B1121/B1122 (emitter timing) and M6/M7 (sprite family).
