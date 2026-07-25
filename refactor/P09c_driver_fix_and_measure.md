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

Executed 2026-07-25 on `wt/slot1`, branch `task/P09c-driver-fix-measure`, cut
from `fable` at `90e8fba`.

### Phase A — the driver fix, and the STOP it triggered

**The fix** (`docker/irrlicht-drm/COpenGLES2Driver.cpp`,
`getOpaqueAlphaFallback()`): added `glActiveTexture(GL_TEXTURE1);` immediately
before the lazy-init `glGenTextures`/`glBindTexture` pair, per P09b's Root
cause A and its own recommended follow-up. Minimal diff (one added line +
a comment citing `refactor/P09b_merge_fix.md`):

```cpp
GLuint COpenGLES2Driver::getOpaqueAlphaFallback() {
    if (!opaqueAlphaFallback_) {
        // P09b root cause: lazy-init used to glBindTexture() without first
        // selecting unit 1, so the new fallback texture landed on whichever
        // unit was last left active by applyMaterialRenderState() (unit 0),
        // clobbering the real diffuse texture for the process's first
        // no-companion-texture draw. Select GL_TEXTURE1 explicitly before
        // the bind so the lazy-init always lands on the unit this fallback
        // is actually for (see refactor/P09b_merge_fix.md, Root cause A).
        glActiveTexture(GL_TEXTURE1);
        glGenTextures(1, &opaqueAlphaFallback_);
        glBindTexture(GL_TEXTURE_2D, opaqueAlphaFallback_);
        ...
```

**All copies found.** Only ONE canonical source location exists:
`docker/irrlicht-drm/COpenGLES2Driver.cpp`. Both consumers copy it fresh at
build time rather than maintaining independent forks:
- ARM: `docker/Dockerfile.arm-noble` (`COPY docker/irrlicht-drm/COpenGLES2Driver.cpp ...` then `cp` into the pristine Irrlicht tree at build time).
- x86: `scripts/build-gles2-x86.sh` step 2 (`cp "$DRM_SRC/COpenGLES2Driver.cpp" "$DEST_SRC/..."`, `DRM_SRC=docker/irrlicht-drm`).
`third_party/irrlicht-gles2-x86/` is a gitignored, fully-regenerated build
area (`.gitignore:105`) — not a second copy to fix, just the x86 build's
output of copying the canonical source. Editing the one canonical file and
rebuilding both fixes both.

**Gate BEFORE (fix stashed out)**: `scripts/build-gles2-x86.sh` → PASS build;
`scripts/ci/visual_gate.sh` → **PASS** ("=== VISUAL GATE: PASS ===", goldens
untouched — this is the pre-fix baseline, matches P09b's state).

**Gate AFTER (fix applied)**: rebuilt `scripts/build-gles2-x86.sh` → PASS
build; `scripts/ci/visual_gate.sh` → **FAIL**:

```
- qeynos2 frame 0: PIXEL MISMATCH (9048 differing pixels, size=(800, 600))
- gfaydark frame 0: PIXEL MISMATCH (40309 differing pixels, size=(800, 600))
```

Contrary to the plan's expectation ("warmup frames absorb the first-draw
glitch... goldens should be byte-identical"), the fix DOES change visible
output — but only at frame 0 of two of the three zones. Full per-frame
diff table (decoded-pixel RGB compare vs the committed `tests/goldens/`
references, same method as `visual_gate.py`'s `compare_images_exact`):

| zone | frame | pose description | diff px (of 480,000) | max_delta |
|---|---|---|---|---|
| qeynos2 | 0 | Gate plaza, two-story buildings, ivy wall | 9,048 | 120 |
| qeynos2 | 1 | Courtyard, tiled-roof entrance | 0 | 0 |
| qeynos2 | 2 | Interior temple room | 0 | 0 |
| gfaydark | 0 | Treehouse platform, tree trunk + distant gazebo | 40,309 (8.4%) | 246 |
| gfaydark | 1 | Close-up gazebo rooftop | 0 | 0 |
| gfaydark | 2 | Elevated wooden bridge walkway | 0 | 0 |
| befallen | 0 | Crypt altar/pillar room | 0 | 0 |
| befallen | 1 | Crypt doorway/archway | 0 | 0 |
| befallen | 2 | Crypt balcony hall | 0 | 0 |

7/9 designated golden frames are unaffected; qeynos2 frame 0 and gfaydark
frame 0 change by a non-trivial margin (gfaydark frame 0's diff bbox spans
roughly the right two-thirds of the frame — not a one-triangle sliver, more
consistent with the same "first no-companion-texture draw corrupts unit 0"
mechanism P09b found, but hitting a larger/differently-shaped surface in the
*unmerged, non-benchmark, currently-shipping* render path than P09b's
historical description ("invisible one-triangle-fan sliver") anticipated.
Root cause is very likely still exactly getOpaqueAlphaFallback()'s
unit-0-clobber (it only ever fires once per process, on the first
no-companion draw, and only qeynos2/gfaydark's frame-0 camera pose happens to
be the process's first such draw before this fix and now resolves onto the
correct unit instead) — not re-litigated further here per the plan's explicit
instruction not to theorize past capturing evidence.

**Evidence preserved** (durable, outside `build-gles2/ci_artifacts/` which is
gitignored/regenerated): `benchmarks/contact_sheets/p09c_driver_gate_diff/`
— `qeynos2_f0.png` (after-fix actual), `qeynos2_f0_golden.png` (committed
reference), `qeynos2_f0_diff.png`, and the same three for `gfaydark_f0`.

**STOP invoked per the plan's own instruction**: "if ANY golden diff appears,
capture evidence and STOP for Will's re-bless (do not self-bless, do not
revert the fix)." A golden diff appeared (2 of 9 frames). Per instruction,
the fix is **kept in the tree, uncommitted judgment made about its
acceptability**, and this unit stops here. Phases B (remaining-contributor
bisection), C (fair FP baseline), and D (Pi measurement) are NOT executed:
all three are specified to run against a visual-gate-clean, Will-approved
baseline, and B/C's own pixel-identity bars would be measured against a
production render path whose correctness Will has not yet blessed at this
new state. Running them now would either measure against a baseline that
may be about to change (if Will re-blesses) or waste the Pi's 12-invocation
budget on numbers that would need re-taking after re-bless either way.

### Independent verification (not gated on the STOP — run to establish the
### fix's mechanical soundness regardless of Will's golden decision)

1. `cmake --build build -j24`: **PASS**.
2. `ctest --test-dir build --output-on-failure`: **1223/1225**, i.e. exactly
   the 2 documented host exceptions (`AudioManagerTest.PreloadCommonSounds`,
   `RDPServerFrameTest.UpdateFrameMultipleTimes`) **plus** the `visual_gate`
   ctest entry, which fails for the same reason as item 4 below (expected,
   given the STOP — not a third unexpected failure).
3. `bash scripts/build-gles2-x86.sh`: **PASS** (both before- and after-fix
   builds compiled clean).
4. `bash scripts/ci/visual_gate.sh`: **BEFORE PASS / AFTER FAIL** — see
   table above. This is the STOP trigger, not a verification failure in the
   ordinary sense.
5. `bash scripts/build-arm-noble.sh`: **PASS** — the driver fix compiles for
   ARM (`build-arm-noble/bin/gles2_zone_benchmark` and `willeq` both built
   successfully with the fix in place; confirms the one-line change is not
   x86-only).
6. `bash scripts/refactor/check_file_sizes.sh fable` (run via `env -C
   wt/slot1` since the script resolves its repo root from cwd): **PASS, no
   new FAILs** — `docker/irrlicht-drm/COpenGLES2Driver.cpp` reports `WARN
   1967 lines (> 1500)`, but this file was already at 1959 lines (already
   WARN) on `fable` before this unit's 8-line addition; the ceiling
   classification (WARN, not FAIL) is unchanged.

### Phases B, C, D — not executed (blocked by the Phase A STOP)

Per the plan's design, all three are gated behind a clean Phase A gate.
None were started. No Pi contact was made this session (nothing ready to
measure — the Pi invocation budget was not touched). No
`benchmarks/pi_runs/p09c_measure/` CSVs exist. No
`benchmarks/contact_sheets/p09c_fp_delta/` contact sheet exists.

### KPI projection

Not computed — Phase D (Pi measurement) did not run, for the same reason
P09 and P09b both withheld a KPI projection when their own gates were not
clean: projecting Pi timing against a render path whose visual correctness
is an open question for Will would misrepresent the state of the work.

### Landing-sketch update

Unchanged from P09b's sketch for now, with one addition: the driver fix
recommended there as a "small, separate, low-risk follow-up unit" turned
out to have a real (if small, 2/9-frame) visible-output consequence in the
production-representative gate, contradicting the "one historically-invisible
sliver" framing P09b used to justify calling it low-risk. Recommendation for
whoever picks this back up: do not assume "warmup absorbs it" for any future
driver-level fix in this area without an explicit before/after gate run, as
done here — the assumption was wrong once already.

### Deviations from the plan

- Phase A's own step 2 ("goldens should be byte-identical... warmup frames
  absorb the first-draw glitch") was written as an expectation, not a
  guarantee, and the plan itself supplied the STOP path for exactly this
  outcome. This is that STOP path, not a skipped step: 2/9 golden frames
  changed (qeynos2 frame 0, gfaydark frame 0), evidence captured under
  `benchmarks/contact_sheets/p09c_driver_gate_diff/`, fix left in place
  uncommitted-as-blessed (fix IS committed to the branch; "blessed" refers to
  Will's golden sign-off, which is separate and has not happened).
- Phases B, C, and D (remaining-contributor bisection, fair FP baseline, Pi
  measurement) were not executed, per the above — this is the direct,
  intended consequence of the Phase A STOP, not an independent deviation.
- `docker/irrlicht-drm/COpenGLES2Driver.cpp` is the ONLY production file
  changed, as required ("the ONLY production change permitted is the driver
  lazy-init fix"). No other production files were touched.
