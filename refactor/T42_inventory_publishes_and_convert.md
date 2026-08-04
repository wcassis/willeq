# T42 — Publish the missing inventory mutations (a real UI bug), then convert `syncInventoryState()`

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the acceptance is a live vendor
transaction.** No sibling unit.

**Territory: the inventory mutation paths in `src/client/eq.cpp` and
`src/client/eq_merchant.cpp`, `src/client/bridge/game_state_sync_bridge.cpp`
(+ header), `syncInventoryState()` in `src/client/application.cpp`, and
tests.**

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T41_inventory_state_via_bridge.md`'s Review — it STOPped on
exactly this and enumerated the paths — then `refactor/T40_pet_state_via_bridge.md`
for the decorator seam and the sweep's goal.

## This is a user-visible bug fix, not just plumbing

T41 stopped because inventory is not reconstructible from events: three
live paths mutate slots without publishing `InventorySlotChanged`. While
scoping this unit the orchestrator established **why that matters beyond
the sweep**:

**S2 removed the renderer's `InventoryManager` pointer.** Its own note in
`irrlicht_renderer.h` says the stored pointer "was dead and no
game-thread object crosses the boundary now" — the display fields were
moved into `InventorySlotChangedData` precisely so the renderer would
stop reading game-thread state directly.

So the inventory UI has **no other way** to learn about a slot change.
Which means, today: **items bought from a vendor, sold to a vendor, or
appearing in the bank window do not show up in the inventory UI** until
something else republishes — in practice the next zone-in snapshot.

Verify that claim early and report what you actually observe. If the UI
does update by some path this analysis missed, say so — that changes what
this unit is, and is worth more than proceeding on a wrong premise.

## Scope

### 1. Publish on the mutations that don't

From T41's Review, confirmed:

- **Vendor buy** — `ZoneProcessShopPlayerBuy` (`eq_merchant.cpp:905`); the
  item actually lands via the generic `HC_OP_ItemPacket` fallback in
  `eq.cpp`'s dispatcher, which calls
  `m_inventory_manager->processItemPacket(p)` and publishes nothing.
- **Bank-window population** — same generic fallback.
- **Vendor sell** — direct mutation; `removeItem()` at
  `eq_merchant.cpp:995` (and the adjacent `getItemMutable()` quantity
  decrement) with no publish anywhere after it.

Publish `InventorySlotChanged` for the slots each of these actually
changes, with the same payload shape the existing publishers use
(`slotId`, `hasItem`, `itemId`, plus S2's display fields — populate them,
an event with empty display fields is worse than useless to the UI).

**Do not double-publish.** `eq_merchant.cpp:80` and `:105` already
publish for item move and delete (D18a). Adding a second publish for a
mutation already covered would push duplicate events at consumers that
are not all idempotent. Check each path against the existing publishers
before adding.

Prefer publishing where the mutation happens over sprinkling calls at
call sites — if `processItemPacket()` is the single choke point for the
generic path, that is the honest place, and it fixes bank and vendor buy
together.

### 2. The graphics gate on the inventory path

The generic inventory branch in `eq.cpp`'s dispatcher is wrapped in
`#ifdef EQT_HAS_GRAPHICS`, so an `EQT_GRAPHICS=OFF` build **drops regular
inventory item packets entirely** — inventory is never populated at all
there. That is the same declassification family as P28, T29, and T41's
own `m_inventory_manager` construction fix, and it will defeat item 3 in
headless if left.

Un-guard it unless there is a genuine graphics dependency, in which case
name the dependency rather than working around it.

### 3. Then convert

With the events complete: extend `GameStateSyncBridge` with an
`InventorySlotChanged` case, delete `syncInventoryState()` and its
`GetInventoryManager()` read from `Application`. T41 already pinned the
current "not yet handled, delegates untouched" default with a test —
re-point it.

**Do the conversion only if items 1 and 2 actually close the gap.** If
some mutation path still escapes, STOP with it named; a half-converted
inventory is worse than the poll, because the poll at least
self-corrects.

## STOP conditions

- The UI turns out to update vendor/bank items by some path this plan
  missed → STOP and report; the premise is wrong and the unit should be
  re-scoped.
- A mutation path resists publishing without restructuring
  `InventoryManager`'s API → STOP and report which.
- Un-guarding the inventory branch has a real graphics dependency → STOP,
  name it, and do items 1 and 3 for graphics mode only, saying so.
- Adding publishes changes what graphics-mode consumers *already*
  received for existing paths (i.e. you introduced a duplicate) → STOP.
- Any behaviour change beyond inventory event publication and where the
  state is read from → STOP.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest --test-dir <slot>/build --output-on-failure` — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); a failure or `Skipped` on
   either is a real signal.
3. Golden replay unchanged and confirmed to have RUN. **If it moves, that
   is a real finding** — new events in the recorded stream would change
   the replayed state — report rather than re-bless.
4. Unit tests: the new `InventorySlotChanged` case in the decorator, and
   coverage that each newly-published path emits exactly one event per
   changed slot.
5. **Live acceptance, and this is the user-visible part**: a live eqemu
   session (LOCK FIRST; `up.sh` only; restore Testchar to zone_id=36,
   x=-77.9232, y=21.9086, z=3, h=190; `down.sh` after) showing a vendor
   **buy** and a vendor **sell** with the inventory reflecting both —
   before/after, so the fix is visible rather than asserted. Run the
   headless leg too, since item 2 is specifically about that build.
   Evidence under `benchmarks/audits/T42/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: whether the UI-doesn't-update premise held, where each
publish was added and why there, the no-double-publish check, the
graphics-gate verdict, whether the gap actually closed and the conversion
therefore happened, the live before/after, deviations.)
