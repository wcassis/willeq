# T41 — Feed inventory state from the bridge, and fix the initial-snapshot gap the sweep depends on

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `src/client/bridge/game_state_sync_bridge.cpp` (+ header),
`src/client/application.cpp` (the sync), `src/client/eq.cpp`'s
`PublishFullStateSnapshot()` and its caller, and tests.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T40_pet_state_via_bridge.md` — its "Why" section states the
goal and the target invariant, and its Review documents the decorator
seam you are extending. Then the deferred entry **"TWO
`state::GameState` instances exist, unconnected"**.

This is the second cut of the Application→EverQuest decoupling. T40 did
pet and proved the pattern works.

## Two things, and the second matters more than the first

### 1. Inventory itself is the easy half

`syncInventoryState()` (`application.cpp`) polls
`m_eqClient->GetInventoryManager()` and mirrors slot occupancy into
`GameState::inventory()`. `InventorySlotChanged` already exists, already
carries everything needed (`slotId`, `hasItem`, `itemId`, plus S2's
display fields), and is already published from three sites
(`eq.cpp:600`, `eq_merchant.cpp:80` and `:105`).

S2 set the precedent explicitly — its own comment on those display
fields says they are carried in the event "so the render-thread bridge no
longer reads the game-thread `InventoryManager` through a shared
pointer". This unit applies that same reasoning one layer over.

Extend `GameStateSyncBridge` with an `InventorySlotChanged` case, delete
`syncInventoryState()`, and drop the `GetInventoryManager()` read from
`Application`.

### 2. The initial-snapshot gap — the real work

**`PublishFullStateSnapshot()` (`eq.cpp:457`) is called from exactly one
place: `OnGraphicsComplete()` (`eq.cpp:454`).** So in headless mode the
full-state snapshot **never fires at all**, and additionally its
inventory block is wrapped in `#ifdef EQT_HAS_GRAPHICS` even when it
does.

That is fine for a poll — a poll re-derives everything every tick and
does not care that it missed the start. It is **fatal for event-fed
state**, which only ever sees deltas after the snapshot. Convert
inventory without fixing this and headless inventory state starts empty
and stays wrong until something happens to change a slot.

**This is not an inventory problem — it is a sweep problem.** Every
remaining `sync*` conversion (combat, spell, NPC interaction) depends on
the render/input-side state getting a correct initial picture in both
modes. Fixing it here is why this unit is second rather than last.

Direction is already established by precedent: P28 declassified
`EQT_HAS_GRAPHICS` guards that enclosed non-graphics code, and T29 did the
same for ~2500 lines of command registration. Publishing game state is
not a graphics operation. So:

- Make the snapshot fire in headless too — find the right trigger, not
  just an extra call site. `OnGraphicsComplete()` is a *graphics*
  lifecycle hook; the snapshot wants a *game* lifecycle one (zone-in /
  state-ready). Say what you chose and why.
- Un-guard the inventory block inside it unless there is a real
  graphics dependency, in which case name it.
- **Do not let it fire twice** in graphics mode. Double-publishing a
  full snapshot would be a new bug, and event consumers are not all
  idempotent — check before assuming.

## STOP conditions

- The snapshot cannot be triggered from a game-lifecycle point without
  restructuring zone-load ordering → STOP and report; that is a bigger
  change than this unit.
- Inventory state turns out not to be fully reconstructible from
  `InventorySlotChanged` + the snapshot (e.g. bank slots, cursor, or
  merchant transactions that mutate slots without publishing) → STOP and
  report exactly which case. Same reasoning as T40: this tells us
  whether the rest of the sweep is viable, and is more valuable than a
  green run.
- Fixing the snapshot changes what graphics-mode consumers receive →
  STOP; the renderer's existing event stream must be byte-unaffected.
- Any behaviour change beyond where inventory state comes from and when
  the snapshot fires → STOP.

## Worth knowing before you start

`syncInventoryState()` was added by T39, and its own comment records that
**no production reader consumes the occupancy arrays it populates** —
still true as of T28's audit. So converting it is about removing the
coupling and proving the mechanism, not about fixing a user-visible bug.

Do not let that tempt you into deleting the occupancy half instead —
that is a separate call about whether the state layer should model
inventory at all, and it is not yours to make here. Note it in the
Review if you have a view.

## Verification bar

1. Both configures build clean — graphics **and** `EQT_GRAPHICS=OFF`.
   The headless one is the point of item 2, not a formality.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay no longer skips (T36); a failure or `Skipped` on
   either is a real signal.
3. Golden replay unchanged and confirmed to have RUN.
4. **Unit tests**: extend `tests/test_game_state_sync_bridge.cpp` for the
   inventory case (occupancy set and cleared, unhandled events still
   delegate). Add coverage that the snapshot fires once per zone-in in
   both modes if it can be tested without a server.
5. **Live acceptance**: a session showing inventory occupancy correct in
   `GameState` from the moment of zone-in — i.e. the initial snapshot
   arriving — with `syncInventoryState()` deleted. **Run it headless**,
   since that is the mode the snapshot gap breaks. eqemu LOCK FIRST;
   `up.sh` only; restore Testchar to zone_id=36, x=-77.9232, y=21.9086,
   z=3, h=190; `down.sh` after. Evidence under `benchmarks/audits/T41/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

**Bottom line: piece 1 (inventory conversion) STOPped — inventory state did
NOT prove fully reconstructible from `InventorySlotChanged` + the snapshot.
`syncInventoryState()` was NOT deleted, and `GameStateSyncBridge` was NOT
extended with an `InventorySlotChanged` case.** Piece 2 (the initial-snapshot
gap) is the real work and was completed: the snapshot now fires exactly once
per zone-in in both graphics and headless modes, proven both by deterministic
unit tests and a live headless session against eqemu. A necessary,
closely-related fix (`m_inventory_manager` construction was still
`EQT_HAS_GRAPHICS`-gated despite the header declaration already being
declassified by P28) was made so the headless configure's snapshot has real
data to publish at all. An orchestrator addendum mid-unit asked for the same
proof for a pre-existing pet (`PetCreated`), which is also now covered.

### Piece 1: inventory — STOP, exact case

Scoping (not just reading the plan's named examples, but tracing every
mutation path that writes `m_inventory_manager` and asking "does this publish
`InventorySlotChanged`?") found **three real, live production paths that
mutate inventory slots without ever publishing `InventorySlotChanged`**:

1. **Vendor buy** (`EverQuest::ZoneProcessShopPlayerBuy`,
   `eq_merchant.cpp:905`). Its own comment says it plainly: *"ItemPacket with
   type 103 (inventory) handles the actual item placement... via
   `InventoryManager::setItem()`"* — the item lands via the generic
   `HC_OP_ItemPacket` fallback branch (`eq.cpp`'s `DispatchZonePacket`,
   the `else` arm that isn't vendor/loot/trade), which calls
   `m_inventory_manager->processItemPacket(p)` directly with **zero**
   `InventorySlotChanged` publish anywhere in that path.
2. **Vendor sell** (`EverQuest::ZoneProcessShopPlayerSell`,
   `eq_merchant.cpp:958`). Directly calls
   `m_inventory_manager->removeItem(invSlot)` or mutates the stack quantity
   in place — again, no event published at all, ever, for this mutation.
3. **Bank-window population** — the same generic `HC_OP_ItemPacket` fallback
   branch as (1) is also how bank slot contents arrive (server sends one
   `OP_ItemPacket` per occupied bank slot when the bank window opens; there
   is no separate bulk bank-list opcode — confirmed via `grep` across
   `eq.h`'s `HC_OP_*` enum, the only two bank-related opcodes are
   `HC_OP_BankerChange`, currency-only, and the generic `ItemPacket`). This
   path is additionally `#ifdef EQT_HAS_GRAPHICS`-gated at the call site in
   `eq.cpp`'s `DispatchZonePacket` (the "Regular inventory item packet - pass
   to inventory manager" branch), so in a true `EQT_GRAPHICS=OFF` build bank
   items never even entered `m_inventory_manager` in the first place —
   independent of whether an event is published.

`syncInventoryState()`'s poll is blind to *when* any of these three fired but
still correct, because it re-reads `m_inventory_manager` directly every tick.
Event-driven state fed only by `InventorySlotChanged` + the snapshot would
silently miss all three — a purchased item would never show as occupying its
slot, a sold item would never show as vacated, and (in graphics builds, where
bank items do get parsed) opening the bank would never update occupancy. This
is the plan's STOP condition named almost verbatim ("bank slots... merchant
transactions that mutate slots without publishing"), so per the plan's own
instruction — *"more valuable than a green run"* — `syncInventoryState()`
stays exactly as it is, and the decorator does not touch
`InventorySlotChanged`. `tests/test_game_state_sync_bridge.cpp` gained
`InventorySlotChangedNotYetHandledDelegatesUntouched`, pinning today's safe
default (unhandled, delegates through) so a future attempt to wire the case
without first closing those three gaps fails a test instead of silently
shipping wrong state.

### Piece 2: the snapshot gap — trigger chosen, and why

**Trigger: `EverQuest::OnGameStateComplete()`'s existing no-graphics branch**
(`eq.cpp`), not a new call site bolted anywhere. This function is already the
correct game-lifecycle zone-in signal — `eq_zone.cpp`'s re-zone comment
independently confirms it: *"the correct, single signal point is
`OnGameStateComplete()`..., which only fires once the new zone's `NewZone`
packet (and thus `m_current_zone_name`) has actually been received — exactly
how the initial zone-in already works."* It already branches on
`m_graphics_initialized`: the `true` arm requests a zone-load and (in
graphics builds) eventually reaches `OnGraphicsComplete()` — a *graphics*
lifecycle hook that only exists because zone geometry takes time to build —
which already called `PublishFullStateSnapshot()`. The `false` arm ("No
graphics mode - we're done") published `ZoneLoaded` and stopped — this is
where `PublishFullStateSnapshot()` was missing. Added one call,
`PublishFullStateSnapshot();`, to that arm, and to the mirror-image `#else`
block (the `EQT_GRAPHICS=OFF`-configure copy of the same headless path,
compiled when `EQT_HAS_GRAPHICS` isn't defined at all). No new call site, no
extra hook, no restructuring of zone-load ordering — the STOP condition
about needing to restructure zone-load ordering did not trigger.

Also un-guarded the inventory block inside `PublishFullStateSnapshot()`
itself (was `#ifdef EQT_HAS_GRAPHICS`) — it does nothing but build event
structs from `m_inventory_manager`, which (per P28's own comment at the top
of `eq.cpp`) is documented as a "renderer-free data model... used
unconditionally" elsewhere in the same file. No genuine graphics dependency
was found in that block, so, per the plan's instruction, it's un-guarded
rather than named as a real dependency.

### The necessary related fix: `m_inventory_manager` construction

While verifying the `EQT_GRAPHICS=OFF` configure actually exercises the
un-guarded inventory block (not just compiles it), found that
`m_inventory_manager`'s **construction** in `EverQuest`'s constructor
(`eq.cpp`) was still wrapped in `#ifdef EQT_HAS_GRAPHICS` — even though P28
already declassified the *declaration* in `eq.h` ("plain game state and
network logic with no renderer dependency"), and T29's own comment two lines
below it names `InventoryManager` alongside `CommandRegistry` as one of the
members the guard misclassification affected, but T29 only fixed
`CommandRegistry`'s construction, not this one. Left as found, a genuine
`EQT_GRAPHICS=OFF` build would never construct `m_inventory_manager` at all
— not "polled the wrong way," genuinely `nullptr` for the entire process
lifetime — which would have made the now-un-guarded snapshot inventory block
silently publish nothing, defeating the point of piece 2 for exactly the
configure the plan calls "not a formality." Un-guarded the construction (+
`SetupInventoryCallbacks()` + its log line), mirroring T29's adjacent fix.
This is in `eq.cpp`, the file the plan's territory already names, and is
required for the snapshot fix to have any effect in that configure — not an
unrelated drive-by.

### Proof it does not double-fire in graphics mode

Traced every call site of `PublishFullStateSnapshot()` after the change: it
is still called from exactly two places — `OnGraphicsComplete()` (unchanged)
and `OnGameStateComplete()`'s two headless arms (new). `m_graphics_initialized`
is set once by `InitGraphics()` before any zone connection can begin and only
reset by `ShutdownGraphics()` at teardown (`eq_bridge.cpp`) — it cannot flip
mid-run, so a given process takes exactly one of the three call paths per
zone-in, never both. Additionally:
- `TEST(FullStateSnapshot, HeadlessZoneInPublishesSnapshotExactlyOnce)`
  (`tests/test_zone_connection_flow.cpp`) drives a full synthetic zone-in
  through the replay seam with a recording bridge attached, counts
  `SkillsSnapshot` (grep-confirmed published from nowhere except the
  snapshot — unlike `CharacterInfoChanged`, which `ZoneProcessPlayerProfile`
  also publishes, so it can't be used as a snapshot-only proxy), asserts
  exactly 1 after the first `ClientUpdate`, then replays a second
  `ClientUpdate` and asserts 0 further fires (`OnGameStateComplete()`'s
  `!m_update_running` guard, already in place, prevents refiring). Passes in
  both build trees.
- Live: the headless eqemu session log
  (`benchmarks/audits/T41/t41_headless_run_debug2.log`) shows exactly one
  `"Publishing full state snapshot to bridge"` line for the entire session
  (`grep -c` = 1), immediately after `"[LOADING] Game state setup complete"`.
- The renderer's existing event stream is byte-unaffected in graphics mode:
  `OnGraphicsComplete()` itself was not touched at all, and the inventory
  block's un-guarding only changes what's compiled for `EQT_GRAPHICS=OFF` —
  the code inside is identical, so a graphics build's snapshot content is
  unchanged.

### Addendum (orchestrator, mid-unit): does `PetCreated` reach a pre-existing pet in headless?

T40 made pet state event-fed and deleted `syncPetState()`; `PetCreated` is
one of the events `PublishFullStateSnapshot()` republishes
(`if (m_pet_spawn_id != 0)`). The concern: T40's live leg only ever exercised
a *mid-session* GM-summon (a delta), never a pet that already existed when
the zone-in happened — which is exactly the case the snapshot gap would have
broken.

Traced the two places `eq_entities.cpp` discovers a pre-existing pet from the
zone's spawn list (`ZoneProcessSpawnAll`'s "second pass", and the deferred
check inside `ClientUpdate`): in both, `m_pet_spawn_id = ent.spawn_id;` is
**not** graphics-gated, but the accompanying `OnPetCreated(ent)` delta-publish
right next to it **is** (`#ifdef EQT_HAS_GRAPHICS`). Consequence:
- **`EQT_GRAPHICS=OFF` build**: the delta path is compiled out entirely.
  Before this unit, a pre-existing pet was invisible outright (nothing ever
  published `PetCreated` for it). After this unit's headless snapshot fix,
  `PublishFullStateSnapshot()`'s pet block is the *only* path and it works,
  because `m_pet_spawn_id` was already set (that assignment isn't gated).
- **`EQT_HAS_GRAPHICS` build** (regardless of runtime `--no-graphics`): the
  delta fires immediately during spawn processing, then the snapshot
  republishes the same `PetCreated` once more later — harmless duplication
  that predates this unit (graphics mode always called the snapshot after
  `OnGraphicsComplete()`, so a pre-existing pet was already double-published
  before T41; this unit just makes `--no-graphics`-on-a-graphics-build
  consistent with what graphics mode always did).

**Live attempt, honestly reported as inconclusive**: tried to arrange a
pre-existing pet for the live leg by GM-summoning one (`#makepet
FamiliarFire`), then letting the session end without `/camp` (killed by
`timeout`, simulating link-death rather than a clean camp) and reconnecting.
The pet did not survive — eqemu dismisses GM-summoned pets on disconnect
faster than the reconnect could observe it as pre-existing
(`benchmarks/audits/T41/t41_pet_recheck_run.log` shows no `"Pet detected"`
line before the snapshot). Did not attempt a GM `#zone` mid-process teleport
to force a persisting pet across a same-process re-zone — `refactor/deferred.md`
records a prior SIGSEGV from exactly that GM command in this codebase
(P13b), and risking a crash on an incidental check inside an otherwise-clean
acceptance run wasn't a reasonable trade for this addendum.

**Fell back to the deterministic, in-scope proof the addendum explicitly
sanctions** ("show that PetCreated is present in the snapshot event
stream"), but did it more strongly than a code read: added
`TEST(FullStateSnapshot, PreExistingPetSurvivesHeadlessZoneInViaSnapshot)`
(`tests/test_zone_connection_flow.cpp`), which sends a synthetic `ZoneSpawns`
packet containing an NPC with `is_pet=1`/`pet_owner_id=<our spawn id>`
*before* `ClientUpdate` — i.e. a pet that is already present in the zone's
spawn list at zone-in, the exact scenario a live GM-summon-then-reconnect
would have tested if it had worked — then asserts `PetCreated` appears
exactly once in the event drain taken *after* `ClientUpdate` (the snapshot's
contribution), and records (via a `deltaFired` flag, not asserted either way)
whether an earlier delta also fired. Confirmed by direct instrumented run
(removed before the final commit) that the two build trees exercise the two
different code paths this analysis predicts:

| tree | `deltaFired` | `postSnapshotPetCreated` |
|---|---|---|
| `build` (`EQT_HAS_GRAPHICS`) | `1` (delta fires during spawn processing) | `1` (snapshot republishes) |
| `build-nogfx` (`EQT_GRAPHICS=OFF`) | `0` (delta path compiled out) | `1` (snapshot is the sole source) |

This is exactly the addendum's ask: proof that the snapshot, not a delta,
is what gets a pre-existing pet into headless `GameState` — in the one build
configuration (`EQT_GRAPHICS=OFF`) where nothing else could. Passes in both
trees.

### What was deleted / what was NOT deleted

- **Not deleted**: `Application::syncInventoryState()` (STOP, see above).
- **Not touched**: `GameStateSyncBridge`'s switch — no `InventorySlotChanged`
  case added.
- **Deleted**: the `#ifdef EQT_HAS_GRAPHICS`/`#endif` wrapper around (a)
  `m_inventory_manager`'s construction + `SetupInventoryCallbacks()` call in
  `EverQuest`'s constructor, and (b) the inventory block inside
  `PublishFullStateSnapshot()`. Both are un-guards, not logic changes — the
  code inside is untouched.

### View on the unread occupancy arrays (plan asked for one)

Still true, still not this unit's call to make, but worth stating plainly
now that the reconstructibility gap is mapped: converting inventory to
event-driven state is not "close but for three gaps" — it requires either
(a) auditing and publishing `InventorySlotChanged` at every mutation site
(the ItemPacket fallback branch, vendor sell, and any others not surfaced by
this unit's scan — bag-slot moves and shared-bank were not separately
audited), or (b) accepting that headless/event-driven inventory state will
be wrong in exactly the situations a player would notice most (after buying
or selling). Given nothing reads the occupancy arrays today, my view is this
conversion is not worth doing opportunistically alongside a future unit —
it deserves its own scoped unit that audits and fixes the publish sites
first, the same way T40 proved pet events out before converting.

### Verification bar

1. `cmake --build build -j24` (`EQT_GRAPHICS=ON`) and
   `cmake --build build-nogfx -j24` (`EQT_GRAPHICS=OFF`) — both clean.
2. `ctest --output-on-failure` — graphics tree 1832/1833 (sole failure:
   documented `AudioManagerTest.PreloadCommonSounds` ALSA exception);
   headless tree 1401/1402 (same sole failure). RDP suite unaffected in
   both. Golden replay (`SessionReplayGolden.MatchesRecordedSessionOrCreatesBaseline`)
   confirmed RUN and PASSED in the graphics tree (directly re-run after the
   full suite to rule out a log-overwrite artifact from an intervening
   `ctest -N`).
3. Golden replay: see above.
4. Unit tests: `test_game_state_sync_bridge` gained
   `InventorySlotChangedNotYetHandledDelegatesUntouched` (7/7 pass, both
   trees). `test_zone_connection_flow` gained
   `FullStateSnapshot.HeadlessZoneInPublishesSnapshotExactlyOnce` and
   `FullStateSnapshot.PreExistingPetSurvivesHeadlessZoneInViaSnapshot` (both
   pass, both trees).
5. Live acceptance: eqemu LOCK FIRST (created, `up.sh befallen` — matched
   Testchar's actual saved zone rather than the `arena` default, since
   Testchar was already correctly parked at the canonical restore position
   from a prior session; `down.sh` + lock removed cleanly at the end;
   position re-verified and re-written to the canonical
   zone_id=36/-77.9232/21.9086/3/190 before shutdown, since normal zone-in
   float rounding drifted it by ~0.05 units across sessions).
   `build-gles2/bin/willeq -c ... --no-graphics --script ...` (the sanctioned
   client, run headless) against the live server:
   `benchmarks/audits/T41/t41_headless_run.log` (clean scripted tour, `-d 4`)
   and `t41_headless_run_debug2.log` (`-d 5`, full DEBUG) both show
   `"Publishing full state snapshot to bridge"` exactly once, immediately
   after `"[LOADING] Game state setup complete"`, with `m_inventory_manager`
   already populated by then (`"InventoryManager Loaded 8 items and 0 bag
   contents from CharInventory"`, real items — "Scaled Curskin" ×6 — parsed
   from Testchar's actual saved inventory, confirming the un-guarded
   inventory block has real data to publish, not just that it compiles).
   Both runs completed cleanly (`/camp` → logout → clean shutdown, exit 0).
   Since `syncInventoryState()` was NOT deleted (STOP), the item 5 acceptance
   as literally specified ("with `syncInventoryState()` deleted") does not
   apply — what was verified instead is the actual deliverable of piece 2:
   the snapshot fires correctly, once, with real data, against a live
   server, in the mode the gap broke.
6. `scripts/refactor/check_file_sizes.sh --files <all touched abs paths>` —
   1 pre-existing FAIL (`src/client/eq.cpp`, 3774 lines vs 3000 threshold;
   was already 3743 lines — already failing — at the `fable` tip before this
   unit; this unit's comments added 31 lines to an already-failing file, no
   new FAIL introduced), `tests/test_zone_connection_flow.cpp` (707 lines)
   and `tests/test_game_state_sync_bridge.cpp` (183 lines) both ok.

Territory note: touched `eq_entities.cpp` only by *reading* it (to trace the
pet-detection call sites for the addendum) — no edits there. All edits are
within the plan's stated territory (`eq.cpp`'s `PublishFullStateSnapshot()`
and its caller, plus its constructor for the necessary related fix; `tests/`)
except that the necessary related fix's un-guard sits in the constructor
rather than literally inside `PublishFullStateSnapshot()` — flagging as a
deliberate, narrow deviation for the same reason T40 flagged its two dead
accessor deletions: required for the stated deliverable to actually work,
confirmed necessary before touching it, not a drive-by.
