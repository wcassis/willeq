#ifndef EQT_GRAPHICS_FIRST_PERSON_BODY_DRAW_H
#define EQT_GRAPHICS_FIRST_PERSON_BODY_DRAW_H

#include <string>

namespace EQT {
namespace Graphics {

// ==========================================================================
// THE FIRST-PERSON DRAW RULE (T309), as measured against the official
// Titanium client by T307 (refactor/T307_official_draw_rule.md,
// review/T307/README.md).
// ==========================================================================
//
// *** In first person the official client draws the player's own model --
// the WHOLE model, with its equipped weapon -- for the duration of an ATTACK
// animation, and at no other time. ***
//
// The discriminators, so nobody re-opens them:
//
//  * It is NOT "any non-idle animation". Eight emotes at the very camera pose
//    that a sword swing fills produced 2250 consecutive frames at 0.0000%
//    maximum per-frame pixel difference, while a third-person control shows
//    those same emotes plainly animating. The first-person null is a DRAWING
//    decision, not an animation that failed to play.
//  * It is NOT "only the animating limb". During a KICK the two-handed
//    halberd -- held in the hands, nothing to do with the kicking leg -- is
//    drawn in full.
//  * It is NOT the /attack STATE. An out-of-range auto-attack that produces
//    no swing draws nothing. The trigger is the animation actually PLAYING.
//  * It is NOT a near plane (refuted T300 Leg A), NOT a viewmodel pass
//    (refuted T299), NOT a pitch clamp (refuted T300 Leg C and T301 Leg B --
//    the measured clamp reaches PAST the nadir, and an identical model
//    teleported onto the actor's own coordinates at that clamp fills 17.8% of
//    the frame. The frustum contains the body; the client does not draw it).
//
// Duty cycle at the clamp was 22.5%: swings ran 1.13 s mean on the Halberd's
// 5.0 s timer, kicks 0.72 s.
//
// WHAT IS STILL UNMEASURED (T307 says so itself, and this file must not
// pretend otherwise): bash, ranged, casting, being hit, jumping and
// sit/stand transitions were never shot. Casting (t04-t06) and the damage
// reactions (d01-d05) are therefore deliberately NOT attacks here -- an
// unmeasured case is left drawing nothing, which is the behaviour fable had
// before this rule existed.

// The EQ animation codes this rule treats as an ATTACK.
//
//   c01..c11  the combat family: kick, the five weapon swings, throwing,
//             bash, hand-to-hand, archery, swim attack, round kick.
//             T307 measured c01 (kick) and the halberd swing (c03/c04)
//             directly; the rest are the same family, played through the
//             same path (eq.cpp's combat-skill dispatch, skill_constants.h's
//             getSkillAnimationCode()).
//   t07..t09  flying kick, tiger strike, dragon punch -- the monk special
//             ATTACKS. They are combat skills that eq.cpp dispatches
//             identically to c01/c11; only their code letter differs.
//             Included by that family argument, NOT by measurement.
//
// Everything else -- emotes (s##), locomotion (l##), poses (p##), idles
// (o##), casting (t02-t06), damage (d##) -- is not an attack.
inline bool isAttackAnimationCode(const std::string& animCode) {
    if (animCode.size() != 3) return false;
    const char a = animCode[0];
    const char b = animCode[1];
    const char c = animCode[2];
    if (b < '0' || b > '9' || c < '0' || c > '9') return false;
    const int n = (b - '0') * 10 + (c - '0');
    if (a == 'c') return n >= 1 && n <= 11;   // the combat family
    if (a == 't') return n >= 7 && n <= 9;    // monk special attacks
    return false;
}

// The rule itself, as a pure function of the local player's animation state.
//
// `animationPlaying` must be the animator's own PLAYING state, not merely
// "an animation code is set": a completed play-through holds its code for a
// frame before the animator hands over to idle, and a paused or stopped
// animation is not a swing.
inline bool shouldDrawOwnBodyInFirstPerson(const std::string& animCode,
                                           bool animationPlaying) {
    return animationPlaying && isAttackAnimationCode(animCode);
}

// ==========================================================================
// HOW THE HIDING MUST BE DONE -- READ THIS BEFORE TOUCHING THE CALL SITE.
// ==========================================================================
//
// *** HIDE BY WITHHOLDING THE NODE FROM THE RENDER LIST, NEVER BY
// setVisible(false). ***
//
// Irrlicht's ISceneNode::OnAnimate() is wrapped in `if (IsVisible)`, so an
// invisible node ships a FROZEN skeleton and never advances its children's
// absolute transforms either. Under THIS rule the model must keep animating
// WHILE HIDDEN -- it is hidden for the 77.5% of the time that is not a swing,
// and the instant a swing starts it becomes visible mid-animation. A
// setVisible() toggle looks perfectly correct in every headless test and
// produces a frozen or snapping weapon live. T296 found this the hard way;
// it is why that unit withheld the node rather than hiding it.
//
// EQAnimatedMeshSceneNode::setWithheldFromRenderList() is the supported way:
// it skips registerNodeForRendering() for the node AND the child recursion
// (so the equipment children go with the body -- the rule draws the whole
// model with its weapon, or none of it), while OnAnimate() still runs.

} // namespace Graphics
} // namespace EQT

#endif // EQT_GRAPHICS_FIRST_PERSON_BODY_DRAW_H
