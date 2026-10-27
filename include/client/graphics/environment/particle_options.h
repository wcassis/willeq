/*
 * The Options window's particle knobs -- Display > Particles, the Spell /
 * Env / Player sub-pages (B995, #315; Will 2026-09-23 on #145: "Env and
 * Player particle tabs (Display) IN, wire them").
 *
 * THE KEYS AND DEFAULTS ARE THE ORACLE'S: /mnt/projects/EverQuestP1999/
 * eqclient.ini carries, for each of Spell / Environment / Actor,
 * `<Family>ParticleOpacity=1.000000`, `<Family>ParticleDensity=1.000000`,
 * `<Family>ParticleNearClipPlane=2.000000` (and `<Family>ParticleCastFilter=24`,
 * the "On For:" combobox, B1165: include/client/spell/spell_fx_options.h). The CONTROLS
 * are EQUI_OptionsWindow.xml's: an Opacity slider, a Density combobox
 * (Off / Low / Medium / High) and a Near Clip combobox (Near / Medium / Far /
 * Farthest) on each page.
 *
 * WHAT EACH KNOB DOES IN fable (per family, at the two particle systems that
 * exist -- the unified point sprites, fire/weather/spells, and the billboard
 * ambient emitters):
 *   opacity   multiplies every particle's alpha (and, for an additive
 *             particle, its RGB -- additive light IS its colour) at draw time;
 *   density   scales how many particles an emitter spawns (spawn rate, burst
 *             count, a weather volume's target count); 0 spawns none;
 *   nearClip  a particle nearer the camera than this is not drawn.
 * Each only ever REDUCES what is drawn: no knob raises a pool, a budget or a
 * preset value (the unit's rule), and the defaults below are the identity
 * except nearClip's 2-unit cull.
 *
 * PROVEN FROM THE DECOMPILE (B1165 #484, refactor/B1165/README.md; eqgame.exe =
 * review/B1141/ghidra/out/eqgame.exe.c, data words read from the binary):
 *   N1. the ini floats are real quantities, not choice indices. The Options
 *       handler writes density Off 0 / Low 0.25 / Medium 0.5 / High 1.0
 *       (0x3e800000 / 0x3f000000 / 0x3f800000) and near clip Near 2 /
 *       Medium 4 / Far 6 / Farthest 8 (0x40000000 / 0x40800000 /
 *       0x40c00000 / 0x41000000), eqgame.exe.c:245163-245204 (Spell page;
 *       Env 245207-245240). The loader clamps the ini density / opacity to
 *       [0, 1] and the near clip to [1, 8] (_DAT_00641fa0 = 8.0, default
 *       _DAT_006410b4 = 2.0), eqgame.exe.c:132608-132637; the combobox shows
 *       the first choice whose value is >= the float, else the last
 *       (0x53b93f..0x53b985, 0x53b994..0x53b9da). So the shipped 2.0 is NEAR.
 *       The client APPLIES the clamped float itself (0x495f29 pushes it into
 *       CreateEmitter), not the shown choice: options_settings.h OptionIniFloats.
 *       The opacity slider's 21 steps stay fable's (readout "x %").
 *   N2. STILL AN ASSUMPTION: "near clip" culls particles CLOSER than the
 *       distance. The three values reach EQGraphicsDX9's engine object
 *       (vtable +0x38 = 0x1005de60 stores them at +8/+0xC/+0x10; eqgame
 *       0x445329..0x445351 passes Spell/Env/Actor), but the per-particle
 *       read of those words was not found in B1165's pass (README sec. 2).
 *   N3. Actor ("Player") particles -- particles emitted by actor models --
 *       do not exist in fable, so that page's knobs have nothing to act on
 *       and stay unwired (options_classification.cpp says so per row).
 *   SpellParticleCastFilter ("On For:") is the eight-entry value table at
 *   DAT_00793b4c = {1, 2, 4, 8, 17, 18, 20, 24} decoded by FUN_00491cff:
 *   wired in include/client/spell/spell_fx_options.h (B1165).
 *
 * Device-free.
 */

#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

namespace EQT {
namespace Graphics {
namespace Environment {

/** Which Options sub-page governs a particle. */
enum class ParticleFamily : uint8_t {
    Environment = 0,  // fire, weather, ambient zone emitters
    Spell = 1,        // spell effects (cast glow, projectile, impact)
    Actor = 2,        // N3: none in fable
    Count = 3,
};

struct ParticleFamilyKnobs {
    float opacity = 1.0f;   // 0..1
    float density = 1.0f;   // 0..1
    float nearClip = 0.0f;  // world units; 0 = no cull
};

using ParticleKnobs = std::array<ParticleFamilyKnobs, static_cast<size_t>(ParticleFamily::Count)>;

/** The identity: what a renderer with no Options applied draws (the visual gate). */
inline ParticleKnobs identityParticleKnobs() { return ParticleKnobs{}; }

// --- The Options values (the settings table's integers) -> the knobs (N1) ---
constexpr int kOpacitySteps = 20;
constexpr int kDensityChoices = 4;   // Off, Low, Medium, High
constexpr int kNearClipChoices = 4;  // Near, Medium, Far, Farthest

// B1165: a step / choice maps to its float through the settings table
// (OptionsLayout::settingTableFloat); the knobs take the APPLIED float
// (OptionsLayout::appliedSettingFloat), which is the ini's own when it set one.

/** How many of `count` a family of `density` spawns (rounded, never more). */
inline int scaledCount(int count, float density) {
    if (density >= 1.0f) return count;
    if (density <= 0.0f) return 0;
    return static_cast<int>(static_cast<float>(count) * density + 0.5f);
}

/** Is a particle at `p` nearer the camera at `cam` than `nearClip` (N2)? */
inline bool nearClipped(const glm::vec3& p, const glm::vec3& cam, float nearClip) {
    if (nearClip <= 0.0f) return false;
    const glm::vec3 d = p - cam;
    return glm::dot(d, d) < nearClip * nearClip;
}

/**
 * THE draw rule both particle renderers apply (UnifiedParticleRenderer and
 * ParticleManager's billboards): false = do not draw (near-clipped);
 * otherwise `color` is scaled by the family's opacity -- alpha always, RGB
 * too for an additive particle, whose light IS its colour.
 */
inline bool applyParticleKnobs(const ParticleKnobs& knobs, ParticleFamily family, bool additive,
                               const glm::vec3& pos, const glm::vec3& cam, glm::vec4& color) {
    const auto& k = knobs[static_cast<size_t>(family) % knobs.size()];
    if (nearClipped(pos, cam, k.nearClip)) return false;
    if (k.opacity < 1.0f) {
        color.a *= k.opacity;
        if (additive) {
            color.r *= k.opacity;
            color.g *= k.opacity;
            color.b *= k.opacity;
        }
    }
    return true;
}

/** The settings table's float persistence: density / near-clip per choice
 *  (N1, PROVEN: eqgame.exe.c:245163-245204). */
inline constexpr float kDensityIniValues[kDensityChoices] = {0.0f, 0.25f, 0.5f, 1.0f};
inline constexpr float kNearClipIniValues[kNearClipChoices] = {2.0f, 4.0f, 6.0f, 8.0f};

/** The loader's clamps (eqgame.exe.c:132608-132637): density / opacity to
 *  [0, 1], near clip to [1, _DAT_00641fa0 = 8]. */
constexpr float kNearClipIniMin = 1.0f;
constexpr float kNearClipIniMax = 8.0f;
inline float clampUnitIni(float f) { return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }
inline float clampNearClipIni(float f) {
    return f < kNearClipIniMin ? kNearClipIniMin : (f > kNearClipIniMax ? kNearClipIniMax : f);
}

/** The choice the oracle's combobox shows for a (clamped) ini float: the
 *  first list value >= f, else the last (0x53b93f..0x53b985 near clip,
 *  0x53b994..0x53b9da density: `fcom; test ah,0x41; jp` = next while f > v). */
inline int choiceForIniFloat(const float* values, int count, float f) {
    for (int i = 0; i < count; ++i)
        if (!(f > values[i])) return i;
    return count - 1;
}

}  // namespace Environment
}  // namespace Graphics
}  // namespace EQT
