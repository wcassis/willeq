# P10b — Root-cause and fix the live mergeZoneDraws SIGSEGV (qeynos2, first frame)

Approved by Will 2026-07-27 ("go ahead with both in parallel").
Slot: wt/slot1, branch task/P10b-merge-crash, cut from fable (e3ba677).
Hard blocker for the P10 preset flip. x86 client work; no Pi needed
(P12b in slot2 has Pi priority if it reaches its optional phase).

## Motivation

Anim-capture session 2 (2026-07-26): enabling mergeZoneDraws=true in
the client config SIGSEGVs 100% deterministically in
ZoneMergeBuffers::build()/textureGroupId() on the FIRST frame in
qeynos2 — real x86 GLES2 client, --constrained orangepi, 5/5 attempts,
both automatic and manual zone load. Full gdb backtrace in
/mnt/projects/fable/videos/p12_anim_check/README.txt. P10's own A/B
(befallen, config details in its review) did NOT hit this.

**Prime hypothesis (check FIRST)**: P10's reduced-landing residency
gate keyed on the WRONG mechanism. P10 concluded "regions are always
built eagerly because ConstrainedMeshCache is never constructed" — but
the orangepi preset sets deferredAssetLoading=true, which builds region
meshes PROGRESSIVELY via BackgroundThreadPool (a different mechanism
from the never-constructed cache). If ZoneMergeBuffers::build() runs at
zone load while some region meshes/materials don't exist yet (or their
buffers are later replaced by the deferred build), textureGroupId()
dereferences garbage. Befallen A/B may have escaped via config (was
deferredAssetLoading off?) or zone size/timing. Verify what P10's A/B
config actually was.

## Design

1. Reproduce under gdb (the capture branch task/anim-video-capture has
   the working harness configs — read its README/commits; do NOT merge
   that branch). Confirm the faulting object (which region/buffer/
   material; is the pointer null, dangling, or a stale pre-deferred
   placeholder?).
2. Test the prime hypothesis: repro with deferredAssetLoading=false
   (same zone/preset otherwise). Also check befallen with
   deferredAssetLoading=true (does IT crash too — was P10's A/B just
   configured differently?). Map the truth table.
3. Fix per what's found, honoring P10's design intent:
   - if deferred building is the cause: the residency gate must key on
     the ACTUAL mesh-availability mechanism (defer merge-buffer build
     until the zone's regions are genuinely all built — hook the
     deferred-build completion — or rebuild/patch merge buffers as
     regions arrive, or fall back to unmerged until complete; choose
     the smallest correct option, document why);
   - regardless: ZoneMergeBuffers::build() must be defensive (null/
     invalid buffer or texture → skip region + WARN + fall back to
     unmerged for that region or the zone; NEVER crash).
4. Re-verify: enable-at-load AND /mergedraws runtime-toggle paths, in
   BOTH qeynos2 and befallen, with deferredAssetLoading both true and
   false (6+ startup combinations, scripted; each renders N frames
   crash-free). Then one A/B pixel check per zone at a fixed pose
   (merge on vs off — FP class only, per P10 precedent).
5. Draw-count INFO line still reports sane numbers in all combinations
   (the merge must actually engage post-fix, not silently fall back
   everywhere — show the engagement evidence).

## Verification

- Standard bar: build, ctest (known host exceptions), gles2-x86 build,
  visual gate PASS goldens untouched, ARM build, file-size check.
- The step-4 matrix + step-5 engagement evidence + backtrace-level
  root-cause writeup in the Review.
- Server use: sanctioned flow, clean shutdown, configs restored.

## Out of scope

- The preset flip (Will's, after this fix + his bless).
- The capture branch's other workarounds (screenshot guard,
  ParticleManager race, item_models abort — separate deferred items).
- Full shared-VBO/eviction reconciliation; entity/object merging.

## Review

(executor fills in)
