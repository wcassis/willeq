# B975 -- entity name tags in one batch

GitLab #293 (class B, perf, lane pi). Branch `task/B975-name-tag-batch`
(worktree `/mnt/projects/fable/wt/audit-nametags`), based on `gitlab/fable`
d3375b65 (B964 / MR !70 merged). Raw evidence: `/mnt/projects/fable/review/B975/`.

## STATE AT HAND-OFF 2026-10-01

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
