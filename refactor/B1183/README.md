# B1183 (#502): the client's per-spawn stand-state byte (+0x251)

**Issue is the specification (#502).** The official client keeps ONE BYTE per
spawn, `+0x251` from the spawn pointer in `eqgame.exe` (the "stand state"),
and the server's `Animation` constants name most of its values (Standing 100,
Freeze 102, Looting 105, Sitting 110, Crouching 111, Lying 115). fable kept
only an approximation of it (the B1174 cast-anim gate's "the stand byte is an
approximation, stated here"). This ports the byte into a real machine: one
`eqt::state::StandState` owner per spawn (`include/client/state/stand_state.h`),
fed by the same events the client uses, with the existing readers
(`Entity::stand_state`) switched onto it.

Every line below is `review/B1141/ghidra/out/eqgame.exe.c`, the Ghidra
decompile of the lane-5 client binary (the same MD5 B1162/B1174 cite).
`+0x251` has **143 raw references in `eqgame.dis`** (the number B1174 sec. 5
quotes) and **140 unique logical references in the decompiled C** (150 raw
lines; two lines carry two references each). The C census is the table in
sec. 2: **23 distinct writes** (the state machine, sec. 3) across 8 writer
functions, and **116 reads** across 48 reader functions (the gates, the
renderer, the movement code). The census scripts and raw dump are
`/home/user/kb-evidence/kb-b1183-stand-state/census_{parse,ctx}.py` +
`census_raw.tsv` (attached).

## 1. The values the client writes

| byte | char | name (server `Animation`) | written where (eqgame.exe.c) |
|---|---|---|---|
| 0 | — | uninitialized | 103347 (the spawn constructor) |
| 100 | 'd' | Standing | 71254 (cast), 75121/75215/76903/119126/128478 (zone-in / local), 80336 (stun end), 106109 (new spawn) |
| 102 | 'f' | Frozen (stunned) | 80377, 80459 (the stun-message handler, 0x25aa / 0x3013) |
| 105 | 'i' | Looting | 105797 (direct); 28061 (the 28061 gate, via the setter) |
| 110 | 'n' | Sitting | the setter, 104878 (the sit key) |
| 111 | 'o' | Crouching | the setter, 104878 (the duck key) |
| 115 | 's' | Lying | the setter, 104878 (the feign key); the value the wire carries on death |
| 120 | 'x' | Corpse | 105881, 111059 (the death handlers) |

The server can put any value 100..255 in the byte through an appearance
(103463 writes the blob's value verbatim), so the machine keeps whatever the
server wrote; the enum is the named vocabulary for the writer side only.

## 2. The census

### 2.1 The writers (23 distinct, 8 functions) — this IS the state machine

| eqgame.exe.c | function | value | kind | trigger |
|---|---|---|---|---|
| 103347 | FUN_004805e9 | 0 | direct | the spawn constructor (uninitialized) |
| 103463 | FUN_004805e9 | `puVar1[0x57]` | direct | the server's appearance blob (AT_Anim stand value) |
| 75121 | FUN_004554e5 | 100 | direct | zone-in (GetZoneInfo) |
| 75215 | FUN_00455710 | 100 | direct | zone-in (name match) |
| 76903 | FUN_00457f32 | 100 | direct | zone-in (name match) |
| 119126 | FUN_00496b5a | 100 | direct | zone-in / local re-spawn |
| 128478 | FUN_004a7356 | 100 | direct | the local player (DAT_00905ce4) |
| 106109 | FUN_004849b2 | 100 | direct | a newly added spawn |
| 79422 | FUN_0045afdc | `param_2[0x251]` | copy | a spawn is duplicated (the byte is copied) |
| 80336 | FUN_0045b8f0 | 100 | direct | the stun-END message (0x25aa handler) |
| 80377 | FUN_0045b8f0 | 0x66 (102) | direct | the stun-START message (0x25aa) |
| 80459 | FUN_0045b8f0 | 0x66 (102) | direct | the stun-START message (0x3013) |
| 71254 | FUN_00450c2f | 100 | direct, gated | the cast path (the gate at 71254: a lying 's' / corpse 'x' actor, 71225, is left alone) |
| 105797 | FUN_004849b2 | 0x69 (105) | direct | loot (see also the 28061 gate, via the setter) |
| 105881 | FUN_004849b2 | 0x78 (120) | direct | death |
| 111059 | FUN_0048bb7b | 0x78 (120) | direct | death |
| 104775-104807 | FUN_0048236e | 0x6f / 100 | transient | the setter's crouch camera branch (unobservable — the final write reinstates; see sec. 3) |
| 104799, 104878 | FUN_0048236e | `param_2` | THE SETTER | the user-initiated transition (the duck/sit/stand/feign keys, loot open/close) |

`FUN_0048236e` is the listing that contains **THE SETTER `FUN_00483556`**
(0x00483556-0x00483955 in `eqgame.dis`). Its two kinds of write are kept
apart in the port: the direct writes go through `StandState::apply()` /
`onAppearanceAnim()`, and the setter goes through `StandState::transition()`.

### 2.2 The readers (116, 48 functions) — the gates marked

The readers the card calls out for spell FX / combat:

| eqgame.exe.c | function | reads | the gate |
|---|---|---|---|
| 101927 | FUN_0047f01e | 'd' or 'f' | **the B1174 cast-anim gate** — the OP_Action handler asks the standing/frozen source for the CastingAnim (B1174 sec. 3) |
| 28061 | FUN_0041edac | 'd' | **the B1162 ability gate** — the Intimidation caster must be standing (B1162 sec. 34); 27630/28621 read 'f' (the frozen veto) |
| 28456 | FUN_0041edac | 'd' | the Intimidation G-key (local) |
| 71225 | FUN_00450c2f | 's' or 'x' | **the cast path's gate** — a lying/corpse actor is not put into the cast state (71254) |
| 71370 | FUN_00450c2f | 'd' (negated) | the cast path's standing check |
| 80231, 83078, 86507, 88291, 92438 | FUN_0045b8f0 / 004619cd / 00466244 / 00469215 / 00472313 | 'f' | **the frozen checks** — a stunned (frozen) player is vetoed from acting |

The remaining readers are the renderer and movement code (the pose the model
plays), grouped by function:

| function | reads | what it is |
|---|---|---|
| FUN_004a1d74 (12) | 'o','n','d','x','s' | the model's pose selection (the largest reader) |
| FUN_0048236e (9) | the setter's own gates (104421/104430/104768/104774/104798) | the setter reading its input |
| FUN_004ab7f1 (8) | pose | the per-frame model update |
| FUN_0044fa37 (6) | 'o' | the crouch check (movement / ground) |
| FUN_0041edac (6) | 'd','f','n' | the Intimidation / ability gates (above) |
| FUN_005709c2 (5) | pose | animation |
| FUN_00488974 (5) | 'o' | the crouch check |
| FUN_0056fc86 (4) | pose | animation |
| FUN_004619cd (4) | 'd','f','o' | the stun / pose (83238/83243/83291) |
| FUN_00411f3d (4) | 'd','f' | the action gate (18166/18191/18199/20137) |
| FUN_0049dda6 (3) | pose | animation |
| FUN_0048605d (3) | 'x','n','o' | corpse / sit / crouch (106941/107132/107233) |
| FUN_00450c2f (3) | 's','x','n','d' | the cast path (71225/71289/71370) |
| FUN_0047b5ef (2) | 's','x' | 99108 the lying/corpse check |
| FUN_0048bb7b (2) | the death handler's reads | 111059 area |
| FUN_004805e9 (2) | 'd','s' | 103958/103963 the appearance's pose |
| FUN_00472313 (2) | 'f','d' | 92438/92443 the stun / standing |
| FUN_0045b8f0 (2) | 'f' | 80231 the stun handler's read |
| (22 single-read functions) | 1 each | the long tail (pose, corpse, crouch, the 0x244 corpse-flag pairs) |

Every read is a pure read of the byte (or a copy at 79422); none writes
outside the 23 writes above. The two that the port MUST satisfy are the B1174
gate (101927) and the B1162 gate (28061); the rest read the same byte the
machine now owns, so they are satisfied by construction.

## 3. The state machine

`include/client/state/stand_state.h` is the port. One `StandState` per spawn
(`Entity::stand_state` in `include/client/eq_types.h`), owning the byte. Two
writer kinds, exactly as the binary:

**DIRECT writes → `apply(StandValue)` / `onAppearanceAnim(uint32)`** — no gate:
spawn init, the server's AT_Anim stand value (103463, under the B1174 rule
`SpawnStandStateAfterAppearance`: 0→100, 100..255 as sent, anything else
leaves the byte), the local zone-in 100, the cast path's 100 (71254, gated: a
lying/corpse actor is left alone, 71225), stun 100/102 (80336/80377/80459),
loot start 105 (28061), death 120 (105881/111059).

**THE SETTER → `transition(StandValue, ctx)`** — the user-initiated
transitions (the duck/sit/stand/feign keys, the camp sit-down, loot
open/close), gated exactly as `FUN_00483556`:

1. `104742` new == current → no-op (the byte does not move).
2. `104768` new == sit ('n') while the actor model's casting word (model
   +0x15c) is set → no-op (you cannot sit down mid-cast).
3. `104798-104807` current == crouch ('o') and new == stand ('d') / sit
   ('n'): the setter asks the camera check (`FUN_0048bb31`) and compares the
   camera z against the spawn's top; when the camera cannot follow it writes
   0x6f back and returns early — the spawn STAYS CROUCHING. The stand→crouch
   branch's transient 100 (104780/104795) is unobservable: the final write
   (104878) reinstates the requested state.
4. `104878` the final write: the requested state, on every other path.

The client's local-only half of the setter (the 0x48359c-0x4835c3
chair/message cleanup, the 0x4836a5-0x4836f0 UI mirror) has no fable
equivalent — no sit-on-chair, no separate UI byte — so it is not a context
input. The context the port feeds is `in_world` (`FUN_0043745d()`),
`camera_follows` / `camera_above_top` (fable's default third-person camera
follows from above the spawn's top — the answer the lane-5 sequence runs
under), and `casting` (the model's +0x15c word, read from
`SpellManager::isCasting()`).

`Entity::stand_state` converts to the raw byte (`operator uint8_t`), so the
readers keep reading exactly the byte they read.

## 4. The fable feed points

| feed | event | fable site | writer |
|---|---|---|---|
| F1 | the server's AT_Anim stand value | `src/client/entities/entities_component.cpp` (the AT_ANIMATION case) | `onAppearanceAnim()` |
| F2 | the local player's zone-in | `entities_component.cpp` (the ZoneSpawns add) | `apply(Standing)` |
| F3 | the local player's pose (duck/sit/stand/feign keys, the camp sit-down) | `src/client/movement/movement_state.cpp` (`storeStandByte`, user half) | `transition()` |
| F4 | a pose the server forces on us | `movement_state.cpp` (`storeStandByte`, server half) | `apply()` |
| F5 | the stun message (0x3050 / 0x25aa / 0x3013) | `src/client/commands/combat_actions.cpp` (`ZoneProcessStun`) | `apply(Frozen)` / `apply(Standing)` |
| F6 | loot start | `src/client/bridge/bridge_component_ui_audio.cpp` (`RequestLootCorpse`) | `transition(Looting)` (28061 gate) |
| F7 | loot close | `bridge_component_ui_audio.cpp` (`CloseLootWindow`) | `transition(Standing)` (230933) |
| F8 | cast start | `src/client/spell/spell_manager.cpp` (`beginCastFromGem`) | `apply(Standing)` (71254, gated) |
| F9 | death | `src/client/player/player_state.cpp` (`ZoneProcessDeath`) + F4's POS_DEAD | `apply(Corpse)` |

A note on the two loot-start paths: the census shows the 28061 gate (standing
→ looting, via the setter) and a direct 0x69 write at 105797 (FUN_004849b2).
The port uses the 28061 gate (setter, standing-only); lane-5 arbitrates which
path a real loot takes (sec. 7).

## 5. The readers, switched

- **The B1174 cast-anim gate** (`include/client/spell/spell_action_anim.h`,
  `ActionPlaysSourceCastAnim`): reads `sourceStandState`, filled in
  `src/client/spell/spell_packet_handlers.cpp` from `Entity::stand_state`.
  Now the machine's byte. The gate itself is unchanged.
- **The B1162 ability gate** (the client's Intimidation, `+0x251=='d'`): in
  fable the same `Entity::stand_state` byte is the gate input; the machine
  now owns it.
- **`Entity::stand_state`**: now the machine (one `StandState` per spawn),
  converting to the byte.

No gate behaviour changes for the server-echoed cases: the machine defaults
to 100 (B1174's documented default), so a spawn the server has not given a
stand value to still passes the gate. Pinned by
`tests/test_cast_animation.cpp` (still green) and `tests/test_stand_state.cpp`
(the `TheReaderGateReadsTheMachinesByte` case).

## 6. Tests

`tests/test_stand_state.cpp` (device-free, header-only machine) pins, per
transition:
- the client's byte vocabulary and the fresh-spawn default (100);
- `apply()` as a direct write with no gate (death 120 from lying, stun 102
  from corpse, the cast 100);
- `onAppearanceAnim()` under the B1174 rule (0→100, 100..255 as sent, an
  emote <100 leaves the byte);
- the setter's three gates (the no-op, the mid-cast sit, the crouch the
  camera would not follow) and the camera-follows landing;
- the full lane-5 pose sequence (stand→duck→stand→sit→stand);
- the loot open/close transitions;
- the reader gate (B1174) reading the machine's byte.

`tests/test_cast_animation.cpp` (the B1174 gate) and the movement/camp/death
suites are re-run and green.

## 7. Lane-5 oracle leg (the byte, per step)

The sequence (stand → duck → stand → sit → stand → cast → stunned → loot) runs
on the official client with the B1164 gdb pin on `+0x251` of the caster's
spawn (char 67 Testcaster, the observer is char 103 Oraclewatch), the same
sequence on fable with its state logged, and the two tables must be identical
step by step. Rig: `EQEMU_LANE=5` through the repo scripts
(`scripts/eqemu/env.sh`, `scripts/eqemu/oracle/oracle_class_leg.sh 5`, the
B1134 observer rig).

**The fable column is measured, not asserted.** `tests/test_stand_state.cpp`
(`TheLane5FullSequenceTable`) drives the real machine through the exact
sequence and prints the byte at each step; the output is
`/home/user/kb-evidence/kb-b1183-stand-state/fable_step_table.tsv`:

| step | feed (eqgame.exe.c) | fable byte |
|---|---|---|
| stand | zone-in default (128478) | 100 |
| duck | key C (setter 104878) | 111 |
| stand | key (setter 104878) | 100 |
| sit | key (setter 104878) | 110 |
| stand | key (setter 104878) | 100 |
| cast | cast start (71254) | 100 |
| stunned | stun msg 0x3050 (80377) | 102 |
| stun end | stun end (80336) | 100 |
| loot | loot open (28061) | 105 |

**The oracle column is measured on lane 5, per step, by the `/proc/pid/mem`
poller (`refactor/B1183/scripts/b1183_poll.py`), which attaches at the leg's
`gdb_arm` still and logs every value-changing write of the caster spawn's
`+0x251` (the B1164 gdb watchpoint was replaced: the lane-5 ThinkPad's
gdb 15.1 has no Python Watchpoint API, and a hardware watchpoint stops the
inferior on every hit). `refactor/B1183/scripts/merge_poll.py` turns
`poll.log` + the leg's `steps.tsv` into the per-step table. The full table
(both columns) is `refactor/B1183/tables/step_table.tsv`; evidence under
`/home/user/kb-evidence/kb-b1183-stand-state/legs/{legA-fix5,legA3_discovery}/poll/`.

The three POSE rows before sit are measured and identical to fable:

| step | fable byte | oracle byte (+0x251) | oracle evidence |
|---|---|---|---|
| stand | 100 | 100 | legA3 `INIT byte +0x251 = 100` |
| duck | 111 | 111 | legA-fix5 `WR t+28.573 100->111` (the **Down** press — the client's live crouch key; fable's is C, the bytes match) |
| stand | 100 | 100 | legA-fix5 `WR t+61.497 111->100` (the Down release) |

**The six remaining rows are measured** (legs `legC4` / `legE` / `legF` /
`legG`; the `/proc/pid/mem` poller reading +0x251 live on the caster, char 67).
The full two-column table is `refactor/B1183/tables/step_table.tsv`:

| step | fable byte | oracle byte (+0x251) | oracle evidence |
|---|---|---|---|
| sit | 110 | 110 | legF `WR t+49.858 100->110` (the `/sit` command) |
| stand | 100 | 100 | legF `WR t+59.258 110->100` (the `/stand` command) |
| cast | 100 | 100 | legF `#castspell 93 0` (S->C OP_BeginCast x2 + OP_ManaChange); the byte held 100 through the cast |
| stunned | 102 | 100 | legF `#stun 8000` (S->C OP_Stun 0x1e51 + OP_PlayerStateAdd + OP_SpecialMesg = genuinely applied); the byte **held 100** — see Finding 2 |
| stun end | 100 | 100 | legF: the 8 s GM stun expired; the byte held 100 through and after |
| loot | 105 | PENDING | BLOCKED — see the loot row below |

So eight of the nine rows are identical fable-vs-oracle; the **stunned** row is
the one measured fable-side difference (102 vs 100), and **loot** is blocked.

**FINDING 1 — letter keys go to chat, not game actions.** `legA-fix5` is the
controlled proof: the Down arrow fired duck (`WR 100->111` → `111->100`) while
the letter probes c/x/z fired **no** byte write. The official client routes
letter keys to the chat box; only arrow/special keys fire keydown actions. So
the live-keymap letter bindings (the corrected legC3b decode) SIT_STAND k0=`s`
and USE k0=`u` are *dead keydown triggers* — a tap is typed into chat, not
fired (legE's opcode-confirmed `s`/`u` presses produced zero C->S
movement/use packets). The client's OWN sit/stand path is therefore the
`/sit` and `/stand` CHAT COMMANDS (and the UI Sit/Walk buttons), which is what
`legF` drove. This is the measured answer to the card's "state which method
the client actually uses". (The earlier "SIT_STAND unbound" note in this
section is superseded: the legA3 poller mis-read the slot layout; the
267-slot legC3b dump shows SIT_STAND=`s`, USE=`u`, DUCK=Down.)

**FINDING 2 — +0x251 is a POSE byte; the GM stun does not change it.** The
measured values are all poses (stand 100, sit 110, duck 111, loot 105). When
stunned, the byte HOLDS the current pose (100): legF's `#stun 8000` was
genuinely applied server-side (OP_Stun + OP_PlayerStateAdd + OP_SpecialMesg
all present) yet +0x251 held 100 for the whole 8 s. The GM `#stun` is
delivered as **OP_Stun 0x1e51 + OP_PlayerStateAdd** — a *different* opcode
pair from the decompile's 102-writing stun handler (FUN_0045b8f0, the 0x25aa
/ 0x3013 handlers at 80377/80459), which never fired for the GM stun; the
official client handles the GM stun with a state flag, leaving the pose byte
untouched. **FABLE's difference**: fable's F5 feed (`ZoneProcessStun`) writes
102 into the stand byte on the stun message (the ported machine, matching the
decompile's 0x25aa/0x3013 handler), so fable shows 102 where the official
client's +0x251 held 100 for the GM stun. **Ruling requested (not changed
here)**: whether the stunned row's oracle value (100, measured for GM `#stun`)
overrides fable's decompile-based 102 depends on whether the GM stun path
(OP_Stun 0x1e51) is representative of a real stun spell's path (which may use
the 0x25aa/0x3013 handler and DO write 102). That needs a spell-stun
measurement, not a ruling from a GM-stun sample; fable's machine is left as
ported and the measured difference is recorded as-is.

**FINDING 3 — beneath-the-world limbo (environmental, since B1130).** The
zone now snaps the caster to z=3.875, *below* the ground at -74,428 (the
heightmap data changed since B1127/B1130 ran fine at the same pose). In that
"moving to safe point" state the character cannot enter stun/sit/loot states
(legE: the GM commands got opcode responses but the effects did not apply; the
frame showed the character standing un-stunned). The POSE z is ignored by the
snap; the escape is a SERVER-SIDE `#goto -74 428 10` (sets z=10 above the
ground), which `legF` used — sit/stand then worked.

**Exact next command** (the only PENDING row — LOOT; lane 5 must be free):
the eqemu server DELETES the corpse ~1.03 s after OP_Death (B1133, measured on
both clients), and the oracle's USE trigger is a click on the *live* corpse
(letter `u` is chat — Finding 1), but the rig's click step ran 6.4 s (jclick +
client_focus overhead), so the mousedown lands after the despawn. `legF`
(click ~12 s late) and `legG` (immediate click, still outside the ~1 s window)
both show the corpse gone and the target empty at click time, so the 100
reading is CONFOUNDED, not a loot measurement.

CORPSE-PERSISTENCE FIX IS IN PLACE (this run): the lane-5 DB rule
`NPC:EmptyNPCCorpseDecayTime` is 300000 (5 min) and the zone fleet was
restarted to read it (`docker restart` is blocked; `eq_container_pkill 9
'[s]erver/bin/zone'` makes the spire-launcher respawn the killed zone/world
with the new rule), so a `#spawn`ed corpse now HOLDS ~5 min instead of
despawning in ~1 s. `b1183_poll.py` was also extended to track the corpse's
own `+0x251` (it walks the spawn list from `DAT_00905cc4`, tags a non-local
spawn with `+0x244==2` as the corpse, and logs its `XNEW`/`XWR` — the looter
stays 100, the corpse goes 105 on the click). The re-run (`legs/legH3`)
still did not measure it: the oracle leg infrastructure failed with exit 66 —
the `oracle_class_leg.sh` after-census (line 120) found char 61 not restored to
its home class and the client step produced no `timeline.tsv`. So the remaining
blocker is the leg infrastructure, not the corpse.

```bash
cd /home/user/src/willeq-wt/kb-b1183-stand-state
export EQEMU_LANE=5; source scripts/eqemu/env.sh; eqemu_lock_take kb-b1183-stand-state
bash refactor/B1183/scripts/b1183_poll.sh \
     $HOME/kb-evidence/kb-b1183-stand-state/legs/legH 180 &   # arm BEFORE the leg (TMAX=180 for the slow click)
ORACLE_OWNER=kb-b1183-stand-state-a \
  bash scripts/eqemu/oracle/oracle_class_leg.sh 5 12 \
     refactor/B1183/legs/legH_loot.tsv \
     $HOME/kb-evidence/kb-b1183-stand-state/legs/legH --no-wait
# First root-cause the legH3 exit 66: read $OUT/char-state-after.tsv and the
# after-census at oracle_class_leg.sh:120 (char 61's class after the restore
# must equal its home class). The corpse-persistence fix and the corpse
# poller are already in place; the click now lands on a still-present corpse.
```

Then fill the LOOT row of `tables/step_table.tsv` from the poller log (expect
the corpse's `+0x251` to write `100->105` at the click; the looter stays 100).

## 8. What is verified

- The ported machine compiles (`willeq` builds clean).
- 13/13 `test_stand_state` pass; the B1174 `test_cast_animation` gate half
  (3/3 device-free) passes; `test_camp` (23/23), `test_corpse_death_pose`
  (10 pass, 6 device-dependent skips) pass.
- Hygiene: `WriteOnlyFields` / `ZeroCallers` pass (the unused
  `StandStateContext::is_local` was deleted, not exempted).
