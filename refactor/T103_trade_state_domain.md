# T103 — Give Trade a real state domain and route it through the bridge

**Slot: slot1. Headless first, then a LIVE leg. T104 runs in parallel and also
wants eqemu — do all headless work first, then contend for the lock; if held,
wait, then report the live leg outstanding rather than stealing it.**

**Territory: `include/client/state/`, `src/client/state/`,
`include/client/bridge/`, `src/client/bridge/game_state_sync_bridge.*`,
`src/client/eq_action_handler.cpp`, `src/client/action/`, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T103/`.**

**Will's ruling: Trade and Combat should go through the bridge.** This unit is
Trade. T104 is Combat.

Read T102's Review (which created the compromise this unit removes), then
T81's Review — it is the model for a source→projection migration done well.

## The defect

**Trade has no `GameState` domain at all.** T102 needed trade state in the
action layer and, finding none, had `EqActionHandler` read
`EverQuest::GetTradeManager()` directly. That is the action layer reaching down
past its own abstraction into `EverQuest` internals — a layering compromise
T102 recorded honestly rather than hiding.

Every other domain reaches the projection through typed bridge events: pet
(T40), inventory (T42), spells (T44), entities (T81), group (T99). Trade is the
gap.

## Follow T81's discipline, it is the standard here

T81 is the model. In particular:

- **Verify reconstructibility, do not assume it.** T81 established that its
  source published the complete struct at every call site, so the projection
  could be rebuilt from events alone. Do the equivalent for trade: identify
  every place trade state changes and confirm each publishes enough.
- **Add a typed event rather than widening an existing one.** T81 added
  `EntityStateSnapshot` instead of overloading `EntitySpawnedData`, because the
  latter was graphics-tuned and missing fields. Adding events is sanctioned;
  silently changing what an existing event means is not.
- **Report field-loss cases rather than glossing them.** T81 found
  `ZoneProcessWearChange()` mutating equipment without re-publishing, and said
  so.

## Scope

1. A **Trade domain** on `state::GameState`, following the shape of the
   existing domains. Decide the field set from what `TradeManager` actually
   holds and what consumers actually need — partner identity, the items offered
   on each side, money, accept/cancel state, whether a trade is active. **State
   your reasoning; do not mirror `TradeManager` field-for-field by reflex.**
2. **Typed events** carrying those changes, handled in `GameStateSyncBridge`.
3. **Remove T102's direct `GetTradeManager()` reads** from
   `EqActionHandler`/the action layer, replacing them with the projection.
4. `declineTrade()`'s message currently omits the partner name because no
   accessor existed — with a real domain it should be able to include it.
   Confirm whether it now can.

## What to check before designing anything

- Does `EverQuest::m_game_state` (the source-side `GameState`) already have
  trade state? If so the domain may exist on one instance and not the other,
  which changes this from "build a domain" to "project an existing one".
- What does `TradeManager` publish today, if anything? Some domains already
  emit events that nothing consumes.

## Live leg

T102 proved the full round trip works: `/trade → /accept → /canceltrade`,
server-confirmed on both clients, with two accounts (`willeq_test`,
`willeq_buddy`). Repeat it and confirm the **projection** now reflects each
stage — not just that the commands succeed.

**Observed, not inferred from acks.** Confirm the partner name now appears
where T102 had to leave it blank.

Session discipline: binary freshness against HEAD first; daylight
`/say #set time 11 0`; pad scripts long; `-d 4` plus `--log-level=DEBUG`; never
guess an item id (resolve via `data/willeq.db`); restore DB state and confirm
by `SELECT` **after logout**; kill clients by real binary PID, not `nohup`'s
`$!`; clean teardown, delete the lock.

## What you may not do

- Do not touch `syncCombatState()` or combat state — that is T104, running in
  parallel. Overlapping edits to `game_state_sync_bridge.cpp` are likely, so
  keep your changes narrow and additive.
- Do not add an `ActionDispatcher` reference to `eq.h` or `eq_commands*.cpp`.
- **STOP and report** if trade state turns out not to be reconstructible from
  events — that is a real finding, and forcing it would produce a projection
  that silently drifts.

## Verification

Full build and suite (current total 2217; sole pre-existing failure the
documented ALSA `AudioManagerTest.PreloadCommonSounds`). Every new test
**proven able to fail in both directions**. Visual gate — goldens must not
move.

## Review

(executor fills: whether a trade domain already existed on either `GameState`;
the field set chosen and why; the events added; reconstructibility evidence per
field; what was removed from `EqActionHandler`; whether `declineTrade()` can
now name the partner; live evidence the projection tracks each stage; any
field-loss case found; deviations)
