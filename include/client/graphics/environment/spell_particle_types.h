#pragma once

#include "unified_particle.h"
#include <vector>
#include <string>
#include <cstdint>

namespace EQT {
namespace Graphics {
namespace Environment {

enum class SpellTrigger : uint8_t {
    IMMEDIATE = 0,        // Fire when effect starts
    ON_CAST_COMPLETE = 1, // Fire when cast completes
    ON_HIT = 2,           // Fire when projectile arrives
    DELAYED = 3,          // Fire after triggerDelay seconds
};

enum class SpellAttach : uint8_t {
    CASTER = 0,
    TARGET = 1,
    GROUND_TARGET = 2,    // Fixed world position
    PROJECTILE_PATH = 3,  // Lerps from caster to target
};

struct SpellEmitterDef {
    EmitterConfig config;
    SpellTrigger trigger = SpellTrigger::IMMEDIATE;
    float triggerDelay = 0.0f;
    SpellAttach attach = SpellAttach::CASTER;
    glm::vec3 positionOffset{0.0f};  // Y-up offset from attach point
};

struct SpellEffectDef {
    std::string name;                        // Debug name
    std::vector<SpellEmitterDef> emitters;   // Up to 8
};

struct SpellEffectInstance {
    uint32_t effectID = 0;
    uint32_t spellID = 0;
    uint16_t casterEntityID = 0;
    uint16_t targetEntityID = 0;
    glm::vec3 groundTarget{0.0f};
    float age = 0.0f;
    float maxDuration = 0.0f;            // 0 = until emitters expire
    bool hitSignaled = false;
    bool castCompleteSignaled = false;

    struct EmitterState {
        int defIndex = 0;                // Index into SpellEffectDef.emitters
        uint16_t activeEmitterID = 0;    // 0 = not yet spawned
        bool triggered = false;
    };
    std::vector<EmitterState> emitterStates;

    // The definition (stored here since we don't have a global registry yet)
    SpellEffectDef def;

    // If true, emitters use direction callback for spray velocity
    bool useDynamicDirection = false;

    // Projectile travel duration (seconds) — set by caller for PROJECTILE_PATH emitters
    float projectileTravelDuration = 0.5f;
};

// === Spell Emitter Presets ===

namespace SpellPresets {
    SpellEffectDef CastGlow(glm::vec4 color, uint32_t spellId = 0);
    SpellEffectDef CastSpray(glm::vec4 color, uint32_t spellId = 0);
    SpellEffectDef SpellComplete(glm::vec4 color, uint32_t spellId = 0);
    SpellEffectDef Impact(glm::vec4 color, uint32_t spellId = 0);
    SpellEffectDef BuffAura(glm::vec4 color, uint32_t spellId = 0);
    SpellEffectDef Projectile(glm::vec4 color, uint32_t spellId = 0);
    SpellEffectDef SpellRain(glm::vec4 color, float radius, uint32_t spellId = 0);
    SpellEffectDef GroundCircle(glm::vec4 color, float radius, uint32_t spellId = 0);
    glm::vec4 resistToParticleColor(uint8_t resistType);

    // T389 (board B162): replace an effect definition's atlas tile choice
    // with the tiles named by a resolved spells.eff stage.
    //
    // spriteNames are the raw slots as read from the file (up to three, any
    // of them possibly empty); each non-empty one is passed through
    // SpellEffectsConfig::getTextureIndex(), which already falls back to
    // SoftCircle for a name it does not know -- so an unmapped name degrades
    // exactly as it does today and nothing is ever left unset. If every slot
    // is empty the definition is left untouched.
    //
    // Applied to every emitter in the definition, because the Target stage
    // describes one impact and the Impact preset is one emitter.
    //
    // T441 (board B398): applying a stage ALSO drops the caller's colour
    // modulation to white. The official client has no per-spell colour field
    // at all -- the colour of a spell's particles IS the art in the
    // `spel*.tga` this stage names, reached from spells_us.txt field 123
    // (SPELLAFFECTINDEX) -> the spells.eff record -> the stage's three
    // 32-byte sprite-name slots. `POINT_SPRITE_FS` computes
    // `texColor * vColor`, so any non-white modulation is fable tinting the
    // oracle's own art with something the oracle never applies. The resist /
    // school colour survives only where NO stage resolved (the ~946 spells
    // with no usable spells.eff record), which is the one case where fable
    // has no art to take a colour from. Alphas are untouched -- they are the
    // style's fade curve, not a colour.
    void applyStageSpriteNames(SpellEffectDef& def,
                               const std::vector<std::string>& spriteNames,
                               uint32_t spellId = 0);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
