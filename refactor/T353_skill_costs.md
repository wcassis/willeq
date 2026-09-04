# T353 — `endurance_cost` / `mana_cost` are 0 for every skill (B007): delete or load, by what the server enforces

Class B, slot3 after T350 (or slot1 after T352). No eqemu needed for the
code; a read of the server source decides.

1. Measure what eqemu enforces for skill use: in
   `/mnt/projects/akk-stack/code/zone/` grep the handlers for the skills
   fable's `SkillManager` gates (`Handle_OP_Taunt`, `Handle_OP_Kick`/
   `Bash`/`Backstab` via `OP_CombatAbility`, `Handle_OP_Hide`, `Sneak`,
   `Forage`, `Mend`, `BindWound`, `FeignDeath`, `Track`, `SenseHeading`,
   `ApplyPoison`, `Disarm`…): does the server check endurance/mana and
   refuse with a message, or does it charge and proceed? Cite each as an
   absolute path. Then the oracle: does the OFFICIAL client refuse
   locally before sending (T328/T342's rig can answer for one skill:
   with 0 endurance, does `/kick` produce an outbound `OP_CombatAbility`
   at all?) — if a session is cheap, do it; otherwise reason from the
   server source and say so.
2. Rule (the T324/T327 precedent: the server is authoritative and reports
   every outcome): if the server enforces, DELETE `endurance_cost`,
   `mana_cost` and both refusal branches in `SkillManager::getSkillUseError()`
   and `:859` (§5 shape, K12 ff check); the server's refusal message
   already reaches the chat. If the server does NOT enforce for some
   skill and the official client does, load the values from wherever
   the client gets them (there is no client-side skill-cost table in
   Titanium's files that we know of — measure; do not invent).
3. Tests: `test_skill_*` with assertions untouched except the ones that
   asserted the unreachable refusal (delete those with the branch, cite
   which).

Territory: `include/client/skill/skill_data.h`, `src/client/skill/skill_manager.cpp`,
tests, `refactor/BOARD.md`. No graphics files.

STOP: a skill the server does not enforce AND no data source exists —
board it as a class C (invent nothing).

---

## Review

**Branch `task/T353-skill-costs`, cut from `fable` at `210a9128`. x86 only. No
eqemu session — slot3 holds the lock; the decision is made from the server
SOURCE, and this section says exactly which lines.**

### 1. The enforcement table

Every opcode fable sends for a skill (`SkillManager::getSkillOpcode()`,
`src/client/skill/skill_manager.cpp:687-719`) traced to its eqemu handler. All
handlers are in `/mnt/projects/akk-stack/code/zone/client_packet.cpp`; the
registration table is `client_packet.cpp:125-389`.

| fable skill (id) | opcode | eqemu handler (absolute path:line) | checks endurance/mana? | refuses with a message? |
|---|---|---|---|---|
| Apply Poison (6) | `OP_ApplyPoison` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:3016` | **no** | no resource refusal |
| Backstab (8) | `OP_CombatAbility` | `client_packet.cpp:5076` -> `/mnt/projects/akk-stack/code/zone/special_attacks.cpp:325` `Client::OPCombatAbility` | **no** | "Ability recovery time not yet met." (timer only) |
| Bind Wound (9) | `OP_Bind_Wound` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:4013` | **no** | no resource refusal |
| Bash (10) | `OP_CombatAbility` | `special_attacks.cpp:325` | **no** | timer only |
| Disarm (16) | `OP_Disarm` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:5749` | **no** | no resource refusal |
| Disarm Traps (17) | `OP_DisarmTraps` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:5862` | **no** | no resource refusal |
| Feign Death (25) | `OP_FeignDeath` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:6415` | **no** | no resource refusal |
| Flying Kick (26) | `OP_CombatAbility` | `special_attacks.cpp:325` | **no** | timer only |
| Forage (27) | `OP_Forage` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:6493` | **no** | no resource refusal |
| Hide (29) | `OP_Hide` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:8874` | **no** | "Ability recovery time not yet met." (timer only) |
| Kick (30) | `OP_CombatAbility` | `special_attacks.cpp:325` | **no** | timer only |
| Mend (32) | `OP_Mend` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:10374` | **no** | timer only |
| Round Kick (38) | `OP_CombatAbility` | `special_attacks.cpp:325` | **no** | timer only |
| Sense Heading (40) | `OP_SenseHeading` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:13688` | **no** | no resource refusal |
| Sneak (42) | `OP_Sneak` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:14587` | **no** | timer only |
| Pick Pocket (48) | `OP_PickPocket` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:11913` | **no** | no resource refusal |
| Tracking (53) | `OP_Track` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:15266` | **no** | no resource refusal |
| Fishing (55) | `OP_Fishing` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:6472` | **no** | no resource refusal |
| Sense Traps (62) | `OP_SenseTraps` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:13710` | **no** | no resource refusal |
| Begging (67) | `OP_Begging` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:3937` | **no** | no resource refusal |
| Intimidation (71) | `OP_InstillDoubt` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:9095` | **no** | no resource refusal |
| Taunt (73) | `OP_Taunt` | `/mnt/projects/akk-stack/code/zone/client_packet.cpp:15215` | **no** | timer, no-combat-zone, TAUNT_TOO_FAR -- no resource refusal |

Method: `grep -in "endurance\|mana\|SetEndurance\|GetEndurance\|GetMana\|SetMana"`
over each handler's own line range returned **nothing, for every one of them.**

**The stronger result, tree-wide.** `grep -rn "SetEndurance\|GetEndurance()\|
endurance_cost\|EnduranceCost\|EndurUpkeep"` over
`/mnt/projects/akk-stack/code/zone/` finds endurance SPENT at exactly three
kinds of site, none of them a skill activation:

- `/mnt/projects/akk-stack/code/zone/client_packet.cpp:9765` --
  `Client::Handle_OP_Jump`, the only opcode handler in the server that spends
  endurance at all;
- `/mnt/projects/akk-stack/code/zone/effects.cpp:900` and
  `/mnt/projects/akk-stack/code/zone/bot.cpp:7965` -- **spell** `endurance_cost`
  (a `SPDat` column, not a skill);
- `/mnt/projects/akk-stack/code/zone/attack.cpp:3829` -- the
  `EnduranceAbsorbPercentDamage` spell bonus, spent when taking damage;
- plus regen (`client_process.cpp:1998`) and the buff upkeep drain
  (`client_process.cpp:2004-2039`).

So the answer to the unit's question — "does the server check and refuse, or
charge and proceed?" — is **neither, and that is the finding.** There is no
per-skill resource cost anywhere on the server side to enforce, because in this
era skills do not have one; resource costs are a **spell** property.

### 2. The oracle

**Not measured live** (slot3 holds the eqemu lock; this run is source-only, as
the contract permits). It does not change the decision, and the STOP does not
fire, because the STOP is conditioned on "the server does not enforce **AND**
the official client does". For the official client to refuse locally it would
need a per-skill cost to compare against, and there is no such table:

- no file under `config/` carries one (`skill_caps.json` is caps, not costs);
- no `endurance` string exists anywhere in `src/client/spell/`,
  `include/client/spell/` or `tools/willeq_db_builder.cpp` outside the
  never-assigned `SpellData::endurance_cost` declaration;
- `SkillData` was the only place in the tree naming a per-skill cost, and both
  fields were assigned at zero sites.

A local refusal with no cost to refuse on is not possible. **No class C entry is
owed.**

### 3. The decision: DELETE

Per the T324/T327 precedent (`refactor/T324_delete_spell_type_processor.md`,
`refactor/T327_delete_spell_effects.md`): the server is authoritative and
reports every outcome it refuses on. Here it refuses on none, so the two client
branches could only ever fire on an invented value — which is precisely what
board item B007 said not to introduce.

Deleted:

1. `include/client/skill/skill_data.h` — `SkillData::endurance_cost` and
   `SkillData::mana_cost` (the whole `// Costs` block, was ~55-57), replaced by
   a tombstone comment carrying the measurement and the absolute path to the
   server file, so a future reader does not re-add them from a guess.
2. `src/client/skill/skill_manager.cpp` — `getSkillUseError()`'s
   `if (!checkResourceRequirement(...)) { "Not enough endurance" /
   "Not enough mana" }` block (was ~314-322).
3. `src/client/skill/skill_manager.cpp` — the `checkResourceRequirement(skill_id) &&`
   conjunct in the `requires_behind` branch (was ~310). It existed only to keep
   T159's ordering correct (endurance out-ranking facing); with no resource
   failure left it out-ranks nothing, and facing is now the sole reason that
   message can be produced — which is what T159 asked for.
4. `src/client/skill/skill_manager.cpp` — `canUseSkill()`'s
   `if (!checkResourceRequirement(...)) return false;` (was ~235-238).
5. `src/client/skill/skill_manager.cpp` — the whole
   `SkillManager::checkResourceRequirement()` body (was ~851-878), now with no
   callers, plus its declaration in
   `include/client/skill/skill_manager.h` (was ~183-184).

**K12 (ff) publisher check, run first:** `checkResourceRequirement()` is a
`const` predicate returning `bool`. It publishes no `GameEventType`, pushes no
intent, queues no packet and calls no `EverQuest::Send*` — its only reads are
`GetCurrentEndurance()`/`GetCurrentMana()`. It is not the last (or any)
publisher of anything, so the zero-caller delete applies cleanly.

**Deliberately NOT touched:** `SpellData::mana_cost` /
`SpellData::endurance_cost` (`include/client/spell/spell_data.h:73-74`).
`mana_cost` is genuinely loaded (`src/client/spell/spell_database.cpp:73`) and
genuinely gated on (`src/client/spell/spell_manager.cpp:536-540`,
`src/client/combat_rounds.cpp:385`) — spells DO cost mana, on both sides of the
wire. Its sibling `SpellData::endurance_cost` is assigned nowhere, which is the
same shape one struct over and outside B007's territory; **boarded as new item
B062 on `refactor/BOARD.md` rather than deleted here** -- and unlike the skill
case it is a REAL question, because the server DOES enforce spell endurance
(`/mnt/projects/akk-stack/code/zone/effects.cpp:900`).

### 4. Tests

Deleted, with the branch it asserted on:

- `tests/test_skill_gates.cpp` —
  `SkillGateTest.T159_ABackstabShortOfEnduranceBlamesEnduranceNotFacing` (was
  ~483-512, the whole "B3" section). It forced
  `data->endurance_cost = 1000000` through the mutable accessor to reach a
  branch no shipped data could reach, and asserted
  `getSkillUseError(kBackstab) == "Not enough endurance"` — a message that no
  longer exists. Its own comment said so: "endurance_cost is 0 for every skill
  in initializeSkillData() today, so the resource gate can never fire from
  shipped data".
  Its surviving half — that facing IS reported when facing is the reason — is
  fully covered by `SkillGateTest.NEGATIVE_BackstabIsRefusedFromTheFrontOfTheTarget`
  (`tests/test_skill_gates.cpp:405`), which asserts exactly
  `getSkillUseError(kBackstab) == "You must be behind your target"` from the
  front. A tombstone comment replaces the section and names that test.

Every other assertion in `test_skill_gates.cpp`, `test_skill_gate_bypasses.cpp`,
`test_skill_possession_gate.cpp`, `test_skill_reachability.cpp`,
`test_skill_packet_bodies.cpp` and `test_train_skill.cpp` is untouched.

### 5. Verification

| Gate | Result |
|---|---|
| `cmake --build build -j24` | **exit 0.** First attempt was killed at the 10-minute tool timeout (exit 143); the post-143 protocol ran — two zero-byte binaries removed (`test_zone_load_surface`, `test_chat_component`), `ar t` member counts sane (`libwilleq_core.a` 321, `libEtcLib.a` 15), and an ELF-magic scan of `build/bin` caught **three more truncated-but-nonzero** binaries the zero-byte rule misses (`test_bridge_component`, `test_chat_channel_names`, `test_entity_size_change` — the last one made the first `ctest` abort at discovery with `Syntax error: ")" unexpected`). Removed and rebuilt; the scan is then clean and the build is exit 0 |
| `ctest -j6 --output-on-failure` | **3850 / 3852 passed.** Failures: `AudioManagerTest.PreloadCommonSounds` (the sanctioned ALSA one) and `HeadlessShutdownTest.NonLifecycleCommandIsHeldNotDroppedOrApplied` — **a load flake, not this unit**: it spawns the headless client, timed out with empty output under three busy slots, and **passes on its own** (`ctest -R ... ` exit 0, 5.07 s). Nothing it touches is skill-related |
| skill subset | `ctest -j6 -R "Skill\|skill"` — **103 / 103 passed**, exit 0 |
| `scripts/refactor/check_file_sizes.sh --all` | **880 ok, 0 warn, 0 FAIL** |
| gles2 / visual gate / ARM | **SKIPPED, with evidence.** `git diff --name-only` lists exactly four files, none under `src/client/graphics` or `include/client/graphics`: `include/client/skill/skill_data.h`, `include/client/skill/skill_manager.h`, `src/client/skill/skill_manager.cpp`, `tests/test_skill_gates.cpp`. The visual gate ran and passed inside the full `ctest` above |

### 6. Note for the next reader

The exit-143 recovery step in `refactor/EXECUTOR_PROTOCOL.md` says to delete
**zero-byte** files under `build/bin`/`build/lib`. That is not sufficient: a
kill during a link leaves files that are non-empty but not ELF, and those
survive the sweep, survive the incremental rebuild (they are newer than their
inputs), and then break `ctest` at gtest DISCOVERY — before a single test runs,
with an error that looks nothing like a build failure. The check that works is
an ELF-magic scan of `build/bin`. Worth folding into the protocol.
