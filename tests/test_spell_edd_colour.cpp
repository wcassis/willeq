// B1146 (#466): a spell particle's colour comes from its spellsnew.edd
// emitter's colour words, exactly as the official client draws it.
//
// Code facts (EQGraphicsDX9.dll, read by B1141, docs/spellsnew_format.md):
//   +0xD4..+0xDC start RGB, +0xE0..+0xE8 end RGB, R = D4 + (E0 - D4) * age / L,
//   vertex colour A<<24|R<<16|G<<8|B (0x100607e0, 0x10063ab7..);
//   +0x90 / +0x94 alpha fade-in / fade-out, +0xA4 peak opacity:
//   alpha = min(fadeIn * fadeOut, peak) (0x10063a42..0x10063a85);
//   +0x68 == 1: SRCBLEND SRCALPHA, DESTBLEND ONE (0x1005f7c2);
//   texture stage 0 COLOROP / ALPHAOP MODULATE: vertex colour x texel.
// Oracle fact (B1134 P4/P5 observer captures, review/B1146/): the four
// discipline records 326 / 329 / 328 / 330 differ ONLY in these words and the
// official client draws them blue / red / green / white.
//
// Pinned here: the decode (synthetic entry and the shipped file), the alpha
// formula, the preset that installs a stage's words per texture region, and
// the particle path (SimulationWorker::computeOnce): an active region's
// particle carries the interpolated RGB and, on the client's alpha (shipped
// since B1149), the client's fade x peak and its blend, white emitters too;
// RGB-only (B1146's setting) keeps the style's alpha and a white emitter's
// particle exactly as the style draws it.

#include <gtest/gtest.h>

#include "client/spell/spells_new_edd_colour.h"
#include "client/spell/spells_new_eff_reader.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)
#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/simulation_worker.h"
#include "particle_test_seed.h"
#include <glm/glm.hpp>
#endif

namespace {

std::string clientDir() {
    const char* env = std::getenv("EQ_CLIENT_PATH");
    return env ? env : "/mnt/projects/EverQuestP1999";
}

void putI(std::vector<uint8_t>& e, size_t off, int32_t v) { std::memcpy(&e[off], &v, 4); }
void putF(std::vector<uint8_t>& e, size_t off, float v) { std::memcpy(&e[off], &v, 4); }

TEST(SpellEddColour, DecodesTheColourWordsAtTheirCodeOffsets) {
    std::vector<uint8_t> e(416, 0);
    putI(e, 0x68, 1);
    putF(e, 0x90, 0.25f);
    putF(e, 0x94, 0.75f);
    putF(e, 0xA4, 0.5f);
    putI(e, 0xD4, 10); putI(e, 0xD8, 20); putI(e, 0xDC, 30);
    putI(e, 0xE0, 40); putI(e, 0xE4, 50); putI(e, 0xE8, 60);
    const EQ::SpellsNewEddColour c = EQ::decodeSpellsNewEddColour(e.data());
    EXPECT_TRUE(c.additive);
    EXPECT_FLOAT_EQ(c.fadeIn, 0.25f);
    EXPECT_FLOAT_EQ(c.fadeOut, 0.75f);
    EXPECT_FLOAT_EQ(c.peakOpacity, 0.5f);
    EXPECT_EQ(c.start[0], 10); EXPECT_EQ(c.start[1], 20); EXPECT_EQ(c.start[2], 30);
    EXPECT_EQ(c.end[0], 40); EXPECT_EQ(c.end[1], 50); EXPECT_EQ(c.end[2], 60);
    EXPECT_FALSE(c.isWhite());

    putI(e, 0x68, 0);  // anything but 1 is the alpha blend (DESTBLEND INVSRCALPHA)
    EXPECT_FALSE(EQ::decodeSpellsNewEddColour(e.data()).additive);
    putI(e, 0x68, 2);
    EXPECT_FALSE(EQ::decodeSpellsNewEddColour(e.data()).additive);
}

TEST(SpellEddColour, AlphaIsTheClientsFadeTimesPeak) {
    // No fade times: the peak opacity alone; a peak above 1 never brightens.
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(0.0f, 1.0f, 0.0f, 0.0f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(0.3f, 0.7f, 0.0f, 0.0f, 8.0f), 1.0f);
    // Fade-in by age, fade-out by remaining time, multiplied.
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(0.25f, 5.0f, 0.5f, 0.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(2.0f, 0.1f, 0.0f, 0.5f, 1.0f), 0.2f);
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(0.25f, 0.25f, 0.5f, 0.5f, 1.0f), 0.25f);
    // Then clamped to the peak.
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(0.45f, 5.0f, 0.5f, 0.0f, 0.1f), 0.1f);
    // Past the fade-in and before the fade-out: the peak.
    EXPECT_FLOAT_EQ(EQ::eddParticleAlpha(1.0f, 1.0f, 0.5f, 0.5f, 0.75f), 0.75f);
}

class SpellEddColourShipped : public ::testing::Test {
protected:
    void SetUp() override {
        if (!table.load(clientDir()) || !reader.load(clientDir()))
            GTEST_SKIP() << "spellsnew.eff/.edd not under " << clientDir();
    }
    EQ::SpellsNewEddColourTable table;
    EQ::SpellsNewEffReader reader;
};

// Every stage lists one colour per texture, in the same order, so texture
// region k always takes the words of the emitter that named it.
TEST_F(SpellEddColourShipped, OneColourPerStageTextureForEveryRecord) {
    for (size_t r = 1; r < reader.recordCount(); ++r) {
        for (int s = 0; s < 3; ++s) {
            ASSERT_EQ(table.stageColours(static_cast<int32_t>(r), s).size(),
                      reader.stageTextures(static_cast<int32_t>(r), s).size())
                << "record " << r << " stage " << s;
        }
    }
}

// The discipline pair the oracle drew red and blue (B1134 P5 / P4), and its
// siblings: same textures, different colour words.
TEST_F(SpellEddColourShipped, TheDisciplineRecordsCarryTheirColours) {
    struct Want { int32_t record; int r, g, b; };
    for (const Want& w : {Want{326, 128, 128, 255}, Want{329, 255, 0, 0},
                          Want{328, 113, 240, 74}, Want{330, 255, 255, 255}}) {
        const auto cs = table.stageColours(w.record, 0);
        ASSERT_EQ(cs.size(), 4u) << w.record;
        const auto tex = reader.stageTextures(w.record, 0);
        for (size_t k = 0; k < cs.size(); ++k) {
            EXPECT_EQ(cs[k].start[0], w.r) << w.record;
            EXPECT_EQ(cs[k].start[1], w.g) << w.record;
            EXPECT_EQ(cs[k].start[2], w.b) << w.record;
            EXPECT_EQ(cs[k].start, cs[k].end) << w.record;
            EXPECT_TRUE(cs[k].additive) << w.record;
            // glowflare02 at peak 0.5, the globe at 0.1 (fade-in 0.5 s).
            EXPECT_FLOAT_EQ(cs[k].peakOpacity, tex[k] == "glowflare02.tga" ? 0.5f : 0.1f)
                << w.record << " " << tex[k];
        }
        EXPECT_EQ(cs[0].isWhite(), w.record == 330);
        EXPECT_TRUE(table.stageColours(w.record, 2).empty()) << "no Target stage";
    }
    // 326 and 329 name the SAME textures: only the colour words tell them apart.
    EXPECT_EQ(reader.stageTextures(326, 0), reader.stageTextures(329, 0));
}

// 93 Burst of Flame (field 145 = record 80) is white in every slot, so B1146
// leaves its draw exactly as it was (the FxHarness gate's spell).
TEST_F(SpellEddColourShipped, BurstOfFlamesRecordIsWhite) {
    for (int s = 0; s < 3; ++s) {
        for (const auto& c : table.stageColours(80, s)) EXPECT_TRUE(c.isWhite()) << "stage " << s;
    }
    EXPECT_EQ(table.stageColours(80, 2).size(), 4u);
}

#if defined(EQT_HAS_GRAPHICS) && defined(EQT_HAS_GLES2)

namespace SpellPresets = EQT::Graphics::Environment::SpellPresets;
using EQT::Graphics::Environment::EmitterConfig;
using EQT::Graphics::Environment::SpellEffectDef;
using EQT::Graphics::Environment::SpellEmitterDef;
using EQT::Graphics::Environment::UnifiedBlendMode;
using EQT::Graphics::Environment::UnifiedParticle;

EQ::SpellsNewEddColour colour(int r, int g, int b, int r1, int g1, int b1, bool additive,
                              float peak, float fadeIn = 0.0f, float fadeOut = 0.0f) {
    EQ::SpellsNewEddColour c;
    c.start = {r, g, b};
    c.end = {r1, g1, b1};
    c.additive = additive;
    c.peakOpacity = peak;
    c.fadeIn = fadeIn;
    c.fadeOut = fadeOut;
    return c;
}

SpellEffectDef oneEmitter() {
    SpellEffectDef def;
    SpellEmitterDef ed;
    ed.config.colorStart = glm::vec4(0.2f, 0.4f, 0.6f, 0.8f);
    ed.config.colorEnd = glm::vec4(0.2f, 0.4f, 0.6f, 0.0f);
    def.emitters.push_back(ed);
    return def;
}

TEST(SpellEddColourPresets, EachRegionTakesItsEmittersWordsAndWhiteStaysInactive) {
    SpellEffectDef def = oneEmitter();
    SpellPresets::applyStageSpriteNames(def, {"glowflare02.tga", "glowflare02.tga", "x.tga"});
    // T441: the resist tint is gone, the style's alphas stay.
    EXPECT_EQ(def.emitters[0].config.colorStart, glm::vec4(1.0f, 1.0f, 1.0f, 0.8f));
    SpellPresets::applyStageEmitterColours(
        def, {colour(255, 0, 0, 255, 0, 0, true, 0.5f), colour(255, 255, 255, 255, 255, 255, true, 1.0f),
              colour(0, 255, 0, 0, 0, 255, false, 0.1f, 0.5f, 0.25f)},
        /*clientAlpha=*/false);
    const EmitterConfig& c = def.emitters[0].config;
    ASSERT_EQ(c.textureRegionCount, 3);
    EXPECT_TRUE(c.regionColours[0].active);
    EXPECT_FALSE(c.regionColours[0].clientAlpha) << "RGB-only (B1146's setting, now test-only)";
    EXPECT_EQ(c.regionColours[0].start, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(c.regionColours[0].additive);
    EXPECT_FLOAT_EQ(c.regionColours[0].peakOpacity, 0.5f);
    EXPECT_FALSE(c.regionColours[1].active) << "RGB-only: a white emitter keeps the style's draw";
    EXPECT_TRUE(c.regionColours[2].active);
    EXPECT_FALSE(c.regionColours[2].additive);
    EXPECT_EQ(c.regionColours[2].end, glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_FLOAT_EQ(c.regionColours[2].fadeIn, 0.5f);
    EXPECT_FLOAT_EQ(c.regionColours[2].fadeOut, 0.25f);
    EXPECT_FALSE(c.regionColours[3].active);

    // A count that does not pair texture k with colour k changes nothing.
    SpellEffectDef other = oneEmitter();
    SpellPresets::applyStageSpriteNames(other, {"glowflare02.tga", "glowflare02.tga"});
    SpellPresets::applyStageEmitterColours(other, {colour(255, 0, 0, 255, 0, 0, true, 0.5f)});
    for (const auto& rc : other.emitters[0].config.regionColours) EXPECT_FALSE(rc.active);

    // B1149 (#469): the production default is the client's alpha, and with it
    // every region is the client's, white included.
    SpellEffectDef prod = oneEmitter();
    SpellPresets::applyStageSpriteNames(prod, {"glowflare02.tga", "glowflare02.tga"});
    SpellPresets::applyStageEmitterColours(
        prod, {colour(255, 0, 0, 255, 0, 0, true, 0.5f), colour(255, 255, 255, 255, 255, 255, true, 1.0f)});
    for (int k = 0; k < 2; ++k) {
        EXPECT_TRUE(prod.emitters[0].config.regionColours[k].active) << k;
        EXPECT_TRUE(prod.emitters[0].config.regionColours[k].clientAlpha) << k;
    }

    // Re-installing a stage's sprites clears the previous stage's words.
    SpellPresets::applyStageSpriteNames(def, {"glowflare02.tga"});
    for (const auto& rc : def.emitters[0].config.regionColours) EXPECT_FALSE(rc.active);
}

// Run the worker until the burst is out and return the alive particles.
std::vector<UnifiedParticle> burst(const SpellEffectDef& def, int frames) {
    EQT::Graphics::SimulationWorker worker;
    EQT::Graphics::SimulationOutput output;
    std::vector<UnifiedParticle> out;
    for (int f = 0; f < frames; ++f) {
        EQT::Graphics::SimulationInput input;
        auto& pi = input.particleInput;
        pi.deltaTime = 1.0f / 60.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 256;
        if (f == 0) {
            EQT::Graphics::ParticleCommandData cmd;
            cmd.type = EQT::Graphics::ParticleCommand::CreateSpellEffect;
            cmd.spellDef = def;
            cmd.preAssignedEffectID = 1;
            pi.commands.push_back(cmd);
        }
        worker.computeOnce(input, output);
        if (output.particleOutput.valid) out = output.particleOutput.renderBuffer;
    }
    return out;
}

SpellEffectDef burstDef() {
    SpellEffectDef def = oneEmitter();
    EmitterConfig& c = def.emitters[0].config;
    c.spawnRate = 0.0f;
    c.burstCount = 20;
    c.emitterLifetime = 3.0f;
    c.lifetimeMin = c.lifetimeMax = 1.0f;
    c.velocityBase = glm::vec3(0.0f);
    c.blendMode = UnifiedBlendMode::ADDITIVE;
    c.textureRegions[0] = 64;
    c.textureRegionCount = 1;
    return def;
}

// B1146's RGB-only setting (test-only since B1149): the colour words' RGB over
// the style's alpha and blend.
TEST(SpellEddColourParticles, AnActiveRegionDrawsTheInterpolatedColourOverTheStylesAlpha) {
    SpellEffectDef plain = burstDef();
    SpellEffectDef def = burstDef();
    SpellPresets::applyStageEmitterColours(
        def, {colour(255, 0, 0, 0, 0, 255, false, 0.5f, 0.0f, 0.4f)}, /*clientAlpha=*/false);
    const auto a = burst(plain, 31);
    const auto b = burst(def, 31);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_FALSE(b.empty());
    for (size_t i = 0; i < b.size(); ++i) {
        const auto& p = b[i];
        EXPECT_FALSE(p.hasEddColour());
        EXPECT_EQ(p.getBlendMode(), UnifiedBlendMode::ADDITIVE) << "+0x68 waits for clientAlpha";
        const float t = p.getNormalizedAge();
        EXPECT_NEAR(p.color.r, 1.0f - t, 1e-4f);
        EXPECT_NEAR(p.color.g, 0.0f, 1e-6f);
        EXPECT_NEAR(p.color.b, t, 1e-4f);
        EXPECT_FLOAT_EQ(p.color.a, a[i].color.a) << "the style's fade";
        EXPECT_NE(glm::vec3(p.color), glm::vec3(a[i].color));
    }
}

TEST(SpellEddColourParticles, OnTheClientsAlphaTheParticleTakesFadeTimesPeakAndTheBlend) {
    SpellEffectDef def = burstDef();
    SpellPresets::applyStageEmitterColours(
        def, {colour(255, 0, 0, 0, 0, 255, true, 0.5f, 0.0f, 0.4f)}, /*clientAlpha=*/true);
    const auto ps = burst(def, 31);  // ~0.5 s of the 1.0 s life
    ASSERT_FALSE(ps.empty());
    for (const auto& p : ps) {
        ASSERT_TRUE(p.hasEddColour());
        EXPECT_EQ(p.getBlendMode(), UnifiedBlendMode::ADDITIVE);
        const float t = p.getNormalizedAge();
        EXPECT_NEAR(p.color.r, 1.0f - t, 1e-4f);
        EXPECT_NEAR(p.color.g, 0.0f, 1e-6f);
        EXPECT_NEAR(p.color.b, t, 1e-4f);
        EXPECT_NEAR(p.color.a, EQ::eddParticleAlpha(p.age, p.maxLifetime - p.age, 0.0f, 0.4f, 0.5f),
                    1e-5f);
        EXPECT_FLOAT_EQ(p.color.a, 0.5f) << "0.5 s left > the 0.4 s fade-out: the peak";
    }

    // +0x68 != 1: the alpha blend.
    SpellEffectDef alphaDef = burstDef();
    SpellPresets::applyStageEmitterColours(alphaDef, {colour(0, 255, 0, 0, 255, 0, false, 1.0f)},
                                           /*clientAlpha=*/true);
    const auto pa = burst(alphaDef, 31);
    ASSERT_FALSE(pa.empty());
    for (const auto& p : pa) {
        EXPECT_TRUE(p.hasEddColour());
        EXPECT_EQ(p.getBlendMode(), UnifiedBlendMode::ALPHA);
        EXPECT_EQ(glm::vec3(p.color), glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

// B1149 (#469): on the client's alpha a WHITE emitter is the client's too: its
// particles keep the white texel colour (vertex RGB 1,1,1) and take the
// client's fade x peak and blend, never the style's alpha ramp. RGB-only
// (clientAlpha false) leaves a white emitter exactly as the style draws it.
TEST(SpellEddColourParticles, AWhiteEmitterIsOnTheClientsAlphaToo) {
    SpellEffectDef plain = burstDef();
    SpellEffectDef white = burstDef();
    SpellPresets::applyStageEmitterColours(white, {colour(255, 255, 255, 255, 255, 255, true, 0.5f)});
    const auto b = burst(white, 31);
    ASSERT_FALSE(b.empty());
    for (const auto& p : b) {
        EXPECT_TRUE(p.hasEddColour());
        EXPECT_EQ(glm::vec3(p.color), glm::vec3(1.0f));
        EXPECT_FLOAT_EQ(p.color.a, 0.5f) << "the peak, not the style's 0.8 -> 0 ramp";
    }
    SpellEffectDef rgbOnly = burstDef();
    SpellPresets::applyStageEmitterColours(rgbOnly, {colour(255, 255, 255, 255, 255, 255, true, 0.5f)},
                                           /*clientAlpha=*/false);
    const auto a = burst(plain, 31);
    const auto c = burst(rgbOnly, 31);
    ASSERT_EQ(a.size(), c.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FALSE(c[i].hasEddColour());
        EXPECT_EQ(a[i].color, c[i].color);
        EXPECT_EQ(a[i].flags, c[i].flags);
    }
}

#endif  // EQT_HAS_GRAPHICS && EQT_HAS_GLES2

}  // namespace
