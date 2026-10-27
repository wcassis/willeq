# B1174 (#493): record 325 (Leechcurse /disc) observed on the oracle

**Outcome.** Record 325 now has an oracle capture on the real `/disc` path.
Leg `b1174_disc8604` ran on lane 5 on 2026-10-27, 18:40-19:2xZ, with rc=0.
The client sent C->S `OP_CastSpell` slot 10 spell 8604. For this leg only,
the lane server's row 8604 was pointed at its own Leechcurse Discipline row.
The server accepted the cast and sent S->C `OP_Action` 8604 to the caster and
to the observer. The server row was restored byte for byte at stop.

**What the oracle draws** is the caster's `t06` swing (CastingAnim 44) from
Act+0.42 to Act+4.36. It draws no sprite. Record 325 has Source slots only,
there is no `OP_BeginCast`, and its Target stage is empty.

**What fable was missing** was the swing. eqgame's `OP_Action` handler asks
the standing source for the spell's CastingAnim. fable played that track
only on `OP_BeginCast`. It now plays it on `OP_Action` as well, in both the
client and the FX harness. The two B1162 "draws nothing" pins are replaced
by tests of the swing, and the ledger clip shows the caster on both sides.

The issue says B1130 has record-325 numbers. **It has none.**
`review/B1130/record_table.tsv` line 5 (row 51) says "325 (3 SHD spells ...)
... not cast here". This README compares against B1130's record-326 row
instead, on the same line. That row is the one disciplines share, measured
on the GM path.

## 1. Why the lane refused 8604, and the server-data change

B1162 ("Correction: Leg A did not land", `refactor/B1162/README.md:450`)
found that the client sends 8604 and the server refuses it. The lane's
`spells_new` row 8604 is "Bear Claw", and every class is 255 in it. The
server's `Client::UseDiscipline` prints "Your class cannot learn from this
tome." (akk-stack `zone/effects.cpp` lines 885-890).

The read-only query (`review/B1174/worker/lane5_spells_new_8604_readonly_20261027.txt`,
taken under the lease with only mariadb started, then put back down) shows
54 of 237 columns differ between row 8604 Bear Claw and row 4504, the
server's own Leechcurse Discipline:

| column | 4504 Leechcurse Discipline | 8604 Bear Claw |
|---|---|---|
| classes5 (SHD) | 60 | 255 |
| targettype | 6 (self) | 5 (single target) |
| goodEffect / effectid1 / effect_base_value1 | 1 / 178 / 100 | 0 / 0 / -500 |
| buffduration / buffdurationformula | 4 / 11 | 0 / 0 |
| recast_time | 4050000 | 12000 |
| cast_time | 0 | 0 |

**Why not just set classes5 = 60.** That alone would pass the class gate,
but it would then cast Bear Claw: a single-target spell (targettype 5) with
no target, since the `/disc` payload's target id is 0. The server's
`CastSpell` refuses that at akk-stack `zone/spells.cpp` lines 196-200:
`DoCastingChecksOnTarget` returns false at lines 781-783 (no target, and the
spell is neither ST_Self nor a group spell), and `CastSpell` then calls
`StopCastSpell`. Were that check skipped, `DoCastSpell` would refuse it at
lines 395-406 with SPELL_NEED_TAR. Either way no `OP_Action` would come.

**What the leg did instead.** It took the issue's second option and pointed
the lane's row at the client's own spell. Every column of the server's 4504
was copied onto 8604, keeping the id. This is one row, made reversible as
follows:

- `refactor/B1174/sql/b1174_server_8604.sql` saves the original row whole in
  `_b1174_spells_new_8604`, then `REPLACE`s 8604 from a temporary copy of
  4504.
- `refactor/B1174/sql/b1174_server_8604.undo.sql` `REPLACE`s the saved row
  back, then drops the helper table.
- `refactor/B1174/sql/b1174_server_8604.dump.sql` is the dump: rows 4504 and
  8604, `CHECKSUM TABLE spells_new EXTENDED`, and the count of helper tables.

**The zone reads its spells only at boot.** They live in shared memory, which
`up.sh`'s `bin/shared_memory` and the container's launcher build at start. A
change made after `up.sh` would therefore never reach the zone. The driver
gained a server-data hook for this:

- `scripts/eqemu/oracle/oracle_session.sh:196` runs `ORACLE_SERVER_SQL`
  inside the lease, with the stack down and only mariadb started, before
  `up.sh`.
- Once the hook is armed, the cleanup runs the undo on every exit path, with
  the world down. It exits 66 unless the dump equals the before-dump byte for
  byte (`oracle_session.sh:159`).
- `oracle_class_leg.sh:45` exposes the hook as `--server-sql-file`,
  `--server-undo-sql-file` and `--server-dump-sql-file`.
- It is documented in `docs/oracle_driver.md` ("A leg-only server-data
  change").

The dumps are in `review/B1174/worker/oracle/b1174_disc8604/`:

| dump | row 8604 | `CHECKSUM TABLE spells_new` | helper tables | md5 |
|---|---|---|---|---|
| `server-data-before.tsv` | Bear Claw | 2457406974 | 0 | a870dc14... |
| `server-data-leg.tsv` | Leechcurse Discipline | 3968827195 | 1 | 31a020a6... |
| `server-data-after.tsv` | Bear Claw | 2457406974 | 0 | a870dc14... (`cmp` equal) |

`driver.log` has the line `server data restored BYTE-IDENTICALLY (4 dump lines)`.

## 2. The leg

**Command** (hermes-ext, from the worktree at its pushed tip):
`refactor/B1174/scripts/run_leg_b1174.sh b1174_disc8604`. That runs
`oracle_class_leg.sh 5 5`: class 12 -> 5 Shadow Knight for the leg, with the
three server-data files above.

**Plan.** `refactor/B1174/plans/b1174_disc8604.tsv` is the B1162 4499 plan
(`plans/b1162_disc4499_v3.tsv`) with `/disc Leechcurse Discipline`. It uses
the same `pose=` / `observer-pose=` directives and the observer's F9 x3
camera.

**Pre-SQL.** `refactor/B1174/sql/b1174_disc8604.sql` sets level 60 and makes
`character_disciplines` (67, 0, 8604) the ONLY discipline. The client's
`/disc` takes the first prefix match in its live list, so 8604 is the one it
picks (B1162, "Leg A result"). `b1174_disc8604.post.sql` restores the level.

No `# char-ini-set` was needed: a `/disc` is not an ability slot. The leg
waited on the lease and never broke it. On the contended ThinkPad (load
about 38 from other cards' renders) it took 40 min.

**The oracle client's files are the ones read here.** The read-only md5s are
in `review/B1174/worker/oracle_client_files_md5_20261027.txt`:
- `spells_us.txt` c598b20a... equals `review/B1162/worker/spells_us.txt`.
- `spellsnew.eff` 2dd100b7... equals `/mnt/projects/EverQuestP1999/spellsnew.eff`.
- `eqgame.exe` a9de1b8c... is the decompiled binary.

**Pcap lines.** The caster's port is 37938 and the observer's is 55891. The
decoder is `refactor/B1174/scripts/pcap_lines.py`; the window is in
`review/B1174/worker/pcap_disc_window.txt`.

```
1793128268.391 S->C 37938 OP_DisciplineUpdate 400  9c210000 ...          (zone-in list: slot 0 = 0x219c = 8604)
1793128478.099 C->S 37938 OP_CastSpell 20  0a000000 9c210000 ffffffff 00000000 ...  (slot 10, spell 8604, target 0)
1793128478.118 S->C 37938 OP_Action 31  0401 0401 3c00 0a000000 ... e7 0000 0000 9c21 3c 00  (target 260 = source 260, type 0xe7, spell 8604, effect_flag 0)
1793128478.118 S->C 37938 OP_Action 31  same, effect_flag 04 (caster only)
1793128478.118 S->C 55891 OP_Action 31  same, effect_flag 00 -> the OBSERVER
1793128478.118 S->C 37938 OP_DisciplineTimer 12  02000000 d20f0000 ...  (timer 2, 0xfd2 = 4050 s = 4504's recast_time)
1793128478.819 S->C 37938 OP_Buff 32  ... 9c210000 03000000 ...      (the buff, 8604; faded at 1793128502.925)
```

Each S->C line appears twice, 17 ms apart (.118 and .135), in this capture.

The census over the whole pcap (`--census`) is:
- C->S: 1 `OP_CastSpell`.
- S->C: 6 `OP_Action`, all 8604.
- S->C: 4 `OP_BeginCast`, all `57000b00d0070000` at 1793128312.783/.799.
  That is source 87, spell 11, the ambient qeynos2 NPC that B1162 also saw,
  166 s before the `/disc`.

There are no `OP_InterruptCast` lines and no refusal line. **There is no
`OP_BeginCast` from the caster.**

### Framing proof: the observer sees the caster (the coordinator's check)

The B1162 8604 clip showed an empty courtyard. `refactor/B1174/scripts/caster_bbox.py`
measures the caster's robe and skin pixels and the green name tag on the
observer stills. The full table is `review/B1174/worker/framing_bbox.tsv`.
The control column x 700..850 is a caster-free patch of the same scene.

| still | caster bbox (robe/skin) | name tag "Testcaster" | control px |
|---|---|---|---|
| **this leg, settle `15_wait.frame.png`** (Act-2.7 s) | x 910..967, y 428..621 (194 px), 3812 px | x 878..994, y 376..390 (407 px) | 16 |
| B1162 4499 leg `15_wait.frame.png` | x 908..968, y 428..621 (194 px) | x 878..994, y 376..390 | 227 |
| B1162 `b1162_disc325_v2`, steps 06..15 (the empty 8604 clip) | **NO CASTER in the box** (0 px) | 0 px | 0-17 |

The mask leaves out the dark boots, which is why the bbox stops at y 621
while B1134/B1162 put the feet at y 639/640. The head top, 428, is the same
on all three. The measurement geometry is `refactor/B1174/scripts/geom_b1174_disc8604.json`,
which is B1134/B1162's rig with the caster as the anchor (`--npc-id 260`).

## 3. What the client draws, from its code

All of the following is `review/B1141/ghidra/out/eqgame.exe.c`, the binary
with the same MD5. Every line cited here was read by hand: the cite checker
does not parse `.c` cites.

**The draw is keyed on the spell id in the packet, looked up in the client's
own table.**
- `FUN_0047f4af` (line 101892) is the `OP_Action` spell handler. The type
  0xe7 packet reaches it from 0x47f614, per `refactor/B1152/README.md:49`.
- Line 101911 reads the spell id from packet offset 0x1b:
  `sVar1 = *(short *)((int)param_2 + 0x1b)`.
- Line 101916, `iVar5 = FUN_0040cbd6((int)sVar1)`, takes the CLIENT's spell
  record for that id. `FUN_0040cbd6` is at line 13109 and indexes
  `param_1 + 0x24 + id*4`.
- Line 101960, `FUN_0049b260(2,iVar5,...)`, plays that record's Target stage
  (stage 2).
- `FUN_0049b260` (line 121989) draws the spellsnew.eff record held at
  `record[0x8d]`. The record parser stores that pointer at lines 69391-69394
  (`iVar5 = param_1[0x8e]` ... `param_1[0x8d] = _Str` = eff base + index x
  0x10c). The index is spells_us.txt field 145 (`docs/spellsnew_format.md:53-54`).
- So whatever the server calls 8604, the client draws the record that ITS
  8604 names: field 145 = 325. The rows are re-read from the md5-checked
  `spells_us.txt`:

| id | field 13 cast | class 5 (field 108) | field 120 CastingAnim | field 123 | field 145 |
|---|---|---|---|---|---|
| 8604 | 0 | 60 | 44 | 51 | 325 |
| 4499 | 0 | 255 (WAR 55) | 27 | 51 | 326 |

**Record 325 has no sprite to draw on this path.** `spellsnew.eff` record
325 is "Discipline - Black", decoded from the 0x10c-byte record:
- Stage 0, Source, has 4 slots: emitters 1844, 1843, 1845 and 1845, all on
  anchor 1 (the caster), at attach points 2 (CHEST), 2, 4 (LHAND) and
  5 (RHAND).
- Stage 1 and stage 2 have empty slots (emitter 0).

Record 326 has the same shape (1818/1817/1819/1819). The client plays a
Source only from its begin-cast handler, and B1144 found there is no
`OP_BeginCast` here, so no Source plays. The Target stage is empty, so there
is no landing.

**The source's swing (the code fable lacked).** Before the Target stage, at
lines 101923-101932 (disassembly 0x47f4f3..0x47f55c), the handler looks up
the source spawn (packet word +2). It then asks the source's animation
manager (`FUN_0047c08e`) for the spell's CastingAnim, `record + 0x186`
(line 101932: `(**(code **)(*piVar8 + 8))(*(undefined1 *)(iVar5 + 0x186),1,0,0x3f800000,1)`).
That is the same call `OP_BeginCast` makes (`include/client/graphics/spell_cast_animation.h`,
B1157).

The handler first returns, before the source lookup and before the Target
stage, unless all three of these hold (lines 101911-101922):

1. `FUN_0044d0d0(id)` is true: the spell id is in 1..9999. See 0x47f4c2..0x47f4ca;
   the function at 0x44d0d0 compares the id with 1 and with 0x2710.
2. `FUN_0040cbd6(id)` is not 0: the client has a record for the id. See
   0x47f4d7..0x47f4e0.
3. The record's byte `+0x180` is not 0. See 0x47f4e6..0x47f4ed. The
   spells_us.txt parser stores that byte at line 69217
   (`*(undefined1 *)(param_1 + 0x60) = uVar2`, with `param_1` an int pointer,
   so +0x180), beside +0x181..+0x185. It is field 98, the target type, which
   fable's db builder stores in its target-type column
   (`tools/willeq_db/db_loaders_text.cpp:165`, `safeInt(98)`). The oracle's `spells_us.txt`
   has 43 rows with target type 0, for example 2246 Call of Alarm
   (CastingAnim 43), 2558 Decession (29) and 3077 Storm Stomp (44): an
   `OP_Action` for any of them plays no swing. 8604's target type is 6.

It then asks the source only when all three of these hold:

1. The source's stand byte `+0x251` is 'd' (100, standing) or 'f' (102,
   frozen). See 0x47f519..0x47f525.
2. `FUN_0044ced9(spell) == 0`, called with ecx = the spell record at
   0x47f527. That means the record's `+0x230`, which is `param_1[0x8c]`, the
   field-123 spells.eff index, is not 61..64. The parser stores it at line
   69247 (`param_1[0x8c] = uVar3;`) and reads it back at line 69382.
3. The source's class byte `+0x252` is not 8 (Bard), or the packet's int at
   +6 (instrument_mod) is above 10. See 0x47f53c..0x47f54c. The compare is
   signed (0x47f548 `cmp dword ptr [eax + 6], 0xa` then `jle`), so a mod of
   0x80000000 or more is negative and a Bard sending it does not swing.

`+0x252` is the class byte. The decompile compares it with 7, 8, 9, 0x0f
and 0x3e alongside the level at `+0x24c` (for example lines 97322 and
97351).

A source in its cast state already plays that id, so the request is the
client's same-id no-op (B1157). The rule therefore only shows where no
BeginCast started the track: a `/disc`.

## 4. The landing row (ask 2)

The observer instrument is `refactor/B1162/scripts/obs_measure_b1162.py <leg> measure/disc8604 geom_b1174_disc8604.json 8604 --npc-id 260 --keep-png`
(`review/B1174/worker/measure/disc8604/b1174_disc8604_8604_1_npc.txt`).
Act = 1793128478.118 = f57743 at 30 fps (t0 1793126553.340, `video.calibration.tsv`).
The strip is `review/B1174/worker/strips/disc8604_observer.png`.

| row | onset (Act+) | end (Act+) | peak | run cov | cen_h | h p5..p95 | ground |
|---|---|---|---|---|---|---|---|
| **oracle 8604 /disc, this leg** (observer, caster box) | +0.422 (f57757) | +4.355 (f57875) | f57802 Act+1.922 cov 0.066 | 0.052 | 0.72 | 0.29..1.13 | 0.01 |
| B1130 record 325 | -- | -- | -- | -- | -- | -- | -- (no row: "325 ... not cast here", record_table.tsv line 5) |
| B1130 record-326 row 51 (GM `#castspell 4499 0`, WITH a BeginCast; same line) | Source +0.70 | lit to Act+8.0-8.3 | Source cov 0.231 | -- | 0.21 | -0.05..0.74 | feet ring |
| B1162 oracle 4499 /disc (`refactor/B1162/README.md:623`) | +0.454 | +1.120 | cov 0.067 | 0.054 | 0.70 | 0.28..1.09 | 0.02 |
| fable harness 8604 action-only BEFORE (B1162 `harness/disc8604_b1130self/`) | none | none | SOURCE max 48 px, LANDING max 112 px | -- | -- | -- | -- |
| **fable harness 8604 action-only AFTER** (d4a23b9b, `harness/disc8604_b1130self/measure_anchor/anchor_table.tsv`, rendered anchor 641,597 / 208 px) | +0.100 (f48) | +3.967 (f164) | f111 Act+2.200 cov 0.065 | 0.052 | 0.64 | 0.24..0.91 | 0.03 |

**Which pixels are lit.** The strip shows Testcaster raising the
orb-holding arm and turning, at f57770 (Act+0.86), f57802 (+1.92) and f57830
(+2.86). By f57875 (+4.36) it is back in the idle pose. There is no glow, no
particle and no ground ring in any frame. So the oracle's lit run is the
`t06` swing. Its length, 3.9 s from lit onset to lit end, is consistent with
the Dark Elf female's 4100 ms `t06` (`refactor/B1148/README.md:28`).

**Real /disc path vs GM path.**
- The GM path's Source (the hand glow and the feet ring of the record-326
  row) does NOT appear on a real `/disc`.
- The GM path's caster pose is the BeginCast track. On the `/disc` it is the
  `OP_Action` request.
- Neither path has a landing sprite for 325 or 326.

**8604 vs 4499.** 8604 swings `t06` for about 3.9 s. 4499's lit run is
0.67 s (CastingAnim 27 = `s01`).

**The fable row.**
- Before this card, fable drew nothing: no sprite, no swing.
- Now the harness draws no slot (`frames/slots.tsv` is empty). It draws no
  effect and no missile in any of its 300 frames (`frames/fx_activity.tsv`
has 300 rows).
- The caster plays `t06` over frames 45..167, Act+0.00..+4.07, then `p01`.
- The run cov matches the oracle's: 0.052 vs 0.052. Peak cov is 0.065 vs
  0.066.

**The onset: the observer rig's packet-to-screen delay.** The oracle's whole
lit run is shifted against the harness's, not only its onset: Act+0.422 to
+4.355 against +0.100 to +3.967. The lengths are the same, 3.93 s against
3.87 s. A threshold difference would move the onset, not the end as well.
The observer's screen shows the packet's own effects late:
- The observer receives only the .118 (and the .135 repeat) `OP_Action` and
  `OP_Damage` on port 55891 (`review/B1174/worker/pcap_disc_window.txt`
  lines 8-9 and 16-17).
- Its chat box prints 8604's cast-on-other line, "Testcaster's skin glows
  with dark energy." (`spells_us.txt` row 8604: "'s skin glows with dark
  energy."). The last chat line (x 418..815, y 752..770 of `video.mp4`)
  first changes at f57756 (924 px change by more than 40, and 0 px in every
  frame from f57736 to f57755 and from f57757 to f57770). That frame is
  Act+0.388 in the instrument's own clock
  (`measure/disc8604/b1174_disc8604_8604_1_npc/frames.tsv` line 49).
- The swing lights in the next frame, f57757, Act+0.422 (the same file,
  line 50).

So the +0.39..+0.42 s is the time the observer rig takes from the packet to
the screen, and the oracle's swing starts with the packet's other effects,
at the packet, as the code says. The script and the per-frame counts are
`review/B1174/worker/chat_line/chat_diff.py` and `chat_diff.txt`. The
height extent (h_p95 0.91 vs 1.13) is a camera difference (the observer rig
against the b1130-self preset); no FX verdict is drawn from it. The verdict
is the content: no sprite on either side, and the same track from the
packet on both.

## 5. The fable change (ask 3)

- `include/client/spell/spell_action_anim.h`, `EQ::ActionPlaysSourceCastAnim`:
  the six conditions above (the three early returns, then the three source
  conditions), as a pure function of `EQ::ActionCastAnimInputs`, with
  `instrument_mod` compared as a signed int.
- `src/client/spell/spell_packet_handlers.cpp`: `handleAction` puts field
  120, whether fable's spell table (built from the client's `spells_us.txt`)
  has the id, its field-98 target type, the source spawn's stand byte and
  class, and the packet's `instrument_mod` into `SpellCastVisualCompleteData`,
  the event the renderer side reads when the packet arrives.
- **The stand byte is an approximation, stated here.** The client keeps
  `+0x251` per spawn and writes it in many places (`eqgame.dis` has 143
  references to `+0x251`; for example 0x484de7 stores 0x69, looting, from
  the client's own code). fable does not port that state machine. It keeps
  `Entity::stand_state` (`include/client/eq_types.h`), default 100: the
  stand value the server last sent for the spawn in `OP_SpawnAppearance`
  AT_Anim (`EQ::SpawnStandStateAfterAppearance`, set in
  `src/client/entities/entities_component.cpp`), and for the local player
  the value of the pose fable itself sets
  (`MovementComponent::ApplyPositionState`, `src/client/movement/movement_state.cpp`).
  So a source the server puts in 105 (looting), 110, 111 or 115 is not
  asked. The renderer's `poseState` (Standing/Sitting/Crouching/Lying only)
  is no longer the input: a looting source counted as Standing there. A
  client-internal transition fable does not model is not in the byte.
- `src/client/bridge/irrlicht_bridge.cpp:1124`: on the cast-animation packet
  (effect_flag 0), `actionEntityCastAnim` runs BEFORE `completeEntityCast`.
  A source still casting therefore keeps B1157's end-of-cast rule.
- `src/client/graphics/entity_castbars.cpp:108`, `EntityRenderer::actionEntityCastAnim`:
  a non-casting live source for which the gate holds plays the track at speed 1.0, through
  `playCastAnim`, the same body the BeginCast path uses for its own track.
- `src/client/graphics/entity_updates.cpp:196`: once the one-shot track has
  played out (`actionCastAnimEnded`, `spell_cast_animation.h:93`), the
  per-frame walk returns the model to the idle. The oracle's caster is idle
  again at Act+4.36.
- `tools/model_viewer/model_viewer_fx_harness.cpp`: the same rule on the
  harness's action-only cast, with `harnessActionInputs` (the plan's spell
  id, its record, the record's target type, a standing source that is no
  Bard). Line 1315 returns to the idle. Line 1429 writes
  a per-frame `fx_activity.tsv` (live effects, missiles, caster track), so a
  test can tell "no sprite" from "the caster moved" without a whole-frame
  pixel diff.
- The two harness comments that said the client never animates a `/disc`
  are corrected.

**Tests.**
- `tests/test_cast_animation.cpp:88` `CastAnimation.OPActionAsksTheStandingSourceForTheCastingAnim`:
  the gate's truth table, including the spell id range (0, 1, 9999, 10000),
  no record, target type 0 (2246, 3077), stand bytes 100/102 against 0, 101,
  105, 110, 111, 115 and 120, and a Bard at instrument_mod 0x80000000,
  0xfffffff5 and 0xffffffff (negative, no swing).
- `:162` `CastAnimation.TheSpawnStandStateFollowsTheServersAppearance`: the stand
  byte after an AT_Anim value.
- `:420` `CastAnimationEntity.ALootingSourceOrATargetTypeZeroSpellStartsNothing`:
  a looting (105) or sitting (110) source starts no track, and 2246 Call of
  Alarm, read from the shipped db (target type 0, CastingAnim 43), starts
  none on a standing source.
- `:355` `CastAnimationEntity.ADiscActionSwingsTheIdleSourceThenReturnsToIdle`:
  8604's db CastingAnim 44 gives `t06` at the packet, `t06` mid-swing, then
  `o01` after the 4.1 s track. The animator is stepped with `OnAnimate`.
- `:393` `AnActionOnACastingOrBardSourceStartsNothing`: a casting source or a Bard at mod 10 starts no track.
- `tests/test_fx_harness.cpp:281` `FxHarness.ADisciplineWithoutBeginCastSwingsTheCasterAndDrawsNoSprite`
  (4499, `s01`) and `:292` `FxHarness.TheLeechcurseDisciplineSwingsT06AndDrawsNoSprite`
  (8604, `t06`) both go through `expectDiscActionSwing` (`:267`): no slot,
  0 effects and 0 missiles in every frame, `p01` before the packet, the track
  from the packet, then `p01`.
- These replace B1162's `ADisciplineWithoutBeginCastDrawsNothing` and
  `TheLeechcurseDisciplineWithoutBeginCastDrawsNothing`. Their whole-frame
  "no pixel moves" bound was wrong, because the oracle's caster DOES move.

## 6. Ledger clip (ask 3 / acceptance D)

The clip is `review/B1174/worker/ledger/clips/b1174_disc8604_leechcurse.mp4`,
with its `_strip.png`. It was made with `refactor/B1158/scripts/make_clip.py`;
the spec is `ledger/specs/b1174_disc8604_leechcurse.json`, the alignment is
in `ledger/clip.log` and `alignments.jsonl`, and the index (`make_index.py`)
is `ledger/index.{tsv,html}`.

The oracle is on the LEFT: the observer at f57743 (BC = Act), t0
1793126553.340. willeq is on the RIGHT: harness b1130-self f45 (BC = Act),
built from d4a23b9b. The window is BC-1.0..Act+8.0 s, 270 frames.

The caster is in frame on both sides in every strip cell, and both swing at
the packet. This clip replaces B1162's empty
`b1162_disc8604_leechcurse.mp4`.

## 7. Lane before == after

Everything below is in `review/B1174/worker/oracle/b1174_disc8604/`:
- **Char 67:** `char-state-before.tsv` = `char-state-after.tsv` =
  `67 12 1 6`. Memorised gems, book and buffs are `cmp`-equal.
- **Char 103:** state, gems, book and buffs before = after (`103 1 1 1`).
- **`driver.log` SAVED/LEG/BACK:**
  - `character_skills` 0/0/0
  - `character_disciplines` 0/1/0
  - `timers` 2/3/2: the 8604 reuse timer was added, then removed.
- **Snapshot:** "snapshot restored BYTE-IDENTICALLY" x2.
- **INI files:** the caster INI (`before-character.ini` vs the file at stop)
  and `eqclient` before/after are `cmp`-equal.
- **Server data:** restored byte-identically (sec. 1).
- **Lease:** `cleanup exit=0 lane=5 lock=state=FREE`, and the class-leg
  helper printed `char 67 restored: class 12, gems 93,38,54,41,253,458,656`.
- **The read-only query** left the stack down, as it had found it.

## 8. Gates

**Fix leg (review note 7961, MR !324), at a5930623:**
- The gate change leaves 8604 as it was: the client's 8604 has target type
  6, its id is in range, and the harness caster stands. The harness was
  re-rendered on the ThinkPad with the same plan and camera
  (`review/B1174/worker/harness/disc8604_b1130self_fix/`, `--camera
  b1130-self --seed 1 --fps 30 --seconds 10`). All 300 frame PNGs and
  `fx_activity.tsv` are `cmp`-equal to the d4a23b9b run, and
  `measure_anchor/anchor_table.tsv` is `cmp`-equal too (+0.100 f48 ..
  +3.967 f164, run cov 0.052).
- `test -R 'CastAnimation|FxHarness'` on gitlab-runner-2: 100% of 20
  passed, with the 9 `CastAnimationEntity` cases skipped there (no client
  tree). See `review/B1174/worker/ctest_targeted_fix.log`.
- `test_cast_animation` on the ThinkPad with
  `EQ_CLIENT_PATH=/mnt/projects/EverQuestP1999`: 11 passed, and
  `AModelWithoutTheTrackKeepsItsCurrentAnimation` skips by design. See
  `review/B1174/worker/test_cast_animation_thinkpad_fix.log`.
- Hygiene on gitlab-runner-2: 100% of 48 passed
  (`review/B1174/worker/hygiene_fix.log`).

**First leg, at 60257a64:**
- `test -R 'CastAnimation|FxHarness'` on gitlab-runner-1: 18/18 passed
  (`review/B1174/worker/ctest_targeted.log`).
- `test_cast_animation` on the ThinkPad: every case passed but the
  by-design skip (`review/B1174/worker/test_cast_animation_thinkpad.log`).
- Hygiene on gitlab-runner-1: 100% of 48 passed
  (`review/B1174/worker/hygiene.log`). The first run failed `ZeroCallers`
  with a stale `EXEMPT_TESTS` row: `SpellVisualFX::missileCount` now has the
  harness as a caller, so the row was retired in `scripts/refactor/check_zero_callers.py`.

`scripts/refactor/cite_check.py` over this README: `checked 32 cites,
0 problems, 0 address warnings`. The `eqgame.exe.c` lines in sec. 3, which
the checker does not parse, were each read by hand.

## FOLLOW-UP

- **4499's swing length.** The harness's `s01` plays 3.0 s (fx_activity
  frames 15..104). B1162's oracle 4499 lit run is 0.67 s. The lit run is a
  thresholded view of the motion, not the track length, and no 4499 clip of
  the oracle's s01 end pose was measured here. A like-for-like check of
  `s01` on the oracle is open.
- **A casting source's `OP_Action`.** The client re-requests the same
  CastingAnim, a no-op. fable keeps B1157's rule there (end the cast pose,
  take the idle). This card does not re-measure that ordering.
