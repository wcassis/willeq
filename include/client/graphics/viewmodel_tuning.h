#pragma once

/**
 * T296: ViewmodelTuning alone, split out of viewmodel_transform.h so
 * irrlicht_renderer.h can hold one by value without pulling in
 * entity_renderer.h (which includes irrlicht_renderer.h back).
 * viewmodel_transform.h includes this file; nothing else should need to.
 */

namespace EQT {
namespace Graphics {

/// Stage-1 framing knobs. Deliberately few: pose/offset/FOV polish is
/// stage 2 and must not be pre-empted here.
struct ViewmodelTuning {
    /// Fraction of the frame the arm bounding box is fitted to.
    float fillFraction = 0.85f;
    /// Vertical placement of the box centre, as a fraction of the vertical
    /// SLACK left over after the fit (clamped to [-1, 1]). -1 puts the arms
    /// as low in the frame as they can go while staying whole; 0 centres
    /// them. Expressed against the slack rather than the half-height so no
    /// value can push the arms off the bottom edge.
    float verticalBias = -0.8f;
    /// The pass's OWN near plane. Safe to make small: it applies to this
    /// pass only, never to the world render (Will ruled out lowering the
    /// global near plane).
    float nearPlane = 0.05f;
    float farPlane = 100.0f;
    /// Minimum gap kept between the near plane and the nearest arm vertex.
    float minClearance = 0.25f;
};

}  // namespace Graphics
}  // namespace EQT
