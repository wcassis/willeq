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

(executor fills: Step 1's blast-radius answer with hardware evidence; the
confirmed root cause and how "not drawn" vs "not captured" was
distinguished; where the fix landed and why that layer; whether T57's
warm-up workaround was removed; before/after captures; deviations)
