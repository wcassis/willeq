// B875 -- chase_rig_fit: fit the Chase rig to a MANUAL correspondence TSV
// (refactor/B875/FIT_PLAN.md has the format and the procedure).
//
//   chase_rig_fit <correspondences.tsv> [--fit-eye-scale] [--ref-eye E]
//                 [--check az pitch dist height hfov]
//
// Exit 0: the fit is usable (every refusal check passed). Exit 1: refused, or
// --check's rig does not explain the stills. Exit 2: usage / unreadable input.
// The start point is deliberately generic, NOT fable's kChaseRig, and a
// second start is run from a different corner; the two must agree.

#include "chase_fit/chase_rig_fit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace chase_fit;

namespace {

void PrintRig(const char* label, const FitResult& r) {
    std::printf("%s: az=%.4f pitch=%.4f dist=%.4f height=%.4f hfov=%.4f eye_exp=%.4f "
                "rms_px=%.4f iters=%d\n",
                label, r.rig.azimuthDeg, r.rig.pitchDeg, r.rig.distance, r.rig.height,
                r.rig.hfovDeg, r.rig.eyeScaleExponent, r.rmsPx, r.iterations);
    std::printf("%s sigma: az=%.4f pitch=%.4f dist=%.4f height=%.4f hfov=%.4f eye_exp=%.4f\n",
                label, r.sigma[0], r.sigma[1], r.sigma[2], r.sigma[3], r.sigma[4], r.sigma[5]);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <correspondences.tsv> [--fit-eye-scale] [--ref-eye E] "
                     "[--check az pitch dist height hfov]\n",
                     argv[0]);
        return 2;
    }
    FitOptions opt;
    bool check = false;
    RigParams checkRig;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fit-eye-scale") == 0) {
            opt.fitEyeScaleExponent = true;
        } else if (std::strcmp(argv[i], "--ref-eye") == 0 && i + 1 < argc) {
            opt.refEyeAboveFeet = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--check") == 0 && i + 5 < argc) {
            check = true;
            checkRig.azimuthDeg = std::atof(argv[++i]);
            checkRig.pitchDeg = std::atof(argv[++i]);
            checkRig.distance = std::atof(argv[++i]);
            checkRig.height = std::atof(argv[++i]);
            checkRig.hfovDeg = std::atof(argv[++i]);
        } else {
            std::fprintf(stderr, "unknown or incomplete option '%s'\n", argv[i]);
            return 2;
        }
    }

    std::vector<CapturePose> poses;
    std::vector<Correspondence> points;
    std::string err;
    if (!readCorrespondenceTsv(argv[1], poses, points, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    std::printf("stills=%zu points=%zu ref_eye=%.4f\n", poses.size(), points.size(),
                opt.refEyeAboveFeet);

    int rc = 0;
    if (check) {
        double rms = 0.0;
        const bool ok = rigExplainsStills(checkRig, poses, points, opt.refEyeAboveFeet, &rms);
        std::printf("check: rms_px=%.4f bound=%.1f -> %s\n", rms, kMaxAcceptedRmsPx,
                    ok ? "EXPLAINS the stills" : "does NOT explain the stills");
        if (!ok) rc = 1;
    }

    // Two generic starts from opposite sides of the plausible range.
    const RigParams startA{0.0, 15.0, 10.0, 4.0, 55.0, 1.0};
    // The exponent is only a free parameter with --fit-eye-scale; otherwise
    // both starts hold fable's law (1) so the printed value means something.
    const RigParams startB{-5.0, 2.0, 3.0, 9.0, 75.0, opt.fitEyeScaleExponent ? 0.5 : 1.0};
    const FitResult a = fitChaseRig(poses, points, startA, opt);
    const FitResult b = fitChaseRig(poses, points, startB, opt);
    PrintRig("fit[startA]", a);
    PrintRig("fit[startB]", b);
    std::vector<std::string> refusals = a.refusals;
    const bool agree = std::fabs(a.rig.azimuthDeg - b.rig.azimuthDeg) < 0.05 &&
                       std::fabs(a.rig.pitchDeg - b.rig.pitchDeg) < 0.05 &&
                       std::fabs(a.rig.distance - b.rig.distance) < 0.01 &&
                       std::fabs(a.rig.height - b.rig.height) < 0.01 &&
                       std::fabs(a.rig.hfovDeg - b.rig.hfovDeg) < 0.05;
    if (!agree) refusals.push_back("the two starts reached different minima");
    for (const std::string& r : refusals) std::printf("REFUSED: %s\n", r.c_str());
    if (!refusals.empty()) rc = 1;
    std::printf("verdict: %s\n", refusals.empty() ? "USABLE" : "NOT USABLE");
    return rc;
}
