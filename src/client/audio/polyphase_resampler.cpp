#include "client/audio/polyphase_resampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace EQT {
namespace Audio {

namespace {

// Zeroth-order modified Bessel function (Kaiser window), series form.
double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}

constexpr int kHalf = PolyphaseResampler::kTaps / 2;
constexpr double kKaiserBeta = 7.86;  // ~80 dB stopband

}  // namespace

bool PolyphaseResampler::init(uint32_t inRate, uint32_t outRate, uint32_t maxOutputFrames) {
    if (inRate == 0 || outRate == 0) return false;
    const uint32_t g = std::gcd(inRate, outRate);
    L_ = outRate / g;
    M_ = inRate / g;
    if (L_ > kMaxPhases) return false;

    // Cutoff just under the lower Nyquist, in cycles per INPUT sample.
    const double nyq = 0.5 * std::min(inRate, outRate);
    const double fc = 0.90 * nyq / inRate;
    const double i0b = besselI0(kKaiserBeta);
    coef_.assign(static_cast<size_t>(L_) * kTaps, 0.0f);
    for (uint32_t p = 0; p < L_; ++p) {
        const double frac = static_cast<double>(p) / L_;
        double sum = 0.0;
        double tmp[kTaps];
        for (int m = 0; m < kTaps; ++m) {
            // Tap m reads input frame ip - kHalf + 1 + m; its distance from
            // the output instant (ip + frac) is x.
            const double x = (kHalf - 1 - m) + frac;
            const double s = (x == 0.0) ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
            const double r = x / kHalf;
            const double w = (std::fabs(r) >= 1.0) ? 0.0 : besselI0(kKaiserBeta * std::sqrt(1.0 - r * r)) / i0b;
            tmp[m] = s * w;
            sum += tmp[m];
        }
        // Unity DC gain on EVERY phase, or the phase-to-phase gain ripple is
        // itself a spur at the phase rate.
        for (int m = 0; m < kTaps; ++m) coef_[static_cast<size_t>(p) * kTaps + m] = static_cast<float>(tmp[m] / sum);
    }

    // Capacity: history + the most input one call can need + slack.
    const size_t maxIn = static_cast<size_t>(
        (static_cast<uint64_t>(maxOutputFrames) * M_) / L_ + 2);
    buf_.assign((static_cast<size_t>(kTaps) * 2 + maxIn + 4) * 2, 0.0f);
    // Start with kHalf frames of silence as history; the first output sits
    // on the last of them (latency kHalf input frames).
    avail_ = kHalf;
    ip_ = kHalf - 1;
    acc_ = 0;
    return true;
}

uint32_t PolyphaseResampler::inputFramesFor(uint32_t outFrames) const {
    if (outFrames == 0) return 0;
    // The last output of this call sits at input frame ipLast; it reads up to
    // ipLast + kHalf.
    const uint64_t ipLast = ip_ + (static_cast<uint64_t>(acc_) + static_cast<uint64_t>(outFrames - 1) * M_) / L_;
    const uint64_t needAvail = ipLast + kHalf + 1;
    return needAvail > avail_ ? static_cast<uint32_t>(needAvail - avail_) : 0u;
}

float* PolyphaseResampler::inputBuffer() { return buf_.data() + avail_ * 2; }

void PolyphaseResampler::process(float* out, uint32_t outFrames) {
    avail_ += inputFramesFor(outFrames);
    const float* x = buf_.data();
    for (uint32_t n = 0; n < outFrames; ++n) {
        const float* h = coef_.data() + static_cast<size_t>(acc_) * kTaps;
        const float* s = x + (ip_ + 1 - kHalf) * 2;
        float l = 0.0f, r = 0.0f;
        for (int m = 0; m < kTaps; ++m) {
            l += h[m] * s[m * 2];
            r += h[m] * s[m * 2 + 1];
        }
        out[n * 2] = l;
        out[n * 2 + 1] = r;
        acc_ += M_;
        ip_ += acc_ / L_;
        acc_ %= L_;
    }
    // Keep only the history the next output reads (from ip - kHalf + 1).
    const size_t keepFrom = ip_ + 1 - kHalf;
    if (keepFrom > 0) {
        std::memmove(buf_.data(), buf_.data() + keepFrom * 2, (avail_ - keepFrom) * 2 * sizeof(float));
        avail_ -= keepFrom;
        ip_ -= keepFrom;
    }
}

}  // namespace Audio
}  // namespace EQT
