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

(executor fills: exactly what was extracted vs skipped and the evidence
each skipped item is already handled; the bound chosen and why; the
encode script's interface; the proof-run numbers; where the docs landed;
review-media path; deviations)

Board (`refactor/deferred.md`, append, never delete): re-title the
"Live x86 client bugs (anim-video-capture session)" entry — its
still-open list is stale, since the missing-equipment FATAL item is now
WARN in `fable`. Note that the capture flag has been extracted and the
branch's remaining content is superseded. Also record, as a separate
item, that **an on-demand `/record start|stop` slash command** would be
better than a whole-session flag for live sessions where only a few
seconds matter — deliberately out of scope here, worth its own unit if
someone wants it.
