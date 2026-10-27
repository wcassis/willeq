/*
 * The Options window's settings table -- see options_settings.h for the shape
 * and the ownership rules (B430/B431/B433).
 *
 * PROVENANCE OF THE FILTER ROWS (B433, the Filters page).
 *   - The SET is the era's own `/filter` vocabulary, quoted from
 *     /mnt/projects/EverQuestP1999/eqstr_us.txt 1354-1360: "badword,
 *     guildsay, social, gsay (for group say), shout, auction, ooc, mymisses,
 *     othersmisses, otherhits, atkmissesme, pcspells, npcspells, bardsongs,
 *     dmgshield, critspells, critmelee" -- seventeen. The other eleven
 *     OP_SetServerFilter slots (spell damage, DoT, pet hits/misses, focus,
 *     pet spells, HoT, item speech, strikethrough, stuns, bard songs on pets)
 *     are the post-Velious list entries 6305 and 6318-6323 / 6285-6290 and are
 *     IGNORED (Will's era ruling): they are never drawn and always "show".
 *   - The LIST TEXT is the Options-list strings 6300-6317 ("Bad Word",
 *     "Guild Chat", ... "NPC Spells"); the `/filter` reply text is the older
 *     1316-1353 set ("Bad Words Filter", "Guild Say", ...), kept in
 *     kFilterSlashText below.
 *   - The VALUES are the OP_SetServerFilter slot values, read from eqemu's
 *     Client::ServerFilter() (akk-stack/code/zone/client.cpp): the Filter0
 *     slots show only for 1, PC spells 0/1/2 = show/hide/group, bard songs
 *     0/1/2/3 = show/mine/group/hide, spell and melee crits 0/1/2 =
 *     show/mine/hide, NPC spells and damage shields 0/1 = show/hide. The
 *     choice captions are eqstr 1347 "Show", 1349 "Show Group Only", 1350
 *     "Show Mine Only" and "Hide".
 *   - The INI KEYS are eqclient.ini's `[Options]` keys (BadWord, PCSpells,
 *     NPCSpells, BardSongs, CriticalSpells, CriticalMelee, HideDamageShield,
 *     Channel00..Channel09).
 *
 * ASSUMPTIONS AWAITING ORACLE (listed again in the B430 report):
 *   A1. Channel00..Channel09 are the ten Filter0 slots in slot order (guild,
 *       socials, group, shouts, auctions, ooc, my misses, others' misses,
 *       others' hits, missed me). Ten keys, ten such slots, and every one of
 *       them reads 1 (= show) in the shipped files except Channel07 (others'
 *       hits) -- consistent, not proven.
 *   A2. The ini value of a filter IS its wire value (PCSpells=0 = show,
 *       BadWord=1 = show). Every shipped file is consistent with it.
 *   A3. The list order is the eqstr 6300.. id order.
 *   A4. `/filter <name>` flips between the filter's Show and Hide values and
 *       replies eqstr 1351 "%1 *ON*" when the filter now HIDES and 1348
 *       "%1 *OFF*" when it shows.
 */

#include "client/graphics/ui/options_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "client/graphics/environment/particle_options.h"  // B995: the ini floats

namespace EQT {
namespace Graphics {
namespace OptionsLayout {

namespace {

using eqt::GameOption;

// --- Choice lists, by the server convention they encode -------------------
const Choice kShowIsOne[] = {{1, "Show"}, {0, "Hide"}};
const Choice kShowHide[] = {{0, "Show"}, {1, "Hide"}};
const Choice kPcSpells[] = {{0, "Show"}, {1, "Hide"}, {2, "Show Group Only"}};
const Choice kBardSongs[] = {{0, "Show"}, {1, "Show Mine Only"},
                             {2, "Show Group Only"}, {3, "Hide"}};
const Choice kCrits[] = {{0, "Show"}, {1, "Show Mine Only"}, {2, "Hide"}};
// B995: EQUI_OptionsWindow.xml ODP_*ParticlesDensityCombobox / NearClipCombobox <Choices>.
const Choice kDensity[] = {{0, "Off"}, {1, "Low"}, {2, "Medium"}, {3, "High"}};
const Choice kNearClip[] = {{0, "Near"}, {1, "Medium"}, {2, "Far"}, {3, "Farthest"}};
// B1165 (#484): ODP_SpellParticlesFilterCombobox's <Choices> (EQUI_OptionsWindow.xml),
// each the eqgame value table DAT_00793b4c entry the handler stores
// (eqgame.exe.c:245181-245185: DAT_0098581c = (&DAT_00793b4c)[index]).
const Choice kCastFilter[] = {{1, "My spells"},     {2, "Group spells"},  {4, "Raid spells"},
                              {8, "All PCs"},       {17, "Me + NPC"},     {18, "Group + NPC"},
                              {20, "Raid + NPC"},   {24, "All spells"}};

#define CHOICES(arr) arr, static_cast<int>(sizeof(arr) / sizeof(arr[0]))
#define NO_CHOICES nullptr, 0

constexpr GameOption kNoGame = GameOption::Count;

// clang-format off
const SettingInfo kSettings[kSettingCount] = {
{Setting::None, Owner::None, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", ""},

// T434's three. Persisted by ui_preferences.cpp's kOptions table under
// Music / SoundVolume / ClipPlane, so iniKey is null HERE (one writer per key).
{Setting::MusicVolume, Owner::Legacy, ValueKind::Step, "musicvolume", nullptr, 14, 20, NO_CHOICES, -1, kNoGame,
 "Music Volume", "eqclient.ini [Defaults] Music=18 (T434)"},
{Setting::SoundVolume, Owner::Legacy, ValueKind::Step, "soundvolume", nullptr, 20, 20, NO_CHOICES, -1, kNoGame,
 "Sound Volume", "eqclient.ini [Defaults] SoundVolume=10 (T434)"},
{Setting::ClipPlane, Owner::Legacy, ValueKind::Step, "clipplane", nullptr, 5, 20, NO_CHOICES, -1, kNoGame,
 "Far Clip Plane", "eqclient.ini [Options] ClipPlane=20 (T434)"},

// --- General ---
// Server-owned (player profile). The two anon rows are views of ONE server
// value (0 normal / 1 anon / 2 roleplay), so at most one is ever checked.
{Setting::Anonymous, Owner::Game, ValueKind::Bool, "anonymous", nullptr, 0, 1, NO_CHOICES, -1, GameOption::AnonStatus,
 "Anonymous", "OP_SpawnAppearance 21 = 1; PlayerProfile.anon @13053"},
{Setting::Roleplaying, Owner::Game, ValueKind::Bool, "roleplaying", nullptr, 0, 1, NO_CHOICES, -1, GameOption::AnonStatus,
 "Roleplaying", "OP_SpawnAppearance 21 = 2; PlayerProfile.anon @13053"},
{Setting::AutoSplit, Owner::Game, ValueKind::Bool, "autosplit", nullptr, 0, 1, NO_CHOICES, -1, GameOption::AutoSplit,
 "Autosplit", "OP_SpawnAppearance 28; PlayerProfile.autosplit @13256"},
{Setting::AutoConsentGroup, Owner::Game, ValueKind::Bool, "autoconsentgroup", nullptr, 0, 1, NO_CHOICES, -1, GameOption::AutoConsentGroup,
 "Auto Consent Group", "OP_SpawnAppearance 40; PlayerProfile.groupAutoconsent @19572"},
{Setting::AutoConsentRaid, Owner::Game, ValueKind::Bool, "autoconsentraid", nullptr, 0, 1, NO_CHOICES, -1, GameOption::AutoConsentRaid,
 "Auto Consent Raid", "OP_SpawnAppearance 41; PlayerProfile.raidAutoconsent @19573"},
{Setting::AutoConsentGuild, Owner::Game, ValueKind::Bool, "autoconsentguild", nullptr, 0, 1, NO_CHOICES, -1, GameOption::AutoConsentGuild,
 "Auto Consent Guild", "OP_SpawnAppearance 42; PlayerProfile.guildAutoconsent @19574"},
// Default ON: the oracle capture review/T377/oracle/
// T377_oracle_options_alt_o_1024x768.png paints "Pet Window Popup" yellow
// (the oracle's checked colour). No shipped ini carries a key for it, so the
// key is FABLE'S OWN -- flagged deviation.
{Setting::PetWindowPopup, Owner::Renderer, ValueKind::Bool, "petwindowpopup", "PetWindowPopup", 1, 1, NO_CHOICES, -1, kNoGame,
 "Pet Window Popup", "T377 oracle capture (checked); key is fable's"},

// --- Filters (see the provenance block at the top of this file) ---
{Setting::FilterSelection, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OFP_FilterList / OFP_FilterComboBox"},
{Setting::FilterBadWords, Owner::Renderer, ValueKind::Choice, "badword", "BadWord", 1, 0, CHOICES(kShowIsOne), 7, kNoGame,
 "Bad Word", "eqstr 6300; slot 7; eqclient.ini BadWord=1"},
{Setting::FilterGuildChat, Owner::Renderer, ValueKind::Choice, "guildsay", "Channel00", 1, 0, CHOICES(kShowIsOne), 1, kNoGame,
 "Guild Chat", "eqstr 6306; slot 1; Channel00 (A1)"},
{Setting::FilterSocials, Owner::Renderer, ValueKind::Choice, "social", "Channel01", 1, 0, CHOICES(kShowIsOne), 2, kNoGame,
 "Socials", "eqstr 6307; slot 2; Channel01 (A1)"},
{Setting::FilterGroupChat, Owner::Renderer, ValueKind::Choice, "gsay", "Channel02", 1, 0, CHOICES(kShowIsOne), 3, kNoGame,
 "Group Chat", "eqstr 6308; slot 3; Channel02 (A1)"},
{Setting::FilterShouts, Owner::Renderer, ValueKind::Choice, "shout", "Channel03", 1, 0, CHOICES(kShowIsOne), 4, kNoGame,
 "Shouts", "eqstr 6309; slot 4; Channel03 (A1)"},
{Setting::FilterAuctions, Owner::Renderer, ValueKind::Choice, "auction", "Channel04", 1, 0, CHOICES(kShowIsOne), 5, kNoGame,
 "Auctions", "eqstr 6310; slot 5; Channel04 (A1)"},
{Setting::FilterOOC, Owner::Renderer, ValueKind::Choice, "ooc", "Channel05", 1, 0, CHOICES(kShowIsOne), 6, kNoGame,
 "Out of Character", "eqstr 6311; slot 6; Channel05 (A1)"},
{Setting::FilterMyMisses, Owner::Renderer, ValueKind::Choice, "mymisses", "Channel06", 1, 0, CHOICES(kShowIsOne), 14, kNoGame,
 "My Misses", "eqstr 6312; slot 14; Channel06 (A1)"},
{Setting::FilterOthersMiss, Owner::Renderer, ValueKind::Choice, "othersmisses", "Channel07", 1, 0, CHOICES(kShowIsOne), 15, kNoGame,
 "Others' Misses", "eqstr 6313; slot 15; Channel07 (A1)"},
{Setting::FilterOthersHit, Owner::Renderer, ValueKind::Choice, "otherhits", "Channel08", 1, 0, CHOICES(kShowIsOne), 16, kNoGame,
 "Others' Hits", "eqstr 6314; slot 16; Channel08 (A1)"},
{Setting::FilterMissedMe, Owner::Renderer, ValueKind::Choice, "atkmissesme", "Channel09", 1, 0, CHOICES(kShowIsOne), 17, kNoGame,
 "Missed Me", "eqstr 6315; slot 17; Channel09 (A1)"},
{Setting::FilterPCSpells, Owner::Renderer, ValueKind::Choice, "pcspells", "PCSpells", 0, 0, CHOICES(kPcSpells), 8, kNoGame,
 "PC Spells", "eqstr 6301; slot 8; eqclient.ini PCSpells=0"},
{Setting::FilterNPCSpells, Owner::Renderer, ValueKind::Choice, "npcspells", "NPCSpells", 0, 0, CHOICES(kShowHide), 9, kNoGame,
 "NPC Spells", "eqstr 6317; slot 9; eqclient.ini NPCSpells=0"},
{Setting::FilterBardSongs, Owner::Renderer, ValueKind::Choice, "bardsongs", "BardSongs", 0, 0, CHOICES(kBardSongs), 10, kNoGame,
 "Bard Songs", "eqstr 6302; slot 10; eqclient.ini BardSongs=0"},
{Setting::FilterDamageShields, Owner::Renderer, ValueKind::Choice, "dmgshield", "HideDamageShield", 0, 0, CHOICES(kShowHide), 18, kNoGame,
 "Damage Shields", "eqstr 6316; slot 18; eqclient.ini HideDamageShield=0"},
{Setting::FilterSpellCrits, Owner::Renderer, ValueKind::Choice, "critspells", "CriticalSpells", 0, 0, CHOICES(kCrits), 11, kNoGame,
 "Spell Criticals", "eqstr 6303; slot 11; eqclient.ini CriticalSpells=0"},
{Setting::FilterMeleeCrits, Owner::Renderer, ValueKind::Choice, "critmelee", "CriticalMelee", 0, 0, CHOICES(kCrits), 12, kNoGame,
 "Melee Criticals", "eqstr 6304; slot 12; eqclient.ini CriticalMelee=0"},

// --- Display (B431) ---
// eqclient.ini [Options] PCNames=1 / NPCNames=1 in every shipped file.
{Setting::PCNames, Owner::Renderer, ValueKind::Bool, "pcnames", "PCNames", 1, 1, NO_CHOICES, -1, kNoGame,
 "Show PC Names", "eqclient.ini [Options] PCNames=1"},
{Setting::NPCNames, Owner::Renderer, ValueKind::Bool, "npcnames", "NPCNames", 1, 1, NO_CHOICES, -1, kNoGame,
 "Show NPC Names", "eqclient.ini [Options] NPCNames=1"},
// B994 (#314): default OFF and the key is FABLE'S OWN -- none of the six
// eqclient*.ini files carries a ring key, and TargetIndicator.ini's
// [instructions] describe turning the indicator ON (target_ring.h, R5).
{Setting::TargetRing, Owner::Renderer, ValueKind::Bool, "targetring", "ShowTargetRing", 0, 1, NO_CHOICES, -1, kNoGame,
 "Show 3D Target Ring", "uifiles/default/TargetIndicator.ini; key is fable's (R5)"},
// --- Display > Particles (B995): eqclient.ini keys, its floats.
// Choices are EQUI_OptionsWindow.xml's <Choices>; the floats per choice are
// the decompile's (N1 PROVEN, B1165); the near-clip meaning is still N2
// (particle_options.h).
{Setting::ParticlesTab, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "ODP_ParticlesTabs"},
{Setting::SpellParticleOpacity, Owner::Renderer, ValueKind::Step, "spellparticleopacity", "SpellParticleOpacity", 20, 20, NO_CHOICES, -1, kNoGame,
 "Spell Particle Opacity", "eqclient.ini SpellParticleOpacity=1.000000 (N1)", 20.0f, nullptr},
{Setting::SpellParticleDensity, Owner::Renderer, ValueKind::Choice, "spellparticledensity", "SpellParticleDensity", 3, 0, CHOICES(kDensity), -1, kNoGame,
 "Spell Particle Density", "eqclient.ini SpellParticleDensity=1.000000 (N1)", 0.0f, Environment::kDensityIniValues},
{Setting::SpellParticleNearClip, Owner::Renderer, ValueKind::Choice, "spellparticlenearclip", "SpellParticleNearClipPlane", 0, 0, CHOICES(kNearClip), -1, kNoGame,
 "Spell Particle Near Clip", "eqclient.ini SpellParticleNearClipPlane=2.000000 = Near (N1 PROVEN, N2)", 0.0f, Environment::kNearClipIniValues},
{Setting::SpellParticleCastFilter, Owner::Renderer, ValueKind::Choice, "spellparticlecastfilter", "SpellParticleCastFilter", 24, 0, CHOICES(kCastFilter), -1, kNoGame,
 "Spell Particles On For", "eqclient.ini SpellParticleCastFilter=24; eqgame.exe.c:132638 default 0x18, kept raw (B1165)",
 0.0f, nullptr, /*rawIniInt=*/true},
{Setting::ShowSpellEffects, Owner::Renderer, ValueKind::Bool, "showspelleffects", "ShowSpellEffects", 1, 1, NO_CHOICES, -1, kNoGame,
 "Show Spell Effects", "eqclient.ini ShowSpellEffects=1; eqgame.exe.c:126007-126011 (B1165)"},
{Setting::EnvParticleOpacity, Owner::Renderer, ValueKind::Step, "envparticleopacity", "EnvironmentParticleOpacity", 20, 20, NO_CHOICES, -1, kNoGame,
 "Environment Particle Opacity", "eqclient.ini EnvironmentParticleOpacity=1.000000 (N1)", 20.0f, nullptr},
{Setting::EnvParticleDensity, Owner::Renderer, ValueKind::Choice, "envparticledensity", "EnvironmentParticleDensity", 3, 0, CHOICES(kDensity), -1, kNoGame,
 "Environment Particle Density", "eqclient.ini EnvironmentParticleDensity=1.000000 (N1)", 0.0f, Environment::kDensityIniValues},
{Setting::EnvParticleNearClip, Owner::Renderer, ValueKind::Choice, "envparticlenearclip", "EnvironmentParticleNearClipPlane", 0, 0, CHOICES(kNearClip), -1, kNoGame,
 "Environment Particle Near Clip", "eqclient.ini EnvironmentParticleNearClipPlane=2.000000 = Near (N1 PROVEN, N2)", 0.0f, Environment::kNearClipIniValues},

// --- Mouse (B433) --- eqclient.ini [Options], every shipped file:
// InverseMouse=0 LookSpring=0 MouseActive=1 MouseWheelZoom=1
// MouseSensitivity=6. ASSUMPTIONS AWAITING ORACLE: A5 OMP_MouseLookCheckbox
// is MouseActive (the only remaining mouse key); A6 the sensitivity slider is
// the oracle's 0..20 slider and step 6 is fable's pre-B433 feel (scale =
// (step + 1) / 7, mouse_options.h); A7 look spring levels the view while the
// player walks with no mouse-look held.
{Setting::MouseInvertY, Owner::Renderer, ValueKind::Bool, "invertmouse", "InverseMouse", 0, 1, NO_CHOICES, -1, kNoGame,
 "Invert Y Axis", "eqclient.ini [Options] InverseMouse=0"},
{Setting::MouseLookSpring, Owner::Renderer, ValueKind::Bool, "lookspring", "LookSpring", 0, 1, NO_CHOICES, -1, kNoGame,
 "Look Spring", "eqclient.ini [Options] LookSpring=0 (A7)"},
{Setting::MouseLook, Owner::Renderer, ValueKind::Bool, "mouselook", "MouseActive", 1, 1, NO_CHOICES, -1, kNoGame,
 "Mouse Look", "eqclient.ini [Options] MouseActive=1 (A5)"},
{Setting::MouseWheelZoom, Owner::Renderer, ValueKind::Bool, "mousewheelzoom", "MouseWheelZoom", 1, 1, NO_CHOICES, -1, kNoGame,
 "Mouse Wheel Zoom", "eqclient.ini [Options] MouseWheelZoom=1 (T461)"},
{Setting::MouseSensitivity, Owner::Renderer, ValueKind::Step, "mousesensitivity", "MouseSensitivity", 6, 20, NO_CHOICES, -1, kNoGame,
 "Mouselook Sensitivity", "eqclient.ini [Options] MouseSensitivity=6 (A6)"},

// --- Keys (B433): not values -- the bindings are HotkeyManager's (global). ---
{Setting::KeysCategory, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OKP_KeyboardFilterCombobox"},
{Setting::KeysAssignment, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OKP_KeyboardAssignmentList"},
{Setting::KeysReloadDefaults, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OKP_DefaultKeysButton"},

// --- Colors (B993): not values -- the table is OptionsPanelState::textColors,
// persisted under [TextColors] User_N_Red/Green/Blue (ui_preferences.cpp). ---
{Setting::TextColorList, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OCP_ColorChooseList"},
{Setting::TextColorReset, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OCP_UserColorDefault"},
{Setting::TextColorChange, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "OCP_UserColorChange"},
{Setting::TextColors, Owner::Marker, ValueKind::None, "", nullptr, 0, 0, NO_CHOICES, -1, kNoGame, "", "ColorPickerWnd Accept / /option textcolor"},
};
// clang-format on

// A3: the OFP_FilterList order -- eqstr 6300.. id order, era rows only.
const Setting kFilterOrder[kFilterCount] = {
    Setting::FilterBadWords,  Setting::FilterPCSpells,  Setting::FilterBardSongs,
    Setting::FilterSpellCrits, Setting::FilterMeleeCrits, Setting::FilterGuildChat,
    Setting::FilterSocials,   Setting::FilterGroupChat, Setting::FilterShouts,
    Setting::FilterAuctions,  Setting::FilterOOC,       Setting::FilterMyMisses,
    Setting::FilterOthersMiss, Setting::FilterOthersHit, Setting::FilterMissedMe,
    Setting::FilterDamageShields, Setting::FilterNPCSpells,
};

std::string lower(const std::string& s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

const SettingInfo& settingInfo(Setting s) {
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSettingCount) return kSettings[0];
    return kSettings[i];
}

const SettingInfo* allSettings() { return kSettings; }

const SettingInfo* findSettingByName(const std::string& name) {
    const std::string key = lower(name);
    if (key.empty()) return nullptr;
    for (const auto& row : kSettings) {
        if (row.name[0] != '\0' && key == row.name) return &row;
    }
    return nullptr;
}

OptionValues defaultOptionValues() {
    OptionValues v{};
    for (int i = 0; i < kSettingCount; ++i) v[i] = kSettings[i].defaultValue;
    return v;
}

bool isValidSettingValue(Setting s, int v) {
    const auto& info = settingInfo(s);
    switch (info.kind) {
        case ValueKind::Step:
        case ValueKind::Bool:
            return v >= 0 && v <= info.maxValue;
        case ValueKind::Choice:
            if (info.rawIniInt) return true;  // B1165: the oracle keeps any int
            for (int i = 0; i < info.choiceCount; ++i) {
                if (info.choices[i].value == v) return true;
            }
            return false;
        case ValueKind::None:
            break;
    }
    return false;
}

int nextChoiceValue(Setting s, int v) {
    const auto& info = settingInfo(s);
    if (info.kind != ValueKind::Choice || info.choiceCount == 0) return v;
    for (int i = 0; i < info.choiceCount; ++i) {
        if (info.choices[i].value == v) {
            return info.choices[(i + 1) % info.choiceCount].value;
        }
    }
    return info.choices[0].value;
}

const char* choiceText(Setting s, int v) {
    const auto& info = settingInfo(s);
    const int i = choiceDisplayIndex(s, v);
    return i >= 0 ? info.choices[i].text : "";
}

int choiceDisplayIndex(Setting s, int v) {
    const auto& info = settingInfo(s);
    if (info.kind != ValueKind::Choice || info.choiceCount == 0) return -1;
    for (int i = 0; i < info.choiceCount; ++i) {
        if (info.choices[i].value == v) return i;
    }
    // B1165: SpellParticleCastFilter's combobox starts at index 7 and takes
    // the index of the first DAT_00793b4c entry equal to the value
    // (0x53b9e9..0x53ba07: push 7; pop edx ... mov edx, eax).
    return info.rawIniInt ? info.choiceCount - 1 : -1;
}

Setting filterAt(int row) {
    if (row < 0 || row >= kFilterCount) return Setting::None;
    return kFilterOrder[row];
}

int filterRow(Setting s) {
    for (int i = 0; i < kFilterCount; ++i) {
        if (kFilterOrder[i] == s) return i;
    }
    return -1;
}

int filterShowValue(Setting s) {
    const auto& info = settingInfo(s);
    return info.choiceCount > 0 ? info.choices[0].value : 0;
}

int filterHideValue(Setting s) {
    const auto& info = settingInfo(s);
    for (int i = 0; i < info.choiceCount; ++i) {
        if (std::string(info.choices[i].text) == "Hide") return info.choices[i].value;
    }
    return filterShowValue(s);
}

bool filterHides(Setting s, int value) {
    return filterRow(s) >= 0 && value == filterHideValue(s);
}

eqt::ServerFilterPayload serverFilterPayload(const OptionValues& v) {
    eqt::ServerFilterPayload p = eqt::defaultServerFilterPayload();
    for (int row = 0; row < kFilterCount; ++row) {
        const Setting s = kFilterOrder[row];
        const auto& info = settingInfo(s);
        if (info.serverFilterSlot < 0 || info.serverFilterSlot >= eqt::kServerFilterSlots) continue;
        const int value = v[static_cast<int>(s)];
        if (!isValidSettingValue(s, value)) continue;
        p[info.serverFilterSlot] = static_cast<uint32_t>(value);
    }
    return p;
}

std::string settingIniText(Setting s, int v) {
    const auto& info = settingInfo(s);
    float f = 0.0f;
    bool isFloat = false;
    if (info.kind == ValueKind::Step && info.iniDivisor > 0.0f) {
        f = static_cast<float>(v) / info.iniDivisor;
        isFloat = true;
    } else if (info.kind == ValueKind::Choice && info.iniFloats) {
        for (int i = 0; i < info.choiceCount; ++i) {
            if (info.choices[i].value == v) { f = info.iniFloats[i]; isFloat = true; break; }
        }
    }
    if (!isFloat) return std::to_string(v);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%f", static_cast<double>(f));  // eqclient.ini's own "1.000000"
    return buf;
}

bool isIniFloatSetting(Setting s) {
    const auto& info = settingInfo(s);
    return (info.kind == ValueKind::Step && info.iniDivisor > 0.0f) ||
           (info.kind == ValueKind::Choice && info.iniFloats);
}

float settingTableFloat(Setting s, int v) {
    const auto& info = settingInfo(s);
    if (info.kind == ValueKind::Step && info.iniDivisor > 0.0f)
        return static_cast<float>(v) / info.iniDivisor;
    if (info.kind == ValueKind::Choice && info.iniFloats) {
        for (int i = 0; i < info.choiceCount; ++i)
            if (info.choices[i].value == v) return info.iniFloats[i];
    }
    return static_cast<float>(v);
}

float appliedSettingFloat(Setting s, int v, const std::optional<float>& raw) {
    return raw ? *raw : settingTableFloat(s, v);
}

float clampSettingIniFloat(Setting s, float f) {
    // B1165 (#484): the oracle's loader clamps before anything reads the
    // float (eqgame.exe.c:132608-132637): opacity and density to [0, 1],
    // the near clip to [1, 8].
    const bool nearClip = settingInfo(s).iniFloats == Environment::kNearClipIniValues;
    return nearClip ? Environment::clampNearClipIni(f) : Environment::clampUnitIni(f);
}

bool parseSettingIniFloat(Setting s, const std::string& text, int& display,
                          std::optional<float>& applied) {
    if (!isIniFloatSetting(s)) return false;
    const auto& info = settingInfo(s);
    float f = 0.0f;
    try {
        size_t used = 0;
        f = std::stof(text, &used);
        if (used == 0 || !std::isfinite(f)) return false;
    } catch (...) {
        return false;
    }
    f = clampSettingIniFloat(s, f);
    int v = 0;
    if (info.kind == ValueKind::Step) {
        // fable's slider shows the nearest step.
        v = static_cast<int>(std::lround(f * info.iniDivisor));
        if (!isValidSettingValue(s, v)) return false;
    } else {
        // The oracle's combobox shows the first choice whose value is >= the
        // float (0x53b93f..0x53b985, 0x53b994..0x53b9da), not the nearest.
        const int i = Environment::choiceForIniFloat(info.iniFloats, info.choiceCount, f);
        if (i < 0) return false;
        v = info.choices[i].value;
    }
    display = v;
    // ... while the client APPLIES the clamped float itself (B1165 fix, MR
    // !318 note 7756: _DAT_00985810.. hold it as read, 0x495f29 pushes it).
    applied = (f == settingTableFloat(s, v)) ? std::nullopt : std::optional<float>(f);
    return true;
}

bool parseSettingIniText(Setting s, const std::string& text, int& out) {
    if (isIniFloatSetting(s)) {
        std::optional<float> applied;
        return parseSettingIniFloat(s, text, out, applied);
    }
    try {
        size_t used = 0;
        const int v = std::stoi(text, &used);
        if (used == 0 || !isValidSettingValue(s, v)) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::string settingIniText(Setting s, int v, const std::optional<float>& raw) {
    if (!raw || !isIniFloatSetting(s)) return settingIniText(s, v);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%f", static_cast<double>(*raw));
    return buf;
}

// The `/filter` reply text, eqstr_us.txt 1316-1353 (the name each one's
// `%1 *ON*` / `%1 *OFF*` line carries).
const char* filterSlashText(Setting s) {
    switch (s) {
        case Setting::FilterBadWords:      return "Bad Words Filter";   // 1316
        case Setting::FilterGuildChat:     return "Guild Say";          // 1318
        case Setting::FilterSocials:       return "Socials";            // 1320
        case Setting::FilterGroupChat:     return "Group Say";          // 1322
        case Setting::FilterShouts:        return "Shouts";             // 1325
        case Setting::FilterAuctions:      return "Auctions";           // 1324
        case Setting::FilterOOC:           return "OOC";                // 1328
        case Setting::FilterMyMisses:      return "My Misses";          // 1330
        case Setting::FilterOthersMiss:    return "Others Misses";      // 1332
        case Setting::FilterMissedMe:      return "Attacker Misses Me"; // 1334
        case Setting::FilterPCSpells:      return "PC Spells";          // 1335
        case Setting::FilterNPCSpells:     return "NPC Spells";         // 1338
        case Setting::FilterBardSongs:     return "Bard Songs";         // 1340
        case Setting::FilterDamageShields: return "Damage Shields";     // 1342
        case Setting::FilterSpellCrits:    return "Critical Spells";    // 1344
        case Setting::FilterMeleeCrits:    return "Critical Melee";     // 1346
        case Setting::FilterOthersHit:     return "Others Hits";        // 1353
        default:                           return "";
    }
}

}  // namespace OptionsLayout
}  // namespace Graphics
}  // namespace EQT
