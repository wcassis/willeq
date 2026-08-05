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
