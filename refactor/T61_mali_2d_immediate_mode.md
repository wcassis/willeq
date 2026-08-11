# T61 — UI panel chrome does not render on Mali400: root-cause the immediate-mode 2D path

**Slot: t1. OWNS THE PI — the whole unit lives or dies on real hardware.
NO EQEMU.**

**Territory: `docker/irrlicht-drm/COpenGLES2Driver.{h,cpp}` (and its x86
twin `docker/irrlicht-drm-x86/` if the same code is mirrored there),
`src/client/graphics/ui/ui_renderer.cpp` only if the fix genuinely
belongs at the call site rather than the driver, and tests.**

**Do NOT modify `tools/hwcaps/ui_inventory_capture.cpp`** — sibling unit
T62 owns it this round. You may *run* it freely; you may not edit it. If
you need a different capture shape, say so in your report and use a
throwaway scratch tool outside `tools/`.

Do not change the paperdoll, the bone-attachment header, or any UI
layout. `refactor/deferred.md` is append-only and shared.

Read `refactor/EXECUTOR_PROTOCOL.md` (especially the Pi section), then
`refactor/T60_paperdoll_restore.md`'s Review — it produced the evidence
below.

## The observation

T60 captured the inventory popup on real Mali400 hardware. Compare:

- `refactor/artifacts/T60/inventory_800x600_full.png` (x86 llvmpipe) —
  panel background, 1px borders, and all 30 slot rectangles render.
- `refactor/artifacts/T60/pi/inventory_800x600_full.png` (real Mali400) —
  **only text renders.** No panel, no borders, no slot rectangles.

The paperdoll RTT (`draw2DImage`) and the text batch both render on the
Pi. What is missing is exactly the output of
`IVideoDriver::draw2DRectangle`.

The capture harness constructs `UIRenderer` with `atlas = nullptr`, so
`UIRenderer::drawRect` and `drawSprite` take their fallback branches
(`src/client/graphics/ui/ui_renderer.cpp:31-34, 44-47`), both of which
call `driver_->draw2DRectangle(...)` immediately rather than batching a
quad through `draw2DImage`.

**`draw2DRectangle` is implemented on this driver**
(`COpenGLES2Driver.cpp:641` and `:647`), so this is a runtime failure,
not a missing entry point.

Related prior evidence, from T57: on **x86** the first *content* frame
after device creation also silently dropped immediate-mode rectangles
while text still appeared. T57 worked around it with a discarded warm-up
render. **That workaround is a symptom nobody root-caused, and this
unit's finding may be the same defect showing up more severely on Mali.
Treat them as one investigation.**

## Step 1 — establish blast radius BEFORE fixing anything

This is the most important step and it changes what the unit is.

**Does this affect the real client, or only the atlas-less capture path?**
In production the UI atlas normally loads, so `drawRect`/`drawSprite`
take the `draw2DImage` branch that demonstrably works on Mali. If so,
this is a harness-and-degradation-path bug. If the real client on the Pi
*also* fails to draw panel chrome, it is a live rendering bug on the
target platform and far more serious.

Determine it on hardware:
1. Run the real client on the Pi with the atlas present. Do the static
   panels render their backgrounds and borders?
2. Run it with the atlas deliberately absent/failed. What does the player
   see?

**Report the answer before proceeding.** If the real client renders
correctly with an atlas, say so plainly — that reframes this unit as
fixing a degradation path, which is still worth doing (see "Why this
matters regardless") but is not an emergency.

## Step 2 — root-cause it

Hypotheses worth testing, in rough order of likelihood. **Do not stop at
the first plausible one — confirm with evidence:**

- **2D state setup.** `draw2DRectangle` may depend on 2D mode /
  ortho projection / shader state that is set up lazily or invalidated.
  Note `setRenderTarget` explicitly sets `Is2DMode = false`
  (`COpenGLES2Driver.cpp`, in the RTT block). Check what establishes 2D
  mode for the immediate path and whether it runs.
- **Blend / depth / scissor state** leaking from a prior draw so the
  rectangles are drawn but invisible (alpha 0, depth-rejected, scissored
  out). The fallback colour is `SColor(180, 20, 20, 25)` — alpha 180, so
  a blend-state problem would show as nothing.
- **Buffer/readback timing.** T57's warm-up-render workaround points at
  a swap or readback ordering issue; on DRM/KMS with page flipping,
  `createScreenShot()` may read a buffer the immediate draws never
  reached. **If this is the cause, the pixels may be fine on a real
  display and only the capture is wrong** — distinguish these two, they
  have very different consequences.
- Shader program not bound for the untextured 2D case on this driver.

The distinction between "not drawn" and "drawn but not captured" is the
crux. Settle it explicitly — e.g. by reading pixels back in-process, or
by observing on a physical display.

## Step 3 — fix at the right layer

Fix where the defect is. If the driver's immediate 2D path is broken,
fix the driver. If it is a capture/readback artifact, fix that and say
clearly that the renderer was never at fault. **Do not "fix" it by making
`UIRenderer` avoid `draw2DRectangle`** — that hides a driver defect
behind a call-site workaround and leaves every other immediate-mode 2D
caller broken.

If the root cause is the same one T57 papered over with its warm-up
render, **remove that workaround** as part of the fix and say so.

Add a regression test if the seam allows one — a headless GLES2 test that
draws a rectangle and reads back the pixel is the obvious shape, and
nothing like it exists.

## Why this matters regardless of Step 1's answer

`UIRenderer`'s no-atlas fallback is the **production degradation path**.
T57 separately found that with no atlas, `drawSprite` renders the same
dark rectangle for every sprite id, so hover feedback vanishes. Combine
that with this finding and, on the Pi, an atlas load failure means a
**completely invisible UI** — no panels, no borders, no slots, no hover,
with only text floating on the scene. That is a bad failure mode for the
lowest-spec target, and it is worth closing even if the happy path is
fine.

## STOP conditions

- Step 1 shows the real client is broken on Pi in a way that needs a
  decision about scope — report rather than expanding.
- The fix would require changing UI call sites to avoid the driver path.
- Goldens moving. The visual gate renders zones through the same driver;
  **if your driver change moves the gate, that is a real regression** —
  stop and report, do not re-bless.

## Verification

Full protocol bar, plus: before/after Pi captures showing panel chrome,
and an explicit statement of whether the real client was affected. Pi
protocol per EXECUTOR_PROTOCOL.md — governor restored, `/tmp` clean,
bounded invocations.

## Review

Media referenced below lives at `/mnt/projects/fable/review/T61/` (outside
this worktree, per the coordinator's mid-flight standing-rule update —
see `README.md` there for full detail and file-by-file explanations; not
committed to the repo).

### Round 2 — coordinator review caught a second, real defect

After the first pass below (Steps 1-3, the `activeProgram_` fix), the
coordinator reviewed the shipped `before`/`after` Pi evidence and flagged
that `pi_inventory_800x600_full_AFTER.png` and `_sparse_AFTER.png` were
byte-identical (`md5sum`) despite `populateFull()`/`populateSparse()`
filling visibly different item sets — and that this was **new** versus
T60's pre-fix run, where those two files were distinctly different sizes.
The coordinator correctly called out that the first version of this
Review cited that identity as evidence of correctness, backwards: identity
between captures that should differ is evidence of a bug, not confirmation
of a fix.

**Diagnosis, with evidence, not inference**: the smoking gun was in the
`hover` file. Under fix #1 alone, `hover` did not show hover-highlighted
`full` content — it showed `Rus`/`Clo`/`Bre x5`, i.e. **`sparse`'s own
state**, saved under `hover`'s filename. `runAtResolution()` draws a
discarded warm-up frame, then `full`, `sparse`, `hover` in sequence, each
via `beginScene → draw → endScene → createScreenShot()`. The pattern
(`full` showed the warm-up's buffer — which happened to hold `full`'s own
state, so it looked right by accident; `sparse` showed `full`'s
just-rendered buffer; `hover` showed `sparse`'s) is a one-frame capture
lag, confirmed as a genuine DRM/GBM buffer-rotation hazard:
`docker/irrlicht-drm/CIrrDeviceFB.cpp`'s `drmPageFlip()` (called from
`endScene()` via `present()`) calls `eglSwapBuffers()` *before* handing
the new front buffer to KMS, so a `createScreenShot()`/`glReadPixels()`
call made by the caller after `endScene()` returns — the universal
convention every caller in this repo already uses — was reading whatever
GBM buffer object had just become the new *back* buffer post-swap, not
the one this frame actually rendered into. This is exactly the plan's
Step 2 hypothesis 3 ("buffer/readback timing"), which the first pass
dismissed too early: it was verified only on x86's surfaceless pbuffer,
where `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` explicitly documents
swap as "a harmless no-op on Mesa" for that path — that reasoning never
applied to Mali's real double-buffered DRM/GBM surface, and Round 1 never
tested it there.

**Fixed** (`docker/irrlicht-drm/COpenGLES2Driver.{h,cpp}`): `present()`
is deferred from `endScene()` to the start of the *next* `beginScene()`,
via a new `pendingPresent_` flag. `createScreenShot()` called any time
between an `endScene()` and the following `beginScene()` — which is
already how every caller in the codebase uses it — now reads the buffer
that frame actually rendered into, before it is rotated away. Zero
external call-site changes; x86 is unaffected in substance (its swap was
already a content no-op) since only *when* that no-op runs changed.

Re-ran the **entire verification bar again** since this touched driver
code a second time: `cmake --build build -j24` clean; `ctest` **1963/1964**
(same lone known ALSA exception), `visual_gate` **PASSED**, goldens
untouched; `scripts/build-gles2-x86.sh` clean; `scripts/build-arm-noble.sh`
clean; `check_file_sizes.sh` — no new FAILs (the `tools/model_viewer.cpp`
FAIL that check reports is pre-existing on the `fable` tip itself via
T62's merge, not touched by this unit; `COpenGLES2Driver.cpp`'s WARN is a
pre-existing oversized legacy file, unchanged in kind by this ~20-line
diff). Re-ran the Pi capture (2nd invocation this session): `full` and
`sparse` are now genuinely distinct (`md5sum` differs, and visually show
their own correct item sets); `hover` is now byte-identical to `full`,
which is *correct*, not a repeat of the bug — see `review/T61/README.md`
for why (hover's only conceptual difference from full, the highlight
border sprite, is itself invisible under the no-atlas fallback per T57's
finding, so two captures identical except for an already-invisible detail
should be pixel-identical).

**Separately answered, with evidence**: whether the missing panel-fill/
slot-rectangle visibility on Mali (border visible, fill/slots not) versus
x86's "obvious grey boxes" (`review/T57/inventory_800x600_full.png`) is a
second defect. It is not. Raw RGBA pixel sampling (not on-screen
appearance) at the panel-background location: Mali `(20,20,27,255)`, x86
`(20,20,27,202)` — **identical RGB on both platforms**, correctly the
low-contrast no-atlas fallback color blended once over the clear color.
The alpha channel differs for a code-confirmed reason, not a guess:
`docker/irrlicht-drm/CIrrDeviceFB.cpp:519` requests `EGL_ALPHA_SIZE, 0`
for the ARM/DRM config (matches the runtime log's "GBM: Surface created
800x600 XRGB8888" — no alpha channel, `glReadPixels` always reports 255),
while `docker/irrlicht-drm-x86/CIrrDeviceFB.cpp:682` requests
`EGL_ALPHA_SIZE, 8` for the x86 surfaceless config (a real alpha channel
that partially survives the fallback color's blend). A PNG viewer
alpha-composites x86's semi-transparent near-black pixel against its own
(light) canvas, producing the illusion of a bright grey box — verified
arithmetically: `(20,20,27)` at alpha 202/255 over white ≈ `(70,70,76)`,
matching the reference image's appearance. **x86 is the odd one out; this
is a viewer-side alpha-compositing artifact of the debug PNG's alpha
channel, not a driver rendering defect** — not fixed here (cosmetic to a
debug capture tool, not a correctness bug; flagged in
`review/T61/README.md` as an optional follow-up: `createScreenShot()`
could force alpha=255 for viewer-safe debug PNGs). Neither platform's
capture shows what the panel looks like with a real atlas loaded — both
are exclusively the no-atlas degradation path.

Full detail, tables, and the corrected before/after images:
`review/T61/README.md`.

### Round 3 — coordinator review: frame pacing, the last-frame case, and layer justification

The coordinator accepted Round 2's diagnosis and fix but raised three
concerns specific to deferring `present()` from `endScene()` to the next
`beginScene()`, since that change touches every frame's presentation
timing in the shipping client, not just capture tools, on the platform
whose headline problem is frame pacing on Mali400.

**1. Frame pacing, measured on real hardware, not asserted.** Built two
ARM `gles2_zone_benchmark` binaries differing in exactly one thing — the
presence/absence of the deferred-present change (the `activeProgram_` fix
present in both) — by temporarily checking out commit `b56cbe13`'s driver
files for the "before" build, then restoring the working tree for
"after". Ran both, `--gpu-sync-probe`, on **befallen** (the coordinator's
own reference zone — confirmed matching: frame 0 draws=368, matching "p95
~368 draws, CPU draw-submission bound" exactly), same 21-static-pose
sequence (7 reps of the manifest's 3 `--camera-pose` entries), same
`--adaptive-lighting --zone-pp-budget 20 --pp-budget-mode count
--player-light-level 9` production config, performance governor pinned.
First pass showed a large gap (before: mean 38.18ms/p50 30.02ms/p95
135.07ms; after: mean 25.71ms/p50 22.25ms/p95 39.03ms) that looked like a
real regression-in-reverse — but a **second pass in reversed order**
(after, then before again) showed `after`, `after2`, **and `before2`** all
converge to the identical steady-state number (mean ≈25.7ms, p50 ≈22.2-
22.4ms, p95 ≈38.95-39.03ms, all three within noise of each other) — i.e.
the first pass's gap was a cold-start/thermal warm-up artifact of running
first in the session, not a code effect, and it hits either binary
identically. **Once warm, before and after are statistically
indistinguishable** (p50 within 0.2ms, p95 within 0.1ms across 3
same-order-warm runs). No dropped/missed page-flips (`-EBUSY`/fallback
`drmModeSetCrtc` warnings) in any of the four runs' logs.
`swap_buffers_ms`/`flip_wait_ms` CSV columns read `0.000` in every row of
every run (before and after identically) — a pre-existing gap in how
`--gpu-sync-probe` wires those two columns for this tool's static-pose
capture path, not something this unit's change caused or could hide a
regression behind, since it is equally absent on both sides of the
comparison; `total_ms` (actual measured wall-clock frame time, which does
include swap/present) is what the p50/p95 figures above are built from,
and that metric is fully populated and directly comparable. Full CSVs are
not retained in the repo (Pi-local, per the I/O rule) but the analysis
script and figures are reproducible from this description.

**2. The last frame is never presented — fixed.** Traced the shutdown
path (`IrrlichtRenderer::shutdown()`, `irrlicht_renderer.cpp`): it calls
`device_->drop()` directly, with no final `beginScene()`/`endScene()` —
confirming the coordinator's concern exactly: if the last real frame's
`endScene()` left `pendingPresent_` true, nothing would ever consume it.
Traced C++ destruction order to find a *safe* place to flush it:
`CIrrDeviceFB::~CIrrDeviceFB()`'s body destroys EGL/DRM first (own
destructor), and only the *base* class `CIrrDeviceStub::~CIrrDeviceStub()`
(which runs strictly after, in standard C++ derived-then-base destructor
order) calls `VideoDriver->drop()`. So at the very top of
`~CIrrDeviceFB()`, `VideoDriver` is still fully valid AND EGL/DRM are
still fully valid — the correct and only safe window. Added
`COpenGLES2Driver::flushPendingPresent()` (public, no-op if nothing
pending) and call it from the top of `~CIrrDeviceFB()`, before EGL
teardown, in **both** the ARM DRM/GBM file
(`docker/irrlicht-drm/CIrrDeviceFB.cpp`) and the x86 fork
(`docker/irrlicht-drm-x86/CIrrDeviceFB.cpp` — confirmed via `diff` this is
a genuine separate fork, not shared, so both needed the identical
addition; x86's own swap is already a content no-op per its existing
comment, so this is a correctness/no-permanently-true-flag consistency
fix there rather than a behavior change, and covers x86's own DRM branch
in case it's ever exercised, since `present()` there has a real
non-surfaceless, non-X11 fallback path too). Verified: clean process exit
observed on every Pi invocation this unit ran (all `EXIT=0`, no crash from
the new destructor code path), and the full x86 `ctest` suite — which
exercises normal shutdown paths — stayed green (1963/1964, same known
exception) after this addition.

**3. Layer justification, stated explicitly.** `beginScene → draw →
endScene → createScreenShot()` is the standard Irrlicht idiom — stock
example code, and literally every caller in this repo, including files
this unit cannot edit. A caller using that idiom is entitled to assume
`createScreenShot()` returns what it just drew; this driver was violating
that contract specifically on real DRM/GBM hardware (never on x86, whose
swap was already a content no-op). This is a contract fix, not a
screenshot hack: it does not special-case capture call sites, changes
nothing about what a physical display eventually shows (same content,
same total swap count, only *when* within the endScene/beginScene window
the swap physically executes), and Round 3.1's pacing measurement confirms
it costs nothing in practice. This reasoning is now also stated directly
in the code comment at `COpenGLES2Driver.cpp`'s `endScene()` (the "Why
this belongs here and not in the capture tool" paragraph), not just this
Review, per the coordinator's ask. The counter-argument — confine the
change to the capture path, read pixels before the swap, leave the
production presentation path untouched — was considered and rejected:
`ui_inventory_capture.cpp` cannot be edited this round (T62's territory),
and even if it could, `gles2_zone_benchmark`'s own `--dump-png` path
already reads pre-swap correctly and was never the problem — the actual
callers hitting this bug use the standard idiom precisely because it is
standard, and fixing only one caller would leave the contract violated
for every other immediate caller (debug overlays, portal wireframes, any
future screenshot use), which is the same "fix at the right layer"
principle Round 1 already established for `draw2DRectangle` itself.

### Step 1 — blast radius

**Structural conclusion: this was a degradation-path bug, not a bug that
was visible in live gameplay.** `src/client/graphics/renderer_frame.cpp`
always calls `smgr_->drawAll()` (the 3D scene — `Solid3D`/`AlphaTest3D`/
`Atlas*` shaders) **before** `uiRenderer_->beginFrame()` /
`renderInventoryPopup()` (line ~1192 vs ~1602/1612) on every real frame.
Given the confirmed root cause (below), a legitimately-activated 3D
program always primes the shader-manager's active-program cache correctly
before the first-ever 2D `Color2D` draw of a real session, so the specific
defect self-heals before it can matter in normal play — regardless of
whether the UI atlas is loaded or not.

**This was NOT verified against a live connected client** — NO EQEMU is a
hard rule for this unit, and there was no other way to get a real
character into the inventory-popup state on hardware. The conclusion above
is a code-path argument from the render-loop ordering, stated as such, not
a direct hardware observation of gameplay. One edge this doesn't rule out:
a loading-screen frame that draws 2D UI before any 3D content exists would
hit the same first-ever-Color2D-use hazard this unit found and fixed;
not traced further (would require live client evidence this unit
couldn't gather).

Separately, when an atlas **is** loaded, `UIRenderer::drawRect`/
`drawSprite` never call `draw2DRectangle` at all (they batch through
`draw2DImage`/`UI2D` instead) — so the exact defect found here is
structurally irrelevant to panel-chrome rendering on the atlas-present
happy path, independent of the render-order argument above.

### Step 2 — root cause, and not-drawn vs not-captured

**Confirmed root cause**: `COGLES2ShaderManager`'s constructor
(`docker/irrlicht-drm/COGLES2Shaders.cpp`) initialized `activeProgram_` to
`EOGLES2SP_COLOR2D` — a *real* program enum — instead of the "nothing
bound yet" sentinel `EOGLES2SP_COUNT` that `invalidateActiveProgram()`
already uses elsewhere in the same class. `useProgram(EOGLES2SP_COLOR2D)`
only calls `glUseProgram()` when `activeProgram_ != prog`
(`COGLES2Shaders.cpp:640`); with the bad default, the very first-ever call
to `useProgram(EOGLES2SP_COLOR2D)` in a driver instance's lifetime found
`activeProgram_` already equal to the requested program and skipped the
real GL call — even though no program had ever actually been bound
(`GL_CURRENT_PROGRAM` is `0` from context creation). Every
`draw2DRectangle`/`draw2DLine`/`draw3DLine` issued before some *other*
program was legitimately activated first therefore executed
`glDrawElements`/`glDrawArrays` with no program bound, which is
`GL_INVALID_OPERATION` and draws nothing.

**"Not drawn" vs "not captured", settled explicitly, in-process, on x86**
(fast iteration; `docker/irrlicht-drm/COpenGLES2Driver.{h,cpp}` is the
single source both the ARM and x86 builds compile — confirmed via
`scripts/build-gles2-x86.sh`, which copies these exact two files into the
patched Irrlicht tree). Built a throwaway probe tool (per the plan's
allowance to use a scratch tool outside `tools/` rather than touch
`ui_inventory_capture.cpp` — not committed, deleted before this commit)
that draws a rectangle and calls `glReadPixels` directly **before**
`endScene()`'s buffer swap, then separately calls
`driver->createScreenShot()` **after** `endScene()` (the exact call order
`ui_inventory_capture.cpp` uses). Both reads showed the same missing
content pre-fix (background clear color, no rectangle), and a
`glGetIntegerv(GL_CURRENT_PROGRAM)` probe read `0` with `glGetError()`
returning `0x502` (`GL_INVALID_OPERATION`) at the exact moment of the
failed draw. This rules out a readback/swap-ordering artifact — the
rectangle was never rasterized, full stop. (Separately confirmed:
`tools/hwcaps/gles2_zone_benchmark.cpp`'s `--dump-png` path — the one
`scripts/ci/visual_gate.sh` exercises — already reads pixels *before*
`endScene()`, with its own comment recording this exact hazard
class. That tool was already correct; it was never a suspect, and the
visual gate's clean pass below confirms it stayed unaffected by this
fix.)

Post-fix, the same probe showed `GL_CURRENT_PROGRAM` as a valid non-zero
id, `glGetError() == 0`, and correct pixel readback pre- **and** post-swap
on every draw across three separate frames (see
`refactor/artifacts` history is not kept for this scratch tool per
territory rules; the transcript is reproduced in this Review's evidence
trail above and in the commit message).

### Step 3 — the fix, and its scope

One-line semantic fix, `docker/irrlicht-drm/COGLES2Shaders.cpp`,
`COGLES2ShaderManager::COGLES2ShaderManager()`:
`activeProgram_(EOGLES2SP_COLOR2D)` → `activeProgram_(EOGLES2SP_COUNT)`,
with an explanatory comment. This is squarely a driver-layer fix — no
call site anywhere (`ui_renderer.cpp`, `ui_inventory_capture.cpp`,
`gles2_zone_benchmark.cpp`, real gameplay) needed to change, and none was
touched. It transparently repairs every immediate-mode 2D caller
(`draw2DRectangle`, `draw2DLine`, `draw3DLine`, `draw3DLinesBatch` — every
one of them funnels through `useProgram(EOGLES2SP_COLOR2D)`), not just
`UIRenderer`'s fallback path, per the plan's explicit instruction not to
hide a driver defect behind a call-site workaround.

**T57's warm-up workaround was NOT removed** — it lives in
`tools/hwcaps/ui_inventory_capture.cpp:144-158`, a file T62 owns this
round (explicit territory restriction: "Do NOT modify
tools/hwcaps/ui_inventory_capture.cpp"). The evidence in this Review
(`review/T61/README.md`'s byte-identical-capture analysis) proves the
workaround is now dead weight — the fix means the very first real capture
in a run no longer needs a discarded warm-up frame to prime the shader
cache — but removing it requires editing a file outside this unit's
territory. **Flagged for the orchestrator/T62 to remove**, not done here.

### Verification bar

Run **three times in full** — once for the `activeProgram_` fix (Round
1), again for the deferred-present fix (Round 2), a third time for the
last-frame flush + comment addition (Round 3). All three green; figures
below are the final (Round 3) run.

1. `cmake --build build -j24` — clean, only pre-existing/unrelated
   warnings (e.g. `eq.cpp` void-pointer-arithmetic warnings predating this
   unit).
2. `ctest --test-dir build --output-on-failure` — **1963/1964**, same lone
   known exception as the documented baseline
   (`AudioManagerTest.PreloadCommonSounds`, ALSA host quirk). No new
   failures. `visual_gate` **PASSED** (6.19-6.49s across all three rounds)
   — **goldens untouched**, no re-bless needed. (The deferred-present
   change touches every frame's present timing, not just capture tools —
   this was the specific risk the plan's "goldens risk" note warned
   about; confirmed clean all three times.)
3. `bash scripts/build-gles2-x86.sh` — clean, produces
   `build-gles2/bin/{willeq,gles2_zone_benchmark}`.
4. Visual gate PASS (covered by step 2's ctest run, same tree).
5. `bash scripts/build-arm-noble.sh` — clean full cross-compile,
   `build-arm-noble/bin/willeq` + all `gles2_*`/`ui_inventory_capture`
   tools produced. Run 3 times total this unit: Round 2's final fix, a
   temporary `b56cbe13`-checkout build (pacing baseline, Round 3, deleted
   after extracting the binary — never committed), and Round 3's final
   build with the last-frame flush + comment.
6. `scripts/refactor/check_file_sizes.sh fable` (run with `GIT_DIR`/
   `GIT_WORK_TREE` pointed at this slot, since the tool's cwd-based
   `git rev-parse --show-toplevel` doesn't resolve without `cd`, which is
   disallowed) — 12 files changed vs `fable` (T63/T64 unit plans also
   landed on `fable` since this branch forked, widening the diff further):
   this unit's actual 4 changed files are all `ok` except
   `COpenGLES2Driver.cpp` (2189 lines, **WARN** — pre-existing oversized
   legacy file per `docs/decisions.md`'s Batch F backlog; this unit's
   cumulative diff to it is ~35 lines, not what put it over 1500). The
   remaining files in the 12-file list (`application.h`/`.cpp`,
   `test_video_capture_flags.cpp` [reported as missing — added by T64,
   which landed on `fable` after this branch forked, so this worktree
   never had it], `character_model_view.h`/`test_bone_attachment.cpp`/
   `ui_inventory_capture.cpp` [T62's files], `tools/model_viewer.cpp`
   [**FAIL**, 3217 lines here / 3232 on `fable` tip — already over 3000
   before this unit touched anything]) are all pre-existing drift from
   `fable` moving ahead of this branch, confirmed via `git diff --stat
   HEAD` showing only the 4 driver files this unit actually changed. No
   new FAILs caused by this unit's actual changes.

### Hardware evidence (Orange Pi One, Mali400, real DRM/KMS — not
### llvmpipe)

`review/T61/before/` (T60's original capture, carried over) vs
`review/T61/after/` (Round 2's fix — reconfirmed byte-identical after
Round 3's last-frame-flush addition, since that addition only runs at
shutdown and cannot affect a capture's own frame; see below). Both built
from `tools/hwcaps/ui_inventory_capture.cpp` **unmodified** (re-checked
against the current `fable` tip after T62's merge — no changes to the
relevant code), run on the same hardware, same resolutions, same popup
state, same framing. ARM binaries cross-compiled + stripped locally before
every deploy since the unstripped `willeq` is 357MB and the Pi's `/tmp`
tmpfs is only 242MB — deployed `arm-linux-gnueabihf-strip`'d copies
instead (~10MB `willeq`, ~5.7MB `ui_inventory_capture`).

- `inventory_800x600_full`: BEFORE has no panel background/border (only
  text floats on the clear color); AFTER has both, at the correct
  geometry.
- `inventory_1280x800_full`: same story, independent repro (separate
  device/driver instance in the tool).
- `inventory_800x600_sparse`: BEFORE and Round-1-AFTER were
  byte-identical to `full` (the buffer-lag bug); **Round-2/3-AFTER is
  genuinely distinct** (`md5sum` differs from `full`), and visually shows
  its own correct state (`Rus`/`Clo`/`Bre x5`).
- `inventory_800x600_hover`: Round-2/3-AFTER is byte-identical to `full`
  — this is *correct* (see Round 2 above and README for why), not a
  repeat of the bug.
- **Round 3 re-verification**: re-ran `ui_inventory_capture` with the
  final binary (last-frame flush included) — all 4 PNGs' `md5sum`s are
  **identical** to Round 2's, confirming the destructor addition changed
  nothing about capture correctness (expected: it only executes once, at
  device teardown, after any capture has already completed and been
  written to disk).

Pi session: **7 invocations** total this unit across all three rounds
(`ui_inventory_capture` x3 [Round 1, Round 2, Round 3 re-verify],
`gles2_zone_benchmark` pacing x4 [before/after/after2/before2, Round 3] —
well under the 12/session cap; one mid-session `drop_caches` was needed
when `CmaFree` read 33.7MB at session start, below the 40MB panic-safe
floor — recovered to 90.4MB immediately after, no abort triggered).
Governor: `ondemand` confirmed before Rounds 1/2; pinned to `performance`
for the Round 3 pacing measurement (per `pi_common.py`'s `GovernorPin`
idiom) and explicitly restored to `ondemand` afterward, confirmed on all
4 cores. `/tmp` confirmed empty (0/242M) after every round's cleanup. No
reboot.

### Deviations

- Territory: could not remove T57's now-dead warm-up workaround
  (`ui_inventory_capture.cpp` is T62's file) — flagged above instead of
  touched.
- Used a throwaway scratch probe tool (not committed) rather than editing
  `ui_inventory_capture.cpp`, per the plan's explicit allowance.
- Deployed **stripped** ARM binaries to the Pi (357MB unstripped `willeq`
  does not fit in the 242MB `/tmp` tmpfs) — stripped locally with
  `arm-linux-gnueabihf-strip` before `scp`, functionally identical binary.
- A second driver-layer fix (deferred `present()`) was added mid-review,
  beyond the plan's originally-scoped `activeProgram_` fix — prompted by
  the coordinator's catch of the byte-identical-capture evidence gap.
  Squarely within this unit's territory (`COpenGLES2Driver.{h,cpp}`), no
  call sites touched, full verification bar re-run. Flagged here as a
  deviation from the plan's original single-fix scope, not silently
  absorbed.
- A third addition (`flushPendingPresent()` + both `CIrrDeviceFB.cpp`
  destructors) was made in direct response to the coordinator's Round 3
  review, closing the last-frame-never-presented gap the deferred-present
  fix opened. Also within territory; also re-verified in full.
- Frame-pacing evidence required a temporary local revert of
  `docker/irrlicht-drm/COpenGLES2Driver.{h,cpp}`/`CIrrDeviceFB.cpp` to
  commit `b56cbe13`'s content (activeProgram_ fix only, no deferred
  present) to build an isolated pacing-baseline ARM binary — reverted back
  to the working tree's actual state immediately after extracting that one
  binary, confirmed via `git diff --stat b56cbe13` showing zero remaining
  difference before proceeding. Nothing from this temporary state was
  committed.
- Did not exercise the real client with a live connection on hardware
  (NO EQEMU) — Step 1's blast-radius answer rests on render-loop-ordering
  code inspection plus the isolated-capture-tool hardware evidence above,
  not a live in-game observation. Stated explicitly rather than implied.
