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
    const bool createGlow = route.onBeginCast(d.casterId, d.spellId, d.castTimeMs, now);
    if (!createGlow || !fx) return;
    // T381 (B160): teach the FX layer this spell's resist type, school and
    // spell_affect_index BEFORE creating anything -- createCastGlow() reads
    // all three back out (colour, spray style, spells.eff stage gate). The
    // game thread filled them; the renderer never touches a SpellDatabase.
    fx->noteSpellVocabulary(d.spellId, d.spellVisual);
    fx->createCastGlow(d.casterId, d.spellId, d.castTimeMs);
}

void ApplySpellCastCompleteFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                              const state::SpellCastVisualCompleteData& d,
                              CastGlowRoute::Clock::time_point now) {
    const auto glow = route.onComplete(d.casterId, d.spellId, d.isCastAnimation, d.isSuccess, now);
    if (!fx) return;
    // T381 (B160): a cast can land without this client having seen its
    // BeginCast (zoned in mid-cast, out of range), so the completion payload
    // carries the vocabulary too.
    fx->noteSpellVocabulary(d.spellId, d.spellVisual);
    // B1131: released, not freed -- the Source holds its record's measured
    // time past OP_Action, longer when the target is someone else.
    if (glow.remove) fx->releaseCastGlow(d.casterId, d.spellId, d.targetId);
    // B1105: action-only casts have no BeginCast. Keep the old removal point,
    // then arm Source through the same FX body.
    if (glow.create) fx->createCastGlow(d.casterId, d.spellId, CastGlowRoute::kActionGlowMs);
    // T395 (board B204): the landing is armed by the CAST-ANIMATION packet
    // (effect_flag == 0), not the success repeat. The success packet reaches
    // only the caster and the target, so gating on isSuccess meant no
    // observing client ever drew an impact -- and the caster, which receives
    // both, would have drawn it twice.
    // B1132 (#452): the whole landing on the record's Target anchor (the
    // target; the caster only for a caster-anchored record).
    if (d.isCastAnimation) fx->createLanding(d.casterId, d.targetId, d.spellId);
}

void ApplySpellCastInterruptedFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                                 const state::SpellCastVisualInterruptedData& d) {
    route.onInterrupt(d.casterId);
    if (fx) fx->removeCastGlow(d.casterId);
}

}  // namespace bridge
}  // namespace eqt
