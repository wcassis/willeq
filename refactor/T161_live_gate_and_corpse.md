# T161 — Live re-probe: the skill gate (T159) and player-corpse consider (T160)

**Slot: slot1. x86. eqemu: ALREADY UP, lock ALREADY HELD by the orchestrator.
No Pi.**

**Do NOT run `scripts/eqemu/up.sh` or `down.sh`, and do NOT touch
`/mnt/projects/fable/.eqemu-lock`.** If the zone dies, restart just the zone and
report it.

**Territory: `refactor/T161_live_gate_and_corpse.md`,
`/mnt/projects/fable/review/T161/`, `data/captures/` (gitignored), and — ONLY if
a probe proves it broken — the specific predicate under probe. No new features.**

T159 and T160 merged on tests alone. This unit is their live acceptance, and it
**closes T153** (the corpse-consider route that T155 found FAILED). Read the
Reviews of T153, T155, T158, T159 and T160 first.

## The evidence rule

Every probe is **VERIFIED** (external evidence quoted — server log, DB, or the
second client), **BLOCKED** (with the exact gate), or **FAILED** (with the
defect). **No local "sent OK" line counts** — and this unit has the inverse
problem too: two probes assert a packet was **NOT** sent, which absence-of-log
alone cannot prove. Use a **positive control on the same run** (see below).

## Cast

- **A = `Oraclerogue`** — account `willeq_rogue` / `WillEqH03Pass`, **status 250**,
  Rogue, level 10, North Qeynos (`zone_id=2`). Scratch character; level/skill
  changes need no restoring. Poison left in slots 30 and 33.
- **B = `Testmelee`** — account `willeq_test`, warrior, level 1. **B is the one
  that dies** (see part 2). Separate accounts, so both can be logged in at once.

**`#logs` works on A's account and only on A's** — `command_settings.access` for
`logs` is 250, which A has and the test account does not. T158 proved it. Use
`#logs set gmsay|file <category> 3` to get server-side confirmation, and say in
the Review which categories you enabled.

## Part 1 — the gate (T159)

| # | probe | expected | how it is proven |
|---|---|---|---|
| 1 | A: `/useskill taunt` | **REFUSED client-side, no packet** | server log shows **no** `OP_Taunt` arriving from A |
| 2 | B: `/useskill taunt` | **sent and handled** | server replies — this is the **positive control** that makes probe 1's silence mean something |
| 3 | A: `/useskill "apply poison"` | **STILL WORKS** | echo `success=1` + a poison row disappears from A's inventory in the DB |
| 4 | A: `/useskill "pick pockets"` | **STILL WORKS** | server `OP_PickPocket` response |

**Probes 3 and 4 are the regression that matters.** T159's gate is
presence-based precisely so Apply Poison keeps working at level 10 with skill 0
— if the gate broke either, that is a **FAILED** and the defect is in T159's
predicate, which is in territory.

Probe 1 must also confirm **what the player is told**. Before T159 the client
printed "You use Taunt!" for a packet that went nowhere; the new message must be
true. Quote it.

⚠ PickPocket hazard, unchanged: NPC target, in range (<20 units), one attempt per
8+ seconds, or eqemu logs `POSSIBLE_HACK` against us.

## Part 2 — player-corpse consider (T160), and the close of T153

T155's probe failed on an NPC corpse and was then BLOCKED by
`NPC:EmptyNPCCorpseDecayTime = 0`. **Use a PLAYER corpse instead** — it persists,
and it is the form T160 was written for.

1. A (`#kill Testmelee` — A has GM status) kills **B**, leaving a player corpse
   named `Testmelee's corpse<id>` (apostrophe + **space**).
2. **A considers the corpse.** Expected: routed to `HC_OP_ConsiderCorpse`, and
   the server answers with a `CORPSE_DECAY_*` / `CORPSE_REZ_*` message. T153
   established there is **no reply packet** — the message IS the effect.
3. **Then the zone-in path, which is the part T160 actually fixed.** A zones out
   and back so the corpse arrives as a **fresh spawn** with `npc_type == 2`, no
   `OP_MobRename` in play. Consider it again. This is the case that was broken
   for every corpse older than our arrival.
4. **Regression:** `/consider` a living NPC still cons normally, and a living
   entity is not treated as a corpse.

If step 3 works only because of the name and not `npc_type`, say so — T160 claims
both routes and only one of them is the interesting one.

## Server state

B dies: **its inventory drops to its corpse.** Record what B was carrying first,
and either loot it back or state plainly what was left. A is scratch. Do not
touch any other character. `SELECT` before and after, both in the Review.

## Method

T148's: `build/bin/willeq` headless, `-d 4 --log-level=DEBUG --script <file>`,
two clients from a single **FOREGROUND** driver that `wait`s on both. Client logs
need **`grep -a`** (T158: plain grep calls them binary). `#spawn` names
containing a **digit** fail silently (T158). Evidence into
`/mnt/projects/fable/review/T161/` with a README.

## Verification

Build + full suite in the slot before probing. Baseline **2585/2586** at tip
`2c60958e`, sole expected failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A defect outside the probed predicate (report it, do not fix it).
- The zone dies twice in one probe.
- Killing B turns out to destroy something not recoverable — stop and report
  before repeating it.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, branch `task/T161-live-gate-corpse`
off `fable` tip **`5cfa6943`**. eqemu was already up and the lock already held by
the orchestrator: `up.sh`/`down.sh` were never run, `.eqemu-lock` was never
touched, no eqemu source or config was patched, and **no zone process died or was
restarted**. `main` and the sibling slots were never touched. Evidence with a
README: `/mnt/projects/fable/review/T161/`.

**No source was changed.** No probe proved the gate's predicate broken, so the
one fix this unit was allowed to make was not needed; the two defects found are
both outside it and are reported, not fixed (STOP condition 1, working as
designed). The tree carries this Review and nothing else.

### Build and suite, before probing

`cmake --build build -j24` clean. `ctest --output-on-failure` **2585/2586**, sole
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` — the plan's
baseline exactly.

One deviation on the build: the **first** `cmake --build` failed inside
`GoogleTestAddTests.cmake` discovery with no compiler error anywhere in the
output. An immediate rebuild with no source change succeeded, and the next full
build was clean. This is T159's deviation 4 reproducing verbatim — it is now
**seen twice, in two different units**, so it is a real parallel-discovery race
in the build, not a one-off.

### Method

Three headless sessions, each **two clients from a single FOREGROUND driver that
`wait`s on both**. A = `Oraclerogue` (Rogue 10, account status 250), B =
`Testmelee` (Warrior 1). Sessions 2 and 3 ran A with
`--log-level=DEBUG --log-module=ENTITY:TRACE` so that `npc_type` is visible at
spawn-parse time; without that the `ZoneSpawn:` line is at TRACE and the whole
`npc_type` question is unanswerable from a client log.

**Server-side logging, enabled from A's account only** (`#logs` needs
`command_settings.access` 250, which only A has — T158's finding):

* **84 `Packet C->S`** at level 3, to file. This is the category that made this
  unit possible: `Client::HandlePacket` (`client_packet.cpp:447`) logs the
  opcode name of **every** packet the server receives, so "a packet was not
  sent" becomes a checkable statement about a file rather than an inference.
* **92 `Corpses`** at level 3, to file and gmsay.
* **22 `Skills`** at level 3, to file and gmsay (session 1 only).

`#logs` is in-memory per zone process: `db_after.txt` confirms
`logsys_categories` rows 9/22/84/92 are still `0/0`.

**Clocks.** The container runs exactly 5 hours behind the host. Server-log
`17:23:34` is client-log `22:23:34`. Every cross-reference below uses that.

---

## Part 1 — the gate (T159)

| # | probe | verdict | evidence |
|---|---|---|---|
| 1 | A `/useskill taunt` | **VERIFIED — refused client-side, no packet** | see "the silence and its control" below |
| 2 | B `/useskill taunt` (control) | **VERIFIED** | `OP_Taunt` reaches the server twice, and the server answers both in ~31 ms |
| 3 | A `/useskill apply poison` | **VERIFIED — still works** | server echo `success=1` **and** the poison row is gone from the database |
| 4 | A `/useskill pick pocket` | **VERIFIED — still works** | 20-byte `OP_PickPocket` in the server log, 86-byte reply 28 ms later, **zero `POSSIBLE_HACK`** |

### The silence and its control (probes 1 and 2)

A, a Rogue, at `22:23:59.381` (`t161_run1_a.log:2310-2315`):

```
Script: executing '/useskill taunt' (step 16/26)
Cannot use Taunt: Your class cannot use Taunt
Cannot activate skill 73: Your class cannot use Taunt
[SYS] Cannot use Taunt: Your class cannot use Taunt
```

**No `Sent skill use packet` line and no `Sent Taunt packet` line.** B, a
Warrior, running the identical command 25 s earlier at `22:23:34.151`
(`t161_run1_b.log:711-727`):

```
[COMBAT] Sent Taunt packet: target=34
[MAIN]   Sent skill use packet: skill=73 target=34
[MAIN]   You use Taunt!
... 31 ms later ...
[ZONE] [SimpleMessage] color_type=270, string_id=5811,
       template='You have failed to taunt your target.'
```

And the arena zone process's own log — the same file, the same window, one
process, both clients — contains **exactly three** `OP_Taunt` lines for the
entire session (`server_arena_230_filtered.log`):

```
[08-20-2026 17:23:34] [Zone] [Packet C->S] [OP_Taunt] [0x5e48] Size [6]   <- B, control 1
[08-20-2026 17:23:42] [Zone] [Packet C->S] [OP_Taunt] [0x5e48] Size [6]   <- B, control 2
[08-20-2026 17:24:44] [Zone] [Packet C->S] [OP_Taunt] [0x5e48] Size [6]   <- A, but via /taunt (defect 1)
```

**There is none at `17:23:59`.** That is probe 1. The silence is evidence and
not merely an absence, for three independent reasons on the same run: B's two
`OP_Taunt`s bracket it and prove the category was live; A's own
`OP_ApplyPoison` at `17:24:14` proves A's connection was logging into the same
file; and A's own `OP_Taunt` 45 s later proves that when A *does* send one, this
log records it.

**What the player is told.** T158's live line was `You use Taunt!` for a packet
the server discarded in silence. It is now
**`Cannot use Taunt: Your class cannot use Taunt`** — printed twice, once as the
`SystemMessage` bridge event (`[SYS]`) and once through the direct log path, and
the shorter `Your class cannot use Taunt` also appears as the raw
`getSkillUseError()` string. T159's test 2 asserted exactly this string off the
bridge; it is now the string a live player sees.

### Probe 3 — Apply Poison still works (the regression that matters)

`t161_run1_a.log:2359-2372`, `22:24:14`:

```
Script: executing '/useskill apply poison' (step 18/26)
Sent skill use packet: skill=6 target=0
You use Apply Poison!
... 21 ms later ...
[ENTITY] ApplyPoison response: slot=29, success=1
```

server log: `[08-20-2026 17:24:14] [Packet C->S] [OP_ApplyPoison] [0x0c2c] Size [10]`

and in the database, `Poison of the Huntsman` (item 52928, slot 30) is **gone**
from `Oraclerogue`'s inventory between `db_before.txt` and `db_after.txt`.

This is T159's central claim proven live: the character is **level 10 with Apply
Poison at 0 and `class_first_level == 18`**, and the presence-based gate lets it
through. A level-based gate would have refused it. One poison (`Spider Venom`,
slot 33) is left in stock.

### Probe 4 — Pick Pocket still works

`t161_run2_a.log:1009-1017`, `22:28:24`:

```
Script: executing '/useskill pick pocket' (step 14/40)
You use Pick Pocket!
Sent PickPocket: to=38 from=37
... 28 ms later ...
PickPocket response received: 86 bytes
```

server log: `[08-20-2026 17:28:24] [Packet C->S] [OP_PickPocket] [0x2ad8] Size [20]`.
One attempt only, NPC target, spawned at A's own position (0 units).
`player_event_logs` contains **zero** `POSSIBLE_HACK` rows.

**Deviation:** the first attempt used `/useskill pick pockets` (plural) and
failed locally with `Unknown skill 'pick pockets'` — the client's name table has
`"Pick Pocket"` singular (`skill_constants.h:210`), and `/useskill` does not do
plural/fuzzy matching. Not a gate failure and not this unit's territory; noted
because it costs a probe cycle exactly the way T158's digit-in-`#spawn`-name
does. The probe was re-run with the singular form in session 2.

---

## Part 2 — player-corpse consider (T160), and the close of T153

Both legs were run **twice over**, on two different corpses:

* **Session 2, on a pre-existing corpse.** `Testmelee's corpse26`
  (`character_corpses.id = 22`, died 2026-08-17) was already lying in arena when
  A zoned in. This is precisely T155's probe-2 case — the one that **FAILED** —
  and, because A never witnessed its death and no rename is ever broadcast for
  it, it is the zone-in case in its purest form.
* **Session 3, on a corpse made for the purpose.** A `#summon Testmelee` +
  `#kill` in qeynos2, giving `Testmelee's corpse275`
  (`character_corpses.id = 25`), then both the death-path and the zone-in leg on it.

| step | probe | verdict | evidence |
|---|---|---|---|
| 2 | consider a player corpse (death path) | **VERIFIED** | `Considering corpse ID: 275` + two server `FormattedMessage`s |
| 3 | consider a player corpse after zone-out/zone-in (fresh spawn, `npc_type == 2`) | **VERIFIED**, twice, on two different corpses | `OP_ConsiderCorpse` on the wire + the same server messages |
| 4 | living NPC still cons normally | **VERIFIED**, before and after the zone-in | `OP_Consider` on the wire, normal con line |
| 4b | living entity not treated as a corpse | **VERIFIED** | living `Testmelee` parses as `npc_type=0`; the corpse in the same batch as `npc_type=2` |

### Step 2 — the death path

`t161_run3_a.log:1443-1486`, qeynos2:

```
22:36:17.549  [SYS] Killing Testmelee (275).
22:36:17.549  [CHAT] Testmelee has been slain by Testmelee!
22:36:17.549  [SYS] [Corpses] [SetRezTimer] Checking for rezzable corpse [Unnamed_Corpse]
22:36:38.469  Considering corpse ID: 275
22:36:38.469  [COMBAT] Sent corpse consider request for target ID: 275
22:36:38.549  [ZONE] [FormattedMessage] template='This corpse's resurrection time will expire in
              %1 hour(s) %2 minute(s) %3 seconds.', formatted='... 2 hour(s) 59 minute(s) 39 seconds.'
22:36:38.549  [ZONE] [FormattedMessage] template='This corpse will decay in %1 day(s) %2 hour(s)
              %3 minute(s) %4 seconds.', formatted='... 6 day(s) 23 hour(s) 59 minute(s) 38 seconds.'
```

server log: `[08-20-2026 17:36:38] [Packet C->S] [OP_ConsiderCorpse] [0x773f]`.

Those two strings are `CORPSE_REZ_TIME_HOUR` and the decay line — both from
inside the `t->IsPlayerCorpse()` branch of `Handle_OP_ConsiderCorpse`
(`client_packet.cpp:5280+`), reachable only if the corpse route was taken.
T153 established there is **no reply packet**; the message IS the effect, and
here it is.

**A finding inside step 2 that the plan did not anticipate, and it matters.** The
`/target Testmelee's corpse` immediately before the consider **failed**:

```
CMD outcome=failed registry=action recognized=true
    message="No target found matching 'Testmelee's corpse'" cmd="/target Testmelee's corpse"
```

The consider still routed to the corpse — because the target was *already* entity
275 from the earlier `/target Testmelee`, and the death handler had set
`Entity::is_corpse` on it. **In the death path the client never learns the
corpse's new name**: eqemu broadcasts `OP_MobRename` for *NPC* corpses only
(`NPC::Death`, `attack.cpp:2850`), never for player ones, so entity 275 is still
called `Testmelee` in `m_entities` while the server calls it
`Testmelee's corpse275`. So on the death path T160's `ParseCorpseName` cannot
fire at all — only the `is_corpse` flag can — and a player who watches someone
die cannot reach the corpse by typing its name. Reported as defect 2.

### Step 3 — the zone-in path, and which route actually fired

**The plan asks specifically whether this worked via `npc_type == 2` or merely via
the name. The honest answer: `npc_type == 2` is what fired, the name was present
too, and no live experiment can separate them.**

`npc_type == 2` was observed on the wire, in the `HC_OP_ZoneSpawns` batch, for
every player corpse in every session — `t161_run3_a.log:2291`, the fresh corpse
arriving after A's `#zone arena` / `#zone qeynos2` round trip:

```
[TRACE] [ENTITY] ZoneSpawn: Testmelee's corpse275 (ID:275)
        pos=(428.00,-74.00,3.00) heading=0.00 npc_type=2
```

and `t161_run2_a.log:107` / `:589` / `:2083`, the pre-existing arena corpse, on
three separate zone-ins:

```
[TRACE] [ENTITY] ZoneSpawn: Testmelee's corpse26 (ID:26) ... npc_type=2
```

with the living player in the same batch as the control
(`t161_run2_a.log:570`): `ZoneSpawn: Testmelee (ID:36) ... npc_type=0`.

`IsCorpseEntity` tests `is_corpse` → `npc_type == 2 || == 3` → `ParseCorpseName`,
in that order. At zone-in `is_corpse` is false (no `OP_Death`, no
`OP_MobRename`), `npc_type` is 2, so **the `npc_type` branch is the one that
returns** and the name is never consulted. That is the interesting route and it
is the one that ran.

What cannot be claimed is that the name route was *unnecessary*: both signals
arrive together, because `Corpse::FillSpawnStruct` (`corpse.cpp:1787`) sets
`ns->spawn.NPC = 2` for every corpse it spawns *and* `CalcCorpseName` always
composes the `'s corpse<id>` name. eqemu cannot be made to emit one without the
other, so no live probe can isolate them; T160's unit test
`PlayerCorpseSpawnedAtZoneInIsACorpse` is where that separation is pinned, and
this unit confirms the wire value the test assumes is the value the server really
sends. Note that on the **death** path the ordering is the reverse of a claim
T160 could have made: there the name is *absent* (defect 2), so `npc_type`/`is_corpse`
is not merely the first test, it is the only one available.

Repeated on the pre-existing arena corpse, session 2 (`t161_run2_a.log:2464-2480`,
after `#zone qeynos2` → `#zone arena`):

```
22:30:18.127  Considering corpse ID: 26
22:30:18.213  [SYS]  This corpse has already accepted a resurrection.
22:30:18.213  [CHAT] This corpse will decay in 6 day(s) 23 hour(s) 27 minute(s) 57 seconds.
```

— the `IsRezzed()` branch, which matches `character_corpses.is_rezzed = 1` for
corpse 22 in the database. Two `OP_ConsiderCorpse` lines in the arena server log
(`17:29:07`, `17:30:18`) and two in the qeynos2 one (`17:36:38`, `17:37:53`).

### Step 4 — the regression

A living `#spawn`ed NPC still cons normally, both before the corpse work and
after the zone-in (`t161_run2_a.log:1027` and `:2495`):

```
Probepick regards you indifferently -- cons green
```

with `[08-20-2026 17:28:37]` and `[17:30:33]` `[Packet C->S] [OP_Consider] [0x65ca]`
in the server log — the ordinary opcode, not the corpse one. So T160's widening
of `CombatManager::IsCorpse` did not make a living entity look like a corpse, and
consumer 1's targeting change did not break ordinary consider.

### Is T153 closed?

**Yes.** Its two legs were `SendConsiderCorpse` (T155 probe 2, FAILED) and
`/appearance` (T155 probe 4, already VERIFIED). The corpse leg is now VERIFIED on
the wire, on four separate sends, on two different corpses, through both the
death path and the zone-in path — including the exact case T155 failed on, a
player corpse that predates our arrival. T155's still-owed item 1 is discharged.

---

## Defects found (both outside the probed predicate — reported, not fixed)

**1. `/taunt` bypasses T159's gate entirely, and still lies to the player.**
Same session, same character, same target, 45 seconds after probe 1
(`t161_run1_a.log:2466-2469`):

```
22:24:44.469  Script: executing '/taunt' (step 22/26)
22:24:44.469  [COMBAT] Taunt: target=34
22:24:44.469  [COMBAT] Sent Taunt packet: target=34
22:24:44.469  [MAIN]   Taunting target
```

and the server received it — `[08-20-2026 17:24:44] [Packet C->S] [OP_Taunt]
[0x5e48] Size [6]` — and discarded it in silence at `Handle_OP_Taunt`'s
`HasSkill(SkillTaunt)` guard: **nothing at all comes back** in the following
12 seconds, against B's 31 ms `FAILED_TAUNT` for the same opcode.

The route is `cmdTaunt` → `ActionDispatcher::taunt()` → `CombatManager::Taunt()`
(`combat.cpp`), which builds the 4-byte body and calls `QueuePacket` with **no
skill check of any kind**, then reports `"Taunting target"`. T159 gated
`SkillManager::activateSkill`; `/taunt` never goes through it. This is T158's
defect 1 surviving on a second command — the user-visible symptom is unchanged
for anyone who types `/taunt` rather than `/useskill taunt`. Not fixed: the
grant is the gate's own predicate, and this is a different call path in a
different file. **This is the single most valuable thing this unit found**, and
it would not have been visible from source alone, because T159's Review
reasonably concluded that "no change to `eq.cpp` was needed" after auditing the
`activateSkill` callers — `/taunt` is not one of them.

**2. A player corpse keeps the living player's name in the client's entity list
on the death path.** Detailed under step 2. eqemu renames only NPC corpses, so
after watching someone die the client's `m_entities` still holds
`Testmelee` for an entity the server calls `Testmelee's corpse275`.
Consequences: `/target <name>'s corpse` fails until you leave and re-enter the
zone; `/who`-style name displays and the target window show a dead player under
their living name; and anything that reaches for a corpse *by name* in that
window misses it. `Entity::is_corpse` does get set, so the consider route is
unaffected — which is why step 2 still passed. This is a spawn/rename-path
question in `src/client/eq.cpp` / `eq_entities.cpp`, i.e. T160's owed item 2
territory, not this unit's.

---

## Server state — before and after

Full `SELECT`s in `review/T161/db_before.txt` and `db_after.txt`.

| what | before | after |
|---|---|---|
| `inventory` `Testmelee` (66), slots 23–27 | Skin of Milk 20, Bread Cakes* 20, Tattered Note, Bandages* 18, Backpack* | **identical, all five rows** |
| `character_corpse_items` | empty | **empty** — B's corpse carries nothing |
| `character_corpses` | 7 rows (18,19,20,21,22,23,24) | **8 rows** — new `id=25`, `Testmelee`, zone 2, `2026-08-20 17:36:17`, not rezzed, empty |
| `character_data` `Testmelee` | level 1, zone 77, (156,-1009,51), hp 34, exp 0 | level 1, **zone 189** (its bind), (18,-147,20), hp **34** (full), exp **0** |
| `inventory` `Oraclerogue` (81) | 9 rows incl. `Poison of the Huntsman` (slot 30) and `Spider Venom` (slot 33) | **8 rows** — the Huntsman poison consumed by probe 3 (that is the evidence); Spider Venom left |
| `character_data` `Oraclerogue` | level 10, zone 2, (167,30,-29.125) | level 10, zone 2, (-74,428,3) — scratch, not restored |
| `character_skills` 6/9/16/48/73 both chars | 0/1/1/55/0 and 0/0/0/0/10 | **unchanged** |
| account status | `willeq_rogue` 250, `willeq_test` 200 | **unchanged** |
| `logsys_categories` 9/22/84/92 | file 0, gmsay 0 | **file 0, gmsay 0** — `#logs` never wrote the table |
| `player_event_logs` | 1400 rows | 1407+; the delta is 1 Death, 22 GM Command, 9 Zoning. **Zero `POSSIBLE_HACK`** |
| `rule_values` | — | untouched |
| zone processes | arena 230, qeynos2 271 | **same PIDs, never restarted** |

**On B's inventory.** The plan required recording what B carried and then either
looting it back or saying plainly what was left. Nothing had to be looted back:
`character_corpse_items` is empty and B's five inventory rows are unchanged, so
the death dropped nothing. That is eqemu's own behaviour for this character, not
something this unit arranged; the empty corpse decays on
`Character:EmptyCorpseDecayTime` = 3 h and needs no cleanup. Had anything
dropped, the recovery would have been awkward — B's bind is in zone 189 and it
died in qeynos2 — so this is worth knowing before anyone repeats the experiment
with a character that carries something.

## Deviations

1. **`#kill Testmelee` is not a thing, and the first attempt killed a corpse.**
   eqemu's `#kill` takes a target or an entity id, never a name
   (`gm_commands/kill.cpp`). Session 2 therefore did `/target Testmelee` +
   `#kill` — and `EntityManager::findEntityByName` is a **substring** match that
   returned the *pre-existing* `Testmelee's corpse26` (entity 26) instead of the
   living `Testmelee` (entity 36), because 26 comes first in `m_entities`. The
   server duly reported `Killing Testmeleescorpse (26).` and nothing happened.
   B did not die in session 2. Session 3 worked around it by moving the whole
   experiment to **qeynos2**, which has no pre-existing `Testmelee` corpse, using
   `#summon Testmelee` (which *is* name-based, server-side, and cross-zone) to
   bring B there first.
   The accident was informative rather than costly: it is what produced the
   pre-existing-corpse evidence, which is a *better* witness for T155's failing
   case than a corpse we made ourselves. It is also a small usability finding in
   its own right — with a corpse on the ground you cannot `/target` the living
   player of the same name.
2. **`--log-module=ENTITY:TRACE` instead of `-d 4`.** The `npc_type` question
   cannot be answered at DEBUG: `ZoneSpawn: ... npc_type={}` is `LOG_TRACE`
   (`eq_entities.cpp:451`). Global TRACE would have buried the session in
   position updates, so sessions 2 and 3 used
   `--log-level=DEBUG --log-module=ENTITY:TRACE` and dropped `-d` (which,
   per T31, would clobber the module level).
3. **Server log category 84 `Packet C->S` was used, which no previous unit in
   this arc had found.** It is the general answer to "prove a packet was not
   sent": `Client::HandlePacket` logs every arriving opcode by name before
   dispatch. Worth carrying forward — it is strictly better evidence than
   inferring silence from a missing reply, and it costs one `#logs` line on a
   status-250 account.
4. **`/useskill pick pockets` (plural) fails locally.** See probe 4. The correct
   token is `pick pocket`.
5. **Three transient `#spawn` NPCs were created and left standing**:
   `Probegate000` and `Probepick000` in arena. In-memory only (T155's finding),
   no database rows, gone with the zone process. Not despawned, for T155's
   reason: `#depop` aims at the current target and `/target` cannot be aimed
   reliably at a substring-colliding name.
6. **`Oraclerogue` was not restored** — scratch character, as the plan says. It
   now sits in qeynos2 rather than North Qeynos's original spot, and is one
   poison lighter.
7. **B was killed once and only once.** STOP condition 3 was checked before any
   repeat: `character_corpse_items` was inspected immediately after the death,
   found empty, and B's inventory found intact, so nothing unrecoverable had
   happened. No second kill was needed or attempted.

## STOP conditions

None was hit. The zone processes never died (arena 230 and qeynos2 271 held the
same PIDs throughout). B's death destroyed nothing. The two defects found are
both outside the probed predicate and are reported rather than fixed, which is
STOP condition 1 doing its job.

## Still owed

1. **Defect 1 — `/taunt` (and any other command that reaches `CombatManager`'s
   skill senders directly) must go through the same gate as `/useskill`,** or
   the gate is one command wide. A sweep of `CombatManager`'s packet-building
   methods for callers that bypass `SkillManager::activateSkill` is the shape of
   that unit; `Taunt()` is the one proven live, but it is unlikely to be alone.
2. **Defect 2 — the client's entity name is not updated when a player dies.**
   Fits naturally with T160's owed item 2 (set `is_corpse` from the spawn
   parser), since both live in the spawn/rename paths.
3. **T159's owed items 2, 3 and 4** are untouched by this unit: the per-level cap
   curve, `config/skill_caps.json` in `application.cpp`'s startup manifest, and
   the Backstab facing check.
4. **T160's owed items 2 and 3** are untouched: widening `Entity::is_corpse` at
   spawn-parse time (which would move `spell_type_processor.cpp:566` and
   `spell_manager.cpp:483`), and the sweep for other places that ask "is this a
   corpse" the long way.
5. **The transient build failure** (T159 deviation 4, now reproduced) deserves a
   look — two units in a row have hit a discovery-time race in
   `GoogleTestAddTests.cmake`.
