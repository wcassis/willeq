# B1022 oracle checkpoint — blocked before a swing-rate mapping

## Decisive source finding (2026-10-19, run 1302) — the OP_Animation speed byte is a red herring

The deployed lane-2 server (`eqemulator/eqemu-server`) **does** emit melee
`OP_Animation` — already captured in its own zone logs, so no inference was
needed:

- `qeynos2_version_0_inst_id_0_port_7000_192.log` (10-14): 6 S->C `OP_Animation`, all body `06 00 0A 05`.
- `qeynos2_version_0_inst_id_0_port_7000_196.log` (10-14): 13 entries, body `06 00 0A 05` and `AF 00 0A 08`.

Wire layout (fable's own reader, `src/client/zone/zone_entry.cpp:808-821`):
`[2-3]=spawn_id`, `[4]=animation_speed (uint8)`, `[5]=animation_id (uint8)`.
So `06 00 | 0A | 05` = spawn 6, speed **10**, anim 5 (melee); `AF 00 | 0A | 08` =
spawn 175, speed **10**, anim 8. **The speed byte is 10 (0x0A) for every swing,
across both spawns and both anim ids** — constant, not per-weapon. This matches
the source (`attack.cpp` `DoAnim`: `a->speed = animation_speed ? animation_speed
: 10`; an ordinary swing passes 0, so 10). Conclusion for the card's
measurement question: **the official client's swing period cannot come from the
OP_Animation speed byte; it comes from the equipped weapon's item delay**
(server `attack_timer` = `100 x delay` ms), which reaches the client via the
equipment/appearance path. That is acceptance item (A)'s source answer.

## Why the fable actor produced no swing on the arena dummy (corrected cause)

Across heading seeds 0/90/180, position adjacent (no "too far away"), target
id=3 set and confirmed (`OP_MobHealth` HP=100), and `auto_attack` on in both
weapon windows (delay-45 axe then delay-20 axe), the deployed server emitted
**zero** `OP_Animation` and zero swing/hit/miss lines for the actor in the
**arena training dummy** — yet the same server emits melee `OP_Animation`
freely in qeynos2 against real NPCs (spawns 6 / 175). The blocker is the
**arena `a_training_dummy` (npc 77002) not resolving a server-side swing**,
exactly the orchestrator's pre-warning, NOT facing cone / position / protocol.
A GM-spawned/DB-seeded real NPC (`a_Lteth_Val_Gnoll`, npc 18113) was tried on
the next run but the zone instance loaded before the spawn row landed, so its
`/target` found nothing (`actor6/`). The actor's own `--record-session` is
the instrument that will give the swing period: timestamps between consecutive
`OP_Animation` for the same weapon = `100 x delay`.

(The earlier mid-run worry that "the deployed image differs from the source
read" is refuted by this: the deployed server's real `OP_Animation` speed=10
matches `/mnt/projects/akk-stack/code`. Zone-log timestamps are TZ-shifted —
log "04:11" is ~09:11 wall clock; the arena log was the correct one all along.)

The prescribed two-client oracle was run on eqemu/oracle lane 2 on 2026-10-19.
The official client observed as Testcaster; the headless fable client acted as
Testbuddy. Both weapon phases used the **same actor**. The captured video and
packet records are under
`/home/user/kb-evidence/kb-b1022-weapon-delay/oracle-d/` and
`/home/user/kb-evidence/kb-b1022-weapon-delay/actor4/`. Earlier unsuccessful
setups remain as `oracle-b`/`actor` (observer faced a wall; actor Z was 51) and
`oracle-c`/`actor2`/`actor3` (server reloaded a cached Z=51 after a late DB
correction). They are failed checks, not discarded green evidence.

## Controlled run

`oracle-plan-3.tsv` put Testcaster in arena at `(200,-963,3.13)`, set daytime,
spawned `a_training_dummy` (npc type 77002), then recorded the actor window at
10 fps. After the oracle wrapper's DB/inventory snapshot and **before the
actor's first login**, Testbuddy was seeded at `(200,-960,3.13)`, level 75.
The actor's zone-in log confirms the server profile's Z was 3.13
(`actor4/actor.log`, `[ZONE-IN] PlayerProfile position`). The earlier Z=51
out-of-range messages are absent. The actor targeted `a_training_dummy001`
and `/attack` was accepted in both phases.

The server's `items` table was read back, and its inventory at the phase
change showed both actual equipment rows:

| Phase | item ID | server item | delay | actor `/attack` timestamp | official video period | OP_Animation speed byte |
| --- | ---: | --- | ---: | --- | --- | --- |
| slow | 2492 | Crystal Staff of the Nexus | 45 | 05:14:21.292 UTC | **not measurable** | **absent** |
| fast | 5730 | Di`zok Sceptre of Authority | 20 | 05:14:58.387 UTC | **not measurable** | **absent** |

These are item delays, **not measured swing periods**. No frame-index
arithmetic can be reported honestly: no swing animation reached the official
observer to establish a pair of swing starts. The video is retained for frame
stepping when a real swing source is available.

## Packet result and cause

`actor4/session.sleq`, parsed with the repository's packed `session_log.h`
layout by `read_sleq.py` in the evidence root, contains **zero** OP_Animation
(`0x2acf`), OP_Action (`0x497c`), or OP_Damage (`0x5c78`) records in the
actor window. It records the two on/off pairs of OP_AutoAttack (`0x5e55`)
and OP_AutoAttack2 (`0x0701`) at recording offsets 13.066, 45.099, 50.162,
and 82.183 seconds. `oracle-d/05_actor_slow_then_fast.opcodes.tsv` has 318
client-to-server and 51 server-to-client packets, **zero** OP_Animation,
OP_Action, or OP_Damage, and 9 OP_WearChange packets. The source census
agrees: `CombatManager::EnableAutoAttack()` sends only the two auto-attack
toggles (`src/client/combat.cpp`), while `EntitiesComponent::SendAnimation()`
is called for an explicit action or skill (`src/client/action/` and
`src/client/skill/`), not the ordinary `/attack` path. Thus the card's
fable-actor capture does not expose the speed byte it asks us to map.

The wrapper stopped the world before restoring. `cmp` of `db-before.tsv`
against `db-after.tsv` and `inventory-before.tsv` against
`inventory-after.tsv` both exited 0; `ledger_check.txt` says
`BROKEN REFERENCES: 0`; the wrapper exited 0 with `lane=2 lock=state=FREE`.
The Testbuddy inventory rows were included in the wrapper's snapshot for
this run. No lane resource remains held.

## Verification at this checkpoint

`bash -n scripts/eqemu/oracle/oracle_session.sh`, `git diff --check`, and
`python3 scripts/refactor/check_doc_counts.py` exited 0. The first
`rbuild <worktree> hygiene` exited 8: `CrowdObservationControls` tried to copy
a concurrent `scripts/b975/__pycache__/*.pyc` that vanished before the copy
(44/45 tests passed, three skips). The unchanged-tree retry exited 0:
45/45 hygiene tests passed, with the same three skips. The failed run remains
a failure in this record. The full C++ suite and MR pipeline were **not run**:
the required oracle mapping and test cannot yet be written.

## Required decision before implementation

The issue requires the official client's measured swing period and packet
speed bytes before a mapping or golden test is written. The prescribed fable
headless actor never emits ordinary swing OP_Animation, even with the correct
ground-level position and an accepted target/auto-attack. An official-client
**actor** (with a second observing client) or a separately specified outbound
combat-animation source is needed to obtain that oracle. Neither source nor
mapping is guessed here. Consequently the red/green test, packet-to-renderer
fix, B494 census removal, decisions entry, and MR acceptance pipeline are
**not complete**.
