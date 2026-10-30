# B1150 (#470): FX-SIM-2 spawn geometry from spellsnew.edd

Third wiring step of B1147 (#467), after B1149 (fable `aa2c64f7`). The client now
spawns the official client's TEN `spellsnew.edd` +0xA8 shapes for every spell
with a field-145 record; the config shapes are retired to test bands.

## The rules (already traced in B1141/B1151 — cited, not re-derived)

`review/B1141/edd_fields_from_code.tsv` rows +0x0a8..+0x0d0, +0x19c
(PROVEN-BY-CODE): shape enum +0xA8; dims +0xAC/+0xB0/+0xB4; spawn OFFSETS
+0xB8/+0xBC/+0xC0 (along the emitter's three basis axes — B1137's "gravity"
reading was wrong); tilts +0xC4/+0xC8 (EQ 512ths, wrapping: -8000 -> 192);
depth bias +0xD0; scale flag +0x19C.

- The shape switch reads `+0xA8` at `10061753 mov eax,[edi+0xa8]`
  (`.dis` line 138103), jumping through the 14-entry table at `0x1006408c`.
- Case addresses (each case cites it in `spell_spawn_shape.h`):
  0 point `0x10061769`; 1 even ring `0x10061779`; 2 stacked rings
  `0x10061826` (count table `0x10061002..0x10061155`); 3 cylinder
  `0x10061905`; 4 filled disc `0x100619d1`; 5 sphere `0x10061acb`
  (B4==0 fallback `LAB_10061dd3`, decompile lines 68860-68906); 6 box
  `0x10061be3`; 7 line/cone `0x10061d9c` (B0 != 0 alternate `0x10061de1`);
  8 torus `0x10061e7d` (B0 != 0 alternate `0x10061f07`, the `jp` target at 0x10061ed5); 9 ring random
  angle `0x10061fa5`.
- `+0xB0 == 0.0` means "same as +0xAC" (circle/sphere, not ellipse).
- Offsets are applied along the PRE-tilt basis, times s19c
  (`0x100612b9..0x100613f7`).
- Tilt A `0x100613fd..0x100615aa`: `b1' = c*b1 + s*b2`, `b2' = c*b2 - s*b1`
  on (b1,b2); tilt B `0x100615ab..0x10061752`: the same on (b1,b3) using b1'.
  Tilt values are EQ 512ths of a full turn.
- Depth bias: the renderer subtracts +0xD0 from the view depth before the
  near/far cull and divide (`0x10063069` / `0x10063198`).
- +0x19C == 1 -> s19c = the attach scale (1 on all 310 player records)
  multiplies r/h and the offsets; +0x19C == 0 -> the attach matrix is
  normalised (scale 1.0). B1149 traced the attach scale.
- Axis mapping (decompile lines 69340-69380): the client's point is
  `height*axis1 + cos(theta)*r*axis2 + sin(theta)*r*axis3` — the shape's
  z (height) rides the emitter's axis 1 (world up for a level actor), its
  x (cos*r) axis 2 (facing), its y (sin*r) axis 3 (perpendicular). Fable's
  `computeEddShapePoint` returns the point in that abstract frame
  (b1=height, b2=cos*r, b3=sin*r); `sim_particles.cpp` maps b1->world up,
  b2->facing, b3->perpendicular.
- Shape 7: ONE u1 (1000-table) drives both r = u1*AC and height = u1*B4; an
  independent u2 (512-table) is the angle; the B0 <= 0 path is the main one
  (decompile 68961-68967: `if (D_B0 == 0.0)` -> the line/cone, `goto
  LAB_10061dd3`). The B0 != 0 branch (68970-68986) remaps the angle through
  the ellipse (atan2 at `+0x18`) and scales by the same u1 — implemented as a
  scaled ellipse; **no gate record uses shape 7 at all** (census below).
- Shape 8: the tube cross-section is `r = AC + cos(phi)*B4`,
  `height = sin(phi)*B4` (decompile lines 68988-68998: `fStack_44c` is the
  tracked +8 draw = sin; `unaff_ESI` is the untracked +0xc draw = cos — the
  same +8/+0xc sin/cos pair the tilt code uses). A standard torus, major AC,
  minor B4. The B0 != 0 alternate (69000-69017) is a flattened variant
  (ellipse major circle + the same tube); implemented approximated.
- The RNG tables (1000-table `0x1005d400`, 512-table `0x1005d3e0`) are
  uniform draws; fable draws `particleRandomFloat(0,1)` — equivalent
  distributions, tables not emulated.

## The census

Shape distribution over ALL 2118 file entries of
`/mnt/projects/EverQuestP1999/spellsnew.edd` (8-byte magic + 2118 x 416-byte
entries; regenerated with `refactor/B1150/scripts/edd_shapes.py` (the +0xA8 census in edd_shapes.tsv's header)
or the reader test `SpellSpawnShapesShipped.CensusOfTheShapeWord`, which
decodes the shipped file):

```
0:297 1:202 2:13 3:125 4:275 5:808 6:5 7:9 8:18 9:366  (sum 2118)
```

Note: the file is 0-based (2118 entries, 0..2117); `spellsnew.eff` slot
numbers are 1-based (slot 0 = empty, so the reader's `shape()`/`timing()`/
`sprite()` reject 0). The census counts every entry including 0 (a shape-0
point — entry 0 is the 297th), which is why the reader gained a zero-based
`shapeAt(size_t)` accessor for the census test.

The 40 gate records' slot rows (299 rows) use only shapes
{0:24, 1:5, 3:1, 4:38, 5:127, 8:2, 9:102} — shapes 2, 6 and 7 are absent from
the gate (they are covered by unit tests only).

## What changed

- `include/client/spell/spells_new_edd_reader.{h,cpp}` — `SpellsNewEddShape`
  (+0xA8 int, +0xAC/B0/B4, +0xB8/BC/C0, +0xC4/C8, +0xD0 floats, +0x19C int)
  decoded beside B1149's timing/sprite words; `shape()` (1-based slot, like
  B1149) and `shapeAt()` (0-based, census).
- `include/client/graphics/environment/spell_spawn_shape.h` — the ten
  official shape cases (each with its Render address), the B0==0->AC rule,
  the pre-tilt offsets, the two tilts, the +0x19C scale. Replaces fable's
  five home-made `SpawnShape`s for record spells. Fire/weather emitters keep
  the old enum through `spell_emitter_presets.cpp` (map, do not break).
- `src/client/graphics/sim_particles.cpp` — `cfg.eddShape.active` branch
  (precedes every config shape read): computes the record point in the
  emitter frame (b1 up, b2 facing, b3 perpendicular) and stores the +0xD0
  depth bias; `config` spawnShape/spawnExtents are NOT read for a record
  emitter.
- `src/client/graphics/environment/spell_emitter_presets.cpp` —
  `applySlotEmitter(const SpellsNewEddShape*)` carries the record's geometry
  (and B1149's populations/sprite sizes) into `cfg.eddShape`;
  `TargetAnchorForSpell` returns the record's anchor directly (decoupled
  from `hasShape`).
- `config/spell_effects.json` — the record spells' shape words are RETIRED
  (the extent words `lo`/`hi`/`halfWidth`/`centroid` stripped from 41 source rows + 40 target
  rows, and the `spawnShape`/`spawnExtents` lines from the 30 overrides, by
  `scripts/retire_shapes.py`, idempotent). Their measured
  `lo/hi/halfWidth/centroid` rows survive as BANDS in
  `tests/spell_fx_shape_oracle.h` (generated by
  `scripts/gen_shape_bands.py` over the pre-retirement config) — the gate
  compares the harness rows against those bands, not against inputs.
- `refactor/B1150/edd_shapes.tsv` (committed) — the 299 S+T slot rows of
  the 40 gate records (the sweep's records; the B1134 GM-path source
  record 51 is outside the sweep), generated here by
  `python3 refactor/B1150/scripts/edd_shapes.py > refactor/B1150/edd_shapes.tsv`
  (reads `/mnt/projects/EverQuestP1999/{spellsnew.edd,spellsnew.eff}`; the
  header lines carry the file sha256 and the full-file +0xA8 census).
  298 of the 299 slots carry `s19c = 1`; the one `s19c = 0` is record 30's
  (231) T slot 2 — an empty shape-0 point (all words 0, `nul` texture), so
  no attached scale exists there.
- `refactor/B1150/shape_replacement_table.md` (committed) — the per-row
  replacement: the retired band on the left, the EDD shape it now draws on
  the right. Regenerated by
  `python3 refactor/B1150/scripts/join_shapes.py /home/user/src/willeq-wt/kb-b1150-shapes 9aa72ed5`
  (9aa72ed5 = the pre-retirement head, whose config still carries the words
  being retired; HEAD is the wrong input). Re-run in this leg:
  byte-identical table (diff of the first 46 lines is empty).
- The CI full-suite run (pipeline 2116) surfaced two tests that still
  stated the PRE-retirement rule -- the first leg's broad ctest regex
  (`Spell|Bridge|Particle|FxHarness|ConfigKeyReachability|Oracle`) did not
  match either suite, so they never ran here. Both are re-stated to the new
  rule, not weakened:
  - `tests/test_spell_source_anchors.cpp` — `SourceAnchors.EachRecordSourceIsTheStyleCloudOnTheCaster`
    bound the no-file record Source's pooled lateral spread at `dx/n <= 0.5`;
    with the rows' shape words retired, every no-file record resolves to the
    Body anchor (spell_emitter_presets.cpp:296-299, hasShape false on all 41
    rows), so the cloud is the castGlow style cloud itself, whose pooled
    |dx| is 0.6655 caster heights (the unseeded CI runner measured 0.53-0.71;
    the test now seeds the particle RNG, B1138 hook,
    test_spell_source_anchors.cpp:292 `pi.rngSeed = 1150u;`) and pins the
    seeded pooled means (test_spell_source_anchors.cpp:388-389, 392-393:
    `EXPECT_NEAR(dx / n, 0.6655f, 0.01f)`, `EXPECT_NEAR(dz / n, 0.3173f,
    0.01f)`, `EXPECT_GT(dy / n, 0.2f)`, `EXPECT_NEAR(dy / n, 0.9775f, 0.01f)`
    -- measured 2026-10-25, x86 ThinkPad, 380 pooled particles per record),
    and re-pins the placement structurally: the pooled window's position
    requests are the caster's entity only, zero hand/head bone requests
    (test_spell_source_anchors.cpp:383-387).
  - `tests/test_spell_slot_rules.cpp` — `SlotRules.AnOverridesOwnGeometryStaysOnTheEntity`
    stated the B1122 rule "an override's own measured geometry stays on the
    entity" (spell_visual_fx.cpp's `ownShape` = `hasSourceShapeOverride`).
    The retirement removed 54's shape words, so the slots draw where the
    file puts them (eqgame 0x495cc3, docs/spellsnew_format.md): the test is
    now `SlotRules.ARetiredOverrideSpellDrawsItsFileSlotsOnTheirAttachPoints`
    (tests/test_spell_slot_rules.cpp:528): record 82 (ElemMastery V2) draws
    its level-1-gate Source at the two minLevel-0 hands (test:510
    `EXPECT_EQ(Points(Created()), (std::vector<uint16_t>{kL, kR}))`), and a
    level-60 landing draws the target's chest/chest/default slots on the
    target's entity and its head slot on its head bone (test:514-517,
    8 commands = the two halves per drawn slot). The record's words,
    regenerated by `python3 refactor/B1150/scripts/eff_record.py 82`:
    S slots 1-2 em292 minLevel 0 attach lhand/rhand (slots 0/3 need levels
    39/24); T slots 0-3 em294/295/296/297, minLevel 0/39/24/24, attach
    chest/chest/default/head.

## The gate

Run on the ThinkPad (`refactor/B1150/scripts/`, surfaceless 1280x800 via
`WILLEQ_DRM_DEVICE=/dev/dri/no-such-card` in `run_scene.sh`; the b1118
camera rig is calibrated to that mode, B1139):

```bash
# tour-7 (7 runs, ~15 min):
refactor/B1150/scripts/tour7.sh <tag>
# the 40-record sweep (~2 h):
refactor/B1150/scripts/run_sweep.sh <tag>
```

Both land per-run TSVs under `refactor/B1150/plans/`, measured by
`review/B1139/scripts/harness_measure.py`.

### Completion and collection (2026-10-25)

- Launched 18:46:14Z (`gate_run.log` line 1). Tour-7 done at 19:01Z
  (`TOUR7_EXIT=0`); the 40-record sweep done 20:11:57Z — the last
  `progress.log` line, `measure b1150_r26_310 rc=0` — and `gate_run.log`
  ends 20:11:58Z with `sweep b1150 done` / `SWEEP_EXIT=0`.
- The `SWEEP_EXIT` *file* was never written — the wrapper echoed the exit
  code into the log without persisting the file — and a `Terminated` line
  in `gate_run.log` shows the nohup wrapper's process group was signaled
  after the tour phase; `run_sweep.sh` itself ran to completion. The
  completion record is `progress.log`: 96 lines = 48 runs x (render rc=0 +
  measure rc=0), zero failures. Not relaunched.
- Collected to this host (no NFS here):
  `rsync -a thinkpad:/mnt/projects/fable/refactor/B1150/runs/ /home/user/kb-evidence/kb-b1150-shapes/runs/`
  — 48 run dirs (40 `b1150_r<row>_<spell>` + 7 `b1150_tour_<spell>` +
  `smoke_93`), per-run harness row at `runs/<n>/measure/summary.tsv`
  (columns: `review/B1132/scripts/b1123_summary.py` docstring), plus
  `gate_run.log` / `progress.log` beside `runs/`.
- The three tables below are regenerated verbatim by
  `python3 refactor/B1150/scripts/gate_table.py /home/user/kb-evidence/kb-b1150-shapes/runs`
  (all inputs in its docstring: the runs dir, the worktree's
  `refactor/B1150/edd_shapes.tsv`, `tests/spell_fx_shape_oracle.h`, and the
  baselines `review/B1130/record_table.tsv`, `review/B1149/sweep_table.tsv`,
  `review/B1142/gate/regression_table.tsv`). The retired bands are
  `tests/spell_fx_shape_oracle.h`, generated by
  `python3 refactor/B1150/scripts/gen_shape_bands.py <pre-retirement config/spell_effects.json>`
  (run against the pre-retirement config, e.g.
  `git show 9aa72ed5:config/spell_effects.json`, so the header pins the
  retired values).

# runs: sweep 40 (want 40), tour [38, 41, 54, 93, 253, 458, 656] (want 7 tour spells)

### Sweep: the Source stage -- the harness vs the retired band (per row)

`src_cov/src_ext/src_cen` are the harness PEAK (summary.tsv, b1123_summary.py);
the band is the retired row (tests/spell_fx_shape_oracle.h): `lo..hi` in caster heights,
`cen` the measured centroid. `cen` verdict: in = src_cen within [lo, hi], short/over = below/above.
`ext x` = src_ext / (hi - lo). `d vs B1149` = the harness src_cov minus B1149's.

| row | spell | band lo..hi / cen | src_cov | src_ext | src_cen | cen | ext x | d vs B1149 |
|---|---|---|---|---|---|---|---|---|
| 0 | 93 | 0.07..1.25 / 0.71 | 0.263 | 1.74 | 0.74 | in | 1.47 | +0.039 |
| 1 | 200 | 0.06..1.17 / 0.48 | 0.241 | 1.55 | 0.63 | in | 1.40 | +0.004 |
| 2 | 202 | 0.3..0.66 / 0.48 | 0.081 | 0.36 | 0.94 | over | 1.00 | -0.068 |
| 3 | 377 | 0.28..0.99 / 0.64 | 0.070 | 0.57 | 0.92 | in | 0.80 | +0.018 |
| 4 | 50 | 0.1..1.86 / 0.82 | 0.821 | 4.38 | 0.74 | in | 2.49 | -1.777 |
| 5 | 80 | 0.26..1.09 / 0.7 | 0.146 | 0.69 | 0.92 | in | 0.83 | +0.012 |
| 6 | 722 | 0.09..1.03 / 0.57 | 0.297 | 1.23 | 0.64 | in | 1.31 | -0.075 |
| 7 | 40 | 0.41..1.55 / 0.9 | 0.261 | 0.62 | 0.84 | in | 0.54 | -0.183 |
| 8 | 346 | 0.06..1.06 / 0.49 | 0.567 | 1.53 | 0.78 | in | 1.53 | +0.005 |
| 9 | 375 | 0.1..1.02 / 0.56 | 0.754 | 1.95 | 0.72 | in | 2.12 | +0.055 |
| 10 | 590 | 0.12..1.86 / 0.79 | 0.817 | 1.55 | 0.88 | in | 0.89 | -0.508 |
| 11 | 285 | 0.1..0.86 / 0.58 | 0.097 | 1.52 | 0.73 | in | 2.00 | -0.006 |
| 12 | 208 | 0.13..1.07 / 0.64 | 1.534 | 1.72 | 0.67 | in | 1.83 | +0.203 |
| 13 | 209 | 0.21..1.96 / 0.91 | 0.605 | 2.66 | 0.94 | in | 1.52 | -0.080 |
| 14 | 373 | 0.09..0.71 / 0.51 | 0.066 | 0.66 | 0.83 | over | 1.06 | +0.006 |
| 15 | 289 | 0.1..1.34 / 0.67 | 0.809 | 1.36 | 0.78 | in | 1.10 | +0.042 |
| 16 | 242 | 0.24..0.81 / 0.51 | 0.517 | 3.21 | 0.34 | in | 5.63 | -0.053 |
| 17 | 237 | 0.1..1.51 / 0.68 | 0.576 | 1.81 | 0.76 | in | 1.28 | -0.227 |
| 18 | 378 | 0.16..1.08 / 0.63 | 0.219 | 1.26 | 0.82 | in | 1.37 | -0.084 |
| 19 | 359 | 0.12..1.53 / 0.76 | 1.134 | 1.82 | 0.85 | in | 1.29 | -0.030 |
| 20 | 341 | 0.07..1.71 / 0.88 | 1.116 | 1.73 | 0.79 | in | 1.05 | +0.062 |
| 21 | 218 | 0.04..1.73 / 0.72 | 0.710 | 2.29 | 0.87 | in | 1.36 | -0.340 |
| 22 | 201 | 0.03..1.09 / 0.68 | 0.195 | 0.33 | 0.91 | in | 0.31 | -0.153 |
| 23 | 372 | 0.08..1.26 / 0.61 | 0.402 | 1.40 | 0.88 | in | 1.19 | -0.008 |
| 24 | 348 | 0.05..0.84 / 0.39 | 0.839 | 3.31 | 0.43 | in | 4.19 | -0.198 |
| 25 | 14 | 0.08..1.56 / 0.83 | 0.403 | 1.67 | 0.99 | in | 1.13 | +0.015 |
| 26 | 310 | 0.14..1.25 / 0.62 | 0.489 | 1.87 | 0.92 | in | 1.68 | -0.129 |
| 27 | 290 | 0.22..1.27 / 0.54 | 0.138 | 1.17 | 0.79 | in | 1.11 | -0.060 |
| 28 | 73 | 0.07..1.47 / 0.72 | 1.335 | 3.77 | 0.63 | in | 2.69 | +0.206 |
| 29 | 286 | 0.22..1.1 / 0.69 | 0.352 | 1.55 | 0.65 | in | 1.76 | +0.134 |
| 30 | 231 | 0.07..1.47 / 0.69 | 0.098 | 1.51 | 0.99 | in | 1.08 | +0.006 |
| 31 | 313 | 0.02..1.13 / 0.41 | 0.867 | 1.57 | 0.40 | in | 1.41 | +0.024 |
| 32 | 374 | 0.01..0.68 / 0.26 | 0.814 | 1.49 | 0.53 | in | 2.22 | -0.060 |
| 33 | 382 | 0.17..0.84 / 0.51 | 0.046 | 0.58 | 0.67 | in | 0.87 | -0.002 |
| 34 | 340 | 0.02..1.27 / 0.52 | 0.383 | 1.48 | 0.69 | in | 1.18 | -0.114 |
| 35 | 253 | 0.31..2.19 / 1.36 | 3.388 | 2.61 | 1.13 | in | 1.39 | -3.415 |
| 40 | 602 | 0.14..1.33 / 0.66 | 1.056 | 1.83 | 0.78 | in | 1.54 | -0.481 |
| 41 | 703 | 0.17..1.5 / 0.79 | 0.164 | 1.35 | 0.81 | in | 1.02 | -0.027 |
| 42 | 700 | 0.35..1.7 / 0.75 | 0.074 | 1.96 | 1.03 | in | 1.45 | -0.030 |
| 43 | 720 | 0.18..1.64 / 0.74 | 0.255 | 1.89 | 0.83 | in | 1.29 | -0.157 |

### Sweep: the landing -- the harness vs the B1130 oracle, and vs the B1149 sweep

`oracle` = review/B1130/record_table.tsv (fixed camera); `ratio` is reported as measured,
not a pass/fail band (B1149's instrument note: the harness geometry differs from the lane).
`d vs B1149` = the harness land_cov minus review/B1149/sweep_table.tsv's; the spread rule is
the p25..p75 of those deltas (below p25 = `under`, explained per row in the notes;
above p75 = `above`, an improvement).

| row | spell | T shape | oracle cov | harness cov | ratio | oracle end | harness end | d end | B1149 cov | d vs B1149 | spread |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 93 | 5/5/0/5 | 3.945 | 2.742 | 0.70 | +6.67 | +6.433 | -0.24 | 4.233 | -1.491 | under |
| 1 | 200 | 5/9/5/9 | 3.050 | 1.447 | 0.47 | +6.58 | +6.433 | -0.15 | 1.598 | -0.151 | in |
| 2 | 202 | 5/5/5/5 | 2.250 | 0.649 | 0.29 | +5.48 | +5.167 | -0.31 | 0.865 | -0.216 | in |
| 3 | 377 | 5/4/4 | 11.688 | 9.303 | 0.80 | +9.00 | +6.433 | -2.57 | 8.053 | +1.250 | above |
| 4 | 50 | 5/9/9/0 | 3.250 | 1.467 | 0.45 | +6.78 | +6.533 | -0.25 | 3.297 | -1.830 | under |
| 5 | 80 | 9/5 | 0.559 | 0.260 | 0.47 | +4.53 | +4.300 | -0.23 | 0.224 | +0.036 | above |
| 6 | 722 | 9/9/5/5/9/9/5/5 | 0.762 | 0.707 | 0.93 | +6.26 | +6.433 | +0.17 | 0.809 | -0.102 | in |
| 7 | 40 | 1/9/5/9 | 3.077 | 2.455 | 0.80 | +6.81 | +6.667 | -0.14 | 2.603 | -0.148 | in |
| 8 | 346 | 9/5 | 6.271 | 3.477 | 0.55 | +3.78 | +3.867 | +0.09 | 3.397 | +0.080 | above |
| 9 | 375 | 1/5/5/9 | 1.999 | 1.353 | 0.68 | +7.64 | +7.533 | -0.11 | 1.338 | +0.015 | in |
| 10 | 590 | 5/9/5/0 | 5.476 | 4.469 | 0.82 | +5.72 | +6.867 | +1.15 | 4.5 | -0.031 | in |
| 11 | 285 | 4/5/9/9 | 0.997 | 0.467 | 0.47 | +5.51 | +5.533 | +0.02 | 0.52 | -0.053 | in |
| 12 | 208 | 9/0/9/9 | 5.788 | 4.160 | 0.72 | +7.04 | +7.767 | +0.73 | 4.346 | -0.186 | in |
| 13 | 209 | 9/9/9/4 | 3.264 | 2.016 | 0.62 | +6.39 | +6.233 | -0.16 | 2.211 | -0.195 | in |
| 14 | 373 | 9/4/9/9 | 3.995 | 5.168 | 1.29 | +8.60 | +8.500 | -0.10 | 5.278 | -0.110 | in |
| 15 | 289 | 5/0/5/9 | 2.904 | 1.121 | 0.39 | +6.57 | - | - | 1.25 | -0.129 | in |
| 16 | 242 | 5/5/9/5 | 1.343 | 0.862 | 0.64 | +5.70 | +5.367 | -0.33 | 0.842 | +0.020 | above |
| 17 | 237 | 5/5/5/0 | 3.655 | 1.434 | 0.39 | +5.16 | +4.833 | -0.33 | 2.317 | -0.883 | under |
| 18 | 378 | 9/0/9/9 | 4.042 | 1.273 | 0.31 | +5.36 | +5.200 | -0.16 | 1.759 | -0.486 | under |
| 19 | 359 | 5/5/9 | 2.332 | 1.456 | 0.62 | +5.58 | +5.533 | -0.05 | 1.505 | -0.049 | in |
| 20 | 341 | 9/5/5/9 | 4.679 | 2.901 | 0.62 | +6.26 | +6.100 | -0.16 | 2.771 | +0.130 | above |
| 21 | 218 | 4/5/9/4 | 5.976 | 5.127 | 0.86 | +5.90 | +5.667 | -0.23 | 4.246 | +0.881 | above |
| 22 | 201 | 9/9/0/5 | 4.636 | 2.713 | 0.59 | +4.83 | +4.700 | -0.13 | 2.708 | +0.005 | in |
| 23 | 372 | 5/0/9/9 | 2.320 | 0.928 | 0.40 | +6.48 | +6.400 | -0.08 | 0.873 | +0.055 | above |
| 24 | 348 | 5/0/0/9 | 3.839 | 1.878 | 0.49 | +6.40 | +6.317 | -0.08 | 2.191 | -0.313 | in |
| 25 | 14 | 5/5/9/5 | 4.847 | 3.251 | 0.67 | +6.64 | +6.500 | -0.14 | 3.091 | +0.160 | above |
| 26 | 310 | 0/0/9/9 | 8.003 | 6.334 | 0.79 | +5.05 | +5.367 | +0.32 | 7.139 | -0.805 | under |
| 27 | 290 | 4/4/5 | 0.747 | 0.408 | 0.55 | +6.55 | +6.567 | +0.02 | 0.492 | -0.084 | in |
| 28 | 73 | 9/9/9/5 | 2.332 | 2.069 | 0.89 | +7.62 | +6.167 | -1.45 | 1.421 | +0.648 | above |
| 29 | 286 | 9/0/5/9 | 2.033 | 1.216 | 0.60 | +7.34 | +7.467 | +0.13 | 1.052 | +0.164 | above |
| 30 | 231 | 4/9/0/9 | 5.939 | 2.330 | 0.39 | +6.65 | +6.233 | -0.42 | 2.938 | -0.608 | under |
| 31 | 313 | 5/1/4 | 3.668 | 3.314 | 0.90 | +6.32 | +6.217 | -0.10 | 4.566 | -1.252 | under |
| 32 | 374 | 9/9/9/9 | 6.547 | 3.970 | 0.61 | +8.80 | +7.817 | -0.98 | 5.157 | -1.187 | under |
| 33 | 382 | 5/5/5 | 2.522 | 0.549 | 0.22 | +5.26 | +5.517 | +0.26 | 0.588 | -0.039 | in |
| 34 | 340 | 4/0 | 3.285 | 1.825 | 0.56 | +7.15 | +6.967 | -0.18 | 1.9 | -0.075 | in |
| 35 | 253 | 9/5/5 | 7.283 | 5.274 | 0.72 | +6.95 | +7.517 | +0.57 | 9.454 | -4.180 | under |
| 40 | 602 | 4/5/9/9 | 0.157 | 0.857 | 5.46 | +0.73 | +4.133 | +3.40 | 1.533 | -0.676 | under |
| 41 | 703 | 5/0/9/4 | 1.892 | 0.800 | 0.42 | +2.75 | +5.567 | +2.82 | 0.852 | -0.052 | in |
| 42 | 700 | 5/5/1 | 0.686 | 0.261 | 0.38 | +5.49 | +5.367 | -0.12 | 0.309 | -0.048 | in |
| 43 | 720 | 9/5/9/0 | 1.176 | 0.634 | 0.54 | +4.43 | +5.633 | +1.20 | 0.885 | -0.251 | in |

# d vs B1149 (landing): median -0.093, p25..p75 -0.443..+0.019 (n 40); min -4.180 (row 35), max +1.250 (row 3)
# under-spread (regresses more than p25..p75): row 0 (-1.491), row 4 (-1.830), row 17 (-0.883), row 18 (-0.486), row 26 (-0.805), row 30 (-0.608), row 31 (-1.252), row 32 (-1.187), row 35 (-4.180), row 40 (-0.676)
# above-spread (improves more): row 3 (+1.250), row 5 (+0.036), row 8 (+0.080), row 16 (+0.020), row 20 (+0.130), row 21 (+0.881), row 23 (+0.055), row 25 (+0.160), row 28 (+0.648), row 29 (+0.164)
# d vs B1149 (source): median -0.030, p25..p75 -0.147..+0.014 (n 40); under-spread: row 4 (-1.777), row 7 (-0.183), row 10 (-0.508), row 17 (-0.227), row 21 (-0.340), row 22 (-0.153), row 24 (-0.198), row 35 (-3.415), row 40 (-0.481), row 43 (-0.157)

### Tour-7 -- the harness vs the B1142 lane-5 "new" rig (review/B1142/gate/regression_table.tsv)

The B1142 `new_land_cov` for 93 is the 3.049 reference (NOT the old rig's 2.938).
roi_l/roi_s verdicts are against the B1142 bands (roi_l_band / roi_s_band columns).

| spell | B1142 land | B1150 land | d | B1142 end | B1150 end | roi_l (band) | roi_s (band) |
|---|---|---|---|---|---|---|---|
| 38 | 2.650 | 2.742 | +0.092 | +4.831 | +6.433 | 0.046 (-0.013 of 0.059-0.079) | 0.073 (+0.010 of 0.063-0.085) |
| 41 | 2.666 | 2.882 | +0.216 | +6.253 | +6.067 | 0.079 (-0.007 of 0.086-0.116) | 0.096 (+0.000 of 0.096-0.130) |
| 54 | 1.696 | 0.999 | -0.697 | +5.054 | +6.400 | 0.024 (-0.013 of 0.037-0.051) | 0.026 (+0.003 of 0.023-0.031) |
| 93 | 3.049 | 2.742 | -0.307 | +4.898 | +6.433 | 0.046 (-0.018 of 0.064-0.086) | 0.073 (+0.002 of 0.071-0.097) |
| 253 | 5.028 | 5.267 | +0.239 | - | +7.617 | 0.062 (-0.017 of 0.079-0.107) | 0.071 (-0.080 of 0.151-0.205) |
| 458 | 1.732 | 0.571 | -1.161 | +4.989 | +5.517 | 0.050 (-0.036 of 0.086-0.116) | 0.038 (+0.001 of 0.037-0.049) |
| 656 | 0.957 | 0.999 | +0.042 | +4.905 | +6.400 | 0.024 (-0.006 of 0.030-0.040) | 0.026 (+0.002 of 0.024-0.032) |

### The under-spread rows -- the shape case of each

Every under-spread row is the same case: the retired config row was a DENSE
cloud over its measured lo..hi extent (a body/orbit/caster up to 2.2 H, or a
measured override box), while the record's EDD shape is SPARSE -- ring
circumferences, small spheres, points -- so fewer lit pixels at a comparable
extent. Every row stays below the B1130 oracle (the ratio p25..p75 is
0.45..0.72 over the 40 rows; range 0.22..1.29, row 40's 5.46 excluded --
a self-landing against the oracle's 0.157 baseline), and no row moves in a
way its shape words do not explain: in each the retired shape lit MORE
pixels, and the record's words are the decompile's (case 5 verified at
68860-68906; the rest pinned by unit tests). EDD units are world units;
H = 6.5 (sourceRecords' scale).

Landing rows (the record's T stage vs the retired target row/override):

- Row 0 (93, -1.491): T 5/5/0/5 = spheres r 5/4/5 (0.77/0.62/0.77 H) + a
  point (offset -0.75/+0.75); the retired row was the body 0.25..1.46 H
  (`kTarget` row 0). `shape.txt` PEAK: B1150 Act+1.93, centroid 0.72,
  frac>1H 0.29; B1149 Act+2.47, centroid 0.88, frac>1H 0.43 (measured in
  this leg from the two runs' `measure/shape.txt`).
- Row 4 (50, -1.830): T 5/9/9/0 = sphere r 10 (1.54 H) + rings r 8, r 12
  (1.85 H, offset -3) + a point; retired body 0.11..2.17 x 1.36 H.
- Row 17 (237, -0.883): T 5/5/5/0 = spheres r 6.5/10/12 (1.00/1.54/1.85 H)
  + a point (offset -1); retired body 0.13..2.18.
- Row 18 (378, -0.486): T 9/0/9/9 = ring r 0.75 + a point + ring r 12
  (offset -2) + ring r 12 (C0 -1); retired body 0.22..1.11.
- Row 26 (310, -0.805): T 0/0/9/9 = two points + ring r 3 (C0 -1) + ring
  r 5; retired body 0.24..1.56.
- Row 30 (231, -0.608): T 4/9/0/9 = disc 2x2 (offset -0.75) + ring r 10 +
  a point + ring r 4 (offset -2.5, C0 -1); retired body 0.3..1.72. Record
  30 carries the one `s19c = 0` slot (T slot 2, the empty shape-0 point).
- Row 31 (313, -1.252): T 5/1/4 = sphere r 8 (1.23 H) + even ring r 5
  (offset -0.75) + disc 2x2 (offset -4.5); retired body 0.2..1.21.
- Row 32 (374, -1.187): T 9/9/9/9 = rings r 2.5/6/5.5 (three at offset -3)
  + ring r 6; retired body 0.29..1.66.
- Row 35 (253, -4.180): T 9/5/5 = ring r 11 (1.69 H) + ellipsoids 5/4 and
  9/8; retired body 0.27..1.67 AND the retired spellOverrides
  impact/spellComplete BOX [14.95, 8.125, 6.5] = 2.30/1.25/1.00 H, the
  largest retired override shape (`kOverrideShape` lines 142-143).
- Row 40 (602, -0.676): T 4/5/9/9 = disc 7x7 (C0 -1) + sphere r 5 (offset
  +0.75) + ring r 10 (offset -3) + ring r 16 (2.46 H, offset -2.828);
  retired caster row 0.07..1.67.

Source rows (the record's S stage vs the retired source row):

- Row 4 (50, -1.777): S 5/5/9 = spheres r 8 (offset +3), r 4 + ring r 15
  (1.85 H, offset -3); retired orbit 0.1..1.86 x 1.195 (spiral).
- Row 7 (40, -0.183): S 5/5/5/5 = spheres r 4.5/4.5/2; retired orbit
  0.41..1.55 x 0.325.
- Row 10 (590, -0.508): S 5/5/5/9 = three spheres r 0.1 + ring r 5;
  retired orbit 0.12..1.86 x 1.27.
- Row 17 (237, -0.227): S 9/9/5/5 = rings r 4 (two at C0 -1) + sphere r 4;
  retired orbit 0.1..1.51 x 0.82.
- Row 21 (218, -0.340): S 4/5/5/4 = disc 9x9 (offset +4) + spheres r 8, r 4
  + disc 0.5 (offset -3.75); retired orbit 0.04..1.73 x 0.62.
- Row 22 (201, -0.153): S 9/9/9/9 = ring r 8 (offset -3) + three rings
  r 1; retired head row (halo disc 0.88..1.12 x 0.43 + the column).
- Row 24 (348, -0.198): S 9/4/5/9 = ring r 5 (offset -2.828) + disc 22
  (offset -3.25) + sphere r 5.5 (offset -1) + ring r 1.25; retired body
  0.05..0.84.
- Row 35 (253, -3.415): S 9/9/9/9 = rings r 4/8/8 + ring r 4 (offset +9);
  retired orbit 0.31..2.19 x 0.65 AND the retired castGlow BOX
  [12.35, 7.215, 6.5] (`kOverrideShape` line 144).
- Row 40 (602, -0.481): S 5/0/5/4 = ellipsoid 1x1 (offset -1) + a point +
  sphere r 6 + disc 6; retired body 0.14..1.33.
- Row 43 (720, -0.157): S 1/5/5/5 = even ring r 5 (offset -0.75) + spheres
  r 7/7 + sphere r 0.16; retired hands 0.18..1.64 x 1.25.

### The tour rows

The B1142 rig predates B1145 (timing), B1149 (populations) and this unit,
so a tour delta includes the whole stack -- which is why every B1150 end
is later than B1142's; the sweep above is the isolated B1149 -> B1150
comparison. The shape cases of the tour rows that move:

- 54 (-0.697): record 23's T 5/0/9/9 (sphere r 4, a point, rings r 11,
  r 7 at offset -3) replaces the retired 54 override impact SPHERE 0.65 /
  spellComplete BOX [0, 4.225, 0] / castGlow BOX [0, 3.575, 0]
  (`kOverrideShape` lines 130-132) + record 23's body row 0.08..1.26.
- 458 (-1.161): record 33's T 5/5/5 (spheres r 2 at offset -1, r 7, r 4)
  replaces the retired 458 castGlow BOX [2.34, 2.925, 2.34] / spellComplete
  SPHERE 1.3 / impact BOX [5.2, 4.55, 5.2] (lines 136-138) + record 33's
  orbit row 0.17..0.84 (spiral).
- 41 (+0.216): record 20's T 9/5/5/9 (ring r 14 at offset +3, spheres
  r 12, r 1, ring r 12) replaces the retired 41 impact BOX
  [2.275, 0.325, 2.275] / spellComplete SPHERE 2.6 / castSpray BOX
  [7.475, 5.2, 7.475] (lines 139-141) + record 20's body 0.07..1.71.
- 93 (-0.307 vs 3.049): row 0's note above.
- 38 (+0.092) and 656 (+0.042): record 0's T 5/5/0/5 (38, no override,
  shared with 93) and record 23 (656, same case as 54).

The roi misses (38 roi_l 0.013 under, 41 roi_l 0.007 under, 54 roi_l
0.013 under, 93 roi_l 0.018 under, 253 roi_l 0.017 under / roi_s 0.080
under, 458 roi_l 0.036 under) are SPRITE-ROI coverage (B1149's domain:
populations/sizes/alpha), not the spawn words; 253's roi_s band
(0.151-0.205) is the B1142 rig's own measured sprite p90 and was met by
the retired-box source, which the record's ring geometry does not
reproduce -- expected, and the B1151/B1153 follow-ups' domain.

### Known difference (documented, not a defect)

Spell 93 landing coverage: B1150 EDD shapes 2.742 vs the oracle (official
client EDD) 3.945 vs B1149 config 4.233. The 4.233 -> 2.742 step is the
shape swap itself: the retired row was the config's body 0.25..1.46 H
cloud, and record 0's T stage is 5/5/0/5 (spheres r 5/4/5 = 0.77/0.62/0.77
H + a point) -- measured in `runs/b1150_tour_93/measure/shape.txt`
(PEAK: cov 2.742, extent 3.29, centroid 0.72, frac>1H 0.29, peak
Act+1.93) against the B1149 run's (cov 4.233, extent 2.82, centroid 0.88,
frac>1H 0.43, peak Act+2.47). The spawn geometry is verified
byte-identical to the decompile for the shapes 93 uses (shape 5 dims,
decompile lines 68860-68906; the sustained pillar is emitter 280 with the
record's own life/duration words). The residual 2.742 = 0.70x of the
oracle sits inside the 40-row harness/oracle ratio p25..p75
(0.45..0.72): it is SPRITE-FIDELITY coverage (B1149's domain:
populations/sizes/alpha), not spawn geometry.

## FOLLOW-UP

- B1151: frames/spin/modes (+0x130.., sprite animation) — separate card.
- B1152: missile/projectile shapes — separate card (its trace is in
  `refactor/B1152/`).
- B1153: retire the remaining `spellOverrides` rows once their measured
  values come from the records.
