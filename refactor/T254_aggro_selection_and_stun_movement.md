# T254 — auto-defence engages the LATEST attacker; does a stun gate movement? (T247 leftovers)

Slot: slot2. x86. OWNS eqemu briefly (noclobber) for the two-beetle leg.
Class A + B. Territory: src/client/combat.cpp `CheckForAggro` /
`NoteAttackedBy` / `GetNearbyHostiles`, src/client/eq_movement.cpp stun
gate (if the answer is yes), tests. Do NOT touch eq_bridge.cpp hotbar arm
(T253 slot1), inventory (T252 slot3).

## 1. Aggro selection
`CheckForAggro` engages the FIRST aggro entry in scan order, not the most
recent attacker. Keep a timestamp in `NoteAttackedBy`; engage the latest
(ties → nearest). Test: two attackers, older one first in scan order →
latest engaged. Red/green. Live: two beetles `#attack` in sequence →
engages the second.

## 2. Stun vs movement (class B)
eqemu: does a stunned client's OP_ClientUpdate get rejected/ignored, and
does the official client freeze movement while stunned? Read
`Client::Handle_OP_ClientUpdate` / `Mob::IsStunned` usage in
/mnt/projects/akk-stack/code/zone. If the server ignores movement while
stunned, gate `UpdateMovement`'s position sends on `!IsStunned()` (so the
client doesn't desync) and test it; if the server accepts movement while
stunned, do NOT gate — record the finding and close the question.

## Verification
Full ctest; Review; evidence review/T254/. Commit:
`T254: auto-defence targets the latest attacker; stun-vs-movement <settled>`.

## Review (executor, 2026-08-26, slot2, off fable 6c5bdb5e)

Evidence: /mnt/projects/fable/review/T254/ (README.md indexes every file).

### 1. Aggro selection -- latest attacker, ties -> nearest

`CheckForAggro()` (src/client/combat.cpp) no longer takes the first
`is_aggro` entry in `m_potential_targets` (= entity-map id order). It now
walks every aggro entry inside `m_aggro_radius`, reads that entity's stamp
from `m_recent_attackers` (the timestamp `NoteAttackedBy` already keeps --
no new state was needed), and engages the one with the LATEST stamp; an
exact tie falls to the smaller `distance`. The engage line now says
`(latest of N attacker(s))`.

Red / green (measured, `red_cast.txt` / `green_cast.txt`):
- `Aggro_TwoAttackers_LatestIsEngagedNotFirstInScan`: beetle 403 (first
  in scan order, dist 2) hits us, its stamp is backdated 500 ms, then
  beetle 404 (dist 4) hits us. RED engaged 403 ("Which is: 403 / 404");
  GREEN engages 404.
- `Aggro_TwoAttackersSameInstant_NearestIsEngaged`: both stamped at the
  same instant, 404 at dist 1 vs 403 at dist 2. RED engaged 403; GREEN
  engages 404 -- proves the tie-break is distance, not scan order.
- The four T247 aggro tests pass on both sides (controls).
  test_eq_action_handler_cast: 32/32 green.

### 2. Stun vs movement -- VERDICT: the server ACCEPTS movement while
stunned; do NOT gate. Closed.

Read in /mnt/projects/akk-stack/code/zone:
- `client_packet.cpp:4831` `Client::Handle_OP_ClientUpdate`: the only
  early returns are `IsAIControlled()` (4832), `dead` (4835) and the size
  check (4839). There is NO `IsStunned()` (or rooted/mezzed) test anywhere
  in the handler; the packet's position is stored unconditionally at
  `m_Position = glm::vec4(cx, cy, cz, new_heading)` (5022) and rebroadcast
  to nearby clients when `positionUpdated` (5036).
- `cheat_manager.cpp:277` `CheatManager::MovementCheck`: `bool
  is_immobile = m_target->GetRunspeed() == 0; // this covers stuns, roots,
  mez, and pseudorooted.` -- and `if (!using_gm_speed && !is_immobile)`
  then skips every warp/speed detection. `Mob::_GetRunSpeed()`
  (`mob.cpp:804`) returns 0 while `IsStunned()`. So a stunned client that
  moves is not only accepted, it is explicitly EXEMPT from the cheat
  checks the server would otherwise run on the movement.
- `spells.cpp:5769` `Mob::Stun` / `5801` `Client::Stun`: set the flag,
  start the timer, `SendAddPlayerState(PlayerState::Stunned)`, and send
  OP_Stun{duration}. Nothing is sent that changes the client's run speed
  and nothing rejects later OP_ClientUpdate packets; the server's only
  stun consequences for a CLIENT are the cast refusals
  (`client_packet.cpp:9471` item clicks, spells.cpp cast paths) and melee
  (`client_process.cpp:337`), which the client already mirrors (T247).
- The zone-side `IsStunned()` movement gates (`mob.cpp:748/804/892`
  speeds, `mob_ai.cpp:805/1030/1158`) are NPC-AI paths; they do not apply
  to a client's self-reported position.

Whether the OFFICIAL client freezes movement on OP_Stun is a client-side
cosmetic that the server neither requires nor checks; the plan's rule
("if the server accepts movement while stunned, do NOT gate") settles it.
`UpdateMovement` / `SendPositionUpdate` are untouched; `IsStunned()` keeps
its single consumer (spell_cast_checks.cpp). Gating would have produced a
client that believes it is standing still while the server (and every
other client) has it wherever the last update put it -- the desync the
plan was worried about, in the other direction.

### Live (Testchar, warrior 2, befallen, DEBUG, surfaceless EGL, `--eqe1-path`)

`review/T254/live_two_beetles.log` (exit 0, 18/18 commands applied). Tour:
`#depopzone` + `#repop`, `#invul on`, two `#npctypespawn 2024` (beetle000 =
ID 306, beetle001 = ID 309, both at our feet), combat OFF; `/target
beetle000` + `#attack Testchar`; 2.5 s later `#stun 60000` on 306 (so its
last swing stays fixed); `/target beetle001` + `#attack Testchar`; `/target
Testchar`; `/combat on`.

| time | line | meaning |
|---|---|---|
| 05:06:17.954 | a fire beetle whispers, 'Attacking yourself.' | first #attack (306 targeted) |
| 05:06:18.009 | Queued combat anim: source=309 / source=306 | BOTH beetles swing at us -- 309 assisted 306 (social aggro), see Deviations |
| 05:06:20.487 | You stunned a fire beetle (306) for 1 Minute. | 306's last swing is now frozen at 18.009 |
| 05:06:21.010, 24.051 | source=309 target=172 | 309 keeps swinging |
| 05:06:24.051 | Target set to Testchar (ID: 172) | target is SELF before the engage |
| 05:06:24.581 | CheckForAggro: a_fire_beetle001 (ID:309) attacked us at 0.0 (latest of 2 attacker(s)) -- engaging | the fix: 306 (6.6 s old, still in the 10 s memory, FIRST in scan order, same distance 0.0) is passed over for 309 |
| 05:06:24.581 | Target set to a_fire_beetle001 (ID: 309) / EnableAutoAttack called | auto-target + OP_AutoAttack(on) |
| 05:06:24.691 .. 05:06:42.297 | You hit a fire beetle for N | swinging at 309 |

The pre-T254 `CheckForAggro` would have taken 306 here: both entries are
`is_aggro` at distance 0.0 and 306 precedes 309 in `m_entities` order.

### Verification bar
| step | result |
|---|---|
| build (GLES2-config) | exit 0 (`build_full.txt`) |
| ctest | **2936/2937** -- `AudioManagerTest.PreloadCommonSounds` (documented ALSA) |
| build-gles2-x86.sh | exit 0 |
| visual gate | not run -- no renderer-adjacent change, goldens untouched |
| build-arm-noble.sh | exit 0 |
| check_file_sizes | 0 FAIL; `combat.cpp` WARN pre-existing (>1500, +12 here) |

### Deviations / findings
- No new state: the plan said "keep a timestamp in NoteAttackedBy" -- it
  already does (`m_recent_attackers`, T247); `CheckForAggro` now reads it.
- Live: the SECOND `#attack Testchar` was redundant -- beetle001 had
  already joined beetle000's attack at 18.009 (eqemu social assist; both
  fire beetles swung within the same tick). The "in sequence" ordering the
  plan asked for was therefore produced by stunning 306 after its first
  swing so its stamp could not refresh, while 309 kept swinging. The
  claim under test is unchanged: two attackers in memory, the one with
  the later stamp engaged, the older one first in scan order. Recorded
  rather than re-run: a re-run would show the same assist.
- `#stun` on an NPC target works (`NPC::Stun` -> `StopNavigation`, AI
  melee gated on `!IsStunned()`, mob_ai.cpp:805): 306 swung once and never
  again in the session.
- eqemu lock: T256 then T255 held it back-to-back (~50 min bounded wait,
  polled every 2 s and taken by noclobber the instant it was free); up.sh
  befallen, one client session, Testchar restored (heading had drifted
  190 -> 192; `db_restored.txt`), down.sh, lock deleted.
- `check_file_sizes.sh fable` lists inventory/eq_bridge/cast_refusal files
  this branch never touched: fable advanced (T253 merged) after this
  branch forked from 6c5bdb5e, so the diff is fable's, not ours. The two
  files this unit changed are `combat.cpp` (WARN, pre-existing) and the
  test (ok).
- Item 2 is closed with NO code change; nothing to test.
