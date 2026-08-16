# T124 — Close the six remaining texture holders

**Slot: whichever is free. x86 only. eqemu lock required. No Pi.**

**Territory: the six holders' node ownership, `renderer_zone_merge.{h,cpp}`
(guard implementations), `irrlicht_renderer.cpp` (registration), `tests/`,
`refactor/`, `/mnt/projects/fable/review/T124/`.**

Read T123's Review first (the confirmed mechanism, the forensics, the holder
inventory), then T122's (the probe and the ring data).

## The job

T123 proved the mechanism and closed one holder. Six were found with the
identical gap shape and deliberately left:

**doors** (assessed the largest remaining contributor), **SpellVisualFX**,
**TumbleweedManager**, **RainOverlay**, **SnowOverlay**, **BoidsManager**.

Covered and safe, per T123: region meshes, entity meshes, sky. Four guards
already exist and work — `RegionMeshTextureGuard`, `EntityMeshTextureGuard`,
`SoftwareCursorTextureGuard`, and T123's `ObjectNodeTextureGuard`.

**Verify each of the six before writing a guard for it.** T123 flagged them by
shape, not by proof. If a holder turns out to be reachable from
`smgr_->getRootSceneNode()` at all times, `clearTextureReferences()` already
covers it and it needs nothing — adding a redundant guard is worse than
adding none. Say which of the six actually needed one.

## Do not write six copies of the same loop

The eviction-listener bodies are near-identical: walk a node's mesh-buffer
materials and its override materials, swap matching texture pointers for the
placeholder. **Six more copies of that loop is exactly how this codebase's
second dominant defect class — duplicated-then-drifted — starts**, and the
first class (a new sibling added under a comment warning about the previous
ones) is what produced T115.

**Extract the load-bearing loop once**, e.g. a free function
`swapTextureInNode(ISceneNode*, ITexture* from, ITexture* to) -> int`, and let
each guard be a trivial typed wrapper over its own collection. The risky logic
then lives in one place with one set of tests.

**Migrating the existing four onto the helper is desirable but optional.**
They are proven working code. Do it only if it is behaviour-preserving — note
they are *not* all identical today (`ObjectNodeTextureGuard` walks mesh-buffer
materials *and* override materials; check each before assuming). **If
migrating any of them would change behaviour, leave it alone and say so.**

## Preserve the constraints the existing guards depend on

Both are called out in the existing guards' comments and are load-bearing:

- **Resolve the placeholder ONCE at construction.** Never re-derive it inside
  `onTextureEvicted()`.
- **Never call back into `ConstrainedTextureCache` from inside
  `onTextureEvicted()`** — it runs under `mutex_`, and re-entering will
  deadlock.

Also check each holder's collection has a lifetime that outlives the guard,
the way `ObjectNodeTextureGuard`'s comment establishes for `objectNodes_`
(cleared between zone visits, not destroyed/recreated). **If a collection is
recreated per zone, holding a reference to it is itself a bug** — report that
rather than working around it.

## Acceptance — the ring count is primary, the crash rate is secondary

This is a change from earlier units in this arc, and it matters.

**Primary: the probe ring.** T122's probe is proven non-perturbing (10-of-10
gate) and gives a *direct count* of use-after-free writes — 81 across 10 cores
before any of this. **Run the probe build and report the post-destroy
TEXPARAMS count. Zero is the target, and any count is a precise measurement.**

**Secondary: the uninstrumented crash rate**, 10 runs, N of 10 clean against
the standing **1 of 10**. Report it, but do **not** lean on it: at n=10 the
rate cannot resolve small changes, and this arc has been misled by exactly
that three times now (T117, T119, T123). If the ring goes to zero and the
crash rate barely moves, **believe the ring and say the rate is
underpowered** — that would mean a remaining crash source is not this
use-after-free at all, which is itself a major finding.

**Fix and measure incrementally.** Do doors first, take a ring count, then add
the others. That yields a per-holder contribution ranking almost for free and
tells the next person which holders actually mattered.

## Fixing

**STOP and report** if closing a holder requires changing texture ownership or
refcounting, the zone load/unload lifecycle, or GL teardown order.

**Do not paper over**: no liveness guard inside `cacheTexParams()`, no
placeholder substitution that hides an uncovered holder rather than covering
it.

## Verification

- x86 suite (current total **2274**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile. Remember
  `docker/Dockerfile.arm-noble` keeps its **own** copy-list for
  `docker/irrlicht-drm/*.h`, separate from `scripts/build-gles2-x86.sh`.
- **Tests are required.** T123's `tests/test_object_node_texture_guard.cpp` is
  the shape to follow — it drives the **real** `ConstrainedTextureCache::clear()`
  destruction path against a real Irrlicht node, not a synthetic call. If you
  extract the shared helper, test it directly too.
- **Visual gate goldens must not move.** Swapping a stale texture for the
  placeholder *could* legitimately change output — if a golden moves, **STOP
  and report**; that needs a human eye, not a rebless.
- **No Pi run.** A Pi re-measurement is the natural follow-up unit.

## Review

(executor fills: which of the six actually needed a guard and which were
already covered, with evidence; the shared helper and whether the existing
four were migrated or deliberately not; the per-holder ring counts as they
were added; the **final post-destroy TEXPARAMS count**; the uninstrumented
**N of 10 clean**; the tests; deviations)

## Board

State the ring count plainly — that is the headline. If it reaches zero,
say explicitly that **the Pi is still unmeasured** and that a Pi
re-measurement against T117's 2-of-10 and T119's 0-of-5 is the next unit; this
arc has been burned before by inferring one platform from another. If UAF
writes remain, name the holders still producing them.
