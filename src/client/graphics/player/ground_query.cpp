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

}  // namespace Graphics
}  // namespace EQT
