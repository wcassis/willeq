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
  8 torus `0x10061e7d` (B0 != 0 alternate `0x10061e93`); 9 ring random
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
entries; regenerated with `refactor/B1150/scripts/gen_shape_bands.py --census`
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

The 40 gate records' slot rows (300 rows) use only shapes
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
  (41 source + 40 target + 30 override `spawnShape`/`spawnExtents` lines
  removed by `scripts/retire_shapes.py`, idempotent). Their measured
  `lo/hi/halfWidth/centroid` rows survive as BANDS in
  `tests/spell_fx_shape_oracle.h` (generated by
  `scripts/gen_shape_bands.py` over the pre-retirement config) — the gate
  compares the harness rows against those bands, not against inputs.

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
`review/B1139/scripts/harness_measure.py`. **The gate tables are PENDING —
the sweep was in flight when this run ended; see the task's kanban block
note for the exact collection commands.** Baselines: `review/B1149/sweep_table.tsv`
(no row may regress beyond its p25..p75 spread) and
`review/B1142/gate/regression_table.tsv` (the 93 reference is 3.049, NOT 2.938).

### Known difference (documented, not a defect)

Spell 93 landing coverage: B1150 EDD shapes 2.742 vs the oracle (official
client EDD) 3.945 vs B1149 config 4.233. The spawn geometry is verified
byte-identical to the decompile for the shapes 93 uses (shape 5 dims,
decompile lines 68860-68906; the sustained pillar is emitter 280 with the
record's own life/duration words). The residual gap is SPRITE-FIDELITY
coverage (B1149's domain: populations/sizes/alpha), not spawn geometry.

## FOLLOW-UP

- B1151: frames/spin/modes (+0x130.., sprite animation) — separate card.
- B1152: missile/projectile shapes — separate card (its trace is in
  `refactor/B1152/`).
- B1153: retire the remaining `spellOverrides` rows once their measured
  values come from the records.
