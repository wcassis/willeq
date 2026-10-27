# B1163 (#482): missile path follow-ups from B1152

Closes the two open items B1152 (#472) and B1153 (#473) left in
`refactor/B1152/README.md` ("FOLLOW-UP"): the missile speed and spell 859's
distance-gated `0x47f614` branch. **Reworked after the cold review FAIL (MR !313
note 7651):** sections A and C were re-traced from the code and their
conclusions reversed, B was re-tabled with the B1159 rows and a stated band, D's
wording was corrected. Every address below is an instruction start in
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis`; `exe.c N` is a line of
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c`; float constants were
read from `/mnt/projects/EverQuestP1999/eqgame.exe` (`.rdata`).

Branch cut at `origin/fable 1150a34a` (B1153 merged).

## (A) The missile speed is the missile's speed word `[missile+0x28]` — derived, not calibrated

The missile object (0x88 bytes) is built by the constructor `FUN_00476f34`.
Its speed word `[missile+0x28]` is written there (exe.c 95390-95412):

```
004774c2  fld1                                   ; [+0x62] == 0: 1.0
004774f4  mov dword ptr [esi + 0x28], 0x40800000 ; types 0x1b..0x1c: 4.0
00477504  mov dword ptr [esi + 0x28], 0x40200000 ; other types: 2.5
0047750d  cmp dword ptr [esi + 0x5c], 0x35b      ; type 9 (spell): spell 859?
00477514  mov dword ptr [esi + 0x28], 0x3fc00000 ; type 9: 1.5
0047751d  mov dword ptr [esi + 0x28], 0x3f4ccccd ; type 9 + spell 0x35b: 0.8
```

The physics step `FUN_004bfe0f` moves the missile by that word along its
heading: `pfVar9 = (float *)(param_2 + 0x10)` (exe.c 146632), so `pfVar9[6]` is
`+0x28`, and `local_18 / local_1c += cos / sin(heading) x pfVar9[6]` (exe.c
146774-146781). The step is `dt = elapsed x [0x6410b8]` (0.02), clamped at
3.0 (exe.c 146620-146627), i.e. ticks of 50 `time()` units. `time()`
(`FUN_0046d655`) divides a counter by a divisor fixed at startup from the
graphics DLL (exe.c 161138-161148), so its unit is not in the static code;
reading it as milliseconds gives 20 ticks/s and **1.5 u/tick x 20 = 30 u/s,
exactly the speed B1152 measured for record 80** (9.05 units over 0.30 s,
`refactor/B1152/TRACE.md` sec. 8) — the measurement corroborates the ms
reading. Spell 859 flies at **0.8 u/tick = 16 u/s**.

So the speed is a constructor constant per missile type (and the one spell id
the client compares, `0047750d cmp dword ptr [esi + 0x5c], 0x35b`), not a
`spellsnew.edd` / `.eff` field and not a `spells_us.txt` field.

What the original B1163 text called the "run value" is **not a speed**:
`FUN_00476b12` (the spawner) computes `time() - [[caster+0x1a8]+0x50]`
(`00476c08 mov ebx, dword ptr [esi + 0x48]`, `00476c0b mov edi, dword ptr [ebx + 0xfe4]`
= the caster, `00476c18 call 0x46d655`, `00476c23 sub eax, dword ptr [ecx + 0x50]`),
times 0.02 (`00476c36 fmul dword ptr [0x6410b8]`), and adds that many ticks of
the missile's velocity `+0x1c/+0x20/+0x24` to its position (`00476d11` ..
`00476d2c`). `[[caster+0x1a8]+0x50]` is the caster's last physics tick
(`FUN_004bfe0f` writes `piVar10[0x14] = iVar5`, exe.c 146708), and the
velocity is the caster's, copied by the constructor (exe.c 95330-95332).
`FUN_00476b12` runs while the missile has no actor (exe.c 60443-60445) and
creates it, so the term is a **one-shot spawn-position catch-up** for the
time since the caster's last tick. The `+0x260` key (`00476c3c mov edi, dword ptr [edi + 0x260]`)
is the caster entity's race/type field (setter `0047a74a`), also not a file
field.

Reachability note (from the code, no inference): the constructor's only
direct call site in the executable is the `0x4033` message handler
(`00460737 call 0x476f34`, a raw `E8` scan of `.text` finds no other), which
passes the received message, and the constructor's message branch copies
`+0x28` from it (exe.c 95457). The branch that writes 1.5 / 0.8 is the one
that builds the missile locally and sends it as `0x4033`
(`00477606 push 0x4033`). The 30 u/s measured on the oracle equals that
branch's 1.5 word (a received missile carries the sender's word). Open: the
B1152 capture `/mnt/projects/fable/review/B1152/worker/oracle/m1_rig38/all-opcodes.tsv`
has no row with code `0x4033`, so the path by which the oracle's spell-38
missile object was created is not traced here (FOLLOW-UP).

**Implemented** (`include/client/graphics/spell_b1163_missile_rules.h`):
`kMissileSpeedWordSpell = 1.5`, `kMissileSpeedWord859 = 0.8`,
`kMissileTicksPerSecond = 20`, `MissileSpeedWord(spell)`,
`MissileSpeed(spell)`, and `kMissileSpeed = 1.5 x 20` (30 u/s, now derived).
The actor's flight time is `MissileFlightTime(spell_id)` = the calibrated
9.05-unit gap over the spell's speed: 0.302 s for every spell but 859,
0.566 s for 859 (`src/client/graphics/spell_visual_fx.cpp`, `createCastGlow`).
The 9.05-unit gap and the 0.184 s / 0.30 s offsets stay the record-80
calibrations (BLESS-ONLY). Test: `B1163MissileRules.MissileSpeedFromTheSpeedWord`
and `.Record80ClockUnchanged`.

## (B) The B1152 missile timing gate — before/after, with the B1159 rows

Rig: the B1155 b1130 preset; run on dev-u24 (the session ENV) with
`bash refactor/B1163/scripts/render_b1130gate_local.sh <model_viewer> <out>`
per binary: **before** = the 685d23fa tip, **after** = this rework. The
measurement is the harness FX-clock line `B1152 missile: … launch@ reach@
land@` (Act-relative, the harness OP_Action at 4.000 s); evidence
`/mnt/projects/fable/review/B1163/worker/gate_before/` and
`/mnt/projects/fable/review/B1163/worker/gate_after/` (`b1130gate.log`, the
`*.run.log` per spell, the 30 fps frames).

Oracle rows (`refactor/B1152/missile_oracle.tsv`, Act-relative = `t − act1_v`,
video frame in brackets):

| leg | spell | flight start | flight end | arrival (Target stage) |
|---|---|---|---|---|
| M1_38 | 38 (rec 80) | −0.484 (f14170) | −0.184 (f14179) | +0.116 (f14188) |
| M1b_38 | 38 (rec 80) | −1.144 (f14509) | +0.156 (f14548) | +0.590 (f14561) |
| M3b_93 | 93 (rec 80) | −1.046 (f18611) | −0.680 (f18622) | +0.454 (f18656) |
| M2_54 | 54 (rec 82) | — | — | +0.458 (f13729) |
| M2b_54 | 54 (rec 82) | — | — | +0.478 (f16588) |

(Act frames: M1_38 f14185, M1b_38 f14543, M3b_93 f18642, M2_54 f13715,
M2b_54 f16574. The previous B1163 text gave 93 the M1_38 values; they are 38's.)

**The band.** B1152 states no numeric tolerance: its gate compared the harness
to M1_38 only and passed at |Δ| ≤ 0.002 s (`refactor/B1152/README.md`, "The
missile timing gate"); `review/B1142/gate/regression_table.tsv` carries no
missile timing column (its columns are landing/source coverage, end, ROI and
hue). The B1159 re-capture shows the oracle's own leg-to-leg spread on record
80 is far wider than that. B1163 therefore states the band per metric as **the
span of the three record-80 oracle legs (M1_38, M1b_38, M3b_93), widened by one
30 fps video frame (0.033 s)**:

| metric | oracle span | band |
|---|---|---|
| flight start | −1.144 .. −0.484 | −1.177 .. −0.451 |
| flight end | −0.680 .. +0.156 | −0.713 .. +0.189 |
| arrival | +0.116 .. +0.590 | +0.083 .. +0.623 |

Harness, before (685d23fa) and after (this rework), `b1130gate.log` lines
(`/mnt/projects/fable/review/B1163/worker/gate_before/b1130gate.log`,
`/mnt/projects/fable/review/B1163/worker/gate_after/b1130gate.log`, identical
for 38 and 93):

```
spell 38: rc=0 B1152 missile: caster=1 spell=38 record=80 -> target=2 launch@-0.4856666773557663 reach@-0.18400000035762787 land@0.11600001156330109 (cast 2500 ms, flight 0.302s)
spell 93: rc=0 B1152 missile: caster=1 spell=93 record=80 -> target=2 launch@-0.4856666773557663 reach@-0.18400000035762787 land@0.11600001156330109 (cast 2500 ms, flight 0.302s)
spell 54 / 41 / 458 / 656: rc=0 NO_MISSILE_LINE
spell 859 (gate_before_859/, gate_after_859/): rc=0 NO_MISSILE_LINE
```

| spell | metric | harness before | harness after | Δ vs M1_38 | Δ vs M1b_38 | Δ vs M3b_93 | band | verdict |
|---|---|---|---|---|---|---|---|---|
| 38 | flight start | −0.486 | −0.486 | −0.002 | +0.658 | +0.560 | −1.177 .. −0.451 | in |
| 38 | flight end | −0.184 | −0.184 | 0.000 | −0.340 | +0.496 | −0.713 .. +0.189 | in |
| 38 | arrival | +0.116 | +0.116 | 0.000 | −0.474 | −0.338 | +0.083 .. +0.623 | in |
| 93 | flight start | −0.486 | −0.486 | −0.002 | +0.658 | +0.560 | −1.177 .. −0.451 | in |
| 93 | flight end | −0.184 | −0.184 | 0.000 | −0.340 | +0.496 | −0.713 .. +0.189 | in |
| 93 | arrival | +0.116 | +0.116 | 0.000 | −0.474 | −0.338 | +0.083 .. +0.623 | in |
| 54 | (no Middle: record 82) | no missile | no missile | — | — | — | — | unchanged (M2_54 +0.458, M2b_54 +0.478 are createLanding's, not a missile row) |
| 41 / 458 / 656 | (no drawn Middle) | no missile | no missile | — | — | — | — | unchanged |
| 859 | (record 85: no Middle) | no missile | no missile | — | — | — | — | unchanged (see C) |

The harness sits on M1_38 (the leg B1152 calibrated to) and inside the
record-80 span; it is 0.34..0.66 s from the two B1159 legs, which disagree with
M1_38 and with each other by up to 0.660 s (flight start −1.144 vs −0.484).
That distance predates B1163 (before == after) and is the oracle's leg-to-leg
spread, not a B1163 change; a tighter band needs more oracle legs (FOLLOW-UP).

**Before == after, frame for frame.**
`python3 refactor/B1163/scripts/frame_identity.py <before_run> <after_run> ...`
(`/mnt/projects/fable/review/B1163/worker/frame_identity.txt`): 38, 93, 54, 41
and 656 — 0 differing frames of 465 / 465 / 420 / 420 / 420. 458 and 859 differ
in 222 of 420 frames, by at most 4 pixels in one frame (frame_00060), all in
rows 445-471 / cols 656-675, and that is run-to-run harness variance, not the
binary: a second run of the BEFORE binary
(`/mnt/projects/fable/review/B1163/worker/gate_before_rerun/`) equals the before
458 run and equals the after 859 run (0 differing frames each).

## (C) Spell 859's `0x47f614` branch — it fires 859's Target stage AT THE LOCAL PLAYER

The missile removal `FUN_0047789d`:

```
0047793d  cmp byte ptr [esi + 0x63], 9      ; a type-9 (spell) missile
00477959  cmp eax, 0x36                     ; spell 54 -> 00477a55 (stage 2, no gate)
00477962  cmp eax, 0x35b                    ; spell 859
004779c6  call 0x439c41                     ; distance(missile, local player)
004779cb  fcomp dword ptr [0x645ec0]        ; vs 40.0
004779d3  test ah, 0x41
004779d6  jp 0x477aab                       ; skip unless C0 or C3 (dist <= 40)
00477a0e  mov al, byte ptr [esi + 0x64]     ; the packet's type byte ...
00477a1b  mov byte ptr [ebp - 0x26], al     ; ... at packet +0x16 (base ebp-0x3c)
00477a4e  call 0x47f614                     ; ecx = the local player ([0x905ce4])
```

- **The distance is 2-D.** `FUN_00439c41(mx, my, mz, px, py, pz, 0.0)`
  (the `fldz` at `00477984` is its 7th argument): with a 0.0 7th argument it
  takes `dx*dx + dy*dy` only (exe.c 52416-52419) — the XY distance.
- **The gate is `dist <= 40.0`.** `fcomp` sets C0 for `dist < 40`, C3 for
  `dist == 40`, neither for `dist > 40`, and C0+C3 (with C2) for an unordered
  compare; `test ah, 0x41` / `jp` skips on even parity, i.e. when neither or
  both are set. So equality fires and a NaN skips.
- **The type byte is 0xe7.** The constructor sets
  `[missile+0x64] = FUN_00479d99([missile+0x63])` (`004772dd call 0x479d99`,
  `004772e2 mov byte ptr [esi + 0x64], al`), and `FUN_00479d99` maps 9 to 0xe7
  (`00479dd3 sub eax, 7` .. `00479dda je 0x479dec`, `00479dec mov al, 0xe7`).
- **The callee therefore fires stage 2.** In `FUN_0047f614`,
  `0047f625 cmp byte ptr [esi + 0x16], 0xe7` is true, and
  `0047f630 call 0x47f4af` runs: `FUN_0047f4af` looks the spell up from packet
  `+0x1b` (`0047f4bb movsx edi, word ptr [ebx + 0x1b]`), resolves the caster
  from packet `+2` (the missile's source id `[esi + 0x4c]`, `004779fa`), and ends in
  `0047f5eb push 2` / `0047f5ed call 0x49b260` with the entity argument
  `esi` = `this` = the local player (ecx at `00477a44`): **spell 859's Target
  stage, fired at the local player** (for a first effect slot of SPA
  0x21/0x47/0x6a/0x6c the position is overridden, `0047f57b`..`0047f594`).
- The six values `0x0c/0x1c/0x1d/0x13a/0x13b/0x13c` (`0047f6c5`..`0047f734`)
  are **SPA ids** handed to `FUN_0040e934`, a buff search that compares each
  buff's spell `+0x120` effect slots to the id (`0040e97a`..`0040e994`), and
  the result goes to `0x411f2f`, which calls `0x411161` (`00411f35`, buff
  removal) — the invisibility SPAs 12 / 28 /
  29 / 314 / 315 / 316. That block and the velocity push on the local player
  (`0047f739`..) are on the NOT-0xe7 branch (`0047f65a` onward), which 859
  never takes. The previous B1163 text had this inverted.

**Implemented:** `SpellMissileRules::Spell859RemovalFiresAtPlayer(spell,
distXY)` (`spell == 0x35b && distXY <= 40.0`) and `GateDistanceXY()`, used by
`SpellVisualFX::fireSpell859RemovalAtPlayer()` when a missile is removed (its
`reachAt`, `updateMissiles`): the missile ends at its target, so the removal
position is the target's; the local player comes from
`setLocalPlayerIdCallback()` (the renderer wires `EntityRenderer::getPlayerSpawnId()`,
`src/client/graphics/renderer/renderer_entities.cpp`); within 40.0 XY units the
spell's Target stage is drawn on the player through `createLandingFromSlots`,
logged as `B1163 859 removal: …`. The literal `0x35b` is the client's own
(`00477962`, `0047750d`). Tests: `B1163MissileRules.Spell859RemovalFiresAtPlayer`
(the truth table incl. the 40.0 boundary and NaN) and `.GateDistanceIsXY`.

**Reachability in fable today — stated, not hidden.** fable builds a missile
only for a spell whose `spellsnew.eff` record has a drawn Middle stage (B1152,
`createCastGlow`). Spell 859 (`Lava Breath - Test`) is record 85 (spells_us.txt
field 145), and record 85's Middle stage has four empty slots:

```
python3 -I - <<'EOF'
import struct; d=open('/mnt/projects/EverQuestP1999/spellsnew.eff','rb').read()
for rec in (80,85):
    o=rec*268+0x40+0x44; print(rec,[struct.unpack_from('<iiii',d,o+4+16*k) for k in range(4)])
EOF
80 [(279, 0, 2, 1), (0, 0, 1, 0), (0, 0, 1, 0), (0, 0, 1, 0)]
85 [(0, 0, 1, 0), (0, 0, 1, 0), (0, 0, 1, 0), (0, 0, 1, 0)]
```

So in fable no 859 missile exists, and neither the 0.8 speed nor the removal
fire runs for it yet: the harness 859 run prints no `B1152 missile` line before
or after (table in B). In the oracle the missile object is independent of the
Middle stage — it is created from the `0x4033` message (`00460737`) and
`FUN_00476b12` builds its actor whether or not the record has Middle emitters —
and fable has no `0x4033` handler (`grep -rn 0x4033 include src` is empty).
That is the RULING REQUEST / FOLLOW-UP below; the rules and the actor code are
in place for the first 859 missile fable builds.

## (D) The 40-record b1118 sweep vs B1157

Measured at 685d23fa (`refactor/B1163/sweep_b1118_table.tsv`, built by
`refactor/B1163/scripts/build_sweep_table.py` from
`/mnt/projects/fable/review/B1163/worker/measure/r*.summary.tsv`; renders
`refactor/B1163/scripts/render_sweep_tp.sh`, measure
`refactor/B1163/scripts/measure_sweep_parallel.sh`, the B1139 instrument with
`FX_SPELLS=auto`):

- **`land_cov` and `end` are identical to B1157's `land_cov_n` / `end_n` on
  40/40 rows** (numerically; six rows differ only in print format, e.g.
  1.570 vs 1.57). No row is worse.
- **The source metrics are compared on 30 rows only.** B1157's
  `src_cov_n` / `src_on25_n` / `src_cen_n` are blank on r2, r3, r4, r10, r11,
  r21, r23, r25, r33 and r34, so on those ten rows B1163's measured values
  (e.g. r2 0.301, r3 0.115, r4 1.322, r11 0.134) were compared against
  nothing. Against B1157's `_b` column (the B1152 baseline) they read
  src_cov r2 0.301 vs 0.225, r3 0.115 vs 0.043, r4 1.322 vs 1.301, r10 1.067
  vs 1.039, r11 0.134 vs 0.097, r21 0.943 vs 0.922, r23 0.418 vs 0.381, r25
  0.448 vs 0.394, r33 0.121 vs 0.046, r34 0.415 vs 0.387. That span is
  B1152 → B1163 and includes B1157's cast-pose change on the Source; B1163
  cannot separate the two on these rows because B1157 left them blank. Owner:
  B1157 (its blank `_n` cells), FOLLOW-UP below. On the other 30 rows
  `src_cov` equals B1157's `_n` (d = +0.000).
- **The rework does not move the sweep.** It changes the missile only for
  spell 859 (not one of the 40 records: `cut -f2 refactor/B1163/plans/sweep/list.tsv`),
  and for every other spell `MissileFlightTime(spell)` = 9.05 / (1.5 x 20) is
  the same float as the old 9.05 / 30. Checked on renders, not only by
  argument: the eight sweep records whose spell's field-145 record draws a Middle stage
  at level 60 (r0_93, r1_200, r2_202, r6_722, r7_40, r18_378, r25_14,
  r26_310) were rendered with the before and the after binary
  (`bash refactor/B1163/scripts/render_sweep_pair_local.sh <model_viewer> <out> <rec_spell>...`,
  dev-u24, `/mnt/projects/fable/review/B1163/worker/sweep_before/` and
  `/mnt/projects/fable/review/B1163/worker/sweep_after/`). All eight print the
  same `B1152 missile` line before and after (launch@−0.486 reach@−0.184
  land@+0.116, flight 0.302 s). `frame_identity.py`
  (`/mnt/projects/fable/review/B1163/worker/frame_identity_sweep.txt`): six
  of the eight (and the non-Middle r14_373) have 0 differing frames; r0_93
  differs in 340 of 435 frames (≤ 28 px in one frame, frame_00060) and r7_40
  in 322 of 450 (≤ 19 px, frame_00099), all inside rows 342-467 / cols
  644-714. That is harness run-to-run variance, not the change: every
  differing pair in this unit (r0_93, r7_40, 458, 859) first differs at
  frame_00000, 45 frames BEFORE the plan's BeginCast at 1.5 s, when no spell
  code has run; and on the ThinkPad (the 685d23fa code and this rework built
  by `tbuild`, renders under
  `/mnt/projects/fable/review/B1163/worker/frames/tp_before/`,
  `tp_after_a/`, `tp_after_b/`) r7_40 has 0 differing frames before vs after,
  two after runs are identical for both records, and r0_93 before vs after
  differs in 71 of 435 frames by at most 1 pixel each, first at frame_00001
  — while a second run of the BEFORE binary (`tp_before_b/`) equals the after
  run (0 differing frames) and differs from the first before run in the same
  71 frames (`/mnt/projects/fable/review/B1163/worker/frame_identity_tp_repeat.txt`).

## (E) Tests + hygiene + CI

Builds and tests on the ThinkPad through the session `tbuild` wrapper (CI's
Release GLES2 configure; tree `/home/user/willeq-builds/kb-b1163-missile-fu/build`):

- `tbuild <wt> --target model_viewer test_fx_harness test_spell_* test_b1163_missile_rules`
  → exit 0.
- `tbuild <wt> test -R 'B1163MissileRules|FxHarness|^Spell'` → **100 % tests
  passed, 0 failed out of 413** (60 skipped by their own data guards), incl.
  the four `B1163MissileRules.*` cases and the six `FxHarness.*` cases.
- `tbuild <wt> hygiene` → **100 % tests passed, 0 failed out of 48**.
- `test_b1163_missile_rules` also builds and passes 4/4 in a headless
  (`-DEQT_GRAPHICS=OFF`) tree (no `<irrlicht.h>`).
- Whole-tree CI build: the MR !313 pipeline on the final push (id in the MR
  note for this rework).
- Cite checker: `python3 -I scripts/refactor/cite_check.py refactor/B1163/README.md --tree . --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked 13 cites, 0 problems, 0 address warnings`. The checker reads `0x`-prefixed addresses only, so every bare
  8-digit address in this README, the rules header, the test and the
  `spell_visual_fx` comments was also checked by hand against `eqgame.dis`
  (all are instruction starts), and every `exe.c N` line was read.

## RULING REQUEST

Should fable build a missile for a spell whose record has no drawn Middle stage
(the oracle builds the missile object from the `0x4033` message, `00460737`,
independent of the Middle emitters)? Until it does, spell 859 (record 85, no
Middle) has no fable missile, so its 0.8 speed word and its at-player Target
stage (both implemented here) never run.

## FOLLOW-UP

- Trace how the oracle's spell missile object is created when no `0x4033`
  appears on the wire (the B1152 m1_rig38 capture has none), i.e. which path
  reaches the constructor's local branch (`004774c2`..`0047751d`).
- The `0x4033` missile message (handler `00460737`, local sender `00477606`):
  fable has no counterpart; it is what gives 859 (and any Middle-less missile)
  an actor.
- The record-80 missile band (B) is the span of three oracle legs that
  disagree by up to 0.660 s; more record-80 legs (or a second record) would
  let it be tightened.
- B1157's ten blank `src_*_n` rows (D): re-measure them so the source metrics
  have a B1157-tip value to compare against.

## Files

- `include/client/graphics/spell_b1163_missile_rules.h` — the speed word, the
  derived speed, the calibrated timing and the 859 removal gate, device-free.
- `include/client/graphics/spell_visual_fx.h`, `src/client/graphics/spell_visual_fx.cpp`
  — per-spell flight time, `fireSpell859RemovalAtPlayer`, the local-player
  callback.
- `include/client/graphics/entity_renderer.h`,
  `src/client/graphics/renderer/renderer_entities.cpp` — `getPlayerSpawnId()`
  and its wiring into `SpellVisualFX`.
- `tests/test_b1163_missile_rules.cpp`, `tests/cmake/spells.cmake` — the four
  device-free cases.
- `refactor/B1163/scripts/`, `refactor/B1163/plans/` — the sweep and gate
  scripts and plans (`render_b1130gate_local.sh`, `plans/b1130gate/s859.tsv`
  added by the rework).
