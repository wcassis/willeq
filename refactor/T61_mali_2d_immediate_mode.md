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

Run **twice in full** — once for the `activeProgram_` fix, again for the
deferred-present fix the coordinator's review prompted (Round 2 above).
Both runs green; figures below are the final (Round 2) run.

1. `cmake --build build -j24` — clean, only pre-existing/unrelated
   warnings (e.g. `eq.cpp` void-pointer-arithmetic warnings predating this
   unit).
2. `ctest --test-dir build --output-on-failure` — **1963/1964**, same lone
   known exception as the documented baseline
   (`AudioManagerTest.PreloadCommonSounds`, ALSA host quirk). No new
   failures. `visual_gate` **PASSED** (6.49s) — **goldens untouched**, no
   re-bless needed. (The deferred-present change touches every frame's
   present timing, not just capture tools — this was the specific risk
   the plan's "goldens risk" note warned about; confirmed clean.)
3. `bash scripts/build-gles2-x86.sh` — clean, produces
   `build-gles2/bin/{willeq,gles2_zone_benchmark}`.
4. Visual gate PASS (covered by step 2's ctest run, same tree).
5. `bash scripts/build-arm-noble.sh` — clean full cross-compile,
   `build-arm-noble/bin/willeq` + all `gles2_*`/`ui_inventory_capture`
   tools produced.
6. `scripts/refactor/check_file_sizes.sh fable` (run with `GIT_DIR`/
   `GIT_WORK_TREE` pointed at this slot, since the tool's cwd-based
   `git rev-parse --show-toplevel` doesn't resolve without `cd`, which is
   disallowed) — 7 files changed vs `fable` (T62's merge landed on `fable`
   after this branch forked, widening the diff): `COGLES2Shaders.cpp` (657
   lines, ok), `COpenGLES2Driver.cpp` (2162 lines, **WARN** — pre-existing
   oversized legacy file per `docs/decisions.md`'s Batch F backlog, not
   newly created by this ~20-line diff), `COpenGLES2Driver.h` (467, ok),
   `character_model_view.h`/`test_bone_attachment.cpp`/
   `ui_inventory_capture.cpp` (T62's files, untouched by this unit, ok),
   `tools/model_viewer.cpp` (3217 lines, **FAIL** — pre-existing on the
   `fable` tip itself via T62's merge; confirmed via `git show
   fable:tools/model_viewer.cpp | wc -l` = 3232 lines, already over the
   3000 ceiling before this unit touched anything; this unit's `git diff`
   contains zero lines of `model_viewer.cpp`). No new FAILs caused by this
   unit's actual changes.

### Hardware evidence (Orange Pi One, Mali400, real DRM/KMS — not
### llvmpipe)

`review/T61/before/` (T60's original capture, carried over) vs
`review/T61/after/` (this unit's **final** fix — both the
`activeProgram_` sentinel and the deferred-present fix — cross-compiled +
stripped locally before deploy since the unstripped ARM binary is 357MB
and the Pi's `/tmp` tmpfs is only 242MB — deployed a locally-
`arm-linux-gnueabihf-strip`'d copy instead, ~10MB). Both built from
`tools/hwcaps/ui_inventory_capture.cpp` **unmodified** (re-checked against
the current `fable` tip after T62's merge — no changes to the relevant
code), run on the same hardware, same resolutions, same popup state, same
framing.

- `inventory_800x600_full`: BEFORE has no panel background/border (only
  text floats on the clear color); AFTER has both, at the correct
  geometry.
- `inventory_1280x800_full`: same story, independent repro (separate
  device/driver instance in the tool).
- `inventory_800x600_sparse`: BEFORE and Round-1-AFTER were
  byte-identical to `full` (the buffer-lag bug); **Round-2-AFTER is
  genuinely distinct** (`md5sum` differs from `full`), and visually shows
  its own correct state (`Rus`/`Clo`/`Bre x5`).
- `inventory_800x600_hover`: Round-2-AFTER is byte-identical to `full` —
  this is *correct* (see Round 2 above and README for why), not a repeat
  of the bug.

Pi session: **2 invocations** total this unit (`ui_inventory_capture` run
once per driver-fix round, producing all 4 PNGs each time — well under
the 12/session cap). Governor confirmed `ondemand` before and after both
invocations (untouched). `/tmp` confirmed empty (0/242M) after final
cleanup. No reboot.

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
- Did not exercise the real client with a live connection on hardware
  (NO EQEMU) — Step 1's blast-radius answer rests on render-loop-ordering
  code inspection plus the isolated-capture-tool hardware evidence above,
  not a live in-game observation. Stated explicitly rather than implied.
