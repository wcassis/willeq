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

## Not verified

- Will HEARING zone music on the XU4 at the fixed publish (his acceptance).
- A real ALSA device: on this host miniaudio's default backend list falls
  through to the Null device, so `native=` from real hardware has not been
  seen by this unit.
- The Pi cost (scaled above, not measured) and the live zone-in ledger row.
