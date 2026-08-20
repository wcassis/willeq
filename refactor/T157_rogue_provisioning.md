# T157 — Provision a Rogue with the eq-oracle automation

**Slot: slot2 (for the record file only). x86. eqemu: ALREADY UP, lock ALREADY
HELD by the orchestrator. No Pi.**

**Do NOT run `scripts/eqemu/up.sh` or `down.sh`. Do NOT create, modify or delete
`/mnt/projects/fable/.eqemu-lock`.** The orchestrator holds it and takes the
stack down. T156 runs in parallel in slot1 and touches nothing you touch.

**Territory: `/mnt/projects/eq-oracle/` (its scripts and logs), the eqemu
database via GM commands, and `refactor/T157_rogue_provisioning.md` in slot2.
No production source, no tests.**

## Why

T155 reported PickPocket BLOCKED for want of a Rogue, and its ApplyPoison probe
can only ever observe `success=0` without one. The H05 oracle session
(2026-08-13) built character/account automation that drives the **official client
under Wine/Xvfb**, and T155 did not know it existed. This unit uses it.

Read `/mnt/projects/eq-oracle/env.sh` first (source it), then
`scripts/login-as.sh`, `scripts/create-character.sh`, and
`/mnt/projects/eq-oracle/notes/ui-automation-findings.md` if present. The
scoping doc is `refactor/H05_official_client_oracle.md`.

## The job

**1. A NEW ACCOUNT, not a new character on an existing one.** Concurrency is
**per-account, not per-character** — that is the whole reason H05 captured
account creation. `auto_create_accounts` is on, so `login-as.sh <name> <pw>` with
an unused account name creates it on first login. Suggested `willeq_rogue`;
record the exact name and password you used.

**2. Create the Rogue** with `create-character.sh <name> <race> <gender> Rogue
<deity> <city>`. Human is fine unless a race blocks the class.

**⚠ VERIFY THE RESULT IN THE DATABASE, not on screen.** The scripts' own header
records that blind clicking once produced an Ogre female when a Human male was
asked for, and that "Start Tutorial" can silently override the chosen start city.
`SELECT id, name, level, class, race, zone_id FROM character_data WHERE name=...`
— **class must be 9 (Rogue).** A screenshot is not acceptance.

**3. Grant the capability the two blocked probes need.** Both gates are read from
eqemu source, already checked:

- **PickPocket** needs `HasSkill(SkillPickPockets)`, and
  `Client::HasSkill` (`zone/client.cpp:3172`) is
  `GetSkill(skill) > 0 && CanHaveSkill(skill)`. **A fresh level-1 rogue may well
  have the skill at 0** — check, and if so raise it with `#setskill`. Being the
  right class is not sufficient.
- **ApplyPoison success** needs an item of `itemtype=42` whose `proclevel2` is
  **at or below the character's level**. Two usable rows already checked in this
  server's `items` table: **`52928` "Poison of the Huntsman", `proclevel2=0`** —
  works at level 1 — and `14015` "Spider Venom", `proclevel2=5`. Summon with
  `#si`. The handler **deletes the poison on every attempt, success or failure**
  (`client_packet.cpp:3065`), so summon several.

Verify each grant by reading it back (DB or the character's inventory), not from
the command's echo.

**4. Leave it usable and DOCUMENTED.** The deliverable is not "a rogue exists" —
it is that the next live unit can log in as it without rediscovering anything.
Record in the Review: account name and password, character name, level, class,
which skills you raised and to what, which item ids are in its inventory and in
which slots, its start zone, and the exact command line to bring it to character
select.

**Do NOT delete these characters or accounts.** H05's standing rule: they are
test capacity and we are short of it.

## Boundaries

- **No character creation by direct DB insert.** H05 established that no
  sanctioned command path creates a character from a script — that is precisely
  why the GUI automation exists. If the client cannot be driven, the answer is
  BLOCKED with evidence, not a hand-built row across a dozen tables.
- No pcap capture is needed; do not start one.
- Do not modify the other accounts or characters.
- The client at `/mnt/projects/EverQuestP1999` is **P1999 and may have a patched
  `eqgame.exe`** — irrelevant for provisioning (the DB row is the deliverable)
  but do not let anything you learn here be quoted later as protocol truth.

## Acceptance

A `character_data` row with `class=9`, its pick-pockets skill above 0, and poison
in its inventory — each shown as a query result in the Review.

## STOP conditions

- The Wine/Xvfb client will not reach character select after two honest attempts
  (report BLOCKED, with the screenshots the scripts already save under
  `${EQO_LOGS}/shots/`, and say exactly which step failed).
- A grant needs a server rule change or a rebuild.

## Review

(executor fills this in)
