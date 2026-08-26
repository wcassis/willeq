# T247 — instantaneous OP_Stun is dropped; CombatTarget::is_aggro is hard-coded false

Slot: slot1. x86. OWNS eqemu briefly (noclobber) for the live leg. Class A
(T245 + T246 findings). Territory: src/client/eq.cpp `OP_Stun` handler
(~1450), src/client/combat.cpp `is_aggro` / `CheckForAggro`, the
`OP_Action` / combat-message handlers that reveal an attacker
(eq_entities.cpp Damage/Action on SELF — read-only unless a one-line flag),
SpellManager's stun sub-check (spell_cast_checks.cpp — consume the flag),
tests. Do NOT touch audio/* (T248), inventory/* (T249).

## 1. OP_Stun
`eq.cpp:~1450` logs OP_Stun and stores nothing, so a duration stun (not a
buff) never blocks casting/movement locally. eqemu `Stun_Struct{duration}`
(verify in common/eq_packet_structs.h). Store `m_stunned_until` (steady
clock + duration ms), clear on expiry and on `OP_Stun{0}` if the server
sends it; `checkPlayerState` reads it (T246 left the hook). Test: stun
packet → cast refused 175 → after duration → allowed. Red/green.

## 2. is_aggro
`CombatTarget::is_aggro` is a hard-coded `false` TODO, so `CheckForAggro`
never fires — no auto-defence. Derive it from what the client already
receives: an `OP_Action`/`OP_Damage` with `target == our spawn id` from an
NPC marks that NPC aggro for N seconds (eqemu's hate is not sent; the
official client infers the same way). Wire the flag; `CheckForAggro`
engages only when `/combat on` (keep the existing gate). Test: damage
packet from spawn X on self → X is_aggro → `CheckForAggro` selects X.
Red/green.

## Live
befallen (first `#depopzone` + `#repop` to clear the leftover pups — T245
note), `/combat on`, let a pup hit Testchar → auto-target + swings without
`/attack`; `#stun 5000`-style GM command if one exists → `/cast` refused
"You are stunned" for 5 s.

## Verification
Full ctest; Review; evidence review/T247/. Commit:
`T247: stuns are tracked; attackers are aggro -- auto-defence works`.

## Review (executor, 2026-08-26, slot1, off fable f461aaf3)

Evidence: /mnt/projects/fable/review/T247/ (README.md indexes every file).
eqemu owned for the two live legs only (noclobber take after T249 released
it; deleted after down.sh; Testchar + Testcaster restored, db_restored.txt).

### 1. OP_Stun -- the Stun_Struct truth

eqemu `common/eq_packet_structs.h:2665`: `struct Stun_Struct { uint32
duration; } // 4 bytes total`. `Client::Stun(int duration)` fills it with
the MILLISECOND duration it also hands `stunned_timer.Start()`;
`Client::UnStun()` sends the same packet with 0. There is NO target field
-- the packet only ever goes to the stunned client. Two more facts that
shape the fix:
- `Mob::Stun` refuses to let a shorter stun cut a longer one short
  (`stunned_timer.GetRemainingTime() > duration` -> return). Mirrored.
- A timed stun EXPIRES SILENTLY: `client_process.cpp:216` calls
  `Mob::UnStun()`, not `Client::UnStun()`, so no OP_Stun{0} is sent at
  expiry -- confirmed live (0 `OP_Stun: unstunned` lines in a session with a
  5 s stun). Only `#stun 0` / explicit `Client::UnStun()` sends the 0. So
  the client MUST expire by its own clock; the plan's "clear on OP_Stun{0}
  if the server sends it" is the rare path, not the normal one.

The pre-T247 handler (`eq.cpp` ~1455) read `GetUInt16(2)` as a "target id"
and `GetUInt16(4)` as "duration in tenths" -- i.e. the low and high halves
of the uint32 duration. For `#stun 5000` it saw entity 5000 stunned for
0.0 s, matched nothing, stored nothing.

Fix: `EverQuest::ZoneProcessStun(uint32_t duration_ms)` (new, in
`eq_combat_actions.cpp` -- eq.cpp is already over the size line) stores
`m_stunned_until = max(current, now + ms)`; 0 clears; `IsStunned()` =
`now < m_stunned_until`. Chat: string 12479 "You are stunned!" on entry,
12480 "You are no longer stunned." on an explicit 0 (both added to the
`eq_strings.cpp` fallback table; the live run shows the DB string).
`SpellManager::checkPlayerState()` (spell_cast_checks.cpp, T246's hook)
checks `m_eq->IsStunned()` FIRST, before the buff scan -> `Stunned` (175).
Only casting consults it; the plan's "movement" is NOT gated (see
Deviations -- what the server does with a stunned client's position
updates was not verified in this unit).

### 2. is_aggro -- how aggro is inferred

eqemu never sends its hate list to a client. What the client DOES receive
is every attack the NPC makes on it: `OP_Damage` (CombatDamage_Struct)
for hits, misses (damage 0) AND invulnerable hits (DMG_INVULNERABLE = -5,
the `#invul on` case -- verified in `attack.cpp` NPC::Attack, which still
calls `other->Damage()` under invul), and `OP_Action` for a spell cast at
us. That is the inference, same as the official client's:

- `CombatManager::NoteAttackedBy(id)` (new) stamps `m_recent_attackers[id]
  = now` and prunes entries older than `kAggroMemoryMs` (10 s).
  `IsRecentAttacker(id)` = stamped within 10 s.
- `EverQuest::ZoneProcessDamage`: `target == m_my_spawn_id && source !=
  m_my_spawn_id` and the source entity has `npc_type == 1` ->
  NoteAttackedBy(source). Our own swings (source == us) and player/corpse
  sources are ignored.
- `EverQuest::ZoneProcessAction`: same target/source rule, plus the spell
  must resolve in the spell DB and be detrimental (an NPC buffing us is not
  an attack).
- `GetNearbyHostiles()` sets `CombatTarget::is_aggro = IsRecentAttacker(id)`
  (was the hard-coded `false` TODO); `ScanForTargets()` refreshes the flag
  on entries it already holds. `CheckForAggro()` is unchanged in shape
  (idle state only, inside `Update()`, which is a no-op unless `/combat on`
  -- the existing gate kept), now logs the engage at INFO and only enables
  auto-attack when `SetTarget` succeeded.

Latency: attack -> engage is one target scan (1 s) + one tick (500 ms),
measured 0.97 s live.

### Red / green (measured)
- RED (`red_cast.txt`, built against the pre-T247 sources with the three
  new-API tests removed): `CastSpell_AfterOpStun_SaysStunnedUntilItExpires`,
  `Aggro_DamageFromNpcOnSelf_CheckForAggroEngagesIt`,
  `Aggro_MissAndInvulnerableHitCount_OwnSwingsDoNot` FAILED (the cast went
  to the wire / nothing engaged); `Aggro_NoAttack_CheckForAggroEngagesNothing`
  passed, as a control must on both sides.
- GREEN (`green_cast.txt`): 28/28 -- the 7 new tests pin: OP_Stun -> cast
  refused with 175 + nothing sent, then allowed after expiry (backdated,
  no sleep); OP_Stun{0} clears; a short packet is ignored, 5000 = 5000 ms,
  a shorter stun does not shorten a longer one; NPC damage on self ->
  `Update()` targets it, state ENGAGED, OP_AutoAttack(on) on the wire; no
  attack -> nothing engages; a miss and a -5 count, our own hit does not;
  an attack older than 10 s does not engage.

### Live (both legs, DEBUG, surfaceless EGL, `--eqe1-path`)
- Auto-defence (`live_aggro.log`, Testchar, befallen, `#depopzone`+`#repop`
  first): `#attack Testchar` makes the spawned beetle attack us; the script
  then re-targets SELF; 0.97 s after the first swing `CheckForAggro:
  a_fire_beetle000 (ID:696) attacked us at 0.0 -- engaging`, target set to
  the beetle, OP_AutoAttack sent, `You hit a fire beetle for N` every ~2 s
  -- with no `/attack` in the tour. 13/13 commands, exit 0. A first attempt
  (`live_aggro_run1_*`) engaged before the script's 1 s-delayed retarget,
  so the retarget then broke the swings; run 2 (0.2 s) is the deliverable.
- Stun (`live_stun.log`, Testcaster, wizard 1, Frost Bolt in gem 1):
  `#stun 5000` -> `OP_Stun: stunned for 5000 ms`, `/cast 1` at +0.4 s and
  +3.0 s refused "You can't cast spells while stunned!" with no CastSpell
  on the wire; `/cast 1` at +8.0 s -> `Sent CastSpell packet` and the
  server's `BeginCast: spell='Frost Bolt'`. 14/14 commands, exit 0.

### Verification bar
| step | result |
|---|---|
| build (GLES2-config) | exit 0 (`build_full.txt`) |
| ctest | **2920/2921** -- `AudioManagerTest.PreloadCommonSounds` (documented ALSA) |
| build-gles2-x86.sh | exit 0 |
| visual gate | not run -- no renderer-adjacent change, goldens untouched |
| build-arm-noble.sh | exit 0 |
| check_file_sizes | 1 FAIL = `eq.cpp` 3923 lines, PRE-EXISTING (3934 on fable, -11 here); WARNs eq.h / combat.cpp pre-existing |

### Deviations / findings
- New code went into `eq_combat_actions.cpp` (ZoneProcessStun/IsStunned)
  rather than eq.cpp -- size discipline; eq.cpp only dispatches.
- `eq_strings.cpp` +2 fallback rows (12479/12480) -- outside the listed
  territory, one-line each, needed so the stun message is never
  `[Unknown message #N]` without the string DB.
- Movement is NOT gated by the stun (plan item 1's wording): the test the
  plan specifies is the cast refusal, and no test or live check of
  stunned movement was made. `IsStunned()` is there for a follow-up to
  consult; whether eqemu rejects a stunned client's position updates was
  NOT checked -- do not assume either way.
- `CheckForAggro` picks the FIRST aggro entry in `m_potential_targets`
  order, not the most recent attacker -- pre-existing shape, fine for one
  attacker; with several it engages whichever the scan listed first.
- The `#attack` GM command needs account status 150 (GMLeadAdmin);
  willeq_test is 200. Noted for future tours.
- No `/cleartarget` slash command exists; the tour used `/target <self>`
  to move the target off the NPC before its swing landed.
