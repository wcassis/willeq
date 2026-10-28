#pragma once

// B1157 (#476): the caster model's cast animation -- the official client's
// rule, one code path for the client and the FX harness.
//
// On OP_BeginCast eqgame plays the spell's CastingAnim (field 120) on the
// caster:
// the handler (0x49b39d..) gives the self-cast path the spawn's animation
// manager (FUN_0047c08e) and calls its set function (vtbl+8) with
// (CastingAnim id, 1, 0, speed 1.0f, 1) -- eqgame.exe.c 122131-122132:
// (**(code **)(*piVar6 + 8))(spell+0x186, 1, 0, 0x3f800000, 1); line 122136
// is the same word's length query, FUN_0047ddd4(spell+0x186) (B1145's
// Source term).
// The setter names the track through eqgame's id -> code table
// (0x8ffbc8 + id * 20, filled by 0x44e7ec..; B1148 read it from the binary:
// 42/43/44 = T04/T05/T06 -- EQ::SpellCastAnimationCode), plays it at the
// given speed through the actor's model interface (actor vtbl+0x178, the
// play codes 3 and 4 with the code string), and stores the id on the actor
// (actor+0x134) so the state machine's already-playing no-op path
// (actor+0x134 == the requested id, FUN_00450c2f) keeps it without
// re-requesting: the model is not re-driven frame by frame, the cast track
// runs, and when it finishes it holds -- the raised-hands end frame the
// oracle's caster keeps through the channel (refactor/B1148/README.md
// sec. 3: leg C1's frames; issue #476 "the oracle's caster raises its
// hands").
//
// Loop or hold once the track's own length is reached: the client passes its
// flag through (p01's idle carries 1, the cast's one-shot carries 3,
// FUN_00450c2f's (param_3 == 0) * 2 + 1) and the model's looped-flag decides;
// fable carries the same split in the model data: the skeleton's per-track
// looped flag (SkeletalAnimator::playAnimation sets holdOnComplete_ =
// !looping_, looping_ = loop || anim->isLooped) -- looped tracks loop,
// one-shot tracks hold their last frame. So one call plays the client's
// rule: the track once at speed 1.0, holding (or looping, per the model)
// through the rest of the channel.
//
// OP_Action / interrupt: the cast state ends and the state machine plays the
// new state's animation -- the idle of a standing caster (the client's
// per-frame machine requests p01-family for the local player, o01 for an
// idle NPC).
//
// fable's pre-B1157 rule: a hard-coded "t04" (playThrough, no loop, stretched
// to 20 % of the cast time) for every NPC caster, the player not animated at
// all (entity_castbars.cpp startEntityCast), and in the harness an o01 idle
// looping during the channel plus a guessed t04/t05/t06 played once at
// completion (tools/model_viewer/model_viewer_spell_test.cpp). Both now go
// through these two functions.
//
// Which track a spell plays is the SPELL's word (CastingAnim, spells.txt
// field 120), never the caster's class or the spell's type: the db gives
// the eight B1149-B1152 b1118 spells (93/38/54/41/201/253/458/656) CastingAnim
// 44 = T06, and 21 of the 40 B1130 record spells; the other 19 carry 43 (x14),
// 42 (x2), 39 (x2) or 40 (x1), so the re-gated sweep plays each spell's own
// code (data/willeq.db spells.cast_anim).

#include <string>

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/spell/spell_fx_timing.h"

namespace EQT {
namespace Graphics {

// The track code the spell's CastingAnim id names on this model; nullptr when
// the id names no code (outside 1..78, EQ::SpellCastAnimationCode) or the
// model has no such track (the client keeps the model's current animation,
// 0x450c2f's no-track path).
inline const char* castAnimTrackFor(const EQAnimatedMeshSceneNode* node, int castAnimId) {
    if (!node) return nullptr;
    const char* code = EQ::SpellCastAnimationCode(castAnimId);
    return (code && node->hasAnimation(code)) ? code : nullptr;
}

// OP_BeginCast: play the track at speed 1.0 (the client's 0x3f800000), one shot
// (loop/hold per the model's own looped flag), play-through so a swing that
// is already playing completes first and the queued cast starts. Returns the
// code played, "" when the model has no such track (its current animation
// then stands, the client's fallback).
inline std::string playCastAnim(EQAnimatedMeshSceneNode* node, int castAnimId) {
    if (!node) return std::string();
    const char* code = castAnimTrackFor(node, castAnimId);
    if (!code) return std::string();
    node->getAnimator().resetPlaybackSpeed();  // the client plays the cast at 1.0
    if (!node->playAnimation(code, /*loop=*/false, /*playThrough=*/true)) return std::string();
    return std::string(code);
}

// B1174 (#493): a track an OP_Action started (no cast state, a /disc) has
// played out when the animator stopped on it (one-shot tracks hold their
// last frame, SkeletalAnimator::holdOnComplete_); the caller then plays the
// standing idle, as the official client's caster returns to its idle pose
// (refactor/B1174/README.md, the observer strip).
inline bool actionCastAnimEnded(EQAnimatedMeshSceneNode* node, const std::string& code) {
    if (!node || code.empty()) return true;
    if (node->getAnimator().getCurrentAnimation() != code) return true;  // something else took over
    return node->getAnimator().getState() == AnimationState::Stopped;
}

// OP_Action / interrupt: the cast state ends; stop the held/playing cast
// track (a non-play-through request would only queue behind it) and play the
// new state's animation -- idleCode, the caster's idle ("p01" the local
// player's, "o01" an NPC's).
inline void endCastAnim(EQAnimatedMeshSceneNode* node, const char* idleCode) {
    if (!node || !idleCode) return;
    node->stopAnimation();
    if (node->hasAnimation(idleCode))
        node->playAnimation(idleCode, /*loop=*/true, /*playThrough=*/false);
}

}  // namespace Graphics
}  // namespace EQT
