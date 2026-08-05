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

### Commands added, and where

All four registered in `EverQuest::m_command_registry` (registry 1, T29's
"tried first" registry — the same registry T28's `/assist`/`/shield`/`/duel`
live), via a new `EverQuest::RegisterVendorCommands()` in
`src/client/eq_commands.cpp`, called from the `RegisterCommands()` driver
alongside `RegisterSocialCommands()`/`RegisterMovementCommands()`/
`RegisterSpellCommands()`. Declared in `include/client/eq.h` next to its
three siblings. This registry (not `action::CommandProcessor`, registry 2)
was the only one with direct access to `EverQuest`'s private vendor state
(`m_vendor_items`, `m_vendor_name`, `GetVendorNpcId()`/`IsVendorWindowOpen()`,
`FormatPrice()`) without adding new public surface — the same rationale T28
used for its own registry-1 commands.

- **`/vendorlist`** — lists the open vendor's inventory (slot, name, price,
  quantity). Not part of the plan's two required deliverables in name, but
  required in substance ("no `buy`/`sell`/`vendor*` command exists today, so
  there is no collision to resolve" confirmed by grep — none of the three
  registries had `vendorlist`/`buy`/`sell`/`vendor` before this unit).
- **`/buy <slot> [quantity]`** and **`/sell <slot> [quantity]`** — the two
  required actions. Both default to the currently open vendor via
  `GetVendorNpcId()`, per the plan — the caller never supplies an npc id.
  `quantity` defaults to 1 when omitted.
- **`/vendor`** — opens/closes a vendor window against the current target.
  **Not in the plan's original two-deliverable list — added once scoping the
  mandated live acceptance showed it was required.** The plan's acceptance
  bar says "open a vendor, list its inventory, buy an item, sell it back —
  driven ENTIRELY BY PIPED COMMANDS." Investigating how a vendor window
  opens at all revealed `VendorToggleIntent` — the intent the renderer's
  right-click-a-merchant gesture pushes (`renderer_frame.cpp:618`) — has
  a working handler in `ProcessBridgeIntents()` (`eq_bridge.cpp:866`,
  validating target/NPC-type/class==merchant then calling
  `RequestOpenVendor()`) but, like `VendorBuyIntent`/`VendorSellIntent`
  before this unit, **nothing anywhere constructed it either.** Without a
  command-channel way to open a vendor, the mandated live acceptance would
  have been impossible to run without a scratch hook — precisely the outcome
  this unit exists to prevent. `/vendor` pushes the exact same
  `VendorToggleIntent{}` the gesture pushes; the open/close validation logic
  itself (target/class checks, `RequestOpenVendor()`/`CloseVendorWindow()`)
  is untouched, pre-existing code. This is a direct, in-spirit extension of
  the same unit, not new functionality — flagged here as a deviation from
  the plan's literal command list, not silently absorbed.

No name collisions: grepped `EverQuest::m_command_registry`,
`action::CommandProcessor`, and the renderer's own command surface for
`vendor`/`vendorlist`/`buy`/`sell` before adding — no existing entries in any
of the three registries T29's dispatch tries in order.

### Fidelity choice

**Chose the intent-push path (maximum fidelity)** for all four commands, not
a direct call to `BuyFromVendor()`/`SellToVendor()`/`RequestOpenVendor()`.
Each handler constructs the exact same intent struct the renderer gesture
constructs (`VendorToggleIntent{}`, `VendorBuyIntent{npcId, slot, quantity}`,
`VendorSellIntent{npcId, slot, quantity}`) and calls
`m_bridge->pushIntent(...)`. From there every hop is **identical** to the
gesture's: the intent sits in `GameStateBridge`'s intent queue and is
drained by the same `EverQuest::ProcessBridgeIntents()` switch
(`eq_bridge.cpp:866,1069,1072`) into `RequestOpenVendor()`/
`BuyFromVendor()`/`SellToVendor()` — unmodified, pre-existing code, per the
plan's fence.

**The one hop that differs, stated exactly**: WHO calls `pushIntent()` and
on which thread. The gesture's caller is renderer code on the **render**
thread, crossing to the **game** thread via the bridge's mutex-protected
queue. Here the caller is `EverQuest` itself — piped-command dispatch
(`Application::DispatchSlashCommand()` → registry 1 →
`EverQuest::m_command_registry`) runs on the **game** thread, the same
thread `GameTick()`/`ProcessBridgeIntents()` runs on — so for a
command-channel buy/sell, push and the later drain both happen on the game
thread instead of crossing threads. `GameStateBridge::pushIntent()`'s mutex
makes this safe either way (no race), and the dispatch logic that consumes
the intent is byte-for-byte the same code either way; this is an honest
difference in the calling thread, not a gap in what gets exercised. This is
also the concrete meaning of the plan's T45 note: `ProcessBridgeIntents()`
now runs in `--no-graphics` builds too (`Application::updateGameState()`'s
`m_graphicsInitialized || IsGraphicsReady()` gate, T45), so this same-thread
push-then-drain works in **both** configures — confirmed live in both trees
below, not just asserted.

### How `/vendorlist` sources `m_vendor_items`

Directly — no intent involved, since listing is a read, not an action.
`m_vendor_items` (`eq.h:1864`, `slot -> GameVendorItem`) is populated by
`ZoneProcessVendorItemToUI()` (`eq_merchant.cpp:1101`, called from the
`HC_OP_ItemPacket`/`ITEM_PACKET_MERCHANT` branch in `eq.cpp`'s zone
dispatch) whenever the server sends vendor-window item data, and cleared on
vendor-window close/zone. `RegisterVendorCommands()`'s `/vendorlist` handler
is a plain lambda on `EverQuest` (same file, same class), so it iterates
`m_vendor_items` directly and formats each entry with the existing static
`FormatPrice()` helper — no new state, no new plumbing.

### Unit tests

`tests/test_eq_commands_vendor.cpp` (new; registered in `tests/CMakeLists.txt`
next to `test_eq_commands_assist.cpp`), 19 cases, modelled on T34's
`/loot` argument-handling cases and T28's `MakeEq()`/`TestBridge`/
`ProcessChatInput()` seam:

- Vendor-open state is built via the **real production path**
  (`ReplayZone()` with `HC_OP_ShopRequest` then `HC_OP_ItemPacket`/
  `ITEM_PACKET_MERCHANT`, the latter using a serialized-item string built
  with T43's `BuildItemString()` idiom, reproduced locally per that file's
  anonymous-namespace/internal-linkage), not a test-only backdoor setter —
  no new test seam was added to `EverQuest` itself.
- Argument handling per the plan's bar: missing slot, malformed slot
  (`"notanumber"`, trailing garbage `"5abc"`), unknown vendor slot, no
  vendor open, malformed/zero quantity, and quantity defaulting to 1 when
  omitted — for both `/buy` and `/sell`.
- `/vendor`'s three cases (no target and not open, with target, already
  open without needing a target).
- Assertions are made directly against `bridge.drainIntents()`'s pushed
  `VendorBuyIntent`/`VendorSellIntent`/`VendorToggleIntent` fields (not by
  driving all the way through `ProcessBridgeIntents()` →
  `BuyFromVendor()`/`SellToVendor()` → `QueuePacket()`) — this unit's
  territory is the command's reachability into the intent system, not
  re-verifying `BuyFromVendor`/`SellToVendor` themselves (plan's fence);
  that hop is exercised for real below.

All 19 pass in both trees.

### Live acceptance — buy/sell transcript, no scratch hooks

eqemu LOCK FIRST: confirmed `/mnt/projects/fable/.eqemu-lock` absent, created
with `T48-vendor-reachability 2026-08-05`, removed after clean shutdown and
confirmed absent again. `scripts/eqemu/up.sh arena` (Testchar's `zone_id=36`
resolves to `befallen`, same "arena default zone name is misleading" detail
T32/T33/T34 documented) + `seed.sh`.

Vendor NPC: id 771 `SummonResupplyAgent` (`merchant_id=1000001`, `class=41`
== `CLASS_MERCHANT`, every `merchantlist` row `faction_required=-100` — a
GM-summonable, always-sells resupply vendor with 24 stocked slots; confirmed
against the live DB before the run), spawned at the GM's own position via
`#npctypespawn 771` — same technique T32/T33/T34 used for their mob.
Testchar started at 0pp/0g/0s/0c (confirmed against the DB); the **first**
run attempt (money-less) proved `/buy` reaches `BuyFromVendor()` and sends
the packet, but the server silently drops an unaffordable purchase (no error
packet, no `"You purchased"`, no inventory change — a real environment
finding, not a bug in this unit's commands). Fixed by sending
`#givemoney 500 0 0 0` (a real GM command, `command_add` in eqemu's
`command.cpp`/`givemoney.cpp` — same class of sanctioned server *operation*
as `#npctypespawn`, not a client-side scratch hook) before the buy step.

`benchmarks/audits/T48/run_verification_primary.sh`, driven entirely by
piped stdin (fifo), no keyboard, no renderer mouse click — commands sent:
`/who`, `/say #givemoney 500 0 0 0`, `/say #npctypespawn 771`, `/list`,
`/target <fresh name>`, **`/vendor`**, **`/vendorlist`**, **`/buy 1`**,
`/vendorlist`, **`/sell 30`**, `/vendorlist`, `/who`, `/vendor` (close),
`/camp`. Transcript excerpt (`benchmarks/audits/T48/transcript_primary.txt`):

```
[INFO ] [INVENTORY] Opened vendor window for SummonResupplyAgent (id=174)
[INFO ] [MAIN] SummonResupplyAgent's inventory (24 items):
[INFO ] [MAIN]   [slot 1] Tiny Jade Inlaid Coffin - 69p 7s 9c (qty: 1)
...
[DEBUG] [INVENTORY] Buying from vendor 174: slot=1 quantity=1
[INFO ] [MAIN] You purchased Tiny Jade Inlaid Coffin for 69p 7s 9c
[DEBUG] [INVENTORY] Publishing InventorySlotChanged (generic ItemPacket fallback): slot=30 hasItem=true itemId=9962 name='Tiny Jade Inlaid Coffin' qty=1
...
[DEBUG] [INVENTORY] Selling to vendor 174: slot=30 quantity=1
[INFO ] [MAIN] You sold item for 60p 9g 9s 8c
[DEBUG] [INVENTORY] Publishing InventorySlotChanged (vendor sell): slot=30 hasItem=false itemId=0 name='' qty=0
...
[DEBUG] [INVENTORY] Closing vendor window for NPC 174
```

**Inventory reflects both**: the `InventorySlotChanged` publish shows slot 30
gaining the purchased item (`hasItem=true`) right after `"You purchased"`,
then losing it (`hasItem=false`) right after `"You sold item for..."` — the
buy and the sell each visibly changed local inventory state, not just server
chat text. `npcId=174` in both the buy and sell debug lines confirms
`GetVendorNpcId()` correctly defaulted the target npc id the caller never
supplied. Driver-side slot resolution (`BUY_SLOT` from `/vendorlist`'s own
output, `BUY_INV_SLOT` from the `InventorySlotChanged` publish line) means
the script never hardcoded a slot number — same discipline T34 used to
resolve corpse/slot ids from the transcript rather than guessing.

**No scratch hooks**: `git diff` for this run touched only
`src/client/eq_commands.cpp`, `include/client/eq.h`,
`tests/test_eq_commands_vendor.cpp`, `tests/CMakeLists.txt`, and new files
under `benchmarks/audits/T48/` — no temporary code was added to or reverted
from `eq_merchant.cpp`, `eq_bridge.cpp`, or anywhere else to make the run
work, unlike T42.

**Run in both trees** (T45's "runs headless too" is exercised, not just
cited): `run_verification_primary.sh` was parameterized (`WILLEQ_BIN`/
`LOG_SUFFIX`) and run once against `build/bin/willeq` (`transcript_primary.txt`,
`EQT_GRAPHICS=ON` configure, invoked with `--no-graphics` like T33/T34) and
once against `build-nogfx/bin/willeq` (`transcript_nogfx.txt`,
`EQT_GRAPHICS=OFF` configure). Both completed identically: `/buy 1` →
`"You purchased"` matched within 1s, `/sell 30` → `"You sold"` matched
within 1s, clean `/camp` shutdown, exit code 0.

Testchar restored to `zone_id=36, x=-77.9232, y=21.9086, z=3, h=190` after
both runs (drifted a few thousandths during the runs — same
coordinate-axis noise T33/T34 documented, not real movement). Currency was
**not** restored (not in the plan's restore list, which names only
position) — left at 982pp 17g 13s 8c after the two `#givemoney`/buy/sell
cycles; noted here for transparency. Server brought down cleanly (`down.sh`,
containers stopped/removed), lock removed and confirmed absent.

### Verification bar

1. Both configures build clean (`build` and `build-nogfx`, `EQT_GRAPHICS=ON`/
   `OFF`) — confirmed clean rebuilds after every source change, no new
   warnings introduced by this unit's files.
2. `ctest --output-on-failure` both trees: `build` 1876/1877 passed,
   `build-nogfx` 1444/1445 passed — the one failure each tree is the
   documented `AudioManagerTest.PreloadCommonSounds` (ALSA) exception. RDP
   unwaived (no RDP failures in either run); golden replay does not skip
   (see below); the pinned T43-T47 tests are all in the passing set.
3. Golden replay: `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq`
   run directly against both trees' `test_session_replay` binaries —
   confirmed it took the **real recorded-session match** branch (not the
   "baseline created" fallback: the binary logged real zone-map lookups from
   the capture, e.g. `befallen.map`/`qeynos2.map`), `PASSED` in both.
   `ctest -R SessionReplayGolden` also green in both trees. Golden file
   itself untouched (`git status` on `data/captures` empty both before and
   after).
4. Unit tests: see above, 19/19 in both trees.
5. Live acceptance: see above.
6. `check_file_sizes.sh --files` on every changed/added file: `eq_commands.cpp`
   1364 lines (`ok`), `eq.h` 2027 lines (`WARN` — but **pre-existing**: it
   was already 2026 lines on `fable` before this unit's 1-line declaration
   addition, confirmed via `git diff fable`, so this is not a new warning
   this unit introduced), `test_eq_commands_vendor.cpp` 384 lines (`ok`),
   `tests/CMakeLists.txt` 1226 lines (`ok`),
   `run_verification_primary.sh` 205 lines (`ok`). No new FAILs.

### Anything else in the intent list with no producer

Scanned every `struct` in `RendererIntent`'s variant (`renderer_intents.h`)
against `src/` for a `pushIntent(eqt::events::<Type>...)` call site. Of 56
intent types, roughly two-thirds still have no producer — but **most of
those are not the same defect** the deferred.md pattern describes.
Investigating a sample (`CastSpellIntent`, `MoveItemIntent`,
`DeleteItemIntent`, `HotbarActivateIntent` — all "OK, has a producer" —
versus e.g. `AttackIntent`) shows the client has **two parallel reachability
mechanisms**: intents (renderer gesture → bridge → `ProcessBridgeIntents()`)
and direct command-to-function calls (`eq_commands*.cpp` lambdas calling
`EverQuest`/`CombatManager`/etc. methods directly, no intent involved at
all) — e.g. `/attack` (`eq_commands_movement.cpp:338`) calls
`m_combat_manager->EnableAutoAttack()` directly, never touching
`AttackIntent`. So an intent type with "no producer" is only actually **the
T48 pattern** (a real feature reachable from a gesture but invisible to
automation) if (a) it has a working `ProcessBridgeIntents()` handler *and*
(b) there is no equivalent direct-call command already covering the same
functionality.

Checking that condition specifically for combat (the category most likely
to matter, given T26's history with this exact pattern): **`AttackIntent`
and `ToggleAutoAttackIntent` both have working handlers in
`ProcessBridgeIntents()` (`eq_bridge.cpp:1410,1419`) and, as far as this
unit's grep found, no producer anywhere — not the renderer, not any
command.** They appear to be dead/superseded code: `/attack`, `/stopattack`,
`/aa` already work via direct `CombatManager` calls, so nothing was ever
motivated to push these intents. This wasn't investigated further (out of
this unit's fence — not vendor-related, and fixing dead code is a
"behaviour change beyond command reachability" STOP condition if it turns
out to matter), but it is a candidate for a future small unit or a
deferred.md entry, flagged here as the plan requested rather than fixed.

**Bottom line: the vendor case (the plan's actual subject) is fully
closed** — `VendorBuyIntent`, `VendorSellIntent`, and (found necessary
during scoping) `VendorToggleIntent` all now have real producers reaching
production code through the same hops the gesture uses. **The broader
deferred.md "PATTERN" entry across the whole intent list is narrowed, not
closed** — dozens of other intent types remain unreached by any command,
though most appear to be intentionally superseded by direct-call commands
rather than live gaps; `AttackIntent`/`ToggleAutoAttackIntent` are the one
pair this unit's scan flagged as looking like a genuine, unresolved instance
of the same defect.
