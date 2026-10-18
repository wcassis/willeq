// B1069: pure tones at the mixer rate through the production backend path
// (MiniaudioBackend on the null device, WILLEQ_AUDIO_DUMP), one dump per tone.
#include "client/audio/audio_backend.h"
#include "client/audio/audio_mixer.h"
#include "common/logging.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
using namespace EQT::Audio;
int main(int argc, char** argv) {
    const std::string dir = argv[1];
    const double amp = argc > 2 ? std::atof(argv[2]) : 0.5;
    for (double hz : {440.0, 1000.0, 3000.0, 6000.0, 9000.0}) {
        AudioMixer mixer;
        std::vector<float> pcm(22050 * 4);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = float(amp * std::sin(2 * M_PI * hz * i / 22050.0));
        int h = mixer.allocSfxChannel();
        auto* c = mixer.getChannel(h);
        c->samples = pcm.data(); c->sampleRate = 22050; c->channels = 1;
        c->totalFrames = pcm.size(); c->looping = true; c->pan = 0.0f;  // centre: 0.5 gain per side
        c->active.store(true);
        const std::string path = dir + "/tone_" + std::to_string(int(hz)) + ".wav";
        setenv("WILLEQ_AUDIO_DUMP", path.c_str(), 1);
        setenv("WILLEQ_AUDIO_DUMP_SECONDS", "3", 1);
        MiniaudioBackend b;
        b.setUseNullBackend(true);
        if (!b.init(&mixer, AudioMixer::SAMPLE_RATE)) return 1;
        b.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(3300));
        b.stop();
        mixer.stopAll();
    }
    FlushThreadLog();
    return 0;
}
