# T459 — Orange Pi live diagnosis: no cursor, inventory fps halving, camera jerk, missing spell gems

Class A, **eqemu lock + Pi ownership**, slot3. Measurement unit: it produces
evidence and board entries, and fixes only what the evidence makes a one-line,
test-pinned change (say so if you do). Fixes that need design belong to
follow-up units the orchestrator plans from your Review.

## What Will reported (2026-09-13, Orange Pi One, `review/pi/README.md` recipe)

Binary `main/build-arm-noble/bin/willeq` (built 2026-09-08 01:27 UTC from
`8401a33f`; the only later commit touches no source, so it IS the tip's code),
`--drm --renderer gles2 --constrained orangepi -r 800 600`, account
`willeq_test` / `Testcaster` (Wizard). Config: `review/pi/willeq_pi.json`
(host now 172.16.200.99).

1. No mouse cursor visible in-game.
2. Opening the inventory halves fps.
3. **Default (Follow) camera**, **moving forward on flat ground**: the camera
   jerks rapidly between two different perspectives.
4. The spell gem bar is missing THE WHOLE SESSION; the character had spells
   memorized.
5. Held items are misoriented in the hands (NOT this unit — needs oracle
   goldens after T461's camera work).

## Orchestrator's code triage (leads, NOT findings — confirm or refute each)

1. **Cursor.** `renderer_init.cpp` creates the software cursor only under
   `--drm`; `renderFrameTail` draws it only while `owner_.softwareCursorTexture_`
   is non-null. `ConstrainedTextureCache::clear()` (zone unload,
   `renderer_zone_lifecycle.cpp`) notifies eviction listeners for every
   texture not `protectTexture()`'d — only the UI atlas and item icon sheets
   are protected — and the cursor's guard (`renderer_zone_merge.h`) nulls
   the pointer; nothing reloads it. So: visible at the loading screen / before
   the first zone unload, gone after. Alternatives: an LRU eviction under the
   orangepi budget; no evdev mouse found (`CIrrDeviceFB.cpp` logs it). **T460
   is fixing this mechanism in parallel on x86** — your job is the live
   before/after-zone-in observation and the log lines, so T460's fix is proven
   against the real symptom.
2. **Inventory fps.** Paperdoll re-renders its RT (~158x254, own scene
   manager, 3 point lights) when dirty + every 200 ms (a hardcoded 16 ms/frame
   counter, not wall time). The static inventory popup (`static_panels.cpp`)
   is redrawn from scratch every frame; the pre-static-UI InventoryWindow was
   cached to an RT (`43c56a70`, measured ~13 ms/frame on Mali-400 for 70+
   uncached draws) and U07c2 (`0f25cb57`) deleted that cache. Will: do not
   chase old numbers — ACCOUNT FOR THE CURRENT HIT.
3. **Camera jerk.** Four writers of the Follow camera (physics, zoom, lifts,
   bridge `PlayerMoved` → `setPlayerPosition` → `setFollowPosition` with dt 0).
   Suspected: `movement_update.cpp` publishes `HeadingDeg()` (0–360) where
   every other publisher sends 0–512 (T460 checks this in code); self
   `OP_ClientUpdate` corrections alternating with local integration; BSP
   camera pull-in with a one-frame-late region. ⚠ The 90° player/NPC heading
   offset is deliberate — never "harmonise" it.
4. **Spell gems.** `renderSpellGemPanel` has no Show gate and draws 8 slots at
   x=764, y=268–552 for 800x600. Suspected: DRM mode fallback (the display has
   no 800x600 mode → `CIrrDeviceFB.cpp` uses the preferred mode while the UI
   lays out for 800x600), or the panel covered.

## Tasks

0. Lock (`eqemu_lock_take T459`), `up.sh --launcher`, check the Pi
   (`ssh orangepi`: uptime, free, `ls /dev/input/by-id`, attached display's
   modes from `/sys/class/drm/*/modes`). Select Testcaster's current pose from
   the db before moving it; restore what you change. Force daylight.
1. **Input.** Find how prior Pi units drove movement/UI (`scripts/pi/`,
   `--path-file benchmarks/paths/*.path`, slash commands such as
   `/inventory`, uinput). If there is no way to inject a mouse move or
   keyboard forward-run on the Pi, say so and use the nearest faithful
   substitute (path-file forward run); STOP only if no substitute exists.
2. **Cursor:** screenshots (`--dump-frames-dir` to Pi-local /tmp, bounded)
   before zone-in and after; the log lines for mouse detection, texture cache
   clear, cursor eviction. State which lead is confirmed.
3. **Spell gems:** full-frame screenshot in world; log `DRM:` mode lines;
   actual framebuffer size vs 800x600; memorized gems in the game state (log).
   Also take the same shot on x86 `build-gles2` at 800x600 for contrast.
4. **Inventory fps:** with the existing `frameTimings_` sections (find how they
   are reported — a debug overlay, log, or `--frame-timings`-style flag), a
   standing-still run of N seconds inventory CLOSED vs OPEN, same pose and
   heading, ≥3 repeats each. Report fps plus the per-section split
   (staticUI, paperdoll RT update, everything else). If the tree cannot
   separate the paperdoll from the popup, add the smallest timing split on
   your branch (commit it) rather than guessing.
5. **Camera jerk:** a forward run in Follow on flat ground, video via
   `--dump-frames-dir` + `scripts/ci/encode_capture.sh` (capture fps is not
   performance), plus DEBUG/TRACE logs of every camera writer with frame
   number, position and heading. Identify the two alternating states and the
   writer of each. Also repeat on x86 `build-gles2` — does it reproduce off
   the Pi?
6. Board each confirmed defect as its own entry (B505–B519 are yours), with
   the measured evidence and the refuted leads named.

## Proof / constraints

- Pi protocol (EXECUTOR_PROTOCOL "Pi"): Pi-local /tmp for I/O, ≤12
  invocations, leave it pristine. eqemu protocol: lock, world DOWN at the end,
  lock released, character pose restored.
- No golden moves. If you commit source (a timing split or a trace line), the
  standard verification bar applies to it; a docs/board-only branch needs the
  doc nets (`ctest -R 'Doc|Board|Comment'`).
- Media + README: `/mnt/projects/fable/review/T459/` (`pi/`, `x86/`).
- File territory: `refactor/BOARD.md` (your B range), this plan,
  `review/T459/`, and — only if needed — timing/trace lines in
  `src/client/graphics/frame/`. Do NOT touch the cursor/texture-cache code
  (T460) or `renderer_camera.cpp`/hotkeys/slash commands (T461).

STOP: the Pi or the display is unreachable; the server cannot reach zone-in.

## Review

Executor: slot3, branch `task/T459-pi-live-diagnosis`. Baseline started on
`9c164693`, then rebased onto `2f0bd914` (T460) at the coordinator's request,
for the post-fix live proof. Media and README: `review/T459/`. Board entries:
**B505–B511** (below). T460's B520, B521 and B525 are answered in "Post-fix
proof".

### Environment (Task 0)
- Lock taken (`T459 2026-09-13T15:27:23+00:00`). The world was already up (one
  `world`, ports 5998/9000 open), so `up.sh` was not needed.
- Pi: up 4 days, 483 MB, governor `ondemand`, `/tmp` empty, CmaFree 5.5 MB at
  idle. **No USB keyboard or mouse attached**: `/proc/bus/input/devices` had
  only `gpio-keys`, and `lsusb` showed root hubs only. HDMI connected; the
  modes include 800x600@60 and @56.
- Testcaster at arrival: `zone_id=34` (nro), x=-41.8658, y=2723.33,
  z=-24.8438, h=331.25. Account status 200 (GM). One memorized spell (slot 0,
  spell 288).

### Input (Task 1)
There was no way to press a key or move a mouse on the Pi, so input went
through `/dev/uinput` (B510). `scripts/uinput_inject.py` creates a virtual
keyboard and mouse before the client starts. The client picked both
(`evdev: Mouse at /dev/input/event2`, `evdev: Keyboard at /dev/input/event1`),
so key and mouse events take the real `CIrrDeviceFB` evdev path: I toggles the
inventory, W runs forward, D turns. Every action is time-stamped in each run's
`marks.txt`. On the dev VM injection is impossible (the VM's AT keyboard is
enumerated first, and the user is not in `input`), so the x86 contrast used
`/move`.

### Pi invocations (6 of 12 before the post-fix runs)
| run | binary | purpose | result |
|---|---|---|---|
| P1 `p1_observe` | main `build-arm-noble` (Will's, tip code) | observe, dumps every 15 | trigger string never logged, so no input ran. Standing-still evidence only |
| P2 `p2_inventory_ab` | main (Will's) | inventory A/B, INFO only | clean |
| P3 `p3_camera_trace` | slot3 @9c164693 + T459 trace | cursor transitions, paperdoll timer, 2 forward runs, `/move` | clean |
| P4 `p4_camera_video` | slot3 @9c164693 + trace | 4-leg forward run with dumps (video) | clean |
| P5 `p5_inventory_ab` | slot3 @9c164693 + trace | inventory A/B repeat, INFO only | client clean. `run_pi.sh` was edited mid-run and its copy tail died; data recovered by hand, Pi `/tmp` cleaned |
(post-fix P6/P7 below)

### Defect 1 — cursor: CONFIRMED (the pointer is nulled at the first zone load)
- P3 and P4 (T459 transition line): `software cursor texture is now LIVE
  (frame 1)` during loading, then `NULL (frame 2, cursor control visible=true)`
  on the first in-world frame (P3 12:13:12.610). x86 x2 shows the same.
- `unloadZone: constrained texture cache cleared` ran during the load, before
  frame 2. **Zero** `Evicting texture` lines in any session.
- Crops at the cursor's position: 400,300 unmoved (P1 frame 45), and 600,420
  after a +200,+120 move (P4 frame 10).
- **The cursor is never drawn at any point in a session.**
  `processFrameRender()` returns before `renderFrameTail()` while the loading
  screen is up, and the pointer is null from the first in-world frame on.
- **Refuted:** LRU eviction under the orangepi budget (0 evictions). "No evdev
  mouse" is refuted as a cause, since the injected mouse was found and the
  texture was still null. Will's own session may also have had no mouse
  attached; unknowable from here.
- T460's B520 covers the mechanism. Post-fix proof below.

### Defect 2 — inventory fps: CONFIRMED, cause is the popup draw, not the paperdoll (B506, B505)
Wall-clock fps is 60 frames over the time between consecutive `FRAME TIMING
BREAKDOWN` lines; it matches the CPU estimate within 1%. Standing still,
windows interleaved:

| run (binary) | state | clean windows | wall fps | frame ms | static UI ms | scene draw ms |
|---|---|---|---|---|---|---|
| P2 (Will's) | closed | 53 | 48.4 | 20.9 | 11.1 | 4.4 |
| P2 (Will's) | open | 35 | 37.9 | 26.4 | 17.0 | 4.4 |
| P5 (slot) | closed | 47 | 38.2 | 26.5 | 10.7 | 6.1 |
| P5 (slot) | open | 26 | 30.0 | 33.3 | 17.7 | 6.0 |

Per cycle, P2 closed→open: 59.3→39.7, 46.5→37.3, 45.2→37.4, 45.1→37.1. P5:
40.2→30.2, 40.2→30.2, 40.0→29.7, then closed-4 fell to 30.2 as chat grew.

- **Paperdoll split** (T459 timer, P3 and P5): `updatePaperdollForFrame`
  averages 175–246 us. 4–6 calls in 60 exceed 1 ms (~2.1 ms, the RTT
  re-render). The first open is one 243 ms model build. That puts the
  paperdoll at ~0.2 ms of the +6–7 ms.
- **Everything else:** the rest of the static-UI delta is the popup's draw
  submission plus `UIRenderer::endFrame()` (one `draw2DImage` per quad).
  Scene draw and the other sections do not move.
- **Why it reads as halving:** P5's open windows are pinned at 30.2 fps, and
  P2's first closed window was 59.3. `drmPageFlip()` waits for the previous
  vblank flip, so any frame over 16.7 ms is held to 33.3 ms.
- Separately, **B505**: static UI grows with chat history. P1 went from
  11.9 ms to 22.0 ms over 5 minutes, inventory closed, standing still.

### Defect 3 — camera jerk: NOT REPRODUCED by keyboard forward runs; two real camera defects found (B508, B507, B521)
- **Two alternating states: none found.** `scripts/analyze_camera.py` and
  run-length analysis of the per-frame `T459 cam` trace (logged after all
  writers, before render):
  - P3 forward-run-1: 454 frames; forward-run-2: 346 frames. Frame dt
    21.8±2.5 ms. Player step 1.06±0.12 per frame, 0 backward steps, 0 zero
    steps.
  - Camera yaw constant per run. Height over player: ONE flip in 800 frames.
  - Writers during the runs: physics `Position updated` 454/346, bridge
    `setPlayerPosition` **0**, BSP ceiling (throttled log) 5/6.
  - P4 video leg: four headings, one state each.
- **B507, the latched height:** at 12:13:52.098 the BSP ceiling clamp read
  `ceiling at -172.0` with the player at z −11, pinning the camera at 3.0
  instead of 10.0 for the rest of both runs. It reproduces on x86 at the same
  spot.
- **B521, live-confirmed (baseline):** P3's `/move` made the camera heading
  jump 437.17 → 76.39 (yaw 127.4° → −126.3°) in one frame. The only writer was
  bridge `setPlayerPosition`, 838 times during the move. x86 x2: 41 → 220.54,
  then 220.54 → 37.
- The 90° player/NPC offset was not touched.
- Video: `review/T459/pi/T459_pi_p4_follow_forward_four_legs_capture.mp4`.
  0.72 fps capture over a measured 87.2 s, a capture artefact and not
  performance; each dump costs ~1 s on the Pi.
- **x86:** reproduces B507 and B521; no keyboard path is available there.

### Defect 4 — spell gems: NOT REPRODUCED in the buffer (B509)
- DRM: `Found matching mode 800x600@60Hz`; `GBM: Surface created 800x600`.
- **Refuted on this display:** the mode-fallback lead.
- Full frames (P1, P4, x86 x2) show the gem column at x 764–796, with A1
  "Mino" (spell 288; the db has exactly one memorized gem) and A2–A8 empty.
- The panel is drawn and not covered. Remaining candidate: display overscan
  of the rightmost ~36 px, which only Will can check on the physical screen.

### Code on this branch
Diagnostic only; no behaviour change. Commit "T459: diagnostic trace lines":
- `src/client/graphics/frame/renderer_frame.cpp`: the per-frame
  `T459 cam` TRACE line (MOD_CAMERA), and an INFO line on each
  software-cursor texture null/non-null transition.
- `src/client/graphics/overlays/overlays_hud_env.cpp`: the paperdoll timer.

**Deviation (territory):** the plan allowed timing lines only under
`frame/`. `renderStaticUI()` is pinned statement-for-statement by the G05
golden and the plan forbids golden moves, so the paperdoll split was taken in
its callee, `OverlaysComponent::updatePaperdollForFrame()`. No golden moved.

### Deviations
- **Not Will's texture path:** runs used no `--eqe1-path`, the S3D path, to
  match Will's recipe. EXECUTOR_PROTOCOL asks for `--eqe1-path` on pixel runs,
  but that would not reproduce his session.
- **P1 trigger:** the injector's trigger was a line these sessions never log,
  so P1 ran without input. Fixed for later runs (B510).
- **P5 recovery:** `run_pi.sh` was edited while P5 was running. The client ran
  to completion; the copy tail was redone by hand and `willeq_exit` is
  unrecorded.
- **Video mtimes:** P4's first encode had wrong mtimes (the `cp -r` copy).
  The mtimes were restored from the log's `Frame dump` timestamps and the
  video re-encoded.
- **x86 attempt x1:** stalled at 40% (B511). x2 ran on a local copy of slot3's
  `build-gles2` with capabilities.
- **Load:** x86 x2 ran alongside other slots' builds; its capture/fps is not a
  performance number.

### Post-fix proof (T460 @2f0bd914)
The branch was rebased onto `2f0bd914`, and ARM was rebuilt in slot3
(`build-arm-noble/bin/willeq` at 16:47:33 UTC, exit 0). The two post-fix runs
use that binary and nothing else.

| run | binary | purpose | result |
|---|---|---|---|
| P6 `p6_postfix_camera` | slot3 @2f0bd914 + T459 trace | P3's schedule unchanged, plus `-d 1 --log-module=MOVEMENT:DEBUG` for B525 | clean |
| P7 `p7_cursor_after_fix` | slot3 @2f0bd914 + T459 trace | cursor stills: mouse moves and inventory, dumps every 20 frames | clean |

That brings the total to **7 of 12** Pi invocations.

- **B520 cursor, fixed live.** P6 and P7 both log `software cursor texture is
  now LIVE (frame 1)` and **no NULL transition for the whole session**. Every
  baseline run went NULL on frame 2.
  - Stills are in `review/T459/pi/after_T460/`: 4x crops centred on the
    injected cursor position, with the full frames alongside. Positions are
    400,300 before any move, 600,420 after +200,+120, and 240,220 after
    −360,−200, the last also with the inventory open.
  - Each crop has a small black-and-white arrow-shaped element at its centre.
    The baseline crops (`review/T459/pi/cursor_absent_*`) have none.
  - Nothing here is a visual judgement.
- **B521 heading, fixed live.** The P6 `/move` drove 864 bridge
  `setPlayerPosition` writes, all with headings between 371.6 and 372.8
  (0..512 scale). The camera yaw changed once, −54° → 81°, as the player
  turned to face the target. Baseline P3 on the same schedule jumped
  437.17 → 76.39.
- **B525 (a), refuted.** In P6 there were 0 `POS S->C [SELF]` lines and 0 bridge
  `setPlayerPosition` writes during forward-run-1 (292 frames) and
  forward-run-2 (351 frames). The only self `OP_ClientUpdate` of the session
  arrived at 12:49:21.833, the zone-in confirmation ("Zone connection
  complete!"), 19 s before the game was ready. **eqemu does not echo our own
  updates while we run.** B525 (b), keyboard during `/follow` or an
  auto-attack chase, was not exercised.
- **Camera jerk, post-fix:** one camera state per forward run (height 10.0,
  constant yaw), zero A-B-A flips. B507's clamp did not fire; this session
  started from a different pose and heading.
- **Still unreproduced:** Will's jerk (B508). With B521 fixed and B525 (a)
  measured absent, the live candidates left are B507 (terrain-dependent
  latched height) and B525 (b).

### Pose and server
Restored at the end: world down, then the db pose was set back to the arrival
values. See the final lines of this Review.
