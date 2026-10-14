/*
 * The target outline's GL state decisions (B1064, #384).
 *
 * The outline around the current target used to be a two-pass STENCIL rim:
 * pass 1 wrote ref 128 over the model, pass 2 drew the model scaled 1.10
 * about its origin where stencil != 128. On a surface with no stencil planes
 * (EGL_STENCIL_SIZE 0 -- the orangepi preset since B971, decisions.md
 * 2026-10-05) the stencil test always passes, so pass 2 painted the WHOLE
 * inflated model as one flat-colour silhouette (Will's XU4 screenshot,
 * review/B1064/xu4_dionna_qeynos_2026-10-14.png).
 *
 * The outline is now an INVERTED HULL: one draw of the scaled mesh with its
 * front faces culled, depth test LESSEQUAL, depth write off. Inside the
 * model's silhouette the hull's back faces lie behind the model's own depth
 * and fail the test; outside it they are the rim. No stencil, on any
 * surface: plan() never asks for one, whatever the surface has.
 *
 * Device-free: plain values the overlay applies with raw GL
 * (OverlaysComponent::drawTargetOutline()). Irrlicht appears only as the
 * matrix value type.
 */

#pragma once

#include <irrlicht.h>

namespace EQT {
namespace Graphics {
namespace TargetOutlinePass {

// Hull scale about the entity origin -- the old stencil rim's factor.
constexpr float kScale = 1.10f;

enum class Cull { None, Front, Back };

struct Plan {
	bool usesStencil;    // never true (B1064)
	Cull cull;           // Front: keep the hull's back faces = the rim
	float scale;         // kScale
	bool depthWrite;     // false: the outline never contaminates depth
	bool depthLessEqual; // true: closer geometry (a pet) still occludes it
};

// The pass for a surface with `stencilBits` stencil planes. The answer does
// not depend on them: the outline is one path on every surface.
Plan plan(int stencilBits);

// The world transform of the hull: `normalTransform` (the mirror of
// EQAnimatedMeshSceneNode::render()'s world transform) followed by a uniform
// `scale` about that transform's translation (the entity origin).
irr::core::matrix4 outlineTransform(const irr::core::matrix4& normalTransform, float scale);

} // namespace TargetOutlinePass
} // namespace Graphics
} // namespace EQT
