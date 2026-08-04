# T45 — Make `GameTick()` run headless (cooldowns tick, buffs expire)

**Slot: t1. x86 only. No Pi. eqemu for the acceptance leg.**
No sibling unit.

**Territory: `Application::updateGameState()`'s gate,
`EverQuest::GameTick()`'s own gate, and tests.** Keep it that narrow —
the fix is two conditions; the work is proving it is safe.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry
**"`GameTick()` never runs headless — spell cooldowns don't tick, buffs
never expire"**, then `refactor/T44_spell_state_via_bridge.md`'s Review
(which found it).

## The defect

`GameTick()` is gated on graphics **twice**:

1. `Application::updateGameState()` — `if (m_eqClient && m_graphicsInitialized) GameTick(...)`
2. inside `GameTick()` — `#ifdef EQT_HAS_GRAPHICS { if (!m_graphics_initialized) return; }`

Its entire body is three calls:

```cpp
m_spell_manager->update(deltaTime);   // cooldowns, memorization, cast timeouts
m_buff_manager->update(deltaTime);    // buff durations, expirations
ProcessBridgeIntents();
```

None of that is renderer work. So headless today: **cooldowns never tick,
memorization never progresses, cast timeouts never fire, buffs never
expire, and renderer intents are never drained.**

## The subtlety that decides the fix — read before changing anything

**`m_graphics_initialized` is not a proxy for "headless".** It is also
false *in graphics mode* during startup and zone loading, until graphics
init completes.

So the naive fix — delete both gates — does not merely enable headless;
it makes `GameTick()` newly run **during the graphics-mode loading
window**, which is a behaviour change in the mode that currently works.
The gate may well have been protecting exactly that window.

The fix is therefore **not** "remove the gate" but "make the condition
mean what it should": run the tick when the *game* is ready, regardless
of whether graphics are. Establish what the right readiness signal is —
`OnGameStateComplete()`/zone-in state-ready is the same lifecycle T41
used for the snapshot, which is a reasonable precedent — and say why you
chose it.

**Do not change graphics-mode behaviour.** If your condition would let
`GameTick()` run during graphics-mode loading where it previously did
not, that is a STOP unless you can show it is safe, in which case say
how.

## What turning it on actually enables — safety analysis already done

The orchestrator traced this; verify rather than re-derive:

- **`ProcessBridgeIntents()` is the only caller of
  `drainIntents()`** (`eq_bridge.cpp:729`). So intents pushed in headless
  today are **never drained — the queue grows unboundedly.**
- **Nearly every intent pusher is renderer-side** (`renderer_frame.cpp`,
  `renderer_overlays.cpp`, `renderer_player_physics.cpp`,
  `static_ui_input.cpp`) and therefore absent headless.
- **The exception is `inventory_manager.cpp`**, which pushes
  `MoveItemIntent`/`EquipmentChangedIntent` on item pickup — and
  `InventoryManager` *does* run headless since T41/T42 un-gated its
  construction and packet path.

So un-gating also closes a latent unbounded-growth hazard. Confirm the
queue actually behaves as described, and check whether anything headless
currently reaches those pickup paths.

Also confirm no double-processing: T26 deliberately wired
`UpdateLootState()` alongside `UpdateMovement()` *because* `GameTick()`
was graphics-gated. It is not in `GameTick()`, so this should be fine —
verify rather than assume, since T26's reasoning was explicitly built on
the gate you are removing.

## Scope

1. Fix both gates so the tick runs on game-readiness, not graphics.
2. Verify the three enabled calls are safe headless — each null-checks
   its manager already, but check `ProcessBridgeIntents()`'s body for
   anything that assumes a renderer.
3. Tests: cooldown ticking and buff expiry in a headless build. T44's
   countdown model computes from a start time on read, so a *cooldown*
   may appear to work without the tick — assert something that genuinely
   requires `update()` to have run (memorization progress, cast timeout,
   or buff expiry).

## STOP conditions

- Your condition changes graphics-mode behaviour (including during the
  loading window) → STOP unless you can demonstrate safety.
- `ProcessBridgeIntents()` turns out to touch renderer state → STOP and
  report; that would mean the gate was load-bearing.
- Enabling the tick causes duplicate processing anywhere (loot, movement,
  intents) → STOP.
- Any behaviour change beyond "these timers now run headless" → STOP.

## Verification bar

1. Both configures build clean.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35);
   the golden replay does not skip (T36); T43's `FullStateSnapshot` and
   T44's spell tests must stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. **Graphics mode provably unaffected** — `visual_gate` PASS with
   goldens untouched, as T43 used it. This matters more than usual: the
   risk here is specifically to the mode that already works.
5. **Live headless acceptance with a real before/after**: show a timer
   that does not advance on `fable` and does advance after the fix —
   a buff expiring or memorization completing is the clearest. This is
   behaviour that has never run; asserting it works is the deliverable.
   eqemu LOCK FIRST; `up.sh` only; restore Testchar to zone_id=36,
   x=-77.9232, y=21.9086, z=3, h=190; `down.sh` after. Testcaster
   (Wizard) exists and T44 used it for spell work. Evidence under
   `benchmarks/audits/T45/`.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: the readiness condition chosen and why, proof graphics
mode is unaffected including the loading window, the intent-queue
finding, the no-double-processing check, the before/after showing a timer
that previously never advanced, deviations.)
