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
for the same pair in the window do not re-arm it, including copies delivered
after the success repeat of an AoE. The success repeat marks the pair finished;
a later BeginCast can then start a new cast of the same spell immediately.
Without BeginCast, a different spell or a later window is a new cast. Two
indistinguishable action-only casts of the same spell inside that window
cannot be separated by the available payload.

The existing BeginCast glow still ends at the action completion's existing
`removeCastGlow` call. For an action-only cast, that call occurs before the
new `createCastGlow` call, and the immediately following success repeat must
not remove the new glow before it reaches a frame; its bounded emitter lifetime
ends it. The action payload has no cast time, so the action-only fallback is
2 seconds (an implementation bound, **not** an oracle duration claim). This
is the minimum ordering needed to make the newly reachable
Source stage visible without changing the BeginCast removal point.

The existing `createCastGlow` maps Source to CastGlow or CastSpray. It is not
a sustained aura/column primitive. B1104 §5 measured Weaken (41) as a
caster cloud in its vf2834-2984 window, Whirling Wind (253) as a
ground-to-head column in vf3156-3306, and Fire Spiral (458) as a sustained
caster-side burst for roughly 10-15 seconds. The issue's measured duration
for 41 is roughly 4-5 seconds; B1104's window does not establish a narrower
duration for 253. This card will play the existing
glow only, not claim those shapes or durations match the oracle.

## Verification

The pre-fix `SpellImpactVisualProducer.LocalActionOnlyHasSourceStageRoute`
failed with exit 8 at the missing `createCastGlow` route; the complete red
ctest transcript is in the task evidence directory as `prefix_red.log`.
The test was kept, not removed or weakened. The ThinkPad Release GLES2 build
then passed all nine `SpellImpactVisualProducer` tests, including real
`OP_Action` dispatch for local action-only, BeginCast+action, success-only,
remote NPC, and repeated animation packets. The adjacent
`CasterOwnLandingTest` and `SpellVisualVocabularyTest` selection passed
11/11. The remote `hygiene` selection passed 45/45 (three declared skips).

Lane-1 proof has two **failed visual checks** preserved under the task's
evidence root: `after_every1_93.png` / `after_every1_253.png` (dump every
frame) and `after_every2_93.png` / `after_every2_253.png` (B1104 cadence).
Both runs received the self-cast `OP_Action` and logged exactly one
`createCastGlow` for each spell, but neither pair of sheets shows a visible
caster effect. The first run's screenshot frames took over one second;
`sim_particles.cpp` returns before processing pending commands when its
`deltaTime > 1.0f`, a possible capture-specific explanation that the images
alone do not prove. Both runs restored the snapshot byte-identically with
world down and released lane 1.

The third run used the rebuilt head `e23830729fa82ffa856efc9c980ff7033f5b08e2`
(ThinkPad binary SHA-256
`0014cd37fabd3270a727914187f4921b3969434bae38d2c07ca354b4cda7cfcb`),
waited 100 seconds for zone streaming, and dumped every fourth frame. It
again logged one local-player `createCastGlow` for each of 93 and 253
(`after/client.log`), but `after_93.png` and `after_253.png` still show no
visible caster glow or aura against B1104's `before_93.png` and
`before_253.png`. The full frame sequence is
`after/B1105_capture.mp4` with `after/B1105_capture.txt` recording the measured
capture rate; that rate is a capture artifact, not client performance.
The third snapshot was also restored byte-identically with world down; lane 1
was released and `eqemu_stack_ready` read NO afterwards.

| acceptance | result | evidence |
|---|---|---|
| A, design before code | PASS | first B1105 commit; Design above |
| B, one Source route per cast | PASS at routing/creation, **not pixels** | `CastGlowRoute`, live `SpellAction` and `createCastGlow` lines |
| C, local/no Begin; both; flag 4; NPC | PASS | `test_spell_impact_visual`, `prefix_red.log` |
| D, 93 and 253 after-sheets visibly show Source | **FAIL** | three after-sheet pairs; third `after_93.png`, `after_253.png` |
| E, no blessed assets/overrides | PASS | changed-path audit |
| F, green CI | CHECK AT MR HEAD | CI result is recorded on the MR; not a substitute for failed row D |

The failed visual row blocks a claim that issue #425 is fixed. The action
route is reached, so a downstream particle-command/render-path investigation
is required; the `deltaTime` gate is a concrete candidate, **not a proven
cause** without command-level tracing. No golden, preset, manifest or
`config/spell_effects.json` override was changed to mask it.

## FOLLOW-UP

The sustained aura/column for 41/253/458 needs a separately measured
primitive and lifetime. Do not approximate it with an invented spell override.
The visible-Source proof for 93/253 remains open: trace the
`CreateSpellEffect` command through `SimulationWorker::computeParticles()`
on a cast frame and determine whether the early return drops it before
altering that separate pipeline.
