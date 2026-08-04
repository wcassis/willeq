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

(Executor fills: the inventory case added, what trigger you chose for the
snapshot in headless and why, how you proved it does not double-fire in
graphics mode, whether inventory proved fully reconstructible, what was
deleted, and any view on the unread occupancy arrays.)
