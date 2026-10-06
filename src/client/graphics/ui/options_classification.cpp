/*
 * The Options window's per-control classification (B430/B431/B433) -- see
 * options_classification.h. The era rulings are #144's table (Will
 * 2026-09-23, final): post-Velious rows are IGNORED, the `?` rows are settled
 * by Will's ruling comment on #144/#145/#146.
 *
 * Reasons name the issue that owns the missing capability. #144 (General),
 * #145 (Display) and #146 (Mouse/Keys/Filters/Colors/Mail) stay open for
 * every row this file does not mark Wired.
 */

#include "client/graphics/ui/options_classification.h"

#include <cstring>

namespace EQT {
namespace Graphics {
namespace OptionsLayout {

namespace {

constexpr Disposition W = Disposition::Wired;
constexpr Disposition PV = Disposition::PostVelious;
constexpr Disposition WO = Disposition::WillOut;
constexpr Disposition NC = Disposition::NoCapability;
constexpr Disposition AO = Disposition::AwaitingOracle;

const char* const kCaption = "caption/readout of a wired control";
const char* const kPostVeliousEra = "post-Velious feature (#144 era table)";
const char* const kWillOut = "Will 2026-09-23 ruling on #144: OUT";
const char* const kNoFade =
    "fable's static panels have no fade or transparency model (#145)";
const char* const kNoParticleKnob =
    "no runtime particle knob of this kind in fable (B995 #315; #145)";
const char* const kNoMail = "no in-game mail in the era or on this server (#146 era table: all OUT)";

// clang-format off
const ControlClass kClasses[] = {
// ---------------------------------------------------------------- General
{"OGP_FastItemDestroyCheckbox",      NC, "fable destroys with no confirmation to skip (InventoryManager pushes DeleteItemIntent directly); #144"},
{"OGP_GuildInvitesCheckbox",         AO, "whether an incoming invite is declined or ignored when off is not in the oracle files; #144 Leg O"},
{"OGP_LootAutosplitCheckbox",        W,  "OP_SpawnAppearance 28 / PlayerProfile.autosplit"},
{"OGP_AnonymousCheckbox",            W,  "OP_SpawnAppearance 21 = 1 (/anon)"},
{"OGP_RoleplayingCheckbox",          W,  "OP_SpawnAppearance 21 = 2 (/roleplay)"},
{"OGP_AANoConfirmCheckbox",          PV, "alternate advancement is Luclin; #144 era table"},
{"OGP_AdvMerchantNoConfirmCheckbox", PV, "adventure merchants are LDoN; #144 era table"},
{"OGP_LeadershipNoConfirmCheckbox",  PV, "leadership AA is OoW; #144 era table"},
{"OGP_PetWindowPopupCheckbox",       W,  "opens the pet window on PetCreated"},
{"OGP_AllowContextMenusCheckbox",    WO, kWillOut},
{"OGP_SystemInfoButton",             WO, kWillOut},
{"OGP_LeftClickHelpText",            AO, "what 'Target Click Shows Help' shows is not in the oracle files (eqclient.ini LeftClickTargetHelp=0 only); #144 Leg O"},
{"OGP_UseTellWindowsCheckbox",       PV, kPostVeliousEra},
{"OGP_AllowAutoDuckCheckbox",        AO, "when the official client auto-ducks is not in the oracle files; #144 Leg O"},
{"OGP_AllowAutoStandCheckbox",       AO, "which actions auto-stand is not in the oracle files; #144 Leg O"},
{"OGP_AutojoinCheckbox",             WO, kWillOut},
{"OGP_AutoConsentGroupCheckbox",     W,  "OP_SpawnAppearance 40"},
{"OGP_AutoConsentRaidCheckbox",      W,  "OP_SpawnAppearance 41"},
{"OGP_AutoConsentGuildCheckbox",     W,  "OP_SpawnAppearance 42"},
{"OGP_SoundRealismLabel",            NC, "fable's audio has no realism/attenuation model to scale (eqclient.ini Realism=1); #144"},
{"OGP_SoundRealismSlider",           NC, "fable's audio has no realism/attenuation model to scale (eqclient.ini Realism=1); #144"},
{"OGP_SoundRealismValueLabel",       NC, "fable's audio has no realism/attenuation model to scale (eqclient.ini Realism=1); #144"},
{"OGP_MusicVolumeLabel",             W,  kCaption},
{"OGP_MusicVolumeSlider",            W,  "T434: MusicVolumeChangeIntent"},
{"OGP_MusicVolumeValueLabel",        W,  kCaption},
{"OGP_SoundVolumeLabel",             W,  kCaption},
{"OGP_SoundVolumeSlider",            W,  "T434: EffectsVolumeChangeIntent"},
{"OGP_SoundVolumeValueLabel",        W,  kCaption},
{"OGP_EnvSoundsCheckbox",            NC, "no switch for zone emitter sounds that survives a zone load (ZoneAudioManager has only pause/resume); #144"},
{"OGP_CombatMusicCheckbox",          NC, "CombatMusicManager is never constructed, so there is no combat music to switch; #144"},
{"OGP_ItemDroppingLabel",            NC, "fable has no drop-to-ground action to confirm; #144"},
{"OGP_ItemDroppingCombobox",         NC, "fable has no drop-to-ground action to confirm; #144"},
{"OGP_PlayerTradeLabel",             AO, "whether a refused trade is declined or ignored is not in the oracle files (eqclient.ini Trade=2); #144 Leg O"},
{"OGP_PlayerTradeCombobox",          AO, "whether a refused trade is declined or ignored is not in the oracle files (eqclient.ini Trade=2); #144 Leg O"},
{"OGP_FontLabel",                    NC, "fable has one bitmap font (BitmapGlyphTable); #144"},
{"OGP_CurrentFont",                  NC, "fable has one bitmap font (BitmapGlyphTable); #144"},
{"OGP_FontSelector",                 NC, "fable has one bitmap font (BitmapGlyphTable); #144"},
{"OGP_ChangeFont",                   NC, "fable has one bitmap font (BitmapGlyphTable); #144"},
{"OGP_LoadSkinButton",               PV, "UI skins; #144 era table"},

// ---------------------------------------------------------------- Display
{"ODP_PCNamesCheckbox",              W,  "EntityRenderer::setNameTagKindsVisible (PC half)"},
{"ODP_NPCNamesCheckbox",             W,  "EntityRenderer::setNameTagKindsVisible (NPC half)"},
{"ODP_ShowTargetRingCheckbox",       NC, "fable draws no 3D target ring (Will 2026-09-23: IN; the ring is a new world-space draw, not a toggle -- B994 #314); #145"},
{"ODP_LevelOfDetailCheckbox",        PV, "Luclin models; #144 era table"},
{"ODP_ShowHelmCheckbox",             PV, "Luclin models; #144 era table"},
{"ODP_SetWindowedButton",            NC, "fable is EGL-surfaceless or DRM: no windowed/fullscreen switch; #145"},
{"ODP_VideoModesButton",             NC, "fable is EGL-surfaceless or DRM: no video-mode switch at runtime; #145"},
{"ODP_FadeDelayLabel",               NC, kNoFade},
{"ODP_FadeDelaySlider",              NC, kNoFade},
{"ODP_FadeDelayValueLabel",          NC, kNoFade},
{"ODP_FadeDurationLabel",            NC, kNoFade},
{"ODP_FadeDurationSlider",           NC, kNoFade},
{"ODP_FadeDurationValueLabel",       NC, kNoFade},
{"ODP_WindowAlphaLabel",             NC, kNoFade},
{"ODP_WindowAlphaSlider",            NC, kNoFade},
{"ODP_WindowAlphaValueLabel",        NC, kNoFade},
{"ODP_FadeToAlphaLabel",             NC, kNoFade},
{"ODP_FadeToAlphaSlider",            NC, kNoFade},
{"ODP_FadeToAlphaValueLabel",        NC, kNoFade},
{"ODP_MaxFPSLabel", NC, "fable has no frame-rate cap to set, and the slider's range/readout units are not in the oracle files (MaxFPS=100, placeholder \"10\"); #145"},
{"ODP_MaxFPSSlider", NC, "fable has no frame-rate cap to set, and the slider's range/readout units are not in the oracle files (MaxFPS=100, placeholder \"10\"); #145"},
{"ODP_MaxFPSSliderValueLabel", NC, "fable has no frame-rate cap to set, and the slider's range/readout units are not in the oracle files (MaxFPS=100, placeholder \"10\"); #145"},
{"ODP_LODBiasLabel",                 WO, kWillOut},
{"ODP_LODBiasSlider",                WO, kWillOut},
{"ODP_LODBiasSliderValueLabel",      WO, kWillOut},
{"ODP_LoadScreenLabel",              AO, "what each LoadScreenMode value shows is not in the oracle files; #145 Leg O"},
{"ODP_LoadScreenCombobox",           AO, "what each LoadScreenMode value shows is not in the oracle files; #145 Leg O"},
{"ODP_SkyLabel",                     WO, kWillOut},
{"ODP_SkyCombobox",                  WO, kWillOut},
{"ODP_GammaLabel",                   NC, "no gamma ramp on the GLES2 surfaceless/DRM path; #145"},
{"ODP_GammaSlider",                  NC, "no gamma ramp on the GLES2 surfaceless/DRM path; #145"},
{"ODP_GammaValueLabel",              NC, "no gamma ramp on the GLES2 surfaceless/DRM path; #145"},
{"ODP_ClipPlaneLabel",               W,  kCaption},
{"ODP_ClipPlaneSlider",              W,  "T434: setRenderDistance()"},
{"ODP_ClipPlaneValueLabel",          W,  kCaption},
{"ODP_ParticlesLabel", AO, "every control behind the Spell/Env/Player sub-tabs is unwired (Will 2026-09-23: Env/Player IN; the knobs are B995 #315), so there is nothing to switch to; #145"},
{"ODP_ParticlesTabs", AO, "every control behind the Spell/Env/Player sub-tabs is unwired (Will 2026-09-23: Env/Player IN; the knobs are B995 #315), so there is nothing to switch to; #145"},
{"ODP_SpellParticlesNearClipLabel",  NC, kNoParticleKnob},
{"ODP_SpellParticlesNearClipCombobox", NC, kNoParticleKnob},
{"ODP_SpellParticlesDensityLabel", AO, "the Density combobox's choices and the float each stores are not in the oracle files (SpellParticleDensity=1.000000 only); #145 Leg O"},
{"ODP_SpellParticlesDensityCombobox", AO, "the Density combobox's choices and the float each stores are not in the oracle files (SpellParticleDensity=1.000000 only); #145 Leg O"},
{"ODP_SpellParticlesFilterLabel",    AO, "the meaning of SpellParticleCastFilter=24 is not in the oracle files; #145 Leg O"},
{"ODP_SpellParticlesFilterCombobox", AO, "the meaning of SpellParticleCastFilter=24 is not in the oracle files; #145 Leg O"},
{"ODP_SpellParticlesOpacityLabel",   NC, kNoParticleKnob},
{"ODP_SpellParticlesOpacitySlider",  NC, kNoParticleKnob},
{"ODP_SpellParticlesOpacityValueLabel", NC, kNoParticleKnob},
{"ODP_AdvancedButton",               PV, "EQUI_AdvancedDisplayOptionsWnd (post-Velious render options); #144 era table"},

// ------------------------------------------------------------------ Mouse
{"OMP_InvertYAxisCheckbox",          W,  "PlayerCameraComponent mouse-look pitch sign"},
{"OMP_LookSpringCheckbox",           W,  "PlayerCameraComponent: level the view while walking (A7)"},
{"OMP_MouseLookCheckbox",            W,  "PlayerCameraComponent: button-drag mouse-look on/off (A5)"},
{"OMP_MouseWheelZoomCheckbox",       W,  "FrameComponent wheel -> zoomCamera gate (T461)"},
{"OMP_MouseHandedness",              NC, "fable gives both buttons the same mouse-look role and the left button every UI/target click; there is no button-role map to swap; #146"},
{"OMP_MouseSensitivityLabel",        W,  kCaption},
{"OMP_MouseSensitivitySlider",       W,  "PlayerCameraComponent mouse-look scale (A6)"},
{"OMP_MouseSensitivityValueLabel",   W,  kCaption},

// ------------------------------------------------------------------- Keys
{"OKP_KeyboardFilterLabel",          W,  kCaption},
{"OKP_KeyboardFilterCombobox",       W,  "selects the listed HotkeyAction category (options_keys.h)"},
{"OKP_DefaultKeysButton",            W,  "HotkeyManager::restoreShippedBindings()"},
{"OKP_KeyboardAssignmentList",       W,  "HotkeyManager bindings; a slot click captures the next key (setBinding)"},

// ---------------------------------------------------------------- Filters
{"OFP_FilterList",                   W,  "the era /filter set, OP_SetServerFilter"},
{"OFP_FilterComboBox",               W,  "the selected filter's setting"},

// ----------------------------------------------------------------- Colors
{"OCP_UserColorDefault",             NC, "chat colours are a compiled per-channel table (getChannelColor), no user table yet -- B993 #313 (UIGAP-small filed it: the table, [TextColors] persistence and EQUI_ColorPickerWnd are not small); #146"},
{"OCP_ColorChooseList",              NC, "no user colour table to list (eqclient.ini [TextColors] User_N_*) -- B993 #313; #146"},
{"OCP_UserColorChange",              NC, "no colour-picker window (the oracle opens EQUI_ColorPickerWnd) -- B993 #313; #146"},

// ------------------------------------------------------------------- Mail
{"OMP_PathToAddressBookLabel",            PV, kNoMail},
{"OMP_PathToAddressbookField",            PV, kNoMail},
{"OMP_FileDialogButton",                  PV, kNoMail},
{"OMP_MailForwardingCheckbox",            PV, kNoMail},
{"OMP_MailForwardingLabel",               PV, kNoMail},
{"OMP_NewMailNotificationCheckbox",       PV, kNoMail},
{"OMP_NewMailNotificationLabel",          PV, kNoMail},
{"OMP_NewMailAudioNotificationCheckbox",  PV, kNoMail},
{"OMP_NewMailAudioNotificationLabel",     PV, kNoMail},
{"OMP_AddressBookConfirmDeletionCheckbox", PV, kNoMail},
{"OMP_AddressBookConfirmDeletionLabel",   PV, kNoMail},
};
// clang-format on

}  // namespace

const ControlClass* controlClassifications() { return kClasses; }

int controlClassificationCount() {
    return static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));
}

const ControlClass* findControlClass(const char* xmlItem) {
    for (const auto& c : kClasses) {
        if (std::strcmp(c.xmlItem, xmlItem) == 0) return &c;
    }
    return nullptr;
}

const char* dispositionText(Disposition d) {
    switch (d) {
        case Disposition::Wired:          return "WIRED";
        case Disposition::PostVelious:    return "POST-VELIOUS";
        case Disposition::WillOut:        return "OUT (Will)";
        case Disposition::NoCapability:   return "NO CAPABILITY";
        case Disposition::AwaitingOracle: return "AWAITING ORACLE";
    }
    return "?";
}

}  // namespace OptionsLayout
}  // namespace Graphics
}  // namespace EQT
