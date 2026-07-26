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

### Setup

Branch `task/P12-animate-root-cause`, cut from `fable` at `2b2b168` (this plan's own
commit — no other commits in between), per the checkout-first instruction. Slot
`wt/slot2`. First build was the expected long incremental (`cmake --build build -j24`,
foreground, ~600s budget; completed on the first attempt, no re-run needed).

Read in full before starting: `refactor/P08d_npc_composite_landing.md` (VOID-notice
history, the orchestrator's two-experiment repro, the parked-patch origin story),
`refactor/scratch/P08d_vertexmapping_one_to_many.patch`, `animated_mesh_scene_node.cpp/.h`,
`mesh_building.cpp`, `animated_mesh_creation.cpp`, `entity_renderer.cpp`, plus (beyond the
plan's reading list, pulled in during the bisection) `s3d_loader.cpp` (`flattenSkeleton`,
`loadCharacters`, `applySkinning`, `buildAnimatedSkeleton`), `skeletal_animator.cpp`,
`geometry_combiner.cpp`, `wld_loader.cpp` (fragment 0x12 track parsing), and
`tests/test_inventory_model_view.cpp`.

### 1. Reproduction

x86 GLES2 build (`build-gles2/bin/gles2_zone_benchmark`, real GLES2/llvmpipe driver,
Xvfb `:97` for the X11/GLX display backend this build was configured with — the plain
`build/` binary uses desktop OpenGL and rendered fully black frames in this environment
regardless of `--animate`, an unrelated environment-setup difference, not investigated
further since `build-gles2` is CI's own real path and reproduces cleanly).

`qeynos2 --entities 6 --seed 42 --animate --frames 60 --warmup 5 --res 1024 768
--dump-frames 15,25` (and variants with `--orbit-radius 15` for closer framing, since
the default orbit-fallback camera never brings an entity close enough to judge, same
finding P08d hit): **reproduces immediately and unambiguously**. Entities render as
collapsed, folded blobs — recognizable only by their (correct) texture/material, not by
silhouette. The static control (identical command, no `--animate`) renders perfectly at
every frame checked, including a close, detailed barbarian (face paint, kilt, correct
proportions) — confirming geometry and texture data are fine and animation application is
what garbles the render, exactly as the P08d orchestrator's two-experiment repro found.
**Critically, the crumple is already present at frame 0** of a fresh run (before the
per-frame `OnAnimate` loop has executed more than the initial creation-time
`forceAnimationUpdate()`) — this is not a drift/accumulation bug that develops over many
frames; it is present the instant any real animation frame (not bind pose) is applied.
Evidence: `benchmarks/contact_sheets/p12_animate/` (`static_f15.png`,
`static_closeup_f35.png` vs `animate_f15.png`, `animate_f25.png`,
`animate_closeup_f35.png`, `animate_frame0_multi_race.png`).

### 2. Prime suspect: VertexMapping one-to-many — tested, eliminated

Applied `refactor/scratch/P08d_vertexmapping_one_to_many.patch` cleanly (`git apply`,
no conflicts), rebuilt `gles2_zone_benchmark`, re-ran the identical close-up animated
command (same seed, camera, frame). Result: **byte-for-byte pixel-identical** to the
pre-patch crumpled render (verified via `PIL.ImageChops.difference`: `bbox=None`,
`max diff=0`, 0 differing pixels out of 768×1024). This is a clean, apples-to-apples
before/after on THIS unit's own (correctly-matched, both-crumpled) renders — not the
garbled-vs-garbled comparison P08d's retake was forced into. **The patch is confirmed
NOT the cause of the crumple.** The underlying bug it fixes (a vertex shared across
UV-seam texture groups was silently frozen at bind pose, one-destination-only
`VertexMapping`) is real and still worth having, but it is evidently not contributing
meaningfully to this symptom — reasonable in hindsight, since duplicate-vertex seams are
a minority of any mesh's vertices and wouldn't produce whole-body collapse.
Evidence: `animate_closeup_f35_with_vertexmapping_patch_applied.png` in the contact
sheet, identical to `animate_closeup_f35.png`.

### 3. Further bisection (STOP fired — unattributed after prime suspect + 4 focused checks)

With the prime suspect eliminated, went through the plan's bisection list plus two
additional checks the evidence pointed toward. None isolated the root cause; all are
recorded here as concrete exclusions so a future unit doesn't re-walk this ground:

1. **Benchmark's synthetic `OnAnimate` driving vs production.** The benchmark drives
   entities with `OnAnimate(frame * 33.3ms)` (`gles2_zone_benchmark.cpp:1840`/`2433`,
   plus an initial `getAnimator().update(offsetMs); forceAnimationUpdate();` stagger in
   `zone_bench_loader.cpp:700-702`). Production drives the SAME `EQAnimatedMeshSceneNode`
   via Irrlicht's own `smgr_->drawAll()` (`renderer_frame.cpp:1170`), which calls
   `OnAnimate(realElapsedMs)` on every scene node every frame — same idiom, same
   `deltaMs`-based advance, same `forceAnimationUpdate()` call immediately after
   `createAnimatedNode()` (`entity_renderer.cpp:562`, identical to what
   `zone_bench_loader.cpp` does). The one benchmark-specific wrinkle found — the
   distance-sorted `--entity-budget`/`animBudget` loop in `gles2_zone_benchmark.cpp`
   skips `OnAnimate` for entities outside the per-frame animation budget — does NOT
   apply to this repro: default `--entity-budget` is 55, our test uses 6 entities, so
   every entity is animated every frame; ruled out for this specific reproduction (left
   as a known DIFFERENT latent risk: an entity that drops in and out of the top-N by
   distance would see large `deltaMs` jumps between `OnAnimate` calls — a real, separate,
   unexercised harness behavior, noted for whoever revisits `--entity-budget` semantics,
   not evaluated further here).
2. **P05-era animation-merge path** (`animated_mesh_creation.cpp:51-144`, HUF→ELF-style
   donor-skeleton animation borrowing). Read in full: it only ADDS missing
   `animationTracks` entries to the RECEIVING skeleton's OWN bones (matched by bone
   NAME, e.g. `ourBoneName` with the race-code substring swapped for the donor's), and
   never overwrites an existing track. It does not touch bone order, bone indices,
   `vertexPieces`, or vertex data in any way. Ruled out as a mechanism for vertex/bone
   index corruption. (Whether the specific races exercised use a merged animation source
   was not the deciding factor here, since the mechanism itself cannot produce this
   symptom regardless.)
3. **Per-buffer vertex-mapping population** (`mesh_building.cpp`'s
   `buildMeshFromGeometry`/`buildCompositeMeshFromGeometry`). `originalVerticesForAnimation_`
   is built by iterating `geometry->vertices` directly in original order (comment: "in
   order, for bone transforms") — confirmed by reading the code, not just the comment.
   A live diagnostic (`P12_ANIM_DIAG`, temporary, added then fully reverted — confirmed
   via `git diff` before/after) dumped, for a real repro run: `vertexPieces=18
   sumCounts=791 originalVerts=791 vertexMapping=791 boneStates=25` — full coverage, no
   gaps, no overlaps, every `boneIndex` in-bounds (`oob=0` for all 18 pieces). Ruled out
   as an incomplete/misaligned mapping.
4. **Bone-index correspondence between load-time and runtime skeleton representations.**
   Hypothesized that `SkeletonTrack::allBones` (used by `S3DLoader::applySkinning` to
   bake the STATIC bind pose, and the index space `vertexPieces[i].boneIndex` is defined
   against) might not correspond 1:1 with `CharacterSkeleton::bones` (used by
   `SkeletalAnimator`/`computeBoneTransforms` at runtime). Read `buildAnimatedSkeleton`
   (`s3d_loader.cpp:1152-1200`): it does `animSkel->bones.resize(skeleton->allBones.size())`
   and copies index-for-index (`for i in 0..allBones.size()`, `parentIndex =
   skeleton->parentIndices[i]` directly) — no reordering, no filtering. Ruled out.
5. **Raw-vs-skinned geometry separation** (`S3DLoader::loadCharacters`,
   `applySkinning`). Confirmed `character->rawParts` are deep-copied from `geom` BEFORE
   `applySkinning()` mutates `character->parts`/`partsWithTransforms` in place with the
   bind-pose bone transform — the intended, correct separation (comment: "Deep copy
   geometry for rawParts BEFORE skinning is applied"). Confirmed
   `getAnimatedMeshForRace` (`animated_mesh_creation.cpp:207`) calls
   `buildMeshFromGeometry(modelData->rawGeometry, ...)` — the correct (unskinned) source
   — immediately followed by `setOriginalVertices`/`setVertexMapping` reading the
   just-populated shared members, with no intervening call that could clobber them with
   `combinedGeometry`-derived data for this repro's call sequence. Ruled out (for this
   call path; the two builder functions sharing mutable member state
   (`originalVerticesForAnimation_`/`vertexMappingForAnimation_`) as scratch space is
   fragile and worth flagging as a code-hygiene risk for a future refactor, but not
   demonstrated to misfire here).
6. **Matrix composition convention.** Compared `s3d_loader.cpp`'s load-time `Mat4`
   (used by `applySkinning` to bake the bind pose) against `skeletal_animator.h`'s
   runtime `BoneMat4` (used by `computeBoneTransforms`): both are column-major, both
   compose `world[i] = world[parent] * local[i]` with `local = T*R*S`, both use the
   identical scalar formula for `operator*`. Ruled out.
7. **Per-bone scale/translation sanity.** A second temporary diagnostic
   (`P12_SCALE_DIAG`, also fully reverted) dumped `BoneTransform.scale`/`shift` for every
   bone during an actual `o01` animation (not just bind pose): scale values ranged
   0.81–1.11 across all 25 bones (HUM) — not degenerate, not compounding toward zero or
   exploding, and translations were plausible humanoid proportions (feet near z=-2.9,
   head near z=+2.9, ~5.8-unit total height). This also surfaced a separate, real, minor
   parsing question in `wld_loader.cpp:1717/1746` (`parseFragment12`): `shiftDenom` is
   used both to compute the per-frame `scale` field (`shiftDenom/256`) AND is NOT used to
   divide `shiftX/Y/Z` (which are hardcoded `/256.0f` instead) — per the classic EQ WLD
   track-frame format `shiftDenom` is documented elsewhere as the divisor for the shift
   values, not a semantically distinct "bone scale" field. This looks like a plausible
   format misreading, but the diagnostic showed the actual resulting numbers stay
   near 1.0 in practice (this model's `shiftDenom` values apparently cluster close to
   256), so it does NOT explain the severe, whole-body crumple observed — flagged as a
   separate, smaller, unresolved question for whoever next touches WLD track parsing,
   not adopted as the root cause here.
8. **Raw vertex bounding-box sanity** (a third temporary diagnostic, also reverted):
   per-piece raw (pre-bone-transform) vertex bounding boxes were mostly small
   (~0.1-1.0 unit spans, consistent with genuine bone-local mesh data) but at least one
   piece spanned ~2.1 units in X — wider than the entire skeleton's arm-to-arm span
   (~1.1 units from bone translations) — which is at minimum surprising for a single
   rigid vertex-to-bone group, though not conclusively wrong (that piece could
   legitimately be a large torso/chest group). Not resolved within this unit's budget;
   flagged as the most promising lead for a follow-up unit (see below).

**STOP fired per the plan's own condition**: prime suspect + 2-3 focused bisections
without attribution → document narrowed evidence and stop. This unit's bisection went
one bisection past that minimum out of caution and still did not land on a single
mechanism. All temporary diagnostics (`P12_ANIM_DIAG` in
`animated_mesh_scene_node.cpp`, `P12_SCALE_DIAG` in `skeletal_animator.cpp`) were
removed before this commit — confirmed via `git diff`/`git status` showing zero net
change to either file.

**Narrowed failing case**: any humanoid race built through
`RaceModelLoader::loadModelFromGlobalChr`'s multi-part fallback path (the
`character->rawParts`-populating branch in `S3DLoader::loadCharacters`), animated via
`EQAnimatedMesh`'s multi-buffer (`useMultiBuffer`) vertex path in
`applyBoneTransformsToMeshImpl` — reproduced on race 1 (HUM) and at least one other race
(a barbarian-textured entity, visually confirmed) among the benchmark's default 12-race
roster. **Excluded** as the mechanism: VertexMapping duplicate-vertex freezing (prime
suspect), entity-animation-budget skipping (not active at this entity count), P05-era
animation-track merging (name-keyed, doesn't touch indices), bone-index correspondence
between `SkeletonTrack` and `CharacterSkeleton` (verified 1:1), raw/skinned geometry
separation order (verified correct), matrix composition convention (verified identical),
and gross scale/translation corruption (values plausible, not degenerate). **Not yet
excluded / best remaining lead for a follow-up unit**: the per-piece raw vertex bbox
outlier noted in item 8 above, and more generally a closer per-vertex (not just
per-piece-bbox or per-bone-transform) comparison of `rawGeometry` vertex positions
against `combinedGeometry`'s corresponding (bind-pose-baked) positions for the SAME
original vertex index, to directly test whether `applySkinning`'s bind-pose transform
and the runtime bind-pose (frame 0 pose track) reconstruction actually agree
vertex-for-vertex — the checks in this unit compared bone-level and piece-bbox-level
aggregates, which could mask a real per-vertex disagreement.

### 4. Ownership verdict

**Shared production code, not benchmark-harness-only.** Every mechanism this unit
exercised (`RaceModelLoader::getAnimatedMeshForRace`, `EQAnimatedMesh`,
`applyBoneTransformsToMeshImpl`, `SkeletalAnimator`, `S3DLoader::loadCharacters`/
`applySkinning`) is the exact same code production's `EntityRenderer` uses
(`entity_renderer.cpp:531-562` calls `createAnimatedNode`/`createAnimatedNodeWithEquipment`
then `forceAnimationUpdate()`, identical to the benchmark's own sequence), driven by the
same `OnAnimate`-via-`drawAll()` idiom every frame. Nothing found in this unit's
bisection is benchmark-specific (the one genuinely benchmark-only wrinkle, the
`--entity-budget` animation-skip, is provably inactive at this repro's entity count).

**Whether this is a live, currently-unnoticed production defect, or masked in practice,
could not be fully settled** — but the evidence leans toward "real and simply
unverified," not "masked by a different production code path": `tests/test_inventory_model_view.cpp`'s
`ModelAnimatesProperly` test — the one automated test whose name suggests it would catch
exactly this class of bug on a production-adjacent path (the inventory paperdoll model
viewer, which uses the same `EQAnimatedMeshSceneNode`) — is unconditionally
`GTEST_SKIP()`'d ("WindowManager has been deleted - test disabled"), and has been for
some time (unrelated to this unit). No other test in the suite renders and visually or
numerically verifies an animated character's geometry. Combined with the P08d VOID
notice's own finding ("nobody ever visually validated `gles2_zone_benchmark --animate`'s
output before"), the honest picture is: **there is currently no verification anywhere in
this codebase — automated or manual — that would have caught this if it were also
happening in live gameplay.** "Production live play looks fine" is institutional belief,
not a checked fact. This unit does not have the budget to run a live-client play session
to check it directly (that step, plus a `model_viewer`-based interactive check, is the
natural next step for whoever picks up the remaining lead in section 3).

### 5. Parked patch disposition

`refactor/scratch/P08d_vertexmapping_one_to_many.patch` is **left parked, unapplied**
(same disposition P08d's static re-retake left it in). Reasons, updated: this unit
directly tested it against a correctly-matched before/after (unlike P08d's
garbled-vs-garbled comparison) and confirmed it makes **zero rendered difference** to
this crumple. It remains a real, worth-having correctness fix for UV-seam vertex
duplication (independently reasoned, not re-litigated here), just not relevant to this
bug. No changes made to the patch file or to `refactor/P08d_npc_composite_landing.md`
(the P08d review already points here for the deferred animation root-cause; this Review
section is the promised follow-up).

**P12b update (2026-07-26)**: the actual crumple fix landed in P12b
(`refactor/P12b_benchmark_node_wiring.md`) — a benchmark-only node-construction/wiring
bug (a stale GLES2 static hardware-buffer snapshot), not `VertexMapping`. With animation
now genuinely trustworthy, P12b re-tested this patch against real coherent poses and
confirmed the same "zero visible difference" verdict found here — and, since that
confirmation is now on a valid (not garbled-vs-garbled) comparison and the patch is a real
correctness fix with no regressions, P12b **landed** it. This patch file is now applied to
the tree; it is kept here for history only.

### Verification

- `cmake --build build -j24`: clean (working tree has zero net diff vs `fable` — no
  shared-code fix to land this unit, see STOP above).
- `ctest --output-on-failure` (`build/`): 1222/1225 passed. Failures:
  `AudioManagerTest.PreloadCommonSounds` (documented host exception) and `visual_gate`
  (see below); `RDPServerFrameTest.UpdateFrameMultipleTimes` (the other documented
  exception) passed this run. `HotkeyJsonLoadTest.WarnsOnInvalidModifier` failed once
  under `-j4` parallel execution but passed cleanly when re-run in isolation — a
  pre-existing parallel-execution flake unrelated to this unit (zero source changes),
  not investigated further.
- `visual_gate` (`build-gles2/`): **FAILED** — `qeynos2_f0` (9048 differing px) and
  `gfaydark_f0` (40309 differing px), both a small isolated foliage/thatch-object patch
  per the diff image, not an entity-rendering difference (the gate renders no entities
  per its own design). Reproduced on a freshly clean-rebuilt `gles2_zone_benchmark`
  (confirmed zero source diff at rebuild time) with `DISPLAY` explicitly set — ruling out
  leftover diagnostic-instrumented binaries or a missing-`DISPLAY` artifact from this
  session's own Xvfb use. This is a **pre-existing environment/host state this unit did
  not cause** (git tree has zero changes) and did not have budget to root-cause (out of
  scope for an animation unit; the failing geometry is static zone foliage, not
  entities) — flagged here rather than silently ignored. `befallen` passed.
- `scripts/build-gles2-x86.sh`: binary rebuilt clean multiple times during the
  bisection (diagnostics added/removed); final state matches a clean checkout of `fable`.
- ARM cross-build: not re-run this unit (zero source changes to cross-compile;
  `build-arm-noble/` from a prior unit stands unmodified).
- File-size check: not applicable — no files changed.
- Static byte-identity: not applicable in the "before/after a fix" sense (no fix
  landed), but directly demonstrated as part of the bisection itself: static renders are
  pixel-perfect at every frame checked, both before and after the (ultimately-not-adopted)
  VertexMapping patch was applied and reverted.
- Animated evidence set: gathered per plan design 4's spirit (coherent-pose validation),
  but since no fix exists, what's captured is deliberately the FAILURE evidence, not a
  passing validation set — see `benchmarks/contact_sheets/p12_animate/`.
- Composite-vs-multi-buffer-under-animation (the deferred P08d Z35-class deliverable):
  **not attempted** — doing so before the animation path itself renders coherently would
  produce exactly the kind of void evidence P08d's VOID notice already burned once;
  explicitly deferred again, to whichever unit lands a real fix.

### Deviations from the plan

- No shared-code fix landed — the plan's STOP condition fired after the prime suspect
  plus more bisection than the minimum 2-3 (5 additional checks, see section 3), still
  without full attribution. Per the plan's own STOP instructions, stopped and documented
  narrowed evidence (failing race/vertex-class + exclusions) rather than continuing
  indefinitely or landing a guess.
- Build environment: used `build-gles2/` (the real GLES2/llvmpipe path, X11/GLX display
  backend under a session-local Xvfb on `:97`) rather than the plain `build/` binary,
  which rendered fully black frames for both static and animated control in this
  environment (an unrelated, pre-existing environment/driver-setup difference between
  the two build configurations, not investigated further since `build-gles2` is CI's own
  real verification path and the one `visual_gate`/P08d's own evidence already relies
  on).
- `visual_gate` failed on this run (foliage-geometry pixel diff, unrelated to entities)
  — reported in Verification above as a pre-existing, unattributed environment
  condition (zero source diff at the time it was observed); not fixed, since root-causing
  it is out of scope for an animation-focused unit and the failing geometry isn't
  entity-related.
- Three temporary diagnostics were added and fully removed before this commit
  (`P12_ANIM_DIAG`/raw-vertex-bbox dump in `animated_mesh_scene_node.cpp`,
  `P12_SCALE_DIAG` in `skeletal_animator.cpp`) — confirmed zero net diff via
  `git status`/`git diff` immediately before finalizing.
- No `--entities`/`tools/hwcaps/gles2_zone_benchmark.cpp`/`zone_bench_loader.cpp` edits
  were made or left in place — all investigation used temporary, fully-reverted
  diagnostics in the animation TUs, per the plan's P11-parallel-safety instruction.
  Nothing to reconcile with slot1 at merge time.
