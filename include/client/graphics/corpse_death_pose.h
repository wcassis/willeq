#pragma once

// T456 (board B477) -- ONE copy of the corpse death-pose ladder.
//
// There were two, textually duplicated: the synchronous build
// (EntityRenderer::buildEntityMesh, src/client/graphics/entity_mesh_build.cpp)
// and the async/staged build (EntityRenderer::processOneEntityBuildStep's
// MeshFinalize phase, src/client/graphics/entity_build_step.cpp). T454 pinned
// the synchronous one with a real EDT_NULL device and a real Human male mesh
// and could not reach the async one at all, so a pose rule with two
// implementations had one test. They are now the same function, and
// tests/test_corpse_death_pose.cpp drives BOTH production paths into it.
//
// The pose itself is the oracle's, not an invention: the official client
// answers its own death with OP_SpawnAppearance AT_ANIMATION param 115 =
// ANIM_LYING (T346, four byte-identical self-death windows), and T431
// photographed the body flat. fable renders "lying" as the LAST frame of the
// death animation d05, HELD -- playAnimation(anim, loop=false,
// playThrough=false) then SkeletalAnimator::setToLastFrame(). d02 (heavy
// damage) and d01 (minor damage) are fallbacks for models that have no d05.

namespace EQT {
namespace Graphics {

struct EntityVisual;
class EQAnimatedMeshSceneNode;

// Puts `visual`'s animated node in the death pose and records it on the
// visual (currentAnimation, corpsePositionAdjusted, corpseYOffset).
//
// witnessed=false (a corpse found lying there: the zone load, a spawn that
// arrives dead): the node is HELD on the animation's last frame at once.
// witnessed=true (T486, item 3 -- EntityRenderer::markEntityAsCorpse(), the
// EntityDeathAnimation arm for a death the player saw): the same ladder, but
// the fall is PLAYED from its first frame, non-looping and play-through, and
// comes to rest on the same last frame. Before T486 that arm carried a third
// textual copy of the ladder; tests/test_corpse_death_pose.cpp
// TheDeathPoseLadderHasOneCopy keeps it here.
//
// Returns true if a pose was applied; false when `animNode` is null or the
// model has none of d05/d02/d01, in which case `visual` is left untouched.
// The caller decides whether the entity is a corpse -- ask
// IsCorpseByFields() (client/corpse_predicate.h), never a name substring.
bool ApplyCorpseDeathPose(EntityVisual& visual, EQAnimatedMeshSceneNode* animNode,
                          bool witnessed = false);

}  // namespace Graphics
}  // namespace EQT
