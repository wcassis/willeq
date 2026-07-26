# P12b — Find the benchmark's animation node-wiring delta, fix the crumple, restore animated evidence

Approved by Will 2026-07-27 ("go ahead with both in parallel").
Slot: wt/slot2, branch task/P12b-node-wiring, cut from fable (e3ba677).
Runs in parallel with P10b (slot1, production merge crash — disjoint
files). Pi allowed ONLY for the optional final phase.

## Motivation — the search space just collapsed

Will's gates-video verdict (2026-07-27): live production NPCs animate
CORRECTLY (multibuffer AND composite runs). So the P12 crumple is
benchmark-context-only after all — despite P12's finding that the
driving idiom (OnAnimate/forceAnimationUpdate) is identical. Production
animates through per-instance state: each EQAnimatedMeshSceneNode owns
its own SkeletalAnimator AND its own instanceMesh_ (per-instance
skinned vertex buffer, createInstanceMesh()/applyAnimation() —
"Per-instance animation state (NOT shared between entities)",
animated_mesh_scene_node.h). The delta is therefore almost certainly in
NODE CONSTRUCTION/WIRING: how the benchmark creates its animated nodes
vs production's createAnimatedNode() path (instanceMesh creation,
material/buffer hookup, skeleton/clip setup). P12's crumple-at-frame-0
finding fits a construction-time difference exactly.

## Design

1. **Diff the construction paths line-by-line**: production
   RaceModelLoader::createAnimatedNode / createAnimatedNodeWithAppearance
   (animated_mesh_creation.cpp:237+) vs the benchmark's animated-node
   creation (gles2_zone_benchmark.cpp ~1704-1724 + zone_bench_loader).
   Enumerate every divergence: constructor arguments, createInstanceMesh
   invocation, mesh source (shared base vs instance), material list
   handling, skeleton/clip initialization, scene-manager registration.
2. Convert the diff into the fix: make the benchmark construct/drive
   nodes the production-faithful way (prefer calling the SAME production
   entry points rather than replicating them). Minimize
   gles2_zone_benchmark.cpp churn where possible (P10b is in slot1;
   file overlap is acceptable here since P10b targets production TUs —
   but keep changes tight anyway).
3. **Validate per the P08d lesson** (garbled-evidence verification is
   void):
   - animated renders show COHERENT walk/idle cycles across 4-6 races ×
     several phases (human-judgeable captures, compare against the
     gates videos' look);
   - static renders byte-identical before/after (the fix must not
     perturb the static path);
   - full suite + gate untouched.
4. **The deferred Z35 verification**: composite vs multi-buffer UNDER
   ANIMATION, aligned frames, quantified — contact sheet →
   benchmarks/contact_sheets/p12b_animated/. (Expected: identical or
   near; any structural diff = report.)
5. Re-evaluate the parked VertexMapping patch
   (refactor/scratch/P08d_vertexmapping_one_to_many.patch) against NOW-
   VALID animated renders: does it change anything visible? Land or
   discard it with evidence; close its loop.
6. **Un-skip/repair ModelAnimatesProperly**
   (tests/test_inventory_model_view.cpp) — the P12 finding that the one
   production-adjacent animation test is unconditionally skipped. Make
   it run meaningfully in CI if feasible (it may need DISPLAY/server —
   if genuinely un-runnable in CI, replace the blanket skip with a
   documented conditional skip + a runnable variant of the assertion at
   whatever level IS testable headlessly).
7. **OPTIONAL final phase (Pi, only if everything above is green and
   budget remains)**: the TRUE composite perf magnitude — canonical
   befallen replay, ANIMATED entities at calibrated load, composite on
   vs off (and +merge if trivially cheap), P02 protocol, <=8
   invocations. This replaces P08c's unreliable number and P11's
   wrong-route ~0% with the real one. If skipped, say so — it becomes
   P11b.

## Verification

- Standard bar: build, ctest (known host exceptions; note if
  ModelAnimatesProperly now runs), gles2-x86 build, visual gate PASS
  goldens untouched, ARM build, file-size check.
- Static byte-identity; the animated evidence set; replay discipline if
  the Pi phase runs.

## Out of scope

- Production animation code changes EXCEPT what step 5/6 justify with
  valid evidence (the crumple itself is benchmark-side by Will's
  verdict; if the diff-hunt unexpectedly implicates production code
  anyway, STOP and report before changing it).
- Impostors; the merge crash (P10b's); foliage alpha; pop-in.

## STOP

- Construction diff yields no candidate delta after a thorough
  enumeration → stop with the enumerated diff table (do not thrash).

## Review

(executor fills in)
