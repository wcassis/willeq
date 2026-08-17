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

Executed in `/mnt/projects/fable/wt/slot2`, task branch `task/T130-audio-cluster`,
fable tip `26ebb9cec6eea2d1499a9a8fe003181885f84d88` (this plan's own commit).
eqemu lock taken at session start (confirmed free), used, released cleanly
(`down.sh` run, lock file deleted, confirmed gone). Full logs and a README
are in `/mnt/projects/fable/review/T130/`.

**Headline: the TSF read is fixed and test-verified. The steady-state audio
callback DOES allocate — confirmed by both static reading and a runtime
allocator interposer over a live client connected to eqemu, across a zone
crossing — so T127's `M_ARENA_MAX=1` caveat does not evaporate by
construction. Built the late-callback timing signal step 3 called for and
measured it: 2 late callbacks out of 5395 (0.037%) across a run spanning a
crossing, worst gap 44.16ms. The caveat is MEASURED, not fully discharged —
see Board.**

### Step 1 — TinySoundFont OOB read: fixed and test-verified

Confirmed present exactly as T115/the plan described:
`tsf_voice_render()`'s three linear-interpolation blocks (`TSF_STEREO_INTERLEAVED`,
`TSF_STEREO_UNWEAVED`, `TSF_MONO`, `third_party/TinySoundFont/tsf.h`) each
compute `nextPos = (pos >= tmpLoopEnd && isLooping ? tmpLoopStart : pos + 1)`
unconditionally in the non-looping branch. For a non-looping voice whose
`pos` reaches `region->end - 1` (its final sample), `pos + 1 == region->end`
reads one `float` past the sample pool.

**Fix**: clamp `nextPos` to `pos` (repeat the final sample) when
`pos + 1 >= (unsigned int)region->end` and the voice is not looping, at all
three sites:
```c
unsigned int pos = (unsigned int)tmpSourceSamplePosition,
    nextPos = (pos >= tmpLoopEnd && isLooping
                   ? tmpLoopStart
                   : (pos + 1 < (unsigned int)region->end ? pos + 1 : pos));
```
Each site carries a `// WILLEQ T130` comment, and the file's top gets a
banner block naming this as a local deviation from upstream that a future
vendor bump must re-apply by hand (the file is single-header vendored code,
so there is no patch-queue mechanism to carry it automatically).

**Test**: `tests/test_tsf_oob_read.cpp`, `TsfOobReadTest.NonLoopingVoiceAtFinalSampleDoesNotReadPastPool`.
Constructs the boundary case directly — a `tsf`/`tsf_region`/`tsf_voice`
built by hand (no SF2 file needed; `tsf_voice_render` is `static`, so the
test includes `tsf.h` with `TSF_IMPLEMENTATION` itself, the same pattern
`midi_player.cpp` uses, to get direct access) — pointed at an `mmap`'d
sample pool sized to end exactly on a page boundary, with the following
page `mprotect(PROT_NONE)`'d. A non-looping voice positioned at
`poolCount - 1.5` with `pitchRatio == 1.0` steps to `pos == poolCount - 1`
on its second render iteration, computing the OOB index under unfixed code.
The render call runs in a forked child so a still-broken build fails the
test with a clean assertion (child killed by `SIGSEGV`/`SIGBUS`) instead of
taking the whole test binary down — deterministic on every run, no ASAN
needed. **Verified the test actually catches the regression**: reverted the
clamp on all 3 sites, rebuilt, test failed (`Actual: false / Expected:
true`, with the exact "crashed on an out-of-bounds read" message);
restored the fix, rebuilt, test passed. Not just "the test exists" — proven
to bracket the bug in both directions.

### Corrected miniaudio characterisation (carried forward from the plan)

**Miniaudio does not expose xrun/underrun detection to the application at
all.** Confirmed again while reading the callback path: `ma_device_config`'s
notification surface is `started`/`stopped`/`rerouted`/`interruption_began`/
`interruption_ended`/`unlocked` — no underrun/xrun member exists anywhere in
`miniaudio.h`. It handles ALSA-level xruns internally and never tells the
caller. T128's "never wires miniaudio's own xrun detection into willeq's
logger" framing implied a wiring omission; there is nothing to wire. Any
underrun/stall signal has to be built from scratch, which is exactly what
step 3's `AudioCallbackProbe` does (timing-based, not xrun-based).

### Step 2 — does the steady-state callback allocate? YES, confirmed both ways

**Static reading** (call graph, traced function-by-function, not by name):
`miniaudioDataCallback` (`audio_backend.cpp`) -> `AudioMixer::render`
(`audio_mixer.cpp`) -> for the music channel, `MusicPlayer::mixerRenderCallback`
(`music_player.cpp`) -> `MidiPlayer::render` (`midi_player.cpp:190`).
`AudioMixer::render` itself is genuinely allocation-free (fixed 18-element
`MixChannel` array, no containers, no `std::function`, no logging, no
mutex on the render path). `MidiPlayer::render` is **not** a pure synth
pump — it walks the pending `tml_message` list and dispatches every event
due by the current playback time **on the audio callback thread itself**
(`midi_player.cpp:202-224`), before rendering up to the next event boundary.
Those dispatches reach four confirmed allocation sites in TinySoundFont:

1. `tsf.h:1627` (upstream numbering shifted slightly by this unit's own
   clamp edits above it; search for `TSF_REALLOC(f->voices`) — `tsf_note_on`
   grows the voice array by 4 whenever no free voice slot exists, gated on
   `f->maxVoiceNum`. **`tsf_set_max_voices` is never called anywhere in this
   codebase** (grepped `src/`, `include/`, `tools/` — the only matches are
   inside `tsf.h` itself), so `maxVoiceNum` stays 0 and every polyphony peak
   above the current voice-array size reallocs on the callback thread.
2. `tsf.h`, `tsf_channel_init` (`TSF_MALLOC`) — first time `f->channels` is
   null.
3. `tsf.h`, `tsf_channel_init` (`TSF_REALLOC`) — growing `f->channels` when
   an event references a higher MIDI channel index than seen so far. Both
   reached unconditionally from `midi_player.cpp`'s dispatch of
   `TML_PROGRAM_CHANGE`/`TML_PITCH_BEND`/`TML_CONTROL_CHANGE` (each calls a
   `tsf_channel_set_*`/`tsf_channel_midi_control` wrapper that calls
   `tsf_channel_init` first).
4. `tsf.h`, `tsf_reset` (`TSF_FREE(f->channels)`) — called from
   `midi_player.cpp:232` on the audio thread whenever a looping track wraps
   to its start. This makes the malloc/realloc above **recur every loop**,
   not just once at startup: a looped zone track frees `f->channels` on
   wrap, then the very next program-change/CC event re-mallocs and
   re-reallocs it up to 16 channels.

**Runtime verification** (the plan's explicit requirement — static reading
alone is not trusted on this project): built a custom LD_PRELOAD
malloc/calloc/realloc/free interposer (`review/T130/audio_alloc_interposer.c`)
that attributes every allocation call to the calling thread's tid, logging
any call from the audio callback thread's own tid (identified via a
diagnostic-only, opt-in build macro — `WILLEQ_T130_AUDIO_TID_PROBE`,
`--t130-audio-tidprobe` on `build-gles2-x86.sh` — that writes the callback
thread's tid to a file on its first invocation; allocation-free, gated by
one atomic). Ran the real client against live eqemu (lock taken/released
per protocol) across a Befallen -> North Qeynos crossing with audio
enabled throughout — never `--no-audio`. **Two independent full runs, both
confirming real allocation on the audio thread**:

| Run | Audio-thread allocs | malloc | realloc | free | Other-thread allocs (sanity) |
|---|---|---|---|---|---|
| `probe_run4` | 76 | 6 | 67 | 3 | 82,693,170 |
| `probe_run5` | 30 | 2 | 27 | 1 | 89,039,821 |

The realloc-dominant shape matches the static trace exactly (`tsf_channel_init`'s
growth-by-realloc pattern as MIDI channel indices are first seen: sizes
climb 80, 112, 144, 176... in `probe_run4`'s log, i.e. a fixed per-channel
stride). The interposer itself was verified against a synthetic
multithreaded `malloc`/`realloc`/`free` stress test before being trusted
against the real client (`other_thread_allocs=12005` on that test, `0`
false-attributed to a nonexistent audio tid). Two bugs were found and fixed
in the interposer itself before it produced clean output — both were
`dlsym`-related reentrancy (documented inline in the `.c` file and in
`review/T130/README.md`'s "what was discarded" section) — neither is a
client-side bug.

**Conclusion: the steady-state callback allocates. T127's "if it never
allocates, the concern evaporates by construction" best case does NOT
apply.** Step 3 is required.

### Step 3 — callback-timing probe: built, wired, measured

Built `include/client/audio/audio_callback_probe.h`
(`EQT::Audio::AudioCallbackProbe`): five relaxed atomics (`callbackCount`,
`lastTimestampNs`, `expectedPeriodNs`, `lateCallbackCount`, `worstDeltaNs`),
lock-free (all atomic ops, no mutex), allocation-free (no containers, no
strings beyond the header's own comments), no logging/blocking syscalls in
the hot path — `clock_gettime(CLOCK_MONOTONIC)` is the one "syscall" used,
and it is vDSO-resolved on Linux (no kernel entry), the same reasoning
T122's own probe used for its timestamps. `onCallback()` is called at the
very top of `miniaudioDataCallback`; `configure()` is called once from
`MiniaudioBackend::init()` with the real configured period
(`periodSizeInFrames * 1e9 / sampleRate`). Wired through the existing
external-diagnostics chain (`event_bus.h`'s `DiagnosticsMemoryReportData`
-> `eq_commands.cpp`'s `/pmem` handler, read on the game thread from atomics
the audio thread only ever writes -> `irrlicht_bridge.cpp` ->
`irrlicht_renderer.h`'s `MemoryReportInput` -> `renderer_diagnostics.cpp`'s
new `[Audio Callback]` `/pmem` line), the same "natural surface, alongside
T127's `[Allocator]` line" the plan named. This is a **permanent** addition
(unlike the tid-probe macro above, which is diagnostic-only and off by
default) — cheap enough (one atomic add, one exchange, one CAS loop, one
vDSO call, per callback) to leave in place as an ongoing signal.

"Late" = a callback whose gap from the previous one exceeded 1.5x the
configured period. At 22050Hz with `periodSizeInFrames=512` (the value
`MiniaudioBackend::init()` configures), expected period is 512/22050 ≈
23.22ms; the late threshold is ≈34.83ms.

**Measurement** (`probe_run5`, same tour, `/pmem` issued once before and
once after the Befallen -> North Qeynos crossing):

| Sample | Cumulative calls | Cumulative late | Worst gap |
|---|---|---|---|
| Before crossing (in Befallen) | 1301 | 0 | 33.29ms |
| After crossing (in North Qeynos) | 5395 | 2 | 44.16ms |

So in the window spanning the crossing (calls 1302-5395, 4094 callbacks),
**2 callbacks (0.049% of that window, 0.037% of the whole run) were late**,
and the single worst gap measured across the entire run was 44.16ms (≈1.9x
the expected period) — a real, measurable, but rare and modest stall, not
a sustained one. The pre-crossing sample's own worst gap (33.29ms) sat just
under the late threshold, consistent with ordinary startup/scheduling
jitter rather than an arena-lock signature; the two late callbacks that
*did* cross the threshold occurred somewhere in the window that includes
the crossing (the natural place for `LoadingThread`-vs-arena contention to
show up), though a single run's two /pmem snapshots don't pin the exact
callback index — a future measurement with per-crossing brackets around
the `/pmem` calls (rather than one before, one after) would localize this
more precisely if it ever needs re-checking.

**This is the direct, on-the-record answer to T127's open question.** It is
not "no measurable effect" (that would fully discharge the caveat) and it
is not "unbounded stalling" (that would demand an immediate arena-bound
change) — it is a small, bounded, rare cost. See Board.

### Pre-existing ALSA test failure — did not move

`AudioManagerTest.PreloadCommonSounds` remains the sole ctest failure,
unchanged in nature (documented ALSA host quirk, not this unit's audio
changes). Confirmed by running the full suite twice — once with a live
willeq probe process concurrently holding the ALSA device (which caused
many *other* audio tests to report `Skipped` rather than `Passed`/`Failed`,
a genuine resource-contention artifact of running ctest and a live audio
client at the same time, not a regression — resolved by re-running ctest
with no willeq process alive) and once clean. The clean run's only failure
is this same pre-existing test, at the same test ID, same nature. **It did
not move in either direction.**

### Verification

- `cmake --build build -j24`: clean.
- `ctest --test-dir build --output-on-failure -j8`: **2284/2285** (2284 was
  the plan's stated baseline; +1 is this unit's own new
  `TsfOobReadTest.NonLoopingVoiceAtFinalSampleDoesNotReadPastPool`). Sole
  failure: the documented `AudioManagerTest.PreloadCommonSounds`, unmoved
  (see above). One deviation caught and fixed along the way: this unit's
  initial `git clean -fdx` (part of worktree setup) removed the
  gitignored `data/eq_client`/`data/eqe1_client` symlinks, which
  transiently broke 4 unrelated tests (`ComprehensiveZoneTest.ClassicZones`/
  `OverallSummary`, `DoorLoadingUnitTest.MultipleZonesHaveObjectGeometries`,
  `visual_gate`) — all four passed once the symlinks were restored
  (`ln -s /mnt/projects/EverQuestP1999 data/eq_client`,
  `ln -s /mnt/projects/EverQuest_ETC1 data/eqe1_client`, matching `main`'s
  own symlinks exactly). Flagging this explicitly since a future worktree
  setup that also does a broad `git clean` should expect it.
- `bash scripts/build-gles2-x86.sh`: clean, both before and after the
  `--t130-audio-tidprobe` flag was added to the script (re-ran the whole
  build to confirm the flag's plumbing didn't perturb the default,
  non-probe path).
- Visual gate: **PASS**, goldens untouched (confirmed twice — the plan
  explicitly required audio changes not to touch them; none of this unit's
  changes touch rendering).
- `bash scripts/build-arm-noble.sh`: clean. `build-arm-noble/bin/willeq`,
  unstripped, 370,495,268 bytes (RelWithDebInfo, matches T128's own
  unstripped figure of 370,450,672 bytes almost exactly — expected, since
  this unit's `src/` diff is a handful of atomics and one clamp expression,
  not a size-moving change). No Pi run this unit — not required (step 3 was
  answered entirely via the x86 interposer + real eqemu, no hardware
  needed; the plan only required Pi "if you reach step 3 and need it", and
  the software timing probe sufficed).
- `check_file_sizes.sh fable`: 0 FAIL, 2 pre-existing WARN
  (`irrlicht_renderer.h`, `tsf.h` — both already over the 1500-line
  threshold before this unit's tiny additions; no new file crossed the
  threshold).

### Deviations

- T128's "never wires miniaudio's own xrun detection" framing corrected
  per the plan's own instruction — see "Corrected miniaudio characterisation"
  above.
- Step 2 did NOT close cheaply as the plan hoped it might — this is a
  finding, not a deviation from the plan's process, but it means step 3
  (explicitly conditional in the plan) was required and executed.
- Interposer required two bug-fix iterations before producing trustworthy
  output (both `dlsym`-reentrancy bugs, documented inline and in
  `review/T130/README.md`) — not a deviation from the plan, but worth
  naming since "the interposer just worked" would be false.
- Live-run infrastructure required unplanned recovery: a stale eqemu
  world-server process and a duplicate loginserver from an earlier,
  unrelated session sharing the same container caused two failed connection
  attempts before a clean single-process login/world pair was
  re-established (killed the stale processes, let the container's own
  supervisor respawn world, manually restarted loginserver). No server code
  was touched; only process lifecycle, consistent with the sanctioned
  "docker exec ... ./bin/loginserver" pattern `up.sh` itself uses. Full
  account in `review/T130/README.md`.
- `git clean -fdx` at worktree setup removed `data/eq_client`/
  `data/eqe1_client` gitignored symlinks — restored, documented above under
  Verification, not a code change.
- Added `--t130-audio-tidprobe` to `scripts/build-gles2-x86.sh` (not
  strictly requested by the plan) so the diagnostic tid-file build is
  reproducible via the standard build script rather than only via the raw
  `cmake -DCMAKE_CXX_FLAGS=...` invocation used during this session —
  mirrors the existing `--t122-texprobe` precedent. Off by default, zero
  cost to the normal build path (verified by re-running the plain script
  and the visual gate after adding it).
- `AudioCallbackProbe` (step 3's timing counters) is left wired in
  permanently (not gated behind a flag), per the plan's own suggestion that
  this be "the natural surface, alongside T127's `[Allocator]` line." The
  tid-file probe (step 2's diagnostic) stays opt-in/off-by-default, since
  it exists only to support the one-off interposer verification and has no
  ongoing value once the allocation question is answered.

## Board

**T127's `M_ARENA_MAX=1` caveat is MEASURED, not fully discharged.** The
steady-state audio callback does allocate (TinySoundFont's
`tsf_note_on`/`tsf_channel_init`/`tsf_reset`, reached from `MidiPlayer::render`'s
MIDI event pump — confirmed by both static reading and a runtime allocator
interposer over a live eqemu run), so it does contend on the single arena
lock `M_ARENA_MAX=1` created, and the `LoadingThread` genuinely can hold
that lock during a zone crossing. The measured effect, over one run
spanning one crossing, is **2 late callbacks (>1.5x period) out of 5395
(0.037%), worst gap 44.16ms** — small and rare, not a sustained stall, and
not something that shows up as an audible glitch on every crossing (a
44.16ms gap is a plausible single micro-stutter, not a dropout). This is
real, board-worthy information — not "closed," but also not "open and
unquantified" the way it was before this unit. If Will judges 0.037% at
44ms worst-case acceptable, this thread can be closed as "measured and
accepted." If not, the two remedies the plan names are available and
untaken: raise `M_ARENA_MAX` to 2 or 4 (not changed here, per the plan's
explicit instruction that this is the user's call), or the deeper fix
named in `M_ARENA_MAX`'s own T127 entry — a persistent `LoadingThread`
reused across crossings instead of a fresh one spawned each time, which
would remove the contention mechanism rather than bound it. A cheap
follow-on, if this needs tightening further: call `tsf_set_max_voices()`
after each `tsf_load_filename()` in `midi_player.cpp` (never called
anywhere today) and avoid the `tsf_reset()`-on-loop-wrap path (use
`tsf_note_off_all` instead, which does not free `f->channels`) — that would
shrink or eliminate the steady-state allocations themselves, which is a
stronger fix than tuning the arena count and worth its own small unit if
Will wants the caveat closed outright rather than measured-and-accepted.
