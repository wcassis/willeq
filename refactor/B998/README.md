# B998 — gfaydark low-geometry frame floor (2× vsync / ~31 fps)

**Status: root cause found + measured ceiling produced; BLOCKED (no code fix).**
This card is the "produce the measurement that proves the ceiling and block with
that evidence" branch of issue #325's acceptance. The floor is a **measured
per-frame-work ceiling**, not a leak, not a bug in the vsync path, and — see
§7 — not a code defect a PR on this branch can remove without first settling a
design question that is B997's and Will's.

**Scope:** this card covers the **steady ~31 fps floor (2× vsync)** and the
**warp-window 3× dips** in gfaydark on the Pi rig. B979 (impostor-capture
hitch) is a separate card; it shares the *present path* identified in §4 but
has a different trigger. B972 (stream-buffer sizing) is on its own worktree.
Do not merge the three.

**Evidence location:** `~/kb-evidence/b998-gfaydark-floor/` (this run) plus the
two earlier floor sweeps it builds on. Every number below is citeable to a
`client.log` line in that directory; the re-runnable scripts are listed at the
end.

---

## 0. The question (re-stated precisely)

In the same build (`60e8f6ec`), same Pi rig, same 800×600 @ 60 Hz CRTC,
DRM/KMS page-flip vsync: **qeynos2 holds ~60-65 fps (1 vsync period),
gfaydark holds ~31 fps (2 vsync periods) in low-geometry regions.** Why does
gfaydark's frame exceed one 16.58 ms vsync period at ~3.5k polys?

## 1. Decisive A/B (the floor, 800×600, build `60e8f6ec`)

New `/frametiming` sweeps (the original floor-sweep tours never enabled
frametiming, so these are the first per-section breakdowns at the floor).

| (ms, 60-frame block medians) | **gfaydark stop A** | **qeynos2 control** |
|---|---:|---:|
| **Total Frame (wall)** | **31.77** (2× vsync, 31.5 fps) | **16.04** (1× vsync, 62.3 fps) |
| Scene Draw All | **17.12** [147 nodes, **3534 polys**, 41 drawCalls] | **8.93** [124 nodes, **2517 polys**, 22 drawCalls] |
| └ Solid Pass (terrain + props) | **16.00** | **8.12** |
| └ Manual Zone Draw | 4.67 | 2.27 |
| Static UI | 2.22 | 1.82 |
| Entity Update | 0.93 | 0.36 |
| measured section sum (top-level) | **22.42** | **12.47** |
| **unaccounted (wall − sections)** | **9.35** | **3.42** |

Citations — `ft-sweep-baseline/zones/gfaydark/client.log:2419-2466` (stop A,
31.5 fps block) and `:2680-2725` (steady, 46.1 fps); `ft-sweep-qeynos2/
zones/qeynos2/client.log:1729-1775` (62.3 fps block) and `:1898-1942`
(67.7 fps steady). `Constrained rendering mode: Custom (800x600 ...)` at gd
`client.log:27` / qe `:27`; `DRM: Available mode [2]: 800x600@60Hz` at gd
`:36`; same CRTC, page-flip vsync on both (the run was `-r "800 600"`;
`CIrrDeviceFB::drmPageFlip()` `docker/irrlicht-drm/CIrrDeviceFB.cpp:687`).

**Reading it:**
- qeynos2's measured work (12.5 ms) + unaccounted (3.4 ms) = 16.0 ms → fits
  one 16.58 ms vsync period → 60 fps.
- gfaydark's measured work (22.4 ms) + unaccounted (9.4 ms) = 31.8 ms →
  exceeds one vsync period by ~15 ms → the frame completes on the *second*
  vsync edge → 31 fps.
- **The excess over qeynos2 is in the zone draw: Solid Pass 16.0 vs 8.1 ms
  (nearly 2×) + Manual Zone Draw 4.7 vs 2.3 ms, at *fewer* polys (3534 vs
  2517) but *nearly 2× more manual draw calls* (41 vs 22, same 19 regions).**
  The ~9.4 ms unaccounted is the deferred present (§4b) and is present on
  both sides.

## 2. Within gfaydark the per-frame cost tracks draw calls, not polys

All stops from the same 800×600 baseline sweep (`blocks_census.py`):

| stop (gd client.log) | fps | Total | Solid Pass | polys | manual drawCalls |
|---|---:|---:|---:|---:|---:|
| stop A, low | 31.5 | 31.77 | 16.00 | 3534 | 41 / 19 reg |
| low, settled | 46.1 | 21.69 | 12.04 | 3540 | 42 / 20 reg |
| low, settled | 53.0 | 18.86 | 9.60 | 1701 | 38 / 15 reg |
| **W8 stop (399/-278, low)** | 35.6 | 28.12 | 8.42 | 1975 | 36 / **4 reg** |
| **W1 stop (684/166, dip)** | 22.4 | 44.70 | 10.39 | 5174 | **101 / 61 reg** |

(`:2419`, `:2680`, `:2805`, and `ft-sweep-dip` B17 `:4621`, B9 `:3296`.) Two
facts:

1. The 3.5k-poly low-geom floor (31.77 ms) is *slower* than the 1.7k-poly
   settled stop (18.86 ms) — geometry count moves the cost, but…
2. **the W1 stop draws only 10.4 ms of solid pass — less than the 12.0 ms of
   the 46.1 fps settled stop — yet takes 44.7 ms of wall time because it
   carries 101 manual zone draw calls across 61 regions** (the 2×-floor W8
   stop carries 36 across only 4 regions; qeynos2 22 across 19). The frame's
   *measured* work is normal there (~16 ms of sections); what pushes it to 3×
   is the 28.5 ms unaccounted present/pipeline time (§4b) on top of the
   region's 101-call state.

So the floor is **draw-call / per-draw-state bound** (gfaydark's zone WLD
fragments into more region draws than qeynos2's), not fill-bound, and not a
simple polys function. That is a scene-structure property feeding
`drawZoneGeometrySorted` / `Manual Zone Draw`
(`src/client/graphics/zone_lifecycle/renderer_zone_draw.cpp`), not a
renderer defect. (I dropped the 1280×720 B972 crowd comparison from the
earlier draft — different resolution, different workload, not a valid
A/B against this 800×600 floor.)

## 3. The 2× floor is vsync *quantization* of a >16.58 ms frame

p50 frame time in the original 64-min run quantizes exactly at **33.15 ms
= 2 × 16.58 ms** (`timeline.py`); frames land on integer vsync edges, never
odd durations. The governor is **not** the cap — it is a *loading* throttle
(Green/Yellow/Red budget for streaming), `src/client/graphics/frame_budget_governor.cpp`
(`getTargetFrameMs`), and reads `gov=Green` on most frames (BUDGET census
gd Green 33 / Yellow 87; qe Green 1092 / Yellow 79). The floor is: *the
frame's wall time (measured work + unaccounted present) exceeds one 16.58 ms
period by ~15 ms → 2 vsyncs → 31 fps.* **Measured and pinned; no frame lands
off vsync edges; no growth** (draws/textures/impostor cells flat,
`growth.py`).

## 4. The warp-window 3× dips: sustained high-draw-call region state + a present/vblank stall

The sub-30 windows (21-26 fps) all start 2-7 s after a `/warp`
(`dip_stops.py`). I captured a dedicated 3-stop tour at the exact W0/W1/W8
warp coordinates with `/frametiming` ON across the re-stream
(`ft-sweep-dip/`, 29 breakdown blocks, 6 "Regenerating detail chunks"
cycles). The breakdown splits the dip into two additive effects:

**(a) The dip's 3× is mostly a *sustained region-state* effect, not a
transient regeneration stall.** The W1 stop (684/166) sits at a *steady*
22.4 fps for 15+ s across 8 consecutive blocks (B9-B16, all 44.7 ms, 101
drawCalls / 61 regions — `dip_blocks.py`, `dip_dc.py`), *long after* its one
detail-chunk regen cycle finished. The regen cycles (6 total,
`dip_regen_corr.py`) land at the window starts; they are not required to
sustain the 3×. The dip is the *region itself*: 101 manual zone draw calls /
61 regions (vs 36/4 at the 2×-floor W8 stop, 22/19 at qeynos2). Its
*measured* work (~16.2 ms of sections, `dip_dc.py` B9) is roughly one vsync —
the same order as the floor — but the unaccounted present/pipeline time (§4b)
is nearly double the floor's, and that is what pushes the frame to the third
vsync edge.

**(b) The unaccounted time is the deferred-present / vblank / pipeline stall,
and it is large on both the floor and the dip.** This is the piece the earlier
drafts could not attribute, and the code path explains it:

- T61 moved `present()` (eglSwapBuffers **plus the DRM page-flip**, the
  operation that waits on vblank) off the end of a frame onto the start of the
  *next* `beginScene()` — `COpenGLES2Driver.cpp:539,485`;
  `CIrrDeviceFB::present()→drmPageFlip()` `CIrrDeviceFB.cpp:1242,687`
  (async `drmModePageFlip` + `poll()` on the flip event).
- `FrameComponent::beginSceneAndDrawWorld()` sets `sectionStart_` **after**
  `driver->beginScene()` returns (`renderer_frame_render.cpp:137-138`), so the
  deferred flip's vblank/pipeline wait happens *before the first named timing
  section starts* — it is exactly the "unaccounted" column in every block:
  **9.4-14.2 ms across the 2×-floor stops (9.4 at stop A, 14.2 at W8), 28.5 ms
  in the 3× dip** (`unacc.py`, `dip_dc.py` B9: Total 44.70 / work 16.21 /
  unacc 28.49).
- In the dip the unaccounted balloons because (i) the region's 101 draw calls
  keep the Mali pipeline deeper at flip time, and (ii) the warp's
  detail-streaming uploads (original 64-min run: 110 "Regenerating detail
  chunks"; 203 meshLoading frames @ 20.3 ms vs 1442 @ 4.0 ms steady,
  `dip_driver2.py`) run concurrently on the shared bus, stalling the flip.
  B979's endScene comment (B979) and this card are the **same present path**;
  B979's trigger is the impostor capture, B998's is the high-draw-call region
  state plus detail-streaming churn at warp stops.

**Honest limit:** the unaccounted column is *attributed to the present path
by elimination* (T61 defers it out of every measured section; the page flip
is the only vblank wait on this run), not directly time-sliced, because
`eqtEnableGPUSyncProbe(true)` is never called in production — see §5.

The ratchet (original run): `frame_p50` steps 1× → 2× (~9-10 min in), then
2× → 3× → 4× *inside* the warp windows (W1: 49.7 → 66.3 ms after the 4th
regen) and does not release within the window (`w1_deep.py`, `ratchet.py`).

The 131 ">100 ms" frames in the original log are **`/screenshot` saves**
(130/131 within ~0.05 s of one), not the dips (`correlate_slow.py`) — excluded
from every number above.

## 5. What remains unmeasured

- **CPU-submit vs GPU-raster inside Solid Pass.** `gpu_sync_probe.h` (P52)
  has the accessors (`eqtGetGPUPhaseNanos`, `eqtGetLastSwapBuffersMs`) but
  `eqtEnableGPUSyncProbe(true)` is **never called in production**, so no
  `ZonePass`/GPU-phase lines exist in any of these sweeps. I did **not**
  enable it — that is a code change whose purpose is measurement, and it
  belongs in a planned unit, not this card. §2 establishes the direction
  (draw-call bound, not fill bound); the probe would split the Solid Pass
  itself, which is what a fix PR needs.

## 6. Ceiling statement (the deliverable for the block)

- **Steady floor, 800×600 @ 60 Hz, build `60e8f6ec`, low-geom gfaydark:**
  **31 fps (2 vsync, 31.8 ms wall)** = 22.4 ms measured frame work
  (Solid Pass 12-16 ms at 41 manual zone draw calls) + 9.4 ms deferred-present
  / unaccounted. qeynos2 control: 16.0 ms (12.5 + 3.4) → 1× vsync → 60+ fps.
- **Warp-window dips:** 3× (22.4 fps, 44.7 ms) sustained for 15-60 s in
  high-draw-call regions (101 calls / 61 regions), 28.5 ms of it the
  present/vblank stall; the 4× tails in the original run add the
  detail-chunk-regen churn on top.
- **It is a ceiling, not a regression or a leak:** flat resource census
  (`growth.py`), governor Green on most frames, no frame off vsync edges, cost
  tracks draw-call/region structure not polys, reproducible across two
  independent tours (baseline + dip-capture agree: 31.77/28.12 ms floor,
  44.7 ms dip).

## 7. Why this BLOCKS rather than ships a fix

The floor is a **scene-structure consequence** (gfaydark's zone WLD fragments
into ~41 zone draws / up to 101 in the W1 region, vs qeynos2's ~22) surfaced
through the renderer's per-draw GLES2 submit cost, plus a present path whose
T61 deferral puts the vblank wait outside every measured section. Removing it
is one of:
1. **Batch/merge the manual zone draws** (fewer draw calls, less per-draw
   state) — B997 "manual zone draw" territory; changes what is drawn per
   region, a **look/coverage change** Will must sign off;
2. **Reduce per-draw submit cost** (shared VBO state, fewer state changes in
   `drawZoneGeometrySorted`) — risks the coverage invariants (B726/B979),
   needs its own plan;
3. **Accept 31 fps in low-geometry gfaydark** as the measured ceiling.

None is a "bug fix" landable on this branch without that decision. Per the
card's own acceptance ("produce the measurement that proves the ceiling and
block with that evidence"), this is blocked with the measurement above.

## 8. Suggested unblock / next unit (for Will / B997)

- **Enable `eqtEnableGPUSyncProbe(true)` behind a flag on the Pi rig** and
  re-run this A/B — one small planned unit; it directly time-slices the
  deferred flip (flip-wait vs swap) and the Solid Pass, deciding whether §7
  option 1 (fewer draws) or option 2 (cheaper submit) is the lever.
- **Cross-link to B997** (manual zone draw): the 41-vs-22 (and 101-vs-22 in
  the W1 dip region) draw-call gap *is* the manual-zone-draw surface area; a
  B997 batch/merge that lands also drops B998's floor.
- The dip's detail-regen component (§4b) is a **detail-streaming** cost —
  larger/less-frequent or pre-staged detail chunks would shrink the warp
  dips without touching the floor.

## Citations index

- Floor A/B: `ft-sweep-baseline/zones/gfaydark/client.log:2419-2466` (31.5 fps
  stop A), `:2680-2725` (46.1 steady), `:2805-2853` (53.0 fps, 1701 polys);
  `ft-sweep-qeynos2/zones/qeynos2/client.log:1729-1775` (62.3 fps), `:1898-1942`
  (67.7 steady). `Constrained rendering mode` gd `:27` / qe `:27`;
  `800x600@60Hz` mode gd `:36`.
- Dip: `ft-sweep-dip/zones/gfaydark/client.log:2447-2494` (B0, first block of
  W0 stop, 23.2 fps), `:3296-3343` (B9, sustained 22.4 fps, 101 dc / 61 reg),
  `:4621-4668` (B17, W8 2× floor, 36 dc / 4 reg); warp lines `:1040`, `:3094`,
  `:4546`; 6 "Regenerating detail chunks".
- Dip ratchet (original 64-min run): `gd-floor-sweep-20261007T151650Z/
  zones/gfaydark/client.log` (110 "Regenerating detail chunks"; 203
  meshLoading @ 20.3 ms / 1442 @ 4.0 ms; W0/W1/W8 warp coords in
  `dip_stops.py`).
- Governor = symptom: `src/client/graphics/frame_budget_governor.cpp`;
  BUDGET census gd Green 33 / Yellow 87, qe Green 1092 / Yellow 79.
- Present path: `docker/irrlicht-drm/COpenGLES2Driver.cpp:483-487,528-560`
  (T61 deferral), `docker/irrlicht-drm/CIrrDeviceFB.cpp:656-754`
  (page-flip + `poll()`), `src/client/graphics/frame/renderer_frame_render.cpp:130-174`
  (`sectionStart_` after `beginScene()`), `:802-819` (`endSceneAndSwap`);
  `docker/irrlicht-drm/gpu_sync_probe.h` (probe not enabled in production).
- Scripts (re-runnable, in `~/kb-evidence/b998-gfaydark-floor/`):
  `blocks_census.py`, `unacc.py`, `dip_blocks.py`, `dip_dc.py`,
  `dip_regen_corr.py`, `timeline.py`, `growth.py`, `windows2.py`,
  `dip_driver2.py`, `w1_deep.py`, `w1_spikes.py`, `ratchet.py`,
  `correlate_slow.py`, `dip_stops.py`, `ab_table.py`.
