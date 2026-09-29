// B875 (issue #219): the Chase-rig solver (tools/chase_fit/chase_rig_fit.h),
// validated NON-CIRCULARLY, offline.
//
// MR !31's "fit" projected its correspondences with the reference camera and
// then "recovered" the reference. Every test here is built so that cannot
// happen silently:
//   - the synthetic ground truth kTruth is a camera that is NOT kChaseRig on
//     any axis (checked by a test of its own), so recovering kChaseRig from
//     this data would be a failure, and a test says so;
//   - the image points carry noise and are rounded to the 0.5 px grid a person
//     clicking pixels produces, as the real manual correspondences will be;
//   - noise-free projected points -- the MR !31 input -- are REFUSED, whether
//     the projecting camera was kChaseRig or the truth;
//   - the result must not depend on where the solver starts, including a
//     start AT kChaseRig.
// What no test here can do is tell a clicked point from a projected one that
// was then rounded to the grid; FIT_PLAN.md closes that by procedure.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "chase_fit/chase_rig_fit.h"
#include "client/graphics/player/camera_rig.h"
#include "scoped_temp_dir.h"

using namespace chase_fit;

namespace {

constexpr double kFitEye = 6.023;  // FitOptions' default reference eye
constexpr double kDwarfEye = 3.58;

// The synthetic official camera. Every field differs from fable's kChaseRig
// (az 0, pitch 6, dist 4, height 7, hfov 62.8) by far more than the bounds.
const RigParams kTruth{2.5, 11.0, 6.5, 5.8, 70.0, 1.0};
// fable's current constants, as a RigParams (the camera the data must NOT be
// mistaken for).
RigParams FableRig() {
    const EQT::Graphics::CameraRig r = EQT::Graphics::kChaseRig;
    return RigParams{r.azimuthDeg, r.pitchDeg, r.distance, r.height, 62.8, 1.0};
}

// Recovery bounds at 0.7 px click noise, 6-8 stills x 20 points.
constexpr double kAzTol = 0.1, kPitchTol = 0.1, kDistTol = 0.05, kHeightTol = 0.05,
                 kFovTol = 0.2, kExpTol = 0.05;

// A deterministic normal deviate: mt19937's raw stream is standardised;
// std::normal_distribution's mapping is not, so do Box-Muller by hand.
struct Noise {
    std::mt19937 rng{20260929u};
    double uniform() { return (static_cast<double>(rng()) + 0.5) / 4294967296.0; }
    double normal() {
        const double u1 = uniform(), u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979323846 * u2);
    }
};

struct Scene {
    std::vector<CapturePose> poses;
    std::vector<Correspondence> points;
};

std::vector<CapturePose> Stills(int n, bool twoEyes) {
    const double headings[] = {0.0, 60.0, 130.0, 200.0, 290.0, 400.0, 460.0, 330.0};
    std::vector<CapturePose> out;
    for (int i = 0; i < n; ++i) {
        CapturePose p;
        p.still = "synthetic_" + std::to_string(i) + ".png";
        p.width = 1024;
        p.height = 768;
        p.playerX = (i % 2) ? -40.0 : 100.0;
        p.playerY = (i % 2) ? 210.0 : 50.0;
        p.feetZ = (i % 2) ? -8.0 : 2.0;
        p.headingEq512 = headings[i % 8];
        p.eyeAboveFeet = (twoEyes && (i % 2)) ? kDwarfEye : kFitEye;
        out.push_back(p);
    }
    return out;
}

// Static "geometry": random points around the player, kept when `camera`
// sees them. noisePx < 0 means exact projection, no rounding (the circular
// input); otherwise Gaussian noise then rounding to the 0.5 px click grid.
Scene MakeScene(const RigParams& camera, int stills, bool twoEyes, double noisePx) {
    Scene s;
    s.poses = Stills(stills, twoEyes);
    Noise noise;
    for (size_t pi = 0; pi < s.poses.size(); ++pi) {
        const CapturePose& p = s.poses[pi];
        int kept = 0;
        for (int tries = 0; kept < 20 && tries < 100000; ++tries) {
            const double ang = noise.uniform() * 2.0 * 3.14159265358979323846;
            const double rad = 6.0 + noise.uniform() * 54.0;
            const double wx = p.playerX + std::sin(ang) * rad;
            const double wy = p.playerY + std::cos(ang) * rad;
            const double wz = p.feetZ - 2.0 + noise.uniform() * 27.0;
            double u = 0.0, v = 0.0;
            if (!projectPoint(camera, p, kFitEye, wx, wy, wz, u, v)) continue;
            if (u < 20.0 || v < 20.0 || u > p.width - 20.0 || v > p.height - 20.0) continue;
            if (noisePx >= 0.0) {
                u = std::round((u + noisePx * noise.normal()) * 2.0) / 2.0;
                v = std::round((v + noisePx * noise.normal()) * 2.0) / 2.0;
            }
            Correspondence c;
            c.pose = static_cast<int>(pi);
            c.worldX = wx;
            c.worldY = wy;
            c.worldZ = wz;
            c.u = u;
            c.v = v;
            c.geometryRef = "synthetic:" + std::to_string(pi) + ":" + std::to_string(kept);
            s.points.push_back(c);
            ++kept;
        }
    }
    return s;
}

void ExpectRecovers(const FitResult& r, const RigParams& truth, bool withExponent) {
    EXPECT_TRUE(r.usable()) << (r.refusals.empty() ? "" : r.refusals.front());
    EXPECT_NEAR(r.rig.azimuthDeg, truth.azimuthDeg, kAzTol);
    EXPECT_NEAR(r.rig.pitchDeg, truth.pitchDeg, kPitchTol);
    EXPECT_NEAR(r.rig.distance, truth.distance, kDistTol);
    EXPECT_NEAR(r.rig.height, truth.height, kHeightTol);
    EXPECT_NEAR(r.rig.hfovDeg, truth.hfovDeg, kFovTol);
    if (withExponent) EXPECT_NEAR(r.rig.eyeScaleExponent, truth.eyeScaleExponent, kExpTol);
    // The reported uncertainty is honest: the truth sits inside 4 sigma.
    EXPECT_LE(std::fabs(r.rig.distance - truth.distance), 4.0 * r.sigma[2] + 1e-9);
    EXPECT_LE(std::fabs(r.rig.pitchDeg - truth.pitchDeg), 4.0 * r.sigma[1] + 1e-9);
    // Click noise is in the residual, not fitted away.
    EXPECT_GT(r.rmsPx, 0.3);
    EXPECT_LT(r.rmsPx, 1.5);
}

}  // namespace

TEST(ChaseRigFit, TheSyntheticTruthIsNotFablesRig) {
    const RigParams f = FableRig();
    EXPECT_GT(std::fabs(kTruth.azimuthDeg - f.azimuthDeg), 4 * kAzTol);
    EXPECT_GT(std::fabs(kTruth.pitchDeg - f.pitchDeg), 4 * kPitchTol);
    EXPECT_GT(std::fabs(kTruth.distance - f.distance), 4 * kDistTol);
    EXPECT_GT(std::fabs(kTruth.height - f.height), 4 * kHeightTol);
    EXPECT_GT(std::fabs(kTruth.hfovDeg - f.hfovDeg), 4 * kFovTol);
}

// The solver's pose model IS the production rig geometry (exponent 1 at the
// reference eye == computeCameraRigPose; at another eye == scaleChaseRigForEye
// first), so the fitted numbers mean what kChaseRig's numbers mean.
TEST(ChaseRigFit, ThePoseModelIsTheProductionRig) {
    using namespace EQT::Graphics;
    const CameraRig rigs[] = {kChaseRig, CameraRig{2.5f, 11.0f, 6.5f, 5.8f}, CameraRig{-7.0f, 30.0f, 12.0f, 3.0f}};
    for (const CameraRig& cr : rigs) {
        for (double h : {0.0, 123.25, 256.0, 400.5}) {
            for (double eye : {kFitEye, kDwarfEye}) {
                CapturePose p;
                p.playerX = 428.0;
                p.playerY = -74.0;
                p.feetZ = 3.119;
                p.headingEq512 = h;
                p.eyeAboveFeet = eye;
                double cam[3], tgt[3];
                cameraPose(RigParams{cr.azimuthDeg, cr.pitchDeg, cr.distance, cr.height, 62.8, 1.0}, p,
                           kFitEye, cam, tgt);
                const CameraRigPose want = computeCameraRigPose(
                    scaleChaseRigForEye(cr, static_cast<float>(eye)), 428.0f, -74.0f, 3.119f,
                    static_cast<float>(h), CameraAzimuthFrame::PlayerHeading);
                EXPECT_NEAR(cam[0], want.camX, 1e-4);
                EXPECT_NEAR(cam[1], want.camY, 1e-4);
                EXPECT_NEAR(cam[2], want.camZ, 1e-4);
                EXPECT_NEAR(tgt[0], want.targetX, 1e-4);
                EXPECT_NEAR(tgt[1], want.targetY, 1e-4);
                EXPECT_NEAR(tgt[2], want.targetZ, 1e-4);
            }
        }
    }
}

TEST(ChaseRigFit, RecoversAGroundTruthThatIsNotKChaseRig) {
    const Scene s = MakeScene(kTruth, 6, false, 0.7);
    ASSERT_TRUE(checkInputs(s.poses, s.points, FitOptions{}).empty());
    const FitResult r = fitChaseRig(s.poses, s.points, RigParams{0.0, 15.0, 10.0, 4.0, 55.0, 1.0},
                                    FitOptions{});
    ExpectRecovers(r, kTruth, false);
}

TEST(ChaseRigFit, StartingAtKChaseRigDoesNotPullTheAnswerToIt) {
    const Scene s = MakeScene(kTruth, 6, false, 0.7);
    const FitResult r = fitChaseRig(s.poses, s.points, FableRig(), FitOptions{});
    ExpectRecovers(r, kTruth, false);
    EXPECT_GT(std::fabs(r.rig.distance - FableRig().distance), 1.0)
        << "the fit stayed at its start (the MR !31 'zero delta' result)";
    // And a third, far start lands on the same minimum.
    const FitResult far = fitChaseRig(s.poses, s.points, RigParams{-5.0, 2.0, 3.0, 9.0, 75.0, 1.0},
                                      FitOptions{});
    EXPECT_NEAR(far.rig.distance, r.rig.distance, 1e-3);
    EXPECT_NEAR(far.rig.pitchDeg, r.rig.pitchDeg, 1e-3);
    EXPECT_NEAR(far.rig.hfovDeg, r.rig.hfovDeg, 1e-3);
}

// The check a later unit runs against fable's constants: data from another
// camera must NOT be explained by kChaseRig, and must be by the truth.
TEST(ChaseRigFit, KChaseRigDoesNotExplainStillsFromAnotherCamera) {
    const Scene s = MakeScene(kTruth, 6, false, 0.7);
    double rmsTruth = 0.0, rmsFable = 0.0;
    EXPECT_TRUE(rigExplainsStills(kTruth, s.poses, s.points, kFitEye, &rmsTruth)) << rmsTruth;
    EXPECT_FALSE(rigExplainsStills(FableRig(), s.poses, s.points, kFitEye, &rmsFable)) << rmsFable;
    EXPECT_GT(rmsFable, 10.0 * kMaxAcceptedRmsPx);
}

// The MR !31 input: points projected with the camera being solved for, no
// clicking. Refused both at the input check and at the fit.
TEST(ChaseRigFit, CorrespondencesGeneratedWithACameraAreRefused) {
    for (const RigParams& generator : {FableRig(), kTruth}) {
        const Scene s = MakeScene(generator, 6, false, -1.0);
        const std::vector<std::string> in = checkInputs(s.poses, s.points, FitOptions{});
        bool offGrid = false;
        for (const std::string& m : in) offGrid |= (m.find("click grid") != std::string::npos);
        EXPECT_TRUE(offGrid) << "projected points must fail the click-grid check";
        const FitResult r = fitChaseRig(s.poses, s.points, RigParams{0.0, 15.0, 10.0, 4.0, 55.0, 1.0},
                                        FitOptions{});
        EXPECT_FALSE(r.usable());
        EXPECT_LT(r.rmsPx, kMinPlausibleRmsPx) << "a noise-free fit is the circular signature";
        bool circular = false;
        for (const std::string& m : r.refusals) circular |= (m.find("circular") != std::string::npos);
        EXPECT_TRUE(circular);
    }
}

TEST(ChaseRigFit, TheEyeScaleExponentIsFittedFromTwoEyesOnly) {
    // fable's law (exponent 1) and "no race scaling" (0) are told apart.
    for (double exponent : {1.0, 0.0}) {
        RigParams truth = kTruth;
        truth.eyeScaleExponent = exponent;
        const Scene s = MakeScene(truth, 8, true, 0.7);
        FitOptions opt;
        opt.fitEyeScaleExponent = true;
        const FitResult r = fitChaseRig(s.poses, s.points, RigParams{0.0, 15.0, 10.0, 4.0, 55.0, 0.5}, opt);
        SCOPED_TRACE(::testing::Message() << "true exponent " << exponent);
        ExpectRecovers(r, truth, true);
    }
    // One eye height: the exponent is not identifiable, and asking is refused.
    const Scene one = MakeScene(kTruth, 6, false, 0.7);
    FitOptions opt;
    opt.fitEyeScaleExponent = true;
    const FitResult r = fitChaseRig(one.poses, one.points, RigParams{0.0, 15.0, 10.0, 4.0, 55.0, 0.5}, opt);
    EXPECT_FALSE(r.usable());
}

TEST(ChaseRigFit, TooFewStillsOrHeadingsOrPointsAreRefused) {
    const Scene four = MakeScene(kTruth, 4, false, 0.7);
    EXPECT_FALSE(checkInputs(four.poses, four.points, FitOptions{}).empty()) << "4 stills";
    Scene sameHeading = MakeScene(kTruth, 6, false, 0.7);
    for (CapturePose& p : sameHeading.poses) p.headingEq512 = 128.0;
    EXPECT_FALSE(checkInputs(sameHeading.poses, sameHeading.points, FitOptions{}).empty()) << "1 heading";
    Scene sparse = MakeScene(kTruth, 6, false, 0.7);
    sparse.points.resize(sparse.points.size() - 15);  // the last still keeps 5
    EXPECT_FALSE(checkInputs(sparse.poses, sparse.points, FitOptions{}).empty()) << "5 points";
    Scene noProvenance = MakeScene(kTruth, 6, false, 0.7);
    noProvenance.poses[2].still.clear();
    EXPECT_FALSE(checkInputs(noProvenance.poses, noProvenance.points, FitOptions{}).empty());
}

TEST(ChaseRigFit, TheCorrespondenceTsvRoundTrips) {
    const willeq_test::ScopedTempDir tmp("chase_rig_fit_tsv");
    const std::string path = (tmp / "c.tsv").string();
    {
        std::ofstream out(path);
        out << "# kind\tid\tstill\tw\th\tplayerX\tplayerY\tfeetZ\theading0_512\teye\n";
        out << "pose\ta\tqeynos2_h160.png\t1024\t768\t-74\t428\t3.751\t160\t6.023\n";
        out << "point\ta\t-60.5\t440\t12\t512.5\t300\tqeynos2:frag123:v7\n";
    }
    std::vector<CapturePose> poses;
    std::vector<Correspondence> points;
    std::string err;
    ASSERT_TRUE(readCorrespondenceTsv(path, poses, points, err)) << err;
    ASSERT_EQ(poses.size(), 1u);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_EQ(poses[0].still, "qeynos2_h160.png");
    EXPECT_DOUBLE_EQ(poses[0].headingEq512, 160.0);
    EXPECT_EQ(points[0].pose, 0);
    EXPECT_DOUBLE_EQ(points[0].u, 512.5);
    EXPECT_EQ(points[0].geometryRef, "qeynos2:frag123:v7");

    const std::string bad = (tmp / "bad.tsv").string();
    { std::ofstream out(bad); out << "point\tz\t1\t2\t3\t4\t5\tref\n"; }
    EXPECT_FALSE(readCorrespondenceTsv(bad, poses, points, err)) << "a point before its pose";
}
