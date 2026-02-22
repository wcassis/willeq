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
};

// === Spell Emitter Presets ===

namespace SpellPresets {
    SpellEffectDef CastGlow(glm::vec4 color);
    SpellEffectDef SpellComplete(glm::vec4 color);
    SpellEffectDef Impact(glm::vec4 color);
    SpellEffectDef BuffAura(glm::vec4 color);
    glm::vec4 resistToParticleColor(uint8_t resistType);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
