#pragma once

// B1069 (#389) device acceptance 2: a band-limited rational resampler for
// the mixer -> device rate step (22050 -> 48000 by default).
//
// Why not miniaudio's linear ma_resampler: measured on pure tones through the
// production path (refactor/B1069/README.md), linear upsampling leaves the
// image at 2*22050 - f, which at a 48000 output folds back IN BAND at
// f + 3900 Hz -- an inharmonic spur at -50 dBc for a 3 kHz tone and -37 dBc
// at 6 kHz (device SINAD 43 / 31 dB, against 154 dB in the 22050 mix). Will
// heard music on the XU4 as "distorted". This is a polyphase Kaiser-windowed
// sinc with L = out/gcd phases, a fixed tap count per phase and its history
// carried across calls, so a period boundary is invisible.
//
// Real-time contract: init() allocates everything; process() never
// allocates, locks or logs. Interleaved stereo float.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace EQT {
namespace Audio {

class PolyphaseResampler {
public:
    // Taps per phase. 48 at 22050 Hz input gives a ~2.3 kHz transition band
    // (passband to ~8.8 kHz, >= 80 dB down from 11.025 kHz).
    static constexpr int kTaps = 48;
    // Largest phase count accepted (table = kTaps * L floats). A rate pair
    // needing more returns false from init() and the caller falls back.
    static constexpr uint32_t kMaxPhases = 2048;

    // maxOutputFrames: the largest process() call.
    bool init(uint32_t inRate, uint32_t outRate, uint32_t maxOutputFrames);

    // Input frames the NEXT process(outFrames) call will consume. Exact.
    uint32_t inputFramesFor(uint32_t outFrames) const;

    // Writable space for those frames (interleaved stereo, inputFramesFor()
    // frames); fill it, then call process().
    float* inputBuffer();

    // Consumes inputFramesFor(outFrames) frames from inputBuffer() and writes
    // outFrames frames to out.
    void process(float* out, uint32_t outFrames);

private:
    uint32_t L_ = 1, M_ = 1;           // out/g, in/g
    std::vector<float> coef_;          // [phase][tap]
    std::vector<float> buf_;           // interleaved stereo history + new input
    size_t avail_ = 0;                 // frames valid in buf_
    size_t ip_ = 0;                    // input frame index of the next output
    uint32_t acc_ = 0;                 // fractional position, 0..L-1
};

}  // namespace Audio
}  // namespace EQT
