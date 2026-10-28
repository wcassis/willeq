# B1163 (#482): missile path follow-ups from B1152

Closes the two open items B1152 (#472) and B1153 (#473) left in
`refactor/B1152/README.md` ("FOLLOW-UP"): the missile speed and spell 859's
distance-gated `0x47f614` branch. **Reworked after the cold review FAIL (MR !313
note 7651):** sections A and C were re-traced from the code and their
conclusions reversed, B was re-tabled with the B1159 rows and a stated band, D's
wording was corrected. **Reworked again after the re-review FAIL (note 7704):**
A now models `[missile+0x28]` as the thrust word it is and keeps 30 u/s as the
B1152 calibration; C lists the four early-outs. Every address below is an instruction start in
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis`; `exe.c N` is a line of
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c`; float constants were
read from `/mnt/projects/EverQuestP1999/eqgame.exe` (`.rdata`).

Branch cut at `origin/fable 1150a34a` (B1153 merged).

## (A) `[missile+0x28]` is a thrust word, not a speed — 30 u/s stays the B1152 calibration

**Reworked again after the re-review FAIL (MR !313 note 7704, finding 1).** The
previous text read the word as a distance per tick and called 30 u/s
"derived". It is not: the word is a thrust term in a damped velocity, and the
oracle legs that measured 30 u/s built no missile object at all. **What is
proven: the `FUN_00476f34` missile object did not draw the measured flight (its
only caller is `00460737` and none of the three captures has a `0x4033` row).
The path that did draw it is untraced (see below and open assumption 1), so
whether a spellsnew.edd/.eff field drives that flight is OPEN, not shown to be
runtime-only. `kMissileSpeed = 30 u/s` is the B1152 record-80 calibration, and
the model is below. ORCHESTRATOR RULING on (A): accepted as a stated limit; the
drawing path is the follow-up card.**

### Where the word comes from

The missile object (0x88 bytes) is built by the constructor `FUN_00476f34`.
Its local branch writes the word `[missile+0x28]` (exe.c 95390-95412):

```
004774c2  fld1                                   ; [+0x62] == 0: 1.0
004774f4  mov dword ptr [esi + 0x28], 0x40800000 ; types 0x1b..0x1c: 4.0
00477504  mov dword ptr [esi + 0x28], 0x40200000 ; other types: 2.5
0047750d  cmp dword ptr [esi + 0x5c], 0x35b      ; type 9 (spell): spell 859?
00477514  mov dword ptr [esi + 0x28], 0x3fc00000 ; type 9: 1.5
0047751d  mov dword ptr [esi + 0x28], 0x3f4ccccd ; type 9 + spell 0x35b: 0.8
```

**No call reaches that branch.** The constructor's only call site is
`00460737 call 0x476f34`, in the `0x4033` arm of the message handler
`FUN_0045b8f0` (a raw `E8`/`E9` scan of `.text` finds no other; the image holds
no `0x00476f34` pointer constant; the missile list head `[0x905cd0]` is
written by the constructor, `00476f6f` / `00476f8b`, by removal (`004778e2` /
`004778fd`), and by a null write in a global teardown,
`0049765d mov dword ptr [0x905cd0], esi` with esi = 0, which creates no missile). The
call passes `ebx`, the handler's message argument
(`0045b90a mov ebx, dword ptr [ebp + 0x10]`), as the third argument, so the
constructor takes its RECEIVED branch and copies the word, the position, the
velocity and `+0x62` from the message (`004770eb mov eax, dword ptr [edi + 0x28]`
/ `004770ee mov dword ptr [esi + 0x28], eax`, `0047712d` / `00477130`). The
1.5 / 0.8 are the values the local branch would put in a `0x4033` it sends
(`00477606 push 0x4033`); this binary never runs that branch.

**The measured legs built no missile object.** The handler dispatches on the
wire opcode (`0045d500 sub ecx, 0x3990` is `OP_BeginCast`'s code in the
captures). Code `0x4033` has 0 rows in all three oracle captures:

```
for f in /mnt/projects/fable/review/B1152/worker/oracle/m1_rig38/all-opcodes.tsv \
         /mnt/projects/fable/review/B1152/worker/oracle/m2_rig54/all-opcodes.tsv \
         /mnt/projects/fable/review/B1159/worker/oracle/m_b1159_rig/all-opcodes.tsv; do
  awk -F'\t' '$4=="0x4033"' $f | wc -l; done        # 0 0 0
```

So whatever the observer saw fly between caster and target in M1_38 / M1b_38 /
M3b_93, it was not a `FUN_00476f34` missile object. The visible flight's path
in the client is not traced (FOLLOW-UP).

### The motion model (what a missile object would do)

`FUN_004bfe0f(0, missile, 0)` (exe.c 60453; `0044558f call 0x4bfe0f`) with
`pfVar9 = missile + 0x10` (exe.c 146632):

- **dt** = (time() − `[actor+0x50]`) × `[0x6410b8]` (0.02), clamped at 3.0
  (`004c0020 sub eax, dword ptr [ecx + 0x50]`, `004c0033 fmul dword ptr [0x6410b8]`,
  `004c0049 mov dword ptr [ebp + 8], 0x40400000`; exe.c 146614-146627).
- **Thrust**: `local_18 / local_1c = cos / sin(heading) × word`
  (`004c0292 fld dword ptr [esi + 0x1c]` = heading `+0x2c`,
  `004c02a4 call dword ptr [eax + 0xc]`, `004c02a7 fmul dword ptr [ebp - 0x20]`
  = the word, `004c02c5 call dword ptr [eax + 8]`; exe.c 146774-146781).
- **Branch.** `004c032c cmp byte ptr [edi + 0x150], 6` → `004c0335 fld dword ptr [0x6431d8]`
  / `004c0341 fld dword ptr [0x643f7c]`; otherwise the gravity branch needs
  `004c0358 cmp dword ptr [ebp + 0x10], 0` (param_3 = 0 for a missile) AND
  `004c0362 cmp byte ptr [edi + 0x9c], 1` AND `FUN_0048a344() == 0` AND the
  DLL's `+0x4c` (exe.c 146793-146795). The constructor clears the actor's
  `+0x9c` in both branches (`00477203`, `00477532`, after `FUN_0048f023` set it
  at `0048f444`), and `FUN_00476b12` clears it again on its no-flight path
  (`00476d6e`). With `+0x9c` = 0 the code falls to `004c04bf`, which loads
  `local_c = [0x6431d8]` = **0.8** and `local_8 = [0x643f7c]` = **0.625**
  (`004c04c1`, `004c04cd`; exe.c 146845-146847) and, the entity pointer being
  0, skips the entity block (`004c04d9 je 0x4c067d`). Both non-gravity arms
  therefore give 0.8 / 0.625. (A writer of `+0x9c` = 1 that takes a missile's
  actor was not found among the `+0x9c` writers in `eqgame.dis`; that list was
  read, not proven exhaustive.)
- **Scale, damp, add, move**: `local_18 *= local_c` (`004c0691` / `004c0694 fmul dword ptr [ebp - 8]`,
  exe.c 146910); when `[missile+0x62]` == 0 the damping becomes 0.85
  (`004c075c cmp byte ptr [eax + 0x62], 0` / `004c0762 mov dword ptr [ebp - 4], 0x3f59999a`,
  exe.c 146920); `v *= local_8` (`004c0782`, exe.c 146941); `v = local_18 + v`
  (`004c0834` / `004c0837 fadd dword ptr [esi + 0xc]` / `004c083a`, exe.c 146961);
  `pos += dt × v` (exe.c 146998).

So per pass **v' = 0.625 v + 0.8 × word** (terminal 0.8 × 1.5 / 0.375 = 3.2
per dt unit, 64 u/s if time() is ms), not "1.5 u/tick".

- **Pass rate.** The missile loop runs the physics only when
  `[frame+0x148] − [actor+0x50]` ≥ 33 (`00445577` .. `00445580 cmp ecx, 0x21`
  / `00445583 jb 0x44559f`), once per pass through `FUN_00445243`'s loop, so the
  pass interval is the larger of 33 time() units and the client's frame
  interval, frame-quantised. Because the damping is per pass, the flight
  depends on that interval, which is not in the static code and was not
  measured on the oracle.
- **Homing and removal** (`FUN_004766aa`, `00445587`, before the physics):
  while the word is non-zero (`004766de fld dword ptr [esi + 0x28]` /
  `004766e8 jnp 0x476ad0`) it re-stores the XY distance to the target
  (`004768b1 call 0x439c41` / `004768b9 fstp dword ptr [eax + 0xff4]`) every pass,
  in two steps. The refresh writes `+0xe4` = now (`00476810 mov dword ptr
  [ecx + 0xe4], edx`) and the compare is unsigned (`004767d1 cmp eax, edi` /
  `004767d3 jbe 0x476930`). Pass 1: the stored distance is 2000, so the
  threshold is 1000 (`004767b9 mov edi, 0x3e8`), and the actor's `+0xe4` was set
  2000 ahead for type 9 (`00476f19 add dword ptr [esi + 0xe4], 0x7d0`), so the
  compare wraps and the first refresh runs. From pass 2 on, a stored distance
  under 200.0 (`00476730 fcomp dword ptr [0x641f90]`) sets the threshold to 0
  (`00476749 xor edi, edi`), so the refresh runs whenever elapsed > 0, i.e. every
  pass;
  under 32 units (`00476939 fcomp dword ptr [0x64112c]`) it snaps the heading
  to the target (`0047694c mov dword ptr [esi + 0x2c], eax`). When the distance
  stored at the PREVIOUS pass is below `[0x6410b4]` = 2.0
  (`00476743 fcomp dword ptr [0x6410b4]`), it flags the missile
  (`00476769 mov byte ptr [eax + 0x1005], 1`) and `FUN_00477cc9` removes it in
  the same pass (`004782b8 cmp byte ptr [eax + 0x1005], 1` → `004782d2 jmp 0x477fbf`
  → `00477fc1 call 0x47789d`). The stored distance starts at 2000.0
  (`00476eef mov dword ptr [eax + 0xff4], 0x44fa0000`).
- **Start.** The local branch puts the missile 1.5 units ahead of the caster
  (`0047742c fmul dword ptr [0x6410d0]`, 1.5); the received branch copies the
  sender's position. The velocity starts at the caster's (local) or the
  message's (received); a standing caster gives 0. Travel type 3
  (spells_us.txt field 122, spell `+0x188`) skips `FUN_00476b12`'s
  spawn catch-up (`00476cd5 cmp byte ptr [eax + 0x188], 3`).
- **Lifetime.** `FUN_00476b12` sets `[actor+0x68]` = time() + spell `+0x28`
  (spells_us.txt field 18, AEDuration; `00476bbe add eax, dword ptr [edi + 0x28]`),
  or, when that is 0, + 30000 for travel type 3 (`00476bd1 add eax, 0x7530`)
  and + 50 otherwise (`00476bdd add eax, 0x32`); `FUN_00477cc9` removes the
  missile once time() passes it (`0047821b cmp ecx, dword ptr [eax + 0x68]` /
  `0047821e ja 0x477fbf`). Spell 38 (field 18 = 0, travel 3) lives 30 s;
  **spell 93 (field 18 = 0, travel 0) would live 50 units**, which a
  0.302 s flight cannot fit — another sign the measured flights are not this
  object (FOLLOW-UP).

**Closing time of the 9.05-unit gap** from rest, homing, word 1.5 / damping
0.625 (and the `+0x62 == 0` case), per pass interval, time() read as ms:
`python3 -I refactor/B1163/scripts/missile_model.py`

| gap | word / damping | 33 | 34 | 40 | 50 | 67 | 100 (pass units) |
|---|---|---|---|---|---|---|---|
| 9.05 | 1.5 / 0.625 | 0.231 s (7) | 0.238 (7) | 0.280 (7) | 0.300 (6) | 0.335 (5) | 0.500 (5) |
| 7.55 (1.5 spawn offset) | 1.5 / 0.625 | 0.231 (7) | 0.204 (6) | 0.240 (6) | 0.250 (5) | 0.335 (5) | 0.400 (4) |
| 9.05 | 1.0 / 0.85 | 0.264 (8) | 0.272 (8) | 0.280 (7) | 0.350 (7) | 0.402 (6) | 0.500 (5) |

(passes in brackets; flight = passes × interval, from the actor's creation.)

**Comparison with B1152.** B1152 measured 0.300 s of visible flight for
record 80 (M1_38 −0.484 → −0.184). The model spans 0.20-0.50 s over plausible
pass intervals and hits 0.300 s only at 50-unit passes with no spawn offset,
so it neither confirms nor refutes the measurement by itself; and the
captures show no `0x4033`, so the measured flight is not this object's.
**Open assumptions, in order: (1) which client path drew the measured flight
(no missile object existed); (2) the oracle's pass interval (its frame rate);
(3) the start position (local 1.5 offset vs the sender's).** fable's 0.302 s
is therefore unchanged and is labelled the B1152 calibration.

### Spell 859: no flight, derived

Spell 859's target type (spells_us.txt field 98, spell `+0x180`) is 8.
`FUN_00476b12` tests it (`00476be6 mov al, byte ptr [edi + 0x180]`,
`00476bec cmp al, 8` / `00476bee je 0x476d31`, also 0x15..0x17 and 0x1c via
`00476bf4` .. `00476c02`), puts the missile on its `[+0x50]` entity
(`00476d31` .. `00476d4d`) and zeroes the velocity and the word
(`00476d50` .. `00476d6b fstp dword ptr [esi + 0x28]`). The 0.8 word is
overwritten for 859 whatever the branch: an 859 missile does not move. Its
field 18 is 5, so it is removed on its first physics pass (lifetime
time() + 5, `00476bbe`), at its target's position, where (C)'s 40-unit gate is
measured. The previous `MissileFlightTime(859)` = 9.05 / 16 = 0.566 s rule was
the misreading and is removed. Values:

```
python3 -I -c "
for l in open('/mnt/projects/EverQuestP1999/spells_us.txt',encoding='latin-1'):
    f=l.split('^')
    if f[0] in ('38','93','859'): print(f[0],f[18],f[98],f[122],f[145])"
38 0 1 3 80
93 0 5 0 80
859 5 8 0 85
```

### What `FUN_00476b12`'s 0.02 term is

What the original B1163 text called the "run value" is not a speed:
`FUN_00476b12` (the spawner) computes `time() - [[caster+0x1a8]+0x50]`
(`00476c08 mov ebx, dword ptr [esi + 0x48]`, `00476c0b mov edi, dword ptr [ebx + 0xfe4]`
= the caster, `00476c18 call 0x46d655`, `00476c23 sub eax, dword ptr [ecx + 0x50]`),
times 0.02 (`00476c36 fmul dword ptr [0x6410b8]`), and adds that many ticks of
the missile's velocity `+0x1c/+0x20/+0x24` to its position (`00476d11` ..
`00476d2c`) for a travel type other than 3. `[[caster+0x1a8]+0x50]` is the
caster's last physics tick (`FUN_004bfe0f` writes `piVar10[0x14] = iVar5`,
exe.c 146708). `FUN_00476b12` runs while the missile has no actor (exe.c
60443-60445) and creates it, so the term is a **one-shot spawn-position
catch-up**. The `+0x260` key (`00476c3c mov edi, dword ptr [edi + 0x260]`) is
the caster entity's race/type field (setter `0047a74a`), also not a file field.

**Implemented** (`include/client/graphics/spell_b1163_missile_rules.h`):
`kMissileThrustWordSpell` / `kMissileThrustWord859` (the code's words, named
as thrust), the update constants (`kMissileThrustScale` 0.8,
`kMissileDamping` 0.625, `kMissileDampingNo62` 0.85, `kMissileDtPerUnit` 0.02,
`kMissileMinPassUnits` 33, `kMissileHitDistance` 2.0,
`kMissileInitialStored` 2000.0), and `kMissileSpeed = 30` as the
**calibration**, with `MissileFlightTime()` = 9.05 / 30 = 0.302 s for every
missile fable builds (`src/client/graphics/spell_visual_fx.cpp`,
`createCastGlow`). The update loop (`MissilePassesToHit`, the same loop as
`missile_model.py`) and the no-flight target-type rule live in
`tests/test_b1163_missile_rules.cpp`, not in the header: fable does not run
them (SpellVisualFX has no spell target type, and wiring either would change
what fable draws for records it does build; FOLLOW-UP), and a header function
with only a test caller fails `ZeroCallers`. Tests:
`B1163MissileRules.ThrustWordIsNotASpeed`, `.ClosingTimeDependsOnThePassRate`,
`.TargetTypesWithNoFlight`, `.Record80ClockUnchanged`.

**Behaviour is unchanged, so no re-render.** `MissileFlightTime()` is
`9.05f / 30.0f`, the same float as the 57166711 tip's `9.05f / (1.5f × 20.0f)`
for every spell but 859, and fable builds no 859 missile (record 85 has no
Middle, (C)); the (B) and (D) renders of 57166711 stand for this tip.

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

The flight start / flight end cells above are RETIRED with their source
columns (`refactor/B1152/missile_oracle.tsv`, B1168; B1170 MR !330): the
oracle never drew the flight. The arrival cells are live oracle measurements.

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
| flight start (RETIRED) | −1.144 .. −0.484 | −1.177 .. −0.451 |
| flight end (RETIRED) | −0.680 .. +0.156 | −0.713 .. +0.189 |
| arrival | +0.116 .. +0.590 | +0.083 .. +0.623 |

The flight start / end bands are RETIRED with their source columns (B1168: the
oracle drew no Middle flight; B1170 MR !330). The arrival band is a live
oracle measurement (the `arrival_*` columns were never retired).

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
| 38 | flight start | −0.486 | −0.486 | −0.002 | +0.658 | +0.560 | −1.177 .. −0.451 | **RETIRED** |
| 38 | flight end | −0.184 | −0.184 | 0.000 | −0.340 | +0.496 | −0.713 .. +0.189 | **RETIRED** |
| 38 | arrival | +0.116 | +0.116 | 0.000 | −0.474 | −0.338 | +0.083 .. +0.623 | **RETIRED** |
| 93 | flight start | −0.486 | −0.486 | −0.002 | +0.658 | +0.560 | −1.177 .. −0.451 | **RETIRED** |
| 93 | flight end | −0.184 | −0.184 | 0.000 | −0.340 | +0.496 | −0.713 .. +0.189 | **RETIRED** |
| 93 | arrival | +0.116 | +0.116 | 0.000 | −0.474 | −0.338 | +0.083 .. +0.623 | **RETIRED** |
| 54 | (no Middle: record 82) | no missile | no missile | — | — | — | — | unchanged (M2_54 +0.458, M2b_54 +0.478 are createLanding's, not a missile row) |
| 41 / 458 / 656 | (no drawn Middle) | no missile | no missile | — | — | — | — | unchanged |
| 859 | (record 85: no Middle) | no missile | no missile | — | — | — | — | unchanged (see C) |

**The six 38 / 93 rows are RETIRED** (B1170 MR !330, 2026-10-28): B1168 proved
the client draws record 80's Middle stage only on a `0x4033` missile object
the lane-5 server never sends, so the launch@−0.486 / reach@−0.184 /
land@+0.116 gate measured a flight the oracle never drew. Struck, not deleted;
the 54 / 41 / 458 / 656 / 859 rows are unchanged.

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
- **Four early-outs** (re-review note 7704, finding 2), all passed by 859:
  - `00477947 cmp al, bl` / `00477949 jne 0x477aab`: the stage-2 fires (both
    54 and 859) run only when `[[missile+0x48]] != 0` (`00477927 xor al, al`,
    `00477929 cmp dword ptr [ecx], ebx`, `0047792d inc al`: al = 1 when it is 0).
    In fable a missile exists only with its emitter, so this holds by
    construction.
  - `0047f4c2 call 0x44d0d0` / `0047f4ca je 0x47f5f5`: the spell id must be in
    1..9999 (`0044d0d3 cmp dword ptr [esp + 4], eax` with eax = 1,
    `0044d0d9 cmp dword ptr [esp + 4], 0x2710`).
  - `0047f4de test edi, edi` / `0047f4e0 je 0x47f5f5`: the spell record must
    exist.
  - `0047f4e6 cmp byte ptr [edi + 0x180], 0` / `0047f4ed je 0x47f5f5`: no fire
    when the record's `+0x180` is 0. `+0x180` is spells_us.txt field 98 (target
    type): `FUN_0044dbf0` stores the field read right after the twelve SPA ids
    (`+0x120`, fields 86-97) as the byte at `param_1 + 0x60` (an int*, so byte
    `+0x180`). Spell 859's is 8 (table in (A)), so it passes; its first SPA
    (field 86) is 0, so the position override at `0047f57b`..`0047f594` is not
    taken.
  - Encoded as `SpellMissileRules::Spell859RemovalFires(Spell859RemovalInputs)`
    and `SpellIdInRange()`, test `B1163MissileRules.Spell859RemovalEarlyOuts`,
    and `fireSpell859RemovalAtPlayer` now decides through it: actor object =
    the missile was launched, record = present (fable built the missile from
    it), target type = `kSpell859TargetType` (8, spells_us.txt field 98:
    SpellVisualFX carries no spell target type, and the gate fires only for
    0x35b). For 859 every early-out passes, so the decision is the same as the
    two-input gate's.

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
(the truth table incl. the 40.0 boundary and NaN), `.Spell859RemovalEarlyOuts`
and `.GateDistanceIsXY`.

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

So in fable no 859 missile exists, and the removal fire does not run for it
yet: the harness 859 run prints no `B1152 missile` line before
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
  and for every other spell the flight time was the same float as the old
  9.05 / 30 (the leg-2 rework makes it literally 9.05 / 30 for all, see (A)). Checked on renders, not only by
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

- `tbuild <wt> --target model_viewer test_fx_harness test_b1163_missile_rules`
  and every `test_spell*` target → exit 0.
- `tbuild <wt> test -R 'B1163MissileRules|FxHarness|^Spell'` → **100 % tests
  passed, 0 failed out of 416** (29 skipped by their own data guards), incl.
  the seven `B1163MissileRules.*` cases and the six `FxHarness.*` cases.
- `tbuild <wt> hygiene` → **100 % tests passed, 0 failed out of 48**
  (`ZeroCallers` included: the test-only model loop lives in the test file).
- No re-render in leg 2: behaviour is unchanged ((A), last paragraph).
- Whole-tree CI build: the MR !313 pipeline on the final push (id in the MR
  note for this rework).
- Cite checker: `python3 -I scripts/refactor/cite_check.py refactor/B1163/README.md --tree . --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked 28 cites, 0 problems, 0 address warnings`. The checker reads
  `0x`-prefixed addresses only, so every bare 8-digit address in this README,
  the rules header, the test and the `spell_visual_fx` diff was also checked
  by script against `eqgame.dis` (270 bare addresses, all instruction starts;
  96 quoted `address instruction` pairs, all matching), and every `exe.c N`
  line was read.

## RULING REQUEST

Should fable build a missile for a spell whose record has no drawn Middle stage
(the oracle builds the missile object from the `0x4033` message, `00460737`,
independent of the Middle emitters)? Until it does, spell 859 (record 85, no
Middle) has no fable missile, so its at-player Target stage (implemented here)
never runs.

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

- `include/client/graphics/spell_b1163_missile_rules.h` — the thrust word and
  the traced update constants, the calibrated timing and the 859 removal gate
  with its early-outs, device-free.
- `include/client/graphics/spell_visual_fx.h`, `src/client/graphics/spell_visual_fx.cpp`
  — per-spell flight time, `fireSpell859RemovalAtPlayer`, the local-player
  callback.
- `include/client/graphics/entity_renderer.h`,
  `src/client/graphics/renderer/renderer_entities.cpp` — `getPlayerSpawnId()`
  and its wiring into `SpellVisualFX`.
- `tests/test_b1163_missile_rules.cpp`, `tests/cmake/spells.cmake` — the seven
  device-free cases (and the test-local update loop).
- `refactor/B1163/scripts/missile_model.py` — the (A) closing-time table.
- `refactor/B1163/scripts/`, `refactor/B1163/plans/` — the sweep and gate
  scripts and plans (`render_b1130gate_local.sh`, `plans/b1130gate/s859.tsv`
  added by the rework).
