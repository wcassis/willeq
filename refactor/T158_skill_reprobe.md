# T158 — Re-probe T152's five skill opcodes over `/useskill`

**Slot: slot1. x86. eqemu: ALREADY UP, lock ALREADY HELD by the orchestrator.
No Pi.**

**Do NOT run `scripts/eqemu/up.sh` or `down.sh`, and do NOT touch
`/mnt/projects/fable/.eqemu-lock`.** If the zone dies, restart just the zone and
say so — one probe here can legitimately kill it.

**Territory: `refactor/T158_skill_reprobe.md`,
`/mnt/projects/fable/review/T158/`, `data/captures/` (gitignored), and — ONLY if
a probe proves it broken — the body of the sender under probe. No action-layer
restructuring.**

T152 fixed five skill packet bodies against eqemu source. T155 then found none of
them could be reached. T156 landed `/useskill <id|name>` (alias `/skill`), which
is scriptable. **This unit closes the loop: the five bodies, on the wire, driven
the way a user would drive them.** Read T152's and T155's Reviews first.

## The evidence rule — unchanged and non-negotiable

Every probe is **VERIFIED** (external evidence from the server log, the DB, or a
second client, quoted), **BLOCKED** (with the exact gate), or **FAILED** (with the
defect). **No local "sent OK" line counts.** Do not upgrade a BLOCKED to a
VERIFIED by reasoning from source — that habit is what created this debt.

## The character

**`Oraclerogue`** (character 81, account **`willeq_rogue` / `WillEqH03Pass`**,
status 250), Human male Rogue, **level 10**, North Qeynos (`zone_id=2`), Pick
Pockets (skill **48**) = 55, poison in inventory slots 28/29/30 and 33 (cursor).
Provisioned by T157 — read its Review before touching skills.

**`Oraclerogue` is a scratch character.** Level and skill changes are expected and
do NOT need restoring; just record its final state. **Do not modify any other
character or account.**

Point a willeq config at it the way `data/captures/` configs already do for
`Testmelee`/`Testbuddy`.

## ⚠ Two gates the orchestrator already checked — do not rediscover them

**1. Our OWN client will refuse Apply Poison before the packet is built.**
`SkillManager::canUseSkill` (`skill_manager.cpp:150`) requires `skill.hasSkill()`,
and Apply Poison (skill **6**) is **0** on this character — the rogue cap starts
at level **18**, which is why T157 could not raise it at level 10. eqemu itself
does not care (its roll is `< .75 + skill/1000`, so skill 0 succeeds ~75%), but
our gate is stricter and fires first.

**Authorised fix, in this order:** `#level 18` (or 20) → `#setskill 6 <cap>` →
**read the value back from `character_skills`**. T157's trap: **`#setskill`
clamps to the cap at the character's CURRENT level and reports success anyway**,
so a grant before the level-up silently sets 0. Verify, never trust the echo.

If Apply Poison still refuses after a verified non-zero skill, **that is a FAILED
with a client-side defect** — report it, do not work around it.

**2. PickPocket has a self-incrimination hazard.** `Handle_OP_PickPocket`
(`client_packet.cpp:11913`) writes a **`POSSIBLE_HACK` player-event log** against
the player when the attempt is out of range (>20 units) or repeats inside the
8-second `pTimerBeggingPickPocket`. The victim must be an **NPC**, not a pet, not
a client, and of body type Humanoid/Monster/Giant/Lycanthrope. **Walk into range
first, one attempt per 8+ seconds, and confirm the target is an NPC before
sending.** Do not brute-force it.

## The five

| skill | id | prior state |
|---|---|---|
| Apply Poison | 6 | never reached; needs gate 1 above |
| Bind Wound | 9 | body VERIFIED by T155 via `/bandage` — **control**, must still pass via `/useskill` |
| Taunt | 73 | VERIFIED by T155 via `/taunt` — **control** |
| Disarm | — | never verified at all |
| Pick Pockets | 48 | never reached; needs gate 2 above |

**Run the two controls FIRST.** They have known-good wire evidence from T155
through a different command, so if they fail via `/useskill` the defect is in
T156's routing, not in T152's bodies — and you want to know that before spending
the rogue-only probes. Say explicitly in the Review which layer each result
exonerates.

Taunt and Disarm need a living NPC target in range; North Qeynos has humanoids.

## Acceptance

Five rows, each VERIFIED / BLOCKED / FAILED with external evidence. **Also state
plainly whether `/useskill` itself works end to end** — that is T156's acceptance
and it has never been exercised against a real server.

## Method

T148's: `build/bin/willeq` headless, `-d 4 --log-level=DEBUG --script <file>`,
launched from a single **FOREGROUND** driver that `wait`s on every client.
Evidence into `/mnt/projects/fable/review/T158/` with a README.

## Verification

Build + full suite in the slot before probing — a stale binary invalidates every
result. Baseline **2570/2571** at tip `3ac315e6`, sole expected failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`.

## STOP conditions

- A defect is outside the probed sender's own body (report it, do not fix it).
- The zone server dies twice in the same probe.
- A probe would need a character beyond `Oraclerogue`'s level/skill changes.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T158-skill-reprobe`, off `fable` tip `3ac315e6`. eqemu was already up and
the lock already held by the orchestrator: `up.sh`/`down.sh` were never run,
`.eqemu-lock` was never touched, no eqemu source or config was patched, and the
zone process never died. `main` and the sibling slots were never touched. Full
evidence with a README: `/mnt/projects/fable/review/T158/`; the quoted line
numbers below all point into `review/T158/evidence_annotated.md`.

**Build and suite before probing**: `cmake --build build -j24` clean;
`ctest --output-on-failure` **2570/2571**, sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds` — the plan's baseline exactly. No source
was changed by this unit, so no second run was owed.

Four headless sessions, each from a single foreground driver that `wait`s on its
client, `-d 4 --log-level=DEBUG --script`. Scripts, configs and logs in
`data/captures/` (gitignored), copies in the review directory.

### Headline: five VERIFIED, none BLOCKED, none FAILED — and `/useskill` works

**`/useskill` works end to end against a real server.** That is T156's
acceptance and it had never been exercised live. Every one of the five opcodes
was driven twice, once by skill **name** and once by numeric **id**, and all ten
sends produced an externally observable server effect. T156's name resolution,
its target plumbing through `EqActionHandler`, and T152's five packet bodies are
all now proven on the wire rather than argued from source.

### The matrix

| # | skill | id | command(s) | verdict | external evidence |
|---|---|---|---|---|---|
| 1 | **Bind Wound** (control) | 9 | `/useskill bind wound`, `/useskill 9` | **VERIFIED** | two server `OP_Bind_Wound` replies per send (`Unhandled zone opcode: HC_OP_BindWound`, `t158_run1_a.log:1702,2084` and `:2288,2711`) — the second of each pair is the +10.0 s `bindwound_timer.Check()` completion, which cannot fire unless the start branch armed it — **plus two `Bandages*` charges consumed in the server's database**, slot 26 item 21779: **20 → 18** (`db_before.txt` / `db_after.txt`) |
| 2 | **Taunt** (control) | 73 | `/useskill taunt`, `/useskill 73` | **VERIFIED** (on `Testmelee`, see below) | `FAILED_TAUNT` string 5811 `You have failed to taunt your target.` (`t158_run2_a.log:686`), then on the second send the `SUCCESSFUL_TAUNT` broadcast naming the taunter — `Probetaunt says 'I'll teach you to interfere with me Testmelee.'` (`:733`) — followed by the NPC attacking (`:741`). T155's exact evidence string, reached through `/useskill` instead of `/taunt` |
| 3 | **Pick Pockets** | 48 | `/useskill pick pocket`, `/useskill 48` | **VERIFIED** | server `OP_PickPocket` response, **86 bytes**, +21 ms and +73 ms after the two sends (`t158_run3_a.log:3510,5161`). **Zero `POSSIBLE_HACK` player events filed** — `player_event_logs` contains none at all (`db_after.txt`) |
| 4 | **Disarm** | 16 | `/useskill disarm`, `/useskill 16` | **VERIFIED** | `DISARM_FAILED` string 12891 `Your attempt to disarm failed.` on both sends (`t158_run3_a.log:4045,5833`) — that string lives **inside** the branch guarded by `disarm->skill == GetSkill(SkillDisarm)` (`client_packet.cpp:5804`), so it proves the skill field matched — **plus** aggro on a never-attacked NPC 0.03 s later, `Probemark hits YOU for 1 points of damage.` (`:4060`), which is `AddToHateList` at `:5811` in the same branch. `DISARM_NO_TARGET`, the tell for a wrong `skill` value, never appeared, and with server-side `Skills` logging at Debug 3 there is **no** `Size mismatch for Disarm_Struct packet` line either |
| 5 | **Apply Poison** | 6 | `/useskill apply poison`, `/useskill 6` | **VERIFIED** | server echo `ApplyPoison response: slot=27, success=1` and `slot=28, success=1` (`t158_run3_a.log:4676,6555`); server item deletion `Item deleted from slot 27/28 via MoveItem (to=-1)` (`:4675,6554`); **the server's own log file**, `[Zone] [Skills] Skill [6] at value [0] cannot increase due to maxmum [0]` at `13:03:00` and `13:03:38` (`server_zone_qeynos2_268.log`), a line that only exists inside `if (IsPoison && GetClass() == Class::Rogue)`; and **two poison rows gone from the database**, server slots 28 and 29 (`db_before.txt` / `db_after.txt`) |

### Which layer each result exonerates

The plan's ordering was the right call and it paid off, though not the way it
expected — nothing had to be diagnosed by elimination because nothing failed.

* **Rows 1 and 2 exonerate T156's routing.** Both had known-good T155 wire
  evidence through a *different* command (`/bandage`, `/taunt`). Reaching the
  same server effects through `/useskill` proves the whole new chain —
  `CommandProcessor::cmdUseSkill` → name/id resolution → `ActionDispatcher` →
  `EqActionHandler::useSkill` → target resolution from `CombatManager` →
  `SkillManager::activateSkill` → `sendBodiedSkillPacket` — carries a skill id
  and a target id intact to a sender that was already known to work. Row 1 also
  exonerates the *self-target* path (target 0 → `GetMySpawnID()`), row 2 the
  *explicit-target* path.
* **Rows 3, 4 and 5 exonerate T152's bodies**, which is the only thing the
  controls could not have told us. These three senders had never once produced
  their intended server effect. `MakeDisarmBody` and `MakePickPocketBody` had no
  authority stronger than a source reading (T152's own deviation 2); they now
  have observed server behaviour. `SendApplyPoison` had zero callers since it
  was written; it works, and its inventory-slot handling round-trips.
* **Rows 4 and 5 additionally exonerate two specific fields** that were the
  likeliest silent failures: Disarm's `skill` (proven by which of the two
  possible messages came back) and ApplyPoison's `inventorySlot` (proven by the
  echo matching what was sent *and* by the correct server-side rows vanishing).

### Two things T152 asked for that fell out for free

1. **T151's open item 2 is closed.** The `OP_ApplyPoison` echo decoded as
   `success=1`, twice, which is the only value the handler can produce for a
   level-10 rogue applying a `proclevel2=5` poison. `success` really does sit
   where `ZoneProcessApplyPoison` reads it.
2. **The slot translation is proven in both directions.** The client sent
   Titanium slot 27 and got 27 back; sent 28, got 28 back; and the rows that
   disappeared are *server* slots 28 and 29. `TitaniumToServerSlot` on the way
   in and `ServerToTitaniumSlot` on the way out, both exercised, both
   consistent. T152's "the slot numbering needs no translation" is now
   observation, not inference.

### The plan's gate 1 is wrong, and the reason is a defect

The plan states that our own client refuses Apply Poison before building the
packet, because `SkillManager::canUseSkill` requires `skill.hasSkill()` and
skill 6 is 0 on this character; and it authorises `#level 18` + `#setskill 6`
to get around it.

**None of that was necessary and none of it was done.** `Oraclerogue` is still
level 10 with Apply Poison at 0, and Apply Poison worked first try. The reason
is defect 1 below: `SkillData::hasSkill()` does not look at the skill's value at
all. The gate the plan was routing around does not exist.

The authorised `#level`/`#setskill` sequence was therefore **not run**, which is
the strictly smaller change and leaves T157's character exactly as provisioned.

### Defects found

**1. `SkillData::hasSkill()` ignores the skill value and the class, so the
client sends skill packets the character cannot possibly use.**

```cpp
// include/client/skill/skill_data.h:101
bool hasSkill() const {
    // Show all valid skills (0-74) regardless of current value
    return skill_id < 75 && name != "Unknown";
}
```

The comment is honest about what it does and it is defensible for the *skills
window*, which is the caller it was written for (`getAllSkills`,
`getSkillCount`). But `canUseSkill()` (`skill_manager.cpp:157`) uses the same
predicate as its "Must have the skill" check, and `getSkillUseError()`
(`:201`) uses it to produce "You do not have this skill" — a string that can
never be reached. So **`activateSkill` has no skill-possession gate whatsoever**.

Observed, not inferred (`t158_run4_a.log:2461-2467`): `Oraclerogue`, a Rogue
with Taunt 0, targeted an NPC, ran `/useskill taunt`, the client emitted
`Sent Taunt packet: target=276`, told the player **"You use Taunt!"**, and the
server discarded it in silence at `Handle_OP_Taunt`'s
`HasSkill(EQ::skills::SkillTaunt)` guard. `skill_caps` has no row for
`class_id=9, skill_id=73` at any level, so a Rogue can never taunt. The
contrast is exact: `Testmelee` got a server string back in **52 ms** for the
identical command (run 2).

The user-visible symptom is a skill that reports success and does nothing. The
fix is a value/cap-aware predicate for the activation path (the data is already
there — `current_value` and `max_value` are populated from the PlayerProfile),
keeping the permissive predicate for the UI listing.

**Not fixed**: this is in `SkillData`/`canUseSkill`, not in the body of any
sender a probe proved broken. Reported per the unit's grant and the first STOP
condition.

**2. `SkillManager::loadClassSkillCaps()` is a stub that invents caps for eight
skills and zeroes the rest.** `skill_manager.cpp:47-71` carries a `TODO: Load
actual class skill caps from data` and hard-codes `max_value` for skills
15/19/33/50/40/9/66/67 as level multiples, for every class. `updateAllSkills`
then patches `max_value = current_value` for any skill whose profile value is
non-zero. This is the data defect underneath defect 1 — there is no real cap
table client-side, which is presumably why `hasSkill()` was written to ignore
caps in the first place. Reported, not chased.

**3. `getSkillUseError()` returns "You must be behind your target" for any skill
with `requires_behind`, unconditionally** (`skill_manager.cpp:229-231`), before
the resource check, and regardless of actual facing — while
`checkPositionRequirement()` (`:688`) always returns true, so `canUseSkill()`
never fails for that reason. Only Backstab sets the flag, so it is latent today.
Cosmetic, out of territory, reported.

### Fixes made: none, and why that is the correct outcome

The grant is "ONLY the body of a sender a probe proves broken". No probe proved
a sender's own body broken — all five passed on the first attempt. The three
defects above are all one layer above the senders, in the activation gate and
its data. The tree is unchanged apart from this Review and the gitignored
`data/captures/` files, so the pre-probe suite result (2570/2571) still stands.

### `Oraclerogue`'s final state

Not restored — it is a scratch character and the plan says so. Recorded:

| what | before | after |
|---|---|---|
| level / class / race / zone | 10 / Rogue (9) / Human (1) / 2 (North Qeynos) | **unchanged** |
| position | x=167 y=30 z=-29.247 | x=167 y=30 z=**-29.125** (settled to floor) |
| `cur_hp` | 233 | **233** (full; it was hit for 1 s of damage repeatedly and regenerated) |
| corpses | 0 | **0** — it never died |
| account `willeq_rogue` status | 250 | **250** (untouched) |
| skill 6 Apply Poison | 0 | **0** |
| skill 9 Bind Wound | 1 | **1** |
| skill 16 Disarm | 1 | **1** |
| skill 48 Pick Pockets | 55 | **55** |
| skill 73 Taunt | 0 | **0** |
| skill 15 Defense | 5 | **9** | 
| skill 19 Dodge | 1 | **7** |
| skill 34 Parry | 1 | **5** (its level-10 cap) |
| `Bandages*` (item 21779, slot 26) | 20 charges | **18** — two bind wounds |
| Spider Venom, server slots 28 and 29 | present | **gone** — two apply-poisons |
| Poison of the Huntsman (slot 30), Spider Venom (slot 33, cursor) | present | **present** — two applications left in stock |

The Defense/Dodge/Parry rises are incidental: the Disarm probe aggroed the
target NPC (which is the *point* of that probe's evidence) and it kept swinging
for the rest of the session.

Nothing else on the server was modified. `logsys_categories`, `rule_values`,
`command_settings` and every other character and account are untouched.

### Deviations

1. **The Taunt control was run on `Testmelee`, not `Oraclerogue`.** It had to
   be: `skill_caps` has no row for `class_id=9, skill_id=73` at any level, so
   `Handle_OP_Taunt`'s `HasSkill` guard rejects a Rogue's taunt forever and the
   probe could only ever have returned "the server said nothing", which by this
   unit's own rule is not evidence. `Testmelee` is T155's Taunt character, in
   T155's zone, using T155's `#spawn` method — only the command changed, which
   is exactly what makes it a control for T156's routing. Taunting modifies
   nothing persistent; `Testmelee` took 5 points of NPC melee damage from the
   NPC it taunted and regenerated. No other character was touched. The rogue's
   own taunt was then run separately as run 4 and produced defect 1.
2. **Server-side debug logging was enabled from inside the game.** T155
   recorded `#logs` as refused on the status-200 `willeq_test` account.
   `command_settings.access` for `logs` is **250** and T157's `willeq_rogue` is
   exactly 250, so run 3 ran `#logs set gmsay|file 22 3` (Skills) and `9 3`
   (Debug). This is in-memory, one zone process, for the lifetime of that
   process — `logsys_categories` was not written and `#logs reload` was not run.
   It is what turned Apply Poison's evidence from "an echo we parsed" into "a
   line in the server's own log", and it removes T155's deviation 1 as a
   permanent obstacle: **the rogue account can do this, the test account
   cannot.**
3. **Three transient `#spawn` NPCs were created and left standing**:
   `Probemark000` and `Probetauntb000` in qeynos2, `Probetaunt000` in arena.
   As T155 established, `NPC::SpawnNPC` is in-memory only — no database rows —
   and they vanish with their zone processes. `Probemark000` is still aggroed on
   a departed `Oraclerogue`. Not despawned, for T155's reason: `#depop` aims at
   the current target and cannot be aimed reliably at a name that
   substring-matches an earlier entity.
4. **The plan's gate 1 was not exercised.** `#level 18` and `#setskill 6` were
   authorised and are not in this unit's evidence, because Apply Poison worked
   at level 10 with skill 0. Not a shortcut — the smaller change, and the reason
   is written up above as defect 1 rather than passed over.
5. **A `#spawn` name with a digit silently fails.** The first attempt at run 4
   used `Probetaunt2`; the server echoed the `ChannelMessageReceived` line but
   spawned nothing, and `/target` reported `No target found matching
   'Probetaunt2'` (the run was repeated with `Probetauntb`). Recorded because it
   costs a probe cycle and the failure is silent from the client's side.
6. **`grep` needs `-a` on these logs.** The client logs contain ANSI colour
   bytes that make `file(1)` call them "Non-ISO extended-ASCII text", so plain
   `grep` treats them as binary and prints nothing at all — not an error, just
   silence. That is an easy way to conclude "the command never ran" when it did.
7. **Only one client was used per session.** The plan allows a second client as
   an evidence source; none of the five probes needed one, because every result
   had either a server-originated string, a server packet reply, a server log
   line, or a database row behind it.

### STOP conditions

None was hit. The zone process never died (not once, let alone twice in a
probe), no probe needed a character beyond `Oraclerogue`'s own level/skill
changes — and in the end it did not even need those — and the three defects
found are all outside the probed senders' bodies and are reported rather than
fixed, which is the first STOP condition working as designed.

**T152's probe 2 was deliberately not completed.** T152 asked for the *new*
Bind Wound path first (done, row 1, and the zone stayed up), and said the
null-`pBuffer` deref should only be confirmed "on a server nobody else is using
and expect to restart it". The server was shared with the orchestrator's lock
this session, so the destructive half was not attempted. It remains an unproven
source reading and is owed.

### Still owed

1. **Defect 1** — a skill-possession gate that actually gates. This is the
   user-visible one: any skill the character's class cannot have reports
   "You use X!" and does nothing.
2. **Defect 2** — a real client-side skill cap table, which is what defect 1's
   fix needs to stand on.
3. **T152's null-`pBuffer` confirmation** (its probe 2, destructive half), on a
   server nobody else is using.
4. **The keyboard hotbar path** — T156's finding 1,
   `InputActionBridge::processHotbarRequests` still turns every hotbar key into
   `castSpell(slot + 1)`. `/useskill` and the mouse hotbar now work; the
   keyboard hotbar still cannot activate a skill.
5. **`Unhandled zone opcode: HC_OP_BindWound`** — the reply arrives four times
   per bind-wound pair and nothing parses it. Its `type` field carries the
   server's outcome (3 unlock, 1 done, 4 target dead, 5 out of zone) and the
   client shows the player none of it.
6. **`character_skills` for `Oraclerogue` shows Apply Poison and Taunt at 0
   forever**, so any future probe of those two on this character is proving the
   handler's *ungated* path. That is fine for Apply Poison (eqemu genuinely does
   not gate it) and impossible for Taunt (a Rogue never can). A Taunt probe
   needs a warrior; that is a permanent fact, not a gap.
