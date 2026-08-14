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

(executor fills: confirmed membership for both families; per-command source
ported; **which `who` survived and why**; the replacement atomicity control;
`GameStateSyncBridge` coverage; live evidence per family; whether `/combat on`
is now reachable; **hours split headless vs live, and whether the shared
session recovered time**; the revised programme estimate; deviations)

## Board

Record whether shared live legs work. Will is deciding on that basis whether
the remaining ~8 families run as three or four batches instead of eight.
