// B1138 (#458): the FX half of the SpellCastVisual* bridge arms, shared by
// IrrlichtBridge and tools/model_viewer's FX harness. See the header. The
// bodies are the arms' FX statements moved verbatim from irrlicht_bridge.cpp.

#include "client/bridge/spell_cast_fx.h"

#include "client/graphics/spell_visual_fx.h"

namespace eqt {
namespace bridge {

void ApplySpellCastStartedFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                             const state::SpellCastVisualStartedData& d,
                             CastGlowRoute::Clock::time_point now) {
    // B1170 (#489): a 0x4033 missile object, not a cast: the object alone.
    if (d.missileObject) {
        if (fx) fx->spawnMissileObject(*d.missileObject, d.fxGateLevel, d.casterFx, d.missileTargetFx);
        return;
    }
    const bool createGlow = route.onBeginCast(d.casterId, d.spellId, d.castTimeMs, now);
    if (!createGlow || !fx) return;
    // T381 (B160): teach the FX layer this spell's resist type, school and
    // spell_affect_index BEFORE creating anything -- createCastGlow() reads
    // all three back out (colour, spray style, spells.eff stage gate). The
    // game thread filled them; the renderer never touches a SpellDatabase.
    fx->noteSpellVocabulary(d.spellId, d.spellVisual);
    // B1144 (#464): the caster's gate level picks the record's slots.
    // B1148 (#468): CastingAnim picks the caster track whose length the
    // Source runs past the cast time.
    // B1165 (#484): the On-For filter's input (spell_fx_options.h).
    fx->createCastGlow(d.casterId, d.spellId, d.castTimeMs, d.fxGateLevel, d.castAnim, d.casterFx);
}

void ApplySpellCastCompleteFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                              const state::SpellCastVisualCompleteData& d,
                              CastGlowRoute::Clock::time_point now) {
    route.onComplete(d.casterId, d.spellId, d.isSuccess, now);
    if (!fx) return;
    // T381 (B160): a cast can land without this client having seen its
    // BeginCast (zoned in mid-cast, out of range), so the completion payload
    // carries the vocabulary too.
    fx->noteSpellVocabulary(d.spellId, d.spellVisual);
    // B1131: released, not freed -- the Source holds its record's measured
    // time past OP_Action, longer when the target is someone else.
    // B1144 (#464): an action with no BeginCast draws no Source (the client
    // plays stage 0 from begin-cast only, cast_glow_route.h); the release is
    // then a no-op.
    fx->releaseCastGlow(d.casterId, d.spellId, d.targetId);
    // T395 (board B204): the landing is armed by the CAST-ANIMATION packet
    // (effect_flag == 0), not the success repeat. The success packet reaches
    // only the caster and the target, so gating on isSuccess meant no
    // observing client ever drew an impact -- and the caster, which receives
    // both, would have drawn it twice.
    // B1132 (#452): the whole landing on the record's Target anchor (the
    // target; the caster only for a caster-anchored record).
    // B1144: the record's Target slots at the caster's gate level; none = no landing.
    // B1165 (#484): stage 2 on (caster, target).
    if (d.isCastAnimation) fx->createLanding(d.casterId, d.targetId, d.spellId, d.fxGateLevel, d.casterFx, d.targetFx);
}

void ApplySpellCastInterruptedFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                                 const state::SpellCastVisualInterruptedData& d) {
    route.onInterrupt(d.casterId);
    if (fx) fx->removeCastGlow(d.casterId);
}

}  // namespace bridge
}  // namespace eqt
