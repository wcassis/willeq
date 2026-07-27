# P18 — Root-cause the animated-composite whole-silhouette rendering delta

Pre-authorized batch (Will, 2026-07-27). Slot: wt/slot2, branch
task/P18-animated-composite-delta. x86-ONLY (no Pi, no server).
Protocol: refactor/EXECUTOR_PROTOCOL.md. Parallel: P17 (slot1,
production zone/eviction files), P19 (slot3, Pi runs) — benchmark
CODE edits are yours if needed, keep tight.

## Motivation

P12b measured composite vs multi-buffer UNDER ANIMATION: pose/
silhouette identical but 17-29% of pixels differ at LOW magnitude
(mean ~0.1-2/255). P16's seam fix did NOT move it — inspection shows
the diff spans nearly the WHOLE model silhouette, not a seam. Static
renders are pixel-identical. So something in the merged-single-buffer
ANIMATED path shades slightly differently.

Suspects (check in order):
1. **Lighting/material state on the merged buffer**:
   buildCompositeMeshFromGeometry sets its own SMaterial (note
   Material.Lighting=false and EMT_SOLID) — diff against the
   multi-buffer path's per-part materials (lighting flags, material
   type, vertex-color usage).
2. **Normals**: are normals carried/recomputed identically through the
   merged path during applyAnimation (per-vertex normal handling in
   the instance-mesh skinning)?
3. Vertex color/alpha attribute divergence in the merge.

## Design

1. Reproduce with the P12b aligned method (canonical replay frames);
   confirm the magnitude/extent baseline.
2. Bisect by state: force-match one variable at a time (e.g. override
   the composite material's lighting flags to match multi-buffer's;
   compare normals numerically on a sampled vertex set between paths
   at the same anim frame).
3. Fix in the composite mesh-build path (production file —
   animation-adjacent; P17 is in zone/eviction files, no overlap);
   bar: animated composite vs multi-buffer diff drops to the FP class
   (scattered singles) or a quantified justified residual; static
   stays pixel-identical; standard bar.
4. STOP: unattributed after the suspect list + 2 focused bisections →
   narrowed evidence.

## Review
(executor fills in)
