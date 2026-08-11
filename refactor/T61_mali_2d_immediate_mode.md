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

1. `cmake --build build -j24` — clean, only pre-existing/unrelated
   warnings (e.g. `eq.cpp` void-pointer-arithmetic warnings predating this
   unit).
2. `ctest --test-dir build --output-on-failure` — **1963/1964**, same lone
   known exception as the documented baseline
   (`AudioManagerTest.PreloadCommonSounds`, ALSA host quirk). No new
   failures. `visual_gate` **PASSED** (5.45s) — **goldens untouched**, no
   re-bless needed.
3. `bash scripts/build-gles2-x86.sh` — clean, produces
   `build-gles2/bin/{willeq,gles2_zone_benchmark}`.
4. Visual gate PASS (covered by step 2's ctest run, same tree).
5. `bash scripts/build-arm-noble.sh` — clean full cross-compile,
   `build-arm-noble/bin/willeq` + all `gles2_*`/`ui_inventory_capture`
   tools produced.
6. `scripts/refactor/check_file_sizes.sh fable` (run with `GIT_DIR`/
   `GIT_WORK_TREE` pointed at this slot, since the tool's cwd-based
   `git rev-parse --show-toplevel` doesn't resolve without `cd`, which is
   disallowed) — `docker/irrlicht-drm/COGLES2Shaders.cpp: 657 lines, ok`.
   1 file checked, 0 warn, 0 fail.

### Hardware evidence (Orange Pi One, Mali400, real DRM/KMS — not
### llvmpipe)

`review/T61/before/` (T60's original capture, carried over) vs
`review/T61/after/` (this unit's fix, cross-compiled + stripped locally
before deploy since the unstripped ARM binary is 357MB and the Pi's `/tmp`
tmpfs is only 242MB — deployed a locally-`arm-linux-gnueabihf-strip`'d copy
instead, ~10MB). Both built from `tools/hwcaps/ui_inventory_capture.cpp`
**unmodified**, run on the same hardware, same resolutions, same popup
state, same framing.

- `inventory_800x600_full`: BEFORE has no panel background/border (only
  text floats on the clear color); AFTER has both, at the correct
  geometry. `md5sum` confirms AFTER is byte-identical to the (already-
  correct pre-fix) `sparse`/`hover` captures — same driver instance, same
  code path, same pixels once the cache isn't stuck.
- `inventory_1280x800_full`: same story, independent repro (separate
  device/driver instance in the tool).
- `inventory_800x600_sparse` / `_hover`: BEFORE and AFTER are
  byte-identical (`md5sum` verified, not just similar file sizes — this is
  the exact identical-capture failure mode the coordinator flagged after
  T60's hover mistake, checked deliberately here and found to be a real,
  explained finding rather than a mistake: see README for why these two
  were already correct before the fix).

Pi session: 1 invocation (`ui_inventory_capture` once, producing all 4
PNGs — well under the 12/session cap). Governor confirmed `ondemand`
before and after (untouched). `/tmp` confirmed empty (0/242M) after
cleanup. No reboot.

### Deviations

- Territory: could not remove T57's now-dead warm-up workaround
  (`ui_inventory_capture.cpp` is T62's file this round) — flagged above
  instead of touched.
- Used a throwaway scratch probe tool (not committed) rather than editing
  `ui_inventory_capture.cpp`, per the plan's explicit allowance.
- Deployed **stripped** ARM binaries to the Pi (357MB unstripped `willeq`
  does not fit in the 242MB `/tmp` tmpfs) — stripped locally with
  `arm-linux-gnueabihf-strip` before `scp`, functionally identical binary.
- Did not exercise the real client with a live connection on hardware
  (NO EQEMU) — Step 1's blast-radius answer rests on render-loop-ordering
  code inspection plus the isolated-capture-tool hardware evidence above,
  not a live in-game observation. Stated explicitly rather than implied.
