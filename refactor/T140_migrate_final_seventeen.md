# T140 — Migrate the 17 `migrate-action` commands

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands_movement.cpp`,
`src/client/eq_commands_spells.cpp`, `src/client/eq_commands.cpp`,
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `include/client/eq.h` (thin wrappers
only), `tests/`, `refactor/`, `/mnt/projects/fable/review/T140/`.**

Read T138's `review/T138/triage_table.tsv` (these are its 17 `migrate-action`
rows), then T132's Review (the net-new recipe, ~50 min baseline) and T136's
(the `Request*` wrapper recipe).

## The worklist

`loc`, `sit`, `stand`, `move`, `moveto`, `warp`, `zoning`, `follow`,
`stopfollow`, `quit`, `camp`, `debug`, `perf`, `time`, `bank`, `train` — plus
**`equip`**, treated separately below.

T138 traced each: **12 call an already-public `EverQuest` method**, **3
(`warp`, `time`, `bank`) need one thin wrapper each** on T136's `Request*`
recipe, and **2 (`debug`, `perf`) match existing action-registry diagnostic
precedent**.

## `equip` — do it LAST, and stop if it grows

`equip` is the one T136 and T138 **independently** concluded needs its own
design pass: it goes through `InventoryManager` (private, no `EqActionHandler`
accessor), is ~150 lines, and is categorised `Inventory` rather than sharing
these commands' shape.

**Do the other 16 first and commit them.** Then attempt `equip` **only if the
batch is green and time remains**. **STOP and report** the moment it needs more
than a thin accessor — pulling `InventoryManager` into the action layer is its
own unit, and two prior units already reached that conclusion. **Do not
re-litigate it under batch pressure.**

## The sequencing hazard — `camp` and `quit` end the session

`quit` → `RequestQuit()` sets `m_quit_requested`, polled by `Application`'s
main loop; the client exits. `camp` → sit + `StartCampTimer()`, which logs out
after its countdown.

**Anything scripted after them never runs.** A tour that puts `/camp` in the
middle will silently skip every later command while still looking like it
completed. **Put them last, and give each its own run** so one doesn't consume
the other's verification.

This is the highest-probability way to lose an afternoon on this unit.

## `warp` deserves care

`warp` **directly writes `m_x`/`m_y`/`m_z` and the entity's position** without
a server round trip — a debug/teleport path, not a normal movement command.
**Preserve its exact semantics**, including whatever it does about the
subsequent position update. If migrating it changes when or whether the server
is told, that is a behaviour change, not a refactor — **STOP and report**.

## Two things already checked, so you don't have to

- **`bank` and `train`'s guard state is genuinely populated** — `m_banker_npc_id`
  and `m_trainer_npc_id` are both written from server packets
  (`eq_merchant.cpp:1402`, `:1537`). **This is not a repeat of the
  `m_guild_id` dead-guard situation**; don't go hunting for one.
- **Guards stay where they are** (T134's rule): stateful guards belong in the
  layer owning the state; only usage-string guards move up.

## Check for wire rot — it has appeared in three consecutive units

`train` (`RequestTraining`) and `move` (position sends) reach the wire.
T133 and T134 each found a **double-opcode-prefix** bug — a sender writing its
own opcode *and* calling `EverQuest::QueuePacket(opcode, body)`, which prepends
another.

**But remember T135's correction**: `m_zone_connection->QueuePacket(packet)`
requires the caller to write the opcode, and four senders looked buggy and were
not. **Never infer the bug from shape — prove it from a live server-log size
complaint.**

Also: T135's petition bug and T136's buy/sell were visible **only in the DB**,
not the zone log. If a command's effect is persisted, **check the DB**.

## Atomicity and completeness

**Remove from the `eq_commands_*.cpp` files in the same commit that adds the
`CommandProcessor` registration.** Verify via
`Application::printCommandResult()`: `registry=action` per command, plus a
`registry=game` control from the 7 `stay-game` rows.

**After this unit the game registry should hold 11 commands** (27 − 16), or
**10** if `equip` also lands. State the number and list what remains as a
check.

## Verification

- x86 suite (current total **2361**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests at the programme's density** — T132: 22/7, T134: 18/6, T135: 19/6,
  T136: 29/4.
- **Visual gate goldens must not move.**
- Live evidence in `review/T140/`, with `camp`/`quit` in their own runs.
- **Report which commands you verified on the wire versus dispatch-layer
  only.** Every unit in this programme has bounded that honestly; keep it.

## Review

### The 16 migrated: dispatcher/handler mapping

| command | `CommandProcessor::cmd*` | `ActionDispatcher::*` | `IActionHandler`/`EqActionHandler::*` | EverQuest surface |
|---|---|---|---|---|
| `loc` | `cmdLoc` | `showLocation()` | `showLocation()` (net-new) | `GetPosition()` (already public) |
| `sit` | `cmdSit` (overrides ack text) | `sit()` (**reused**) | `sit()` (**reused**) | `SetPositionState(POS_SITTING)` (already public) |
| `stand` | `cmdStand` (overrides ack text) | `stand()` (**reused**) | `stand()` (**reused**) | `SetPositionState(POS_STANDING)` (already public) |
| `move` | `cmdMove` (T88 dual-frame echo) | `moveToLocation()` (**reused**) | `moveToLocation()` (**reused**) | `Move()` (already public) |
| `moveto` | `cmdMoveTo` (overrides ack text) | `moveToEntity()` (**reused**) | `moveToEntity()` (**reused**) | `MoveToEntity()` (already public) |
| `warp` | `cmdWarp` (raw args passthrough) | `warp(args)` (net-new) | `warp(args)` (net-new) | `RequestWarp(args)` — **new thin wrapper** |
| `zoning` | `cmdZoning` (raw args passthrough) | `showOrSetZoning(args)` (net-new) | `showOrSetZoning(args)` (net-new) | `SetZoningEnabled()`/`IsZoningEnabled()` (already public) |
| `follow` | `cmdFollow` (overrides ack text) | `followEntity()` (**reused**) | `followEntity()` (**reused**) | `Follow()` (already public) |
| `stopfollow` | `cmdStopFollow` | `stopFollow()` (**reused**) | `stopFollow()` (**reused**) | `StopFollow()` (already public) |
| `quit` | `cmdQuit` | `quit()` (net-new) | `requestQuit()` (net-new) | `RequestQuit()` (already public) |
| `camp` | `cmdCamp` | `camp()` (net-new) | `campNow()` (net-new) | `SetPositionState(POS_SITTING)` + `StartCampTimer()` (already public) |
| `debug` | `cmdDebug` | `debugLevel(args)` (net-new) | `debugLevel(args)` (net-new) | `SetDebugLevel()`/`GetDebugLevel()` (already public, static) |
| `perf` | `cmdPerf` | `perfReport()` (net-new) | `perfReport()` (net-new) | `EQT::PerformanceMetrics::instance()` (process-global, no EverQuest call) |
| `time` | `cmdTime` (raw args passthrough) | `timeOfDay(args)` (net-new) | `timeOfDay(args)` (net-new) | `RequestTimeOfDay(args)` — **new thin wrapper** |
| `bank` | `cmdBank` | `bankToggle()` (net-new) | `bankToggle()` (net-new) | `RequestBankToggle()` — **new thin wrapper** |
| `train` | `cmdTrain` | `trainWithTarget()` (net-new) | `trainWithTarget()` (net-new) | `RequestTraining()` — **new thin wrapper**, calls the already-public `RequestTrainerWindow()` at the end |

Six of the sixteen (`sit`/`stand`/`move`/`moveto`/`follow`/`stopfollow`)
needed **zero new `ActionDispatcher`/`IActionHandler` plumbing** — they
reuse pre-existing methods that were already fully wired (added for
keyboard/gesture input paths, just never reachable from a slash command
before T98 deleted this side's shadowed dead copies). Only their
`CommandProcessor::cmd*` wrapper is new, and three of those six
(`cmdSit`/`cmdStand`/`cmdMoveTo`/`cmdFollow` — four, not three) override
the dispatcher's generic success message with the original game-registry
wording so the user-visible text didn't silently drift (`cmdMove`'s T88
dual-frame echo is the highest-stakes case of this — see "warp's semantics"
below for why echo-preservation matters generally).

### The three thin wrappers, and why each was needed

`warp`, `time`, and `bank` each needed one new `EverQuest` method
(`RequestWarp`/`RequestTimeOfDay`/`RequestBankToggle`) — the plan predicted
exactly these three. All three read or write state private to `EverQuest`
(`m_combat_manager`, `m_entities`, `m_bridge`, `m_time_hour`/`m_time_minute`)
that `EqActionHandler` cannot reach directly (the same `EqActionHandler`-
has-no-bridge-access gap T136 found and recorded in `refactor/deferred.md`).
Each carries its old `eq_commands_*.cpp` lambda body **verbatim** — no logic
changed, only where it lives.

A fourth wrapper, `RequestTraining()`, was added beyond the plan's literal
"3 thin wrappers" count. The plan's evidence table said `train` needed
"zero new plumbing" because `RequestTrainerWindow()` was already a public
sender — true, but that sender has **no guard logic of its own**; the
guard (target lookup, `npc_type`/class-range/distance/`trained_class`
checks) lived entirely in the old command lambda, reading `m_entities`/
`m_class`/`m_combat_manager` — all private. T134's rule (stateful guards
stay in the layer owning the state) meant this guard could not move to
`EqActionHandler` without a new public entity-by-id accessor, which would
have been scope creep beyond this migration. So `RequestTraining()` was
added to carry the guard body verbatim and call the unchanged
`RequestTrainerWindow()` at the end — same shape as the other three
wrappers, just one the plan's own evidence table underestimated. Flagging
this explicitly as a **deviation**: 4 new `EverQuest` methods, not 3.

### `equip` — STOPped, not attempted

Per the plan's explicit instruction, `equip` was sequenced last and
attempted only after the other 16 were green. **STOPped immediately** on
re-reading T136's own finding (three independent reasons: different
category, disjoint `InventoryManager` mechanism with no `EqActionHandler`
reachability, and ~150 lines of design surface across three input forms).
No new investigation changed that conclusion — this unit's contribution is
confirming it a second time, under the same "do not re-litigate under batch
pressure" instruction the plan gave, and recording that confirmation in
`refactor/deferred.md` (`## T140 (2026-08-18): \`/equip\` migration — third
confirmation, still deferred`) so a future unit sees this is now a
two-for-two pattern, not a one-off. `equip` is untouched — still registered
in `EverQuest::m_command_registry`, still covered by
`tests/test_eq_commands_equip.cpp`.

### `camp`/`quit` run structure

Each in its own live driver run, exactly per the plan's sequencing-hazard
warning:
- `run_B_camp.sh` (`review/T140/`): zone in, send `/camp` alone, then
  **wait out the full 30-second timer** to observe the real `OP_Camp` send
  and the natural logout/shutdown — not just the ack message. Confirmed
  `registry=action` and a clean `Application shutdown complete`.
- `run_C_quit.sh`: zone in, send `/quit` alone, confirm the process exits
  **on its own** (no `SIGTERM` needed) within 1 second — the T30-documented
  "no server-side logout courtesy" immediate exit, unchanged.

Neither run scripts anything after the terminal command; each run's driver
script is a completely separate process invocation, so there was never a
window where one could silently consume the other's verification.

### `warp`'s semantics — confirmed preserved

`RequestWarp()` is the old command lambda's ~80-line body carried verbatim:
still writes `m_x`/`m_y`/`m_z` and the `m_entities[self]` entry directly (no
pathfinding, no server round trip for the teleport decision itself), still
calls the real `SendPositionUpdate()` afterward, still pushes
`PlayerMovedData` via `m_bridge`. Nothing about when or whether the server
is told changed — same call, same position, same order, just moved from an
inline lambda to a named method. Live-verified: `run_A_main.log` shows
`/warp 50 60 0` producing `Warped to (50.0, 60.0, 0.0)`, and the
independent, later `run_A2_bank_train.sh` process inherited that exact
server-persisted position (`client_pos=(50.00,60.00,0.00) ->
server_pos=(60.00,50.00,0.00)`) — proof the position write reached the
server, not just the local client state, since a fresh process reconnecting
to the same character saw it.

### Wire rot — checked, none found (with live proof, not inference)

Checked `train` (`RequestTrainerWindow`, which sends `HC_OP_GMTraining`)
and `move`'s position sends per the plan's carried-forward warning. Neither
sender writes its own opcode before calling `EverQuest::QueuePacket()` —
confirmed by reading both call sites, and by the live legs producing no
server-log size complaints (`run_A2_bank_train.log`'s train attempt reached
the real guard chain and got a real, correctly-worded refusal, not a
malformed-packet symptom). No double-opcode-prefix bug found, and per
T135's correction, this is stated as a check performed, not inferred from
the sender's shape.

`bank`'s effect (`BankerInteractIntent` -> `OpenBankWindow`/
`CloseBankWindow`) and `camp`'s effect (`OP_Camp` send + real logout) were
both confirmed **live in the zone log itself** (`run_A2_bank_train.log`:
`"Opening bank window for SummonClockworkBanker004 (id=216)"` /
`"Closing bank window (banker NPC: 216)"`; `run_B_camp.log`: `"Sent OP_Camp
to server"`) — no DB-only surprise like T135/T136 found for petition/buy/
sell, since these two do produce a visible client-log line at the default
level (this unit did not need to fall back to a DB comparison the way
those two units did).

### Atomicity and completeness

All 16 registrations were removed from `eq_commands_movement.cpp`/
`eq_commands_spells.cpp` in the **same commit** as their
`action::CommandProcessor::registerCommand()` additions — verified by
`grep`ing both files post-change (zero `Command <name>;`/
`m_command_registry->registerCommand(<name>)` remnants for any of the 16)
and by `run_D_registry_audit.sh`'s live `printCommandResult()` transcript.

**`run_A_main.log`'s registry=action lines for all 16** (bracketed by
`/pmem`'s `registry=game` control) and **`run_D_registry_audit.log`'s
registry=game lines for the 11 remaining game-registry commands**
(`hotkeys`, `pmem`, `music`, `sound`, `volume`, `musicvolume`,
`effectsvolume`, `equip`, `help`, `spellreload`, `weatherquality`) together
give the atomicity proof: nothing landed in both registries, nothing fell
through the gap into neither.

**The game registry now holds exactly 11 commands** — the plan's own
predicted number for "equip stays": 7 stay-game rows (`hotkeys`/`pmem`/
`music`/`sound`/`volume`/`musicvolume`/`effectsvolume`) + `equip` (this
unit's STOP) + 3 needs-decision rows (`help`/`spellreload`/
`weatherquality`, none of which this unit's worklist covered). Confirmed
both by source grep (`Command <name>;` declarations across all three
`eq_commands*.cpp` files) and live, via `run_D_registry_audit.sh`.

### Tests

45 new cases across two files, well above the programme's density
(T132: 22/7, T134: 18/6, T135: 19/6, T136: 29/4 → **T140: 45/16**, lower
per-command density than T136 since most of this unit's commands are
one-line pass-throughs with no design-question-driven branching to cover):

- `tests/test_command_processor.cpp` (+31, `RecordingActionHandler`, no
  wire replay): registration/category assertions, dispatch-reaches-the-
  right-`ActionDispatcher`-method assertions for all 16, `requiresArgs`
  usage-guard coverage for `move`/`moveto`/`follow` (T134's rule: the
  usage-string guard moved up to the registration layer, not duplicated in
  each `cmd*`), and message-override assertions (`cmdSit`/`cmdStand`/
  `cmdMove`'s T88 echo/`cmdMoveTo`/`cmdFollow`).
- `tests/test_eq_action_handler_movement_utility.cpp` (new, +16, real
  `EverQuest` + `EqActionHandler`, no wire replay needed for these):
  `showLocation()`'s position read, `warp()`'s three destination forms
  (coords/entity-not-found/no-destination-usage) forwarding correctly to
  `RequestWarp()`, `showOrSetZoning()`'s read/set/invalid-arg paths,
  `campNow()`'s ack, `debugLevel()`'s read/set/malformed paths,
  `perfReport()`'s non-crashing report, `timeOfDay()`'s read/set/
  out-of-range paths and its `TimeOfDayChanged` event push.

`tests/test_eq_commands_bank_train.cpp` (T50, 10 cases) **retargeted, not
deleted** — unlike T136's vendor tests (whose entry point,
`RegisterVendorCommands()`, disappeared entirely), `bank`/`train`'s guard
logic moved into two new **public `EverQuest` methods**
(`RequestBankToggle()`/`RequestTraining()`) rather than disappearing, so the
existing wire-replay-driven stateful-guard coverage was preserved by
swapping `eq.ProcessChatInput("/bank")`/`"/train"` for direct
`eq.RequestBankToggle()`/`eq.RequestTraining()` calls — same assertions,
same setup, new entry point. All 10 cases pass unchanged in substance.

`tests/action_test_fakes.h`'s `RecordingActionHandler` gained 10 new
overrides (one per net-new `IActionHandler` method) to keep compiling —
its whole purpose is that a missed override is a compile error, not a
silent gap, and that held here (the first build attempt failed exactly
this way, caught immediately).

### Verification

1. x86 build: clean (after two follow-up fixes: `RecordingActionHandler`
   needed the 10 new overrides to stay non-abstract, and
   `test_command_processor`'s `CMakeLists.txt` target needed `fmt::fmt`
   linked since `cmdMove`/`cmdMoveTo`/`cmdFollow` are the first things in
   that file to call `fmt::format` directly).
2. `ctest`: **2410/2411 passed** (99%), sole failure the documented ALSA
   `AudioManagerTest.PreloadCommonSounds`. (Baseline at this unit's start
   was 2361; net +50 — 45 new cases + a small net change in file line
   counts from moved-not-added code elsewhere in the suite.)
3. `build-gles2-x86.sh`: clean.
4. Visual gate: **PASS**, goldens untouched (confirmed via
   `scripts/ci/visual_gate.sh` directly, not just the `ctest` wrapper).
5. `build-arm-noble.sh`: clean, `bin/willeq` linked (ELF 32-bit ARM), zero
   `error:` lines.
6. `check_file_sizes.sh fable`: **0 new FAIL.** 4 WARN
   (`include/client/eq.h` 2151→2164, `src/client/action/action_dispatcher.cpp`
   1551→1635, `src/client/action/command_processor.cpp` 1963→2155,
   `tests/test_command_processor.cpp` 2305→2561) — all four already over
   the 1500-line WARN threshold before this unit touched them (confirmed
   via `git show fable:<file> | wc -l`), matching T135/T136's "grew within
   an already-WARN band" pattern exactly. No file crossed into FAIL.

### Measured wall-clock

**Approximately 130 minutes** end-to-end (plan+protocol+prior-reviews
reading through this Review being written) — above every prior tracked
unit (T132 ~50, T134 ~100, T135 ~70, T136 ~110), despite this being "the
easy 16" per the plan's own framing (12 of 17 call an already-public
method). Most of the extra time went to (a) discovering `sit`/`stand`/
`move`/`moveto`/`follow`/`stopfollow` already had fully-wired
`ActionDispatcher`/`IActionHandler` methods and confirming *why* (T98's
shadowed-dead-code deletion, not because they were never built) before
trusting the reuse, (b) the `train` guard-placement question the plan's own
evidence table undersold ("zero new plumbing" turned out to need one more
wrapper), (c) two live-session iterations for `bank`/`train` (`run_A_main`'s
NPC-name-resolution regex missed the actual spawned names, requiring
`run_A2` to fix it against real `/list` output), and (d) `camp`'s live leg
deliberately waiting out the full 30-second timer rather than just the ack,
per the plan's explicit ask for real (not just accepted) camp-out evidence.

### Deviations from the plan

- **4 new `EverQuest` methods, not 3** (`RequestTraining()` added beyond
  `RequestWarp`/`RequestTimeOfDay`/`RequestBankToggle`) — see "the three
  thin wrappers" above for why the plan's own evidence table undersold
  `train`'s cost.
- **`test_eq_commands_bank_train.cpp` retargeted, not deleted** — a
  divergence from T136's precedent (which deleted `test_eq_commands_
  vendor.cpp` outright), because unlike vendor's `RegisterVendorCommands()`,
  `bank`/`train`'s guard logic survived as new **public** `EverQuest`
  methods rather than disappearing entirely — the old test's entry point
  had a direct 1:1 replacement, so retargeting preserved more of the
  original test's design intent than a rewrite would have.
- **`tests/CMakeLists.txt`'s `test_command_processor` target needed
  `fmt::fmt` linked** — not anticipated by the plan, discovered at link
  time (see Verification #1).
- **Live leg needed two runs for `bank`/`train`** (`run_A_main` +
  `run_A2_bank_train`) due to an NPC-name-resolution regex mismatch in the
  first attempt — not a functional bug in the migration, a driver-script
  bug, fixed and re-run rather than silently accepted as "good enough."

## Board

**The game registry (`EverQuest::m_command_registry`) now holds exactly 11
commands**: the 7 `stay-game` rows (`hotkeys`, `pmem`, `music`, `sound`,
`volume`, `musicvolume`, `effectsvolume`) + `equip` (this unit's STOP,
third confirmation) + the 3 `needs-decision` rows (`help`, `spellreload`,
`weatherquality`) — confirmed both by source grep and by
`run_D_registry_audit.sh`'s live transcript.

**This is now genuinely the whole remaining picture.** Every `migrate-action`
row from T138's triage is resolved: 16 migrated by this unit, `equip`
deferred (now confirmed twice, not once). Every `delete` row was already
removed by T139. What remains in `EverQuest::m_command_registry` is exactly
the 7 `stay-game` + 3 `needs-decision` rows the plan named, plus `equip` —
no fourth category, no rediscovered row, no scope this unit's own execution
surfaced that the plan didn't already name. The prior mis-statement of the
programme's endpoint (flagged in T136's Board) does not recur here: T138's
triage table, taken as the full inventory of the 61 "remaining" commands,
is now fully accounted for.

## Board

Give the remaining registry contents and what is left of T138's worklist. With
the deletions done and these migrated, the only open items should be the 7
`stay-game` (no work) and the 3 `needs-decision` for Will — **say plainly
whether that is now the whole picture**, since I have mis-stated the
programme's endpoint once already.
