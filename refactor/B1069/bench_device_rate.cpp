// B1069 (#389) design measurement: what does a 48 kHz device cost?
//
// NOT part of the build. Compiled by hand against a built willeq_core
// (command in refactor/B1069/README.md) and run on this host.
//
// Three shapes, all driving the REAL AudioMixer + MidiPlayer (TSF) on the
// zone's own music (qeynos2.xmi, synthusr.sf2 -- the GM font qeynos2 plays
// on) plus four looping 11025 Hz mono effects (the shipped effect rate, T401):
//
//   base  : pre-B1069. Mixer 22050, 512-frame callbacks, no resampler.
//   opt1  : candidate 1. The mixer RENDERS at 48000: TSF synthesises at
//           48000 and each effect steps 11025/48000 per output frame. A
//           48000 mixer is emulated exactly by giving each channel the
//           sampleRate that yields that step against the compiled
//           SAMPLE_RATE (22050); the per-frame arithmetic is identical.
//           1115-frame callbacks (the same 23.2 ms period).
//   opt2  : candidate 2 (shipped). Mixer 22050 + DeviceRateAdapter
//           (one linear ma_resampler, 22050 -> 48000), 1115-frame callbacks.
//
// Per shape: thread CPU time per callback (CLOCK_THREAD_CPUTIME_ID) over
// SECONDS of audio, mean / p50 / p99 / max, and mix-thread CPU % =
// CPU time / audio time; plus the ledger's `audio` category live/peak bytes
// after setup and after the run (the same category the orangepi budget caps).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "client/audio/audio_backend.h"
#include "client/audio/audio_mixer.h"
#include "client/audio/midi_player.h"
#include "client/audio/xmi_decoder.h"
#include "client/core/memory_ledger.h"

using namespace EQT::Audio;

static int64_t threadCpuNs() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return int64_t(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static void midiRender(void* user, float* buf, int frames) {
    static_cast<MidiPlayer*>(user)->render(buf, frames);
}

struct Result {
    const char* name;
    double meanUs, p50Us, p99Us, maxUs, cpuPct;
    uint64_t liveSetup, liveAfter, peak;
    uint64_t callbacks;
};

static Result run(const char* name, uint32_t synthRate, uint32_t deviceRate, bool adapter,
                  uint32_t period, double seconds, const std::string& eq) {
    Result r{};
    r.name = name;
    eqt::memory::resetPeaks();
    const uint64_t live0 = eqt::memory::stats(eqt::memory::Category::Audio).liveBytes;
    std::unique_ptr<AudioMixer> mixer;
    std::unique_ptr<MidiPlayer> midi;
    std::unique_ptr<DeviceRateAdapter> ad;
    std::vector<std::vector<float>> sfx;
    std::vector<uint8_t> mid;
    {
        eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
        mixer = std::make_unique<AudioMixer>();
        midi = std::make_unique<MidiPlayer>();
        if (!midi->init(eq + "/synthusr.sf2", synthRate)) { std::fprintf(stderr, "sf2 init failed\n"); std::exit(2); }
        XmiDecoder dec;
        mid = dec.decodeFile(eq + "/qeynos2.xmi", 0);
        if (mid.empty() || !midi->play(mid, true)) { std::fprintf(stderr, "xmi play failed\n"); std::exit(2); }
        int h = mixer->allocMusicChannel();
        auto* ch = mixer->getChannel(h);
        ch->renderCallback = &midiRender;
        ch->renderUserData = midi.get();
        ch->active.store(true);
        // Four looping effects at the shipped 11025 Hz. For a mixer rendering
        // at deviceRate the step is 11025/deviceRate; against the compiled
        // SAMPLE_RATE that is sampleRate = 11025 * 22050 / deviceRate.
        const uint32_t mixRate = adapter ? AudioMixer::SAMPLE_RATE : deviceRate;
        const uint32_t sfxRate = uint32_t((11025ull * AudioMixer::SAMPLE_RATE + mixRate / 2) / mixRate);
        for (int i = 0; i < 4; ++i) {
            sfx.emplace_back(11025 + 977 * i);
            for (size_t k = 0; k < sfx.back().size(); ++k)
                sfx.back()[k] = float((k * 2654435761u >> 8) & 0xffff) / 65535.0f - 0.5f;
            int s = mixer->allocSfxChannel();
            auto* c = mixer->getChannel(s);
            c->samples = sfx.back().data();
            c->sampleRate = sfxRate;
            c->channels = 1;
            c->totalFrames = sfx.back().size();
            c->looping = true;
            c->pan = -0.5f + 0.3f * i;
            c->active.store(true);
        }
        if (adapter) {
            ad = std::make_unique<DeviceRateAdapter>();
            ad->init(mixer.get(), AudioMixer::SAMPLE_RATE, deviceRate);
        }
    }
    r.liveSetup = eqt::memory::stats(eqt::memory::Category::Audio).liveBytes - live0;

    std::vector<float> out(size_t(period) * 2);
    const uint64_t n = uint64_t(seconds * deviceRate / period);
    std::vector<int64_t> per;
    per.reserve(n);
    int64_t total = 0;
    {
        // The audio thread runs under no scope in production either (its
        // allocations would land in `unattributed`); the run is wrapped so
        // any callback-time allocation is visible in `audio` here.
        eqt::memory::MemoryScope scope(eqt::memory::Category::Audio);
        for (uint64_t i = 0; i < n; ++i) {
            const int64_t t0 = threadCpuNs();
            if (ad) ad->render(out.data(), period);
            else mixer->render(out.data(), int(period));
            const int64_t dt = threadCpuNs() - t0;
            per.push_back(dt);
            total += dt;
        }
    }
    const auto st = eqt::memory::stats(eqt::memory::Category::Audio);
    r.liveAfter = st.liveBytes - live0;
    r.peak = st.peakBytes - live0;
    std::sort(per.begin(), per.end());
    r.callbacks = n;
    r.meanUs = double(total) / double(n) / 1000.0;
    r.p50Us = per[n / 2] / 1000.0;
    r.p99Us = per[size_t(n * 0.99)] / 1000.0;
    r.maxUs = per.back() / 1000.0;
    r.cpuPct = 100.0 * double(total) / (double(n) * period / deviceRate * 1e9);
    midi->stop();
    mixer->stopAll();
    return r;
}

int main(int argc, char** argv) {
    const std::string eq = argc > 1 ? argv[1] : "data/eq_client";
    const double seconds = argc > 2 ? std::atof(argv[2]) : 60.0;
    std::printf("shape\tsynth_hz\tdevice_hz\tperiod\tcallbacks\tmean_us\tp50_us\tp99_us\tmax_us\tcpu_pct\taudio_live_setup_B\taudio_live_after_B\taudio_peak_B\n");
    struct S { const char* n; uint32_t synth, dev; bool ad; uint32_t period; };
    const S shapes[] = {
        {"base", 22050, 22050, false, 512},
        {"opt1", 48000, 48000, false, 1115},
        {"opt2", 22050, 48000, true, 1115},
    };
    for (int rep = 0; rep < 3; ++rep) {
        for (const auto& s : shapes) {
            Result r = run(s.n, s.synth, s.dev, s.ad, s.period, seconds, eq);
            std::printf("%s\t%u\t%u\t%u\t%llu\t%.1f\t%.1f\t%.1f\t%.1f\t%.2f\t%llu\t%llu\t%llu\n", r.name,
                        s.synth, s.dev, s.period, (unsigned long long)r.callbacks, r.meanUs, r.p50Us,
                        r.p99Us, r.maxUs, r.cpuPct, (unsigned long long)r.liveSetup,
                        (unsigned long long)r.liveAfter, (unsigned long long)r.peak);
            std::fflush(stdout);
        }
    }
    return 0;
}
