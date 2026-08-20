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

(executor fills this in)
