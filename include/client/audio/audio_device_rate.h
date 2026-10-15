#pragma once

// B1069 (GitLab #389): the playback DEVICE sample rate and format, as
// configured.
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
#include <string>

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

// B1069 device acceptance (XU4, 2026-10-14): with the rate fixed the PCM ran
// at 48000 S24_LE and the monitor was still silent; speaker-test at 48000
// S16_LE was heard, 48000 S24_LE was not. The playback FORMAT was f32,
// which ALSA cannot take, so it negotiated the card's native S24_LE. The
// device is therefore asked for 16-bit by default (every HDMI sink takes
// 16-bit PCM); `audio.device_format` / `--audio-format` choose another, and
// Native lets the backend pick. The mixer still renders f32; the backend
// converts once.
enum class DeviceFormat : uint8_t { S16, F32, S24, S32, Native };

inline constexpr DeviceFormat kDefaultDeviceFormat = DeviceFormat::S16;

inline const char* deviceFormatName(DeviceFormat f) {
    switch (f) {
        case DeviceFormat::S16:    return "s16";
        case DeviceFormat::F32:    return "f32";
        case DeviceFormat::S24:    return "s24";
        case DeviceFormat::S32:    return "s32";
        case DeviceFormat::Native: return "native";
    }
    return "s16";
}

// The config/CLI spelling: s16, f32, s24, s32, native. False on anything
// else (out is left untouched).
inline bool parseDeviceFormat(const std::string& text, DeviceFormat& out) {
    for (DeviceFormat f : {DeviceFormat::S16, DeviceFormat::F32, DeviceFormat::S24,
                           DeviceFormat::S32, DeviceFormat::Native}) {
        if (text == deviceFormatName(f)) { out = f; return true; }
    }
    return false;
}

inline std::atomic<DeviceFormat>& configuredDeviceFormatSlot() {
    static std::atomic<DeviceFormat> format{kDefaultDeviceFormat};
    return format;
}

inline void setConfiguredDeviceFormat(DeviceFormat f) {
    configuredDeviceFormatSlot().store(f, std::memory_order_relaxed);
}

inline DeviceFormat configuredDeviceFormat() {
    return configuredDeviceFormatSlot().load(std::memory_order_relaxed);
}

}  // namespace Audio
}  // namespace EQT
