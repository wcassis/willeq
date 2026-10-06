/*
 * The Options window's Mouse page, as the camera consumes it (B433).
 *
 * The keys and defaults are eqclient.ini's `[Options]` (every shipped file:
 * InverseMouse=0, LookSpring=0, MouseActive=1, MouseWheelZoom=1,
 * MouseSensitivity=6); options_settings.cpp holds the rows. Device-free so
 * the arithmetic is tested without a client.
 *
 * ASSUMPTIONS AWAITING ORACLE (A5-A7 in options_settings.cpp):
 *   - `mouseLook` is MouseActive: off, a held button no longer turns the view;
 *   - sensitivity is the 0..20 slider step and step 6 -- the oracle's shipped
 *     value -- is fable's pre-B433 feel, so the scale is (step + 1) / 7
 *     (1/7x .. 3x);
 *   - look spring levels the view (pitch -> 0) while the player walks with no
 *     mouse-look held, at kLookSpringDegPerSec.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace EQT {
namespace Graphics {

struct MouseOptions {
    bool invertY = false;     // InverseMouse
    bool lookSpring = false;  // LookSpring
    bool mouseLook = true;    // MouseActive
    bool wheelZoom = true;    // MouseWheelZoom
    int sensitivity = 6;      // MouseSensitivity, 0..20
};

constexpr float kLookSpringDegPerSec = 90.0f;

/** The mouse-look scale for a sensitivity step; step 6 == 1.0. */
inline float mouseLookScale(int step) {
    step = std::max(0, std::min(20, step));
    return static_cast<float>(step + 1) / 7.0f;
}

/** One frame of look spring: pitch moves toward 0, never past it. */
inline float applyLookSpring(float pitchDeg, float dt) {
    const float stepDeg = kLookSpringDegPerSec * std::max(0.0f, dt);
    if (std::fabs(pitchDeg) <= stepDeg) return 0.0f;
    return pitchDeg > 0.0f ? pitchDeg - stepDeg : pitchDeg + stepDeg;
}

}  // namespace Graphics
}  // namespace EQT
