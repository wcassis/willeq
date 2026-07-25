# P09c — Production driver fix, remaining merge contributor, fair FP baseline, and the Pi measurement

Approved by Will 2026-07-25 ("go ahead with P09c").
Slot: wt/slot1, branch task/P09c-driver-fix-measure, cut from fable
(0dfcc63). Three fixes then the twice-deferred measurement. ONE
production change (the driver bug); everything else benchmark-side.

## Motivation

P09b root-caused the merge corruption to a PRE-EXISTING production
driver bug (COpenGLES2Driver::getOpaqueAlphaFallback() lazy-init binds
its 1x1 white fallback on the currently-active texture unit — unit 0 —
clobbering the diffuse texture for the process's first no-companion
draw) and worked around it benchmark-side. Remaining before the merge
can be measured: the driver deserves the real fix; qeynos2/befallen
goldens still show an unattributed contributor (max_delta up to 209;
suspects: wind-shader per-buffer uMeshYBounds packing, atlas
unit-1/AtlasSolid3D paths); and the ±1-3px alpha-edge FP class needs a
fair comparison baseline (baked-vs-baked) instead of an impossible bar.

## Design

### A. Fix the driver bug IN PRODUCTION (the one production change)

1. `getOpaqueAlphaFallback()`: select the correct texture unit
   (glActiveTexture(GL_TEXTURE1)) BEFORE the lazy-init glGenTextures/
   glBindTexture sequence (or save/restore the active unit) so the
   fallback never binds onto unit 0. Minimal diff; comment cites P09b.
   Find ALL copies/install paths of the driver source — docker/
   irrlicht-drm/ AND whatever the x86 build (docker/irrlicht-drm-x86 /
   scripts/build-gles2-x86.sh install step) actually compiles; fix the
   canonical source so both ARM and x86 builds get it. Verify by
   rebuilding both and re-running the P09b repro WITHOUT the benchmark
   workaround (see A3).
2. **Golden impact check**: the glitch only ever hit the process's first
   no-companion draw; the gate's warmup frames likely absorb it, so
   goldens should be byte-identical after the fix. Run the gate BEFORE
   and AFTER. If any golden diffs appear, that IS the driver fix
   becoming visible — capture the diff evidence and STOP for Will's
   re-bless (do not self-bless, do not revert the fix).
3. Remove P09b's benchmark first-draw-unmerged workaround (keep the
   SMaterial::operator== run grouping — that one is correct design, not
   a workaround) and confirm the gfaydark golden4 repro stays clean
   under the fixed driver.

### B. Attribute and fix the remaining contributor(s) (benchmark-side)

4. Same bisection methodology as P09b (run isolation → minimal failing
   pair), on the worst remaining poses (befallen golden6/7, qeynos2
   golden0). Check the named suspects FIRST:
   - wind shader per-buffer uMeshYBounds packing (a merged run spanning
     buffers with different Y bounds displaces vertices differently than
     unmerged — if so, split runs on wind-material + bounds, or exclude
     wind materials from merging entirely and count the cost: P08
     histograms say how many draws are wind-material);
   - atlas paths (AtlasSolid3D/AtlasAlpha3D texcoord1 machinery, unit-1
     companions) — note the gate config is RGBA/non-EQE1; identify what
     atlas-like state, if any, is active at the failing poses before
     theorizing.
5. Fix in the merge TUs (split/key runs on whatever is found; excluding
   a material class from merging is an acceptable fix if the draw-count
   cost is small — quantify it).
6. STOP: unattributed after 2-3 focused rounds → stop, report the
   narrowed evidence.

### C. Fair FP baseline + the identity verdict

7. Add an unmerged-but-baked comparison mode (render the SAME baked
   world-space vertex data as individual per-region draws with identity
   world transform — cheap: reuse the merge buffers, submit runs split
   back into per-region ranges). Bars:
   - **merged vs baked-unmerged: max_delta=0 at ALL 15 poses** (isolates
     merge-only effects; any diff = a real remaining bug, loop to B).
   - **baked-unmerged vs production-path unmerged**: this is the pure FP
     transform-order delta — quantify (diff px, max_delta, spatial
     character) and produce a small contact sheet →
     benchmarks/contact_sheets/p09c_fp_delta/ for Will's one-time
     judgment (production landing would bake positions, so this delta is
     what shipping the merge design ultimately shows). Do NOT
     self-declare acceptability.

### D. Pi measurement (P09 plan C8-C10, at last)

8. P02 protocol, canonical replays, production config: merged vs
   unmerged — befallen zone-only, befallen combined (--entities 205),
   qeynos2 zone-only if budget allows. Whole-run avg/p95 + submission
   columns + pinned-pose extraction; unmerged geometry columns
   byte-identical to P08/P08b baselines; per-pose draw reduction vs the
   97.8% ceiling. KPI projection incl. stacking with P08c composite
   (flagged as projection). <=12 invocations/session, 2 sessions OK.
9. Update the production landing sketch (driver fix now landed;
   run-grouping learnings; wind/atlas handling; bake-delta bless status).

## Verification

- Standard bar: `cmake --build build -j24`; ctest (known host
  exceptions); scripts/build-gles2-x86.sh; visual gate per A2 (expect
  PASS with goldens untouched; any diff = STOP + evidence for Will);
  scripts/build-arm-noble.sh (driver change compiles for ARM);
  scripts/refactor/check_file_sizes.sh fable (no new FAILs).
- The A3 repro check (workaround removed, fixed driver, clean render).
- Replay discipline per Q01; Pi rules per P02; CSVs →
  benchmarks/pi_runs/p09c_measure/.
- Review must contain: the driver diff + all copies fixed + gate
  before/after verdict; the contributor root cause + fix (or narrowed
  STOP evidence); the merged-vs-baked max_delta=0 table; the FP-delta
  quantification + contact sheet path; Pi tables; KPI projection;
  landing-sketch update.

## Out of scope

- Any OTHER production change beyond the driver lazy-init fix.
- Entity/object paths; occlusion; lighting; goldens (except reporting
  A2 evidence if the driver fix surfaces); paths; second architectures.

## Review

(executor fills in)
