#pragma once

// B1138 (#458): the BONE half of the particle manager's entity callbacks --
// SpellVisualFX's hand pseudo-entities (bit 15, bit 14 = left) and B1131's
// head pseudo-entity (bit 14 alone) resolved on an entity's animated node.
// IrrlichtRenderer's callbacks (renderer_entities.cpp) and tools/model_viewer's
// FX harness both call these, so a Source on a hand or a head lands on the
// same bone in both. The plain-entity half (the node origin) stays with each
// caller: the client reads it off EntityVisual, the viewer off its node.
//
// B1144 (#464): which pseudo-id a spellsnew.eff slot follows is chosen from
// the slot's attach point by EQ::SpellVisualFX::attachPointEntityId():
// LHAND/RHAND -> the hand ids, HEAD/LEYE/REYE/MOUTH -> the head id, every
// other point -> the entity (resolved by the caller's plain-entity half).

#include "client/graphics/eq/animated_mesh_scene_node.h"
#include "client/graphics/spell_visual_fx.h"

namespace EQT {
namespace Graphics {

// True for an id the two functions below resolve (hand or head).
inline bool isSpellFxBoneId(uint16_t fxId) {
    return EQ::SpellVisualFX::isHandBoneId(fxId) || EQ::SpellVisualFX::isHeadBoneId(fxId);
}

// World position of the bone a hand/head pseudo-id names; false when the node
// has no such bone (placeholder or non-humanoid model) or the id is no bone id.
inline bool resolveSpellFxBonePosition(const EQAnimatedMeshSceneNode* node, uint16_t fxId,
                                       irr::core::vector3df& out) {
    if (!node) return false;
    int bone = -1;
    if (EQ::SpellVisualFX::isHandBoneId(fxId)) {
        bone = EQ::SpellVisualFX::isRightHand(fxId) ? node->findRightHandBoneIndex()
                                                    : node->findLeftHandBoneIndex();
    } else if (EQ::SpellVisualFX::isHeadBoneId(fxId)) {
        bone = node->findHeadBoneIndexForFx();
    }
    return bone >= 0 && node->getBoneWorldPosition(bone, out);
}

// World direction of a HAND bone (the spray direction); heads have none.
inline bool resolveSpellFxBoneDirection(const EQAnimatedMeshSceneNode* node, uint16_t fxId,
                                        irr::core::vector3df& out) {
    if (!node || !EQ::SpellVisualFX::isHandBoneId(fxId)) return false;
    const int bone = EQ::SpellVisualFX::isRightHand(fxId) ? node->findRightHandBoneIndex()
                                                          : node->findLeftHandBoneIndex();
    return bone >= 0 && node->getBoneWorldDirection(bone, out);
}

// B1167 (#486): the direction an ATTACHED spellsnew.edd emitter orients to.
// EQGraphicsDX9's CParticleSystem::Render (0x100605b0) gives every emitter
// attached to an actor that actor's frame: +0x60 == 1 on a model point takes
// the point's world matrix (axis 1 = row 2, axis 2 = row 0, axis 3 = -row 1,
// each / s19c: decompile FUN_100605b0, the `+0x60 == 1 && point` block after
// LAB_10060b82, 0x10060b8d..), otherwise an attached actor's own matrix (the
// last `else`, actor vtable +0x234: rows +0x20 / +0x00 / -+0x10); only an
// unattached emitter keeps a fixed frame. Row 0 of a model's matrix is its
// rest facing (+X for every race model, kHarnessFaceOnRotY's note), so the
// direction is the node's +X in world space; a hand pseudo-id keeps the hand
// bone's direction (the same row of the hand point). The sim builds the
// emitter frame from it (sim_particles.cpp: axis 2 = this direction made
// horizontal, axis 1 = world up, axis 3 = their cross).
inline bool resolveSpellFxAttachDirection(const EQAnimatedMeshSceneNode* node, uint16_t fxId,
                                          irr::core::vector3df& out) {
    if (!node) return false;
    if (EQ::SpellVisualFX::isHandBoneId(fxId)) return resolveSpellFxBoneDirection(node, fxId, out);
    irr::core::vector3df d(1.0f, 0.0f, 0.0f);
    node->getAbsoluteTransformation().rotateVect(d);
    if (d.getLengthSQ() < 1e-12f) return false;
    out = d.normalize();
    return true;
}

// B1148 (#468): the length in ms of `node`'s animation track `code` (the
// skeleton's own, donor tracks included: frames x frame ms,
// s3d_loader_skinning.cpp) -- what eqgame 0x47ddd4 reads off the caster
// actor for the spell's CastingAnim. -1 when the node, its skeleton or the
// track is missing.
inline int32_t CastAnimationTrackMs(const EQAnimatedMeshSceneNode* node, const char* code) {
    if (!node || !code) return -1;
    const auto skeleton = node->getAnimator().getSkeleton();
    if (!skeleton) return -1;
    const auto it = skeleton->animations.find(code);
    if (it == skeleton->animations.end() || !it->second) return -1;
    return it->second->animationTimeMs;
}

}  // namespace Graphics
}  // namespace EQT
