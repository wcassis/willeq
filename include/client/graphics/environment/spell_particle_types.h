#pragma once

#include "unified_particle.h"
#include "spell_effects_config.h"
#include "client/spell/spells_new_edd_colour.h"
#include "client/spell/spell_fx_timing.h"
#include "client/spell/spells_new_edd_reader.h"
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
    // B1131: `record` = the spell's spells.eff record (-1 = none): a landing on
    // the style is scaled for that record's held Source (applyPool).
    SpellEffectDef SpellComplete(glm::vec4 color, uint32_t spellId = 0, int record = -1);
    SpellEffectDef Impact(glm::vec4 color, uint32_t spellId = 0, int record = -1);
    SpellEffectDef Projectile(glm::vec4 color, uint32_t spellId = 0);
    glm::vec4 resistToParticleColor(uint8_t resistType);

    // B1131 (#451): the Source of one spells.eff record (anchor class, shape,
    // onset) built over a castGlow/castSpray definition -- see the TU.
    SpellEffectDef SourceForRecord(const SpellEffectDef& style,
                                   const SpellEffectsConfig::SourceRecord& rec,
                                   float anchorAboveFeet, float share);
    // The whole Source of a spell as SpellVisualFX::createCastGlow draws it:
    // the castGlow/castSpray style + the spell's overrides + its record's row.
    SpellEffectDef SourceForSpell(glm::vec4 color, uint32_t spellId, int record, bool spray,
                                  float anchorAboveFeet, float share);
    // Which point that Source follows (Body for an override's own geometry).
    SpellEffectsConfig::SourceAnchor SourceAnchorForSpell(uint32_t spellId, int record, bool spray);

    // B1132 (#452): one landing half (`complete` = the spellComplete style,
    // else the impact style) shaped for its spells.eff record's Target row
    // (SpellEffectsConfig::TargetRecord) -- see the TU. `anchorAboveFeet` is
    // the followed point's height above the landing entity's feet, world units.
    // B1144: `followsHeadBone` = the emitter follows the head pseudo-entity
    // (a head row is then placed from boneAt); false places it from
    // anchorAboveFeet like every other row (a head row's slot on DEFAULT).
    SpellEffectDef TargetForRecord(const SpellEffectDef& style,
                                   const SpellEffectsConfig::TargetRecord& rec,
                                   float anchorAboveFeet, bool complete,
                                   bool followsHeadBone = true);
    // A spell's landing half as SpellVisualFX::createLanding draws it: the
    // style + the spell's overrides + its record's row (an override that sets
    // its own spawnShape keeps that geometry).
    // B1144: `share` scales its rates and burst (one spellsnew.eff slot's part
    // of the landing).
    SpellEffectDef TargetForSpell(glm::vec4 color, uint32_t spellId, int record, bool complete,
                                  float anchorAboveFeet, float share = 1.0f,
                                  bool followsHeadBone = true);
    // B1144: true when the spell's spellOverrides give this landing half's
    // style its own spawnShape (B1120/B1122's measured clouds): that geometry
    // stays on the landing entity whatever slot draws it.
    bool TargetHasOverrideShape(uint32_t spellId);
    // Which point that landing follows.
    SpellEffectsConfig::TargetAnchor TargetAnchorForSpell(uint32_t spellId, int record);

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
    // T441 (board B398): applying a stage ALSO drops the caller's resist /
    // school colour modulation to white: field 85 RESISTTYPE is a mechanic
    // the client does not paint with. `POINT_SPRITE_FS` computes
    // `texColor * vColor`. The resist / school colour survives only where NO
    // stage resolved (the spells with no usable record), where fable has no
    // art to take a colour from. Alphas are untouched -- they are the style's
    // fade curve. A spellsnew.eff stage's OWN colour is applied after this
    // by applyStageEmitterColours() (B1146).
    void applyStageSpriteNames(SpellEffectDef& def,
                               const std::vector<std::string>& spriteNames,
                               uint32_t spellId = 0);

    // B1146 (#466): the spellsnew.edd colour words of the stage's emitters,
    // one per texture in the order applyStageSpriteNames() was given them
    // (EQ::SpellsNewEddColourTable::stageColours()), installed on every
    // emitter of `def` as EmitterConfig::regionColours[k]. The official
    // client multiplies the sprite texel by the vertex colour: RGB
    // interpolated start -> end over the particle's life, alpha its fade x
    // peak, additive (+0x68) blended SRC_ALPHA, ONE. An ACTIVE region's
    // particle takes the RGB; with `clientAlpha` also that alpha and blend.
    //
    // B1149 (#469): production passes clientAlpha = true for every
    // spellsnew.eff emitter (the per-slot emitters carry the client's own
    // counts, sizes and lifetimes), and with it EVERY region is active, white
    // ones too: a spellsnew.eff particle never draws the style's alpha ramp
    // or its ONE, ONE blend. clientAlpha = false keeps B1146's RGB-only
    // setting (white inactive), for tests.
    //
    // Every region stays inactive when `colours` is empty or its count
    // differs from the regions the stage installed (no texture/colour
    // pairing to trust).
    void applyStageEmitterColours(SpellEffectDef& def,
                                  const std::vector<EQ::SpellsNewEddColour>& colours,
                                  bool clientAlpha = true);

    // B1149 (#469): one spellsnew.eff slot drawn from its spellsnew.edd
    // emitter, over the definitions that carry the slot's (still measured)
    // geometry -- one for a Source slot (two for a head row's disc and
    // column), the spellComplete + impact halves for a landing slot:
    //   - timing (B1145): every emitter starts `stageLatency` + the slot's
    //     +0x8C delay after the effect is created, emits for the slot's window
    //     (run - delay), and each particle lives the slot's +0x7C lifetime L
    //     (the motion time-scaled by old mean life / L so a particle still
    //     covers the measured extent in its life; motion is B1151's);
    //   - population: the FILE's -- the burst (+0x80) on the first frame,
    //     then +0x84 particles per tick at +0x88 ticks/s (EmitterConfig::
    //     ticksPerSecond / tickParticles), divided over the definitions'
    //     emitters in the ratio of their measured peaks (the geometry split
    //     the row was measured with), so the slot as a whole emits exactly
    //     the file's count;
    //   - sprite: when `sprite` is given, the file's width/height ranges times
    //     +0x16C (and `attachScale` when +0x6C is set), grow-in/shrink-out,
    //     drawn as world-unit quads (EmitterConfig::eddSprite).
    // A slot that emits nothing (window < 0, or L <= 0) gets no particles.
    void applySlotEmitter(const std::vector<SpellEffectDef*>& slotDefs, const EQ::SpellFxSlotTiming& timing,
                          const EQ::SpellsNewEddSprite* sprite, float stageLatency, float attachScale = 1.0f);
}

} // namespace Environment
} // namespace Graphics
} // namespace EQT
