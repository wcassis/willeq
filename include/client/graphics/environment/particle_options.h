/*
 * The Options window's particle knobs -- Display > Particles, the Spell /
 * Env / Player sub-pages (B995, #315; Will 2026-09-23 on #145: "Env and
 * Player particle tabs (Display) IN, wire them").
 *
 * THE KEYS AND DEFAULTS ARE THE ORACLE'S: /mnt/projects/EverQuestP1999/
 * eqclient.ini carries, for each of Spell / Environment / Actor,
 * `<Family>ParticleOpacity=1.000000`, `<Family>ParticleDensity=1.000000`,
 * `<Family>ParticleNearClipPlane=2.000000` (and `<Family>ParticleCastFilter=24`,
 * whose meaning is not in the files and which stays unwired). The CONTROLS
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
 * ASSUMPTIONS AWAITING ORACLE (flagged; none is in the files):
 *   N1. the ini floats are real quantities, not choice indices: opacity
 *       0..1 (the slider's 21 steps, readout "x %" like the T434 sliders);
 *       density Off 0 / Low 0.25 / Medium 0.5 / High 1.0; near clip in world
 *       units Near 1 / Medium 2 / Far 4 / Farthest 8 -- so the shipped 2.0
 *       is Medium and the shipped density 1.0 is High.
 *   N2. "Near clip" culls particles CLOSER than the distance (the plane's
 *       name), not farther.
 *   N3. Actor ("Player") particles -- particles emitted by actor models --
 *       do not exist in fable, so that page's knobs have nothing to act on
 *       and stay unwired (options_classification.cpp says so per row).
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

inline float opacityForStep(int step) {
    if (step < 0) step = 0;
    if (step > kOpacitySteps) step = kOpacitySteps;
    return static_cast<float>(step) / kOpacitySteps;
}

/** Density choice -> the oracle's float (N1). */
inline float densityForChoice(int choice);
/** Near-clip choice -> world units (N1). */
inline float nearClipForChoice(int choice);

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

/** The settings table's float persistence: density / near-clip per choice (N1). */
inline constexpr float kDensityIniValues[kDensityChoices] = {0.0f, 0.25f, 0.5f, 1.0f};
inline constexpr float kNearClipIniValues[kNearClipChoices] = {1.0f, 2.0f, 4.0f, 8.0f};

inline float densityForChoice(int choice) {
    return (choice >= 0 && choice < kDensityChoices) ? kDensityIniValues[choice] : 1.0f;
}

inline float nearClipForChoice(int choice) {
    return (choice >= 0 && choice < kNearClipChoices) ? kNearClipIniValues[choice] : 2.0f;
}

}  // namespace Environment
}  // namespace Graphics
}  // namespace EQT
