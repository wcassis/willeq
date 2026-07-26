# P12 — Root-cause the benchmark's animated-path geometry garbling

Approved by Will 2026-07-26 ("go ahead with both in parallel").
Slot: wt/slot2, branch task/P12-animate-root-cause, cut from fable
(2c69c65). x86-ONLY unit (no Pi — P11 owns it). Runs in parallel with
P11 (slot1, benchmark measurement) — MINIMIZE benchmark-tool file edits
(gles2_zone_benchmark.cpp is P11-adjacent; prefer diagnostics in the
animation TUs or standalone drivers; if benchmark edits are unavoidable
keep them additive and isolated for merge-time reconciliation).

## Motivation

Discovered during P08d evidence rounds (Will's eye): the benchmark's
--animate scenegraph path renders CRUMPLED/collapsed model geometry
(bodies smashed, head-blobs remaining) and always has — pre-P08d
binaries reproduce it identically; static renders are perfect. Until
fixed: NO animated benchmark evidence is trustworthy, composite/merge
animation verification queues behind Will's live-play availability, and
P08b's --animate "nonrepresentative" flag was an earlier symptom.
Production live play animates correctly — the delta is in how the
harness drives animation, or a production edge case live play masks.

**Prime suspect (check FIRST)**: the parked VertexMapping one-to-many
patch (refactor/scratch/P08d_vertexmapping_one_to_many.patch, reverted
from P08d because it could only be verified against garbled renders).
Its own description — "VertexMapping recorded only one destination per
original vertex, silently freezing UV-seam duplicate vertices at bind
pose" — predicts EXACTLY the observed symptom: some vertices animate,
seam-duplicates stay at bind pose → smashed-together geometry.

## Design

1. **Reproduce**: qeynos2, --entities 6 --seed 42 --animate, frames
   15/25 (orchestrator repro crops in the P08d review; commands there).
   Static control (no --animate) renders perfectly — keep as the
   sanity anchor.
2. **Test the prime suspect**: apply the parked patch, re-render. If
   the crumple resolves → root cause found; proceed to proper
   validation (step 4). If partial/no effect → bisect further:
   - how the benchmark drives EQAnimatedMeshSceneNode (OnAnimate with
     synthetic simTimeMs outside the normal scene-manager pipeline —
     compare against production's driving in renderer/entity_renderer);
   - the P05-era animation MERGE path (bone mapping when animations are
     merged across race skeletons — partial mappings → collapsed
     vertices; check whether affected races use merged animations);
   - per-buffer vertex-mapping population differences (multi-buffer
     buildMeshFromGeometry path).
3. **Which side owns the bug**: benchmark-harness-only, or shared
   production code (mesh_building/animated_mesh_creation/
   EQAnimatedMesh)? If shared: does production's normal driving mask it
   (e.g. per-frame re-skinning through a different entry point), or is
   it a REAL production defect visible in some live circumstance
   (document what circumstance — this matters to Will)?
4. **Validate the fix properly** (the P08d lesson — garbled-evidence
   verification is void):
   - animated renders show COHERENT poses (walk cycles readable as
     walking) across 4-6 races × several phases — human-judgeable
     captures;
   - static renders byte-identical before/after the fix (the fix must
     not perturb the valid path);
   - full suite + visual gate untouched;
   - THEN the deferred P08d deliverable: composite vs multi-buffer
     UNDER ANIMATION, aligned frames, quantified diffs (expected:
     pixel-identical or near, now that both geometry paths animate) —
     the Z35-class verification that P08d could not provide. Contact
     sheet → benchmarks/contact_sheets/p12_animate/.
5. If the parked patch is the fix (in shared code): land it WITH the
   proper validation above; annotate the P08d review pointer. If the
   root cause is elsewhere, land what's right and dispose of the parked
   patch explicitly (apply/discard with reasons).

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- Static byte-identity before/after any shared-code fix (full-frame
  compare at the P08d evidence poses).
- The animated evidence set per design 4.
- No Pi. No --animate evidence claimed without the fix validated.

## Out of scope

- Impostors/LOD (P08e); perf measurement (P11); production preset
  changes; entity gameplay-clip selection redesign (the benchmark may
  legitimately pick a fixed clip — coherence is the bar, not
  clip-choice fidelity).

## STOP

- Root cause resists attribution after the suspect list + 2-3 focused
  bisections → stop with narrowed evidence (minimal failing
  race/vertex-class + what was excluded).

## Review

(executor fills in)
