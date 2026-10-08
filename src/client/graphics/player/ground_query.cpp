// ground_query.cpp -- T437 (board B435). The two ground decisions, device-free.
//
// These four lines used to be four copies of an inline comparison inside
// PlayerCameraComponent::resolveIrrlichtClearPath() /
// resolveIrrlichtBlockedPath() / resolveHCMapCollision(), bodies that cannot
// be driven in a unit test (they need a live Irrlicht device, an event
// receiver, a loaded zone with collision geometry, an entity renderer and a
// bridge). That is why B435 shipped: there was no seam to test.

#include "client/graphics/player/ground_query.h"

namespace EQT {
namespace Graphics {

bool ShouldLandJump(const GroundZ& ground, float feetZ, float verticalVelocity) {
	// A ceiling, an empty column, or no collision data is not something to
	// land on. This is the guard whose absence was B435.
	if (!ground.hasFloor()) return false;
	if (verticalVelocity > 0.0f) return false;
	return feetZ <= ground.z();
}

bool ShouldClampSwimToFloor(const GroundZ& ground, float feetZ) {
	if (!ground.hasFloor()) return false;
	return feetZ < ground.z();
}

// B892 (GitLab #208). See the header for the full contract -- in one line:
// at or below the hover line the feet rest on it exactly; above it they
// descend one frame's worth of the rate, never undershooting it; a floor
// above the feet owns the z and is returned untouched.
float ClampLevitationFeet(float groundZ, float feetZ, float hoverLine,
                          float descentRate, float deltaTime) {
	// The floor moving up under the player (a step-up) is the collision
	// path's to resolve; the hover must not fight it.
	if (feetZ < groundZ) return feetZ;
	if (feetZ <= hoverLine) return hoverLine;
	const float descended = feetZ - descentRate * deltaTime;
	return descended < hoverLine ? hoverLine : descended;
}

// T440 (B438). A ray that hit NOTHING is not a floor at your feet.
GroundZ ClassifyBspGroundRay(bool hit, float hitZ, float feetZ, float maxStepUp) {
	// The miss. Pre-T440 this fell through to `float groundZ = feetZ;` and the
	// function returned Floor(feetZ) -- so a player falling down a hole was
	// told the floor was exactly where his feet already were, ShouldLandJump()
	// agreed, and the fall ended in mid-air. Nothing under the column means
	// there is nothing to land on.
	if (!hit) return GroundZ::NoFloor();
	// The ceiling / overhang / platform underside (T437, B435): geometry above
	// the step-up limit is not something to stand on either.
	if (hitZ > feetZ + maxStepUp + 0.1f) return GroundZ::NoFloor();
	return GroundZ::Floor(hitZ);
}

}  // namespace Graphics
}  // namespace EQT
