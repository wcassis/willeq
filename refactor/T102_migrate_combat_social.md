# T102 — Migrate Combat + Social in one batch, with ONE shared live leg

**Slot: slot1. Headless work for both families, then a SINGLE live session
covering both. eqemu lock normally.**

**Territory: `src/client/action/`, `src/client/eq_commands*.cpp` (removals
only), `src/client/bridge/game_state_sync_bridge.*` if coverage is missing,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T102/`.**

**Follow the migration recipe in T99's Review**, plus T101's addition: *add a
new dispatcher method when an existing one is test-pinned to different
behaviour; repoint only when the internals are untested.*

## Why this batch is shaped differently — it is an experiment

T100 (Chat, 9 commands, zero new action-layer code) still took ~2.5-3h, with
the **live leg** taking the larger share. T101 (the hard family) landed inside
its band. Conclusion: live verification is closer to a **fixed cost per
family** than one that scales with difficulty.

So this batch tests whether **one live session can cover two families**. If it
can, the remaining ~10 families collapse into three or four batches instead of
ten, and the 60-90h estimate holds or improves. If it cannot, say so — that is
the finding, and it revises the programme estimate upward.

**Report the actual hours split between headless work and the live leg**, and
state plainly whether sharing recovered time versus two separate sessions.

## Scope — 23 commands, confirm membership yourself

Extracted while scoping, **verify before trusting**:

- **Combat (12)**: `assist`, `shield`, `duel`, `duelaccept`, `dueldecline`,
  `target`, `attack`, `stopattack`, `aa`, `combat`, `taunt`, `autofire`
- **Social (11)**: `trade`, `accept`, `tradedecline`, `canceltrade`, `who`,
  `inspect`, `lfg`, `surname`, `random`, `yell`, and one more — resolve it

## Two traps in this batch specifically

### 1. `target` is the atomicity control every prior unit used

T99, T100 and T101 all proved atomicity by leaving `/target` unmigrated and
showing it still logged `registry=game` in the same session. **This batch
migrates `target`, so that control is gone.** Pick a different unmigrated
control, name it in the Review, and confirm it still logs `registry=game`
alongside your migrated commands logging `registry=action`.

### 2. `who` is G02's intra-registry duplicate

Two `who` registrations exist in the `game` registry — Social's and Movement's
— and the later one wins. T97 confirmed this is one of exactly two such cases
(the other being `follow`, which T99 dealt with).

So migrating `who` means **deciding which of the two behaviours survives**.
Read both, state which you took and why, and note that the losing one has been
silently dead. Do not migrate both under one name.

`who` is also one of the commands T82 originally found shadowed and that this
whole arc began with — getting it right matters more than the others.

## Things not to confuse

- **`/accept` is TRADE-accept, not group-accept.** T99 established this and
  added `/groupaccept` separately. Do not merge their behaviour.
- **`aa` is alternate advancement.** T96 traced `/pet hold` and `/pet focus`
  producing no server response to an AA gate neither test character's class
  can purchase — so AA-dependent behaviour may not be verifiable live here.
  Note it rather than chasing it.
- **`combat`, `attack`, `stopattack`, `autofire` touch `CombatManager`
  state.** T101 found `CombatManager` is an older subsystem that
  `EqActionHandler::castSpell` still routes through. Expect the same
  test-pinning question T101 hit, and apply its rule.
- `/combat on` is historically the exact command T26 could not reach, which is
  what motivated T29's unified command channel. It should now be reachable —
  confirm it.

## Do this first — T99's addendum

**Check `GameStateSyncBridge` coverage BEFORE the live leg.** Combat and trade
state are plausible gaps. T101 found Pet/Spell already covered by T40/T44, and
T100 found Chat not applicable — so this may need nothing, but check rather
than assume.

## Atomicity

`game` is tried before `action`. Add and remove **in the same change**; verify
`registry=action` per command; keep an unmigrated control (**not `target`**).

## The shared live leg

One session, two clients (`willeq_test`, `willeq_buddy` — concurrency is
per-account). Plan the running order so both families are exercised without
relogging:

- **Combat**: attack something and confirm it takes damage; `/combat on` and
  confirm the auto-attack cycle; `/assist`; `/taunt` if a pet or group allows.
- **Social**: `/who` listing real nearby players; `/inspect` another character;
  `/trade` with the second client and `/tradedecline` or `/canceltrade`;
  `/duel` between the two clients if the server permits.
- **Observed, not inferred from acks.** `outcome=applied` means a handler
  returned success and nothing more.

Session discipline: verify binary freshness against HEAD first; force daylight
`/say #set time 11 0`; pad scripts far longer than you think; `-d 4` plus
`--log-level=DEBUG`; never guess an item id (resolve via `data/willeq.db`);
restore DB state and confirm by `SELECT` **after logout**; clean teardown,
delete the lock. Kill clients by the real binary PID, not `nohup`'s `$!` —
T100 left two clients running by making exactly that mistake.

## What you may not do

- **No `ActionDispatcher` reference in `eq.h` or `eq_commands*.cpp`** — the
  dependency cycle. Migration goes upward only.
- Do not migrate renderer commands; they stay on the `RendererCommand` bridge
  queue.
- Do not migrate beyond Combat and Social.
- **STOP and report if the recipe does not fit**, naming the step that fails.

## Verification

Full build and suite (expect the current total plus your new tests; sole
pre-existing failure the documented ALSA `AudioManagerTest.PreloadCommonSounds`).
Every new test **proven able to fail in both directions**. Visual gate —
goldens must not move.

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T102-combat-social`, off `fable` tip `5d51f517`. Full evidence:
`/mnt/projects/fable/review/T102/` (README + four filtered live-session
logs).

### Confirmed membership

Re-derived from the registrations, not trusted from the plan's prose
(which matched exactly, including the "one more" hint):

- **Combat: exactly 12**, split across two files — `eq_commands_movement.cpp`'s
  Combat category: `target` (alias `tar`), `attack` (alias `att`),
  `stopattack`, `aa` (alias `~`), `combat`, `taunt`, `autofire` (alias
  `af`) — 7; `eq_commands.cpp`'s Combat category: `assist` (alias `a`),
  `shield`, `duel`, `duelaccept`, `dueldecline` — 5.
- **Social: exactly 11 registrations, 10 distinct names** —
  `eq_commands.cpp`'s Social category: `trade`, `accept`, `tradedecline`
  (alias `rejecttrade`), `canceltrade` (alias `stoptrade`), `who_cmd`
  (alias `whoall`), `inspect`, `lfg`, `surname`, `random` (alias `roll`),
  `yell` — 10; plus `eq_commands_movement.cpp`'s **second** `who`
  registration (also literally categorized `"Social"`, not `"Movement"` as
  the plan's prose guessed) — the 11th registration is `who`'s own
  intra-registry duplicate, not a distinct 11th command. This resolves the
  plan's "and one more — resolve it" exactly: the "one more" IS the
  duplicate `who`.

### Which `who` survived, and why

**Ported Social's `who_cmd`** (the real server `/who` request —
`SendWhoAllRequest()`, with name/level-range argument parsing, a genuine
`OP_WhoAllRequest`/`OP_WhoAllResponse` round trip) — **not** Movement's
`who` (a client-side scan of the locally-known `m_entities` map, format
`"[id] Name (Lvl N)"`/`"...and N more"`), even though Movement's is the
one that has been silently winning the intra-registry collision since G02
(`RegisterMovementCommands()` runs after `RegisterSocialCommands()`).
Reasoning: real EQ's `/who` sends a server request and gets a proper
server-side listing (not just what happens to be locally visible);
Social's `who_cmd` is the faithful port of that, while Movement's is a
simplified local substitute that was only ever live because of the
registration-order accident, not because it's the better implementation.
Movement's `who` registration is **deleted, not migrated** — confirmed
live: `/who`'s `CMD outcome=` line shows `registry=action` in both
sessions, and `data/captures/t102*_a_session.log` shows the real
`SendWhoAllRequest` debug trace, not the deleted client-scan code path.

### Per-command source ported

All 23 are direct ports of `game`'s existing handler logic (T97's
divergence table doesn't cover Combat/Social explicitly, but every row
traced here showed the same "Equivalent, port for behavior parity" shape
T99/T100 already established for the family):

| command(s) | source ported | notes |
|---|---|---|
| `target` | `game`'s substring-match search, re-expressed against `GameState::entities().findEntityByName()` (already case-insensitive) instead of `game`'s own lowercase-both-sides loop over `m_entities` — same semantics, existing `ActionDispatcher::targetEntity(spawnId)` reused, no new dispatcher code |
| `attack`/`stopattack`/`aa` | `game`'s handlers **were already exact matches** for pre-existing `ActionDispatcher::enableAutoAttack()`/`disableAutoAttack()`/`toggleAutoAttack()` (used directly by `InputActionBridge`'s keyboard gesture since T51) — no new dispatcher code, pure routing |
| `combat` | `game`'s on/off/toggle parsing verbatim; **new** `enableCombatManager()`/`disableCombatManager()`/`toggleCombatManager()` wrapping `CombatManager::Enable()`/`Disable()`/`IsEnabled()` — a flag distinct from both auto-attack and `setAutoHunting()`'s `m_auto_hunting_enabled` |
| `taunt` | `game`'s target-check + `CombatManager::Taunt()` call; **new** `taunt()` |
| `autofire` | `game`'s enable/disable-toggle shape (same as `/aa`'s), over `CombatManager`'s auto-fire flag; **new** `toggleAutoFire()` |
| `assist` | `game`'s explicit-name-else-current-target logic; **new** `assist(uint16_t)`, name resolution done in `CommandProcessor` (`GameState::entities()`), current-target fallback read from `GameState::combat()` |
| `shield`/`duel` | `game`'s current-target-only logic (no name argument); **new** `shieldTarget(uint16_t)`/`duelRequest(uint16_t)` |
| `duelaccept`/`dueldecline` | `game`'s `HasPendingDuel()` pre-check + `SendDuelAccept`/`SendDuelDecline`; **new** `duelAccept()`/`duelDecline()`, the pending-duel check moved into `EqActionHandler` (no GameState duel domain exists to project it into — see below) |
| `trade` | `game`'s trade_cmd verbatim **except** the self-trade guard (needs "my own spawn id", no public accessor from this layer — see Deviations); **repointed** the pre-existing `requestTrade(uint16_t)` stub (T101's rule: untested internals, safe to repoint) |
| `accept` | `game`'s accept_cmd; **repointed** the pre-existing `acceptTrade()` stub — found and fixed a real aliasing-order bug live, see below |
| `tradedecline` | `game`'s tradedecline_cmd logic; **new** `declineTrade()` (no pre-existing dispatcher method for this half of trade) |
| `canceltrade` | `game`'s canceltrade_cmd; **repointed** the pre-existing `cancelTrade()` stub |
| `who` | see above; **new** `requestWho(name, lvlLow, lvlHigh)` |
| `inspect` | `game`'s inspect_cmd explicit-name-else-current-target logic; **new** `inspectTarget(uint16_t)` |
| `lfg` | `game`'s `SendLFGCommand(!m_is_lfg)`; **new** `toggleLFG()` |
| `surname` | `game`'s non-empty-args guard + `SendSurname()`; **new** `setSurname()` |
| `random` | `game`'s low/high parsing (default 0-100, swap if high<low); **new** `rollRandom()` |
| `yell` | `game`'s `SendYellForHelp()`; **new** `yellForHelp()` |

Applying T101's addendum throughout: every "new" method above either had
no pre-existing `ActionDispatcher`/`EqActionHandler` counterpart at all
(taunt, autofire's toggle shape, assist/shield/duel/duelaccept/
duelDecline/who/inspect/lfg/surname/random/yell/combat-manager-toggle), or
repointed a pre-existing **stub** whose only test coverage was
fake-handler routing (`requestTrade`/`acceptTrade`/`cancelTrade` —
confirmed by reading `test_action_dispatcher.cpp` before touching them).
No case needed a new method purely to avoid breaking test-pinned internals
(unlike T101's `cast`/`mem`/`gems`), because nothing in Combat/Social had
real, tested internals sitting behind an existing dispatcher call.

### The replacement atomicity control: `/loc`

`/target` (every prior unit's control) is migrated in this batch, so a
different unmigrated command was needed. **`/loc`** was chosen — it's
`eq_commands_movement.cpp`'s Movement category, untouched by T102 (T98
already established Movement's loc/sit/stand/move/moveto/follow/
stopfollow as permanently un-migrated, shadowed-but-correct code that
stays in `game` on purpose). Confirmed live, same session, same client, as
every migrated command: `CMD outcome=applied registry=game
recognized=true cmd="/loc"` appears immediately after migrated commands
logging `registry=action` — proving dispatch order itself was not
disturbed by this batch's changes, the exact check the plan asked for.

### `GameStateSyncBridge` coverage — checked before the live leg, more nuanced than "yes/no"

**Combat: covered, but not by `GameStateSyncBridge`.** Read
`game_state_sync_bridge.cpp` in full first, per the plan's addendum — it
has **no** `TargetChanged`/`TargetHPUpdated`/Trade-event cases at all. But
`Application::syncCombatState()` (`application.cpp`, predates
`GameStateSyncBridge`, T39) already polls `CombatManager` every tick and
mirrors it into the **same** `state::GameState` instance
(`Application::m_gameState`) `CommandProcessor` reads — a different
mechanism, same effect. `cmdHail()` (T39) already relies on this; this
unit's `resolveCombatTarget()` helper (used by `assist`/`shield`/`duel`/
`inspect`) reads `m_state.combat()` the identical, already-proven-live
way. No bridge fix was needed for Combat, and none was made.

**Trade: genuinely NOT covered, by either mechanism, and there is no
`TradeState` domain in `GameState` at all** (confirmed: `game_state.h` has
`tradeskill()` — tradeskill combines, a different feature — but no
trade-with-another-player domain). Building one (a new state module, a new
bridge/poll wiring, new tests) is a much bigger lift than this migration's
scope justifies. Worked around it the way `enableAutoAttack()` et al.
already work around `GameState`'s combat gaps in edge cases: all of
`trade`/`accept`/`tradedecline`/`canceltrade`'s real precondition logic
(already trading? in combat? distance? current target fallback) lives in
`EqActionHandler`, reading `EverQuest::GetTradeManager()`/
`GetCombatManager()` directly (both pre-existing public accessors) — live,
authoritative, no projection needed. `CommandProcessor`'s role is only
name resolution via `GameState::entities()` (confirmed synced, T101's pet
precedent). This is a real, load-bearing finding, not a "nothing to
report" — flagged in `refactor/deferred.md`.

### `/combat on` — confirmed reachable

T26's original blocker (this exact command, motivating T29's unified
command channel) is confirmed fixed and this migration keeps it that way:
`data/captures/t102*_a_session.log`, `CMD outcome=applied registry=action
message="Combat manager enabled" cmd="/combat on"`, dispatched via
`--script` (the same channel T26 could not reach it through).

### Live evidence per family

Full detail: `/mnt/projects/fable/review/T102/README.md`. Headlines:

- **Combat**: `/attack`/`/stopattack`/`/combat on|off|toggle`/`/taunt`/
  `/assist`/`/target` all dispatch `registry=action`, real packets sent
  (`EnableAutoAttack called`, `Sending auto attack packet`, `Sent Taunt
  packet`). `/attack` got a real, server-adjudicated refusal ("Your
  target is too far away, get closer!") rather than a damage exchange — a
  GM `#goto` attempt to close the ~25-unit gap to the nearest arena NPC
  did not visibly move the character; **no actual damage was captured
  this session** — reported plainly, not glossed over. `/duel` dispatched
  and sent a real packet but was never visibly received by the second
  client in either round — likely an eqemu test-server dueling-support
  gap (both send/accept/decline are 100% pre-existing, unmodified code);
  noted per the plan's own precedent for this class of finding (T96's AA
  gate), not chased further.
- **Social**: `/who`/`/inspect` dispatch and produce real server round
  trips; `/trade`→`/accept`→`/canceltrade` is a **complete, real,
  cross-client round trip**, server-confirmed on both ends (`Testmelee
  wants to trade with you` on B; both clients independently log `Trade
  cancelled`). This round also **found and fixed** a real bug: `/accept`
  printed the partner's name blank (`acceptTrade()` read
  `TradeManager::getPartnerName()` **before** calling
  `acceptTradeRequest()`, which is what actually populates it) — fixed by
  reordering, re-verified live (`Accepting trade with Testmelee`).

### Whether the shared live leg worked, and the hours split

**Yes — and the finding is stronger than "it can be done", it's that the
live leg was proportionally SMALL this round, the opposite of T99's and
T100's experience.** Headless work (reading T99/T100/T101/T97's Reviews
and divergence table, confirming membership from the registrations,
designing the split of responsibility between `CommandProcessor`/
`ActionDispatcher`/`EqActionHandler` for a family with real trade/duel/
combat-manager state to reason about, writing ~450 lines of production
code across 6 files plus 70 new `CommandProcessorTest` cases, six full
rebuild+full-suite cycles including two both-directions proof round trips,
an ARM cross-compile, and a file-size check) was the larger share — call
it **~2.5-3 hours**. The live leg itself — eqemu lock held
2026-08-14T02:11:42Z through `down.sh` completing and the lock file
deleted (~02:54Z), including one round burned to a shell-scripting mistake
(relative `-c`/`--script` paths inside a combined background-launch
command put client B in the wrong `cwd`; caught via `readlink
/proc/<pid>/cwd` before it was ever treated as evidence, not repeated) —
was **under 45 minutes of actual wall time** covering **both** families,
including finding and fixing a real bug and re-verifying it.

**Sharing recovered real time, unambiguously**: T99's Group alone needed
~2 hours of live time (two full sessions, one of which found and required
fixing the `GameStateSyncBridge` gap). T100's Chat alone needed ~1 hour
across two rounds. A naive two-separate-sessions estimate for
Combat+Social, extrapolating from those, would be in the 1.5-2.5 hour
live-time range; this batch's actual combined live time was well under
half that. The mechanism is exactly what the plan hypothesized: session
setup (lock, server up, binary freshness, daylight force, two-client
launch, DB baseline capture) is a **fixed cost paid once**, not per
family — and this batch is the first evidence that when NEITHER family
needs a `GameStateSyncBridge`/new-state-domain fix (Combat didn't; Trade's
gap was worked around without one), the live leg's own execution time can
shrink dramatically too, not just its setup overhead.

### Revised programme estimate

**Lowers T99's 60-90h band, does not just confirm it.** T99's estimate
assumed live verification is close to a per-family fixed cost regardless
of batching; this unit shows that when live evidence-gathering itself is
efficient (no infra gap requiring a fix, one session driving two clients
through a written script rather than manual interactive exploration), a
two-family batch's live leg can cost **less than one T100-sized single
family's live leg did**. Revised recommendation: **the remaining ~8
families should run as three to four batches** (matching the plan's own
hoped-for outcome), and the total programme estimate should be revised
**down** to roughly **40-65 hours** for the remainder — the upper end
still budgeted for any family that turns out to need a new
`GameStateSyncBridge` case or a new state domain (this unit's Trade gap
is proof that class of cost hasn't gone away, only that it's avoidable
often enough to matter), the lower end reflecting that most remaining
families (per T97's divergence table, mostly "Equivalent" rows) should
run closer to this unit's ~3h total than T99's ~5-6h.

### Tests, and proof they can fail in both directions

70 new `CommandProcessorTest` cases in `tests/test_command_processor.cpp`
(registration/category checks for all 23 commands, per-command dispatch-
reaches-the-right-`ActionDispatcher`-method assertions, entity-name-
resolution and current-target-fallback cases, argument-parsing cases for
`/combat`/`/who`/`/random`, and the alias/failure-path cases matching
every other migrated family's coverage shape). One pre-existing test
updated in place (`T51_AttackStopattackAa_NoLongerRegistered` →
`T102_AttackStopattackAa_NowRegistered`, since T102 reverses T51's
finding the same way T99/T100/T101 each reversed a T98 deletion). Two
obsolete test files retired: `test_eq_commands_attack.cpp` deleted
entirely (every case exercised the now-deleted `game`-registry
attack/stopattack/aa path via `ProcessChatInput()`); six of
`test_eq_commands_assist.cpp`'s seven cases deleted for the same reason
(assist/shield/duel), the seventh (`/bandage`, out of T102's scope, still
live in `eq_commands.cpp`) kept on its original seam.

**Proved failure in both directions, twice:**
1. `CommandProcessor::cmdTaunt` — replaced its body with an unconditional
   `return ActionResult::Failure("T95 BROKEN FOR TEST")`; reran
   `Taunt_WithTarget_DispatchesTaunt`: **failed** (`result.success` false,
   `taunt()` never dispatched). Restored from a pre-edit backup (`diff`
   confirmed byte-identical), rebuilt, reran: **passed**.
2. `ActionDispatcher::enableCombatManager()` — changed its body to call
   `m_handler->disableCombatManager()` instead of `enableCombatManager()`
   (wrong-method bug, not a stub-out). Reran
   `Combat_On_DispatchesEnableCombatManager`: **failed** (recorded call
   was `disableCombatManager()`, not the expected `enableCombatManager()`).
   Restored, `diff` confirmed byte-identical, reran: **passed**.

### Deviations

1. **`EverQuest::GetPendingDuelInitiatorId()` added to `eq.h`** (plain
   public accessor, same shape as the pre-existing `GetCombatManager()`/
   `GetTradeManager()`) — `duelAccept()`/`duelDecline()` need
   `m_duel_initiator_id`'s value from `EqActionHandler`, which has no
   friendship with `EverQuest` (only `CombatManager` does). No
   `ActionDispatcher` reference added to `eq.h` — the prohibited cycle —
   this is a one-line getter for a `uint32_t`, nothing upward.
2. **The self-trade guard from `game`'s `trade_cmd` was NOT ported** —
   it needs "my own spawn id" compared against the resolved target, and
   neither `EverQuest`'s public surface nor `GameState::player()` (used
   from `EqActionHandler` via `m_eq.GetGameState()`) has been confirmed to
   expose that cleanly from this layer without further plumbing;
   dropped rather than guessed at. Flagged here and in
   `refactor/deferred.md`, not silently absorbed.
3. **`declineTrade()`'s message omits the partner's name** (game's
   `tradedecline_cmd` included it) — `TradeManager` has no public accessor
   for the pending request's name on the decline path (only on the accept
   path, via `m_partner_name`, which `rejectTradeRequest()` never
   populates). Fixing this would mean adding an accessor to
   `trade_manager.h`/`.cpp`, outside this unit's declared territory.
   Message is deliberately name-free rather than printing a blank the way
   the pre-fix `acceptTrade()` did.
4. **A real live-found bug in `acceptTrade()` (partner-name read-before-
   populate ordering) was found and fixed within this unit** — see "Live
   evidence" above. Same class of bug as T99's `AcceptGroupInvite()`
   finding; this one was fixed (not just reported) because it directly
   blocked the plan's own live-verification bar for `/accept`.
5. **Round 2 of the live leg was discarded** (not kept as evidence) due to
   a shell-scripting mistake — relative `-c`/`--script` paths inside a
   combined background-launch command left client B's process with the
   wrong `cwd`. Caught before use via `readlink /proc/<pid>/cwd`; fixed by
   using absolute paths for round 3.
6. **Testmelee's DB position was found already relocated to `arena`** at
   this unit's own session start (not this unit's doing — T100's Review
   documents Testmelee's true baseline as `zone_id=25` (nektulos), and a
   session since then apparently moved it to `arena` and never restored
   it). Not "fixed" to the older baseline by this unit (no way to confirm
   which prior session is responsible or whether reverting would itself
   be wrong); left exactly as found, confirmed unchanged via `SELECT`
   after this session's own clients logged out. Flagged in
   `refactor/deferred.md`.
7. No scope extension beyond Combat (12) + Social (10 distinct names). No
   `ActionDispatcher` reference added to `eq_commands*.cpp`. No renderer
   command touched. No registry reordering.

### Verification

1. `cmake --build build -j24` — clean throughout, including both
   both-directions-proof round trips and the post-live-leg bugfix rebuild.
2. `ctest --test-dir build --output-on-failure` — **2217/2217 passed
   except the documented ALSA `AudioManagerTest.PreloadCommonSounds`**
   (final run, after the `acceptTrade()` fix).
3. `bash scripts/build-gles2-x86.sh` — clean, rebuilt after the
   `acceptTrade()` fix before the second live round.
4. Visual gate (`visual_gate`, folded into the `ctest` run) — PASS, 6.16s;
   `git status` clean of any golden files throughout.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — 9 ok, 3 WARN
   (`include/client/eq.h`, `src/client/action/command_processor.cpp`,
   `tests/test_command_processor.cpp`, all pre-existing over-1500-line
   files that grew further — same shape as T99/T100's WARN, not a new
   FAIL — `eq_commands.cpp`/`eq_commands_movement.cpp` both got
   **smaller**), 0 FAIL.
7. Live eqemu leg — three rounds (round 2 discarded, see Deviations),
   `willeq_test`/`Testmelee` + `willeq_buddy`/`Testbuddy`, both already in
   `arena` at session start (see Deviations #6). Lock taken/released
   cleanly, `down.sh` run, lock file deleted, both characters' DB
   positions confirmed unchanged via `SELECT` after both clients' final
   logout (nothing needed restoring — neither character moved during this
   session).

## Board

Record whether shared live legs work. Will is deciding on that basis whether
the remaining ~8 families run as three or four batches instead of eight.
