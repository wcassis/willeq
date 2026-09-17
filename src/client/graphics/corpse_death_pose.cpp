#include "client/graphics/corpse_death_pose.h"

#include "client/graphics/entity_renderer.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"

#include <string>

namespace EQT {
namespace Graphics {

bool ApplyCorpseDeathPose(EntityVisual& visual, EQAnimatedMeshSceneNode* animNode,
                          bool witnessed) {
	if (!animNode) {
		return false;
	}

	// d05 is the death animation. d02 (heavy damage) and d01 (minor damage)
	// are NOT death animations -- they are the fallbacks for a model that has
	// no d05, and the order is deliberate: heavy damage before minor.
	std::string deathAnim;
	if (animNode->hasAnimation("d05")) {
		deathAnim = "d05";
	} else if (animNode->hasAnimation("d02")) {
		deathAnim = "d02";
	} else if (animNode->hasAnimation("d01")) {
		deathAnim = "d01";
	}
	if (deathAnim.empty()) {
		return false;
	}

	if (witnessed) {
		// T486: the player saw this one die. Stop whatever was playing so
		// the fall starts clean, then play it ONCE, play-through (nothing may
		// pre-empt it), and let it come to rest on its last frame -- the same
		// frame the held branch below jumps to. This is what
		// EntityRenderer::markEntityAsCorpse() did with its own copy of the
		// ladder before T486.
		animNode->stopAnimation();
		animNode->playAnimation(deathAnim, false, true);
	} else {
		// Not looping and not playThrough: the body must come to rest, not
		// fall over again every few seconds.
		animNode->playAnimation(deathAnim, false, false);

		// Jump to the last frame -- the "lying down" pose. Frame 0 of d05 is
		// the STANDING pose, which is board B473's symptom.
		SkeletalAnimator& animator = animNode->getAnimator();
		animator.setToLastFrame();

		// Force the mesh update now so the corpse appears lying down on the
		// very first frame it is drawn, rather than standing until the next
		// tick.
		animNode->forceAnimationUpdate();
	}

	// No Z offset needed for the death pose.
	visual.corpsePositionAdjusted = true;
	visual.corpseYOffset = 0.0f;
	visual.currentAnimation = deathAnim;
	return true;
}

}  // namespace Graphics
}  // namespace EQT
