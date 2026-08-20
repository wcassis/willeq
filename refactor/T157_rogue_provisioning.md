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

---

# Review (executor, 2026-08-20)

**ACCEPTANCE MET.** `character_data` row with `class=9`, pick-pockets skill 55,
four `itemtype=42` items in inventory. All three shown as query output below.

## The deliverable — everything the next live unit needs

| | |
|---|---|
| account | **`willeq_rogue`** (account id 78) |
| password | **`WillEqH03Pass`** (`EQ_TEST_PASSWORD` in `scripts/eqemu/env.sh` — same as every other test account) |
| account status | **250** (GM). Raised deliberately; `#setskill`/`#si` need it, and the next unit will need it too if it wants more poison. |
| character | **`Oraclerogue`** (character id 81) |
| class / race / gender | Rogue (9) / Human (1) / Male (0) |
| deity | Agnostic (396) |
| level | **10** |
| start zone | **North Qeynos, `zone_id=2`** — the chosen city, NOT the tutorial (see "Start Tutorial" below) |
| loc | x=167 y=30 z=-29.247 (Rogue guild, next to Hanns Krieghor) |
| left | camped cleanly to character select, then the client was stopped. Nothing linkdead, no session held. |

### The exact command line to bring it to character select

```bash
source /mnt/projects/eq-oracle/env.sh
/mnt/projects/eq-oracle/scripts/login-as.sh willeq_rogue          # password defaults to EQ_TEST_PASSWORD
/mnt/projects/eq-oracle/scripts/enter-world.sh                    # slot 1 is Oraclerogue; only character on the account
```

Both must run with `DISPLAY=:99`; `login-as.sh` starts Xvfb itself if that
display is absent. Each takes 3–6 minutes under llvmpipe. Screenshots land in
`${EQO_LOGS}/shots/login_willeq_rogue/`.

### Skills raised

| skill | id | before | after | why |
|---|---|---|---|---|
| Pick Pockets | **48** | 0 | **55** | `Client::HasSkill` gate for `OP_PickPocket` |

Nothing else was touched. Skills that came free with the class/level are
unchanged (1H Piercing 10, Sneak 10, Sense Heading 200, Throwing 10, …).

### Items and slots

| slot_id | item id | name | `itemtype` | `proclevel2` |
|---|---|---|---|---|
| **28** | 14015 | Spider Venom | 42 | 5 |
| **29** | 14015 | Spider Venom | 42 | 5 |
| **30** | 52928 | Poison of the Huntsman | 42 | 0 |
| **33 (cursor)** | 14015 | Spider Venom | 42 | 5 |

Slots 23–30 are the eight general inventory slots; **28/29/30 are the three
that were free**. Slot 33 is the cursor — see the deviation note. Untouched
starting gear: 13 Dagger* (9997), 14 Gloomingdeep Lantern (9979), 23 Skin of
Milk, 24 Bread Cakes*, 25 Napkin From Crow's, 26 Bandages*, 27 Backpack*.

**The handler deletes one poison per `OP_ApplyPoison`, success or failure**
(`client_packet.cpp:3065`), so four attempts is all that is stocked. To restock:
log in, `#si 14015`, then left-click a free general slot. The account is GM, so
this needs no further setup.

## Verifying queries (run after camping out, on the saved rows)

```
mysql_exec "SELECT id,name,level,class,race,gender,deity,zone_id,x,y,z,account_id
            FROM character_data WHERE name='Oraclerogue'"
        id: 81
      name: Oraclerogue
     level: 10
     class: 9          <-- Rogue
      race: 1          <-- Human
    gender: 0          <-- Male
     deity: 396
   zone_id: 2          <-- North Qeynos, not tutorialb
         x: 167   y: 30   z: -29.247
account_id: 78

mysql_exec "SELECT id,skill_id,value FROM character_skills WHERE id=81 AND skill_id=48"
id      skill_id        value
81      48              55           <-- Pick Pockets > 0

mysql_exec "SELECT i.slot_id,i.item_id,it.name,it.itemtype,it.proclevel2
            FROM inventory i JOIN items it ON it.id=i.item_id
            WHERE i.character_id=81 AND it.itemtype=42 ORDER BY i.slot_id"
slot_id item_id name                    itemtype  proclevel2
28      14015   Spider Venom            42        5
29      14015   Spider Venom            42        5
30      52928   Poison of the Huntsman  42        0
33      14015   Spider Venom            42        5

mysql_exec "SELECT id,name,status FROM account WHERE id=78"
78      willeq_rogue    250
```

## Findings — read these before the next PickPocket/ApplyPoison unit

### 1. The plan's level-1 assumption is wrong, and it is `#setskill` that breaks

The unit plan says a level-1 rogue may have Pick Pockets at 0 and to raise it
with `#setskill`. Raising it **at level 1 silently does nothing**:

```cpp
// zone/gm_commands/set/set_skill.cpp
t->SetSkill(skill_type,
            skill_value > t->MaxSkill(skill_type) ? t->MaxSkill(skill_type) : skill_value);
// client.h:940  MaxSkill(skill) == MaxSkill(skill, GetClass(), GetLevel())
```

`#setskill` clamps to the cap **at the character's current level**, and
`skill_caps` has no Pick Pockets row for class 9 below level 7:

```
SELECT skill_id,class_id,level,cap FROM skill_caps WHERE class_id=9 AND skill_id=48 AND level<=12
48  9  7  40      (nothing at all for levels 1-6, i.e. cap 0)
48  9  8  45
48  9  9  50
48  9 10  55
```

So `#setskill 48 55` on a level-1 rogue sets it to 0 and reports success.
**The character was raised to level 10 with `#level 10` first.** Level 10 also
puts Spider Venom (`proclevel2=5`) in range, which a level-1 rogue could not
apply.

Note the asymmetry that makes this easy to get wrong: `Client::HasSkill` calls
`CanHaveSkill`, which uses `RuleI(Character, MaxLevel)` — the cap at the
server's **max** level, not the character's. So the *gate* would pass at level
1; it is only the *granting* that silently fails.

### 2. Pick Pockets is skill **48**, not 35

`EQ::skills::SkillPickPockets = 48` (`common/skills.h`). 35 is `SkillPickLock`,
and it also has a level-gated cap for rogues, which makes it a convincing wrong
answer. Apply Poison is skill **6**, and its rogue cap starts at **level 18** —
it cannot be raised on this character, and does not need to be: the handler
only rolls `ChanceRoll < .75 + poison_skill/1000`, so skill 0 still succeeds
~75% of the time.

### 3. "Start Tutorial" did not fire, because the server has it disabled

`ui-automation-findings.md` #6 warns that leaving Start Tutorial alone lands the
character in `tutorialb` (189). That did not happen here — `zone_id=2`. The
reason is server-side and was set during the H05 baseline session
(`notes/willeq-session-handoff.md`): ruleset 1 carries
`World:EnableTutorialButton=false`, `World:TutorialZoneID=0`,
`World:MaxLevelForTutorial=0`. **Verify those rules are still in place before
trusting a start city on any future character**, and verify `zone_id` regardless.

### 4. `#si` puts the item on the CURSOR; clicking a bag SWAPS rather than inserts

There is no GM command that summons straight into inventory
(`SummonItemIntoInventory` exists in `zone/inventory.cpp:4581` but is exposed
only to Lua/Perl). `#si` → cursor → you must click a **free general slot** to
stow it. Clicking the cursor item onto the *Backpack* icon swapped the two
(poison into slot 27, backpack onto the cursor) rather than inserting into the
bag; it was swapped straight back. Right-clicking the backpack to open it never
worked, at either click duration — the bag window simply did not appear.

## What the automation needed to be coaxed into working

Less than expected. The H05 scripts had not been run since 2026-08-13 and
**worked unmodified** — no script was edited in this unit.

- `login-as.sh willeq_rogue` reached character select **on the first attempt**,
  twice (once to create the account, once after the GM grant). Xvfb was not
  running; the script started it itself. The Wine prefix, the writable client
  copy at `~/eq-oracle/client` and its `eqhost.txt` (→ `10.0.30.23:5998`) all
  survived intact.
- `create-character.sh Oraclerogue Human Male Rogue AGNOSTIC QEYNOS` succeeded
  on the first attempt with **no retries logged** for any pick — race, gender,
  class, deity, city and the OCR'd name all verified first time.
- `enter-world.sh` needed **two** attempts (its own retry loop handled it).
- Both long scripts take 5–10 minutes. Run them with `nohup … &` and poll in a
  bounded foreground loop; a single `timeout 580` foreground call is cutting it
  fine.
- The only genuine coaxing was the **Tip of the Day** dialog after zone-in: it
  needs a `jclick` on Close at (814,425) and its own OCR check. `enter-world.sh`
  does not dismiss it, and it sits over the middle of the screen. Everything
  after that (chat commands, the inventory window at `i`, clicking general
  slots) was ordinary `jclick`/`xdotool type` work.

Screenshots from this unit: `${EQO_LOGS}/shots/login_willeq_rogue/`,
`${EQO_LOGS}/shots/create_Oraclerogue/`, and `${EQO_LOGS}/shots/t157_*.png`
(char select, in-world, inventory with the three poisons, camped).

## Deviations

1. **Level 10, not level 1.** Forced by finding 1 — Pick Pockets cannot be
   granted below level 7. Documented above.
2. **Account status raised to 250 by a direct `UPDATE` on `account` id 78.**
   The GM commands the plan asks for (`#setskill`, `#si`) are status-gated
   (`set` 50 / sub-command `skill` 80, `summonitem` 150) and there is no in-game
   path to elevate an account that has no GM anywhere. This touches only the
   account this unit created. No other account or character was modified.
   A relog was required afterwards — world caches the account status at login.
3. **One poison sits on the cursor (slot 33).** All eight general slots were
   full after three poisons, and the backpack could not be opened. It is a real
   `inventory` row and `GetInv().GetItem(33)` resolves it, so it is usable; it
   will simply be attached to the mouse pointer at next login. Left-click any
   freed slot to stow it.
4. No character was created by DB insert, nothing was deleted, no production
   source or test was touched, and the eqemu lock was neither read as ours nor
   modified.
