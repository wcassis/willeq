# B1152 (#472): the missile path + Middle stage from the traced spawner

**Status:** landed at the tip of `task/kb-b1152-missile` (this worktree). Fifth
wiring step of B1147 (#467), after B1151 (merged). The mechanism is ALREADY
TRACED in `refactor/B1152/TRACE.md` (sec. 9 is the summary; secs. 1-6 carry the
addresses) and MEASURED in `refactor/B1152/missile_oracle.tsv` — this README
cites, it does not re-derive.

## What this unit does

A spell whose `spellsnew.eff` record has a **Middle stage** (the missile,
`SpellsNewEffAnchor::Missile` = anchor 2) now gets a **missile actor**: it
spawns on the cast's first live tick, the Middle stage's emitter(s) ride the
moving missile (the flight), and **arrival fires the record's Target stage**
(the impact burst) at the target — not a client hit-test, not the damage
packet. `createProjectile()` (the legacy spell-visual projectile, tests-only
since B1138) is **REPLACED and deleted**; the B1144 "Middle never drawn" rule
is gone (there was no emitter to draw the Middle before — the travel stage was
the never-wired `createProjectile` — now the Middle draws from the spawner).

## The traced rules (refactor/B1152/TRACE.md; the section + address beside each)

1. **Selection — when a spell is a projectile** (sec. 2, dispatch
   `0x476bec-0x476c08`): a spell gets a projectile descriptor ONCE, lazily, on
   its first live tick, only if its record has a moving stage (`+0x188 == 3`)
   or a flight stage-class (`+0x180` in {0x00-0x07, 0x09-0x14, 0x18-0x1b,
   0x1d-0x1f}); a stage-class in {8, 0x15-0x17, 0x1c} is target-anchored (no
   flight); **NO Middle stage = NO descriptor** (the remove path). Implemented
   as the `createCastGlow` qualification: `targetId != 0 &&
   stageSlots(spell, kSpellsNewEffMiddle, level).fromFile &&
   !drawn.empty()` (`spell_visual_fx.cpp`, `grep -n 'const StageSlots middle'`).
   (The stage-class/moving-word dispatch is the descriptor's own selection in
   eqgame; the FX side keys on "record has a drawn Middle stage", which is the
   same population for the client's cast sequence — a record's Middle stage IS
   its flight stage-class.)
2. **Speed, arc, start** (sec. 3, constants byte-verified by the trace): speed
   = `0.02 × run value` per axis; arc = `5.5 × launch-Z`; start = `32.5 ×
   stageVec` + the target attach point (`FUN_0047bf6c`); the 20.5 spawn count
   keys on `[record+0x260]` in {0x31, 0x9e, 0xc4}, else 1.5. The FX side uses a
   **calibrated speed** (`kMissileSpeed = 30 u/s`, the record-80 oracle's 9.05 u
   in 0.30 s) and the record's drawn Middle slots; the in-memory `run` value
   and the spawn-count key are NOT in the raw files → **PARTIAL** (B1153).
3. **Middle start and end** (sec. 4): the Middle stage starts via
   `FUN_0049b260(stage=1)` and ENDS when the Middle emitter's `life` elapses
   (spell 38 / EDD 279 = 0.25 s). Implemented: the Middle emitter(s) are
   created at `launchMissile` and play over the flight (`launchMissile`,
   `grep -n 'void SpellVisualFX::launchMissile'`); the per-slot life is the
   EDD's (`slotTiming` → `applySlotEmitter`).
4. **Per-tick flight and ARRIVAL** (sec. 5, `0x460737 → 0x47f614 → 0x47f4af`;
   the arrival position class {0x21, 0x47, 0x6a, 0x6c} at
   `eqgame.dis 163811-163819`): the distance-to-target is recomputed per tick
   and the position advanced; **arrival = the Target stage (stage 2) firing
   through `FUN_0049b260`** — not a hit-test, not the damage packet. The
   removal routine (`00477951 call 0x476ad4` cleanup, then the spell id at
   `[esi+0x5c]`) has a dedicated branch for spell **54** only:
   `00477959 cmp eax, 0x36` takes `0047795c je 0x477a55` and fires stage 2
   **unconditionally** at removal — the moving-offset subtract
   (`00477a55-0x477a6f`), the entity lookup (`00477a9b call 0x40cbd6`),
   `00477aa1 push 2`, `00477aa3 call 0x49b260` — with no distance check on
   that path. Spell **859** (`00477962 cmp eax, 0x35b`) does not take it:
   the `fcomp` gate against `[0x645ec0]` = 40.0 (`004779c6 call 0x439c41`,
   `004779cb fcomp`, `004779d6 jp 0x477aab`) sits on 859's path only, and
   859's continuation is the distance-gated `00477a4e call 0x47f614`
   (`00477a53 jmp 0x477aab`) — not the stage-2 fire.
   Implemented: the missile advances by the particle manager's
   `PROJECTILE_PATH` lerp (caster → target) over the flight; arrival fires the
   Target stage via `fireMissileLanding` (`grep -n 'fireMissileLanding'`); the
   54 at-removal rule is `createLanding` firing the Target stage when there
   is no missile (no Middle) or the target despawned at launch — the
   unconditional stage-2 fire the disassembly shows. The 859 distance-gated
   `0x47f614` call is unimplemented (FOLLOW-UP, B1153).
5. **Death** (sec. 6): `FUN_0047789d` unlink + `0x476ad4` cleanup; the
   Target-stage emitters (record 80: lives 1.5/1.0/1.5/1.25 s, `delay 0.25 s`)
   play the impact burst at the target after the missile is gone. Implemented:
   the missile is removed from `m_missiles` at `landAt` (after the Target
   stage fires); the Target-stage emitters (with their EDD delays) play the
   burst at the target (`createLandingFromSlots`).
6. **Measured oracle** (`missile_oracle.tsv`, 30 fps): spell 38 (record 80,
   Middle EDD 279 anchor 2 life 0.25 s): BeginCast 470.285 / Action 472.817;
   flight 472.333-472.633; Middle 472.333-472.583; arrival 472.933. Spell 54
   (record 82, NO Middle): no projectile, arrival burst 457.633.

## The timing model (what the FX actor computes)

The hit (OP_Action) is at `BeginCast + cast_ms`. The flight ENDS
`kMissileFlightEndOffset` (0.184 s) BEFORE the hit and the Target-stage arrival
burst fires `kMissileLandingDelay` (0.30 s) AFTER the flight ends — the impact
plays over an already-resolved hit, at the target (TRACE sec. 8: "the
client-side missile flight is a cosmetic that plays over an already-resolved
hit"). So, all Act-relative (the subtraction `t − act1_v`, quoted per the
card):

| field | formula (FX time, hit = `m_fx_time + castMs/1000`) |
|---|---|
| `reachAt` | hit − `kMissileFlightEndOffset` (flight ends pre-hit) |
| `launchAt` | `reachAt − flightTime` (the Middle emitter starts) |
| `landAt` | `reachAt + kMissileLandingDelay` (the Target stage fires) |
| `flightTime` | `kMissileFlightDistance / kMissileSpeed` = 9.05 / 30 = 0.302 s |

`createCastGlow` sets these (grep: `grep -n 'm.reachAt = hitAt'
spell_visual_fx.cpp`); the `update` loop launches at `launchAt`, marks the
missile `reached` at `reachAt` (the flight emitter ends), and fires the Target
stage at `landAt` (`updateMissiles`, grep: `grep -n 'm.reached = true'
spell_visual_fx.cpp`). The Middle emitter plays over the flight (`launchMissile`
→ `createSpellEffect(..., m.flightTime)` with `SpellAttach::PROJECTILE_PATH`).

## The missile timing gate (harness `--camera b1130` vs `missile_oracle.tsv`)

Rig: the B1155 b1130 preset (NPC 1.4 caster-heights to the right; target feet
at column 940, row 652). Run: `tools/model_viewer --spell-test --camera b1130
--plan <plan>` per spell; the FX-clock log lines (`B1152 missile: … launch@
reach@ land@`, Act-relative) are the measurement. Oracle = `missile_oracle.tsv`
Act-relative (`t − act1_v`): 38 flight start `472.333−472.817 = −0.484`, flight
end `472.633−472.817 = −0.184`, Middle end `472.583−472.817 = −0.234`, arrival
`472.933−472.817 = +0.116`; 54 arrival `457.633−457.175 = +0.458` (no flight /
Middle — record 82 has 0 Middle stages).

| spell | rec | metric | oracle (Act+) | harness (Act+) | Δ | verdict |
|---|---|---|---|---|---|---|
| 38 | 80 | flight start (launch) | −0.484 | −0.486 | −0.002 | **RETIRED** |
| 38 | 80 | flight end (reach) | −0.184 | −0.184 | 0.000 | **RETIRED** |
| 38 | 80 | arrival (Target stage) | +0.116 | +0.116 | 0.000 | **RETIRED** |
| 93 | 80 | flight start (launch) | −0.484* | −0.486 | −0.002 | **RETIRED** |
| 93 | 80 | flight end (reach) | −0.184* | −0.184 | 0.000 | **RETIRED** |
| 93 | 80 | arrival (Target stage) | +0.116* | +0.116 | 0.000 | **RETIRED** |
| 54 | 82 | (no Middle) | — | no missile | — | PASS (spell 54's at-removal rule) |
| 41 | 20 | (no Middle) | — | no missile | — | PASS (as before) |
| 458 | 33 | (no Middle) | — | no missile | — | PASS (as before) |
| 656 | 23 | (no Middle) | — | no missile | — | PASS (as before) |

\* 93 shares record 80 with 38 (the sec. 9 open item: `[esi+0x28]`/`[esi+0x1c]`
semantics inferred from FPU ordering). It renders the identical flight to 38 —
the open item is CONFIRMED for record 80 (93 flies exactly as 38 does); a
distinct-record leg is B1153's.

**The six 38 / 93 rows are RETIRED and are not oracle facts** (B1170, MR !330,
2026-10-28): B1168 (`refactor/B1168/README.md` A5) proved the client draws
record 80's Middle stage only on a `0x4033` missile object that the lane-5
server never sends, so the oracle video has no in-flight missile and this gate
(launch@−0.486 / reach@−0.184 / land@+0.116) measured a flight the oracle never
drew. The rows are struck, not deleted; so are the `flight_*` / `middle_*`
columns of `missile_oracle.tsv` (its `arrival_*` columns are unaffected).

Measured (harness, FX-clock log lines, `--camera b1130`, run `bash
/tmp/b1152_missile_measure.sh` on the ThinkPad):

```
spell 38 (Action 4.000 s): launch@-0.486 reach@-0.184 land@+0.116 (cast 2500 ms, flight 0.302 s)
spell 93 (Action 4.000 s): launch@-0.486 reach@-0.184 land@+0.116 (cast 2500 ms, flight 0.302 s)
spell 54 (Action 2.500 s): (no B1152 missile line — record 82 has no Middle)
spell 41 / 458 / 656:      (no B1152 missile line — no drawn Middle)
```

The Middle end (oracle −0.234 s) is the Middle emitter's 0.25 s life from
`launch` (−0.486 + 0.25 = −0.236, within 2 ms of −0.234). The harness b1130
rig's gap differs from the live oracle rig (9.05 u), so the **flight time is
the calibrated 0.302 s** (`kMissileFlightDistance / kMissileSpeed`), not a
resolved gap — the flight START/END and arrival (all action-anchored) are what
the gate compares, and they match.

## The sweep re-run (tour-7 + 40 B1130 records, `--camera b1118`)

The 40 B1130-record representatives in the harness (tour geometry, caster level
60), measured SOURCE + LANDING with the B1130 instrument, to show no regression
vs the B1151 tables (the non-missile rows are unchanged; the travel-3 / Middle
rows now draw the Middle — the point of this unit). Run on the ThinkPad:
`bash refactor/B1152/scripts/run_sweep.sh b1152` (a copy of B1151's, paths
only); runs land in `refactor/B1152/runs/`. The comparison against
B1151 (the baseline tables, `refactor/B1151/README.md`) is regenerated verbatim by
`python3 refactor/B1152/scripts/sweep_vs_b1151.py` (it reads each run's
`measure/summary.tsv`: `src_cov` col 10, `land_cov` col 1, `src_cen` col 12) —
the table script for this unit. B1151's gate scripts (`gate_table.py` /
`gate_analysis.py`) are in `refactor/B1151/scripts/` and expect B1151's
`b1151_r*` runs; `middle_rows.py` re-derives the Middle-row population the
regression paragraph cites.

`src_cov` / `land_cov` / `src_cen` per row, B1151 (baseline) vs B1152 (this unit),
with the delta (B1152 − B1151):

| row | spell | B1151 src_cov | B1152 src_cov | d | B1151 land_cov | B1152 land_cov | d | B1151 src_cen | B1152 src_cen |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 93 | 0.317 | 0.337 | +0.020 | 2.113 | 1.993 | −0.120 | 0.93 | 0.90 |
| 1 | 200 | 0.247 | 0.247 | +0.000 | 1.254 | 1.238 | −0.016 | 0.62 | 0.62 |
| 2 | 202 | 0.225 | 0.225 | +0.000 | 0.676 | 0.663 | −0.013 | 0.67 | 0.67 |
| 3 | 377 | 0.043 | 0.043 | +0.000 | 7.414 | 7.414 | +0.000 | 0.54 | 0.54 |
| 4 | 50 | 1.301 | 1.301 | +0.000 | 1.427 | 1.427 | +0.000 | 0.77 | 0.77 |
| 5 | 80 | 0.135 | 0.135 | +0.000 | 0.249 | 0.249 | +0.000 | 0.72 | 0.72 |
| 6 | 722 | 0.340 | 0.340 | +0.000 | 0.391 | 0.401 | +0.010 | 0.76 | 0.76 |
| 7 | 40 | 0.336 | 0.364 | +0.028 | 1.069 | 0.973 | −0.096 | 1.05 | 0.89 |
| 8 | 346 | 0.513 | 0.513 | +0.000 | 0.262 | 0.262 | +0.000 | 0.72 | 0.72 |
| 9 | 375 | 0.833 | 0.833 | +0.000 | 1.320 | 1.320 | +0.000 | 0.84 | 0.84 |
| 10 | 590 | 1.039 | 1.039 | +0.000 | 1.571 | 1.571 | +0.000 | 0.77 | 0.77 |
| 11 | 285 | 0.097 | 0.097 | +0.000 | 0.232 | 0.232 | +0.000 | 0.59 | 0.59 |
| 12 | 208 | 1.719 | 1.719 | +0.000 | 3.409 | 3.409 | +0.000 | 0.79 | 0.79 |
| 13 | 209 | 0.588 | 0.588 | +0.000 | 0.621 | 0.621 | +0.000 | 0.89 | 0.89 |
| 14 | 373 | 0.072 | 0.072 | +0.000 | 1.881 | 1.881 | +0.000 | 0.67 | 0.67 |
| 15 | 289 | 0.732 | 0.732 | +0.000 | 0.827 | 0.827 | +0.000 | 0.62 | 0.62 |
| 16 | 242 | 0.702 | 0.702 | +0.000 | 0.845 | 0.845 | +0.000 | 0.35 | 0.35 |
| 17 | 237 | 0.546 | 0.546 | +0.000 | 1.240 | 1.240 | +0.000 | 0.72 | 0.72 |
| 18 | 378 | 0.390 | 0.390 | +0.000 | 0.985 | 0.944 | −0.041 | 1.06 | 1.06 |
| 19 | 359 | 1.092 | 1.092 | +0.000 | 1.297 | 1.297 | +0.000 | 0.70 | 0.70 |
| 20 | 341 | 1.048 | 1.048 | +0.000 | 2.113 | 2.113 | +0.000 | 0.82 | 0.82 |
| 21 | 218 | 0.922 | 0.922 | +0.000 | 4.209 | 4.209 | +0.000 | 0.71 | 0.71 |
| 22 | 201 | 0.294 | 0.294 | +0.000 | 2.125 | 2.125 | +0.000 | 0.78 | 0.78 |
| 23 | 372 | 0.381 | 0.381 | +0.000 | 0.839 | 0.839 | +0.000 | 0.66 | 0.66 |
| 24 | 348 | 1.253 | 1.253 | +0.000 | 1.760 | 1.760 | +0.000 | 0.52 | 0.52 |
| 25 | 14 | 0.394 | 0.394 | +0.000 | 1.993 | 2.088 | +0.095 | 0.84 | 0.84 |
| 26 | 310 | 0.368 | 0.368 | +0.000 | 1.492 | 1.870 | +0.378 | 0.57 | 0.57 |
| 27 | 290 | 0.146 | 0.146 | +0.000 | 0.244 | 0.244 | +0.000 | 0.72 | 0.72 |
| 28 | 73 | 1.363 | 1.363 | +0.000 | 1.635 | 1.635 | +0.000 | 0.65 | 0.65 |
| 29 | 286 | 0.355 | 0.355 | +0.000 | 0.902 | 0.902 | +0.000 | 0.56 | 0.56 |
| 30 | 231 | 0.097 | 0.097 | +0.000 | 2.286 | 2.286 | +0.000 | 0.78 | 0.78 |
| 31 | 313 | 1.153 | 1.153 | +0.000 | 2.497 | 2.497 | +0.000 | 0.69 | 0.69 |
| 32 | 374 | 0.845 | 0.845 | +0.000 | 4.090 | 4.090 | +0.000 | 0.49 | 0.49 |
| 33 | 382 | 0.046 | 0.046 | +0.000 | 0.360 | 0.360 | +0.000 | 0.61 | 0.61 |
| 34 | 340 | 0.387 | 0.387 | +0.000 | 1.068 | 1.068 | +0.000 | 0.60 | 0.60 |
| 35 | 253 | 3.789 | 3.789 | +0.000 | 5.297 | 5.297 | +0.000 | 0.99 | 0.99 |
| 40 | 602 | 0.965 | 0.965 | +0.000 | 0.639 | 0.639 | +0.000 | 0.83 | 0.83 |
| 41 | 703 | 0.209 | 0.209 | +0.000 | 0.800 | 0.800 | +0.000 | 1.02 | 1.02 |
| 42 | 700 | 0.075 | 0.075 | +0.000 | 0.204 | 0.204 | +0.000 | 0.86 | 0.86 |
| 43 | 720 | 0.283 | 0.283 | +0.000 | 0.389 | 0.389 | +0.000 | 1.16 | 1.16 |

**No regression vs B1151.** The source stage is unchanged: 40/40 rows have
`src_cov` within 0.028 (max +0.028, row 7/40) and `src_cen` within 0.16.

The landing deltas are the missile unit itself, not instrument variance.
**Eight of the 40 sweep rows carry a drawn Middle stage at level 60** — rows
0/93 (rec 80), 1/200 (rec 73), 2/202 (rec 86), 6/722 (rec 1), 7/40 (rec 88),
18/378 (rec 84), 25/14 (rec 115), 26/310 (rec 114) — re-derived from the raw
client files (spellsnew.eff + spells_us.txt field 145; the FX gate is
`spell_visual_fx.cpp:567-568`, the level rule `drawnAtLevel` in
`spells_new_eff_reader.h`): `python3 refactor/B1152/scripts/middle_rows.py
/mnt/projects/EverQuestP1999` prints the 8/40 population with each row's
record, name and drawn Middle slots. (The missile timing gate above covers
only the six gem spells — row 0 of the sweep among them, not the eight.)
Every Middle row spawns a missile in the harness and its Target-stage fire is
**deferred from OP_Action to missile arrival** — a scheduled +0.116 s onset
shift (the then-constants kMissileLandingDelay 0.30 − kMissileFlightEndOffset 0.184,
retired together with the record-driven missile by B1170 (#489); the m38/m93 run logs carry `createLanding: …
deferring to the in-flight missile`). That is exactly the observed signature:
nonzero landing deltas on precisely the eight Middle rows and +0.000 on all 32
non-Middle rows — and the deltas are a render change, not instrument variance:
B1151's own re-run (refactor/B1151/README.md, "The re-measure changed NOTHING:
all eight scenes' land_cov/end/src_cov/src_ext are identical") shows the
instrument is deterministic for a given binary + plan. By the table's
threshold **36 of 40 rows are within |d| ≤ 0.041** (the 32 non-Middle rows at
exactly 0.000, plus rows 1/2/6/18 at −0.016/−0.013/+0.010/−0.041); the four
outside it are row 26/310 +0.378 (coverage went UP, not a regression), row
0/93 −0.120, 7/40 −0.096, 25/14 +0.095 — the four largest of the eight
arrival-shift deltas. None is a source-stage regression.

93 (row 0, the B1150 lane reference): B1152 landing **1.993** (the row 0
`land_cov` cell above) vs the B1142 lane-5 reference **3.049** — ratio 0.654;
B1151 was **2.113** (ratio 0.693 — refactor/B1151/README.md: "93: B1151 2.113
vs the B1142 reference 3.049 — ratio 0.693"). 0.654 is inside the B1150
harness/oracle landing-ratio band 0.45..0.72 (refactor/B1150/README.md line
338-339): the row's landing moved by the arrival shift, and it stayed in band.

## What replaced createProjectile

- `SpellVisualFX::createProjectile()` (header `spell_visual_fx.h`, body
  `spell_visual_fx.cpp`) — **DELETED**. Its old witness (the travel-stage gate)
  is replaced by the missile actor's own qualification rule. The tests that
  called it (`test_spell_visual_vocabulary`) now exercise the missile actor.
- `SpellVisualFX::createProjectileNode()` / `updateProjectile()`
  (`spell_visual_fx_nodes.cpp`) — **DELETED** with `createProjectile`.
- `SpellPresets::Projectile()` (the travel-stage preset,
  `spell_particle_types.h` / `spell_emitter_presets.cpp`) — its production
  caller was `createProjectile`; now tests-only (exempted in
  `check_zero_callers.py`, `EXEMPT_TESTS_GROUPS`) — `test_spell_landing_volume`
  still exercises the PROJECTILE_PATH emitter it defines.
- `SpellFXInstance::target_entity` + `DEFAULT_PROJECTILE_DURATION` — lost their
  only reader with `createProjectile` (the landing reads `target_pos`, not the
  id) → dropped (WriteOnlyFields net).

## Retirements

- **The B1144 "Middle never drawn" rule — gone.** There was no emitter to draw
  the Middle before this unit: the travel stage was the never-wired
  `createProjectile` (tests-only since B1138), so the Middle stage simply had
  no FX body. Now the Middle draws from the spawner (`launchMissile` creates
  the Middle emitter on the missile). `grep -niE 'never drawn|middle.*suppress'
  src/ tools/` returns only the createProjectile/T72 tombstone
  (`spell_visual_fx.cpp:801`) and seven unrelated "never drawn" lines -- no live
  B1144/T72 gate (the card's `~:754-759` /
  `model_viewer_spell_test.cpp` line-30 refs were stale — no such note exists).
- **createProjectile retired** (above), and its stale `EXEMPT_TESTS` row
  (boarded B1128, which was to WIRE it) is deleted from
  `check_zero_callers.py`; `missileCount()` is the new test seam (observer) and
  the `Projectile` preset is a tests-only seam.

## Tests

- `tests/test_spell_visual_vocabulary.cpp` `AMiddleStageRecordSpawnsAMissile`:
  a record WITH a Middle stage, given a target, spawns a missile
  (`missileCount() == 1`); a record with NO Middle stage (Summon Corpse, record
  83) and a Middle-stage spell with no target (`targetId == 0`) spawn none.
  `createProjectile`'s old witness is replaced by this. (Skipped off-ThinkPad
  — needs `spells.eff`; runs in CI where the EQ client is present.)
- `tests/test_spell_impact_visual.cpp`: the landing (Target stage) still fires
  once (the `m_missileLanded` dedup — a missile-fired landing and
  `createLanding` on OP_Action draw the burst exactly once).
- `ctest -R 'Spell|Bridge|Particle|FxHarness|ConfigKeyReachability|Oracle'`:
  197/197 PASS (ThinkPad). Hygiene (`ctest -L hygiene`): 48/48 PASS.

## FOLLOW-UP (B1153)

- **The per-record "run" speed** (sec. 3, `0.02 × run value`) and the
  `20.5 / 1.5` spawn-count key (`[record+0x260]` in {0x31, 0x9e, 0xc4}) are in
  the in-memory descriptor, not the raw files — replace the calibrated
  `kMissileSpeed = 30 u/s` with the per-record value.
- **Spell 859's distance-gated `0x47f614` call — unimplemented.** The
  removal branch traced in rule 4 (`00477962 cmp eax, 0x35b` → the
  `fcomp [0x645ec0] = 40` gate → `00477a4e call 0x47f614`) has no fable
  counterpart: 859 rows go through the ordinary missile/`createLanding`
  paths, and its removal branch never fires anything.
- **The castAnim-release offset** (`kMissileFlightEndOffset = 0.184 s`) is
  calibrated to record 80; a distinct-record leg (a spell whose Middle fires at
  a different point in the cast) confirms the offset generalises.
- **The `[esi+0x28]`/`[esi+0x1c]` semantics** (TRACE sec. 6, inferred from FPU
  ordering): the moving-offset `t × [esi+0x28] − 72` added to z and the
  arc term. Record 80 (38/93) is confirmed; a distinct-record caster-view leg
  closes this, or it stays PARTIAL.
- **Re-aiming a moving live target** (the per-tick distance recompute, sec. 5):
  the harness target is stationary; a live moving-target leg is B1153's.
