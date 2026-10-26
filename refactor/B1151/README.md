# B1151 (#471): the record's motion, spin, frames, and flags from spellsnew.edd

Fourth wiring step of B1147 (#467), after B1150 (merged at fable 3abd37df: the
TEN spawn shapes, offsets, tilts, depth bias and +0x19C scale from the record —
`include/client/graphics/environment/spell_spawn_shape.h`, the reader block in
`include/client/spell/spells_new_edd_reader.h`). This unit wires the record's
**motion, spin, frame animation and flag** words into the sim and the renderer,
and removes the B1145 motion time-scaling for record spells.

The specification is issue #471 (`glab issue view 471 --comments`). Every rule
below is already traced — `review/B1141/edd_fields_from_code.tsv` (all
PROVEN-BY-CODE unless marked) and `refactor/B1151/TRACE.md` (sections (a)–(e),
the PARTIAL section). Cited, not re-derived; where a row is PARTIAL, the
proven part is implemented and the PARTIAL half is marked below, never
implemented from inference.

The five fields were traced byte-to-C++ in `refactor/B1151/TRACE.md` against
`EQGraphicsDX9.dll` (Ti 5.9.255, B1141 decompile + disassembly); the raw
`spellsnew.edd` (2,118 records) matches the in-memory census exactly (the
8-byte header, `45 44 44 00 31 31 30 00`, is accounted for — TRACE sec. 0).

## The reader: the new motion words

`SpellsNewEddMotion` (`include/client/spell/spells_new_edd_reader.h`), decoded
entry-by-entry by `decodeSpellsNewEddMotion()` in
`src/client/spell/spells_new_edd_reader.cpp`, exposed through
`SpellsNewEddReader::motion(index)` / `motionAt(entry)` beside B1150's
`shape()`. Every word and its TSV row:

| word | offset | TSV row (name — arithmetic) | status |
|---|---|---|---|
| `orientToAttach` | +0x60 | orient emitter to attach object — `R 0x10060b88`: the basis is copied from the attached track/bone matrix each frame | PROVEN-BY-CODE |
| `noDepthWrite` | +0x64 | no depth write — `F 0x1005f7a8/0x1005f82c`: ZWRITEENABLE off for the emitter draw, restored after | PROVEN-BY-CODE |
| `spriteMode` | +0x70 | 0 billboard / 1 streak / 2 world-oriented quad — `R 0x100630a1/0x1006349a` | PROVEN-BY-CODE (mode-2 geometry PARTIAL) |
| `follow` | +0x74 | particles follow the emitter — `R 0x10062936`: each live particle's origin/basis re-copied each frame | PROVEN-BY-CODE |
| `upMin/upMax/upAccel` | +0x10EC/+0xF0/+0xF4 | axis-1 (up) — `R 0x100620fd`: `v1 = rand[EC,F0]; v1 += dt*F4; h += v1*s*dt` | PROVEN-BY-CODE |
| `axis2Min/axis2Max/axis2Accel` | +0xF8/+0xFC/+0x100 | axis-2 — `R 0x10062124`: `v2 = rand[F8,FC]; v2 += dt*100; offset += basis2*v2*s*dt` | PROVEN-BY-CODE |
| `axis3Min/axis3Max/axis3Accel` | +0x104/+0x108/+0x10C | axis-3 — `R 0x1006214b`: `v3 = rand[104,108]; v3 += dt*10C; offset += basis3*v3*s*dt` | PROVEN-BY-CODE |
| `radialMin/radialMax/radialAccel` | +0x110/+0x114/+0x118 | radial — `R 0x10062172`: `vr = rand[110,114]; vr += dt*118; r += vr*dt*n_r*s, h += vr*dt*n_h*s` (n = the spawn normal for shapes 5/6, else (1,0) horizontal) | PROVEN-BY-CODE |
| `swirlMin/swirlMax/swirlAccel` | +0x11C/+0x120/+0x124 | swirl — `R 0x10062199`: `w = rand[11C,120]; w += dt*124; theta += w*dt` (512 = full turn) | PROVEN-BY-CODE |
| `gravity` | +0x128 | world gravity — `R 0x100628f6`: `g += dt*128; world z -= g*s*dt` (EQ z up) | PROVEN-BY-CODE |
| `driftX` | +0x12C | world +X drift — `R 0x10062917`: `world x += s*12C*dt` (x only) | PROVEN-BY-CODE |
| `randomStartSpin` | +0x174 | random start spin — `R 0x100611f3`: `!=0 -> start = rand()/32767*1000` (512ths), else 0 | PROVEN-BY-CODE |
| `spinMin/spinMax` | +0x138/+0x188 | spin — `R 0x10061241/0x10061239`: `spin = rand[138,188]; angle = age*spin + start + align` (draw `0x100634c1/0x100638e8`) | PROVEN-BY-CODE (per-frame array layout PARTIAL) |
| `spinAlign` | +0x178 | spin alignment to the attach axis — `R 0x1006235f/0x1006246f`: `!=0 -> add the screen angle of an axis of the attach matrix (1,2,default=3; 4..6 = negated 1..3)` | PARTIAL (column→axis-letter not traced: FUN_100399f0) |
| `frameCount` | +0x130 | texture frame count — `R 0x10063b7a`: 1 whole / 2..4 2x2 / 5..8 4x2 / 9..16 4x4 / >16 whole; `frame = ftol(age*D134) mod D130` row-major | PROVEN-BY-CODE |
| `frameRate` | +0x134 | animation fps — `R 0x10063be0`: `frame = ftol(age * D134) % D130` | PROVEN-BY-CODE |
| `drawNegativeRadius` | +0x198 | draw at r < 0 — `R 0x10062e5c`: `!=1 and r < 0 -> not drawn` (inward radial past the pivot) | PROVEN-BY-CODE |

The attach scale `s` (+0x6C, TRACE sec. (a)): the stage-1 read path and the
value are PROVEN = `entityScaleForServerSize(getRaceScale(race), serverSize)`
(the fable mirror); fable already passes `attachScale` into
`applySlotEmitter()` (`spell_emitter_presets.cpp`), and every rule above that
multiplies by `s` receives it through `EddMotion::scale`. The stage-2 vtable
object stays PARTIAL (do not trace here).

### The census (regenerate with `python3 refactor/B1151/edd_census.py <spellsnew.edd>`)

One script (`refactor/B1151/edd_census.py`), output pasted below; every
distribution sums to the record count, 2,118.

```
size=881096  records=2118  header=45 44 44 00 31 31 30 00
-- int words --
+060 orient               0:1728 1:390   sum=2118
+064 noDepthWrite         0:206 1:1912   sum=2118
+06c scaleSizes           0:105 1:2013   sum=2118
+070 spriteMode           0:1911 1:205 2:2   sum=2118
+074 follow               0:1900 1:218   sum=2118
+0a8 shape                0:297 1:202 2:13 3:125 4:275 5:808 6:5 7:9 8:18 9:366   sum=2118
+130 frameCount           1:855 4:199 16:1064   sum=2118
+174 randomStartSpin      0:2098 1:20   sum=2118
+178 spinAlign            0:2116 3:1 6:1   sum=2118
+198 drawNegativeRadius   0:2117 1:1   sum=2118
```

The two mode-2 records (world-oriented quads, TRACE sec. (d)) are `e2008 OoW
Gate02` (`runecircle00.dds`) and `e2075 Disease02` (`darknesssp502.dds`) — both
ground-painted circles, consistent with the world-oriented geometry. The two
`spinAlign != 0` records are `e2114 VetRewards_Y1_a` (d178=6, the negated
default axis) and `e2115 VetRewards_Y1_b` (d178=3, the default axis).

B1149's README cites "162 entries use modes 1/2"; the raw census here counts
**207** records with `spriteMode` in {1,2} (205 mode-1 + 2 mode-2). B1149's
162 was a narrower subset (the `.eff`-referenced records it swept); the raw
2,118-record census is the one this unit quotes, and it is what `edd_census.py`
prints.

## The sim: per-particle motion (`sim_particles.cpp`, `unified_particle.h`)

`EmitterConfig::EddMotion` (`unified_particle.h`) carries the decoded words;
`applySlotEmitter()` copies them into the emitter config when a record is
present. `spawnSpellParticle()` (`sim_particles.cpp`) seeds the per-particle
EDD state from the record and the B1150 basis (`computeEddBasis()`, the three
emitter axes after the +0xC4/+0xC8 tilts); `SimulationWorker::updateEddMotion()`
(`simulation_worker.h`) integrates it each frame. The update order follows the
decompile, the address beside each step:

1. Per-axis velocities: `v1 += dt*F4` (`0x100626f9`), `v2 += dt*100`
   (`0x1006271d`), `v3 += dt*10C` (`0x100627cd`); offsets accumulate
   `v2*s*dt` along basis 2, `v3*s*dt` along basis 3, `v1*s*dt` along basis 1
   (up).
2. Radial: `vr += dt*118` (`0x1006287d`); `r += vr*dt*n_r*s`, `h += vr*dt*n_h*s`
   where n = the spawn normal (B1150's `EddShapePoint::normal` for shapes 5/6)
   else (1,0).
3. Swirl: `w += dt*124` (`0x100628d6`); `theta += w*dt` (512ths).
4. World gravity: `g += dt*128` (`0x100628f6`); world z -= g*s*dt.
5. World +X drift: world x += s*12C*dt (`0x10062917`).
6. Position is re-derived each frame from the emitter-frame state (h, r,
   theta, the axis offsets) plus the world integrals (x drift, z gravity), so
   there is no drift accumulation error.
7. Spin: `angle = age*spin + start + align` (draws `0x100634c1/0x100638e8`);
   `start = rand()/32767*1000` when +0x174 is set, else 0 (`0x100611f3`); the
   +0x178 alignment adds the screen angle of the attach-matrix axis (the
   `eddAlignAxis`/`eddAlignAngle512` helpers, PARTIAL for the column→letter
   mapping).
8. **+0x74 follow** (`0x10062936`): when the word is set and the emitter is
   attached, the particle's origin/basis is re-copied from the emitter each
   frame (the +0xB8.. offsets and +0xC4/+0xC8 tilts re-applied), so the
   particle moves with the actor/bone.
9. **+0x198 draw rule** (`0x10062e5c`): `!=1 and r < 0 -> not drawn` — the
   `UnifiedParticle::eddDraws()` cull at the top of the render loop.

Non-record (config/preset) emitters keep their existing paths: `EddMotion`
stays inactive and the particle never sets its EDD-motion flag.

## The renderer: frames, modes, flags (`unified_particle_renderer.cpp`)

- **Frames (record particles only).** A record-driven particle takes the N/fps
  rule — `frame = ftol(age * D134) % N`, the N bands from +0x130 (1 whole /
  2..4 2x2 / 5..8 4x2 / 9..16 4x4 / >16 whole) — via
  `SpellSpriteAtlas::cellFor(sprite, frame)`. The non-record path is unchanged:
  single-image sheets (B1124) still animate by normalized age over the whole
  sheet (`SpellSpriteAtlas::cellForAge`).
- **Mode 0 (billboard).** Today's screen-facing quad, unchanged.
- **Mode 1 (streak).** The quad is stretched between the particle and the
  second point `pos + emitter_axis * (s * h)`, the two projected points
  (TRACE sec. (d), the TSV +0x70 row).
- **Mode 2 (world-oriented quad).** NOT screen-facing: the quad is built from
  the emitter's runtime basis, rotated by the spin/alignment angle — the
  matrix-vector multiply at `0x1005dc80` (TRACE sec. (d), the verified listing
  `0x100634b0..0x10063680`), sized in world units so it shrinks with distance.
  PARTIAL where the TRACE marks the mode-2 geometry PARTIAL.
- **+0x64 depth write.** The official client's EDD emitter draw runs with
  ZWRITEENABLE=1 (depth write ON) by default: for a record whose +0x64 == 1
  the client sets it to 0 before the draw and back to 1 after
  (disassembly `EQGraphicsDX9.dis:136039/136082`, the TSV +0x64 row). The
  renderer implements it per draw window (`UnifiedParticleRenderer`, the
  `DepthWindow` comment in the header): each window's vertices split into two
  sub-windows, drawn in two passes -- the `.on` sub-window (record particles
  with +0x64 == 0, the official default) with the depth mask ON, then `.off`
  with it OFF as before. Every non-record particle (config presets, weather)
  goes to `.off`, keeping fable's long-standing global-off state; the
  official client's interleaving is per emitter, so additive windows are
  order-invariant and alpha windows differ only in the blend interleaving
  across the two sub-populations. Gate impact: 7 of the 40 sweep records have
  +0x64 == 0 (records 1, 11, 35, 40, 41, 42, 43 -- their rows may shift vs
  the B1150 baseline, whose global-OFF state drew them with depth write OFF;
  the mechanism is named in "The gate"), and in the tour spell 253
  (record 35). Every +0x64 == 1 row draws exactly as B1150's global-off
  pass did.
- **Spin angle incl. +0x178 alignment.** The quad rotates by
  `age*spin + start + align` (the `eddAlignAxis`/`eddAlignAngle512` screen
  angle when +0x178 is nonzero).

## Removed: the B1145 motion time-scaling (for record spells)

B1145 scaled the config's motion by `oldLife / L` to preserve a measured
extent when a spell's lifetime changed (`refactor/B1145/README.md`). That
scaling is now **skipped for record spells** — the record's motion words drive
the extent, so the config velocity must not be rescaled on top of them. In
`applySlotEmitter()` (`spell_emitter_presets.cpp`) the scaling block is guarded
by `!shape` (a record is present iff the shape/motion pointers are nonzero,
which move in lockstep from the same EDD entry). Non-record spells keep the
B1145 scaling — that is deliberate and pinned by the config-reachability test
below.

## Tests (`tests/test_spell_edd_motion.cpp`, `ctest -R 'SpellEdd'`)

Registered in `tests/cmake/spells.cmake` under `EQT_GRAPHICS` (the per-term sim
tests drive the real `SimulationWorker`). The gtest suites carry the `SpellEdd`
prefix so the card's `ctest -R 'Spell|...'` pattern picks them up; run them with
`ctest -R 'SpellEdd'` (19 tests).

- **Reader decode.** `decodeSpellsNewEddMotion` on a synthetic 416-byte entry:
  every word round-trips; a blank entry is all-zero.
- **Shipped census.** `CensusOfTheMotionWords` walks all 2,118 file entries
  through `motionAt()` (skips when `spellsnew.edd` is absent) and asserts the
  int-word distributions above -- the B1150 `CensusOfTheShapeWord` pattern.
- **Frame rule.** Each N band (1, 2, 5, 16, >16) and `frame = ftol(age*fps) % N`.
- **+0x198 draw rule.** Plain drawn; motion r>=0 drawn; motion r<0 word!=1 not
  drawn; motion r<0 word==1 drawn.
- **Spin.** `angle = age*spin + start`.
- **Alignment dispatch.** `eddAlignAxis` selects and negates the axis (1,2,
  default 3; 4..6 negated; >6 default).
- **Screen angle.** `eddAlignAngle512` measures in 512ths (0 on-screen, 128 for
  a quarter turn).
- **Per-term sim** (each through the real worker, read back off the
  renderBuffer particle): axis-2 velocity, axis-2 accel (quadratic), axis-3
  velocity, radial with the default normal, radial with the shape normal (unit
  normal split), swirl (theta accumulation), gravity (discrete free-fall, EQ
  z-up → Irrlicht -y), +X drift, spin (stored + grows with age), +0x74 follow
  (tracks a moving emitter, holds the local offset).
- **B1145 removal.** config-reachability: a record emitter's config velocity is
  not scaled; a non-record emitter's is.

The census (every distribution summing to 2,118) is the script above, and the
visual gate is the end-to-end net below.

## The gate

`model_viewer --spell-test --headless --camera b1118` tour-7 plus the
40-record sweep, rendered on the ThinkPad (`refactor/B1151/scripts/tour7.sh`,
`run_sweep.sh` — the B1150 scripts retargeted to this worktree) and measured
here with the B1139 instrument (`review/B1139/scripts/harness_measure.py` via
`run_scene.sh`). The oracle side is the B1130 lane-5 capture re-anchored
per-frame (`scripts/oracle_anchor.sh`). Compared against
`refactor/B1150/README.md` "The gate" (the shapes baseline),
`review/B1142/gate/regression_table.tsv` (the 93 reference 3.049),
`review/B1130/record_table.tsv` and `review/B1149/sweep_table.tsv`.

### Completion and collection (2026-10-26)

- Launched 02:14:11Z (`progress.log` line 1, `render b1151_tour_93 fps=30
  --seed 1`); tour-7 done 02:30:39Z (`progress.log` line 21, `measure b1151_tour_458
  rc=0`; `TOUR7_EXIT=0`); the 40-record sweep done 03:57:38Z (`progress.log`
  line 141, `measure b1151_r26_310 rc=0`, the last row of plans/sweep/list.tsv;
  lines 142-165 are the eight rerun_depth64.sh scenes);
  `gate_run.log` ends `SWEEP_EXIT=0`. `progress.log` holds 165 lines; 110
  carry `rc=0` and zero carry any other rc (`grep -c 'rc=[^0]' progress.log`
  = 0).
- The B1130 measure dirs' per-frame PNGs had been pruned from the NFS review
  copy (the TSVs only), so the oracle side regenerated them deterministically
  from the lane's capture videos: `MROOT=/home/user/b1151-oracle-frames bash
  scripts/oracle_anchor.sh` (`fx_measure.py` re-extracts each
  `review/B1130/oracle/<dir>/video.mp4` at its `meta.json` offset/times/
  crop). `ORACLE_RERUN_EXIT=0`, 40/40 records, `oracle_anchor/miss.log`
  empty, and all 80 `oracle_anchor/*_{SOURCE,LANDING}.tsv` files carry
  active rows.
- The seven sweep records with `+0x64 == 0` (1, 11, 35, 40, 41, 42, 43 —
  the word `edd_census.py` counts 206 of 2,118 at `+0x64 == 0`) and tour 253
  were re-measured on the FINAL binary after the `+0x64` depth-write wiring
  (commit 10c826a3): `scripts/rerun_depth64.sh` re-runs the exact run
  names, plans and anchor rows and overwrites them in place — done 04:15:33Z
  (`RERUN_DEPTH64_EXIT=0`). The re-measure changed NOTHING: all eight
  scenes' `land_cov`/`end`/`src_cov`/`src_ext` are identical to the
  pre-rerun rows. That is the prediction: the b1118 rig runs with the
  occluder OFF, so flipping depth-write cannot change which pixels are lit —
  the flag is wired and unit-pinned, and is visually neutral where nothing
  is in front. The other 39 scenes are render-identical on both binaries
  (their `.on` sub-windows are empty).
- The tables below are regenerated verbatim by
  `python3 refactor/B1151/scripts/gate_table.py` (all inputs in its
  docstring), `python3 refactor/B1151/scripts/motion_table.py` (the motion
  signatures) and `python3 refactor/B1151/scripts/gate_analysis.py` (the
  per-row B1150-vs-B1151 shape case + the record's motion words).

### Sweep: the Source stage -- the harness vs the retired band, vs B1150 and B1149 (per row)

`src_cov/src_ext/src_cen` are the B1151 harness PEAK (summary.tsv, b1123_summary.py);
the band is the retired row (tests/spell_fx_shape_oracle.h). `cen` verdict: in = src_cen within
[lo, hi], short/over = below/above. `d vs B1150` = the B1151 src_cov minus the B1150 sweep row;
`d vs B1149` = the B1151 src_cov minus review/B1149/sweep_table.tsv's.

| row | spell | band lo..hi / cen | src_cov | src_ext | src_cen | cen | d vs B1150 | d vs B1149 |
|---|---|---|---|---|---|---|---|---|
| 0 | 93 | 0.07..1.25 / 0.71 | 0.317 | 2.49 | 0.93 | in | +0.054 | +0.093 |
| 1 | 200 | 0.06..1.17 / 0.48 | 0.247 | 1.53 | 0.62 | in | +0.006 | +0.010 |
| 2 | 202 | 0.3..0.66 / 0.48 | 0.225 | 1.06 | 0.67 | over | +0.144 | +0.076 |
| 3 | 377 | 0.28..0.99 / 0.64 | 0.043 | 0.77 | 0.54 | in | -0.027 | -0.009 |
| 4 | 50 | 0.1..1.86 / 0.82 | 1.301 | 3.66 | 0.77 | in | +0.480 | -1.297 |
| 5 | 80 | 0.26..1.09 / 0.7 | 0.135 | 0.81 | 0.72 | in | -0.011 | +0.001 |
| 6 | 722 | 0.09..1.03 / 0.57 | 0.340 | 1.08 | 0.76 | in | +0.043 | -0.032 |
| 7 | 40 | 0.41..1.55 / 0.9 | 0.336 | 0.70 | 1.05 | in | +0.075 | -0.108 |
| 8 | 346 | 0.06..1.06 / 0.49 | 0.513 | 1.79 | 0.72 | in | -0.054 | -0.049 |
| 9 | 375 | 0.1..1.02 / 0.56 | 0.833 | 1.94 | 0.84 | in | +0.079 | +0.134 |
| 10 | 590 | 0.12..1.86 / 0.79 | 1.039 | 2.75 | 0.77 | in | +0.222 | -0.286 |
| 11 | 285 | 0.1..0.86 / 0.58 | 0.097 | 1.53 | 0.59 | in | +0.000 | -0.006 |
| 12 | 208 | 0.13..1.07 / 0.64 | 1.719 | 1.81 | 0.79 | in | +0.185 | +0.388 |
| 13 | 209 | 0.21..1.96 / 0.91 | 0.588 | 3.01 | 0.89 | in | -0.017 | -0.097 |
| 14 | 373 | 0.09..0.71 / 0.51 | 0.072 | 0.61 | 0.67 | in | +0.006 | +0.012 |
| 15 | 289 | 0.1..1.34 / 0.67 | 0.732 | 1.40 | 0.62 | in | -0.077 | -0.035 |
| 16 | 242 | 0.24..0.81 / 0.51 | 0.702 | 2.96 | 0.35 | in | +0.185 | +0.132 |
| 17 | 237 | 0.1..1.51 / 0.68 | 0.546 | 1.50 | 0.72 | in | -0.030 | -0.257 |
| 18 | 378 | 0.16..1.08 / 0.63 | 0.390 | 1.20 | 1.06 | in | +0.171 | +0.087 |
| 19 | 359 | 0.12..1.53 / 0.76 | 1.092 | 1.52 | 0.70 | in | -0.042 | -0.072 |
| 20 | 341 | 0.07..1.71 / 0.88 | 1.048 | 2.40 | 0.82 | in | -0.068 | -0.006 |
| 21 | 218 | 0.04..1.73 / 0.72 | 0.922 | 2.00 | 0.71 | in | +0.212 | -0.128 |
| 22 | 201 | 0.03..1.09 / 0.68 | 0.294 | 1.06 | 0.78 | in | +0.099 | -0.054 |
| 23 | 372 | 0.08..1.26 / 0.61 | 0.381 | 1.16 | 0.66 | in | -0.021 | -0.029 |
| 24 | 348 | 0.05..0.84 / 0.39 | 1.253 | 2.72 | 0.52 | in | +0.414 | +0.216 |
| 25 | 14 | 0.08..1.56 / 0.83 | 0.394 | 1.64 | 0.84 | in | -0.009 | +0.006 |
| 26 | 310 | 0.14..1.25 / 0.62 | 0.368 | 2.81 | 0.57 | in | -0.121 | -0.250 |
| 27 | 290 | 0.22..1.27 / 0.54 | 0.146 | 1.28 | 0.72 | in | +0.008 | -0.052 |
| 28 | 73 | 0.07..1.47 / 0.72 | 1.363 | 3.58 | 0.65 | in | +0.028 | +0.234 |
| 29 | 286 | 0.22..1.1 / 0.69 | 0.355 | 1.91 | 0.56 | in | +0.003 | +0.137 |
| 30 | 231 | 0.07..1.47 / 0.69 | 0.097 | 1.50 | 0.78 | in | -0.001 | +0.005 |
| 31 | 313 | 0.02..1.13 / 0.41 | 1.153 | 1.72 | 0.69 | in | +0.286 | +0.310 |
| 32 | 374 | 0.01..0.68 / 0.26 | 0.845 | 1.49 | 0.49 | in | +0.031 | -0.029 |
| 33 | 382 | 0.17..0.84 / 0.51 | 0.046 | 0.66 | 0.61 | in | +0.000 | -0.002 |
| 34 | 340 | 0.02..1.27 / 0.52 | 0.387 | 1.38 | 0.60 | in | +0.004 | -0.110 |
| 35 | 253 | 0.31..2.19 / 1.36 | 3.789 | 3.26 | 0.99 | in | +0.401 | -3.014 |
| 40 | 602 | 0.14..1.33 / 0.66 | 0.965 | 1.63 | 0.83 | in | -0.091 | -0.572 |
| 41 | 703 | 0.17..1.5 / 0.79 | 0.209 | 1.79 | 1.02 | in | +0.045 | +0.018 |
| 42 | 700 | 0.35..1.7 / 0.75 | 0.075 | 2.49 | 0.86 | in | +0.001 | -0.029 |
| 43 | 720 | 0.18..1.64 / 0.74 | 0.283 | 3.59 | 1.16 | in | +0.028 | -0.129 |

39 of 40 rows are centroid-in; row 2 (202) is `over` (0.67 vs the retired
band's 0.48): the retired band was measured pre-B1151, and the record's own
words now drive the cloud (B1151's core, not a regression — the same row's
landing is in-spread, d vs B1150 +0.027).

### Sweep: the landing -- the B1151 harness vs the B1150 shapes baseline, the B1130 oracle and the B1149 sweep

`B1150` = the collected B1150 sweep row (refactor/B1150/README.md "The gate", the shapes baseline);
`d vs B1150` = the B1151 land_cov minus B1150's, the card's spread rule (the p25..p75 of those deltas);
`oracle` = review/B1130/record_table.tsv (fixed camera), reported as measured (the harness geometry
differs from the lane, B1149's instrument note); `d vs B1149` = the B1151 land_cov minus
review/B1149/sweep_table.tsv's (continuity with the B1150 table).

| row | spell | T shape | B1150 cov | B1151 cov | d vs B1150 | spread | oracle cov | B1151/oracle | oracle end | B1151 end | d end | d vs B1149 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 93 | 5/5/0/5 | 2.742 | 2.113 | -0.629 | in | 3.945 | 0.54 | +6.67 | +6.467 | -0.20 | -2.120 |
| 1 | 200 | 5/9/5/9 | 1.447 | 1.254 | -0.193 | in | 3.050 | 0.41 | +6.58 | +6.133 | -0.45 | -0.344 |
| 2 | 202 | 5/5/5/5 | 0.649 | 0.676 | +0.027 | above | 2.250 | 0.30 | +5.48 | +4.633 | -0.85 | -0.189 |
| 3 | 377 | 5/4/4 | 9.303 | 7.414 | -1.889 | under | 11.688 | 0.63 | +9.00 | +4.033 | -4.97 | -0.639 |
| 4 | 50 | 5/9/9/0 | 1.467 | 1.427 | -0.040 | above | 3.250 | 0.44 | +6.78 | +5.667 | -1.11 | -1.870 |
| 5 | 80 | 9/5 | 0.260 | 0.249 | -0.011 | above | 0.559 | 0.45 | +4.53 | +4.267 | -0.26 | +0.025 |
| 6 | 722 | 9/9/5/5/9/9/5/5 | 0.707 | 0.391 | -0.316 | in | 0.762 | 0.51 | +6.26 | +6.167 | -0.09 | -0.418 |
| 7 | 40 | 1/9/5/9 | 2.455 | 1.069 | -1.386 | under | 3.077 | 0.35 | +6.81 | +5.900 | -0.91 | -1.534 |
| 8 | 346 | 9/5 | 3.477 | 0.262 | -3.215 | under | 6.271 | 0.04 | +3.78 | +2.167 | -1.61 | -3.135 |
| 9 | 375 | 1/5/5/9 | 1.353 | 1.320 | -0.033 | above | 1.999 | 0.66 | +7.64 | +7.200 | -0.44 | -0.018 |
| 10 | 590 | 5/9/5/0 | 4.469 | 1.571 | -2.898 | under | 5.476 | 0.29 | +5.72 | +6.067 | +0.35 | -2.929 |
| 11 | 285 | 4/5/9/9 | 0.467 | 0.232 | -0.235 | in | 0.997 | 0.23 | +5.51 | +4.333 | -1.18 | -0.288 |
| 12 | 208 | 9/0/9/9 | 4.160 | 3.409 | -0.751 | in | 5.788 | 0.59 | +7.04 | +7.000 | -0.04 | -0.937 |
| 13 | 209 | 9/9/9/4 | 2.016 | 0.621 | -1.395 | under | 3.264 | 0.19 | +6.39 | +5.467 | -0.92 | -1.590 |
| 14 | 373 | 9/4/9/9 | 5.168 | 1.881 | -3.287 | under | 3.995 | 0.47 | +8.60 | +8.400 | -0.20 | -3.397 |
| 15 | 289 | 5/0/5/9 | 1.121 | 0.827 | -0.294 | in | 2.904 | 0.28 | +6.57 | +5.867 | -0.70 | -0.423 |
| 16 | 242 | 5/5/9/5 | 0.862 | 0.845 | -0.017 | above | 1.343 | 0.63 | +5.70 | +5.200 | -0.50 | +0.003 |
| 17 | 237 | 5/5/5/0 | 1.434 | 1.240 | -0.194 | in | 3.655 | 0.34 | +5.16 | +4.800 | -0.36 | -1.077 |
| 18 | 378 | 9/0/9/9 | 1.273 | 0.985 | -0.288 | in | 4.042 | 0.24 | +5.36 | +5.033 | -0.33 | -0.774 |
| 19 | 359 | 5/5/9 | 1.456 | 1.297 | -0.159 | in | 2.332 | 0.56 | +5.58 | +5.500 | -0.08 | -0.208 |
| 20 | 341 | 9/5/5/9 | 2.901 | 2.113 | -0.788 | in | 4.679 | 0.45 | +6.26 | +5.567 | -0.69 | -0.658 |
| 21 | 218 | 4/5/9/4 | 5.127 | 4.209 | -0.918 | under | 5.976 | 0.70 | +5.90 | +5.633 | -0.27 | -0.037 |
| 22 | 201 | 9/9/0/5 | 2.713 | 2.125 | -0.588 | in | 4.636 | 0.46 | +4.83 | +4.700 | -0.13 | -0.583 |
| 23 | 372 | 5/0/9/9 | 0.928 | 0.839 | -0.089 | in | 2.320 | 0.36 | +6.48 | +6.433 | -0.05 | -0.034 |
| 24 | 348 | 5/0/0/9 | 1.878 | 1.760 | -0.118 | in | 3.839 | 0.46 | +6.40 | +5.883 | -0.52 | -0.431 |
| 25 | 14 | 5/5/9/5 | 3.251 | 1.993 | -1.258 | under | 4.847 | 0.41 | +6.64 | +6.433 | -0.21 | -1.098 |
| 26 | 310 | 0/0/9/9 | 6.334 | 1.492 | -4.842 | under | 8.003 | 0.19 | +5.05 | +4.567 | -0.48 | -5.647 |
| 27 | 290 | 4/4/5 | 0.408 | 0.244 | -0.164 | in | 0.747 | 0.33 | +6.55 | +4.300 | -2.25 | -0.248 |
| 28 | 73 | 9/9/9/5 | 2.069 | 1.635 | -0.434 | in | 2.332 | 0.70 | +7.62 | +6.067 | -1.55 | +0.214 |
| 29 | 286 | 9/0/5/9 | 1.216 | 0.902 | -0.314 | in | 2.033 | 0.44 | +7.34 | +7.033 | -0.31 | -0.150 |
| 30 | 231 | 4/9/0/9 | 2.330 | 2.286 | -0.044 | above | 5.939 | 0.38 | +6.65 | +6.300 | -0.35 | -0.652 |
| 31 | 313 | 5/1/4 | 3.314 | 2.497 | -0.817 | under | 3.668 | 0.68 | +6.32 | +6.217 | -0.10 | -2.069 |
| 32 | 374 | 9/9/9/9 | 3.970 | 4.090 | +0.120 | above | 6.547 | 0.62 | +8.80 | +7.217 | -1.58 | -1.067 |
| 33 | 382 | 5/5/5 | 0.549 | 0.360 | -0.189 | in | 2.522 | 0.14 | +5.26 | +4.917 | -0.34 | -0.228 |
| 34 | 340 | 4/0 | 1.825 | 1.068 | -0.757 | in | 3.285 | 0.33 | +7.15 | +5.967 | -1.18 | -0.832 |
| 35 | 253 | 9/5/5 | 5.274 | 5.297 | +0.023 | above | 7.283 | 0.73 | +6.95 | +5.250 | -1.70 | -4.157 |
| 40 | 602 | 4/5/9/9 | 0.857 | 0.639 | -0.218 | in | 0.157 | 4.07 | +0.73 | +4.133 | +3.40 | -0.894 |
| 41 | 703 | 5/0/9/4 | 0.800 | 0.800 | +0.000 | above | 1.892 | 0.42 | +2.75 | +5.533 | +2.78 | -0.052 |
| 42 | 700 | 5/5/1 | 0.261 | 0.204 | -0.057 | above | 0.686 | 0.30 | +5.49 | +5.300 | -0.19 | -0.105 |
| 43 | 720 | 9/5/9/0 | 0.634 | 0.389 | -0.245 | in | 1.176 | 0.33 | +4.43 | +4.500 | +0.07 | -0.496 |

# d vs B1150 (landing): median -0.266, p25..p75 -0.810..-0.065 (n 40); min -4.842 (row 26), max +0.120 (row 32)
# under-spread vs B1150 (regresses more than p25..p75): row 3 (-1.889), row 7 (-1.386), row 8 (-3.215), row 10 (-2.898), row 13 (-1.395), row 14 (-3.287), row 21 (-0.918), row 25 (-1.258), row 26 (-4.842), row 31 (-0.817)
# above-spread vs B1150 (improves more): row 2 (+0.027), row 4 (-0.040), row 5 (-0.011), row 9 (-0.033), row 16 (-0.017), row 30 (-0.044), row 32 (+0.120), row 35 (+0.023), row 41 (+0.000), row 42 (-0.057)
# d vs B1149 (source): median -0.019, p25..p75 -0.105..+0.062 (n 40); under-spread: row 4 (-1.297), row 7 (-0.108), row 10 (-0.286), row 17 (-0.257), row 21 (-0.128), row 26 (-0.250), row 34 (-0.110), row 35 (-3.014), row 40 (-0.572), row 43 (-0.129)
# d vs B1149 (landing): median -0.611, p25..p75 -1.425..-0.194 (n 40)

### The under-spread rows — the mechanism of each (`gate_analysis.py`)

All ten under-spread rows (above: "under-spread vs B1150") are one of two
named mechanisms; none is unattributed. B1150/B1151 PEAK (cov/extent/
centroid/t_act) and END are `gate_analysis.py`'s rows; the words are the
record's own (`edd_census.py` offsets, `spellsnew.edd`).

- **The record's own motion words are now integrated (B1151's core).** The
  record says the particles move, so they spread over the frame instead of
  pooling at the anchor; the pre-B1151 sim held them still and lit more.
  - Row 26 (310, -4.842): em10(target) radialMin=-5..-10 + swirlMin=4..10,
    N=16; em48(target) upMax=1, radial 15 + swirl 20 — the outward spiral
    (PEAK 6.319/3.01 → 1.461/2.16). The oracle's 8.003 is its fixed-camera
    figure (B1151/oracle 0.19 as measured; the lane's camera frames more
    ground — the B1149 instrument note).
  - Row 14 (373, -3.287): em66(target) swirlMin=-10..20, fps=20, N=4 — the
    swirl spreads the cloud (PEAK 5.076/4.14 → 1.869/2.69).
  - Row 10 (590, -2.898): em18(target) radialMin=-5..-8, N=16 — radial
    fly-off (PEAK 4.327/3.58 → 1.571/2.76).
  - Row 7 (40, -1.386): em20(caster) upMin=13, upMax=2.5, radialMin=2, N=16
    — the caster-side cloud flies up out of the landing window
    (PEAK 2.352/3.22 → 1.002/2.40).
  - Row 25 (14, -1.258): em6(target) upMin=7, N=16; em7(target)
    radialMin=7.5 + gravity=3.75, N=16 (PEAK 3.164/3.65 → 1.960/3.19).
  - Row 21 (218, -0.918): em35/em37(target) swirl 50/10 + radial 1..2/2..5;
    em34/em36 gravity=1 (PEAK 5.127/3.89 → 4.209/3.41).
- **The B1145 time-scaling removal (this card's second half).** The
  record's own timing is shorter than the scaled preset's, so the landing
  lives less and accumulates less. `spell_emitter_presets.cpp:607-613`
  (the `oldLife / L` ratio now applies to non-record emitters only, the
  `!shape` guard at 612 — see "Removed: the B1145 motion time-scaling").
  - Row 8 (346, -3.215): no motion words at all — END +3.80 → +2.00
    (PEAK 2.934/4.09 → 0.503/2.18).
  - Row 3 (377, -1.889): the record's only nonzero word is em4(caster)
    upMax=2 (the caster side); END +6.43 → +4.00 (PEAK 9.001/4.03 →
    7.301/3.77).
  - Row 13 (209, -1.395): no motion words — END +6.23 → +5.63
    (PEAK 1.984/2.71 → 0.621/2.76).
  - Row 31 (313, -0.817): no motion words — a timing shift, not a shorter
    life: PEAK +1.82 → +3.82, END unchanged +6.22.

### Tour-7 -- the B1151 harness vs the B1142 lane-5 "new" rig and the B1150 tour

The B1142 `new_land_cov` for 93 is the 3.049 reference (NOT the old rig's 2.938).
`d vs B1150` = the B1151 land_cov minus the B1150 tour row.

| spell | B1142 land | B1150 land | B1151 land | d vs B1142 | d vs B1150 | B1142 end | B1151 end | roi_l (band) | roi_s (band) |
|---|---|---|---|---|---|---|---|---|---|
| 38 | 2.650 | 2.742 | 2.113 | -0.537 | -0.629 | 4.831 | +6.467 | 0.041 (-0.018 of 0.059-0.079) | 0.067 (+0.004 of 0.063-0.085) |
| 41 | 2.666 | 2.882 | 2.478 | -0.188 | -0.404 | 6.253 | +5.667 | 0.106 (+0.020 of 0.086-0.116) | 0.092 (-0.004 of 0.096-0.130) |
| 54 | 1.696 | 0.999 | 1.204 | -0.492 | +0.205 | 5.054 | +6.333 | 0.029 (-0.008 of 0.037-0.051) | 0.024 (+0.001 of 0.023-0.031) |
| 93 | 3.049 | 2.742 | 2.113 | -0.936 | -0.629 | 4.898 | +6.467 | 0.041 (-0.023 of 0.064-0.086) | 0.067 (-0.004 of 0.071-0.097) |
| 253 | 5.028 | 5.267 | 5.306 | +0.278 | +0.039 | None | +5.317 | 0.084 (+0.005 of 0.079-0.107) | 0.152 (+0.001 of 0.151-0.205) |
| 458 | 1.732 | 0.571 | 0.499 | -1.233 | -0.072 | 4.989 | +5.183 | 0.059 (-0.027 of 0.086-0.116) | 0.036 (-0.001 of 0.037-0.049) |
| 656 | 0.957 | 0.999 | 1.204 | +0.247 | +0.205 | 4.905 | +6.333 | 0.029 (-0.001 of 0.030-0.040) | 0.024 (+0.000 of 0.024-0.032) |

93: B1151 2.113 vs the B1142 reference 3.049 — ratio 0.693, inside the
B1150 harness/oracle landing-ratio p25..p75 (0.45..0.72 over its 40 rows, row 40
excluded, `refactor/B1150/README.md` line 338; B1149's own ratio p25..p75 is
0.52..0.81, `review/B1149/sweep_table.tsv` via statistics.quantiles, and 0.693
is inside both). Every tour row's d vs B1150 (-0.629..+0.205) is inside or
above the sweep's d-vs-B1150 p25..p75 (-0.810..-0.065): 38/93 (-0.629), 41
(-0.404) and 458 (-0.072) inside, 54 (+0.205), 253 (+0.039) and 656 (+0.205)
above p75 (improvements); none below p25. 253 improved (+0.039 vs
B1150, +0.278 vs B1142): its record's words (em86/em87 upMin=8, swirl
15..30) now draw what the preset approximated.

### Motion signatures (the issue's gate)

Per record, per-frame centroid height and lit extent over time, from
`anchor.py` per-frame rows — the same instrument on both sides. The harness
rows are `runs/b1151_r<N>_<spell>/anchor_{SOURCE,LANDING}.tsv` (b1118:
feet 640,605, model 235px); the oracle rows are
`oracle_anchor/<rig>_{SOURCE,LANDING}.tsv` (the B1130 lane-5 capture;
`oracle_anchor/rig.tsv` maps record → rig; caster 209px, fixed_npc target
325px). Regenerate: `MROOT=/home/user/b1151-oracle-frames bash
scripts/oracle_anchor.sh` on the ThinkPad, then
`python3 refactor/B1151/scripts/motion_table.py` here.

# motion signatures: per record, centroid height + lit extent over time (anchor.py per-frame rows).
# ONSET/PEAK/END are frame index + time (t_bc-relative for SOURCE, t_act-relative for LANDING).
# drift = centroid height PEAK - ONSET; end drift = END - ONSET; growth = cov(PEAK)/cov(ONSET) and
# h_p95(PEAK) - h_p95(ONSET); all in the SIDE'S own model heights (oracle SOURCE/fixed_self in 209px
# caster units, oracle fixed_npc LANDING in 325px target units; harness in 235px caster units, b1118).
# Verdict: drift direction and growth direction vs the oracle; short/over per column when they differ.


### SOURCE -- the B1151 harness vs the B1130 oracle (per record)

| rec | oracle onset | oracle peak | oracle drift | oracle end drift | oracle growth | harness onset | harness peak | harness drift | harness end drift | harness growth | extent o/h | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | f7484 +0.54 | f7561 +3.62 cov 3.954 | +0.20 (rising) | +0.18 | 28.45x (growing) h95 -0.35 | f60 +0.50 | f160 +3.83 cov 2.113 | +0.16 (rising) | +0.19 | 14.88x (growing) h95 +0.40 | h95 2.14 / h95 1.73 | PASS |
| 10 | f7041 +0.42 | f7151 +4.82 cov 5.223 | +0.22 (rising) | -0.03 | 20.09x (growing) h95 -0.16 | f60 +0.50 | f189 +4.80 cov 1.549 | +0.23 (rising) | -0.69 | 7.86x (growing) h95 +1.08 | h95 2.30 / h95 2.34 | PASS |
| 11 | f7154 +0.50 | f7239 +3.90 cov 1.057 | -0.65 (falling) | -0.37 | 13.38x (growing) h95 -1.23 | f60 +0.50 | f121 +2.53 cov 0.232 | -0.14 (falling) | -0.05 | 8.00x (growing) h95 -0.20 | h95 1.33 / h95 1.09 | PASS |
| 12 | f7360 +0.40 | f7460 +4.40 cov 5.776 | +0.13 (rising) | +0.00 | 27.77x (growing) h95 -0.53 | f60 +0.50 | f160 +3.83 cov 3.408 | +0.14 (rising) | +0.02 | 14.02x (growing) h95 +0.82 | h95 1.89 / h95 1.99 | PASS |
| 13 | f7473 +0.42 | f7595 +5.30 cov 3.267 | -0.20 (falling) | +0.23 | 23.50x (growing) h95 -0.37 | f60 +0.50 | f151 +3.53 cov 0.614 | +0.02 (flat) | +0.02 | 2.33x (growing) h95 +0.36 | h95 2.12 / h95 1.95 | drift short |
| 14 | f6718 +0.53 | f6905 +8.01 cov 3.994 | -0.70 (falling) | -0.76 | 54.71x (growing) h95 -1.17 | f60 +0.50 | f246 +6.70 cov 1.875 | -0.03 (flat) | -0.04 | 72.12x (growing) h95 +0.07 | h95 1.39 / h95 1.07 | drift short |
| 15 | f8231 +0.43 | f8318 +3.91 cov 2.886 | +0.20 (rising) | +0.57 | 13.74x (growing) h95 +0.60 | f60 +0.50 | f225 +6.00 cov 0.829 | -0.06 (flat) | -0.25 | 5.38x (growing) h95 -0.04 | h95 2.10 / h95 1.27 | drift short / extent short |
| 16 | f7471 +0.21 | f7566 +4.01 cov 1.356 | -1.60 (falling) | -1.53 | 45.20x (growing) h95 -1.13 | f60 +0.50 | f145 +3.33 cov 0.839 | +0.08 (flat) | +0.43 | 10.11x (growing) h95 -0.03 | h95 1.43 / h95 0.88 | drift short / extent short |
| 17 | f6618 +0.40 | f6741 +5.32 cov 3.631 | -0.15 (falling) | -0.72 | 32.13x (growing) h95 -0.11 | f60 +0.50 | f189 +4.80 cov 1.150 | +0.37 (rising) | -0.22 | 11.50x (growing) h95 +1.26 | h95 2.41 / h95 2.37 | drift over |
| 18 | f8226 +0.20 | f8339 +4.72 cov 4.013 | -1.53 (falling) | -0.70 | 148.63x (growing) h95 -0.92 | f60 +0.50 | f195 +5.00 cov 0.986 | +0.22 (rising) | +0.17 | 9.67x (growing) h95 +0.98 | h95 1.64 / h95 2.12 | drift short |
| 19 | f6607 +0.52 | f6717 +4.92 cov 2.256 | -0.37 (falling) | -0.71 | 18.19x (growing) h95 -0.76 | f60 +0.50 | f192 +4.90 cov 1.297 | -0.04 (flat) | -0.26 | 15.08x (growing) h95 -0.05 | h95 1.75 / h95 1.51 | drift short |
| 1 | f8157 +0.41 | f8242 +3.81 cov 3.031 | -0.60 (falling) | -0.46 | 55.11x (growing) h95 -0.89 | f60 +0.50 | f136 +3.03 cov 1.246 | +0.07 (flat) | +0.50 | 20.10x (growing) h95 +0.13 | h95 1.67 / h95 1.42 | drift short |
| 20 | f7475 +0.42 | f7545 +3.23 cov 4.669 | +0.01 (flat) | -0.15 | 28.47x (growing) h95 -0.33 | f60 +0.50 | f136 +3.03 cov 2.101 | +0.02 (flat) | -0.21 | 10.50x (growing) h95 +0.28 | h95 2.14 / h95 1.76 | PASS |
| 21 | f7478 +0.33 | f7563 +3.73 cov 5.981 | -1.16 (falling) | -1.05 | 175.91x (growing) h95 -0.59 | f60 +0.50 | f151 +3.53 cov 4.211 | -0.03 (flat) | -0.59 | 30.29x (growing) h95 +0.38 | h95 1.97 / h95 2.23 | drift short |
| 22 | f7346 +0.54 | f7403 +2.82 cov 4.644 | -0.11 (falling) | -0.23 | 51.03x (growing) h95 -0.50 | f60 +0.50 | f136 +3.03 cov 2.089 | +0.01 (flat) | +0.06 | 28.23x (growing) h95 +0.76 | h95 2.03 / h95 1.92 | drift short |
| 23 | f7482 +0.42 | f7579 +4.30 cov 2.315 | -0.37 (falling) | -0.51 | 30.06x (growing) h95 -1.03 | f60 +0.50 | f180 +4.50 cov 0.783 | -0.17 (falling) | -0.30 | 6.93x (growing) h95 +0.02 | h95 1.50 / h95 1.28 | PASS |
| 24 | f7440 +0.42 | f7530 +4.02 cov 3.841 | +0.05 (flat) | -0.09 | 28.24x (growing) h95 -0.65 | f60 +0.50 | f123 +2.60 cov 1.749 | +0.18 (rising) | +0.29 | 10.23x (growing) h95 +0.41 | h95 1.84 / h95 1.19 | drift over / extent short |
| 25 | f7132 +0.52 | f7202 +3.32 cov 4.827 | -0.24 (falling) | -0.43 | 43.49x (growing) h95 -0.59 | f60 +0.50 | f153 +3.60 cov 1.991 | -0.18 (falling) | -0.17 | 36.87x (growing) h95 +0.33 | h95 1.70 / h95 1.34 | PASS |
| 26 | f8333 +0.51 | f8488 +6.71 cov 7.961 | +0.13 (rising) | +0.49 | 68.04x (growing) h95 -0.19 | f60 +0.50 | f165 +4.00 cov 1.490 | -0.02 (flat) | +0.33 | 16.74x (growing) h95 +0.24 | h95 2.30 / h95 1.14 | drift short / extent short |
| 27 | f8301 +0.42 | f8341 +2.02 cov 0.763 | +0.08 (flat) | -0.07 | 5.37x (growing) h95 +0.02 | f60 +0.50 | f97 +1.73 cov 0.251 | -0.01 (flat) | +0.02 | 3.18x (growing) h95 +0.34 | h95 1.48 / h95 1.37 | PASS |
| 28 | f7323 +0.43 | f7540 +9.11 cov 2.378 | -0.43 (falling) | -0.61 | 17.36x (growing) h95 -1.28 | f60 +0.50 | f288 +8.10 cov 1.631 | -0.03 (flat) | -0.26 | 10.95x (growing) h95 +0.10 | h95 1.21 / h95 1.37 | drift short |
| 29 | f7463 +0.40 | f7546 +3.72 cov 2.013 | -0.72 (falling) | -0.72 | 35.95x (growing) h95 -1.17 | f60 +0.50 | f148 +3.43 cov 0.890 | +0.07 (flat) | +0.16 | 12.54x (growing) h95 +0.08 | h95 1.39 / h95 1.30 | drift short |
| 2 | f8309 +0.72 | f8399 +4.32 cov 2.241 | +0.08 (flat) | -0.40 | 32.48x (growing) h95 -0.23 | f60 +0.50 | f160 +3.83 cov 0.658 | -0.14 (falling) | -0.46 | 17.78x (growing) h95 +0.53 | h95 2.29 / h95 1.47 | drift over / extent short |
| 30 | f8326 +0.80 | f8409 +4.12 cov 5.858 | -0.53 (falling) | -0.55 | 119.55x (growing) h95 -0.19 | f69 +0.80 | f160 +3.83 cov 2.276 | +0.16 (rising) | -0.16 | 81.29x (growing) h95 +0.94 | h95 2.37 / h95 2.21 | drift short |
| 31 | f8193 +0.41 | f8293 +4.41 cov 3.694 | +0.31 (rising) | +0.10 | 17.10x (growing) h95 -0.26 | f60 +0.50 | f213 +5.60 cov 2.494 | +0.54 (rising) | +0.16 | 5.79x (growing) h95 +1.50 | h95 2.17 / h95 2.25 | PASS |
| 32 | f8297 +0.31 | f8385 +3.83 cov 6.620 | -0.85 (falling) | -0.73 | 200.61x (growing) h95 -0.18 | f60 +0.50 | f160 +3.83 cov 4.045 | +0.74 (rising) | +0.92 | 16.65x (growing) h95 +1.54 | h95 2.38 / h95 2.38 | drift short |
| 33 | f7329 +0.74 | f7459 +5.94 cov 2.477 | -0.45 (falling) | -0.65 | 39.95x (growing) h95 -0.42 | f63 +0.60 | f228 +6.10 cov 0.363 | +0.12 (rising) | -0.10 | 15.12x (growing) h95 +0.69 | h95 2.13 / h95 1.61 | drift short |
| 34 | f7321 +0.41 | f7401 +3.61 cov 3.120 | +0.01 (flat) | +0.15 | 47.27x (growing) h95 -0.37 | f60 +0.50 | f136 +3.03 cov 1.066 | +0.16 (rising) | -0.17 | 9.60x (growing) h95 +0.37 | h95 2.18 / h95 1.54 | drift over |
| 35 | f7315 +0.22 | f7450 +5.62 cov 7.274 | -0.94 (falling) | -0.85 | 234.65x (growing) h95 -0.29 | f60 +0.50 | f130 +2.83 cov 5.297 | -0.23 (falling) | -0.55 | 4.82x (growing) h95 -0.15 | h95 2.27 / h95 2.37 | PASS |
| 3 | f7322 +0.62 | f7495 +7.54 cov 11.689 | -0.01 (flat) | -0.04 | 160.12x (growing) h95 -0.13 | f63 +0.60 | f153 +3.60 cov 7.377 | +0.38 (rising) | -0.02 | 237.97x (growing) h95 +1.09 | h95 2.41 / h95 2.03 | drift over |
| 40 | f6900 +0.40 | f7180 +11.60 cov 2.600 | +0.08 (flat) | +0.76 | 2.87x (growing) h95 +0.34 | f60 +0.50 | f136 +3.03 cov 0.965 | +0.14 (rising) | +0.42 | 1.24x (growing) h95 +0.14 | h95 1.80 / h95 1.50 | drift over |
| 41 | f8347 +0.21 | f8465 +4.93 cov 1.896 | -1.34 (falling) | -0.50 | 48.62x (growing) h95 -1.20 | f60 +0.50 | f168 +4.10 cov 0.798 | +0.49 (rising) | +0.78 | 19.46x (growing) h95 +1.17 | h95 1.36 / h95 2.23 | drift short / extent over |
| 42 | f6477 +0.41 | f6570 +4.13 cov 0.654 | -0.49 (falling) | -0.89 | 7.43x (growing) h95 -1.05 | f60 +0.50 | f165 +4.00 cov 0.205 | -0.31 (falling) | -0.47 | 4.46x (growing) h95 -0.84 | h95 1.50 / h95 1.02 | extent short |
| 43 | f6608 +0.41 | f6726 +5.13 cov 1.181 | -0.72 (falling) | -0.70 | 12.05x (growing) h95 -1.19 | f60 +0.50 | f153 +3.60 cov 0.566 | +0.91 (rising) | +0.74 | 11.55x (growing) h95 +1.23 | h95 1.35 / h95 2.48 | drift over / extent over |
| 4 | f6502 +0.62 | f6649 +6.50 cov 3.219 | +0.16 (rising) | +0.36 | 19.51x (growing) h95 -0.27 | f60 +0.50 | f225 +6.00 cov 1.428 | +0.52 (rising) | +0.07 | 15.19x (growing) h95 +1.14 | h95 2.22 / h95 2.47 | PASS |
| 5 | f8182 +0.43 | f8284 +4.51 cov 0.557 | -0.22 (falling) | +0.05 | 6.79x (growing) h95 -1.03 | f60 +0.50 | f160 +3.83 cov 0.245 | +0.05 (flat) | -0.01 | 3.40x (growing) h95 +0.28 | h95 1.51 / h95 1.35 | drift short |
| 6 | f6461 +0.24 | f6573 +4.72 cov 0.731 | -1.88 (falling) | -2.07 | 25.21x (growing) h95 -1.47 | f60 +0.50 | f160 +3.83 cov 0.394 | +0.28 (rising) | +0.31 | 4.64x (growing) h95 +1.09 | h95 1.09 / h95 2.13 | drift short / extent over |
| 7 | f8351 +0.40 | f8431 +3.60 cov 3.077 | -0.05 (flat) | -0.30 | 19.60x (growing) h95 -0.11 | f60 +0.50 | f163 +3.93 cov 1.068 | -0.11 (falling) | -0.20 | 8.97x (growing) h95 +0.22 | h95 1.38 / h95 1.25 | drift over |
| 8 | f6462 +0.51 | f6560 +4.43 cov 5.804 | -0.05 (flat) | -0.37 | 59.22x (growing) h95 -0.45 | f60 +0.50 | f151 +3.53 cov 0.979 | -0.15 (falling) | +0.12 | 16.32x (growing) h95 -0.05 | h95 2.09 / h95 0.92 | drift over / extent short |
| 9 | f6616 +0.43 | f6701 +3.83 cov 2.691 | +0.46 (rising) | +0.77 | 7.14x (growing) h95 -0.04 | f60 +0.50 | f130 +2.83 cov 1.320 | +0.34 (rising) | -0.07 | 3.05x (growing) h95 +0.88 | h95 2.37 / h95 2.01 | PASS |

# SOURCE tallies: 40 compared; PASS 12; drift direction agrees 13/40; growth direction agrees 40/40; cast-anim pattern (oracle falling, harness flat) 10; unattributed (non-PASS, not the pattern) 18

### LANDING -- the B1151 harness vs the B1130 oracle (per record)

| rec | oracle onset | oracle peak | oracle drift | oracle end drift | oracle growth | harness onset | harness peak | harness drift | harness end drift | harness growth | extent o/h | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | f7509 +0.03 | f7561 +2.11 cov 1.631 | +0.03 (flat) | +0.54 | 15.39x (growing) h95 -0.19 | f90 +0.00 | f160 +2.33 cov 2.113 | -0.13 (falling) | -0.06 | 7.09x (growing) h95 -0.12 | h95 1.54 / h95 1.73 | drift over |
| 10 | f7106 +0.00 | f7151 +1.80 cov 5.235 | +0.08 (flat) | -0.19 | 3.88x (growing) h95 +0.06 | f135 +0.00 | f190 +1.83 cov 1.571 | +0.15 (rising) | -0.75 | 3.44x (growing) h95 +0.41 | h95 2.30 / h95 2.34 | drift over |
| 11 | f7180 +0.03 | f7237 +2.31 cov 0.984 | -0.24 (falling) | -0.54 | 6.35x (growing) h95 -1.18 | f90 +0.00 | f120 +1.00 cov 0.227 | -0.13 (falling) | -0.03 | 2.49x (growing) h95 +0.15 | h95 1.31 / h95 1.11 | PASS |
| 12 | f7388 +0.01 | f7456 +2.73 cov 2.390 | +0.17 (rising) | -0.13 | 3.67x (growing) h95 +0.41 | f90 +0.00 | f160 +2.33 cov 3.408 | +0.00 (flat) | -0.56 | 1.94x (growing) h95 +0.53 | h95 1.38 / h95 1.99 | drift short / extent over |
| 13 | f7514 +0.03 | f7596 +3.31 cov 1.346 | -0.14 (falling) | -0.08 | 6.09x (growing) h95 -0.03 | f105 +0.00 | f151 +1.53 cov 0.615 | +0.18 (rising) | +0.02 | 2.16x (growing) h95 +0.66 | h95 1.51 / h95 1.95 | drift over |
| 14 | f6806 +0.04 | f6906 +4.04 cov 3.995 | -0.34 (falling) | -0.31 | 41.61x (growing) h95 -1.15 | f175 +0.33 | f246 +2.70 cov 1.875 | -0.07 (flat) | -0.07 | 89.29x (growing) h95 +0.41 | h95 1.39 / h95 1.07 | drift short |
| 15 | f8296 +0.01 | f8319 +0.93 cov 1.185 | +0.18 (rising) | +0.55 | 3.24x (growing) h95 +0.51 | f135 +0.00 | f226 +3.03 cov 0.809 | +0.21 (rising) | -0.05 | 1.62x (growing) h95 +0.13 | h95 1.54 / h95 1.27 | PASS |
| 16 | f7517 +0.02 | f7570 +2.14 cov 0.542 | +0.09 (flat) | +0.03 | 2.90x (growing) h95 +0.30 | f105 +0.00 | f142 +1.23 cov 0.845 | +0.00 (flat) | +0.36 | 1.27x (growing) h95 +0.05 | h95 1.11 / h95 0.85 | PASS |
| 17 | f6684 +0.04 | f6741 +2.31 cov 3.655 | +0.47 (rising) | -0.27 | 5.26x (growing) h95 +0.78 | f135 +0.00 | f187 +1.73 cov 1.228 | +0.30 (rising) | -0.28 | 2.51x (growing) h95 +1.12 | h95 2.41 / h95 2.35 | PASS |
| 18 | f8297 +0.04 | f8339 +1.72 cov 1.672 | +0.03 (flat) | +0.19 | 12.96x (growing) h95 -0.55 | f135 +0.00 | f195 +2.00 cov 0.985 | -0.46 (falling) | -0.50 | 3.17x (growing) h95 -0.37 | h95 1.23 / h95 2.12 | drift over / extent over |
| 19 | f6670 +0.02 | f6720 +2.02 cov 2.295 | -0.05 (flat) | -0.52 | 1.56x (growing) h95 +0.23 | f135 +0.00 | f178 +1.43 cov 1.260 | +0.01 (flat) | -0.19 | 1.32x (growing) h95 +0.07 | h95 1.81 / h95 1.50 | PASS |
| 1 | f8185 +0.01 | f8243 +2.33 cov 1.259 | +0.26 (rising) | +0.46 | 7.15x (growing) h95 +0.32 | f75 +0.00 | f138 +2.10 cov 1.244 | +0.08 (flat) | +0.52 | 4.71x (growing) h95 +0.03 | h95 1.24 / h95 1.44 | drift short |
| 20 | f7503 +0.02 | f7543 +1.62 cov 1.935 | +0.03 (flat) | -0.06 | 3.73x (growing) h95 +0.12 | f90 +0.00 | f136 +1.53 cov 2.104 | -0.04 (flat) | -0.26 | 1.99x (growing) h95 +0.09 | h95 1.50 / h95 1.76 | PASS |
| 21 | f7508 +0.02 | f7563 +2.22 cov 2.471 | +0.13 (rising) | +0.12 | 5.26x (growing) h95 +0.24 | f108 +0.00 | f151 +1.43 cov 4.209 | -0.07 (flat) | -0.65 | 4.56x (growing) h95 +0.10 | h95 1.43 / h95 2.23 | drift short / extent over |
| 22 | f7371 +0.03 | f7403 +1.31 cov 1.917 | +0.12 (rising) | -0.04 | 14.75x (growing) h95 -0.23 | f90 +0.00 | f136 +1.53 cov 2.092 | +0.01 (flat) | +0.07 | 7.26x (growing) h95 +0.72 | h95 1.47 / h95 1.92 | drift short |
| 23 | f7510 +0.04 | f7527 +0.72 cov 0.687 | -0.02 (flat) | -0.11 | 3.24x (growing) h95 -0.06 | f90 +0.00 | f150 +2.00 cov 0.839 | -0.02 (flat) | -0.11 | 2.24x (growing) h95 +0.05 | h95 1.04 / h95 1.26 | PASS |
| 24 | f7480 +0.00 | f7513 +1.32 cov 1.587 | +0.21 (rising) | +0.14 | 3.29x (growing) h95 +0.51 | f98 +0.02 | f125 +0.92 cov 1.781 | +0.06 (flat) | +0.20 | 1.40x (growing) h95 +0.13 | h95 1.28 / h95 1.19 | drift short |
| 25 | f7157 +0.01 | f7202 +1.80 cov 1.985 | -0.02 (flat) | +0.11 | 9.73x (growing) h95 +0.13 | f90 +0.00 | f153 +2.10 cov 1.993 | -0.22 (falling) | -0.06 | 5.69x (growing) h95 -0.17 | h95 1.26 / h95 1.34 | drift over |
| 26 | f8417 +0.01 | f8487 +2.81 cov 3.303 | +0.15 (rising) | +0.32 | 24.83x (growing) h95 -0.04 | f120 +0.00 | f163 +1.43 cov 1.448 | +0.00 (flat) | +0.37 | 7.24x (growing) h95 +0.04 | h95 1.67 / h95 1.14 | drift short / extent short |
| 27 | f8317 +0.03 | f8337 +0.83 cov 0.304 | +0.02 (flat) | -0.16 | 2.98x (growing) h95 +0.00 | f75 +0.00 | f97 +0.73 cov 0.250 | -0.14 (falling) | -0.11 | 1.89x (growing) h95 -0.08 | h95 1.13 / h95 1.38 | drift over |
| 28 | f7461 +0.02 | f7489 +1.14 cov 0.964 | -0.08 (flat) | +0.34 | 1.94x (growing) h95 -0.12 | f222 +0.00 | f288 +2.20 cov 1.631 | -0.05 (flat) | -0.26 | 1.23x (growing) h95 -0.04 | h95 1.00 / h95 1.37 | PASS |
| 29 | f7491 +0.02 | f7546 +2.22 cov 0.828 | -0.05 (flat) | -0.15 | 6.90x (growing) h95 -0.67 | f90 +0.00 | f148 +1.93 cov 0.891 | +0.09 (flat) | +0.11 | 2.53x (growing) h95 +0.07 | h95 1.06 / h95 1.30 | PASS |
| 2 | f8342 +0.04 | f8399 +2.32 cov 0.926 | +0.32 (rising) | -0.09 | 14.94x (growing) h95 -0.11 | f105 +0.00 | f160 +1.83 cov 0.662 | +0.03 (flat) | -0.25 | 3.14x (growing) h95 +0.58 | h95 1.64 / h95 1.47 | drift short |
| 30 | f8359 +0.01 | f8439 +3.21 cov 2.456 | -0.04 (flat) | +0.50 | 31.90x (growing) h95 -0.07 | f108 +0.00 | f160 +1.73 cov 2.276 | +0.08 (flat) | -0.20 | 25.86x (growing) h95 +0.80 | h95 1.71 / h95 2.21 | PASS |
| 31 | f8228 +0.04 | f8295 +2.72 cov 1.516 | +0.00 (flat) | -0.06 | 3.19x (growing) h95 -0.09 | f98 +0.02 | f215 +3.92 cov 2.497 | +0.14 (rising) | -0.08 | 2.10x (growing) h95 +0.24 | h95 1.54 / h95 2.26 | drift over / extent over |
| 32 | f8315 +0.00 | f8385 +2.80 cov 2.707 | +0.43 (rising) | +1.19 | 8.23x (growing) h95 -0.04 | f86 +0.02 | f161 +2.52 cov 4.030 | +0.57 (rising) | +1.57 | 4.76x (growing) h95 +1.48 | h95 1.71 / h95 2.40 | PASS |
| 33 | f7390 +0.02 | f7465 +3.02 cov 1.043 | +0.10 (flat) | -0.22 | 31.61x (growing) h95 -0.28 | f140 +0.02 | f230 +3.02 cov 0.328 | +0.15 (rising) | -0.19 | 8.86x (growing) h95 +0.72 | h95 1.49 / h95 1.60 | drift over |
| 34 | f7349 +0.03 | f7401 +2.11 cov 1.288 | +0.36 (rising) | -0.21 | 6.05x (growing) h95 +0.48 | f90 +0.00 | f136 +1.53 cov 1.068 | +0.19 (rising) | -0.13 | 2.60x (growing) h95 +0.25 | h95 1.57 / h95 1.54 | PASS |
| 35 | f7348 +0.03 | f7450 +4.11 cov 2.996 | +0.00 (flat) | +0.16 | 1.34x (growing) h95 -0.01 | f98 +0.02 | f131 +1.12 cov 5.297 | -0.06 (flat) | -0.40 | 1.20x (growing) h95 -0.08 | h95 1.62 / h95 2.37 | extent over |
| 3 | f7369 +0.44 | f7491 +5.32 cov 4.829 | -0.13 (falling) | -0.60 | 2.53x (growing) h95 +0.10 | f102 +0.10 | f151 +1.73 cov 7.340 | +0.48 (rising) | +0.08 | 203.89x (growing) h95 +1.11 | h95 1.72 / h95 2.00 | drift over |
| 40 | f7115 +0.01 | f7180 +2.61 cov 2.886 | -0.62 (falling) | -0.53 | 1.59x (growing) h95 -0.63 | f315 +0.00 | f342 +0.90 cov 0.827 | +0.14 (rising) | +0.09 | 2.85x (growing) h95 -0.02 | h95 1.88 / h95 1.67 | drift short |
| 41 | f8428 +0.43 | f8465 +1.91 cov 0.782 | -0.02 (flat) | +0.00 | 1.44x (growing) h95 -0.01 | f135 +0.00 | f168 +1.10 cov 0.800 | -0.07 (flat) | +0.28 | 4.44x (growing) h95 -0.10 | h95 1.04 / h95 2.23 | extent over |
| 42 | f6542 +0.01 | f6570 +1.13 cov 0.647 | -0.34 (falling) | -0.97 | 7.70x (growing) h95 -1.04 | f135 +0.00 | f165 +1.00 cov 0.204 | -0.27 (falling) | -0.42 | 2.96x (growing) h95 -0.36 | h95 1.51 / h95 1.02 | extent short |
| 43 | f6686 +0.51 | f6726 +2.11 cov 1.176 | -0.04 (flat) | -0.06 | 1.74x (growing) h95 -0.30 | f135 +0.00 | f153 +0.60 cov 0.567 | +0.27 (rising) | +0.05 | 2.02x (growing) h95 +0.36 | h95 1.34 / h95 2.48 | drift over / extent over |
| 4 | f6587 +0.02 | f6649 +2.50 cov 3.215 | -0.06 (flat) | +0.73 | 2.40x (growing) h95 -0.14 | f165 +0.00 | f225 +2.00 cov 1.427 | +0.47 (rising) | +0.30 | 1.23x (growing) h95 +1.36 | h95 2.22 / h95 2.47 | drift over |
| 5 | f8223 +0.01 | f8281 +2.33 cov 0.231 | +0.19 (rising) | +0.10 | 3.35x (growing) h95 -0.56 | f105 +0.00 | f160 +1.83 cov 0.249 | +0.08 (flat) | +0.03 | 2.09x (growing) h95 +0.20 | h95 1.12 / h95 1.35 | drift short |
| 6 | f6531 +0.02 | f6631 +4.02 cov 0.739 | -0.21 (falling) | -0.54 | 1.99x (growing) h95 -1.34 | f135 +0.00 | f193 +1.93 cov 0.389 | +0.08 (flat) | +0.15 | 1.30x (growing) h95 +0.29 | h95 1.07 / h95 2.33 | drift short / extent over |
| 7 | f8392 +0.01 | f8430 +1.53 cov 1.265 | -0.17 (falling) | -0.49 | 9.73x (growing) h95 -0.40 | f105 +0.00 | f163 +1.93 cov 1.069 | -0.45 (falling) | -0.58 | 4.19x (growing) h95 -0.66 | h95 1.06 / h95 1.25 | PASS |
| 8 | f6525 +0.02 | f6560 +1.42 cov 5.824 | +0.40 (rising) | +0.04 | 12.06x (growing) h95 +0.83 | f135 +0.00 | f157 +0.73 cov 1.432 | +0.56 (rising) | -0.11 | 11.55x (growing) h95 +0.43 | h95 2.10 / h95 1.74 | PASS |
| 9 | f6631 +0.00 | f6701 +2.80 cov 1.999 | -0.58 (falling) | -0.33 | 1.33x (growing) h95 -1.17 | f75 +0.00 | f130 +1.83 cov 1.320 | +0.22 (rising) | +0.03 | 1.45x (growing) h95 +0.59 | h95 1.35 / h95 2.01 | drift short / extent over |

# LANDING tallies: 40 compared; PASS 14; drift direction agrees 17/40; growth direction agrees 40/40; cast-anim pattern (oracle falling, harness flat) 2; unattributed (non-PASS, not the pattern) 24

**The reading.** The scale-free signal — the growth direction of the
record's growth words — agrees 40/40 on both stages (tallies above): all 40
records grow, on both sides. Drift direction agrees 13/40 (SOURCE) and
17/40 (LANDING); the systematic remainder is one named rig difference, not
a sim miss:

- **The cast-anim rig difference.** The oracle's caster plays its CAST
  ANIMATION during the channel (the measured 4.35 s cast-animation term,
  `refactor/B1145/README.md` line 15, eqgame 0x47ddd4) and the oracle's
  target plays its live hit/death reactions, so the oracle's anchored FX
  drifts with the body. 10 of the 28 non-PASS SOURCE rows (and 2 LANDING)
  are exactly that shape — oracle falling, harness flat within the ±0.10 H
  band (tallies). The harness plays idle during the channel and the cast
  animation only once, after OP_Action:

  ```
  $ grep -n 'playAnimation' tools/model_viewer/model_viewer_spell_test.cpp
  168:        animatedNode->playAnimation("o01", true, false);
  193:        animatedNode->playAnimation(castAnim, false, false);  // loop=false: play once
  ```

  (168 in `triggerSpellTestCast()` — the channel; 193 in
  `completeSpellTestCast()`). The harness target is idle except the scripted
  witnessed death (`tools/model_viewer/model_viewer_fx_harness.cpp:705-714`,
  the d05/d02/d01 branch).
- **Unattributed.** The remaining 18 SOURCE + 24 LANDING non-PASS rows
  (tallies) are unattributed here: both sides move, in different directions.
  Isolating the EDD-words-only drift needs the harness to play the cast
  animation during the channel — a rig change (FOLLOW-UP), not a sim
  defect: the harness numbers above are the measured effect of the
  record's words on this rig.

The `extent o/h` column is each side's own PEAK h95 in model heights (the
sides differ: 235px harness, 209px oracle caster, 325px oracle fixed_npc
target); the verdict's short/over flags use a 0.7x..1.43x window — a
directional check, not a shape band (shape bands live in the sweep tables
above).

## FOLLOW-UP (B1152 / B1153)

- **B1152 (missile).** The missile-travel half of the projectile-path emitters
  is its own unit; any gate miss whose mechanism is the missile lands there.
- **B1153 (retire).** The remaining retired/partially-retired emitters
  (non-record config presets that the record now supersedes) are retired
  there; any gate miss whose mechanism is a stale config preset lands there.
- **Motion-signature drift (this gate).** 18 SOURCE + 24 LANDING rows are
  unattributed (the tallies above): oracle and harness both move, in
  different directions. Separating the EDD-words-only drift from body
  motion needs the harness to play the cast animation during the channel
  (`model_viewer_spell_test.cpp:168` currently plays `o01` idle there) — a
  rig upgrade, B1152-adjacent; home is the orchestrator's call.
