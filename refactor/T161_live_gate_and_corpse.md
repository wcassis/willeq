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

(executor fills this in)
