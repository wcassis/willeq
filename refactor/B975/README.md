# B975 -- entity name tags in one batch

GitLab #293 (class B, perf, lane pi), MR !73. Branch `task/B975-name-tag-batch`;
current worktree `/home/user/src/willeq-wt/kb-b975-name-tags`.

## Current state — implemented, QA/review passed, merged

The final implementation handoff and cumulative independent QA both cover
`bcf5335ffa7bcdabddb6090b9469a564d6618015`, not just the earlier candidate.
Review run 285 requested the documentation correction recorded here. Run 304
accepted the correction at `3872871a559b59cf2a5493c2259a89f3d1a2e555` with
pipeline 499 green. MR !73 merged as `ee538d2c14849ac309dd6aa0d7dbef429b884498`;
the merge tree equals the reviewed source. Issue #293 is closed.

- Final implementation: [run 281 final report](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-report.txt)
  and [validated handoff](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/result.json).
  Final-head Pi N200 render medians are **1914 / 1955 us** (production policy /
  explicit `nobocache` repeat), below the unchanged 2000 us bar. Raw CSVs:
  [default](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-micro/capture/drawcost/drawcost.csv),
  [repeat](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-micro/capture/drawcost-nobocache/drawcost.csv).
  Final matrix tables, distinct from the committed candidate tables, are
  [Pi timing](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-verification/pi-timing.md),
  [Pi memory](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-verification/pi-crowd.md),
  [x86 timing](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-verification/x86-timing.md)
  and [x86 memory](/home/user/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/final-verification/x86-crowd.md).
- Independent cumulative QA: [run 284 PASS verdict](/home/user/kb-evidence/b975-qa-20261005/run-284/verdict.md)
  and [handoff](/home/user/kb-evidence/b975-qa-20261005/run-284/result.json).
  Hash-revalidated independent Pi N200 measurements are **1918 / 1908 / 1924 /
  1915 us**; [timing evidence with raw CSV paths](/home/user/kb-evidence/b975-qa-20261005/run-283/timing-verification.json).
  Both platforms' nine primary poses have zero differing pixels. Focused
  regressions pass with no name-tag test skips.
- Recovery crowd A/B: **1.336 / 32.553 ms** transparent-pass medians (batch /
  per-node), from complete post-ready idle windows, not whole-run timings;
  [raw-log paths, source lines and guard results](/home/user/kb-evidence/b975-qa-20261005/run-284/crowd-verification.json).
  Both legs pass the unchanged guards; snapshots restore byte-identically and
  cleanup completes. Only bounded waits changed, idle 40→180 s and hold 15→75 s;
  [tour diff](/home/user/kb-evidence/b975-qa-20261005/run-284/tour-differences.json).
  [Original run 283](/home/user/kb-evidence/b975-qa-20261005/run-283/result.json)
  remains FAILED (`no_steady_60s`, no final per-node idle timing blocks).
  **B987 / issue #306 is the separate harness follow-up** for durable observation
  controls; see [plan](../B987_plan.md). Its fresh verification is distinct from
  this historical recovery. Both wrappers now expose `--idle 180 --hold-after 75
  --cap 720 --frametiming` for this acceptance recipe, and reject missing final
  readiness/steady or post-ready idle timing. Longer waits do not guarantee a
  pass: readiness beyond the finite observation window must still fail.
- Pipeline **496** on that implementation head passed hygiene/build/test/gate/
  build-arm; [CI response](/home/user/kb-evidence/b975-qa-20261005/run-284/ci-response.json).
  This is incremental CI, not a cold-build claim. This documentation-only
  rework requires its own exact-new-head CI and source-equivalence proof;
  [Run 286](/home/user/kb-evidence/b975-doc-rework-20261005/run-286/result.json)
  failed pipeline 498's DocCitations and BoardDirNumbers checks; it remains a
  failed attempt. The bounded documentation correction and exact-new-head
  revalidation belong to [run 303](/home/user/kb-evidence/b975-doc-rework-20261005/run-303/result.json).

## Not verified

- The **64 MiB owned-memory target is unmet**. Live x86 llvmpipe captures exceed
  the 256 MiB bound; x86 functional acceptance is not a memory-bound pass.
- Primary pose identity is not global transparent-scene identity: nearer
  transparent overlaps and equal-distance tie ordering can differ. The
  rendering-order boundary below remains in force; no human visual blessing
  is claimed, including for live targeted-overlay/whole-frame differences.
- QA's shutdown-point sample retains **1584 GPU bytes before renderer
  teardown**, not a measured zero after destruction. The destructor regression
  supplies release coverage; [owner evidence](/home/user/kb-evidence/b975-qa-20261005/run-284/owner-verification.json).
- Review's opt-in `StaticUiFrameCostTest.AccountPerStageTimings` diagnostic
  skipped without `WILLEQ_T462_ACCOUNT=1`; it is not a timing pass.
  [Review verdict and limitations](/home/user/kb-evidence/b975-review-20261005/run-285/verdict.md).
- No fresh hardware capture or full review-stage live matrix is claimed for
  this documentation-only repair. Earlier failed candidates, CI failures,
  canceled CI and run 283 remain preserved, not retroactively passed.

## Historical origin

Originally based on `gitlab/fable` d3375b65 (B964 / MR !70 merged), in
`/mnt/projects/fable/wt/audit-nametags`, with initial raw evidence at
`/mnt/projects/fable/review/B975/`. Current evidence is linked above.

## Submission investigation, run 281 — candidate, not acceptance

The orchestrator authorized further submission work after run 279's stop.
Evidence: `~/kb-evidence/b975-name-tags/codex-resume-20261004/run-281/`.
The production policy, pinned Mesa and all acceptance limits remain unchanged.

An isolated GL/ioctl trace of the previous binary measured **5659 us** for
the 200-tag render pass. Large draws spent substantial time in GEM_CREATE,
with no wait/submit ioctl inside those draws (`profile-job/capture/profile.log`).
Reusable VBO/EBO storage alone measured **3459 us**: whole-buffer subdata
writes still allocated replacement Lima BOs (`vbo-probe-job/capture/profile.log`).
Keeping a small unused tail and reusing both small and large draw storage
measured **2412 us**, still FAILING the unchanged 2000 us bar
(`pad-probe-job/capture/profile.log`). Both probes preserved the nine primary
pixel comparisons. They are diagnostic preload experiments, not production
acceptance or a change to the shipped Mesa.

Source attribution is retained beside the traces: Mesa 25.2.8
`lima_resource.c:lima_transfer_map` creates a new BO on whole-resource discard;
`lima_draw.c:lima_update_varying` allocates separate geometry-output storage
for every draw. The latter remains even with reusable input buffers.

The candidate uses bounded reusable GPU vertex/index storage with one slack
vertex, normal synchronized subdata writes, and a state-only driver entry
point instead of drawing a first glyph to establish state. All glyphs now
belong to the same run. GPU bytes enter the driver's logical tally and the
GEM census owner tag; the CPU owner row reports GPU storage separately.
Both destruction and idle-window trimming release storage. Benchmark columns
separate state setup, index preparation, upload (including synchronization)
and draw calls. Regression coverage checks chunk/shorter-run reuse, pixels,
fallback ordering, and bounded/accounted/released GPU storage.

Candidate `850b679c6974a5dccf08cea93c063459efa91d89` passes the production
Pi micro bar: **1917 us** at N200, with an explicit-policy repeat of
**1963 us**. All primary poses are identical on both platforms. CI pipeline
**495** passes hygiene, build, test, gate and build-arm; its test/gate traces
contain no failed retry. These are candidate results, not a substitute for
the final-head retake required by the execution contract.
The CI evidence is from the runner's incremental tree, not a claimed cold run.

Its full Pi crowd/normal matrix passes the unchanged guards, binary/Mesa
identity, final-visit, spawn-acknowledgement and cleanup checks. Idle crowd
transparent-pass samples are **1.3 / 32.3 ms** (batch / per-node, Qeynos) and
**1.5 / 34.9 ms** (Greater Faydark). Full tables, including normal-zone rows
and the distinct whole-run measurements, are in
[`evidence-run281/pi-timing.md`](evidence-run281/pi-timing.md) and
[`evidence-run281/pi-crowd.md`](evidence-run281/pi-crowd.md).
The micro history preserves each failing candidate and its exact source/hash
in [`evidence-run281/micro-history.json`](evidence-run281/micro-history.json).

The x86 functional matrix also passes every case: final-visit ready/steady,
spawn acknowledgements, selected renderer and clean exit. Its full memory
figures remain in `evidence-run281/x86-crowd.md`: live llvmpipe RSS exceeds
256 MiB in captured cases. These are not Pi memory-guard or hardware-performance
passes, and the 64 MiB owned-memory target is not claimed as achieved.

Live targeted, untargeted and corpse captures preserve the existing white
NPC and blue player masks exactly between renderers. The corpse name changes
through `setText`. The targeted view has fewer blue glyph pixels than the
untargeted control: the post-scene gold target outline overlaps the player's
label. This supports an overlay-phase explanation for run 279's six blue/gold
differences; that earlier mismatch remains recorded, not retroactively passed.
Current whole-frame differences (world animation and UI text) are retained
separately in `evidence-run281/live-comparison.json`; no human visual approval
is inferred. Both live snapshots and the x86 matrix snapshot restore
byte-identically, with the stack stopped and locks released.

The required terminal-head retake subsequently passed, followed by independent
cumulative QA run 284; see Current state above. The committed tables retain
their candidate SHA and never stand in for that final retake. The former
QA-scheduled/held status is superseded; review and merge are still pending.

The first candidate's local test and pipeline 492 test job retain their
failure: the re-registration test still expected the removed bootstrap
draw. The corrected assertion expects the actual single run; local name-tag,
static-UI and Lima-policy tests and all primary x86 poses pass
(`local-corrected/`). A subsequent packing candidate copies each glyph's
four corners together rather than performing separate vector insertions;
its local regression and pixel checks pass (`local-packed/`). Neither local
timing is Pi performance evidence. A shader-precision preload experiment
did not reduce allocation volume or materially improve timing and was not
added to production (`shader-probe-job/`, original-shader PNG comparisons
in `shader-original-pixel-comparison.json`).

The native reusable-buffer candidate at `7374b642` passes all primary Pi
poses but still reports **2221 us** at 200 tags (**765 build / 1442 submit**).
Its explicit-policy repeat is **2203 us**, including **50 setup / 0 index /
213 upload / 1120 draw** microseconds (independent medians). Raw results:
`candidate-micro/capture/`. Large-draw tracing now shows one geometry-output
allocation per draw rather than transient input uploads; the pinned Mesa's
geometry-output allocation remains in place. This candidate still FAILS.

A further CPU candidate retains a sized vertex array and writes into its used
prefix after growing once per tag. Shorter runs, chunk boundaries and font
fallback splits must never draw unused retained vertices; the existing pixel
regressions exercise all three. Local tests and fixed poses pass
(`local-prefix/`). Native Pi measurement still FAILS: **2046 us** at N200,
with an explicit-policy repeat of **2017 us** (`prefix-micro/capture/`).
All primary Pi poses remain identical. Atlas UVs are now cached once per
glyph using the same arithmetic, a fixed 2 KiB in the batch object rather
than additional storage per tag. Its native measurements are the passing
`uv-micro/capture/` candidate results above.

## Historical run 279, 2026-10-04 — production CPU bar failed

The following prior-attempt records are historical evidence, not the current
work queue. Their failures remain failures; run 281's measurements above do
not rewrite them.

Run `279` of `t_8f86d310` reviewed and integrated B986 commit
`fd8b002b2a3ecb4f1b7d1a896cad20b9812f4d5d` into this branch. The fix disables
Lima BO retention before DRM device creation; it does not change a memory
threshold or golden. Its parent evidence is not B975 acceptance. Fresh run
evidence is under
`~/kb-evidence/b975-name-tags/codex-resume-20261004/run-279/`.

Source review found the earlier shrink still regenerated every glyph's
indices each frame. The compact path now grows a reusable index prefix,
bounded by the 16-bit draw limit, and reuses it for each vertex chunk. Draw
length comes from the current vertices, never the retained index suffix.
Texture reciprocals are computed once per run with the same arithmetic.
Trimming checks vertex and index capacities independently and immediately
publishes the reduced memory-owner row. The standalone `nametag_drawcost`
now applies the same DRM Lima policy as the client, so its default timing
tests the production policy rather than the former cache setting.

The focused Release checks and all primary x86 poses pass after this change
(`local-reconfigured/job.log` and `local-reconfigured/pixel/pixel.csv`). The
new regression crosses the 16-bit index boundary, then draws shorter,
single-glyph and empty runs against the original per-node framebuffers.
An earlier local build command failed because its stale CMake configuration
did not know the new Lima-policy test target; that exit remains recorded in
`local-index-check/job.json`. Explicit reconfiguration resolved it.
The later local hygiene run also retains exit 8: the partial test build lacks
the texture, pack and database builders, so their reproducibility checks
fail to start. Source/doc checks pass; `documentation-hygiene.log` records
the distinction. Complete-build CI results are separate evidence, not a
rewrite of those local failures.

### The old tool's default was not the production policy

The fresh baseline tool reports **1706 us** at 200 visible tags, but explicitly
applying the client's `LIMA_DEBUG=nobocache` policy reports **5771 us**. The
standalone tool had omitted B986's initialization. Its old default number
cannot establish production acceptance. This is why the tool now calls the
same policy helper before creating its device.

The optimized production-policy tool at
`c81e226090f2960bcd21eac8abee41b2f38ed72b` reports **5604 us** at 200 tags:
**751 us build, 4845 us submit**, with **358456 B** of buffers. The explicit
policy repeat reports **5621 us**, with **758 us build, 4834 us submit**.
Component medians need not sum to the total median. All primary fixed poses
remain pixel-identical on both platforms. Sources and compact tables:
[`evidence-20261004/`](evidence-20261004/); raw optimized Pi results are
`index-micro/capture/{drawcost,drawcost-nobocache,pixel}/` under the run root.

**The unchanged 2000 us bar fails: submission alone exceeds it.** The planned
CPU-side shrink cannot remove that measured submission cost. Per the card's
stop condition, this is a blocked handoff for the orchestrator to plan the
remaining submission work, not permission to restore BO retention or relax
a budget. CI pipeline 490 passes at the optimized code revision, but does
not waive this performance failure.

### Verification boundaries

The fresh crowd/normal matrix uses the integrated B986 revision above, before
the index change. Every Pi variant passes its unchanged memory, final-visit
and no-OOM checks, with byte-identical restoration. That independently
confirms the memory fix, not optimized-head crowd acceptance. The complete
optimized-head matrix is not rerun after the CPU stop condition; it remains
a required check for a resumed implementation.

The baseline x86 matrix also passes its exit, final-visit, spawn-acknowledgement
and renderer-path checks. Idle frame-time and memory tables for both platforms
are committed in `evidence-20261004/`; x86 is integration evidence, not a Pi
performance measurement or a waiver of B974's llvmpipe memory finding.
Baseline live screenshots show white NPC tags, blue player tags and the
corpse-name update. The scoped NPC masks match in both phases, and the player
mask matches after death; the living player mask differs by six pixels in
the overlapping scene. That difference remains unapproved. The comparison
and rectangles are in `evidence-20261004/live-comparison.json`; whole live
frames are not a deterministic oracle.

The terminal `result.json` and `final-report.txt` under the run root record
the exact final head, CI jobs, binary identities, required-check failures
and cleanup. MR !73 remains draft; #293 is not closed or advanced to review.
No human visual approval or downstream release is implied. The historical
sections below retain earlier failures rather than silently replacing them.

## STATE AT HAND-OFF 2026-10-01 (historical)

The unit was stopped on a hand-off order mid-way. Everything below is the
state at the WIP commit that carries this section.

## Guided Codex verification, 2026-10-03/04 — blocked by B986

Resumed the paused card `t_8f86d310` in its existing worktree. Merged
`origin/fable` at `c778d23dab9024a55c983c62bf61169453f2a2bb`; CI pipeline
483 supplies the x86 binaries and the ARM publication for that commit.
Raw evidence for this run is under
`~/kb-evidence/b975-name-tags/codex-20261003/`.

The earlier `pi/crowd800/` runs are **not successful crowd evidence**:
the first two runs have no `summary.json`, and the third was interrupted.
Their Pi-side `runner.out` reports a missing launcher-local script path.
This is the already-filed B985 / #304 transport defect. The B975 branch now
stages the runner, inputs and binary explicitly and retrieves the Pi run
directory over rsync. The `transport-smoke/` leg (previous `d0bb5fba` binary,
harness verification only) passed, returned three rendered frames, proved
the pinned Mesa was mapped, and restored the server snapshot byte-identically.

Fresh merged-build Pi microbenchmark (`pi-micro/`, exits both zero):
200 tags take **1690 us** in `render()` (879 us build, 801 us submit;
component medians need not sum to the total median), with 381424 B of batch
buffers. All nine primary pixel comparisons have zero differing pixels.
The deliberately different tie-order variant retains the documented
0–23 pixel differences; it is separate from the primary comparison.

The x86 crowd runner now supports the required `tagnode` A/B and
`--frametiming`. The B975 timing parser now uses the **last** face before
movement, excluding zone-hop warmup; `NameTagCrowdTiming` pins this and
refuses to substitute warmup samples when the idle interval has no samples.
Red/green output is in `frametiming-{red,green}.log`.

Pipeline **485** at `a28f1b0795602d3dfb592291ee44150d3e391a15` passes
hygiene, build, the full test job and the visual gate. The renderer binaries
remain those from pipeline 483: subsequent changes affect only the capture
harness, its tests and documentation. Fresh x86 NameTagBatch tests and all
nine primary fixed-pose pixel comparisons also pass.

The intentional crowd tour visits arena and returns. Its verification now
opts into `--final-visit`, requiring ready and steady in the last visit to
the requested zone. A failed return cannot borrow an earlier success;
`RenderedZoneFinalVisit` covers that boundary. Whole-process exit/FATAL/OOM
checks remain in force. No memory threshold or golden was changed.

Both Qeynos normal-population variants pass. The Qeynos crowd variants
reach ready/steady and exit cleanly, but fail the existing 10 MiB unnamed
GPU allocation guard: 18.70 MiB batched, 19.23 MiB per-node. This is present
with batching disabled too; its cause is not yet isolated (B986 / #305). The first crowd
comparison reduces the idle transparent pass from 19.7 ms to 1.1 ms and
steady owned memory from 130.68 MiB to 122.74 MiB. These are medians from
separate live runs, with different dynamic visibility/governor decisions.

An intermediate crowd attempt was interrupted by editing its running shell
harness. Its partial `pi-crowd/qeynos2/{imp0,tagnode0}` directories are invalid;
the verified replacements are under `pi-crowd-resume/qeynos2/`. The earlier
successful crowd pair remains under `pi-crowd/qeynos2/`.

Greater Faydark reproduces the crowd guard failure in both paths:
27.32 MiB batch and 26.68 MiB per-node. Its idle transparent pass is
1.2 ms batch versus 24.0 ms per-node; the batch reports 200 visible tags
and 1137.5 us CPU. Steady owned memory is 146.48 versus 144.93 MiB.
The per-node run's live tmpfs cache was only 688 KiB
(`gfaydark-tagnode-cache-kib.txt`), insufficient to explain the excess.
B986 / #305 records the unresolved accounting failure; it is not waived.

Source inspection also corrects the old colour-check wording: NPC name tags
are white and player tags blue; con colours belong to placeholder meshes.
The live check must preserve those existing colours and exercise the corpse
name update, rather than claim the batch adds con-coloured name tags.

All Pi variants are captured. Qeynos normal-population passes; Greater
Faydark normal-population fails the same guard (12.24/11.80 MiB,
batch/per-node). `evidence-20261003/pi-verification.csv` contains the
verifier's net-of-PNG values and exact source paths. The separate generated
`pi-crowd.md` table prints raw GPU values, including captured PNG charges;
do not mix those memory definitions.

All eight x86 variants exit zero, with no watchdog or FATAL, and reach
ready and steady in the final requested zone (`x86-verification.csv`).
These are integration results, not Pi performance or memory-budget passes:
the crowd runs reproduce the known llvmpipe growth in B974 / #292. Some
x86 `gpu_driver` ledger values even exceed resident anonymous bytes, so the
reported clamped owned value is not a useful memory comparison.

Live daylight A/B (`live-tags/{batch,tagnode}/{living,corpse}.png`) exercises
white NPC tags, blue player tags, and `setText` through NPC death. Both clients
exit zero with ready/steady and no lost zone link. Whole frames differ by
172050 pixels (living) and 149714 (corpse); the world, animations and chat are
not deterministic. Fixed-rectangle colour masks match exactly for the NPC
in both phases and the player in the corpse phase. The living player mask
differs by six pixels where it overlaps the NPC; this is recorded, not waived
or represented as exact whole-frame identity. See `live-tags-comparison.json`
for rectangles, colours and counts. Will retains visual approval.

### Verification result and handoff

| Check | Result |
|---|---|
| Pi 200-tag render pass <=2 ms | PASS: 1.690 ms, `pi-drawcost.csv` |
| Nine primary fixed poses, x86 and Pi | PASS: zero differing pixels, both pixel CSVs |
| Full CI suite and visual gate | PASS at pipeline 485; final pushed-head pipeline is linked from MR !73 |
| Pi crowd/normal matrix | All eight captured; two pass, six FAIL the unchanged GPU guard |
| x86 crowd/normal matrix | All eight integration checks pass; known llvmpipe memory growth remains |
| Live colour and corpse-name update | Captured and diffed; limitations above, visual approval remains Will's |

The fresh code/data comparison against c778d23d is empty
(`renderer-source-since-c778.diff`), so the measured renderer is unchanged by
the harness/documentation commits. Evidence retrieval now preserves remote
files if interrupted or rsync fails, and stops before retrying over them.

**Acceptance remains blocked by B986 / #305.** MR !73 stays draft and refers
to #293 without closing it. No budget, golden or baseline was changed; the
kanban board remains paused and no dependent worker was released. All Pi,
x86 and live-run character snapshots restored byte-identically; both locks
are free and the EQEmu stack is down. B985's
non-NFS transport fix is included in this branch but awaits review/merge.
The next decision is how to address the unchanged memory guard failure, not
whether to relax the name-tag CPU target (which is already met).

## PROGRESS 2026-10-03 (REM item 1: the shrink) — commit 05533bcb

REM item 1 is implemented (x86 side verified; the Pi re-measure is in
flight):

- **One proj x view per frame.** `projectNameTag()` replaces the per-tag
  `getScreenCoordinatesFrom3DPosition()` (which rebuilt the product every
  call); `render()` forms the single product once. The collision manager's
  arithmetic is fed through it, unchanged.
- **Compact 80 B/quad vertices, no `UiQuad` intermediate.**
  `{float x, y; u8 r, g, b, a; float u, v}` (20 B/vertex, 80 B/quad; the
  colour is normalised at the attribute, the driver's `UI2D_FS` samples it
  as a `vec4`). One static index pattern per run, a growing index buffer,
  and the compact vertex buffer. `UiQuadBatch` keeps its `UiQuad`/vertex path
  for the non-GLES2 driver (the batch node uses it there, unchanged).
- **Buffers at need.** `logWindow()` trims the compact vertex+index buffers
  and the `UiQuad`/`UiQuadBatch` buffers at a window boundary to
  `2 * windowMaxTags_ * 16 + 64` quads (the 791 KB at N=200 was `UiQuad` +
  144 B/quad vertex over-allocation).
- **`nametag_drawcost` published to the Pi** (REM item 1's publish
  extension): `scripts/pi/publish_arm_build.sh` also copies + strips
  `bin/nametag_drawcost` and runs `mesa_nollvm_select.sh` to produce
  `bin/nametag_drawcost-nollvm` (DT_RPATH to the Pi's LLVM-less Mesa
  prefix) when the ARM build has it.

### x86 (llvmpipe, loaded host — not perf evidence)

| metric | before (e4eeca69) | after (05533bcb) |
|---|---|---|
| N=200 batch render total | 3120 us | 2109 us |
| build (sort + project + layout) | 730 us | 382 us |
| submit (vertex fill + 2 draws) | 2390 us | 1731 us |
| pixel identity, 9 poses | 0 differing px | 0 differing px |
| 2 draws / frame, 2183 quads @ N=200 | yes | yes |

Full evidence: `~/kb-evidence/b975-name-tags/x86-shrink/{pixel,drawcost}/`.
**The 2 ms bar is a Pi number** — the ARM build publishes via CI
(`glab ci trigger build-arm`), then `nametag_drawcost-nollvm
pixel|drawcost --drm` on the Pi under `pi_lock kb-b975`.

### Remaining after this section

- REM item 2 (Pi crowd A/B, `scripts/b964/pi_crowd.sh --allow-vm-lane` —
  passthrough added in 05533bcb).
- REM item 3 (x86 crowd run, `scripts/b964/crowd_run.sh`).
- REM items 4-6 (live con-colour check; verification bar full ctest +
  visual gate; docs/BOARD/issues).
- REM item 7 (merge `gitlab/fable`, MR `Closes #293`).

### Done (with evidence)

1. **Per-tag cost of the old path, measured** (`tools/hwcaps/nametag_drawcost drawcost`,
   N tags under N empty entity nodes 115-160 units ahead, 800x600, no zone).
   Orange Pi (Lima, LLVM-less Mesa, `--drm`), CPU submission per tag:

   | component (per tag, Pi) | us |
   |---|---|
   | `getScreenCoordinatesFrom3DPosition` (recomputes proj x view every call) | 1.9 |
   | `CGUIFont::draw()` total (layout, 2+ `core::array`s, sprite bank, driver draw) | 150 |
   | of which `draw2DImageBatch()` alone (program/uniform setup + one `glDrawElements` over client arrays) | 128 |

   So ~85 % of a tag is the per-tag GL draw submission; layout + allocations
   ~20 us; projection ~2 us. Whole scene through `drawAll()`, glFinish-bracketed,
   per-node CTextSceneNodes: N=150 29.3 ms, N=200 44.8 ms (CPU 25.9 / 34.3 ms);
   the empty scene is 16.5 ms (the Pi's page-flip floor even with Vsync off).
   x86 llvmpipe (loaded host, not perf evidence): font draw 42-44 us/tag,
   draw2DImageBatch 34-36 us/tag. Files: `review/B975/pi/drawcost_800x600/drawcost.csv`,
   `review/B975/x86/drawcost_800x600/drawcost.csv`.
   Glyph atlas: the built-in font is ONE texture (all glyphs one sprite-bank
   texture) -- there was never a per-string texture; the cost is draws, not
   textures. Visibility today: `SimulationWorker::computeNameTagVisibility()`
   (distance <= `nameTagDistance_` 200 + PVS + LOS), kept unchanged. The
   oracle's own name distance / crowd rule is NOT recorded anywhere in
   `docs/` or `refactor/` (grepped) and was NOT measured on the oracle rig.

2. **Batch implemented** (commits 6632933e, e4eeca69):
   - `include/client/graphics/name_tag_batch.h`, `src/client/graphics/name_tag_batch.cpp`:
     `NameTagNode` (an `ITextSceneNode`: same parent, same 5.5/10.5 x scale offset,
     same setText/setTextColor/setVisible/remove; its `OnRegisterSceneNode()`
     hands itself to the batch under CTextSceneNode's exact condition, so the
     visibility rules, first-person hiding and B964's `ImpostorBatchNode`
     re-registration of withheld entities' tags all work unchanged) and
     `NameTagBatchNode` (one node in ESNRP_TRANSPARENT at camera distance 0,
     i.e. drawn after every other transparent node; sorts tags with the scene
     manager's own `TransparentNodeEntry` order + `core::array::sort`;
     projects with the same `getScreenCoordinatesFrom3DPosition()` call;
     centres exactly as `CGUIFont::draw(hcenter,vcenter)`; per-tag glyph run
     cached, rebuilt only on setText; draws through `UiQuadBatch`: first glyph
     via `draw2DImage()` (sets state), every other glyph of every tag in ONE
     `glDrawElements` = **2 draw calls per frame, any N**; a tag with a
     non-ASCII char goes to `font->draw()` in its place in the order).
     `[nametags]` line every 5 s (tags, cpu us p50/p95, us/tag, build/submit us,
     draws, quads, buffer bytes); owner row `[memledger-owner] owner=ent.nametag_batch
     category=entity_instances` (buffer capacity; trimmed at a window boundary
     when capacity > 4x the window's need).
   - `include/client/graphics/ui/bitmap_glyph_table.h` + `.cpp`: T462's glyph
     table moved out of `TextBatch` (shared, not duplicated); `TextBatch` uses it.
   - `EntityRenderer::createNameTagNode()` (`src/client/graphics/entity_name_tags.cpp`)
     is the one factory; the three `addTextSceneNode` sites
     (`entity_mesh_build.cpp` x2, `entity_build_step.cpp`) call it.
     `WILLEQ_NAMETAG_BATCH=0` = the old CTextSceneNodes (A/B switch).
   - `UiQuadBatch::bytesReserved()/trim()`.
3. **Pixel identity, fixed poses** (`nametag_drawcost pixel`, 9 poses, N = 1..200,
   near 4-46 and far 115-160, yaw 0/20/25): **0 differing pixels in every pose on
   BOTH x86 and the Pi** (`review/B975/{x86,pi}/pixel_800x600/pixel.csv`, PNGs
   in `before/` / `after/`). The one way the paths can differ: two tags at
   EXACTLY equal float distance from the camera (mirrored grid columns) whose
   text overlaps -- the scene manager's heapsort orders ties by the whole
   transparent list, the batch by the tags alone. Measured with the (empty)
   batch node left in the per-node frame's list: 0-23 px per pose
   (`differing_pixels_tie_order` column), identical on x86 and Pi.
   Also by design: a transparent node NEARER than a tag that overlaps it used to
   be drawn over the tag; now every tag is drawn after it (not exercised by the
   poses -- no other transparent nodes there).
4. **Batch cost, Pi, first version** (same csv): N=200 tags, 2183 glyph quads:
   `render()` 3.12 ms = build 0.73 ms (3.65 us/tag: sort + projection + layout +
   UiQuad emission) + submit 2.39 ms (UiQuadBatch vertex fill 144 B/quad + the 2
   draws). Whole-scene frame at N=200: per-node 44.8 ms -> batch 16.5 ms (the
   flip floor). **The bar "tag pass <= 2 ms at 200 on the Pi" is NOT met yet (3.1 ms)**;
   buffer capacity at N=200 = 791,024 B (too high, see remaining step 1).
   x86: render 2.29 ms at 200 (build 0.25 ms).
5. **Tests**: `tests/test_name_tag_batch.cpp` (4 cases, GLES2 device, self-skip
   without one): pixel identity vs CTextSceneNodes (3 poses, overlapping tags,
   all con colours, a fallback name, a tag behind the camera, a hidden tag),
   2 draws + one quad per glyph for N = 1/50/200, re-registration of a withheld
   parent's tag (B964's path), setText relayout, buffer bytes >= frame need.
   Passes on x86. Full suite on 6632933e: 5144/5146, the 2 reds
   (`DataPathRouting.EveryGles2DeviceCreatorInjectsTheShaderCacheDir`,
   `ZeroCallers`) fixed in e4eeca69 and re-run green with
   NameTagBatch/StaticUiFrameCost/TextBatch.
6. **ARM build** of e4eeca69 made locally (`scripts/build-arm-noble.sh`;
   `nametag_drawcost` added to `docker/Dockerfile.arm-noble`), published to
   `/mnt/projects/fable/review/B975/arm/e4eeca69/` (`bin/willeq-nollvm`,
   `bin/nametag_drawcost-nollvm`, config/, data/).
7. Rig changes: `scripts/b964/pi_crowd.sh` takes `--owner` and a `tagnode`
   variant (impostors on, `WILLEQ_NAMETAG_BATCH=0`);
   `scripts/b975/frametiming_table.py` folds `/frametiming` blocks (idle phase)
   + `[nametags]` + the owner row into a table (reproduces B964's 18.0 / 36.7 ms
   from `review/B964/pi/crowd800_frametiming/`).

### Remaining (numbered checklist)

1. **Make the batch fit the 2 ms bar on the Pi** (designed, not written):
   - project with ONE proj x view per frame instead of
     `getScreenCoordinatesFrom3DPosition()` per tag (1.9 us x 200 = 0.39 ms);
     the pixel tool must still show 0 differing px on x86 AND Pi (FMA
     contraction could move a rounding: if it does, keep the Irrlicht call);
   - write vertices directly (no `UiQuad` intermediate) in a compact layout
     `{float x,y; u8 r,g,b,a (GL_UNSIGNED_BYTE normalized); float u,v}` = 80 B/quad
     instead of 144; per-glyph UVs precomputed once with the driver's
     `src * (1.0f/texW)` arithmetic; a static, grown-once index pattern;
     add a `UiQuadBatch::drawCompactTexturedRun()` (first quad still through
     `draw2DImage()` for state; attribute slots 0/2/3 as `ui_quad_batch.cpp`);
     re-run `nametag_drawcost pixel` x86 + Pi (normalized-ubyte colour must
     still give 0 differing px; else keep float colour, 128 B/quad);
   - keep buffers at need (the 791 KB at N=200 is `std::vector` doubling of
     UiQuad + 144 B/quad vertices).
   Then: `cmake --build /mnt/projects/fable/wt/audit-nametags/build -j24`,
   `build/bin/test_name_tag_batch`, `build/bin/nametag_drawcost pixel|drawcost
   /mnt/projects/fable/review/B975/x86/...`, `bash scripts/build-arm-noble.sh`,
   publish (`ARM_PUBLISH_ROOT=/mnt/projects/fable/review/B975/arm ARM_PUBLISH_KEEP=50
   MESA_NOLLVM_STORE_ROOT=/mnt/projects/fable/ci-arm/mesa bash scripts/pi/publish_arm_build.sh <sha> task/B975-name-tag-batch`,
   then strip + `scripts/pi/mesa_nollvm_select.sh bin/nametag_drawcost bin/nametag_drawcost-nollvm $MESA_NOLLVM_PI_PREFIX`),
   Pi: `pi_lock_take b975`; `ssh orangepi '<dir>/bin/nametag_drawcost-nollvm pixel /tmp/b975/pixel --drm --png; ... drawcost /tmp/b975/drawcost --drm'`;
   scp back to `review/B975/pi/`; `pi_lock_release b975`. (Pi invocations used
   this session: 1 tool run + 2 trivial ssh.)
2. **Pi crowd before/after (the measurement of record)**, 800x600, the B964 recipe:
   `bash scripts/b964/pi_crowd.sh --pi-dir /mnt/projects/fable/review/B975/arm/<sha>
   --zones qeynos2,gfaydark --variants imp200far,tagnode200far,imp0,tagnode0
   -r "800 600" --frametiming --owner b975 --out /mnt/projects/fable/review/B975/pi/crowd800`
   (each variant takes/releases the eqemu LOCAL lane lock and the Pi lock
   itself; check `pi_lock_show` / `eqemu_lock_show` first -- B973 held both
   earlier today). Table: `python3 scripts/b975/frametiming_table.py
   review/B975/pi/crowd800/*/*` + `python3 scripts/b964/crowd_table.py ...`
   (memory: owned / gpu_buffers at steady_60s, the `ent.nametag_batch` row).
   `imp0` is the normal-zone case (qeynos2 safe point near the gate, gfaydark).
3. **x86 the same** (not perf evidence): `scripts/b964/crowd_run.sh` with the
   same variants (needs `EQEMU_HOST=local`, `eqemu_lock_take b975`).
4. Live check that tags still follow con colour / player colour / setText
   (entity_updates.cpp, entity_interpolation.cpp, entity_renderer.cpp call
   sites unchanged) -- a screenshot pair on the local lane with
   `WILLEQ_NAMETAG_BATCH=0` vs default, diffed per pixel (NPCs move: take it
   with a GM-frozen crowd or accept movement noise and say so).
5. Verification bar: full `ctest --test-dir build -j8`; `bash scripts/build-gles2-x86.sh`
   then `scripts/ci/visual_gate.sh` (goldens are `gles2_zone_benchmark` frames,
   which build no `EntityRenderer` -> no name tag in any golden; confirm, STOP
   if a golden pixel moves); `bash scripts/refactor/check_file_sizes.sh fable`;
   hygiene (`ctest -L hygiene`; a new number in a doc needs a
   `check_doc_counts.py` row).
6. Docs: `src/client/graphics/CLAUDE.md` (name tags section), `docs/module_map.md`
   (new TUs), `docs/future/ui_fixes.md` Chunk 2 (name tags now batched),
   `refactor/BOARD.md` (B975 entry), this README's final tables; issues for new
   findings (next B number from the max `B###:` GitLab title; B976 = #294 was
   last known).
7. Merge `gitlab/fable` before the final push; MR `Closes #293` (this draft MR
   says Refs); #293 -> `status::review`.

### Locks / stacks at hand-off

- Pi lock: taken as `b975` for the one tool run, RELEASED (`pi_lock_show` empty);
  Pi left with governor `ondemand`, `/tmp/b975` removed.
- eqemu: never taken by this unit; no stack started.
- GitLab #293 labels: `status::running` (set by this unit).

### Deviations so far

- 2 draw calls per frame, not 1: the first glyph goes through the driver so
  every GL state (program, texture units, blend, uniforms) is the driver's own;
  constant in N. One draw would need a state-only entry point in
  `docker/irrlicht-drm/COpenGLES2Driver` (not done).
- Commit trailers: the first two commits say `Claude Opus 5.5`; the hand-off
  commit says `Claude Fable 5.1` as ordered.
