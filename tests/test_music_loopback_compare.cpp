/**
 * Test to compare music audio from loopback capture vs ffmpeg capture
 *
 * This test:
 * 1. Plays XMI through FluidSynth → MusicPlayer → OpenAL loopback
 * 2. Captures the loopback output to a WAV file
 * 3. Compares two WAV files for similarity (loopback vs ffmpeg reference)
 *
 * Usage:
 *   ./test_music_loopback_compare capture <xmi_file> <soundfont> <output.wav> [duration_sec]
 *   ./test_music_loopback_compare compare <file1.wav> <file2.wav>
 */

#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>
#include <algorithm>
#include <numeric>

#ifdef WITH_AUDIO
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <sndfile.h>
#include "client/audio/music_player.h"

using EQT::Audio::MusicPlayer;
#endif

namespace {

// WAV file utilities
struct WavData {
    std::vector<int16_t> samples;
    int sampleRate = 0;
    int channels = 0;
    double duration = 0.0;
};

bool loadWav(const std::string& filepath, WavData& wav) {
#ifdef WITH_AUDIO
    SF_INFO sfInfo = {};
    SNDFILE* file = sf_open(filepath.c_str(), SFM_READ, &sfInfo);
    if (!file) {
        std::cerr << "Failed to open WAV file: " << filepath << std::endl;
        return false;
    }

    wav.sampleRate = sfInfo.samplerate;
    wav.channels = sfInfo.channels;
    wav.samples.resize(sfInfo.frames * sfInfo.channels);

    sf_count_t read = sf_read_short(file, wav.samples.data(), wav.samples.size());
    sf_close(file);

    if (read != static_cast<sf_count_t>(wav.samples.size())) {
        std::cerr << "Warning: Read " << read << " samples, expected " << wav.samples.size() << std::endl;
        wav.samples.resize(read);
    }

    wav.duration = static_cast<double>(wav.samples.size() / wav.channels) / wav.sampleRate;

    std::cerr << "Loaded WAV: " << filepath << std::endl;
    std::cerr << "  Sample rate: " << wav.sampleRate << " Hz" << std::endl;
    std::cerr << "  Channels: " << wav.channels << std::endl;
    std::cerr << "  Duration: " << wav.duration << " seconds" << std::endl;
    std::cerr << "  Samples: " << wav.samples.size() << std::endl;

    return true;
#else
    return false;
#endif
}

bool saveWav(const std::string& filepath, const std::vector<int16_t>& samples, int sampleRate, int channels) {
#ifdef WITH_AUDIO
    SF_INFO sfInfo = {};
    sfInfo.samplerate = sampleRate;
    sfInfo.channels = channels;
    sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* file = sf_open(filepath.c_str(), SFM_WRITE, &sfInfo);
    if (!file) {
        std::cerr << "Failed to create WAV file: " << filepath << std::endl;
        return false;
    }

    sf_write_short(file, samples.data(), samples.size());
    sf_close(file);

    std::cerr << "Saved WAV: " << filepath << std::endl;
    std::cerr << "  Samples: " << samples.size() << std::endl;
    std::cerr << "  Duration: " << (samples.size() / channels / static_cast<double>(sampleRate)) << " seconds" << std::endl;

    return true;
#else
    return false;
#endif
}

// Convert stereo to mono by averaging channels
std::vector<double> toMono(const std::vector<int16_t>& samples, int channels) {
    if (channels == 1) {
        std::vector<double> mono(samples.size());
        for (size_t i = 0; i < samples.size(); i++) {
            mono[i] = samples[i] / 32768.0;
        }
        return mono;
    }

    std::vector<double> mono(samples.size() / channels);
    for (size_t i = 0; i < mono.size(); i++) {
        double sum = 0;
        for (int c = 0; c < channels; c++) {
            sum += samples[i * channels + c];
        }
        mono[i] = (sum / channels) / 32768.0;
    }
    return mono;
}

// Normalize audio to peak amplitude of 1.0
void normalize(std::vector<double>& samples) {
    double maxAbs = 0;
    for (double s : samples) {
        maxAbs = std::max(maxAbs, std::abs(s));
    }
    if (maxAbs > 0) {
        for (double& s : samples) {
            s /= maxAbs;
        }
    }
}

// Compute RMS energy
double computeRMS(const std::vector<double>& samples) {
    double sum = 0;
    for (double s : samples) {
        sum += s * s;
    }
    return std::sqrt(sum / samples.size());
}

// Compute cross-correlation coefficient at a given lag
double crossCorrelationAtLag(const std::vector<double>& a, const std::vector<double>& b, int lag) {
    size_t n = std::min(a.size(), b.size());
    if (lag < 0) {
        lag = -lag;
        // Swap conceptually: correlate b against a with positive lag
        return crossCorrelationAtLag(b, a, lag);
    }

    if (static_cast<size_t>(lag) >= n) return 0;

    size_t count = n - lag;
    double sumA = 0, sumB = 0, sumAB = 0, sumA2 = 0, sumB2 = 0;

    for (size_t i = 0; i < count; i++) {
        double va = a[i];
        double vb = b[i + lag];
        sumA += va;
        sumB += vb;
        sumAB += va * vb;
        sumA2 += va * va;
        sumB2 += vb * vb;
    }

    double meanA = sumA / count;
    double meanB = sumB / count;
    double varA = sumA2 / count - meanA * meanA;
    double varB = sumB2 / count - meanB * meanB;
    double cov = sumAB / count - meanA * meanB;

    if (varA <= 0 || varB <= 0) return 0;

    return cov / (std::sqrt(varA) * std::sqrt(varB));
}

// Find best correlation and lag
struct CorrelationResult {
    double correlation;
    int lag;  // Positive = b is delayed relative to a
};

CorrelationResult findBestCorrelation(const std::vector<double>& a, const std::vector<double>& b, int maxLag) {
    CorrelationResult best = {-2.0, 0};

    for (int lag = -maxLag; lag <= maxLag; lag++) {
        double corr = crossCorrelationAtLag(a, b, lag);
        if (corr > best.correlation) {
            best.correlation = corr;
            best.lag = lag;
        }
    }

    return best;
}

// Compute spectral centroid (approximation of "center of mass" of frequencies)
// Higher value = higher frequencies = sped up audio
double computeSpectralCentroid(const std::vector<double>& samples, int sampleRate, int windowSize = 4096) {
    // Simple approximation using zero-crossing rate as proxy for frequency content
    // (Real spectral centroid requires FFT, but ZCR correlates with it)

    size_t zeroCrossings = 0;
    for (size_t i = 1; i < samples.size(); i++) {
        if ((samples[i] >= 0) != (samples[i-1] >= 0)) {
            zeroCrossings++;
        }
    }

    // ZCR = zero crossings per second
    double duration = static_cast<double>(samples.size()) / sampleRate;
    double zcr = zeroCrossings / duration;

    // Approximate fundamental frequency from ZCR
    // For a sine wave, ZCR = 2 * frequency
    return zcr / 2.0;
}

// Compute energy envelope (RMS in windows)
std::vector<double> computeEnergyEnvelope(const std::vector<double>& samples, int windowSize = 1024) {
    std::vector<double> envelope;
    for (size_t i = 0; i + windowSize <= samples.size(); i += windowSize / 2) {
        double sum = 0;
        for (size_t j = 0; j < static_cast<size_t>(windowSize); j++) {
            sum += samples[i + j] * samples[i + j];
        }
        envelope.push_back(std::sqrt(sum / windowSize));
    }
    return envelope;
}

// Compare two audio files
struct CompareResult {
    double correlation;
    int lagSamples;
    double lagMs;
    double spectralCentroid1;
    double spectralCentroid2;
    double spectralRatio;  // >1 means file2 is higher frequency (sped up)
    double rms1;
    double rms2;
    double envelopeCorrelation;
    bool sampleRateMatch;
    bool channelMatch;
    std::string assessment;
};

CompareResult compareAudio(const WavData& wav1, const WavData& wav2) {
    CompareResult result = {};

    result.sampleRateMatch = (wav1.sampleRate == wav2.sampleRate);
    result.channelMatch = (wav1.channels == wav2.channels);

    if (!result.sampleRateMatch) {
        std::cerr << "Warning: Sample rates differ: " << wav1.sampleRate << " vs " << wav2.sampleRate << std::endl;
    }

    // Convert to mono and normalize
    auto mono1 = toMono(wav1.samples, wav1.channels);
    auto mono2 = toMono(wav2.samples, wav2.channels);

    // Trim to same length for comparison
    size_t minLen = std::min(mono1.size(), mono2.size());
    mono1.resize(minLen);
    mono2.resize(minLen);

    // Compute RMS before normalization
    result.rms1 = computeRMS(mono1);
    result.rms2 = computeRMS(mono2);

    // Normalize for correlation comparison
    normalize(mono1);
    normalize(mono2);

    // Find best correlation
    int maxLag = wav1.sampleRate / 2;  // Search up to 500ms offset
    auto corrResult = findBestCorrelation(mono1, mono2, maxLag);
    result.correlation = corrResult.correlation;
    result.lagSamples = corrResult.lag;
    result.lagMs = (corrResult.lag * 1000.0) / wav1.sampleRate;

    // Compute spectral centroids
    result.spectralCentroid1 = computeSpectralCentroid(mono1, wav1.sampleRate);
    result.spectralCentroid2 = computeSpectralCentroid(mono2, wav2.sampleRate);
    result.spectralRatio = result.spectralCentroid2 / result.spectralCentroid1;

    // Compute energy envelope correlation
    auto env1 = computeEnergyEnvelope(mono1);
    auto env2 = computeEnergyEnvelope(mono2);
    size_t envMinLen = std::min(env1.size(), env2.size());
    env1.resize(envMinLen);
    env2.resize(envMinLen);
    if (!env1.empty()) {
        normalize(env1);
        normalize(env2);
        result.envelopeCorrelation = crossCorrelationAtLag(env1, env2, 0);
    }

    // Assessment
    std::string assessment;
    if (result.correlation > 0.9) {
        assessment = "EXCELLENT - Audio files are very similar";
    } else if (result.correlation > 0.7) {
        assessment = "GOOD - Audio files are similar with some differences";
    } else if (result.correlation > 0.5) {
        assessment = "MODERATE - Audio files have noticeable differences";
    } else if (result.correlation > 0.3) {
        assessment = "POOR - Audio files are quite different";
    } else {
        assessment = "VERY POOR - Audio files are very different or uncorrelated";
    }

    if (result.spectralRatio > 1.3) {
        assessment += " [File2 appears SPED UP by ~" + std::to_string(int(result.spectralRatio * 100 - 100)) + "%]";
    } else if (result.spectralRatio < 0.7) {
        assessment += " [File2 appears SLOWED DOWN by ~" + std::to_string(int(100 - result.spectralRatio * 100)) + "%]";
    }

    if (std::abs(result.lagMs) > 50) {
        assessment += " [Time offset: " + std::to_string(int(result.lagMs)) + "ms]";
    }

    result.assessment = assessment;
    return result;
}

void printCompareResult(const CompareResult& result) {
    std::cout << "\n========== Audio Comparison Results ==========" << std::endl;
    std::cout << "Sample rate match: " << (result.sampleRateMatch ? "YES" : "NO") << std::endl;
    std::cout << "Channel match: " << (result.channelMatch ? "YES" : "NO") << std::endl;
    std::cout << std::endl;
    std::cout << "Cross-correlation: " << result.correlation << std::endl;
    std::cout << "Best lag: " << result.lagSamples << " samples (" << result.lagMs << " ms)" << std::endl;
    std::cout << std::endl;
    std::cout << "Spectral centroid (file1): " << result.spectralCentroid1 << " Hz" << std::endl;
    std::cout << "Spectral centroid (file2): " << result.spectralCentroid2 << " Hz" << std::endl;
    std::cout << "Spectral ratio (file2/file1): " << result.spectralRatio << std::endl;
    std::cout << std::endl;
    std::cout << "RMS energy (file1): " << result.rms1 << std::endl;
    std::cout << "RMS energy (file2): " << result.rms2 << std::endl;
    std::cout << "Envelope correlation: " << result.envelopeCorrelation << std::endl;
    std::cout << std::endl;
    std::cout << "ASSESSMENT: " << result.assessment << std::endl;
    std::cout << "==============================================" << std::endl;
}

}  // namespace

#ifdef WITH_AUDIO

// OpenAL loopback function pointers
static LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT = nullptr;
static LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT = nullptr;
static LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT = nullptr;

bool initLoopbackExtensions() {
    alcLoopbackOpenDeviceSOFT = (LPALCLOOPBACKOPENDEVICESOFT)alcGetProcAddress(nullptr, "alcLoopbackOpenDeviceSOFT");
    alcIsRenderFormatSupportedSOFT = (LPALCISRENDERFORMATSUPPORTEDSOFT)alcGetProcAddress(nullptr, "alcIsRenderFormatSupportedSOFT");
    alcRenderSamplesSOFT = (LPALCRENDERSAMPLESSOFT)alcGetProcAddress(nullptr, "alcRenderSamplesSOFT");

    if (!alcLoopbackOpenDeviceSOFT || !alcIsRenderFormatSupportedSOFT || !alcRenderSamplesSOFT) {
        std::cerr << "Failed to get OpenAL loopback extension functions" << std::endl;
        return false;
    }
    return true;
}

class MusicLoopbackCapture {
public:
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int FRAMES_PER_RENDER = 1024;

    bool initialize(const std::string& soundfontPath) {
        if (!initLoopbackExtensions()) {
            return false;
        }

        // Create loopback device
        device_ = alcLoopbackOpenDeviceSOFT(nullptr);
        if (!device_) {
            std::cerr << "Failed to create loopback device" << std::endl;
            return false;
        }

        // Check format support
        if (!alcIsRenderFormatSupportedSOFT(device_, SAMPLE_RATE, ALC_STEREO_SOFT, ALC_SHORT_SOFT)) {
            std::cerr << "Loopback format not supported" << std::endl;
            alcCloseDevice(device_);
            device_ = nullptr;
            return false;
        }

        // Create context with loopback attributes
        ALCint attrs[] = {
            ALC_FREQUENCY, SAMPLE_RATE,
            ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
            ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT,
            0
        };

        context_ = alcCreateContext(device_, attrs);
        if (!context_) {
            std::cerr << "Failed to create loopback context" << std::endl;
            alcCloseDevice(device_);
            device_ = nullptr;
            return false;
        }

        alcMakeContextCurrent(context_);

        // Initialize music player
        // First param is EQ path (for built-in soundfonts), second is explicit soundfont
        musicPlayer_ = std::make_unique<MusicPlayer>();
        if (!musicPlayer_->initialize("", soundfontPath)) {
            std::cerr << "Failed to initialize music player" << std::endl;
            return false;
        }

        // Enable software rendering (same as RDP flow)
        musicPlayer_->enableSoftwareRendering();

        // Verify source was created correctly
        ALuint testSource;
        alGenSources(1, &testSource);
        ALenum err = alGetError();
        if (err == AL_NO_ERROR) {
            std::cerr << "Test source created OK: " << testSource << std::endl;
            alDeleteSources(1, &testSource);
        } else {
            std::cerr << "ERROR: Can't create test source in loopback context: " << err << std::endl;
        }

        std::cerr << "Loopback capture initialized" << std::endl;
        std::cerr << "  Sample rate: " << SAMPLE_RATE << " Hz" << std::endl;
        std::cerr << "  Channels: " << CHANNELS << std::endl;

        return true;
    }

    bool captureMusic(const std::string& xmiPath, const std::string& outputPath, double durationSec) {
        if (!musicPlayer_ || !device_ || !context_) {
            std::cerr << "Not initialized" << std::endl;
            return false;
        }

        // Start playing music
        if (!musicPlayer_->play(xmiPath, false)) {
            std::cerr << "Failed to play: " << xmiPath << std::endl;
            return false;
        }

        std::cerr << "Playing: " << xmiPath << std::endl;
        std::cerr << "Capturing " << durationSec << " seconds..." << std::endl;

        // Debug: Query all sources in context
        std::cerr << "Querying OpenAL sources..." << std::endl;
        for (ALuint srcId = 1; srcId <= 5; srcId++) {
            if (alIsSource(srcId)) {
                ALint state, queued, offset;
                alGetSourcei(srcId, AL_SOURCE_STATE, &state);
                alGetSourcei(srcId, AL_BUFFERS_QUEUED, &queued);
                alGetSourcei(srcId, AL_SAMPLE_OFFSET, &offset);
                std::cerr << "  Source " << srcId << ": state=" << state
                          << " (" << (state == AL_PLAYING ? "PLAYING" : "OTHER")
                          << "), queued=" << queued
                          << ", offset=" << offset << std::endl;
            }
        }

        // Track source offset during capture
        ALuint musicSource = 1;  // MusicPlayer uses source 1

        // Calculate total frames to capture
        size_t totalFrames = static_cast<size_t>(durationSec * SAMPLE_RATE);
        size_t capturedFrames = 0;

        std::vector<int16_t> capturedAudio;
        capturedAudio.reserve(totalFrames * CHANNELS);

        std::vector<int16_t> renderBuffer(FRAMES_PER_RENDER * CHANNELS);

        auto startTime = std::chrono::steady_clock::now();

        while (capturedFrames < totalFrames && musicPlayer_->isPlaying()) {
            // Verify context is still current
            ALCcontext* currentCtx = alcGetCurrentContext();
            if (currentCtx != context_ && capturedFrames == 0) {
                std::cerr << "WARNING: Context changed! expected=" << context_
                          << " actual=" << currentCtx << std::endl;
            }

            // Render from loopback
            alcRenderSamplesSOFT(device_, renderBuffer.data(), FRAMES_PER_RENDER);

            // Check for ALC errors
            ALCenum alcErr = alcGetError(device_);
            if (alcErr != ALC_NO_ERROR && capturedFrames == 0) {
                std::cerr << "ALC error after render: " << alcErr << std::endl;
            }

            // Check for actual audio content
            int16_t maxSample = 0;
            for (size_t i = 0; i < renderBuffer.size(); i++) {
                maxSample = std::max(maxSample, static_cast<int16_t>(std::abs(renderBuffer[i])));
            }

            // Append to captured audio
            capturedAudio.insert(capturedAudio.end(), renderBuffer.begin(), renderBuffer.end());
            capturedFrames += FRAMES_PER_RENDER;

            // Progress update every second
            static size_t lastReportFrame = 0;
            if (capturedFrames - lastReportFrame >= SAMPLE_RATE) {
                double elapsed = capturedFrames / static_cast<double>(SAMPLE_RATE);

                // Check source offset to verify it's advancing
                ALint srcOffset, srcProcessed;
                alGetSourcei(musicSource, AL_SAMPLE_OFFSET, &srcOffset);
                alGetSourcei(musicSource, AL_BUFFERS_PROCESSED, &srcProcessed);

                std::cerr << "  Captured: " << elapsed << "s, maxSample=" << maxSample
                          << ", srcOffset=" << srcOffset << ", srcProcessed=" << srcProcessed << std::endl;
                lastReportFrame = capturedFrames;
            }

            // Small sleep to allow music player streaming thread to work
            std::this_thread::sleep_for(std::chrono::microseconds(
                (FRAMES_PER_RENDER * 1000000) / SAMPLE_RATE));
        }

        auto endTime = std::chrono::steady_clock::now();
        double actualDuration = std::chrono::duration<double>(endTime - startTime).count();

        std::cerr << "Capture complete" << std::endl;
        std::cerr << "  Requested duration: " << durationSec << "s" << std::endl;
        std::cerr << "  Actual wall time: " << actualDuration << "s" << std::endl;
        std::cerr << "  Captured frames: " << capturedFrames << std::endl;

        // Stop music
        musicPlayer_->stop();

        // Save to WAV
        return saveWav(outputPath, capturedAudio, SAMPLE_RATE, CHANNELS);
    }

    void shutdown() {
        if (musicPlayer_) {
            musicPlayer_->stop();
            musicPlayer_.reset();
        }
        if (context_) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context_);
            context_ = nullptr;
        }
        if (device_) {
            alcCloseDevice(device_);
            device_ = nullptr;
        }
    }

    ~MusicLoopbackCapture() {
        shutdown();
    }

private:
    ALCdevice* device_ = nullptr;
    ALCcontext* context_ = nullptr;
    std::unique_ptr<MusicPlayer> musicPlayer_;
};

TEST(MusicLoopbackCompare, CaptureAndCompare) {
    // This test is meant to be run manually with command line args
    // Skip in automated runs
    GTEST_SKIP() << "Manual test - use command line interface";
}

#endif  // WITH_AUDIO

// Command line interface
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  " << argv[0] << " capture <xmi_file> <soundfont> <output.wav> [duration_sec]" << std::endl;
        std::cerr << "  " << argv[0] << " compare <file1.wav> <file2.wav>" << std::endl;
        std::cerr << std::endl;
        std::cerr << "Examples:" << std::endl;
        std::cerr << "  " << argv[0] << " capture /path/to/qeynos2.xmi /path/to/soundfont.sf2 loopback.wav 10" << std::endl;
        std::cerr << "  " << argv[0] << " compare loopback.wav ffmpeg_capture.wav" << std::endl;
        return 1;
    }

    std::string mode = argv[1];

    if (mode == "compare") {
        if (argc < 4) {
            std::cerr << "Usage: " << argv[0] << " compare <file1.wav> <file2.wav>" << std::endl;
            return 1;
        }

        WavData wav1, wav2;
        if (!loadWav(argv[2], wav1) || !loadWav(argv[3], wav2)) {
            return 1;
        }

        auto result = compareAudio(wav1, wav2);
        printCompareResult(result);

        // Return code based on similarity
        if (result.correlation > 0.7) {
            return 0;  // Similar enough
        } else {
            return 2;  // Too different
        }
    }
    else if (mode == "capture") {
#ifdef WITH_AUDIO
        if (argc < 5) {
            std::cerr << "Usage: " << argv[0] << " capture <xmi_file> <soundfont> <output.wav> [duration_sec]" << std::endl;
            return 1;
        }

        std::string xmiFile = argv[2];
        std::string soundfont = argv[3];
        std::string outputWav = argv[4];
        double duration = (argc > 5) ? std::stod(argv[5]) : 10.0;

        MusicLoopbackCapture capture;
        if (!capture.initialize(soundfont)) {
            std::cerr << "Failed to initialize capture" << std::endl;
            return 1;
        }

        if (!capture.captureMusic(xmiFile, outputWav, duration)) {
            std::cerr << "Failed to capture music" << std::endl;
            return 1;
        }

        std::cerr << "\nCapture complete: " << outputWav << std::endl;
        return 0;
#else
        std::cerr << "Audio support not compiled in" << std::endl;
        return 1;
#endif
    }
    else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        std::cerr << "Use 'capture' or 'compare'" << std::endl;
        return 1;
    }
}
