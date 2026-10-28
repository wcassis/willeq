/*
 * B1175 (GitLab #494): fable's GLOBAL eqclient.ini -- the Options settings the
 * official client keeps in eqclient.ini `[Defaults]`, read and written where
 * the client does, not in the per-character UI_<Char>_<server>.ini.
 *
 * WHAT IS IN IT. Every OptionsLayout row with `eqclientSection` set
 * (options_settings.cpp): the Display > Particles keys fable wires --
 * SpellParticleOpacity / Density / NearClipPlane / CastFilter,
 * EnvironmentParticleOpacity / Density / NearClipPlane -- and
 * ShowSpellEffects. The client reads them all from section "Defaults"
 * (eqgame.exe.c:132608-132671 through FUN_004ae67a / FUN_004ae64c, which read
 * .\defaults.ini and then .\eqclient.ini; ShowSpellEffects at :126007 from
 * .\eqclient.ini only) and writes them to .\eqclient.ini [Defaults]
 * (FUN_004ae514 / FUN_004ae55f, eqgame.exe.c:132091-132120; the window's
 * handlers at :245163-245204, :245310, :245434-245467; the bulk writer
 * FUN_004aed0e at :132526-132553).
 *
 * WHERE. `<cache dir>/eqclient.ini` (eqt::ResolveCachePathForRead/Write, the
 * same generated-state rule as UI_<Char>_<server>.ini): ONE file per install,
 * not per character or server, as the client's `.\eqclient.ini` is one file
 * beside its executable. fable does not read the client install's own
 * eqclient.ini (eqClientPath is read-only asset data) and does not read
 * defaults.ini for these keys (the shipped defaults.ini carries none of them).
 *
 * READ. Section and key names match case-insensitively (the Windows profile
 * API the client calls does); the first `[Defaults]` and the first line of a
 * key win; every other section and key is ignored. A key that is absent or
 * does not parse keeps the table default (options_settings.cpp), and a parsed
 * float is clamped and kept as the APPLIED value exactly as B1165 does for the
 * window (OptionIniFloats).
 *
 * WRITE. Like WritePrivateProfileString: the existing file is kept line for
 * line (other sections, other keys -- e.g. ActorParticle* or the client's own
 * [TextColors] -- comments, line endings), a key that is present gets its
 * value replaced in place, a missing key is appended at the end of
 * `[Defaults]`, a missing `[Defaults]` is appended at the end of the file.
 * fable writes all of its eqclient keys whenever one of them changes, as the
 * client's bulk writer FUN_004aed0e writes every particle key.
 *
 * NO MIGRATION. Before B1175 fable kept these keys in the per-character
 * `[Options]` section; the client has no such keys there, so
 * ui_preferences.cpp now ignores them on read and drops them on the next
 * write. A value a player set in an old per-character file is NOT carried
 * over: the global file (or the default) wins.
 *
 * Device-free: no Irrlicht type, no thread.
 */

#pragma once

#include <string>

#include "client/graphics/ui/options_settings.h"

namespace EQT {
namespace Graphics {

struct OptionsPanelState;

/** The eqclient.ini cache name: `<cache dir>/eqclient.ini`. */
constexpr const char* kEqClientIniCacheName = "eqclient.ini";

/** The eqclient.ini-owned slots (isEqClientIniSetting rows only are meaningful). */
struct EqClientIni {
    OptionsLayout::OptionValues optionValues = OptionsLayout::defaultOptionValues();
    OptionsLayout::OptionIniFloats optionIniFloats{};

    bool operator==(const EqClientIni& o) const;
    bool operator!=(const EqClientIni& o) const { return !(*this == o); }
};

/** The eqclient rows of `s` (every other slot keeps its default). */
EqClientIni CollectEqClientIni(const OptionsPanelState& s);

/** Write the eqclient rows of `p` into `s` (values re-checked against the table). */
void ApplyEqClientIni(const EqClientIni& p, OptionsPanelState& s);

/** Parse eqclient.ini text over `defaults` (see READ above). */
EqClientIni ParseEqClientIni(const std::string& text, const EqClientIni& defaults = EqClientIni{});

/** `existing` with every eqclient key set to `p`'s value (see WRITE above). */
std::string UpdateEqClientIniText(const std::string& existing, const EqClientIni& p);

/** Read `<cache dir>/eqclient.ini` over `out`; false (out untouched) when there is none. */
bool LoadEqClientIni(EqClientIni& out);

/** Update `<cache dir>/eqclient.ini` (temp file + rename); false when nothing is writable. */
bool SaveEqClientIni(const EqClientIni& p);

}  // namespace Graphics
}  // namespace EQT
