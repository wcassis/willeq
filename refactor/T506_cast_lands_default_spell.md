# B484: the cast_lands scenario's default spell cannot be cast by its own default character

Card: kb-b484. Issue: #35. Class A. Lane: VM (172.16.200.99).

## The defect [measured, this card]

spells_new row 46 (Ultravision) has `classes12 = 255` — the MINIMUM level at
which class 12 (Wizard) may cast it; `classesN` across the table are minimum
levels per class (eqemu's own documentation: `code/common/spdat.h:1649`,
"Classes, and their min levels"). The shipped default (`SPELL=46` in
scripts/scenario.sh, `castSpellId = 46` in the client) ran Testcaster — class
12, level 1 (id 67, `gm = 0`) — below every class's minimum for spell 46 (min
over all 16 columns: 26).

### What the 255 actually means on this server

There is NO hard class/level block in the cast path: `#memspell` is accepted
verbatim (`Client::MemSpell` does not check `classes[]`), and the only
class-dependent gate is the fizzle roll in `Client::CheckFizzle`
(zone/spells.cpp). Two models exist behind `RuleB(Spells,
UseLegacyFizzleCode)` (default **false**):

- **legacy** (`UseLegacyFizzleCode = true`): `fizzle_chance = spell_difficulty
  + base_difficulty - caster_skill - prime_stat_reduction`, clamped to **2..95**,
  with `spell_difficulty = min_level * 5` (capped 255) and `min_level` the
  MINIMUM across all 16 class columns.
- **non-legacy**: `fizzle_chance = 10 - specialize + (par_skill +
  base_difficulty - act_skill)/5`, clamped to 1..95, where
  `par_skill = classes[class]*5 - 10` (capped 235) + `classes[class]` and
  `act_skill = skill + level + bonuses`.

Neither model is a hard block, and for this config's character they agree:
a level-1, skill-0 Wizard on spell 46 rolls a 95% fizzle under BOTH, lands ~1
in 20 per gem cast, and the 3-attempt lane passes ~1 in 7. Proof, measured on
this card, same server, same character, same spell 46, minutes apart:

| run | outcome | fizzle lines |
|-----|---------|--------------|
| RED (06:13, fable `59c526de`, shipped default) | FAIL, exit 1 | 3/3 `Your spell fizzles!` |
| RED2 (05:59, same spell 46 forced by hand) | PASS, exit 0 | 0 — `spell 46 landed ... 5.09 s after gem cast #1` |

Where the two models diverge matters for the pre-flight's wording, not its
verdict: at level 60/skill 200 the legacy model's fizzle collapses to its 2%
floor (`130 + 25 − 200 − (INT−75)/10 < 2`) while the non-legacy one stays at
~61% (`10 + (490 + 25 − 260)/5`). T449's "level 60, skills 200, fizzled 3/3"
fits neither exactly (legacy would land ~98% of casts, non-legacy ~39% of
runs) — but both models agree a caster far UNDER a spell's class minimum is
the fizzle band, which is exactly the pre-flight's guard.

## The fix

1. **Default is 288 everywhere the lane reads one** (all three, so a direct
   `willeq --scenario cast_lands` and `scripts/scenario.sh` agree):
   - `scripts/scenario.sh`: `SPELL=288` (Minor Shielding, `classes12 = 1`,
     the spell T449 proved lands and this card re-proved);
   - `include/client/scenario/scenario.h`: `castSpellId = 288`;
   - `include/client/application.h`: `scenarioSpellId = 288`;
   - `src/client/application_config.cpp`: the `--help` line names the new
     default.
2. **A comment is not a guard, so the rig asserts at run time.** Before the
   client launches, `scripts/scenario.sh` (cast_lands only, after the
   snapshots so a refusal restores everything) queries the database for the
   config's character's class and level and for `spells_new.classes<N>` of
   the spell it is about to cast. If the character's level is below the class
   minimum (which always includes the 255 sentinel — no character can reach
   level 255), the run fails fast with **exit 72** and an operator-readable
   line naming the spell, the class and the level — instead of three 20-second
   fizzle windows:

   `spell 46 has no minimum level for Wizard (spells_new.classes12 = 255, the
   table's 255 sentinel); Testcaster (level 1) cannot cast it reliably -- pick
   a spell this class carries, e.g. --spell 288 (Minor Shielding)`

   A missing character row is the existing operator code 64; a spell with no
   spells_new row, or a class outside 1–16, is 72, because in both cases the
   spell simply cannot run.
3. **The state-machine tests follow the default** (`tests/
   test_scenario_state_machines.cpp`): the four cast_lands cases that pinned
   46 now pin 288, and
   `CastLandsScenario.TheDefaultSpellIsOneTheConfigClassCanCast` asserts
   `DefaultOptions().castSpellId == 288`, so a future "cleanup" back to an
   uncastable default is a red test, not a fizzle loop.

## Verification [all measured on the VM, 2026-09-19]

- `test_scenario_state_machines`: 30 tests, all pass (includes the new case).
- Shipped lane: `scenario.sh cast_lands --timeout 240` → **exit 0**;
  pre-flight prints `castability pre-flight: spell 288 is castable by
  Testcaster (Wizard, level 1; minimum level 1)` and the verdict is
  `spell 288 landed on Testcaster 2.57837s after gem cast #1 (buff slots 0 ->
  1)`.
- `scenario.sh cast_lands --spell 46` → **exit 72** in under a minute, with
  `spell 46 has no minimum level for Wizard (spells_new.classes12 = 255, the
  table's 255 sentinel); Testcaster (level 1) cannot cast it reliably -- pick
  a spell this class carries, e.g. --spell 288 (Minor Shielding)`; no client
  launched, restore byte-identical, lock released.
- Evidence, committed under `refactor/B484/`: `red_run.log` +
  `red_cast_lands_client.log` (the RED fail, 3 fizzle lines, exit 1);
  `red2_run.log` + `red2_cast_lands_client.log` (the same spell 46 landing 1/3
  — the 95% cap, not a block); `green_run.log` +
  `green_cast_lands_client.log` (the shipped lane, exit 0); `spell46_run.log`
  (the fail-fast, exit 72).
- Stack left DOWN, lock released (`eqemu_lock_show` empty after every run;
  `eqemu_stack_ready` false).

Closes #35. Closes B484.
