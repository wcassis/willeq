# T49 — NPC-interaction state: migrate the poll to the event stream, and give it a reader (closes the state-layer sweep)

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the acceptance is a live
vendor/banker/trainer session driven entirely by piped commands.**
No sibling unit.

**Territory: `Application::syncNPCInteractionState()` and its `m_last*NpcId`
members, `GameStateSyncBridge::applyEvent()`, the reader command, and
tests.** Do **not** change `eq_merchant.cpp`'s publish sites, the merchant
protocol, or `PlayerState`'s setter semantics — this unit changes who feeds
that state and who reads it, not what it means.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T44_spell_state_via_bridge.md` (the closest precedent — same
poll→event migration shape), then `refactor/T48_vendor_reachability.md`
(the reader composes with its commands), then the deferred entry **"Dead
parallel state copies in the state layer, WITH LIVE READERS"**.

## Why this unit exists

This is the **last item in the state-layer sweep**: pet ✓ (T40),
inventory ✓ (T42), spell ✓ (T44), NPC interaction — open. The sweep's
shape has been identical each time: `Application` polls the client
once per game tick and mirrors into `state::GameState`; the unit replaces
the poll with the already-published bridge event stream via
`GameStateSyncBridge`.

`syncNPCInteractionState()` (`application.cpp:1843-1900`) is the last
poll standing. It mirrors vendor, banker and trainer window state into
`m_gameState->player()`, gated on three `m_last*NpcId` members it keeps
purely to detect edges.

## Two things I established while scoping — verify, do not re-derive

**1. The "missing events" blocker recorded against this item DOES NOT
EXIST.** All six window events are declared *and published* today:

| event | publish site |
|---|---|
| `VendorWindowOpened` | `eq_merchant.cpp:912` |
| `VendorWindowClosed` | `eq_merchant.cpp:1091`, `:1257` |
| `BankWindowOpened` | `eq_merchant.cpp:1295` |
| `BankWindowClosed` | `eq_merchant.cpp:1321` |
| `TrainerWindowOpened` | `eq_merchant.cpp:1429` |
| `TrainerWindowClosed` | `eq_merchant.cpp:1494` |

`VendorWindowOpened`/`BankWindowOpened` carry `WindowOpenedData`
(`npcId`, `npcName`, `sellRate`); the closes carry `WindowClosedData`
(`npcId`); `TrainerWindowOpened` carries the richer
`TrainerWindowOpenedData` (`event_bus.h:501`). **The event stream is a
complete substitute for the poll** — confirm that yourself before
relying on it, the way T42 had to close three mutation paths first.

**2. The migration is a FIDELITY GAIN, and that is your observable
before/after.** Two places where the poll is provably lossy:

- **`sellRate`**: the poll hardcodes `setVendor(npcId, 1.0f, name)`
  (`application.cpp:1858`). The event carries the **real**
  `m_vendor_sell_rate` from the merchant packet
  (`eq_merchant.cpp:895, 909`). After the migration the state layer
  holds the true rate for the first time.
- **`npcName`**: the poll uses the raw `entity.name`; every publish site
  uses `EQT::toDisplayName(...)`, and falls back to `"Merchant"`/
  `"Banker"` where the poll says `"Vendor"`/`"Trainer"`.

**The name strings will therefore CHANGE.** That is a real behaviour
delta, not a wash — grep for anything asserting the old values and say
in the Review what changed and why the new value is the right one.

## The state has NO readers — which is why this unit also lands one

`player().vendorNpcId()/vendorName()/sellRate()/bankerNpcId()/
trainerNpcId()/trainerName()` have **zero production call sites** —
only their declarations in `player_state.h:274-293`. Nothing subscribes
to the six window events on the EventBus either (`console_renderer.cpp:84-123`
subscribes to ten types; none of these).

So today the chain is: poll → setter → publish → **nobody**. A pure
poll→event migration would be correct, consistent with the three prior
sweep units, and **completely unobservable** — and this project has
rejected evidence that proves less than it appears to (P44, P08d round 5).
Will's ruling on scope: migrate **and** land a reader.

**So this unit delivers both:**

1. **The migration** — three (or six) new cases in
   `GameStateSyncBridge::applyEvent()`, calling the existing
   `setVendor`/`clearVendor`/`setBanker`/`clearBanker`/`setTrainer`/
   `clearTrainer`; `syncNPCInteractionState()` and its three
   `m_last*NpcId` members deleted; the Phase 7 comment at
   `application.cpp:1770-1777` updated to say NPC interaction is now
   event-fed (it currently names it as un-migrated).
2. **A reader** — a query command reporting current vendor/banker/
   trainer context **from the state layer**, so a script can ask "am I
   at a vendor, which one, at what sell rate". This is what makes the
   migration observable, and it composes directly with T48's `/vendor`
   + `/vendorlist`.

## THE TRAP THAT DECIDES WHERE THE READER LIVES — read this before writing it

**There are two `state::GameState` instances** (`deferred.md`, "Two
GameState instances"). Populating one and reading the other produces
exactly the symptom of a setter that runs and a reader that sees
nothing — that is the `/assist` bug (T28) and the `cmdHail` gap (T39).

I traced it for you:

- `GameStateSyncBridge` is constructed with **`Application::m_gameState`**
  in *both* modes (`application.cpp:539` graphics, `:616` headless).
- `CommandProcessor` (registry 2) is constructed with
  **`Application::m_gameState`** too (`application.cpp:278`). ✅
- `EverQuest::m_command_registry` (registry 1 — where T48 put `/vendor`,
  `/vendorlist`, `/buy`, `/sell`) reads **`EverQuest::m_game_state`**,
  a *different* object. ❌

**So the reader must go in `CommandProcessor`, not the registry T48
used.** If you put it next to `/vendorlist` it will read an instance
nothing in this unit populates and report empty forever.

Two consequences to handle explicitly, not silently:

- **Name collision / dispatch order.** Registry 1 is tried **first**
  (`application.cpp:1095-1120`). A name already in the `EverQuest`
  registry would be shadowed and your command would never run. Pick a
  name that collides with nothing and *say* you checked.
- **The split is odd and worth naming.** Vendor commands will now live
  in two registries for a defensible reason (one acts on the client, one
  reads the state layer). Note it in the Review as a fresh instance of
  the open **"~37-command registry overlap"** board item — do **not**
  try to fix that here.

## The re-publish is by design — do not "fix" it

`PlayerState::setVendor()` etc. publish `VendorWindowOpened` on the
**EventBus** (`player_state.cpp:182, 209, 220, 242`). After your change a
single window-open will travel: bridge event → sync bridge → setter →
EventBus publish. **That is not a double-publish.** The two are
different channels with disjoint consumers — the bridges do not
`subscribe()` to the EventBus and nothing forwards between them
(established at length in the deferred entry, after an earlier
orchestrator claim to the contrary was checked and corrected). It is
also exactly what T40/T42/T44 already do. Leave it alone.

## STOP conditions

- The event stream turns out **not** to be a complete substitute — a
  window-state transition that mutates `m_vendor_npc_id`/
  `m_banker_npc_id`/`m_trainer_npc_id` without publishing → **STOP and
  report it.** T42 hit exactly this and had to close three mutation
  paths first; do not paper over it by keeping the poll alongside.
- Deleting the poll changes behaviour beyond the two fidelity deltas
  named above → STOP and report what.
- The reader cannot see the migrated state from `CommandProcessor` →
  STOP; that means the instance analysis above is wrong and I want to
  know before you work around it.
- Any change to `eq_merchant.cpp` publish sites or the merchant protocol
  → STOP; different unit.
- Timing: the poll ran every game tick, so it re-mirrored state
  continuously; events fire once per transition. If any consumer depends
  on the *repetition* rather than the value, say so rather than
  restoring the poll.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP unwaived (T35),
   golden replay does not skip (T36); the pinned tests from
   T43/T44/T45/T46/T47 stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `scripts/ci/visual_gate.sh` PASS, goldens untouched — this unit
   touches `Application`'s tick path, so prove you did not perturb it.
5. Unit tests: extend `test_game_state_sync_bridge.cpp` with the six
   window events (open sets id/name/rate, close clears), following its
   existing pet/spell/inventory cases; plus reader-command tests in
   `test_command_processor.cpp` (no vendor open, vendor open, argument
   handling) on the T34/T48 model.
6. **Live acceptance, and this is the deliverable — a real before/after,
   not just a passing test.** Real eqemu session, driven **entirely by
   piped commands, no scratch hooks** (T48's standard):
   - `/vendor` (T48) to open a merchant → reader reports the vendor,
     **with the true sell rate, not 1.0** → close → reader reports none.
   - **The "before" leg matters**: run the same script on `fable` and
     show the reader reporting `1.0` (or nothing at all, if the command
     is new — in which case demonstrate the delta by dumping the state
     via the test instead, and say plainly that is what you did).
   - Banker and trainer legs too if reachable from the command channel;
     **if they are not, that is a finding — record it as the next
     instance of the automation-reachability pattern rather than
     inventing a hook.**
   - **Both trees** (graphics and headless): headless is the whole point
     of the arc, and this is the first sweep unit landing after T45/T46
     completed it.
   - eqemu LOCK FIRST; `up.sh` only; restore Testchar to zone_id=36,
     x=-77.9232, y=21.9086, z=3, h=190; `down.sh` after. Evidence under
     `benchmarks/audits/T49/`.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## What to report in the Review

- The completeness argument for the event stream (the T42-shaped check),
  with the mutation sites you inspected.
- The two fidelity deltas, measured — actual sell rate observed live,
  actual name strings before and after.
- Where the reader landed, why, and the collision check.
- Whether banker/trainer are reachable from the command channel at all.
- **This closes the state-layer sweep (T39→T49).** Say whether it does,
  honestly — if something in NPC-interaction state remains un-migrated,
  the sweep is narrowed, not closed, and the board should say so.

## Review

### The two facts the plan established — verified, both correct

1. **"Missing events" blocker does not exist.** Confirmed by grep: all six
   window events (`VendorWindowOpened`/`Closed`, `BankWindowOpened`/`Closed`,
   `TrainerWindowOpened`/`Closed`) are declared in `event_bus.h` and
   published from `eq_merchant.cpp` at exactly six sites, matching the
   plan's table (`ZoneProcessShopRequest`:907 for open,
   `ZoneProcessShopEndConfirm`:1091 and `CloseVendorWindow`-equivalent:1257
   for close, `OpenBankWindow`:1290, `CloseBankWindow`:1319,
   `ZoneProcessGMTraining`:1429, `CloseTrainerWindow`:1489).
2. **Two `state::GameState` instances, reader must go in `CommandProcessor`.**
   Confirmed by reading both constructor call sites: `GameStateSyncBridge`
   and `CommandProcessor` are both built with `Application::m_gameState`;
   `EverQuest::m_command_registry` (T48's `/vendor`/`/vendorlist`) reads a
   different `EverQuest::m_game_state`. Reader landed in
   `action::CommandProcessor` as directed.

### Completeness argument (the T42-shaped check)

Grepped every assignment to `m_vendor_npc_id`/`m_banker_npc_id`/
`m_trainer_npc_id` in `eq_merchant.cpp` (the only file that mutates them —
confirmed by a repo-wide grep for the three identifiers): six mutation
sites, one per window transition, and **each one has a bridge-event publish
immediately adjacent to it** (open publishes before the state is fully live,
close publishes before clearing) — the same six sites the plan's table
names. There is no mutation path that changes these ids without publishing.
The event stream is a complete substitute for the poll; no STOP needed, no
gap-closing work required (unlike T42's inventory unit, which had to fix
three unpublished mutation paths first).

### The migration

`GameStateSyncBridge::applyEvent()` (`src/client/bridge/game_state_sync_bridge.cpp`)
gained six new cases, each calling the existing `PlayerState` setter:
`VendorWindowOpened`→`setVendor(npcId, sellRate, npcName)`,
`VendorWindowClosed`→`clearVendor()`, `BankWindowOpened`→`setBanker(npcId)`
(the event's `npcName`/`sellRate` fields are unused — `setBanker()` takes
only an id, matching the deleted poll which never read a bank name either),
`BankWindowClosed`→`clearBanker()`, `TrainerWindowOpened`→
`setTrainer(npcId, npcName)` (reading only two of `TrainerWindowOpenedData`'s
many fields — the rest, the trainable-skills list and currency, is for a
future trainer-UI consumer, not this unit's territory),
`TrainerWindowClosed`→`clearTrainer()`.

`Application::syncNPCInteractionState()` and its three `m_lastVendorNpcId`/
`m_lastBankerNpcId`/`m_lastTrainerNpcId` members are deleted outright
(`application.cpp`, `application.h`), with a comment left in both places
pointing at the replacement. The `syncGameStateFromClient()` Phase 7 comment
is updated: it now says pet/inventory/spell/NPC-interaction are all
event-fed, and names combat as the only sub-sync still polling (out of this
sweep's territory — `CombatManager` remains authoritative and there's no
migration target for it here).

`EverQuest::GetBankerNpcId()`/`GetTrainerNpcId()` (`eq.h`) lose their only
caller (`Application`'s deleted poll was the sole call site — confirmed by
grep across `src/`, `include/`, and `tests/`). Left in place: `eq.h` is not
in this unit's stated territory (`Application::syncNPCInteractionState()`,
`GameStateSyncBridge::applyEvent()`, the reader command, and tests), and
they're one-line accessors with no cost to keeping them. `GetVendorNpcId()`
is *not* dead — T48's `/buy`/`/sell` still call it.

**The re-publish is by design, not touched.** `PlayerState::setVendor()`/
`setBanker()`/`clearVendor()`/etc already publish onto the (separate)
`EventBus` `ConsoleRenderer` subscribes to (`player_state.cpp:172-242`).
Confirmed by reading `game_state_sync_bridge.cpp` end to end: the decorator
never calls `EventBus::publish()` or anything equivalent — the only bus
interaction in the file is the unconditional `m_wrapped.applyEvent(event)`
forwarding the original bridge event, a completely different pipe. Left
alone, per the plan.

### The reader: where it landed, why, and the collision check

`action::CommandProcessor::cmdNpcState()`, registered as **`/npcstate`**
(`include/client/action/command_processor.h`,
`src/client/action/command_processor.cpp`) — registry 2, constructed with
`Application::m_gameState`, the instance `GameStateSyncBridge` populates.
Grepped all three command surfaces (`action::CommandProcessor`'s ~90
registered names, `EverQuest::m_command_registry`'s ~120 names including
T48's `vendor`/`vendorlist`/`buy`/`sell`/`bank`/`train`, and the renderer's
own command surface) for `npcstate` before adding: no collision. Modelled
directly on `cmdHail()` (T39) — the established precedent for a
`CommandProcessor` command reading pure `state::GameState&` with no
dispatcher call — reporting all three (vendor/banker/trainer) in one
invocation since they share a single "am I at a vendor, which one, at what
rate" mental model per the plan's own framing. No args; extra text is
ignored, same convention as `/loc`.

Splitting vendor commands across two registries (T48's action commands in
`EverQuest::m_command_registry`, T49's read-only `/npcstate` in
`action::CommandProcessor`) is a fresh instance of the open "~37-command
registry overlap" board item, noted here per the plan, not fixed.

### The two fidelity deltas — measured live, not just asserted

Live eqemu session (`benchmarks/audits/T49/transcript_primary.txt` for the
graphics tree, `transcript_nogfx.txt` for `EQT_GRAPHICS=OFF`), vendor NPC
771 "SummonResupplyAgent" (same NPC T48 used), spawned via
`#npctypespawn 771` at the GM's own position:

```
[DEBUG] [INVENTORY] ShopRequest response: npc_id=178 action=1 sell_rate=1.0656
[INFO ] [INVENTORY] Opened vendor window for SummonResupplyAgent (id=178)
...
[System] Vendor: SummonResupplyAgent (id=178, sellRate=1.0656)
```

- **Sell rate**: the server's own `ShopRequest` packet (logged independently
  by `eq_merchant.cpp` at `LOG_DEBUG` *before* any state-layer involvement)
  reports `sell_rate=1.0656`. `/npcstate`, reading the migrated state
  layer, reports the identical `1.0656`. The deleted poll hardcoded
  `setVendor(npcId, 1.0f, name)` regardless of what the packet said — it
  would have reported `1.0` here, silently wrong by 6.56%. Reproduced
  identically in both trees (`sell_rate=1.0656`/`sellRate=1.0656` in both
  transcripts).
- **Name string**: the entity's raw spawn name is `SummonResupplyAgent003`
  (confirmed in the same transcript: `New spawn: SummonResupplyAgent003
  (ID: 175)`) — the deleted poll used `entity.name` directly, so it would
  have reported `Vendor: SummonResupplyAgent003 (...)`, trailing digits and
  all. The migrated path uses `eq_merchant.cpp`'s
  `EQT::toDisplayName(it->second.name)` at the publish site, which strips
  the trailing digits; `/npcstate` reports the clean `SummonResupplyAgent`.
  Both are visible side by side in the same transcript. The fallback string
  also changed as the plan predicted (`"Merchant"` now, `"Vendor"` before)
  though this run always found the entity, so the fallback path itself
  wasn't exercised live — confirmed instead by reading the publish site,
  which is unconditional on `m_entities.find()` succeeding.

Both deltas reproduced identically in the `EQT_GRAPHICS=OFF` tree run
(`transcript_nogfx.txt`: `sell_rate=1.0656` / `sellRate=1.0656`,
`SummonResupplyAgent003` raw vs `SummonResupplyAgent` displayed).

**On the plan's "before" leg**: `/npcstate` is a brand-new command — it does
not exist on `fable` at all, so running this same script there is not
meaningful (there is no command to pipe). Per the plan's explicit fallback
("demonstrate the delta via the test instead, and say plainly that is what
you did"): that's what was done —
`CommandProcessorTest.NpcState_VendorOpen_ReportsVendorWithRealSellRate`
(`tests/test_command_processor.cpp`) asserts a non-`1.0` sell rate
(`1.25f`) round-trips through `/npcstate` output, which would fail against
naive code that still hardcoded `1.0f`. The live leg above is strictly
stronger evidence than that test alone: it shows the *actual* delta value
the poll would have gotten wrong, sourced from a real server packet, not a
synthetic test fixture.

### Banker and trainer reachability — both partially reachable, both blocked by pre-existing bugs, neither fixed

**Banker**: `/bank` (pre-existing, registered in
`EverQuest::m_command_registry` before this unit, `eq_commands_spells.cpp`)
toggles `OpenBankWindow()`/`CloseBankWindow()` and *is* reachable from the
command channel. But it opens and self-closes within the same processing
step, every time, in both trees:

```
[DEBUG] [INVENTORY] Opening bank window (banker NPC: 1)
[DEBUG] [INVENTORY] Player moved too far from banker (31689.8 > 15.0), closing bank
[INFO ] [MAIN] Bank window closed
```

Root cause, traced (not fixed — `eq_movement.cpp`/`eq_commands_spells.cpp`
are outside this unit's territory): `OpenBankWindow()` has no npc-id
parameter from `/bank`'s call site, so it falls back to `bankerNpcId != 0 ?
bankerNpcId : 1` — a **hardcoded dummy id of 1**. In this zone (`arena`
resolves to `befallen`, per T32-T34's "misleading default zone name"
finding), spawn id 1 is a real, unrelated entity (`zone_controller000`, at
extreme coordinates `(80000, 240000, -79999)` per the zone's spawn log). The
very next movement-tick proximity check (`eq_movement.cpp`'s
"NPC_INTERACTION_DISTANCE" gate) computes the distance to *that* entity,
gets `31689.8`, and closes the window it just opened. This is a **pre-existing
bug, unrelated to T49**, reproduced identically in both trees — `/bank`'s
"reachable" answer is technically yes but not usefully so without also
fixing this dummy-id collision (a different unit's territory: it touches
`eq_commands_spells.cpp`/`eq_movement.cpp`, neither in this plan's
territory list). `GameStateSyncBridgeTest.BankWindowOpened*`/
`CommandProcessorTest.NpcState_BankerOpen_ReportsBanker` cover the
migration and reader correctness directly against synthetic events instead,
since the live open state can't be held open long enough to observe.

**Trainer**: `/train` (pre-existing, same file) is also reachable — it
validates the target (NPC, class 20-35) and calls
`RequestTrainerWindow(target_id)`, which queues the `HC_OP_GMTraining`
packet. Confirmed sent in both trees (`Requesting trainer window for NPC
176`/`179`), using guildmaster-trainer NPC id 1007 ("Miltiades_Tal`Azroth",
`class=22`, confirmed against the DB before spawning). **But the server
never responded** with trainee data in either run — no
`ZoneProcessGMTraining` handler log, no `Trainer window opened for`, ever.
Not investigated further (server-side behavior, out of this unit's
territory and out of the eqemu-ownership scope this unit was granted, which
covers DB reads/position writes, not server debugging); recorded as a live
finding, the next instance of the automation-reachability pattern, per the
plan's explicit instruction not to invent a hook. `GameStateSyncBridgeTest.
TrainerWindowOpened*`/`CommandProcessorTest.NpcState_TrainerOpen_ReportsTrainer`
cover migration and reader correctness against synthetic events.

**Bottom line on reachability**: vendor is fully reachable and was fully
exercised live (open, observe, close, observe cleared, in both trees).
Banker and trainer are reachable-but-blocked by two distinct, pre-existing
environment bugs that this unit's territory does not cover fixing; both are
reported rather than worked around, and both have full unit-test coverage
of the actual territory this unit owns (the bridge decorator and the
reader).

### Verification bar

1. Both configures build clean: `build` (graphics) and `build-nogfx`
   (`EQT_GRAPHICS=OFF`) — clean rebuilds, no new warnings.
2. `ctest --output-on-failure`: `build` 1889/1890 passed, `build-nogfx`
   1457/1458 passed — sole failure in each tree is the documented
   `AudioManagerTest.PreloadCommonSounds` (ALSA). No RDP failures in either
   run. `SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`
   passed in both, and separately confirmed (via direct
   `WILLEQ_SESSION_LOG=...` invocation of `test_session_replay` in both
   trees) to have taken the real recorded-session branch, not the
   baseline-creation fallback — both logged real zone-map lookups from the
   capture (`befallen.map`, `qeynos2.map`).
3. Golden replay: see above, confirmed to have RUN in both trees.
4. `scripts/ci/visual_gate.sh`: `PASS` (qeynos2/gfaydark/befallen all
   verified). `git status` on `data/` shows no changes before or after —
   goldens untouched, despite this unit touching `Application`'s tick path.
5. Unit tests: `test_game_state_sync_bridge` grew from 20 to 27 tests (7
   new: `VendorWindowOpenedSetsVendorState`,
   `VendorWindowClosedClearsVendorState`, `BankWindowOpenedSetsBankerState`,
   `BankWindowClosedClearsBankerState`, `TrainerWindowOpenedSetsTrainerState`,
   `TrainerWindowClosedClearsTrainerState`,
   `NpcInteractionEventsStillDelegateToWrappedBridge`), following the
   pet/spell/inventory case shape (open sets id/name/rate, close clears,
   delegation still happens). `test_command_processor` gained 6
   `cmdNpcState` cases (no interaction, vendor open with the non-1.0 rate
   proof, banker open, trainer open, cleared-afterward, ignores-arguments)
   on the T39 `cmdHail` model. All pass in both trees, confirmed by direct
   binary run (not just ctest's aggregate count): `test_game_state_sync_bridge`
   27/27, `test_command_processor` 51/51 (the full suite, not just the 6 new
   cases).
6. Live acceptance: see above — vendor leg fully closed-loop in both trees
   with the two fidelity deltas measured; banker/trainer legs partially
   blocked by two distinct pre-existing bugs, reported not worked around;
   eqemu locked first (`T49-npc-interaction-state 2026-08-05`), `up.sh
   arena` only, Testchar restored to `zone_id=36, x=-77.9232, y=21.9086,
   z=3, heading=190` (confirmed via `SELECT` before `down.sh`; drifted to
   `-77.875/21.875` during the runs — same coordinate-axis noise T33/T34/T48
   documented, not real movement), `down.sh` clean, lock removed and
   confirmed absent. Evidence under `benchmarks/audits/T49/`
   (`run_verification_primary.sh`, `willeq_t49.json`,
   `transcript_primary.txt`/`.driver`, `transcript_nogfx.txt`/`.driver`).
7. `bash scripts/refactor/check_file_sizes.sh --files <8 touched paths>`:
   7 ok, 1 WARN (`application.cpp`, 2275 lines — pre-existing at 2324 on
   `fable`, *shrank* by 49 lines from this unit's deletion, not a new
   warning), 0 FAIL.

### Does this close the state-layer sweep (T39→T49)?

**Yes, for the migration.** All four sweep items (pet T40, inventory T42,
spell T44, NPC interaction T49) are now event-fed via
`GameStateSyncBridge`; `Application::syncGameStateFromClient()`'s only
remaining sub-sync is `syncCombatState()`, which was never part of this
sweep (T39 built it as a direct mirror of `CombatManager`, the
authoritative source, not a migration target — there's no "poll → event"
shape to convert there, unlike the four sweep items). No poll remains for
vendor/banker/trainer state.

**With one honest caveat on the "reader" half**: the plan required landing
*a* reader to make the migration observable, which is done (`/npcstate`,
fully exercised live for vendor, unit-tested for all three). But two of the
three NPC-interaction sub-states (banker, trainer) could not be **held open
live** long enough to observe via `/npcstate` in this environment, due to
pre-existing bugs this unit's territory doesn't cover fixing. That is a gap
in *live demonstration depth* for two of six event types, not a gap in the
migration itself — the completeness argument above (six mutation sites, six
matching publishes, all six wired into `GameStateSyncBridge`, all six
covered by synthetic-event unit tests) is unaffected by whether eqemu will
hold a banker/trainer session open long enough to watch. The sweep is
closed; the two live-reachability bugs are new findings for the board, not
open sweep items.

### Deviations

- `EverQuest::GetBankerNpcId()`/`GetTrainerNpcId()` lose their only caller
  but were left in place (not in the plan's stated territory,
  cost-free to keep) — flagged rather than silently left or silently
  removed.
- Banker's self-closing bug and trainer's server-non-response were both
  discovered live, not by code reading, and both root-caused enough to
  report precisely (dummy npc id=1 collision; GMTraining request sent but
  never answered) without being fixed, since both require touching files
  outside this unit's territory (`eq_commands_spells.cpp`,
  `eq_movement.cpp`, or eqemu server-side behavior).
- `benchmarks/audits/T49/willeq_t49.json` is a copy of T48's config
  (same account/character/server), not itself part of the plan's file
  list but needed to drive the live leg.
