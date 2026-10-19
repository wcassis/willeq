# B1022 — the swing plays at the model's NATIVE animation length; the rate is the server's packet cadence

## The corrected oracle run (2026-10-19, run 1326) — the official client as ACTOR

`/home/user/kb-evidence/kb-b1022-weapon-delay/oracle-final2/` (lane 2,
`EQEMU_LANE=2`): the official client (Wine) is the ACTOR (Testchar, L60 GM),
a visible Human/Warrior L50 target `SwingMob` (10,000,000 HP, `#spawn`ed at
the actor's own position — no `#goto`, zero distance) is the dummy. Video +
the actor's loopback pcap + the per-step opcode TSVs.

**Why the earlier runs produced no swings** (all proven from their own
evidence): `#giveitem` requires a PLAYER target (giveitem.cpp:22 — the chat
said "You must target a player"); `/equip <id> <slot>` is not a client
command; and the race-10 undead model is invisible in the world ("You cannot
see your target"). `oracle-final/` had **zero** OP_Animation for the same
reasons. This run fixed the target (visible humanoid at own position); the
actor engaged (chat: "You try to slash SwingMob… INVULNERABLE!"; the GM
Entity Info and target frame both show SwingMob).

**The decisive census — 50/50 packets, speed byte 10 (1.0× native):**

| spawn | anim ids seen | packets | speed byte |
| --- | --- | ---: | ---: |
| 11 (the actor) | 5 (melee) | 1 | 10 |
| 12 (SwingMob) | 8, 1, 7 | 49 | 10 |

`all-app-packets.tsv` (eqpcap.py, `--all --hex`), `anim-analysis.txt` in the
evidence root: **every one of the 50 OP_Animation bodies carries speed byte
10**, constant across both spawns and all four anim ids (5 = primary-weapon
melee, 8/1/7 = the target's swing/parry/reaction mix). `0x0A` = 1.0 per the
server's own convention (`attack.cpp` `DoAnim`: `a->speed =
animation_speed ? animation_speed : 10`), so **the per-swing animation plays
at the model's native length — 1.0× means no speed change**. A constant speed
byte cannot carry the weapon delay.

_Evidence note: the decoded packet TSV (`oracle-final2.app2-preserved.tsv`
in the evidence root — 1333 rows, all 50 OP_Animation with hex bodies) is
the surviving record of the capture; the raw `capture.pcap`, the per-step
TSVs, and the video were lost when a killed stray re-run wiped the
evidence directory (its teardown never ran). The census above and the
`anim-analysis.txt` numbers were computed from the surviving TSV before the
wipe._

**The sustained ~3.0 s cadence is the TARGET's swing, not the actor's.**
SwingMob's sustained swing (anim 8) arrived at min=3.001 / med=3.023 /
max=6.046 s (n=36); the actor's own attack resolved as a **miss** against the
invulnerable NPC (chat: "…INVULNERABLE!"), so the actor registered one
swing packet (anim 5 at 14:57:11.675) and the server did not sustain it. The
actor's sustained swing period is therefore **not measurable from this
capture's packets**, and neither weapon phase was completed (`/equip 1 13`
did not take; `#summonitem 6555` did land the "Sword of Truth" on the
cursor). The per-swing DURATION is instead established from the model itself
(headless, `tools/b1022_anim_probe`, ELM donor for all humanoid males — the
native lengths the 1.0× speed byte plays at):

| anim | native length | | anim | native length |
| --- | ---: | --- | --- | ---: |
| c06 1H slash | 1000 ms | | c05 throw | 1000 ms |
| c02 1H pierce | 800 ms | | c07 bash | 1800 ms |
| c01 kick | 900 ms | | c08 hand | 900 ms |
| c03 2H slash | 1300 ms | | c09 archery | 1100 ms |
| c04 2H blunt | 1200 ms | | c10/c11 kick/swim | 1200 ms |

`animationTimeMs = frameMs × frames` (s3d_loader_skinning.cpp:340). The
fable defect was stretching the swing to `weaponDelayMs × 0.5` (1.5 s at the
3000 ms default) regardless of the model's real length.

**Ruling.** The per-swing duration is the model's native animation length,
played at 1.0× (speed byte 10); the swing RATE is the server's attack timer
(100 × item delay ms), i.e. the spacing between consecutive OP_Animation
packets. So the fix DELETES the delay-based stretch: `weaponDelayMs` and
`setEntityWeaponDelay` (dead writer — B494 zero-caller census, whose row goes
with them) and the stretch sites in `entity_updates.cpp` /
`entity_combat_anim.cpp`. The swing animator runs at native 1.0×; the server
re-triggers it at its own cadence.

**Not verified (honest):** (A) the actor's SUSTAINED swing period in the
pcap and (A) the per-swing animation duration from stepped video — the
target's invulnerability made the actor's attacks resolve as misses, so the
actor's sustained cadence and its own swing frames are absent from this
capture; the two-weapon comparison (delay-45 vs delay-20 spacing) is
likewise not measurable, because the second equip did not take. What the
speed-byte census DOES prove is the per-swing speed is constant 10 — i.e. the
per-swing duration does NOT vary with anything the packet carries, which is
exactly the measurement the card's branching question asked for.

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

## Verification at the landed head

`tests/test_b1022_swing_native.cpp` (registered in
`tests/cmake/packets_skills.cmake`): the source half scans the three
production TUs for the stretch (RED on the parent — the census names
`entity_updates.cpp` — GREEN at the head); the behavioral half drives the
real animation-5 update path over an EDT_NULL device and asserts the
animator's playback speed is the native 1.0 (RED on the parent at
0.6667 = 1000 ms native / 1500 ms stretched, GREEN at the head). RED/GREEN
logs: `/home/user/kb-evidence/kb-b1022-weapon-delay/red-green.md`. The full
ctest suite runs on the head (the touched TUs' goldens included). Hygiene +
`check_doc_counts.py` are green at the head.
