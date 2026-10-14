#pragma once

// B1069 (GitLab #389): the playback DEVICE sample rate, as configured.
//
// The mixer renders at AudioMixer::SAMPLE_RATE (22050, the EQ asset rate)
// and MiniaudioBackend resamples once to the device rate (DeviceRateAdapter,
// audio_backend.h). Before B1069 the device was opened at 22050 itself; ALSA
// rounded that to the hardware's nearest rate (32000 on the ODROID XU4's
// I2S/HDMI path, measured in /proc/asound/card0/pcm0p/sub0/hw_params) and an
// HDMI monitor that drops 32 kHz played nothing. 48000 is accepted by every
// HDMI sink. 0 = let the backend pick its native rate.
//
// Why a process-wide value and not one more Bridge setter: the config
// reaches the audio system through Application::initEverQuestClient(), whose
// statement order is pinned line-for-line by the G04 golden
// (tests/goldens/application_init_order_g04.txt). main.cpp publishes the
// parsed `audio.device_sample_rate` / `--audio-rate` here once, before
// Application::initialize(); AudioManager reads it when it opens the
// backend. Header-only and not WITH_AUDIO-guarded, so main.cpp and
// application.h can name it in every build.

#include <atomic>
#include <cstdint>

namespace EQT {
namespace Audio {

inline constexpr uint32_t kDefaultDeviceSampleRate = 48000;

inline std::atomic<uint32_t>& configuredDeviceSampleRateSlot() {
    static std::atomic<uint32_t> rate{kDefaultDeviceSampleRate};
    return rate;
}

// Set once at startup (main.cpp) from the parsed config; tests may set it.
inline void setConfiguredDeviceSampleRate(uint32_t hz) {
    configuredDeviceSampleRateSlot().store(hz, std::memory_order_relaxed);
}

inline uint32_t configuredDeviceSampleRate() {
    return configuredDeviceSampleRateSlot().load(std::memory_order_relaxed);
}

}  // namespace Audio
}  // namespace EQT
