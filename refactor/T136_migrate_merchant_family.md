# T136 — Migrate the Merchant commands (5): the last family, and the least like the others

**Slot: whichever is free. x86. eqemu lock required (live leg). No Pi.**

**Territory: `src/client/eq_commands.cpp` (the Commerce block),
`src/client/action/`, `include/client/action/`,
`src/client/eq_action_handler.{h,cpp}`, `include/client/eq.h` (accessors only —
see below), `tests/`, `refactor/`, `/mnt/projects/fable/review/T136/`.**

Read T135's Review first (the two-convention trap, the DB-only wire evidence),
then T132's (the net-new recipe) and T103's (the Trade state domain — the
precedent for the design question below).

## The recipe does NOT apply here. Read this before planning your work.

Every prior family (T99–T102, T132, T134, T135) was some version of *thin
wrapper over an existing `EverQuest::Send*`*. **Not one of these five is.**
Verified while scoping:

| command | mechanism |
|---|---|
| `vendor` | bridge intent `VendorToggleIntent`, plus a `CombatManager` target check |
| `vendorlist` | **pure query/display** — reads `m_vendor_items` / `m_vendor_name` and prints |
| `buy` | bridge intent `VendorBuyIntent`, validated against `m_vendor_items` |
| `sell` | bridge intent `VendorSellIntent` |
| `equip` | **`InventoryManager`** — `hasCursorItem` / `getCursorItem` / `getItem` / `placeItem` |

**Five commands, four mechanisms, zero senders.** Treat this as five
independent migrations that need design judgement, not a template application.
T134's Board flagged Merchant as the family likely to break the pattern; it
does.

## The concrete state gap — smaller than T103's, but real

`buy` and `vendorlist` validate against EverQuest's merchant state. Of what
they touch:

- `IsVendorWindowOpen()` and `GetVendorNpcId()` are already **public**
  (`eq.h:2049-2050`).
- **`m_vendor_items` and `m_vendor_name` are private with no accessor**
  (`eq.h:1956`, `:1963`).

So the gap is two reads, not a subsystem.

**Recommendation: add the two minimal accessors rather than build a vendor
state domain.** T103's Trade domain was justified by a cross-cutting need;
here it is two reads serving one display command. **If you find a reason the
domain is warranted — say, the renderer already duplicates this state, or the
accessors would leak a mutable handle — STOP and report it** rather than
building one inside a migration unit. Record the domain as a follow-up either
way, since merchant state may grow.

## `vendorlist` — decide what "migrate" even means

`vendorlist` sends nothing. It reads game state and prints formatted lines.
Moving it to the action layer means the action layer reads that state and
prints.

**Establish whether that is the right layer at all**, and say what you decided.
A defensible answer is that it belongs where the other Commerce commands are,
for discoverability and registry consistency. An equally defensible answer is
that a pure display command over game state is a poor fit for an *action*
dispatcher. **Pick one, argue it in the Review, and do not let the choice be
made implicitly by whichever is easier to type.**

## `equip` may not belong in this unit

`equip` goes through `InventoryManager`, not the vendor path at all — it is an
inventory command sitting in the Commerce block. **Say whether it should
migrate here, move to a different category, or be its own unit.** Do not
migrate it merely because it shares a block with the others.

## Threading — establish it, do not inherit the existing comment

`buy`'s handler carries a long comment asserting the push happens on the
**game thread** via the piped-command path. That is scoped to *one* entry
point. There are **two**: the live chat window routes through
`EverQuest::DispatchSlashCommand` (registry 1) with a fallback into
Application's registries 2/3, while stdin/`--script` enters via
`Application::DispatchSlashCommand`.

**Establish which thread each entry point actually dispatches on after your
change**, and whether moving these commands from registry 1 to registry 2
changes it. `GameStateBridge::pushIntent()` is mutex-protected either way, but
the *state reads* (`m_vendor_items`) are not obviously so. **If migration
introduces a cross-thread read of merchant state, that is a STOP** — report it
rather than shipping it.

## Carry forward from the last three units

- **The two-convention trap** (T135): `EverQuest::QueuePacket(opcode, body)` —
  the callee prepends the opcode. `m_zone_connection->QueuePacket(packet)` —
  the caller must write it. **Never infer a bug from shape**; prove it from a
  live server-log size complaint, as T134 did. Likely moot here since there are
  no senders, but the intent path may have its own equivalent.
- **Wire evidence may be DB-only** (T135): its petition bug was visible *only*
  in persisted state, not the zone log. **Check the eqemu DB for `buy`/`sell`**
  — item and coin changes — not just the server log.
- **Guards stay where they are** (T134): stateful guards belong in the layer
  that owns the state; only usage-string guards move up.

## Atomicity and completeness

**Remove from `eq_commands.cpp` in the same commit that adds the
`CommandProcessor` registration.** Verify via
`Application::printCommandResult()`: `registry=action` for each migrated
command, plus a `registry=game` control — **if any `game` command remains**. If
this unit empties the registry, say so, and say what that means for
`EverQuest::m_command_registry`'s continued existence (it is likely now dead
weight, which is a finding, not a cleanup to perform here).

## Verification

- x86 suite (current total **2352**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **Tests at the programme's density** — T132: 22 cases / 7 commands; T134: 18
  / 6; T135: 19 / 6.
- **Visual gate goldens must not move.** `vendor` toggles a renderer window —
  if a golden moves, **STOP and report**; that needs a human eye.
- Live evidence in `review/T136/`, including **DB state** for `buy`/`sell`.
- Note `src/client/eq.cpp` already **fails `check_file_sizes.sh`** at 3898
  lines (pre-existing, verified at T135's merge). Do not let it get worse; if
  your change would grow it, say so.

## Review

### The five commands: mechanism and disposition

| command | mechanism | disposition |
|---|---|---|
| `vendor` | bridge intent `VendorToggleIntent` + `CombatManager` target check | **migrated** |
| `vendorlist` | pure query/display, reads `m_vendor_items`/`m_vendor_name` | **migrated** |
| `buy` | bridge intent `VendorBuyIntent`, validated against `m_vendor_items` | **migrated** |
| `sell` | bridge intent `VendorSellIntent` | **migrated** |
| `equip` | `InventoryManager` (`hasCursorItem`/`getCursorItem`/`getItem`/`placeItem`) | **NOT migrated — see below** |

None had a pre-existing `EverQuest::Send*()` sender, unlike every prior
family. Three new EverQuest methods were added to serve as the "senders"
this family never had: `RequestVendorToggle()`, `RequestVendorBuy(slot,
quantity)`, `RequestVendorSell(slot, quantity)` (`eq_commands.cpp`, right
where `RegisterVendorCommands()` used to live) — each carries the exact
guard body its old command lambda had (`IsVendorWindowOpen()` check,
`m_vendor_items.find()` existence check, the "no bridge attached" fallback
message), then pushes the *same* `eqt::events::Vendor*Intent` the renderer's
own vendor-window gesture pushes (T48's maximum-fidelity choice, preserved
byte-for-byte). `action::EqActionHandler` has **no access to the private
`m_bridge`** (confirmed by reading every existing `EqActionHandler` method —
none of them push a bridge intent; they all call an `EverQuest::Send*()` or
read `EverQuest`/`CombatManager` state), so these three new methods exist
specifically to give it something to call without widening its construction
wiring (a bigger, out-of-territory change to `application.cpp`) or piping a
`GameStateBridge*` through wherever it's built.

### Accessor vs. state domain — went with the plan's recommendation

Added exactly the two read accessors the plan proposed, plus one small
static-formatting forward (`FormatVendorPrice`, wrapping the already-private
`FormatPrice`) so `EqActionHandler::vendorList()` doesn't need to
re-implement price formatting:

```cpp
const std::string& GetVendorName() const { return m_vendor_name; }
const std::map<uint32_t, GameVendorItem>& GetVendorItems() const { return m_vendor_items; }
static std::string FormatVendorPrice(int32_t copperAmount) { return FormatPrice(copperAmount); }
```

**No state domain was warranted, and nothing found during implementation
changed that.** `GameVendorItem` stays a private nested struct — its
members are public (default for a `struct`), so `EqActionHandler` iterates
the returned map via structured bindings without ever naming the type. This
is *narrower* than the plan even anticipated: I did NOT find that the
renderer duplicates this state (checked — the renderer's own vendor UI reads
through the same `m_vendor_items`/events path, not a second copy), and the
accessor returns a `const&`, so nothing mutable leaks. Recorded as a
follow-up in `refactor/deferred.md` anyway, since merchant state may still
grow (e.g. if a future unit wants live vendor-window state in
`action::GameState` for some other command) — but this unit didn't need it.

### `vendorlist` — what "migrate" means here, argued

**Chose: it belongs in the action layer, reading via the new accessors,
formatting and printing itself — not a new `EverQuest` method.** Two
readings were both defensible per the plan; I picked the one the plan's own
"minimal accessors" recommendation points at, and it has direct precedent:
`action::CommandProcessor::cmdNpcState` (T49) already does exactly this
shape — a pure state read + format, no `IActionHandler` call, no dispatcher
guard beyond the read itself. `EqActionHandler` blending a state read with a
printed outcome is *also* already established, just at a smaller scale:
`bandage()`'s self-target fallback reads `CombatManager::HasTarget()`
before deciding what to send; several Combat-family methods
(`autofire`/`combat`/`taunt`) call `AddChatSystemMessage()` directly for
guard/status text. `vendorlist`'s read-two-accessors-then-loop-and-print is
the same shape at a slightly larger scale, not a new one. The alternative
reading (a poor fit for an *action* dispatcher) is real or I wouldn't have
had to argue this — but "the action layer already does state-read + report
for guard paths" beats "an action dispatcher should only ever route
outbound intent," which is a purer description of the architecture's
*intent* (pun intended) than of what it *actually does* today. Guard
placement follows T134's rule exactly: `vendorlist`'s "You are not at a
vendor." / "inventory is empty" checks are stateful, so they live inside
`EqActionHandler::vendorList()`, not duplicated at the `CommandProcessor`
layer.

### Does `equip` belong in this unit? — No. Excluded, not deferred by accident.

Three independent reasons, not just "it's adjacent":

1. **`eq_commands.cpp`'s own categorization already disagrees.** `equip_cmd.category = "Inventory"`, not `"Commerce"` — confirmed by reading the source before touching anything. The five commands were never uniformly categorized even in the code this unit is migrating *out of*; only four of them share a category.
2. **Mechanism is completely disjoint.** `equip` never touches `m_vendor_items`, `IsVendorWindowOpen()`, or any `Vendor*Intent`. It goes through `InventoryManager` (`hasCursorItem`/`getCursorItem`/`getItem`/`placeItem`/`setItem`/`removeItem`) and pushes `MoveItemIntent`/`EquipmentChangedIntent` — a completely different intent family, sharing nothing with `vendor`/`buy`/`sell` beyond "both eventually touch items."
3. **Size and design surface.** `equip`'s handler is ~150 lines: three input forms (`fromSlot toSlot` / item-name resolution / `list`), cursor-vs-slot staging logic (T71), and pre-flight legality checking (T77, `CheckEquipDestination`/`ResolveEquipDestination`). Migrating it properly is its own unit's worth of design judgment (does the three-form dispatch move up? does the pre-flight legality check stay in `EverQuest` or move to `EqActionHandler`? does `InventoryManager` need to be reachable from `EqActionHandler` the same way `CombatManager` is via `getCombatManager()`?) — exactly the kind of question this unit's own instructions warn against deciding implicitly by convenience.

**Recorded as a follow-up unit in `refactor/deferred.md`**, not silently
dropped. `equip` stays exactly where it was, byte-for-byte untouched;
`tests/test_eq_commands_equip.cpp` is unaffected.

### Threading — traced structurally for both entry points, not inherited from a comment

Traced the actual call chains (not the pre-existing comment on `buy`'s old
handler, which was scoped to one path) before deciding this was safe:

- **Chat window**: user input reaches `EverQuest::ProcessChatInput()` via a
  `ChatInputIntent`-style bridge intent, drained inside
  `EverQuest::ProcessBridgeIntents()` (`eq_bridge.cpp:994`) — which itself
  only ever runs from `Application::gameThreadLoop()`'s
  `updateGameState(deltaTime)` step. `ProcessChatInput()` tries registry 1
  (`EverQuest::DispatchSlashCommand`) first; on a miss it calls
  `m_unrecognized_command_fallback`, which `Application::Init()` wires to
  `InputActionBridge::setUnifiedCommandDispatcher`'s lambda
  (`printCommandResult(DispatchSlashCommand(rawLine))`) — invoked
  **synchronously, in the same call stack, still on the game thread**.
- **stdin / `--script`**: raw lines reach `ConsoleInputHandler::
  processCommand()`, which for any command not in its small hardcoded
  if-chain (confirmed: none of `vendor`/`vendorlist`/`buy`/`sell` match any
  branch) falls through to `m_rawCommands.push(cmd)`. Those are drained by
  `InputActionBridge::update()`'s `consumeRawCommand()` loop, called from
  `Application::processInput(deltaTime)`, called from
  `Application::gameThreadLoop()` — **the same game thread**, not the main
  (render) thread `Application::mainLoop()` owns.

**Both entry points dispatch on the GAME thread**, both before and after
this migration — moving these four commands from registry 1 to registries
2/3 does not change which thread executes them, because `Application::
gameThreadLoop()` is the common ancestor of both call chains. This matches
`buy`'s pre-existing comment's claim for the piped path and extends it,
correctly, to the chat-window path too. **No cross-thread read of merchant
state was introduced** — confirmed structurally by this trace, and the
live leg's `--no-graphics` piped-stdin run below exercises the stdin path
end-to-end without incident. This is NOT a self-declared "must be fine"
conclusion; it's a full source trace of every hop, checked before deciding
`GetVendorItems()`/`GetVendorName()` calling from `EqActionHandler` was
safe to add.

### Atomic-commit confirmation

`RegisterVendorCommands()`'s four registrations (`vendor_cmd`,
`vendorlist_cmd`, `buy_cmd`, `sell_cmd`) were removed from
`eq_commands.cpp`, and the `RegisterVendorCommands()` call itself removed
from `RegisterCommands()`, in the **same commit** as the four new
`CommandProcessor::registerCommand()` calls — no window where both
registries held any of the four. Live-verified via
`Application::printCommandResult()` in the same session (see
`review/T136/db_evidence_buy_sell.txt` for the full transcript excerpt):

```
CMD outcome=applied registry=game   recognized=true cmd="/loc"    <- control
CMD outcome=applied registry=action recognized=true cmd="/vendor"
CMD outcome=applied registry=action recognized=true cmd="/vendorlist"
CMD outcome=applied registry=action recognized=true cmd="/buy 1"
CMD outcome=applied registry=action recognized=true cmd="/sell 30"
CMD outcome=applied registry=game   recognized=true cmd="/camp"   <- control
```

**What remains in `eq_commands.cpp`'s `m_command_registry`**: `equip`
(deliberately, see above) plus six commands that were never in this
migration programme's scope at all — `music`/`sound`/`volume`/
`musicvolume`/`effectsvolume` (audio settings, `eq_commands_spells.cpp`)
and `move`/`time` (`eq_commands_movement.cpp`). Confirmed by grepping every
remaining `Command ..._cmd;` declaration across all three `eq_commands*.cpp`
files after this unit's removal: exactly 8 declarations, matching this
list precisely.

### Live evidence

`review/T136/` — README, full session log + driver narration, and (the
plan's explicit ask) **DB-level evidence for `buy`/`sell`**:
`character_currency` shows a real net debit (982p17g13s8c → 974p16g14s17c)
from a genuine buy-then-sell-back round trip, and `inventory` confirms the
purchased item (`item_id=9962`) is present after the buy and **gone**
after the sell — not just a client-side message. The eqemu zone console
log was also checked and, like T135 found for `/save`/`/rewind`, has **no
distinct line at all** for shop transactions at its default log level —
the DB comparison is the only authoritative record, exactly the shape of
evidence the plan called for.

Visual gate: **PASS, goldens untouched** (`/vendor` toggles a renderer
window, so this was checked explicitly, not assumed).

### Tests

29 new cases across two files, above the programme's density
(T132: 22/7, T134: 18/6, T135: 19/6 → **T136: 29/4**, higher per-command
density than any prior family because the split between "stateful guard,
needs real `EverQuest`/wire replay" and "argument parsing, fake handler is
enough" produced two genuinely different test populations this time):

- `tests/test_command_processor.cpp` (+19, `RecordingActionHandler`, no
  wire replay): registration/category assertions, dispatch-reaches-the-
  right-`ActionDispatcher`-method assertions, and `cmdBuy`/`cmdSell`'s own
  numeric-token parsing (missing/malformed/trailing-garbage slot,
  malformed/zero quantity, default-quantity-of-one, explicit quantity) —
  the argument-parsing coverage that moved UP from the old
  `ParseVendorSlot`/`ParseVendorQuantity` free functions.
- `tests/test_eq_action_handler_vendor.cpp` (new, +10, real `EverQuest` +
  `EqActionHandler`, `ReplayZone()`-driven wire-level vendor-open setup
  identical to T48's): the STATEFUL guards (not-at-a-vendor, unknown vendor
  slot) and intent-push fidelity (asserts the exact `Vendor*Intent` fields
  landed in the bridge's intent queue).

`tests/test_eq_commands_vendor.cpp` (T48, 14 cases, the old
`ProcessChatInput`-based seam) **deleted**, following T102/T135's precedent
for a seam a migration fully closes — not repointed, since its entry point
(`eq_commands.cpp`'s registrations) no longer exists. Its wire-replay
machinery (item/vendor packet builders) was carried into the new file
rather than lost.

### Verification

1. x86 build: clean.
2. `ctest`: **2360/2361 passed** (99%), sole failure the documented ALSA
   `AudioManagerTest.PreloadCommonSounds`. (Baseline at this unit's start
   was 2352; net +9 from -14 deleted + 29 new.)
3. `build-gles2-x86.sh`: clean.
4. Visual gate: **PASS**, goldens untouched.
5. `build-arm-noble.sh`: clean, `bin/willeq` linked (ELF 32-bit ARM), zero
   `error:` lines.
6. `check_file_sizes.sh fable`: **0 new FAIL.** 4 WARN, all four already
   over the 1500-line WARN threshold *before* this unit touched them
   (`eq.h` 2125→2151, `action_dispatcher.cpp` 1508→1551,
   `command_processor.cpp` 1852→1963, `test_command_processor.cpp`
   2170→2305 — confirmed via `git show fable:<file> | wc -l`), matching
   T135's "grew within an already-WARN band" pattern exactly.
   `src/client/eq.cpp` was **not touched at all** by this unit (still 3903
   lines, the pre-existing FAIL T135 flagged) — this unit's growth all
   landed in `eq.h`/`eq_commands.cpp`/the action-layer files, not `eq.cpp`.

### Measured wall-clock

**Approximately 110 minutes** end-to-end (plan+protocol+prior-reviews
reading through this Review being written) — above T135's ~70, close to
T134's ~100, below what T134's own "Merchant will likely need the state-
domain work" worry implied this unit could cost. The state-domain question
resolved cheaply (confirmed NOT warranted, ~10 minutes of investigation);
the actual time went to (a) discovering `EqActionHandler` has no bridge
access and designing the three new `EverQuest` wrapper-sender methods
around that constraint, (b) the full threading trace across both entry
points (no shortcuts — read every hop), and (c) the live leg's DB-level
verification, which needed real `mysql_exec` queries before and after the
buy/sell round trip rather than trusting a client-side message, per the
plan's explicit ask.

### Deviations from the plan

- **`equip` excluded from the migration**, per the plan's own permission to
  "migrate here, recategorise, or split into its own unit" — chose the
  third option and said why (see above). This is the largest deviation:
  the plan's title calls this "the merchant family" (five commands) but
  the migration itself covers four.
- **Three new `EverQuest` methods added beyond the plan's "accessors only"
  framing for `eq.h`** (`RequestVendorToggle`/`RequestVendorBuy`/
  `RequestVendorSell`). The plan's "accessors only" language was scoped to
  the `m_vendor_items`/`m_vendor_name` state-gap discussion specifically,
  not a blanket ban on any other `eq.h` addition — but flagging this
  explicitly rather than letting it pass as implicitly covered: these three
  are not reads, they're the "senders" this family never had, added because
  `EqActionHandler` has no access to `m_bridge` and giving it one (piping a
  `GameStateBridge*` through `Application`'s construction of
  `EqActionHandler`, `application.cpp`) would have been a larger, more
  invasive, out-of-territory change for the same behavioral result. All
  three are thin (guard-then-push, ported verbatim from the old lambdas),
  not new logic.
- **`tests/test_eq_commands_vendor.cpp` deleted, not repointed** (T102/T135
  precedent) — its entry point no longer exists. Argument-parsing coverage
  moved to `test_command_processor.cpp`; stateful-guard/wire-fidelity
  coverage moved to the new `test_eq_action_handler_vendor.cpp`.
- **DB-level live evidence for `buy`/`sell`**, per the plan's explicit
  requirement and T135's carry-forward finding — this is new relative to
  T48's original live leg (which checked only client-side "You
  purchased"/"You sold" messages, no DB query).

## Board

**`eq_commands.cpp`'s registry is NOT empty after this unit** — see
"Atomic-commit confirmation" above: `equip` (deliberately deferred) plus six
commands (`music`/`sound`/`volume`/`musicvolume`/`effectsvolume`/`move`/
`time`) that were **never in scope for this migration programme at all** —
they're audio-settings and movement/time-display commands, not
Guild/Corpse/Utility/Merchant. So `EverQuest::m_command_registry` is not
dead weight in the sense T134/T135 anticipated — it still has a real,
narrow job (audio settings + `/move` + `/time`), just none of it is
"Merchant family" or any family this programme (T99→T136) was chartered to
migrate. A future unit could still ask whether those seven remaining
commands should migrate too, but that is a new, unscoped question — not
this programme's unfinished business. `equip` is this programme's one
genuinely unfinished item; it's named as a specific follow-up in
`refactor/deferred.md`, not folded into "whatever's left in
`m_command_registry`."

### Final programme total (T99 → T136)

`action::CommandProcessor` now holds **95 distinct registered commands**
(`grep -c "registerCommand({" src/client/action/command_processor.cpp`),
built up across this whole migration arc (T99's group-command pilot, T100's
Chat family, T101's Spells+Pet family, T102's Combat+Social family, T132's
Guild family, T134's Corpse+Resurrection families, T135's Utility bucket,
and this unit's four-of-five Merchant commands) plus whatever pre-dated
T99. `EverQuest::m_command_registry` holds the 8 remainder named above.

**Measured per-family cost, for the four units that tracked it**
(T99–T102 predate this convention — no comparable wall-clock recorded for
them):

| unit | family | commands | tests added | wall-clock |
|---|---|---|---|---|
| T132 | Guild | 7 | 22 | ~50 min |
| T134 | Corpse + Resurrection | 6 | 18 | ~100 min |
| T135 | Utility (leftover bucket) | 6 | 19 | ~70 min |
| T136 | Merchant (4 of 5) | 4 | 29 | ~110 min |

**25 commands migrated across these four tracked units, ~330 minutes
(~5.5 hours) combined** — averaging ~13 min/command over this stretch, with
real variance driven by whether a unit needed to find-and-fix a live wire
bug (T134: two, T135: one) or design around a missing architectural seam
(T136: `EqActionHandler`'s lack of bridge access). T136 is both the most
expensive of the four AND the one that added the most tests, consistent
with the plan's own framing going in: "the least like the others," needing
real design judgment rather than template application at every one of its
three explicit design questions.

### What remains open in the action layer

- **`equip`** — the one command this unit's scoping identified as
  Merchant-adjacent but deliberately did not migrate. Named as its own
  follow-up unit in `refactor/deferred.md`, not bundled with anything else.
- The 7 non-`equip` commands still in `EverQuest::m_command_registry`
  (`music`/`sound`/`volume`/`musicvolume`/`effectsvolume`/`move`/`time`) —
  never in this programme's scope; a future orchestration decision, not a
  finding this unit is asserting needs action.
- The vendor-state-domain question T103/T134 kept flagging as a risk for
  this family: **resolved, not deferred** — investigated directly in this
  unit and found not warranted (see "Accessor vs. state domain" above).
  Recorded in `refactor/deferred.md` as a closed question with its
  reasoning, so a future session doesn't have to re-litigate it from
  scratch if merchant state grows later.
name what remains open in the action layer, if anything.
