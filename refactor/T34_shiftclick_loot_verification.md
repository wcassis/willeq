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

### Scope item 1 — `/loot` wired to the exact gesture path

`CommandProcessor::cmdLoot()` (`src/client/action/command_processor.cpp`)
now parses its argument as a `uint16_t` corpse id (`std::stol` with
`consumed == args.size()` validation, range-checked to `[0, 0xFFFF]`) and
calls `m_dispatcher.lootCorpse(id)` — the exact same
`ActionDispatcher::lootCorpse()` (`action_dispatcher.cpp:473`) that
`InputActionBridge::processLootRequests()` (`input_action_bridge.cpp:381`)
calls for the shift+click gesture. `ActionDispatcher::lootCorpse()` →
`EqActionHandler::lootCorpse()` (`eq_action_handler.cpp:353`) →
`CombatManager::LootCorpse()` → `BeginLootRequest()` is untouched code —
`/loot` and shift+click are now two callers into the identical last three
hops, which is the whole basis for this unit's live verification meaning
anything. Registration changed from `requiresArgs=false` (silently
succeeding with a "not implemented" message) to `requiresArgs=true`
(usage error on a bare `/loot`); help text updated to `/loot <corpseId> —
Loot a corpse by id (see /list)`.

Per the plan's scope fence, an explicit id is the *only* form — no
nearest-corpse / proximity detection was added.

**Collision check (required by the plan):** grepped all three
`eq_commands*.cpp` files (`EverQuest::m_command_registry`, T29's registry
1, tried before `action::CommandProcessor` on the piped-stdin dispatch
path per `application.cpp`'s three-registry fallback) for `.name = "loot"`
— no match. `/loot` is not registered there, so nothing is silently
shadowing `action::CommandProcessor`'s `/loot`. No collision to resolve.

### Scope item 2 — the action-layer path, verified live

**The path is genuinely the shift+click one, not the death-event one.**
Deliberately left auto-hunting **off** for this run (never sent `/hunt
on`) — `CombatManager::NotifyTargetDied()`'s gate is `if
(!m_auto_loot_enabled || !m_auto_hunting_enabled) return;`, so the T33
death-event route never entered the loot machine on its own. The mob
(npc 79037 `a_scaled_wolf_pup`, same guaranteed-100%-chance item 12654
"Scaled Curskin" T32/T33 used, reconfirmed against the live DB
immediately before this run) was killed via plain `/combat on` +
`/target` + `/attack` (no auto-hunt), then the corpse sat untouched — no
`Auto-loot enabled, looking for corpse` line anywhere after the death,
unlike T33's transcripts — until this unit's own `/loot <id>` was the
**first and only** thing to drive the state machine. Evidence
(`benchmarks/audits/T34/transcript_primary.txt`):

```
16:31:19.371 [COMBAT] Our combat target 169 died, disabling auto-attack
16:31:19.372 [ENTITY] Entity 169 became corpse: '...pup000' -> '...corpse169'
 (2s gap, corpse untouched, no auto-loot line)
16:31:22.441 [COMBAT] Combat state changed to: 3          <- /loot 169 sent
16:31:22.441 [COMBAT] BeginLootRequest: Sending loot request for corpse ID 169
16:31:22.492 [INVENTORY] ItemPacket received, type=102 m_player_looting_corpse_id=0 ...
16:31:22.492 [INVENTORY] Loot window item: slot 22 = ...
```

`m_player_looting_corpse_id == 0` throughout (confirmed on every
`ItemPacket received` line during the whole loot cycle) — this field is
only ever set by the *other* loot route
(`EverQuest::RequestLootCorpse()`, reached from `LootCorpseIntent`, a
third, older mechanism this unit never touches); `CombatManager::
BeginLootRequest()` sends its own `HC_OP_LootRequest` and never assigns
it. That single fact is the DEBUG-log proof that dispatch takes the
`m_player_looting_corpse_id == 0` branch in `eq.cpp`'s `HC_OP_ItemPacket`
handler, which falls through to the `m_combat_manager->IsLooting()`
("headless mode looting") branch → `ZoneProcessLootItem()` →
`CombatManager::CheckAutoLoot()` — confirmed not by inference but because
`ZoneProcessLootItem()`'s own log line (`"Loot window item: slot ..."`,
`eq_combat_actions.cpp:510`) is only ever emitted from that one call site
(`eq.cpp`, the `IsLooting()` branch), and it fired twice in this run.
(Line numbers have drifted a little from the plan's `eq.cpp:1021` since
T33 landed — the branch is still there, now a few lines further down;
confirmed by reading the current file, not assumed from the plan text.)

**The machine enters `AwaitingItems` and advances → `LootingItems` →
`Closing`.** There's no dedicated per-substate log line in `combat.cpp`
(T33 didn't have one either), so this is shown via the causal marker
chain `BeginLootRequest` (enters `AwaitingItems`) → two `Added loot item
in slot N` lines (items populate during `AwaitingItems`) → `Item
quiescence reached (1 items), starting to loot` → `LootItem ACK`
(`LootingItems` draining the queue) → `CloseLootWindow: Sent
HC_OP_EndLootRequest` (`Closing` → done) → `Combat state changed to: 0`
(back to Idle).

**Items actually moved into inventory.** Corpse held items *before* any
loot action (server populated `Loot window item: slot 22` and `slot 29`
as soon as the loot request was acknowledged — not an empty corpse).
`LootItem ACK: lootee=169 looter=168 slot=22 auto_loot=65536` (not `-1`,
i.e. not denied — `65536` is the same raw value T33's own successful run
logged for this field, so not a T34-specific anomaly) followed by the
real chat confirmation `"--You have looted a kin.--"` — the same
pre-existing, out-of-scope `FormattedMessage` truncation bug T33 already
documented (`"Scaled Curskin"` → `"kin"`), which is itself further
evidence this hit the real guaranteed-drop item, not a placeholder.

**The game thread was not blocked**, and this is the property the whole
unit exists to prove:

| | timestamp | offset from loot start |
|---|---|---|
| `/loot 169` → `Combat state changed to: 3` (loot starts) | 16:31:22.441 | 0 ms |
| `/who` full roster answered (138 entries: 20 printed + "and 118 more") | 16:31:22.458 | **17 ms** |
| `/say liveness-probe-during-loot` echoed in chat | 16:31:22.509 | **68 ms** |
| `CloseLootWindow` (loot sequence closes) | 16:31:23.547 | 1106 ms |

Both liveness probes were sent immediately after `/loot` (no delay) and
answered within 68 ms — while the loot state machine was still mid-cycle
(it didn't close until 1106 ms later). **Compare to T23's documented
pre-T26 baseline: a ~1.1 s *blocking* stall per corpse** (T32's review,
citing T23: "T26 replaced four blocking `sleep_for` calls on the game
thread (~1.1 s ... blocked ~1.1 s and nothing was drained.") — the total
duration here (1106 ms, `Combat state changed to: 3` → `Combat state
changed to: 0`) is coincidentally almost the *same* wall-clock length as
T23's stall, which makes the comparison exact rather than approximate:
same real-world duration, but T23's was the game thread frozen solid for
that whole window with commands queuing up unanswered, and this run
answered two independent commands in 17 ms and 68 ms *inside* that same
window. That is the T26 property, verified live on the path T26 was
actually built for, for the first time.

Second `/who` after `CloseLootWindow` also answered normally; clean
`/camp` → `You have camped.` → `Application shutdown complete`, exit code
0.

### Tests

`tests/test_command_processor.cpp`, 7 new cases (`Loot_*`): valid id
dispatches `lootCorpse(<id>)` with the recording fake
(`action_test_fakes.h`'s pre-existing `lootCorpse` recording, unmodified);
missing/malformed/trailing-garbage/negative/out-of-range (uint16_t
overflow) ids all fail cleanly without reaching the dispatcher
(`handler.empty()`); `0` is accepted as a valid corpse id (it's a
legitimate spawn id in this codebase per T33's own corpse-matching
findings — the parser must not treat it as "missing"). All 7 pass; full
`test_command_processor` binary and the full suite both green.

### Verification bar

1. `cmake --build build -j24` — clean, no new warnings.
2. `ctest --test-dir build --output-on-failure` — **1813/1815**; the two
   failures are exactly the documented exceptions
   (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes` [SEGFAULT, known
   use-after-free]).
3. Golden replay: confirmed it **ran** (not skipped) — `WILLEQ_SESSION_LOG
   =/mnt/projects/fable/captures/session.sleq ctest -R SessionReplayGolden`
   → `Passed`; ran the test binary directly too and confirmed the
   `MatchesRecordedSessionOrCreatesBaseline` path (not the "baseline
   created" path), golden unchanged. Worktree's `data/captures` symlink
   was already present.
4. Unit coverage: see Tests above.
5. **Live acceptance.** LOCK FIRST protocol followed
   (`T34-shiftclick-loot 2026-08-03`, confirmed absent before, removed
   after clean `down.sh`, confirmed absent after).
   `scripts/eqemu/up.sh arena` + `seed.sh` (Testchar's zone_id=36 resolves
   to `befallen`, same "arena default zone name is misleading" detail T32
   documented). `benchmarks/audits/T34/run_verification_primary.sh`
   (adapted from `benchmarks/audits/T33/run_verification_primary.sh` per
   the plan's instruction to start from it), config
   `benchmarks/audits/T34/willeq_t34.json`, full transcript
   `transcript_primary.txt` (+`.driver.txt`) committed. Testchar restored
   to zone_id=36, x=-77.9232, y=21.9086, z=3, h=190 after the run (drifted
   a few thousandths during the fight despite distance=0.0 combat — same
   coordinate-axis-swap noise visible in the `POS C->S` lines, not real
   movement — restored exactly via direct DB write, then verified).
   Server brought down cleanly (`down.sh`), lock removed and confirmed
   absent.
6. `check_file_sizes.sh --files <absolute paths>` on both changed files:
   `command_processor.cpp` 1219 lines, `test_command_processor.cpp` 519
   lines — both `ok`, no new FAILs (`command_processor.cpp` was already
   comfortably under any warn/fail line before this unit's ~20-line net
   change).

### Deviations from the plan

- The plan's cited `eq.cpp:1021` branch has drifted a few lines from
  prior units' edits; verified the same logical branch (the
  `m_player_looting_corpse_id == 0` → `IsLooting()` → `ZoneProcessLootItem`
  chain) is still there by reading the current file rather than trusting
  the line number, and cited the DEBUG-log proof instead of the line
  number in this Review.
- No dedicated multi-corpse live run was added (T33 added one to nail
  down landmine 2's corpse-identity matching specifically; this unit's
  scope is the action-layer *entry point*, and the state machine and
  corpse-matching logic themselves are T33's already-verified, untouched
  code — a second live run would re-demonstrate T33's finding, not this
  unit's). One clean primary run satisfies every bullet the plan lists
  under "verify the path live."
- `run_verification_primary.sh`'s final shutdown `wait_for_log` timeout
  was bumped from T33's `15` to `40` seconds after this unit's own run
  showed `/camp` has a real 30s in-game logout timer
  (`"You have begun to camp. You will log out in 30 seconds."`) — T33's
  shorter timeout would have (and possibly did) fall through to the
  `/quit`+`SIGTERM` fallback every time. This was a script-only fix made
  after the committed run already completed cleanly (exit code 0); not
  re-run, since the existing transcript's evidence is unaffected by a
  driver-side polling-timeout value.
- No changes to `CombatManager`'s loot state machine, per the plan's
  explicit fence — confirmed by `git diff` touching only
  `command_processor.cpp`, `tests/test_command_processor.cpp`, and new
  files under `benchmarks/audits/T34/`.
