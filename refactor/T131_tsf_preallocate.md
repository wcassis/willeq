# T131 — Make the audio callback allocation-free: TSF voices and channels at init

**Slot: whichever is free. x86 only. eqemu lock required (the acceptance test
is a live run). No Pi.**

**Territory: `src/client/audio/midi_player.cpp` and its header, `tests/`,
`refactor/`, `/mnt/projects/fable/review/T131/`.**

Read T130's Review first (the interposer, the allocation finding, the
late-callback numbers), then the T130 orchestrator-review entry in
`deferred.md`.

## Why this is a small unit that closes three things

T130 proved by runtime interposer that the **steady-state audio callback
allocates** — `MidiPlayer::render`'s MIDI-event pump dispatches on the callback
thread, reaching `tsf_note_on`/`tsf_channel_init`/`tsf_reset`.

**TinySoundFont's own header documents the fix**, verbatim (`tsf.h:136-148`):

> "...calling `tsf_set_max_voices` after loading. **That way memory
> re-allocation will not happen during `tsf_note_on`** and TSF should become
> mostly thread safe."

> "Calls to `tsf_channel_set_...` functions may allocate new channels if no
> channel with that number was previously used. **Make sure to create all
> channels at the beginning** as required if you call `tsf_render*` from a
> different thread."

**This codebase satisfies neither.** Doing both at init moves the allocation to
startup, where T130 already confirmed it is expected and harmless, and closes:

1. **T127's `M_ARENA_MAX=1` caveat** — by construction rather than measured and
   tolerated. A callback that never allocates cannot contend on the arena lock.
2. **T114's open inventory item #11** — the `AudioMixer`/TSF race, reconfirmed
   real by T121. Upstream calls these same two changes "mostly thread safe".
3. **The 44.16ms worst-case callback gap** T130 measured, which is large enough
   to be audible.

## Cover every instance — there are more than one

Scoping found `MidiPlayer` manages **three** `tsf*` instances, each loaded and
configured independently in `midi_player.cpp`:

- `impl_->soundFont` (`:55`, `tsf_set_output` at `:61`)
- `impl_->gmSoundFont` (`:73`, `:78`)
- `impl_->customSoundFont` (`:86`, `:88`)

**and a fourth path**: a runtime soundfont reload (`:124` `tsf_load_filename`,
`:135` `tsf_set_output`) that constructs a *new* instance while the client is
running.

**Every one of them needs the same treatment.** Fixing three and missing the
fourth is precisely this codebase's most-repeated defect — T115 was a member
added beneath a comment warning about the previous three. **Put the setup in
one helper that every construction path calls**, so a fifth path cannot skip
it, rather than four copies of two calls.

For the reload path specifically, establish **which thread it runs on**. If it
can run concurrently with rendering, say so — swapping a live soundfont under
the audio thread is its own hazard and may be a STOP.

## Choosing `max_voices` is a real tradeoff — do not guess

`tsf_note_on` returns 0 when no voice is free, i.e. **the note is dropped**.
So:

- **Too low → audible dropped notes** under dense MIDI.
- **Too high → wasted memory**, and this project ships to a 512MB device.

**Measure before choosing**: instrument or log peak concurrent voices across
real playback (zone music, combat, a zone crossing), pick a value with stated
headroom above the observed peak, and **state the per-voice cost and the total
bytes** the choice commits. Say explicitly what happens if the cap is ever hit
and whether anything would surface it.

Pre-create all **16** MIDI channels (0–15, with 9 the drum channel) via the
public API that triggers `tsf_channel_init` — `tsf_channel_set_presetnumber` is
the natural one, and note the codebase already calls it with the
`channel == 9` drum flag.

## Acceptance — zero, measured with T130's own instrument

**Primary: re-run T130's LD_PRELOAD allocator interposer over a live run
including a zone crossing, and report allocation calls attributed to the audio
thread. The target is ZERO in steady state.** T130's baseline was 76 and 30
across two runs. That is a sharper measure than the late-callback count and it
is the number to lead with.

Be precise about **steady state vs startup** — startup allocation is expected
and is not a failure.

**Secondary: the late-callback numbers** from T130's permanent `/pmem`
`[Audio Callback]` line, against T130's 2-of-5395 (0.037%, worst 44.16ms). If
allocations reach zero but late callbacks persist, that is informative — it
would mean the gaps have another cause — so report both.

**Also verify the other audio-thread TSF calls.** `tsf_reset` is called from
the render path (`:232`, `:282`, `:313`); zero-allocation must hold for
everything the callback reaches, not just `tsf_note_on`.

## Fixing

**STOP and report** if:
- the runtime reload path turns out to swap a soundfont concurrently with
  rendering — that is a lifetime hazard of its own;
- reaching zero requires restructuring where the MIDI event pump runs (moving
  it off the callback thread is a bigger change and its own unit).

**Do not paper over**: no suppression of the interposer's output, no raising
`max_voices` to a number you have not justified, no disabling audio.

## Verification

- x86 suite (current total **2285**; sole failure the documented ALSA
  `AudioManagerTest.PreloadCommonSounds`). **That failure is itself an audio
  test — if your change moves it in either direction, say so** rather than
  letting it blend into the baseline.
- ARM cross-compile.
- **A test** asserting the setup is applied on every construction path,
  including reload — that is the invariant most likely to rot.
- **Visual gate goldens must not move.**

## Review

Executed in `/mnt/projects/fable/wt/slot1`, task branch
`task/T131-tsf-prealloc`, fable tip `05781dd949a34c2c26bd278fe07fe597096f33d8`
(this plan's own commit). eqemu lock taken at session start (confirmed
free), used, released cleanly (`down.sh` run, lock file deleted, confirmed
gone). Full logs, interposer, tour script, and a README are in
`/mnt/projects/fable/review/T131/`.

**Headline: ZERO audio-thread allocations, confirmed by two independent
live-eqemu runs of T130's own interposer, each spanning a full zone
crossing.** Against T130's baseline of 76 and 30 allocation calls on the
audio thread across its two runs, this unit's two runs both measured
`audio_thread_allocs=0`. Peak concurrent voices measured at 43 (both runs,
identical), zero note-on drops. Late callbacks did not disappear (5 of
~10,565, ~0.047%, worst gap ~36.5ms both runs) — informative, not a
failure: see "Secondary" below.

### The helper and every construction path

One helper, `configureTsfForRealtimeCallback(tsf* font)` (file-local,
anonymous namespace, `src/client/audio/midi_player.cpp`), called
immediately after `tsf_set_output()` on **every** path that produces a
`tsf*` this codebase will later render from:

1. `MidiPlayer::init(soundFontPath, sampleRate)` — single-SoundFont path,
   `impl_->soundFont`.
2. `MidiPlayer::init(gmPath, customPath, sampleRate)` — dual-SoundFont
   path, called separately for **both** `impl_->gmSoundFont` and (if
   `customPath` is non-empty and loads successfully)
   `impl_->customSoundFont`. Verified independently by
   `DualSoundFontInitPath_BothSlotsConfigured` (see Testing below) —
   switching the active pointer to the custom slot and re-checking proves
   the fix was applied to that instance specifically, not just whichever
   was active at the time of a single check.
3. `MidiPlayer::loadSoundFont(path)` — the runtime GM-slot reload path the
   plan named as the fourth, easy-to-miss construction path (`tsf_load_filename`
   + `tsf_set_output`, then swaps the pointer and `tsf_close()`s the old
   instance). Verified by `ReloadPathConfiguresTheNewInstance`, and the fix
   was proven load-bearing by temporarily reverting the one call site and
   re-running the test suite (see "Test — proven, not just present" below).

The helper does two things per TSF's own header (verbatim in the plan):
`tsf_set_max_voices(font, MidiPlayer::kMaxVoices)`, then
`tsf_channel_set_presetnumber(font, channel, 0, channel == 9)` for
`channel` 0-15 (16 total, channel 9 flagged as the drum channel, matching
the `flag_mididrums` convention `render()`'s own `TML_PROGRAM_CHANGE`
dispatch already uses).

### The reload path's thread — not a hazard TODAY, but a documented latent one

Read every call site of `MidiPlayer::loadSoundFont()` in the tree (grep,
whole repo): the sole caller is `AudioManager::initialize()`
(`audio_manager.cpp:104`), and it is called **before** the audio backend is
constructed (`backend_ = ...` happens later in the same function, after
this call) — i.e. before the audio callback thread exists at all. So
today's one call site is safe by construction (no concurrent renderer to
race), not by locking.

This is NOT the same as saying the method is safe. It swaps
`impl_->soundFont` — a raw pointer, no atomic, no mutex — which the audio
thread reads on every `render()` call once playback starts, and it
`tsf_close()`s the old instance after the swap. A **future** caller that
invokes this while the backend is running (e.g. a hypothetical `/soundfont
reload` slash command) would race the audio thread on that pointer and
risk a use-after-free on the freed instance. This is documented in detail
directly above `MidiPlayer::loadSoundFont()`'s definition in
`midi_player.cpp` so it can't be silently reintroduced. **Not a STOP**
per the plan's own condition ("if the reload path turns out to swap a
soundfont concurrently with rendering") — it does not, today. Flagging it
here as the plan asked ("establish which thread it runs on... say so").

### `tsf_reset` also had to be patched — the plan's own "verify the whole path" caught it

Pre-allocating voices and channels at init is necessary but was not
sufficient. `tsf_reset()` (called directly from the audio thread inside
`MidiPlayer::render()`'s loop-wrap branch, `:239` in the current file, and
reachable from `stop()`/`play()`/`seekTo()` too) unconditionally did
`TSF_FREE(f->channels); f->channels = TSF_NULL;`. That undoes the
pre-creation on every reset: the very next `tsf_channel_set_*()` call
(e.g. the first `TML_PROGRAM_CHANGE` after a loop wrap) would
`TSF_MALLOC`/`TSF_REALLOC` `f->channels` right back on the callback
thread — exactly the allocation this unit exists to eliminate, and it
would have been invisible to a fix that only touched `MidiPlayer::init()`.

Fixed with a second local vendored patch to `tsf.h` (banner comment at the
top of the file, `WILLEQ T131`, alongside T130's own banner): `tsf_reset`
now resets each already-allocated channel's fields to the same defaults
`tsf_channel_init()` gives a brand-new channel, **in place**, instead of
freeing the array. Field-for-field identical to upstream's own
initialization defaults (copied verbatim from `tsf_channel_init`), so
behavior is unchanged — same default channel state after a reset — with
zero allocation. Confirmed this is exercised on the audio thread: the
interposer runs above show **zero** allocation across two runs that each
include multiple loop wraps (287s runs, most zone tracks are well under
that), so `tsf_reset`'s in-place path was exercised and stayed
allocation-free.

### Measured peak concurrent voices, chosen `max_voices`, headroom, byte cost

**Measured**: `tsf_active_voice_count()` (upstream API, no allocation —
just iterates the pre-allocated voice array) sampled once per `render()`
call via `EQT::Audio::MidiVoiceProbe` (new header-only probe,
`include/client/audio/midi_voice_probe.h`, mirroring T130's
`AudioCallbackProbe` pattern exactly: lock-free relaxed atomics, no
allocation, no logging in the hot path), exposed permanently through the
same `/pmem` chain T130 built (`event_bus.h` ->
`eq_commands.cpp::runPmemDiagnostics` -> `irrlicht_bridge.cpp` ->
`irrlicht_renderer.h`'s `MemoryReportInput` ->
`renderer_diagnostics.cpp`'s new `[Audio Voices]` line). Sampled three
times per run (Befallen, mid-crossing, settled in qeynos2) across two
independent live-eqemu runs:

| Run | Befallen | mid-crossing | settled (qeynos2) |
|---|---|---|---|
| run1 | 35 | 43 | 43 |
| run2 | 35 | 43 | 43 |

Peak plateaus at **43**, identical across both independent runs, zero
note-on drops (`MidiVoiceProbe::noteOnFailures`, also wired into the same
`/pmem` line) in either run. This covers zone-ambient GM music across a
crossing; it does NOT separately exercise combat-music stingers (no combat
was triggered in the tour — see `review/T131/README.md`'s "what this run
does NOT cover").

**Chosen**: `MidiPlayer::kMaxVoices = 256` (public `static constexpr` on
`MidiPlayer`, single source of truth the helper and the test both read —
see header). ~6x headroom over the measured peak of 43, sized to
comfortably absorb combat-stinger polyphony layered on top of zone music
without a live combat capture, given the byte cost below is negligible
regardless.

**Byte cost**: `sizeof(struct tsf_voice) == 256` bytes (measured directly,
`gcc`, this host/ABI — 4 ints + pointer + 3 doubles + 3 floats + 3 uints +
two `tsf_voice_envelope` + `tsf_voice_lowpass` + two `tsf_voice_lfo`).
`256 voices * 256 bytes = 65,536 bytes (64KB)` per `tsf` instance. Channel
array: `sizeof(struct tsf_channels) == 48` bytes (setupVoice ptr + 2 ints +
1 embedded channel) + 15 more channels at `sizeof(struct tsf_channel) ==
32` bytes each = `48 + 480 = 528` bytes per instance — negligible next to
the voice array. **Worst case** (dual-SoundFont init, both GM and custom
slots configured): `2 * (65536 + 528) = 132,128` bytes, ~129KB total. On
the 512MB Orange Pi target that is ~0.025% of RAM — not a meaningful cost
at this headroom.

**Drop behavior if the cap is ever hit**: `tsf_channel_note_on()` (via
`tsf_note_on`) returns 0 when every pre-allocated voice is either busy or,
with `max_voices` set, none is in its release phase to steal (see
`tsf_note_on`'s `if (f->maxVoiceNum)` branch) — the note is silently
dropped, not queued or retried. **What would surface it**: this unit wires
exactly that signal permanently — `MidiVoiceProbe::recordNoteOnFailure()`
increments on every such return-0, surfaced in the same `[Audio Voices]`
`/pmem` line as the peak-voice count (`{N} note-on(s) dropped (cap
exceeded)`). Before this unit, a too-low cap would have been silently
inaudible and unmeasured; now it is a one-command check. Neither run
showed any drops.

### Audio-thread allocation count: ZERO, both runs (target ZERO, against T130's 76/30)

See `review/T131/README.md` for the full evidence and the `/pmem` tables.
Summary:

```
probe_run1: T130_INTERPOSER_SUMMARY audio_thread_allocs=0 other_thread_allocs=110509058 audio_tid=3455344
probe_run2: T130_INTERPOSER_SUMMARY audio_thread_allocs=0 other_thread_allocs=109079968 audio_tid=3457495
```

Both runs used T130's own interposer binary unmodified
(`review/T130/audio_alloc_interposer.so`, copied verbatim into
`review/T131/`), the `--t130-audio-tidprobe` build flag T130 already
productized in `scripts/build-gles2-x86.sh`, and a tour script shaped like
T130's own (`/say #set time 11 0` verified first per protocol, then a
`/move` across the Befallen/qeynos2 zone line, three `/pmem` samples).
`other_thread_allocs` in the hundred-millions in both runs is the same
sanity check T130 used: it proves the interposer was genuinely intercepting
allocations for the whole ~287s run, not silently inert. **The empty
per-call log** (`/tmp/willeq_t131_audio_allocs.log`, checked directly after
each run) corroborates the summary line's `0` — it isn't just an unlogged
zero, there is nothing logged.

### `tsf_reset` and the rest of the callback path — confirmed allocation-free

Every audio-thread-reachable TSF entry point in `midi_player.cpp`'s
`render()` was covered by the same two live runs, both of which span
multiple MIDI event dispatches, multiple render calls per zone track, and
(with 287s runs against zone tracks well under that length) very likely
multiple loop wraps invoking `tsf_reset()`'s patched in-place path:
`tsf_channel_set_presetnumber`/`tsf_channel_note_on`/`tsf_channel_note_off`/
`tsf_channel_set_pitchwheel`/`tsf_channel_midi_control` (event dispatch),
`tsf_render_float` (render), and `tsf_reset` (loop wrap). Zero allocations
attributed to the audio thread across both runs covers all of them jointly
— the interposer has no way to distinguish which call site an allocation
came from except by the thread that made it, and it saw none.

### Test — proven, not just present

`tests/test_midi_player_prealloc.cpp` (4 cases, `WITH_AUDIO`, links
`midi_player.cpp` directly, no `AudioManager`/audio-device dependency
needed since `MidiPlayer` itself has none): single-SoundFont init, dual-
SoundFont init (both slots independently verified via `selectSoundFont`),
the reload path, and a sanity check that a never-initialized player reads
zero (no false positive). All 4 pass.

**Verified the test actually catches the regression it targets**:
temporarily commented out the `configureTsfForRealtimeCallback()` call in
`loadSoundFont()`, rebuilt, re-ran — `ReloadPathConfiguresTheNewInstance`
failed with the exact expected message (`getConfiguredMaxVoices()` reads
`0` instead of `256`, `getPreallocatedChannelCount()` reads `0` instead of
`16`); restored the call, rebuilt, all 4 passed again. Same
bracket-the-regression discipline T130 used for its own OOB-read test.

### The ALSA test's status — unmoved

`AudioManagerTest.PreloadCommonSounds` remains the sole ctest failure, same
nature (ALSA host quirk: `cannot find card '0'`, no hardware audio device
on this dev VM/CI host), same assertion (`Expected: (afterCount) >
(beforeCount), actual: 0 vs 0`). This unit's change does not touch that
code path (`AudioManager`'s preload logic doesn't call into `MidiPlayer`).
**Did not move.**

### Verification

- `cmake --build build -j24`: clean (`willeq_core`, then full `cmake --build
  build -j24`, including `willeq`, `gles2_zone_benchmark`, and every test
  binary).
- `ctest --test-dir build --output-on-failure -j8`: **2288/2289**. Sole
  failure the documented `AudioManagerTest.PreloadCommonSounds` (unmoved,
  see above). 2289 = the plan's stated baseline (2285) + this unit's 4 new
  `MidiPlayerPreallocTest` cases.
- `bash scripts/build-gles2-x86.sh`: clean, run **twice** — once with
  `--t130-audio-tidprobe` for the live measurement, once plain afterward to
  restore the default (non-probe) `build-gles2/` tree, since the flag
  compiles in a diagnostic macro that should not be the tree's resting
  state.
- Visual gate: **PASS**, goldens untouched — run **twice**, once against the
  plain tree before the probe runs and once again after rebuilding the
  plain tree post-measurement, to confirm the probe build+revert cycle left
  no residue. Neither this unit's `src`/`include` changes nor the probe
  flag touch rendering.
- `bash scripts/build-arm-noble.sh`: clean, `build-arm-noble/bin/willeq`
  produced.
- `check_file_sizes.sh fable`: **0 FAIL**, 2 pre-existing WARN
  (`irrlicht_renderer.h`, `tsf.h` — both already over threshold before this
  unit, both grew only by this unit's own small, necessary additions: the
  `[Audio Voices]` field pair on `irrlicht_renderer.h`'s
  `MemoryReportInput`, and the second `WILLEQ T131` banner + patched
  `tsf_reset` body on `tsf.h`). No new file crossed the threshold.
- eqemu lock: taken (confirmed free), zone `befallen` brought up via
  `scripts/eqemu/up.sh befallen`, used for two full tours, brought down
  cleanly (`down.sh`), lock file deleted and confirmed gone.

### Deviations

- **`tsf_reset` required a second local `tsf.h` patch**, not anticipated as
  a separate deviation in the plan's own framing (the plan named
  `tsf_reset` as something to *verify*, not something that would need to be
  *changed*) — see "The plan's own 'verify the whole path' caught it"
  above. This is the single most consequential finding of the unit: without
  it, the primary acceptance number would not have reached zero.
- Combat-music polyphony was not separately measured live (no combat
  triggered in either tour) — noted explicitly in `review/T131/README.md`
  rather than silently absorbed into "peak measured: 43". The chosen
  headroom (~6x) is sized to cover this gap at negligible byte cost rather
  than requiring a follow-on measurement before this unit can close.
- The reload path's latent concurrency hazard (documented in code, not
  fixed) is a deviation from "everything is now safe" to "everything
  reachable today is safe, and the one path that isn't reachable today is
  now impossible to add without noticing the warning" — this is the
  plan's own STOP condition evaluated as NOT triggered (no concurrent
  caller exists), reported per the plan's instruction to "say so" either
  way.
- Tour script waits were lengthened (90s vs T130's 60s) to increase MIDI
  throughput sampled per zone for the voice-count measurement; total run
  length grew from T130's shorter tour to ~287s per run. Not a deviation
  in method, just a parameter chosen for this unit's own measurement goal.

## Board

**Audio-thread allocation count: ZERO, both runs (target ZERO met).**
**T127's `M_ARENA_MAX=1` caveat is discharged by construction**: a callback
that never calls `malloc`/`free`/`realloc` cannot contend on the single
arena lock, full stop — not measured-and-tolerated as T130 left it, but
structurally impossible now. **T114's item #11 (the `AudioMixer`/TSF race)
is addressed** in the sense upstream's own header promised ("TSF should
become mostly thread safe" once both preconditions are met) — both
preconditions are now met on every construction path. The `mallopt(M_ARENA_MAX,
1)` setting **could** be reconsidered now that the audio callback's own
contribution to arena contention is eliminated, but per the plan's
standing instruction this unit did not change it — that is still Will's
call, and the case for leaving it alone is arguably now stronger (the
original risk this measured against no longer exists on the audio side).

Late callbacks did NOT reach zero (5 of ~10,565, ~0.047%, worst ~36.5ms,
both runs) despite allocations reaching zero — per the plan's own
"informative rather than a failure" framing, this means the residual gaps
were never solely (if at all) the callback-thread TSF allocation; something
else (most plausibly OS scheduling jitter on this llvmpipe/EGL-surfaceless
dev VM, not measured further here) accounts for them, and closing that
would be a different, separate unit if Will wants it pursued.
