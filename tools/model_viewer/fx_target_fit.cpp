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
// (the FABLE human renders 285 px there at the client's actor scale 1.2,
// refactor/B1172/README.md sec. 2; 238 px at fable's old 1.0, B1166).
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
    float x = -2.0f, z = 2.0f;
    std::array<float, 3> m{1e9f, -1e9f, 0.0f};
    for (int iter = 0; iter < 24; ++iter) {
        m = measure(x, z);
        if (m[0] < 1e8f) {
            const float r0 = m[0] - targetFeetXS, r1 = m[1] - targetFeetYS;
            std::printf("[fx-harness]   tgt fit %d: (x %.3f, z %.3f) -> feet row %.1f at column %.1f, %.0f px (surface)\n",
                        iter, x, z, m[1], m[0], m[2]);
            if (std::fabs(r0) < 0.5f * Sx && std::fabs(r1) < 0.5f * Sy) break;
            const float d = 0.05f;
            const auto mx = measure(x + d, z);
            const auto mz = measure(x, z + d);
            if (mx[0] < 1e8f && mz[0] < 1e8f) {
                const float j00 = (mx[0] - m[0]) / d, j01 = (mz[0] - m[0]) / d;
                const float j10 = (mx[1] - m[1]) / d, j11 = (mz[1] - m[1]) / d;
                const float det = j00 * j11 - j01 * j10;
                if (std::fabs(det) > 1e-6f) {
                    const float dx = (j11 * (-r0) - j01 * (-r1)) / det;
                    const float dz = (-j10 * (-r0) + j00 * (-r1)) / det;
                    const float step = std::min(1.0f, 6.0f / std::max(std::fabs(dx), std::fabs(dz)));
                    x += dx * step;
                    z += dz * step;
                    continue;
                }
            }
            // singular Jacobian: nudge toward the target pixels
            x -= 0.2f * std::copysign(1.0f, r0);  // column right = world -X
            z += 0.2f * std::copysign(1.0f, r1);  // feet row down = closer
        }
    }
    if (animatedNode) animatedNode->setVisible(casterWasVisible);
    const bool ok = m[0] < 1e8f && std::fabs(m[0] - targetFeetXS) < 1.0f * Sx &&
                    std::fabs(m[1] - targetFeetYS) < 1.0f * Sy;
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
                "solved standing height %.0f px: d %+.0f (tol %.0f) | measured standing height %.0f px: d %+.0f "
                "(tol %.0f; ratio %.3f of the oracle's %.0f px): %s\n",
                win.x0, win.x1, sil.top, sil.bottom, sil.height(), sil.bottom, sil.col, a.feetY, a.feetX,
                chk.dFeet, fxharness::kFeetTolPx, chk.dCol, fxharness::kColumnTolPx, b1130TargetFitPx, chk.dPx,
                fxharness::kHeightTolPx, fxharness::kStandingPx, chk.dStand, fxharness::kStandingTolPx, chk.ratio,
                a.px, chk.ok() ? "PASS" : "FAIL");
    std::fflush(stdout);
    return chk.ok();
}
