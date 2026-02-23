/**
 * Test to compare two WAV files for audio similarity
 *
 * Usage:
 *   ./test_music_loopback_compare compare <file1.wav> <file2.wav>
 */

#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <numeric>

#include "dr_wav.h"

namespace {

// WAV file utilities
struct WavData {
    std::vector<int16_t> samples;
    int sampleRate = 0;
    int channels = 0;
    double duration = 0.0;
};

bool loadWav(const std::string& filepath, WavData& wav) {
    drwav wavFile;
    if (!drwav_init_file(&wavFile, filepath.c_str(), nullptr)) {
        std::cerr << "Failed to open WAV file: " << filepath << std::endl;
        return false;
    }

    wav.sampleRate = wavFile.sampleRate;
    wav.channels = wavFile.channels;
    wav.samples.resize(wavFile.totalPCMFrameCount * wavFile.channels);

    drwav_uint64 framesRead = drwav_read_pcm_frames_s16(&wavFile, wavFile.totalPCMFrameCount, wav.samples.data());
    drwav_uninit(&wavFile);

    if (framesRead != wavFile.totalPCMFrameCount) {
        std::cerr << "Warning: Read " << framesRead << " frames, expected " << wavFile.totalPCMFrameCount << std::endl;
        wav.samples.resize(framesRead * wav.channels);
    }

    wav.duration = static_cast<double>(wav.samples.size() / wav.channels) / wav.sampleRate;

    std::cerr << "Loaded WAV: " << filepath << std::endl;
    std::cerr << "  Sample rate: " << wav.sampleRate << " Hz" << std::endl;
    std::cerr << "  Channels: " << wav.channels << std::endl;
    std::cerr << "  Duration: " << wav.duration << " seconds" << std::endl;
    std::cerr << "  Samples: " << wav.samples.size() << std::endl;

    return true;
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

struct CorrelationResult {
    double correlation;
    int lag;
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

struct CompareResult {
    double correlation;
    int lagSamples;
    double lagMs;
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

    auto mono1 = toMono(wav1.samples, wav1.channels);
    auto mono2 = toMono(wav2.samples, wav2.channels);

    size_t minLen = std::min(mono1.size(), mono2.size());
    mono1.resize(minLen);
    mono2.resize(minLen);

    result.rms1 = computeRMS(mono1);
    result.rms2 = computeRMS(mono2);

    normalize(mono1);
    normalize(mono2);

    int maxLag = wav1.sampleRate / 2;
    auto corrResult = findBestCorrelation(mono1, mono2, maxLag);
    result.correlation = corrResult.correlation;
    result.lagSamples = corrResult.lag;
    result.lagMs = (corrResult.lag * 1000.0) / wav1.sampleRate;

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

    if (result.correlation > 0.9) {
        result.assessment = "EXCELLENT - Audio files are very similar";
    } else if (result.correlation > 0.7) {
        result.assessment = "GOOD - Audio files are similar with some differences";
    } else if (result.correlation > 0.5) {
        result.assessment = "MODERATE - Audio files have noticeable differences";
    } else if (result.correlation > 0.3) {
        result.assessment = "POOR - Audio files are quite different";
    } else {
        result.assessment = "VERY POOR - Audio files are very different or uncorrelated";
    }

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
    std::cout << "RMS energy (file1): " << result.rms1 << std::endl;
    std::cout << "RMS energy (file2): " << result.rms2 << std::endl;
    std::cout << "Envelope correlation: " << result.envelopeCorrelation << std::endl;
    std::cout << std::endl;
    std::cout << "ASSESSMENT: " << result.assessment << std::endl;
    std::cout << "==============================================" << std::endl;
}

}  // namespace

TEST(MusicLoopbackCompare, CompareSkip) {
    // This test is meant to be run manually with command line args
    GTEST_SKIP() << "Manual test - use command line interface";
}

// Command line interface
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "  " << argv[0] << " compare <file1.wav> <file2.wav>" << std::endl;
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

        if (result.correlation > 0.7) {
            return 0;
        } else {
            return 2;
        }
    }
    else {
        std::cerr << "Unknown mode: " << mode << std::endl;
        std::cerr << "Use 'compare'" << std::endl;
        return 1;
    }
}
