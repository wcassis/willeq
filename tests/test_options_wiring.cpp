// B430/B431/B433 -- the Options window's controls, wired.
//
// What is pinned:
//   * every kControls row is classified exactly once, and a row is WIRED iff
//     it has a setting (an excluded one can never be wired by accident);
//   * the settings table is consistent (one row per Setting, legal defaults,
//     unique names and ini keys);
//   * per page: a synthetic click changes the setting, the change survives
//     the persistence round trip under the oracle's own key, and the slash
//     command sets the same field; plus the negatives.
//
// Device-free, like test_options_window.cpp.

#include <gtest/gtest.h>

#include "client/graphics/ui/options_classification.h"
#include "client/graphics/ui/options_command.h"
#include "client/graphics/ui/options_layout.h"
#include "client/graphics/ui/options_panel.h"
#include "client/graphics/ui/static_ui_input.h"
#include "client/graphics/ui/ui_layout.h"
#include "client/graphics/ui/ui_preferences.h"
#include "common/packets/guild.h"
#include "client/graphics/player/mouse_options.h"
#include "client/graphics/ui/options_keys.h"
#include "client/data_paths.h"
#include "client/input/hotkey_manager.h"

#include <cstdio>

#include <cstring>
#include <map>
#include <set>
#include <string>

#include "scoped_temp_dir.h"

using namespace EQT::Graphics;
namespace OL = EQT::Graphics::OptionsLayout;

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

// One synthetic left click at (x, y) through the production input path.
OL::Setting click(OptionsPanelState& s, int x, int y) {
    StaticUIInput input;
    OL::Setting changed = OL::Setting::None;
    input.processOptionsInput(x, y, true, layout800(), s, changed);
    return changed;
}

// A click on the centre of control `item`, on its own tab.
OL::Setting clickControl(OptionsPanelState& s, const char* item) {
    const int i = controlIndex(item);
    EXPECT_GE(i, 0) << item;
    if (i < 0) return OL::Setting::None;
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::kControls[i].tab);
    const auto p = centre(OL::controlRect(layout800(), i));
    return click(s, p.X, p.Y);
}

// Persist the state and read it back into a fresh one -- the whole
// Collect -> Serialize -> Parse -> Apply path the client runs.
OptionsPanelState roundTrip(OptionsPanelState& s) {
    UiPreferencesBinding b;
    b.options = &s;
    const std::string text = SerializeUiPreferences(CollectUiPreferences(b, 1));
    OptionsPanelState back;
    UiPreferencesBinding b2;
    b2.options = &back;
    ApplyUiPreferences(ParseUiPreferences(text), b2);
    return back;
}

std::string serialized(OptionsPanelState& s) {
    UiPreferencesBinding b;
    b.options = &s;
    return SerializeUiPreferences(CollectUiPreferences(b, 1));
}

int value(const OptionsPanelState& s, OL::Setting setting) {
    return optionsSettingStep(s, setting);
}

}  // namespace

// ===========================================================================
// The classification census
// ===========================================================================

TEST(OptionsClassification, EveryControlRowIsClassifiedExactlyOnce) {
    std::map<std::string, int> seen;
    for (int i = 0; i < OL::controlClassificationCount(); ++i) {
        seen[OL::controlClassifications()[i].xmlItem]++;
    }
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        const std::string item = OL::kControls[i].xmlItem;
        EXPECT_EQ(seen[item], 1) << item << " must have exactly one classification row";
    }
    // ... and the table names nothing the layout does not have.
    for (int i = 0; i < OL::controlClassificationCount(); ++i) {
        EXPECT_GE(controlIndex(OL::controlClassifications()[i].xmlItem), 0)
            << OL::controlClassifications()[i].xmlItem << " is not a layout row";
    }
    EXPECT_EQ(OL::controlClassificationCount(), OL::CONTROL_COUNT);
}

TEST(OptionsClassification, AControlIsWiredIffItHasASetting) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        const auto& c = OL::kControls[i];
        const auto* cls = OL::findControlClass(c.xmlItem);
        ASSERT_NE(cls, nullptr) << c.xmlItem;
        EXPECT_EQ(cls->disposition == OL::Disposition::Wired, c.setting != OL::Setting::None)
            << c.xmlItem << " is " << OL::dispositionText(cls->disposition);
        EXPECT_GT(std::strlen(cls->reason), 0u) << c.xmlItem << " needs a reason";
    }
}

TEST(OptionsClassification, OutOfEraRowsStayUnwired) {
    // Will's rulings, as a negative: these may never acquire a setting.
    for (const char* item : {"OGP_AANoConfirmCheckbox", "OGP_AdvMerchantNoConfirmCheckbox",
                             "OGP_LeadershipNoConfirmCheckbox", "OGP_UseTellWindowsCheckbox",
                             "OGP_LoadSkinButton", "OGP_AllowContextMenusCheckbox",
                             "OGP_SystemInfoButton", "OGP_AutojoinCheckbox",
                             "ODP_LevelOfDetailCheckbox", "ODP_ShowHelmCheckbox",
                             "ODP_AdvancedButton", "ODP_LODBiasSlider", "ODP_SkyCombobox",
                             "OMP_MailForwardingCheckbox"}) {
        const int i = controlIndex(item);
        ASSERT_GE(i, 0) << item;
        EXPECT_EQ(OL::kControls[i].setting, OL::Setting::None) << item;
        const auto d = OL::findControlClass(item)->disposition;
        EXPECT_TRUE(d == OL::Disposition::PostVelious || d == OL::Disposition::WillOut) << item;
    }
}

TEST(OptionsClassification, EveryMailRowIsExcluded) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        if (OL::kControls[i].tab != OL::Tab::Mail) continue;
        EXPECT_EQ(OL::kControls[i].setting, OL::Setting::None) << OL::kControls[i].xmlItem;
    }
}

// ===========================================================================
// The settings table
// ===========================================================================

TEST(OptionsSettingsTable, OneRowPerSettingInEnumOrder) {
    for (int i = 0; i < OL::kSettingCount; ++i) {
        EXPECT_EQ(static_cast<int>(OL::allSettings()[i].setting), i) << "row " << i;
    }
}

TEST(OptionsSettingsTable, DefaultsAreLegalAndNamesAndKeysAreUnique) {
    std::set<std::string> names, keys;
    for (int i = 1; i < OL::kSettingCount; ++i) {
        const auto& info = OL::allSettings()[i];
        if (info.owner == OL::Owner::Renderer || info.owner == OL::Owner::Game) {
            EXPECT_TRUE(OL::isValidSettingValue(info.setting, info.defaultValue)) << info.name;
        }
        if (info.name[0] != '\0') EXPECT_TRUE(names.insert(info.name).second) << info.name;
        if (info.iniKey) EXPECT_TRUE(keys.insert(info.iniKey).second) << info.iniKey;
        // A Game-owned value is the server's: it must never be persisted.
        if (info.owner == OL::Owner::Game) EXPECT_EQ(info.iniKey, nullptr) << info.name;
    }
    // The legacy three keep their kOptions keys; nothing else may reuse them.
    EXPECT_EQ(keys.count("Music"), 0u);
    EXPECT_EQ(keys.count("SoundVolume"), 0u);
    EXPECT_EQ(keys.count("ClipPlane"), 0u);
}

TEST(OptionsSettingsTable, EveryWiredControlSettingHasATableRow) {
    for (int i = 0; i < OL::CONTROL_COUNT; ++i) {
        const auto s = OL::kControls[i].setting;
        if (s == OL::Setting::None) continue;
        EXPECT_EQ(OL::settingInfo(s).setting, s) << OL::kControls[i].xmlItem;
        EXPECT_NE(OL::settingInfo(s).owner, OL::Owner::None) << OL::kControls[i].xmlItem;
    }
}

// ===========================================================================
// Filters (B433)
// ===========================================================================

TEST(OptionsFilters, TheEraSetIsTheSeventeenSlashFilterNames) {
    // eqstr_us.txt 1357-1360, verbatim order aside.
    const std::set<std::string> era = {
        "badword", "guildsay", "social", "gsay", "shout", "auction", "ooc",
        "mymisses", "othersmisses", "otherhits", "atkmissesme", "pcspells",
        "npcspells", "bardsongs", "dmgshield", "critspells", "critmelee"};
    std::set<std::string> listedNames;
    std::set<int> slots;
    for (int row = 0; row < OL::kFilterCount; ++row) {
        const auto& info = OL::settingInfo(OL::filterAt(row));
        listedNames.insert(info.name);
        EXPECT_TRUE(slots.insert(info.serverFilterSlot).second) << info.name;
        EXPECT_EQ(OL::filterRow(info.setting), row);
    }
    EXPECT_EQ(listedNames, era);
}

TEST(OptionsFilters, TheSlotsAreEqemusFilterSlots) {
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterGuildChat).serverFilterSlot, EQT::FILTER_GUILD_CHAT);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterSocials).serverFilterSlot, EQT::FILTER_SOCIALS);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterBadWords).serverFilterSlot, EQT::FILTER_BAD_WORDS);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterPCSpells).serverFilterSlot, EQT::FILTER_PC_SPELLS);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterBardSongs).serverFilterSlot, EQT::FILTER_BARD_SONGS);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterMissedMe).serverFilterSlot, EQT::FILTER_MISSED_ME);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterDamageShields).serverFilterSlot, EQT::FILTER_DAMAGE_SHIELDS);
    EXPECT_EQ(OL::settingInfo(OL::Setting::FilterMeleeCrits).serverFilterSlot, EQT::FILTER_MELEE_CRITS);
}

TEST(OptionsFilters, DefaultsSendT243sShowEverythingPayload) {
    const OptionsPanelState s;
    EXPECT_EQ(OL::serverFilterPayload(s.values), eqt::defaultServerFilterPayload());
    for (int row = 0; row < OL::kFilterCount; ++row) {
        const auto f = OL::filterAt(row);
        EXPECT_FALSE(OL::filterHides(f, value(s, f))) << OL::settingInfo(f).name;
    }
}

TEST(OptionsFilters, ClickARowThenTheComboCyclesThatFilter) {
    OptionsPanelState s;
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::Tab::Filters);
    const auto l = layout800();
    const int list = controlIndex("OFP_FilterList");
    const int combo = controlIndex("OFP_FilterComboBox");
    ASSERT_GE(list, 0);
    ASSERT_GE(combo, 0);

    // Row 2 is Bard Songs (eqstr 6302): select it -- a selection is not a setting.
    const auto row = centre(OL::listRowRect(l, list, 2));
    EXPECT_EQ(click(s, row.X, row.Y), OL::Setting::None);
    EXPECT_EQ(optionsSelectedFilter(s), OL::Setting::FilterBardSongs);

    // Each combo click moves it to the next of its four oracle values.
    const auto cp = centre(OL::controlRect(l, combo));
    const int expect[] = {1, 2, 3, 0};
    for (int v : expect) {
        EXPECT_EQ(click(s, cp.X, cp.Y), OL::Setting::FilterBardSongs);
        EXPECT_EQ(value(s, OL::Setting::FilterBardSongs), v);
    }
    // Nothing else moved.
    EXPECT_EQ(value(s, OL::Setting::FilterBadWords), 1);
}

TEST(OptionsFilters, AHiddenFilterReachesItsWireSlot) {
    OptionsPanelState s;
    ASSERT_TRUE(setOptionsValue(s, OL::Setting::FilterShouts, 0));      // Filter0: 0 = hide
    ASSERT_TRUE(setOptionsValue(s, OL::Setting::FilterNPCSpells, 1));   // 1 = hide
    const auto p = OL::serverFilterPayload(s.values);
    EXPECT_EQ(p[EQT::FILTER_SHOUTS], 0u);
    EXPECT_EQ(p[EQT::FILTER_NPC_SPELLS], 1u);
    EXPECT_EQ(p[EQT::FILTER_AUCTIONS], 1u);      // untouched, still shown
    // Post-Velious slots keep "show" (0) whatever the era filters do.
    EXPECT_EQ(p[EQT::FILTER_DOT], 0u);
    EXPECT_EQ(p[EQT::FILTER_ITEM_SPEECH], 0u);
}

TEST(OptionsFilters, FiltersPersistUnderTheOracleKeysAndReload) {
    OptionsPanelState s;
    setOptionsValue(s, OL::Setting::FilterGuildChat, 0);
    setOptionsValue(s, OL::Setting::FilterPCSpells, 2);
    setOptionsValue(s, OL::Setting::FilterDamageShields, 1);
    const std::string text = serialized(s);
    EXPECT_NE(text.find("Channel00=0"), std::string::npos) << text;
    EXPECT_NE(text.find("PCSpells=2"), std::string::npos) << text;
    EXPECT_NE(text.find("HideDamageShield=1"), std::string::npos) << text;
    const OptionsPanelState back = roundTrip(s);
    EXPECT_EQ(back.values, s.values);
}

TEST(OptionsFilters, AnOfficialOptionsSectionLoads) {
    // Quoted from /mnt/projects/EverQuestP1999/eqclient-obian.ini [Options].
    const UiPreferences p = ParseUiPreferences(
        "[Options]\r\nChannel06=0\r\nChannel07=0\r\nBadWord=1\r\nPCSpells=0\r\n"
        "BardSongs=0\r\nHideDamageShield=0\r\nCriticalSpells=0\r\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::FilterMyMisses)], 0);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::FilterOthersMiss)], 0);
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::FilterOthersHit)], 1);  // absent: default
}

TEST(OptionsFilters, AnIllegalFilterValueIsRefused) {
    const UiPreferences p = ParseUiPreferences("[Options]\nBardSongs=9\nChannel03=2\nPCSpells=x\n");
    const UiPreferences d;
    EXPECT_EQ(p.optionValues, d.optionValues);
    OptionsPanelState s;
    EXPECT_FALSE(setOptionsValue(s, OL::Setting::FilterSpellCrits, 3));
    EXPECT_EQ(value(s, OL::Setting::FilterSpellCrits), 0);
}

TEST(OptionsFilters, SlashFilterTogglesAndRepliesWithTheEraStrings) {
    OptionsPanelState s;
    auto r = RunOptionsCommand(s, "/filter", "shout");
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_EQ(r.lines[0], "Shouts *ON*");   // eqstr 1351: now filtered
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(r.changed[0], OL::Setting::FilterShouts);
    EXPECT_EQ(value(s, OL::Setting::FilterShouts), 0);
    r = RunOptionsCommand(s, "/filter", "shout");
    EXPECT_EQ(r.lines[0], "Shouts *OFF*");  // eqstr 1348
    EXPECT_EQ(value(s, OL::Setting::FilterShouts), 1);

    // bardsongs flips Show <-> Hide (3), never through mine/group.
    RunOptionsCommand(s, "/filter", "bardsongs");
    EXPECT_EQ(value(s, OL::Setting::FilterBardSongs), 3);
}

TEST(OptionsFilters, SlashFilterWithNoOrABadNamePrintsTheEraUsage) {
    OptionsPanelState s;
    for (const char* args : {"", "spelldamage", "nonsense"}) {
        const auto r = RunOptionsCommand(s, "/filter", args);
        ASSERT_EQ(r.lines.size(), 7u) << args;
        EXPECT_EQ(r.lines[0], "Usage: /filter <name>");
        EXPECT_EQ(r.lines[6], "bardsongs, dmgshield, critspells, critmelee");
        EXPECT_TRUE(r.changed.empty()) << args;
    }
}

TEST(OptionsFilters, SlashOptionSetsAFilterByChoiceText) {
    OptionsPanelState s;
    const auto r = RunOptionsCommand(s, "/option", "bardsongs show mine only");
    ASSERT_EQ(r.changed.size(), 1u);
    EXPECT_EQ(value(s, OL::Setting::FilterBardSongs), 1);
    EXPECT_EQ(r.lines.back(), "bardsongs = Show Mine Only");
    const auto bad = RunOptionsCommand(s, "/option", "bardsongs loud");
    EXPECT_TRUE(bad.changed.empty());
    EXPECT_EQ(value(s, OL::Setting::FilterBardSongs), 1);
}

// ===========================================================================
// General (B430)
// ===========================================================================

TEST(OptionsGeneral, ACheckboxClickTogglesItsSetting) {
    OptionsPanelState s;
    EXPECT_EQ(clickControl(s, "OGP_LootAutosplitCheckbox"), OL::Setting::AutoSplit);
    EXPECT_EQ(value(s, OL::Setting::AutoSplit), 1);
    EXPECT_EQ(clickControl(s, "OGP_LootAutosplitCheckbox"), OL::Setting::AutoSplit);
    EXPECT_EQ(value(s, OL::Setting::AutoSplit), 0);
    for (const char* item : {"OGP_AutoConsentGroupCheckbox", "OGP_AutoConsentRaidCheckbox",
                             "OGP_AutoConsentGuildCheckbox"}) {
        const auto changed = clickControl(s, item);
        EXPECT_NE(changed, OL::Setting::None) << item;
        EXPECT_EQ(value(s, changed), 1) << item;
    }
}

TEST(OptionsGeneral, AnonymousAndRoleplayingAreOneServerValue) {
    OptionsPanelState s;
    clickControl(s, "OGP_AnonymousCheckbox");
    EXPECT_EQ(value(s, OL::Setting::Anonymous), 1);
    clickControl(s, "OGP_RoleplayingCheckbox");
    EXPECT_EQ(value(s, OL::Setting::Roleplaying), 1);
    EXPECT_EQ(value(s, OL::Setting::Anonymous), 0) << "the server keeps one of 0/1/2";
    EXPECT_EQ(gameOptionValueFor(OL::Setting::Roleplaying, true), 2);
    EXPECT_EQ(gameOptionValueFor(OL::Setting::Anonymous, true), 1);
    EXPECT_EQ(gameOptionValueFor(OL::Setting::Anonymous, false), 0);
}

TEST(OptionsGeneral, TheGameThreadsPublishedValueIsWhatTheBoxesShow) {
    OptionsPanelState s;
    EXPECT_TRUE(applyGameOptionChanged(s, eqt::GameOption::AnonStatus, 2));
    EXPECT_EQ(value(s, OL::Setting::Roleplaying), 1);
    EXPECT_EQ(value(s, OL::Setting::Anonymous), 0);
    EXPECT_TRUE(applyGameOptionChanged(s, eqt::GameOption::AnonStatus, 1));
    EXPECT_EQ(value(s, OL::Setting::Anonymous), 1);
    EXPECT_EQ(value(s, OL::Setting::Roleplaying), 0);
    EXPECT_TRUE(applyGameOptionChanged(s, eqt::GameOption::AutoConsentRaid, 1));
    EXPECT_EQ(value(s, OL::Setting::AutoConsentRaid), 1);
    EXPECT_FALSE(applyGameOptionChanged(s, eqt::GameOption::AutoConsentRaid, 1));
}

TEST(OptionsGeneral, ServerOwnedSettingsAreNeverWrittenLocally) {
    OptionsPanelState s;
    setOptionsValue(s, OL::Setting::Anonymous, 1);
    setOptionsValue(s, OL::Setting::AutoSplit, 1);
    const std::string text = serialized(s);
    EXPECT_EQ(text.find("Anonymous"), std::string::npos) << text;
    EXPECT_EQ(text.find("AutoSplit"), std::string::npos) << text;
    // ...and a reload does not resurrect them from anywhere.
    const OptionsPanelState back = roundTrip(s);
    EXPECT_EQ(value(back, OL::Setting::Anonymous), 0);
    EXPECT_EQ(value(back, OL::Setting::AutoSplit), 0);
}

TEST(OptionsGeneral, PetWindowPopupPersistsAndDefaultsOn) {
    OptionsPanelState s;
    EXPECT_EQ(value(s, OL::Setting::PetWindowPopup), 1) << "the T377 capture shows it checked";
    EXPECT_EQ(clickControl(s, "OGP_PetWindowPopupCheckbox"), OL::Setting::PetWindowPopup);
    EXPECT_EQ(value(s, OL::Setting::PetWindowPopup), 0);
    EXPECT_NE(serialized(s).find("PetWindowPopup=0"), std::string::npos);
    EXPECT_EQ(value(roundTrip(s), OL::Setting::PetWindowPopup), 0);
    // The oracle's TRUE/FALSE spelling is accepted for a bool key.
    const UiPreferences p = ParseUiPreferences("[Options]\nPetWindowPopup=FALSE\n");
    EXPECT_EQ(p.optionValues[static_cast<int>(OL::Setting::PetWindowPopup)], 0);
}

TEST(OptionsGeneral, SlashOptionReachesTheSameFields) {
    OptionsPanelState s;
    auto r = RunOptionsCommand(s, "/option", "autoconsentguild on");
    EXPECT_EQ(value(s, OL::Setting::AutoConsentGuild), 1);
    ASSERT_EQ(r.changed.size(), 1u);
    r = RunOptionsCommand(s, "/option", "petwindowpopup off");
    EXPECT_EQ(value(s, OL::Setting::PetWindowPopup), 0);
    r = RunOptionsCommand(s, "/option", "musicvolume 3");
    EXPECT_EQ(s.musicVolume, 3);
    r = RunOptionsCommand(s, "/option", "nosuchoption on");
    EXPECT_TRUE(r.changed.empty());
    r = RunOptionsCommand(s, "/option", "list");
    EXPECT_GT(r.lines.size(), 20u);
}

TEST(OptionsGeneral, ACaptionIsDrawnEnabledButIsNotAControl) {
    OptionsPanelState s;
    s.isOpen = true;
    const int label = controlIndex("OGP_MusicVolumeLabel");
    ASSERT_GE(label, 0);
    EXPECT_EQ(OL::kControls[label].setting, OL::Setting::MusicVolume);
    const auto p = centre(OL::controlRect(layout800(), label));
    EXPECT_EQ(hitTestOptionsControl(layout800(), s, p.X, p.Y), -1);
}

TEST(OptionsGeneral, AnExcludedCheckboxStillDoesNothing) {
    OptionsPanelState s;
    const auto before = s.values;
    EXPECT_EQ(clickControl(s, "OGP_GuildInvitesCheckbox"), OL::Setting::None);
    EXPECT_EQ(clickControl(s, "OGP_AANoConfirmCheckbox"), OL::Setting::None);
    EXPECT_EQ(s.values, before);
}

// ===========================================================================
// Display (B431)
// ===========================================================================

TEST(OptionsDisplay, PcAndNpcNamesAreTwoIndependentSettings) {
    OptionsPanelState s;
    EXPECT_EQ(value(s, OL::Setting::PCNames), 1);
    EXPECT_EQ(value(s, OL::Setting::NPCNames), 1);
    EXPECT_EQ(clickControl(s, "ODP_NPCNamesCheckbox"), OL::Setting::NPCNames);
    EXPECT_EQ(value(s, OL::Setting::NPCNames), 0);
    EXPECT_EQ(value(s, OL::Setting::PCNames), 1) << "one control must not move the other";
    const std::string text = serialized(s);
    EXPECT_NE(text.find("NPCNames=0"), std::string::npos) << text;
    EXPECT_NE(text.find("PCNames=1"), std::string::npos) << text;
    const OptionsPanelState back = roundTrip(s);
    EXPECT_EQ(value(back, OL::Setting::NPCNames), 0);
    EXPECT_EQ(value(back, OL::Setting::PCNames), 1);
    RunOptionsCommand(s, "/option", "pcnames off");
    EXPECT_EQ(value(s, OL::Setting::PCNames), 0);
}

// ===========================================================================
// Mouse (B433)
// ===========================================================================

TEST(OptionsMouse, TheCheckboxesAndTheSliderMoveTheirSettings) {
    OptionsPanelState s;
    EXPECT_EQ(clickControl(s, "OMP_InvertYAxisCheckbox"), OL::Setting::MouseInvertY);
    EXPECT_EQ(value(s, OL::Setting::MouseInvertY), 1);
    EXPECT_EQ(clickControl(s, "OMP_LookSpringCheckbox"), OL::Setting::MouseLookSpring);
    EXPECT_EQ(clickControl(s, "OMP_MouseLookCheckbox"), OL::Setting::MouseLook);
    EXPECT_EQ(value(s, OL::Setting::MouseLook), 0);
    EXPECT_EQ(clickControl(s, "OMP_MouseWheelZoomCheckbox"), OL::Setting::MouseWheelZoom);
    EXPECT_EQ(value(s, OL::Setting::MouseWheelZoom), 0);

    const int slider = controlIndex("OMP_MouseSensitivitySlider");
    ASSERT_GE(slider, 0);
    const auto r = OL::controlRect(layout800(), slider);
    EXPECT_EQ(click(s, r.LowerRightCorner.X - 1, centre(r).Y), OL::Setting::MouseSensitivity);
    EXPECT_EQ(value(s, OL::Setting::MouseSensitivity), OL::SLIDER_STEPS);
    // The readout is no longer the greyed placeholder.
    EXPECT_EQ(optionsSettingText(s, OL::Setting::MouseSensitivity), "100 %");
}

TEST(OptionsMouse, TheMousePagePersistsUnderEqclientKeys) {
    OptionsPanelState s;
    setOptionsValue(s, OL::Setting::MouseInvertY, 1);
    setOptionsValue(s, OL::Setting::MouseSensitivity, 11);
    const std::string text = serialized(s);
    EXPECT_NE(text.find("InverseMouse=1"), std::string::npos) << text;
    EXPECT_NE(text.find("MouseSensitivity=11"), std::string::npos) << text;
    EXPECT_NE(text.find("MouseActive=1"), std::string::npos) << text;
    const OptionsPanelState back = roundTrip(s);
    EXPECT_EQ(value(back, OL::Setting::MouseInvertY), 1);
    EXPECT_EQ(value(back, OL::Setting::MouseSensitivity), 11);
    // Out of range is refused, the default kept.
    const UiPreferences bad = ParseUiPreferences("[Options]\nMouseSensitivity=21\nInverseMouse=2\n");
    EXPECT_EQ(bad.optionValues[static_cast<int>(OL::Setting::MouseSensitivity)], 6);
    EXPECT_EQ(bad.optionValues[static_cast<int>(OL::Setting::MouseInvertY)], 0);
}

TEST(OptionsMouse, TheCameraDefaultsAreTheTableDefaults) {
    const MouseOptions m;
    const OptionsPanelState s;
    EXPECT_EQ(m.invertY, value(s, OL::Setting::MouseInvertY) != 0);
    EXPECT_EQ(m.lookSpring, value(s, OL::Setting::MouseLookSpring) != 0);
    EXPECT_EQ(m.mouseLook, value(s, OL::Setting::MouseLook) != 0);
    EXPECT_EQ(m.wheelZoom, value(s, OL::Setting::MouseWheelZoom) != 0);
    EXPECT_EQ(m.sensitivity, value(s, OL::Setting::MouseSensitivity));
}

TEST(OptionsMouse, SensitivityAndLookSpringArithmetic) {
    EXPECT_FLOAT_EQ(mouseLookScale(6), 1.0f) << "the oracle default is fable's pre-B433 feel";
    EXPECT_LT(mouseLookScale(0), mouseLookScale(6));
    EXPECT_GT(mouseLookScale(20), mouseLookScale(6));
    EXPECT_FLOAT_EQ(mouseLookScale(99), mouseLookScale(20));
    EXPECT_FLOAT_EQ(applyLookSpring(30.0f, 0.1f), 21.0f);
    EXPECT_FLOAT_EQ(applyLookSpring(-30.0f, 0.1f), -21.0f);
    EXPECT_FLOAT_EQ(applyLookSpring(5.0f, 0.1f), 0.0f) << "never overshoots level";
}

// ===========================================================================
// Keys (B433)
// ===========================================================================

namespace {
// Leave the singleton as the shipped file has it, whatever a test did.
struct ShippedBindings {
    ShippedBindings() { reset(); }
    ~ShippedBindings() { reset(); }
    static void reset() {
        auto& m = eqt::input::HotkeyManager::instance();
        m.resetToDefaults();
        m.loadFromFile(eqt::ResolveDataPath("config/hotkeys.json"));
    }
};
}  // namespace

TEST(OptionsKeys, EveryActionIsInExactlyOneCategoryAndEveryCategoryFits) {
    const int visible = OL::listVisibleRows(layout800(), controlIndex("OKP_KeyboardAssignmentList"));
    std::map<int, int> seen;
    for (int c = 0; c < OptionsKeys::kCategoryCount; ++c) {
        EXPECT_GT(std::strlen(OptionsKeys::categoryName(c)), 0u);
        const auto& actions = OptionsKeys::actionsIn(c);
        EXPECT_FALSE(actions.empty()) << OptionsKeys::categoryName(c);
        EXPECT_LE(static_cast<int>(actions.size()), visible)
            << OptionsKeys::categoryName(c) << " would need a scrollbar";
        for (const auto a : actions) seen[static_cast<int>(a)]++;
    }
    for (int a = 0; a < static_cast<int>(eqt::input::HotkeyAction::Count); ++a) {
        EXPECT_EQ(seen[a], 1) << eqt::input::HotkeyManager::actionEnumToName(
            static_cast<eqt::input::HotkeyAction>(a));
    }
}

TEST(OptionsKeys, TheCategoryComboCyclesAndTheListShowsTheBindings) {
    ShippedBindings guard;
    OptionsPanelState s;
    EXPECT_EQ(clickControl(s, "OKP_KeyboardFilterCombobox"), OL::Setting::None);
    EXPECT_EQ(s.keysCategory, 1);
    for (int i = 1; i < OptionsKeys::kCategoryCount; ++i) clickControl(s, "OKP_KeyboardFilterCombobox");
    EXPECT_EQ(s.keysCategory, 0) << "wraps";
    // Alt+O, measured at the oracle's Keys > UI page (T377), shows as such.
    EXPECT_EQ(OptionsKeys::bindingText(eqt::input::HotkeyAction::ToggleOptions, 0), "Alt+O");
    EXPECT_EQ(OptionsKeys::bindingText(eqt::input::HotkeyAction::ToggleOptions, 1), "-------");
    EXPECT_EQ(OptionsKeys::commandText(eqt::input::HotkeyAction::TargetGroupMember1),
              "Target group member 1");
}

TEST(OptionsKeys, ARowClickArmsACaptureAndTheNextKeyRebindsIt) {
    ShippedBindings guard;
    using HA = eqt::input::HotkeyAction;
    OptionsPanelState s;
    s.isOpen = true;
    s.activeTab = static_cast<int>(OL::Tab::Keys);
    for (int c = 0; c < OptionsKeys::kCategoryCount; ++c) {
        for (const auto a : OptionsKeys::actionsIn(c)) {
            if (a == HA::ToggleOptions) s.keysCategory = c;
        }
    }
    const int list = controlIndex("OKP_KeyboardAssignmentList");
    const auto& actions = OptionsKeys::actionsIn(s.keysCategory);
    int row = -1;
    for (int i = 0; i < static_cast<int>(actions.size()); ++i) {
        if (actions[i] == HA::ToggleOptions) row = i;
    }
    ASSERT_GE(row, 0);
    const auto rr = OL::listRowRect(layout800(), list, row);
    // The Keypress column starts 180px in (the oracle's <Columns>).
    EXPECT_EQ(click(s, rr.UpperLeftCorner.X + 200, (rr.UpperLeftCorner.Y + rr.LowerRightCorner.Y) / 2),
              OL::Setting::None);
    EXPECT_EQ(s.keysCaptureAction, static_cast<int>(HA::ToggleOptions));
    EXPECT_EQ(s.keysCaptureSlot, 0);

    // A bare modifier does not finish it.
    EXPECT_FALSE(OptionsKeys::finishKeyCapture(s, irr::KEY_LSHIFT, false, true, false, false));
    EXPECT_EQ(s.keysCaptureAction, static_cast<int>(HA::ToggleOptions));
    // Ctrl+K: bound, and K's old owner (ToggleSkills, bare K) is untouched
    // because the modifiers differ.
    EXPECT_TRUE(OptionsKeys::finishKeyCapture(s, irr::KEY_KEY_K, true, false, false, false));
    EXPECT_EQ(s.keysCaptureAction, -1);
    EXPECT_EQ(OptionsKeys::bindingText(HA::ToggleOptions, 0), "Ctrl+K");
    auto& mgr = eqt::input::HotkeyManager::instance();
    auto hit = mgr.getAction(irr::KEY_KEY_K, true, false, false, eqt::input::HotkeyMode::Player);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, HA::ToggleOptions);
    EXPECT_FALSE(mgr.getAction(irr::KEY_KEY_O, false, false, true, eqt::input::HotkeyMode::Player)
                     .has_value()) << "Alt+O was the slot that got replaced";
}

TEST(OptionsKeys, AKeyNeverMeansTwoThingsAndEscCancels) {
    ShippedBindings guard;
    using HA = eqt::input::HotkeyAction;
    OptionsPanelState s;
    s.keysCaptureAction = static_cast<int>(HA::ToggleOptions);
    s.keysCaptureSlot = 1;
    // Bare I is ToggleInventory's; taking it moves it.
    EXPECT_TRUE(OptionsKeys::finishKeyCapture(s, irr::KEY_KEY_I, false, false, false, false));
    EXPECT_EQ(OptionsKeys::bindingText(HA::ToggleOptions, 1), "I");
    auto hit = eqt::input::HotkeyManager::instance().getAction(
        irr::KEY_KEY_I, false, false, false, eqt::input::HotkeyMode::Player);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, HA::ToggleOptions);

    s.keysCaptureAction = static_cast<int>(HA::ToggleSkills);
    EXPECT_FALSE(OptionsKeys::finishKeyCapture(s, irr::KEY_ESCAPE, false, false, false, false));
    EXPECT_EQ(s.keysCaptureAction, -1);
    EXPECT_EQ(OptionsKeys::bindingText(HA::ToggleSkills, 0), "K") << "ESC bound nothing";
}

TEST(OptionsKeys, ReloadDefaultsIsAButtonWiredToItsAction) {
    OptionsPanelState s;
    EXPECT_EQ(clickControl(s, "OKP_DefaultKeysButton"), OL::Setting::KeysReloadDefaults);
}

TEST(OptionsKeys, TheSavedCopyRoundTripsThroughTheLoader) {
    ShippedBindings guard;
    using HA = eqt::input::HotkeyAction;
    auto& mgr = eqt::input::HotkeyManager::instance();
    ASSERT_TRUE(mgr.setBinding(HA::Hail, 0, irr::KEY_KEY_J, eqt::input::ModifierFlags::Alt));
    // B1101 (#421): per-process mkdtemp dir (B1098's recipe); the fixed name
    // under gtest's TempDir() (/tmp) collided across uids on shared boxes.
    willeq_test::ScopedTempDir dir{"options_wiring"};
    const std::string path = (dir / "b433_hotkeys.json").string();
    ASSERT_TRUE(mgr.saveToFile(path));
    mgr.resetToDefaults();
    ASSERT_TRUE(mgr.loadFromFile(path));
    EXPECT_EQ(OptionsKeys::bindingText(HA::Hail, 0), "Alt+J");
    // The dir (and the file in it) goes with the scope above.
}
