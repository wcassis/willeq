# B1105 (#425): Source stage from the cast-animation action

## Design (recorded before implementation)

`SpellCastVisualCompleteData::isCastAnimation` already carries the
`OP_Action` effect_flag 0 distinction to the render thread, with the spell's
visual vocabulary copied by value. The bridge will use that event to call the
same `SpellVisualFX::createCastGlow` as `SpellCastVisualStarted`. It will not
turn the action into a synthetic Started event: that event also starts casting
bars, whereas a GM instant cast has no BeginCast or casting bar.

A small device-free gate in `IrrlichtBridge` records caster + spell id for a
bounded cast window. BeginCast arms the existing glow and records the pair;
the first animation action arms it only if BeginCast did not already do so.
An action packet may be repeated for multiple targets, so repeated actions
for the same pair in the window do not re-arm it. The success repeat clears
the pair for the next cast. A different spell or a later window is a new cast.

The existing BeginCast glow still ends at the action completion's existing
`removeCastGlow` call. For an action-only cast, that call occurs before the
new `createCastGlow` call, and the immediately following success repeat must
not remove the new glow before it reaches a frame; its bounded emitter lifetime
ends it. This is the minimum ordering needed to make the newly reachable
Source stage visible without changing the BeginCast removal point.

The existing `createCastGlow` maps Source to CastGlow or CastSpray. It is not
a sustained aura/column primitive. B1104 §5 measured Weaken (41) as a
caster cloud in its vf2834-2984 window, Whirling Wind (253) as a
ground-to-head column in vf3156-3306, and Fire Spiral (458) as a sustained
caster-side burst for roughly 10-15 seconds. This card will play the existing
glow only, not claim those shapes or durations match the oracle.

## Verification

Pending implementation and the required pre-fix red test, x86 checks, lane-1
capture, and CI.

## FOLLOW-UP

The sustained aura/column for 41/253/458 needs a separately measured
primitive and lifetime. Do not approximate it with an invented spell override.
