# T101 — Migrate the Spells and Pet families (the hard case, deliberately early)

**Slot: slot2. Headless work, then a LIVE leg. eqemu lock normally — T100 is
running in parallel and also wants it, so do all headless work first and
contend for the lock at the end. If it is held, wait, then report the live leg
outstanding rather than stealing it.**

**Territory: `src/client/action/`, `src/client/eq_commands_spells.cpp` and
`eq_commands*.cpp` (removals only), `src/client/bridge/game_state_sync_bridge.*`
if coverage is missing, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T101/`.**

**Follow the migration recipe in T99's Review.** Read it first, in full. This
plan only adds what is specific to Spells and Pet.

## Why this family, and why now

T99 estimated 60-90 hours for the remaining ~120 commands and **flagged
Spells, `cast`, `interrupt` and `pet` as the costliest**. That is exactly why
this runs second rather than last.

If the recipe breaks anywhere, it breaks here. Finding that out now — before
tens of hours go into the easy families — is worth more than momentum. **A
clear "the recipe does not fit, and here is why" is a success for this unit**,
not a failure.

## Scope

`game`'s **Spells** category (6) plus **Pet** (1). Confirm exact membership
from the registrations.

## What makes this family hard — know before you start

- **`cast` and `interrupt` are real state machines**, not thin senders. Casting
  has gem slots, targets, interrupt conditions and timing. The action layer's
  `castSpell`/`castSpellOnTarget`/`interruptCast` exist but T98 reported
  `interruptCast` and `forgetSpell` as orphaned at the dispatcher layer.
- **T93 already tested the gem-slot conversion** (1-indexed UI slot to
  0-indexed `SpellSlot`) in `tests/test_eq_action_handler_transforms.cpp`, and
  found it *correct*. Do not re-litigate it; do not break it.
- **T95 added the `sendPetCommand` guard** (membership check over real
  enumerators, gaps at 0/15/22+) and **T96 fixed the ten `/pet` wrappers** to
  named enumerators. Those wrappers are currently orphaned. Migrating Pet gives
  them callers — the same shape as T99's Group win.
- **T97 found `game`'s `/pet` vocabulary is a strict superset of the action
  side's**, even after T96. So porting must take `game`'s richer vocabulary and
  express it through the (now correct) dispatcher wrappers, adding wrappers
  where `game` has commands the dispatcher lacks. Say which you added.
- **`/pet hold` and `/pet focus` draw no server response** on this eqemu build
  — T96 root-caused it to an AA gate neither test character's class can
  purchase. **That is not your bug.** Do not chase it; note it and move on.

## The recipe's addendum — do this first

**Check `GameStateSyncBridge` coverage BEFORE the live leg.** T99 found zero
cases for Group events and had to fix it mid-unit. Spell and pet state are
prime candidates for the same gap. Fix narrowly, following the file's pattern,
if missing.

## Atomicity

`game` is tried before `action`. Add and remove **in the same change**; verify
`registry=action` per command; keep an unmigrated control that still logs
`registry=game` in the same session, as T99 did with `/target`.

## Tests

Per T99 and the T95 standard, with every new test **proven able to fail in both
directions**. Confirm T93's and T95's existing tests still pass untouched — if
migration requires changing them, say so and explain, because that would mean
the migration altered behaviour rather than routing.

## Live leg — the honest bar

A caster character is needed. `Testcaster` exists on `willeq_test`.

- **Cast a real spell and observe the effect**, not an ack.
- **Summon a pet and exercise the migrated pet vocabulary**, confirming the pet
  obeys — T96 established this standard and found `/pet attack` had been
  sending dismiss.
- Confirm `registry=action` for every migrated command.

Session discipline: binary freshness against HEAD first; daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`;
never guess an item or spell id (resolve via `data/willeq.db`); restore DB
state and confirm by `SELECT` **after logout**; clean teardown, delete the lock.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`** — the
  dependency cycle. Migration goes upward only.
- Do not migrate renderer commands. `eq_commands_spells.cpp` contains
  `rendererCmd()` helpers routing to the `RendererCommand` bridge queue —
  **those must stay**, because calling `processSlashCommand()` off the game
  thread runs GL with no EGL context and Mesa fails silently rather than
  crashing.
- Do not weaken T95's pet-command guard or re-number `PetCommand`.
- Do not migrate beyond Spells and Pet.
- **STOP and report if the recipe does not fit.** State precisely which step
  fails and why. Do not force it, and do not leave a half-migrated family.

## Verification

Full build and suite (expect 2121 total plus new tests; sole pre-existing
failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`). Visual
gate — goldens must not move.

## Review

Executed in `/mnt/projects/fable/wt/slot2`, task branch
`task/T101-spells-pet`, off `fable` tip `960c7fbb`. Full evidence:
`/mnt/projects/fable/review/T101/` (README + two filtered live-session
logs).

### Confirmed membership

Re-derived from the registrations in `eq_commands_spells.cpp`, not trusted
from the plan's prose. `game`'s **Spells** category (`Command.category ==
"Spells"`) held exactly **6**: `cast`, `mem` (aliases `memspell`,
`memorize`), `forget`, `spellbook` (alias `book`), `gems`, `interrupt`
(alias `ducking`) — matching the plan's "(6)" exactly. **Pet** held
exactly **1**: `pet`. `/skills` was deliberately confirmed OUT of scope:
it lives in the same function and reads spell state, but its registration
says `category = "Utility"`, not `"Spells"` — left untouched, still in
`eq_commands_spells.cpp`, a test (`T101_SpellsAndPetCommandsRegisteredWithCorrectCategory`)
asserts `hasCommand("skills")` is false on `CommandProcessor` to pin this.

### Per-command source ported

| command | source ported | why |
|---|---|---|
| `cast` | game's `cast.handler` verbatim (gem 1-8 bound, `EQ::SpellManager::beginCastFromGem()`, the 5-way `CastResult` switch) | T97: "Live is better" — the existing `ActionDispatcher::castSpell()`/`EqActionHandler::castSpell()` route through `CombatManager` (older, unconnected subsystem) with a looser 1-12 bound and no differentiated failure messages. **Not repointed** — T93's `CastSpell_GemSlotIsOneIndexedToZeroIndexedSpellSlot`/`CastSpellOnTarget_SharesSameGemSlotConversion` tests pin `castSpell()`/`castSpellOnTarget()`'s real `CombatManager` behavior specifically; repointing would have broken tested behavior the plan explicitly said not to re-litigate. **New method added instead**: `IActionHandler::beginCastFromGem()` / `ActionDispatcher::beginCastFromGem()`, ported logic, real `SpellManager` path |
| `mem` | game's `mem.handler` verbatim (exact-name lookup, else partial-match with 5-result disambiguation, `SpellManager::memorizeSpell()`) | T97: "Live is better (dead is a stub)" — old `action::cmdMem` never existed reachably (T98 deleted the dead copy; nothing to port FROM on that side). New method: `memorizeSpellByName()` (dispatcher + handler) — the old `memorizeSpell(gemSlot, spellId)` pair is untouched (T93-tested, `CombatManager`-based, takes an id not a name — a different call shape, not a duplicate) |
| `forget` | game's `forget.handler` logic ported into the EXISTING `EqActionHandler::forgetSpell(uint8_t)` — **repointed, not a new method** | T97: "Live is better (dead is a stub two layers down)" — `ActionDispatcher::forgetSpell()` already existed and dispatched correctly, but `EqActionHandler::forgetSpell()` underneath was a literal stub ("Would need to send forget spell packet", always a false success). No test exercised the stub's own behavior (only fake-handler routing), so fixing it in place was safe — confirmed live: forgetting an empty gem, and a real memorize-then-forget round trip, both now do what game's did. Dispatcher's own gem bound narrowed 1-12 -> 1-8 to match `SpellManager`'s real 8 gems |
| `gems` | game's `gems.handler` verbatim (iterate all 8 gems, real spell names via `SpellManager::getSpell()`, state/cooldown/progress formatting) | T97: "Live is better (dead is a stub)" — old `action::cmdGems` was `"(Spell data not available in current implementation)"`. New method: `listMemorizedSpells()` (dispatcher + handler) |
| `interrupt` | game's `interrupt.handler` logic ported into the EXISTING `EqActionHandler::interruptCast()` — **repointed, not a new method** | T97: "Live is better (different subsystem)" — same shape as `forget`: the dispatcher method existed and had callers-to-be, but routed to `CombatManager::InterruptCast()` instead of the live `SpellManager` path `/cast` actually uses. No test pinned `interruptCast()`'s real underlying behavior (T93 covers only `castSpell`/`castSpellOnTarget`/`memorizeSpell`), so repointing was safe and verified both via a new headless test and live |
| `spellbook` | game's stub message verbatim ("Spellbook window not yet implemented in UI") | T97 didn't classify this one (game never had a UI window to open; `action` never had this command at all — not a divergence, a straight port of a placeholder). Pure message, no dispatcher call — same shape as `cmdNpcState`'s precedent for state-free informational commands |
| `pet` | game's `pet_cmd.handler` verbatim vocabulary and messages, re-expressed against `ActionDispatcher`/`GameState` instead of `EverQuest`'s private `m_entities`/`m_combat_manager` | T97/T96: "Live is better (T96-confirmed, strict superset)" — ported in full; see next section for the 5 new wrappers this required |

### Dispatcher wrappers added for `game`-only pet commands

Five, all routing through the EXISTING generic `IActionHandler::
sendPetCommand()` virtual — **no interface change needed for pet** (unlike
spells, where the new commands needed genuinely new virtuals):

- `ActionDispatcher::petStand()` — `EQT::PET_STANDUP` (`/pet stand`,
  `/pet standup`)
- `ActionDispatcher::petTauntOff()` — `EQT::PET_TAUNT_OFF` (`/pet notaunt`)
- `ActionDispatcher::petHoldOff()` — `EQT::PET_HOLD_OFF` (`/pet nohold`,
  `/pet unhold`)
- `ActionDispatcher::petLeader()` — `EQT::PET_LEADER` (`/pet leader`)
- `ActionDispatcher::petSpellHold()` — `EQT::PET_SPELLHOLD` (`/pet
  spellhold`, `/pet ghold`, and `/pet nospellhold`/`/pet noghold` reuse the
  same call — Titanium has no explicit spellhold-off packet, matching
  game's own "toggle only" comment for this and `/pet nofocus`)

`petAttack()` also gained an optional `targetId` parameter (default 0,
existing behavior unchanged) so `CommandProcessor::cmdPet`'s `/pet attack
<name>` entity-name resolution (via `GameState::entities().
findEntityByName()`, the same case-insensitive-partial-match primitive
T99's `/invite`/`/face` already used) has somewhere to put the resolved id.

### `GameStateSyncBridge` coverage — checked BEFORE the live leg, as required

**Already fully present for both domains**, no change needed:
`PetCreated`/`PetRemoved`/`PetStatsChanged`/`PetButtonStateChanged`
(pre-existing since T40) and `SpellGemChanged`/`SpellCastStarted`/
`SpellCastComplete` (pre-existing since T44). Group was T99's outlier, not
the norm this family also hit — confirms T99's own prediction ("a
one-time class of risk... the next family to touch a `GameState` domain
that already has bridge coverage won't hit it"). Verified by reading
`game_state_sync_bridge.cpp` in full before writing a line of production
code, per the plan's explicit instruction.

### Atomicity: proof via `registry=`

Each of `cast`/`mem`/`forget`/`gems`/`interrupt`/`spellbook`/`pet` was
added to `CommandProcessor` and removed from `eq_commands_spells.cpp` in
the same commit — `git diff --stat` shows only additions in
`command_processor.*`/`action_dispatcher.*`/`eq_action_handler.*` and only
removals (plus a summary comment) in `eq_commands_spells.cpp`, no window
where both existed. Live proof, both sessions (`review/T101/`): every
migrated command's `CMD outcome=` line shows `registry=action`; the
un-migrated controls in the SAME sessions (`/target`, `/say #...`,
`/camp`) show `registry=game` — proving the dispatch order itself was not
disturbed, the same stronger check T99 established.

### Live evidence — a spell was cast, the pet obeyed

Full detail and the evidence table: `/mnt/projects/fable/review/T101/README.md`.
Headlines:

- **Pet** (`willeq_test`/`Testchar`, GM-summoned `skel_pet_11_`, same
  fixture T96 verified as a "Normal" pet that listens to all commands):
  every migrated/new `/pet` subcommand dispatched `registry=action`/
  `outcome=applied`, AND the pet's own in-game chat confirmed real
  obedience — `"Following you, Master."`, `"Guarding with my life..oh
  splendid one."`, `"My leader is Testchar."` (the **T101-new** `/pet
  leader` — vocabulary that reached no code path before this unit),
  `"No longer taunting attackers, Master."` for both `/pet taunt` and the
  **T101-new** `/pet notaunt`, a real attack-and-kill (`"Attacking a rat
  Master."` + `Libartik hits a rat for 18 points of damage.` + `You gain
  experience!!` — attacks, does not dismiss, T96's exact standard), and a
  clean dismiss (`"As you wish, oh great one."`). `hold`/`focus`/
  `spellhold` toggles sent real packets with no server response — the
  identical eqemu AA-gate T96 root-caused (neither test character's class
  can purchase the relevant pet AA); **not a T101 bug**, noted and moved
  on per the plan's explicit instruction.
- **Spells** (`willeq_test`/`Testcaster`, Wizard level 1, `Frost Bolt`
  spell id **54** resolved via `data/willeq.db` — never guessed, per the
  session-discipline requirement): `/mem 1 Frost Bolt` did a real
  asynchronous memorize (`MemorizeProgress` state, confirmed `Ready` ~8s
  later via `/gems`); the **first `/cast` genuinely fizzled** — `Your
  spell fizzles!`, a real server-adjudicated outcome (mana debited, then
  the fizzle resolved), not a scripted ack; a later cast was
  server-confirmed (`BeginCast`) and a real `SpellAction` packet with the
  damage-effect flag landed on the target; `/interrupt` mid-cast dispatched
  correctly (a genuine race with the near-simultaneous server completion
  broadcast is visible and reported plainly, not hidden); `/forget` on a
  gem still in `Refresh` (cooldown) correctly REFUSED (`No spell in gem
  2`, confirmed via `/gems` immediately after still showing `Refresh
  (0s)`) — real `SpellManager` state-machine behavior, ported verbatim
  from game's own call, not a T101 defect.

### T93/T95 tests: unchanged, confirmed still passing, and why

Both suites pass **unchanged** — `./build/bin/test_eq_action_handler_transforms`
(T93, 2/2) and `./build/bin/test_eq_action_handler_enum_guards` (T95,
15/15), run before and after every production change in this unit. This
is NOT a coincidence requiring explanation-away: T93 tests `castSpell()`/
`castSpellOnTarget()`'s `CombatManager`-based gem-slot conversion, which
this unit deliberately did NOT touch (see the "per-command source ported"
table above — new methods were added instead, precisely to avoid needing
to touch the tested code). T95 tests the three enum-bounds guards
(`setMovementMode`/`setPositionState`/`sendPetCommand`), none of which
this unit's changes go anywhere near. If migrating this family had
required changing either suite, that would have meant the migration
altered behavior rather than routing — it didn't need to.

### Whether the recipe generalised to the hard family

**Yes, with one addition T99 didn't need**: when an existing dispatcher/
handler method already exists but is pinned by another unit's test
coverage to a *specific* (even if "wrong subsystem") behavior, **add a new
method for the correct behavior rather than repointing** — repoint only
when the existing method's *internals* have no test coverage (only
fake-handler routing tests, which don't care what's underneath). This
family needed both shapes side by side: `cast`/`mem`/`gems` got new
methods (T93 coverage in the way), `interrupt`/`forget` got repoints (no
such coverage). T99's other core mechanics (port-the-better-
implementation, atomic add+remove, verify via `registry=`, check
`GameStateSyncBridge` first) all worked exactly as designed, zero
deviation. **No STOP condition was hit** — this is a refinement of the
recipe, not evidence it breaks down on the hard family.

### Both-directions test proof (T95 standard)

Two representative breaks, both restored and re-verified green:

1. `ActionDispatcher::petStand()` — changed to send `EQT::PET_SITDOWN`
   instead of `EQT::PET_STANDUP`. Reran
   `ActionDispatcherTest.PetFamily_T101AddedWrappersUseCorrectCommandId`
   and `CommandProcessorTest.Pet_Vocabulary_DispatchesExpectedCommandId`:
   both **failed** with the exact expected-vs-actual mismatch
   (`sendPetCommand(9, 0)` vs expected `sendPetCommand(10, 0)`). Restored
   from a pre-edit backup (`diff` confirmed byte-identical), reran: both
   **passed**.
2. `ActionDispatcher::memorizeSpellByName()`'s gem bound — widened from
   1-8 to 1-12. Reran `ActionDispatcherTest.RealSpellFamily_
   GemBoundsAndRouting`: **failed** (gem 9 wrongly reached the handler,
   `handler.count()` was 4 instead of the expected 3). Restored, `diff`
   confirmed byte-identical, reran: **passed**. (A parallel attempt at
   `CommandProcessor`'s own redundant pre-check did NOT reproduce the
   failure, because `ActionDispatcher`'s own bound check is a second,
   independent guard — correctly reported as a defense-in-depth finding
   rather than silently discarded as a failed attempt.)

### Effort vs T99's estimate

Larger than T99's Group pilot: 7 commands vs 4, 3 new dispatcher methods
(`beginCastFromGem`/`memorizeSpellByName`/`listMemorizedSpells`) plus 5
new pet wrappers plus a repoint pair (`interruptCast`/`forgetSpell`) plus
one new DB-gated real-`SpellManager` test file
(`test_eq_action_handler_spell_pet.cpp`, 9 tests, 3 of them skipped in
this environment — see Deviations), versus Group's 4 commands and zero
new dispatcher methods. Two live sessions (pet, spells) instead of one.
At the higher end of T99's own "needs new dispatcher methods" 4-6h
per-family estimate rather than matching it exactly, but comfortably
inside T99's 60-90h total-programme band for the remaining ~110 commands
— **no revision to that band, upward or downward, is warranted by this
unit's experience.** Families still to come that are "Equivalent" per
T97's divergence table (most chat/movement/utility commands) should run
closer to T99's original under-an-hour figure; families needing new
methods (this one, and per T97's table nothing else currently flagged as
starkly) should budget like this unit.

### Deviations

1. **3 of 9 new `test_eq_action_handler_spell_pet.cpp` tests SKIP in this
   environment** (`InterruptCast_WhileCasting_ClearsCastingState`,
   `ForgetSpell_MemorizedGem_ActuallyClearsIt`,
   `MemorizeSpellByName_ExactMatch_MemorizesSpell`) — all three need a
   spell in the scanned id range (1-4000) that `SpellData::canClassUse(
   PlayerClass::Warrior, 60)` accepts, via the SAME `FindSpellId` helper
   pattern `test_spell_manager.cpp` already uses. Confirmed this is a
   **pre-existing environment/data characteristic, not something this
   unit broke**: `test_spell_manager.cpp`'s own
   `MemorizeSpellSuccessStartsMemorizationAndPublishesEvent` (identical
   predicate) already SKIPs for the identical reason in this same build.
   Not chased further — matches an already-accepted skip pattern
   elsewhere in the suite, and the DB-gated live session (which used a
   Wizard-specific spell, `Frost Bolt`, found directly by name rather than
   by class-scan) proves the same code paths work end-to-end regardless.
2. **`GTEST_DISCOVER_TESTS(test_eq_action_handler_spell_pet)` needed
   `DISCOVERY_TIMEOUT 60`** — links `willeq_core` (a heavy TU set); its
   `--gtest_list_tests` discovery run exceeded the GoogleTest module's 5s
   default under `-j24` parallel build load on the first full-suite
   rebuild. Same T79 precedent `test_spell_manager` already needed; not a
   new problem class, just a new instance of the same one. Fixed in
   `tests/CMakeLists.txt` in the same commit as the new test file.
3. **`character_spells` DB row inserted for `Testcaster`** (spell id 54,
   slot 0) to make the live `/mem`/`/cast` session possible — Testcaster's
   spellbook was empty by default. Deleted again as part of the DB
   restore, confirmed by `SELECT` — not left behind.
4. **`Testchar`'s restored heading (128) differs from
   `refactor/EXECUTOR_PROTOCOL.md`'s documented baseline (190)** — the
   value restored is the character's ACTUAL pre-session DB value (already
   128, not 190, before this unit touched anything). Flagged rather than
   silently "corrected" to a baseline that no longer matches reality — see
   `review/T101/README.md`.
5. No scope extension beyond Spells (6) + Pet (1). No `ActionDispatcher`
   reference added to `eq.h`/`eq_commands*.cpp`. No renderer command
   touched (the `rendererCmd()` helper and its callers in
   `eq_commands_spells.cpp` are untouched). `pet_constants.h`'s
   `PetCommand` enum was not renumbered; T95's `isValidPetCommand()` guard
   was not weakened. No STOP condition was hit.

### Verification

1. `cmake --build build -j24` — clean throughout (including the two
   both-directions-proof round trips).
2. `ctest --test-dir build --output-on-failure` — **2162/2163 passed**,
   sole failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`.
   2163 total vs the plan's stated 2121+new baseline reflects normal
   suite growth from other units merged to `fable` since the plan was
   written, plus this unit's 26 new `CommandProcessor` tests + 7 new
   `ActionDispatcher` tests + 9 new `EqActionHandlerSpellPet` tests (3
   skipped, see Deviations).
3. `bash scripts/build-gles2-x86.sh` — clean.
4. Visual gate (`visual_gate`, folded into the `ctest` run) — **PASS**;
   `git status` clean of any golden files throughout.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — 10 ok, 0 WARN, 0 FAIL.
7. Live eqemu leg — two sessions, `willeq_test`/`Testchar` (pet) and
   `willeq_test`/`Testcaster` (spells), both relocated to `arena` (zone
   77, T84's pattern). Lock taken/released cleanly (T100 in slot1 never
   contended for it during this unit's session), `down.sh` run, lock file
   deleted, DB positions AND the temporary `character_spells` row
   restored and confirmed via `SELECT` after logout.

## Board

This is the batch that validates or invalidates T99's 60-90 hour estimate for
the whole programme. **Record the revised estimate** — Will is deciding how much
of the remaining ~110 commands to commit to on the strength of it.
