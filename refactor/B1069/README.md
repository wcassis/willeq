# B1069 (#389) -- open the audio device at 48 kHz

**Defect (measured by Fable on the XU4, issue #389):** the client opened the
playback device at 22050 Hz (`MiniaudioBackend::init()` set
`config.sampleRate = AudioMixer::SAMPLE_RATE`). The Exynos I2S/HDMI path has
no 22050, ALSA ran the PCM at 32000 S24_LE, and Will's HDMI monitor plays
nothing at 32 kHz (`speaker-test -r 32000` silent, `-r 48000` audible). This
branch does not re-derive that; it fixes the rate.

## A. Design, measured

miniaudio delivers callback frames at `ma_device_config.sampleRate` and
converts from there to the hardware rate itself, so "open at 48000" means
either the mixer renders at 48000 or the backend resamples the 22050 mix.

| shape | what it is |
|---|---|
| `base` | pre-B1069: mixer 22050, 512-frame callbacks, device asked for 22050 |
| `opt1` | candidate 1: mixer RENDERS at 48000 (TSF synthesises at 48000; effects step 11025/48000), 1115-frame callbacks |
| `opt2` | candidate 2: mixer stays 22050 + one linear `ma_resampler` 22050 -> 48000 in the backend (`DeviceRateAdapter`), 1115-frame callbacks |

Both 48 kHz shapes keep the old ~23.2 ms period (512 frames of mixer time).
The bench drives the real `AudioMixer` + `MidiPlayer` on the zone's own music
(`qeynos2.xmi` on `synthusr.sf2`) plus four looping 11025 Hz mono effects, 60 s
of audio per shape, three repetitions, one pinned core
(`taskset -c 3`, Xeon E5-2680 v3). Source: `refactor/B1069/bench_device_rate.cpp`;
raw rows: `refactor/B1069/bench_x86.tsv`. `opt1` is emulated exactly, not
approximated: each effect channel gets the `sampleRate` that yields the
48 kHz step against the compiled `SAMPLE_RATE`, so the per-frame arithmetic
is the one a 48 kHz mixer would do, and TSF is really initialised at 48000.

| shape | CPU / callback mean (us) | p99 (us) | mix-thread CPU % | `audio` live after setup (B) | `audio` peak (B) |
|---|---|---|---|---|---|
| base | 36.8-44.4 | 77.8-88.8 | 0.16-0.19 | 1,344,912-1,345,584 | 1,591,024-1,591,616 |
| opt1 | 101.3-113.6 | 205.9-241.8 | 0.44-0.49 | 1,344,896-1,344,960 | 1,590,976-1,591,008 |
| opt2 | 71.4-84.9 | 119.9-142.7 | 0.31-0.37 | 1,353,056-1,353,072 | 1,590,992-1,591,056 |

(ranges = the three repetitions in `bench_x86.tsv`; the first repetition is
the warm-up and is the high end of every range.)

**Choice: opt2.** It is the cheaper of the two by ~0.13 percentage points of a
core (~30 us per callback, ~30 %) on every repetition, and the gap is
structural, not noise: TSF's voice rendering is per OUTPUT frame, so opt1
pays 48000/22050 = 2.18x the synthesis, while opt2 pays the synthesis at
22050 plus one stereo linear interpolation per device frame. opt2 also leaves
everything downstream of the mixer bit-for-bit unchanged (the T404 asset
resampling, the MIDI timing in `MidiPlayer::render`, the fade arithmetic in
`MusicPlayer::mixerRenderCallback`, and the 22050 Hz RDP loopback stream),
where opt1 changes all of them. Memory: opt2 costs +8.1 KB in the `audio`
category (1,353,056 vs 1,344,912 B after setup -- the resampler state and its
mixer-rate scratch, `DeviceRateAdapter::Impl::mixScratch`); the peak is
unchanged at ~1.59 MB. The orangepi preset's `audio` budget is 33,554,432 B
(`config/constrained_presets.json`); +8.1 KB is 0.02 % of it. Not touched.

**Orange Pi One estimate (scaled, NOT measured):** the bench was not run on
lane 1 -- the board takes published binaries only and the bench is not one.
Scaling the x86 figures by an ASSUMED 10-15x per-core slowdown for scalar
float code on a 1.2 GHz Cortex-A7 (an assumption, no measurement in the tree
backs the factor): opt2 ~3.1-5.6 % of one core, ~0.7-1.3 ms of each 23.2 ms
period; opt1 ~4.4-7.4 %; base ~1.6-2.9 %. A 48 kHz device on the Pi costs
roughly 1.5-2.7 points of one core over today; a Pi sink that accepts a lower
rate can set `audio.device_sample_rate` to 32000/44100, or to 22050 to turn
the resampler off entirely (the pre-B1069 path, bit-exact -- pinned).

**Ledger on a live qeynos2 zone-in: NOT measured.** The local eqemu lane's
lock was held by another session (`/mnt/projects/.eqemu-lock`, taken
2026-10-14T21:55:42Z from hermes-ext) for the whole of this unit, and the
headless (`EQT_GRAPHICS=OFF`) client never initialises audio
(`BridgeComponent::InitializeAudio()` is reached only from `InitGraphics`), so
the bench's own `audio`-category rows above are the measurement.

Reproduce:

```bash
c++ -O3 -DNDEBUG -std=gnu++17 -DWITH_AUDIO -DEQT_MEMORY_LEDGER=1 -DFMT_SHARED \
    -Iinclude -Isrc -Ithird_party/TinySoundFont -Ithird_party/miniaudio \
    -Ithird_party/dr_libs -Ithird_party/minimp3 -Ithird_party -I/usr/include/jsoncpp \
    refactor/B1069/bench_device_rate.cpp -o /tmp/bench_device_rate \
    build/hygiene/lib/libwilleq_core.a -ljsoncpp -lfmt -lssl -lcrypto -lz \
    -lboost_graph -lboost_regex -lRecast -lDetour -ldl -lm -lpthread
taskset -c 3 /tmp/bench_device_rate data/eq_client 60
```

## B. What changed

- `include/client/audio/audio_device_rate.h` (new, header-only):
  `kDefaultDeviceSampleRate = 48000` and the process-wide configured rate.
  It is process-wide, not one more `BridgeComponent` setter, because the
  config reaches audio through `Application::initEverQuestClient()`, whose
  statements are pinned line-for-line by the G04 golden
  (`tests/goldens/application_init_order_g04.txt`), which this unit may not
  edit. `src/client/main.cpp` publishes the parsed value once, after
  `loadConfigFile()`, before `Application::initialize()`.
- `ApplicationConfig::audioDeviceSampleRate` (default 48000): JSON
  `audio.device_sample_rate` and CLI `--audio-rate <hz>`
  (`application_config.cpp`); 0 = backend native; clamped to 0..384000. The
  config file is loaded AFTER the arguments, so `--audio-rate` sets
  `audioDeviceSampleRateFromCli` and the file then leaves it alone.
- `AudioManager::setDeviceSampleRate()` / `getDeviceSampleRate()`; its
  default is the published rate; `initialize()` hands it to the
  `MiniaudioBackend`. The RDP loopback backends are untouched
  (`LOOPBACK_SAMPLE_RATE` stays 22050 -- the loopback tests and the RDPSND
  stream run at the mixer rate, no resampling there).
- `DeviceRateAdapter` (`audio_backend.h/.cpp`): renders the mixer at its rate,
  one linear `ma_resampler` to the device rate, chunked at 2048 device frames
  so the scratch allocated in `init()` never grows on the audio thread;
  equal rates are a pass-through straight into the device buffer.
- `MiniaudioBackend::init()`: `config.sampleRate` = the requested DEVICE rate
  (0 = native); period = 512 frames of mixer time at the device rate (1115
  at 48000; `periodSizeInMilliseconds` when 0), so the T130 callback probe's
  expected period is unchanged; the callback's user data is the adapter.
  `setUseNullBackend()` is a test seam (miniaudio's null device).
- The log. `Miniaudio backend initialized (22050Hz mixer, 48000Hz device,
  stereo)` at init, and after `ma_device_start` ONE grep-able line:
  `Audio device: requested=<knob>Hz configured=<ma_device.sampleRate>Hz
  native=<playback.internalSampleRate>Hz format=<internalFormat> channels=..
  period=.. backend=.. device=".." mixer=22050Hz resample=linear|off`.
  On the XU4 the pre-fix case would have read `native=32000Hz`.

## C. Pins

`tests/test_audio_device_rate.cpp` (17 cases, links `willeq_core`):
config default 48000 everywhere, the JSON key honoured, 0 passes through
(file and CLI), the CLI wins over the file, the published rate is the
manager's default; the adapter pulls exactly 22050 mixer frames per second
over 10 s of 1115-frame callbacks and of ragged/oversized pulls (+/- 2
frames), a one-second 22050 Hz asset fills 48000 (and 44100) device frames
(+/- 4), equal rates are bit-exact with a direct `render()`; the backend on
the null device configures 48000 and resamples, 0 takes the native rate,
22050 is the pre-B1069 pass-through, and the init log line is asserted
(`requested=48000Hz configured=48000Hz`, `mixer=22050Hz resample=linear`);
the whole chain published rate -> `AudioManager` -> backend is asserted off
the log (skips only if no miniaudio device opens at all).

Every pre-existing audio test passes on this host
(`ctest -R "Audio|Sound|Music|Midi|...|InitOrder"` in `build/hygiene`: 454/454 incl. the 17 new cases and the G04 init-order net,
1 skipped as before -- `SoundBufferTest.LoadSwingWav`).

## Device acceptance 1 (XU4, publish de282232): FAILED -> the format

Measured on the box by the coordinator with that publish running:
`/proc/asound/card0/pcm0p/sub0/hw_params` = S24_LE, rate 48000, RUNNING --
the rate was right and Will heard nothing. With the box idle, `speaker-test`
at 48000 S16_LE was HEARD, 48000 S24_LE and 32000 S24_LE were silent. The
backend asked miniaudio for `ma_format_f32`; the hardware has no float, so
ALSA negotiated the card's native S24_LE, which this sink drops.

Fix: the device is asked for `audio.device_format` / `--audio-format`
(`include/client/audio/audio_device_rate.h` `DeviceFormat`; default `s16`;
`f32`, `s24`, `s32`, `native` = `ma_format_unknown`), published by main.cpp
like the rate. The mixer and the resampler stay f32; when the callback's
format (`ma_device.playback.format`) is not f32 the adapter renders f32
into a scratch allocated in `init()` (2048 frames) and `ma_pcm_convert`
writes the device format, chunk by chunk, no dither. The init line now
reads `format=<negotiated internalFormat> requested_format=<knob>
client_format=<playback.format>`. Pins: default s16 (config, CLI, manager,
backend on the null device, the log line), every spelling honoured from the
file and the CLI, native passes through to a real format, an unknown
spelling keeps s16, the CLI wins. The CPU cost of the s16 conversion was not
benchmarked (one float->int16 per sample, against the table above).

## Device acceptance 2 (XU4, publish 42ebeabc): audible, "distorted" -> the resampler

Will heard music at s16/48 kHz but "it sounds distorted". Measured here, not
guessed, with a new diagnostic tap: `WILLEQ_AUDIO_DUMP=<file.wav>`
(`WILLEQ_AUDIO_DUMP_SECONDS`, default 30) records exactly what the callback
hands miniaudio (device rate and format) and, beside it, `<file>.mix.wav` --
the mixer's own f32 output at 22050 for the SAME input (`PcmTap`,
`audio_backend.cpp`; buffers allocated in `init()`, a memcpy in the callback,
files written in `stop()`). Inputs: (a) pure tones at the mixer rate through
`MiniaudioBackend` on the null device (`acceptance2/tones.cpp`), analysed by
`acceptance2/sinad.py` (sine-bin power over everything else, Kaiser-windowed
FFT, left channel); (b) the real `AudioManager` playing `qeynos2.xmi` plus
four effects every 1.7 s for 30 s (`acceptance2/harness.cpp`), analysed by
`acceptance2/analyze.py`.

Tones, device SINAD (the 22050 mix itself is 153-154 dB on every tone):

| tone | linear `ma_resampler` (42ebeabc) | worst spur | polyphase sinc (this fix) | worst spur |
|---|---|---|---|---|
| 440 Hz | 75.9 dB | 4,340 Hz -84.2 dBc | 83.7 dB | 1,320 Hz -95.9 dBc |
| 1 kHz | 62.4 dB | 4,900 Hz -69.5 dBc | 83.3 dB | 3,000 Hz -93.9 dBc |
| 3 kHz | 43.2 dB | 6,900 Hz -49.7 dBc | 85.1 dB | 9,000 Hz -94.9 dBc |
| 6 kHz | 30.8 dB | 9,900 Hz -37.4 dBc | 96.0 dB | 18,000 Hz -102.0 dBc |
| 9 kHz | 15.4 dB | 13,050 Hz -20.1 dBc | 88.2 dB | 3,000 Hz -97.3 dBc |

(`acceptance2/tones_before_linear.txt`, `tones_after_sinc48.txt`.) **The
mechanism:** the worst spur sits at f + 3,900 Hz for every tone up to 6 kHz.
Linear interpolation leaves the image at 2 x 22050 - f = 44100 - f; above the
48 kHz Nyquist it folds to 48000 - (44100 - f) = f + 3900 Hz -- an
INHARMONIC tone inside the audible band, -50 dBc at 3 kHz, -37 dBc at 6 kHz,
which on music reads as distortion. It is not clipping (0 clipped samples in
the device stream and in the mix, peaks 0.637 / 0.637), not drift (length
ratio 2.176871 = 48000/22050 exactly over 30 s), not a period-boundary
discontinuity (|first difference| at the 1115-frame boundaries max 0.097,
p99 0.036, vs 0.268 / 0.046 elsewhere), and the only all-zero run is the
~1 s before the song starts (`music_before_linear.txt`).

**Fix:** `PolyphaseResampler` (`include/client/audio/polyphase_resampler.h`,
`src/client/audio/polyphase_resampler.cpp`): rational L/M (320/147 for
22050 -> 48000), Kaiser-windowed sinc (beta 7.86, ~80 dB stopband), 48 taps
per phase, cutoff 0.9 x the lower Nyquist (9.9 kHz), every phase normalised
to unity DC gain, history carried across calls (ragged pulls are
sample-identical to one big pull -- pinned). `DeviceRateAdapter` uses it by
default (`resample=sinc48` in the init line); miniaudio's linear resampler is
kept only as the fallback for a rate pair needing more than 2048 phases
(WARN logged). After the fix, music: energy above 11,025 Hz -83.0 dB rel.
total (was -51.9 dB), 0 clipped samples, length ratio 2.176871, boundary
first difference max 0.145 / p99 0.048 vs 0.325 / 0.054 elsewhere
(`music_after_sinc48.txt`). Tones are now at the s16 floor for a -12 dBFS
sine. (The music "SNR vs a band-limited reference" row in `analyze.py` is
alignment-limited -- the filter's delay is 24 input frames, a fractional
number of output frames -- and is not used as a verdict.)

**Cost** (`bench_x86_sinc.tsv`, same bench as section A; ranges over 3 runs):
opt2 with the sinc is 91.1-106.1 us mean / 121.1-142.3 us p99 per callback,
0.39-0.46 % of a core (linear was 0.31-0.37 %); `audio` live +70 KB over
base (1,415,152 vs 1,344,896-1,345,584 B -- the 320 x 48 coefficient table
is 61,440 B of it); peak unchanged. opt1 (mixer rendering at 48 kHz) would
NOT have been clean either: its effects path is the mixer's own linear
interpolation (T404) from 11,025 Hz, whose images all land in band. Pi One
estimate, scaled with the same assumed 10-15x: ~3.9-6.9 % of one core; a Pi
sink that accepts 22050 can still turn the resampler off.

Pins added: `DeviceRateAdapter.TonesComeOutCleanAt48000` (SINAD >= 70 dB at
440 Hz-8 kHz, sine fitted at the exact frequency, f32, before any s16) and
`DeviceRateAdapter.PullSizesDoNotChangeTheSamples`.

## Not verified

- Will HEARING zone music on the XU4 at the fixed publish (his acceptance).
- A real ALSA device: on this host miniaudio's default backend list falls
  through to the Null device, so `native=` from real hardware has not been
  seen by this unit.
- The Pi cost (scaled above, not measured) and the live zone-in ledger row.
