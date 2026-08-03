# T34 — Make the shift+click loot path reachable from automation, then verify it

**Slot: t1. x86 only. No Pi. OWNS EQEMU (lock protocol applies).**
No sibling unit.

**Territory: `src/client/action/command_processor.cpp` (the `/loot`
stub), whatever minimal plumbing that needs, and new/updated tests.**
Do **not** change `CombatManager`'s loot state machine — T33 just landed
there and this unit is verifying it, not modifying it. If you conclude
the machine itself is wrong, that is a finding to report, not to fix.

Read `refactor/EXECUTOR_PROTOCOL.md`, then `docs/decisions.md`
2026-08-02 "Automation is a first-class requirement", then
`refactor/T33_loot_death_event_wiring.md` and
`refactor/T32_loot_verification.md` Reviews. T33's
`benchmarks/audits/T33/run_verification_primary.sh` is a working
piped-stdin live harness — **start from it rather than writing a new
one.**

## Why this unit exists

T26 removed four blocking `sleep_for` calls from the loot path. T32 then
proved the *kill→loot* route never fired at all (dead since long before
T26), and T33 wired it to the death event and verified it live.

But the route T26 was actually built for — the one T23 measured stalling
the game thread **~1.1 s per corpse** — is the **action-layer loot path**:

```
shift+click on a corpse
  → GraphicsInputHandler::consumeLootRequest()
  → InputActionBridge::processLootRequests()   (input_action_bridge.cpp:379)
  → ActionDispatcher::lootCorpse(corpseId)     (action_dispatcher.cpp:473)
  → EqActionHandler::lootCorpse(corpseId)      (eq_action_handler.cpp:353)
  → CombatManager::LootCorpse()  → COMBAT_STATE_LOOTING, m_player_looting_corpse_id = 0
  → zone dispatch takes the sleeping branch (eq.cpp:1021) → CheckAutoLoot()
  → (T26) LootSubState::AwaitingItems
```

**That path has never been verified live.** It is the last piece of
T26's value that rests on unit tests alone.

## The blocker, and why fixing it is in scope

**`/loot` is a stub.** `CommandProcessor::cmdLoot()`
(`command_processor.cpp:1077`) only prints *"Loot functionality requires
corpse detection - use '/list' to find corpse IDs"* and returns Success.
It never calls `m_dispatcher.lootCorpse()`.

So today `EqActionHandler::lootCorpse` is reachable **only** from the
renderer's shift+click overlay callback — there is no command-channel
route at all. (`ConsoleInputHandler::consumeLootRequest()` exists but its
`m_lootRequests` queue is never populated, so that is not a route
either.)

This is the **third** instance of the same pattern in three units: a
real feature reachable only through a UI gesture, invisible to
automation (T26 hit it with `/combat on`, T29 hit it with the whole
EverQuest registry, now this). Under the 2026-08-02 standing policy that
is a defect to fix, not a limitation to work around — so implementing
`/loot` is in scope here rather than being deferred.

### Scope item 1 — implement `/loot`

Make `/loot <corpseId>` call `m_dispatcher.lootCorpse(corpseId)`, i.e.
enter the exact same path shift+click enters. The last three hops must be
**identical** to the gesture's — that identity is the whole reason this
verification means anything.

Keep it small and honest:
- An explicit corpse id is the required form. The stub's message hints
  that "nearest corpse" was why this was left unimplemented; **do not
  build corpse-proximity detection in this unit.** If a no-arg form is
  trivial given what the client already tracks, adding it is fine, but
  it is not the deliverable and must not expand scope.
- Update the stub's help text so it stops telling users the feature
  does not exist.
- If `/loot` collides with a name in the EverQuest registry (T29's
  dispatch tries that first), say so and resolve it explicitly — a
  silently shadowed command would defeat the point.

### Scope item 2 — verify the path live

Using the now-working `/loot`, drive the action-layer path end to end
against a real server and show:

- **The path is genuinely the shift+click one** — `LootCorpse()` sets
  `COMBAT_STATE_LOOTING` with `m_player_looting_corpse_id == 0`, and zone
  dispatch takes the `eq.cpp:1021` branch into `CheckAutoLoot()`. Show
  it in the DEBUG log; do not infer it.
- **The machine enters `AwaitingItems`** and advances
  (`→ LootingItems → Closing`).
- **Items actually move into inventory.** Confirm the corpse has items
  first; a clean run against an empty corpse proves nothing.
- **The game thread is not blocked.** This is the property T26 exists
  for and the one T23 measured at ~1.1 s. Interleave `/who` or chat
  *during* the loot sequence and show it answered. Compare against T23's
  documented pre-T26 behaviour in your Review — that is the before/after
  that makes this unit worth running.

## STOP conditions

- Implementing `/loot` would require touching `CombatManager`'s state
  machine or the loot protocol → STOP; that is not this unit's job.
- The action-layer path turns out **not** to reach `CheckAutoLoot()` —
  i.e. it is dead the way the kill path was → **STOP and report loudly.**
  That would mean T26's remaining justification is also unverified, and
  it is a bigger finding than this unit.
- Corpse-proximity/"nearest corpse" detection starts to look necessary →
  STOP; take the explicit-id form and report.
- Any behaviour change beyond making `/loot` functional → STOP.

## Verification bar

1. `cmake --build <slot>/build -j24` clean. (Slots may carry a stale
   `third_party/irrlicht-gles2-x86/libIrrlicht.a` predating `3328570` →
   missing `eqtGPUPhaseBegin` symbols; rebuild with
   `bash <slot>/scripts/build-gles2-x86.sh --irrlicht-only`.)
2. `ctest --test-dir <slot>/build --output-on-failure` — known
   exceptions: `AudioManagerTest.PreloadCommonSounds` (ALSA) and the
   `RDPServerFrameTest`/`RDPServerLifecycleTest` family (known unfixed
   use-after-free — rerun isolated and say that is what you saw).
3. Golden replay: `test_session_replay` with
   `WILLEQ_SESSION_LOG=/mnt/projects/fable/captures/session.sleq`,
   golden **unchanged**. The worktree needs its own `data/captures`
   symlink or this test **silently SKIPS** — confirm it RAN.
4. Unit coverage for the new `/loot` dispatch (that it reaches
   `lootCorpse` with the right id, and rejects a malformed/missing id
   cleanly).
5. **The live acceptance** above, evidence committed under
   `benchmarks/audits/T34/`. eqemu LOCK FIRST; `up.sh` only; restore
   Testchar to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190; server
   down cleanly afterwards.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — **use the `--files` form with absolute paths.** The bare
   `check_file_sizes.sh fable` form prints nothing and exits 0 when the
   diff is empty, which is indistinguishable from a clean pass (see
   deferred.md).

## Review

(Executor fills: how `/loot` was wired and the proof it enters the same
path as the gesture, the live evidence that dispatch took the
`eq.cpp:1021` branch, the state transitions, items actually looted, the
liveness measurement against T23's ~1.1 s baseline, tests added,
deviations.)
