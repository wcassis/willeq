// B995 (#315) -- the Options window's Display > Particles sub-pages.
//
// What is pinned:
//   * the sub-tab strip (ODP_ParticlesTabs) is WIRED and switches between the
//     Spell / Env / Player pages; a page's controls are drawn and hit only
//     while its tab is active;
//   * the Spell and Env pages' Opacity / Density / Near Clip rows are WIRED,
//     persist under eqclient.ini's own keys in its own float format, and an
//     official [Options] section loads; the Player page and the two filter
//     rows stay unwired with their reason;
//   * each knob reaches the particle systems: density scales what the
//     simulation worker spawns (0 spawns none), opacity and near clip are
//     applied per particle at draw by the one rule both renderers share;
//   * no knob raises anything: the defaults are the identity except the
//     2-unit near clip, and the most a knob can do is the identity.

#include <gtest/gtest.h>

#include "client/graphics/environment/particle_options.h"
#include "client/graphics/environment/unified_particle.h"
#include "client/graphics/simulation_worker.h"
#include "client/graphics/ui/options_classification.h"
#include "client/graphics/ui/options_command.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_preferences.h"
#include "particle_test_seed.h"

#include <string>

using namespace EQT::Graphics;
namespace OL = EQT::Graphics::OptionsLayout;
namespace Env = EQT::Graphics::Environment;

namespace {

UILayout layout800() {
    UILayout l;
    l.computeLayout(800, 600);
    return l;
}

int controlIndex(const char* xmlItem) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        if (std::string(OL::kControls[i].xmlItem) == xmlItem) return i;
    }
    return -1;
}

irr::core::vector2di centre(const irr::core::rect<irr::s32>& r) {
    return irr::core::vector2di((r.UpperLeftCorner.X + r.LowerRightCorner.X) / 2,
                                (r.UpperLeftCorner.Y + r.LowerRightCorner.Y) / 2);
}

OL::Setting click(OptionsPanelState& s, const irr::core::vector2di& p) {
    StaticUIInput input;
    OL::Setting changed = OL::Setting::None;
    input.processOptionsInput(p.X, p.Y, true, layout800(), s, changed);
    return changed;
}

OptionsPanelState displayPage() {
    OptionsPanelState s;
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::Tab::Display);
    return s;
}

int value(const OptionsPanelState& s, OL::Setting setting) { return optionsSettingStep(s, setting); }

const Env::ParticleFamilyKnobs& knob(const Env::ParticleKnobs& k, Env::ParticleFamily f) {
    return k[static_cast<size_t>(f)];
}

}  // namespace

// ===========================================================================
// The page
// ===========================================================================

TEST(OptionsParticles, SpellAndEnvRowsAreWiredPlayerAndFiltersAreNot) {
    for (const char* item :
         {"ODP_ParticlesTabs", "ODP_SpellParticlesOpacitySlider", "ODP_SpellParticlesDensityCombobox",
          "ODP_SpellParticlesNearClipCombobox", "ODP_EnvironmentParticlesOpacitySlider",
          "ODP_EnvironmentParticlesDensityCombobox", "ODP_EnvironmentParticlesNearClipCombobox"}) {
        ASSERT_NE(OL::findControlClass(item), nullptr) << item;
        EXPECT_EQ(OL::findControlClass(item)->disposition, OL::Disposition::Wired) << item;
    }
    for (const char* item : {"ODP_ActorParticlesOpacitySlider", "ODP_ActorParticlesDensityCombobox",
                             "ODP_ActorParticlesNearClipCombobox", "ODP_ActorParticlesFilterCombobox",
                             "ODP_SpellParticlesFilterCombobox"}) {
        const int i = controlIndex(item);
        ASSERT_GE(i, 0) << item;
        EXPECT_EQ(OL::kControls[i].setting, OL::Setting::None) << item;
        EXPECT_NE(OL::findControlClass(item)->disposition, OL::Disposition::Wired) << item;
    }
}

TEST(OptionsParticles, TheSubTabsSwitchWhichPageIsHit) {
    OptionsPanelState s = displayPage();
    const UILayout l = layout800();
    const int tabs = controlIndex("ODP_ParticlesTabs");
    const int spellSlider = controlIndex("ODP_SpellParticlesOpacitySlider");
    const int envSlider = controlIndex("ODP_EnvironmentParticlesOpacitySlider");
    ASSERT_GE(envSlider, 0);
    // The two sliders share the oracle's rect; the active page owns it.
    const auto p = centre(OL::controlRect(l, envSlider));
    EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), spellSlider);
    // Click "Env" in the strip.
    EXPECT_EQ(click(s, centre(particleSubTabRect(l, tabs, 1))), OL::Setting::None);
    EXPECT_EQ(s.particlesTab, 1);
    EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), envSlider);
    click(s, centre(particleSubTabRect(l, tabs, 2)));
    EXPECT_EQ(s.particlesTab, 2);
    EXPECT_EQ(hitTestOptionsControl(l, s, p.X, p.Y), -1) << "the Player page is greyed (N3)";
    // A click inside the box but below the strip is not the strip's.
    const auto box = OL::controlRect(l, tabs);
    EXPECT_NE(hitTestOptionsControl(l, s, box.UpperLeftCorner.X + 2, box.LowerRightCorner.Y - 3), tabs);
}

TEST(OptionsParticles, ClicksMoveTheKnobsOfTheActivePageOnly) {
    OptionsPanelState s = displayPage();
    const UILayout l = layout800();
    s.particlesTab = 1;  // Env
    const auto density = OL::controlRect(l, controlIndex("ODP_EnvironmentParticlesDensityCombobox"));
    EXPECT_EQ(click(s, centre(density)), OL::Setting::EnvParticleDensity);
    EXPECT_EQ(value(s, OL::Setting::EnvParticleDensity), 0) << "High cycles to Off";
    EXPECT_EQ(value(s, OL::Setting::SpellParticleDensity), 3) << "the Spell page is untouched";
    const auto slider = OL::controlRect(l, controlIndex("ODP_EnvironmentParticlesOpacitySlider"));
    EXPECT_EQ(click(s, irr::core::vector2di(slider.UpperLeftCorner.X, centre(slider).Y)),
              OL::Setting::EnvParticleOpacity);
    EXPECT_EQ(value(s, OL::Setting::EnvParticleOpacity), 0);
    EXPECT_EQ(optionsSettingText(s, OL::Setting::EnvParticleOpacity), "0 %");
}

// ===========================================================================
// Persistence and /option
// ===========================================================================

TEST(OptionsParticles, KnobsPersistUnderEqclientKeysAsFloats) {
    OptionsPanelState s;
    UiPreferencesBinding b;
    b.options = &s;
    std::string text = SerializeUiPreferences(CollectUiPreferences(b, 1));
    // The defaults are eqclient.ini's own lines, byte for byte.
    EXPECT_NE(text.find("SpellParticleOpacity=1.000000\n"), std::string::npos) << text;
    EXPECT_NE(text.find("SpellParticleDensity=1.000000\n"), std::string::npos);
    EXPECT_NE(text.find("SpellParticleNearClipPlane=2.000000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleOpacity=1.000000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleDensity=1.000000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleNearClipPlane=2.000000\n"), std::string::npos);
    EXPECT_EQ(text.find("ActorParticle"), std::string::npos) << "N3: nothing behind them";

    setOptionsValue(s, OL::Setting::SpellParticleOpacity, 10);
    setOptionsValue(s, OL::Setting::EnvParticleNearClip, 3);
    text = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(text.find("SpellParticleOpacity=0.500000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleNearClipPlane=8.000000\n"), std::string::npos);
    const UiPreferences p = ParseUiPreferences(text);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 10);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleNearClip)], 3);
}

TEST(OptionsParticles, AnOfficialOptionsSectionLoads) {
    const UiPreferences p = ParseUiPreferences(
        "[Options]\r\nSpellParticleOpacity=0.250000\r\nSpellParticleDensity=0.500000\r\n"
        "EnvironmentParticleNearClipPlane=4.000000\r\nEnvironmentParticleDensity=0.000000\r\n"
        "SpellParticleNearClipPlane=banana\r\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 5);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleDensity)], 2);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleNearClip)], 2);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleDensity)], 0);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleNearClip)], 1)
        << "not a number: the default";
}

TEST(OptionsParticles, SlashOptionSetsAKnobByChoiceText) {
    OptionsPanelState s;
    auto r = RunOptionsCommand(s, "/option", "spellparticledensity low");
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(value(s, OL::Setting::SpellParticleDensity), 1);
    r = RunOptionsCommand(s, "/option", "envparticleopacity 4");
    EXPECT_EQ(value(s, OL::Setting::EnvParticleOpacity), 4);
}

// ===========================================================================
// The knobs reach the particle systems
// ===========================================================================

TEST(ParticleKnobs, TheOptionsMapOntoTheKnobsAndTheDefaultsAreAlmostTheIdentity) {
    OptionsPanelState s;
    const Env::ParticleKnobs k = particleKnobsFromOptions(s.values);
    for (auto f : {Env::ParticleFamily::Spell, Env::ParticleFamily::Environment}) {
        EXPECT_FLOAT_EQ(knob(k, f).opacity, 1.0f);
        EXPECT_FLOAT_EQ(knob(k, f).density, 1.0f);
        EXPECT_FLOAT_EQ(knob(k, f).nearClip, 2.0f);
    }
    EXPECT_FLOAT_EQ(knob(k, Env::ParticleFamily::Actor).nearClip, 0.0f);
    setOptionsValue(s, OL::Setting::SpellParticleDensity, 1);
    setOptionsValue(s, OL::Setting::EnvParticleOpacity, 5);
    const Env::ParticleKnobs k2 = particleKnobsFromOptions(s.values);
    EXPECT_FLOAT_EQ(knob(k2, Env::ParticleFamily::Spell).density, 0.25f);
    EXPECT_FLOAT_EQ(knob(k2, Env::ParticleFamily::Environment).opacity, 0.25f);
    EXPECT_FLOAT_EQ(knob(k2, Env::ParticleFamily::Environment).density, 1.0f);
}

TEST(ParticleKnobs, TheDrawRuleScalesOpacityAndCullsNearTheCamera) {
    Env::ParticleKnobs k = Env::identityParticleKnobs();
    auto& spell = k[static_cast<size_t>(Env::ParticleFamily::Spell)];
    spell.opacity = 0.5f;
    spell.nearClip = 4.0f;
    const glm::vec3 cam(0.0f);
    glm::vec4 c(1.0f, 0.5f, 0.25f, 0.8f);
    // Additive: the light is the colour, so RGB scales too.
    EXPECT_TRUE(Env::applyParticleKnobs(k, Env::ParticleFamily::Spell, true, glm::vec3(10, 0, 0), cam, c));
    EXPECT_FLOAT_EQ(c.r, 0.5f);
    EXPECT_FLOAT_EQ(c.a, 0.4f);
    // Alpha blend: only alpha.
    c = glm::vec4(1.0f, 0.5f, 0.25f, 0.8f);
    EXPECT_TRUE(Env::applyParticleKnobs(k, Env::ParticleFamily::Spell, false, glm::vec3(10, 0, 0), cam, c));
    EXPECT_FLOAT_EQ(c.r, 1.0f);
    EXPECT_FLOAT_EQ(c.a, 0.4f);
    // Nearer than the clip: not drawn. Another family: untouched.
    EXPECT_FALSE(Env::applyParticleKnobs(k, Env::ParticleFamily::Spell, true, glm::vec3(3, 0, 0), cam, c));
    c = glm::vec4(1.0f);
    EXPECT_TRUE(Env::applyParticleKnobs(k, Env::ParticleFamily::Environment, true, glm::vec3(1, 0, 0), cam, c));
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(ParticleKnobs, AParticleCarriesItsFamily) {
    Env::UnifiedParticle p;
    p.setBlendMode(Env::UnifiedBlendMode::ALPHA);
    p.setAlive(true);
    EXPECT_EQ(p.getFamily(), Env::ParticleFamily::Environment);
    p.setFamily(Env::ParticleFamily::Spell);
    EXPECT_EQ(p.getFamily(), Env::ParticleFamily::Spell);
    EXPECT_TRUE(p.isAlive());
    EXPECT_EQ(p.getBlendMode(), Env::UnifiedBlendMode::ALPHA) << "the family bits leave the others alone";
}

namespace {

// Two worker frames over the zone's fire emitters with `envDensity`.
size_t fireParticles(float envDensity) {
    SimulationWorker worker;
    SimulationOutput out;
    size_t n = 0;
    for (int frame = 0; frame < 30; ++frame) {
        SimulationInput in;
        auto& pi = in.particleInput;
        pi.deltaTime = 1.0f / 30.0f;
        pi.unifiedRendererInitialized = true;
        pi.rngSeed = particle_test::kSeed;  // B1169: fixed draws (tests/particle_test_seed.h)
        pi.poolSize = 1024;
        pi.density[static_cast<size_t>(Env::ParticleFamily::Environment)] = envDensity;
        if (frame == 0) {
            ParticleCommandData cmd;
            cmd.type = ParticleCommand::CreateFireEmitters;
            for (int i = 0; i < 4; ++i) {
                cmd.firePositions.push_back(glm::vec3(10.0f * i, 0.0f, 0.0f));
                cmd.fireRadii.push_back(200.0f);  // campfires
            }
            pi.commands.push_back(cmd);
        }
        worker.computeOnce(in, out);
    }
    for (const auto& p : out.particleOutput.renderBuffer) {
        EXPECT_EQ(p.getFamily(), Env::ParticleFamily::Environment);
        ++n;
    }
    return n;
}

}  // namespace

TEST(ParticleKnobs, DensityScalesWhatTheWorkerSpawnsAndOffSpawnsNothing) {
    const size_t full = fireParticles(1.0f);
    const size_t quarter = fireParticles(0.25f);
    const size_t off = fireParticles(0.0f);
    EXPECT_GT(full, 20u);
    EXPECT_LT(quarter, full);
    EXPECT_GT(quarter, 0u);
    EXPECT_EQ(off, 0u);
    EXPECT_EQ(Env::scaledCount(10, 1.0f), 10);
    EXPECT_EQ(Env::scaledCount(10, 0.25f), 3);
    EXPECT_EQ(Env::scaledCount(10, 0.0f), 0);
}
