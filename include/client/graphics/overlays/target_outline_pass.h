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
 * The outline is now an INVERTED HULL in two draws, neither touching the
 * stencil: (1) a DEPTH-ONLY pre-pass of the unscaled mesh (colour writes off,
 * depth write on, LESSEQUAL) -- the target's own depth, present even when the
 * scene drew it as an impostor billboard or not at all (beyond
 * impostorTierDistance the target is a billboard, and without this pass the
 * hull painted solid: review/B1064/x86/README.md); (2) the scaled mesh with
 * its front faces culled, depth LESSEQUAL, depth write off. Inside the
 * silhouette the hull's back faces lie behind the pre-pass depth and fail the
 * test; outside it they are the rim. plan() never asks for a stencil,
 * whatever the surface has.
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
	bool depthPrepass;   // true: unscaled mesh, colour off, depth write on
	Cull cull;           // Front: keep the hull's back faces = the rim
	float scale;         // kScale
	bool depthWrite;     // false: the hull itself writes no depth
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
