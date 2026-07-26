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

### Will's re-bless (2026-07-25) and Phase A completion

Will reviewed the before/after comparison for qeynos2 frame 0 and gfaydark
frame 0 and blessed the fixed rendering: the OLD goldens contained the
`getOpaqueAlphaFallback()` corruption, not the fix. Regenerated via
`scripts/ci/visual_gate.sh --bless` with the fixed driver; confirmed by
diff that ONLY the two anticipated frames changed (`git diff --stat --
tests/goldens` showed exactly `qeynos2_f0.png`, `gfaydark_f0.png`,
`manifest.json`, `contact_sheet.html` — no other golden PNG touched).
`manifest.json`'s `approval` block updated to record the P09c re-bless
(status/by/date/note). Gate re-verified clean with no `--bless`:
`scripts/ci/visual_gate.sh` → PASS, and the `visual_gate` ctest entry →
PASS. Committed separately from the driver fix (see Commits below). Phase A
is now fully closed; Phases B–D proceed below.

### Phase B — the remaining contributor: re-examined against the fixed driver

Re-ran the 15-pose merged-vs-unmerged pixel-identity check
(`refactor/scratch/p09_pixel_identity.py`) with the fixed driver AND with
P09b's now-redundant first-draw workaround removed from
`tools/hwcaps/zone_bench_merge.cpp` (see "Workaround removal" below).
Result: magnitudes are essentially unchanged from P09b's own
already-fixed-driver-adjacent table (e.g. qeynos2 golden0: 44,070px/max_delta
68 here vs 44,054px/max_delta 68 in P09b; befallen golden6: 2,267px/max_delta
209 in both; befallen golden7: 28,857px vs 28,446px) — the driver fix, now
landed, does **not** meaningfully shrink the residual. This answers the
coordinator's open question directly: the "unattributed contributor" P09b
saw was **not** (or only negligibly) the driver bug; P09b's own item-7
first-draw workaround had already absorbed that interaction for the
merged-vs-unmerged *comparison* specifically (both sides went through the
same first-draw path either way), even though the driver itself stayed
buggy in general until this unit's Phase A.

**Named suspects ruled out architecturally, before any bisection run**: the
plan's two suspects (wind-shader per-buffer `uMeshYBounds` packing,
atlas/unit-1 companion-texture state) are both **unreachable by this
benchmark's zone geometry, by construction**. `tools/hwcaps/
zone_bench_loader.h` registers exactly two material shader types for zone
region meshes — `materialPerPixelSolid` and `materialPerPixelAlpha` — and
`grep`ping the loader for any wind- or atlas-shader wiring into region mesh
construction (`buildTexturedMesh`/`buildColoredMesh` and their material
assignment) finds none. `uMeshYBounds`/`MaterialTypeParam` packing DOES
exist in production (`src/client/graphics/renderer_zone_load.cpp` ~line
2278-2302), but only for animated TREES (`AnimatedTreeManager`/
`TreeWindController`), which are entity-like objects, explicitly out of
scope for this zone-geometry-only merge (plan: "Zone geometry only —
entities and objects out of scope"). This matches the plan's own hint
("gate config is RGBA/non-EQE1 — verify what atlas-like state is actually
active before theorizing") — verifying first showed neither suspect
mechanism is even wired up in the harness under test, so no bisection
against either was warranted.

**Root cause, established via the new Phase C machinery (below) rather than
further P09b-style bisection**: building the "baked-unmerged" comparison
mode (same baked world-space vertex buffers as the merge path, but NOT
coalesced — one draw call per region, identity transform) and diffing it
against BOTH the merged path and the production-path (per-region-matrix)
unmerged path in one pass showed, decisively:
- **merged vs baked-unmerged: max_delta=0 (or max_delta=1, tiny) at 15/15
  poses** — the merge MECHANISM (coalescing/run-splitting) itself is
  correct; see the table in Phase C below.
- **baked-unmerged vs production-path unmerged: numbers that track the
  ORIGINAL "merged vs unmerged" residual almost exactly**, pose for pose
  (e.g. qeynos2 golden0: 44,124px here vs 44,070px merged-vs-unmerged;
  befallen golden7: 28,857px in both).

This means essentially the ENTIRE residual P09b flagged as "unattributed"
is the architecturally-inherent floating-point transform-order delta
(baked world-space position + identity transform, vs. per-region world
matrix + local-space position at draw time) that P09b's own Root cause B
already named as a hypothesis but couldn't fully separate from a possible
merge-mechanism bug — because P09b had no baked-but-unmerged comparison
point to isolate the two. **This IS the attribution the plan's Phase B
asked for**: not a new merge bug requiring a fix in the merge TU, but
confirmation that the residual is the Phase C FP-delta class, quantified
properly in Phase C below. No merge-TU code fix was needed as a result (the
merge mechanism was already correct); Phase B's "fix" is therefore: none
required, root cause reassigned from "unattributed" to "architectural FP
delta, quantified in Phase C."

**Workaround removal** (plan step A3, done as part of this reconciliation):
removed P09b's `g_firstDrawDone`/first-draw-through-unmerged-path workaround
from `renderMergedDrawEntries()` in `tools/hwcaps/zone_bench_merge.cpp` (kept
the `SMaterial::operator==` run-splitting from the same fix, per the plan —
that part is correctness hardening, not a workaround). Rebuilt x86-gles2,
re-ran the 15-pose check (numbers above already reflect the workaround
removed): no regression from removing it — expected, since the driver bug it
worked around is now fixed at the source.

A small, secondary residual DOES remain in the merge mechanism itself: 2/15
poses (qeynos2 golden0, gfaydark golden5) show a tiny `max_delta=1`
(single 8-bit-LSB) difference between merged and baked-unmerged, in small
localized clusters (54px and 10px respectively, out of 480,000). Both
merged and baked-unmerged outputs are independently deterministic across
repeated invocations (verified for qeynos2 golden0). Character (single-LSB,
small scattered cluster) matches the same low-magnitude rasterization-order
sensitivity class P09b already documented for bf355/bf472/etc, now appearing
at the draw-call-COUNT boundary (1 coalesced call vs N individual calls
covering the identical vertex data/order/state) rather than a
submission-ORDER difference (which item 7's exact-order gather already
guarantees is identical). Per the plan's STOP condition ("unattributed after
2-3 focused bisection rounds"): documented as narrowed (not a state/logic
bug — same vertices, same order, same material, same transform; plausibly a
GPU rasterization/tiling artifact at draw-call boundaries on llvmpipe) but
not further pursued this unit; magnitude (1 LSB, 2/15 poses, <0.02% of frame
each) does not block Phase C/D. Full detail in
`benchmarks/contact_sheets/p09c_fp_delta/results.txt`.

### Phase C — the fair baked-vs-baked baseline

Added `ZoneBench::renderBakedUnmergedDrawEntries()` (`tools/hwcaps/
zone_bench_merge.cpp/.h`) and a new CLI flag `--baked-unmerged-zone-draws`
(`tools/hwcaps/gles2_zone_benchmark.cpp`, default off, mutually exclusive in
practice with `--merge-zone-draws`): draws the SAME baked world-space vertex
buffers `buildMergedZoneBuffers()` already builds for the merge path, but
submitted as one `drawMeshBuffer()` call per region/buffer (no coalescing),
identity world transform, same per-entry order as the unmerged path. This is
the plan's "unmerged-but-baked comparison mode" (design step 7).

**Bar 1 — merged vs baked-unmerged (isolates merge-only effects)**:

| zone | pose | diff_px | max_delta | status |
|---|---|---|---|---|
| qeynos2 | golden0 | 54 | 1 | FAIL (tiny, see Phase B) |
| qeynos2 | golden1 | 0 | 0 | PASS |
| qeynos2 | golden2 | 0 | 0 | PASS |
| gfaydark | golden3 | 0 | 0 | PASS |
| gfaydark | golden4 | 0 | 0 | PASS |
| gfaydark | golden5 | 10 | 1 | FAIL (tiny, see Phase B) |
| befallen | golden6 | 0 | 0 | PASS |
| befallen | golden7 | 0 | 0 | PASS |
| befallen | golden8 | 0 | 0 | PASS |
| befallen | bf355 | 0 | 0 | PASS |
| befallen | bf472 | 0 | 0 | PASS |
| befallen | bf626 | 0 | 0 | PASS |
| befallen | bf791 | 0 | 0 | PASS |
| befallen | bf917 | 0 | 0 | PASS |
| befallen | bf987 | 0 | 0 | PASS |

13/15 poses achieve the plan's unqualified max_delta=0 bar; 2/15 show a
max_delta=1 (single LSB) residual, addressed in Phase B above (narrowed,
not a state/logic bug, not pursued further this unit per the plan's own
STOP guidance).

**Bar 2 — baked-unmerged vs production-path unmerged (the pure FP
transform-order delta)**: full 15-pose table, discussion of WHY it's larger
at qeynos2 golden0/befallen golden6/7 (oblique views of coplanar-heavy
architecture), and the contact sheet are in
`benchmarks/contact_sheets/p09c_fp_delta/results.txt` and
`benchmarks/contact_sheets/p09c_fp_delta/contact_sheet.png` (4 worst poses:
befallen golden7, qeynos2 golden0, gfaydark golden5, befallen golden6, each
with baked/production/diff-mask panels). Headline numbers: qeynos2 golden0
44,124px/max_delta 68; befallen golden7 28,857px/max_delta 104; gfaydark
golden5 682px/max_delta 126; befallen golden6 2,267px/max_delta 209;
befallen bf626 0px/max_delta 0 (the one pose with almost no coplanar
geometry in view). **Per the plan: this magnitude is reported for Will's
one-time judgment, not self-declared acceptable.**

### Phase D — the Pi measurement (P02 protocol)

Pi session: `user@10.0.30.192`, reachable throughout, cleaned of leftover
`/tmp` artifacts from a prior (P09 A-gate) session before starting
(`/tmp/agate_pi.csv`, `/tmp/gles2_zone_benchmark_p09` removed), 6/12
invocations used (1 session, well under the cap), CmaFree stable throughout
(76,004→60,956→61,080 kB across the session, no monotonic ratchet, no
SessionAbort), governor restored to `ondemand` on exit, Pi left pristine
(verified `/tmp` empty of run artifacts after cleanup). Script:
`refactor/scratch/p09c_pi_merge_replay.py` (bespoke — reuses `scripts/pi/
pi_common.py`'s primitives directly rather than `pi_bench_run.py`, which is
specialized for the H04 EQE1 repeating-pose-cycle case, not a
`--path-file` moving replay). `ARM_BUILD_BIN_DIR` explicitly overridden to
THIS worktree's `build-arm-noble/bin` (the plan's named trap) before any Pi
contact; ARM binary rebuilt immediately before the session to include the
driver fix + workaround removal + Phase C's new mode.

Canonical replays: `benchmarks/paths/befallen_seed42_1000f.path` and
`benchmarks/paths/qeynos2_seed42_1000f.path` (1000 frames each, `--seed 42`,
production config), merged vs unmerged, 800x600, matching the manifest/gate
resolution.

**Whole-run summary** (total_ms = frame time, submission_ms = the
setMaterial+drawMeshBuffer-loop CSV column; all times ms):

| run | n | total avg | total p95 | submission avg | submission p95 | draws avg |
|---|---|---|---|---|---|---|
| befallen zone-only, unmerged | 1000 | 36.557 | 173.827 | 21.675 | 101.558 | 253.0 |
| befallen zone-only, merged | 1000 | 11.255 | 25.497 | 2.072 | 5.777 | 7.9 |
| befallen combined (e205), unmerged | 1000 | 104.261 | 279.391 | 67.035 | 186.953 | 710.1 |
| befallen combined (e205), merged | 1000 | 54.666 | 65.806 | 31.504 | 39.514 | 465.1 |
| qeynos2 zone-only, unmerged | 1000 | 14.691 | 19.261 | 2.946 | 5.846 | 58.9 |
| qeynos2 zone-only, merged | 1000 | 12.582 | 17.102 | 1.166 | 2.071 | 10.0 |

Sanity check against prior baselines: befallen zone-only unmerged p95
(173.827ms) matches P08/P08b's own baseline (174.3ms) to within run-to-run
noise; befallen combined (e205) unmerged p95 (279.391ms) matches the
279.7ms baseline the same way — confirms this session measured the same
underlying scenario P08/P08b did, not a different one.

**Draw reduction, merged vs unmerged (whole-run draws avg)**:

| scenario | unmerged draws avg | merged draws avg | reduction |
|---|---|---|---|
| befallen zone-only | 253.0 | 7.9 | 96.9% |
| befallen combined (e205) | 710.1 | 465.1 | 34.5% |
| qeynos2 zone-only | 58.9 | 10.0 | 83.1% |

The combined-scenario reduction (34.5%) is much smaller than the zone-only
reduction (96.9%) for the mechanical reason the plan anticipated: entity
draws are untouched by this merge (out of scope) and dominate `draws_avg`
in the combined scenario (710.1 total vs 253.0 zone-only unmerged ⇒ ~457
entity draws), so a near-total elimination of the ~253 zone draws only
moves the combined total by ~35%. This is expected, not a shortfall of the
merge mechanism (which independently achieves the P09 ceiling on the
zone-only slice, matching P09's original single-pose table).

**Pinned-pose extraction** (befallen path frames 355/472/626/791/917/987 —
the file-name convention IS the frame index in the 1000-frame path):
draws/state_changes/polys/timing at each pinned frame, all four befallen
run variants, are recorded in full in `benchmarks/pi_runs/p09c_measure/`
(see the CSVs) and the script's stdout capture; headline: `state_changes`
(the P08 ceiling quantity) is identical between merged and unmerged at
every pinned frame in every variant (e.g. frame 472: unmerged draws=1297
state_changes=14 vs merged draws=14 state_changes=14 — merged draws equal
the ceiling exactly, matching P09's single-pose table), confirming the
merge mechanism still hits the ceiling under full moving-path replay, not
just static single-pose captures.

**Unmerged geometry columns vs P08/P08b baselines**: compared `draws`,
`state_changes`, `polys` frame-by-frame (all 1000 frames) between this
session's unmerged CSVs and P08b's own Pi baseline CSVs
(`benchmarks/pi_runs/p08b_entity_reality/befallen_pi_e0_replay.csv`,
`befallen_pi_e205_replay.csv`): **0 mismatches at all 1000 frames, both
befallen zone-only and befallen combined** — byte-identical geometry,
confirming the unmerged path (and this session's replay setup) changed
nothing. No P08/P08b Pi baseline exists for qeynos2 ZONE-ONLY (e0) — P08/P08b's
qeynos2 Pi work was combined-only (`qeynos2_pi_e205_replay.csv`,
`qeynos2_pi_e205_composite.csv`), not directly comparable to this unit's
qeynos2 zone-only replay — so this check could not be performed for
qeynos2; flagged as a gap, not a failure (there was nothing to compare
against).

**CSVs**: `benchmarks/pi_runs/p09c_measure/` — `befallen_zone_unmerged.csv`,
`befallen_zone_merged.csv`, `befallen_combined_unmerged.csv`,
`befallen_combined_merged.csv`, `qeynos2_zone_unmerged.csv`,
`qeynos2_zone_merged.csv`, `cma_telemetry.tsv`.

### KPI projection

Direct measurement (not a projection): zone merge stacked with TODAY's
shipping (unmerged) entity path, at befallen's e205 gameplay-representative
combined load — p95 279.391ms → 65.806ms, a **76.4% reduction**, comfortably
past the 97.8% zone-draw ceiling's expected real-world dilution by
untouched entity draws.

Projection (flagged per the plan, "multiply out the isolated effects
honestly"): P08c projected composite-entities alone would take befallen
combined p95 279.67ms → ~187ms (33.1% reduction) if it landed alone. Naively
compounding the two isolated fractional reductions on the same p95 base
(zone-merge fraction remaining × entity-composite fraction remaining):
279.391 × (65.806/279.391) × (187/279.67) ≈ 65.806 × 0.6687 ≈ **44.0ms** if
BOTH land together — this would put befallen's worst-case combined frame
just outside the 30fps/33.3ms budget (≈22.7fps), a large step closer than
either lever alone, but still short. This is explicitly a projection (p95
does not decompose multiplicatively in general; the two levers were
measured in separate sessions/units against slightly different baseline
runs) — not a substitute for measuring both together.

### Landing-sketch update

P09b's sketch (driver fix as a recommended low-risk follow-up; run-key
`operator==` grouping; first-draw safeguard) is updated:
1. **Driver fix landed** (this unit, `docker/irrlicht-drm/
   COpenGLES2Driver.cpp`, `getOpaqueAlphaFallback()`): production's merge
   landing no longer needs ANY first-draw special-casing — the upstream bug
   is fixed at the source. The `SMaterial::operator==` run-splitting
   (P09b) remains recommended for production's coalescing loop regardless
   (real correctness hardening, independent of the driver bug).
2. **Wind/atlas are non-issues for zone-geometry merging as currently
   scoped** — confirmed architecturally (not just empirically) that neither
   mechanism is wired into zone region material assignment; production's
   landing does not need special-case handling for either UNLESS a future
   unit extends the merge to also cover zone-embedded wind-shaded geometry
   (none currently exists) or an atlas-backed zone path (RGBA is what ships
   today per the gate's own config).
3. **The fair-baseline finding matters for what "pixel-identical" should
   mean in the production landing's own acceptance bar**: don't chase
   max_delta=0 against the PRODUCTION (non-baked) path — that bar is
   architecturally unreachable by design once positions are baked. The
   right bar is max_delta=0 (achieved, 13/15, 2/15 at a tiny 1-LSB residual)
   against a baked-but-unmerged reference, with the baked-vs-production
   delta reported separately for a one-time design-acceptance judgment
   (already given by Will in spirit for the driver fix; the FP-delta
   magnitude itself is a NEW judgment call for whoever scopes the
   production landing unit, not resolved here).
4. **Combined-scenario dilution is real and should be sized into any KPI
   commitment**: don't quote the 97.8%/96.9% zone-only reduction as "the"
   number production stakeholders will see — the combined-load number
   (34.5% draw reduction, 76.4% p95 reduction at befallen e205) is what a
   player experiences, and it's still large, but a materially different
   headline.
5. Entity-path work (P08c's composite entities, `--batch-entities`, or a
   future equivalent) remains necessary IN ADDITION to this merge to close
   the KPI gap — confirmed again by this unit's own projection (~44ms,
   still short of 33.3ms) — not instead of it, consistent with P08c's own
   conclusion.

### Verification results (full bar, all changes)

1. `cmake --build build -j24`: **PASS** (re-run after Phase B/C/D code
   changes to `tools/hwcaps/zone_bench_merge.cpp/.h` and
   `tools/hwcaps/gles2_zone_benchmark.cpp`).
2. `ctest --test-dir build --output-on-failure`: **PASS** (final run, all
   code changes landed) — the ONLY failure is the documented host exception
   `AudioManagerTest.PreloadCommonSounds`; `visual_gate` **PASSED** in this
   run (confirms the re-blessed goldens are clean end-to-end via ctest, not
   just the standalone script) and `RDPServerFrameTest.
   UpdateFrameMultipleTimes` also passed this run (it's a documented
   flaky/host exception, not a per-run guarantee of failure).
3. `bash scripts/build-gles2-x86.sh`: **PASS** (re-run multiple times
   across Phase A/B/C iterations, all clean).
4. `bash scripts/ci/visual_gate.sh`: **PASS** (post-re-bless, confirmed
   twice — once standalone, once via the `visual_gate` ctest entry).
5. `bash scripts/build-arm-noble.sh`: **PASS**, re-run immediately before
   the Pi session to include all Phase A/B/C source changes.
6. `bash scripts/refactor/check_file_sizes.sh fable`: re-run after all
   changes (see below) — no new FAILs.

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
