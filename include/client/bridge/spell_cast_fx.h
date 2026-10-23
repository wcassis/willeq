#pragma once

// B1138 (#458): the FX half of the three SpellCastVisual* bridge events, in
// ONE place. IrrlichtBridge's arms call these (with Clock::now()) and so does
// tools/model_viewer --spell-test (with its fixed-dt clock), so the fable-side
// FX harness draws exactly the sequence the client draws:
//   BeginCast  -> noteSpellVocabulary + createCastGlow(caster, spell, castMs)
//   OP_Action  -> noteSpellVocabulary + releaseCastGlow (B1131 hold) or the
//                 action-only createCastGlow (B1105), then, on the
//                 cast-animation packet only, createLanding (B1132)
//   interrupt  -> removeCastGlow
// The non-FX parts of those arms (casting bars, NPC completion animation)
// stay in the bridge.

#include "client/bridge/cast_glow_route.h"
#include "client/state/event_payloads_ui.h"

namespace EQ { class SpellVisualFX; }

namespace eqt {
namespace bridge {

void ApplySpellCastStartedFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                             const state::SpellCastVisualStartedData& d,
                             CastGlowRoute::Clock::time_point now);

void ApplySpellCastCompleteFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                              const state::SpellCastVisualCompleteData& d,
                              CastGlowRoute::Clock::time_point now);

void ApplySpellCastInterruptedFx(CastGlowRoute& route, EQ::SpellVisualFX* fx,
                                 const state::SpellCastVisualInterruptedData& d);

}  // namespace bridge
}  // namespace eqt
