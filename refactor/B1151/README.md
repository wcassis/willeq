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
- **+0x64 depth write.** The renderer turns depth write OFF for the emitter's
  draw and restores it after (the TSV +0x64 row: `SetRenderState(ZWRITE, 0)`
  then restored to 1).
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
`ctest -R 'SpellEdd'` (18 tests).

- **Reader decode.** `decodeSpellsNewEddMotion` on a synthetic 416-byte entry:
  every word round-trips; a blank entry is all-zero.
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
40-record sweep (`refactor/B1150/scripts/tour7.sh` + `run_sweep.sh`, rendered
on the ThinkPad, measured here), compared to `refactor/B1150/README.md` "The
gate" tables (the shapes baseline), `review/B1142` (93 reference 3.049),
`review/B1130/record_table.tsv`, and the motion signatures the issue asks for
(centroid drift over time and extent growth per record vs the oracle strips
`review/B1130/strips/record_<N>.png` + `review/B1130/scripts/anchor.py`
per-frame rows). Per row: PASS / short / over per column, owner of every miss
(B1152 missile, B1153 retire, "unattributed"). No row may regress vs the B1150
table by more than its p25..p75 spread without the mechanism named.

<!-- GATE-TABLES -->

## Remaining steps (for the continuation card)

The core (reader, sim, renderer, B1145 removal, 18 unit tests, census) is
landed and committed (see `git log`). What remains, in order:

1. **Build the gate tree + smoke-test on the ThinkPad**:
   `rbuild <worktree> --target model_viewer` (rbuild tree =
   `/home/user/willeq-builds/kb-b1151-frames/build`); on the ThinkPad run one
   scene via `refactor/B1151/scripts/run_scene.sh b1151_smoke 30 --seed 1`
   (a tour plan) to confirm the model_viewer + client files + eqe1 pack
   resolve. The three gate scripts (`run_scene.sh`, `tour7.sh`,
   `run_sweep.sh`) are copies of the B1150 scripts retargeted to this
   worktree; fix any path issue there.
2. **tour-7** (fast): `tour7.sh b1151`; collect
   `rsync -a thinkpad:/home/user/src/willeq-wt/kb-b1151-frames/refactor/B1151/runs/ ./refactor/B1151/runs/`.
3. **40-record sweep** (~2 h, start early in the background):
   `run_sweep.sh b1151`.
4. **Gate tables + motion signatures**: per record, centroid drift over time
   and extent growth vs the oracle strips (`review/B1130/strips/record_<N>.png`
   + `review/B1130/scripts/anchor.py` per-frame rows), banded against
   `refactor/B1150/README.md` "The gate" (shapes baseline), `review/B1142`
   (93 reference 3.049) and `review/B1130/record_table.tsv`. Per row: PASS /
   short / over per column, owner of every miss (B1152 missile, B1153 retire,
   "unattributed"); no row may regress vs the B1150 table by more than its
   p25..p75 spread without the mechanism named.
   `scripts/gate_table_reference.py` is the B1150 gate table (shape bands) —
   adapt it to emit the motion-signature columns (the render scripts +
   `harness_measure.py` output are unchanged; only the table assembly differs).
5. **Paste the gate tables into the README** (replace the `GATE-TABLES` marker).
6. **Final verification + MR**: `ctest -R 'Spell|Bridge|Particle|FxHarness|ConfigKeyReachability|Oracle'`
   + hygiene via rbuild; ONE whole-tree CI-flag build; push WITH CI (no
   `-o ci.skip`); open MR "Closes #471"; confirm pipeline green at head.
7. **`kanban complete`** first line: "B1151 landed at <full sha>: MR !<iid>
   pipeline <id> <status>".

Known PARTIALs carried into the gate (see "PARTIAL"): the +0x178
column→axis-letter convention, mode-1 streak vertex construction, mode-2
world-quad vertex construction. If the gate shows a band of short/over rows
sharing a mechanism, name that mechanism (do not tune per-row).

## FOLLOW-UP (B1152 / B1153)

- **B1152 (missile).** The missile-travel half of the projectile-path emitters
  is its own unit; any gate miss whose mechanism is the missile lands there.
- **B1153 (retire).** The remaining retired/partially-retired emitters
  (non-record config presets that the record now supersedes) are retired
  there; any gate miss whose mechanism is a stale config preset lands there.
