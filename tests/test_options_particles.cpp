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

// B1165 (#484): the Spell page's "On For:" is wired too (FUN_00491cff); the
// Player page stays unwired (N3).
TEST(OptionsParticles, SpellAndEnvRowsAreWiredPlayerIsNot) {
    for (const char* item :
         {"ODP_ParticlesTabs", "ODP_SpellParticlesOpacitySlider", "ODP_SpellParticlesDensityCombobox",
          "ODP_SpellParticlesNearClipCombobox", "ODP_SpellParticlesFilterCombobox",
          "ODP_EnvironmentParticlesOpacitySlider",
          "ODP_EnvironmentParticlesDensityCombobox", "ODP_EnvironmentParticlesNearClipCombobox"}) {
        ASSERT_NE(OL::findControlClass(item), nullptr) << item;
        EXPECT_EQ(OL::findControlClass(item)->disposition, OL::Disposition::Wired) << item;
    }
    for (const char* item : {"ODP_ActorParticlesOpacitySlider", "ODP_ActorParticlesDensityCombobox",
                             "ODP_ActorParticlesNearClipCombobox", "ODP_ActorParticlesFilterCombobox"}) {
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
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleNearClip)], 1)
        << "B1165: 4.0 is Medium (Near 2 / Medium 4 / Far 6 / Farthest 8)";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleDensity)], 0);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleNearClip)], 0)
        << "not a number: the default, Near (2.0)";
}

// B1165 (#484): the oracle's near-clip table (eqgame.exe.c:245189-245204:
// 0x40000000 / 0x40800000 / 0x40c00000 / 0x41000000) -- the shipped 2.0 is
// NEAR -- and its loader's clamp to [1, 8] (eqgame.exe.c:132626-132637).
TEST(OptionsParticles, TheNearClipTableIsTheOraclesAndTwoIsNear) {
    EXPECT_FLOAT_EQ(Env::nearClipForChoice(0), 2.0f);
    EXPECT_FLOAT_EQ(Env::nearClipForChoice(1), 4.0f);
    EXPECT_FLOAT_EQ(Env::nearClipForChoice(2), 6.0f);
    EXPECT_FLOAT_EQ(Env::nearClipForChoice(3), 8.0f);
    OptionsPanelState s;
    EXPECT_EQ(value(s, OL::Setting::SpellParticleNearClip), 0) << "the default is Near";
    EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleNearClip, 0), "Near");
    UiPreferencesBinding b;
    b.options = &s;
    const char* const ini[4] = {"2.000000", "4.000000", "6.000000", "8.000000"};
    for (int c = 0; c < 4; ++c) {
        setOptionsValue(s, OL::Setting::SpellParticleNearClip, c);
        const std::string text = SerializeUiPreferences(CollectUiPreferences(b, 1));
        EXPECT_NE(text.find(std::string("SpellParticleNearClipPlane=") + ini[c] + "\n"), std::string::npos) << c;
        const UiPreferences p = ParseUiPreferences(text);
        EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleNearClip)], c) << "round trip " << c;
    }
}

// B1165: an off-table ini float is clamped as the loader does and shown as
// the oracle's combobox shows it: the first choice whose value is >= it
// (0x53b93f..0x53b985 near clip, 0x53b994..0x53b9da density).
TEST(OptionsParticles, TheLoaderClampsAndTheComboboxRoundsUp) {
    const UiPreferences p = ParseUiPreferences(
        "[Options]\r\nSpellParticleNearClipPlane=0.500000\r\nEnvironmentParticleNearClipPlane=100.0\r\n"
        "SpellParticleDensity=0.300000\r\nEnvironmentParticleDensity=-1.0\r\nSpellParticleOpacity=1.500000\r\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleNearClip)], 0) << "0.5 -> 1 -> Near";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleNearClip)], 3) << "100 -> 8 -> Farthest";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleDensity)], 2) << "0.3 -> Medium";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleDensity)], 0) << "-1 -> 0 -> Off";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 20) << "1.5 -> 1";
    EXPECT_FLOAT_EQ(Env::clampNearClipIni(3.0f), 3.0f);
    EXPECT_EQ(Env::choiceForIniFloat(Env::kNearClipIniValues, Env::kNearClipChoices, 3.0f), 1);
    EXPECT_EQ(Env::choiceForIniFloat(Env::kNearClipIniValues, Env::kNearClipChoices, 6.0f), 2);
}

// B1165 fix (MR !318 note 7756 finding 3): the oracle APPLIES the clamped ini
// float itself -- the loader stores it as read (eqgame.exe.c:132608-132636)
// and 0x495f29 pushes SpellParticleDensity unrounded into CreateEmitter --
// and only its combobox DISPLAY rounds up to a choice (0x53b93f..0x53b9da).
// So an ini 0.3 density is applied as 0.3 and shown as Medium (0.5).
TEST(OptionsParticles, AnOffTableIniFloatIsAppliedRawAndShownAsTheChoice) {
    const UiPreferences p = ParseUiPreferences(
        "[Options]\r\nSpellParticleDensity=0.300000\r\nSpellParticleNearClipPlane=3.000000\r\n"
        "SpellParticleOpacity=0.330000\r\nEnvironmentParticleDensity=1.000000\r\n");
    const int dens = static_cast<int>(OL::Setting::SpellParticleDensity);
    const int clip = static_cast<int>(OL::Setting::SpellParticleNearClip);
    const int opac = static_cast<int>(OL::Setting::SpellParticleOpacity);
    EXPECT_EQ(p.optionValues[dens], 2) << "0.3 is shown as Medium";
    EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleDensity, p.optionValues[dens]), "Medium");
    EXPECT_EQ(p.optionValues[clip], 1) << "3.0 is shown as Medium (4.0)";
    ASSERT_TRUE(p.optionIniFloats[dens].has_value());
    EXPECT_FLOAT_EQ(*p.optionIniFloats[dens], 0.3f);
    ASSERT_TRUE(p.optionIniFloats[clip].has_value());
    EXPECT_FLOAT_EQ(*p.optionIniFloats[clip], 3.0f);
    EXPECT_FALSE(p.optionIniFloats[static_cast<int>(OL::Setting::EnvParticleDensity)].has_value())
        << "a table value needs no applied float";

    OptionsPanelState s;
    UiPreferencesBinding b;
    b.options = &s;
    ApplyUiPreferences(p, b);
    EQ::SpellFxOptions o = spellFxOptionsFromOptions(s.values, s.iniFloats);
    EXPECT_FLOAT_EQ(o.density, 0.3f) << "applied as 0.3, not the shown 0.5";
    EXPECT_FLOAT_EQ(o.nearClip, 3.0f);
    EXPECT_FLOAT_EQ(o.opacity, 0.33f) << "the slider shows step " << s.values[opac];
    EXPECT_FLOAT_EQ(knob(particleKnobsFromOptions(s.values, s.iniFloats), Env::ParticleFamily::Spell).density, 0.3f);
    EXPECT_FALSE(EQ::IsOracleCaptureSpellFxOptions(o));

    // A save writes the applied float back, so the value survives.
    const std::string t = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(t.find("SpellParticleDensity=0.300000\n"), std::string::npos) << t;
    EXPECT_NE(t.find("SpellParticleNearClipPlane=3.000000\n"), std::string::npos) << t;
    const UiPreferences back = ParseUiPreferences(t);
    EXPECT_TRUE(back.optionValues == p.optionValues);
    EXPECT_TRUE(back.optionIniFloats == p.optionIniFloats);

    // Choosing in the window stores the table value, as the handler does
    // (eqgame.exe.c:245163-245180) -- even re-choosing the shown Medium.
    EXPECT_TRUE(setOptionsValue(s, OL::Setting::SpellParticleDensity, 2));
    o = spellFxOptionsFromOptions(s.values, s.iniFloats);
    EXPECT_FLOAT_EQ(o.density, 0.5f);
    EXPECT_FALSE(s.iniFloats[dens].has_value());
    EXPECT_FLOAT_EQ(o.nearClip, 3.0f) << "the other knobs keep theirs";
}

// B1165 fix: the oracle keeps the SpellParticleCastFilter int RAW (loader
// eqgame.exe.c:132638, no validation) and FUN_00491cff decodes its BITS
// (0x491d0f..0x491d41); the combobox shows the index of the value in
// DAT_00793b4c, index 7 (All spells) when it is not listed (0x53b9e9..0x53ba07).
// So an ini 6 (0x04 | 0x02) behaves as "Raid spells" (4) and is shown as "All spells".
TEST(OptionsParticles, AnOffTableCastFilterIntIsKeptRawAndDecodedByItsBits) {
    const UiPreferences p = ParseUiPreferences("[Options]\r\nSpellParticleCastFilter=6\r\n");
    const int i = static_cast<int>(OL::Setting::SpellParticleCastFilter);
    EXPECT_EQ(p.optionValues[i], 6) << "kept, not reset to the default 24";
    EXPECT_EQ(OL::choiceDisplayIndex(OL::Setting::SpellParticleCastFilter, 6), 7);
    EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleCastFilter, 6), "All spells");
    EXPECT_EQ(OL::choiceDisplayIndex(OL::Setting::SpellParticleCastFilter, 4), 2);

    OptionsPanelState s;
    UiPreferencesBinding b;
    b.options = &s;
    ApplyUiPreferences(p, b);
    const EQ::SpellFxOptions o = spellFxOptionsFromOptions(s.values, s.iniFloats);
    EXPECT_EQ(o.castFilter, 6);
    EQ::SpellFxActor me, groupie, raider, other, npc;
    for (auto* a : {&me, &groupie, &raider, &other, &npc}) a->present = true;
    me.isSelf = true;
    groupie.inGroup = true;
    raider.inRaid = true;
    npc.spawnType = 1;
    for (const auto* c : {&me, &groupie, &raider, &other, &npc}) {
        for (const auto* t : {&me, &groupie, &raider, &other, &npc}) {
            EXPECT_EQ(EQ::SpellParticleCastFilterShows(6, *c, *t),
                      EQ::SpellParticleCastFilterShows(4, *c, *t));
        }
    }
    EXPECT_TRUE(EQ::SpellParticleCastFilterShows(6, raider, npc)) << "a raid member's spell shows";
    EXPECT_FALSE(EQ::SpellParticleCastFilterShows(6, other, npc)) << "a stranger's does not";
    EXPECT_TRUE(EQ::SpellParticleCastFilterShows(24, other, npc)) << "as All spells (24) would show it";

    const std::string t = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(t.find("SpellParticleCastFilter=6\n"), std::string::npos) << t;
    EXPECT_EQ(ParseUiPreferences("[Options]\r\nSpellParticleCastFilter=x\r\n").optionValues[i], 24)
        << "not a number keeps the default";
}

// B1165 (#484): "On For:" is wired -- the eight choices are eqgame's table
// DAT_00793b4c, the default is 24 (All spells), it persists as the int.
TEST(OptionsParticles, TheOnForComboboxIsTheCastFilter) {
    OptionsPanelState s = displayPage();
    const UILayout l = layout800();
    EXPECT_EQ(value(s, OL::Setting::SpellParticleCastFilter), 24);
    EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleCastFilter, 24), "All spells");
    const int combo = controlIndex("ODP_SpellParticlesFilterCombobox");
    ASSERT_GE(combo, 0);
    EXPECT_EQ(OL::kControls[combo].setting, OL::Setting::SpellParticleCastFilter);
    EXPECT_EQ(click(s, centre(OL::controlRect(l, combo))), OL::Setting::SpellParticleCastFilter);
    EXPECT_EQ(value(s, OL::Setting::SpellParticleCastFilter), 1) << "All spells cycles to My spells";
    const int32_t table[8] = {1, 2, 4, 8, 17, 18, 20, 24};
    const char* const text[8] = {"My spells", "Group spells", "Raid spells", "All PCs",
                                 "Me + NPC",  "Group + NPC",  "Raid + NPC",  "All spells"};
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(OL::settingInfo(OL::Setting::SpellParticleCastFilter).choices[i].value, table[i]) << i;
        EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleCastFilter, table[i]), text[i]) << i;
    }
    UiPreferencesBinding b;
    b.options = &s;
    setOptionsValue(s, OL::Setting::SpellParticleCastFilter, 18);
    const std::string t = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(t.find("SpellParticleCastFilter=18\n"), std::string::npos) << t;
    EXPECT_EQ(ParseUiPreferences(t).optionValues[static_cast<int>(OL::Setting::SpellParticleCastFilter)], 18);
    RunOptionsCommand(s, "/option", "spellparticlecastfilter group spells");
    EXPECT_EQ(value(s, OL::Setting::SpellParticleCastFilter), 2);
    EXPECT_EQ(spellFxOptionsFromOptions(s.values).castFilter, 2);
}

// B1165: ShowSpellEffects is a real switch: [Defaults] ShowSpellEffects=1,
// `/showspelleffects <ON | OFF>` (eqgame.exe.c:88027-88054, eqstr 13168-13171),
// and it reaches SpellVisualFX's stage gate (spellFxOptionsFromOptions).
TEST(OptionsParticles, ShowSpellEffectsIsTheSlashCommandsSwitch) {
    OptionsPanelState s;
    EXPECT_EQ(value(s, OL::Setting::ShowSpellEffects), 1);
    EXPECT_TRUE(spellFxOptionsFromOptions(s.values).showSpellEffects);
    auto r = RunOptionsCommand(s, "/showspelleffects", "");
    ASSERT_EQ(r.lines.size(), 3u);
    EXPECT_EQ(r.lines[0], "FORMAT: /showspelleffects <ON | OFF> (default is ON)");
    EXPECT_EQ(r.lines[2], "Spell particle effects are currently ON.");
    EXPECT_TRUE(r.changed.empty());
    r = RunOptionsCommand(s, "/showspelleffects", "OFF");
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(r.lines.back(), "Spell particle effects are currently OFF.");
    EXPECT_EQ(value(s, OL::Setting::ShowSpellEffects), 0);
    EXPECT_FALSE(spellFxOptionsFromOptions(s.values).showSpellEffects);
    UiPreferencesBinding b;
    b.options = &s;
    const std::string t = SerializeUiPreferences(CollectUiPreferences(b, 1));
    EXPECT_NE(t.find("ShowSpellEffects=0\n"), std::string::npos) << t;
    r = RunOptionsCommand(s, "showspelleffects", "banana");  // any non-"off" turns it on
    EXPECT_EQ(value(s, OL::Setting::ShowSpellEffects), 1);
}

// B1165: the Options record reaches SpellVisualFX as the capture's values by default.
TEST(OptionsParticles, TheDefaultsAreTheOracleCapturesIni) {
    OptionsPanelState s;
    const EQ::SpellFxOptions o = spellFxOptionsFromOptions(s.values);
    EXPECT_FLOAT_EQ(o.density, 1.0f);
    EXPECT_FLOAT_EQ(o.opacity, 1.0f);
    EXPECT_FLOAT_EQ(o.nearClip, 2.0f);
    EXPECT_EQ(o.castFilter, 24);
    EXPECT_TRUE(o.showSpellEffects);
    EXPECT_TRUE(EQ::IsOracleCaptureSpellFxOptions(o));
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
