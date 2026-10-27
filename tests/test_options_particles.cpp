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
#include "client/graphics/ui/eqclient_ini.h"
#include "common/willeq_paths.h"
#include "particle_test_seed.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>

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

// B1175 (#494): the particle keys persist in the global eqclient.ini
// [Defaults] (eqclient_ini.h); a fresh file's text for `s`.
std::string eqclientText(const OptionsPanelState& s) {
    return UpdateEqClientIniText("", CollectEqClientIni(s));
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
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
    std::string text = eqclientText(s);
    EXPECT_EQ(text.rfind("[Defaults]\n", 0), 0u) << text;
    // The defaults are eqclient.ini's own lines, byte for byte.
    EXPECT_NE(text.find("SpellParticleOpacity=1.000000\n"), std::string::npos) << text;
    EXPECT_NE(text.find("SpellParticleDensity=1.000000\n"), std::string::npos);
    EXPECT_NE(text.find("SpellParticleNearClipPlane=2.000000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleOpacity=1.000000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleDensity=1.000000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleNearClipPlane=2.000000\n"), std::string::npos);
    EXPECT_EQ(text.find("ActorParticle"), std::string::npos) << "N3: nothing behind them";

    setOptionsValue(s, OL::Setting::SpellParticleOpacity, 50);
    setOptionsValue(s, OL::Setting::EnvParticleNearClip, 3);
    text = eqclientText(s);
    EXPECT_NE(text.find("SpellParticleOpacity=0.500000\n"), std::string::npos);
    EXPECT_NE(text.find("EnvironmentParticleNearClipPlane=8.000000\n"), std::string::npos);
    const EqClientIni p = ParseEqClientIni(text);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 50);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleNearClip)], 3);
}

TEST(OptionsParticles, AnOfficialDefaultsSectionLoads) {
    const EqClientIni p = ParseEqClientIni(
        "[Defaults]\r\nSpellParticleOpacity=0.250000\r\nSpellParticleDensity=0.500000\r\n"
        "EnvironmentParticleNearClipPlane=4.000000\r\nEnvironmentParticleDensity=0.000000\r\n"
        "SpellParticleNearClipPlane=banana\r\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 25);
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
    EXPECT_FLOAT_EQ(OL::settingTableFloat(OL::Setting::SpellParticleNearClip, 0), 2.0f);
    EXPECT_FLOAT_EQ(OL::settingTableFloat(OL::Setting::SpellParticleNearClip, 1), 4.0f);
    EXPECT_FLOAT_EQ(OL::settingTableFloat(OL::Setting::SpellParticleNearClip, 2), 6.0f);
    EXPECT_FLOAT_EQ(OL::settingTableFloat(OL::Setting::SpellParticleNearClip, 3), 8.0f);
    OptionsPanelState s;
    EXPECT_EQ(value(s, OL::Setting::SpellParticleNearClip), 0) << "the default is Near";
    EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleNearClip, 0), "Near");
    const char* const ini[4] = {"2.000000", "4.000000", "6.000000", "8.000000"};
    for (int c = 0; c < 4; ++c) {
        setOptionsValue(s, OL::Setting::SpellParticleNearClip, c);
        const std::string text = eqclientText(s);
        EXPECT_NE(text.find(std::string("SpellParticleNearClipPlane=") + ini[c] + "\n"), std::string::npos) << c;
        const EqClientIni p = ParseEqClientIni(text);
        EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleNearClip)], c) << "round trip " << c;
    }
}

// B1165: an off-table ini float is clamped as the loader does and shown as
// the oracle's combobox shows it: the first choice whose value is >= it
// (0x53b93f..0x53b985 near clip, 0x53b994..0x53b9da density).
TEST(OptionsParticles, TheLoaderClampsAndTheComboboxRoundsUp) {
    const EqClientIni p = ParseEqClientIni(
        "[Defaults]\r\nSpellParticleNearClipPlane=0.500000\r\nEnvironmentParticleNearClipPlane=100.0\r\n"
        "SpellParticleDensity=0.300000\r\nEnvironmentParticleDensity=-1.0\r\nSpellParticleOpacity=1.500000\r\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleNearClip)], 0) << "0.5 -> 1 -> Near";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleNearClip)], 3) << "100 -> 8 -> Farthest";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleDensity)], 2) << "0.3 -> Medium";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleDensity)], 0) << "-1 -> 0 -> Off";
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 100) << "1.5 -> 1";
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
    const EqClientIni p = ParseEqClientIni(
        "[Defaults]\r\nSpellParticleDensity=0.300000\r\nSpellParticleNearClipPlane=3.000000\r\n"
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
    ApplyEqClientIni(p, s);
    EQ::SpellFxOptions o = spellFxOptionsFromOptions(s.values, s.iniFloats);
    EXPECT_FLOAT_EQ(o.density, 0.3f) << "applied as 0.3, not the shown 0.5";
    EXPECT_FLOAT_EQ(o.nearClip, 3.0f);
    EXPECT_FLOAT_EQ(o.opacity, 0.33f) << "the slider shows step " << s.values[opac];
    EXPECT_FLOAT_EQ(knob(particleKnobsFromOptions(s.values, s.iniFloats), Env::ParticleFamily::Spell).density, 0.3f);
    EXPECT_FALSE(EQ::IsOracleCaptureSpellFxOptions(o));

    // A save writes the applied float back, so the value survives.
    const std::string t = eqclientText(s);
    EXPECT_NE(t.find("SpellParticleDensity=0.300000\n"), std::string::npos) << t;
    EXPECT_NE(t.find("SpellParticleNearClipPlane=3.000000\n"), std::string::npos) << t;
    const EqClientIni back = ParseEqClientIni(t);
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
    const EqClientIni p = ParseEqClientIni("[Defaults]\r\nSpellParticleCastFilter=6\r\n");
    const int i = static_cast<int>(OL::Setting::SpellParticleCastFilter);
    EXPECT_EQ(p.optionValues[i], 6) << "kept, not reset to the default 24";
    EXPECT_EQ(OL::choiceDisplayIndex(OL::Setting::SpellParticleCastFilter, 6), 7);
    EXPECT_STREQ(OL::choiceText(OL::Setting::SpellParticleCastFilter, 6), "All spells");
    EXPECT_EQ(OL::choiceDisplayIndex(OL::Setting::SpellParticleCastFilter, 4), 2);

    OptionsPanelState s;
    ApplyEqClientIni(p, s);
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

    const std::string t = eqclientText(s);
    EXPECT_NE(t.find("SpellParticleCastFilter=6\n"), std::string::npos) << t;
    EXPECT_EQ(ParseEqClientIni("[Defaults]\r\nSpellParticleCastFilter=x\r\n").optionValues[i], 24)
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
    setOptionsValue(s, OL::Setting::SpellParticleCastFilter, 18);
    const std::string t = eqclientText(s);
    EXPECT_NE(t.find("SpellParticleCastFilter=18\n"), std::string::npos) << t;
    EXPECT_EQ(ParseEqClientIni(t).optionValues[static_cast<int>(OL::Setting::SpellParticleCastFilter)], 18);
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
    const std::string t = eqclientText(s);
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
    setOptionsValue(s, OL::Setting::EnvParticleOpacity, 25);
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

// ===========================================================================
// B1175 (#494): the opacity sliders and the ini file, as the client has them
// ===========================================================================

namespace {

const OL::Setting kOpacitySliders[] = {OL::Setting::SpellParticleOpacity,
                                       OL::Setting::EnvParticleOpacity};

std::string percentF(float f) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%f", static_cast<double>(f));
    return buf;
}

}  // namespace

// Ask 1: every particle opacity slider fable wires has the client's 101
// positions (SetNumTicks(0x65), 0x541032..0x541060); position p applies
// p x 0.01f (the handler, eqgame.exe.c:245434, 0x640038 = 0x3c23d70a) and
// labels "p %" ("%d %c" with '%', 0x65944c). Per slider, every position:
// position -> value -> label, and the ini text the handler writes ("%f").
TEST(OptionsParticlesB1175, EachOpacitySliderIs101PositionsOfPointZeroOne) {
    EXPECT_EQ(OL::kParticleOpacitySliderPositions, 101);
    float hundredth = 0.0f;
    const uint32_t bits = 0x3c23d70au;
    std::memcpy(&hundredth, &bits, sizeof(hundredth));
    for (const OL::Setting slider : kOpacitySliders) {
        const auto& info = OL::settingInfo(slider);
        EXPECT_EQ(info.maxValue, 100) << info.name;
        EXPECT_EQ(info.defaultValue, 100) << info.name << ": the capture's 1.000000";
        EXPECT_EQ(OL::sliderSteps(slider), 100) << info.name;
        EXPECT_FALSE(OL::isValidSettingValue(slider, 101)) << info.name;
        for (int p = 0; p <= 100; ++p) {
            OptionsPanelState s;
            ASSERT_TRUE(setOptionsValue(s, slider, p) || p == 100) << info.name << " " << p;
            const float expect = static_cast<float>(p) * hundredth;
            const auto k = particleKnobsFromOptions(s.values, s.iniFloats);
            const auto fam = slider == OL::Setting::SpellParticleOpacity ? Env::ParticleFamily::Spell
                                                                         : Env::ParticleFamily::Environment;
            EXPECT_EQ(knob(k, fam).opacity, expect) << info.name << " position " << p;
            EXPECT_EQ(OL::settingTableFloat(slider, p), expect) << p;
            EXPECT_EQ(optionsSettingText(s, slider), std::to_string(p) + " %") << info.name;
            EXPECT_EQ(OL::opacitySliderLabel(p), std::to_string(p) + " %");
            const std::string ini = OL::settingIniText(slider, p, std::nullopt);
            EXPECT_EQ(ini, percentF(expect)) << info.name << " position " << p;
            // ... and the client's own read of that text puts the slider back on p.
            int shown = -1;
            std::optional<float> applied;
            ASSERT_TRUE(OL::parseSettingIniFloat(slider, ini, shown, applied)) << ini;
            EXPECT_EQ(shown, p) << info.name << " " << ini;
        }
    }
    // The two ends and a few names, spelled out.
    EXPECT_EQ(OL::settingIniText(OL::Setting::SpellParticleOpacity, 50, std::nullopt), "0.500000");
    EXPECT_EQ(OL::settingIniText(OL::Setting::SpellParticleOpacity, 100, std::nullopt), "1.000000");
    EXPECT_EQ(OL::settingIniText(OL::Setting::EnvParticleOpacity, 0, std::nullopt), "0.000000");
    EXPECT_EQ(OL::settingIniText(OL::Setting::EnvParticleOpacity, 1, std::nullopt), "0.010000");
    // The Actor slider (also 101 positions in the client) stays unwired: N3.
    EXPECT_EQ(OL::kControls[controlIndex("ODP_ActorParticlesOpacitySlider")].setting, OL::Setting::None);
}

// The client's float -> position (FUN_0053ac98(f x 100.0), eqgame.exe.c:243666):
// floor when the fraction is below 0.5 (0x6410bc), else ceil; SetValue clamps.
TEST(OptionsParticlesB1175, TheSliderPositionOfAnIniFloatIsTheClientsRounding) {
    EXPECT_EQ(OL::opacitySliderPosition(0.0f), 0);
    EXPECT_EQ(OL::opacitySliderPosition(1.0f), 100);
    EXPECT_EQ(OL::opacitySliderPosition(0.254f), 25);
    EXPECT_EQ(OL::opacitySliderPosition(0.255f), 26) << "25.5 -> ceil";
    EXPECT_EQ(OL::opacitySliderPosition(0.33f), 33);
    EXPECT_EQ(OL::opacitySliderPosition(0.996f), 100);
    EXPECT_EQ(OL::opacitySliderPosition(2.0f), 100) << "SetValue clamps to ticks - 1";
    EXPECT_EQ(OL::opacitySliderPosition(-0.5f), 0);
    // An ini value between positions is shown on its position and APPLIED raw (B1165).
    const EqClientIni p = ParseEqClientIni("[Defaults]\nSpellParticleOpacity=0.333000\n");
    const int i = static_cast<int>(OL::Setting::SpellParticleOpacity);
    EXPECT_EQ(p.optionValues[i], 33);
    ASSERT_TRUE(p.optionIniFloats[i].has_value());
    EXPECT_FLOAT_EQ(*p.optionIniFloats[i], 0.333f);
    OptionsPanelState s;
    ApplyEqClientIni(p, s);
    EXPECT_EQ(optionsSettingText(s, OL::Setting::SpellParticleOpacity), "33 %");
    EXPECT_FLOAT_EQ(spellFxOptionsFromOptions(s.values, s.iniFloats).opacity, 0.333f);
}

// The slider geometry: both ends reachable, and the knob moves with the
// position over the slider's 100 px (EQUI_OptionsWindow.xml <CX>100</CX>).
TEST(OptionsParticlesB1175, TheOpacitySliderSpansItsHundredPositions) {
    const UILayout l = layout800();
    const int i = controlIndex("ODP_SpellParticlesOpacitySlider");
    ASSERT_GE(i, 0);
    const auto r = OL::controlRect(l, i);
    OptionsPanelState s = displayPage();
    EXPECT_EQ(click(s, irr::core::vector2di(r.UpperLeftCorner.X, centre(r).Y)),
              OL::Setting::SpellParticleOpacity);
    EXPECT_EQ(value(s, OL::Setting::SpellParticleOpacity), 0);
    EXPECT_EQ(optionsSettingText(s, OL::Setting::SpellParticleOpacity), "0 %");
    click(s, irr::core::vector2di(r.LowerRightCorner.X - 1, centre(r).Y));
    EXPECT_EQ(value(s, OL::Setting::SpellParticleOpacity), 100);
    EXPECT_EQ(optionsSettingText(s, OL::Setting::SpellParticleOpacity), "100 %");
    click(s, centre(OL::sliderKnobRect(l, i, 50, 100)));
    EXPECT_EQ(value(s, OL::Setting::SpellParticleOpacity), 50);
    int last = -1;
    for (int p = 0; p <= 100; ++p) {
        const int x = OL::sliderKnobRect(l, i, p, 100).UpperLeftCorner.X;
        EXPECT_GE(x, last) << p;
        last = x;
    }
    EXPECT_EQ(OL::sliderKnobRect(l, i, 100, 100).LowerRightCorner.X, r.LowerRightCorner.X);
    // The T434 sliders keep their 0..20.
    EXPECT_EQ(OL::sliderSteps(OL::Setting::MusicVolume), OL::SLIDER_STEPS);
    EXPECT_EQ(OL::sliderSteps(OL::Setting::MouseSensitivity), 20);
}

// Ask 2: every particle key the window wires, and ShowSpellEffects, is the
// client's eqclient.ini [Defaults] key (loader eqgame.exe.c:132608-132671 and
// :126007; writers :245163-245204, :245310, :245434-245467) -- NOT a
// per-character UI_<Char>_<server>.ini [Options] key.
TEST(OptionsParticlesB1175, TheParticleKeysAreEqclientIniDefaultsNotPerCharacter) {
    const OL::Setting rows[] = {
        OL::Setting::SpellParticleOpacity, OL::Setting::SpellParticleDensity,
        OL::Setting::SpellParticleNearClip, OL::Setting::SpellParticleCastFilter,
        OL::Setting::ShowSpellEffects,     OL::Setting::EnvParticleOpacity,
        OL::Setting::EnvParticleDensity,   OL::Setting::EnvParticleNearClip};
    const char* const keys[] = {
        "SpellParticleOpacity", "SpellParticleDensity", "SpellParticleNearClipPlane",
        "SpellParticleCastFilter", "ShowSpellEffects", "EnvironmentParticleOpacity",
        "EnvironmentParticleDensity", "EnvironmentParticleNearClipPlane"};
    int eqclientRows = 0;
    for (int i = 0; i < OL::kSettingCount; ++i)
        if (OL::isEqClientIniSetting(static_cast<OL::Setting>(i))) ++eqclientRows;
    EXPECT_EQ(eqclientRows, 8) << "exactly these eight";
    for (int n = 0; n < 8; ++n) {
        const auto& info = OL::settingInfo(rows[n]);
        EXPECT_STREQ(info.iniKey, keys[n]);
        ASSERT_NE(info.eqclientSection, nullptr) << keys[n];
        EXPECT_STREQ(info.eqclientSection, "Defaults") << keys[n];
    }
    // The per-character file neither writes nor reads them...
    OptionsPanelState s;
    setOptionsValue(s, OL::Setting::SpellParticleOpacity, 40);
    setOptionsValue(s, OL::Setting::ShowSpellEffects, 0);
    UiPreferencesBinding b;
    b.options = &s;
    const std::string perChar = SerializeUiPreferences(CollectUiPreferences(b, 1));
    for (const char* k : keys) EXPECT_EQ(perChar.find(k), std::string::npos) << k << "\n" << perChar;
    EXPECT_NE(perChar.find("PCNames="), std::string::npos) << "the per-character [Options] keys stay";
    // ... so an OLD per-character [Options] value is ignored, not migrated:
    // the client has no such key there (eqclient_ini.h, NO MIGRATION).
    const UiPreferences old = ParseUiPreferences(
        "[Options]\nSpellParticleOpacity=0.250000\nShowSpellEffects=0\nSpellParticleCastFilter=1\n");
    OptionsPanelState fresh;
    UiPreferencesBinding fb;
    fb.options = &fresh;
    ApplyUiPreferences(old, fb);
    EXPECT_EQ(value(fresh, OL::Setting::SpellParticleOpacity), 100);
    EXPECT_EQ(value(fresh, OL::Setting::ShowSpellEffects), 1);
    EXPECT_EQ(value(fresh, OL::Setting::SpellParticleCastFilter), 24);
    // ... while the global file carries them under [Defaults].
    const std::string global = eqclientText(s);
    EXPECT_EQ(global.rfind("[Defaults]\n", 0), 0u) << global;
    EXPECT_NE(global.find("\nSpellParticleOpacity=0.400000\n"), std::string::npos) << global;
    EXPECT_NE(global.find("\nShowSpellEffects=0\n"), std::string::npos) << global;
}

// Acceptance B: the lane-5 capture's own eqclient.ini (review/B1159 rig
// m_b1159_rig/eqclient-after.ini = review/B1130/oracle/e17/eqclient-after.ini,
// md5 453da624da093a2aa62aa71d4b2ca8e2, copied to tests/data/b1175/) loads to
// its lines 63, 88-89 and 106-110, and writing those same values back leaves
// the file byte-identical (CRLF, every other section and key kept).
TEST(OptionsParticlesB1175, TheCaptureEqclientIniLoads) {
    const std::string text = readFile(WILLEQ_REPO_PATH("tests/data/b1175/eqclient-after.ini"));
    ASSERT_GT(text.size(), 9000u) << "fixture tests/data/b1175/eqclient-after.ini unreadable";
    const EqClientIni p = ParseEqClientIni(text);
    auto v = [&](OL::Setting st) { return p.optionValues[static_cast<int>(st)]; };
    auto raw = [&](OL::Setting st) { return p.optionIniFloats[static_cast<int>(st)].has_value(); };
    EXPECT_EQ(v(OL::Setting::ShowSpellEffects), 1);          // line 63  ShowSpellEffects=1
    EXPECT_EQ(v(OL::Setting::SpellParticleOpacity), 100);    // line 88  1.000000
    EXPECT_EQ(v(OL::Setting::EnvParticleOpacity), 100);      // line 89  1.000000
    EXPECT_EQ(v(OL::Setting::SpellParticleDensity), 3);      // line 106 1.000000 = High
    EXPECT_EQ(v(OL::Setting::SpellParticleNearClip), 0);     // line 107 2.000000 = Near
    EXPECT_EQ(v(OL::Setting::SpellParticleCastFilter), 24);  // line 108 24
    EXPECT_EQ(v(OL::Setting::EnvParticleDensity), 3);        // line 109 1.000000
    EXPECT_EQ(v(OL::Setting::EnvParticleNearClip), 0);       // line 110 2.000000
    for (OL::Setting st : {OL::Setting::SpellParticleOpacity, OL::Setting::EnvParticleOpacity,
                           OL::Setting::SpellParticleDensity, OL::Setting::SpellParticleNearClip,
                           OL::Setting::EnvParticleDensity, OL::Setting::EnvParticleNearClip})
        EXPECT_FALSE(raw(st)) << "every capture value is a table value";
    EXPECT_TRUE(p == EqClientIni{}) << "the capture IS fable's default";
    OptionsPanelState s;
    ApplyEqClientIni(p, s);
    EXPECT_TRUE(EQ::IsOracleCaptureSpellFxOptions(spellFxOptionsFromOptions(s.values, s.iniFloats)));

    EXPECT_EQ(UpdateEqClientIniText(text, p), text) << "a save of the same values changes no byte";

    // A change rewrites only its own line, in place, with the file's CRLF;
    // ActorParticleOpacity (line 90) and the legacy [Options] ParticleDensity stay.
    setOptionsValue(s, OL::Setting::SpellParticleOpacity, 37);
    const std::string after = UpdateEqClientIniText(text, CollectEqClientIni(s));
    EXPECT_NE(after.find("\r\nSpellParticleOpacity=0.370000\r\n"), std::string::npos);
    EXPECT_EQ(after.find("SpellParticleOpacity=1.000000"), std::string::npos);
    EXPECT_NE(after.find("\r\nActorParticleOpacity=1.000000\r\n"), std::string::npos);
    EXPECT_NE(after.find("\r\nParticleDensity=3\r\n"), std::string::npos);
    EXPECT_EQ(after.size(), text.size());
    EXPECT_EQ(ParseEqClientIni(after).optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 37);
}

// The read is the profile API's: section and key names case-insensitive, the
// first [Defaults] wins, other sections' same-named keys are not read; a
// missing key goes to the end of [Defaults], a missing [Defaults] to the end.
TEST(OptionsParticlesB1175, TheIniReadAndWriteFollowTheProfileApi) {
    const EqClientIni p = ParseEqClientIni(
        "[Options]\nSpellParticleOpacity=0.100000\n"
        "[defaults]\nspellparticleopacity=0.200000\nSPELLPARTICLEOPACITY=0.300000\n"
        "[Defaults]\nEnvironmentParticleOpacity=0.400000\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::SpellParticleOpacity)], 20);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::EnvParticleOpacity)], 100)
        << "a second [Defaults] is not read";

    OptionsPanelState s;
    const std::string grown =
        UpdateEqClientIniText("[Defaults]\r\nGamma=0.000000\r\n\r\n[Options]\r\nClipPlane=20\r\n", CollectEqClientIni(s));
    EXPECT_EQ(grown.rfind("[Defaults]\r\nGamma=0.000000\r\nSpellParticleOpacity=1.000000\r\n", 0), 0u) << grown;
    EXPECT_NE(grown.find("\r\nEnvironmentParticleNearClipPlane=2.000000\r\n\r\n[Options]\r\nClipPlane=20\r\n"),
              std::string::npos) << grown;
    const std::string added = UpdateEqClientIniText("[Options]\nClipPlane=20", CollectEqClientIni(s));
    EXPECT_EQ(added.rfind("[Options]\nClipPlane=20\n[Defaults]\nSpellParticleOpacity=1.000000\n", 0), 0u) << added;
}

// The file itself: <cache dir>/eqclient.ini, one per install. A save updates
// an existing file in place (the capture's other 500-odd lines survive) and a
// load reads it back.
TEST(OptionsParticlesB1175, TheGlobalFileSavesAndLoadsInTheCacheDir) {
    char tmpl[] = "/tmp/b1175_eqclient_XXXXXX";
    ASSERT_NE(::mkdtemp(tmpl), nullptr);
    const std::string dir = tmpl;
    std::string saved;
    const bool had = std::getenv("WILLEQ_CACHE_DIR") != nullptr;
    if (had) saved = std::getenv("WILLEQ_CACHE_DIR");
    ::setenv("WILLEQ_CACHE_DIR", dir.c_str(), 1);

    EqClientIni none;
    EXPECT_FALSE(LoadEqClientIni(none)) << "no file yet: defaults";

    const std::string capture = readFile(WILLEQ_REPO_PATH("tests/data/b1175/eqclient-after.ini"));
    { std::ofstream(dir + "/eqclient.ini", std::ios::binary) << capture; }
    OptionsPanelState s;
    setOptionsValue(s, OL::Setting::EnvParticleOpacity, 12);
    setOptionsValue(s, OL::Setting::ShowSpellEffects, 0);
    ASSERT_TRUE(SaveEqClientIni(CollectEqClientIni(s)));
    const std::string onDisk = readFile(dir + "/eqclient.ini");
    EXPECT_NE(onDisk.find("\r\nEnvironmentParticleOpacity=0.120000\r\n"), std::string::npos);
    EXPECT_NE(onDisk.find("\r\nShowSpellEffects=0\r\n"), std::string::npos);
    EXPECT_NE(onDisk.find("\r\n[KeyMaps]\r\n"), std::string::npos) << "the rest of the file stays";
    EXPECT_EQ(onDisk.size(), capture.size());

    EqClientIni back;
    ASSERT_TRUE(LoadEqClientIni(back));
    EXPECT_EQ(back.optionValues[static_cast<int>(OL::Setting::EnvParticleOpacity)], 12);
    EXPECT_EQ(back.optionValues[static_cast<int>(OL::Setting::ShowSpellEffects)], 0);
    EXPECT_TRUE(back == CollectEqClientIni(s));

    std::remove((dir + "/eqclient.ini").c_str());
    ::rmdir(dir.c_str());
    if (had) ::setenv("WILLEQ_CACHE_DIR", saved.c_str(), 1);
    else ::unsetenv("WILLEQ_CACHE_DIR");
}
