/*
 * T309 -- the measured first-person draw rule.
 *
 * In first person the official Titanium client draws the player's own model --
 * the WHOLE model, with its equipped weapon -- for the duration of an ATTACK
 * animation, and at no other time (T307;
 * refactor/T307_official_draw_rule.md, review/T307/README.md). The rule
 * itself, its discriminators, and the reason it must hide by WITHHOLDING the
 * node from the render list rather than by setVisible(), are documented on
 * include/client/graphics/first_person_body_draw.h. This TU is only the
 * per-frame driver.
 *
 * Split out of entity_renderer.cpp, which is over the file-size ceiling.
 */
#include "client/graphics/entity_renderer.h"
#include "client/graphics/first_person_body_draw.h"
#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/eq/skeletal_animator.h"
#include "common/logging.h"

namespace EQT {
namespace Graphics {

// T309: THE MEASURED FIRST-PERSON DRAW RULE, driven once per frame.
//
// The signal is the local player's own animator: fable already carries
// exactly what the rule needs and no parallel state machine was invented.
// EQAnimatedMeshSceneNode::getCurrentAnimation() is the animation code the
// per-instance SkeletalAnimator is running, and getAnimator().getState() is
// whether it is actually PLAYING. Combat animations arrive here through
// setPlayerEntityAnimation()/setEntityAnimation() (eq.cpp's combat-skill
// dispatch, entity_combat_anim.cpp's buffer, the spell and skill managers) as
// NON-LOOPING play-through animations.
//
// *** READ THE STATE, NOT THE CODE. *** A non-looping animation sets
// holdOnComplete_ (SkeletalAnimator::playAnimation: holdOnComplete_ =
// !looping_), so a finished swing FREEZES ON ITS LAST FRAME and KEEPS the
// code "c01" until renderer_player_physics asks for "p01" again. Testing the
// code alone would leave the body drawn permanently after the first swing.
// The animator's own Playing state is the honest edge, so the pair
// "getCurrentAnimation() is an attack code AND the state is Playing" is
// precisely "an attack animation is currently playing" -- with no timer of
// our own and no dependence on /attack state. An out-of-range auto-attack
// that produces no swing never sets an attack code, so it draws nothing,
// which is what T307 measured.
void EntityRenderer::updateFirstPersonBodyDrawRule() {
    auto it = entities_.find(playerSpawnId_);
    if (playerSpawnId_ == 0 || it == entities_.end()) return;
    EntityVisual& visual = it->second;
    if (!visual.isPlayer) return;

    if (!visual.animatedNode) {
        // A static placeholder mesh has no skeleton to freeze, so the
        // OnAnimate trap does not apply to it and setVisible is honest here.
        if (visual.meshNode) {
            visual.meshNode->setVisible(!visual.isFirstPersonMode);
        }
        return;
    }

    bool draw = true;
    if (visual.isFirstPersonMode) {
        const SkeletalAnimator& animator = visual.animatedNode->getAnimator();
        draw = shouldDrawOwnBodyInFirstPerson(
            visual.animatedNode->getCurrentAnimation(),
            animator.getState() == AnimationState::Playing);
    }

    // WITHHOLD, never setVisible(false) -- see first_person_body_draw.h.
    // The node keeps animating while hidden, so the swing that reveals it is
    // already at the right phase.
    const bool withhold = !draw;
    if (visual.animatedNode->isWithheldFromRenderList() != withhold) {
        LOG_DEBUG(MOD_GRAPHICS,
                  "first-person draw rule: player body {} (anim='{}')",
                  withhold ? "hidden" : "DRAWN",
                  visual.animatedNode->getCurrentAnimation());
    }
    visual.animatedNode->setWithheldFromRenderList(withhold);
}

bool EntityRenderer::isPlayerBodyWithheldFromRenderList() const {
    auto it = entities_.find(playerSpawnId_);
    if (playerSpawnId_ == 0 || it == entities_.end()) return false;
    if (!it->second.animatedNode) return false;
    return it->second.animatedNode->isWithheldFromRenderList();
}

} // namespace Graphics
} // namespace EQT
