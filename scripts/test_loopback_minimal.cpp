// Minimal test of OpenAL Soft loopback
// Compile: g++ -std=c++17 test_loopback_minimal.cpp -lopenal -o test_loopback_minimal

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>

LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT = nullptr;
LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT = nullptr;
LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT = nullptr;

int main() {
    // Get loopback extension functions
    alcLoopbackOpenDeviceSOFT = (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT");
    alcIsRenderFormatSupportedSOFT = (LPALCISRENDERFORMATSUPPORTEDSOFT)alcGetProcAddress(nullptr, "alcIsRenderFormatSupportedSOFT");
    alcRenderSamplesSOFT = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(nullptr, "alcRenderSamplesSOFT");

    if (!alcLoopbackOpenDeviceSOFT || !alcRenderSamplesSOFT) {
        std::cerr << "Failed to get loopback functions" << std::endl;
        return 1;
    }

    // Create loopback device
    ALCdevice* device = alcLoopbackOpenDeviceSOFT(nullptr);
    if (!device) {
        std::cerr << "Failed to create loopback device" << std::endl;
        return 1;
    }

    // Create context
    ALCint attrs[] = {
        ALC_FREQUENCY, 44100,
        ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
        ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT,
        0
    };
    ALCcontext* context = alcCreateContext(device, attrs);
    alcMakeContextCurrent(context);

    std::cerr << "Loopback device and context created" << std::endl;

    // Create source
    ALuint source;
    alGenSources(1, &source);
    std::cerr << "Source created: " << source << std::endl;

    // Create buffer with simple sine wave
    const int SAMPLE_RATE = 44100;
    const int DURATION_MS = 500;
    const int NUM_SAMPLES = SAMPLE_RATE * DURATION_MS / 1000;
    const float FREQ = 440.0f;  // A4

    std::vector<int16_t> sineWave(NUM_SAMPLES * 2);  // stereo
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int16_t sample = (int16_t)(32767.0f * sin(2.0f * M_PI * FREQ * i / SAMPLE_RATE));
        sineWave[i * 2] = sample;      // left
        sineWave[i * 2 + 1] = sample;  // right
    }

    ALuint buffer;
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_STEREO16, sineWave.data(), sineWave.size() * sizeof(int16_t), SAMPLE_RATE);
    std::cerr << "Buffer created with 440Hz sine wave (" << DURATION_MS << "ms)" << std::endl;

    // Queue buffer and play
    alSourceQueueBuffers(source, 1, &buffer);
    alSourcePlay(source);

    ALint state, queued, processed;
    alGetSourcei(source, AL_SOURCE_STATE, &state);
    alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
    std::cerr << "After play: state=" << state << " (PLAYING=0x1012), queued=" << queued << ", processed=" << processed << std::endl;

    // Render and check
    std::vector<int16_t> renderBuffer(1024 * 2);  // 1024 frames stereo
    int16_t maxSample = 0;

    std::cerr << "\nRendering 10 iterations..." << std::endl;
    for (int iter = 0; iter < 10; iter++) {
        alcRenderSamplesSOFT(device, renderBuffer.data(), 1024);

        // Check max sample
        int16_t iterMax = 0;
        for (size_t i = 0; i < renderBuffer.size(); i++) {
            if (abs(renderBuffer[i]) > iterMax) iterMax = abs(renderBuffer[i]);
        }
        if (iterMax > maxSample) maxSample = iterMax;

        alGetSourcei(source, AL_SOURCE_STATE, &state);
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

        std::cerr << "  Iter " << iter << ": maxSample=" << iterMax
                  << ", state=" << state << ", queued=" << queued
                  << ", processed=" << processed << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(23));
    }

    std::cerr << "\nMax sample captured: " << maxSample << std::endl;
    if (maxSample > 1000) {
        std::cerr << "SUCCESS: Audio was captured from source!" << std::endl;
    } else {
        std::cerr << "FAILURE: No significant audio captured" << std::endl;
    }

    // Cleanup
    alSourceStop(source);
    alDeleteSources(1, &source);
    alDeleteBuffers(1, &buffer);
    alcMakeContextCurrent(nullptr);
    alcDestroyContext(context);
    alcCloseDevice(device);

    return 0;
}

// Second test: Check if position advances
int main2() {
    // ... (same setup as above, but check AL_BYTE_OFFSET and AL_SAMPLE_OFFSET)
    return 0;
}
