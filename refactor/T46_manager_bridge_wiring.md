# T46 — Wire the managers' bridge pointers in both modes (manager-originated events are dead headless)

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `EverQuest::setBridge()` (currently inline in
`include/client/eq.h`), the wiring block in
`EverQuest::InitGraphics()` (`src/client/eq_bridge.cpp`), and tests.**
Keep it narrow — the fix is a few lines; the value is that it completes
the headless story five units have been building.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry T45 added
about manager bridge pointers, then `refactor/T44_spell_state_via_bridge.md`
(whose spell deltas are the concrete casualty).

## The defect

`InventoryManager`, `SpellManager`, `SkillManager` and `HotbarModel` each
hold their own bridge pointer, and those are wired in exactly one place:
`EverQuest::InitGraphics()` (`eq_bridge.cpp:122-132`), which **never runs
headless**.

`EverQuest`'s own `m_bridge` *is* set headless — `Application` calls
`setBridge()` (`application.cpp:618`) on the ConsoleBridge+decorator path
— which is why the full-state snapshot and the `eq.cpp`/`eq_merchant.cpp`
publishes work there, and why T43's pin passes.

So the headless picture today is: **snapshot state ✓, EverQuest-side
deltas ✓, manager-side deltas ✗ — silently.** The concrete casualty is
`SpellManager`'s own `SpellGemChanged` publish, which T44's spell-delta
conversion depends on: in headless it is inert, so gem changes never
reach the state after the initial snapshot.

## The fix

`setBridge()` is a one-line inline setter in `eq.h` that assigns
`m_bridge` and nothing else. Extract the subsystem wiring into a helper
(`wireBridgeToSubsystems()` or similar) and call it from **both**
`setBridge()` and `InitGraphics()`, so the managers are wired whichever
path sets the bridge. `setBridge()` will need to move out of the header
to call into the manager types.

**Ordering is already verified — do not re-derive:** all four managers
are constructed in the `EverQuest` constructor (`eq.cpp:102-169`, ctor at
`:83`), and `Application` constructs `EverQuest` at `application.cpp:187`
before calling `setBridge()` at `:618`. So the managers exist when
`setBridge()` runs.

**Idempotency**: in graphics mode both paths may end up wiring with the
same pointer. That should be harmless — confirm it, and make the helper
safe to call twice.

## The teardown asymmetry — handle deliberately, do not quietly widen it

`Application::shutdown` sets its own `m_bridge = nullptr` and resets
`m_stateSyncBridge`/`m_irrlichtBridge`, **but nothing ever clears
`EverQuest::m_bridge`** — and that teardown block sits inside
`#ifdef EQT_HAS_GRAPHICS`. Nothing calls `setBridge(nullptr)` anywhere
today.

So `EverQuest` already holds a dangling bridge pointer after shutdown,
and wiring four managers adds four more of the same. That is pre-existing
and presumably survivable only because nothing uses them post-shutdown —
which is exactly the kind of reasoning that stops being true later.

Make the helper propagate whatever it is given, **including null**, so a
clear path exists. Then **report** what you find about teardown — whether
headless resets the bridge at all given the `#ifdef`, and whether
`setBridge(nullptr)` should be called on shutdown — rather than fixing
the lifecycle here. If you conclude it is a real hazard, that is a
finding worth its own unit, not a drive-by.

## STOP conditions

- Wiring in `setBridge()` turns out to run before some manager exists in
  a path not traced above → STOP and report the path.
- Making the managers live headless surfaces a manager publish that is
  *wrong* headless (rather than merely absent) → STOP and report; that
  would mean a manager assumes a renderer.
- Any graphics-mode behaviour change → STOP. Graphics already wires
  these; this must be a no-op there.
- Fixing the teardown lifecycle starts looking necessary to land the fix
  → STOP and report rather than expanding.

## Verification bar

1. Both configures build clean.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); T43's `FullStateSnapshot`,
   T44's spell tests and T45's timing tests must all stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `visual_gate` PASS with goldens untouched — graphics mode is supposed
   to be unaffected, so prove it.
5. **Live headless acceptance, and this is the point of the unit**: show
   a *manager-originated* publish arriving in headless that does not
   today. `SpellManager`'s `SpellGemChanged` is the natural one — memorize
   or cast so a gem changes state, and show it reaching the state via the
   decorator with no full-state snapshot involved. A before/after against
   `fable` makes it unambiguous. eqemu LOCK FIRST; `up.sh` only; restore
   Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190; `down.sh`
   after. Testcaster (Wizard) exists — T44 and T45 both used it. Evidence
   under `benchmarks/audits/T46/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: the helper and both call sites, the idempotency check,
the before/after showing a manager-originated event arriving headless,
what you found about teardown and whether it warrants its own unit,
deviations.)
