// B875 -- the Chase-rig solver. See chase_rig_fit.h for the model and the
// circularity guards, refactor/B875/FIT_PLAN.md for the procedure.

#include "chase_fit/chase_rig_fit.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace chase_fit {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kNumParams = 6;
// A point that falls behind the camera during a trial step costs this much,
// so the step is rejected rather than silently dropped.
constexpr double kBehindPenaltyPx = 1000.0;

double& Param(RigParams& r, int i) {
    switch (i) {
        case 0: return r.azimuthDeg;
        case 1: return r.pitchDeg;
        case 2: return r.distance;
        case 3: return r.height;
        case 4: return r.hfovDeg;
        default: return r.eyeScaleExponent;
    }
}

void Residuals(const RigParams& rig, const std::vector<CapturePose>& poses,
               const std::vector<Correspondence>& points, double refEye,
               std::vector<double>& out) {
    out.resize(points.size() * 2);
    for (size_t i = 0; i < points.size(); ++i) {
        const Correspondence& c = points[i];
        double u = 0.0, v = 0.0;
        if (projectPoint(rig, poses[static_cast<size_t>(c.pose)], refEye, c.worldX, c.worldY,
                         c.worldZ, u, v)) {
            out[2 * i] = u - c.u;
            out[2 * i + 1] = v - c.v;
        } else {
            out[2 * i] = kBehindPenaltyPx;
            out[2 * i + 1] = kBehindPenaltyPx;
        }
    }
}

double SumSq(const std::vector<double>& r) {
    double s = 0.0;
    for (double x : r) s += x * x;
    return s;
}

// Solve A x = b for a small dense system (Gauss-Jordan, partial pivoting).
// Returns false if A is singular.
bool Solve(std::vector<std::vector<double>> A, std::vector<double> b, std::vector<double>& x) {
    const size_t n = b.size();
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t r = col + 1; r < n; ++r) {
            if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
        }
        if (std::fabs(A[piv][col]) < 1e-300) return false;
        std::swap(A[piv], A[col]);
        std::swap(b[piv], b[col]);
        for (size_t r = 0; r < n; ++r) {
            if (r == col) continue;
            const double f = A[r][col] / A[col][col];
            for (size_t k = col; k < n; ++k) A[r][k] -= f * A[col][k];
            b[r] -= f * b[col];
        }
    }
    x.resize(n);
    for (size_t i = 0; i < n; ++i) x[i] = b[i] / A[i][i];
    return true;
}

bool OnClickGrid(double x) {
    const double twice = x * 2.0;
    return std::fabs(twice - std::round(twice)) < 1e-9;
}

}  // namespace

void cameraPose(const RigParams& rig, const CapturePose& pose, double refEye, double cam[3],
                double target[3]) {
    double scale = 1.0;
    if (pose.eyeAboveFeet > 0.0 && refEye > 0.0) {
        scale = std::pow(pose.eyeAboveFeet / refEye, rig.eyeScaleExponent);
    }
    const double dist = rig.distance * scale;
    const double height = rig.height * scale;
    const double a = pose.headingEq512 / 512.0 * 2.0 * kPi + rig.azimuthDeg * kPi / 180.0;
    const double p = rig.pitchDeg * kPi / 180.0;
    const double horiz = dist * std::cos(p);
    target[0] = pose.playerX;
    target[1] = pose.playerY;
    target[2] = pose.feetZ + height;
    cam[0] = target[0] - std::sin(a) * horiz;
    cam[1] = target[1] - std::cos(a) * horiz;
    cam[2] = target[2] + dist * std::sin(p);
}

bool projectPoint(const RigParams& rig, const CapturePose& pose, double refEye, double wx,
                  double wy, double wz, double& u, double& v) {
    double c[3], t[3];
    cameraPose(rig, pose, refEye, c, t);
    double f[3] = {t[0] - c[0], t[1] - c[1], t[2] - c[2]};
    const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (!(fl > 0.0)) return false;
    for (double& x : f) x /= fl;
    // right = f x zUp; up = right x f. At heading 0 the camera looks +Y and
    // the screen's right is +X (camera_rig.h, the renderer's own frame).
    double r[3] = {f[1], -f[0], 0.0};
    const double rl = std::sqrt(r[0] * r[0] + r[1] * r[1]);
    if (!(rl > 0.0)) return false;
    r[0] /= rl;
    r[1] /= rl;
    const double up[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2],
                          r[0] * f[1] - r[1] * f[0]};
    const double d[3] = {wx - c[0], wy - c[1], wz - c[2]};
    const double zc = d[0] * f[0] + d[1] * f[1] + d[2] * f[2];
    if (!(zc > 1e-6)) return false;
    const double xc = d[0] * r[0] + d[1] * r[1];
    const double yc = d[0] * up[0] + d[1] * up[1] + d[2] * up[2];
    const double focal = (pose.width * 0.5) / std::tan(rig.hfovDeg * kPi / 360.0);
    u = pose.width * 0.5 + focal * xc / zc;
    v = pose.height * 0.5 - focal * yc / zc;
    return true;
}

double rmsResidualPx(const RigParams& rig, const std::vector<CapturePose>& poses,
                     const std::vector<Correspondence>& points, double refEye) {
    if (points.empty()) return 0.0;
    std::vector<double> r;
    Residuals(rig, poses, points, refEye, r);
    // RMS per POINT (the pixel distance), not per coordinate.
    return std::sqrt(SumSq(r) / static_cast<double>(points.size()));
}

std::vector<std::string> checkInputs(const std::vector<CapturePose>& poses,
                                     const std::vector<Correspondence>& points,
                                     const FitOptions& options) {
    std::vector<std::string> out;
    if (static_cast<int>(poses.size()) < kMinPoses) {
        out.push_back("fewer than " + std::to_string(kMinPoses) + " stills (" +
                      std::to_string(poses.size()) + ")");
    }
    std::set<long> headings;
    std::set<long> eyes;
    for (const CapturePose& p : poses) {
        headings.insert(std::lround(p.headingEq512 * 100.0));
        eyes.insert(std::lround(p.eyeAboveFeet * 1000.0));
        if (p.still.empty()) out.push_back("a still has no file name (provenance)");
        if (p.width <= 0 || p.height <= 0) out.push_back("still '" + p.still + "' has no image size");
        if (!(p.eyeAboveFeet > 0.0)) out.push_back("still '" + p.still + "' has no eye height");
    }
    if (static_cast<int>(headings.size()) < kMinDistinctHeadings) {
        out.push_back("fewer than " + std::to_string(kMinDistinctHeadings) + " distinct headings");
    }
    if (options.fitEyeScaleExponent && eyes.size() < 2) {
        out.push_back("the eye-scale exponent is not identifiable from one eye height");
    }
    std::vector<int> perPose(poses.size(), 0);
    int offGrid = 0, outside = 0, noRef = 0;
    for (const Correspondence& c : points) {
        if (c.pose < 0 || c.pose >= static_cast<int>(poses.size())) {
            out.push_back("a point names an unknown still");
            continue;
        }
        ++perPose[static_cast<size_t>(c.pose)];
        if (!OnClickGrid(c.u) || !OnClickGrid(c.v)) ++offGrid;
        const CapturePose& p = poses[static_cast<size_t>(c.pose)];
        if (c.u < 0.0 || c.v < 0.0 || c.u > p.width || c.v > p.height) ++outside;
        if (c.geometryRef.empty()) ++noRef;
    }
    if (offGrid > 0) {
        out.push_back(std::to_string(offGrid) +
                      " image point(s) off the 0.5 px click grid: generated by a camera, not picked");
    }
    if (outside > 0) out.push_back(std::to_string(outside) + " image point(s) outside their still");
    if (noRef > 0) out.push_back(std::to_string(noRef) + " world point(s) with no geometry reference");
    for (size_t i = 0; i < perPose.size(); ++i) {
        if (perPose[i] < kMinPointsPerPose) {
            out.push_back("still '" + poses[i].still + "' has fewer than " +
                          std::to_string(kMinPointsPerPose) + " points");
        }
    }
    return out;
}

FitResult fitChaseRig(const std::vector<CapturePose>& poses,
                      const std::vector<Correspondence>& points, const RigParams& start,
                      const FitOptions& options) {
    FitResult res;
    res.rig = start;
    res.refusals = checkInputs(poses, points, options);
    for (const Correspondence& c : points) {
        if (c.pose < 0 || c.pose >= static_cast<int>(poses.size())) return res;  // refused above
    }

    std::vector<int> free;
    for (int i = 0; i < kNumParams; ++i) {
        if (i == 5 && !options.fitEyeScaleExponent) continue;
        free.push_back(i);
    }
    const size_t n = free.size();
    const double refEye = options.refEyeAboveFeet;

    std::vector<double> r;
    Residuals(res.rig, poses, points, refEye, r);
    double cost = SumSq(r);
    double lambda = 1e-3;
    std::vector<std::vector<double>> J(r.size(), std::vector<double>(n));
    std::vector<std::vector<double>> JtJ(n, std::vector<double>(n));

    auto jacobian = [&](const RigParams& at) {
        std::vector<double> rp, rm;
        for (size_t k = 0; k < n; ++k) {
            RigParams hi = at, lo = at;
            const double step = 1e-6 * std::max(1.0, std::fabs(Param(hi, free[k])));
            Param(hi, free[k]) += step;
            Param(lo, free[k]) -= step;
            Residuals(hi, poses, points, refEye, rp);
            Residuals(lo, poses, points, refEye, rm);
            for (size_t i = 0; i < r.size(); ++i) J[i][k] = (rp[i] - rm[i]) / (2.0 * step);
        }
        for (size_t a = 0; a < n; ++a) {
            for (size_t b = 0; b < n; ++b) {
                double s = 0.0;
                for (size_t i = 0; i < r.size(); ++i) s += J[i][a] * J[i][b];
                JtJ[a][b] = s;
            }
        }
    };

    for (res.iterations = 0; res.iterations < options.maxIterations; ++res.iterations) {
        jacobian(res.rig);
        std::vector<double> g(n, 0.0);
        for (size_t a = 0; a < n; ++a) {
            for (size_t i = 0; i < r.size(); ++i) g[a] -= J[i][a] * r[i];
        }
        bool improved = false;
        double stepNorm = 0.0;
        while (lambda < 1e12) {
            std::vector<std::vector<double>> A = JtJ;
            for (size_t a = 0; a < n; ++a) A[a][a] += lambda * std::max(JtJ[a][a], 1e-12);
            std::vector<double> delta;
            if (!Solve(A, g, delta)) {
                lambda *= 10.0;
                continue;
            }
            RigParams trial = res.rig;
            stepNorm = 0.0;
            for (size_t k = 0; k < n; ++k) {
                Param(trial, free[k]) += delta[k];
                stepNorm = std::max(stepNorm,
                                    std::fabs(delta[k]) / std::max(1.0, std::fabs(Param(trial, free[k]))));
            }
            std::vector<double> rt;
            Residuals(trial, poses, points, refEye, rt);
            const double ct = SumSq(rt);
            if (ct < cost) {
                res.rig = trial;
                r.swap(rt);
                const double rel = (cost - ct) / std::max(cost, 1e-300);
                cost = ct;
                lambda = std::max(lambda / 10.0, 1e-12);
                improved = true;
                if (rel < 1e-12 || stepNorm < 1e-10) res.converged = true;
                break;
            }
            lambda *= 10.0;
        }
        if (!improved) {
            res.converged = true;  // no step lowers the cost: a minimum
            break;
        }
        if (res.converged) break;
    }

    res.rmsPx = points.empty() ? 0.0 : std::sqrt(cost / static_cast<double>(points.size()));

    // Covariance: sigma^2 (J^T J)^-1, sigma^2 = cost / (2N - n).
    jacobian(res.rig);
    const double dof = static_cast<double>(r.size()) - static_cast<double>(n);
    if (dof > 0.0) {
        const double s2 = cost / dof;
        for (size_t k = 0; k < n; ++k) {
            std::vector<double> e(n, 0.0), col;
            e[k] = 1.0;
            if (Solve(JtJ, e, col)) res.sigma[free[k]] = std::sqrt(std::max(0.0, s2 * col[k]));
        }
    }

    if (!res.converged) res.refusals.push_back("the solver did not converge");
    if (res.rmsPx < kMinPlausibleRmsPx) {
        res.refusals.push_back("RMS residual below what hand-picked points can reach: the "
                               "correspondences were generated with a camera (circular)");
    }
    if (res.rmsPx > kMaxAcceptedRmsPx) {
        res.refusals.push_back("RMS residual above the acceptance bound: the model does not "
                               "explain the stills");
    }
    return res;
}

bool rigExplainsStills(const RigParams& rig, const std::vector<CapturePose>& poses,
                       const std::vector<Correspondence>& points, double refEye, double* rmsOut) {
    const double rms = rmsResidualPx(rig, poses, points, refEye);
    if (rmsOut) *rmsOut = rms;
    return rms <= kMaxAcceptedRmsPx;
}

bool readCorrespondenceTsv(const std::string& path, std::vector<CapturePose>& poses,
                           std::vector<Correspondence>& points, std::string& error) {
    std::ifstream in(path);
    if (!in.good()) {
        error = "cannot open " + path;
        return false;
    }
    std::map<std::string, int> ids;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string cell;
        while (std::getline(ss, cell, '\t')) f.push_back(cell);
        const std::string where = path + ":" + std::to_string(lineNo) + ": ";
        try {
            if (f[0] == "pose" && f.size() == 10) {
                if (ids.count(f[1])) {
                    error = where + "duplicate pose id " + f[1];
                    return false;
                }
                CapturePose p;
                p.still = f[2];
                p.width = std::stoi(f[3]);
                p.height = std::stoi(f[4]);
                p.playerX = std::stod(f[5]);
                p.playerY = std::stod(f[6]);
                p.feetZ = std::stod(f[7]);
                p.headingEq512 = std::stod(f[8]);
                p.eyeAboveFeet = std::stod(f[9]);
                ids[f[1]] = static_cast<int>(poses.size());
                poses.push_back(p);
            } else if (f[0] == "point" && f.size() == 8) {
                const auto it = ids.find(f[1]);
                if (it == ids.end()) {
                    error = where + "point names pose " + f[1] + " before (or without) its pose line";
                    return false;
                }
                Correspondence c;
                c.pose = it->second;
                c.worldX = std::stod(f[2]);
                c.worldY = std::stod(f[3]);
                c.worldZ = std::stod(f[4]);
                c.u = std::stod(f[5]);
                c.v = std::stod(f[6]);
                c.geometryRef = f[7];
                points.push_back(c);
            } else {
                error = where + "expected a 10-field 'pose' or 8-field 'point' row";
                return false;
            }
        } catch (const std::exception&) {
            error = where + "a numeric field does not parse";
            return false;
        }
    }
    return true;
}

}  // namespace chase_fit
