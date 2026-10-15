// B1069 harness: the real AudioManager (default backend list -> null device
// on this host), qeynos2 music + a few effects, dumped via WILLEQ_AUDIO_DUMP.
#include "client/audio/audio_manager.h"
#include "client/audio/audio_device_rate.h"
#include "common/logging.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
int main(int argc, char** argv) {
    SetLogLevel(LOG_INFO);
    const char* eq = argc > 1 ? argv[1] : "data/eq_client";
    double secs = argc > 2 ? std::atof(argv[2]) : 32;
    EQT::Audio::AudioManager m;
    if (!m.initialize(eq, false)) { std::printf("init failed\n"); return 1; }
    m.playMusic("qeynos2.xmi", true, 0);
    auto t0 = std::chrono::steady_clock::now();
    const char* sfx[] = {"BuyItem.WAV", "SpelCast.WAV", "LevDn.WAV", "Swing.WAV"};
    int k = 0;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
        m.playSoundByName(sfx[k++ % 4]);
        m.update();
        FlushThreadLog();
        std::this_thread::sleep_for(std::chrono::milliseconds(1700));
    }
    m.shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    FlushThreadLog();
    return 0;
}
