# T47 — Clear the bridge pointers at teardown (all five, both modes)

**Slot: t1. x86 only. No Pi. No eqemu needed** — the evidence here is a
sanitizer/test construction, not a live session.
No sibling unit.

**Territory: `Application::shutdown()`, `EverQuest::ShutdownGraphics()`,
and tests.** Small unit; the value is the proof, not the diff.

Read `refactor/EXECUTOR_PROTOCOL.md`, then the deferred entry **"Bridge
pointers are never cleared at teardown — five dangling pointers"**, then
`refactor/T46_manager_bridge_wiring.md`'s Review (it left this one line
away deliberately).

## The defect, stated honestly

Five pointers reference the bridge: `EverQuest::m_bridge` and the four
managers' (`InventoryManager`, `SpellManager`, `SkillManager`,
`HotbarModel`).

- **Graphics builds**: `ShutdownGraphics()` clears `EverQuest::m_bridge`
  only — the four managers keep theirs.
- **True-headless (`EQT_GRAPHICS=OFF`)**: none of the five are cleared,
  because `Application::shutdown()`'s teardown block sits inside
  `#ifdef EQT_HAS_GRAPHICS`.

`Application::shutdown()` drops its own pointer and destroys the
decorator, so after shutdown those five point at freed memory.

**This is latent, not a live race — say so and do not oversell it.**
The orchestrator traced the ordering: `main.cpp` calls `run()` then
`shutdown()`; `run()` → `mainLoop()`, which **joins the game thread**
before returning; `Application::~Application()` calls `shutdown()` again
behind an idempotence guard. So nothing is publishing while the bridge is
destroyed today.

The reason to fix it anyway: it becomes real the moment anything
publishes during shutdown, or the ordering changes — and this codebase
has been bitten by exactly that class three times (P36's self-join
`EDEADLK`, the RDP `stop()` use-after-free that survived weeks as a
"flake", T30's unjoined stdin thread). Each looked survivable until it
wasn't.

## Scope

1. **Clear all five before the bridge is destroyed.** T46 left this one
   line away: `wireBridgeToSubsystems()` propagates null, so
   `m_eqClient->setBridge(nullptr)` clears `EverQuest::m_bridge` and all
   four managers at once. Place it in `Application::shutdown()` before
   `m_stateSyncBridge.reset()`, and make sure it runs in **both** modes —
   the current block being inside `#ifdef EQT_HAS_GRAPHICS` is half the
   bug.
2. **Reconcile with `ShutdownGraphics()`**, which already clears
   `EverQuest::m_bridge` (and only that). Do not leave two competing
   partial clear paths — either route it through the same helper or say
   why the split is correct.
3. **Check nothing publishes during shutdown.** If something does — a
   final save, a camp confirmation, a flush — clearing early would drop
   it. Look before you place the call, and if you find such a publisher,
   say so; the right order might be "let it publish, then clear".

## The evidence — this is the actual work

There is no live failure to point at, so the "before" has to be
constructed. Build a case that genuinely fails on `fable` and passes
after:

- Preferred: a test that publishes through a manager **after** shutdown,
  run under **ASAN**. Before the fix that is a use-after-free ASAN will
  name; after it, a clean no-op. That is unambiguous and cheap to re-run.
- Acceptable: a deterministic non-sanitizer test asserting the pointers
  are null after shutdown, **plus** an explicit statement that it pins
  the invariant rather than demonstrating the hazard.

**Do not claim a fixed use-after-free without showing one.** An
assertion that pointers are null is a weaker claim than "this was a UAF
and now is not" — make whichever claim your evidence actually supports.
This project has had two rounds of evidence that proved less than it
appeared to ("a kin", the P44 bless pairs); the standard is that the
claim matches the artifact.

## STOP conditions

- Something legitimately publishes during shutdown and clearing early
  would drop it → STOP and report; the ordering question is worth more
  than the fix.
- Clearing in both modes requires restructuring `shutdown()`'s
  `#ifdef` layout beyond moving one call → STOP and report.
- ASAN surfaces *other* teardown use-after-frees → report them
  separately; do not expand this unit to chase them.
- Any behaviour change beyond teardown pointer hygiene → STOP.

## Verification bar

1. Both configures build clean.
2. `ctest` in both trees — known exception:
   `AudioManagerTest.PreloadCommonSounds` (ALSA). RDP is unwaived (T35),
   the golden replay does not skip (T36); T43/T44/T45/T46's pinned tests
   must stay green.
3. Golden replay unchanged and confirmed to have RUN, both trees.
4. The evidence from the section above, with the claim matched to it.
5. `visual_gate` PASS with goldens untouched — graphics teardown is being
   touched, so prove the rendering path is unaffected.
6. `bash <slot>/scripts/refactor/check_file_sizes.sh --files <abs paths>`
   — the bare `... fable` form prints "nothing was checked, this is not a
   pass" (T37); that line is a failed verification.

## Review

(Executor fills: where the clear was placed and why there, how
`ShutdownGraphics()` was reconciled, whether anything publishes during
shutdown, the evidence and precisely what claim it supports, any other
teardown problems ASAN surfaced, deviations.)
