# T155 — Clear the wire-probe debt (T149, T150, T152, T153)

**Slot: slot1. x86. eqemu: ALREADY UP, lock ALREADY HELD by the orchestrator.
No Pi.**

**Do NOT run `scripts/eqemu/up.sh` or `down.sh`, and do NOT touch
`/mnt/projects/fable/.eqemu-lock`.** The stack was brought up for you
(loginserver:5998, world:9000, zone `arena`) and the orchestrator takes it down.
`scripts/eqemu/status.sh` is yours to read freely. If the zone process dies,
restart just the zone — and **say so in the Review**, because one of the probes
below can legitimately kill it.

**Territory: `refactor/T155_probe_debt.md`, `/mnt/projects/fable/review/T155/`,
`data/captures/` (gitignored), and — ONLY for a sender that a probe proves
broken — that sender's own body. No action-layer restructuring, no new
commands.**

Four units merged without wire verification. This unit converts as much of that
debt as it can into observed evidence, and reports the rest as still owed. Read
each unit's Review first: `refactor/T149_tradeskill_recipes.md`,
`T150_social_find_family.md`, `T152_skill_opcode_audit.md`,
`T153_consider_corpse_and_face.md`.

## The standing evidence rule — this is the whole point of the unit

**Nothing is accepted on a local "sent OK" log line.** Most of these handlers
bare-return on a size mismatch with no server-side complaint, so silence proves
nothing. Every row of your result table names an effect observed by **the server
log, the database, or a second client**.

**A probe in this arc has already faked a result once** (see the resume memory's
INSTRUMENTATION WARNING). Three outcomes are allowed per probe and only three:

- **VERIFIED** — with the external evidence quoted.
- **BLOCKED** — with the exact gate that stopped it (missing class, missing
  item, missing NPC, no second client). A blocked probe is a *good* outcome
  honestly reported; the debt simply stays owed.
- **FAILED** — the packet was rejected or the effect did not happen. This is the
  expected outcome for roughly one in two, per the arc's measured rate. **Report
  the defect; fix it only if it is the sender's own body.**

Do not convert a BLOCKED into a VERIFIED by reasoning from source. Source
reading is what created this debt.

## Server facts, established by the orchestrator — do not re-derive

Characters on `willeq_test` (account status 200): `Testchar` (warrior, L2,
zone 36), `Testmelee` (warrior, L1, zone 77), `Testcaster` (wizard, L1).
`willeq_buddy`: `Testbuddy` (magician, L1, zone 77). **There is no Rogue and no
poison item anywhere in the roster.**

Three handler facts read from eqemu source at
`/mnt/projects/claude/EQEmu/zone/client_packet.cpp`, already checked:

1. **`Handle_OP_ApplyPoison` (:3016) ALWAYS replies** with an `OP_ApplyPoison`
   echo carrying `success` and `inventorySlot`, whatever your class or item —
   the rogue/poison test only decides whether `success` is 1 or 0. **So the
   round trip is probeable TODAY with a warrior and no poison**, and that round
   trip is exactly what T152's fix owes: right size, accepted by the handler,
   echo parsed by `ZoneProcessApplyPoison` at offset 6. A `success=0` echo is a
   PASS for this unit. Do not create a Rogue for it.
2. **`Handle_OP_PickPocket` (:11913) returns early on
   `!HasSkill(SkillPickPockets)`** and sends nothing, so with no Rogue this one
   is **BLOCKED** — expect to report it that way. ⚠ It also writes a
   **`POSSIBLE_HACK` player-event log** against the player for an out-of-range
   or too-fast attempt. Do not brute-force it.
3. **`Handle_OP_Bind_Wound` (:4013) is the T152 upstream hazard**: it logs a
   size mismatch and does **not** return. Our body is now non-empty so `pBuffer`
   exists and the null-deref path is closed — but **if the zone process dies
   during this probe, that is the upstream eqemu bug confirming itself.**
   Record it with the log, restart the zone, move on. Do not chase it, do not
   patch eqemu.

## The probe matrix — run in this order, cheapest and safest first

Order matters: each group is independently reportable, so a blocker late in the
list cannot cost you the earlier evidence.

**Group 1 — T153 (single client, no gates).**
- `/consider` a living NPC — regression check, must still work exactly as before.
- `/consider` a **corpse** — the new route. Make a corpse the cheap way
  (`/say #kill` on an NPC with the GM account, T148's precedent for driving
  server-side `#` commands through `/say`). Evidence: a `CORPSE_DECAY_*` /
  `CORPSE_REZ_*` message from the server. T153 established there is **no reply
  packet** — the message IS the effect, do not look for one.
- `/appearance` — evidence is **`character_data.face` (and the other six fields)
  changing in the DB**, plus the server's `FACE_ACCEPTED` message. T153 already
  established a second client will NOT see it (`OP_SetFace` has no Titanium
  opcode); do not spend a second client proving that again.

**Group 2 — T152's five skills (single client, one is blocked).**
ApplyPoison (echo, per fact 1 above), BindWound (per fact 3), Disarm, Taunt,
PickPocket (expect BLOCKED, per fact 2). Taunt and Disarm need a living NPC
target in range. For each: did the packet clear the size gate, and what did the
server do?

**Group 3 — T150's social/find family.**
`/groupassist`, `/track`, `/find`, `/friendswho`. Several are skill- or
group-gated; `/track` needs a tracking class. **T150 already recorded that
`SendFriendsWho`'s payload can never match** (eqemu `strchr`s the whole buffer)
— probe it anyway and confirm the failure mode on the wire, since a confirmed
negative is evidence too.

**Group 4 — T149's tradeskill recipes.** Needs a tradeskill container (T144's
`Object_Struct` work). `SendRecipesFavorite` is already known to be structurally
impossible (variable-length body vs an exact 2008-byte gate) — same rule: confirm
the failure on the wire rather than restating the source claim.

**Stop when the matrix is done or a group is genuinely blocked.** Do not invent
extra probes; do not go after batch C or D.

## Method

T148's, verbatim where it fits: `build/bin/willeq` headless,
`-d 4 --log-level=DEBUG --script <file>`, driven from a single **foreground**
driver that `wait`s on every client it launched. **Never end your turn on a
background process.** Configs/scripts/logs under `data/captures/` (gitignored).
Annotated evidence — logs, DB before/after `SELECT`s — into
`/mnt/projects/fable/review/T155/` with a README, per the review-media
convention.

**`--log-level=INFO` is a Pi rule, not an x86 one; DEBUG is correct here.**

## Server state — leave as found

`SELECT` the affected `character_data` rows before and after and put both in the
Review. `/appearance` deliberately mutates a character's face — **restore it**,
and show the read-back. Same for anything else you change. Do not create or
delete characters; do not edit account status.

## Verification

Build + full suite in the slot before you start probing (a stale binary
invalidates every result) and again at the end if you changed any source.
Baseline **2542/2543** at tip `a10f3cf1`, sole expected failure the documented
ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A probe would need a character, class, item or account change beyond the
  face restore above — report BLOCKED instead.
- A defect turns out to be outside the probed sender's own body (report it; do
  not fix it here).
- The zone server dies twice in the same probe.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch `task/T155-probe-debt`,
off `fable` tip `a10f3cf1`. eqemu was already up and the lock already held by the
orchestrator: `up.sh`/`down.sh` were never run, `.eqemu-lock` was never touched,
and no eqemu source or config was patched. Full evidence, with a README:
`/mnt/projects/fable/review/T155/`.

**Build and suite before probing** (a stale binary invalidates every result):
`cmake --build build -j24` clean, `ctest --output-on-failure` **2542/2543**, sole
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds` — the plan's
baseline exactly. **No source was changed by this unit**, so no second run was
owed (see "Fixes" below for why not).

Ten headless sessions, all driven from foreground drivers that `wait` on every
client they launch; one of them (`t155_run5.sh`) two-client. Scripts, configs and
logs under `data/captures/` (gitignored), copies in the review directory.

### The matrix — 17 rows, one per probe

| # | probe | unit | verdict | external evidence |
|---|---|---|---|---|
| 1 | `/consider` living NPC | T153 | **VERIFIED** | server `OP_Consider` reply parsed: `Consider: target=40, faction=5, level=20`; `Probemob regards you indifferently -- cons white` (`t155_run2_a.log:771,773`) |
| 2 | `/consider` player corpse | T153 | **FAILED** | `Target set to Testmelee's corpse30 (ID: 30)` then `Consider: target=30, faction=1, level=20` — an `OP_Consider` reply, and **no** `Considering corpse ID:` line. The corpse route was never taken (`t155_run3_a.log:640,660,662`). Defect 1 below |
| 3 | `/consider` NPC corpse | T153 | **BLOCKED** | corpse gone before the probe could land. `/loot <id>` at death **+0.41 s** already returns the server's `Error: OP_LootRequest: Corpse not found (ent = 0)` (`t155_run8_a.log:628,642,650`). Gate: `rule_values` `NPC:EmptyNPCCorpseDecayTime = 0` |
| 4 | `/appearance` | T153 | **VERIFIED** | server `Facial features accepted.` (`FACE_ACCEPTED`, string 12028, `t155_run1_a.log:767`) **and** `character_data.face` for Testmelee 4 → 3 in the server's own DB, then 3 → 4 on the restore |
| 5 | ApplyPoison (skill 6) | T152 | **BLOCKED** | no reachable caller anywhere in the client — defect 2, `review/T155/skill_reachability_evidence.txt` |
| 6 | BindWound (`/bandage`) | T152 | **VERIFIED** | **two inbound `OP_Bind_Wound` reply packets** (`Unhandled zone opcode: HC_OP_BindWound`, `t155_run1_a.log:744,778`) plus a **bandage charge consumed in the database** (inventory row 66/slot 26/item 21779: 18 → 17). Zone process PID 232 stayed up |
| 7 | Disarm (skill 16) | T152 | **BLOCKED** | same gate as 5 |
| 8 | Taunt (`/taunt`) | T152 | **VERIFIED** | NPC broadcast `SUCCESSFUL_TAUNT` naming the taunter: `Probemob says 'I'll teach you to interfere with me Testmelee.'` (`t155_run2_a.log:803-805`). Out-of-range control from an earlier run: `TAUNT_TOO_FAR`, string 386 (`t155_run1_a.log:718`) |
| 9 | PickPocket (skill 48) | T152 | **BLOCKED** | same gate as 5, **and** no Rogue exists (the plan's own fact 2). No `POSSIBLE_HACK` was provoked; nothing was brute-forced |
| 10 | `/groupassist` | T150 | **VERIFIED** | server answered `OP_Assist` 65 ms later — `Unhandled zone opcode: HC_OP_Assist` (`t155_run3_a.log:781,783`). Both halves of T150's prediction confirmed at once: the send lands, the reply is dropped |
| 11 | `/track` | T150 | **BLOCKED** | `Handle_OP_TrackTarget` gates on `CanThisClassTrack()`; the roster is warrior/warrior/wizard/magician. No tracking class, and creating one is a STOP |
| 12 | `/find` | T150 | **VERIFIED** | first ever `OP_FindPersonReply`: `Find person result: destination at (156, -1009, 51.1)` — the target's **server-side** coordinates (`t155_run3_a.log:751,754,757`) |
| 13 | `/friendswho` | T150 | **VERIFIED** | matched name → `Who response: 1 player(s)`, `Who header: id=46, playersinzonestring=5028, playercount=1` (`t155_run5_a.log:617,619`); unmatched name → `WHOALL_NO_RESULTS`, string 5029 (`t155_run3_a.log:831`). Two names, two server outcomes |
| 14 | `/recipesearch` | T149 | **VERIFIED** | `Recipe: Bread Tin (ID: 3842, Trivial: 38)` — server-side recipe data, first ever `OP_RecipeReply` (`t155_run10_a.log:3263`) |
| 15 | `/recipefavorites` | T149 | **VERIFIED** | the same recipe came back from the **favorites** send (`t155_run10_a.log:3579,3581`). `Handle_OP_RecipesFavorite` `LogError`s and returns on any size but 2008, so this is T149's structural fix proven on the wire |
| 16 | `/recipedetails` | T149 | **VERIFIED (send only)** | server answered on the same opcode; client has no parser: `Unhandled zone opcode: HC_OP_RecipeDetails` (`t155_run10_a.log:3893`). Confirms T149's finding 3 |
| 17 | `/recipecombine` | T149 | **VERIFIED** | `OP_RecipeAutoCombine` reply with `reply_code != 0`, **and** the server named the four missing components — `You are missing a Bread Tin Mold / Water Flask / Metal Bits / Ceramic Lining` (`t155_run10_a.log:4162-4191`) |

**11 VERIFIED, 1 VERIFIED-send-only, 4 BLOCKED, 1 FAILED.** The arc's measured
"one FAILED per two senders" did not repeat here, and the reason is worth stating
plainly rather than celebrating: five of the seventeen rows were *already correct
senders that had simply never been called*, and the one hard FAILED plus the
biggest BLOCKED are both **reachability** defects, not byte defects. That is the
same pattern this arc keeps finding, one layer up.

### Defects found

**1. `/consider` on a player corpse never reaches `SendConsiderCorpse` (probe 2 —
FAILED).** eqemu's `Corpse::CalcCorpseName` (`zone/corpse.cpp:612`) names *player*
corpses `X's corpse<id>` — apostrophe, then a **space** — and *NPC* corpses
`` X`s_corpse<id> ``, which `NPC::Death` then renames client-side to `X's_corpse<id>`.
`CombatManager::IsCorpse` (`combat.cpp:1681`) tests
`entity.is_corpse || name.find("'s_corpse")`; the player form matches neither, and
`Entity::is_corpse` is only ever set by the death path (`eq.cpp:3311`), which
requires having witnessed the `OP_Death` in this session. So a corpse that was
already lying in the zone when you zoned in — the ordinary case, and the only case
that survives more than a second on this server — is considered as a living mob.
Observed, not inferred: the client sent `HC_OP_Consider` for `Testmelee's corpse30`
and the server answered with a normal con.
**Not fixed**: this is in `IsCorpse`/the spawn parser, not in `SendConsiderCorpse`'s
own body, which is the only thing this unit was allowed to touch.

**2. `SkillManager::activateSkill()` has zero reachable callers, so all five T152
skill-body fixes are unreachable (probes 5, 7, 9 — BLOCKED).** Greps in
`review/T155/skill_reachability_evidence.txt`:

* Only two call sites: `eq_bridge.cpp:1052` (`SkillActivateIntent`) and
  `eq_bridge.cpp:1360` (`HotbarActivateIntent`, `case 3: // Skill`).
* `SkillActivateIntent` appears **four** times in the whole tree: the consumer, the
  struct, the variant entry, and a doc line. **No producer exists.**
* `HotbarActivateIntent` has exactly one producer,
  `static_ui_input.cpp:42`: `bridge_->pushIntent(eqt::events::HotbarActivateIntent{slot});`
  — a brace-init with one value, so `index = slot` and `buttonType`, `id`,
  `emoteText` are value-initialised. `buttonType` is therefore always `0` (Spell).
  **`case 3` cannot be entered by any code path in this client.**
* The action layer cannot help: `EqActionHandler::useSkill()`
  (`eq_action_handler.cpp:561`) is a `"Use skill {} not yet implemented"` stub, and
  `grep useSkill` over both command registries returns nothing.

This is the reachability pattern again, and it is a *stronger* statement than "no
Rogue": even with a Rogue and a poison, nothing in this client could fire skill 6.
The plan's fact 1 ("the round trip is probeable TODAY with a warrior and no
poison") is correct about **eqemu**, and wrong about **this client** — the gate is
on our side, and it was not visible from the source reading T152 did.

**3. `/loot <id>` on a fresh NPC corpse is refused (`Error: OP_LootRequest: Corpse
not found (ent = 0)`), three times, at 0.41 s, 0.66 s and 11 s after death.**
That message is a literal string in `client_packet.cpp:10310` — it does **not**
echo the id, so it cannot distinguish "the corpse decayed" from "our loot-request
body is wrong". Both readings survive this unit's data. Reported, not chased: it is
T34's sender, not one of the four units being cleared.

**4. `/find`'s reply prints `Total points 5` where eqemu's non-pathing branch
builds exactly two `FindPerson_Point`s** (`Client::SendPath`, `client.cpp:12114`).
The destination itself decodes correctly and matches the target's real server
position, so `ZoneProcessFindPersonReply` is not garbage — but the point count
does not obviously agree with what was sent. Reported; it is a receive-side parse
question of the T145/T147 struct-audit class.

**5. `OP_WhoAllResponse` renders as `There is %1 player in EverQuest.: 1`** — the
`%1` placeholder is never substituted before display. Cosmetic, receive-side,
reported.

### Fixes made: none, and why that is the correct outcome

The unit's grant is "ONLY the body of a sender a probe proves broken". No probe
proved a sender's own body broken. Every byte-level claim that was testable came
back VERIFIED — including the two that four units' worth of source reading had
flagged as the most likely to be wrong (T149's 2008-byte favorites body and T152's
8-byte bind-wound body). The one FAILED and the largest BLOCKED are both in
*routing*, one layer above the senders, and are reported above rather than fixed.
Consequently the tree is unchanged apart from this Review, and the pre-probe
suite result (2542/2543) still stands.

### Deviations

1. **`#logs` was refused, so eqemu-side `LogDebug` lines were never available.**
   `#logs set gmsay 9 3` and `#logs set file 9 3` both returned
   `Your status is not high enough to use this command.` (`t155_run3_a.log:609,627`)
   on the status-200 account. Every eqemu handler under test that rejects a bad
   size does so with `LogDebug` (`Handle_OP_ConsiderCorpse`,
   `Handle_OP_AssistGroup`) or with no log at all, and the zone's own log is at
   Info. **This is why probe 3 is BLOCKED and not FAILED**: with no server-side
   debug line and no way to prove the corpse existed at the instant of the probe,
   "the server said nothing" is not evidence about our packet.
2. **`#spawn` was used to create disposable NPC targets** (`Probemob`, `Probetwo`,
   `Probethree`) and `#npcloot add 21779` to try to make one corpse persist.
   `NPC::SpawnNPC` and `AddLootDrop` are in-memory only — neither writes to the
   database — so this is transient server state, not a mutation. It is recorded
   because the plan did not anticipate it.
3. **The tradeskill leg (Group 4) required leaving `arena`.** Arena reports
   `LoadZoneObjects Loaded [0] world objects`, so there is no tradeskill container
   in it at all. `/say #zone qeynos` → probes → `/say #zone arena`, twice. Zoning
   moved Testmelee's saved position to arena's safe point (x 156 → 146); restored
   by SQL, read back, table below. Nothing else about the character changed.
4. **Three zone NPCs were killed with `#kill`** (`a_training_dummy000`, `a_rat022`,
   `a_rat021`, `a_rat015`, `a_rat014`) to try to produce an NPC corpse. These are
   spawn2-driven zone spawns that respawn on their own timers; nothing persists.
5. **A temporary NPC was left standing.** `Dummy000`, from the first probe run,
   is still in `arena`. It has no database row and vanishes with the zone process,
   but it could not be despawned: `/target Dummy000` resolves through a
   case-insensitive substring search that finds `an_undead_training_dummy000`
   (lower entity id) first, so `#depop` could never be aimed at it. Reported rather
   than worked around with a destructive command like `#depopzone`.
6. **One two-client launch failed and was retried.** Launching both clients in the
   same instant made client A die at `S04: Initializing spell system` with
   `Failed to initialize application` (`t155_run4_a.log`). Re-running A alone
   worked, and the later two-client run (`t155_run5.sh`) staggered the launches by
   15 s and both clients came up. The failure is recorded, not hidden; it is a
   startup race in the client, worth a look, and out of this unit's territory.
7. **The corpse probe was attempted five times** (runs 1, 2, 3, 5, 6, 7, 8) with
   progressively tighter windows and a loot-based discriminator, before being
   called BLOCKED. Killing a *player* character would have produced a week-long
   corpse and settled it — that was deliberately **not** done: it is a character
   change beyond the sanctioned face restore, i.e. the plan's first STOP condition.
8. **`/friendswho`'s positive case was proven twice**, once against a name whose
   `ClientListEntry` was still live in world after the second client camped, and
   once with the second client genuinely online (`t155_run5`). Both gave
   `playercount=1`; the negative control gave `WHOALL_NO_RESULTS`.

### STOP conditions

None was hit in the sense of aborting. Two were respected and are the reason for
two of the four BLOCKEDs: creating a Rogue (probes 5/9) and creating a tracking
class (probe 11) would both have been character/class changes, and killing a
player character to get a durable corpse (probe 3) would have been the same. The
zone server did not die once, so the "dies twice in the same probe" condition
never came near.

### Server state — leave as found

`SELECT`s in `review/T155/db_before.txt`, `db_after_probes.txt`,
`db_after_restore.txt`.

| what | before | after probes | after restore |
|---|---|---|---|
| `character_data.face` (Testmelee, id 66) | 4 | 4 (restored in-game by `/appearance face 4`, server answered `Facial features accepted.`) | **4** |
| `character_data.x` (Testmelee) | 156 | 146 | **156** (SQL) |
| `character_data` zone_id / y / z / heading / last_name (all four characters) | — | unchanged | unchanged |
| `character_data` hair_color / beard_color / eye_color_1 / eye_color_2 / hair_style / beard | all 0 | all 0 | all 0 |
| `inventory` (66, slot 26, item 21779 Bandages) charges | 18 | 17 | **18** (SQL) |
| `character_skills` id 66, skills 6/9/16/48/53/73 | 0/0/0/0/0/10 | 0/0/0/0/0/10 | 0/0/0/0/0/10 |
| `character_corpses` rows | 7 | 7 | 7 |
| `rule_values`, `logsys_categories`, account status | — | untouched (`#logs` refused) | untouched |
| zone process `arena` | PID 232 | PID 232, never restarted | PID 232 |

The 18-charge "before" for the bandages is read from the run-1 login log, which
parsed `Bandages*` at `stackCount=18` before any bind-wound was sent
(`t155_run1_a.log`, `InventoryManager Parsed item: Bandages* in slot 25`) — the
pre-unit `SELECT` covered `character_data`, `character_skills` and `group_id` but
not `inventory`, which is itself a small deviation and is why the log reading is
quoted here instead of a row from `db_before.txt`.

### Still owed

1. **`/consider` on a corpse.** Defect 1 must be fixed (teach the client that
   `X's corpse<id>` is a corpse, or set `is_corpse` from the spawn packet) before
   the route can be probed at all; then defect 3 must be resolved so a corpse's
   existence at probe time can be asserted. Only then can T153's corpse leg move
   off BLOCKED/FAILED.
2. **The five T152 skill opcodes.** Defect 2 means they cannot be probed by anyone
   until `activateSkill` has a caller — either a producer for `SkillActivateIntent`,
   or a hotbar that fills `buttonType`/`id`, or a real `EqActionHandler::useSkill`.
   That is one small unit, and it unblocks probes 5, 7 and 9 (ApplyPoison and
   Disarm outright; PickPocket still wants a Rogue).
3. **`ZoneProcessTrack` / `OP_Track`.** Unchanged from T150: no sender exists, and
   no tracking class exists either. Two gates, both still shut.
4. **A parser for `OP_RecipeDetails`.** The send is now proven and the reply is
   proven to arrive; T149 asked for it to be written against captured bytes, and
   those bytes are now obtainable in one session.
5. **Capture `OP_RecipeReply` and `OP_RecipeAutoCombine` into
   `tests/captured_packets.h`.** Both were parsed from real server data in this
   unit; T149 listed the capture as owed and it is now cheap.
6. **Defects 3, 4 and 5** above, each in its own territory.

