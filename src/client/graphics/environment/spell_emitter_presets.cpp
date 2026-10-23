#include "client/graphics/environment/spell_particle_types.h"
#include "client/graphics/environment/spell_effects_config.h"
#include "client/graphics/environment/particle_types.h"

namespace EQT {
namespace Graphics {
namespace Environment {
namespace SpellPresets {

// === Helpers ===

static SpawnShape parseSpawnShape(const std::string& s) {
    if (s == "BOX") return SpawnShape::BOX;
    if (s == "SPHERE") return SpawnShape::SPHERE;
    if (s == "RING") return SpawnShape::RING;
    if (s == "DISC") return SpawnShape::DISC;
    return SpawnShape::POINT;
}

static MotionType parseMotionType(const std::string& s) {
    if (s == "LINEAR") return MotionType::LINEAR;
    if (s == "ORBITAL") return MotionType::ORBITAL;
    if (s == "RADIAL_EXPAND") return MotionType::RADIAL_EXPAND;
    if (s == "BURST") return MotionType::BURST;
    if (s == "CAMERA_RELATIVE") return MotionType::CAMERA_RELATIVE;
    return MotionType::LINEAR;
}

static UnifiedBlendMode parseBlendMode(const std::string& s) {
    if (s == "ALPHA") return UnifiedBlendMode::ALPHA;
    return UnifiedBlendMode::ADDITIVE;
}

// Apply a StyleSettings to an EmitterConfig, using color for start/end RGBA
static void applyStyle(const SpellEffectsConfig::StyleSettings& s, EmitterConfig& c, glm::vec4 color) {
    const auto& cfg = SpellEffectsConfig::instance();
    float density = cfg.getGlobal().densityMultiplier;

    if (s.hasMotionType) c.motionType = parseMotionType(s.motionType);
    if (s.hasSpawnRate) c.spawnRate = s.spawnRate * density;
    if (s.hasBurstCount) c.burstCount = static_cast<int>(s.burstCount * density);
    if (s.hasEmitterLifetime) c.emitterLifetime = s.emitterLifetime;
    if (s.hasSpawnWindow) c.spawnWindow = s.spawnWindow;
    if (s.hasSustainRate) c.sustainRate = s.sustainRate * density;
    if (s.hasSustainFrom) c.sustainFrom = s.sustainFrom;

    if (s.hasSpawnShape) c.spawnShape = parseSpawnShape(s.spawnShape);
    if (s.hasSpawnExtents) c.spawnExtents = s.spawnExtents;

    if (s.hasVelocityBase) c.velocityBase = s.velocityBase;
    if (s.hasVelocitySpread) c.velocitySpread = s.velocitySpread;

    if (s.hasGravity) c.gravity = s.gravity;
    if (s.hasDrag) c.drag = s.drag;

    c.colorStart = glm::vec4(color.r, color.g, color.b,
        s.hasColorStartAlpha ? s.colorStartAlpha : 0.8f);
    c.colorEnd = glm::vec4(color.r, color.g, color.b,
        s.hasColorEndAlpha ? s.colorEndAlpha : 0.0f);

    if (s.hasSizeStartMin) c.sizeStartMin = s.sizeStartMin;
    if (s.hasSizeStartMax) c.sizeStartMax = s.sizeStartMax;
    if (s.hasSizeEndMin) c.sizeEndMin = s.sizeEndMin;
    if (s.hasSizeEndMax) c.sizeEndMax = s.sizeEndMax;
    if (s.hasLifetimeMin) c.lifetimeMin = s.lifetimeMin;
    if (s.hasLifetimeMax) c.lifetimeMax = s.lifetimeMax;

    if (s.hasBlendMode) c.blendMode = parseBlendMode(s.blendMode);

    if (s.hasTextureRegions && !s.textureRegions.empty()) {
        c.textureRegionCount = static_cast<uint8_t>(
            std::min(s.textureRegions.size(), size_t(4)));
        for (uint8_t i = 0; i < c.textureRegionCount; ++i) {
            c.textureRegions[i] = cfg.getTextureIndex(s.textureRegions[i]);
        }
    }

    if (s.hasOrbitalRadius) c.orbitalRadius = s.orbitalRadius;
    if (s.hasOrbitalAngularVelocity) c.orbitalAngularVelocity = s.orbitalAngularVelocity;
    if (s.hasExpandSpeed) c.expandSpeed = s.expandSpeed;
}

// B1120: a style's `triggerDelay` turns an IMMEDIATE emitter into a DELAYED
// one (the sim's SpellTrigger::DELAYED arm, effect.age >= triggerDelay).
// B1122: the Source styles read it too (castGlow/castSpray).
static void applyTrigger(const SpellEffectsConfig::StyleSettings& s, SpellEmitterDef& ed) {
    if (s.hasTriggerDelay && s.triggerDelay > 0.0f && ed.trigger == SpellTrigger::IMMEDIATE) {
        ed.trigger = SpellTrigger::DELAYED;
        ed.triggerDelay = s.triggerDelay;
    }
}

// === Preset Functions ===

SpellEffectDef CastSpray(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("castSpray", spellId);

    SpellEffectDef def;
    def.name = "CastSpray";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);  // the record's onset replaces it (SourceForRecord, B1131)

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef CastGlow(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("castGlow", spellId);

    SpellEffectDef def;
    def.name = "CastGlow";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.5f, 0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);  // the record's onset replaces it (SourceForRecord, B1131)

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef SpellComplete(glm::vec4 color, uint32_t spellId, int record) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("spellComplete", spellId, record);

    SpellEffectDef def;
    def.name = "SpellComplete";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef Impact(glm::vec4 color, uint32_t spellId, int record) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("impact", spellId, record);

    SpellEffectDef def;
    def.name = "Impact";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 1.0f, 0.0f);

    applyStyle(s, ed.config, color);
    applyTrigger(s, ed);

    def.emitters.push_back(ed);
    return def;
}

// B1131 (#451): one spells.eff record's Source, from the caster-height row the
// B1130 survey measured (SpellEffectsConfig::SourceRecord). `style` is the
// castGlow/castSpray definition: its sprite sizes, lifetimes, art and alphas
// stay (sizes-first, !280); this sets WHERE the sprites are born and how they
// move. `anchorAboveFeet` is the height of the point the emitter follows
// above the caster's feet, world units: the entity anchor for body / orbit /
// feet-ring, the hand or head bone for hands / head, so every row's heights
// land where the oracle drew them whatever the bone's pose. `share` is this
// definition's part of the record's population (each hand draws half).
SpellEffectDef SourceForRecord(const SpellEffectDef& style,
                               const SpellEffectsConfig::SourceRecord& rec,
                               float anchorAboveFeet, float share) {
    using A = SpellEffectsConfig::SourceAnchor;
    const auto& sc = SpellEffectsConfig::instance().sourceScale();
    const float H = sc.casterHeight;
    SpellEffectDef def = style;
    if (def.emitters.empty()) return def;
    SpellEmitterDef base = def.emitters[0];
    base.trigger = rec.onset > 0.0f ? SpellTrigger::DELAYED : SpellTrigger::IMMEDIATE;
    base.triggerDelay = rec.onset;
    base.config.emitterLifetime = 0.0f;  // emits until releaseCastGlow's hold ends it
    base.config.burstCount = 0;
    if (rec.spawnRate > 0.0f)
        base.config.spawnRate = rec.spawnRate * share *
                                SpellEffectsConfig::instance().getGlobal().densityMultiplier;
    else
        base.config.spawnRate *= share;
    def.emitters.assign(1, base);
    if (!rec.hasShape) return def;  // the default row: the style's own body cloud

    auto& ed = def.emitters[0];
    auto& c = ed.config;
    const float life = 0.5f * (c.lifetimeMin + c.lifetimeMax);
    const float w = rec.halfWidth * H;
    const float span = (rec.hi - rec.lo) * H;
    // Height h (caster heights above the feet) relative to the followed point.
    auto at = [&](float h) { return h * H - anchorAboveFeet; };
    c.gravity = glm::vec3(0.0f);
    c.drag = 0.0f;
    switch (rec.anchor) {
        case A::Body: {
            // B1122's cloud over the measured box; its rise is the style's.
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::BOX;
            c.spawnExtents = glm::vec3(w, 0.5f * span, w);
            ed.positionOffset = glm::vec3(0.0f, at(0.5f * (rec.lo + rec.hi)) - 0.5f * c.velocityBase.y * life, 0.0f);
            break;
        }
        case A::Orbit: {
            c.motionType = MotionType::ORBITAL;
            c.spawnShape = SpawnShape::BOX;
            c.orbitalRadius = w;
            c.orbitalAngularVelocity = sc.orbitAngularVelocity;
            c.velocitySpread = glm::vec3(0.0f);
            if (rec.spiral) {  // born at the bottom, rising to the top over one life
                c.spawnExtents = glm::vec3(0.0f);
                c.velocityBase = glm::vec3(0.0f, span / life, 0.0f);
                ed.positionOffset = glm::vec3(0.0f, at(rec.lo), 0.0f);
            } else {           // a band over lo..hi
                c.spawnExtents = glm::vec3(0.0f, 0.5f * span, 0.0f);
                c.velocityBase = glm::vec3(0.0f);
                ed.positionOffset = glm::vec3(0.0f, at(0.5f * (rec.lo + rec.hi)), 0.0f);
            }
            break;
        }
        case A::FeetRing: {
            // A ring round the feet rising into a curtain up to hi over one life.
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::RING;
            c.spawnExtents = glm::vec3(w, 0.0f, 0.0f);
            c.velocityBase = glm::vec3(0.0f, span / life, 0.0f);
            c.velocitySpread = glm::vec3(0.0f);
            ed.positionOffset = glm::vec3(0.0f, at(rec.lo), 0.0f);
            break;
        }
        case A::Hands: {
            // Born AT the hand; the spray's mean sits at the measured centroid
            // and its 5th..95th percentile spans the measured box. A sprite's
            // offset is (velocity spread U(-s,s)) x (age ~U(0,life)), and the
            // 95th percentile of a product of two U(0,1) is 0.70, so s is the
            // half-span over 0.70 x life.
            constexpr float kP95OfUniformProduct = 0.70f;
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::POINT;
            c.spawnExtents = glm::vec3(0.0f);
            c.velocityBase = glm::vec3(0.0f, 2.0f * at(rec.centroid) / life, 0.0f);
            const float k = 1.0f / (kP95OfUniformProduct * life);
            c.velocitySpread = glm::vec3(w * k, 0.5f * span * k, w * k);
            ed.positionOffset = glm::vec3(0.0f);
            break;
        }
        case A::Head: {
            // The halo disc at the head bone, and the column that falls from it.
            const float discMid = 0.5f * (rec.discLo + rec.discHi);
            SpellEmitterDef column = ed;
            c.motionType = MotionType::LINEAR;
            c.spawnShape = SpawnShape::DISC;
            c.spawnExtents = glm::vec3(rec.discHalfWidth * H, 0.5f * (rec.discHi - rec.discLo) * H, 0.0f);
            c.velocityBase = glm::vec3(0.0f);
            c.velocitySpread = glm::vec3(0.0f);
            c.spawnRate *= rec.discShare;
            ed.positionOffset = glm::vec3(0.0f, at(discMid), 0.0f);
            auto& cc = column.config;
            cc.motionType = MotionType::LINEAR;
            cc.spawnShape = SpawnShape::DISC;
            cc.spawnExtents = glm::vec3(rec.discHalfWidth * H, 0.0f, 0.0f);
            cc.velocityBase = glm::vec3(0.0f, -(rec.discLo - rec.lo) * H / life, 0.0f);
            cc.velocitySpread = glm::vec3(0.0f);
            cc.spawnRate *= (1.0f - rec.discShare);
            column.positionOffset = glm::vec3(0.0f, at(rec.discLo), 0.0f);
            def.emitters.push_back(column);
            break;
        }
    }
    return def;
}

// The record row a spell's Source is drawn with: a spellOverrides castGlow /
// castSpray that sets its own spawnShape (B1122's measured 54/656/41/253/458
// clouds) keeps that geometry on the caster's entity, and one that sets its
// own spawnRate keeps that rate; the anchor class, onset and hold are always
// the record's.
static SpellEffectsConfig::SourceRecord rowForSpell(uint32_t spellId, int record, bool spray) {
    const auto& cfg = SpellEffectsConfig::instance();
    SpellEffectsConfig::SourceRecord rec = cfg.sourceRecord(record);
    const char* style = spray ? "castSpray" : "castGlow";
    if (cfg.hasSourceShapeOverride(spellId, style)) rec.hasShape = false;
    if (cfg.hasSourceRateOverride(spellId, style)) rec.spawnRate = 0.0f;
    return rec;
}

SpellEffectsConfig::SourceAnchor SourceAnchorForSpell(uint32_t spellId, int record, bool spray) {
    const auto rec = rowForSpell(spellId, record, spray);
    return rec.hasShape ? rec.anchor : SpellEffectsConfig::SourceAnchor::Body;
}

SpellEffectDef SourceForSpell(glm::vec4 color, uint32_t spellId, int record, bool spray,
                              float anchorAboveFeet, float share) {
    const SpellEffectDef style = spray ? CastSpray(color, spellId) : CastGlow(color, spellId);
    // The pool rule's factor (1 unless the held Source cannot sit beside its landing).
    const float k = SpellEffectsConfig::instance().sourcePoolScale(spellId, record);
    return SourceForRecord(style, rowForSpell(spellId, record, spray), anchorAboveFeet, share * k);
}

void applyStageSpriteNames(SpellEffectDef& def,
                           const std::vector<std::string>& spriteNames,
                           uint32_t spellId) {
    (void)spellId;
    const auto& cfg = SpellEffectsConfig::instance();

    uint8_t regions[4] = {0, 0, 0, 0};
    uint8_t count = 0;
    for (const auto& name : spriteNames) {
        if (name.empty()) continue;
        if (count >= 4) break;
        regions[count++] = cfg.getTextureIndex(name);
    }
    if (count == 0) return;

    for (auto& ed : def.emitters) {
        ed.config.textureRegionCount = count;
        for (uint8_t i = 0; i < 4; ++i) {
            ed.config.textureRegions[i] = (i < count) ? regions[i] : regions[0];
        }
        // T441 (board B398). The sprite art IS the colour -- see the header.
        // Modulate with white so `texColor * vColor` in POINT_SPRITE_FS draws
        // the official client's own texels. The alphas stay: they are the
        // style's fade curve.
        ed.config.colorStart = glm::vec4(1.0f, 1.0f, 1.0f, ed.config.colorStart.a);
        ed.config.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, ed.config.colorEnd.a);
    }
}

SpellEffectDef BuffAura(glm::vec4 color, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("buffAura", spellId);

    SpellEffectDef def;
    def.name = "BuffAura";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::CASTER;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.5f, 0.0f);

    applyStyle(s, ed.config, color);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef Projectile(glm::vec4 color, uint32_t spellId) {
    SpellEffectDef def;
    def.name = "Projectile";

    // Emitter 0: Trail particles (attached to the moving projectile)
    {
        auto s = SpellEffectsConfig::instance().getStyleSettings("projectileTrail", spellId);

        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::IMMEDIATE;
        ed.attach = SpellAttach::PROJECTILE_PATH;
        ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f);

        applyStyle(s, ed.config, color);

        def.emitters.push_back(ed);
    }

    // Emitter 1: Impact burst on arrival (triggers when projectile hits)
    {
        auto s = SpellEffectsConfig::instance().getStyleSettings("projectileImpact", spellId);

        SpellEmitterDef ed;
        ed.trigger = SpellTrigger::ON_HIT;
        ed.attach = SpellAttach::TARGET;
        ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 1.0f, 0.0f);

        applyStyle(s, ed.config, color);

        def.emitters.push_back(ed);
    }

    return def;
}

SpellEffectDef SpellRain(glm::vec4 color, float radius, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("spellRain", spellId);

    SpellEffectDef def;
    def.name = "SpellRain";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::GROUND_TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 10.0f, 0.0f);

    applyStyle(s, ed.config, color);

    // Override spawn extents with the radius parameter
    ed.config.spawnExtents = glm::vec3(radius, 0.0f, radius);

    def.emitters.push_back(ed);
    return def;
}

SpellEffectDef GroundCircle(glm::vec4 color, float radius, uint32_t spellId) {
    auto s = SpellEffectsConfig::instance().getStyleSettings("groundCircle", spellId);

    SpellEffectDef def;
    def.name = "GroundCircle";

    SpellEmitterDef ed;
    ed.trigger = SpellTrigger::IMMEDIATE;
    ed.attach = SpellAttach::GROUND_TARGET;
    ed.positionOffset = s.hasPositionOffset ? s.positionOffset : glm::vec3(0.0f, 0.3f, 0.0f);

    applyStyle(s, ed.config, color);

    // Override radius-dependent fields with the radius parameter
    ed.config.spawnExtents = glm::vec3(radius, 0.0f, 0.0f);
    ed.config.orbitalRadius = radius;

    def.emitters.push_back(ed);
    return def;
}

// Map resist type to particle color — delegates to config
glm::vec4 resistToParticleColor(uint8_t resistType) {
    return SpellEffectsConfig::instance().getResistColor(resistType);
}

} // namespace SpellPresets
} // namespace Environment
} // namespace Graphics
} // namespace EQT
