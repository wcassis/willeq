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

(executor fills: the helper and every path that calls it, including reload;
the reload path's thread and whether it is a hazard; **measured peak concurrent
voices**, the chosen `max_voices`, its headroom and total byte cost, and the
drop behaviour if exceeded; **audio-thread allocation count, target zero**,
against T130's 76/30; late-callback numbers against 2-of-5395; confirmation
that `tsf_reset` and the rest of the callback path are allocation-free; the
ALSA test's status; deviations)

## Board

Lead with the audio-thread allocation count. If it is zero, say plainly that
**T127's `M_ARENA_MAX=1` caveat is discharged by construction** and that
**T114's item #11 is addressed** — and say whether the `mallopt` could now be
reconsidered, without changing it. If it is not zero, name what still
allocates.
