# T51 — Auto-attack has three implementations, two of them unreachable: establish ground truth and consolidate

**Slot: t1. x86 only. No Pi. OWNS EQEMU — the ground-truth step and the
acceptance are both live.**
No sibling unit.

**Territory: the combat command registrations in
`src/client/eq_commands_movement.cpp` (registry 1) and
`src/client/action/command_processor.cpp` (registry 2), the two combat
intents in `include/client/events/renderer_intents.h` and their handlers
in `src/client/eq_bridge.cpp`, plus tests.** Do **not** change
`CombatManager`, `EqActionHandler`, `ActionDispatcher`'s combat methods,
or the keyboard/`InputActionBridge` path — those are live and correct.

Read `refactor/EXECUTOR_PROTOCOL.md`, then
`refactor/T50_bank_train_prevalidation.md` and
`refactor/T48_vendor_reachability.md` (this unit is the same family and
inherits their standards), then the deferred entry "PATTERN: real
features reachable only through a UI gesture".

## Why this unit exists, and what it is NOT

T48 flagged `AttackIntent`/`ToggleAutoAttackIntent` as producer-less with
working handlers, and the board has carried "attack/auto-attack
reachability" ever since. **I checked, and that framing is wrong — this
is not a reachability gap.** The capability is reachable today. What is
actually there is a **three-way duplication**, and it is a concrete,
bounded instance of the open "~37-command registry overlap" item rather
than a new instance of the automation-reachability pattern.

## Ground truth I traced — verify, do not re-derive

There are **three** implementations of enable/disable/toggle auto-attack:

1. **Registry 1** (`eq_commands_movement.cpp:332-370`) — `/attack`
   (alias `att`), `/stopattack`, `/aa` (alias `~`). Calls
   `m_combat_manager->EnableAutoAttack()/DisableAutoAttack()` **directly**
   and prints "Auto attack ON"/"OFF".
2. **Registry 2** (`command_processor.cpp:374-384`) — `/attack`
   (alias `a`), `/stopattack`, `/aa`. Routes
   `cmdAttack`/`cmdStopAttack`/`cmdAutoAttack` →
   `ActionDispatcher::enableAutoAttack()/disableAutoAttack()/
   toggleAutoAttack()` → `EqActionHandler` → the same `CombatManager`.
3. **The two intents** — `AttackIntent`/`ToggleAutoAttackIntent`
   (`renderer_intents.h:34-38`), handled in `ProcessBridgeIntents`
   (`eq_bridge.cpp:1410`, `:1419`). **No producer anywhere.**

**Dispatch order is registry 1 → registry 2 → registry 3**
(`application.cpp`'s `DispatchSlashCommand`/`dispatchToActionOrRenderer`,
T29). So **registry 1 wins every colliding name, and registry 2's three
combat registrations are shadowed dead code.**

Two more facts that matter:

- **The keyboard gesture does NOT use the intents.** It goes
  `InputAction::ToggleAutoAttack` → `InputActionBridge`
  (`input_action_bridge.cpp:78-84`) → `ActionDispatcher` →
  `EqActionHandler`. So the **dispatcher methods are live** even though
  registry 2's *command registrations* are not. Deleting those
  registrations must not touch the dispatcher.
- **`ActionDispatcher::enableAutoAttack()` adds a
  `checkZoneConnection()` gate that registry 1's direct call skips.** I
  verified the gate is genuinely populated on the instance that matters:
  `setZoneConnected(true)` is called on `Application::m_gameState` at
  `application.cpp:743`, unguarded. (Note `eq_entities.cpp:1542` sets it
  on `EverQuest`'s *own* separate instance — the two-GameState trap
  again; do not confuse them.)

## Piece 0 — establish live ground truth FIRST, before changing anything

I traced the shadowing by reading dispatch code. **Prove it live before
acting on it.** The dispatcher logs the winning registry — T49's
transcript shows lines of the form:

```
CMD outcome=applied registry=game recognized=true cmd="/bank"
```

Run `/attack`, `/stopattack`, `/aa`, `/a` and `/att` in a live headless
session against a real target and record which registry serves each.
**If registry 2 turns out to serve any of them, my analysis is wrong —
STOP and report, because the whole shape of this unit changes.**

Also confirm auto-attack actually *engages* (swings happen), not merely
that a command was accepted. This is the "verify `/attack` live headless"
step the board has been carrying, and it is a deliverable in its own
right regardless of what you change afterwards.

## Piece 1 — consolidate

Assuming Piece 0 confirms the trace:

- **Delete registry 2's three shadowed combat registrations**
  (`attack`, `stopattack`, `aa`) and their now-unused
  `cmdAttack`/`cmdStopAttack`/`cmdAutoAttack` methods. **Leave
  `ActionDispatcher`'s combat methods alone** — the keyboard gesture
  needs them.
- **PRESERVE THE `a` ALIAS.** Registry 2's `/attack` carries alias `a`;
  registry 1's carries `att`. Registry 1 has no `a`, so **`/a` is served
  today only by registry 2 and would be silently lost by a naive
  deletion.** Move it onto registry 1's `attack` command and prove `/a`
  still works live. Check the same way for any other alias that only
  exists on the registry-2 side.
- **Delete `AttackIntent` and `ToggleAutoAttackIntent`** and their
  `ProcessBridgeIntents` handlers. Nothing produces them and the gesture
  does not use them.
  - **Check first whether anything depends on `RendererIntent`'s variant
    ordering or index** (serialization, a static_assert on
    `variant_size`, a test asserting indices). If something does, say so
    and adjust rather than silently shifting indices.
  - `AttackIntent` carries a capability the others lack — "target this id
    AND start attacking" in one step. `/target <id>` + `/attack`
    composes to the same thing, so nothing is lost, **but say so
    explicitly** rather than deleting it silently.

**If you conclude a different consolidation is better — e.g. keeping
registry 2's dispatcher-routed version and deleting registry 1's so the
zone-connection gate applies — argue it.** Note the structural
constraint before you do: registry 1's handlers live inside `EverQuest`,
which has no access to `Application`'s `ActionDispatcher`, so "route
registry 1 through the dispatcher" is not a small change. My lean is the
deletion above; the gate difference is worth a sentence in the Review
either way.

## STOP conditions

- Piece 0 contradicts the trace → STOP and report.
- Deleting the intents requires touching the `IrrlichtBridge` event
  switch or any live path → STOP; that is a different unit.
- Any change in what a user-typed combat command actually does, beyond
  the alias consolidation → STOP. **This unit should be behaviour-neutral
  at the command line**; that is the point.
- You cannot get auto-attack to visibly engage live → STOP and report
  rather than asserting from logs alone.

## Verification bar

1. Both configures build clean — graphics and `EQT_GRAPHICS=OFF`.
2. `ctest` both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP unwaived (T35),
   golden replay does not skip (T36); pinned tests from T43-T50 stay
   green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. `scripts/ci/visual_gate.sh` PASS, goldens untouched.
5. Unit tests: that the surviving commands and **every alias**
   (`attack`, `att`, `a`, `stopattack`, `aa`, `~`) still resolve, and
   that the deleted registry-2 names no longer register. Extend
   `test_command_processor.cpp` and/or the registry-1 tests as fits.
6. **Live acceptance, both trees, entirely piped:**
   - The Piece 0 registry table (which registry serves each name).
   - Auto-attack visibly engaging and disengaging against a real
     mob — swing/damage messages, not just command acknowledgements.
   - **Every alias exercised live after the change**, `/a` especially.
   - A before/after showing the command set is unchanged from the user's
     point of view.
   - eqemu LOCK FIRST (check `/mnt/projects/fable/.eqemu-lock` is absent;
     if it holds YOUR unit id from a crashed run, it is yours — do not
     stop over it); `up.sh` only; restore Testchar to zone_id=36,
     x=-77.9232, y=21.9086, z=3, h=190; `down.sh` and delete the lock
     after. Evidence under `benchmarks/audits/T51/`.
7. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## What to report in the Review

- **The Piece 0 registry table** — this is the durable artifact even if
  the consolidation is trivial.
- Whether auto-attack genuinely works from the command channel headless
  (the board has assumed it does; nobody has shown it).
- What you deleted, what you preserved, and the alias audit.
- Whether anything depended on the intent variant ordering.
- **Whether the "~37-command registry overlap" item can now be
  narrowed** — you will have looked at the collision machinery closely.
  If you can cheaply enumerate the other colliding names, that list is
  worth more than this unit's diff. Do not fix them here.
- Be explicit that this **closes the board's "attack/auto-attack
  reachability" item by disproving it** — the capability was always
  reachable; the defect was duplication.

## Commit early

Two executors in a row have been interrupted mid-run by transient API
529s. **Commit working code before starting long-running verification**,
so an interruption costs a resume rather than the work.
