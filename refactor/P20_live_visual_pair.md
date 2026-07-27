# P20 — Live-client visual pair: foliage alpha + geometry pop-in

**Slot: slot1. OWNS EQEMU (sole owner — no sibling agents this dispatch).
x86 only; do NOT touch the Pi.**

Two production-visible defects Will identified with his own eyes in the
live-client gameplay videos (llvmpipe capture sessions, qeynos2/qeynos
gates area, 2026-07-27 video review):

(a) **Tree foliage alpha is OPAQUE in the live client** — solid dark
    quads where masked leaf textures should be transparent. NOTE: the
    H02c/H02d arc already fixed this class in the GLES2 *benchmark/golden*
    path (alpha-source-aware shader selection, Option A) and gfaydark
    goldens render foliage correctly today — so this is a divergence
    between the live-client zone path and the (fixed) golden path.

(b) **World geometry sections missing, then appearing as the camera
    moves.** Standing hypotheses (from the review, in order):
    (1) `deferredAssetLoading` progressive build working as designed —
    i.e. cosmetic-by-design, verdict is a report, or (2) a real
    culling/BSP/PVS/occlusion bug. Distinguish them with evidence.

Both share one reproduction harness (a live session against eqemu), which
is why they are one unit. They are otherwise independent investigations —
do not force a shared root cause.

## Background (trusted prior findings — do not re-derive)

- H02d landed alpha-source-aware alpha-test shaders: RGBA/DDS-pipeline
  textures read their OWN alpha (`uTexture.a`); the companion
  `uAlphaTexture` (unit 1) variant is for ETC1 only. The GLES2 driver has
  an opaque-white 1×1 unit-1 fallback (COpenGLES2Driver ~1090-1101) —
  masked materials that end up on the companion variant WITHOUT a real
  companion render solid. Three zone_geometry.cpp sites were fixed to
  select the correct variant. Shader debug mode 12 (`companionAlpha`)
  exists; `/shaderdebug` in the live client can visualize alpha sources.
- `citywlivy.bmp` (qeynos ivy) is WLD-flagged masked but painted opaque —
  renders solid BY DESIGN under Option A. Not a bug; don't chase it.
- The video capture sessions' tooling and workarounds live UNMERGED on
  branch `task/anim-video-capture` (consult READ-ONLY for session
  scripting, camera placement at the qeynos gates, and the known
  live-client crash workarounds: befallen first-frame SIGSEGV,
  item_models FATAL, screenshot guard, ParticleManager race). Videos +
  README: `/mnt/projects/fable/videos/p12_anim_check/`. Do NOT merge or
  cherry-pick that branch's workarounds into your commits; if one of
  those crashes blocks you, apply the minimal workaround LOCALLY
  (uncommitted) and say so in the Review.
- P17 (already on fable) made zone unload synchronous and fixed the
  snapshot race — the live client you build from fable tip includes it.
- First, determine WHICH renderer path the capture videos used (check
  the videos README / capture branch scripts: desktop-GL build under
  Xvfb vs GLES2-x86 surfaceless). Reproduce in THAT path first; then
  check whether the other path shares the defect. Fixes must be correct
  for the path(s) actually shipped: GLES2 is the Pi production path;
  desktop-GL x86 is the dev/live-review path — a divergence between
  them is itself a finding to document.

## Part A — foliage alpha (root-cause and fix)

1. Reproduce: live session, camera at trees (qeynos hills side of the
   gates has trees in frame per the gates videos; gfaydark is the
   richest foliage zone if reachable via GM #zone — Testchar is GM).
   Capture stills showing the opaque quads. If the live client renders
   foliage CORRECTLY in your session, STOP Part A and report — the
   defect may have been environment- or build-specific (cross-ref the
   P12-era zero-source-diff foliage gate mismatch on slot2, deferred.md
   ~line 201).
2. Diagnose which stage loses the transparency on the LIVE zone path
   (not the benchmark loader — that one is fixed). Suspects in order:
   (a) live zone-load material classification (mesh_building.cpp /
   zone_geometry.cpp routes the live path takes vs the benchmark
   loader's) — is the masked WLD flag mapped to the alpha-test material
   at all on this route; (b) alpha-source variant selection on the live
   route (an H02d-class site the fix didn't cover — e.g. the deferred /
   progressive asset path building materials before alpha data exists);
   (c) texture decode dropping alpha on the live route; (d) desktop-GL
   only: EMT_TRANSPARENT_ALPHA_CHANNEL_REF mapping on the live path.
   Use shader debug modes (7=alpha, 11=rawTexAlpha, 12=companionAlpha)
   before writing any fix.
3. Fix at the true root cause. Before/after stills from the SAME camera
   pose, saved under `benchmarks/audits/p20_live_visual/`.
4. The x86 GLES2 goldens must be UNTOUCHED (visual gate PASS,
   no re-bless). If your fix moves any golden pixel, STOP and report —
   golden changes require Will's re-bless, which is the orchestrator's
   to arrange, not yours to accept.

## Part B — geometry pop-in (diagnose; fix only if it's a real bug)

1. Reproduce: scripted camera movement through the area where the
   videos showed it (approach to the qeynos gates). Capture a frame
   sequence (every-frame stills or video per the capture branch's
   method) demonstrating the missing→appearing behavior.
2. Distinguish the hypotheses with a controlled experiment, e.g.:
   run the same camera path with `deferredAssetLoading` disabled (find
   the actual config/flag name — verify it exists on the live path) —
   if pop-in vanishes, it's the progressive build; if it persists, it's
   culling/BSP/PVS/occlusion. Also check whether the popped-in sections
   correspond to BSP regions (PVS culling granularity) vs individual
   meshes/assets (loader granularity) — the SHAPE of what's missing is
   diagnostic.
3. If progressive-build-by-design: REPORT-ONLY verdict — document the
   mechanism, measure how long sections stay missing at gameplay move
   speed, and propose (don't implement) mitigation options. If a real
   culling bug: root-cause, fix, before/after frame sequences, goldens
   untouched (same STOP rule as Part A step 4).
4. Note for your experiment design: the videos were captured at ~7fps
   under llvmpipe + recording overhead — some "pop-in" may be loading
   that would be invisible at real frame rates. Quantify in frames AND
   wall-clock time; call this out explicitly in the verdict.

## Rules

- eqemu per EXECUTOR_PROTOCOL.md: sanctioned start/stop only, never
  modify server code, restore Testchar to the documented befallen
  coordinates and shut the server down cleanly when finished.
- Territory: `src/client/graphics/**`, `docker/irrlicht-drm*/**` (driver,
  only if the root cause truly lives there), capture/audit scripts under
  `benchmarks/audits/p20_live_visual/`. Server code and `tools/hwcaps/`
  are off-limits. Do not touch goldens.
- Evidence discipline: every claim in the Review needs an artifact
  (still, frame sequence, log) committed under
  `benchmarks/audits/p20_live_visual/`. Keep videos OUT of git — write
  them to `/mnt/projects/fable/videos/p20_live_visual/` and reference
  paths. You do NOT declare visual acceptability — Will does; your job
  is faithful evidence.
- STOP conditions: (i) either defect does not reproduce (report what you
  observed instead); (ii) a fix would require touching goldens or server
  code; (iii) the parked live-client crashes block a session even with
  local workarounds; (iv) Part A root cause lands in the GLES2 driver's
  texture/state caching — that collides with P18's open wrap-param
  investigation; document the overlap and stop rather than half-fixing.

## Verification

- Full 6-step bar (EXECUTOR_PROTOCOL.md) for any production code change:
  build, ctest (known exceptions: AudioManagerTest.PreloadCommonSounds,
  RDPServerFrameTest.UpdateFrameMultipleTimes), gles2-x86 build, visual
  gate PASS with goldens untouched, arm-noble build, file sizes.
- Part A: before/after stills, same pose, foliage transparent after.
- Part B: verdict backed by the controlled experiment + frame sequences.

## Review

(executor fills in: per-part root cause with evidence, fix locations,
before/after evidence paths, pop-in verdict + quantification, deviations)
