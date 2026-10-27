# B1163 (#482): missile path follow-ups from B1152

Closes the two open items B1152 (#472) and B1153 (#473) left in
`refactor/B1152/README.md` ("FOLLOW-UP"): the per-record **missile** run
speed and spell 859's distance-gated `0x47f614` branch.

Branch cut at `origin/fable 1150a34a` (B1153 merged: the measured rows are the
test bands). All numbers below are beside the command that produces them.

## (A) The per-record "run" speed is a RUNTIME quantity — keep the 30 u/s calibration

B1152 stood in `kMissileSpeed = 30 u/s` for the oracle's per-record "run"
speed (`0.02 × run value`, `refactor/B1152/README.md` sec. 3) and left
naming the source field to B1163. Traced from the oracle's disassembly
(`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis`):

The missile spawner (`FUN_00476b12`) scales the stage's **elapsed time** —
`time()` minus the stage start word (`+0x50`) — by 0.02 and multiplies it by
the **live target's per-axis velocity vector**:

```
00476c18  call 0x46d655          ; time()
00476c23  sub  eax, [ecx+0x50]   ; elapsed since the stage started
00476c36  fmul [0x6410b8]        ; x 0.02  (the "run value" scale)
00476d11  fld  [ebp-4]           ; stageScalar = elapsed x 0.02
00476d14  fmul [esi+0x1c]        ; x per-axis velocity (x)
00476d17  fadd [esi+0x10]        ; pos.x += stageScalar x vel.x
00476d1d  fld  [ebp-4]           ; ... y ...
00476d20  fmul [esi+0x20]
00476d23  fadd [esi+0x14]
00476d29  fld  [ebp-4]           ; ... z ...
00476d2c  fmul [esi+0x24]
```

The per-axis velocity the effect carries (`effect+0x1c/+0x20/+0x24`) is copied
from the **live entity** the same tick (`004770f1-004770f7`, `vel.x/y`), so the
"run value" is *elapsed-since-stage × live-velocity*. Neither word is read from
`spellsnew.edd` or `spellsnew.eff`. The spell record is **zero-initialised at
construction** (`0044d941 xor edx, edx`; the whole record cleared,
`0044d932-0044db05`) and never has a run-speed word filled from the file.

The `+0x260` spawn-count key B1152 noted (20.5 vs 1.5) is likewise the **live
creature type**, written by the entity type setter (`0047a74a mov [ecx+0x260],
eax`) — a runtime quantity, not a file field.

**Conclusion:** there is no per-record run speed to read from the files. The
value is a runtime quantity (elapsed × live velocity), so fable's missile actor
flies at one **calibrated** speed that reproduces the record-80 oracle flight
(9.05 units over 0.30 s = 30 u/s, `refactor/B1152/TRACE.md` sec. 3/8). The
calibration is **kept**, and now lives in one device-free header
(`include/client/graphics/spell_b1163_missile_rules.h`) — the single source of
truth `SpellVisualFX`'s timing and `test_b1163_missile_rules` both read.

## (B) The B1152 missile timing gate — re-run, all rows inside the bands

Rig: the B1155 b1130 preset; run `tools/model_viewer --spell-test --camera
b1130 --plan <plan>` per gate spell on the ThinkPad
(`refactor/B1163/scripts/render_b1130gate_tp.sh`); the measurement is the
FX-clock log line `B1152 missile: … launch@ reach@ land@` (Act-relative),
compared to `refactor/B1152/missile_oracle.tsv` Act-relative values.

| spell | rec | metric | oracle (Act+) | harness (Act+) | Δ | verdict |
|---|---|---|---|---|---|---|
| 38 | 80 | flight start (launch) | −0.484 | −0.486 | −0.002 | PASS |
| 38 | 80 | flight end (reach) | −0.184 | −0.184 | 0.000 | PASS |
| 38 | 80 | arrival (Target stage) | +0.116 | +0.116 | 0.000 | PASS |
| 93 | 80 | flight start (launch) | −0.484 | −0.486 | −0.002 | PASS |
| 93 | 80 | flight end (reach) | −0.184 | −0.184 | 0.000 | PASS |
| 93 | 80 | arrival (Target stage) | +0.116 | +0.116 | 0.000 | PASS |
| 54 | 82 | (no Middle) | — | no missile | — | PASS |
| 41 | 20 | (no Middle) | — | no missile | — | PASS |
| 458 | 33 | (no Middle) | — | no missile | — | PASS |
| 656 | 23 | (no Middle) | — | no missile | — | PASS |

Measured (this run, FX-clock lines, `--camera b1130`):

```
spell 38 (cast 2500 ms, flight 0.302 s): launch@-0.4857 reach@-0.1840 land@+0.1160
spell 93 (cast 2500 ms, flight 0.302 s): launch@-0.4857 reach@-0.1840 land@+0.1160
spell 54 / 41 / 458 / 656: (no B1152 missile line — no drawn Middle)
```

**Before/after:** the change moves the four calibrated constants
(`kMissileSpeed`, `kMissileFlightDistance`, `kMissileFlightEndOffset`,
`kMissileLandingDelay`) from a `SpellVisualFX` private member to the pure
header and routes the actor through `SpellMissileRules::Missile{FlightTime,
ReachAt,LandAt}()`. The values are byte-identical (30.0 / 9.05 / 0.184 / 0.30),
so the timing is **unchanged** — the harness rows above are identical to
B1152's own gate (its README table). Every row stays inside its B1152 band.

## (C) Spell 859's `0x47f614` branch — a no-draw network + velocity step

The oracle's spell-859 gate (in the effect/removal `FUN/0047789d`):

```
00477959  cmp  eax, 0x36        ; spell 54
0047795c  je   0x477a55         ;   -> its (unconditional) stage-2 path
00477962  cmp  eax, 0x35b       ; spell 859
004779c6  call 0x439c41         ;   distance from the player
004779cb  fcomp [0x645ec0]      ;   vs 40.0
00477a4e  call 0x47f614         ;   the distance-gated branch
...
00477aa3  call 0x49b260         ; spell 54's UNCONDITIONAL stage-2 fire (no gate)
```

The callee `0x47f614` does **not** spawn a local particle. It either takes the
removal path (`0047f625 cmp [esi+0x16], 0xe7` → `0047f630 call 0x47f4af`,
a reposition/clean-up) or, on the fire path, sends **six zone messages**
(`0047f6c5-0047f734`: `0x0c, 0x1c, 0x1d, 0x13a, 0x13b, 0x13c` via
`call 0x411f2f`) and **steers the in-memory record's velocity** —
`0047f760 call [eax+0xc]` (cos heading) × speed `fadd [edi+0x3c]` (vel.x),
`0047f77b` (sin) → `fadd [edi+0x40]` (vel.y), pitch → vel.z. There is no call
to the stage/effect spawner, so **nothing the visual ledger would show is
drawn**.

**Conclusion:** the 859 branch is a server-registration + velocity-prediction
step, not a local draw. fable's 859 rows therefore take the ordinary
Middle-emitter / `createLanding` path (the visible projectile, if any, comes
from there), and fable **intentionally implements no draw** for the
distance-gated branch. Spell 54 is the contrast: its stage-2 fire
(`0x49b260`) is unconditional (no distance gate) and *is* implemented (B1152).

The traced gate (the branch fires iff `spell == 859` **and** the effect is
within 40.0 units of the player) is pinned device-free by
`test_b1163_missile_rules` (`B1163MissileRules.Spell859GatedBranch`), which
encodes the rule against the production constants
(`EQ::SpellMissileRules::kSpell859 = 0x35b`, `kSpell859GateDistance = 40.0`)
and asserts the truth table including the spell-54 / absent-spell negatives.

## (D) The 40-record b1118 sweep — 40/40 reproduce B1157 (no row worse, no owner needed)

`refactor/B1163/scripts/render_sweep_tp.sh` renders the 40 B1130-record
representatives in the harness (`--camera b1118`, caster level 60, seed 1) on
the ThinkPad (40 records, 0 failed); the host measures each with the B1139
instrument (`FX_SPELLS=auto FX_KEEP=1 harness_measure.py`, the b1155 venv — the
same env B1155/B1157 measured with) and `refactor/B1163/scripts/
build_sweep_table.py` compares the rows to
`refactor/B1157/b1118_table.tsv`'s `_n` column (the pre-B1163 tip).

**Result: 40/40 PASS.** Every record's `land_cov` / `end` / `src_cov` /
`src_on25` / `src_cen` reproduces B1157's `_n` to within the gate tolerance
(±3 % land_cov, ±0.10 s end, ±0.05 src_cov) — in fact most rows are
byte-identical (`d = +0.000`). That is the expected outcome of an FX-neutral
change (the four missile constants moved into a header, values unchanged): the
sweep shows the b1118 rendering is **unchanged** by B1163, so no row is worse
and none needs an owner.

| rec | spell | land_cov | end | src_cov | src_on25 | src_cen | b1157 land | b1157 end | b1157 src | d_land | d_end | d_src | verdict |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| r0 | 93 | 1.993 | +6.633 | 0.385 | +0.433 | 0.86 | 1.993 | +6.633 | 0.385 | +0.000 | +0.000 | +0.000 | PASS |
| r1 | 200 | 1.308 | +6.233 | 0.304 | +0.500 | 0.64 | 1.308 | +6.233 | 0.304 | +0.000 | +0.000 | +0.000 | PASS |
| r2 | 202 | 0.694 | +4.633 | 0.301 | +0.567 | 0.90 | 0.694 | +4.633 | — | +0.000 | +0.000 | — | PASS |
| r3 | 377 | 7.413 | +4.033 | 0.115 | +0.400 | 0.65 | 7.413 | +4.033 | — | +0.000 | +0.000 | — | PASS |
| r4 | 50 | 1.431 | — | 1.322 | +0.900 | 0.77 | 1.431 | — | — | +0.000 | — | — | PASS |
| r5 | 80 | 0.248 | — | 0.214 | +0.467 | 0.78 | 0.248 | — | 0.214 | +0.000 | — | +0.000 | PASS |
| r6 | 722 | 0.401 | — | 0.340 | +0.600 | 0.76 | 0.401 | — | 0.34 | +0.000 | — | +0.000 | PASS |
| r7 | 40 | 0.902 | +5.633 | 0.524 | +0.433 | 0.85 | 0.902 | +5.633 | 0.524 | +0.000 | +0.000 | +0.000 | PASS |
| r8 | 346 | 0.261 | +2.500 | 0.550 | +0.567 | 0.71 | 0.261 | +2.500 | 0.55 | +0.000 | +0.000 | +0.000 | PASS |
| r9 | 375 | 1.321 | +7.200 | 0.866 | +0.433 | 0.83 | 1.321 | +7.200 | 0.866 | +0.000 | +0.000 | +0.000 | PASS |
| r10 | 590 | 1.570 | +6.100 | 1.067 | +0.567 | 0.81 | 1.57 | +6.100 | — | +0.000 | +0.000 | — | PASS |
| r11 | 285 | 0.232 | +4.333 | 0.134 | +0.100 | 0.62 | 0.232 | +4.333 | — | +0.000 | +0.000 | — | PASS |
| r12 | 208 | 3.409 | +7.000 | 1.744 | +0.600 | 0.78 | 3.409 | +7.000 | 1.744 | +0.000 | +0.000 | +0.000 | PASS |
| r13 | 209 | 0.621 | +5.467 | 0.610 | +0.467 | 0.85 | 0.621 | +5.467 | 0.61 | +0.000 | +0.000 | +0.000 | PASS |
| r14 | 373 | 1.881 | +8.433 | 0.110 | +0.100 | 0.73 | 1.881 | +8.433 | 0.11 | +0.000 | +0.000 | +0.000 | PASS |
| r15 | 289 | 0.829 | — | 0.789 | +0.600 | 0.62 | 0.829 | — | 0.789 | +0.000 | — | +0.000 | PASS |
| r16 | 242 | 0.846 | — | 0.760 | +0.700 | 0.37 | 0.846 | — | 0.76 | +0.000 | — | +0.000 | PASS |
| r17 | 237 | 1.237 | +4.833 | 0.603 | +0.533 | 0.74 | 1.237 | +4.833 | 0.603 | +0.000 | +0.000 | +0.000 | PASS |
| r18 | 378 | 0.945 | +5.200 | 0.433 | +0.500 | 1.03 | 0.945 | +5.200 | 0.433 | +0.000 | +0.000 | +0.000 | PASS |
| r19 | 359 | 1.297 | — | 1.129 | +0.567 | 0.70 | 1.297 | — | 1.129 | +0.000 | — | +0.000 | PASS |
| r20 | 341 | 2.113 | +5.567 | 1.079 | +0.567 | 0.81 | 2.113 | +5.567 | 1.079 | +0.000 | +0.000 | +0.000 | PASS |
| r21 | 218 | 4.209 | +5.633 | 0.943 | +0.600 | 0.71 | 4.209 | +5.633 | — | +0.000 | +0.000 | — | PASS |
| r22 | 201 | 2.125 | +4.700 | 0.325 | +0.467 | 0.73 | 2.125 | +4.700 | 0.325 | +0.000 | +0.000 | +0.000 | PASS |
| r23 | 372 | 0.838 | +6.433 | 0.418 | +0.500 | 0.65 | 0.838 | +6.433 | — | +0.000 | +0.000 | — | PASS |
| r24 | 348 | 1.760 | — | 1.282 | +0.633 | 0.52 | 1.76 | — | 1.282 | +0.000 | — | +0.000 | PASS |
| r25 | 14 | 2.088 | +6.533 | 0.448 | +0.567 | 0.84 | 2.088 | +6.533 | — | +0.000 | +0.000 | — | PASS |
| r26 | 310 | 1.870 | +4.667 | 0.401 | +0.500 | 0.58 | 1.87 | +4.667 | 0.401 | +0.000 | +0.000 | +0.000 | PASS |
| r27 | 290 | 0.244 | — | 0.178 | +0.433 | 0.67 | 0.244 | — | 0.178 | +0.000 | — | +0.000 | PASS |
| r28 | 73 | 1.635 | — | 1.419 | +0.667 | 0.65 | 1.635 | — | 1.419 | +0.000 | — | +0.000 | PASS |
| r29 | 286 | 0.897 | +7.033 | 0.411 | +0.467 | 0.56 | 0.897 | +7.033 | 0.411 | +0.000 | +0.000 | +0.000 | PASS |
| r30 | 231 | 2.286 | +6.300 | 0.185 | +0.700 | 0.73 | 2.286 | +6.300 | 0.185 | +0.000 | +0.000 | +0.000 | PASS |
| r31 | 313 | 2.498 | +6.217 | 1.166 | +0.433 | 0.69 | 2.498 | +6.217 | 1.166 | +0.000 | +0.000 | +0.000 | PASS |
| r32 | 374 | 4.090 | +7.217 | 0.852 | +0.467 | 0.49 | 4.09 | +7.217 | 0.852 | +0.000 | +0.000 | +0.000 | PASS |
| r33 | 382 | 0.361 | +4.917 | 0.121 | +0.300 | 0.57 | 0.361 | +4.917 | — | +0.000 | +0.000 | — | PASS |
| r34 | 340 | 1.068 | +5.967 | 0.415 | +0.500 | 0.60 | 1.068 | +5.967 | — | +0.000 | +0.000 | — | PASS |
| r35 | 253 | 5.297 | — | 3.806 | +0.500 | 0.99 | 5.297 | — | 3.806 | +0.000 | — | +0.000 | PASS |
| r40 | 602 | 0.639 | +4.133 | 0.986 | +0.433 | 0.83 | 0.639 | +4.133 | 0.986 | +0.000 | +0.000 | +0.000 | PASS |
| r41 | 703 | 0.800 | +5.533 | 0.222 | +0.500 | 0.94 | 0.8 | +5.533 | 0.222 | +0.000 | +0.000 | +0.000 | PASS |
| r42 | 700 | 0.204 | +5.300 | 0.075 | +0.433 | 0.86 | 0.204 | +5.300 | 0.075 | +0.000 | +0.000 | +0.000 | PASS |
| r43 | 720 | 0.390 | +4.567 | 0.328 | +0.500 | 1.11 | 0.39 | +4.567 | 0.328 | +0.000 | +0.000 | +0.000 | PASS |

(A `—` is the instrument's own: a blank `src_cov` is `source_all` not clearing
its blob threshold in the window, a blank `end` is fx_timing finding no
LANDING run in its own form — the same blanks B1157 carries; the delta is
computed only where both sides have a value. Verbatim table:
`refactor/B1163/sweep_b1118_table.tsv`.)

## (E) Tests + hygiene + CI

- **New device-free ctest** `test_b1163_missile_rules` (two cases:
  `MissileSpeedIsCalibratedConstantNotPerRecord`, `Spell859GatedBranch`) —
  builds and runs in both the graphics and headless trees (no `<irrlicht.h>`).
  `rbuild <wt> test -R 'B1163MissileRules'` → 2/2 pass.
- **Hygiene** `rbuild <wt> hygiene` → 48/48 (1 skipped), including
  `ZeroCallers` (the timing helpers have production callers in
  `spell_visual_fx.cpp`; the 859 rule is test-local).
- **Whole-tree CI build**: pipeline <PIPELINE_ID> on MR <MR> → <STATUS>.

## Files

- `include/client/graphics/spell_b1163_missile_rules.h` — the calibrated
  missile timing + the 859 gate constants, as pure device-free rules (the
  single source of truth).
- `include/client/graphics/spell_visual_fx.h` — includes the rules header;
  the four missile constants are aliases to it.
- `src/client/graphics/spell_visual_fx.cpp` — the actor uses
  `SpellMissileRules::Missile{FlightTime,ReachAt,LandAt}()`.
- `tests/test_b1163_missile_rules.cpp` — the two device-free cases.
- `tests/cmake/spells.cmake` — registers the test.
- `refactor/B1163/scripts/` — the sweep + b1130-gate render scripts.
- `refactor/B1163/plans/` — the sweep + gate plans.
