# T44 — Feed spell state from the bridge, and settle how countdowns work

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `src/client/bridge/game_state_sync_bridge.cpp` (+ header),
`syncSpellState()` in `src/client/application.cpp`,
`src/client/state/spell_state.cpp` (+ header) if the countdown model
changes, the spell publish sites in `src/client/spell/spell_manager.cpp`
and `src/client/eq.cpp`, and tests.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T40_pet_state_via_bridge.md` (goal, target invariant, decorator
seam) and `refactor/T42_inventory_publishes_and_convert.md` (what an
events-first fix looks like when coverage is incomplete).

Third cut of the Application→EverQuest decoupling. Pet and inventory are
done.

## Why this one is harder, and what to decide first

`syncSpellState()` polls two genuinely different kinds of thing:

**Discrete state** — casting started/stopped, which spell is in which
gem, gem state transitions. This is exactly like pet and inventory: it
maps onto events cleanly.

**Continuous values** — `updateCastProgress(getCastTimeRemaining())` runs
*every tick while casting*, and `setGemCooldown(remaining, total)` runs
*every tick while a gem refreshes*. These are countdowns. There is no
event for "another 16 ms elapsed", and **publishing one every tick would
be a poll with extra steps** — strictly worse than what is there now.

**The event schema already anticipates this.** `SpellGemChangedData`
carries `cooldownRemainingMs`, `cooldownTotalMs` *and* `memorizeTotalMs`,
with the comment "(for progress computation)"; `SpellCastStartedData`
carries `castTimeMs`. Whoever designed these intended **publish once,
consumer computes elapsed** — not per-tick republishing.

So the first thing to settle, before writing conversion code:

- **Preferred**: `SpellState` stores start-time + total and computes
  remaining/progress on read. No ticking, no polling, and the events
  already carry what it needs.
- **Acceptable**: the state is advanced by a *local* tick that reads
  nothing from `EverQuest`. That still satisfies this arc's goal — the
  point is removing the cross-boundary reach, not event purism.
- **Not acceptable**: publishing a progress event per tick.

Say which you chose and why. If changing `SpellState`'s model would break
its readers, that is worth knowing — check who reads
`getCastProgress()`/cooldown values before committing.

## A property worth not breaking

`SpellState` **already publishes** `SpellGemChanged`/`CastingStateChanged`
onto the EventBus from its own setters (`spell_state.cpp`), and
`ConsoleRenderer` subscribes. So today the EventBus fan-out works — it is
just driven by the poll.

After conversion, the decorator drives those same setters from bridge
events and the EventBus fan-out keeps working unchanged. Preserve that;
do not have the decorator publish to the EventBus itself, or consumers
will see duplicates.

## Coverage — check before converting

`SpellGemChanged` has 3 publish sites, `SpellCastStarted` has 1, and
**`SpellCastComplete` has none — defined but never published**, the same
shape as `PetStatsChanged` before T40 and the inventory gaps before T42.

Establish end-to-end whether casting state (start, progress basis,
completion, interruption) is fully reconstructible from what the game
side publishes to the bridge. If it is not, fix the publishes first, as
T42 did — and if that turns out to be a large job, **STOP and report
rather than half-converting**; a partially-fed spell state is worse than
the poll, which self-corrects.

Spell state is in the full-state snapshot (`SpellGemChanged`,
`SpellbookSnapshot`), so initial state is covered since T41 — that half
you can rely on.

## Scope

1. Settle the countdown model (above) and implement it.
2. Fix any publish gaps needed to make casting state reconstructible.
3. Extend `GameStateSyncBridge` with the spell cases.
4. Delete `syncSpellState()`, its `m_lastIsCasting`/`m_lastCastingSpellId`
   members, and the `GetSpellManager()` read from `Application` — if that
   accessor loses its last caller, say so.
5. Tests: decorator cases, and the countdown model (a cast that starts,
   progresses and completes without any per-tick publish).

## STOP conditions

- Casting state cannot be made reconstructible without a large publish
  campaign → STOP, report the gap list, do not half-convert.
- The countdown cannot be modelled without per-tick publishing → STOP and
  report. **"Spell keeps a local tick" is a legitimate answer**; "spell
  publishes 60 events a second" is not.
- Changing `SpellState`'s countdown model breaks readers in a way that
  needs its own unit → STOP with the reader list.
- Any behaviour change beyond where spell state comes from → STOP.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35);
   the golden replay does not skip (T36); `FullStateSnapshot.
   HeadlessZoneInPopulatesGameState` (T43) asserts spell gems reach the
   bridge headless — if it fails, you broke the thing T43 just pinned.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. Unit tests per item 5, including that no progress event is published
   per tick.
5. **Live acceptance**: a real eqemu session casting a spell — gem goes
   to Casting, cast completes, gem enters and leaves cooldown — with
   `syncSpellState()` deleted, and cast progress/cooldown visibly
   advancing without per-tick events. eqemu LOCK FIRST; `up.sh` only;
   restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190;
   `down.sh` after. Evidence under `benchmarks/audits/T44/`.
   Note Testchar's class may not have useful spells — if casting cannot
   be exercised, say so plainly and lean on the unit tests rather than
   inventing a fixture.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

### Countdown model: PREFERRED (start-time + total, computed on read)

Chosen, not the "acceptable" local-tick alternative. Before touching
anything, grepped every reader of `SpellState::castProgress()`/
`getGemCooldownRemaining()`/`getGemCooldownProgress()` outside the class
itself: there were **none** — `Application::syncSpellState()` was the only
caller of the setters (`m_gameState->spells().{setCasting,
updateCastProgress,setGem,setGemCooldown}`), and nothing outside
`spell_state.cpp` ever read the countdown getters. That made changing the
storage model unconditionally safe (no reader to break), and the event
schema already carried what the preferred model needs
(`SpellGemChangedData.cooldownRemainingMs/cooldownTotalMs/memorizeTotalMs`,
`SpellCastStartedData.castTimeMs`) — "publish once, consumer computes
elapsed" was clearly the intended design, not an afterthought.

Implementation (`include/client/state/spell_state.h`,
`src/client/state/spell_state.cpp`): `GemData` now stores
`cooldownStartTime` (a `std::chrono::steady_clock::time_point`, backdated by
`totalMs - remainingMs` when `setGemCooldown()` is called) instead of a raw
`cooldownRemainingMs`; casting state stores `m_castStartTime` the same way.
`getGemCooldownRemaining()`/`getGemCooldownProgress()`/`castTimeRemaining()`/
`castProgress()` compute elapsed from `steady_clock::now()` on every read.
`updateCastProgress()` (the per-tick setter the old poll called every tick)
is **deleted outright** — nothing calls it, by construction, since a single
`setCasting()`/`setGemCooldown()` call is enough for all future reads to be
correct. No ticking, no polling, and the unit tests prove it (see below) —
this rules out the "not acceptable" per-tick-publish option cleanly, no
STOP needed.

### Casting state reconstructibility: not reconstructible as found — three gaps, all fixed

The plan's named gap (`SpellCastComplete` defined, never published) was real,
but tracing every place `SpellManager::m_is_casting` flips found **two more**
gaps beyond it, one found by code reading and one found only by running the
live leg:

1. **`SpellCastComplete` never published** (the plan's named gap). Fixed by
   adding `SpellManager::publishCastComplete(spellId, CastResult)`
   (`src/client/spell/spell_manager.cpp`) and calling it from all three
   places `m_is_casting` flips false for *our own* cast that were reachable
   by reading the code: `completeCast()` (success — the `if(success)` guard
   is honest: it fires `Success` or `Interrupted` depending on the `success`
   parameter, even though every current caller happens to pass `true`),
   `interruptCast()` (player-initiated interrupt), and `handleInterrupt()`'s
   own-cast branch (`Fizzle` or `Interrupted` depending on `message_type`,
   reusing the `result` the function already computes).
2. **`SpellCastStarted` timing/content gap.** The only existing publish site
   (`eq_player_info.cpp`'s `ZoneProcessBeginCast`) fires from the
   *server-echoed* `BeginCast` packet, for *any* caster. Two problems: (a)
   for our own gem/item casts, `SpellManager::beginCastFromGem()`/
   `beginCastById()` already flip `m_is_casting = true` **locally, before
   any network round-trip** (the same optimistic-start the deleted poll saw
   on the very next tick) — waiting for the server echo would add RTT lag
   the poll never had; (b) `SpellCastStartedData` had no `targetId` field, so
   even the server-echoed event couldn't carry what `SpellState::setCasting`
   needs. Fixed by adding `targetId` to `SpellCastStartedData`
   (`include/client/state/event_bus.h`) and a new
   `SpellManager::publishCastStarted()` called synchronously from both
   `beginCastFromGem()`/`beginCastById()` at the moment they set
   `m_is_casting = true`; `eq_player_info.cpp`'s existing publish (which
   still fires for every caster, including NPCs, so the decorator can filter
   to ours) now also fills `targetId` for our own casts via
   `m_spell_manager->getCurrentTargetId()`.
3. **A fourth cast-end path, found live, not by code reading.**
   `SpellManager::update()` has a "waiting for server confirm" 5-second
   timeout (distinct from the "cast ongoing too long" timeout that already
   routes through `completeCast()`): if the server never echoes `BeginCast`
   at all, this branch flips `m_is_casting = false` and resets the gem
   directly, **bypassing `completeCast()`/`interruptCast()`/
   `handleInterrupt()` entirely**. This was invisible from code reading
   because it looked like a rare edge case; the live leg hit it directly (an
   invalid-target cast — see below — got mana deducted server-side but no
   `BeginCast`/`Action`/`Interrupt` packet ever came back, so this was the
   *only* thing that ever cleared `m_is_casting`). Without a publish here,
   `GameStateSyncBridge` would have no way to learn the cast ended and
   `SpellState` would report "casting" forever. Fixed with the same
   `publishCastComplete(m_current_spell_id, CastResult::Interrupted)` call,
   covered by a new regression test
   (`SpellManagerCastComplete.ServerConfirmTimeoutPublishesSpellCastComplete`).

With all three fixed, casting state (start, progress basis, completion,
interruption) is fully reconstructible from bridge events. No STOP was
needed — none of the gaps required restructuring, all were additive
publishes at existing state-transition points, matching the shape T42
established for "fix the publishes, then convert."

`SpellGemChanged` needed no publish-side fix: `SpellManager::publishGemChanged()`
is already the single choke point for every gem-state transition (9 call
sites: begin-cast, interrupt, both timeouts, cooldown expiry, memorize
completion, forget, swap), always fired on the transition itself, never
per-tick.

### The decorator: no double-publish to the EventBus

`GameStateSyncBridge::applyEvent()` (`src/client/bridge/game_state_sync_bridge.cpp`)
gained four new cases — `PlayerSpawnIdSet`, `SpellGemChanged`,
`SpellCastStarted`, `SpellCastComplete` — each of which calls a `SpellState`
*setter* (`setGem`, `setGemCooldown`, `setCasting`, `clearCasting`) on
`m_state.spells()`. It does not call `EventBus::publish()` or anything
equivalent itself, anywhere. `SpellState`'s setters already publish
`SpellGemChanged`/`CastingStateChanged` onto the (separate) `EventBus` that
`ConsoleRenderer` subscribes to, from `spell_state.cpp` — that fan-out is
unchanged, just now driven by the decorator's calls instead of the poll's.
Confirmed by reading `game_state_sync_bridge.cpp` end to end: the only bus
interaction anywhere in the file is the unconditional
`m_wrapped.applyEvent(event)` at the end of `applyEvent()`, which forwards
the *original bridge event* to the wrapped `IrrlichtBridge`/`ConsoleBridge`
— a completely different pipe from `SpellState`'s internal `EventBus`.

`SpellCastStarted`/`SpellCastComplete` filter to `d.casterId ==
m_state.player().spawnId()` so an NPC's or another player's cast doesn't
perturb our casting state; that required a new `PlayerSpawnIdSet` case too
(feeding `m_state.player().setSpawnId()`), since nothing previously fed
`Application::m_gameState`'s player spawn id from the bridge. It's published
reliably at zone-in (three sites in `eq_entities.cpp`, part of T41's
full-state snapshot), well before any cast can occur.

### What was deleted

- `Application::syncSpellState()` (full body, `src/client/application.cpp`)
  and its declaration (`include/client/application.h`).
- `m_lastIsCasting`/`m_lastCastingSpellId` (`include/client/application.h`).
- The `syncSpellState();` call in `syncGameStateFromClient()`.

**`EverQuest::GetSpellManager()` was NOT deleted** — unlike T40's pet
accessors, it does not lose its last caller: `spell/spell_manager.cpp`'s own
test suites (`test_spell_manager.cpp`, `test_spell_manager_timing.cpp`) call
it dozens of times, and internally `EqActionHandler`/`ActionDispatcher` and
other in-territory `EverQuest` logic still use it for their own purposes.
Only `Application`'s cross-boundary read of it is gone.

### Live acceptance

**Testchar cannot cast**: confirmed via DB (`class=1` — Warrior, `level=2`)
before touching anything, matching the plan's anticipated caveat exactly. A
second seeded character on the *same account* (`willeq_test`), `Testcaster`
(`class=12` — Wizard, `level=1`), already exists for exactly this purpose —
`scripts/eqemu/env.sh`'s own comment documents it: "Characters (created via
official client): Testchar+Testmelee(warrior)+Testcaster(wizard) on
willeq_test." Used it instead of inventing a fixture.

**Two more pre-existing obstacles found live** (neither caused by this unit,
neither fixed — both out of T44's territory, documented here per the
plan's "say so plainly" instruction rather than silently worked around):

1. The GM command `#scribespells` (used to give `Testcaster` castable
   spells) scribes server-side correctly (`character_spells` table gets the
   rows) and the server also proactively sends unsolicited
   `HC_OP_MemorizeSpell` "confirmation" packets for gem slots — but
   `eq.cpp`'s handler for that opcode (`DispatchZonePacket`'s
   `HC_OP_MemorizeSpell` case) only acts on it when
   `m_pending_scribe_spell_id` is set (i.e. only for a scribe *we*
   initiated); the "else if (slot < MAX_SPELL_GEMS...)" branch is a
   log-only stub whose comment ("SpellManager handles this via other
   mechanisms") is not true — nothing else handles it. Net effect: the
   client's local spellbook/gem tracking never learns about GM-driven
   scribing, so `/mem` fails with "not in spellbook" even though the spell
   genuinely is scribed server-side. **Worked around**, not fixed: seeded
   `character_spells`/`character_memmed_spells` directly for `Testcaster`
   (id 67) before zone-in — `INSERT ... (67, 0, 54)` (Frost Bolt, gem 1) —
   so the character zoned in already scribed and memorized via the ordinary
   `PlayerProfile`/full-state-snapshot path T41 already established (the
   same path `FullStateSnapshot.HeadlessZoneInPopulatesGameState` pins).
   Deleted both rows and reset `Testcaster`'s position back to its original
   values after the run (`DELETE FROM character_spells/character_memmed_spells
   WHERE id=67`, position restored — verified via `SELECT` before/after).
2. **Found only by running the live leg, not by code reading**:
   `Application::updateGameState()` (`src/client/application.cpp`) gates
   `m_eqClient->GameTick(deltaTime)` behind `if (m_eqClient &&
   m_graphicsInitialized)` — **unconditionally**, independent of
   `EQT_HAS_GRAPHICS`/`EQT_GRAPHICS=OFF`. In any `--no-graphics` run (both
   build trees — `m_graphicsInitialized` never becomes true without real
   graphics init), `SpellManager::update()` never runs at all, so gem
   cooldown expiry, cast timeouts, and memorization progress never resolve
   on their own. This is **pre-existing and not a T44 regression**: the
   deleted poll's `getGemCooldownRemaining()`/`getGemCooldownProgress()`
   were already time-computed reads of `SpellManager`'s own state (same as
   `SpellState`'s new model), but `GemState` itself (`Refresh`→`Ready`) has
   *always* only transitioned inside `updateGemCooldowns()`, so both the old
   poll and the new event-driven `SpellState` are equally blocked from ever
   *seeing* a gem leave cooldown in headless mode — this only ever worked in
   a real graphics run, where `m_graphicsInitialized` is true. Reported, not
   fixed: fixing `Application::updateGameState()`'s gate is a bigger,
   cross-subsystem change (buff manager, movement-adjacent logic sit right
   next to it) well beyond "any behaviour change beyond where spell state
   comes from" — a STOP-shaped change, not a T44 fix.

**What was observed live**, with `syncSpellState()` fully deleted
(`scripts/eqemu/up.sh`, lock taken/released per protocol; evidence at
`benchmarks/audits/T44/t44_live_run_nogfx_tree.log`, the
`EQT_GRAPHICS=OFF` tree, run against `Testcaster`/Frost Bolt/target
`a_rat000`):

```
beginCastFromGem: gem=1 state=1 (0=Empty,1=Ready,2=Casting,3=Refresh,4=Memorize)
validateCast: spell='Frost Bolt' target_type=1 target_id=473
Sent CastSpell packet: spell=54, gem=0, target=473
Beginning cast of 'Frost Bolt' (ID: 54) on target 473          <- local optimistic start,
                                                                    gem -> Casting, via the
                                                                    new publishCastStarted()
BeginCast: caster=483, spell='Frost Bolt' (54), time=1000ms    <- server echo arrives
Server confirmed cast of 'Frost Bolt'
SpellAction: caster=483, target=473, spell='Frost Bolt' (54), effect_flag=0
Gem 1 on cooldown for 2250ms                                   <- startGemCooldown(),
                                                                    gem -> Refresh
Cast of 'Frost Bolt' completed successfully                    <- completeCast(true),
                                                                    publishCastComplete() fires
SpellAction: caster=483, target=473, spell='Frost Bolt' (54), effect_flag=4
```

Cast start (client-optimistic, not waiting on server RTT), gem entering
`Casting`, server-confirmed cast, cast completion (`SpellCastComplete`
publish verified indirectly via the log line the completion path emits —
`completeCast()`'s own `LOG_INFO`), and gem entering cooldown (`Refresh`,
`SpellGemChanged` publish) are all demonstrated live, driven only by
bridge events, with `syncSpellState()` deleted. **Gem leaving cooldown was
not observed live** — blocked by finding 2 above (headless
`SpellManager::update()` never runs) — and per the plan's explicit
allowance, this leg leans on unit tests instead:
`SpellManagerTiming.StartGemCooldownThenBackdatePastDurationTransitionsToReady`
proves `SpellManager` itself publishes `SpellGemChanged` on cooldown expiry,
and `GameStateSyncBridgeTest.SpellGemChangedReadySetsGemReadyAndClearsCooldown`
proves the decorator correctly applies that event to `SpellState`. Also
tried the same script against the graphics-config tree in real (surfaceless
EGL) graphics mode, expecting `m_graphicsInitialized` to be true there and
let the gem actually clear — zone loading (Nektulos Forest, full asset set)
did not finish inside a reasonable wait and the run was killed rather than
let it consume further time; not pursued further given the unit tests
already cover this leg soundly. An earlier attempt casting with no target
selected (`/target` with no argument, which turned out to require a name)
surfaced finding 3 above — the server deducted mana but never echoed
`BeginCast`, so nothing but the "waiting for server confirm" timeout ever
cleared casting state, which is exactly the gap that timeout's missing
publish left.

**Progress advancing without any per-tick publish** is proven directly by
`GameStateSyncBridgeTest.CastProgressAdvancesFromSingleEventWithNoPerTickPublish`:
applies exactly one `SpellCastStarted` event, sleeps 150ms, asserts
`castProgress()`/`castTimeRemaining()` moved, and asserts the wrapped
bridge's applied-event count is still 2 (`PlayerSpawnIdSet` +
`SpellCastStarted`) — nothing else was applied or needed to be for the
value to advance.

**Testchar** was never touched — DB position confirmed identical before and
after (`zone_id=36, x=-77.9232, y=21.9086, z=3, heading=190`).

### Verification bar

1. Both configures build clean. `cmake --build build -j24` (graphics) and
   `cmake --build build-nogfx -j24` (`EQT_GRAPHICS=OFF`) — clean, `willeq`
   linked in both.
2. `ctest --output-on-failure`: graphics tree 1851/1852 passed (documented
   `AudioManagerTest.PreloadCommonSounds` ALSA exception, same as prior
   units); `build-nogfx` 1420/1421, same sole exception. RDP suite (graphics
   tree only, as in prior units) unwaived, no `Skipped`. Golden replay
   (`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`) ran (not
   skipped) and passed in both trees, unchanged. `FullStateSnapshot.
   HeadlessZoneInPopulatesGameState` (T43) passed — the raw event-publish
   path it pins (`eq.cpp`'s full-snapshot `SpellGemChanged` publish) was not
   touched by this unit.
3. Unit tests: `test_game_state_sync_bridge` grew from 10 to 20 tests (the
   10 new ones cover `PlayerSpawnIdSet`, all three `SpellGemChanged` gem
   states including cooldown, `SpellCastStarted`/`SpellCastComplete`
   self-vs-other filtering, the no-per-tick-publish countdown proof, and
   delegation-to-wrapped-bridge for the new cases).
   `test_spell_manager_timing` grew by 5 tests (the four `publishCastComplete`
   call sites, including the live-discovered server-confirm-timeout one, plus
   a negative "other caster does not publish" case).
   `test_spell_manager` gained a `SpellCastStarted`-publish assertion on its
   existing `beginCastFromGem` success test. `test_state_modules`'s
   `SpellStateTest` suite updated for the new time-based model (its two
   tests that depended on `updateCastProgress()`/exact-equality countdown
   reads now use real sleeps/`EXPECT_NEAR` with tolerance, matching this
   file's own "public accessors only" charter rule and the tolerance
   pattern `SpellManagerTiming`'s existing backdating tests already use).
4. `bash scripts/refactor/check_file_sizes.sh --files <all touched paths>`
   (run from this slot on this task branch): 12 ok, 2 WARN
   (`application.cpp` at 2287 lines — pre-existing at 2358 on `fable`,
   *shrank*; `eq_player_info.cpp` at 2021 — pre-existing at 2012, grew by 9
   lines for the `targetId` fill-in), 0 FAIL.
5. Live acceptance: see above.

### Deviations

- `include/client/state/event_bus.h`: added `targetId` to
  `SpellCastStartedData` — needed for casting-state reconstructibility (gap
  2 above), not itself in the plan's listed territory but a minimal,
  additive, source-compatible struct field addition, same class of
  deviation T42 took for `InventoryManager::processItemPacket()`'s
  signature change.
- Seeded and then deleted `character_spells`/`character_memmed_spells` rows
  for `Testcaster` (account `willeq_test`, same account as `Testchar`) to
  drive the live leg, working around the pre-existing `HC_OP_MemorizeSpell`
  gap described above. Fully reverted (`DELETE ... WHERE id=67`, position
  reset), verified via `SELECT` before finishing.
- Two pre-existing bugs found and reported, not fixed (both out of T44's
  territory and both would be STOP-shaped changes on their own): the
  `HC_OP_MemorizeSpell` gem-slot branch in `eq.cpp` (log-only, does nothing),
  and `Application::updateGameState()`'s `m_graphicsInitialized` gate on
  `GameTick()` blocking all headless `SpellManager`/buff-manager ticking.
- `SpellManager::completeCast(false)` now publishes `SpellCastComplete` with
  `CastResult::Interrupted` for correctness/honesty, even though no current
  caller passes `false` — flagged since it's untested-by-production-path
  (only reachable via the new unit test that constructs the scenario
  directly), not a behavior change for any real caller today.
