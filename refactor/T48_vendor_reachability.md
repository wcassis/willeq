# T48 — Make vendor buy/sell reachable from the command channel (retires the automation-reachability pattern)

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the acceptance is a live
vendor transaction, driven entirely by piped commands.**
No sibling unit.

**Territory: the command registration in `src/client/eq_commands*.cpp`
(and/or `src/client/action/command_processor.cpp`, whichever you
justify), plus tests.** Do **not** change `BuyFromVendor`/`SellToVendor`
or the merchant protocol — this unit is about reaching them.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry **"PATTERN:
real features reachable only through a UI gesture, invisible to
automation"**, then `refactor/T34_shiftclick_loot_verification.md` — it
solved the same problem for `/loot` and its shape is the precedent.

## Why this unit exists

The 2026-08-02 ruling makes anything blocking fully automated development
and testing a defect. This pattern has now appeared four times: `/combat
on` (T26), the whole `EverQuest` registry (T29), `/loot` as a stub (T34),
and vendor buy/sell (T42).

**Vendor is the live instance.** `VendorBuyIntent`/`VendorSellIntent`
exist as types and their handlers exist in `ProcessBridgeIntents()`
(`eq_bridge.cpp:1069-1074`, dispatching to `BuyFromVendor(npcId, slot,
quantity)` and `SellToVendor(...)`) — but **nothing anywhere constructs
either intent.** T42 had to add temporary scratch hooks calling the
production functions directly, then revert them, just to run its live
acceptance. The next unit that touches vendor behaviour will write them
again.

Closing this also unblocks the last state-layer sweep item
(NPC-interaction sync), which cannot be exercised without it.

## What the client already gives you — do not re-derive

- `EverQuest::GetVendorNpcId()` / `IsVendorWindowOpen()` —
  `m_vendor_npc_id` is 0 when not trading, so **commands should default
  to the open vendor rather than making the caller pass an npc id**.
- `m_vendor_items` (`eq.h:1864`) — a `slot -> GameVendorItem` map,
  populated in `eq_merchant.cpp` (~1139) and cleared on window
  close/zone. That is the vendor's inventory, already in memory.

## Reachability is only half the gap — discoverability is the other half

T42 could not simply call `BuyFromVendor()`; it had to know **which
slot**. A command that requires a slot number the operator has no way to
obtain is reachable but not usable, and the next unit is back to
guessing.

So this unit delivers **both**:

1. **A listing** — surface `m_vendor_items` (slot, item name, price,
   quantity) so a script can discover what is for sale. Name it as you
   see fit; note that no `buy`/`sell`/`vendor*` command exists today, so
   there is no collision to resolve.
2. **The actions** — buy and sell, defaulting to the open vendor,
   taking a slot and an optional quantity.

## Fidelity — the point of the exercise

T34's lesson: the value of a command-channel path is that it enters the
**same** production path the gesture does. The gesture's route is
`renderer → pushIntent(VendorBuyIntent) → ProcessBridgeIntents →
BuyFromVendor(...)`.

You may either push the intent (maximum fidelity — and note that
**`ProcessBridgeIntents()` now runs headless since T45**, so this works
in both modes for the first time) or call `BuyFromVendor()` directly.
Either is defensible; **say which you chose and exactly which hops
differ from the gesture's**. Do not claim parity you do not have.

## STOP conditions

- Buy/sell turns out to need renderer-side state the command path cannot
  reach → STOP and report what.
- Making these reachable would require changing `BuyFromVendor`/
  `SellToVendor` or the merchant protocol → STOP; that is a different
  unit.
- A command name collides with an existing registry entry → resolve
  explicitly and say how; a silently shadowed command defeats the point
  (T29's dispatch tries the `EverQuest` registry first).
- Any behaviour change beyond command reachability → STOP.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); the pinned tests from
   T43/T44/T45/T46/T47 must stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. Unit tests for argument handling — missing/malformed slot, no vendor
   open, quantity defaulting — following `test_command_processor.cpp`'s
   `/loot` cases (T34) as the model.
5. **Live acceptance, and this is the deliverable**: a real eqemu session
   driven **entirely by piped commands** — open a vendor, list its
   inventory, buy an item, sell it back — with the inventory reflecting
   both. **No temporary hooks**: if you find yourself adding scratch code
   to make the run work, that is the bug this unit exists to fix, so stop
   and fix it properly instead. Run it in **both** trees, since headless
   intent processing is newly enabled (T45). eqemu LOCK FIRST; `up.sh`
   only; restore Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3,
   h=190; `down.sh` after. Evidence under `benchmarks/audits/T48/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: the commands added and where registered, the fidelity
choice and which hops differ from the gesture, how the listing sources
`m_vendor_items`, the live buy/sell transcript with no scratch hooks, and
whether anything else in the intent list still has no producer — that
last one determines whether the pattern can be closed or only narrowed.)
