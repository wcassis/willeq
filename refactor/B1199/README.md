# B1199 (#518): remove the 102 write on the 0x1e51 stun feed

**Issue is the specification (#518).** B1183 (#502) measured the stunned row
on lane 5: **fable 102 vs oracle 100** on GM #stun (refactor/B1183/README.md
FINDING 2, the poller log at `refactor/B1183/legs/legF/poll/poll.log`). The
client's 0x1e51 case does NOT write the stand byte, so the fable-side 102
write is the defect. This card removes it and re-measures the stunned row
both sides.

Every line below is `review/B1141/ghidra/out/eqgame.exe.c` (the B1141 Ghidra
decompile, the same binary B1183 cited).

## 1. The client's stun cases (the specification)

| case | eqgame.exe.c | +0x251 write |
|---|---|---|
| 0x1e51 (OP_Stun — the stun the server applies; the GM #stun) | 79576-79581 (`if (DAT_00905d00 != 0) FUN_00417839();`) | **none** — the byte holds whatever pose it had |
| 0x25aa | 80373-80384 | **102** (0x66, frozen) at 80377 |
| 0x3013 | 80455-80465 | **102** (0x66, frozen) at 80459 |
| 0x210b (stun end) | 80326-80340 | **100** at 80336 (+ the stun-flag clear at 80337) |

Fable processes the 0x1e51 and 0x210b feeds (its OP_Stun opcode and the
stun-end path in `ZoneProcessStun`); it does not process 0x25aa or 0x3013,
but the machine keeps those two feeds so its vocabulary is the client's,
byte for byte.

## 2. The change

- `include/client/state/stand_state.h`: the stun feed is now ONE entry
  point, `StandState::onStunFeed(StunFeed)` — `OpStun0x1e51` is the
  documented no-op (the client's 79576-79581 writes no +0x251),
  `Frozen0x25aa` / `Frozen0x3013` are the client's 102 direct writes
  (80377 / 80459), `StunEnd0x210b` is the 100 direct write (80336).
- `src/client/commands/combat_actions.cpp` (`ZoneProcessStun`): the stun
  feed no longer writes 102 — `onStunFeed(StunFeed::OpStun0x1e51)` is a
  no-op; the stun end goes through `onStunFeed(StunFeed::StunEnd0x210b)`.
  The 102 write the B1183 port had here is removed.
- `tests/test_stand_state.cpp`: the three card tests (0x1e51 → unchanged,
  0x25aa → 102, 0x3013 → 102) + the stun-end pin; the sequence table's
  stunned row is now 100.

## 3. Unit tests (A)

`tests/test_stand_state.cpp` (device-free, header-only machine):

- `TheOpStunFeed0x1e51WritesNoStandByte` — 0x1e51 feed → +0x251 unchanged:
  stays 100 from standing, and holds 110 / 111 from sit / crouch (the
  client writes nothing, so the pose holds whatever it was).
- `TheFrozenFeed0x25aaWrites102` — 0x25aa feed → 102 (direct, from any pose).
- `TheFrozenFeed0x3013Writes102` — 0x3013 feed → 102.
- `TheStunEndFeed0x210bWrites100` — stun end → 100 (from 102 included).

Plus the existing 14 StandState tests and the B1174 / B1162 device-free
gates, unchanged. rbuild: `ctest -R 'StandState|CastAnimation'` = 18/18
green (the ctest names are the gtest names via GTEST_DISCOVER_TESTS);
hygiene exit 0.

## 4. Lane-5 stunned row, both sides (B)

Two runs on lane 5 (char 67 Testcaster, GM `#stun 8000`), both on 2026-10-30
UTC. **`legs/leg1` is run #1** (09:19: the steps and the wire ran clean —
`legs/leg1/12_stun_start.opcodes.tsv` shows the `#stun 8000` C->S and the
S->C OP_Stun 0x1e51 x2 — but its poller launch ssh self-killed: the remote
`pkill -f '[b]1199_poll.sh'` matched its own command line, which also
contained the unbracketed poller path, so no poll log exists for leg1).
**`legs/leg2` is run #2** (09:55): the poller (the B1183 poller
`refactor/B1183/scripts/b1183_poll.py` driven by
`refactor/B1199/scripts/b1199_poll.sh`, armed at the leg's `gdb_arm`
still) ran, and its log is committed at
`refactor/B1199/legs/leg2/poll/poll.log`.

**Oracle column** (leg2; poll epoch 1793354475.986 = 10:01:15.986 UTC, the
poller attaching right after the leg's `gdb_arm` still; poll interval 25 ms):

| poll event | poll t | wall time | leg2 step (timeline.tsv) | byte |
|---|---|---|---|---|
| INIT | t=0 | 10:01:15.986 | 10_gdb_dump | 100 |
| SNAP | t=15.158 | 10:01:31.144 | 12_stun_start — 0.65 s after the 0x1e51 x2 (10:01:30.487/503 in `12_stun_start.opcodes.tsv`) | 100 |
| SNAP | t=20.214 | 10:01:36.200 | 13_stunned (1793354493.163–497.696) — inside the 8 s stun | 100 |
| SNAP | t=25.269 | 10:01:41.255 | 14_stun_end | 100 |
| SNAP | t=30.323 | 10:01:46.309 | 14_stun_end | 100 |
| SNAP | t=35.376 | 10:01:51.362 | 15_post_stun | 100 |
| LIST/XNEW | t=38.205 | 10:01:54.191 | 16_quit — the client's own /q destroys the target spawn; the log's final `ABORT 1002 consecutive read failures` is that expected teardown, not a lost target mid-stun | 100 |

**No WR line anywhere in the log**: the byte never left 100 at any 25 ms
sample from zone-in through the whole stun window and past it. A 102 write
would have persisted for the 8 s stun and be caught by the next sample.

**Fable column** (`tests/test_stand_state.cpp` `TheLane5FullSequenceTable`
output at this tip, the machine driven through its real feed points;
`refactor/B1199/tables/fable_step_table.tsv`): `stunned` = **100**,
`stun end` = 100.

**100 vs 100: MET.** No residual. `refactor/B1199/tables/step_table.tsv`
is the two-side table; B1183's pre-fix table (fable 102) stands as the
historical record of that MR's measurement.

## 5. Commands

```bash
# unit + gates (rbuild on the ThinkPad, B1087)
rbuild kb-b1199-stun-feed configure
rbuild kb-b1199-stun-feed --target test_stand_state
rbuild kb-b1199-stun-feed test -R 'StandState|CastAnimation'
rbuild kb-b1199-stun-feed hygiene
# fable column (the table the test prints)
ssh thinkpad /home/user/willeq-builds/kb-b1199-stun-feed/build/bin/test_stand_state \
    --gtest_filter='StandState.TheLane5FullSequenceTable'
# lane 5 (the oracle session takes its own lease; the poller is armed on the
# lane host BEFORE the leg so its 20-min window covers the gdb_arm still)
cd /home/user/src/willeq-wt/kb-b1199-stun-feed
export EQEMU_LANE=5; source scripts/eqemu/env.sh
# on the lane host (thinkpad):
bash /home/user/src/willeq-wt/kb-b1199-stun-feed/refactor/B1199/scripts/b1199_poll.sh \
     $HOME/kb-evidence/kb-b1199-stun-feed/legs/leg2 150 &
ORACLE_OWNER=kb-b1199-stun-feed-a \
  bash scripts/eqemu/oracle/oracle_class_leg.sh 5 12 \
     refactor/B1199/plans/leg1_stun.tsv \
     $HOME/kb-evidence/kb-b1199-stun-feed/legs/leg2 --no-wait
```

## 6. Scope

Changed: `include/client/state/stand_state.h`,
`src/client/commands/combat_actions.cpp`, `tests/test_stand_state.cpp`,
`.gitignore` (the B1199 poller-log negation), `refactor/B1199/` (this README,
the poller driver, the plan, the two tables, the committed run evidence
`legs/leg1` + `legs/leg2` — text only, no video/frames).
`refactor/B1183/` is untouched: its table records the PRE-FIX fable byte
(102, measured by the test at B1183's head); this MR supersedes that row
with the post-fix re-measurement in `refactor/B1199/tables/step_table.tsv`.
No goldens / manifests / baselines / ratchets / config / third_party /
packs touched.
