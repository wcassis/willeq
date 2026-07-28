# P23 — ParticleManager race real fix + llvmpipe stability items

**Slot: slot3. x86 ONLY — no Pi, no eqemu.** Siblings: P21 (slot1,
doors/entities + eqemu), P22 (slot2, GLES2 driver) — territories
disjoint; respect them. You have NO live-session harness this unit —
verify with tests/stress, not eqemu.

Two long-parked stability items from the video-capture era
(deferred.md, "x86 llvmpipe client stability needs a pass"):

## Item 1 — ParticleManager::renderUnified race (the real fix)

Known state (deferred.md): nondeterministic crash in
`ParticleManager::renderUnified` — dangling-pointer-shaped race against
the SimulationWorker result buffer; currently gated on `isEnabled()` as
a WORKAROUND (properly applied, P09c era); the real fix is owed.
History pointer: compare the RDP frame race fixed earlier in this
file's history for the established synchronization idiom.

1. Root-cause with evidence, not inspection alone: reproduce under
   stress (a headless/unit harness spinning the simulation worker +
   render loop; ThreadSanitizer build if practical — a local
   `-fsanitize=thread` target is fine, do not commit build-system
   changes unless they're cleanly optional).
2. Fix with the proper synchronization (match the file's existing
   worker/result-buffer patterns; no broad locks around render).
3. Prove it: the stress reproduction runs clean for a bounded soak
   (state iterations/duration); wire the stress test into ctest if it
   is deterministic-fast, otherwise commit it as a manual tool under
   `tools/` and document the invocation.
4. Remove or keep the isEnabled() gate deliberately: if the real fix
   makes the workaround redundant, remove it and say so; if it still
   guards an independent hazard, keep it and document why.

## Item 2 — screenshot-readback guard widening

Known state (P20 Review, deviation 2a; also the unmerged capture
branch): `saveScreenshot()`'s safe-readback path is compiled behind
`EQT_HAS_DRM` only; the x86 GLES2 dev build compiles it OUT and falls
through to `driver_->createScreenShot()`, which segfaults
nondeterministically under llvmpipe. Two separate agents have now
applied the same local widening (`EQT_HAS_DRM || EQT_HAS_GLES2`) as an
uncommitted workaround — land it properly this time: widen the guard,
verify the safe path actually works on the x86 GLES2 build (a
`/screenshot` smoke via the benchmark or a headless run — NO eqemu),
and confirm the ARM build still takes the same path it always did.

## Rules

- Territory: particle files (`src/client/graphics/particle*`,
  simulation worker), the `saveScreenshot()` site + its guard, tests/
  tools you add. Do NOT touch the GLES2 driver internals (P22),
  doors/entities (P21), zone loaders, or goldens.
- Goldens untouched (visual gate PASS) — particles are excluded from
  golden frames but run the gate anyway; STOP if it moves.
- The post-camp shutdown-thread aborts (documented class) are OUT OF
  SCOPE unless your particle fix directly resolves one — do not start
  a general shutdown refactor.

## Verification

- Full 6-step bar (known ctest exceptions per protocol).
- Item 1: stress evidence before (crash/TSAN report) and after (clean
  soak), synchronization mechanism explained in the Review.
- Item 2: smoke evidence of the safe readback on x86 GLES2 + ARM build
  unchanged-path confirmation.

## Review

(executor fills in)
