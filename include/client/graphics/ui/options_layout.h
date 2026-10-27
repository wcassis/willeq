/*
 * OptionsLayout — the Options window's geometry, quoted from the oracle
 * (T434, board B115's owed window / T432's `[OptionsWindow]` gap).
 *
 * THE PLACEMENT IS THE ORACLE'S, from
 * /mnt/projects/EverQuestP1999/uifiles/default/EQUI_OptionsWindow.xml:
 *
 *   <Screen item="OptionsWindow">
 *     <RelativePosition>false</RelativePosition>
 *     <Location><X>90</X><Y>47</Y></Location>
 *     <Size><CX>400</CX><CY>452</CY></Size>
 *     <Text>Options</Text>
 *     <Style_Titlebar>true</Style_Titlebar>
 *     <Style_Closebox>true</Style_Closebox>
 *     <Style_Sizable>true</Style_Sizable>
 *     <Pieces>OPTW_OptionsSubwindows</Pieces>
 *
 *   <TabBox item="OPTW_OptionsSubwindows">   AutoStretch, fills the client
 *     <Pages>OptionsGeneralPage</Pages>      TabText General
 *     <Pages>OptionsDisplayPage</Pages>      TabText Display
 *     <Pages>OptionsMousePage</Pages>        TabText Mouse
 *     <Pages>OptionsKeyboardPage</Pages>     TabText Keys
 *     <Pages>OptionsChatPage</Pages>         TabText Filters
 *     <Pages>OptionsColorPage</Pages>        TabText Colors
 *     <Pages>OptionsMailPage</Pages>         TabText Mail
 *
 * Every <Page> carries <Location>(0,22)</Location> and
 * <Size>387x402</Size>, which is what fixes the bands: the page starts 22px
 * below the top of the tab box (the tab strip) and is 402 tall, so the
 * window's 452 splits as 28 + 22 + 402. The 28 is the titlebar+border the
 * oracle draws and fable's own title band, and it is DERIVED here rather
 * than picked — 452 - 22 - 402 = 28. Likewise the page is 387 wide inside a
 * 400 window, so 13px of border: 6 left, 7 right.
 *
 * UNSCALED, and the reason is measured. The window is `Style_Sizable true`,
 * which is the condition under which T415 mapped the tradeskill window
 * proportionally onto `centerPopup`. It does not apply here:
 * /mnt/projects/EverQuestP1999/defaults.ini records `[OptionsWindow]`
 * `Width<res>=400` / `Height<res>=452` at EVERY resolution it lists, and the
 * real player file /mnt/projects/EverQuestP1999/UI_Obian_project1999.ini
 * carries `Width1280x1024=400 Height1280x1024=452` AND
 * `Width1600x900=400 Height1600x900=452` — the shipped size never changes and
 * a real player never resized it. So the window is drawn at the oracle's own
 * 400x452 with every interior rect at the oracle's own offset, and nothing
 * can round. It is CENTRED rather than placed at the oracle's absolute
 * <Location> (90,47), because fable's top-left is the player status panel;
 * that anchor is the one deviation, flagged, exactly as T415 flagged the
 * combat-abilities anchor.
 *
 * THE ONE OTHER DEVIATION: the oracle's TabBox sizes each tab to its
 * TabText's rendered width (visible in review/T377/oracle/
 * T377_oracle_options_alt_o_1024x768.png, where "General" is wider than
 * "Keys"). A layout header is device-free and has no font metrics, so the
 * seven tabs split the window width in an exact integer partition instead.
 * Flagged, not silently absorbed.
 *
 * WHAT IS WIRED. `OptionsSetting` is `None` for every control whose setting
 * does not exist in fable today, and such a control is drawn greyed and is
 * NOT hit-testable — the T363 rule, so nothing advertises what does not
 * exist. B430/B433 wired many more: options_classification.cpp is the census.
 */

#pragma once

#include <irrlicht.h>
#include <cstdint>

#include "client/graphics/ui/options_settings.h"
#include "client/graphics/ui/ui_layout.h"

namespace EQT {
namespace Graphics {
namespace OptionsLayout {

// --- The window, from <Screen item="OptionsWindow"> ---
constexpr irr::s32 ORACLE_WINDOW_W = 400;
constexpr irr::s32 ORACLE_WINDOW_H = 452;

// 452 - 22 (the <Page> Location Y, i.e. the tab strip) - 402 (the <Page>
// Size CY). See the header comment; this is derived, not chosen.
constexpr irr::s32 TITLE_H = 28;
constexpr irr::s32 TAB_H   = 22;
constexpr irr::s32 PAGE_W  = 387;
constexpr irr::s32 PAGE_H  = 402;
constexpr irr::s32 PAGE_X  = (ORACLE_WINDOW_W - PAGE_W) / 2;  // 6

constexpr int TAB_COUNT = 7;

/** The seven <Pages> of OPTW_OptionsSubwindows, in XML order. */
enum class Tab : uint8_t {
    General = 0,  // OptionsGeneralPage
    Display,      // OptionsDisplayPage
    Mouse,        // OptionsMousePage
    Keys,         // OptionsKeyboardPage
    Filters,      // OptionsChatPage
    Colors,       // OptionsColorPage
    Mail,         // OptionsMailPage
};

/** The tab captions — each page's own <TabText>. */
inline const char* tabText(int i) {
    switch (i) {
        case 0: return "General";
        case 1: return "Display";
        case 2: return "Mouse";
        case 3: return "Keys";
        case 4: return "Filters";
        case 5: return "Colors";
        case 6: return "Mail";
        default: return "";
    }
}

/** The XML element kind, so the draw can paint a checkbox unlike a slider. */
enum class Kind : uint8_t {
    Label,      // <Label>
    Checkbox,   // <Button> with Style_Checkbox true
    Slider,     // <Slider>
    ValueLabel, // <Label> holding a slider's readout
    Combobox,   // <Combobox>
    Button,     // <Button> with Style_Checkbox false
    Listbox,    // <Listbox>
    Editbox,    // <Editbox>
    TabBox,     // a nested <TabBox> frame (ODP_ParticlesTabs)
};

// `Setting` (and the table behind it) lives in options_settings.h, which is
// device-free so the persistence and the slash commands can use it.


/**
 * One control, at its oracle offset inside its page. `x,y,w,h` are the
 * XML's own <Location>/<Size> where it states them, and the DERIVED
 * absolute rect where the XML anchors instead (the anchor arithmetic is
 * written out beside each such row, so it can be checked without redoing
 * it).
 */
struct Control {
    Tab tab;
    Kind kind;
    const char* xmlItem;
    irr::s32 x, y, w, h;
    const char* text;
    Setting setting;
    // B995: a control inside ODP_ParticlesTabs belongs to one of its three
    // <Pages> (0 Spell, 1 Environment, 2 Actor/"Player") and is drawn and hit
    // only while that sub-tab is active; -1 = on the page itself.
    int subPage = -1;
};

// clang-format off
constexpr Control kControls[] = {
// ---------------------------------------------------------------- General
// The nineteen left-column checkboxes, <Location>/<Size> verbatim.
{Tab::General, Kind::Checkbox, "OGP_FastItemDestroyCheckbox",      10,  15, 150, 20, "Fast Item Destroy",        Setting::None},
{Tab::General, Kind::Checkbox, "OGP_GuildInvitesCheckbox",         10,  35, 150, 20, "Receive Guild Invites",    Setting::None},
{Tab::General, Kind::Checkbox, "OGP_LootAutosplitCheckbox",        10,  55, 150, 20, "Autosplit",                Setting::AutoSplit},
{Tab::General, Kind::Checkbox, "OGP_AnonymousCheckbox",            10,  75, 150, 20, "Anonymous",                Setting::Anonymous},
{Tab::General, Kind::Checkbox, "OGP_RoleplayingCheckbox",          10,  95, 150, 20, "Roleplaying",              Setting::Roleplaying},
{Tab::General, Kind::Checkbox, "OGP_AANoConfirmCheckbox",          10, 115, 150, 20, "Fast AA Purchase",         Setting::None},
{Tab::General, Kind::Checkbox, "OGP_AdvMerchantNoConfirmCheckbox", 10, 135, 150, 20, "Fast Adv Purchase",        Setting::None},
{Tab::General, Kind::Checkbox, "OGP_LeadershipNoConfirmCheckbox",  10, 155, 150, 20, "Fast Leadership Purchase", Setting::None},
{Tab::General, Kind::Checkbox, "OGP_PetWindowPopupCheckbox",       10, 175, 150, 20, "Pet Window Popup",         Setting::PetWindowPopup},
{Tab::General, Kind::Checkbox, "OGP_AllowContextMenusCheckbox",    10, 195, 150, 20, "Allow Context Menus",      Setting::None},
{Tab::General, Kind::Checkbox, "OGP_SystemInfoButton",             10, 215, 150, 20, "Allow SysInfo Send",       Setting::None},
{Tab::General, Kind::Checkbox, "OGP_LeftClickHelpText",            10, 235, 150, 20, "Target Click Shows Help",  Setting::None},
{Tab::General, Kind::Checkbox, "OGP_UseTellWindowsCheckbox",       10, 255, 150, 20, "Use Tell Windows",         Setting::None},
{Tab::General, Kind::Checkbox, "OGP_AllowAutoDuckCheckbox",        10, 275, 150, 20, "Allow Auto-duck",          Setting::None},
{Tab::General, Kind::Checkbox, "OGP_AllowAutoStandCheckbox",       10, 295, 150, 20, "Allow Auto-stand",         Setting::None},
{Tab::General, Kind::Checkbox, "OGP_AutojoinCheckbox",             10, 315, 150, 20, "Join General Channels",    Setting::None},
{Tab::General, Kind::Checkbox, "OGP_AutoConsentGroupCheckbox",     10, 335, 150, 20, "Auto Consent Group",       Setting::AutoConsentGroup},
{Tab::General, Kind::Checkbox, "OGP_AutoConsentRaidCheckbox",      10, 355, 150, 20, "Auto Consent Raid",        Setting::AutoConsentRaid},
{Tab::General, Kind::Checkbox, "OGP_AutoConsentGuildCheckbox",     10, 375, 150, 20, "Auto Consent Guild",       Setting::AutoConsentGuild},
// The right column: three slider triples, two checkboxes, two combos, the
// font row and the skin button.
{Tab::General, Kind::Label,      "OGP_SoundRealismLabel",      200,  10, 150, 14, "Sound Realism",       Setting::None},
{Tab::General, Kind::Slider,     "OGP_SoundRealismSlider",     200,  30, 100, 16, "",                    Setting::None},
{Tab::General, Kind::ValueLabel, "OGP_SoundRealismValueLabel", 300,  30,  50, 16, "0 %",                 Setting::None},
{Tab::General, Kind::Label,      "OGP_MusicVolumeLabel",       200,  50, 150, 14, "Music Volume",        Setting::MusicVolume},
{Tab::General, Kind::Slider,     "OGP_MusicVolumeSlider",      200,  70, 100, 16, "",                    Setting::MusicVolume},
{Tab::General, Kind::ValueLabel, "OGP_MusicVolumeValueLabel",  300,  70,  50, 16, "",                    Setting::MusicVolume},
{Tab::General, Kind::Label,      "OGP_SoundVolumeLabel",       200,  90, 150, 14, "Sound Volume",        Setting::SoundVolume},
{Tab::General, Kind::Slider,     "OGP_SoundVolumeSlider",      200, 110, 100, 16, "",                    Setting::SoundVolume},
{Tab::General, Kind::ValueLabel, "OGP_SoundVolumeValueLabel",  300, 110,  50, 16, "",                    Setting::SoundVolume},
{Tab::General, Kind::Checkbox,   "OGP_EnvSoundsCheckbox",      200, 130, 150, 20, "Environment Sounds",  Setting::None},
{Tab::General, Kind::Checkbox,   "OGP_CombatMusicCheckbox",    200, 155, 150, 20, "Combat Music",        Setting::None},
{Tab::General, Kind::Label,      "OGP_ItemDroppingLabel",      200, 179, 150, 14, "Fast Drop:",          Setting::None},
{Tab::General, Kind::Combobox,   "OGP_ItemDroppingCombobox",   200, 199, 150, 24, "",                    Setting::None},
{Tab::General, Kind::Label,      "OGP_PlayerTradeLabel",       200, 230, 150, 14, "Allow trading with:", Setting::None},
{Tab::General, Kind::Combobox,   "OGP_PlayerTradeCombobox",    200, 250, 150, 24, "",                    Setting::None},
{Tab::General, Kind::Label,      "OGP_FontLabel",              200, 285,  90, 14, "Current Font:",       Setting::None},
{Tab::General, Kind::Label,      "OGP_CurrentFont",            290, 285,  80, 14, "Arial",               Setting::None},
{Tab::General, Kind::Combobox,   "OGP_FontSelector",           200, 305, 110, 24, "",                    Setting::None},
{Tab::General, Kind::Button,     "OGP_ChangeFont",             316, 305,  50, 24, "Accept",              Setting::None},
{Tab::General, Kind::Button,     "OGP_LoadSkinButton",         200, 345, 150, 20, "Load UI Skin",        Setting::None},

// ---------------------------------------------------------------- Display
{Tab::Display, Kind::Checkbox,   "ODP_PCNamesCheckbox",             10,  12, 150, 18, "Show PC Names",        Setting::PCNames},
{Tab::Display, Kind::Checkbox,   "ODP_NPCNamesCheckbox",            10,  30, 150, 18, "Show NPC Names",       Setting::NPCNames},
{Tab::Display, Kind::Checkbox,   "ODP_ShowTargetRingCheckbox",      10,  48, 150, 18, "Show 3D Target Ring",  Setting::TargetRing},
{Tab::Display, Kind::Checkbox,   "ODP_LevelOfDetailCheckbox",       10,  66, 150, 18, "Level of Detail",      Setting::None},
{Tab::Display, Kind::Checkbox,   "ODP_ShowHelmCheckbox",            10,  84, 150, 18, "Show My Helm",         Setting::None},
// ODP_SetFullscreenButton and ODP_SetWindowedButton share the rect (10,106)
// in the XML; the official client shows only the applicable one. fable is
// EGL-surfaceless or DRM and has neither mode switch, so the windowed one
// is drawn (disabled) and the fullscreen one is not drawn twice on top.
{Tab::Display, Kind::Button,     "ODP_SetWindowedButton",           10, 106, 150, 18, "Switch to Windowed",   Setting::None},
{Tab::Display, Kind::Button,     "ODP_VideoModesButton",            10, 124, 150, 20, "Video Modes",          Setting::None},
{Tab::Display, Kind::Label,      "ODP_FadeDelayLabel",              10, 145, 150, 14, "Fade Delay (seconds)", Setting::None},
{Tab::Display, Kind::Slider,     "ODP_FadeDelaySlider",             10, 165, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_FadeDelayValueLabel",        110, 165,  50, 16, "0.0",                  Setting::None},
{Tab::Display, Kind::Label,      "ODP_FadeDurationLabel",           10, 185, 150, 14, "Fade Duration (seconds)", Setting::None},
{Tab::Display, Kind::Slider,     "ODP_FadeDurationSlider",          10, 205, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_FadeDurationValueLabel",     110, 205,  50, 16, "0.5",                  Setting::None},
{Tab::Display, Kind::Label,      "ODP_WindowAlphaLabel",            10, 225, 150, 14, "Window Transparency",  Setting::None},
{Tab::Display, Kind::Slider,     "ODP_WindowAlphaSlider",           10, 245, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_WindowAlphaValueLabel",      110, 245,  50, 16, "0 %",                  Setting::None},
{Tab::Display, Kind::Label,      "ODP_FadeToAlphaLabel",            10, 265, 150, 14, "Fade-to Transparency", Setting::None},
{Tab::Display, Kind::Slider,     "ODP_FadeToAlphaSlider",           10, 285, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_FadeToAlphaValueLabel",      110, 285,  50, 16, "0 %",                  Setting::None},
{Tab::Display, Kind::Label,      "ODP_MaxFPSLabel",                 10, 305, 150, 14, "Max. Frames Per Second", Setting::None},
{Tab::Display, Kind::Slider,     "ODP_MaxFPSSlider",                10, 325, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_MaxFPSSliderValueLabel",     110, 325,  50, 16, "10",                   Setting::None},
{Tab::Display, Kind::Label,      "ODP_LODBiasLabel",                10, 345, 150, 14, "LOD Bias",             Setting::None},
{Tab::Display, Kind::Slider,     "ODP_LODBiasSlider",               10, 365, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_LODBiasSliderValueLabel",    110, 365,  70, 16, "10",                   Setting::None},
{Tab::Display, Kind::Label,      "ODP_LoadScreenLabel",            200,  13, 150, 14, "Load Screen",          Setting::None},
{Tab::Display, Kind::Combobox,   "ODP_LoadScreenCombobox",         272,   8, 105, 24, "",                     Setting::None},
{Tab::Display, Kind::Label,      "ODP_SkyLabel",                   200,  40, 150, 14, "Sky Type",             Setting::None},
{Tab::Display, Kind::Combobox,   "ODP_SkyCombobox",                272,  35, 105, 24, "",                     Setting::None},
{Tab::Display, Kind::Label,      "ODP_GammaLabel",                 200,  69, 150, 14, "Gamma",                Setting::None},
{Tab::Display, Kind::Slider,     "ODP_GammaSlider",                200,  89, 100, 16, "",                     Setting::None},
{Tab::Display, Kind::ValueLabel, "ODP_GammaValueLabel",            300,  89,  50, 16, "0 %",                  Setting::None},
// THE ONE WIRED CONTROL ON THIS PAGE. The oracle's own slash-command alias
// for it is `/clipplane` (renderer_slash_commands.cpp), which is why the
// mapping in options_panel.h lands on setRenderDistance().
{Tab::Display, Kind::Label,      "ODP_ClipPlaneLabel",             200, 109, 150, 14, "Far Clip Plane",       Setting::ClipPlane},
{Tab::Display, Kind::Slider,     "ODP_ClipPlaneSlider",            200, 129, 100, 16, "",                     Setting::ClipPlane},
{Tab::Display, Kind::ValueLabel, "ODP_ClipPlaneValueLabel",        300, 129,  50, 16, "",                     Setting::ClipPlane},
{Tab::Display, Kind::Label,      "ODP_ParticlesLabel",             200, 160, 150, 14, "Particles",            Setting::ParticlesTab},
{Tab::Display, Kind::TabBox,     "ODP_ParticlesTabs",              190, 174, 190, 170, "Spell|Env|Player",    Setting::ParticlesTab},
// The nested tab box's three <Pages> (B995), each at the box origin (190,174)
// plus its own 22px sub-tab strip: subpage origin (190,196), so every child
// is its XML <Location> + (190,196). SpellParticlesPage and ActorParticlesPage
// declare the same nine children; EnvironmentParticlesPage has no filter.
{Tab::Display, Kind::Label,      "ODP_SpellParticlesNearClipLabel",    200, 206, 100, 14, "Near Clip:", Setting::SpellParticleNearClip, 0},
{Tab::Display, Kind::Combobox,   "ODP_SpellParticlesNearClipCombobox", 265, 202, 100, 24, "",           Setting::SpellParticleNearClip, 0},
{Tab::Display, Kind::Label,      "ODP_SpellParticlesDensityLabel",     200, 236, 100, 14, "Density:",   Setting::SpellParticleDensity, 0},
{Tab::Display, Kind::Combobox,   "ODP_SpellParticlesDensityCombobox",  265, 232, 100, 24, "",           Setting::SpellParticleDensity, 0},
{Tab::Display, Kind::Label,      "ODP_SpellParticlesFilterLabel",      200, 266, 100, 14, "On For:",    Setting::SpellParticleCastFilter, 0},
{Tab::Display, Kind::Combobox,   "ODP_SpellParticlesFilterCombobox",   265, 262, 100, 24, "",           Setting::SpellParticleCastFilter, 0},
{Tab::Display, Kind::Label,      "ODP_SpellParticlesOpacityLabel",     200, 296, 150, 14, "Opacity",    Setting::SpellParticleOpacity, 0},
{Tab::Display, Kind::Slider,     "ODP_SpellParticlesOpacitySlider",    200, 316, 100, 16, "",           Setting::SpellParticleOpacity, 0},
{Tab::Display, Kind::ValueLabel, "ODP_SpellParticlesOpacityValueLabel",300, 316,  50, 16, "0 %",        Setting::SpellParticleOpacity, 0},
{Tab::Display, Kind::Label,      "ODP_EnvironmentParticlesNearClipLabel",    200, 206, 100, 14, "Near Clip:", Setting::EnvParticleNearClip, 1},
{Tab::Display, Kind::Combobox,   "ODP_EnvironmentParticlesNearClipCombobox", 265, 202, 100, 24, "",           Setting::EnvParticleNearClip, 1},
{Tab::Display, Kind::Label,      "ODP_EnvironmentParticlesDensityLabel",     200, 236, 100, 14, "Density:",   Setting::EnvParticleDensity, 1},
{Tab::Display, Kind::Combobox,   "ODP_EnvironmentParticlesDensityCombobox",  265, 232, 100, 24, "",           Setting::EnvParticleDensity, 1},
{Tab::Display, Kind::Label,      "ODP_EnvironmentParticlesOpacityLabel",     200, 296, 150, 14, "Opacity",    Setting::EnvParticleOpacity, 1},
{Tab::Display, Kind::Slider,     "ODP_EnvironmentParticlesOpacitySlider",    200, 316, 100, 16, "",           Setting::EnvParticleOpacity, 1},
{Tab::Display, Kind::ValueLabel, "ODP_EnvironmentParticlesOpacityValueLabel",300, 316,  50, 16, "0 %",        Setting::EnvParticleOpacity, 1},
{Tab::Display, Kind::Label,      "ODP_ActorParticlesNearClipLabel",    200, 206, 100, 14, "Near Clip:", Setting::None, 2},
{Tab::Display, Kind::Combobox,   "ODP_ActorParticlesNearClipCombobox", 265, 202, 100, 24, "",           Setting::None, 2},
{Tab::Display, Kind::Label,      "ODP_ActorParticlesDensityLabel",     200, 236, 100, 14, "Density:",   Setting::None, 2},
{Tab::Display, Kind::Combobox,   "ODP_ActorParticlesDensityCombobox",  265, 232, 100, 24, "",           Setting::None, 2},
{Tab::Display, Kind::Label,      "ODP_ActorParticlesFilterLabel",      200, 266, 100, 14, "On For:",    Setting::None, 2},
{Tab::Display, Kind::Combobox,   "ODP_ActorParticlesFilterCombobox",   265, 262, 100, 24, "",           Setting::None, 2},
{Tab::Display, Kind::Label,      "ODP_ActorParticlesOpacityLabel",     200, 296, 150, 14, "Opacity",    Setting::None, 2},
{Tab::Display, Kind::Slider,     "ODP_ActorParticlesOpacitySlider",    200, 316, 100, 16, "",           Setting::None, 2},
{Tab::Display, Kind::ValueLabel, "ODP_ActorParticlesOpacityValueLabel",300, 316,  50, 16, "0 %",        Setting::None, 2},
{Tab::Display, Kind::Button,     "ODP_AdvancedButton",                 190, 350, 150, 20, "Advanced",   Setting::None},

// ------------------------------------------------------------------ Mouse
{Tab::Mouse, Kind::Checkbox,   "OMP_InvertYAxisCheckbox",       10,  10, 150, 20, "Invert Y Axis",         Setting::MouseInvertY},
{Tab::Mouse, Kind::Checkbox,   "OMP_LookSpringCheckbox",        10,  30, 150, 20, "Look Spring",           Setting::MouseLookSpring},
{Tab::Mouse, Kind::Checkbox,   "OMP_MouseLookCheckbox",         10,  50, 150, 20, "Mouse Look",            Setting::MouseLook},
{Tab::Mouse, Kind::Checkbox,   "OMP_MouseWheelZoomCheckbox",    10,  70, 150, 20, "Mouse Wheel Zoom",      Setting::MouseWheelZoom},
{Tab::Mouse, Kind::Checkbox,   "OMP_MouseHandedness",           10,  90, 150, 20, "Right-handed mouse",    Setting::None},
{Tab::Mouse, Kind::Label,      "OMP_MouseSensitivityLabel",    200,  10, 150, 14, "Mouselook Sensitivity", Setting::MouseSensitivity},
{Tab::Mouse, Kind::Slider,     "OMP_MouseSensitivitySlider",   200,  30, 100, 16, "",                      Setting::MouseSensitivity},
{Tab::Mouse, Kind::ValueLabel, "OMP_MouseSensitivityValueLabel",300, 30,  50, 16, "0 %",                   Setting::MouseSensitivity},

// ------------------------------------------------------------------- Keys
{Tab::Keys, Kind::Label,    "OKP_KeyboardFilterLabel",    10, 10, 120, 14, "Select category:", Setting::KeysCategory},
{Tab::Keys, Kind::Combobox, "OKP_KeyboardFilterCombobox", 20, 24, 150, 24, "",                 Setting::KeysCategory},
{Tab::Keys, Kind::Button,   "OKP_DefaultKeysButton",     200, 15, 150, 20, "Reload defaults",  Setting::KeysReloadDefaults},
// Anchored: Top=60, Left=5, Right=5 (from the page's right), Bottom=5 (from
// the page's bottom) => (5,60)-(387-5, 402-5) = (5,60) 377x337.
// Its columns are Command 180 / Keypress 75 / Alternate 75.
{Tab::Keys, Kind::Listbox,  "OKP_KeyboardAssignmentList",  5, 60, 377, 337, "Command|Keypress|Alternate", Setting::KeysAssignment},

// ---------------------------------------------------------------- Filters
// OFP_FilterList: Top=10, Left=40, Right=40 (from right), Bottom=40 (from
// bottom) => (40,10)-(347,362) = 307x352.
{Tab::Filters, Kind::Listbox,  "OFP_FilterList",     40,  10, 307, 352, "Filter description|Filter Setting", Setting::FilterSelection},
// OFP_FilterComboBox: TopAnchorToTop false, so Top=34 is measured from the
// page's BOTTOM => y1 = 402-34 = 368; Bottom=10 from the bottom => y2 = 392.
// Left=60, Right=60 (from right) => x 60..327, width 267.
{Tab::Filters, Kind::Combobox, "OFP_FilterComboBox", 60, 368, 267,  24, "",                                 Setting::FilterSelection},

// ----------------------------------------------------------------- Colors
// OCP_UserColorDefault: Top=8, Bottom=30 (both from the top), Left=126,
// Right=102 (from right) => (126,8) 159x22.
{Tab::Colors, Kind::Button,  "OCP_UserColorDefault",  126,   8, 159,  22, "Reset Default Colors",       Setting::TextColorReset},
// OCP_ColorChooseList: Top=35, Left=20, Right=20, Bottom=35 (from bottom)
// => (20,35)-(367,367) = 347x332.
{Tab::Colors, Kind::Listbox, "OCP_ColorChooseList",    20,  35, 347, 332, "Text description|Color Sample", Setting::TextColorList},
// OCP_UserColorChange: both anchors from the BOTTOM, Top=30 => y1 = 372,
// Bottom=8 => y2 = 394.
{Tab::Colors, Kind::Button,  "OCP_UserColorChange",   126, 372, 159,  22, "Change Selected Color",      Setting::TextColorChange},

// ------------------------------------------------------------------- Mail
// Every one of these is anchored to the page's top-left, so the offsets are
// direct; a Right offset with RightAnchorToLeft false is measured from the
// page's right edge (387).
{Tab::Mail, Kind::Label,    "OMP_PathToAddressBookLabel",           12,  10, 365, 24, "Address Book Filename",  Setting::None},
{Tab::Mail, Kind::Editbox,  "OMP_PathToAddressbookField",           10,  30, 342, 24, "",                       Setting::None},
{Tab::Mail, Kind::Button,   "OMP_FileDialogButton",                353,  31,  26, 24, "...",                    Setting::None},
{Tab::Mail, Kind::Checkbox, "OMP_MailForwardingCheckbox",           10,  80,  16, 16, "",                       Setting::None},
{Tab::Mail, Kind::Label,    "OMP_MailForwardingLabel",              30,  80, 347, 16, "Forward mail to the address registered in my Station Account.", Setting::None},
{Tab::Mail, Kind::Checkbox, "OMP_NewMailNotificationCheckbox",      10, 100,  16, 16, "",                       Setting::None},
{Tab::Mail, Kind::Label,    "OMP_NewMailNotificationLabel",         30, 100, 347, 16, "Notify me in the chat window when new mail arrives.", Setting::None},
{Tab::Mail, Kind::Checkbox, "OMP_NewMailAudioNotificationCheckbox", 10, 120,  16, 16, "",                       Setting::None},
{Tab::Mail, Kind::Label,    "OMP_NewMailAudioNotificationLabel",    30, 120, 347, 16, "Play a sound when new mail arrives.", Setting::None},
{Tab::Mail, Kind::Checkbox, "OMP_AddressBookConfirmDeletionCheckbox",10,140,  16, 16, "",                       Setting::None},
{Tab::Mail, Kind::Label,    "OMP_AddressBookConfirmDeletionLabel",  30, 140, 347, 16, "Ask for confirmation before deleting an address book entry.", Setting::None},
};
// clang-format on

constexpr int CONTROL_COUNT =
    static_cast<int>(sizeof(kControls) / sizeof(kControls[0]));

/**
 * The window rect: the oracle's own 400x452, CENTRED (see the flagged
 * deviation in the header comment), clamped so it is always wholly on
 * screen even at 640x480 where 452 leaves 28px of slack.
 */
inline irr::core::rect<irr::s32> windowRect(const UILayout& layout) {
    // B1075 (#395): the layout table's ScreenCentre anchor.
    return layout.placeScreenCentre(ORACLE_WINDOW_W, ORACLE_WINDOW_H);
}

/** fable's title band, holding the oracle's <Text>Options</Text>. */
inline irr::core::rect<irr::s32> titleRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    return irr::core::rect<irr::s32>(w.UpperLeftCorner.X, w.UpperLeftCorner.Y,
                                     w.LowerRightCorner.X,
                                     w.UpperLeftCorner.Y + TITLE_H);
}

/**
 * One tab's header. An exact integer partition of the window width, so the
 * seven tabs tile the strip with no gap and no overlap — see the flagged
 * deviation (the oracle sizes each to its caption).
 */
inline irr::core::rect<irr::s32> tabRect(const UILayout& layout, int i) {
    if (i < 0 || i >= TAB_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto w = windowRect(layout);
    const irr::s32 x0 = w.UpperLeftCorner.X + (ORACLE_WINDOW_W * i) / TAB_COUNT;
    const irr::s32 x1 =
        w.UpperLeftCorner.X + (ORACLE_WINDOW_W * (i + 1)) / TAB_COUNT;
    const irr::s32 y0 = w.UpperLeftCorner.Y + TITLE_H;
    return irr::core::rect<irr::s32>(x0, y0, x1, y0 + TAB_H);
}

/** The active <Page>'s rect — 387x402 at (PAGE_X, TITLE_H + TAB_H). */
inline irr::core::rect<irr::s32> pageRect(const UILayout& layout) {
    const auto w = windowRect(layout);
    const irr::s32 x = w.UpperLeftCorner.X + PAGE_X;
    const irr::s32 y = w.UpperLeftCorner.Y + TITLE_H + TAB_H;
    return irr::core::rect<irr::s32>(x, y, x + PAGE_W, y + PAGE_H);
}

/**
 * A control's rect. THE one placement body: the draw loop and both hit
 * tests go through it, so render and hit-test cannot drift (T57's rule).
 * Degenerate for an out-of-range index.
 */
inline irr::core::rect<irr::s32> controlRect(const UILayout& layout, int i) {
    if (i < 0 || i >= CONTROL_COUNT) return irr::core::rect<irr::s32>(0, 0, 0, 0);
    const auto page = pageRect(layout);
    const Control& c = kControls[i];
    const irr::s32 x = page.UpperLeftCorner.X + c.x;
    const irr::s32 y = page.UpperLeftCorner.Y + c.y;
    return irr::core::rect<irr::s32>(x, y, x + c.w, y + c.h);
}

/**
 * A slider's knob, for `step` of `steps + 1` positions (the oracle's
 * sliders are 0..20 — see options_panel.h for where that scale is
 * measured). Width KNOB_W, travelling the slider's usable span.
 */
constexpr irr::s32 KNOB_W = 8;
constexpr int SLIDER_STEPS = 20;

inline irr::core::rect<irr::s32> sliderKnobRect(const UILayout& layout, int i,
                                                int step) {
    const auto r = controlRect(layout, i);
    if (r.getWidth() <= KNOB_W) return r;
    if (step < 0) step = 0;
    if (step > SLIDER_STEPS) step = SLIDER_STEPS;
    const irr::s32 travel = r.getWidth() - KNOB_W;
    const irr::s32 x = r.UpperLeftCorner.X + (travel * step) / SLIDER_STEPS;
    return irr::core::rect<irr::s32>(x, r.UpperLeftCorner.Y, x + KNOB_W,
                                     r.LowerRightCorner.Y);
}

/** The step a click at `mouseX` selects on slider `i`. Clamped to 0..20. */
inline int sliderStepAt(const UILayout& layout, int i, int mouseX) {
    const auto r = controlRect(layout, i);
    const irr::s32 travel = r.getWidth() - KNOB_W;
    if (travel <= 0) return 0;
    // The click names the knob's CENTRE, so the usable span starts half a
    // knob in; rounding to the nearest step makes both ends reachable.
    irr::s32 rel = mouseX - r.UpperLeftCorner.X - KNOB_W / 2;
    if (rel < 0) rel = 0;
    if (rel > travel) rel = travel;
    return static_cast<int>((rel * SLIDER_STEPS + travel / 2) / travel);
}

/**
 * A <Listbox>'s rows (B433). The oracle's listboxes draw a column-header row
 * and then fixed-height item rows; the row height is fable's (the bitmap
 * font's line plus 2px), not quoted -- a layout header has no font metrics.
 * THE one row-placement body: the draw and the hit test both use it.
 */
constexpr irr::s32 LIST_HEADER_H = 16;
constexpr irr::s32 LIST_ROW_H = 14;

/** How many whole rows list control `i` shows. */
inline int listVisibleRows(const UILayout& layout, int i) {
    const auto r = controlRect(layout, i);
    const irr::s32 body = r.getHeight() - LIST_HEADER_H;
    return body > 0 ? static_cast<int>(body / LIST_ROW_H) : 0;
}

/** The rect of visible row `row` (0 = first under the header) of list `i`. */
inline irr::core::rect<irr::s32> listRowRect(const UILayout& layout, int i, int row) {
    const auto r = controlRect(layout, i);
    const irr::s32 y = r.UpperLeftCorner.Y + LIST_HEADER_H + row * LIST_ROW_H;
    return irr::core::rect<irr::s32>(r.UpperLeftCorner.X, y,
                                     r.LowerRightCorner.X, y + LIST_ROW_H);
}

/** The visible row under `mouseY` in list `i`, or -1 (header / past the end). */
inline int listRowAt(const UILayout& layout, int i, int mouseY) {
    const auto r = controlRect(layout, i);
    const irr::s32 rel = mouseY - r.UpperLeftCorner.Y - LIST_HEADER_H;
    if (rel < 0) return -1;
    const int row = static_cast<int>(rel / LIST_ROW_H);
    return row < listVisibleRows(layout, i) ? row : -1;
}

}  // namespace OptionsLayout
}  // namespace Graphics
}  // namespace EQT
