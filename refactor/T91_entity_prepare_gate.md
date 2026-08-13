# T91 — Every zone entity's mesh is built at load, with no distance gate

**Slot: slot2. Headless. NO EQEMU — slot1 owns it this round. Do not bring
the server up.**

**Territory: the zone-load entity-preparation path in
`src/client/graphics/`, `tests/`, `refactor/`,
`/mnt/projects/fable/review/T91/`.**

Read T82's Review (the Nektulos load failure), T83's Review, then this plan.

## The defect

`loadZoneSequential` logs `Sequential: preparing 452 entities inline
(chrCacheMaxEntries=8)` and then builds a mesh for **every entity in the
zone**, inline and blocking, at load time. The 40-entity cap
(`Entities 40/171 (max 40)`) is applied at **render** time — after all the
expensive work is already done.

Measured consequence: Nektulos took **~230 seconds and never finished** (still
87% when killed), thrashing an 8-entry character cache against `global*_chr.s3d`.
That destroyed T82's Part 1 capture set outright.

Receiving 452 spawns is correct — EQ's `ZoneSpawns` sends the whole zone.
Building 452 meshes to draw at most 40 is not.

## Why this one matters beyond itself

It gates iteration speed generally. Any future probe/harness mode that skips
login and world but still calls the zone-load path inherits the same 230
seconds, so this is effectively a prerequisite for faster live testing rather
than an isolated performance bug.

## Part 1 — Characterize before changing anything

- Find the unconditional prepare-all loop and name it precisely (file,
  function, line).
- Is there **any** distance, visibility or count gate at load time? State what
  exists rather than assuming there is none.
- What is `chrCacheMaxEntries=8` doing against 452 entities? Quantify the
  thrash: cache hits vs misses, and the cost distribution — is time going to
  S3D reads, mesh construction, texture upload, or cache eviction?
- **Measure a baseline** on at least two zones of different populations
  (Befallen ~171, Nektulos ~452) so any improvement is measured, not asserted.

## Part 2 — The design question, which is Will's to settle

Do **not** pick an approach unilaterally. Characterize the options and their
trade-offs, then **report**:

- **Distance-gated build at load** — build only what is within some radius,
  build the rest on demand. Risk: pop-in, and a stall when the player turns.
- **Lazy/on-demand build** — build nothing at load, build on first visibility.
  Risk: stutter during play rather than at load, which may be worse on the Pi.
- **Raise the cache to match the working set** — cheapest change, but does not
  address building 452 meshes to draw 40.
- **Background/incremental build** — keep building after the zone is playable.
  Risk: threading, and the GL context is owned by the loading thread.

For each, state the effect on the **Orange Pi** (Mali400, 512MB) specifically,
not just the x86 dev VM. A change that helps the VM and hurts the Pi is not an
improvement.

## Part 3 — Only implement if one option is unambiguously safe and small

If the characterization shows an obviously-correct narrow change — most likely
sizing the character cache to the actual working set — implement that and
measure the result.

**STOP and report** if the fix requires restructuring the zone-load pipeline,
touching the GL context handoff, or changing the loading-thread model. That is
its own unit with its own review. Do not begin a redesign inside a
characterization unit.

## What you may not do

- **Do not start the eqemu server.** slot1 owns it this round.
- Do not change rendering behaviour to make loading faster — pop-in traded for
  load time is a product decision, not an optimization.
- Do not assert an improvement without a before/after measurement on both
  zones.

## Verification

Full build and suite (expect 2070/2071; sole failure the documented ALSA
`AudioManagerTest.PreloadCommonSounds`). Visual gate — **if goldens change,
that is a real signal here**, since this path feeds what gets drawn: say so
explicitly and explain why.

Measurements and findings to `/mnt/projects/fable/review/T91/` with a README.
If you produce images, open them and describe what is actually visible.

## Review

(executor fills: the exact location of the prepare-all loop; what gating
exists today; the cache-thrash quantification; baseline timings for both
zones; the options with Pi-specific trade-offs; what was implemented if
anything and the measured before/after; deviations)
