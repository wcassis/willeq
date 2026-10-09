#ifndef EQT_GRAPHICS_PLAYER_GROUND_QUERY_H
#define EQT_GRAPHICS_PLAYER_GROUND_QUERY_H

// T437 (board B435 / B436) -- the ground query's single honest answer shape,
// and the two decisions that read it.
//
// Before this unit, three different magic FLOATS travelled the "what is the
// floor under this point?" path, and each one was mistaken for a real floor by
// at least one caller:
//
//   * `feetZ + 1000.0f` -- findGroundZBspFiltered()'s "that hit was a CEILING,
//     not a floor" sentinel. Four jump-landing comparisons tested
//     `feetZ <= groundZ` with no upper bound, so a jump under low geometry
//     evaluated true, set `newZ = groundZ + modelYOffset` and ended the jump
//     1,000 units in the air. The two swim clamps beside them were guarded by
//     a `ctx_.playerZ + 100.0f` heuristic; the landings were not. (B435)
//   * `BEST_Z_INVALID` (-99999) -- HCMap's "nothing found", which findGroundZ()
//     quietly rewrote to the caller's own centre z. Every
//     `!= BEST_Z_INVALID` guard downstream was therefore DEAD, and a column
//     with no floor reported a phantom floor at the player's own centre --
//     which the landing then snapped to `centreZ + modelYOffset`, drifting the
//     player upward by one model offset per frame.
//   * a ceiling z with no marker at all -- HCMap::FindBestZ()'s upward re-cast,
//     deleted in this unit because not one of its five callers could do
//     anything with a ceiling. (B436)
//
// A GroundZ cannot be compared against a float by accident: there is no
// conversion, so hasFloor() has to be asked first and the compiler rejects
// every site that forgets. That is the point of the type -- it is not another
// magic number with a nicer name.

namespace EQT {
namespace Graphics {

class GroundZ {
public:
	// There IS a floor here, at world z `z`.
	static GroundZ Floor(float z) { return GroundZ(true, z); }
	// There is NO floor here: the ray met a ceiling, an empty column, or there
	// was no collision data to ask. Nothing may land on it and nothing may
	// clamp to it.
	static GroundZ NoFloor() { return GroundZ(false, 0.0f); }

	bool hasFloor() const { return hasFloor_; }
	// Only meaningful when hasFloor(); ask first.
	float z() const { return z_; }
	// The floor if there is one, else `fallback` -- for the callers whose
	// answer to "no floor here" is "stay exactly where you are".
	float zOr(float fallback) const { return hasFloor_ ? z_ : fallback; }

private:
	GroundZ(bool hasFloor, float z) : hasFloor_(hasFloor), z_(z) {}
	bool hasFloor_;
	float z_;
};

// The jump-landing test, lifted out of PlayerCameraComponent so it can be
// exercised without an Irrlicht device (T435's RayTestDoorCollision is the
// model: the comparison used to live inline in a body that needs a device, a
// loaded zone and an entity renderer, which is why the B435 defect had no
// regression net at all).
//
// True when the jump ends at `ground`. The caller then sets its centre z to
// `ground.z() + modelYOffset`.
bool ShouldLandJump(const GroundZ& ground, float feetZ, float verticalVelocity);

// The swim floor clamp: true when the swimmer's feet would pass through the
// floor. Note the STRICT `<` -- a swimmer resting exactly on the bottom is not
// re-clamped every frame, which is the pre-T437 comparison, preserved.
bool ShouldClampSwimToFloor(const GroundZ& ground, float feetZ);

// B892 (GitLab #208) -- the levitation hover clamp, the pure part.
//
// A levitating player's feet rest at the hover LINE (the floor plus the
// hover height, computed by the CALLER -- see applyLevitationClamp) instead
// of on the floor, and from height they DESCEND to it (the classic client's
// slow glide when walking off a ledge) rather than free-falling. The
// swim-clamp shape (ShouldClampSwimToFloor) with a hover line as the rest
// position:
//
//   * at or below the line (`feetZ <= hoverLine`) the feet go TO the line --
//     exactly, with no epsilon, so a player on flat ground rests on the line
//     every frame without jitter, and the frame after the buff lands raises
//     the player to the line in one step;
//   * above the line the feet descend at `descentRate` units/second, one
//     frame's worth, and never undershoot the line (a large frame or a high
//     fall clamps to the line instead of tunnelling through it);
//   * a floor ABOVE the feet (`feetZ < groundZ`) owns the z: the step-up the
//     collision path is about to resolve is returned untouched -- the clamp
//     must not fight the floor moving up under the player.
//
// The caller must not call this for a `GroundZ::NoFloor()` answer (a
// ceiling, an empty column, or no collision data): over a hole there is no
// floor to hover above and the feet keep falling exactly as a normal fall
// does (the T440 lesson: a miss is not a floor) -- that skip lives in
// applyLevitationClamp().
//
// The hover height and the descent rate the caller feeds in are the [assumed]
// classic-client numbers (EQ::kLevitationHoverHeight / EQ::
// kLevitationDescentRate, client/spell/levitation.h) -- measured, not
// assumed, once the lane-1 oracle capture lands.
float ClampLevitationFeet(float groundZ, float feetZ, float hoverLine,
                          float descentRate, float deltaTime);

// T440 (board B438) -- the BSP downward raycast's verdict, lifted out of
// PlayerCameraComponent::findGroundZBspFiltered() for the same reason as the
// two decisions above: it lives in a body that needs a live Irrlicht device, a
// loaded zone with region triangles and a BSP tree, so it had no regression
// net either.
//
// Before T440 the MISS case ("the ray met nothing at all") answered
// `Floor(feetZ)` -- the floor is exactly under your feet, wherever you happen
// to be. ShouldLandJump() then saw `feetZ <= feetZ`, evaluated TRUE, and ended
// a fall in mid-air over a hole. A miss is not a floor; it is the absence of
// one, and the falling player must keep falling.
//
//   hit == false                      -> NoFloor  (T440; was Floor(feetZ))
//   hit, hitZ above the step-up limit -> NoFloor  (T437's ceiling case)
//   hit, hitZ within reach            -> Floor(hitZ)
//
// `maxStepUp` is the collision step height; the 0.1f slack is the pre-existing
// tolerance, kept verbatim so this is the same predicate the two phases of the
// BSP query already spelled inline.
GroundZ ClassifyBspGroundRay(bool hit, float hitZ, float feetZ, float maxStepUp);

}  // namespace Graphics
}  // namespace EQT

#endif  // EQT_GRAPHICS_PLAYER_GROUND_QUERY_H
