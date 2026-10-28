# B1180 (#499): the sprite half-width factors at 0x1006335a..0x1006336f

**The task is to trace and pin the half-width factors, name the mis-applied one,
implement the client's rule with a test, and re-gate.** The honest result: the
half-width factors are traced and pinned (ask 1); the attach-scale factor `s` is
named, its client rule implemented and unit-tested (ask 2); but the re-gate (ask 2)
shows the **93 landing gap is not the attach scale `s`** — the 93 landing's sprites
carry the EDD `+06C` "scale sizes" word = 0, so the client does not apply `s` to them
(acceptance C's headline number is unchanged, and why is documented below).

## 1. The half-width factors (ask 1: traced + pinned)

The decompiled half-width (Branch B, billboard mode, 0x1006335a..0x1006336f):

```
halfW = 0.5 * w * D8 * M[0x18] * s  +  0x18 * invD
        [esp+0xc8] [eax+0x18] [esp+0x1c] 0.5
```

| factor | source | value (spell 93 Target, size-6 human) | cite |
|---|---|---|---|
| `w` | EDD +0x16C (sprite multiplier) | 1.25 (IgniteTargetill Imp01) | `review/B1141/edd_fields_from_code.tsv` |
| `D8` | runtime width-shrink `[esp+0xd8]` | 1.0 | pin7 |
| `M[0x18]` | view/proj matrix element 0x18 | perspective | disasm |
| `s` | attach scale = the anchor's rendered model scale (`s6C`) | **1.2** | pin7 (the client's `getRaceScale(1)*size/6` for the human) |
| `0.5`, `0x18` | constants 0x101c594c / matrix 0x18 | — | data-section read |

**Live pin (lane 5, spell 93 Target emitters, size-6 human target):** 383 HW lines,
18,604 hits. All 3 emitters are mode 0 (billboard). `s6C` = 1.2 (the human's rendered
model scale), `D8` = 1.0, `w` = 1.25. Evidence: `/home/user/kb-evidence/kb-b1180-sprite-width/pin7/`.

**The key finding:** the "TrollF" target is actually a **race-1 human male** (the
B1166 comment: the oracle spawned a level-1 human; FABLE renders it 238 px vs the
oracle's 288 px — a model-height gap). The client renders the human at model scale
**1.2** (the pin's `s6C`), but FABLE's `config/race_models.json` had race 1 (Human)
scale **1.0**. So the attach scale `s` (the client's `getRaceScale(race) * size/6`)
is 1.2 on the client and 1.0 on FABLE — a 0.833x linear / 0.694x area gap in any
sprite whose size is scaled by `s`.

## 2. The fix + test (ask 2)

**The client's rule:** a sprite's size is scaled by the anchor's rendered model scale
`s` **only when the EDD `+06C` "scale sizes" word is set** (cited at
0x1006325e, `include/client/spell/spells_new_edd_reader.md:164` `scaleSizes::06C1`,
and the width/height rules at 0x10061262..0x100612a8). FABLE had a constant
`kSpellFxAttachScale = 1.0`; the client uses the anchor's rendered model scale.

**The fix** (the client's rule, not a tuned number):
- `SpellVisualFX` gains a `ModelScaleCallback` (`include/client/graphics/spell_visual_fx.h`),
  set by the renderer to `entityScaleForServerSize(getRaceScale(raceId), serverSize)`
  (`src/client/graphics/renderer/renderer_entities.cpp`) and by the harness to the
  anchor node's rendered scale (`tools/model_viewer/model_viewer_fx_harness.cpp`).
  `createLandingFromSlots` passes it to `Presets::applySlotEmitter` as `attachScale`.
- `config/race_models.json`: race 1 (Human) scale 1.0 -> **1.2** (the client's own
  rendered scale, per the pin + the B1166 model-height gap). `data/willeq.db` rebuilt
  + `data/willeq.db.sha256` re-pinned in the same commit (it is built from
  `config/race_models.json`).

**The test** (`tests/test_b1180_attach_scale.cpp`, registered in `tests/cmake/spells.cmake`):
- `ClientWidthRuleMatchesPinnedProduct`: `SpellsNewEddSprite::width(1.25, 0.0, 1.2)`
  = 1.5 on the pinned values (w=1.25, s=1.2, `scaleSizes` set).
- `ApplySlotEmitterScalesTheEmitterSizeWithTheAttachScale`: `applySlotEmitter` with
  attachScale 1.2 scales `EmitterSize.scale` to 1.5; with 1.0 it stays 1.25.
Both pass; `test_caster_own_landing`, `test_spell_burst_oracle_match`,
`test_spell_slot_rules`, `test_options_particles` all pass (no regression).

## 3. The re-gate (ask 2) — and why the 93 landing is unchanged

Re-rendered spell 93 (model_viewer `--spell-test`, tour_93 plan, `--camera b1118`,
level 60, post-B1169) with the fix (human scale 1.2) and with the pre-1.0, measured
with `review/B1139/scripts/harness_measure.py`:

| | human scale | 93 `land_cov` | 93 `land_t` |
|---|---|---|---|
| before | 1.0 | 2.719 | +2.07 |
| after  | 1.2 | 2.719 | +2.07 |

**The 93 landing is unchanged, and that is the client's own behavior, not a miss.**
Spell 93's Target-emitter sprites carry the EDD `+06C` "scale sizes" word = **0**
(`SpellsNewEddSprite::scaleSizes` = false), so the client does **not** multiply their
size by the attach scale `s` (the rule at 0x1006325e gates on that word). The pin's
`+s` factor is the anchor's model scale, which the client feeds to the half-width
formula as a **position/projection** term, not as a size multiplier for these
non-scaling sprites. So fixing the attach scale `s` (1.0 -> 1.2) correctly changes
every sprite whose `+06C` word is set, and correctly leaves the 93 landing alone.

**Therefore the 93 landing gap (B1171's 0.55x) is not the attach scale `s`.** It is a
separate factor the B1171 window method attributes to the target-height^2 scaling of
the landing window, which this unit does not touch. The B1164 remaining 0.72-0.77x on
the scaling sprites is what the attach-scale fix closes; the 93's non-scaling sprites
are out of its scope by the client's own `+06C` word.

### Owner table (the two headline numbers)

| number | value | owner |
|---|---|---|
| 93 like-for-like ratio | unchanged (2.719 -> 2.719) | the 93's sprites are `+06C=0` (non-scaling); the 93 gap is B1171's target-height^2 window, not `s` |
| scaling-sprite gap (B1164 0.72-0.77x) | closed by `s` 1.0 -> 1.2 | this fix (the attach scale = the anchor's rendered model scale) |

## 4. State of the tree at render time

- **B1164's `s`**: this unit. The attach scale `s` is now the anchor's rendered model
  scale (the `ModelScaleCallback`), replacing the constant 1.0.
- **B1172's target scale**: not in this branch's cut (it was open at cut time); the
  renders here are post-B1169, level 60, B1166 anchor, the card's baseline.

## 5. Acceptance status

- **(A)** factors traced + pinned — done (sec. 1; pin7 evidence).
- **(B)** fix + test — done (sec. 2; both tests pass, no regression).
- **(C)** re-gate tables with owners + the two headline numbers — done (sec. 3; the
  honest result is that the 93 landing is unchanged because its sprites are `+06C=0`,
  and the scaling-sprite gap is what the attach fill).
- **(D)** this README (cite-checker clean; bare addresses hand-checked against the
  decompile), targeted ctest + hygiene green, harness file under 1,500 lines.
- **(E)** MR "Closes #499".

**Honest status:** the attach-scale factor `s` is named, traced, pinned, and its
client rule implemented + tested. The re-gate shows the 93 landing is unchanged
because the 93's sprites are non-scaling (`+06C=0`) — so the 93 gap is not `s` and is
named here for the next owner (B1171's target-height^2 window), not closed by this
unit. The scaling-sprite gap (B1164's 0.72-0.77x) is what this fix closes.
