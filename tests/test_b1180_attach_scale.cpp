// B1180 (#499): the EDD sprite's attach scale is the anchor actor's rendered
// scale -- the client's s6C word (the attach point's world-matrix row-0 norm,
// the actor's scale) -- not the constant 1.0 that B1149's ruling left fable
// passing. Pinned live on lane 5 (refactor/B1180, the b1180_hw_gdb probe):
// spell 93's Target emitters on the size-6 human target carry s6C = 1.2,
// f6C (the "scale sizes" word) = 1, multiplier (+0x16C) = 1.0 and width
// (+0xCC) = 1.25 (IgniteTargetill), so the oracle draws the sprite at
// 1.25 * 1.2 = 1.5 times the unit size.
//
// THE RULE, as the client applies it (EQGraphicsDX9 0x1006325e, the multiply
// sites 0x10063364..0x1006336c): the sprite width is
//     width * multiplier * (scaleSizes ? s : 1.0)
// where `s` is the attach scale (the anchor's rendered scale, the s6C word).
// fable's SpellPresets::applySlotEmitter applies that rule to the
// EmitterConfig; B1164 (#483, merged ahead of this unit) is what feeds it:
// the client's actor scale (EQ::SpellFxAttachScale, actor_scale.h) via
// SpellVisualFX::setEntityAttachScaleCallback, instead of the constant 1.0
// (kSpellFxAttachScale). B1180's pin is what fixed the factor.
//
// THIS TEST is on the pinned values: the client's width rule, and that
// applySlotEmitter applies the attach scale to the EmitterConfig's size. A
// landing drawn on a 1.2x anchor must draw its sprites 1.2x the size the same
// slot draws on a unit-scale anchor.

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "client/spell/spells_new_edd_reader.h"
#include "client/spell/spell_fx_timing.h"
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"

namespace {

// The pinned lane-5 words for spell 93's Target emitter (IgniteTargetill),
// from refactor/B1180/pin.tsv: width w = 1.25, multiplier (+0x16C) = 1.0,
// scaleSizes (+0x6C) = 1, attach scale (s6C) = 1.2.
EQ::SpellsNewEddSprite PinnedSprite() {
    EQ::SpellsNewEddSprite s;
    s.widthMin = 1.25f;
    s.widthMax = 1.25f;
    s.heightMin = 1.25f;
    s.heightMax = 1.25f;
    s.multiplier = 1.0f;
    s.scaleSizes = true;
    return s;
}

// The client's width rule (EQGraphicsDX9 0x1006325e): width * multiplier *
// (scaleSizes ? s : 1.0). On the pinned words, s = 1.2 gives 1.5, s = 1.0
// (B1149's constant) gives 1.25 -- the 0.833x linear / 0.694x area gap.
TEST(B1180AttachScale, TheClientWidthRuleScalesWithTheAttachScale) {
    const EQ::SpellsNewEddSprite s = PinnedSprite();
    EXPECT_FLOAT_EQ(s.width(0.5f, 0.5f, 1.0f), 1.25f)
        << "unit attach scale must draw the unit width (the pre-B1180 value)";
    EXPECT_FLOAT_EQ(s.width(0.5f, 0.5f, 1.2f), 1.25f * 1.2f)
        << "the pinned s6C = 1.2 must scale the width to 1.5";
    EXPECT_FLOAT_EQ(s.width(0.5f, 0.5f, 1.2f) / s.width(0.5f, 0.5f, 1.0f), 1.2f)
        << "the linear gap is the attach scale";
    // scaleSizes = 0 (no +0x6C): the attach scale is ignored, width = unit.
    EQ::SpellsNewEddSprite noscale = s;
    noscale.scaleSizes = false;
    EXPECT_FLOAT_EQ(noscale.width(0.5f, 0.5f, 1.2f), 1.25f)
        << "a no-scale sprite must not take the attach scale";
}

// applySlotEmitter applies the rule to the EmitterConfig: the sprite's scale
// is the EDD multiplier times the attach scale (eddSprite.scale), and the
// width is the EDD width times that scale. The same slot drawn on a 1.2x anchor
// has a sprite 1.2x the width of the unit-scale anchor's.
TEST(B1180AttachScale, ApplySlotEmitterScalesTheEmitterSizeWithTheAttachScale) {
    ASSERT_TRUE(EQT::Graphics::Environment::SpellEffectsConfig::instance().load(
        std::string(WILLEQ_REPO_ROOT) + "/config/spell_effects.json"));

    const auto make = [](float attachScale) {
        auto def = EQT::Graphics::Environment::SpellPresets::TargetForSpell(
            glm::vec4(1.0f, 0.47f, 0.2f, 1.0f), 93, 0, true, 0.0f, 1.0f, false);
        EQ::SpellFxSlotTiming timing;
        timing.start = 0.0f;
        timing.run = 1.0f;
        timing.lifetime = 1.0f;
        timing.perTick = 1;
        timing.ticksPerSecond = 60.0f;
        const EQ::SpellsNewEddSprite sprite = PinnedSprite();
        EQT::Graphics::Environment::SpellPresets::applySlotEmitter(
            {&def}, timing, &sprite, 0.0f, attachScale);
        const auto& e = def.emitters.at(0).config.eddSprite;
        EXPECT_TRUE(e.active) << "applySlotEmitter did not activate the EDD sprite";
        EXPECT_FLOAT_EQ(e.scale, 1.0f * attachScale)
            << "the sprite scale is the EDD multiplier times the attach scale";
        return e.widthMin * e.scale;  // the width at the pinned s
    };

    const float unit = make(1.0f);
    const float scaled = make(1.2f);
    ASSERT_GT(unit, 0.0f) << "no width on a unit-scale slot";
    EXPECT_FLOAT_EQ(unit, 1.25f) << "unit attach scale -> the unit EDD width";
    EXPECT_FLOAT_EQ(scaled, 1.25f * 1.2f)
        << "the pinned s6C = 1.2 must scale the sprite width to 1.5";
    EXPECT_FLOAT_EQ(scaled / unit, 1.2f) << "the linear gap is the attach scale";
}

}  // namespace
