# B1168 (#487): which oracle path draws the record-80 missile flight with no 0x4033 on the wire

Branch `task/kb-b1168-missile-path`, rebased onto `gitlab/fable 1297ec6e` (B1163 merged).
Every address below is an instruction start in
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis` with the quoted
instruction as it stands there (checked by `refactor/B1168/scripts/check_addrs.py`,
section E); `exe.c N` is a line of
`/mnt/projects/fable/review/B1141/ghidra/out/eqgame.exe.c`. Evidence:
`/mnt/projects/fable/review/B1168/worker/`.

**Result in one line.** No such path exists. The client draws a record's Middle
stage (stage 1) only from two call sites, and record 80's only Middle slot
(EDD 279, anchor 2) is drawn only on a missile object, which only a received
`0x4033` creates. None of the three oracle captures carries a `0x4033`, so the
oracle drew no record-80 Middle stage in M1_38, M1b_38 or M3b_93; and a
re-measure of the B1152 "flight" pixels shows nothing moving from caster to
target in any of the three legs. There is no speed to derive for those legs.
**fable is not changed** (section C); the conflict with fable's B1152 missile is
the RULING REQUEST.

## (A) The drawing path, traced

### A1. Where a stage is drawn

`FUN_0049b260(stage, spell, caster, target, pos, missile, …)` (exe.c 121989) is
the stage dispatcher. For a spell with a `spellsnew.eff` record
(`0049b284 cmp dword ptr [esi + 0x234], ecx`) it hands the stage to
`FUN_00495cc3`, passing its own 6th argument as `FUN_00495cc3`'s 7th:

```
0049b33f  push dword ptr [ebp + 0x1c]     ; FUN_0049b260 arg 6 -> FUN_00495cc3 arg 7
0049b34a  push edi                        ; the stage
0049b34c  call 0x495cc3
```

`FUN_00495cc3` walks the stage's four slots (`00495cff imul eax, eax, 0x44`,
`00495d02 lea esi, [eax + ecx + 0x48]`, `00495d09 mov dword ptr [ebp - 0x18], 4`)
and switches on each slot's anchor word (`00495d45 mov eax, dword ptr [eax + 4]`):

```
00495d4c  sub eax, ebx              ; anchor 0 -> 00495dbe: the explicit position (arg 6)
00495d53  dec eax                   ; anchor 1 -> 00495db0: the caster entity (arg 4)
00495d56  dec eax
00495d57  je 0x495d8e               ; anchor 2 -> 00495d8e
00495d59  dec eax                   ; anchor 3 -> 00495d60: the target entity (arg 5)
00495d8e  mov eax, dword ptr [ebp + 0x20]   ; anchor 2 reads arg 7, the missile object
00495d91  cmp eax, ebx
00495d93  je 0x495e6d               ; no missile object: the slot is SKIPPED
00495d99  mov ecx, dword ptr [eax + 0x10]   ; else the emitter starts at missile +0x10/+0x14/+0x18
00495da8  mov eax, dword ptr [eax + 0x48]   ; and rides the missile's actor (+0x48)
```

Record 80's stages, read from `/mnt/projects/EverQuestP1999/spellsnew.eff`
(record stride 0x10c, stage base +0x40, stride 0x44; slot = emitter, min level,
anchor, attach):

```
python3 -I - <<'EOF'
import struct; d=open('/mnt/projects/EverQuestP1999/spellsnew.eff','rb').read()
for st in range(3):
    o=80*268+st*0x44+0x40; print(st,[struct.unpack_from('<iiii',d,o+4+16*k) for k in range(4)])
EOF
0 [(276, 0, 1, 4), (276, 0, 1, 5), (277, 39, 1, 1), (278, 24, 1, 1)]
1 [(279, 0, 2, 1), (0, 0, 1, 0), (0, 0, 1, 0), (0, 0, 1, 0)]
2 [(280, 0, 3, 1), (281, 0, 3, 1), (282, 39, 3, 1), (9, 24, 3, 1)]
```

So the Middle (stage 1) has exactly one slot, EDD 279 with **anchor 2**: it is
drawn only when the caller passes a missile object, and it then moves with
that object. Nothing in the EDD row moves it from caster to target: the
emitter's position source is the anchor, and anchor 2 is the missile.

### A2. Who fires stage 1

Every direct call of the three emitter entry points in `eqgame.dis`
(`grep -n 'call 0x49b260\|call 0x495cc3\|call 0x49ab78'`), with the stage argument:

| call | stage | source of the stage | arg 7 of `FUN_00495cc3` |
|---|---|---|---|
| `00476ecc call 0x49b260` | 1 or 2 | `00476ec9 push dword ptr [ebp - 0xc]`; 1 at `00476b46` / `00476c11`, 2 at `00476d52` (target-anchored types) | the missile object: `00476ebf push esi` |
| `004b6970 call 0x495cc3` | 0, 1, 2 | `004b69a2 push 1`, `004b69bc push 2`, exe.c 138248 (0) | **0**: `004b6958 push 0` |
| `0049b982 call 0x49b260` | 0 | exe.c 122270 | 0 |
| `004bff96 call 0x49b260` | 0 | `004bff95 push esi` (esi = 0, exe.c 146701) | 0 |
| `0047f5ed call 0x49b260` | 2 | `0047f5eb push 2` | 0 (exe.c 101960) |
| `00477aa3 call 0x49b260` | 2 | `00477aa1 push 2` | — |
| `00454476 call 0x495cc3` | 3 | exe.c 74000 | 0 |
| `004544e2 call 0x495cc3` | 2 | exe.c 74028 | 0 |
| `00481b03 call 0x495cc3` | 2 | `00481b00 push 2` | 0 (`00481af6 push edx`, edx = 0) |
| `0041760f call 0x49ab78` | 2 | `0041760d push 2` | (legacy path) |
| `0043699d call 0x49ab78` | 2 | `0043699b push 2` | (legacy path) |

No absolute pointer to `0x495cc3`, `0x49ab78`, `0x49b260`, `0x476b12`,
`0x476f34` or `0x4b6908` exists in the image (a 4-byte little-endian scan of
`/mnt/projects/fable/review/B1141/bin/eqgame.exe` finds none), so these are all
the callers.

Stage 1 therefore comes from two places only:

1. **`00476ecc`, the missile spawner `FUN_00476b12`**, with the missile object as
   the anchor-2 argument. `FUN_00476b12` runs only on entries of the missile
   list `[0x905cd0]` (`00445556 call 0x476b12`), which only the constructor
   `FUN_00476f34` fills, and its only caller is the `0x4033` arm of the message
   handler: `004606ee sub ecx, 0x105` (0x3f26 + 0x105 = 0x402b),
   `004606f6 sub ecx, 8` / `004606f9 je 0x46070f` (0x4033),
   `0046070f push 0x88` (the 0x88-byte object), `00460737 call 0x476f34`, with
   the message as the constructor's third argument (`00460732 push ebx`), i.e.
   the RECEIVED branch (B1163 (A), `refactor/B1163/README.md`).
2. **`004b69a6`, the scheduled three-stage sequence `FUN_004b697f`**
   (stage 1 once its elapsed time passes `[esi+0xc]`:
   `004b6998 cmp edi, eax` / `004b699a jbe 0x4b69ab`), created only by
   message `0x22c5` (`0045ff15 call 0x4b6bc5`, `0045ff2e call 0x4b6b1b`;
   exe.c 82022-82030). It passes **0** as the anchor-2 argument
   (`004b6958 push 0`), so on this path record 80's EDD 279 slot is skipped
   at `00495d93`.

The measured legs have neither message:

```
for f in /mnt/projects/fable/review/B1152/worker/oracle/m1_rig38/all-opcodes.tsv \
         /mnt/projects/fable/review/B1152/worker/oracle/m2_rig54/all-opcodes.tsv \
         /mnt/projects/fable/review/B1159/worker/oracle/m_b1159_rig/all-opcodes.tsv; do
  awk -F'\t' '$4=="0x22c5"||$4=="0x4033"' $f | wc -l; done      # 0 0 0
```

(The opcode column is the wire opcode the handler switches on: OP_BeginCast
`0x3990` is `0045d500 sub ecx, 0x3990`, OP_Action `0x497c` is
`00460af8 sub ecx, 0x1d5` after `00460ae7 sub ecx, 0x475e` / `00460af3 sub ecx, 0x49`.)

### A3. What the oracle did draw in those legs

- **Source (stage 0) on OP_BeginCast:** `0045d506 je 0x45d565` →
  `0045d584 call 0x49b39d` → `0049b982 call 0x49b260` with stage 0 (exe.c 122270).
- **Target (stage 2) on OP_Action:** `00460afe je 0x460b0e` →
  `00460b3e call 0x47f614` → `0047f630 call 0x47f4af` → `0047f5eb push 2` /
  `0047f5ed call 0x49b260`.
- **Middle (stage 1): nothing.** Neither the missile spawner nor the sequence
  ran, and the sequence would have skipped the anchor-2 slot anyway.

### A4. The speed source, and the derived flight times

The only code that moves a Middle emitter is the missile object's motion
(the anchor-2 emitter rides `[missile+0x48]`, `00495da8`): the damped thrust
model B1163 traced, `v' = 0.625 v + 0.8 × [missile+0x28]`, `pos += 0.02 × elapsed × v`
(`refactor/B1163/README.md` (A)). On the received branch the thrust word,
velocity and position are copied FROM the message (`004770eb mov eax, dword ptr [edi + 0x28]`,
`004770ee mov dword ptr [esi + 0x28], eax`), so **the speed source is the
server's `0x4033` message**, not a spell or EDD field.

| spell | record | Middle drawn in the measured legs? | derived flight | B1152/B1159 "flight" |
|---|---|---|---|---|
| 38 | 80 | no (no `0x4033`) | **none** | M1_38 0.300 s, M1b_38 1.300 s |
| 93 | 80 | no (no `0x4033`) | **none** | M3b_93 0.366 s |
| 54 | 82 | no (record 82 has no Middle slot) | none | none |

(The "flight" spans are `flight_end_v − flight_start_v` of
`refactor/B1152/missile_oracle.tsv`: 472.633 − 472.333, 484.933 − 483.633,
620.733 − 620.367.)

### A5. The B1152 "flight" pixels, re-measured: nothing travels caster → target

`refactor/B1168/scripts/gap_track.py` tracks the ≥180-grey pixels in the same
deep-gap box B1152 used (x 660-760, y 440-620; the target stands left of it,
the caster right of it) and prints their count and centroid per video frame
(`/mnt/projects/fable/review/B1168/worker/gap_track_{m1_38,m1b_38,m3b_93}.tsv`):

```
python3 -P refactor/B1168/scripts/gap_track.py /mnt/projects/fable/review/B1152/worker/oracle/m1_rig38/video.mp4 14166 14183 <out>
python3 -P refactor/B1168/scripts/gap_track.py /mnt/projects/fable/review/B1159/worker/oracle/m_b1159_rig/video.mp4 14505 14552 <out>
python3 -P refactor/B1168/scripts/gap_track.py /mnt/projects/fable/review/B1159/worker/oracle/m_b1159_rig/video.mp4 18607 18626 <out>
```

- **M1_38 (f14170-f14179, the B1152 calibration leg): a stationary blob.**
  17-41 px, centroid x 719.2-719.8, y 535.6-537.0 on all ten frames; 0 px on
  f14169 and f14180. A 9.05-unit flight in 0.30 s would cross the 100-px box,
  this did not move by more than 0.6 px.
- **M3b_93 (f18611-f18622): moves AWAY from the target.** Centroid x 699.4
  (f18611) → 724.9 (f18622) → 742.5 (f18623) → 759.0 (f18624), i.e. toward the
  caster.
- **M1b_38 (f14509-f14548): no direction.** 45-280 px, centroid x jumping
  between 670.6 and 754.9 (f14526 682.5 → f14529 754.9 → f14530 695.2).

Together with A2: the pixels B1152 read as the missile are not a caster-to-target
flight, consistent with the code drawing no Middle in those legs. The 0.302 s
calibration therefore has no oracle flight behind it.

### A6. The server side (why no `0x4033`)

The server is the only sender the code leaves: the client's own send
(`00477606 push 0x4033`) sits in the constructor's LOCAL branch, which the
single call `00460737` never takes (its third argument is the message,
`00460732 push ebx`; B1163 (A)). In the lane-5 server source
(`/opt/akk-stack/code` on the ThinkPad, commit `ba2ca5eada80ac758ef8c2a2510cb39b0563119a`;
excerpts with line numbers in `/mnt/projects/fable/review/B1168/worker/server_excerpts.txt`):
`OP_SomeItemPacketMaybe=0x4033` (utils/patches/patch_Titanium.conf line 585) is
sent by `Mob::ProjectileAnimation` (zone/special_attacks.cpp lines 1838-1857,
`QueueCloseClients`), which spells reach through `TrySpellProjectile` for target
type `ST_TargetOptional` = 1 (zone/spells.cpp lines 2585-2586,
common/spdat.h line 997), and that path delays the damage to a projected hit
time `1200 + 10 × distance / speed` ms (zone/spell_effects.cpp line 9679).
In M1_38 the damage message is on the OP_Action tick (all-opcodes.tsv rows
15588 OP_Action and 15590 `string_id=9073 … 'Testcaster', 'Oraclewatch', '121'`,
both 23:50:44.973), and no `0x4033` row exists: this server did not take the
projectile path for these casts. Why (the server's own `spells_new.targettype`
for 38/93) was NOT read: the lane-5 stack was down
(`eq_docker ps` on 172.16.200.205 lists no container) and the card has no live
step. FOLLOW-UP.

## (B) The oracle's frame interval

`refactor/B1168/scripts/frame_cadence.py` reads every video frame of a window,
takes the whole-frame mean absolute grey difference to the previous frame and
counts repeats (difference < 0.005; the measured gap in M1_38 is ≤ 0.0015 for
repeats vs ≥ 0.018 for changed frames). Output
`/mnt/projects/fable/review/B1168/worker/cadence_summary.txt`, per-frame rows
`cadence_<leg>.tsv`:

| leg | video frames | repeated frames | client-frame duration (video frames) |
|---|---|---|---|
| M1_38 f14079-f14215 | 137 | 8 (f14208-f14215, after the observer's death at f14199) | 1 (128 of 128) |
| M2_54 f13654-f13745 | 92 | 0 | 1 (91 of 91) |
| M1b_38 f14438-f14575 | 138 | 0 | 1 (137 of 137) |
| M2b_54 f16513-f16600 | 88 | 0 | 1 (87 of 87) |
| M3b_93 f18567-f18670 | 104 | 0 | 1 (103 of 103) |

So the observer client presented a new image on every 1/30 s video frame
through every cast window: its frame interval is **≤ 0.0333 s**; the 30 fps
recording cannot resolve it further. Measured inside the gap box (A5), the
particle content is identical on some consecutive frame pairs (f14170 = f14171,
f14174 = f14175, f14176 = f14177; f18611 = f18612, f18614 = f18615,
f18616 = f18617: same count and centroid), which the whole-frame test does not
see; no claim about the particle update rate is made from it. The missile
object's pass gate (`00445580 cmp ecx, 0x21` / `00445583 jb 0x44559f`) is in
`time()` units, and `time()` is `rdtsc` divided by a runtime calibration
(`0046d655 call 0x4cc98f`, `004cc99d rdtsc`, `0046d668 call 0x60f930` with the
divisor `[0x988c68]` stored at `004cff37` / `004cff48`): its unit is not a
constant in the image. With no missile object in these legs (A2), neither
number enters a flight time.

## (C) Ruling on `0x4033`, and the fable decision

- **From the code:** the `0x4033` missile object is created only from a
  received `0x4033` (`00460737`); the client never sends one itself (A6).
  Whether the gate spells get one is the server's choice (A6). In the three
  oracle legs they did not, so **the oracle drew no Middle stage for 38 / 93**.
- **There is no "Middle-emitter motion" to give fable instead of an object**:
  an anchor-2 Middle slot has no position without the object (`00495d93`).
- **No fable change.** The card allows replacing the calibration only if a
  derived flight matches the measured one within one 30 fps frame on 38, 54
  and 93. The derivation gives no flight; the measured spans (0.300 / 1.300 /
  0.366 s) are not flights (A5). There is nothing to replace the 30 u/s
  calibration WITH, and removing fable's B1152 missile (the code-faithful
  consequence for this server) or adding a `0x4033` handler is a behaviour
  change outside this card's change condition: RULING REQUEST below.

## Ask 4: the no-flight target types

The target-type dispatch (`00476bec cmp al, 8` … `00476c02 je 0x476d31`) is in
`FUN_00476b12`, which runs only on a missile object (A2, item 1). In the
measured legs it never ran, so the oracle applied no target-type rule on the
drawing path. `kSpell859TargetType` and the target-type rule stay
**pinned, not wired** (`include/client/graphics/spell_b1163_missile_rules.h`,
`tests/test_b1163_missile_rules.cpp`, unchanged).

## (D) Gate rows

**No change**: no fable code changed, so the B1163 renders of the missile gate
rows stand (`refactor/B1163/README.md` (B):
`/mnt/projects/fable/review/B1163/worker/gate_after/b1130gate.log`, 38 / 93
`launch@-0.486 reach@-0.184 land@+0.116`, 54 / 41 / 458 / 656 / 859 no
missile line). Against A5 those 38 / 93 rows draw a flight the oracle legs did
not draw (RULING REQUEST).

## (E) Checks

- Cite checker:
  `python3 -I scripts/refactor/cite_check.py refactor/B1168/README.md --tree . --extra /mnt/projects/fable --dis /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis --dis /mnt/projects/fable/review/B1141/ghidra/out/EQGraphicsDX9.dis`
  → `checked 39 cites, 0 problems, 0 address warnings`.
- Bare addresses and quoted `address instruction` pairs:
  `python3 -I refactor/B1168/scripts/check_addrs.py refactor/B1168/README.md /mnt/projects/fable/review/B1141/ghidra/out/eqgame.dis`
  → `checked 85 bare addresses, 76 quoted instructions, 0 problems`.
- ThinkPad (`tbuild`): `--target test_b1163_missile_rules test_fx_harness`,
  `test -R 'B1163MissileRules|FxHarness'` (100 % passed, 0 failed out of 14),
  `hygiene` (100 % passed, 0 failed out of 48); logs
  `/mnt/projects/fable/review/B1168/worker/{build,ctest,hygiene}.log`.

## RULING REQUEST

The code says the record-80 Middle stage (EDD 279, anchor 2) is drawn only on a
missile object that a server `0x4033` creates, and the three oracle legs had
none, while fable's B1152 missile draws EDD 279 flying caster → target on every
record-80 cast (`createCastGlow`, 0.302 s calibration). Choose:
(a) drop fable's record-driven missile and build it only from a server
`OP_SomeItemPacketMaybe` (`0x4033`) message (fable has no handler today), which
on this server draws no record-80 flight, matching the legs; or
(b) keep the B1152 missile as a fable deviation, labelled as such.
Recommendation: (a).

## FOLLOW-UP

- Read the lane-5 server's `spells_new.targettype` (and the
  `Spells:UseLiveSpellProjectileGFX` rule) for 38 / 93 to state why the server
  sent no `0x4033` (A6).
- An oracle leg where the server DOES send `0x4033` (a target-type-1 bolt on a
  server that takes `TrySpellProjectile`) would give the flight the B1163 model
  describes, and the pass interval with it.

## Files

- `refactor/B1168/README.md` — this trace.
- `refactor/B1168/scripts/gap_track.py` — A5 centroid track.
- `refactor/B1168/scripts/frame_cadence.py` — B frame cadence.
- `refactor/B1168/scripts/check_addrs.py` — bare-address / quoted-instruction check.
