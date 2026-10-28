// tools/model_viewer/fx_target_fit.cpp -- B1172 (#491) FIX leg: the b1130
// fixed_npc target fit, moved verbatim from model_viewer_fx_harness.cpp
// (a pure move, no behaviour change) after the fable merge put the harness
// over the 1,500-line ceiling. PresetGeometry and the declarations live in
// fx_target_fit.h; the solver, its solved-placement globals and the frame-0
// self-check are below, unchanged. The harness still owns the scene this
// code measures against (model_viewer_state.h) and the options
// (model_viewer_fx_harness.h).

#include "fx_target_fit.h"

#include "fx_target_anchor.h"
#include "model_viewer_state.h"
#include "model_viewer_fx_harness.h"

#include <array>
#include <cmath>
#include <cstdio>

// B1155 b1130: the solved target placement (world units), set by
// fitFixedNpcTarget and used by placeHarnessTarget.
float b1130TargetX = 0.0f, b1130TargetY = 0.0f, b1130TargetZ = 0.0f;
// B1166: the standing target's pixel height the solve rendered (requested px).
float b1130TargetFitPx = 0.0f;

// B1155: b1130's fixed_npc target. review/B1130/README.md "Geometry": the
// B1118 opening pose with the caster #goto -67.6,434.4 so the #spawn TrollF
// stands ~1.4 caster heights to her right. The b1130 camera is already fitted
// to the caster (209 px, feet (640,598)); this solves the target's world
// offset (x, z) from its two feet pixels. B1166 (#485): the feet are the
// oracle target's MEASURED box (fx_target_anchor.h: feet (940,615), 288 px,
// by difference against a target-free still; B1130's 325 px / (940,652)
// began at the name label), the solve sees the STANDING frame, and the
// rendered height is the model's own -- a self-check, never a fitted input
// (the FABLE human renders 294 px there at the client's actor scale 1.2 and
// the preset's back-turned pose, the B1167/B1172 merge, fx_target_anchor.h
// kStandingPx; 285 px face-on, refactor/B1172/README.md sec. 2; 238 px at
// fable's old 1.0, B1166).
//
// Screen right for a camera looking along -kCasterFacing is world -X
// (targetRightOfCaster), so the target at column 940 sits at x < 0, and its
// lower feet row (615 > 598) puts it CLOSER to the camera than the caster.
bool fitFixedNpcTarget(const PresetGeometry& g) {
    if (!targetAnimatedNode || !camera) return false;
    const video::SColor clear(255, 50, 50, 80);
    const core::vector3df look = camera->getTarget();
    // Hide the caster while fitting (its silhouette would be a second
    // off-clear blob), and take a throwaway screenshot: the EGL surface can be
    // another size than the requested frame (see fitFaceOnCamera), and this
    // measures against the SCALED facts, surface pixel (x*Sx, y*Sy).
    const bool casterWasVisible = animatedNode && animatedNode->isVisible();
    if (animatedNode) animatedNode->setVisible(false);
    targetAnimatedNode->setVisible(true);
    targetAnimatedNode->setPosition(core::vector3df(0.0f, 0.0f, 0.0f));
    targetAnimatedNode->updateAbsolutePosition();
    // B1166 (#485): solve on the STANDING frame, not the rest pose. Without
    // this the fit saw the bind pose (a ~50 px strip whose bottom is not the
    // feet) and the running frames -- where the harness loop's OnAnimate
    // skins the idle -- put the standing figure's feet 130 px lower. The same
    // rule fitFaceOnCamera applies to the caster (the client's createEntity:
    // force the animated pose before measuring it); the scene clock is
    // already frozen at the run's t = 0 (setupSpellTestCamera), so this is
    // the pose frame 0 is drawn in.
    targetAnimatedNode->forceAnimationUpdate();
    // B1166: stand the target ON the ground the frames draw. addGroundPlane
    // puts the depth-writing plane at the caster's pose-box bottom (the
    // client's collisionZOffset rule: an entity's pose box stands on the
    // ground); the target gets the same rule -- its own pose-box bottom on
    // that plane -- or the plane hides its feet in every written frame while
    // the fit (drawn before the plane exists) still counts them.
    float y = 0.0f;
    if (animatedNode) {
        animatedNode->forceAnimationUpdate();
        animatedNode->updateAbsolutePosition();
        core::aabbox3df cb = animatedNode->getBoundingBox();
        animatedNode->getAbsoluteTransformation().transformBoxEx(cb);
        core::aabbox3df tb = targetAnimatedNode->getBoundingBox();
        targetAnimatedNode->getAbsoluteTransformation().transformBoxEx(tb);
        y = cb.MinEdge.Y - tb.MinEdge.Y;
        std::printf("[fx-harness] b1130 target: pose box y %.3f..%.3f at y 0; caster pose box bottom (the ground "
                    "plane) %.3f -> target stands at y %.3f\n",
                    tb.MinEdge.Y, tb.MaxEdge.Y, cb.MinEdge.Y, y);
    }
    driver->beginScene(true, true, clear);
    smgr->drawAll();
    driver->endScene();
    float Sx = 1.0f, Sy = 1.0f;
    if (video::IImage* probe = driver->createScreenShot()) {
        const auto dim0 = probe->getDimension();
        Sx = static_cast<float>(dim0.Width) / static_cast<float>(harnessOpts.width);
        Sy = static_cast<float>(dim0.Height) / static_cast<float>(harnessOpts.height);
        probe->drop();
    }
    const float targetFeetXS = g.targetFeetX * Sx, targetFeetYS = g.targetFeetY * Sy;
    // The target's silhouette alone (any pixel off the clear colour), in
    // SURFACE px: {column centroid, bottom row, pixel height}.
    auto measure = [&](float x, float z) -> std::array<float, 3> {
        targetAnimatedNode->setPosition(core::vector3df(x, y, z));
        targetAnimatedNode->updateAbsolutePosition();
        driver->beginScene(true, true, clear);
        smgr->drawAll();
        driver->endScene();
        video::IImage* img = driver->createScreenShot();
        if (!img) return {1e9f, -1e9f, 0.0f};
        int top = -1, bottom = -1;
        double sx = 0;
        int nx = 0;
        const auto dim = img->getDimension();
        for (u32 y = 0; y < dim.Height; ++y)
            for (u32 x = 0; x < dim.Width; ++x) {
                const video::SColor c = img->getPixel(x, y);
                if (std::abs(int(c.getRed()) - 50) + std::abs(int(c.getGreen()) - 50) +
                        std::abs(int(c.getBlue()) - 80) <= 12)
                    continue;
                if (top < 0) top = static_cast<int>(y);
                bottom = static_cast<int>(y);
                sx += x;
                ++nx;
            }
        img->drop();
        if (nx == 0) return {1e9f, -1e9f, 0.0f};
        return {static_cast<float>(sx / nx), static_cast<float>(bottom),
                static_cast<float>(bottom - top + 1)};
    };

    // Newton on the two feet-pixel residuals (finite-difference Jacobian),
    // damped. Starts on the caster's right and closer to the camera.
    //
    // B1177 (#496): the measured rows are whole pixels, so a 0.05-unit
    // difference can move neither (a zero Jacobian row): the step retries the
    // difference at 4x and 16x before it falls back to a nudge. The nudge's
    // signs were inverted (it moved the target AWAY from the anchor pixels:
    // review/B1172/worker/wrong_model/r1_6_1_6.log fits 7 -> 8, column 940.2
    // -> 948.8 and feet row 616 -> 618 for an anchor at 940 / 615) and moved
    // both axes even when one was already in; that is the 616/616/618 cycle
    // (refactor/B1177/README.md sec. 2). The solve stops on a detected cycle
    // (fxharness::fitCyclePeriod) or the iteration cap, and prints every
    // candidate of the cycle (or the closest one) with why it misses.
    const fxharness::TargetAnchor anchorReq{g.targetFeetX, g.targetFeetY, g.targetPx};
    std::vector<fxharness::FitSample> samples;
    float x = -2.0f, z = 2.0f;
    float mX = x, mZ = z;  // where `m` was measured (x, z move on after it)
    std::array<float, 3> m{1e9f, -1e9f, 0.0f};
    int cycle = 0;
    for (int iter = 0; iter < harnessOpts.targetFitIterations; ++iter) {
        m = measure(x, z);
        mX = x;
        mZ = z;
        if (m[0] >= 1e8f) {
            std::printf("[fx-harness]   tgt fit %d: (x %.3f, z %.3f) -> no target pixels\n", iter, x, z);
            break;
        }
        const float r0 = m[0] - targetFeetXS, r1 = m[1] - targetFeetYS;
        std::printf("[fx-harness]   tgt fit %d: (x %.3f, z %.3f) -> feet row %.1f at column %.1f, %.0f px (surface)\n",
                    iter, x, z, m[1], m[0], m[2]);
        if (std::fabs(r0) < 0.5f * Sx && std::fabs(r1) < 0.5f * Sy) break;
        samples.push_back({x, z, m[0] / Sx, m[1] / Sy, m[2] / Sy});
        cycle = fxharness::fitCyclePeriod(samples);
        if (cycle) break;
        bool stepped = false;
        // B1167 (#486) continuation: 0.05 units moves the silhouette < 1 px at
        // ~30 units range (Jacobian 0/noise); 0.25 moves it 4-9 px.
        for (float d = 0.05f; d <= 0.8f && !stepped; d *= 4.0f) {
            const auto mx = measure(x + d, z);
            const auto mz = measure(x, z + d);
            if (mx[0] >= 1e8f || mz[0] >= 1e8f) break;
            const float j00 = (mx[0] - m[0]) / d, j01 = (mz[0] - m[0]) / d;
            const float j10 = (mx[1] - m[1]) / d, j11 = (mz[1] - m[1]) / d;
            const float det = j00 * j11 - j01 * j10;
            if (std::fabs(det) <= 1e-6f) {
                std::printf("[fx-harness]     singular Jacobian at d %.2f (d col/dx %.2f, d col/dz %.2f, "
                            "d row/dx %.2f, d row/dz %.2f px/unit)\n",
                            d, j00, j01, j10, j11);
                continue;
            }
            const float dx = (j11 * (-r0) - j01 * (-r1)) / det;
            const float dz = (-j10 * (-r0) + j00 * (-r1)) / det;
            const float step = std::min(1.0f, 6.0f / std::max(std::fabs(dx), std::fabs(dz)));
            x += dx * step;
            z += dz * step;
            stepped = true;
        }
        if (stepped) continue;
        // Still singular: nudge only the axis that is out, TOWARD its pixel.
        // Screen right is world -X (targetRightOfCaster), so a column right of
        // the anchor (r0 > 0) needs x up; a feet row below it (r1 > 0) means
        // the target stands too close, so z goes down (+z = closer = lower).
        if (std::fabs(r0) >= 0.5f * Sx) x += 0.05f * std::copysign(1.0f, r0);
        if (std::fabs(r1) >= 0.5f * Sy) z -= 0.05f * std::copysign(1.0f, r1);
        std::printf("[fx-harness]     still singular: nudge to (x %.3f, z %.3f)\n", x, z);
    }
    if (animatedNode) animatedNode->setVisible(casterWasVisible);
    const bool ok = m[0] < 1e8f && std::fabs(m[0] - targetFeetXS) < fxharness::kSolveAcceptPx * Sx &&
                    std::fabs(m[1] - targetFeetYS) < fxharness::kSolveAcceptPx * Sy;
    // B1177: the diagnostic -- the cycle's candidates, or the closest one.
    if (!ok)
        for (const auto& l : fxharness::fitDiagnostic(samples, cycle, anchorReq, harnessOpts.targetFitIterations))
            std::printf("[fx-harness] %s\n", l.c_str());
    // The placement is where `m` was MEASURED (before B1177 the line and the
    // stored placement used x, z after the last step, which no frame drew).
    x = mX;
    z = mZ;
    std::printf("[fx-harness] camera b1130: target (x %.3f, y %.3f, z %.3f) -> rendered target feet row %.0f at "
                "column %.0f, %.0f px head top to feet (oracle r51 target, fx_target_anchor.h: feet %d at %d, "
                "%d px)\n",
                x, y, z, m[1] / Sy, m[0] / Sx, m[2] / Sy, static_cast<int>(g.targetFeetY),
                static_cast<int>(g.targetFeetX), static_cast<int>(g.targetPx));
    std::fflush(stdout);
    if (ok) {
        b1130TargetX = x;
        b1130TargetY = y;
        b1130TargetZ = z;
        b1130TargetFitPx = m[2] / Sy;
    }
    return ok;
}

// B1166 (#485): measure the rendered target in a written frame (off-clear
// silhouette in the anchor's window, fx_target_anchor.h) and check feet row,
// column centroid and height against the preset's anchor within the stated
// tolerances. Printed to the run log either way; false = out of tolerance.
bool b1130TargetSelfCheck(video::IImage* img) {
    if (!img) return false;
    const auto dim = img->getDimension();
    const auto& a = fxharness::kB1130TargetAnchor;
    const auto win = fxharness::targetWindow(a, static_cast<int>(dim.Height));
    const auto sil = fxharness::measureSilhouette(
        [img](int x, int y, int& r, int& g, int& b) {
            const video::SColor c = img->getPixel(static_cast<u32>(x), static_cast<u32>(y));
            r = static_cast<int>(c.getRed());
            g = static_cast<int>(c.getGreen());
            b = static_cast<int>(c.getBlue());
        },
        static_cast<int>(dim.Width), static_cast<int>(dim.Height), win.x0, win.y0, win.x1, win.y1);
    const auto chk = fxharness::checkTargetAnchor(sil, a, b1130TargetFitPx);
    std::printf("[fx-harness] b1130 target self-check (frame 0, cols %d..%d): rows %d..%d (%d px, feet row %d), "
                "column %.1f | anchor feet %.0f at %.0f: d feet %+.0f (tol %.0f), d col %+.1f (tol %.0f) | "
                "solved standing height %.0f px: d %+.0f (tol %.0f) | measured standing height %.0f px vs ratchet "
                "%.0f: d %+.0f (tol %.0f; ratio %.3f of the oracle's %.0f px): %s\n",
                win.x0, win.x1, sil.top, sil.bottom, sil.height(), sil.bottom, sil.col, a.feetY, a.feetX,
                chk.dFeet, fxharness::kFeetTolPx, chk.dCol, fxharness::kColumnTolPx, b1130TargetFitPx, chk.dPx,
                fxharness::kHeightTolPx, static_cast<double>(sil.height()), fxharness::kStandingPx, chk.dStand,
                fxharness::kStandingTolPx, chk.ratio,
                a.px, chk.ok() ? "PASS" : "FAIL");
    std::fflush(stdout);
    return chk.ok();
}
