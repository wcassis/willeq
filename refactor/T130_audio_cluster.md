# T130 — The audio cluster: the OOB read, and whether the arena can touch the callback

**Slot: whichever is free. x86 first. eqemu lock only if a live run is needed.
Pi only if step 3 is reached.**

**Territory: `third_party/TinySoundFont/tsf.h` (vendored), `src/client/audio/`,
`tests/`, `refactor/`, `/mnt/projects/fable/review/T130/`.**

Read T128's Review (the audio finding), then the T115 `deferred.md` entry for
the TinySoundFont read.

## Correction to carry in — T128's framing was imprecise

T128 reported that `MiniaudioBackend::init()` "never wires miniaudio's own xrun
detection into willeq's logger."

**Checked while scoping: miniaudio does not expose xrun detection to the
application at all.** Its notification types are `started`, `stopped`,
`rerouted`, `interruption_began`, `interruption_ended`, `unlocked` — there is
no underrun notification. It handles ALSA-level xruns internally and never
tells the caller.

So this is not a wiring omission. **Any underrun signal has to be built**, and
that changes the cheapest path to an answer — see the ordering below. Correct
the record when you write this up.

## Do these in order — the cheapest decisive check is first

### Step 1 — Fix the TinySoundFont out-of-bounds read

`tsf.h:1295` interpolates `input[nextPos]` where
`nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1)`. For a
**non-looping** voice on its **final** sample, `pos + 1` reads one element past
the sample pool. Confirmed still present at the line above. ASAN caught it in
T115 (`heap-buffer-overflow READ of size 4, 0 bytes after an 884016-byte
region`, on the miniaudio callback thread) and it has been open since.

Fix by clamping `nextPos` for the non-looping case. The file is **vendored**,
so patch in tree and mark the change clearly as a local deviation from
upstream so a future vendor bump does not silently drop it.

This is independent of everything below — do it regardless of what steps 2 and
3 conclude.

### Step 2 — Does the audio callback allocate at all?

**This is the decisive question and it may close the whole arena concern
without any instrumentation.**

The worry from T127 is that `mallopt(M_ARENA_MAX, 1)` makes the real-time audio
callback contend on a single malloc arena lock that the `LoadingThread` can
hold during zone load. **But a callback that never calls `malloc`/`free` never
touches that lock, and the concern evaporates by construction.**

So: establish whether the steady-state callback path —
`miniaudioDataCallback` → `AudioMixer::render` → `MusicPlayer::mixerRenderCallback`
→ `MidiPlayer::render` → `tsf_render_float`, plus any sound-effect voices —
allocates.

Read it, and then **verify rather than trust the reading**: a counter
incremented from an allocation hook, or an ASAN/`malloc` interposer active only
during a run, is enough to turn "I believe it doesn't" into "it did not, across
N seconds of playback including a zone crossing". Static reading alone is not
sufficient — this project has been wrong about reachability repeatedly.

**If it never allocates in steady state, say so with the evidence and stop.**
The `M_ARENA_MAX=1` audio risk is then closed, and step 3 is unnecessary. That
is the best outcome available and it is cheap.

Note the interesting case: **initialisation** allocates (`tsf_load_samples` is
in T115's report), but that is startup, not steady state. Be precise about
which you measured.

### Step 3 — Only if step 2 cannot clear it: build a callback-timing signal

If the callback does allocate, build the underrun signal miniaudio does not
provide: record wall-clock deltas between callback invocations, and count those
exceeding the expected period.

Same constraints that made T122's probe trustworthy: **lock-free,
allocation-free, no logging or syscalls in the callback.** Timestamp into an
atomic, bump an atomic counter, and report from elsewhere — `/pmem` is the
natural surface, alongside T127's `[Allocator]` line.

Then measure across a zone crossing with the arena bound in place, and report
late-callback counts. That is the direct answer to T127's open question.

## What to establish

- The TSF read fixed, and confirmed gone (an ASAN run over an audio-enabled
  tour is the natural check — T115's report is the before).
- **Whether the steady-state audio callback allocates**, with runtime evidence.
- If it does not: the `M_ARENA_MAX=1` audio risk **closed**, and T127's caveat
  discharged in full.
- If it does: late-callback counts across a zone crossing, and whether the
  arena bound is implicated.

## Do not

- **Do not disable audio** to make anything easier. That reflex is how the
  original ASAN blind spot formed (T115 ran `--no-audio` from run 2 onward and
  lost coverage of this entire subsystem).
- **Do not change `M_ARENA_MAX`.** If the measurement argues for 2 or 4, report
  it; the choice is the user's.
- **Do not paper over**: no guard that skips the interpolation, no silencing of
  a signal you have not explained.

## Verification

- x86 suite (current total **2284**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`) plus ARM cross-compile.
- **A test for the TSF clamp** — the boundary case is easy to construct
  directly: a non-looping voice rendered to its final sample.
- Note the pre-existing ALSA failure is an audio test. **If your change moves
  it in either direction, say so** — do not let it blend into the baseline.
- **Visual gate goldens must not move** (audio changes should not touch them;
  if one moves, something is wrong — STOP and report).

## Review

(executor fills: the TSF fix and its test, with the vendored-file deviation
marked; **whether the steady-state callback allocates, and the runtime evidence
for it**; whether the arena risk is closed or measured; if measured, the
late-callback numbers; the corrected characterisation of miniaudio's xrun
capability; deviations)

## Board

Say whether T127's `M_ARENA_MAX=1` caveat is now **fully discharged**. If it
is, the crash arc's last open thread closes with it — say that plainly. If it
is not, say exactly what remains and what it would take.
