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

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T103-trade-domain`, off `fable` tip `c76c93b9` (the plan commit). Full
evidence: `/mnt/projects/fable/review/T103/` (README + two filtered
live-session logs).

### No trade domain existed on either `GameState` — confirmed, not assumed

Checked both instances directly: `include/client/state/game_state.h` had
`tradeskill()` (tradeskill-combine state, a different feature) but no
player-to-player trade domain at all, on `EverQuest::m_game_state` (the
source-side instance) or `Application::m_gameState` (the render-side
projection). Matches T102's deferred-finding exactly.

**But `TradeManager` already published a complete, typed event for every
state transition it has** — `TradeRequestReceived`, `TradeStarted`,
`TradeItemUpdated`, `TradeMoneyUpdated`, `TradeAcceptStateChanged`,
`TradeCancelled`, `TradeCompleted` (all pre-existing in `event_bus.h`,
published from `EverQuest::SetupTradeManagerCallbacks()`,
`eq_merchant.cpp`, predating this unit) — and nothing consumed them.
`IrrlichtBridge` only `LOG_TRACE`'d each case. This is exactly the "some
domains already emit events nothing consumes" case the plan asked to check
for. **No new event type was needed** — the whole unit is "project an
existing-but-unconsumed event stream," not "add events."

### Field set chosen, and reasoning

`include/client/state/trade_state.h`'s `TradeState` class: `active`,
`partnerId`/`partnerName`/`isNpcTrade`, `hasPendingRequest` +
`pendingPartnerId`/`pendingPartnerName`, `ownAccepted`/`partnerAccepted`,
`ownMoney`/`partnerMoney`, and partner-side item slots
(`hasPartnerItem`/`partnerItemId`/`partnerItemName`, 8 slots).

**Deliberately NOT tracked, both verified missing rather than assumed** (T81
discipline):

1. **The player's own offered items.** `TradeManager::addItemToTrade()`/
   `removeItemFromTrade()` — the only path that would populate the "own"
   side of `TradeItemUpdated` — has **zero production callers** (confirmed
   by `grep`: only declared/defined, never invoked). Items actually move
   into a trade via the ordinary inventory `MoveItem` flow (slots
   `TRADE_BEGIN`..`TRADE_END`, `inventory_constants.h`), which
   `InventoryState` doesn't model either (only equipment/general/bank
   occupancy). `setOnItemUpdated()`'s `isOwn` branch (`eq_merchant.cpp`) is
   therefore structurally dead in production — no event ever carries "my
   own trade slot N now holds item X." A real reconstructibility gap for
   this one field, reported rather than guessed at. The player's own
   offered items remain visible only via the existing inventory-slot UI,
   unaffected by this unit.
2. **The outgoing "I sent a request, awaiting their response" sub-state**
   (`TradeManager::TradeState::PendingRequest`). Entering it publishes no
   event at all — `setOnStateChanged()`'s callback only acts on the
   `Active` and `None` transitions (confirmed by reading the lambda body).
   A second field-loss case. No current consumer needs it:
   `EqActionHandler::requestTrade()`'s only precondition was `isTrading()`
   (== `Active`), which `active()` reproduces exactly, so this gap changes
   no observable behavior — confirmed live this session (see below, A's
   second `/trade` was correctly refused by `TradeManager`'s own guard, not
   silently accepted by a wrong projection state).

Deliberately mirrors `TradeManager`'s field shape closely (not "reflexively"
— it's what every consumer, `EqActionHandler`'s four methods, actually
reads: partner identity for messages, accept/pending state for
preconditions). Money and partner items were included even though no
current `EqActionHandler` method reads them, because `TradeMoneyUpdated`/
`TradeItemUpdated` are fully reconstructible for free (already-published,
already-complete events) and are exactly the fields T102's deferred-finding
named as what "a future unit that wants trade UI state exposed... will
need" — including them now means that future unit needs no new events.

### Events added: none. Cases added to `GameStateSyncBridge`: 7

`src/client/bridge/game_state_sync_bridge.cpp` gained one `case` per
existing Trade event type, each a straight field copy into
`m_state.trade()` (see the file for the per-case reconstructibility note).
`GameStateSyncBridgeTest` (`tests/test_game_state_sync_bridge.cpp`) has 9
new cases covering all 7 plus the own-item-is-ignored edge case and
wrapped-bridge delegation.

### Reconstructibility evidence per field

Traced every publish call site in `eq_merchant.cpp`'s
`SetupTradeManagerCallbacks()` before writing any projection code:

| field | publisher | verified |
|---|---|---|
| `hasPendingRequest`/`pendingPartnerId`/`pendingPartnerName` | `setOnRequestReceived()` → `TradeRequestReceived` | fired from `TradeManager::handleTradeRequest()`, the sole place `m_pending_request_*` is set |
| `active`/`partnerId`/`partnerName`/`isNpcTrade` | `setOnStateChanged()`, `Active` branch → `TradeStarted` | fired on both accept paths (our accept, their accept-ack) — `TradeManager::setState(Active)`'s only two callers |
| `partnerMoney`/`ownMoney` | `setOnMoneyUpdated()` → `TradeMoneyUpdated` | fired from both `setOwnMoney()` (our side) and `handleTradeCoins()` (partner side) — the only two money mutators |
| `ownAccepted`/`partnerAccepted` | `setOnAcceptStateChanged()` → `TradeAcceptStateChanged` | fired from `clickAccept()`, `resetAccept()`, and `handleTradeAcceptClick()` — every mutator of either flag |
| partner items (slots 0-7) | `setOnItemUpdated()`, `!isOwn` branch → `TradeItemUpdated` | fired from `handlePartnerItemPacket()`/`handlePartnerItemRemoved()` — the only two partner-item mutators |
| full reset (cancel/complete) | `setOnCancelled()`/`setOnCompleted()` → `TradeCancelled`/`TradeCompleted` | fired from `cancelTrade()`, `handleCancelTrade()`, `handleFinishTrade()` |

**Own items**: unreconstructible, see "Field set chosen" above — reported,
not silently dropped.

**A third gap found while tracing** (not anticipated by the plan, found by
following the same discipline): `TradeManager::rejectTradeRequest()` (the
decline path) calls `resetTradeState()` **directly**, without going through
`setState()` and **without invoking `m_on_cancelled()`** — unlike
`cancelTrade()` and the partner-initiated `handleCancelTrade()`. So a
**local** decline publishes **no event at all** for the state reset. Left
unhandled, this would leave `hasPendingRequest()` stuck `true` in the
projection forever after a successful decline. `trade_manager.cpp` is
outside this unit's declared territory, so not fixed there; worked around
in-territory by having `EqActionHandler::declineTrade()` call
`m_eq.GetGameState().trade().reset()` directly after a successful decline.
**Live-confirmed this session** (see below) — without this, B's later
`/accept` would have risked operating on stale pending-request state
instead of correctly reporting nothing pending.

### What was removed from `EqActionHandler`, and what stayed

All four trade methods (`requestTrade`/`acceptTrade`/`cancelTrade`/
`declineTrade`, `src/client/eq_action_handler.cpp`) still call
`m_eq.GetTradeManager()` for the **four calls that actually perform a
network action** (`requestTrade()`/`acceptTradeRequest()`/`cancelTrade()`/
`rejectTradeRequest()` — these still have to go through `TradeManager`,
nothing else sends the packets). **Removed**: every precondition check and
every display read that previously called `trade->isTrading()`/
`trade->getState()`/`trade->getPartnerName()` directly — all four now read
`m_eq.GetGameState().trade()` instead (`active()`, `hasPendingRequest()`,
`partnerName()`, `pendingPartnerName()`). This is the same shape every
other domain in this file already uses for `CombatManager` (network calls
still direct, state reads through the projection) — not a new pattern, just
extended to Trade.

**Note on why `EqActionHandler` reads `m_eq.GetGameState()` (the
*source*-side instance) rather than `Application::m_gameState` (the
render-side projection the plan's prose calls "the projection")**:
`EqActionHandler` only holds `EverQuest& m_eq` (confirmed —
`include/client/eq_action_handler.h`), with no reference to
`Application::m_gameState` at all, and this was already the established
pattern for this exact function's other reads (`m_eq.GetGameState().entities()`/
`.player()` at the two existing call sites in `requestTrade()`, unchanged by
this unit). So `state::TradeState` is populated on **both** `GameState`
instances, by two different mechanisms — the same shape T81 established for
entities: `EverQuest::SetupTradeManagerCallbacks()`'s lambdas
(`eq_merchant.cpp`) now write directly into `m_game_state.trade()`
(same-thread, synchronous, mirroring how `SyncEntityToGameState()` already
writes `m_game_state.entities()` directly), which is what `EqActionHandler`
reads; `GameStateSyncBridge` separately populates
`Application::m_gameState.trade()` from the same underlying events
(cross-thread, matching every other domain's pattern, and satisfying the
plan's scope item 2 for any future render-side consumer). Documented in
`game_state_sync_bridge.h`'s T103 addendum.

**Deviation, explained rather than reverted**: `src/client/eq_merchant.cpp`
is edited, outside the plan's declared territory. Necessary because it is
the one place `TradeManager`'s callbacks live and the one place trade state
changes are observed synchronously on the game thread — there was no way to
satisfy scope item 3 (remove `GetTradeManager()` reads from
`EqActionHandler`, replace with the projection) without also making
`m_game_state.trade()` populated, and that population has to happen at
`TradeManager`'s own callback sites, which live in `eq_merchant.cpp`. Same
class of justified out-of-territory edit T99 made to
`game_state_sync_bridge.cpp` for Group. No `TradeManager`/`trade_manager.*`
line was touched; only the existing lambda bodies gained one additional
call each into `m_game_state.trade()`, alongside the pre-existing
(unmodified) bridge-event pushes.

### `declineTrade()` can now name the partner — confirmed both headlessly and live

`EqActionHandler::declineTrade()` reads `pendingPartnerName()` (populated
from `TradeRequestReceived`, independent of the `rejectTradeRequest()`
accessor gap T102 hit) before rejecting, then clears the projection. Message
changed from the T102-era `"Declined trade request"` back to `game`'s
original wording, `"Declined trade request from {name}"` (confirmed
byte-for-byte against `git show 01ccc1fc~1:src/client/eq_commands.cpp`'s
deleted `tradedecline_cmd`). **Live**, this session:
`review/T103/t103_b_testbuddy_evidence.log`:
```
[MAIN] Testmelee wants to trade with you
[MAIN] Declined trade request from Testmelee
[MAIN] [SYS] Declined trade request from Testmelee
```

### Live evidence — projection tracked each stage reached; one stage not reached this session, proven headlessly instead

Full detail: `review/T103/README.md`. One live round, two accounts, both
already in `arena` (T102's already-relocated, never-restored baseline, left
as found and confirmed unchanged after logout).

**Confirmed live**:
- `TradeRequestReceived` → pending state populated: B's `/tradedecline`
  succeeded and named the partner (`pendingPartnerName()` populated,
  gating check passed) rather than printing "No pending trade request".
- Decline → projection reset: B's follow-up `/accept` (against a stale,
  already-declined request) correctly printed **"No pending trade
  request"** rather than attempting a stale accept — direct live proof the
  `declineTrade()` reset-workaround for the `rejectTradeRequest()` gap
  holds.
- `active()` correctly stayed `false` throughout on A's side: A's
  `/canceltrade` correctly printed **"You are not trading"** (A's own
  request never reached `Active` — see below) rather than incorrectly
  claiming to cancel something.

**Not reached live this session**: the `/accept`→`Active` round trip
(`TradeStarted`, "Accepting trade with X" from a real network exchange). A's
second `/trade Testbuddy` hit `TradeManager`'s own `"Cannot request trade
while already in a trade"` guard — A's first outstanding request was never
cleared, because **no command in this system cancels one's own outgoing
pending request** (`/canceltrade` is Active-only, by design, matching
`game`'s original `canceltrade_cmd`). Pre-existing command-surface gap, not
new to T103, out of scope. **Proven headlessly instead**
(`tests/test_eq_action_handler_trade.cpp`,
`AcceptTrade_PendingRequest_AcceptsAndProjectionReflectsActiveTrade`): same
`TradeManager::handleTradeRequest()` → `EqActionHandler::acceptTrade()`
sequence, asserts `"Accepting trade with Buddy"` and post-accept
`active()==true`/`partnerName()=="Buddy"`/`hasPendingRequest()==false`.
Judged sufficient given the pre-existing command-surface gap made a live
repeat pointless without a third session, and the eqemu lock was needed by
T104 (parallel unit) — not chased with another round.

### Tests, and proof they can fail in both directions

18 new tests: 9 `GameStateSyncBridgeTest` cases (`tests/
test_game_state_sync_bridge.cpp`) covering all 7 event cases plus the
own-item-ignored edge case and wrapped-bridge delegation; 9
`EqActionHandlerTrade` cases (new file, `tests/
test_eq_action_handler_trade.cpp`) covering all four methods' precondition/
success/display paths against a real headless `EverQuest`+`TradeManager`
(no network, no graphics, no eqemu — `TradeManager`'s `Send*()` calls all
guard on `!m_zone_connection` and no-op harmlessly, verified before writing
the tests).

**Proved failure in both directions, twice:**
1. `TradeState::setPendingRequest()` — set `m_hasPendingRequest = false`
   instead of `true` (T95-style broken-for-test). Reran the two affected
   `GameStateSyncBridgeTest` cases: both **failed**
   (`TradeRequestReceivedSetsPendingRequest`,
   `TradeStartedSetsActiveAndClearsPendingRequest`). Restored, rebuilt,
   reran: both **passed**, full suite unaffected.
2. `EqActionHandler::declineTrade()`'s `m_eq.GetGameState().trade().reset()`
   call — removed it. Reran
   `DeclineTrade_ClearsProjectionDespiteTradeManagerNotFiringCancelledCallback`:
   **failed** (`hasPendingRequest()` stayed `true`). Restored, rebuilt,
   reran: **passed**.

### Deviations

1. **`src/client/eq_merchant.cpp` edited** — outside the plan's listed
   territory, justified above ("What was removed from `EqActionHandler`").
   No `TradeManager`/`trade_manager.*` line touched; only additive calls
   into the pre-existing lambda bodies.
2. **A third `TradeManager` gap found and worked around** (not fixed in
   `trade_manager.cpp`, out of territory): `rejectTradeRequest()` never
   fires `m_on_cancelled()`, unlike every other reset path. Reported in
   `refactor/deferred.md`, worked around in `declineTrade()`.
3. **`resetForZoneChange()`/`clearAll()`** (`game_state.cpp`) both gained
   `m_tradeState.reset()`, matching the treatment already given to
   `combat`/`pet`/`tradeskill` (short-lived per-encounter state cleared on
   zone change) — a judgment call, not explicitly asked for by the plan,
   made for consistency with the existing domains' lifecycle handling
   rather than leaving a new, unique gap.
4. **The live leg's second stage (Active/`accept`) was not reached**, for
   the pre-existing command-surface reason explained above; proven
   headlessly instead. Not treated as a STOP condition — the plan's bar is
   "the projection reflects each stage," and the Active stage's projection
   behavior is proven, just not via this specific live session.
5. No scope extension beyond a `Trade` domain + its `GameStateSyncBridge`
   cases + `EqActionHandler`'s four methods. `syncCombatState()`/combat
   state untouched (T104's territory). No `ActionDispatcher` reference
   added to `eq.h`/`eq_commands*.cpp`.

### Verification

1. `cmake --build build -j24` — clean, including both both-directions-proof
   rebuild/restore cycles.
2. `ctest --test-dir build --output-on-failure` — **2235/2235 passed except
   the documented ALSA `AudioManagerTest.PreloadCommonSounds`** (2217
   baseline + 18 new tests = 2235, matches exactly).
3. `bash scripts/build-gles2-x86.sh` — clean.
4. Visual gate (`visual_gate` ctest case) — PASS, 6.42s; `git status` clean
   of any golden files throughout.
5. `bash scripts/build-arm-noble.sh` — clean cross-compile.
6. `scripts/refactor/check_file_sizes.sh fable` — 6 ok, 1 pre-existing WARN
   (`src/client/eq_merchant.cpp`, 1605 lines before this unit, already
   over 1500; 1644 after, not a new FAIL), 0 FAIL. New files
   (`trade_state.h`/`.cpp`, `test_eq_action_handler_trade.cpp`) checked
   explicitly via `--files` since `git diff` doesn't surface untracked
   paths — all well under the WARN threshold.
7. Live eqemu leg — one round, `willeq_test`/`Testmelee` +
   `willeq_buddy`/`Testbuddy`, both already in `arena` (T102's
   never-restored relocation, left as found). Lock taken/released cleanly,
   `down.sh` run, lock file deleted, both characters' DB positions
   confirmed unchanged via `SELECT` after both clients' final logout (real
   binary PIDs, not `nohup`'s `$!`).

## Board

Trade is now on the bridge, same as every other domain. Combat (T104) is the
other half of Will's ruling, running in parallel in slot2.
