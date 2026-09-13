# T462 — the static UI's per-frame cost on the Pi: the inventory popup (B506) and the chat re-wrap (B505)

Class A (Will's Pi report: "opening the inventory halves fps"), **eqemu lock +
Pi ownership for the before/after measurement**, slot3. Read T459's Review
(`refactor/T459_pi_live_diagnosis.md`) and `review/T459/README.md` first —
its numbers, its uinput recipe (B510) and its run scripts are your rig.

## Will's ruling (2026-09-13)

Do not chase the old pre-static-UI numbers; account for and reduce the perf
hit of the CURRENT static-UI inventory.

## What T459 measured (Pi, 800x600, DRM, vsync)

- Inventory closed → open: static UI 11.1 → 17.0 ms, wall 48.4 → 37.9 fps
  (Will's binary). The paperdoll is ~0.2 ms/frame amortised (re-render
  ~2.1 ms every ~12 frames + a one-off 243 ms build on first open) — NOT the
  cost. The ~6 ms is the popup's own draw calls.
- With vsync every frame over 16.7 ms is presented at 33.3 ms, which is why
  it reads as "halved".
- **B505:** the chat panel re-wraps its whole 1,000-message buffer every
  frame; static UI grew 11.9 → 22.0 ms over 5 minutes standing still with the
  inventory closed. This is a baseline cost Will pays everywhere and grows
  with session length.

## Tasks

1. **Account.** On x86 first (cheap, deterministic): count the popup's draw
   calls / text entries / state changes per frame (the T393 render pin
   `TheInventoryPopupEmitsTwoTextEntriesPerCoinRow` shows the tree can count
   entries headlessly). Profile where the 6 ms goes (text layout? per-slot
   icon lookups? 2D state switches that break batching? the alpha companion
   bind per draw?). Same for the chat panel's wrap path. Write the account
   into the Review before changing anything.
2. **B505 fix:** wrap on change only (new message, width change, scroll),
   cache wrapped lines; pre-fix failing test that the wrap runs O(changes)
   not O(frames) (a counter seam), plus a correctness test that the displayed
   lines are identical.
3. **B506 fix:** choose from the account — batching / removing redundant
   per-frame work first; a render-target cache of the popup redrawn only on
   change (the `43c56a70` shape U07c2 deleted) only if the account shows the
   draws themselves are the cost. If an RT: memory on the 483 MB board is
   part of the design (size it, count it against the constrained budgets),
   and the paperdoll RT composes into it without re-rendering it. Pixel
   identity against the uncached draw must be pinned by a test (the gate's
   GLES2 device), and hover/drag/tooltip state must invalidate correctly.
4. **Diagnostics from T459** (`renderer_frame.cpp` camera trace + cursor log,
   `overlays_hud_env.cpp` paperdoll timer): keep what is useful as a
   permanent, properly named timing section (`frameTimings_`) or TRACE line;
   delete the `T459`-named INFO lines. If the camera trace goes, restore the
   `getActualFollowDistance` row in `scripts/refactor/check_zero_callers.py`
   (T459 removed it).
5. **Prove on the Pi** with T459's rig: inventory closed vs open, ≥3 repeats,
   same pose; and a 5-minute standing run for B505. Before = fable tip binary,
   after = your branch. Report the per-section split and wall fps; note the
   vsync quantisation when interpreting.

## Proof

Standard verification bar; G-batch goldens for every TU touched (the
static-UI render function is statement-pinned — a reviewed edit updates the
golden IN PLACE per G-correction 32, and say so). Visual gate: no image golden
moves. eqemu + Pi protocols (lock, world down, pose restored, Pi pristine,
≤12 invocations). Media `review/T462/`. Board B540–B549; strike B505/B506 with
evidence.

File territory: `src/client/graphics/ui/` (static panels, chat), the T459
diagnostic lines, `frameTimings_`, tests. **Not** camera/hotkeys/slash
(T461 in slot1), not corpse/combat (T456 in slot2).

STOP: the only effective fix is an RT cache that costs more memory than the
orangepi budget has headroom for (report the numbers); an image golden moves.

## Review

Executor: slot3, branch `task/T462-static-ui-frame-cost` from `f70b6c97`.
Commits: `ff2a421e` (instrumentation + account harness, behaviour-neutral),
`592b2302` (B505 chat wrap cache + atlas-path run batching), `96590c78`
(fallback-path and text batching), plus the docs commit carrying this Review.
Media and README: `review/T462/`. Board: B505/B506 struck; B507/B508/B509
amended (1280x720 addendum); B540, B541 added.

### The headline

**Will's Pi recipe never runs the UI T459 and the x86 captures looked at.**
It passes no `--eqe1-path`, so `renderer_init.cpp` loads no `UIAtlas` and the
whole static UI runs `UIRenderer`'s placeholder FALLBACK path: every
rectangle was an immediate `draw2DRectangle()` during submission, and every
text entry one `CGUIFont::draw()` (board **B540**, class C: whether the recipe
should pass the pack is Will's call). The instrumented pre-fix binary
measured it on the Pi at 1280x720: of a 17.9 ms (closed) / 26.0 ms (open)
Static UI, the text flush was 8.2 / 11.8 ms and the immediate rectangles plus
submission plus the chat wrap were 9.6 / 14.0 ms. Both are now a few draw
calls: **Static UI 1.3 ms closed, 2.5 ms open, 60.0 fps in both states** (the
fable tip held 30.0 fps in both).

### 1. The account (before any fix)

**Counts (x86, headless GLES2, the real atlas, `renderStaticUI()`'s draw list
at 800x600 -- `tests/test_static_ui_frame_cost.cpp`).** A fresh session with
the inventory closed submits 61 draw commands and 25 text entries; open, 159
commands and 49 text entries. The popup's own share is 98 commands (panel 5 =
1 sprite + a 4-rect frame, 22 equipment slots x 2 sprites = 44, 8 general
slots x 2 = 16, 4 coin rows x (sprite + 4-rect frame + decal) = 24, and one
icon per item -- 9 in the fixture) and 24 text entries. Before T462 each
command was one driver draw call and each text entry one font draw.

**What a draw costs (docker/irrlicht-drm/COpenGLES2Driver.cpp).** Every
`draw2DImage()` re-sets the world-view-projection uniform and both sampler
uniforms, ALWAYS issues `glBindTexture` on unit 0 and on unit 1 (the T411
alpha companion -- `bindTexture()` never caches, by design), re-points and
enables three vertex attributes, disables two, and draws from client-side
arrays. `draw2DRectangle()` is the same minus the textures. A text entry adds
CPU on top: `TextBatch::addText` builds a `std::wstring`, `CGUIFont::draw`
allocates two `core::array`s, walks the glyphs, then
`CGUISpriteBank::draw2DSpriteBatch` allocates per-texture arrays and
`draw2DImageBatch` two more, per string. Per-slot icon lookups are NOT a Pi
cost: without the atlas `drawItemIcon()` returns at its first line.

**The Pi account (instrumented pre-fix binary `ff2a421e`, run X1, Will's
flags at 1280x720, inventory A/B x3 in 20 s windows, per state):**

| state | wall fps | frame ms | Static UI | UI Paperdoll | UI Quad Flush | UI Text Flush | remainder | text entries |
|---|---|---|---|---|---|---|---|---|
| closed (n=50) | 42.1 | 24.5 | 17.85 | 0.00 | 0.00 (0 commands) | 8.23 | 9.62 | 36 |
| open (n=27) | 30.1 | 33.3 | 26.02 | 0.21 | 0.00 (0 commands) | 11.79 | 14.02 | 74 |

- "0 commands" is the fallback path: nothing reached `endFrame()`; the
  rectangles were drawn inside the remainder. Opening the inventory: +3.6 ms of
  text flush (+38 entries, ~94 us each), +4.4 ms of remainder (the popup's
  immediate rectangles), +0.2 ms of paperdoll.
- The paperdoll is confirmed as T459 found it: 0.21 ms per frame, 4-6 calls in
  60 at ~2.1 ms (the RTT re-render), one ~150 ms build on first open (T459's
  timer lines in B1rr).
- **The chat wrap** is the remainder's growth: the closed remainder went
  4.2 -> 10.8 -> 13.3 -> 13.4 ms across the four closed windows of X1 as
  nro's GM chat accumulated. On x86 llvmpipe a 500-message buffer cost
  10.3 ms of the frame in `renderChatPanel()` alone (`review/T462/x86/
  account_x86_800x600_prefix.txt`). The live client's buffer is 500 messages
  (`renderer_init.cpp`), not the 1,000 B505 said.
- **Why it reads as halving, at 1280x720:** the fable-tip binary (B1rr) held
  33.3 ms / 30.0 fps in EVERY window, closed and open -- Static UI 7.8 ms
  closed, 14.6 ms open, but any frame over 16.7 ms waits for the next vblank
  (T459). X1 (same code plus the sub-sections) opened at 42 fps closed and
  30 fps open; the two sessions differ in nro's chat volume, not in code.

**The render-target cache (U07c2's `43c56a70` shape) was not needed.** The
account showed the cost is per-draw state setup and per-entry text work, not
fill: batching took the open popup to 2.5 ms of Static UI with no new texture
memory, so the RT and its orangepi budget question never arose. No STOP.

### 2. B505 -- chat wrap on change (`592b2302`)

`ChatWrapCache` (`include/client/graphics/ui/chat_wrap_cache.h`) keeps every
buffered message's wrapped lines, keyed by the buffer's new append sequence
(`ChatMessageBuffer::appendedCount()` / `clearGeneration()`); each frame it
drops the lines of messages evicted from the front and wraps only the new
ones, and rebuilds on a width, timestamp-setting or clear() change.
`wrapChatText()` is the pre-T462 loop verbatim.
- Pre-fix failing test, a counter seam (`ChatPanelState::messagesWrapped`):
  ten frames over 200 unchanged messages wrapped 2,000 times; now 200. Three
  new messages are three wraps, scrolling is zero, 400 past the cap are 400, a
  timestamp toggle re-wraps the 500 buffered once. Red run:
  `review/T462/tests/red_prefix_run.txt`.
- Correctness: the cache's lines equal a verbatim copy of the pre-T462 loop
  kept in the test, through arrivals in batches, front eviction (130 messages
  into a 50-message buffer), an unbreakable 200-character word, a leading
  space, a width change, a timestamp toggle and a clear(); a warm cache that
  lived through 720 arrivals paints exactly the pixels of a cold one.
- x86, 500 messages: `renderChatPanel()` 10.3 ms -> 7 us per frame.

### 3. B506 -- run batching (`592b2302` atlas path, `96590c78` fallback + text)

- `UiQuadBatch` (`include/client/graphics/ui/ui_quad_batch.h`): a run of
  quads sharing their GL state is at most two driver draws -- the first quad
  through the driver's own `draw2DImage()` / `draw2DRectangle()`, which sets
  every piece of state, the rest in one `glDrawElements` over vertices built
  with the driver's own arithmetic, layout and triangle order. GLES2 driver
  only; any other driver gets the per-quad calls. It is in the client, not the
  driver: no Irrlicht rebuild, and no change to any other 2D caller.
- `UIRenderer`: the fallback path now QUEUES its rectangles and blits like the
  atlas path (no panel draws through the driver directly -- grepped), and
  `endFrame()` flushes texture runs and solid runs (a solid run splits where
  `draw2DRectangle()`'s blend enable -- alpha < 255 -- changes).
- `TextBatch::flush()`: the glyphs of every plain entry go into texture runs,
  laid out from a table taken through the public font API exactly as
  `CGUIFont::draw()` lays them out (underhang, advance, sprite rect, the
  default invisible set `L" "`). An entry the table cannot reproduce exactly --
  a line break, a character outside 0..127, a glyph set spanning textures --
  is drawn by the font as before, after flushing the run, so order holds.
- Pins, all red before their commit (`review/T462/tests/red_prefix_run.txt`,
  `red2_fallback_and_text_before_fix.txt`): at most two driver draws per run
  (atlas path, open inventory: was 159 draws for 159 commands in 19 runs, now
  29); the fallback path queues >100 commands and draws at most two per run
  (was 0 commands); the text flush is at most 2 draws on both paths (was 61 /
  70); and the batched flush -- commands and text -- paints EXACTLY the pixels
  of the pre-T462 per-command, per-entry flush over the whole 800x600 frame on
  the atlas AND the fallback path, with every panel, the open inventory, a
  hovered slot, the paperdoll blit inside a run, a scrolled chat box, a
  line-break entry and a non-ASCII entry (`UIRenderer::setBatchingEnabled()`
  is the seam). No hover/drag/tooltip invalidation exists to get wrong: nothing
  is cached across frames except the chat lines.

### 4. T459's diagnostics

- `T459 software cursor texture is now ...` (INFO): deleted.
- The paperdoll INFO timer: now the permanent `staticUIPaperdoll` sub-section
  (`OverlaysComponent::takePaperdollUpdateUs()`), printed as "UI Paperdoll".
- The per-frame camera TRACE line: kept, renamed `CameraFrame` (T459's
  `analyze_camera.py` matches the old `T459 cam` prefix; T462's
  `analyze_camera_runs.py` matches both). `getActualFollowDistance()` keeps its
  caller, so the ZeroCallers row stays removed.
- `/frametiming` now prints under Static UI: `UI Paperdoll`, `UI Quad Flush
  [N commands, M draws]`, `UI Text Flush [N entries, M draws]`, taken after the
  render in `processFrame()` so `renderStaticUI()` -- and the G05 golden --
  did not change (no in-place golden update was needed).

### 5. Proof on the Pi (Will's flags, 1280x720, Testcaster in nro)

Before = `review/T462/pi/bin_before/willeq` (slot3's ARM build of the fable
tip's code); after = `bin_after` (`96590c78`, rebuilt, new strings verified in
the binary). Wall fps is 60 frames over the time between consecutive
breakdown lines.

**Inventory closed vs open** (3 cycles, 20 s windows; windows straddling a
toggle dropped):

| run | binary | state | windows | wall fps | frame ms | scene ms | Static UI ms | quad flush | text flush | remainder |
|---|---|---|---|---|---|---|---|---|---|---|
| B1rr | before | closed | 36 | 30.0 | 33.3 | 4.7 | 7.76 | - | - | - |
| B1rr | before | open | 26 | 30.0 | 33.3 | 4.8 | 14.57 | - | - | - |
| X1 | instrumented | closed | 50 | 42.1 | 24.5 | 3.1 | 17.85 | 0.00 | 8.23 | 9.62 |
| X1 | instrumented | open | 27 | 30.1 | 33.3 | 2.9 | 26.02 | 0.00 | 11.79 | 14.02 |
| A1 | after | closed | 76 | **60.0** | 9.0 | 2.9 | **1.34** | 0.48 (62 cmds, 15 draws) | 0.74 (36 entries, 2 draws) | 0.12 |
| A1 | after | open | 57 | **60.0** | 11.4 | 2.9 | **2.46** | 1.23 (152 cmds, 38 draws) | 0.85 (74 entries, 2 draws) | 0.17 |

Per cycle, A1 closed/open: 1.34/2.49, 1.35/2.44, 1.35/2.44, closed-4 1.33 --
flat; every A1 window is 59.9-60.0 fps. Opening the inventory now costs
+1.1 ms of Static UI and no frames. Vsync note: frames under 16.7 ms present
every vblank; the before binary's 33.3 ms in every B1rr window is the 30 fps
quantisation, not 33 ms of work.

**Five minutes standing, inventory closed, 600 x `/say #loc` at 2/s**
(`chat_fill_5min.script`; 600 replies logged in both runs; per 30 s bucket):

| run | binary | Static UI ms, first -> last bucket (range) | wall fps |
|---|---|---|---|
| B2r | before | 20.96 -> 20.60 (19.1-21.8) | 30.0 in every bucket |
| A2 | after | 1.10 -> 1.08 (1.06-1.10) | 60.0 in every bucket |

B2r was already at ~20 ms in its first bucket and did not grow within the five
minutes, so this pair proves the level, not the growth; the growth is
measured by X1's closed remainder above and T459's P1 (11.9 -> 22.0 ms).

### 6. The coordinator's 1280x720 addendum (fable-tip binary, not fixed here)

- **Spell gems (B509 amended):** DRM matched 1280x720@60Hz and GBM made a
  1280x720 surface; `computeLayout(1280, 720)` puts the column at
  (1244,388)-(1276,672); the in-world dump and its 4x crop
  (`review/T462/pi/gems/`) hold eight slot boxes there, "Mino"/A1 first. The
  buffer has the column at Will's resolution too. Not static UI cost; no fix.
- **Camera jerk (B508 amended, B507 reproduced):** C1, two keyboard forward
  runs (445 and 562 frames, frame dt 27.0 ± 6.8 and 21.4 ± 4.3 ms): follow
  distance 20.0 throughout, zero yaw jumps, zero offset A-B-A, zero backward
  player steps; the one discontinuity per run is B507's 10 -> 3 height latch.
  C2 (every frame dumped, video in `review/T462/pi/`) the same with no latch.
  Not reproduced at 1280x720 either. Not static UI; no fix.

### Pi and server protocol

- Pi invocations: **11 of 12.** Four were wasted, all counted: B1 and B0 died
  at startup (a binary copied out of the checkout finds no `config/`/`data/`),
  B1r and B2 had no `--log-level=INFO`, so the trigger and the breakdown never
  logged (board **B541**). Then B1rr, B2r, C1, C2, X1, A1, A2.
- Pi left pristine: no willeq or injector process, no virtual input device,
  `/tmp` without t462, governor `ondemand`.
- Server: lock `T462` taken at 18:14 UTC; the world was down on arrival and
  brought up with `up.sh --launcher` (one `world`). Testcaster found at zone 34
  (-41.8658, 2723.33, -24.8438, h 331.25), moved only by the forward runs,
  written back and verified by SELECT before `down.sh`.

### Verification bar (slot3, `96590c78` + this docs commit; logs in `review/T462/verification/`)

1. `cmake --build build -j24` (RelWithDebInfo, slot's own Irrlicht): exit 0,
   after the last source change. Every earlier T462 build also exited 0.
2. `ctest --test-dir build -j6`: **4712 of 4712 passed** (170.6 s), exit 0,
   no reruns. It includes FrameRenderOrder (the G05 golden for the static UI's
   render pass -- `renderer_frame_render.cpp` is untouched and the golden did
   not move), FileSizeCeiling, ZeroCallers, DocCounts and the board/doc nets.
   The eight T462 pins run in it; the timing account self-skips.
3. `scripts/build-gles2-x86.sh`: exit 0.
4. Visual gate: `=== VISUAL GATE: PASS ===`, run standalone
   (`scripts/ci/visual_gate.sh`), because `visual_gate` is not registered in
   this build tree's ctest; qeynos2 / gfaydark / befallen, CPU p95 4.96 /
   5.85 / 6.09 ms against ceilings 10.1 / 10.7 / 10.9. **No image golden
   moved:** `git diff --stat f70b6c97 -- tests/goldens` is empty.
5. `scripts/build-arm-noble.sh`: exit 0; `build-arm-noble/bin/willeq` written
   20:25 UTC after `96590c78`, ELF, carrying the new `[{} entries, {} draws]`
   format string and neither T459 diagnostic string. That binary is
   `review/T462/pi/bin_after/`, the one A1 and A2 ran.
6. `scripts/refactor/check_file_sizes.sh fable`: 61 ok, 0 warn, 0 fail (the
   largest T462 file is `static_panels.cpp` at 1,095, down from 1,120).
- eqemu: world down (`down.sh` exit 0), lock `T462` released and verified
  absent (`eqemu_lock_show` exit 1), Testcaster's pose restored and verified.
- Pi: 11 of 12 invocations, left pristine.

### Deviations

- **No 800x600 Pi runs.** The coordinator asked for 800x600 "if cheap"; the
  four B541 runs took that budget. T459's 800x600 numbers stand for
  continuity; the x86 account is at both resolutions.
- **Before binary:** the plan says "fable tip binary". It is slot3's
  `build-arm-noble` of the tip's code (built 16:47 from `2f0bd914` + T459's
  trace commit; the tip's later commits touch no source), preserved before any
  T462 rebuild.
- **Territory:** besides `src/client/graphics/ui/`, T462 edited
  `src/client/graphics/text_batch.{h,cpp}` (the static UI's text path),
  `src/client/chat/chat_message_buffer.{h,cpp}` (the append sequence the wrap
  cache keys on), `renderer_types.h` / `renderer_frame.cpp` /
  `diagnostics_component.cpp` (`frameTimings_`, named in the plan),
  `overlays_hud_env.cpp` + `overlays_component.h` (T459's paperdoll timer,
  named in the plan), and `include/client/graphics/ui/CLAUDE.md`.
- **x86 timings are llvmpipe.** It rasterises on the CPU, so its flush times
  are an account of work, not a Mali-400 prediction; the Pi runs are the proof.
- **Pixel identity is pinned on x86 GLES2 (llvmpipe), not on Mali.** The
  vertex data is the driver's own arithmetic and the Pi's visual acceptance is
  Will's; no Pi capture of the fixed UI was taken.
- **The account's "remainder" is not split further on the Pi.** Input
  hit-testing, the panels' submission and (pre-fix) the chat wrap and the
  fallback rectangles share it; after the fix it is 0.12-0.17 ms.
