# T64 — Extract the frame-dump video capture flag from its parked branch and make it a first-class tool

**Slot: t3. x86 only. No Pi. NO EQEMU** — this unit builds and proves the
capture mechanism; it does not need a live session to do that.

**Territory: `include/client/application.h`, `src/client/application.cpp`
(the flag), a new encode script under `scripts/`, `tests/`, and
documentation (`CLAUDE.md`, `refactor/EXECUTOR_PROTOCOL.md`).**

Do not touch the renderer, the GLES2 driver, or the equipment/bone path —
sibling units own those. `refactor/deferred.md` is append-only and shared.

Read `refactor/EXECUTOR_PROTOCOL.md` (including its "Media for Will's
review" section), then
`/mnt/projects/fable/videos/p12_anim_check/README.txt` **in full** — it is
the field report from the session that built this, and it records the
encoding convention and the traps.

## Why this unit exists

Will asked for video recording so that temporal things — a weapon
appearing during an attack swing, casting, zoning, particles — can
actually be reviewed. Stills cannot capture them; T63 is currently
reduced to burst-capturing a swing and hoping a frame lands.

**The mechanism already exists and was never merged.** The P12
anim-video-check session (2026-07-26) built `--dump-frames-dir <dir>
--dump-every <N>`: a hook in `Application::render()` immediately after a
successful `processFrame()`, writing numbered PNGs through the existing
`IrrlichtRenderer::saveScreenshot()` path. Off by default, zero cost when
unused. It lives on the parked branch **`task/anim-video-capture`**,
commit **`6c48bb4c`**.

**Why it was parked, and why that no longer applies.** That branch also
carried three *workarounds* rather than fixes, so merging it wholesale
would have put hacks into `fable` — the right call at the time
(`refactor/deferred.md`, "Live x86 client bugs (anim-video-capture
session)"). Since then all three were resolved properly and independently:
P23 (`e702341`) fixed the `saveScreenshot` compile guard and root-caused
the `ParticleManager::renderUnified` race (two real races in
`SimulationWorker`), and the missing-equipment-model case is now
`LOG_WARN` + skip in `fable` (`renderer_zone_load.cpp:1614`).

So the reason to keep the branch parked has expired, but nobody went back
for the 55 lines that were worth having. **This unit extracts exactly
that, and nothing else.**

## Step 1 — extract the flag, and only the flag

Take from `6c48bb4c` **only** the `include/client/application.h` and
`src/client/application.cpp` changes:

- `ApplicationConfig::dumpFramesDir` / `dumpFramesEvery`
- `parseArguments` handling for `--dump-frames-dir` / `--dump-every`, plus
  the `--help` lines
- the `create_directories` call in `initialize()`
- the render-thread dump hook in `Application::render()`
- the two counters (`m_dumpFrameCounter`, `m_dumpFrameIndex`)

**Do NOT take** the `constrained_presets.json` additions (capture-session
scratch presets), the `renderer_frame.cpp` particle gating (superseded by
P23's real fix), the `renderer_zone_load.cpp` change (already in `fable`),
or the `irrlicht_renderer.cpp` guard (already in `fable` via P23).
**Verify each of those is genuinely already handled before skipping it** —
do not take my word for it; a wrong assumption here silently drops a fix.

Re-word the comments: they currently reference "the P12 anim video-check
session" as a one-off. This is now a supported tool, so say what it does
and what it costs, not which session birthed it.

## Step 2 — bound it, because unbounded frame dumping is a footgun

The parked version dumps until the session ends. Add **`--dump-max-frames
<N>`**, defaulting to something sane rather than infinite, and stop
dumping (with one clear log line) when the cap is hit. Rationale, and put
it in the comment: P12's 75-second stationary capture produced 576 frames;
at 800x600 PNG that is tens of MB, which is fine on this VM and **not**
fine on the Pi, whose `/tmp` tmpfs is ~242MB and which T61 already had to
work around by deploying stripped binaries.

Keep the existing behaviour that an empty dir or `--dump-every 0` is a
complete no-op.

## Step 3 — the encode step, productised

P12 did the ffmpeg step by hand. Turn it into
`scripts/ci/encode_capture.sh` (or similar — pick a name consistent with
the existing `scripts/` layout):

- Input: a frame directory. Output: an `.mp4` in
  `/mnt/projects/fable/review/<UNIT>/`.
- **Compute the effective fps from the frames' file mtimes — do not
  assume 30 or 60.** This is P12's hard-won convention and the reason its
  videos play back at true elapsed speed. Encode with that measured fps,
  e.g. `ffmpeg -framerate <measured> -i frame_%06d.png -c:v libx264
  -pix_fmt yuv420p -crf 24`.
- Print the measured fps, frame count and real-time span, and **write
  them into the output directory** so the number behind the video is
  never lost.
- ffmpeg 6.1.1 is present at `/usr/bin/ffmpeg`. Fail with a clear message
  if it is absent rather than producing nothing.

## Step 4 — the honesty requirement, which matters more than the code

Per-frame `glReadPixels` + PNG encode is expensive. P12 measured
**~4–7.6 effective fps** on this x86 VM under llvmpipe — far below
real-time. A viewer who does not know that will look at a capture and
conclude the game runs at 5fps.

**Every video this tool produces must carry its measured fps, frame count
and real-time span alongside it**, and the docs must state plainly that
capture fps is an artifact of the capture, **not** a performance
measurement, and that nothing about frame rate may be concluded from it.
Put that in the encode script's output, in the docs, and in the
`review/<UNIT>/README.md` template.

## Step 5 — tests and docs

- Test what is testable: `parseArguments` accepts the flags; defaults are
  off; an absent/zero `--dump-every` is a no-op. The rendering hook itself
  is not unit-testable, and saying so plainly is better than a fake test.
- Prove the whole path end to end **once**: run something that renders
  (the existing `gles2_zone_benchmark` or a short scripted client run),
  dump frames, encode, and confirm a playable mp4. **Check the frames are
  real images, not noise, before encoding** — `saveScreenshot` has a
  documented history here (P10, P20) and a video of garbage is worse than
  no video.
- Document in `CLAUDE.md` (running/debugging section) and add a short
  entry to `refactor/EXECUTOR_PROTOCOL.md`'s media section: **when the
  thing under review is temporal, record video rather than burst-capturing
  stills.**

Deliver the proof video to `/mnt/projects/fable/review/T64/` with a
`README.md`, as the worked example future units copy.

## STOP conditions

- Any of the three skipped changes turning out NOT to be already handled
  in `fable` — report it, do not quietly bring the workaround along.
- The dump hook needing changes outside `Application` to work.
- Frames coming back garbled and not being fixable within this unit's
  territory — report the harness limitation.

## Verification

Full protocol bar. Additionally: the end-to-end proof run, the measured
fps/frame-count/span for it, and confirmation that the flags are a
complete no-op when unused (state how you verified that — a normal run
with no flags should touch no capture code path).

## Review

### What was extracted (from `task/anim-video-capture` @ `6c48bb4c`)

Only `include/client/application.h` + `src/client/application.cpp`, taken
faithfully with comments re-worded to describe a first-class tool instead of
a one-off session (no functional diff vs the parked commit beyond that
plus the added bound):

- `ApplicationConfig::dumpFramesDir` / `dumpFramesEvery` (default `0` /
  disabled).
- `parseArguments` handling for `--dump-frames-dir <dir>` / `--dump-every
  <N>`, plus new `--dump-max-frames <N>` (see "Bound chosen" below) and
  `--help` lines for all three.
- The `create_directories` call in `initialize()` (unchanged logic, now
  also logs the chosen `dumpFramesMaxCount`).
- The render-thread dump hook in `Application::render()`, right after a
  successful `processFrame()` — identical `saveScreenshot()` call and
  `frame_%06llu.png` naming, now additionally gated on
  `m_dumpFrameIndex < dumpFramesMaxCount`.
- The two counters, `m_dumpFrameCounter` / `m_dumpFrameIndex`.

### What was skipped, and the evidence each is already handled

All three verified by direct read of `fable`'s current source (not taken on
faith), diffed against the parked commit's own changes to the same files:

1. **`config/constrained_presets.json`** (the `orangepi_multibuffer` /
   `orangepi_composite` scratch presets) — not taken. These were
   session-scoped capture scaffolding (disable composite entities /
   fireEffects for capture stability), not a fix to anything, and the plan
   named them explicitly out of scope. Confirmed fable's
   `config/constrained_presets.json` is untouched by this unit (only
   `include/client/application.h` / `src/client/application.cpp` /
   `tests/` / `scripts/ci/encode_capture.sh` / `CLAUDE.md` /
   `refactor/EXECUTOR_PROTOCOL.md` / `refactor/deferred.md` changed —
   `scripts/refactor/check_file_sizes.sh fable` confirms only 2 C++ files
   changed).
2. **`src/client/graphics/renderer_zone_load.cpp`** (equipment-model
   FATAL→WARN) — **already present in `fable`**, byte-for-byte the same
   `LOG_WARN` wording and `return;`-without-`equipError=true` behavior as
   the parked commit's fix, at `renderer_zone_load.cpp:1611-1617`. Read
   directly, not inferred.
3. **`src/client/graphics/irrlicht_renderer.cpp`** (saveScreenshot compile
   guard) — **already present in `fable`**: `#if defined(EQT_HAS_DRM) ||
   defined(EQT_HAS_GLES2)` at line 2686, matching the parked commit's
   widened guard exactly (P23, `e702341`).
4. **`src/client/graphics/renderer_frame.cpp`** (particle gating) — **not
   merely handled but superseded by a real fix**: the parked commit's
   workaround gated `renderUnified()` on `particleManager_->isEnabled()`
   to dodge a dangling-pointer race. Current `fable` has NO such gate on
   `renderUnified()` (verified: only the environmental-particle
   `render()` call above it is gated) — P23 root-caused two real races in
   `SimulationWorker` (TSAN: 55 warnings incl. 2 heap-UAF before, 0 across
   3×100k-iteration soaks after) and removed the workaround entirely, per
   `refactor/deferred.md`'s P23 entry.

No STOP condition triggered — all three were confirmed already handled,
none needed re-introduction.

### Bound chosen

`--dump-max-frames <N>`, default **600**. Rationale (also in the
`application.h` field comment): P12's 75s stationary capture wrote 576
frames at 800×600 PNG (tens of MB total on this VM — fine here, not fine on
the Pi's ~242MB `/tmp` tmpfs, which T61 already had to work around with
stripped binaries). 600 comfortably covers a P12-scale session while
keeping a hard ceiling regardless of how long a capture session runs.
Dumping stops with one `LOG_INFO` line once the cap is hit; the counters
otherwise behave exactly as before. Confirmed `--dump-frames-dir` alone
(no `--dump-every`), `--dump-every 0`, and no flags at all all remain a
complete no-op (`tests/test_video_capture_flags.cpp`,
`DirWithoutEveryIsNoOp` / `ZeroDumpEveryIsExplicitNoOp` / `DefaultsAreOff`).

### Encode script interface — `scripts/ci/encode_capture.sh`

```
scripts/ci/encode_capture.sh <frame_dir> <unit_id> [output_dir]
# output_dir defaults to /mnt/projects/fable/review/<unit_id>
```

Writes `<output_dir>/<unit_id>_capture.mp4` and `<unit_id>_capture.txt`
(measured fps, frame count, real-time span, and the honesty note, kept
next to the video so the numbers are never lost). Effective fps is computed
as `(frame_count - 1) / (last_frame_mtime - first_frame_mtime)`, read via
`stat -c %.Y` (sub-second resolution) — never assumed 30/60, per P12's
convention. Refuses to run (clear error, no partial output) if: ffmpeg is
absent, fewer than 2 frames exist, or the measured real-time span is ≤0.
ffmpeg invoked as `-framerate <measured> -i <glob> -c:v libx264 -pix_fmt
yuv420p -crf 24`.

### Proof-run — measured numbers

**Scope note (read this before the numbers):** this unit is NO EQEMU
territory, and the render-thread dump hook only fires once a client has
reached "normal gameplay" (i.e., successfully zoned in) — which requires a
live server connection. So the end-to-end proof does not exercise the
`Application::render()` hook live; it uses `gles2_zone_benchmark`'s own
`--dump-frames`/`--dump-png-prefix` mechanism instead (loads a zone
directly from local EQ files, no server — the same no-eqemu path the
visual gate already uses), which the plan explicitly names as an
acceptable "something that renders" for this proof. It validates
`encode_capture.sh`'s full pipeline against real rendered frames; it does
not independently exercise the exact `Application::render()` code path
(covered instead by build success + full ctest + the `parseArguments`
tests + line-level diff review above). Full reasoning and scope in
`review/T64/README.md`.

- Command: `gles2_zone_benchmark --zone qeynos2 --eq-path data/eq_client
  --frames 60 --warmup 5 --res 800 600 --entities 10 --animate
  --dump-frames 0,1,...,59 --dump-png-prefix <dir>/frame --occluder-budget
  16` (animated NPC entities + player-moving camera, matching the visual
  gate's own invocation style).
- 60 frames written, renamed into the real hook's `frame_%06d.png`
  convention (zero-padded) preserving original mtimes, before encoding —
  the benchmark's own `f<N>` naming isn't zero-padded and would sort
  wrong.
- **Verified real images, not noise, before encoding**: per-frame RGB mean/
  stdev computed (non-uniform, structured — not the flat/near-zero-variance
  signature of a garbage buffer) AND three frames (start/mid/end) visually
  inspected — a player character walking with several animated NPCs on
  qeynos2 zone geometry, sky/ground clearly present. Stills delivered to
  `review/T64/`.
- **Measured**: 60 frames, real-time span **15.034s**, effective fps
  **3.9244** (`review/T64/T64_capture.txt`). Encoded mp4:
  `ffprobe`-confirmed h264, 800×600, 60 frames, 15.29s duration — matches
  the measured span. This number is a capture-mechanism artifact (per-frame
  glReadPixels+PNG save), consistent with P12's ~4-7.6fps range on the same
  class of hardware, and is called out as such in both `T64_capture.txt`
  and `review/T64/README.md`.

### Verification bar — pass/fail

1. `cmake --build build -j24` — **PASS** (clean build, GLES2-config tree,
   includes the new `test_video_capture_flags` target).
2. `ctest --test-dir build --output-on-failure` — **PASS**, 1972/1973
   (only failure: `AudioManagerTest.PreloadCommonSounds`, the documented
   pre-existing ALSA host exception — no new failures, no new skips beyond
   the documented baseline set). `test_video_capture_flags`'s 6 cases all
   pass.
3. `scripts/build-gles2-x86.sh` — **PASS**.
4. Visual gate — **PASS** (`=== VISUAL GATE: PASS ===`), goldens untouched
   (this unit's territory never touches renderer/goldens code).
5. `scripts/build-arm-noble.sh` — **PASS**, cross-compiled binary produced.
6. `scripts/refactor/check_file_sizes.sh fable` — 2 files changed:
   `application.h` (569 lines, ok) and `application.cpp` (2337 lines, WARN
   — but WARN, not FAIL, and pre-existing: `application.cpp` was already
   2277 lines on `fable` before this unit, i.e. already over the 1500-line
   warn threshold; this unit added 60 lines to an already-warned file, no
   new FAIL introduced).

Additionally, per the plan: no-flags-used no-op confirmed via
`VideoCaptureFlags.DefaultsAreOff`/`UnrelatedFlagsLeaveCaptureOff` (parsed
config has empty `dumpFramesDir` / `dumpFramesEvery == 0` with no dump
flags or with unrelated flags present) plus the existing
`test_headless_shutdown` suite (part of the full ctest run above) passing
unchanged, confirming a real no-flags client run still exits cleanly with
no capture-path involvement.

### Docs

- `CLAUDE.md` — new "Video capture (temporal evidence...)" subsection under
  "## Running", with usage, the bound rationale, the encode script, and the
  honesty requirement stated plainly.
- `refactor/EXECUTOR_PROTOCOL.md` — "Media for Will's review" section gained
  a bullet: when the thing under review is temporal, record video, pointing
  at `CLAUDE.md` and `review/T64/README.md`.
- `refactor/deferred.md` — re-titled the "Live x86 client bugs
  (anim-video-capture session)" entry (4 of 5 items now closed, only the
  unrelated befallen SIGSEGV still open), recorded the equipment-model
  closure evidence inline, noted the capture flag extraction and that the
  rest of the parked branch is superseded, and appended a new standalone
  item proposing an on-demand `/record start|stop` slash command as a
  future unit (deliberately not built here).

### Review-media path

`/mnt/projects/fable/review/T64/` — `README.md` (worked-example writeup,
scope note on the proof method), `T64_capture.mp4`, `T64_capture.txt`
(measured numbers + honesty note), and three source stills
(`still_frame000_start.png` / `still_frame030_mid.png` /
`still_frame059_end.png`).

### Deviations

- The end-to-end proof run used `gles2_zone_benchmark`'s built-in frame
  dump instead of a live `willeq --dump-frames-dir` session, because the
  latter requires a zoned (server-connected) client and this unit is NO
  EQEMU territory. This is within the letter of the plan (which names
  `gles2_zone_benchmark` as an acceptable alternative for this exact
  step) but is flagged here explicitly per "deviations flagged, never
  silently absorbed" — the `Application::render()` hook itself was proven
  by build + tests + line-level review, not by a live capture. Documented
  prominently in `review/T64/README.md` so nobody mistakes the proof video
  for a live-gameplay capture.
- No other deviations. All three potentially-skipped changes were verified
  already handled; none required re-introducing a workaround.

Board (`refactor/deferred.md`, append, never delete): re-title the
"Live x86 client bugs (anim-video-capture session)" entry — its
still-open list is stale, since the missing-equipment FATAL item is now
WARN in `fable`. Note that the capture flag has been extracted and the
branch's remaining content is superseded. Also record, as a separate
item, that **an on-demand `/record start|stop` slash command** would be
better than a whole-session flag for live sessions where only a few
seconds matter — deliberately out of scope here, worth its own unit if
someone wants it.
